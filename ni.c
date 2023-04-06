// -------------------------------- Includes ----------------------------------
#include <ctype.h>   // isnumber, isblank, isprint, isspace, isalnum
#include <errno.h>   // errno
#include <signal.h>  // signal, SIGWINCH
#include <stdarg.h>  // va_list, va_start, va_end
#include <stdbool.h> // bool, true, false
#include <stdio.h>   // fopen, fclose, perror, sys_nerr
#include <stdlib.h>  // realloc, free, exit, atexit
#include <string.h>  // strndup, strdup, memmove
#include <termios.h> // struct termios, tcsetattr, tcgetattr, TCSANOW, BRKINT, ICRNL, INPCK, ISTRIP, IXON, OPOST, CS8, ECHO, ICANON, ISIG, IEXTEN
#include <time.h>    // timespec_get, struct timespec, TIME_UTC
#include <unistd.h>  // write, read, STDIN_FILENO, STDOUT_FILENO

// --------------------------------- Defines ----------------------------------
#define NI_VERSION "0.0.1"

#define NORETURN __attribute__((noreturn)) void

#define MAX_SCREEN_LEN 1 << 16
#define MAX_RENDER 1024
#define MAX_MESSAGE_LEN 256
#define MAX_CHORD 3 // d[ge] or d[f..]
#define TABSTOP 8
#define NUM_UTIL_LINES 2

// Mask 00011111 i.e. zero out the upper three bits
#define CTRL_KEY(k) ((k)&0x1f)

// Input / output
#define READ(C) (read(STDIN_FILENO, &(C), 1) == 1)
#define SEND_ESCAPE(C)                                                         \
	(write(STDOUT_FILENO, (C), sizeof(C) - 1) == sizeof(C) - 1)

// Error handling / Debugging
#define STR(A) #A

#define PERROR_(F, L, S)                                                       \
	do {                                                                   \
		if (errno >= 0 && errno < sys_nerr)                            \
			perror(F ":" STR(L) " " S);                            \
		else printf("%s:%d %s\n", F, L, S);                            \
	} while (0)
#define PERROR(S) PERROR_(__FILE__, __LINE__, S)

#define DIE(S)                                                                 \
	do {                                                                   \
		clear_screen();                                                \
		PERROR(S);                                                     \
		exit(EXIT_FAILURE);                                            \
	} while (0)

// Common constructs
#define CLINE (E.lines + E.cy)
#define NOLINES (E.numlines == 0)
#define LASTLINE (E.numlines - 1)
#define ENDOFLINE (CLINE->len - 1)

// ---------------------------------- Types -----------------------------------
typedef unsigned int uint;
typedef struct termios Term;

typedef enum EditorMode {
	MODE_NORMAL,
	MODE_INSERT,
} EditorMode;

typedef enum EditorKey {
	KEY_UP = 1000,
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
	char data[MAX_SCREEN_LEN];
	size_t len;
} ScreenBuffer;

typedef struct MessageBuffer {
	char data[MAX_MESSAGE_LEN];
	size_t len;
} MessageBuffer;

typedef struct Line {
	uint len;
	char *chars;
} Line;

typedef struct Find {
	char c;
	bool forward;
} Find;

typedef struct Chord {
	char keys[MAX_CHORD];
	uint len;
} Chord;

typedef struct Editor {
	// Terminal
	Term term_orig;
	Term term;

	// Lines
	Line *lines;
	uint numlines;

	// Cursor
	uint cx, cy, rx;

	// Viewport
	uint rowoff, coloff;
	uint rows, cols;

	// Mode
	EditorMode mode;

	// File
	char *filename;
	bool dirty;

	// Input
	Chord chord;
	Find find;

	// Status & Messages
	MessageBuffer message;

	// Rendering
	// Screen i.e draw buffer. The output is written to this buffer so that
	// it can be send to the screen in a single call to avoid flickering.
	ScreenBuffer screen;
	// Renders individual lines before printing them to the screen.
	// TODO: Do we even need this buffer? Why not render straight to the
	// screen?
	char render_buffer[MAX_RENDER];

	// Settings
	char render_tab_characters[2];
} Editor;

