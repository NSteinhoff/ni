#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

static struct termios term_orig;

static void enable_raw_mode(void);
static void reset_term(void);

int main(void) {
	enable_raw_mode();

	char c;
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q')
		;

	return 0;
}

static void reset_term(void) {
	tcsetattr(STDIN_FILENO, TCSANOW, &term_orig);
}

static void enable_raw_mode(void) {
	// Save terminal settings and setup cleanup on exit.
	tcgetattr(STDIN_FILENO, &term_orig);
	atexit(reset_term);

	// Set terminal to raw mode.
	struct termios term_raw = term_orig;
	term_raw.c_lflag &= ~(unsigned)(ECHO);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &term_raw);
}
