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

/* TODO

	 cleanup stuff up.  display is so slow, that we see blinking.
	 For comparison, running seq 100000 from dvtm in master
	 takes approx:
	 real    0m0.646s
	 user    0m0.059s
	 sys     0m0.050s
	 outside dvtm on that same terminal is:
	 real	0m0.157s
	 user	0m0.053s
	 sys	0m0.047s

	 Note that tmux performance is comparable to the raw terminal


	 When last window is closed, should exit.

    Make it possible to list current key bindings.
    Add ability to do simple commands without command mode.
      eg, window navigation commands.  Maybe have a command
      to enter command mode rather than always going in, or
      have 2 prefix sequences.
    Make layout more flexible, perhaps dlopenable.
    Make it possible to pass layouts on the cmd fifo.  eg, give
      dimensions like "1:100x20@10,20\n2:hxw@y,x\n..."

 get rid of status.fifo and command.fifo, instead us
 MVTM_STATUS_URL and MVTM_CMD_URL.  We can sent layout
 info, and status bar updates, etc to CMD_URL and query STATUS_URL
 for current state.

 A window data structure should not contain any layout info.
 layouts can look like:  wxh@x,y (eg 100x80@0,0 as above,
 or .2x.4@.3,.5 to describe a window with 20% of the lines spanning
 40% of the width, positioned .3 down the screen .5 to the right)
 Each tag stack (9 total, 1 for the "default" and 1 for each of the
 8 tags) will contain current layout including window id:
 eg "2:nxm@x,y;1:nxm@x,y" where n,m,x, and y are absolute values.
 If we get winched, recalculate (so we store the current total sizes
 so we have access to them to recalculate).  Relative sizes
 can be sent on the command url and converted to absolute values
 on the fly.  Windows can overlap!  Seems like a novelty and a
 terrible idea, but I'm pretty sure ncurses allows overlap so
 that layouts like: "1:100x140@0,0;2:40x70@10,20" actaully
 make sense, in which window 1 is partially occluded.  We'll
 need a key binding for a function that resloves (eg, falling
 back to old style layout enforcement).  Currently, I like
 the idea that if a layout only defines placement for
 N windows and the current tag has M > N windows, we only
 display N.  Makes the implementation simpler.

 *
 * Write errors somewhere.  Either in a dedidcated window
 * or in the status bar.  Write any final error to stderr
 * on exit.  Probably should read initialization from stdin.
 */
#include "config.h"
#include "mvtm.h"

struct key_binding *bindings;
struct state state;
enum { DEFAULT, BLUE, RED, CYAN };
struct color colors[] = {
	[DEFAULT] = { .fg = -1,         .bg = -1, .fg256 =  -1, .bg256 = -1, },
	[BLUE]    = { .fg = COLOR_BLUE, .bg = -1, .fg256 =  68, .bg256 = -1, },
	[RED]     = { .fg = COLOR_RED,  .bg = -1, .fg256 =   9, .bg256 = -1, },
	[CYAN]    = { .fg = COLOR_CYAN, .bg = -1, .fg256 =  14, .bg256 = -1, },
};

extern void wstack(void);

unsigned char modifier_key = CTRL('g');

const struct color_rule colorrules[] = {
	{ "", A_NORMAL, &colors[DEFAULT] },
};

/* Commands which can be invoked via the cmdfifo */
struct command commands[] = {
	{ "create", { create,	{ NULL } } },
	{ "focus",  { focusid,	{ NULL } } },
};

void cleanup(void);
void push_action(const struct action *a);
static void reset_entry(struct entry_buf *);

unsigned available_width;  /* width of total available screen real estate */
unsigned available_height; /* height of total available screen real estate */
struct client *clients = NULL;
char *title;

struct action *actions = NULL; /* actions are executed when mvtm is started */

struct screen screen = { .mfact = MFACT, .nmaster = NMASTER, .history = SCROLL_HISTORY };

struct client *stack = NULL;  /* clients are pushed onto the stack as they get the focus */
struct client *sel = NULL;
struct client *lastsel = NULL;
unsigned int seltags;
unsigned int tagset[2] = { 1, 1 };
struct statusbar bar = { .fd = -1, .h = 1 };
CmdFifo cmdfifo = { .fd = -1 };
const char *shell;
struct data_buffer copyreg;
volatile sig_atomic_t stop_requested = 0;
int sigwinch_pipe[] = {-1, -1};
int sigchld_pipe[] = {-1, -1};

void
eprint(const char *errstr, ...) {
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
}

void
error(int include_errstr, const char *errstr, ...) {
	int save_errno = errno;
	cleanup();
	if( errstr != NULL ) {
		va_list ap;
		va_start(ap, errstr);
		vfprintf(stderr, errstr, ap);
		va_end(ap);
	}
	if(include_errstr) {
		fprintf(stderr, ": %s", strerror(save_errno));
	}
	fputc('\n', stderr);
	exit(EXIT_FAILURE);
}

bool
isvisible(struct client *c) {
	return c->tags & tagset[seltags];
}

bool
is_content_visible(struct client *c) {
	return c && isvisible(c) && !c->minimized;
}

struct client*
nextvisible(struct client *c) {
	for (; c && !isvisible(c); c = c->next);
	return c;
}

