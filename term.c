#include <ctype.h>   // isnumber
#include <errno.h>   // errno
#include <stdarg.h>  // va_list, va_start, va_end
#include <stdbool.h> // bool, true, false
#include <stdio.h>   // fopen, fclose, perror, sys_nerr
#include <stdlib.h>  // exit, atexit
#include <string.h>  // memcpy, strncpy
#include <termios.h> // struct termios, tcsetattr, tcgetattr, TCSANOW, BRKINT, ICRNL, INPCK, ISTRIP, IXON, OPOST, CS8, ECHO, ICANON, ISIG, IEXTEN
#include <time.h>    // timespec_get, struct timespec, TIME_UTC
#include <unistd.h>  // write, read, STDIN_FILENO, STDOUT_FILENO

#include "ni.h"

// --------------------------------- Defines ----------------------------------
#define NORETURN __attribute__((noreturn)) void

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

typedef struct ScreenBuffer {
	char data[MAX_SCREEN_LEN];
	size_t len;
} ScreenBuffer;

static ScreenBuffer screen;
static char render_buffer[MAX_RENDER];

static struct termios term_orig;
static struct termios term;

static void clear_screen(void) {
	SEND_ESCAPE("\x1b[2J");
	SEND_ESCAPE("\x1b[H");
}

static NORETURN quit(int code) {
	clear_screen();
	exit(code);
}

static void reset_term(void) {
	if (tcsetattr(STDIN_FILENO, TCSANOW, &term_orig) == -1)
		DIE("tcsetattr");
}

static void enable_immediate_mode(void) {
	term.c_cc[VMIN] = 0;
	term.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &term) == -1) DIE("tcsetattr");
}

static void enable_block_mode(void) {
	term.c_cc[VMIN] = 1;
	term.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &term) == -1) DIE("tcsetattr");
}

static void enable_raw_mode(void) {
	// Save terminal settings and setup cleanup on exit.
	if (tcgetattr(STDIN_FILENO, &term_orig) == -1) DIE("tcgetattr");
	atexit(reset_term);

	// Set terminal to raw mode.
	term = term_orig;
	term.c_iflag &= ~(uint)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	term.c_oflag &= ~(uint)(OPOST);
	term.c_cflag |= (uint)(CS8);
	term.c_lflag &= ~(uint)(ECHO | ICANON | ISIG | IEXTEN);
	term.c_cc[VMIN] = 1;
	term.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &term) == -1) DIE("tcsetattr");
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

static struct timespec get_current_time(void) {
	struct timespec ts;

	if (timespec_get(&ts, TIME_UTC) == 0) DIE("timespec_get");