// ------------------------------ State & Data --------------------------------
static Editor E;

// -------------------------------- Terminal ----------------------------------
static void clear_screen(void) {
	SEND_ESCAPE("\x1b[2J");
	SEND_ESCAPE("\x1b[H");
}

static NORETURN quit(int code) {
	clear_screen();
	exit(code);
}

static void reset_term(void) {
	if (tcsetattr(STDIN_FILENO, TCSANOW, &E.term_orig) == -1)
		DIE("tcsetattr");
}

static void enable_immediate_mode(void) {
	E.term.c_cc[VMIN] = 0;
	E.term.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &E.term) == -1) DIE("tcsetattr");
}

static void enable_block_mode(void) {
	E.term.c_cc[VMIN] = 1;
	E.term.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &E.term) == -1) DIE("tcsetattr");
}

static void enable_raw_mode(void) {
	// Save terminal settings and setup cleanup on exit.
	if (tcgetattr(STDIN_FILENO, &E.term_orig) == -1) DIE("tcgetattr");
	atexit(reset_term);

	// Set terminal to raw mode.
	E.term = E.term_orig;
	E.term.c_iflag &= ~(uint)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	E.term.c_oflag &= ~(uint)(OPOST);
	E.term.c_cflag |= (uint)(CS8);
	E.term.c_lflag &= ~(uint)(ECHO | ICANON | ISIG | IEXTEN);
	E.term.c_cc[VMIN] = 1;
	E.term.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &E.term) == -1) DIE("tcsetattr");
}

static int read_escape_sequence(void) {
	enable_immediate_mode();
	EditorKey key = KEY_ESCAPE;
	char seq[3];

	if (!READ(seq[0])) goto outro;
	if (!READ(seq[1])) goto outro;
	if (seq[0] != '[') goto outro;

	switch (seq[1]) {
	case 'A': key = KEY_UP; goto outro;
	case 'B': key = KEY_DOWN; goto outro;
	case 'D': key = KEY_LEFT; goto outro;
	case 'C': key = KEY_RIGHT; goto outro;
	}

	if (isnumber(seq[1]) && READ(seq[2]) && seq[2] == '~') switch (seq[1]) {
		case '3': key = KEY_DELETE; goto outro;
		case '5': key = KEY_PAGE_UP; goto outro;
		case '6': key = KEY_PAGE_DOWN; goto outro;
		}

outro:
	enable_block_mode();
	return (int)key;
}

static int read_key(void) {
	char c;
	if (!READ(c)) return KEY_NOOP;

	switch (c) {
	case 13: return KEY_RETURN;

	case 8:
	case 127: return KEY_DELETE;

	case '\x1b': return read_escape_sequence();

	default: return c;
	}
}

static int get_cursor_position(uint *rows, uint *cols) {
	// Solicit device report
	if (!SEND_ESCAPE("\x1b[6n")) return -1;

	// Read device report: \x1b[<rows>;<cols>R
	char buf[32];
	uint i = 0;
	while (i < sizeof buf - 1) {
		if (!READ(buf[i])) return -1;
		if (buf[i++] == 'R') break;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[1], "[%d;%dR", rows, cols) != 2) return -1;

	return 0;
}

static int get_window_size(uint *rows, uint *cols) {
	// Reset cursor to home
	if (!SEND_ESCAPE("\x1b[H")) return -1;

	// Move to end
	if (!SEND_ESCAPE("\x1b[999C\x1b[999B")) return -1;
	if (get_cursor_position(rows, cols) == -1) return -1;

	return 0;
}

// ---------------------------------- Misc ------------------------------------
static struct timespec get_current_time(void) {
	struct timespec ts;

	if (timespec_get(&ts, TIME_UTC) == 0) DIE("timespec_get");

	return ts;
}

