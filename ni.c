/// NI
/// ====
///
/// ni is a minimalist modal text editor written in plain C without using
/// external libraries.
///

// -------------------------------- Includes ----------------------------------
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

// --------------------------------- Defines ----------------------------------
#define Ni_VERSION "0.0.1"
#define NORETURN __attribute__((noreturn)) void
// Mask 00011111 i.e. zero out the upper three bits
#define CTRL_KEY(k) ((k)&0x1f)

#define L (E.lines[E.cy])
#define N (E.numlines)

// ---------------------------------- Types -----------------------------------
typedef unsigned int uint;
typedef struct termios Term;

typedef enum EditorMode {
	MODE_NORMAL,
	MODE_INSERT,
	MODE_COUNT,
} EditorMode;

typedef enum EditorKey {
	KEY_UP = 1000, // Start the enum so that it does not conflict with raw
		       // characters
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_PAGE_UP,
	KEY_PAGE_DOWN,
	KEY_DELETE,
	KEY_RETURN,
	KEY_ESCAPE,
	KEY_NOOP,
} EditorKey;

typedef struct ScreenBuffer {
	char *data;
	size_t len;
} ScreenBuffer;

typedef struct Line {
	size_t len;
	char *chars;
} Line;

typedef struct Editor {
	Term term_orig;
	Line *lines;
	uint numlines;
	uint cx, cy;
	uint rowoff, coloff;
	uint rows, cols;
	EditorMode mode;
} Editor;

typedef struct Mode {
	char *status;
	void (*process_key)(int c);
} Mode;

// --------------------------------- State ------------------------------------
static Editor E;

// ---------------------------------- Modes -----------------------------------
static void process_key_normal(const int c);
static void process_key_insert(const int c);

static Mode modes[MODE_COUNT] = {
	[MODE_NORMAL] = {"NORMAL", process_key_normal},
	[MODE_INSERT] = {"INSERT", process_key_insert},
};

// -------------------------------- Terminal ----------------------------------
static NORETURN die(const char s[]) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	perror(s);
	exit(EXIT_FAILURE);
}

static NORETURN quit(void) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	exit(EXIT_SUCCESS);
}

static void reset_term(void) {
	if (tcsetattr(STDIN_FILENO, TCSANOW, &E.term_orig) == -1)
		die("tcsetattr");
}

static void enable_raw_mode(void) {
	// Save terminal settings and setup cleanup on exit.
	if (tcgetattr(STDIN_FILENO, &E.term_orig) == -1) die("tcgetattr");
	atexit(reset_term);

	// Set terminal to raw mode.
	Term term_raw = E.term_orig;
	term_raw.c_iflag &= ~(uint)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	term_raw.c_oflag &= ~(uint)(OPOST);
	term_raw.c_cflag |= (uint)(CS8);
	term_raw.c_lflag &= ~(uint)(ECHO | ICANON | ISIG | IEXTEN);
	term_raw.c_cc[VMIN] = 0;
	term_raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &term_raw) == -1) die("tcsetattr");
}

static int read_key(void) {
	char c;
	if (read(STDIN_FILENO, &c, 1) != 1) goto error;
	if (c != '\x1b') {
		switch (c) {

		case 13: return KEY_RETURN;

		case 8:
		case 127: return KEY_DELETE;

		default: return c;
		}
	}

	// Start reading multi-byte escape sequences
	char seq[3];

	if (read(STDIN_FILENO, &seq[0], 1) != 1) return KEY_ESCAPE;
	if (read(STDIN_FILENO, &seq[1], 1) != 1) return KEY_ESCAPE;

	// CSI: \x1b[...
	// For now we only deal with '['
	if (seq[0] != '[') goto error;

	// Check the character following the CSI
	switch (seq[1]) {

	// CSI [A-Z]
	case 'A': return KEY_UP;
	case 'B': return KEY_DOWN;
	case 'D': return KEY_LEFT;
	case 'C': return KEY_RIGHT;

	// CSI [0-9]~
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9': {
		// Sequences like where a number follows the CSI end with a
		// trailing '~', so we read one more character.
		// e.g. 'CSI 5~'
		if (read(STDIN_FILENO, &seq[2], 1) != 1 || seq[2] != '~')
			goto error;

		switch (seq[1]) {
		case '3': return KEY_DELETE;
		case '5': return KEY_PAGE_UP;
		case '6': return KEY_PAGE_DOWN;
		}
	}
	}

error:
	return KEY_NOOP;
}