	return ts;
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

static int screen_append(ScreenBuffer *screen, const char s[], size_t len) {
	if (screen->len + len > MAX_SCREEN_LEN) return -1;

	memcpy(&screen->data[screen->len], s, len);
	screen->len += len;

	return 0;
}

static int place_cursor(ScreenBuffer *screen, uint x, uint y) {
	char s[32];
	int len = snprintf(s, sizeof s, "\x1b[%d;%dH", y + 1, x + 1);
	if (len == -1 || len >= (int)sizeof s) DIE("place_cursor");
	screen_append(screen, s, (uint)len);

	return 0;
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

	mode_len = MIN(mode_len, (int)sizeof mode_buf);

	char cursor_buf[12];
	int cursor_len = snprintf(
		cursor_buf, sizeof cursor_buf - 1, "[%d:%d]", E.cy + 1,
		E.cx + 1);
	if (cursor_len == -1) return -1;
	cursor_len = MIN(cursor_len, (int)sizeof cursor_buf);

	char filename_buf[128];
	int filename_len = snprintf(
		filename_buf, sizeof filename_buf - 1, "%s%s",
		E.filename[0] == '\0' ? "[NO NAME]" : E.filename,
		E.dirty ? " [+]" : "");
	if (filename_len == -1) return -1;
	filename_len = MIN(filename_len, (int)sizeof filename_buf);

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
	int duration_len = snprintf(
		duration_msg, sizeof duration_msg, " %lu us",
		total_microseconds(duration));
	if (duration_len < 0) return 0;

	if ((uint)duration_len >= sizeof duration_msg)
		duration_len = sizeof duration_msg;

	int remaining = (int)E.cols - duration_len;
	if (remaining < 0) return 0;

	size_t message_len = strlen(E.message);
	if (message_len > 0) {
		size_t len = MIN(message_len, (size_t)remaining);
		screen_append(screen, E.message, len);
		remaining -= len;
	}
	while (remaining--) screen_append(screen, " ", 1);
	screen_append(screen, duration_msg, (size_t)duration_len);

	return 0;
}

static uint render(const Line *line, char *dst, const size_t size) {
	uint length = 0;
	for (uint i = 0; i < line->len && i < size - 1; i++)
		if (line->chars[i] == '\t') {
			dst[length++] = E.render_tab_characters[0];
			while (length % TABSTOP)
				dst[length++] = E.render_tab_characters[1];
		} else dst[length++] = line->chars[i];

	return length;
}

static void draw_line(ScreenBuffer *screen, Line *line) {
	if (E.coloff >= line->len) return;
	uint rendered_len = render(line, render_buffer, sizeof(render_buffer));
	uint visible_len = MIN(rendered_len - E.coloff, E.cols);
	screen_append(screen, render_buffer + E.coloff, visible_len);
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

static void refresh_screen(const struct timespec *duration) {
	screen.len = 0;
	editor_scroll();
	screen_append(&screen, "\x1b[?25l", 6); // hide cursor

	place_cursor(&screen, 0, 0);
	draw_lines(&screen, duration);
	place_cursor(&screen, E.rx - E.coloff, E.cy - E.rowoff);

	screen_append(&screen, "\x1b[?25h", 6); // show cursor
	write(STDOUT_FILENO, screen.data, screen.len);
}

static void handle_resize(int sig) {
	(void)sig;
	if (get_window_size(&E.rows, &E.cols) == -1) DIE("get_window_size");
	refresh_screen(NULL);
}

// -------------------------------- File I/O ----------------------------------
static int editor_append_line(const char *chars, uint len) {
	Line *line = insert_line(E.numlines);
	if (!line) return -1;
	return set_line(line, chars, len);
}

static void format_message(const char *restrict format, ...) {
	size_t size = MAX_MESSAGE_LEN;

	va_list ap;
	va_start(ap, format);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
	int len = vsnprintf(E.message, size, format, ap);
#pragma clang diagnostic pop
	if (len < 0) DIE("format_message");

	va_end(ap);
}

static void editor_open(const char *restrict fname) {
	FILE *f = fopen(fname, "r");
	if (!f) DIE("fopen");

	E.numlines = 0;

	char line[MAX_LINE_LEN];

	int c;
	size_t i = 0;
	while ((c = fgetc(f)) != EOF) {
		bool line_too_long = i >= sizeof line;

		if (c == '\n' || line_too_long) {
			if (editor_append_line(line, (uint)i) == -1) break;
			i = 0;
			if (line_too_long)
				while ((c = fgetc(f)) != EOF && c != '\n')
					;
		} else {
			line[i++] = (char)c;
		}
	}

	fclose(f);
	strncpy(E.filename, fname, MAX_FILENAME - 1);
	E.filename[MAX_FILENAME - 1] = '\0';

	format_message("Loaded: \"%s\"", fname);
}

static void editor_save(void) {
	if (E.filename[0] == '\0') return;

	FILE *f = fopen(E.filename, "w");
	if (!f) DIE("fopen");

	for (uint i = 0; i < E.numlines; i++) {
		fputs(E.lines[i].chars, f);
		fputs("\n", f);
	}

	fclose(f);

	format_message("Saved: \"%s\"", E.filename);
	E.dirty = false;
	E.save = false;
}

int main(int argc, char *argv[]) {
	signal(SIGWINCH, handle_resize);
	enable_raw_mode();

	uint rows, cols;
	if (get_window_size(&rows, &cols) == -1) DIE("get_window_size");
	editor_init(rows, cols);

	if (argc >= 2) editor_open(argv[1]);

	struct timespec render_done, input_received = get_current_time();
	struct timespec duration = {0};

	while (true) {
		// Paint
		refresh_screen(&duration);
		render_done = get_current_time();
		duration = elapsed_time(&input_received, &render_done);

		// Process Input
		int key = read_key();
		input_received = get_current_time();
		process_key(key);

		if (E.save) editor_save();
		if (E.quit) quit(E.status);
	}
}