// --------------------------------- Editing ----------------------------------
static void insert_line(size_t at) {
	if (at > E.numlines) at = E.numlines;

	E.lines = realloc(E.lines, (sizeof *E.lines) * (E.numlines + 1));
	if (!E.lines) DIE("realloc");

	if (at < LASTLINE)
		memmove(&E.lines[at + 1], &E.lines[at],
		        (sizeof *E.lines) * (E.numlines - at));

	E.lines[at].len = 0;
	E.lines[at].chars = strdup("");

	E.numlines++;

	E.dirty = true;
}

static void delete_line(uint at) {
	if (NOLINES) return;
	if (at >= E.numlines) at = LASTLINE;

	free(E.lines[at].chars);
	E.lines[at].chars = NULL;

	if (at < LASTLINE)
		memmove(&E.lines[at], &E.lines[at + 1],
		        (sizeof *E.lines) * (E.numlines - (at + 1)));

	E.numlines--;

	E.dirty = true;
}

static void split_line(uint at, uint split_at) {
	if (NOLINES) return;
	if (at >= E.numlines) return;

	insert_line(at + 1);
	if (split_at >= E.lines[at].len) return;

	free(E.lines[at + 1].chars);
	uint len = E.lines[at].len - split_at;
	E.lines[at + 1].chars = strndup(&E.lines[at].chars[split_at], len);
	E.lines[at + 1].len = len;

	// Shorten original line by len
	E.lines[at].len -= len;
	E.lines[at].chars[E.lines[at].len] = '\0';
	E.lines[at].chars = realloc(E.lines[at].chars, E.lines[at].len + 1);

	E.dirty = true;
}

static void join_lines(uint at) {
	if (E.numlines <= 1) return;
	if (at >= E.numlines) at = LASTLINE;

	const Line *const src = &E.lines[at + 1];
	Line *const target = &E.lines[at];

	if (src->len > 0) {
		uint add_len = E.lines[at + 1].len;
		const bool add_space = src->chars[0] != ' ';
		if (add_space) add_len++;

		target->chars =
			realloc(target->chars, target->len + add_len + 1);
		if (!target->chars) DIE("realloc");

		if (add_space) target->chars[target->len] = ' ';
		memmove(&target->chars[target->len + 1], E.lines[at + 1].chars,
		        E.lines[at + 1].len);
		target->len += add_len;
		target->chars[target->len] = '\0';
	}

	delete_line(at + 1);

	E.dirty = true;
}

static void crop_line(uint at) {
	if (NOLINES) return;

	CLINE->len = at;

	E.dirty = true;
}

static void line_insert_char(Line *line, uint at, char c) {
	if (at > line->len) at = line->len;

	// line->len does not include the terminating '\0'
	line->chars = realloc(line->chars, line->len + 1 + 1);
	if (!line->chars) DIE("realloc");

	memmove(&line->chars[at + 1], &line->chars[at], line->len - at + 1);

	line->chars[at] = c;
	line->len++;

	E.dirty = true;
}

static void delete_chars(uint at, uint n, Line *line) {
	// line->len does not include the terminating '\0'
	if (line->len == 0) return;
	if (at >= line->len) return;
	uint end = at + n;

	memmove(&line->chars[at], &line->chars[end], line->len - end + 1);

	line->len -= n;
	line->chars = realloc(line->chars, line->len + 1);
	if (!line->chars) DIE("realloc");

	E.dirty = true;
}

// ---------------------------------- Input -----------------------------------
static void cursor_move(int c) {
	switch (c) {

	case 'k':
	case KEY_UP: E.cy > 0 && E.cy--; break;

	case 'j':
	case KEY_DOWN:
		if (!NOLINES && E.cy < LASTLINE) E.cy++;
		break;

	case 'h':
	case KEY_LEFT: E.cx > 0 && E.cx--; break;

	case 'l':
	case KEY_RIGHT:
		if (!NOLINES && E.cx < ENDOFLINE) E.cx++;
		break;

	default: return;
	}

	if (NOLINES || CLINE->len == 0) E.cx = 0;
	else if (E.cx >= CLINE->len) E.cx = (uint)ENDOFLINE;
}
static void cursor_normalize(void) {
	if (NOLINES) {
		E.cy = 0;
		E.cx = 0;
		return;
	}

	uint max_y = LASTLINE;
	uint max_x = CLINE->len == 0 ? 0 : ENDOFLINE;

	if (E.cy > max_y) E.cy = max_y;
	if (E.cx > max_x) E.cx = max_x;
}

