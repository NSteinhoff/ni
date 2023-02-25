/*** includes ***/
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define FPS 1
#define NORETURN __attribute__((noreturn)) void
// Mask 00011111 i.e. zero out the upper three bits
#define CTRL_KEY(k) ((k)&0x1f)

/*** typedefs ***/
typedef unsigned int uint;

/*** declarations ***/

typedef struct Buffer {
	char *data;
	size_t len;
} Buffer;

typedef struct Editor {
	uint rows, cols;
	struct termios term_orig;
} Editor;

/*** data ***/

static Editor E;

/*** terminal ***/

static NORETURN die(const char *s) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	perror(s);
	exit(1);
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

static int move_cursor(uint drow, uint dcol) {
	char buf[32];
	int chars_printed =
		snprintf(buf, sizeof buf, "\x1b[%dC\x1b[%dB", drow, dcol);
	if (chars_printed == -1 || chars_printed >= (int)sizeof buf) return -1;
	if (write(STDOUT_FILENO, buf, (size_t)chars_printed) != chars_printed)
		return -1;

	return 0;
}

static int get_cursor_position(uint *rows, uint *cols) {
	// Solicit device report
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	char buf[32];
	uint i = 0;
	while (i < sizeof buf - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) return -1;
		if (buf[i++] == 'R') break;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[1], "[%d;%dR", rows, cols) != 2)
		return -1;

	return 0;
}

static int get_window_size(uint *rows, uint *cols) {
	move_cursor(999, 999);
	if (get_cursor_position(rows, cols) == -1) return -1;

	return 0;
}

/*** input ***/

static void process_key(void) {
	char c = read_key();
	switch (c) {
	case CTRL_KEY('q'):
		write(STDOUT_FILENO, "\x1b[2J", 4);
		write(STDOUT_FILENO, "\x1b[H", 3);
		exit(0);
	default: break;
	}
}

/*** output ***/

static void buffer_append(Buffer *buffer, const char *s, size_t len) {
	char *new = realloc(buffer->data,
			    sizeof *buffer->data * (buffer->len + len));
	if (new == NULL) return;
	buffer->data = new;

	memcpy(&buffer->data[buffer->len], s, len);
	buffer->len += len;
}

static void buffer_free(Buffer *buffer) {
	free(buffer->data);
}

static void draw_rows(Buffer *buffer) {
	for (uint y = 0; y < E.rows; y++) {
		buffer_append(buffer, "~", 1);
		if (y < E.rows - 1)
			buffer_append(buffer, "\r\n", 2);
	}
}

static void refresh_screen(void) {
	Buffer buffer = {0};
	buffer_append(&buffer, "\x1b[?25l", 4);
	buffer_append(&buffer, "\x1b[2J", 4);
	buffer_append(&buffer, "\x1b[H", 3);
	draw_rows(&buffer);
	buffer_append(&buffer, "\x1b[H", 3);
	buffer_append(&buffer, "\x1b[?25h", 4);
	write(STDOUT_FILENO, buffer.data, buffer.len);
	buffer_free(&buffer);
}

/*** main ***/

static void init(void) {
	enable_raw_mode();
	if (get_window_size(&E.rows, &E.cols) == -1) die("get_window_size");
}

int main(void) {
	init();

	while (true) {
		refresh_screen();
		process_key();
	}
}