static int get_cursor_position(uint *rows, uint *cols) {
	// Solicit device report
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	// Read device report
	// \x1b[<rows>;<cols>R
	char buf[32];
	uint i = 0;
	while (i < sizeof buf - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) return -1;
		if (buf[i++] == 'R') break;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[1], "[%d;%dR", rows, cols) != 2) return -1;

	return 0;
}

static int get_window_size(uint *rows, uint *cols) {
	// Reset cursor to home
	if (write(STDOUT_FILENO, "\x1b[H", 3) != 3) return -1;
	// Move to end
	if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
	if (get_cursor_position(rows, cols) == -1) return -1;

	return 0;
}

// --------------------------------- Editing ----------------------------------
static void insert_line(size_t at) {
	if (at >= E.numlines) at = E.numlines - 1;
	E.lines = realloc(E.lines, (sizeof *E.lines) * (E.numlines + 1));
	if (!E.lines) die("realloc");

	if (at < E.numlines - 1)
		memmove(&E.lines[at + 1], &E.lines[at],
			(sizeof *E.lines) * (E.numlines - at));

	E.lines[at].len = 0;
	E.lines[at].chars = strdup("");

	E.numlines++;
}

static void delete_line(size_t at) {
	if (E.numlines == 0) return;
	if (at >= E.numlines) at = E.numlines - 1;

	free(E.lines[at].chars);
	E.lines[at].chars = NULL;

	if (at < E.numlines - 1)
		memmove(&E.lines[at], &E.lines[at + 1],
			(sizeof *E.lines) * (E.numlines - (at + 1)));

	E.numlines--;
}

static void split_line(size_t at, size_t split_at) {
	if (at >= E.numlines) return;

	insert_line(at + 1);
	if (split_at >= E.lines[at].len) return;

	free(E.lines[at + 1].chars);
	size_t len = E.lines[at].len - split_at;
	E.lines[at + 1].chars = strndup(&E.lines[at].chars[split_at], len);
	E.lines[at + 1].len = len;

	// Shorten original line by len
	E.lines[at].len -= len;
	E.lines[at].chars[E.lines[at].len] = '\0';
	E.lines[at].chars = realloc(E.lines[at].chars, E.lines[at].len + 1);
}

static void join_lines(size_t at) {
	(void)at;
}

static void line_insert_char(Line *line, size_t at, char c) {
	if (at > line->len) at = line->len;
	// line->len does not include the terminating '\0'
	line->chars = realloc(line->chars, line->len + 1 + 1);
	if (!line->chars) die("realloc");
	memmove(&line->chars[at + 1], &line->chars[at], line->len - at + 1);
	line->chars[at] = c;
	line->len++;
}

static void line_delete_char(Line *line, size_t at) {
	// line->len does not include the terminating '\0'
	if (line->len == 0) return;
	if (at >= line->len) at = line->len - 1;
	memmove(&line->chars[at], &line->chars[at + 1],
		line->len - (at + 1) + 1);
	line->len--;
	line->chars = realloc(line->chars, line->len + 1);
	if (!line->chars) die("realloc");
}

// ---------------------------------- Input -----------------------------------
static void cursor_move(int c) {
	switch (c) {
	case 'k':
	case KEY_UP: E.cy > 0 && E.cy--; break;
	case 'j':
	case KEY_DOWN:
		if (E.numlines != 0 && E.cy < E.numlines - 1) E.cy++;
		break;
	case 'h':
	case KEY_LEFT: E.cx > 0 && E.cx--; break;
	case 'l':
	case KEY_RIGHT:
		if (E.numlines != 0 && E.cx < E.lines[E.cy].len - 1) E.cx++;
		break;
	default: return;
	}

	if (E.lines[E.cy].len == 0) E.cx = 0;
	else if (E.cx >= E.lines[E.cy].len) E.cx = (uint)E.lines[E.cy].len - 1;
}

static void cursor_normalize(void) {
	if (N == 0) {
		E.cy = 0;
		E.cx = 0;
	}
	while (E.cy > N - 1) E.cy--;

	if (L.len == 0) E.cx = 0;
	while (E.cx > L.len - 1) E.cx--;
}

