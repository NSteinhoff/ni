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

typedef enum EditorKey {
	KEY_UP = 1000,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_PAGE_UP,
	KEY_PAGE_DOWN,
} EditorKey;

typedef struct Buffer {
	char *data;
	size_t len;
} Buffer;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct Editor {
	struct termios term_orig;
	uint cx, cy;
	uint rows, cols;
	char mode;
	// 7 bytes of padding
} Editor;
#pragma clang diagnostic pop

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
static void process_key(void) {
	int c = read_key();
	switch (E.mode) {
	case 'n':
		switch (c) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(EXIT_SUCCESS);

		case 'k':
		case KEY_UP: E.cy > 0 && E.cy--; break;
		case 'j':
		case KEY_DOWN: E.cy < E.rows - 1 && E.cy++; break;
		case 'h':
		case KEY_LEFT: E.cx > 0 && E.cx--; break;
		case 'l':
		case KEY_RIGHT: E.cx < E.cols - 1 && E.cx++; break;

		default: break;
		}
		break;
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
	E.mode = 'n';
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
