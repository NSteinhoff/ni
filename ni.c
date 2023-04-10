// -------------------------------- Includes ----------------------------------
#include <ctype.h>   // isblank, isprint, isspace, isalnum
#include <stdarg.h>  // va_list, va_start, va_end
#include <stdbool.h> // bool, true, false
#include <stdio.h>   // vsnprintf
#include <string.h>  // memmove

#include "ni.h"

typedef union LineStorage LineStorage;
union LineStorage {
	LineStorage *next;
	char chars[MAX_LINE_LEN];
};

static LineStorage line_pool[MAX_LINES];
static LineStorage *freelist = NULL;

// --------------------------------- Defines ----------------------------------
#define DIE(MSG)                                                               \
	do {                                                                   \
		E.quit = true;                                                 \
		E.status = 1;                                                  \
		E.error = MSG;                                                 \
	} while (0)

// Mask 00011111 i.e. zero out the upper three bits
#define CTRL_KEY(k) ((k)&0x1f)

// ------------------------------ State & Data --------------------------------
Editor E;

char *get_line_storage(void) {
	if (freelist == NULL) return NULL;
	char *chars = freelist->chars;
	freelist = freelist->next;

	return chars;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-align"
static void free_line_storage(char *chars) {
	LineStorage *next = freelist;

	freelist = (LineStorage *)chars;
	freelist->next = next;
}
#pragma clang diagnostic pop

// --------------------------------- Editing ----------------------------------
static void format_message(const char *restrict format, ...);
static Line *insert_line(size_t at) {
	char *chars = get_line_storage();
	if (chars == NULL) {
		format_message("Maximum number of lines reached.");
		return NULL;
	}

	if (at > E.numlines) at = E.numlines;

	if (at <= LASTLINE)
		memmove(E.lines + at + 1, E.lines + at,
		        (sizeof *E.lines) * (E.numlines - at));

	E.lines[at].chars = chars;
	E.lines[at].len = 0;

	E.numlines++;
	E.dirty = true;

	return E.lines + at;
}

static void delete_line(uint at) {
	if (NOLINES) return;
	if (at >= E.numlines) at = LASTLINE;

	free_line_storage(E.lines[at].chars);

	if (at < LASTLINE)
		memmove(E.lines + at, E.lines + at + 1,
		        (sizeof *E.lines) * (E.numlines - (at + 1)));

	E.numlines--;

	E.dirty = true;
}

static Line *split_line(uint at, uint split_at) {
	if (NOLINES) return NULL;
	if (at >= E.numlines) return NULL;
	if (split_at >= E.lines[at].len) return NULL;

	Line *dst = insert_line(at + 1);
	if (dst == NULL) return NULL;

	Line *src = E.lines + at;
	dst->len = src->len - split_at;
	strncpy(dst->chars, src->chars + split_at, dst->len);

	// Shorten original line by len
	src->len -= dst->len;

	E.dirty = true;

	return dst;
}

static void join_lines(uint at) {
	if (E.numlines <= 1) return;
	if (at >= E.numlines) at = LASTLINE;

	Line *const dst = E.lines + at;
	const Line *const src = dst + 1;

	if (src->len > 0) {
		const bool add_space = !isspace(src->chars[0]) &&
		                       !isspace(dst->chars[dst->len - 1]);
		if (add_space) dst->len++;

		if (add_space) dst->chars[dst->len - 1] = ' ';
		memmove(dst->chars + dst->len, src->chars, src->len);

		dst->len += src->len;
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
	if (line->len < MAX_LINE_LEN) line->len++;

	memmove(&line->chars[at + 1], &line->chars[at], line->len - at - 1);

	line->chars[at] = c;

	E.dirty = true;
}

static void delete_chars(uint at, uint n, Line *line) {
	if (line->len == 0) return;
	if (at >= line->len) return;
	uint end = at + n;

	memmove(&line->chars[at], &line->chars[end], line->len - end);

	line->len -= n;

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

static void show_file_info(void) {
	if (E.numlines > 0)
		format_message(
			"\"%s\" %d lines, --%.0f%%--",
			E.filename[0] == '\0' ? E.filename : "[NO NAME]",
			E.numlines,
			((double)E.cy + 1) / (double)E.numlines * 100);
	else
		format_message(
			"\"%s\" --No lines in buffer--",
			E.filename[0] == '\0' ? E.filename : "[NO NAME]");
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
	if (!E.numlines)
		if (!insert_line(0)) return;

	switch (c) {
	case 'a':
		if (CLINE->len > 0) E.cx++;
		break;
	case 'A': E.cx = MIN(CLINE->len, MAX_LINE_LEN - 1); break;
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

static void quit(int status) {
	E.status = status;
	E.quit = true;
}

static void process_key_normal(const int c) {
	E.chord.keys[E.chord.len++] = (char)c;
	if (E.chord.len == 1) {
		switch (c) {
		case 'q': quit(0); return;
		case CTRL_KEY('q'): quit(1); return;
		case CTRL_KEY('s'): E.save = true; break;
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
			if (insert_line(E.cy)) {
				E.cx = 0;
				E.mode = MODE_INSERT;
			}
			break;
		case 'o':
			if (insert_line(NOLINES ? 0 : E.cy + 1)) {
				E.cx = 0;
				if (E.cy != LASTLINE) E.cy++;
				E.mode = MODE_INSERT;
			}
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
			case 'Z':
				E.save = true;
				quit(0);
				return;
			case 'Q': quit(1); return;
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
			case '0':
				delete_chars(0, E.cx, CLINE);
				E.cx = 0;
				break;
			case '$':
				delete_chars(E.cx, CLINE->len - E.cx, CLINE);
				if (E.cx > 0) E.cx--;
				break;
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
			case '0':
				delete_chars(0, E.cx, CLINE);
				E.cx = 0;
				break;
			case '$':
				delete_chars(E.cx, CLINE->len - E.cx, CLINE);
				break;
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
		if (split_line(E.cy, E.cx)) {
			E.cy++;
			E.cx = 0;
		}
		break;

	default:
		if (isprint(c) || isblank(c)) {
			line_insert_char(CLINE, E.cx, (char)c);
			if (E.cx < ENDOFLINE) E.cx++;
		}
		break;
	}
}

void process_key(int key) {
	switch (E.mode) {
	case MODE_NORMAL: {
		process_key_normal(key);
		if (E.mode == MODE_NORMAL) cursor_normalize();
	} break;
	case MODE_INSERT: {
		process_key_insert(key);
	} break;
	}
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
	E.message.len = MIN((size_t)len, size - 1);

	va_end(ap);
}
#pragma clang diagnostic pop

// ---------------------------------- Main ------------------------------------
static void lines_init(void) {
	for (size_t i = 0; i < MAX_LINES - 1; i++)
		line_pool[i].next = line_pool + i + 1;

	freelist = line_pool;
}
void editor_init(uint rows, uint cols) {
	E.quit = false;
	E.error = NULL;
	E.rows = rows;
	E.cols = cols;
	E.mode = MODE_NORMAL;
	E.filename[0] = '\0';
	E.numlines = 0;
	E.rowoff = E.coloff = 0;
	E.cx = E.cy = E.rx = 0;
	E.render_tab_characters[0] = '>';
	E.render_tab_characters[1] = '-';
	E.message.len = 0;
	E.dirty = false;
	E.chord.len = 0;
	E.find.c = 0;

	lines_init();
}