void
updatebarpos(void) {
	bar.y = 0;
	available_height = screen.h;
	available_width = screen.w;
	available_height -= bar.h;
	bar.y = available_height;
}

void
drawbar(void) {
	int sx, sy, x, y, width;
	unsigned int occupied = 0, urgent = 0;

	for (struct client *c = clients; c; c = c->next) {
		occupied |= c->tags;
		if (c->urgent)
			urgent |= c->tags;
	}

	getyx(stdscr, sy, sx);
	attrset(BAR_ATTR);
	move(bar.y, 0);

	for( unsigned i = 0; i < TAG_COUNT; i++ ) {
		unsigned mask = 1 << i;
		if( tagset[seltags] & mask ) {
			attrset(TAG_SEL);
		} else if( urgent & mask ) {
			attrset(TAG_URGENT);
		} else if( occupied & mask ) {
			attrset(TAG_OCCUPIED);
		} else {
			attrset(TAG_NORMAL);
		}
		printw(TAG_SYMBOL, i + 1);
	}

	attrset(state.runinall ? TAG_SEL : TAG_NORMAL);

	if( state.mode == command_mode ) {
		attrset(COLOR(RED) | A_REVERSE);
	} else {
		attrset(COLOR(DEFAULT) | A_NORMAL);
	}

	getyx(stdscr, y, x);
	(void)y; /* ??? Is this to suppress a compiler warning?? */
	int maxwidth = screen.w - x - 2;

	addch(BAR_BEGIN);

	wchar_t wbuf[sizeof bar.text];
	size_t numchars = mbstowcs(wbuf, bar.text, sizeof bar.text);

	if (numchars != (size_t)-1 && (width = wcswidth(wbuf, maxwidth)) != -1) {
		int pos = 0;

		for (size_t i = 0; i < numchars; i++) {
			pos += wcwidth(wbuf[i]);
			if (pos > maxwidth)
				break;
			addnwstr(wbuf+i, 1);
		}
		clrtoeol();
	}

	mvaddch(bar.y, screen.w - 1, BAR_END);
	attrset(NORMAL_ATTR);
	move(sy, sx);
	wnoutrefresh(stdscr);
}

int
show_border(void) {
	return clients && clients->next;
}

void
draw_border(struct client *c) {
	int x, y, attrs = NORMAL_ATTR;
	char border_title[128];
	char *msg = NULL;
	char *title = c->title;

	if (!show_border())
		return;
	if (sel != c && c->urgent)
		attrs = URGENT_ATTR;
	if (sel == c || (state.runinall && !c->minimized))
		attrs = COLOR(BLUE) | A_NORMAL;

	if( sel == c && state.mode == command_mode ) {
		attrs = COLOR(RED) | A_REVERSE;
	} else if( sel == c && c->term == c->editor ) {
		attrs = COLOR(CYAN) | A_NORMAL;
	}
	if( state.mode == command_mode ) {
		msg = " COMMAND MODE ";
	} else if( c->term == c->editor ) {
		msg = " COPY MODE ";
		title = c->editor_title;
	}

	wattrset(c->window, attrs);
	getyx(c->window, y, x);
	mvwhline(c->window, 0, 0, ACS_HLINE, c->w);

	snprintf(border_title, MIN(c->w, sizeof border_title), "%s%s#%d (%ld)",
		c->w > 32 ? title : "",
		c->w > 32 ? " | " : "",
		c->id, (long)c->pid);

	if(c->tags) {
		unsigned mask = 0x1;
		int first = 1;
		for( int i=0; i < TAG_COUNT; i++, mask <<= 1 ) {
			if( (c->tags & mask) != 0) {
				char b[32];
				sprintf(b, "%s%d", first ? "" : ",", i + 1);
				strcat(border_title, b);
				first = 0;
			}
		}
	}

	mvwprintw(c->window, 0, 2, "[%s]", border_title);
	if( msg != NULL ) {
		int start = strlen(border_title) + 4 + 2;
		if( c->w > start + strlen(msg) + 2 ) {
			mvwprintw(c->window, 0, start, "%s", msg);
		}
	}
	wmove(c->window, y, x);
}

void
draw_content(struct client *c) {
	vt_draw(c->term, c->window, c->has_title_line, 0);
}

void
draw(struct client *c) {
	if (is_content_visible(c)) {
		redrawwin(c->window);
		draw_content(c);
	}
	draw_border(c);
	wnoutrefresh(c->window);
}

void
draw_all(void) {
	drawbar();
	if (!nextvisible(clients)) {
		sel = NULL;
		curs_set(0);
		erase();
		drawbar();
		doupdate();
		return;
	}

	for (struct client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (c != sel)
			draw(c);
	}
	/* as a last step the selected window is redrawn,
	 * this has the effect that the cursor position is
	 * accurate
	 */
	if (sel)
		draw(sel);
}

