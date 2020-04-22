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

struct entry_buf {
	unsigned char data[128];;
	const struct key_binding *binding;
	int count;
	unsigned char *next; /* first unused char in data */
};
/*
 struct state is the global state.  Currently, not much is here.  I intend
 to move global objects into here as I manipulate the code and learn then
 architecture.
 */
struct state {
	enum { keypress_mode, command_mode } mode;
	struct entry_buf buf; /* user entered keys in command_mode */
	int runinall;
	int signal;  /* Signal sent by killclient */
};

struct client {
	WINDOW *window;
	Vt *term;
	Vt *editor, *app;
	int editor_fds[2];
	volatile sig_atomic_t editor_died;
	const char *cmd;
	char title[255];
	int order;
	pid_t pid;
	unsigned short int id;
	unsigned short int x;
	unsigned short int y;
	unsigned short int w;
	unsigned short int h;
	bool has_title_line;
	bool minimized;
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

#define MAX_ARGS 8
#ifndef CTRL
# define CTRL(k)   ((k) & 0x1F)
#endif

typedef void (command)(const char * const args[]);
extern command * get_function(const char *name);
#define MAX_KEYS 3
struct action {
	command *cmd;
	const char *args[MAX_KEYS];
	struct action *next;
};

struct key_binding {
	struct action action;
	struct key_binding *next;
};
extern int parse_binding(struct action *a, const char *d);
extern char *binding_desc[][5];

struct command {
	const char *name;
	struct action action;
};

struct statusbar {
	int fd;
	int hidden;
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
void bind(const char * const args[]);
void change_kill_signal(const char *const args[]);
void copymode(const char * const args[]);
void create(const char * const args[]);
void focusdown(const char * const args[]);
void focusid(const char * const args[]);
void focuslast(const char * const args[]);
void focusleft(const char * const args[]);
void focusn(const char * const args[]);
void focusnext(const char * const args[]);
void focusnextnm(const char * const args[]);
void focusprev(const char * const args[]);
void focusprevnm(const char * const args[]);
void focusright(const char * const args[]);
void focusup(const char * const args[]);
void incnmaster(const char * const args[]);
void killclient(const char * const args[]);
void paste(const char * const args[]);
void quit(const char * const args[]);
void redraw(const char * const args[]);
void scrollback(const char * const args[]);
void send(const char * const args[]);
void setmfact(const char * const args[]);
void startup(const char * const args[]);
void tag(const char * const args[]);
void togglebar(const char * const args[]);
void togglebarpos(const char * const args[]);
void toggleminimize(const char * const args[]);
void togglerunall(const char * const args[]);
void toggletag(const char * const args[]);
void toggleview(const char * const args[]);
void untag(const char * const args[]);
void view(const char * const args[]);
void viewprevtag(const char * const args[]);
void zoom(const char *const args[]);
void fullscreen(void);

enum { DEFAULT, BLUE, RED };
