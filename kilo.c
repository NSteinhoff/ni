/// KILO
/// ====
///
/// Kilo is a minimalist text editor written in plain C without using external
/// libraries.

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
#define KILO_VERSION "0.0.1"
#define FPS 1
#define NORETURN __attribute__((noreturn)) void
// Mask 00011111 i.e. zero out the upper three bits
#define CTRL_KEY(k) ((k)&0x1f)

// ---------------------------------- Types -----------------------------------
typedef unsigned int uint;
typedef struct termios Term;

typedef enum EditorKey {
	KEY_UP = 1000,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_PAGE_UP,
	KEY_PAGE_DOWN,
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
	uint linescap;
	uint cx, cy;
	uint rowoff, coloff;
	uint rows, cols;
	char mode;
} Editor;

// ---------------------------------- Data ------------------------------------
static Editor E;

// -------------------------------- Terminal ----------------------------------
static NORETURN die(const char s[]) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	perror(s);
	exit(EXIT_FAILURE);
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
	// clang-format off
	term_raw.c_iflag &= ~(uint)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	term_raw.c_oflag &= ~(uint)(OPOST);
	term_raw.c_cflag |=  (uint)(CS8);
	term_raw.c_lflag &= ~(uint)(ECHO | ICANON | ISIG | IEXTEN);
	// clang-format on
	term_raw.c_cc[VMIN] = 0;
	term_raw.c_cc[VTIME] = 10 / FPS;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &term_raw) == -1) die("tcsetattr");
}

static int read_key(void) {
	char c;
	if (read(STDIN_FILENO, &c, 1) != 1) goto error;
	if (c != '\x1b') return c;

	// Start reading multi-byte escape sequences
	char seq[3];

	if (read(STDIN_FILENO, &seq[0], 1) != 1) goto error;
	if (read(STDIN_FILENO, &seq[1], 1) != 1) goto error;

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
		case '5': return KEY_PAGE_UP;
		case '6': return KEY_PAGE_DOWN;
		}
	}
	}

error:
	return '\x1b';
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

static void process_key(void) {
	int c = read_key();
	uint half_screen = E.rows / 2;
	switch (E.mode) {
	case 'n':
		switch (c) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(EXIT_SUCCESS);

		// Scrolling
		case CTRL_KEY('l'): E.coloff++; break;
		case CTRL_KEY('h'):
			if (E.coloff > 0) E.coloff--;
			break;
		case CTRL_KEY('e'): E.rowoff++; break;
		case CTRL_KEY('y'):
			if (E.rowoff > 0) E.rowoff--;
			break;

		// Half-screen up and down
		case CTRL_KEY('d'):
			while (half_screen--) cursor_move('j');
			break;
		case CTRL_KEY('u'):
			while (half_screen--) cursor_move('k');
			break;

		default: cursor_move(c);
		}
		break;
	default: break;
	}
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
	int len_required = snprintf(s, sizeof s, "Kilo editor -- version %s",
				    KILO_VERSION);
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
				E.mode == 'n' ? "NORMAL" : "UNKNOWN");
	if (mode_len == -1) return -1;
	mode_len = mode_len > (int)sizeof mode ? sizeof mode : mode_len;
	screen_append(screen, mode, (uint)mode_len);

	char cursor[12];
	int cursor_len =
		snprintf(cursor, sizeof cursor - 1, "[%d:%d]", E.cy, E.cx);
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

static void draw_rows(ScreenBuffer *screen) {
	for (uint y = 0; y < E.rows; y++) {
		uint idx = y + E.rowoff;

		if (y == E.rows - 1) {
			draw_status(screen);
		} else if (idx >= E.numlines) {
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
		if (y < E.rows - 1) screen_append(screen, "\r\n", 2);
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
	draw_rows(&screen);
	place_cursor(&screen, E.cx - E.coloff, E.cy - E.rowoff);

	screen_append(&screen, "\x1b[?25h", 4); // show cursor
	write(STDOUT_FILENO, screen.data, screen.len);
	screen_free(&screen);
}

// -------------------------------- File I/O ----------------------------------
static void editor_append_line(const char *line, size_t len) {
	if (E.numlines + 1 > E.linescap) {
		E.linescap = E.linescap == 0 ? 2 : E.linescap * 2;
		Line *lines = realloc(E.lines, (sizeof *lines) * E.linescap);
		if (lines) E.lines = lines;
		else die("realloc");
	}

	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
		len--;

	E.lines[E.numlines].chars = strndup(line, len);
	E.lines[E.numlines].len = len;
	E.numlines++;
}

static void editor_open(const char *restrict fname) {
	FILE *f = fopen(fname, "r");
	if (!f) die("fopen");

	E.numlines = 0;
	E.linescap = 0;
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
	E.mode = 'n';
	E.numlines = 0;
	E.linescap = 0;
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
	if (argc >= 2) {
		editor_open(argv[1]);
	}

	while (true) {
		refresh_screen();
		process_key();
	}
}
