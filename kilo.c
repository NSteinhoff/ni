/// Kilo is a minimalist text editor written in plain C without external
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

typedef struct Buffer {
	char *data;
	size_t len;
} Buffer;

typedef struct Editor {
	uint cx, cy;
	uint rows, cols;
	struct termios term_orig;
} Editor;

// ---------------------------------- Data ------------------------------------
static Editor E;

// -------------------------------- Terminal ----------------------------------
static NORETURN die(const char *s) {
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
	struct termios term_raw = E.term_orig;
	term_raw.c_iflag &= ~(unsigned)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	term_raw.c_oflag &= ~(unsigned)(OPOST);
	term_raw.c_cflag |= (unsigned)(CS8);
	term_raw.c_lflag &= ~(unsigned)(ECHO | ICANON | ISIG | IEXTEN);
	term_raw.c_cc[VMIN] = 0;
	term_raw.c_cc[VTIME] = 10 / FPS;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &term_raw) == -1) die("tcsetattr");
}

static char read_key(void) {
	char c;
	ssize_t characters_read;
	while ((characters_read = read(STDIN_FILENO, &c, 1) != 1))
		if (characters_read == -1 && errno != EAGAIN) die("read");
	return c;
}

static int get_cursor_position(uint *rows, uint *cols) {
	// solicit device report
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	// read device report
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
	// reset cursor to home
	if (write(STDOUT_FILENO, "\x1b[H", 3) != 3) return -1;
	// move to end
	if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
	if (get_cursor_position(rows, cols) == -1) return -1;

	return 0;
}

// ---------------------------------- Input -----------------------------------
static void process_key(void) {
	char c = read_key();
	switch (c) {
	case CTRL_KEY('q'):
		write(STDOUT_FILENO, "\x1b[2J", 4);
		write(STDOUT_FILENO, "\x1b[H", 3);
		exit(EXIT_SUCCESS);

	case 'j': E.cy < E.rows - 1 && E.cy++; break;
	case 'k': E.cy > 0 && E.cy--; break;
	case 'h': E.cx > 0 && E.cx--; break;
	case 'l': E.cx < E.cols - 1 && E.cx++; break;

	default: break;
	}
}

// --------------------------------- Output -----------------------------------
static int buffer_append(Buffer *buffer, const char *s, size_t len) {
	char *new = realloc(buffer->data,
			    sizeof *buffer->data * (buffer->len + len));
	if (new == NULL) return -1;
	buffer->data = new;

	memcpy(&buffer->data[buffer->len], s, len);
	buffer->len += len;

	return 0;
}

static void buffer_free(Buffer *buffer) {
	free(buffer->data);
}

static void draw_welcome_message(Buffer *buffer) {
	char s[80];
	int len_required = snprintf(s, sizeof s, "Kilo editor -- version %s",
				    KILO_VERSION);
	if (len_required == -1) {
		perror("snprintf");
		exit(EXIT_FAILURE);
	}
	uint len = (uint)len_required > E.cols ? E.cols : (uint)len_required;
	uint padding = (E.cols - len) / 2;
	if (padding) {
		buffer_append(buffer, "~", 1);
		padding--;
	}
	while (padding--) {
		buffer_append(buffer, " ", 1);
	}
	buffer_append(buffer, s, len);
}

static void draw_rows(Buffer *buffer) {
	for (uint y = 0; y < E.rows; y++) {
		if (y == E.rows / 3) draw_welcome_message(buffer);
		else buffer_append(buffer, "~", 1);
		buffer_append(buffer, "\x1b[K", 3); // clear till EOL
		if (y < E.rows - 1) buffer_append(buffer, "\r\n", 2);
	}
}

static int place_cursor(Buffer *buffer, uint x, uint y) {
	char s[32];
	int len_required = snprintf(s, sizeof s, "\x1b[%d;%dH", y + 1, x + 1);
	if (len_required == -1 || len_required >= (int)sizeof s) return -1;
	buffer_append(buffer, s, (uint)len_required);

	return 0;
}

static void refresh_screen(void) {
	Buffer buffer = {0};
	buffer_append(&buffer, "\x1b[?25l", 4); // hide cursor
	place_cursor(&buffer, 0, 0);
	draw_rows(&buffer);
	place_cursor(&buffer, E.cx, E.cy);
	buffer_append(&buffer, "\x1b[?25h", 4); // show cursor
	write(STDOUT_FILENO, buffer.data, buffer.len);
	buffer_free(&buffer);
}

// ---------------------------------- Main ------------------------------------
static void init(void) {
	enable_raw_mode();
	E.cx = 0;
	E.cy = 0;
	if (get_window_size(&E.rows, &E.cols) == -1) die("get_window_size");
}

int main(void) {
	init();

	while (true) {
		refresh_screen();
		process_key();
	}
}
