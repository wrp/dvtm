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
  Implement Command-line mode (eg, :create arg1 args)
  Make it possible to list current key bindings.
  Make layout more flexible, perhaps dlopenable.
  Make it possible to pass layouts on the cmd fifo.  eg, give
    dimensions like "1:100x20@10,20\n2:hxw@y,x\n..."
  Write errors somewhere.  Either in a dedidcated window
    or in the status bar.  Write any final error to stderr
    on exit.  Probably should read initialization from stdin.
  Need to check errors in copymode.  If mvtm-editor is not in the
    path,for example, we should see an error message.  copymode needs
    to read the error stream and print somewhere.  (See previous TODO)
  Get rid of status.fifo and command.fifo, instead use
    MVTM_STATUS_URL and MVTM_CMD_URL.  We can send layout
    info, and status bar updates, etc to CMD_URL and query STATUS_URL
    for current state.
  Consider name change: 'mtm'
  Make tagset nameable.  As soon as we do that, we basically have
  named tabs.
 */
#include "config.h"
#include "mvtm.h"

struct key_binding *bindings;     /* keypress_mode bindings */
struct key_binding *cmd_bindings;  /* command_mode bindings */
struct state state;

enum { DEFAULT, BLUE, RED, CYAN };
struct color colors[] = {
	[DEFAULT] = { .fg = -1,         .bg = -1, .fg256 =  -1, .bg256 = -1, },
	[BLUE]    = { .fg = COLOR_BLUE, .bg = -1, .fg256 =  68, .bg256 = -1, },
	[RED]     = { .fg = COLOR_RED,  .bg = -1, .fg256 =   9, .bg256 = -1, },
	[CYAN]    = { .fg = COLOR_CYAN, .bg = -1, .fg256 =  14, .bg256 = -1, },
};

static unsigned char modifier_key = CTRL('g');
static void render_layout(struct layout *, unsigned, unsigned, unsigned, unsigned);

const struct color_rule colorrules[] = {
	{ "", A_NORMAL, &colors[DEFAULT] },
};

/* Commands which can be invoked via the cmdfifo */
struct command commands[] = {
	{ "create", { create,	{ NULL } } },
};

void cleanup(void);
void push_action(const struct action *a);
static void reset_entry(struct entry_buf *);

unsigned available_width;  /* width of total available screen real estate */
unsigned available_height; /* height of total available screen real estate */
struct client *clients = NULL;
char *title;

struct action *actions = NULL; /* actions are executed when mvtm is started */

struct screen screen = { .history = SCROLL_HISTORY };

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

/* return non-zero if c is in the current view
 * this is not quite accurate: we should confirm that
 * c is in an active window in a layout.   But for now
 this is adequate.
*/
bool
isvisible(struct client *c) {
	struct view * v = state.current_view;
	struct client **cp;
	assert( v != NULL );

	for( cp = v->vclients; *cp != NULL; cp += 1 ) {
		if( *cp == c ) {
			return 1;
		}
	}
	return 0;
}

bool
is_content_visible(struct client *c) {
	return c && isvisible(c);
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

	getyx(stdscr, sy, sx);
	move(bar.y, 0);

	for( unsigned i = 0; i < TAG_COUNT; i++ ) {
		unsigned mask = 1 << i;
		if( tagset[seltags] & mask ) {
			attrset(TAG_SEL);
		} else {
			attrset(TAG_NORMAL);
		}
		printw(TAG_SYMBOL, i + 1);
	}

	attrset(TAG_NORMAL);

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
	struct view *v = state.current_view;
	return v && v->vclients && v->vclients[0] != NULL;
}

void
draw_border(struct window *w) {
	struct client *c = w->c;
	int x, y, attrs = NORMAL_ATTR;
	char border_title[128];
	char *msg = NULL;
	char *title = c->title;

	if( !show_border() || c == NULL ) {
		return;
	}
	if (sel == c )
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
	if( c == NULL ) {
		return;
	}

	if (is_content_visible(c)) {
		redrawwin(c->window);
		draw_content(c);
	}
	draw_border(c->win);
	wnoutrefresh(c->window);
}