static void format_message(const char *restrict format, ...);
static void show_file_info(void) {
	if (E.numlines > 0)
		format_message(
			"\"%s\" %d lines, --%.0f%%--",
			E.filename ? E.filename : "[NO NAME]", E.numlines,
			((double)E.cy + 1) / (double)E.numlines * 100);
	else
		format_message(
			"\"%s\" --No lines in buffer--",
			E.filename ? E.filename : "[NO NAME]");
}

static uint find_word(uint x, const Line *line) {
	if (x >= line->len - 1) return x;

	// Consume current word
	while (x < line->len - 1 && isalnum(line->chars[x])) x++;

	// Consume whitespaces
	while (x < line->len - 1 && !isalnum(line->chars[x])) x++;

	return x;
}

static uint find_end(uint x, const Line *line) {
	if (x >= line->len - 1) return x;

	// Consume whitespace to the right
	if (!isalnum(line->chars[x + 1]))
		while (x < line->len - 1 && !isalnum(line->chars[x + 1])) x++;

	// Consume word till the end
	while (x < line->len - 1 && isalnum(line->chars[x + 1])) x++;

	return x;
}

static uint find_word_backwards(uint x, const Line *line) {
	if (x == 0) return x;

	if (!isalnum(line->chars[x - 1]))
		while (x > 0 && !isalnum(line->chars[x - 1])) x--;

	// Consume word till the beginning
	while (x > 0 && !!isalnum(line->chars[x - 1])) x--;

	return x;
}

static uint find_end_backwards(uint x, const Line *line) {
	if (x == 0) return x;

	// Consume word till the beginning
	while (x > 0 && isalnum(line->chars[x])) x--;

	// Consume whitespace to the left
	while (x > 0 && !isalnum(line->chars[x])) x--;

	return x;
}

static uint find_char_in_line(uint x, const Line *line, char c, bool forward) {
	uint xx = x;
	if (forward) xx++;
	else xx--;

	while (xx < line->len) {
		if (line->chars[xx] == c) return xx;
		if (forward) xx++;
		else xx--;
	}

	return x;
}

static uint repeat_find(uint x, const Line *line, bool same_direction) {
	bool forward = same_direction ? E.find.forward : !E.find.forward;
	return find_char_in_line(x, line, E.find.c, forward);
}

static void enter_insert_mode(char c) {
	if (!E.numlines) insert_line(0);

	switch (c) {
	case 'a':
		if (CLINE->len > 0) E.cx++;
		break;
	case 'A': E.cx = CLINE->len; break;
	case 'I': E.cx = 0; break;
	}

	E.mode = MODE_INSERT;
}

static void delete_motion(char c) {
	Line *line = CLINE;
	uint start, end;

	switch (c) {
	case 'w':
		start = E.cx;
		end = find_word(E.cx, line);
		break;
	case 'e':
		start = E.cx;
		end = find_end(E.cx, line) + 1;
		break;
	case 'b':
		start = find_word_backwards(E.cx, line);
		end = E.cx;
		break;
	case 'E':
		start = find_end_backwards(E.cx, line);
		end = E.cx;
		break;

	default: return;
	}

	delete_chars(start, end - start, line);
	E.cx = start;
}

static void editor_save(void);

