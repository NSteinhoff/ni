/*** includes ***/
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define FPS 1
#define TENTHS_OF_SEC 10 / FPS
#define NORETURN __attribute__((noreturn)) void
// Mask 00011111 i.e. zero out the upper three bits
#define CTRL_KEY(k)  ((k) &  0x1f)

/*** declarations ***/

static void die(const char *s);
static void enable_raw_mode(void);
static void reset_term(void);

/*** data ***/

static struct termios term_orig;

/*** main ***/

int main(void) {
	enable_raw_mode();

	char c;
	while (true) {
		c = 0;
		ssize_t characters_read = read(STDIN_FILENO, &c, 1);
		if (characters_read == -1 && errno != EAGAIN) die("read");

		if (iscntrl(c)) printf("%d\r\n", c);
		else printf("%d ('%c')\r\n", c, c);

		if (c == CTRL_KEY('q')) break;
	}

	return 0;
}

/*** terminal ***/

static NORETURN die(const char *s) {
	perror(s);
	exit(1);
}

static void enable_raw_mode(void) {
	// Save terminal settings and setup cleanup on exit.
	if (tcgetattr(STDIN_FILENO, &term_orig) == -1) die("tcgetattr");
	atexit(reset_term);

	// Set terminal to raw mode.
	struct termios term_raw = term_orig;
	term_raw.c_iflag &= ~(unsigned)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	term_raw.c_oflag &= ~(unsigned)(OPOST);
	term_raw.c_cflag |= (unsigned)(CS8);
	term_raw.c_lflag &= ~(unsigned)(ECHO | ICANON | ISIG | IEXTEN);
	term_raw.c_cc[VMIN] = 0;
	term_raw.c_cc[VTIME] = TENTHS_OF_SEC;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &term_raw) == -1) die("tcsetattr");
}

static void reset_term(void) {
	if (tcsetattr(STDIN_FILENO, TCSANOW, &term_orig) == -1)
		die("tcsetattr");
}