static void
draw_layout(struct layout *L)
{
	struct window *w, *e;
	if( L != NULL ) {
		for( w = L->windows; w < L->windows + L->count; w++ ) {
			draw(w->c);
			draw_layout(w->layout);
		}
	}
}

void
draw_all(void)
{
	drawbar();
	if( state.current_view ) {
		draw_layout(state.current_view->layout);
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
	unsigned int m = 0;
	erase();
	attrset(NORMAL_ATTR);
	if(state.current_view) {
		render_layout(state.current_view->layout, 0, 0,
			available_height, available_width);
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
}

void
detach(struct client *c) {
	struct client *d;
	if (c->prev)
		c->prev->next = c->next;
	if (c->next) {
		c->next->prev = c->prev;
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


/* this is a tragic hack.  All of the functions
that manipulat clients ought instead to be manipulating
windows.  until we have enough changes in place to make
that happen, we need somehting like this.  NOte that eventually
it will be possible to have a client mapped to multiple
windows, but for now the mapping should be unique*/
struct window *
find_window(struct layout *lay, struct client *c)
{
	struct window *w;
	struct window *k;
	if( lay == NULL ) {
		return NULL;
	}
	for(w = lay->windows; w < lay->windows + lay->count; w++ ) {
		if( w->c == c ) {
			return w;
		} else if( ( k = find_window(w->layout, c)) != NULL ) {
			return k;
		}
	}
	return NULL;
}

void
focus(struct client *c) {
	if( c == NULL && state.current_view && state.current_view->vclients) {
		c = state.current_view->vclients[0];
	}
	if (sel == c)
		return;
	lastsel = sel;
	sel = c;
	if (lastsel) {
		lastsel->urgent = false;
		draw_border(lastsel->win);
		wnoutrefresh(lastsel->window);
	}

	if (c) {
		set_term_title(c->title);
		c->urgent = false;
		draw_border(c->win);
		wnoutrefresh(c->window);
	}
	curs_set(c && vt_cursor_visible(c->term));
	state.current_view->vfocus = find_window(state.current_view->layout, c);
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
	draw_border(c->win);
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
		draw_border(c->win);
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

void
sigchld_handler(int sig) {
	write(sigchld_pipe[1], "\0", 1);
}

static struct client *
for_each_client(int reset) {
	static struct view *v = NULL;
	static struct client **cp = NULL;
	while( !reset ) {
		while( *cp ) {
			return *cp++;
		}
		v += 1;
		if( v == state.views + state.viewcount ) {
			break;
		}
		cp = v->vclients;
	}
	v = state.views;
	cp = v->vclients;
	return NULL;
}

void
handle_sigchld() {
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG)) != 0) {
		struct client *c;
		if (pid == -1) {
			if (errno == ECHILD) {
				/* no more child processes */
				break;
			}
			eprint("waitpid: %s\n", strerror(errno));
			break;
		}

		for_each_client(1);
		while( (c = for_each_client(0)) != NULL ) {
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

	struct client *c = sel;
	if (is_content_visible(c)) {
		c->urgent = false;
		if (code == '\e')
			vt_write(c->term, buf, len);
		else
			vt_keypress(c->term, code);
		if (key != -1)
			vt_keypress(c->term, key);
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
internal_bind(enum mode m, int loop, unsigned char *keys, command *func,
	const char * args[])
{
	struct action a = {0};
	typeof(bindings) b;

	switch( m ) {
	case keypress_mode: b = bindings; break;;
	case command_mode: b = cmd_bindings; break;;
	}
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
	char mod_binding[] = { modifier_key, '\0' };
	char const *args[2] = { mod_binding, NULL };
	state.binding = bindings = xcalloc(1u << CHAR_BIT, sizeof *bindings);
	cmd_bindings = xcalloc(1u << CHAR_BIT, sizeof *cmd_bindings);
	internal_bind(keypress_mode, 0, (unsigned char *)mod_binding,
		transition_no_send, args );

	for( b = mod_bindings; b[0][0]; b++) {
		char **e = *b;
		command *cmd = get_function(e[1]);
		if( cmd == NULL ) {
			error(0, "couldn't find %s", e[1]);
		}
		const char *args[] = {e[2], e[3], e[4]};
		if( internal_bind(command_mode, 0, (unsigned char *)e[0], cmd, args) ) {
			error(0, "failed to bind to %s", e[1]);
		}
	}
	for( int i=0; i < 10; i++ ) {
		char *buf = xcalloc(2, 1);
		const char *args[] = { buf, NULL };
		buf[0] = '0' + i;
		if( internal_bind(command_mode, 0, (unsigned char *)buf, digit, args) ) {
			error(0, "failed to bind to '%d'", i);
		}
	}

	internal_bind(command_mode, 0, (unsigned char *)mod_binding,
		transition_with_send, args );
	for( b = keypress_bindings; b[0][0]; b++) {
		char **e = *b;
		command *cmd = get_function(e[1]);
		if( cmd == NULL ) {
			error(0, "couldn't find %s", e[1]);
		}
		const char *args[] = {e[2], e[3], e[4]};
		if( internal_bind(keypress_mode, 0, (unsigned char *)e[0], cmd, args) ) {
			error(0, "failed to bind to %s", e[1]);
		}
	}
}

const char *
scan_fmt(const char *d, struct window *w)
{
	/* expected format: "%gx%g@%g,%g " */
	char *end;

	w->p.h = strtod(d, &end);
	if(*end++ != 'x') {
		return NULL;
	}
	w->p.w = strtod(end, &end);
	if(*end++ != '@') {
		return NULL;
	}
	w->p.y = strtod(end, &end);
	if(*end++ != ',') {
		return NULL;
	}
	w->p.x = strtod(end, &end);
	if( ! strchr(" \n", *end)) {
		return NULL;
	}
	return end;
}

struct layout *
new_layout(struct client *c)
{
	struct layout *ret;

	ret = calloc(1, sizeof *ret);
	if( ret != NULL ) {
		ret->windows = calloc(ret->capacity = 32, sizeof *ret->windows);
		if( ret->windows == NULL ) {
			free(ret);
			ret = NULL;
		} else {
			ret->count = 1;
			ret->windows[0].p = (struct position){.y = 0, .x = 0, .h = 1.0, .w = 1.0};
			ret->windows[0].enclosing_layout = ret;
			ret->windows[0].c = c;
		}
	}
	return ret;
}

static void
clamp( char ** d, char *e, int count, size_t *r)
{
	if( *d + count > e ) {
		*d = e;
	} else {
		*d += count;
	}
	*r += count;
}

void
create_views(void)
{
	struct view *v;

	state.views = v = xcalloc(state.viewcount = 1, sizeof *v);
	v->layout = new_layout(NULL);
	if( v->layout == NULL) {
		error(0, "out of memory");
	}
	v->vclients = xcalloc(v->capacity = 32, sizeof *v->vclients);
	v->vfocus = v->layout->windows;
	state.current_view = v;
}

void
init_state(struct state *s)
{
	reset_entry(&s->buf);
	create_views();
}

void
setup(void) {
	build_bindings();
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
	return;
#if 0
	if (sel == c)
		focusnextnm(NULL);
	detach(c);
	if (sel == c) {
		struct client *next = nextvisible(clients);
		if (next) {
			focus(next);
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
#endif
}

void
cleanup(void) {
/*
	while (clients)
		destroy(clients);
		*/
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
	toggle_mode(NULL);
	return 0;
}

int
transition_with_send(const char * const args[])
{
	assert(state.mode == command_mode);
	keypress(state.code);
	toggle_mode(NULL);
	return 0;
}

int
toggle_borders(const char * const args[])
{
	state.hide_borders = !state.hide_borders;
	draw_all();
	return 0;
}

static int
add_client_to_view(struct view *v, struct client *c)
{
	assert( v != NULL );
	struct client **cl = v->vclients;
	int found = 0;

	for( ; *cl != NULL; cl++ ) {
		if( *cl == c ) {
			found = 1;
			break;
		}
	}
	if( ! found ) {
		assert( *cl == NULL );
		assert( cl - v->vclients < v->capacity );
		if( cl - v->vclients == v->capacity - 1 ) {
			struct client **t = realloc( v->vclients, (v->capacity + 32) * sizeof *t);
			if( t == NULL ) {
				return 0;
			}
			v->vclients = t;
			v->capacity += 32;
		}
		cl[0] = c;
		cl[1] = NULL;
	}

	return 1;
}

static struct layout *
get_current_layout(void)
{
	return state.current_view->vfocus->enclosing_layout;
}

static struct window *
split_current_window(void)
{
	struct window *w;
	struct circular_queue *cq, *n;
	double factor;
	struct layout *lay = get_current_layout();
	unsigned count = lay->count;
	if( lay->type == undetermined ) {
		assert( count == 1 );
		lay->type = column_layout;
	}
	factor = (double)count / ( count + 1 );
	for( w = lay->windows; w < lay->windows + count; w++ ) {
		switch(lay->type) {
		case undetermined: assert(0); break;
		case row_layout:
			assert( w->p.y == 0 );
			assert( w->p.h == 1.0 );
			w->p.x *= factor;
			w->p.w *= factor;
			break;
		case column_layout:
			assert( w->p.x == 0 );
			assert( w->p.w == 1.0 );
			w->p.y *= factor;
			w->p.h *= factor;
		}
	}
	if( w != NULL && count >= lay->capacity ) {
		struct window *tmp = realloc(lay->windows, sizeof *tmp * (lay->capacity += 32));
		if(tmp == NULL) {
			return NULL;
		}
		lay->windows = tmp;
	}
	if(lay->type == row_layout) {
		lay->windows[count].p = (struct position){.y = 0, .x = factor, .h = 1.0, .w = 1.0 - factor};
	} else {
		assert(lay->type == column_layout);
		lay->windows[count].p = (struct position){.y = factor, .x = 0, .h = 1.0 - factor, .w = 1.0};
	}
	lay->windows[count].enclosing_layout = lay;
	return lay->windows + lay->count++;
}


struct window *
find_empty_window(struct layout *layout)
{
	if( layout == NULL || layout->windows == NULL ) {
		return NULL;
	}
	struct window *w = layout->windows;
	for( ; w < layout->windows + layout->count; w++ ) {
		struct window *t;
		if( w->c == NULL && w->layout == NULL ) {
			return w;
		}
		if( (t = find_empty_window(w->layout)) != NULL ) {
			return t;
		}
	}
	return NULL;
}


static void
push_client_to_view(struct view *v, struct client *c)
{
	struct window *w;
	add_client_to_view(v, c);
	if( (w = find_empty_window(state.current_view->layout)) == NULL ) {
		w = split_current_window();
	}
	w->c = c;
	c->win = w;
}

int
split(const char * const args[])
{
	struct layout *lay = get_current_layout();
	struct window *w = state.current_view->vfocus;
	typeof(lay->type) t = column_layout;
	if( lay->type != undetermined ) {
		if( args[0] && args[0][0] == 'v' ) {
			t = row_layout;
		}
		if( lay->type != t ) {
			lay = w->layout = new_layout(w->c);
			state.current_view->vfocus = lay->windows;
			w->c = NULL;
		}
	}
	lay->type = t;
	create(NULL);
	return 1;
}

int
create(const char * const args[]) {
	const char *pargs[4] = { shell, NULL };
	char buf[8];
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
		return 1;
	c->tags = tagset[seltags];
	c->id = ++cmdfifo.id;
	snprintf(buf, sizeof buf, "%d", c->id);

	if (!(c->window = newwin(available_height, available_width, 0, 0))) {
		free(c);
		return 1;
	}

	c->term = c->app = vt_create(screen.h, screen.w, screen.history);
	if (!c->term) {
		delwin(c->window);
		free(c);
		return 1;
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

	c->pid = vt_forkpty(c->term, shell, pargs, NULL, env, NULL, NULL);
	vt_data_set(c->term, c);
	vt_title_handler_set(c->term, term_title_handler);
	vt_urgent_handler_set(c->term, term_urgent_handler);
	applycolorrules(c);
	c->x = 0;
	c->y = 0;
	debug("client with pid %d forked\n", c->pid);
	attach(c);
	push_client_to_view(state.current_view, c);
	focus(c);
	arrange();

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
toggle_mode(const char * const args[])
{
	struct state *s = &state;
	reset_entry(&s->buf);
	switch(s->mode) {
	case keypress_mode:
		s->mode = command_mode;
		s->binding = cmd_bindings;
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
	draw_border(sel->win);
	toggle_mode(NULL);
	return 0;
}

static struct client *
select_client(const struct view *v)
{
	struct client *c = sel;

	if( state.buf.count != 0 ) {
		struct client *t;
		if( v == NULL ) {
			struct view *v = state.views;
			for( ; v < state.views + state.viewcount; v++ ) {
				if( (t = select_client(v)) != NULL ) {
					c = t;
					break;
				}
			}
		} else {
			for( struct client **cp = v->vclients; *cp; cp++ ) {
				if( (*cp)->id == state.buf.count) {
					c = *cp;
					break;
				}
			}
		}
	}
	return c;
}

int
focusn(const char * const args[])
{
	struct client *c = select_client(state.current_view);
	if( c != NULL ) {
		focus(c);
		toggle_mode(NULL);
	}
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
	struct client *c = select_client(NULL);
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
	toggle_mode(NULL);
	return 0;
}

int
quit(const char * const args[]) {
	stop_requested = 1;
	return 0;
}

int
redraw(const char * const args[]) {
	struct client *c;
	for_each_client(1);
	while( (c = for_each_client(0)) != NULL ) {
		vt_dirty(c->term);
		wclear(c->window);
		wnoutrefresh(c->window);
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
	draw_border(c->win);
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

	new = xcalloc(1, sizeof *new);
	if( actions == NULL ) {
		actions = new;
	} else {
		struct action *a = actions;
		while( a->next != NULL ) {
			a = a->next;
		}
		a->next = new;
	}
	memcpy(new, act, sizeof *new);
	new->next = NULL;
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
check_client_fds(fd_set *rd, int *nfds)
{
	struct client *c;

	for_each_client(1);
	while( (c = for_each_client(0)) != NULL ) {
		int pty = c->editor ? vt_pty_get(c->editor) : vt_pty_get(c->app);
		set_fd_mask(pty, rd, nfds);
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
	} else if( b != NULL ) {
		*s->buf.next++ = code;
		*s->buf.next = '\0';
		if(b->action.cmd != NULL) {
			b->action.cmd(b->action.args);
			/* Some actions change s->mode. */
			if( s->mode == command_mode )  {
				s->binding = cmd_bindings;
			}
			if( b->action.cmd != digit ) {
				s->buf.count = 0;
			}
		} else {
			s->binding = b;
		}
	} else {
		s->binding = cmd_bindings;
	}
	if( s->binding == cmd_bindings && s->buf.count == 0 ) {
		reset_entry(&s->buf);
	}
	/* TODO: consider just using bar.text for the buffer */
	snprintf(bar.text, sizeof bar.text, "%s", s->buf.data);
	drawbar();
	draw(sel);
}

int
main(int argc, char *argv[])
{
	struct state *s = &state;

	parse_args(argc, argv);
	setup();
	init_state(&state);
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

		check_client_fds(&rd, &nfds);

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

		for_each_client(1);
		struct client *c;
		while( (c = for_each_client(0)) != NULL ) {
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


static void
render_layout(struct layout *lay, unsigned y, unsigned x, unsigned h, unsigned w)
{
	/* This shouldn't be necessary, but currently resize_screen()
	and arrange() are coupled, and we don't know the screen size
	so cannot initialize state until resize_screen() is called.
	But we cannot arrange until state is initialized.
	Until  that is decoupled, just do this check
	*/
	if(lay == NULL ) {
		return;
	}
	struct window *win = lay->windows;
	struct window *end = win + lay->count;
	for( ; win < end; win++ ) {
		struct position *p = &win->p;
		unsigned ny = y + p->y * h;
		unsigned nx = x + p->x * w;
		unsigned nh = p->h * h;
		unsigned nw = p->w * w;

		if( win->c ) {
			if( nx > 0 && nx < available_width ) {
				mvvline(ny, nx, ACS_VLINE, nh);
				mvaddch(ny, nx, ACS_TTEE);
				nx += 1;
				nw -= 1;
			}
			resize(win->c, nx, ny, nw, nh);
		} else if( win->layout ) {
			render_layout(win->layout, ny, nx, nh, nw);
		}
	}
}