static void process_key_normal(const int c) {
	E.chord.keys[E.chord.len++] = (char)c;
	if (E.chord.len == 1) {
		switch (c) {
		case 'q': quit(EXIT_SUCCESS);
		case CTRL_KEY('q'): quit(EXIT_FAILURE);
		case CTRL_KEY('s'): editor_save(); break;
		case CTRL_KEY('g'): show_file_info(); break;

		// Enter INSERT mode
		case 'i':
		case 'a':
		case 'A':
		case 'I': enter_insert_mode((char)c); break;

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
			if (E.numlines) E.cx = ENDOFLINE;
			break;

		// Word wise movement
		case 'w': E.cx = find_word(E.cx, CLINE); break;
		case 'b': E.cx = find_word_backwards(E.cx, CLINE); break;
		case 'e': E.cx = find_end(E.cx, CLINE); break;

		// Jumps
		case 'G': E.cy = LASTLINE; break;

		// Inserting lines
		case 'O':
			E.cx = 0;
			insert_line(E.cy);
			E.mode = MODE_INSERT;
			break;
		case 'o':
			E.cx = 0;
			if (NOLINES) insert_line(E.cy);
			else insert_line(E.cy++ + 1);
			E.mode = MODE_INSERT;
			break;

		// Join lines
		case 'J': join_lines(E.cy); break;

		// Deleting
		case 'D': crop_line(E.cx--); break;

		// Changing
		case 'C':
			if (E.numlines > 0) crop_line(E.cx);
			enter_insert_mode('i');
			break;

		// Delete single character
		case 'x': delete_chars(E.cx, 1, CLINE); break;

		// Search in line
		case ';':
		case ',': E.cx = repeat_find(E.cx, CLINE, c == ';'); break;

		case 'c':
		case 'd':
		case 'g':
		case 'f':
		case 'F':
		case 'Z': return;

		default: cursor_move(c); break;
		}
	} else if (E.chord.len == 2) {
		switch (E.chord.keys[0]) {
		case 'Z':
			switch (c) {
			case 'Z': editor_save(); quit(EXIT_SUCCESS);
			case 'Q': quit(EXIT_SUCCESS);
			}

		case 'g':
			switch (c) {
			case 'g': E.cy = 0; break;
			case 'e': E.cx = find_end_backwards(E.cx, CLINE); break;
			}
			break;

		case 'd':
			switch (c) {
			case 'd': delete_line(E.cy); break;
			case 'w':
			case 'e':
			case 'b': delete_motion((char)c); break;
			case 'f':
			case 'F':
			case 'g': return;
			}
			break;

		case 'c':
			switch (c) {
			case 'w':
			case 'e':
			case 'b': delete_motion((char)c); break;
			}
			enter_insert_mode('i');
			break;

		case 'f':
		case 'F':
			if (isprint(c) || isblank(c)) {
				E.find.forward = E.chord.keys[0] == 'f';
				E.find.c = (char)c;
				E.cx = find_char_in_line(
					E.cx, CLINE, E.find.c, E.find.forward);
			}
			break;
		}
	} else if (E.chord.len == 3) {
		switch (E.chord.keys[0]) {
		case 'd':
			switch (E.chord.keys[1]) {
			case 'g':
				switch (c) {
				case 'e': delete_motion('E'); break;
				}
				break;
			case 'f':
				if (isprint(c) || isblank(c)) {
					E.find.forward = true;
					E.find.c = (char)c;
					uint target = find_char_in_line(
						E.cx, CLINE, E.find.c, true);
					delete_chars(
						E.cx, target + 1 - E.cx, CLINE);
				}
				break;
			case 'F':
				if (isprint(c) || isblank(c)) {
					E.find.forward = false;
					E.find.c = (char)c;
					uint target = find_char_in_line(
						E.cx, CLINE, E.find.c, false);
					delete_chars(
						target, E.cx - target, CLINE);
					E.cx = target;
				}
				break;
			}
			break;
		}
	}

	E.chord.len = 0;
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
		delete_chars(--E.cx, 1, CLINE);
		break;

	case KEY_RETURN:
		split_line(E.cy, E.cx);
		E.cy++;
		E.cx = 0;
		break;

	default:
		if (isprint(c) || isblank(c))
			line_insert_char(CLINE, E.cx++, (char)c);
		break;
	}
}

static struct timespec process_key(void) {
	int key = read_key();
	struct timespec key_received_at = get_current_time();

