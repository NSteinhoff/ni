/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define NORETURN __attribute__((noreturn)) void

/*** declarations ***/

static void die(const char *s);
static void enable_raw_mode(void);
static void reset_term(void);

/*** data ***/

static struct termios term_orig;

/*** main ***/

int main(void) {
	enable_raw_mode();

	for (char c = 0; c != 'q';) {
		c = 0;
		if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
			die("read");
		if (iscntrl(c)) printf("%d\r\n", c);
		else printf("%d ('%c')\r\n", c, c);
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
	term_raw.c_cc[VTIME] = 1;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &term_raw) == -1) die("tcsetattr");
}

static void reset_term(void) {
	if (tcsetattr(STDIN_FILENO, TCSANOW, &term_orig) == -1)
		die("tcsetattr");
}
