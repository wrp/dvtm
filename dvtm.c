/*
 * The initial "port" of dwm to curses was done by
 *
 * © 2007-2016 Marc André Tanner <mat at brain-dump dot org>
 *
 * It is highly inspired by the original X11 dwm and
 * reuses some code of it which is mostly
 *
 * © 2006-2007 Anselm R. Garbe <garbeam at gmail dot com>
 *
 * See LICENSE for details.
 */

#include "config.h"
#include "dvtm.h"

/* commands for use by keybindings */
static void create(const char *args[]);
static void copymode(const char *args[]);
static void focusn(const char *args[]);
static void focusid(const char *args[]);
static void focusnext(const char *args[]);
static void focusnextnm(const char *args[]);
static void focusprev(const char *args[]);
static void focusprevnm(const char *args[]);
static void focuslast(const char *args[]);
static void focusup(const char *args[]);
static void focusdown(const char *args[]);
static void focusleft(const char *args[]);
static void focusright(const char *args[]);
static void killclient(const char *args[]);
static void paste(const char *args[]);
static void quit(const char *args[]);
static void redraw(const char *args[]);
static void scrollback(const char *args[]);
static void send(const char *args[]);
static void setlayout(const char *args[]);
static void incnmaster(const char *args[]);
static void setmfact(const char *args[]);
static void startup(const char *args[]);
static void tag(const char *args[]);
static void tagid(const char *args[]);
static void togglebar(const char *args[]);
static void togglebarpos(const char *args[]);
static void toggleminimize(const char *args[]);
static void togglerunall(const char *args[]);
static void toggletag(const char *args[]);
static void toggleview(const char *args[]);
static void untag(const char *args[]);
static void viewprevtag(const char *args[]);
static void view(const char *args[]);
static void zoom(const char *args[]);
static void fullscreen(void);

/* valid curses attributes are listed below they can be ORed
 *
 * A_NORMAL        Normal display (no highlight)
 * A_STANDOUT      Best highlighting mode of the terminal.
 * A_UNDERLINE     Underlining
 * A_REVERSE       Reverse video
 * A_BLINK         Blinking
 * A_DIM           Half bright
 * A_BOLD          Extra bright or bold
 * A_PROTECT       Protected mode
 * A_INVIS         Invisible or blank mode
 */

enum {
	DEFAULT,
	BLUE,
	RED,
};

static Color colors[] = {
	[DEFAULT] = { .fg = -1,         .bg = -1, .fg256 = -1, .bg256 = -1, },
	[BLUE]    = { .fg = COLOR_BLUE, .bg = -1, .fg256 = 68, .bg256 = -1, },
	[RED]     = { .fg = COLOR_RED,  .bg = -1, .fg256 = 68, .bg256 = -1, },
};

const int tags = 8;

extern void wstack(void);
/* by default the first layout entry is used */
static Layout layouts[] = {
	{ "---", wstack },
	{ "[ ]", fullscreen },
};

unsigned modifier_key = CTRL('g');