	switch (E.mode) {
	case MODE_NORMAL: {
		process_key_normal(key);
		if (E.mode == MODE_NORMAL) cursor_normalize();
	} break;
	case MODE_INSERT: {
		process_key_insert(key);
	} break;
	}

	return key_received_at;
}

static uint cx2rx(uint cx, Line *line) {
	if (!line->len) return cx;

	const char *chars = line->chars;
	uint rx = 0;

	for (uint i = 0; i < cx; i++) {
		if (chars[i] == '\t') rx += TABSTOP - (rx % TABSTOP);
		else rx++;
	}

	return rx;
}

static void editor_scroll(void) {
	E.rx = E.cy < E.numlines ? cx2rx(E.cx, CLINE) : E.cx;
	if (E.cy < E.rowoff) E.rowoff = E.cy;
	if ((E.cy + 1) > E.rowoff + (E.rows - NUM_UTIL_LINES))
		E.rowoff = (E.cy + 1) - (E.rows - NUM_UTIL_LINES);
	if (E.rx < E.coloff) E.coloff = E.rx;
	if ((E.rx + 1) > E.coloff + E.cols) E.coloff = (E.rx + 1) - E.cols;
}

// --------------------------------- Output -----------------------------------
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
static void format_message(const char *restrict format, ...) {
	size_t size = MAX_MESSAGE_LEN;

	va_list ap;
	va_start(ap, format);

	int len = vsnprintf(E.message.data, size, format, ap);
	if (len < 0) DIE("format_message");
	E.message.len = (size_t)len >= size ? size - 1 : (size_t)len;

	va_end(ap);
}
#pragma clang diagnostic pop

static int screen_append(ScreenBuffer *screen, const char s[], size_t len) {
	if (screen->len + len > MAX_SCREEN_LEN) return -1;

	memcpy(&screen->data[screen->len], s, len);
	screen->len += len;

	return 0;
}

static void draw_welcome_message(ScreenBuffer *screen) {
	uint max_len = E.cols - 1;
	char buf[80];
	int required_len = snprintf(
		buf, sizeof buf, "ni editor -- version %s", NI_VERSION);
	if (required_len == -1) DIE("snprintf");
	uint len = (uint)required_len > max_len ? max_len : (uint)required_len;
	uint padding = (max_len - len) / 2;
	while (padding--) screen_append(screen, " ", 1);
	screen_append(screen, buf, len);
}

static int draw_status(ScreenBuffer *screen) {
	const int max_len = (int)E.cols;

	char mode_buf[32];
	int mode_len = snprintf(
		mode_buf, sizeof mode_buf - 1, " --- %s --- ",
		E.mode == MODE_NORMAL ? "NORMAL" : "INSERT");
	if (mode_len == -1) return -1;
	if (mode_len < (int)sizeof mode_buf && E.mode == MODE_NORMAL) {
		int chord_len = snprintf(
			mode_buf + mode_len,
			sizeof mode_buf - 1 - (size_t)mode_len, "%.*s",
			E.chord.len, E.chord.keys);
		if (chord_len == -1) return -1;
		mode_len += chord_len;
	}
	mode_len = mode_len > (int)sizeof mode_buf ? sizeof mode_buf : mode_len;

	char cursor_buf[12];
	int cursor_len = snprintf(
		cursor_buf, sizeof cursor_buf - 1, "[%d:%d]", E.cy + 1,
		E.cx + 1);
	if (cursor_len == -1) return -1;
	cursor_len = cursor_len > (int)sizeof cursor_buf ? sizeof cursor_buf
	                                                 : cursor_len;

	char filename_buf[128];
	int filename_len = snprintf(
		filename_buf, sizeof filename_buf - 1, "%s%s",
		E.filename == NULL ? "[NO NAME]" : E.filename,
		E.dirty ? " [+]" : "");
	if (filename_len == -1) return -1;
	if (filename_len > (int)sizeof filename_buf)
		filename_len = sizeof filename_buf;

	const int total_len = mode_len + filename_len + cursor_len;
	if (max_len < total_len) {
		char msg[] = "!!! ERROR: Status too long !!!";
		screen_append(screen, msg, sizeof msg);
		return -1;
	}

	// clang-format off
	struct { char *s;  int len; } parts[] = {
		{mode_buf,     mode_len},
		{filename_buf, filename_len},
		{cursor_buf,   cursor_len}
	};
	// clang-format on

	const int n_parts = sizeof parts / sizeof parts[0];
	const int padding = (max_len - total_len) / (n_parts - 1);
	int remaining = max_len;

	screen_append(screen, "\x1b[7m", 4);
	for (int i = 0; i < n_parts; i++) {
		const char *part = parts[i].s;
		const uint len = (uint)parts[i].len;

		remaining -= len;
		if (i > 0) {
			int pad = padding;
			remaining -= pad;
			if (i == n_parts - 1)
				while (remaining-- > 0) pad++;
			while (pad-- > 0) screen_append(screen, " ", 1);
		}
		screen_append(screen, part, len);
	}
	screen_append(screen, "\x1b[0m", 4);

	return 0;
}