void
arrange(void) {
	unsigned int m = 0, n = 0;
	for (struct client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
		c->order = ++n;
		if (c->minimized)
			m++;
	}
	erase();
	attrset(NORMAL_ATTR);
	if( m )
		available_height--;
	wstack();
	if( m ) {
		unsigned int i = 0, nw = available_width / m, nx = 0;
		for (struct client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
			if (c->minimized) {
				resize(c, nx, available_height, ++i == m ? available_width - nx : nw, 1);
				nx += nw;
			}
		}
		available_height++;
	}
	focus(NULL);
	wnoutrefresh(stdscr);
	draw_all();
}

void
attach(struct client *c) {
	if (clients)
		clients->prev = c;
	c->next = clients;
	c->prev = NULL;
	clients = c;
	for (int o = 1; c; c = nextvisible(c->next), o++)
		c->order = o;
}

void
attachafter(struct client *c, struct client *a) { /* attach c after a */
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

void
attachstack(struct client *c) {
	c->snext = stack;
	stack = c;
}

void
detach(struct client *c) {
	struct client *d;
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
set_term_title(char *new_title) {
	char *term, *t = title;

	assert(new_title == sel->title);
	if( !t && *new_title ) {
		t = new_title;
	}

	if (t && (term = getenv("TERM")) && !strstr(term, "linux")) {
		printf("\033]0;%s\007", t);
		fflush(stdout);
	}
}

void
detachstack(struct client *c) {
	struct client **tc;
	for (tc = &stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;
}

void
focus(struct client *c) {
	if (!c)
		for (c = stack; c && !isvisible(c); c = c->snext);
	if (sel == c)
		return;
	lastsel = sel;
	sel = c;
	if (lastsel) {
		lastsel->urgent = false;
		draw_border(lastsel);
		wnoutrefresh(lastsel->window);
	}

	if (c) {
		detachstack(c);
		attachstack(c);
		set_term_title(c->title);
		c->urgent = false;
		draw_border(c);
		wnoutrefresh(c->window);
	}
	curs_set(c && !c->minimized && vt_cursor_visible(c->term));
}

void
applycolorrules(struct client *c) {
	const struct color_rule *r = colorrules;
	const struct color_rule *e = r + LENGTH(colorrules);
	short fg = r->color->fg, bg = r->color->bg;
	attr_t attrs = r->attrs;

	for( r += 1; r < e; r++) {
		if (strstr(c->title, r->title)) {
			attrs = r->attrs;
			fg = r->color->fg;
			bg = r->color->bg;
			break;
		}
	}

	vt_default_colors_set(c->term, attrs, fg, bg);
}

/* copy title to dest, compressing whitespace and discarding non-printable */
static void
sanitize_string(const char *title, char *dest, size_t siz)
{
	char *d = dest;
	char *e = dest + siz - 1;
	for( ; title && *title && d < e; title += 1 ) {
		if( isprint(*title) && ! isspace(*title)) {
			*d++ = *title;
		} else if( d > dest && d[-1] != ' ') {
			*d++ = ' ';
		}
	}
	*d = '\0';
}

void
term_title_handler(Vt *term, const char *title) {
	struct client *c = (struct client *)vt_data_get(term);
	sanitize_string(title, c->title, sizeof c->title);
	if( sel == c ) {
		set_term_title(c->title);
	}
	draw_border(c);
	applycolorrules(c);
}

void
term_urgent_handler(Vt *term) {
	struct client *c = (struct client *)vt_data_get(term);
	c->urgent = true;
	printf("\a");
	fflush(stdout);
	drawbar();
	if( sel != c && isvisible(c) )
		draw_border(c);
}

void
move_client(struct client *c, int x, int y) {
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

void
resize_client(struct client *c, int w, int h) {
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
resize(struct client *c, int x, int y, int w, int h) {
	resize_client(c, w, h);
	move_client(c, x, y);
}

struct client*
get_client_by_coord(unsigned int x, unsigned int y) {
	if (y < 0 || y >= available_height)
		return NULL;
	for (struct client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h) {
			return c;
		}
	}
	return NULL;
}

void
sigchld_handler(int sig) {
	write(sigchld_pipe[1], "\0", 1);
}

void
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

		for (struct client *c = clients; c; c = c->next) {
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

void
sigwinch_handler(int sig) {
	write(sigwinch_pipe[1], "\0", 1);
}

void
sigterm_handler(int sig) {
	stop_requested = 1;
}

void
resize_screen(void) {
	struct winsize ws;

	if (ioctl(0, TIOCGWINSZ, &ws) == -1) {
		getmaxyx(stdscr, screen.h, screen.w);
	} else {
		screen.w = ws.ws_col;
		screen.h = ws.ws_row;
	}
	resizeterm(screen.h, screen.w);
	wresize(stdscr, screen.h, screen.w);
	updatebarpos();
	clear();
	arrange();
	screen.winched = 0;
}

/*
 * Find a keybinding the matches the entry_buf.  If there
 * are multiple possible keybindings, this will return
 * a struct key_binding with an emtpy action and a non-empty
 * next.
 */
const struct key_binding *
keybinding(unsigned char k, const struct key_binding *r)
{
	assert(r != NULL);
	const struct key_binding *b = r + k;
	return b->action.cmd ? b : b->next;
}

unsigned int
bitoftag(int tag) {
	unsigned t = tag ? 0 : ~0;
	if( tag > 0 && tag < 9 ) {
		t = 1 << (tag - 1);
	}
	return t;
}

void
tagschanged() {
	bool allminimized = true;
	for (struct client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
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


int
untag(const char * const args[]) {
	if( sel ) {
		sel->tags = 1;
		tagschanged();
	}
	return 0;
}

int
tag(const char * const args[]) {
	if( sel != NULL ) {
		int t = state.buf.count % 8;
		sel->tags |= bitoftag(t);
		tagschanged();
	}
	return 0;
}

int
toggletag(const char * const args[]) {
	if (!sel)
		return 0;
	int tag = args[0] ? args[0][0] - '0' : 0;
	unsigned int newtags = sel->tags ^ (bitoftag(tag));
	if (newtags) {
		sel->tags = newtags;
		tagschanged();
	}
	return 0;
}

int
toggleview(const char * const args[]) {
	int tag = args[0] ? args[0][0] - '0' : 0;
	unsigned int newtagset = tagset[seltags] ^ (bitoftag(tag));
	if (newtagset) {
		tagset[seltags] = newtagset;
		tagschanged();
	}
	return 0;
}

int
view(const char * const args[]) {
	int tag = state.buf.count;
	unsigned int newtagset = bitoftag(tag);
	if (tagset[seltags] != newtagset && newtagset) {
		seltags ^= 1; /* toggle sel tagset */
		tagset[seltags] = newtagset;
		tagschanged();
	}
	return 0;
}

int
viewprevtag(const char * const args[]) {
	seltags ^= 1;
	tagschanged();
	return 0;
}

void
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

	for (struct client *c = state.runinall ? nextvisible(clients) : sel; c; c = nextvisible(c->next)) {
		if (is_content_visible(c)) {
			c->urgent = false;
			if (code == '\e')
				vt_write(c->term, buf, len);
			else
				vt_keypress(c->term, code);
			if (key != -1)
				vt_keypress(c->term, key);
		}
		if (!state.runinall)
			break;
	}
}

const char *
getshell(void) {
	const char *shell = getenv("SHELL");

	if( shell == NULL ) {
		struct passwd *pw;
		pw = getpwuid(getuid());
		shell = pw != NULL ? pw->pw_shell : "/bin/sh";
	}
	if(
		shell == NULL
		|| *shell != '/'
		|| strcmp(strrchr(shell, '/') + 1, PACKAGE) == 0
		|| access(shell, X_OK)
	) {
		fprintf(stderr, "SHELL (%s) is invalid\n", shell);
		exit(EXIT_FAILURE);
	}
	return shell;
}

bool
set_blocking(int fd, bool blocking) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return false;
	flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
	return !fcntl(fd, F_SETFL, flags);
}

void *
xcalloc(size_t count, size_t size)
{
	void *rv = calloc(count, size);
	if( rv == NULL ) {
		error(1, "calloc");
	}
	return rv;
}


static int
push_binding(struct key_binding *b, const unsigned char *keys, const struct action *a, int loop)
{
	struct key_binding *t = b + keys[0];
	if( t->action.cmd != NULL ) {
		return 1; /* conflicting binding */
	}
	if( keys[1] && loop ) {
		return 1; /* invalid binding */
	}
	if( keys[1] ) {
		if( t->next == NULL ) {
			t->next = xcalloc(1u << CHAR_BIT, sizeof *t->next);
		}
		push_binding(t->next, keys + 1, a, 0);
	} else {
		memcpy(&t->action, a, sizeof t->action);
		t->next = loop ? t : NULL;
	}
	return 0;
}

/*
 * Wrapper around the external facing bind(), which
 * may at some point be used as a command to allow
 * run time override of bingings.  The data in args
 * is *not* copied, so the caller must ensure that
 * they are non-volatile. (eg, don't pass a stack variable).
 */
static int
internal_bind(int leader, int loop, unsigned char *keys, command *func,
	const char * args[])
{
	struct action a = {0};
	typeof(bindings) b;

	b = leader ? bindings[modifier_key].next : bindings;
	a.cmd = func;
	for(int i = 0; *args && i < MAX_ARGS; args++ ) {
		a.args[i] = *args;
	}
	return push_binding(b, keys, &a, loop);
}

static void
build_bindings(void)
{
	binding_description *b = mod_bindings;
	state.binding = bindings = xcalloc(1u << CHAR_BIT, sizeof *bindings);
	bindings[modifier_key].next = xcalloc(1u << CHAR_BIT, sizeof *bindings->next);
	for( ; b[0][0]; b++) {
		char **e = *b;
		command *cmd = get_function(e[1]);
		if( cmd == NULL ) {
			error(0, "couldn't find %s", e[1]);
		}
		const char *args[] = {e[2], e[3], e[4]};
		if( internal_bind(1, 0, (unsigned char *)e[0], cmd, args) ) {
			error(0, "failed to bind to %s", e[1]);
		}
	}
	for( int i=0; i < 10; i++ ) {
		char *buf = xcalloc(2, 1);
		const char *args[] = { buf, NULL };
		buf[0] = '0' + i;
		if( internal_bind(1, 0, (unsigned char *)buf, digit, args) ) {
			error(0, "failed to bind to '%d'", i);
		}
	}
}

void
setup(void) {
	build_bindings();
	reset_entry(&state.buf);
	shell = getshell();
	setlocale(LC_CTYPE, "");
	setenv("MVTM", VERSION, 1);
	initscr();
	start_color(); /* initializes globals COLORS and COLOR_PAIRS */
	noecho();
	nonl();
	keypad(stdscr, TRUE);
	raw();
	vt_init();
	for( struct color *t = colors; t < colors + LENGTH(colors); t++) {
		if (COLORS == 256) {
			if (t->fg256)
				t->fg = t->fg256;
			if (t->bg256)
				t->bg = t->bg256;
		}
		t->pair = vt_color_reserve(t->fg, t->bg);
	}
	resize_screen();

	int *pipes[] = { sigwinch_pipe, sigchld_pipe };
	for (int i = 0; i < 2; ++i) {
		if( pipe(pipes[i]) < 0 ) {
			error(1, "pipe");
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

void
destroy(struct client *c) {
	if (sel == c)
		focusnextnm(NULL);
	detach(c);
	detachstack(c);
	if (sel == c) {
		struct client *next = nextvisible(clients);
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
	if( !clients ) {
		stop_requested = 1;
	}
	free(c);
	arrange();
}

void
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

char *getcwd_by_pid(struct client *c) {
	if (!c)
		return NULL;
	char buf[32];
	snprintf(buf, sizeof buf, "/proc/%d/cwd", c->pid);
	return realpath(buf, NULL);
}

int
bind(const char * const args[])
{
	const unsigned char *binding = (void*)args[0];
	struct action a = {0};
	const char *t;

	a.cmd = get_function(args[1]);
	for(int i = 0; i < 3; i++ ) {
		a.args[i] = args[i+2];
	}
	return push_binding(bindings, binding, &a, 0);
}

int
digit(const char *const args[])
{
	assert( args && args[0]);
	int val = args[0][0] - '0';

	state.buf.count = 10 * state.buf.count + val;
	return 0;
}

int
transition_no_send(const char * const args[])
{
	assert(state.mode == command_mode);
	change_mode(NULL);
}

int
transition_with_send(const char * const args[])
{
	assert(state.mode == command_mode);
	keypress(state.code);
	change_mode(NULL);
}

int
create(const char * const args[]) {
	const char *pargs[4] = { shell, NULL };
	char buf[8], *cwd = NULL;
	const char *env[] = {
		"MVTM_WINDOW_ID", buf,
		NULL
	};

	if (args && args[0]) {
		pargs[1] = "-c";
		pargs[2] = args[0];
		pargs[3] = NULL;
	}
	struct client *c = calloc(1, sizeof *c);
	if (!c)
		return 0;
	c->tags = tagset[seltags];
	c->id = ++cmdfifo.id;
	snprintf(buf, sizeof buf, "%d", c->id);

	if (!(c->window = newwin(available_height, available_width, 0, 0))) {
		free(c);
		return 0;
	}

	c->term = c->app = vt_create(screen.h, screen.w, screen.history);
	if (!c->term) {
		delwin(c->window);
		free(c);
		return 0;
	}

	if (args && args[0]) {
		c->cmd = args[0];
	} else {
		c->cmd = shell;
	}

	if( args && args[1] ) {
		sanitize_string(args[1], c->title, sizeof c->title);
	} else {
		sanitize_string(c->cmd, c->title, sizeof c->title);
	}

	if (args && args[2])
		cwd = !strcmp(args[2], "$CWD") ? getcwd_by_pid(sel) : (char*)args[2];

	c->pid = vt_forkpty(c->term, shell, pargs, cwd, env, NULL, NULL);
	if (args && args[2] && !strcmp(args[2], "$CWD"))
		free(cwd);
	vt_data_set(c->term, c);
	vt_title_handler_set(c->term, term_title_handler);
	vt_urgent_handler_set(c->term, term_urgent_handler);
	applycolorrules(c);
	c->x = 0;
	c->y = 0;
	debug("client with pid %d forked\n", c->pid);
	attach(c);
	focus(c);
	arrange();

	if( args && args[2] && ! strcmp(args[2], "master") ) {
		const char * const args[2] = { "+1", NULL };
		incnmaster(args);
	}
	return 0;
}

static void
reset_entry(struct entry_buf *e)
{
	e->next = e->data;
	e->count = 0;
	*e->next = '\0';
}

int
change_mode(const char * const args[])
{
	struct state *s = &state;
	reset_entry(&s->buf);
	switch(s->mode) {
	case keypress_mode:
		s->mode = command_mode;
		s->binding = bindings[modifier_key].next;
		break;
	case command_mode:
		s->mode = keypress_mode;
		s->binding = bindings;
	}
	draw_all();
	return 0;
}

size_t
get_bindings(char **b)
{
	typeof(*mod_bindings) *t = mod_bindings;
	size_t cap, len;
	char *dst;
	dst = *b = realloc(NULL, cap = BUFSIZ);
	if( dst == NULL ) {
		/* TODO: emit error */
		return 0;
	}
	*dst = '\0';
	for( len = 0; t[0][0]; t++ ) {
		for( char **e = *t; *e; e += 1 ) {
			size_t extra_len;
			extra_len = strlen(*e) + 1;
			if( len + extra_len >= cap ) {
				dst = *b = realloc( dst, cap += BUFSIZ );
				if( dst == NULL ) {
					return 0;
				}
			}
			strncat(dst + len, *e, extra_len - 1);
			len += extra_len;
			assert(dst[len - 1] == '\0');
			dst[len - 1] = e[1] ? ' ' : '\n';
			dst[len] = '\0';
		}
	}
	return len;
}

int
copymode(const char * const args[])
{
	if (!args || !args[0] || !sel || sel->editor)
		goto end;

	bool colored = strstr(args[0], "pager") != NULL;

	if (!(sel->editor = vt_create(sel->h - sel->has_title_line, sel->w, 0)))
		goto end;

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
		goto end;;
	}

	sel->term = sel->editor;

	if (sel->editor_fds[0] != -1) {
		char *buf = NULL;
		size_t len;

		sanitize_string(args[0], sel->editor_title, sizeof sel->editor_title);

		if( args[1] && !strcmp(args[1], "bindings") ) {
			len = get_bindings(&buf);
		} else {
			len = vt_content_get(sel->app, &buf, colored);
		}
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

end:
	assert(state.mode == command_mode);
	draw_border(sel);
	change_mode(NULL);
	return 0;
}

static struct client *
select_client(int only_visible)
{
	struct client *c = sel;
	if( state.buf.count != 0 ) {
		for( c = clients; c; c = c->next) {
			if( only_visible && ! isvisible(c) ) {
				continue;
			}
			if( c->id == state.buf.count) {
				break;
			}
		}
	}
	return c;
}

int
focusn(const char * const args[])
{
	struct client *c = select_client(1);
	if( c != NULL ) {
		focus(c);
		if( c->minimized ) {
			toggleminimize(NULL);
		}
	}
	return 0;
}

int
focusid(const char * const args[]) {
	if (!args[0])
		return 0;

	const int win_id = atoi(args[0]);
	for (struct client *c = clients; c; c = c->next) {
		if (c->id == win_id) {
			focus(c);
			if (c->minimized)
				toggleminimize(NULL);
			if (!isvisible(c)) {
				c->tags |= tagset[seltags];
				tagschanged();
			}
			return 0;
		}
	}
	return 0;
}

int
focusnext(const char * const args[]) {
	struct client *c;
	if (!sel)
		return 0;
	for (c = sel->next; c && !isvisible(c); c = c->next);
	if (!c)
		for (c = clients; c && !isvisible(c); c = c->next);
	if (c)
		focus(c);
	return 0;
}

int
focusnextnm(const char * const args[]) {
	if (!sel)
		return 0;
	struct client *c = sel;
	do {
		c = nextvisible(c->next);
		if (!c)
			c = nextvisible(clients);
	} while (c->minimized && c != sel);
	focus(c);
	return 0;
}

int
focusprev(const char * const args[]) {
	struct client *c;
	if (!sel)
		return 0;
	for (c = sel->prev; c && !isvisible(c); c = c->prev);
	if (!c) {
		for (c = clients; c && c->next; c = c->next);
		for (; c && !isvisible(c); c = c->prev);
	}
	if (c)
		focus(c);
	return 0;
}

int
focusprevnm(const char * const args[]) {
	if (!sel)
		return 0;
	struct client *c = sel;
	do {
		for (c = c->prev; c && !isvisible(c); c = c->prev);
		if (!c) {
			for (c = clients; c && c->next; c = c->next);
			for (; c && !isvisible(c); c = c->prev);
		}
	} while (c && c != sel && c->minimized);
	focus(c);
	return 0;
}

int
focuslast(const char * const args[]) {
	if (lastsel)
		focus(lastsel);
	return 0;
}

int
focusup(const char * const args[]) {
	if (!sel)
		return 0;
	/* avoid vertical separator, hence +1 in x direction */
	struct client *c = get_client_by_coord(sel->x + 1, sel->y - 1);
	if (c)
		focus(c);
	else
		focusprev(args);
	return 0;
}

int
focusdown(const char * const args[]) {
	if (!sel)
		return 0;
	struct client *c = get_client_by_coord(sel->x, sel->y + sel->h);
	if (c)
		focus(c);
	else
		focusnext(args);
	return 0;
}

int
focusleft(const char * const args[]) {
	if (!sel)
		return 0;
	struct client *c = get_client_by_coord(sel->x - 2, sel->y);
	if (c)
		focus(c);
	else
		focusprev(args);
	return 0;
}

int
focusright(const char * const args[]) {
	if (!sel)
		return 0;
	struct client *c = get_client_by_coord(sel->x + sel->w + 1, sel->y);
	if (c)
		focus(c);
	else
		focusnext(args);
	return 0;
}

int
change_kill_signal(const char *const args[])
{
	state.signal = state.buf.count ? state.buf.count : SIGHUP;
	return 0;
}

int
signalclient(const char * const args[])
{
	int signal = state.buf.count ? state.buf.count : SIGHUP;
	if( sel ) {
		kill( -sel->pid, signal);
	}
	return 0;
}

int
killclient(const char * const args[])
{
	struct client *c = select_client(0);
	if( c != NULL ) {
		kill( -c->pid, state.signal ? state.signal : SIGHUP);
	}
	return 0;
}

static void
trim_whitespace(struct data_buffer *r)
{
	char *end = r->data + r->len;
	while( isspace(*--end) ) {
		r->len -= 1;
	}
}

int
paste(const char * const args[]) {
	if (sel && copyreg.data) {
		trim_whitespace(&copyreg);
		vt_write(sel->term, copyreg.data, copyreg.len);
	}
	assert(state.mode == command_mode);
	change_mode(NULL);
	return 0;
}

int
quit(const char * const args[]) {
	stop_requested = 1;
	return 0;
}

int
redraw(const char * const args[]) {
	for (struct client *c = clients; c; c = c->next) {
		if (!c->minimized) {
			vt_dirty(c->term);
			wclear(c->window);
			wnoutrefresh(c->window);
		}
	}
	resize_screen();
	return 0;
}

int
scrollback(const char * const args[]) {
	double pages = args[0] ? strtod(args[0], NULL) : -0.5;
	if( !sel || !is_content_visible(sel) ) {
		return 0;
	}
	if( state.buf.count ) {
		pages *= state.buf.count;
	}
	vt_scroll(sel->term,  pages * sel->h);
	draw(sel);
	curs_set(vt_cursor_visible(sel->term));
	return 0;
}

int
send(const char * const args[]) {
	if (sel && args && args[0])
		vt_write(sel->term, args[0], strlen(args[0]));
	return 0;
}

int
incnmaster(const char * const args[]) {
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
	return 0;
}

int
setmfact(const char * const args[]) {
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
	return 0;
}

int
toggleminimize(const char * const args[]) {
	struct client *c, *m, *t;
	unsigned int n;
	if (!sel)
		return 0;
	/* the last window can't be minimized */
	if (!sel->minimized) {
		for (n = 0, c = nextvisible(clients); c; c = nextvisible(c->next))
			if (!c->minimized)
				n++;
		if (n == 1)
			return 0;
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
	return 0;
}

int
togglerunall(const char * const args[]) {
	state.runinall = !state.runinall;
	draw_all();
	return 0;
}

int
zoom(const char * const args[]) {
	struct client *c = select_client(0);

	if( c != NULL ) {
		detach(c);
		attach(c);
		focus(c);
		if( c->minimized ) {
			toggleminimize(NULL);
		}
		arrange();
	}
	return 0;
}


struct command *
get_cmd_by_name(const char *name) {
	for (unsigned int i = 0; i < LENGTH(commands); i++) {
		if (!strcmp(name, commands[i].name))
			return &commands[i];
	}
	return NULL;
}

void
handle_cmdfifo(void) {
	int r;
	char *p, *s, cmdbuf[512], c;
	struct command *cmd;

	r = read(cmdfifo.fd, cmdbuf, sizeof cmdbuf - 1);
	if (r <= 0) {
		cmdfifo.fd = -1;
		return;
	}

	cmdbuf[r] = '\0';
	cmd = get_cmd_by_name(cmdbuf);
	if( cmd != NULL ) {
		cmd->action.cmd(cmd->action.args);
	}
}

void
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

void
handle_editor(struct client *c) {
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
	draw_border(c);
	wnoutrefresh(c->window);
}

int
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
			error(1, "%s", name );
		}
	} while (fd == -1);

	if (fstat(fd, &info) == -1) {
		error(1, "%s", name);
	} else if (!S_ISFIFO(info.st_mode)) {
		error(0, "%s is not a named pipe", name);
	}

	if( ( abs_name = realpath(name, NULL)) == NULL ) {
		error(1, "%s", name);
	}
	setenv(env_name, abs_name, 1);
	free(abs_name);
	return fd;
}


void
parse_args(int argc, char *argv[]) {
	char *arg;
	const char *name = strrchr(argv[0], '/');
	const char *basename = name == NULL ? argv[0] : name + 1;
	int force = 0;

	if( getenv("ESCDELAY") == NULL ) {
		set_escdelay(100);
	}
	while( (arg = *++argv) != NULL ) {
		if (arg[0] != '-') {
			char * const args[] = { arg, NULL, NULL };
			struct action a = { create, {arg, NULL, NULL}};
			push_action(&a);
			continue;
		}
		if( strchr("drtscm", arg[1]) != NULL && argv[1] == NULL ) {
			error(0, "%s requires an argument (-h for usage)", arg);
		}
		switch (arg[1]) {
		case 'h':
			printf("usage: %s [-v] [-h] [-f] [-m mod] [-d delay] "
				"[-r lines] [-t title] [-s status-fifo] "
				"[-c cmd-fifo] [cmd...]\n", basename);
			exit(EXIT_SUCCESS);
		case 'v':
			printf("%s-%s\n", PACKAGE, VERSION);
			exit(EXIT_SUCCESS);
		case 'f':
			force = 1;
			break;
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
		case 'r':
			screen.history = strtol(*++argv, NULL, 10);
			break;
		case 't':
			title = *++argv;
			break;
		case 's':
			bar.fd = open_or_create_fifo(*++argv, &bar.file, "MVTM_STATUS_FIFO");
			break;
		case 'c': {
			cmdfifo.fd = open_or_create_fifo(*++argv, &cmdfifo.file, "MVTM_CMD_FIFO");
			break;
		}
		default:
			error(0, "unknown option: %s (-h for usage)", arg);
		}
	}
	if( actions == NULL ) {
		struct action defaults = { create, { NULL } };
		push_action(&defaults);
	}
	if( getenv("MVTM") && ! force ) {
		error(0, "Nested session prevented.  Use -f to allow");
	}
	return;
}

void
push_action(const struct action *act)
{
	struct action *new;

	new = malloc( sizeof *new );
	if( new == NULL ) {
		error(1, "malloc");
	}
	if( actions == NULL ) {
		actions = new;
	} else {
		struct action *a = actions;
		while( a->next != NULL ) {
			a = a->next;
		}
		a->next = new;
	}
	memcpy(new, act, sizeof *new );
}

void
set_fd_mask(int fd, fd_set *r, int *nfds) {
	if( fd != -1 ) {
		FD_SET(fd, r);
		if( fd > *nfds ) {
			*nfds = fd;
		}
	}
}

void
cleanup_dead_clients(struct client *c)
{
	while( c != NULL ) {
		if( c->editor && c->editor_died ) {
			handle_editor(c);
		}
		if( !c->editor && c->died ) {
			struct client *t = c->next;
			destroy(c);
			c = t;
			continue;
		}
		c = c->next;
	}
}

void
check_client_fds(fd_set *rd, int *nfds, struct client *c)
{
	while(c != NULL) {
		int pty = c->editor ? vt_pty_get(c->editor) : vt_pty_get(c->app);
		set_fd_mask(pty, rd, nfds);
		c = c->next;
	}
}

void
handle_input(struct state *s)
{
	const struct key_binding *b;

	int code = s->code = getch();
	if( code < 0 || code > 1 << CHAR_BIT ) {
		keypress(code);
	} else if( NULL == (b = keybinding(code, s->binding))
			&& s->mode == keypress_mode) {
		assert(s->binding == bindings);
		keypress(code);
	} else if( code == modifier_key ) {
		if( s->mode == command_mode ) {
			keypress(code);
		}
		change_mode(NULL);
	} else {
		if( b != NULL ) {
			*s->buf.next++ = code;
			*s->buf.next = '\0';
			if(b->action.cmd != NULL) {
				b->action.cmd(b->action.args);
				/* Some actions change s->mode.  digit does a loop back.
				everything else needs to reset the keybinding lookup.
				TODO: find a cleaner way to do this */
				if(b->action.cmd != digit && s->mode == command_mode)  {
					reset_entry(&s->buf);
					s->binding = bindings[modifier_key].next;
				}
			} else {
				s->binding = b;
			}
		} else {
			reset_entry(&s->buf);
			s->binding = bindings[modifier_key].next;
		}
		/* TODO: consider just using bar.text for the buffer */
		snprintf(bar.text, sizeof bar.text, "%s", s->buf.data);
	}
	drawbar();
	draw(sel);
}

int
main(int argc, char *argv[])
{
	struct state *s = &state;

	parse_args(argc, argv);
	setup();
	for( struct action *a = actions; a && a->cmd; a = a->next ) {
		a->cmd(a->args);
	}

	while( !stop_requested ) {
		int r, nfds = 0;
		fd_set rd;

		FD_ZERO(&rd);
		set_fd_mask(STDIN_FILENO, &rd, &nfds);
		set_fd_mask(sigwinch_pipe[0], &rd, &nfds);
		set_fd_mask(sigchld_pipe[0], &rd, &nfds);
		set_fd_mask(cmdfifo.fd, &rd, &nfds);
		set_fd_mask(bar.fd, &rd, &nfds);

		check_client_fds(&rd, &nfds, clients);

		doupdate();
		r = select(nfds + 1, &rd, NULL, NULL, NULL);

		if( r < 0 ) {
			if (errno == EINTR)
				continue;
			perror("select()");
			exit(EXIT_FAILURE);
		}

		if (FD_ISSET(STDIN_FILENO, &rd)) {
			handle_input(s);

			if (r == 1) /* no data available on pty's */
				continue;
		}

		if( FD_ISSET(sigwinch_pipe[0], &rd) ) {
			char buf[512];
			if( read(sigwinch_pipe[0], &buf, sizeof(buf)) < 0 ) {
				error(1, "read from sigwinch pipe");
			}
			screen.winched = 1;
		}

		if( FD_ISSET(sigchld_pipe[0], &rd) ) {
			char buf[512];
			if( read(sigchld_pipe[0], &buf, sizeof(buf)) < 0) {
				error(1, "read from sigchld pipe");
			}
			handle_sigchld();
		}

		if (cmdfifo.fd != -1 && FD_ISSET(cmdfifo.fd, &rd))
			handle_cmdfifo();

		if (bar.fd != -1 && FD_ISSET(bar.fd, &rd))
			handle_statusbar();

		for (struct client *c = clients; c; c = c->next) {
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
		cleanup_dead_clients(clients);
		if( screen.winched ) {
			resize_screen();
		}
	}

	cleanup();
	return 0;
}
