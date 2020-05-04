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
  Make it possible to pass layouts on the cmd fifo.  eg, give
    dimensions like "1:100x20@10,20\n2:hxw@y,x\n..."
  Write errors somewhere.  Either in a dedidcated window
    or in the status bar.  Write any final error to stderr
    on exit.  Probably should read initialization from stdin.
  Need to check errors in copymode.  If tm-editor is not in the
    path,for example, we should see an error message.  copymode needs
    to read the error stream and print somewhere.  (See previous TODO)
  Consider name change: eg 'stm', 'pyx', 'pyt', 'pym' (latter too pythonic)
  Make tagset nameable.  As soon as we do that, we basically have
  named tabs.

  Make the top-level entity a window with a layout rather than a layout
  with a window.

  Do error checking when the layout is crowded.  ie, either print
  an error message and do not create the new client, or ... basically,
  we need to finish implementing handling clients that aren't in a layout
  and put new clients there.

  Make cleanup slower.  That is, when a client dies, do not automatically
  reap the enclosing window.  This way, any error messages will stay
  visible.  Instead, consider a command to send signal and close window,
  another to just close the window (after the client is dead)
 */
#include "config.h"
#include "package.h"

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
static struct client * select_client(const struct view *);

const struct color_rule colorrules[] = {
	{ "", A_NORMAL, &colors[DEFAULT] },
};

void cleanup(void);
void push_action(const struct action *a);
static void reset_entry(struct entry_buf *);

struct action *actions = NULL; /* actions are executed at startup  */

struct screen screen = { .history = SCROLL_HISTORY };

unsigned int seltags;
static unsigned id;
struct data_buffer copyreg;
int signal_pipe[] = {-1, -1};

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

void
draw_border(struct window *w) {
	struct client *c = w->c;
	struct client *f = NULL;
	int attrs = NORMAL_ATTR;
	char border_title[128];
	char *msg = NULL;
	chtype fill = ACS_HLINE;

	assert( c != NULL );
	if( w->title == NULL ) {
		return;
	}

	char *title = c->title;

	if( state.current_view->vfocus != NULL ) {
		f = state.current_view->vfocus->c;
	}
	if( f == c ) {
		attrs = c->term == c->editor ? COLOR(CYAN) : COLOR(BLUE);
		attrs |= A_NORMAL;
	}
	if( state.mode == command_mode ) {
		fill = ACS_BLOCK;
	} else if( c->term == c->editor ) {
		msg = " COPY MODE ";
		title = c->editor_title;
		fill = ACS_CKBOARD;
	}

	mvwhline(w->title->window, 0, 0, fill, c->p.w);
	snprintf(border_title, MIN(c->p.w, sizeof border_title),
		"%s%s#%d (%ld)",
		c->p.w > 32 ? title : "",
		c->p.w > 32 ? " | " : "",
		c->id,
		(long)c->pid
	);
	mvwprintw(w->title->window, 0, 2, "[%s]", border_title);
	if( msg != NULL ) {
		int start = strlen(border_title) + 4 + 2;
		if( c->p.w > start + strlen(msg) + 2 ) {
			mvwprintw(w->title->window, 0, start, "%s", msg);
		}
	}
	wnoutrefresh(w->title->window);
}


void
draw(struct client *c) {
	if( c == NULL ) {
		return;
	}
	struct window *w = c->win;
	assert( w != NULL );
	unsigned vline = c->p.x > 0 && c->p.x < screen.w;
	redrawwin(c->window);
	vt_draw(c->term, c->window, 0, 0);
	draw_border(w);
	if( w->div ) {
		mvwvline(w->div->window, 0, 0, ACS_VLINE, c->p.h);
		mvwaddch(w->div->window, w->div->p.h - 1, 0, ACS_BTEE);
		wnoutrefresh(c->win->div->window);
	}
	wnoutrefresh(c->window);
}

static void
draw_window(struct window *w)
{
	for( ; w; w = w->next ) {
		draw(w->c);
		if( w->layout ) {
			draw_window(w->layout->windows);
		}
	}
}

void
arrange(void) {
	unsigned int m = 0;
	erase();
	attrset(NORMAL_ATTR);
	if(state.current_view) {
		render_layout(state.current_view->layout, 0, 0,
			screen.h, screen.w);
		focus(NULL);
		wnoutrefresh(stdscr);
		if(state.current_view->layout) {
			draw_window(state.current_view->layout->windows);
		}
	}
}