static unsigned long total_microseconds(const struct timespec *ts) {
	if (ts == NULL) return 0;

	return ((unsigned long)ts->tv_sec * 1000000000ul +
	        (unsigned long)ts->tv_nsec) /
	       1000;
}

static int draw_message(ScreenBuffer *screen, const struct timespec *duration) {
	char duration_msg[32];
	unsigned long duration_us = total_microseconds(duration);
	int duration_len =
		duration_us == 0
			? 0
			: snprintf(
				  duration_msg, sizeof duration_msg, " %lu us",
				  total_microseconds(duration));
	if (duration_len < 0) return 0;

	if ((uint)duration_len >= sizeof duration_msg)
		duration_len = sizeof duration_msg;

	int remaining = (int)E.cols - duration_len;
	if (remaining < 0) return 0;

	if (E.message.len > 0) {
		size_t len = (size_t)(E.message.len > (size_t)remaining
		                              ? (size_t)remaining
		                              : E.message.len);
		screen_append(screen, E.message.data, len);
		remaining -= len;
	}
	while (remaining--) screen_append(screen, " ", 1);
	screen_append(screen, duration_msg, (size_t)duration_len);

	return 0;
}

static uint render_tab(char *dst, const size_t size, uint len) {
	dst[len++] = E.render_tab_characters[0];

	while (len % TABSTOP && len < size)
		dst[len++] = E.render_tab_characters[1];

	return len;
}

static uint render(const Line *line, char *dst, const size_t size) {
	uint len = 0;

	for (uint i = 0; i < line->len && i < size - 1; i++)
		switch (line->chars[i]) {
		case '\t': len += render_tab(dst, size, len); break;
		default: dst[len++] = line->chars[i]; break;
		}

	dst[len] = '\0';

	return len;
}

static void draw_line(ScreenBuffer *screen, Line *line) {
	if (E.coloff >= line->len) return;
	uint rendered_len =
		render(line, E.render_buffer, sizeof(E.render_buffer));
	uint visible_len = rendered_len - E.coloff <= E.cols
	                         ? (rendered_len - E.coloff)
	                         : E.cols;
	screen_append(screen, E.render_buffer + E.coloff, visible_len);
}

static void draw_lines(ScreenBuffer *screen, const struct timespec *duration) {
	for (uint y = 0; y < E.rows; y++) {
		uint line_index = y + E.rowoff;

		if (E.rows - y == 2) draw_status(screen);
		else if (E.rows - y == 1) draw_message(screen, duration);
		else if (line_index < E.numlines)
			draw_line(screen, E.lines + line_index);
		else screen_append(screen, "~", 1);

		// TODO: Welcome screen
		if (NOLINES && y == E.rows / 3) draw_welcome_message(screen);

		// Close of the line
		screen_append(screen, "\x1b[K", 3);
		if (E.rows - y > 1) screen_append(screen, "\r\n", 2);
	}
}