static void process_key_normal(const int c) {
	switch (c) {
	case 'q':
	case CTRL_KEY('q'): quit();

	// Enter INSERT mode
	case 'i':
	case 'a':
	case 'A':
	case 'I':
		E.mode = MODE_INSERT;
		if (N == 0) insert_line(0);
		switch (c) {
		case 'a':
			if (L.len > 0) E.cx++;
			break;
		case 'A':
			E.cx = (uint)L.len;
			break;
		case 'I':
			E.cx = 0;
			break;
		}
		break;

	// Scrolling
	case CTRL_KEY('l'): E.coloff++; break;
	case CTRL_KEY('h'):
		if (E.coloff > 0) E.coloff--;
		break;
	case CTRL_KEY('e'): E.rowoff++; break;
	case CTRL_KEY('y'):
		if (E.rowoff > 0) E.rowoff--;
		break;

	// Jump half-screen up/down
	case CTRL_KEY('d'):
		for (uint n = E.rows / 2; n > 0; n--) cursor_move('j');
		break;
	case CTRL_KEY('u'):
		for (uint n = E.rows / 2; n > 0; n--) cursor_move('k');
		break;

	// Start/End of line
	case '0': E.cx = 0; break;
	case '$':
		if (E.numlines) E.cx = (uint)L.len - 1;
		break;

	// Word wise movement
	case 'w':
		if (E.cx >= E.lines[E.cy].len - 1) break;

		// Consume current word
		while (E.cx < E.lines[E.cy].len - 1 &&
		       !isspace(E.lines[E.cy].chars[E.cx]))
			E.cx++;
		// Consume whitespaces
		while (E.cx < E.lines[E.cy].len - 1 &&
		       isspace(E.lines[E.cy].chars[E.cx]))
			E.cx++;
		break;
	case 'e':
		if (E.cx >= E.lines[E.cy].len - 1) break;

		// Consume whitespace to the right
		if (isspace(E.lines[E.cy].chars[E.cx + 1]))
			while (E.cx < E.lines[E.cy].len - 1 &&
			       isspace(E.lines[E.cy].chars[E.cx + 1]))
				E.cx++;

		// Consume word till the end
		while (E.cx < E.lines[E.cy].len - 1 &&
		       !isspace(E.lines[E.cy].chars[E.cx + 1]))
			E.cx++;
		break;
	case 'b':
		if (E.cx == 0) break;

		// Consume whitespace to the left
		if (isspace(E.lines[E.cy].chars[E.cx - 1]))
			while (E.cx > 0 &&
			       isspace(E.lines[E.cy].chars[E.cx - 1]))
				E.cx--;

		// Consume word till the beginning
		while (E.cx > 0 && !isspace(E.lines[E.cy].chars[E.cx - 1]))
			E.cx--;
		break;

	// File start and end
	case 'g':
	case 'G':
		E.cy = c == 'g' ? 0 : E.numlines - 1;
		break;

	// Inserting lines
	case 'O':
		E.cx = 0;
		insert_line(E.cy);
		E.mode = MODE_INSERT;
		break;
	case 'o':
		E.cx = 0;
		insert_line(E.cy++ + 1);
		E.mode = MODE_INSERT;
		break;

	// Join lines
	case 'J':
		join_lines(E.cy);
		break;

	// Deleting lines
	case 'd':
		delete_line(E.cy);
		break;

	// Delete single character
	case 'x':
		line_delete_char(&E.lines[E.cy], E.cx);
		break;

	default: cursor_move(c);
	}
}

static void process_key_insert(const int c) {
	switch (c) {
	case CTRL_KEY('q'):
	case KEY_ESCAPE:
		E.mode = MODE_NORMAL;
		cursor_normalize();
		break;

	case KEY_DELETE:
		if (E.cx == 0) break;
		line_delete_char(&E.lines[E.cy], --E.cx);
		break;

	case KEY_RETURN:
		split_line(E.cy, E.cx);
		E.cy++;
		E.cx = 0;
		break;

	default:
		if (!isalnum(c) && c != ' ') break;
		line_insert_char(&E.lines[E.cy], E.cx++, (char)c);
		break;
	}
}

static void process_key(void) {
	modes[E.mode].process_key(read_key());
	if (E.mode == MODE_NORMAL) cursor_normalize();
}

static void editor_scroll(void) {
	if (E.cy < E.rowoff) E.rowoff = E.cy;
	if ((E.cy + 1) > E.rowoff + (E.rows - 1))
		E.rowoff = (E.cy + 1) - (E.rows - 1);
	if (E.cx < E.coloff) E.coloff = E.cx;
	if ((E.cx + 1) > E.coloff + E.cols) E.coloff = (E.cx + 1) - E.cols;
}

// --------------------------------- Output -----------------------------------
static int screen_append(ScreenBuffer *screen, const char s[], size_t len) {
	char *new = realloc(screen->data,
			    sizeof *screen->data * (screen->len + len));
	if (new == NULL) return -1;
	screen->data = new;

	memcpy(&screen->data[screen->len], s, len);
	screen->len += len;

	return 0;
}

static void screen_free(ScreenBuffer *screen) {
	free(screen->data);
}

