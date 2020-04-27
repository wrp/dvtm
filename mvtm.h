#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <wchar.h>
#include <limits.h>
#include <libgen.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <curses.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <pwd.h>
#if HAVE_TERMIOS_H
# include <termios.h>
#endif
#include "vt.h"

struct screen {
	float mfact;
	unsigned int nmaster;
	int history;
	int w;
	int h;
	int winched;
};
struct client;
struct entry_buf {
	unsigned char data[128];;
	int count;
	unsigned char *next; /* first unused char in data */
};
enum window_description_type { relative, absolute };
struct rel_window {
	/* relative position and size */
	/* Values between 0 and 1, indicating fraction of total available */
	float y, x;  /* position of upper left corner */
	float h, w;  /* height and width */
};
struct abs_window {
	/* absolute position and size */
	unsigned short y, x;   /* position of upper left corner */
	unsigned short h, w;   /* height and width */
};
/* A window is a specified chunk of screen which contains at most 1 client */
struct window {
	struct rel_window relative;
	struct abs_window absolute;
	struct window *next;
	struct window *prev;
	struct client *c;
};
struct client_list {
	struct client *c;
	struct client_list *next;
};
/*
 * A layout is a loosely coupled list of windows and clients.
 * When rendering a layout, any window without an
 * associated client will be filled with a filler character.
*/
struct layout {
	struct window *w;
	struct layout *next;
};
/* A view is a history of layouts. */
struct view {
	struct layout *layout;
	struct client_list *cl;
	char name[64];
};
/*
 struct state is the global state.  Currently, not much is here.  I intend
 to move global objects into here as I manipulate the code and learn then
 architecture.
 */
enum mode { keypress_mode, command_mode };
struct state {
	enum mode mode;
	int code;  /* The last code returned by getch() */
	struct entry_buf buf; /* user entered keys in command_mode */
	const struct key_binding *binding;
	int signal;  /* Signal sent by killclient */
	int runinall;
	int hide_borders;
	struct view views[8];
	struct view *current_view;
};

struct client {
	WINDOW *window;
	Vt *term;
	Vt *editor, *app;
	int editor_fds[2];
	volatile sig_atomic_t editor_died;
	const char *cmd;
	char title[128];
	char editor_title[128];
	int order;
	pid_t pid;
	unsigned short int id;
	unsigned short y, x;  /* position of upper left corner */
	unsigned short h, w;  /* height and width */
	bool has_title_line;
	bool urgent;
	volatile sig_atomic_t died;
	struct client *next;
	struct client *prev;
	struct client *snext;
	unsigned int tags;
};


struct client* nextvisible(struct client *c);
void focus(struct client *c);
void resize(struct client *c, int x, int y, int w, int h);
extern struct screen screen;
extern unsigned available_width, available_height;
extern struct client *clients;
extern char *title;

struct color {
	short fg;
	short bg;
	short fg256;
	short bg256;
	short pair;
};

struct color_rule {
	const char *title;
	attr_t attrs;
	struct color *color;
};

#define ESC 0x1b

/*
 * MAX_BIND is maximum length of a key binding
 * Keybindings are handled by building a tree with fanout 256,
 * and depth equal to the max length of a key.  Keep MAX_BIND
 * low to prevent an explosion.  (It's not a full tree, and the
 * max depth is only attained by the long keys.)
 */
#define MAX_BIND 5
typedef char * binding_description[MAX_BIND];

/*
 * MAX_ARGS is the maximum number of args passed to a command
 * TODO: get rid of this.  Use a char ** in struct action.
 */
#define MAX_ARGS 3
#ifndef CTRL
# define CTRL(k)   ((k) & 0x1F)
#endif

typedef int (command)(const char * const args[]);
extern command * get_function(const char *name);
struct action {
	command *cmd;
	const char *args[MAX_ARGS];
	struct action *next;
};

struct key_binding {
	struct action action;
	struct key_binding *next;
};
extern int parse_binding(struct action *a, const char *d);
extern char *mod_bindings[][MAX_BIND];
extern char *keypress_bindings[][MAX_BIND];

struct command {
	const char *name;
	struct action action;
};

struct statusbar {
	int fd;
	unsigned short int h;
	unsigned short int y;
	char text[512];
	const char *file;
};

typedef struct {
	int fd;
	const char *file;
	unsigned short int id;
} CmdFifo;

struct data_buffer {
	char *data;
	size_t len;
	size_t size;
};

typedef struct {
	char *name;
	const char *argv[4];
	bool filter;
	bool color;
} Editor;

#define LENGTH(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MAX(x, y)   ((x) > (y) ? (x) : (y))
#define MIN(x, y)   ((x) < (y) ? (x) : (y))

#ifdef DEBUG
 #define debug eprint
#else
 #define debug(format, args...)
#endif


#ifdef PDCURSES
int ESCDELAY;
#endif

#ifndef NCURSES_REENTRANT
# define set_escdelay(d) (ESCDELAY = (d))
#endif

#define COLOR(c)        COLOR_PAIR(colors[c].pair)
/* curses attributes for normal (not selected) windows */
#define NORMAL_ATTR     (COLOR(DEFAULT) | A_NORMAL)
/* curses attributes for a window with pending urgent flag */
#define URGENT_ATTR     NORMAL_ATTR
/* curses attributes for the status bar */
#define BAR_ATTR        (COLOR(BLUE) | A_NORMAL)
/* characters for beginning and end of status bar message */
#define BAR_BEGIN       '['
#define BAR_END         ']'
/* master width factor [0.1 .. 0.9] */
#define MFACT 0.8
/* number of clients in master area */
#define NMASTER 1
/* scroll back buffer size in lines */
#define SCROLL_HISTORY 500
#define TAG_COUNT    8
#define TAG_SYMBOL   "[%d]" /* format string for the tag in the status bar */
#define TAG_SEL      (COLOR(BLUE) | A_BOLD)       /* attributes for the currently selected tags */
#define TAG_NORMAL   (COLOR(DEFAULT) | A_NORMAL)  /* attributes for unselected empty tags */
#define TAG_OCCUPIED (COLOR(RED) | A_NORMAL)      /* attributes for unselected nonempty tags */
#define TAG_URGENT (COLOR(RED) | A_NORMAL | A_BLINK) /* attributes for unselected tags with urgent windows */

/* commands for use by keybindings */
command bind;
command change_kill_signal;
command copymode;
command create;
command digit;
command focusdown;
command focusid;
command focuslast;
command focusleft;
command focusn;
command focusnext;
command focusnextnm;
command focusprev;
command focusprevnm;
command focusright;
command focusup;
command incnmaster;
command killclient;
command paste;
command quit;
command redraw;
command scrollback;
command send;
command setmfact;
command startup;
command tag;
command togglebar;
command togglebarpos;
command toggleminimize;
command togglerunall;
command toggletag;
command toggleview;
command untag;
command view;
command viewprevtag;
command zoom;
command toggle_mode;
command transition_no_send;
command transition_with_send;
command toggle_borders;
