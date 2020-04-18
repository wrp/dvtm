#include <assert.h>
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

typedef struct {
	const char *symbol;
	void (*arrange)(void);
} Layout;

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

/* functions and variables available to layouts */
struct client* nextvisible(struct client *c);
void focus(struct client *c);
void resize(struct client *c, int x, int y, int w, int h);
extern struct screen screen;
extern unsigned waw, wah, wax, way;
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

struct action {
	void (*cmd)(const char *args[]);
	const char *args[3];
};

#define MAX_KEYS 3
struct key_binding {
	unsigned keys[MAX_KEYS];
	struct action action;
};
extern struct key_binding bindings[];
extern size_t key_binding_length;

typedef struct {
	mmask_t mask;
	struct action action;
} Button;

struct command {
	const char *name;
	struct action action;
};

enum { BAR_TOP, BAR_BOTTOM, BAR_OFF };

typedef struct {
	int fd;
	int pos, lastpos;
	bool autohide;
	unsigned short int h;
	unsigned short int y;
	char text[512];
	const char *file;
} StatusBar;

typedef struct {
	int fd;
	const char *file;
	unsigned short int id;
} CmdFifo;

typedef struct {
	char *data;
	size_t len;
	size_t size;
} Register;

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
/* curses attributes for the currently focused window */
#define SELECTED_ATTR   (COLOR(BLUE) | A_NORMAL)
/* curses attributes for normal (not selected) windows */
#define NORMAL_ATTR     (COLOR(DEFAULT) | A_NORMAL)
/* curses attributes for a window with pending urgent flag */
#define URGENT_ATTR     NORMAL_ATTR
/* curses attributes for the status bar */
#define BAR_ATTR        (COLOR(BLUE) | A_NORMAL)
/* characters for beginning and end of status bar message */
#define BAR_BEGIN       '['
#define BAR_END         ']'
/* status bar (command line option -s) position */
#define BAR_POS         BAR_TOP /* BAR_BOTTOM, BAR_OFF */
/* whether status bar should be hidden if only one client exists */
#define BAR_AUTOHIDE    true
/* master width factor [0.1 .. 0.9] */
#define MFACT 0.8
/* number of clients in master area */
#define NMASTER 1
/* scroll back buffer size in lines */
#define SCROLL_HISTORY 500
/* printf format string for the tag in the status bar */
#define TAG_SYMBOL   "[%d]"
/* curses attributes for the currently selected tags */
#define TAG_SEL      (COLOR(BLUE) | A_BOLD)
/* curses attributes for not selected tags which contain no windows */
#define TAG_NORMAL   (COLOR(DEFAULT) | A_NORMAL)
/* curses attributes for not selected tags which contain windows */
#define TAG_OCCUPIED (COLOR(RED) | A_NORMAL)
/* curses attributes for not selected tags which with urgent windows */
#define TAG_URGENT (COLOR(RED) | A_NORMAL | A_BLINK)

/* commands for use by keybindings */
void create(const char *args[]);
void copymode(const char *args[]);
void focusn(const char *args[]);
void focusid(const char *args[]);
void focusnext(const char *args[]);
void focusnextnm(const char *args[]);
void focusprev(const char *args[]);
void focusprevnm(const char *args[]);
void focuslast(const char *args[]);
void focusup(const char *args[]);
void focusdown(const char *args[]);
void focusleft(const char *args[]);
void focusright(const char *args[]);
void killclient(const char *args[]);
void paste(const char *args[]);
void quit(const char *args[]);
void redraw(const char *args[]);
void scrollback(const char *args[]);
void send(const char *args[]);
void setlayout(const char *args[]);
void incnmaster(const char *args[]);
void setmfact(const char *args[]);
void startup(const char *args[]);
void tag(const char *args[]);
void tagid(const char *args[]);
void togglebar(const char *args[]);
void togglebarpos(const char *args[]);
void toggleminimize(const char *args[]);
void togglerunall(const char *args[]);
void toggletag(const char *args[]);
void toggleview(const char *args[]);
void untag(const char *args[]);
void viewprevtag(const char *args[]);
void view(const char *args[]);
void zoom(const char *args[]);
void fullscreen(void);

enum { DEFAULT, BLUE, RED };
