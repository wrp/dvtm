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
struct position {
	/* relative position and size */
	/* Values between 0 and 1, indicating fraction of total available */
	double offset;
	double portion;
};
struct abs_position {
	/* absolute position and size */
	unsigned short y, x;   /* position of upper left corner */
	unsigned short h, w;   /* height and width */
};
/*
 * A window is a specified chunk of screen which contains either at most
 * one client or one layout (the layout can contain multiple windows).
 */
struct window {
	struct position p;  /* relative to enclosing layout */
	struct client *c;
	struct layout *layout;
	struct layout *enclosing_layout;
	struct window *next;
};
/*
 * A layout is a set of windows, each having either the same height
 * (a row layout) or the same width (a column layout).
*/
struct layout {
	enum { undetermined, col_layout, row_layout } type;
	struct window *lwindows;
	size_t count;
};
struct view {
	/* TODO: move vclients to struct state.  Or, rather,
	keep a global pool in struct state, and have the values
	here reference them */
	struct client **vclients; /* NULL terminated array */
	unsigned capacity;
	struct layout *layout;
	struct window *vfocus;
	char name[32];
};
/*
 * struct state is the global state.
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
	struct view *views;
	unsigned viewcount;
	struct view *current_view;
	const char *shell;
};

/*
   Consider the following, in which the screen (10 rows) is split
   once vertically, each half is split horizontally once and twice,
   respectively, and the focus is on the window marked x.  eg:

   +--------------------------------+
   |                |               |
   |                +---------------+
   |                |               |
   |                |               |
   +----------------+       x       |
   |                |               |
   |                +---------------+
   |                |               |
   |                |               |
   +--------------------------------+

   Here, *state.current_view->layout is a row layout with two windows, described
   by "1x.5@0,0 1x.5@0,.5" (in any window of a row layout, y == 0, h == 1, and
   the sum of w's == 1).  Also, state.current_view->layout.windows->client == NULL,
   while *state.current_view->layout.windows->layout is a column layout with 3 windows
   described by ".2x1@0,0 .5x1@.2,0 .3x1@.7,0"

   Much of this info is redundant.  A layout can be fully described
   by a sequence of floats.  In above case, the top level is a row layout
   row:.5,.5.  The first component window contains col:.5,.5 and the second
   window contains col:.2,.5,.7

*/

struct client {
	WINDOW *window;
	Vt *term;
	Vt *editor, *app;
	int editor_fds[2];
	volatile sig_atomic_t editor_died;
	const char *cmd;
	struct abs_position p;
	char title[128];
	char editor_title[128];
	pid_t pid;
	unsigned short int id;
	bool urgent;
	volatile sig_atomic_t died;
	struct window *win;
};


struct client* nextvisible(struct client *c);
void focus(struct client *c);
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
/* characters for beginning and end of status bar message */
#define BAR_BEGIN       '['
#define BAR_END         ']'
/* scroll back buffer size in lines */
#define SCROLL_HISTORY 500
#define TAG_COUNT    8
#define TAG_SYMBOL   "[%d]" /* format string for the tag in the status bar */
#define TAG_SEL      (COLOR(BLUE) | A_BOLD)       /* attributes for the currently selected tags */
#define TAG_NORMAL   (COLOR(DEFAULT) | A_NORMAL)  /* attributes for unselected empty tags */

/* commands for use by keybindings */
command bind;
command change_kill_signal;
command copymode;
command create;
command digit;
command focusn;
command focus_transition;
command focusnext;
command killclient;
command paste;
command quit;
command redraw;
command scrollback;
command send;
command toggleview;
command split;
command toggle_mode;
command transition_no_send;
command transition_with_send;
command toggle_borders;