#define TAGKEYS(KEY,TAG) \
	{ { 'v', KEY,     }, { view,           { #TAG }               } }, \
	{ { 't', KEY,     }, { tag,            { #TAG }               } }, \
	{ { 'V', KEY,     }, { toggleview,     { #TAG }               } }, \
	{ { 'T', KEY,     }, { toggletag,      { #TAG }               } }

/* you can specifiy at most 3 arguments */
static KeyBinding bindings[] = {
	{ { 'c',          }, { create,         { NULL, NULL, "master" }    } },
	{ { 'C',          }, { create,         { NULL, NULL, "$CWD" }      } },
	{ { 'x', 'x',     }, { killclient,     { NULL }                    } },
	{ { 'j',          }, { focusnext,      { NULL }                    } },
	{ { 'J',          }, { focusdown,      { NULL }                    } },
	{ { 'K',          }, { focusup,        { NULL }                    } },
	{ { 'H',          }, { focusleft,      { NULL }                    } },
	{ { 'L',          }, { focusright,     { NULL }                    } },
	{ { 'k',          }, { focusprev,      { NULL }                    } },
	{ { 'f',          }, { setlayout,      { "---" }                   } },
	{ { 'm',          }, { setlayout,      { "[ ]" }                   } },
	{ { ' ',          }, { setlayout,      { NULL }                    } },
	{ { 'i',          }, { incnmaster,     { "+1" }                    } },
	{ { 'd',          }, { incnmaster,     { "-1" }                    } },
	{ { 'h',          }, { setmfact,       { "-0.05" }                 } },
	{ { 'l',          }, { setmfact,       { "+0.05" }                 } },
	{ { '.',          }, { toggleminimize, { NULL }                    } },
	{ { 's',          }, { togglebar,      { NULL }                    } },
	{ { 'S',          }, { togglebarpos,   { NULL }                    } },
	{ { '\n',         }, { zoom ,          { NULL }                    } },
	{ { '\r',         }, { zoom ,          { NULL }                    } },
	{ { '1',          }, { focusn,         { "1" }                     } },
	{ { '2',          }, { focusn,         { "2" }                     } },
	{ { '3',          }, { focusn,         { "3" }                     } },
	{ { '4',          }, { focusn,         { "4" }                     } },
	{ { '5',          }, { focusn,         { "5" }                     } },
	{ { '6',          }, { focusn,         { "6" }                     } },
	{ { '7',          }, { focusn,         { "7" }                     } },
	{ { '8',          }, { focusn,         { "8" }                     } },
	{ { '9',          }, { focusn,         { "9" }                     } },
	{ { '\t',         }, { focuslast,      { NULL }                    } },
	{ { 'q', 'q',     }, { quit,           { NULL }                    } },
	{ { 'a',          }, { togglerunall,   { NULL }                    } },
	{ { CTRL('L'),    }, { redraw,         { NULL }                    } },
	{ { 'r',          }, { redraw,         { NULL }                    } },
	{ { 'e',          }, { copymode,       { "dvtm-editor" }           } },
	{ { 'E',          }, { copymode,       { "dvtm-pager" }            } },
	{ { '/',          }, { copymode,       { "dvtm-pager", "/" }       } },
	{ { 'p',          }, { paste,          { NULL }                    } },
	{ { KEY_PPAGE,    }, { scrollback,     { "-1" }                    } },
	{ { KEY_NPAGE,    }, { scrollback,     { "1"  }                    } },
	{ { '?',          }, { create,         { "man dvtm", "dvtm help" } } },
	{ { 'u',          }, { scrollback,     { "-1" }                    } },
	{ { 'n',          }, { scrollback,     { "1"  }                    } },
	{ { '0',          }, { view,           { NULL }                    } },
	{ { 'v', '0'      }, { view,           { NULL }                    } },
	{ { 'v', '\t',    }, { viewprevtag,    { NULL }                    } },
	{ { 't', '0'      }, { tag,            { NULL }                    } },
	{ { 'T', '0'      }, { untag,          { NULL }                    } },
	TAGKEYS( '1', 1),
	TAGKEYS( '2', 2),
	TAGKEYS( '3', 3),
	TAGKEYS( '4', 4),
	TAGKEYS( '5', 5),
	TAGKEYS( '6', 6),
	TAGKEYS( '7', 7),
	TAGKEYS( '8', 8),
};

static const ColorRule colorrules[] = {
	{ "", A_NORMAL, &colors[DEFAULT] }, /* default */
};



static Cmd commands[] = {
	/* create [cmd]: create a new window, run `cmd` in the shell if specified */
	{ "create", { create,	{ NULL } } },
	/* focus <win_id>: focus the window whose `DVTM_WINDOW_ID` is `win_id` */
	{ "focus",  { focusid,	{ NULL } } },
	/* tag <win_id> <tag> [tag ...]: add +tag, remove -tag or set tag of the window with the given identifier */
	{ "tag",    { tagid,	{ NULL } } },
};

static char const * const keytable[] = {
	/* add your custom key escape sequences */
};

/* forward declarations of internal functions */
static void cleanup(void);
static void push_action(const Action *a);

unsigned waw, wah, wax, way;
Client *clients = NULL;
char *title;

/* global variables */
static Action *actions = NULL; /* actions are executed when dvtm is started */

static const char *dvtm_name = "dvtm";
Screen screen = { .mfact = MFACT, .nmaster = NMASTER, .history = SCROLL_HISTORY };
static Client *stack = NULL;
static Client *sel = NULL;
static Client *lastsel = NULL;
static Client *msel = NULL;
static unsigned int seltags;
static unsigned int tagset[2] = { 1, 1 };
static Layout *layout = layouts;
static StatusBar bar = { .fd = -1, .lastpos = BAR_POS, .pos = BAR_POS, .autohide = BAR_AUTOHIDE, .h = 1 };
static CmdFifo cmdfifo = { .fd = -1 };
static const char *shell;
static Register copyreg;
static volatile sig_atomic_t running = true;
static bool runinall = false;
static int sigwinch_pipe[] = {-1, -1};
static int sigchld_pipe[] = {-1, -1};

enum {PIPE_RD, PIPE_WR};

static void
eprint(const char *errstr, ...) {
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
}

static void
error(const char *errstr, ...) {
	cleanup();
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static bool
isarrange(void (*func)()) {
	return func == layout->arrange;
}

static bool
isvisible(Client *c) {
	return c->tags & tagset[seltags];
}

static bool
is_content_visible(Client *c) {
	if (!c)
		return false;
	if (isarrange(fullscreen))
		return sel == c;
	return isvisible(c) && !c->minimized;
}

Client*
nextvisible(Client *c) {
	for (; c && !isvisible(c); c = c->next);
	return c;
}

static void
updatebarpos(void) {
	bar.y = 0;
	wax = 0;
	way = 0;
	wah = screen.h;
	waw = screen.w;
	if (bar.pos == BAR_TOP) {
		wah -= bar.h;
		way += bar.h;
	} else if (bar.pos == BAR_BOTTOM) {
		wah -= bar.h;
		bar.y = wah;
	}
}

static void
hidebar(void) {
	if (bar.pos != BAR_OFF) {
		bar.lastpos = bar.pos;
		bar.pos = BAR_OFF;
	}
}

static void
showbar(void) {
	if (bar.pos == BAR_OFF)
		bar.pos = bar.lastpos;
}

static void
drawbar(void) {
	int sx, sy, x, y, width;
	unsigned int occupied = 0, urgent = 0;
	if (bar.pos == BAR_OFF)
		return;

	for (Client *c = clients; c; c = c->next) {
		occupied |= c->tags;
		if (c->urgent)
			urgent |= c->tags;
	}

	getyx(stdscr, sy, sx);
	attrset(BAR_ATTR);
	move(bar.y, 0);

	for (unsigned int i = 0; i < tags; i++){
		if (tagset[seltags] & (1 << i))
			attrset(TAG_SEL);
		else if (urgent & (1 << i))
			attrset(TAG_URGENT);
		else if (occupied & (1 << i))
			attrset(TAG_OCCUPIED);
		else
			attrset(TAG_NORMAL);
		printw(TAG_SYMBOL, i + 1);
	}

	attrset(runinall ? TAG_SEL : TAG_NORMAL);
	addstr(layout->symbol);
	attrset(TAG_NORMAL);

	getyx(stdscr, y, x);
	(void)y;
	int maxwidth = screen.w - x - 2;

	addch(BAR_BEGIN);
	attrset(BAR_ATTR);

	wchar_t wbuf[sizeof bar.text];
	size_t numchars = mbstowcs(wbuf, bar.text, sizeof bar.text);

	if (numchars != (size_t)-1 && (width = wcswidth(wbuf, maxwidth)) != -1) {
		int pos;
		for (pos = 0; pos + width < maxwidth; pos++)
			addch(' ');

		for (size_t i = 0; i < numchars; i++) {
			pos += wcwidth(wbuf[i]);
			if (pos > maxwidth)
				break;
			addnwstr(wbuf+i, 1);
		}

		clrtoeol();
	}

	attrset(TAG_NORMAL);
	mvaddch(bar.y, screen.w - 1, BAR_END);
	attrset(NORMAL_ATTR);
	move(sy, sx);
	wnoutrefresh(stdscr);
}

static int
show_border(void) {
	return (bar.pos != BAR_OFF) || (clients && clients->next);
}

static void
draw_border(Client *c) {
	char t = '\0';
	int x, y, maxlen, attrs = NORMAL_ATTR;
	char tagmsg[128] = "";
	char location[128] = "";

	if (!show_border())
		return;
	if (sel != c && c->urgent)
		attrs = URGENT_ATTR;
	if (sel == c || (runinall && !c->minimized))
		attrs = SELECTED_ATTR;

	wattrset(c->window, attrs);
	getyx(c->window, y, x);
	mvwhline(c->window, 0, 0, ACS_HLINE, c->w);
	maxlen = c->w - 10;
	if (maxlen < 0)
		maxlen = 0;
	if ((size_t)maxlen < sizeof(c->title)) {
		t = c->title[maxlen];
		c->title[maxlen] = '\0';
	}

	sprintf(location, " %dx%d@(%d,%d)", c->h, c->w, c->y, c->x);

	if(c->tags) {
		unsigned mask = 0x1;
		int first = 1;
		for( int i=0; i < tags; i++, mask <<= 1 ) {
			if( (c->tags & mask) != 0) {
				char b[32];
				sprintf(b, "%s%d", first ? "" : ",", i + 1);
				strcat(tagmsg, b);
				first = 0;
			}
		}
	}

	mvwprintw(
		c->window, 0, 2, "[%s%s#%d (%d:%ld)%s %s]",
		*c->title ? c->title : "",
		*c->title ? " | " : "",
		c->order,
		c->id,
		(long)c->pid,
#ifdef WRPDEBUG
		location,
#else
		"",
#endif
		tagmsg
	);
	if (t)
		c->title[maxlen] = t;
	wmove(c->window, y, x);
}

static void
draw_content(Client *c) {
	vt_draw(c->term, c->window, c->has_title_line, 0);
}

static void
draw(Client *c) {
	if (is_content_visible(c)) {
		redrawwin(c->window);
		draw_content(c);
	}
	if (!isarrange(fullscreen) || sel == c)
		draw_border(c);
	wnoutrefresh(c->window);
}

static void
draw_all(void) {
	if (!nextvisible(clients)) {
		sel = NULL;
		curs_set(0);
		erase();
		drawbar();
		doupdate();
		return;
	}

	if (!isarrange(fullscreen)) {
		for (Client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
			if (c != sel)
				draw(c);
		}
	}
	/* as a last step the selected window is redrawn,
	 * this has the effect that the cursor position is
	 * accurate
	 */
	if (sel)
		draw(sel);
}

static void
arrange(void) {
	unsigned int m = 0, n = 0;
	for (Client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
		c->order = ++n;
		if (c->minimized)
			m++;
	}
	erase();
	attrset(NORMAL_ATTR);
	if (bar.fd == -1 && bar.autohide) {
		if ((!clients || !clients->next) && n == 1)
			hidebar();
		else
			showbar();
		updatebarpos();
	}
	if (m && !isarrange(fullscreen))
		wah--;
	layout->arrange();
	if (m && !isarrange(fullscreen)) {
		unsigned int i = 0, nw = waw / m, nx = wax;
		for (Client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
			if (c->minimized) {
				resize(c, nx, way+wah, ++i == m ? waw - nx : nw, 1);
				nx += nw;
			}
		}
		wah++;
	}
	focus(NULL);
	wnoutrefresh(stdscr);
	drawbar();
	draw_all();
}

static void
attach(Client *c) {
	if (clients)
		clients->prev = c;
	c->next = clients;
	c->prev = NULL;
	clients = c;
	for (int o = 1; c; c = nextvisible(c->next), o++)
		c->order = o;
}

static void
attachafter(Client *c, Client *a) { /* attach c after a */
	if (c == a)
		return;
	if (!a)
		for (a = clients; a && a->next; a = a->next);

	if (a) {
		if (a->next)
			a->next->prev = c;
		c->next = a->next;
		c->prev = a;
		a->next = c;
		for (int o = a->order; c; c = nextvisible(c->next))
			c->order = ++o;
	}
}

static void
attachstack(Client *c) {
	c->snext = stack;
	stack = c;
}

static void
detach(Client *c) {
	Client *d;
	if (c->prev)
		c->prev->next = c->next;
	if (c->next) {
		c->next->prev = c->prev;
		for (d = nextvisible(c->next); d; d = nextvisible(d->next))
			--d->order;
	}
	if (c == clients)
		clients = c->next;
	c->next = c->prev = NULL;
}

static void
settitle(Client *c) {
	char *term, *t = title;
	if (!t && sel == c && *c->title)
		t = c->title;
	if (t && (term = getenv("TERM")) && !strstr(term, "linux")) {
		printf("\033]0;%s\007", t);
		fflush(stdout);
	}
}

static void
detachstack(Client *c) {
	Client **tc;
	for (tc = &stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;
}

void
focus(Client *c) {
	if (!c)
		for (c = stack; c && !isvisible(c); c = c->snext);
	if (sel == c)
		return;
	lastsel = sel;
	sel = c;
	if (lastsel) {
		lastsel->urgent = false;
		if (!isarrange(fullscreen)) {
			draw_border(lastsel);
			wnoutrefresh(lastsel->window);
		}
	}

	if (c) {
		detachstack(c);
		attachstack(c);
		settitle(c);
		c->urgent = false;
		if (isarrange(fullscreen)) {
			draw(c);
		} else {
			draw_border(c);
			wnoutrefresh(c->window);
		}
	}
	curs_set(c && !c->minimized && vt_cursor_visible(c->term));
}

static void
applycolorrules(Client *c) {
	const ColorRule *r = colorrules;
	short fg = r->color->fg, bg = r->color->bg;
	attr_t attrs = r->attrs;

	for (unsigned int i = 1; i < LENGTH(colorrules); i++) {
		r = &colorrules[i];
		if (strstr(c->title, r->title)) {
			attrs = r->attrs;
			fg = r->color->fg;
			bg = r->color->bg;
			break;
		}
	}

	vt_default_colors_set(c->term, attrs, fg, bg);
}

static void
term_title_handler(Vt *term, const char *title) {
	Client *c = (Client *)vt_data_get(term);
	if (title)
		strncpy(c->title, title, sizeof(c->title) - 1);
	c->title[title ? sizeof(c->title) - 1 : 0] = '\0';
	settitle(c);
	if (!isarrange(fullscreen) || sel == c)
		draw_border(c);
	applycolorrules(c);
}

static void
term_urgent_handler(Vt *term) {
	Client *c = (Client *)vt_data_get(term);
	c->urgent = true;
	printf("\a");
	fflush(stdout);
	drawbar();
	if (!isarrange(fullscreen) && sel != c && isvisible(c))
		draw_border(c);
}

static void
move_client(Client *c, int x, int y) {
	if (c->x == x && c->y == y)
		return;
	debug("moving, x: %d y: %d\n", x, y);
	if (mvwin(c->window, y, x) == ERR) {
		eprint("error moving, x: %d y: %d\n", x, y);
	} else {
		c->x = x;
		c->y = y;
	}
}

static void
resize_client(Client *c, int w, int h) {
	bool has_title_line = show_border();
	bool resize_window = c->w != w || c->h != h;
	if (resize_window) {
		debug("resizing, w: %d h: %d\n", w, h);
		if (wresize(c->window, h, w) == ERR) {
			eprint("error resizing, w: %d h: %d\n", w, h);
		} else {
			c->w = w;
			c->h = h;
		}
	}
	if (resize_window || c->has_title_line != has_title_line) {
		c->has_title_line = has_title_line;
		vt_resize(c->app, h - has_title_line, w);
		if (c->editor)
			vt_resize(c->editor, h - has_title_line, w);
	}
}

void
resize(Client *c, int x, int y, int w, int h) {
	resize_client(c, w, h);
	move_client(c, x, y);
}

static Client*
get_client_by_coord(unsigned int x, unsigned int y) {
	if (y < way || y >= way+wah)
		return NULL;
	if (isarrange(fullscreen))
		return sel;
	for (Client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h) {
			return c;
		}
	}
	return NULL;
}

static void
sigchld_handler(int sig) {
	write(sigchld_pipe[PIPE_WR], "\0", 1);
}

static void
handle_sigchld() {
	int errsv = errno;
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG)) != 0) {
		if (pid == -1) {
			if (errno == ECHILD) {
				/* no more child processes */
				break;
			}
			eprint("waitpid: %s\n", strerror(errno));
			break;
		}

		debug("child with pid %d died\n", pid);

		for (Client *c = clients; c; c = c->next) {
			if (c->pid == pid) {
				c->died = true;
				break;
			}
			if (c->editor && vt_pid_get(c->editor) == pid) {
				c->editor_died = true;
				break;
			}
		}
	}

	errno = errsv;
}

static void
sigwinch_handler(int sig) {
	write(sigwinch_pipe[PIPE_WR], "\0", 1);
}

static void
handle_sigwinch() {
	screen.need_resize = true;
}

static void
sigterm_handler(int sig) {
	running = false;
}

static void
resize_screen(void) {
	struct winsize ws;

	if (ioctl(0, TIOCGWINSZ, &ws) == -1) {
		getmaxyx(stdscr, screen.h, screen.w);
	} else {
		screen.w = ws.ws_col;
		screen.h = ws.ws_row;
	}

	debug("resize_screen(), w: %d h: %d\n", screen.w, screen.h);

	resizeterm(screen.h, screen.w);
	wresize(stdscr, screen.h, screen.w);
	updatebarpos();
	clear();
	arrange();
}

static KeyBinding*
keybinding(KeyCombo keys, unsigned int keycount) {
	/* TODO: stop doing a linear search on all bindings for
	   every keystroke. */
	KeyBinding *b = bindings;
	KeyBinding *e = bindings + LENGTH(bindings);
	for( ; b < e; b++) {
		unsigned k = 1;
		for (; k < keycount; k++) {
			if (keys[k] != b->keys[k-1])
				break;
		}
		if (k == keycount)
			return b;
	}
	return NULL;
}

static unsigned int
bitoftag(const char *tag) {
	unsigned t = tag ? 0 : ~0;
	if( tag && strchr("123456789", *tag) ) {
		t = 1 << (*tag - '1');
	}
	return t;
}

static void
tagschanged() {
	bool allminimized = true;
	for (Client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (!c->minimized) {
			allminimized = false;
			break;
		}
	}
	if (allminimized && nextvisible(clients)) {
		focus(NULL);
		toggleminimize(NULL);
	}
	arrange();
}


static void
untag(const char *args[]) {
	if( sel ) {
		sel->tags = 1;
		tagschanged();
	}
}

static void
tag(const char *args[]) {
	if (!sel)
		return;
	sel->tags |= bitoftag(args[0]);
	tagschanged();
}

static void
tagid(const char *args[]) {
	if (!args[0] || !args[1])
		return;

	const int win_id = atoi(args[0]);
	for (Client *c = clients; c; c = c->next) {
		if (c->id == win_id) {
			unsigned int ntags = c->tags;
			for (unsigned int i = 1; i < MAX_ARGS && args[i]; i++) {
				if (args[i][0] == '+')
					ntags |= bitoftag(args[i]+1);
				else if (args[i][0] == '-')
					ntags &= ~bitoftag(args[i]+1);
				else
					ntags = bitoftag(args[i]);
			}
			if (ntags) {
				c->tags = ntags;
				tagschanged();
			}
			return;
		}
	}
}

static void
toggletag(const char *args[]) {
	if (!sel)
		return;
	unsigned int newtags = sel->tags ^ (bitoftag(args[0]));
	if (newtags) {
		sel->tags = newtags;
		tagschanged();
	}
}

static void
toggleview(const char *args[]) {
	unsigned int newtagset = tagset[seltags] ^ (bitoftag(args[0]));
	if (newtagset) {
		tagset[seltags] = newtagset;
		tagschanged();
	}
}

static void
view(const char *args[]) {
	unsigned int newtagset = bitoftag(args[0]);
	if (tagset[seltags] != newtagset && newtagset) {
		seltags ^= 1; /* toggle sel tagset */
		tagset[seltags] = newtagset;
		tagschanged();
	}
}

static void
viewprevtag(const char *args[]) {
	seltags ^= 1;
	tagschanged();
}

static void
keypress(int code) {
	int key = -1;
	unsigned int len = 1;
	char buf[8] = { '\e' };

	if (code == '\e') {
		/* pass characters following escape to the underlying app */
		nodelay(stdscr, TRUE);
		for (int t; len < sizeof(buf) && (t = getch()) != ERR; len++) {
			if (t > 255) {
				key = t;
				break;
			}
			buf[len] = t;
		}
		nodelay(stdscr, FALSE);
	}

	for (Client *c = runinall ? nextvisible(clients) : sel; c; c = nextvisible(c->next)) {
		if (is_content_visible(c)) {
			c->urgent = false;
			if (code == '\e')
				vt_write(c->term, buf, len);
			else
				vt_keypress(c->term, code);
			if (key != -1)
				vt_keypress(c->term, key);
		}
		if (!runinall)
			break;
	}
}


static bool
checkshell(const char *shell) {
	if (shell == NULL || *shell == '\0' || *shell != '/')
		return false;
	if (!strcmp(strrchr(shell, '/')+1, dvtm_name))
		return false;
	if (access(shell, X_OK))
		return false;
	return true;
}

static const char *
getshell(void) {
	const char *shell = getenv("SHELL");
	struct passwd *pw;

	if (checkshell(shell))
		return shell;
	if ((pw = getpwuid(getuid())) && checkshell(pw->pw_shell))
		return pw->pw_shell;
	return "/bin/sh";
}

static bool
set_blocking(int fd, bool blocking) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return false;
	flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
	return !fcntl(fd, F_SETFL, flags);
}

static void
setup(void) {
	shell = getshell();
	setlocale(LC_CTYPE, "");
	initscr();
	start_color();
	noecho();
	nonl();
	keypad(stdscr, TRUE);
	raw();
	vt_init();
	vt_keytable_set(keytable, LENGTH(keytable));
	for (unsigned int i = 0; i < LENGTH(colors); i++) {
		if (COLORS == 256) {
			if (colors[i].fg256)
				colors[i].fg = colors[i].fg256;
			if (colors[i].bg256)
				colors[i].bg = colors[i].bg256;
		}
		colors[i].pair = vt_color_reserve(colors[i].fg, colors[i].bg);
	}
	resize_screen();

	int *pipes[] = {&sigwinch_pipe[0], &sigchld_pipe[0]};
	for (int i = 0; i < 2; ++i) {
		int r = pipe(pipes[i]);
		if (r < 0) {
			perror("pipe()");
			exit(EXIT_FAILURE);
		}

		for (int j = 0; j < 2; ++j) {
			if (!set_blocking(pipes[i][j], false)) {
				perror("fcntl()");
				exit(EXIT_FAILURE);
			}
		}
	}

	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = sigwinch_handler;
	sigaction(SIGWINCH, &sa, NULL);
	sa.sa_handler = sigchld_handler;
	sigaction(SIGCHLD, &sa, NULL);
	sa.sa_handler = sigterm_handler;
	sigaction(SIGTERM, &sa, NULL);
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);
}

static void
destroy(Client *c) {
	if (sel == c)
		focusnextnm(NULL);
	detach(c);
	detachstack(c);
	if (sel == c) {
		Client *next = nextvisible(clients);
		if (next) {
			focus(next);
			toggleminimize(NULL);
		} else {
			sel = NULL;
		}
	}
	if (lastsel == c)
		lastsel = NULL;
	werase(c->window);
	wnoutrefresh(c->window);
	vt_destroy(c->term);
	delwin(c->window);
	if (!clients && actions) {
		if (!strcmp(c->cmd, shell))
			quit(NULL);
		else
			create(NULL);
	}
	free(c);
	arrange();
}

static void
cleanup(void) {
	while (clients)
		destroy(clients);
	vt_shutdown();
	endwin();
	free(copyreg.data);
	if (bar.fd > 0)
		close(bar.fd);
	if (bar.file)
		unlink(bar.file);
	if (cmdfifo.fd > 0)
		close(cmdfifo.fd);
	if (cmdfifo.file)
		unlink(cmdfifo.file);
}

static char *getcwd_by_pid(Client *c) {
	if (!c)
		return NULL;
	char buf[32];
	snprintf(buf, sizeof buf, "/proc/%d/cwd", c->pid);
	return realpath(buf, NULL);
}

static void
create(const char *args[]) {
	const char *pargs[4] = { shell, NULL };
	char buf[8], *cwd = NULL;
	const char *env[] = {
		"DVTM_WINDOW_ID", buf,
		NULL
	};

	if (args && args[0]) {
		pargs[1] = "-c";
		pargs[2] = args[0];
		pargs[3] = NULL;
	}
	Client *c = calloc(1, sizeof(Client));
	if (!c)
		return;
	c->tags = tagset[seltags];
	c->id = ++cmdfifo.id;
	snprintf(buf, sizeof buf, "%d", c->id);

	if (!(c->window = newwin(wah, waw, way, wax))) {
		free(c);
		return;
	}

	c->term = c->app = vt_create(screen.h, screen.w, screen.history);
	if (!c->term) {
		delwin(c->window);
		free(c);
		return;
	}

	if (args && args[0]) {
		c->cmd = args[0];
		char name[PATH_MAX];
		strncpy(name, args[0], sizeof(name));
		name[sizeof(name)-1] = '\0';
		strncpy(c->title, basename(name), sizeof(c->title));
	} else {
		c->cmd = shell;
	}

	if (args && args[1])
		strncpy(c->title, args[1], sizeof(c->title));
	c->title[sizeof(c->title)-1] = '\0';

	if (args && args[2])
		cwd = !strcmp(args[2], "$CWD") ? getcwd_by_pid(sel) : (char*)args[2];

	c->pid = vt_forkpty(c->term, shell, pargs, cwd, env, NULL, NULL);
	if (args && args[2] && !strcmp(args[2], "$CWD"))
		free(cwd);
	vt_data_set(c->term, c);
	vt_title_handler_set(c->term, term_title_handler);
	vt_urgent_handler_set(c->term, term_urgent_handler);
	applycolorrules(c);
	c->x = wax;
	c->y = way;
	debug("client with pid %d forked\n", c->pid);
	attach(c);
	focus(c);
	arrange();

	if( args && args[2] && ! strcmp(args[2], "master") ) {
		const char *args[2] = { "+1", NULL };
		incnmaster(args);
	}
}

static void
copymode(const char *args[]) {
	if (!args || !args[0] || !sel || sel->editor)
		return;

	bool colored = strstr(args[0], "pager") != NULL;

	if (!(sel->editor = vt_create(sel->h - sel->has_title_line, sel->w, 0)))
		return;

	int *to = &sel->editor_fds[0];
	int *from = strstr(args[0], "editor") ? &sel->editor_fds[1] : NULL;
	sel->editor_fds[0] = sel->editor_fds[1] = -1;

	const char *argv[3] = { args[0], NULL, NULL };
	char argline[32];
	int line = vt_content_start(sel->app);
	snprintf(argline, sizeof(argline), "+%d", line);
	argv[1] = argline;

	if (vt_forkpty(sel->editor, args[0], argv, NULL, NULL, to, from) < 0) {
		vt_destroy(sel->editor);
		sel->editor = NULL;
		return;
	}

	sel->term = sel->editor;

	if (sel->editor_fds[0] != -1) {
		char *buf = NULL;
		size_t len = vt_content_get(sel->app, &buf, colored);
		char *cur = buf;
		while (len > 0) {
			ssize_t res = write(sel->editor_fds[0], cur, len);
			if (res < 0) {
				if (errno == EAGAIN || errno == EINTR)
					continue;
				break;
			}
			cur += res;
			len -= res;
		}
		free(buf);
		close(sel->editor_fds[0]);
		sel->editor_fds[0] = -1;
	}

	if (args[1])
		vt_write(sel->editor, args[1], strlen(args[1]));
}

static void
focusn(const char *args[]) {
	for (Client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (c->order == atoi(args[0])) {
			focus(c);
			if (c->minimized)
				toggleminimize(NULL);
			return;
		}
	}
}

static void
focusid(const char *args[]) {
	if (!args[0])
		return;

	const int win_id = atoi(args[0]);
	for (Client *c = clients; c; c = c->next) {
		if (c->id == win_id) {
			focus(c);
			if (c->minimized)
				toggleminimize(NULL);
			if (!isvisible(c)) {
				c->tags |= tagset[seltags];
				tagschanged();
			}
			return;
		}
	}
}

static void
focusnext(const char *args[]) {
	Client *c;
	if (!sel)
		return;
	for (c = sel->next; c && !isvisible(c); c = c->next);
	if (!c)
		for (c = clients; c && !isvisible(c); c = c->next);
	if (c)
		focus(c);
}

static void
focusnextnm(const char *args[]) {
	if (!sel)
		return;
	Client *c = sel;
	do {
		c = nextvisible(c->next);
		if (!c)
			c = nextvisible(clients);
	} while (c->minimized && c != sel);
	focus(c);
}

static void
focusprev(const char *args[]) {
	Client *c;
	if (!sel)
		return;
	for (c = sel->prev; c && !isvisible(c); c = c->prev);
	if (!c) {
		for (c = clients; c && c->next; c = c->next);
		for (; c && !isvisible(c); c = c->prev);
	}
	if (c)
		focus(c);
}

static void
focusprevnm(const char *args[]) {
	if (!sel)
		return;
	Client *c = sel;
	do {
		for (c = c->prev; c && !isvisible(c); c = c->prev);
		if (!c) {
			for (c = clients; c && c->next; c = c->next);
			for (; c && !isvisible(c); c = c->prev);
		}
	} while (c && c != sel && c->minimized);
	focus(c);
}

static void
focuslast(const char *args[]) {
	if (lastsel)
		focus(lastsel);
}

static void
focusup(const char *args[]) {
	if (!sel)
		return;
	/* avoid vertical separator, hence +1 in x direction */
	Client *c = get_client_by_coord(sel->x + 1, sel->y - 1);
	if (c)
		focus(c);
	else
		focusprev(args);
}

static void
focusdown(const char *args[]) {
	if (!sel)
		return;
	Client *c = get_client_by_coord(sel->x, sel->y + sel->h);
	if (c)
		focus(c);
	else
		focusnext(args);
}

static void
focusleft(const char *args[]) {
	if (!sel)
		return;
	Client *c = get_client_by_coord(sel->x - 2, sel->y);
	if (c)
		focus(c);
	else
		focusprev(args);
}

static void
focusright(const char *args[]) {
	if (!sel)
		return;
	Client *c = get_client_by_coord(sel->x + sel->w + 1, sel->y);
	if (c)
		focus(c);
	else
		focusnext(args);
}

static void
killclient(const char *args[]) {
	if (!sel)
		return;
	debug("killing client with pid: %d\n", sel->pid);
	kill(-sel->pid, SIGKILL);
}

static void
paste(const char *args[]) {
	if (sel && copyreg.data)
		vt_write(sel->term, copyreg.data, copyreg.len);
}

static void
quit(const char *args[]) {
	cleanup();
	exit(EXIT_SUCCESS);
}

static void
redraw(const char *args[]) {
	for (Client *c = clients; c; c = c->next) {
		if (!c->minimized) {
			vt_dirty(c->term);
			wclear(c->window);
			wnoutrefresh(c->window);
		}
	}
	resize_screen();
}

static void
scrollback(const char *args[]) {
	if (!is_content_visible(sel))
		return;

	if (!args[0] || atoi(args[0]) < 0)
		vt_scroll(sel->term, -sel->h/2);
	else
		vt_scroll(sel->term,  sel->h/2);

	draw(sel);
	curs_set(vt_cursor_visible(sel->term));
}

static void
send(const char *args[]) {
	if (sel && args && args[0])
		vt_write(sel->term, args[0], strlen(args[0]));
}

static void
setlayout(const char *args[]) {
	unsigned int i;

	if (!args || !args[0]) {
		if (++layout == &layouts[LENGTH(layouts)])
			layout = &layouts[0];
	} else {
		for (i = 0; i < LENGTH(layouts); i++)
			if (!strcmp(args[0], layouts[i].symbol))
				break;
		if (i == LENGTH(layouts))
			return;
		layout = &layouts[i];
	}
	arrange();
}

static void
incnmaster(const char *args[]) {
	int delta;

	/* arg handling, manipulate nmaster */
	if (args[0] == NULL) {
		screen.nmaster = NMASTER;
	} else if (sscanf(args[0], "%d", &delta) == 1) {
		if (args[0][0] == '+' || args[0][0] == '-')
			screen.nmaster += delta;
		else
			screen.nmaster = delta;
		if (screen.nmaster < 1)
			screen.nmaster = 1;
	}
	arrange();
}

static void
setmfact(const char *args[]) {
	float delta;

	/* arg handling, manipulate mfact */
	if (args[0] == NULL) {
		screen.mfact = MFACT;
	} else if (sscanf(args[0], "%f", &delta) == 1) {
		if (args[0][0] == '+' || args[0][0] == '-')
			screen.mfact += delta;
		else
			screen.mfact = delta;
		if (screen.mfact < 0.1)
			screen.mfact = 0.1;
		else if (screen.mfact > 0.9)
			screen.mfact = 0.9;
	}
	arrange();
}

static void
startup(const char *args[]) {
	Action *a = actions;
	for( ; a && a->cmd; a++ ) {
		a->cmd(a->args);
	}
}

static void
togglebar(const char *args[]) {
	if (bar.pos == BAR_OFF)
		showbar();
	else
		hidebar();
	bar.autohide = false;
	updatebarpos();
	redraw(NULL);
}

static void
togglebarpos(const char *args[]) {
	switch (bar.pos == BAR_OFF ? bar.lastpos : bar.pos) {
	case BAR_TOP:
		bar.pos = BAR_BOTTOM;
		break;
	case BAR_BOTTOM:
		bar.pos = BAR_TOP;
		break;
	}
	updatebarpos();
	redraw(NULL);
}

static void
toggleminimize(const char *args[]) {
	Client *c, *m, *t;
	unsigned int n;
	if (!sel)
		return;
	/* the last window can't be minimized */
	if (!sel->minimized) {
		for (n = 0, c = nextvisible(clients); c; c = nextvisible(c->next))
			if (!c->minimized)
				n++;
		if (n == 1)
			return;
	}
	sel->minimized = !sel->minimized;
	m = sel;
	/* check whether the master client was minimized */
	if (sel == nextvisible(clients) && sel->minimized) {
		c = nextvisible(sel->next);
		detach(c);
		attach(c);
		focus(c);
		detach(m);
		for (; c && (t = nextvisible(c->next)) && !t->minimized; c = t);
		attachafter(m, c);
	} else if (m->minimized) {
		/* non master window got minimized move it above all other
		 * minimized ones */
		focusnextnm(NULL);
		detach(m);
		for (c = nextvisible(clients); c && (t = nextvisible(c->next)) && !t->minimized; c = t);
		attachafter(m, c);
	} else { /* window is no longer minimized, move it to the master area */
		vt_dirty(m->term);
		detach(m);
		attach(m);
	}
	arrange();
}

static void
togglerunall(const char *args[]) {
	runinall = !runinall;
	drawbar();
	draw_all();
}

static void
zoom(const char *args[]) {
	Client *c;

	if (!sel)
		return;
	if (args && args[0])
		focusn(args);
	if ((c = sel) == nextvisible(clients))
		if (!(c = nextvisible(c->next)))
			return;
	detach(c);
	attach(c);
	focus(c);
	if (c->minimized)
		toggleminimize(NULL);
	arrange();
}


static Cmd *
get_cmd_by_name(const char *name) {
	for (unsigned int i = 0; i < LENGTH(commands); i++) {
		if (!strcmp(name, commands[i].name))
			return &commands[i];
	}
	return NULL;
}

static void
handle_cmdfifo(void) {
	int r;
	char *p, *s, cmdbuf[512], c;
	Cmd *cmd;

	r = read(cmdfifo.fd, cmdbuf, sizeof cmdbuf - 1);
	if (r <= 0) {
		cmdfifo.fd = -1;
		return;
	}

	cmdbuf[r] = '\0';
	p = cmdbuf;
	while (*p) {
		/* find the command name */
		for (; *p == ' ' || *p == '\n'; p++);
		for (s = p; *p && *p != ' ' && *p != '\n'; p++);
		if ((c = *p))
			*p++ = '\0';
		if (*s && (cmd = get_cmd_by_name(s)) != NULL) {
			bool quote = false;
			int argc = 0;
			const char *args[MAX_ARGS], *arg;
			memset(args, 0, sizeof(args));
			/* if arguments were specified in config.h ignore the one given via
			 * the named pipe and thus skip everything until we find a new line
			 */
			if (cmd->action.args[0] || c == '\n') {
				debug("execute %s", s);
				cmd->action.cmd(cmd->action.args);
				while (*p && *p != '\n')
					p++;
				continue;
			}
			/* no arguments were given in config.h so we parse the command line */
			while (*p == ' ')
				p++;
			arg = p;
			for (; (c = *p); p++) {
				switch (*p) {
				case '\\':
					/* remove the escape character '\\' move every
					 * following character to the left by one position
					 */
					switch (p[1]) {
						case '\\':
						case '\'':
						case '\"': {
							char *t = p+1;
							do {
								t[-1] = *t;
							} while (*t++);
						}
					}
					break;
				case '\'':
				case '\"':
					quote = !quote;
					break;
				case ' ':
					if (!quote) {
				case '\n':
						/* remove trailing quote if there is one */
						if (*(p - 1) == '\'' || *(p - 1) == '\"')
							*(p - 1) = '\0';
						*p++ = '\0';
						/* remove leading quote if there is one */
						if (*arg == '\'' || *arg == '\"')
							arg++;
						if (argc < MAX_ARGS)
							args[argc++] = arg;

						while (*p == ' ')
							++p;
						arg = p--;
					}
					break;
				}

				if (c == '\n' || *p == '\n') {
					if (!*p)
						p++;
					debug("execute %s", s);
					for(int i = 0; i < argc; i++)
						debug(" %s", args[i]);
					debug("\n");
					cmd->action.cmd(args);
					break;
				}
			}
		}
	}
}


static void
handle_statusbar(void) {
	char *p;
	int r;
	switch (r = read(bar.fd, bar.text, sizeof bar.text - 1)) {
		case -1:
			strncpy(bar.text, strerror(errno), sizeof bar.text - 1);
			bar.text[sizeof bar.text - 1] = '\0';
			bar.fd = -1;
			break;
		case 0:
			bar.fd = -1;
			break;
		default:
			bar.text[r] = '\0';
			p = bar.text + r - 1;
			for (; p >= bar.text && *p == '\n'; *p-- = '\0');
			for (; p >= bar.text && *p != '\n'; --p);
			if (p >= bar.text)
				memmove(bar.text, p + 1, strlen(p));
			drawbar();
	}
}

static void
handle_editor(Client *c) {
	if (!copyreg.data && (copyreg.data = malloc(screen.history)))
		copyreg.size = screen.history;
	copyreg.len = 0;
	while (c->editor_fds[1] != -1 && copyreg.len < copyreg.size) {
		ssize_t len = read(c->editor_fds[1], copyreg.data + copyreg.len, copyreg.size - copyreg.len);
		if (len == -1) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (len == 0)
			break;
		copyreg.len += len;
		if (copyreg.len == copyreg.size) {
			copyreg.size *= 2;
			if (!(copyreg.data = realloc(copyreg.data, copyreg.size))) {
				copyreg.size = 0;
				copyreg.len = 0;
			}
		}
	}
	c->editor_died = false;
	c->editor_fds[1] = -1;
	vt_destroy(c->editor);
	c->editor = NULL;
	c->term = c->app;
	vt_dirty(c->term);
	draw_content(c);
	wnoutrefresh(c->window);
}

static int
open_or_create_fifo(const char *name, const char **name_created, const char *env_name)
{
	struct stat info;
	char *abs_name;
	int fd;

	do {
		if ((fd = open(name, O_RDWR|O_NONBLOCK)) == -1) {
			if (errno == ENOENT && !mkfifo(name, S_IRUSR|S_IWUSR)) {
				*name_created = name;
				continue;
			}
			error("%s\n", strerror(errno));
		}
	} while (fd == -1);

	if (fstat(fd, &info) == -1) {
		error("%s\n", strerror(errno));
	} else if (!S_ISFIFO(info.st_mode)) {
		error("%s is not a named pipe\n", name);
	}

	if( ( abs_name = realpath(name, NULL)) == NULL ) {
		error("%s: %s\n", name, strerror(errno));
	}
	setenv(env_name, abs_name, 1);
	free(abs_name);
	return fd;
}


static void
parse_args(int argc, char *argv[]) {
	char *arg;
	const char *name = argv[0];

	if( name && (name = strrchr(name, '/')) != NULL ) {
		dvtm_name = name + 1;
	}
	if( getenv("ESCDELAY") == NULL ) {
		set_escdelay(100);
	}
	while( (arg = *++argv) != NULL ) {
		if (arg[0] != '-') {
			char *args[] = { arg, NULL, NULL };
			Action a = { create, {arg, NULL, NULL}};
			push_action(&a);
			continue;
		}
		if( strchr("dhtscm", arg[1]) != NULL && argv[1] == NULL ) {
			error("%s requires an argument (-? for usage)\n", arg);
		}
		switch (arg[1]) {
		case '?':
			printf("usage: dvtm [-v] [-?] [-m mod] [-d delay] [-h lines] [-t title] "
			       "[-s status-fifo] [-c cmd-fifo] [cmd...]\n");
			exit(EXIT_SUCCESS);
		case 'v':
			printf("%s-%s\n", dvtm_name, VERSION);
			exit(EXIT_SUCCESS);
		case 'm': {
			char *mod = *++argv;
			modifier_key = *mod;
			if( mod[0] == '^' && mod[1] != '\0' ) {
				modifier_key = CTRL(mod[1]);
			}
			break;
		}
		case 'd':
			set_escdelay(atoi(*++argv));
			if (ESCDELAY < 50)
				set_escdelay(50);
			else if (ESCDELAY > 1000)
				set_escdelay(1000);
			break;
		case 'h':
			screen.history = atoi(*++argv);
			break;
		case 't':
			title = *++argv;
			break;
		case 's':
			bar.fd = open_or_create_fifo(*++argv, &bar.file, "DVTM_STATUS_FIFO");
			updatebarpos();
			break;
		case 'c': {
			cmdfifo.fd = open_or_create_fifo(*++argv, &cmdfifo.file, "DVTM_CMD_FIFO");
			break;
		}
		default:
			error("unknown option: %s (-? for usage)\n", arg);
		}
	}
	return;
}

static void
push_action(const Action *a)
{
	int count = 0;
	while( actions && actions[count].cmd ) {
		count += 1;
	}

	actions = realloc( actions, ( count + 2 ) * sizeof *actions );
	if( actions == NULL ) {
		error("realloc: %s\n", strerror(errno));
	}
	memcpy(actions + count, a, sizeof *a);
	actions[count + 1].cmd = NULL;
}

int
main(int argc, char *argv[]) {
	KeyCombo keys;
	unsigned int key_index = 0;
	memset(keys, 0, sizeof(keys));

	setenv("DVTM", VERSION, 1);

	parse_args(argc, argv);
	if( actions == NULL ) {
		Action defaults = { create, { NULL } };
		push_action(&defaults);
	}
	setup();
	startup(NULL);

	while (running) {
		int r, nfds = 0;
		fd_set rd;

		if (screen.need_resize) {
			resize_screen();
			screen.need_resize = false;
		}

		FD_ZERO(&rd);
		FD_SET(STDIN_FILENO, &rd);

		FD_SET(sigwinch_pipe[PIPE_RD], &rd);
		nfds = MAX(nfds, sigwinch_pipe[PIPE_RD]);

		FD_SET(sigchld_pipe[PIPE_RD], &rd);
		nfds = MAX(nfds, sigchld_pipe[PIPE_RD]);

		if (cmdfifo.fd != -1) {
			FD_SET(cmdfifo.fd, &rd);
			nfds = MAX(nfds, cmdfifo.fd);
		}

		if (bar.fd != -1) {
			FD_SET(bar.fd, &rd);
			nfds = MAX(nfds, bar.fd);
		}

		for (Client *c = clients; c; ) {
			if (c->editor && c->editor_died)
				handle_editor(c);
			if (!c->editor && c->died) {
				Client *t = c->next;
				destroy(c);
				c = t;
				continue;
			}
			int pty = c->editor ? vt_pty_get(c->editor) : vt_pty_get(c->app);
			FD_SET(pty, &rd);
			nfds = MAX(nfds, pty);
			c = c->next;
		}

		doupdate();
		r = select(nfds + 1, &rd, NULL, NULL, NULL);

		if (r < 0) {
			if (errno == EINTR)
				continue;
			perror("select()");
			exit(EXIT_FAILURE);
		}

		if (FD_ISSET(STDIN_FILENO, &rd)) {
			int code = getch();
			if (code >= 0) {
				keys[key_index++] = code;
				KeyBinding *binding = NULL;
				if( keys[0] == modifier_key && (binding = keybinding(keys, key_index))) {
					unsigned int key_length = MAX_KEYS;
					while (key_length > 1 && !binding->keys[key_length-2])
						key_length--;
					if (key_index == key_length) {
						binding->action.cmd(binding->action.args);
						key_index = 0;
						memset(keys, 0, sizeof(keys));
					}
				} else {
					key_index = 0;
					memset(keys, 0, sizeof(keys));
					keypress(code);
				}
			}
			if (r == 1) /* no data available on pty's */
				continue;
		}

		if (FD_ISSET(sigwinch_pipe[PIPE_RD], &rd)) {
			char buf[512];
			while (read(sigwinch_pipe[PIPE_RD], &buf, sizeof(buf)) > 0);
			handle_sigwinch();
		}

		if (FD_ISSET(sigchld_pipe[PIPE_RD], &rd)) {
			char buf[512];
			while (read(sigchld_pipe[PIPE_RD], &buf, sizeof(buf)) > 0);
			handle_sigchld();
		}

		if (cmdfifo.fd != -1 && FD_ISSET(cmdfifo.fd, &rd))
			handle_cmdfifo();

		if (bar.fd != -1 && FD_ISSET(bar.fd, &rd))
			handle_statusbar();

		for (Client *c = clients; c; c = c->next) {
			if (FD_ISSET(vt_pty_get(c->term), &rd)) {
				if (vt_process(c->term) < 0 && errno == EIO) {
					if (c->editor)
						c->editor_died = true;
					else
						c->died = true;
					continue;
				}
			}

			if (c != sel && is_content_visible(c)) {
				draw_content(c);
				wnoutrefresh(c->window);
			}
		}

		if (is_content_visible(sel)) {
			draw_content(sel);
			curs_set(vt_cursor_visible(sel->term));
			wnoutrefresh(sel->window);
		}
	}

	cleanup();
	return 0;
}

static void fullscreen(void)
{
	for (Client *c = nextvisible(clients); c; c = nextvisible(c->next))
		resize(c, wax, way, waw, wah);
}
