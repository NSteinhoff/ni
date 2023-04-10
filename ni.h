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

	// Status & Messages
	char message[MAX_MESSAGE_LEN];

	// File
	char filename[MAX_FILENAME];
	bool dirty;

	// Input
	Chord chord;
	Find find;

	// Settings
	char render_tab_characters[2];
} Editor;

extern void editor_init(uint rows, uint cols);
extern void process_key(int key);
extern Line *insert_line(uint at);
extern int set_line(Line* line, const char *chars, uint len);

extern Editor E;
