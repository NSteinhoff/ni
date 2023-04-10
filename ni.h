#define NI_VERSION "0.0.1"

#define TABSTOP 8
#define NUM_UTIL_LINES 2

#define MAX_LINES 4096
#define MAX_LINE_LEN 4096
#define MAX_FILENAME 256
#define MAX_SCREEN_LEN 1 << 16
#define MAX_MESSAGE_LEN 256
#define MAX_CHORD 3 // d[ge] or d[f..]
#define MAX_RENDER 1024

#define MIN(A, B) (A) <= (B) ? (A) : (B)

// Common constructs
#define CLINE (E.lines + E.cy)
#define NOLINES (E.numlines == 0)
#define LASTLINE (E.numlines - 1)
#define ENDOFLINE (CLINE->len - 1)

// ---------------------------------- Types -----------------------------------
typedef unsigned int uint;

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
	bool quit, save;
	int status;
	char *error;

	// Lines
	Line lines[MAX_LINES];
	uint numlines;

	// Cursor
	uint cx, cy, rx;

	// Viewport
	uint rowoff, coloff;
	uint rows, cols;

	// Mode
	EditorMode mode;

	// File
	char filename[MAX_FILENAME];
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

extern void process_key(int key);
extern void editor_init(uint rows, uint cols);
extern char *get_line_storage(void);

extern Editor E;