static void
set_term_title(char *title)
{
	if( title ) {
		printf("\033]0;%s\007", title);
		fflush(stdout);
	}
}


void
focus(struct client *c) {
	struct window *old = NULL;
	if( state.current_view == NULL ) {
		return;
	}
	if( c && state.current_view->vfocus == c->win ) {
		return;
	}
	if( c == NULL && state.current_view->vfocus ) {
		c = state.current_view->vfocus->c;
	}
	if( c == NULL ) {
		return;
	}
	if( state.current_view->vfocus && c->win != state.current_view->vfocus ) {
		old = state.current_view->vfocus;
	}
	state.current_view->vfocus = c->win;
	if (c) {
		set_term_title(c->title);
		c->urgent = false;
		draw_border(c->win);
		wnoutrefresh(c->window);
	}
	if( old ) {
		draw_border(old);
		wnoutrefresh(old->c->window);
	}
	curs_set(c && vt_cursor_visible(c->term));
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
	struct client *f = state.current_view->vfocus->c;
	sanitize_string(title, c->title, sizeof c->title);
	if( f == c ) {
		set_term_title(c->title);
	}
	draw_border(c->win);
	applycolorrules(c);
}

void
term_urgent_handler(Vt *term)
{
	struct client *c = (struct client *)vt_data_get(term);
	c->urgent = true;
	printf("\a");
	fflush(stdout);
}

void
move_client(struct client *c, int x, int y) {
	if (c->p.x == x && c->p.y == y)
		return;
	debug("moving, x: %d y: %d\n", x, y);
	if (mvwin(c->window, y, x) == ERR) {
		eprint("error moving, x: %d y: %d\n", x, y);
	} else {
		c->p.x = x;
		c->p.y = y;
	}
}

void
resize_client(struct client *c, int w, int h)
{
	if( c->p.w != w || c->p.h != h ) {
		if( wresize(c->window, h, w) == ERR ) {
			eprint("error resizing, w: %d h: %d\n", w, h);
		} else {
			c->p.w = w;
			c->p.h = h;
		}
		vt_resize(c->app, h, w);
		if( c->editor ) {
			vt_resize(c->editor, h, w);
		}
	}
}