static int place_cursor(ScreenBuffer *screen, uint x, uint y) {
	char s[32];
	int len = snprintf(s, sizeof s, "\x1b[%d;%dH", y + 1, x + 1);
	if (len == -1 || len >= (int)sizeof s) DIE("place_cursor");
	screen_append(screen, s, (uint)len);

	return 0;
}

static struct timespec refresh_screen(const struct timespec *duration) {
	E.screen.len = 0;
	editor_scroll();
	screen_append(&E.screen, "\x1b[?25l", 6); // hide cursor

	place_cursor(&E.screen, 0, 0);
	draw_lines(&E.screen, duration);
	place_cursor(&E.screen, E.rx - E.coloff, E.cy - E.rowoff);

	screen_append(&E.screen, "\x1b[?25h", 6); // show cursor
	write(STDOUT_FILENO, E.screen.data, E.screen.len);

	return get_current_time();
}

// -------------------------------- File I/O ----------------------------------
static uint line_length(const char *chars, uint len) {
	while (len > 0 && (chars[len - 1] == '\n' || chars[len - 1] == '\r'))
		len--;

	return len;
}

static void editor_append_line(const char *chars, uint len) {
	uint i = E.numlines;
	E.numlines++;
	E.lines = realloc(E.lines, (sizeof *E.lines) * E.numlines);
	len = line_length(chars, len);
	E.lines[i].chars = strndup(chars, len);
	E.lines[i].len = len;
}

static void editor_open(const char *restrict fname) {
	FILE *f = fopen(fname, "r");
	if (!f) DIE("fopen");

	E.numlines = 0;
	if (E.lines) free(E.lines);
	E.lines = NULL;

	char *line = NULL;
	size_t linecap = 0;
	ssize_t bytes_read;

	while ((bytes_read = getline(&line, &linecap, f)) != -1)
		editor_append_line(line, (uint)bytes_read);

	if (line) free(line);
	fclose(f);
	if (E.filename) free(E.filename);
	E.filename = strdup(fname);

	format_message("Loaded: \"%s\"", fname);
}

static void editor_save(void) {
	if (!E.filename) return;

	FILE *f = fopen(E.filename, "w");
	if (!f) DIE("fopen");

	for (uint i = 0; i < E.numlines; i++) {
		fputs(E.lines[i].chars, f);
		fputs("\n", f);
	}

	fclose(f);

	format_message("Saved: \"%s\"", E.filename);
	E.dirty = false;
}

// ---------------------------------- Main ------------------------------------
static void handle_resize(int sig) {
	(void)sig;
	if (get_window_size(&E.rows, &E.cols) == -1) DIE("get_window_size");
	refresh_screen(NULL);
}

static void editor_init(void) {
	E.mode = MODE_NORMAL;
	E.lines = NULL;
	E.filename = NULL;
	E.numlines = 0;
	E.rowoff = E.coloff = 0;
	E.cx = E.cy = E.rx = 0;
	E.render_tab_characters[0] = '>';
	E.render_tab_characters[1] = '-';
	E.message.len = 0;
	E.dirty = false;
	E.chord.len = 0;
	E.find.c = 0;

	if (get_window_size(&E.rows, &E.cols) == -1) DIE("get_window_size");
}

static struct timespec
elapsed_time(const struct timespec *start, const struct timespec *end) {
	struct timespec elapsed;

	elapsed.tv_sec = end->tv_sec - start->tv_sec;
	elapsed.tv_nsec = end->tv_nsec - start->tv_nsec;

	if (elapsed.tv_nsec < 0) {
		elapsed.tv_nsec += 1000000000;
		elapsed.tv_sec--;
	}

	return elapsed;
}

int main(int argc, char *argv[]) {
	signal(SIGWINCH, handle_resize);
	enable_raw_mode();
	editor_init();
	if (argc >= 2) editor_open(argv[1]);

	struct timespec render_done, input_received = get_current_time();
	struct timespec duration = {0};

	while (true) {
		render_done = refresh_screen(&duration);
		duration = elapsed_time(&input_received, &render_done);
		input_received = process_key();
	}
}