static void draw_welcome_message(ScreenBuffer *screen) {
	char s[80];
	int len_required =
		snprintf(s, sizeof s, "ni editor -- version %s", Ni_VERSION);
	if (len_required == -1) {
		die("snprintf");
	}
	uint len = (uint)len_required > E.cols ? E.cols : (uint)len_required;
	uint padding = (E.cols - len) / 2;
	if (padding) {
		screen_append(screen, "~", 1);
		padding--;
	}
	while (padding--) {
		screen_append(screen, " ", 1);
	}
	screen_append(screen, s, len);
}

static int draw_status(ScreenBuffer *screen) {
	char mode[32];
	int mode_len = snprintf(mode, sizeof mode - 1, " --- %s --- ",
				modes[E.mode].status);
	if (mode_len == -1) return -1;
	mode_len = mode_len > (int)sizeof mode ? sizeof mode : mode_len;
	screen_append(screen, mode, (uint)mode_len);

	char cursor[12];
	int cursor_len = snprintf(cursor, sizeof cursor - 1, "[%d:%d]",
				  E.cy + 1, E.cx + 1);
	if (cursor_len == -1) return -1;
	cursor_len =
		cursor_len > (int)sizeof cursor ? sizeof cursor : cursor_len;

	int padding = (int)E.cols - cursor_len - mode_len;
	while (padding-- > 0) {
		screen_append(screen, " ", 1);
	}

	screen_append(screen, cursor, (uint)cursor_len);

	return 0;
}

static void draw_lines(ScreenBuffer *screen) {
	for (uint y = 0; y < E.rows; y++) {
		uint idx = y + E.rowoff;

		if (y == E.rows - 1) {
			draw_status(screen);
			return;
		}

		if (idx >= E.numlines) {
			if (E.numlines == 0 && y == E.rows / 3)
				draw_welcome_message(screen);
			else screen_append(screen, "~", 1);
		} else if (E.coloff < E.lines[idx].len) {
			uint len = E.lines[idx].len - E.coloff <= E.cols
					 ? (uint)(E.lines[idx].len - E.coloff)
					 : E.cols;
			screen_append(screen, E.lines[idx].chars + E.coloff,
				      len);
		}

		screen_append(screen, "\x1b[K", 3); // clear till EOL
		screen_append(screen, "\r\n", 2);
	}
}

static int place_cursor(ScreenBuffer *screen, uint x, uint y) {
	char s[32];
	int len_required = snprintf(s, sizeof s, "\x1b[%d;%dH", y + 1, x + 1);
	if (len_required == -1 || len_required >= (int)sizeof s) return -1;
	screen_append(screen, s, (uint)len_required);

	return 0;
}

static void refresh_screen(void) {
	ScreenBuffer screen = {0};
	editor_scroll();
	screen_append(&screen, "\x1b[?25l", 4); // hide cursor

	place_cursor(&screen, 0, 0);
	draw_lines(&screen);
	place_cursor(&screen, E.cx - E.coloff, E.cy - E.rowoff);

	screen_append(&screen, "\x1b[?25h", 4); // show cursor
	write(STDOUT_FILENO, screen.data, screen.len);
	screen_free(&screen);
}

// -------------------------------- File I/O ----------------------------------
static size_t line_length(const char *chars, size_t len) {
	while (len > 0 && (chars[len - 1] == '\n' || chars[len - 1] == '\r'))
		len--;
	return len;
}

static void editor_append_line(const char *chars, size_t len) {
	size_t i = E.numlines;
	E.numlines++;
	E.lines = realloc(E.lines, (sizeof *E.lines) * E.numlines);
	len = line_length(chars, len);
	E.lines[i].chars = strndup(chars, len);
	E.lines[i].len = len;
}

static void editor_open(const char *restrict fname) {
	FILE *f = fopen(fname, "r");
	if (!f) die("fopen");

	E.numlines = 0;
	if (E.lines) free(E.lines);
	E.lines = NULL;

	char *line = NULL;
	size_t linecap = 0;
	ssize_t bytes_read;

	while ((bytes_read = getline(&line, &linecap, f)) != -1)
		editor_append_line(line, (size_t)bytes_read);

	if (line) free(line);
	fclose(f);
}

// ---------------------------------- Main ------------------------------------
static void editor_init(void) {
	E.mode = MODE_NORMAL;
	E.numlines = 0;
	E.lines = NULL;
	E.rowoff = 0;
	E.coloff = 0;
	E.cx = 0;
	E.cy = 0;
	if (get_window_size(&E.rows, &E.cols) == -1) die("get_window_size");
}

int main(int argc, char *argv[]) {
	enable_raw_mode();
	editor_init();
	if (argc >= 2) editor_open(argv[1]);

	while (true) {
		refresh_screen();
		process_key();
	}
}