static struct client *
for_each_client(int reset) {
	static struct client *c = NULL;
	while( !reset && c != NULL ) {
		struct client *p = c;
		c = c->next;
		return p;
	}
	c = state.clients;
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
signal_handler(int sig)
{
	write(signal_pipe[1], &sig, sizeof sig);
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

void
tagschanged() {
	arrange();
}

void
keypress(int code) {
	int key = -1;
	unsigned int len = 1;
	char buf[8] = { '\e' };

	if( state.current_view->vfocus == NULL ) {
		return;
	}

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
	struct client *c = state.current_view->vfocus->c;
	if( c ) {
		assert( c-> win != NULL );
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
	const char *shell = state.shell = getenv("SHELL");

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
	return state.shell = shell;
}

static void
set_non_blocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if( flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1 ) {
		error(1, "fcntl");
	}
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

int
push_binding(struct key_binding *b, const unsigned char *keys, const struct action *a)
{
	struct key_binding *t = b + keys[0];
	if( t->action.cmd != NULL ) {
		return 1; /* conflicting binding */
	}
	if( keys[1] ) {
		if( t->next == NULL ) {
			t->next = xcalloc(1u << CHAR_BIT, sizeof *t->next);
		}
		push_binding(t->next, keys + 1, a);
	} else {
		if( t->next != NULL ) {
			return 1; /* conflicting binding */
		}
		memcpy(&t->action, a, sizeof t->action);
	}
	return 0;
}

/*
 * Wrappers around the external facing bind(), which
 * may at some point be used as a command to allow
 * run time override of bingings.  The data in args
 * is *not* copied, so the caller must ensure that
 * they are non-volatile. (eg, don't pass a stack variable).
 */
static int
internal_bind(enum mode m, unsigned char *keys, command *f, const char * args[])
{
	struct action a = {0};
	typeof(bindings) b;

	switch( m ) {
	case keypress_mode: b = bindings; break;;
	case command_mode: b = cmd_bindings; break;;
	}
	a.cmd = f;
	for(int i = 0; *args && i < MAX_ARGS; args++ ) {
		a.args[i] = *args;
	}
	return push_binding(b, keys, &a);
}
static int
xinternal_bind(enum mode m, unsigned char *keys, command *f, const char *args[])
{
	if( internal_bind(m, keys, f, args) ) {
		error(0, "conflicting binding for '%s'", keys);
	}
}

static void
build_bindings(void)
{
	binding_description *b = mod_bindings;
	char mod_binding[] = { modifier_key, '\0' };
	char const *args[2] = { mod_binding, NULL };
	state.binding = bindings = xcalloc(1u << CHAR_BIT, sizeof *bindings);
	cmd_bindings = xcalloc(1u << CHAR_BIT, sizeof *cmd_bindings);
	xinternal_bind(keypress_mode, (unsigned char *)mod_binding,
		transition_no_send, args );

	for( b = mod_bindings; b[0][0]; b++) {
		char **e = *b;
		command *cmd = get_function(e[1]);
		if( cmd == NULL ) {
			error(0, "couldn't find %s", e[1]);
		}
		const char *args[] = {e[2], e[3], e[4]};
		xinternal_bind(command_mode, (unsigned char *)e[0], cmd, args);
	}
	for( int i=0; i < 10; i++ ) {
		char *buf = xcalloc(2, 1);
		const char *args[] = { buf, NULL };
		buf[0] = '0' + i;
		xinternal_bind(command_mode, (unsigned char *)buf, digit, args);
	}

	xinternal_bind(command_mode, (unsigned char *)mod_binding,
		transition_with_send, args );
	for( b = keypress_bindings; b[0][0]; b++) {
		char **e = *b;
		command *cmd = get_function(e[1]);
		if( cmd == NULL ) {
			error(0, "couldn't find %s", e[1]);
		}
		const char *args[] = {e[2], e[3], e[4]};
		xinternal_bind(keypress_mode, (unsigned char *)e[0], cmd, args);
	}
}

#if 0
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
#endif

struct window *
new_window(struct layout *parent, struct client *c)
{
	struct window *w = calloc(1, sizeof *w);
	assert( parent != NULL );
	if( w != NULL ) {
		w->portion = 1.0;
		w->c = c;
		w->enclosing_layout = parent;
		if( c != NULL ) {
			c->win = w;
		}
		parent->count += 1;
	}
	return w;
}

struct layout *
new_layout(struct window *w)
{
	struct layout *ret;

	ret = calloc(1, sizeof *ret);
	if( ret != NULL ) {
		struct client *c = NULL;
		if( w != NULL ) {
			c = w->c;
			assert( c == NULL || c->win == w );
		}
		if( (ret->windows = new_window(ret, c)) == NULL ) {
			free(ret);
			ret = NULL;
		}
		ret->parent = w;
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
init_state(struct state *s)
{
	reset_entry(&s->buf);
	s->views = xcalloc(s->viewcount = 1, sizeof *s->views);
	s->views->layout = new_layout(NULL);
	if( s->views->layout == NULL) {
		error(0, "out of memory");
	}
	s->views->vfocus = s->views->layout->windows;
	s->current_view = s->views;
}

static void
handle(int s, void(*h)(int))
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = h;
	if( sigaction(s, &sa, NULL) == -1 ) {
		error(1, "sigaction");
	}
}

void
setup(void) {
	build_bindings();
	getshell();
	setlocale(LC_CTYPE, "");
	setenv("TM_VERSION", VERSION, 1);
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

	if( pipe(signal_pipe) < 0 ) {
		error(1, "pipe");
	}
	set_non_blocking(signal_pipe[0]);
	set_non_blocking(signal_pipe[1]);
	handle(SIGWINCH, signal_handler);
	handle(SIGCHLD, signal_handler);
	handle(SIGTERM, signal_handler);
	handle(SIGPIPE, SIG_IGN);
}

void
destroy(struct client *c) {
	struct view *v = state.current_view;

	if( state.current_view->vfocus->c == c ) {
		state.current_view->vfocus = NULL;
	}
	werase(c->window);
	wnoutrefresh(c->window);
	vt_destroy(c->term);
	delwin(c->window);
	if(state.clients == c) {
		state.clients = c->next;
	} else for( struct client *cp = state.clients; cp; cp = cp->next ) {
		if( cp->next == c ) {
			cp->next = c->next;
		}
	}
	if(state.clients != NULL) {
		if( state.current_view->vfocus == NULL ) {
			state.current_view->vfocus = state.clients->win;
		}
	} else {
		state.stop_requested = 1;
	}
	if( c->win ) {
		c->win->c = NULL;
	}
	free(c);
	arrange();
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
}

static struct layout *
get_layout(struct window *w)
{
	if( w == NULL ) {
		w = state.current_view->vfocus;
	}
	return w->layout ? w->layout : w->enclosing_layout;
}

static struct window *
split_window(struct window *target)
{
	struct window *w;
	double factor;
	int found = 0;
	struct layout *lay = target->layout ? target->layout : target->enclosing_layout;
	unsigned count = lay->count;
	struct window *ret = new_window(lay, NULL); /* increments lay->count */
	unsigned index = 0;

	if( ret == NULL ) {
		return NULL;
	}
	if( lay->type == undetermined ) {
		assert( count == 1 );
		lay->type = column_layout;
	}
	assert( count > 0 );
	factor = (double)count / ( count + 1 );
	assert( factor >= .5 );
	for( w = lay->windows; w; w = w->next ) {
		assert(w->enclosing_layout = lay);
		w->portion *= factor;
		index += found;
		if( w == target ) {
			ret->next = w->next;
			w->next = ret;
			ret->prev = w;
			if( ret->next ) {
				ret->next->prev = ret;
			}
			w = ret;
			found = 1;
		}
	}
	assert( found );  /* target must be in the list */
	ret->portion = 1.0 - factor;
	ret->enclosing_layout = lay;
	ret->layout = NULL;
	return ret;
}


struct window *
find_empty_window(struct layout *layout)
{
	if( layout == NULL || layout->windows == NULL ) {
		return NULL;
	}
	struct window *w = layout->windows;
	for( ; w; w = w->next ) {
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
	if( (w = find_empty_window(state.current_view->layout)) == NULL ) {
		w = split_window(state.current_view->vfocus);
	}
	w->c = c;
	c->win = w;
}

int
split(const char * const args[])
{
	/* TODO: we should be selecting windows, not clients.
	   That is, we should hoist c->id to w->id
	*/
	struct client *c = select_client(state.current_view);
	if( c == NULL ) {
		return -1;
	}
	const char *pargs[] = { NULL, NULL };
	struct window *w = c->win;
	struct layout *lay = get_layout(w);
	typeof(lay->type) t = column_layout;
	if( args[0] && args[0][0] == 'v' ) {
		t = row_layout;
	}
	state.current_view->vfocus = w;
	if( lay->type != undetermined ) {
		if( lay->type != t ) {
			lay = w->layout = new_layout(w);
			state.current_view->vfocus = lay->windows;
			w->c = NULL;
		}
	}
	lay->type = t;
	create(pargs);
	return 0;
}

struct client *
new_client(const char *cmd, const char *title)
{
	const char *pargs[4] = { state.shell, cmd ? "-c" : NULL, cmd, NULL };
	char buf[8];
	const char *env[] = { "TM_WINDOW_ID", buf, NULL };
	struct client *c = calloc(1, sizeof *c);
	if( c != NULL ) {
		if( (c->window = newwin(screen.h, screen.w, 0, 0)) == NULL ) {
			goto err1;
		}
		c->term = c->app = vt_create(screen.h, screen.w, screen.history);
		if( c->term == NULL ) {
			goto err2;
		}
		c->cmd = cmd ? cmd : state.shell;
		sanitize_string(title ? title : c->cmd, c->title, sizeof c->title);
		c->id = ++id;
		snprintf(buf, sizeof buf, "%d", c->id);
		c->pid = vt_forkpty(c->term, state.shell, pargs, NULL, env, NULL, NULL);
		vt_data_set(c->term, c);
		vt_title_handler_set(c->term, term_title_handler);
		vt_urgent_handler_set(c->term, term_urgent_handler);
		applycolorrules(c);
		c->p.x = 0;
		c->p.y = 0;
	}
	return c;
err2:
	delwin(c->window);
err1:
	free(c);
	return NULL;
}

int
create(const char * const args[]) {
	struct client *c = new_client(args[0], args[1]);
	if( c == NULL ) {
		return 1;
	}
	push_client_to_view(state.current_view, c);
	c->next = state.clients;
	state.clients = c;
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
	if(state.current_view->layout) {
		draw_window(state.current_view->layout->windows);
	}
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
	struct client *f = state.current_view->vfocus->c;

	if (!args || !args[0] || !f || f->editor)
		goto end;

	bool colored = strstr(args[0], "pager") != NULL;
	assert(f == state.current_view->vfocus->c);

	/* subtract 1 for title line */
	if (!(f->editor = vt_create(f->p.h - 1, f->p.w, 0)))
		goto end;

	int *to = &f->editor_fds[0];
	int *from = strstr(args[0], "editor") ? &f->editor_fds[1] : NULL;
	f->editor_fds[0] = f->editor_fds[1] = -1;

	const char *argv[3] = { args[0], NULL, NULL };
	char argline[32];
	int line = vt_content_start(f->app);
	snprintf(argline, sizeof(argline), "+%d", line);
	argv[1] = argline;

	if (vt_forkpty(f->editor, args[0], argv, NULL, NULL, to, from) < 0) {
		vt_destroy(f->editor);
		f->editor = NULL;
		goto end;;
	}

	f->term = f->editor;

	if (f->editor_fds[0] != -1) {
		char *buf = NULL;
		size_t len;

		sanitize_string(args[0], f->editor_title, sizeof f->editor_title);

		if( args[1] && !strcmp(args[1], "bindings") ) {
			len = get_bindings(&buf);
		} else {
			len = vt_content_get(f->app, &buf, colored);
		}
		char *cur = buf;
		while (len > 0) {
			ssize_t res = write(f->editor_fds[0], cur, len);
			if (res < 0) {
				if (errno == EAGAIN || errno == EINTR)
					continue;
				break;
			}
			cur += res;
			len -= res;
		}
		free(buf);
		close(f->editor_fds[0]);
		f->editor_fds[0] = -1;
	}

end:
	assert(state.mode == command_mode);
	draw_border(f->win);
	toggle_mode(NULL);
	return 0;
}

static struct client *
select_client(const struct view *v)
{
	struct client *c = NULL;
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
			for( struct client *cp = state.clients; cp; cp = cp->next ) {
				if( cp->id == state.buf.count) {
					c = cp;
					break;
				}
			}
		}
	} else if( state.current_view->vfocus != NULL ) {
		c = state.current_view->vfocus->c;
	}
	return c;
}

int
focus_transition(const char * const args[])
{
	struct client *c = select_client(state.current_view);
	if( c != NULL ) {
		focus(c);
		toggle_mode(NULL);
	}
	return 0;
}

int
focusn(const char * const args[])
{
	struct client *c = select_client(state.current_view);
	if( c != NULL ) {
		focus(c);
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
	struct client *c = state.current_view->vfocus->c;
	if( c ) {
		kill( -c->pid, signal);
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
paste(const char * const args[])
{
	struct client *f = state.current_view->vfocus->c;
	if( f && copyreg.data ) {
		trim_whitespace(&copyreg);
		vt_write(f->term, copyreg.data, copyreg.len);
	}
	assert(state.mode == command_mode);
	toggle_mode(NULL);
	return 0;
}

int
quit(const char * const args[]) {
	state.stop_requested = 1;
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
scrollback(const char * const args[])
{
	double pages = args[0] ? strtod(args[0], NULL) : -0.5;
	struct client *f = state.current_view->vfocus->c;
	if( f == NULL || f->win == NULL ) {
		return -1;
	}
	if( state.buf.count ) {
		pages *= state.buf.count;
	}
	vt_scroll(f->term,  pages * f->p.h);
	draw(f);
	curs_set(vt_cursor_visible(f->term));
	return 0;
}

int
send(const char * const args[])
{
	struct client *f = state.current_view->vfocus->c;
	if( f && args && args[0] ) {
		vt_write(f->term, args[0], strlen(args[0]));
	}
	return 0;
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
	vt_draw(c->term, c->window, 0, 0);
	draw_border(c->win);
	wnoutrefresh(c->window);
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
		if( strchr("drtm", arg[1]) != NULL && argv[1] == NULL ) {
			error(0, "%s requires an argument (-h for usage)", arg);
		}
		switch (arg[1]) {
		case 'h':
			printf("usage: %s [-v] [-h] [-f] [-m mod] [-d delay] "
				"[-r lines] [-t title] "
				"[cmd...]\n", basename);
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
			state.title = *++argv;
			break;
		default:
			error(0, "unknown option: %s (-h for usage)", arg);
		}
	}
	if( actions == NULL ) {
		struct action defaults = { create, { NULL } };
		push_action(&defaults);
	}
	if( getenv("TM_VERSION") && ! force ) {
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
cleanup_dead_clients(void)
{
	struct client *c;
	for_each_client(1);
	while( (c = for_each_client(0)) != NULL ) {
		if( c->editor && c->editor_died ) {
			handle_editor(c);
		}
		if( !c->editor && c->died ) {
			destroy(c);
			continue;
		}
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
}

static void
reset_cursor(void)
{
	struct view *v = state.current_view;;

	if( v && v->vfocus && v->vfocus->c ) {
		struct client *c = v->vfocus->c;
		int y, x;
		/*
		getyx(c->window, y, x);
		wmove(stdscr, y + c->p.y, x + c->p.x);
		wnoutrefresh(stdscr);
		*/
		wnoutrefresh(c->window);
	}
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

	while( !state.stop_requested ) {
		int r, nfds = 0;
		fd_set rd;

		FD_ZERO(&rd);
		set_fd_mask(STDIN_FILENO, &rd, &nfds);
		set_fd_mask(signal_pipe[0], &rd, &nfds);

		check_client_fds(&rd, &nfds);

		reset_cursor();
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

		if( FD_ISSET(signal_pipe[0], &rd) ) {
			int s;
			ssize_t rc;
			while( (rc = read(signal_pipe[0], &s, sizeof s)) == sizeof s
					|| (rc == -1 && errno == EINTR)) {
				if( rc == sizeof s) switch(s) {
				case SIGWINCH: screen.winched = 1; break;
				case SIGCHLD: handle_sigchld(); break;
				case SIGTERM: state.stop_requested = 1; break;
				default: assert(0);
				}
			}
			if( rc < 0 && errno != EAGAIN && errno != EWOULDBLOCK ) {
				error(1, "read from sigwinch pipe");
			}
		}

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
			if( c->win ) {
				vt_draw(c->term, c->window, 0, 0);
				wnoutrefresh(c->window);
			}
		}

		cleanup_dead_clients();
		if( screen.winched ) {
			resize_screen();
		}
	}

	cleanup();
	return 0;
}

static void
status_window(struct status_window **n, int y, int x, int h, int w)
{
	struct position p = { y, x, h, w };
	struct status_window *win = *n;
	if( win ) {
		wresize(win->window, h, w);
		mvwin(win->window, y, x);
	} else {
		win = *n = xcalloc(1 , sizeof **n);
		win->window = newwin(h, w, y, x);
	}
	win->p = p;
}

static void
render_layout(struct layout *lay, unsigned y, unsigned x, unsigned h, unsigned w)
{
	if( lay == NULL || w == 0 || h == 0 ) {
		return;
	}
	int row = lay->type == row_layout;
	unsigned consumed = 0;
	for( struct window *win = lay->windows; win; win = win->next ) {
		int last = win->next == NULL;
		unsigned count, nh, nw;
		unsigned unit = row ? w : h;
		assert( win->portion > 0.0 && win->portion <= 1.0 );
		count = last ? unit - consumed : win->portion * unit;
		nw = row ? count : w;
		nh = row ? h : count;

		if( win->c ) {
			if( row && x > 0 ) {
				status_window( &win->div, y, x, nh, 1 );
				nw -= 1;
				x += 1;
			}
			status_window( &win->title, y + nh - 1, x, 1, nw );
			nh -= 1;
			assert( win->layout == NULL );
			resize_client(win->c, nw, nh);
			move_client(win->c, x, y);
		} else if( win->layout ) {
			render_layout(win->layout, y, x, nh, nw);
		}
		y += row ? 0 : count;
		x += row ? nw : 0;
		consumed += count;
	}
	assert( row || consumed == h );
	assert( !row || consumed == w );
}
