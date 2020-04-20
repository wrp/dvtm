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
 *   Make it possible to list current key bindings.
 *   Add ability to do simple commands without command mode.
 *     eg, window navigation commands.  Maybe have a command
 *     to enter command mode rather than always going in, or
 *     have 2 prefix sequences.
 *   Make layout more flexible, perhaps dlopenable.
 *
 *   Make it possible to pass layouts on the cmd fifo.  eg, give
 * dimensions like "1:100x20@10,20\n2:hxw@y,x\n..."
 *
 * Cleanup count (eg, focusn works, but implementation is hacky)
 *
 * Probably a layout stack, per tag.
 * Implement bind command
 */
#include "config.h"
#include "mvtm.h"

struct key_binding *bindings;
struct state state;
struct color colors[] = {
	[DEFAULT] = { .fg = -1,         .bg = -1, .fg256 =  -1, .bg256 = -1, },
	[BLUE]    = { .fg = COLOR_BLUE, .bg = -1, .fg256 =  68, .bg256 = -1, },
	[RED]     = { .fg = COLOR_RED,  .bg = -1, .fg256 =   9, .bg256 = -1, },
};

extern void wstack(void);
struct layout layouts[] = {
	{ "---", wstack },
	{ "[ ]", fullscreen },
};

unsigned modifier_key = CTRL('g');

const struct color_rule colorrules[] = {
	{ "", A_NORMAL, &colors[DEFAULT] },
};

/* Commands which can be invoked via the cmdfifo */
struct command commands[] = {
	{ "create", { create,	{ NULL } } },
	{ "focus",  { focusid,	{ NULL } } },
	{ "tag",    { tagid,	{ NULL } } },
};

void cleanup(void);
void push_action(const struct action *a);

unsigned available_width;  /* width of total available screen real estate */
unsigned available_height; /* height of total available screen real estate */
unsigned main_window_x;   /* x coord (right) of upper left corner of main window */
unsigned main_window_y;   /* y coord (down) of upper left corner of main window */
struct client *clients = NULL;
	/* clients appears to be the list of all clients, and only
	those marked with the current tagset are displayed.  HOwever, it is not clear
	how that set differs from stack, nor the purpose of the checks that the
	current layout is fullscreen */
char *title;

struct action *actions = NULL; /* actions are executed when mvtm is started */

struct screen screen = { .mfact = MFACT, .nmaster = NMASTER, .history = SCROLL_HISTORY };

struct client *stack = NULL;
struct client *sel = NULL;
struct client *lastsel = NULL;
unsigned int seltags;
unsigned int tagset[2] = { 1, 1 };
struct layout *layout = layouts;
StatusBar bar = { .fd = -1, .hidden = 0, .autohide = 1, .h = 1 };
CmdFifo cmdfifo = { .fd = -1 };
const char *shell;
Register copyreg;
volatile sig_atomic_t stop_requested = 0;
bool runinall = false;
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
isarrange(void (*func)()) {
	return func == layout->arrange;
}

bool
isvisible(struct client *c) {
	return c->tags & tagset[seltags];
}

bool
is_content_visible(struct client *c) {
	if (!c)
		return false;
	if (isarrange(fullscreen))
		return sel == c;
	return isvisible(c) && !c->minimized;
}

struct client*
nextvisible(struct client *c) {
	for (; c && !isvisible(c); c = c->next);
	return c;
}

void
updatebarpos(void) {
	bar.y = 0;
	main_window_x = 0;
	main_window_y = 0;
	available_height = screen.h;
	available_width = screen.w;
	available_height -= bar.h;
	bar.y = available_height;
}

void
drawbar(void) {
	int sx, sy, x, y, width;
	unsigned int occupied = 0, urgent = 0;
	if( bar.hidden )
		return;

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

	attrset(runinall ? TAG_SEL : TAG_NORMAL);

	if( state.mode == command_mode ) {
		attrset(COLOR(RED) | A_REVERSE);
	} else {
		attrset(COLOR(DEFAULT) | A_NORMAL);
	}
	addstr(layout->symbol);

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
	return !bar.hidden || (clients && clients->next);
}

void
draw_border(struct client *c) {
	int x, y, attrs = NORMAL_ATTR;
	char border_title[128];

	if (!show_border())
		return;
	if (sel != c && c->urgent)
		attrs = URGENT_ATTR;
	if (sel == c || (runinall && !c->minimized))
		attrs = SELECTED_ATTR;

	wattrset(c->window, attrs);
	getyx(c->window, y, x);
	mvwhline(c->window, 0, 0, ACS_HLINE, c->w);

	snprintf(border_title, sizeof border_title, "%s%s#%d (%ld)",
		*c->title ? c->title : "",
		*c->title ? " | " : "",
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
	if (!isarrange(fullscreen) || sel == c)
		draw_border(c);
	wnoutrefresh(c->window);
}

void
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
		for (struct client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
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
	if (bar.fd == -1 && bar.autohide) {
		if ((!clients || !clients->next) && n == 1)
			bar.hidden = 1;
		else
			bar.hidden = 0;
		updatebarpos();
	}
	if (m && !isarrange(fullscreen))
		available_height--;
	layout->arrange();
	if (m && !isarrange(fullscreen)) {
		unsigned int i = 0, nw = available_width / m, nx = main_window_x;
		for (struct client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
			if (c->minimized) {
				resize(c, nx, main_window_y+available_height, ++i == m ? available_width - nx : nw, 1);
				nx += nw;
			}
		}
		available_height++;
	}
	focus(NULL);
	wnoutrefresh(stdscr);
	drawbar();
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
		if (!isarrange(fullscreen)) {
			draw_border(lastsel);
			wnoutrefresh(lastsel->window);
		}
	}

	if (c) {
		detachstack(c);
		attachstack(c);
		set_term_title(c->title);
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

static void set_client_title(struct client *c, const char *title) {
	char *d = c->title;
	char *e = c->title + sizeof c->title - 1;
	for( ; title && *title && d < e; title += 1 ) {
		if( isprint(*title) && ! isspace(*title)) {
			*d++ = *title;
		} else if( d > c->title && d[-1] != ' ') {
			*d++ = ' ';
		}
	}
	*d = '\0';
}

void
term_title_handler(Vt *term, const char *title) {
	struct client *c = (struct client *)vt_data_get(term);
	set_client_title(c, title);
	if( sel == c ) {
		set_term_title(c->title);
	}
	if (!isarrange(fullscreen) || sel == c)
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
	if (!isarrange(fullscreen) && sel != c && isvisible(c))
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
	if (y < main_window_y || y >= main_window_y+available_height)
		return NULL;
	if (isarrange(fullscreen))
		return sel;
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

struct key_binding *
keybinding(const struct state *s)
{
	char *end;
	int count = strtol(s->entry_buf, &end, 10);
	struct key_binding *b = bindings + *end;
	int i;

	assert(bindings != NULL);
	for( end += 1; *end && b->next; end++ ) {
		b = b->next + *end;
	}
	return b;
}

unsigned int
bitoftag(const char *tag) {
	unsigned t = tag ? 0 : ~0;
	if( tag && strchr("123456789", *tag) ) {
		t = 1 << (*tag - '1');
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


void
untag(const char *args[]) {
	if( sel ) {
		sel->tags = 1;
		tagschanged();
	}
}

void
tag(const char *args[]) {
	if (!sel)
		return;
	sel->tags |= bitoftag(args[0]);
	tagschanged();
}

void
tagid(const char *args[]) {
	if (!args[0] || !args[1])
		return;

	const int win_id = atoi(args[0]);
	for (struct client *c = clients; c; c = c->next) {
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

void
toggletag(const char *args[]) {
	if (!sel)
		return;
	unsigned int newtags = sel->tags ^ (bitoftag(args[0]));
	if (newtags) {
		sel->tags = newtags;
		tagschanged();
	}
}

void
toggleview(const char *args[]) {
	unsigned int newtagset = tagset[seltags] ^ (bitoftag(args[0]));
	if (newtagset) {
		tagset[seltags] = newtagset;
		tagschanged();
	}
}

void
view(const char *args[]) {
	unsigned int newtagset = bitoftag(args[0]);
	if (tagset[seltags] != newtagset && newtagset) {
		seltags ^= 1; /* toggle sel tagset */
		tagset[seltags] = newtagset;
		tagschanged();
	}
}

void
viewprevtag(const char *args[]) {
	seltags ^= 1;
	tagschanged();
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

	for (struct client *c = runinall ? nextvisible(clients) : sel; c; c = nextvisible(c->next)) {
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
		memcpy(&t->action, a, sizeof t->action);
		t->next = NULL;
	}
	return 0;
}

static void
build_bindings(void)
{
	typeof(*binding_desc) *b = binding_desc;
	bindings = xcalloc(1u << CHAR_BIT, sizeof *bindings);
	for( ; (*b)[0]; b++) {
		const char *arr[] = {(*b)[0], (*b)[1], (*b)[2], (*b)[3], (*b)[4]};
		bind(arr);
	}
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

void
bind(const char *args[])
{
	const char *binding = args[0];
	struct action a = {0};
	const char *t;

	a.cmd = get_function(args[1]);
	for(int i = 0; i < 3; i++ ) {
		a.args[i] = args[i+2];
	}
	push_binding(bindings, binding, &a);
}


void
create(const char *args[]) {
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
		return;
	c->tags = tagset[seltags];
	c->id = ++cmdfifo.id;
	snprintf(buf, sizeof buf, "%d", c->id);

	if (!(c->window = newwin(available_height, available_width, main_window_y, main_window_x))) {
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
	} else {
		c->cmd = shell;
	}

	if( args && args[1] ) {
		set_client_title(c, args[1]);
	} else {
		set_client_title(c, c->cmd);
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
	c->x = main_window_x;
	c->y = main_window_y;
	debug("client with pid %d forked\n", c->pid);
	attach(c);
	focus(c);
	arrange();

	if( args && args[2] && ! strcmp(args[2], "master") ) {
		const char *args[2] = { "+1", NULL };
		incnmaster(args);
	}
}

void
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

void
focusn(const char *args[]) {
	char *end;
	int target = strtol( state.entry_buf, &end, 10);
	for (struct client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (c->id == target) {
			focus(c);
			if (c->minimized)
				toggleminimize(NULL);
			return;
		}
	}
}

void
focusid(const char *args[]) {
	if (!args[0])
		return;

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
			return;
		}
	}
}

void
focusnext(const char *args[]) {
	struct client *c;
	if (!sel)
		return;
	for (c = sel->next; c && !isvisible(c); c = c->next);
	if (!c)
		for (c = clients; c && !isvisible(c); c = c->next);
	if (c)
		focus(c);
}

void
focusnextnm(const char *args[]) {
	if (!sel)
		return;
	struct client *c = sel;
	do {
		c = nextvisible(c->next);
		if (!c)
			c = nextvisible(clients);
	} while (c->minimized && c != sel);
	focus(c);
}

void
focusprev(const char *args[]) {
	struct client *c;
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

void
focusprevnm(const char *args[]) {
	if (!sel)
		return;
	struct client *c = sel;
	do {
		for (c = c->prev; c && !isvisible(c); c = c->prev);
		if (!c) {
			for (c = clients; c && c->next; c = c->next);
			for (; c && !isvisible(c); c = c->prev);
		}
	} while (c && c != sel && c->minimized);
	focus(c);
}

void
focuslast(const char *args[]) {
	if (lastsel)
		focus(lastsel);
}

void
focusup(const char *args[]) {
	if (!sel)
		return;
	/* avoid vertical separator, hence +1 in x direction */
	struct client *c = get_client_by_coord(sel->x + 1, sel->y - 1);
	if (c)
		focus(c);
	else
		focusprev(args);
}

void
focusdown(const char *args[]) {
	if (!sel)
		return;
	struct client *c = get_client_by_coord(sel->x, sel->y + sel->h);
	if (c)
		focus(c);
	else
		focusnext(args);
}

void
focusleft(const char *args[]) {
	if (!sel)
		return;
	struct client *c = get_client_by_coord(sel->x - 2, sel->y);
	if (c)
		focus(c);
	else
		focusprev(args);
}

void
focusright(const char *args[]) {
	if (!sel)
		return;
	struct client *c = get_client_by_coord(sel->x + sel->w + 1, sel->y);
	if (c)
		focus(c);
	else
		focusnext(args);
}

void
killclient(const char *args[]) {
	if (!sel)
		return;
	debug("killing client with pid: %d\n", sel->pid);
	kill(-sel->pid, SIGKILL);
}

void
paste(const char *args[]) {
	if (sel && copyreg.data)
		vt_write(sel->term, copyreg.data, copyreg.len);
}

void
quit(const char *args[]) {
	stop_requested = 1;
}

void
redraw(const char *args[]) {
	for (struct client *c = clients; c; c = c->next) {
		if (!c->minimized) {
			vt_dirty(c->term);
			wclear(c->window);
			wnoutrefresh(c->window);
		}
	}
	resize_screen();
}

void
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

void
send(const char *args[]) {
	if (sel && args && args[0])
		vt_write(sel->term, args[0], strlen(args[0]));
}

void
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

void
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

void
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

void
togglebar(const char *args[]) {
	bar.hidden = !bar.hidden;
	bar.autohide = 0;
	updatebarpos();
	redraw(NULL);
}

void
toggleminimize(const char *args[]) {
	struct client *c, *m, *t;
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

void
togglerunall(const char *args[]) {
	runinall = !runinall;
	drawbar();
	draw_all();
}

void
zoom(const char *args[]) {
	struct client *c;

	if (!sel)
		return;
	if (args && args[0])
		focusn(args);
	/* Do nothing if sel is the only visible client */
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
			char *args[] = { arg, NULL, NULL };
			struct action a = { create, {arg, NULL, NULL}};
			push_action(&a);
			continue;
		}
		if( strchr("drtscm", arg[1]) != NULL && argv[1] == NULL ) {
			error(0, "%s requires an argument (-? for usage)", arg);
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
			updatebarpos();
			break;
		case 'c': {
			cmdfifo.fd = open_or_create_fifo(*++argv, &cmdfifo.file, "MVTM_CMD_FIFO");
			break;
		}
		default:
			error(0, "unknown option: %s (-? for usage)", arg);
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


int
main(int argc, char *argv[])
{
	struct state *s = &state;
	unsigned int key_index = 0;

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
			int code = getch();
			if( code == modifier_key ) {
				if( s->mode == keypress_mode) {
					s->mode = command_mode;
					key_index = 0;
				} else {
					s->mode = keypress_mode;
					*bar.text = '\0';
					keypress(code);
				}
			} else if( code == ESC || code == 0x0d) {
				switch(s->mode) {
				case keypress_mode:
					keypress(code);
					break;
				case command_mode:
					s->mode = keypress_mode;
					*bar.text = '\0';
				}
			} else if (code >= 0) {
				if( s->mode == keypress_mode) {
					keypress(code);
				} else {
					struct key_binding *binding;
					s->entry_buf[key_index++] = code;
					s->entry_buf[key_index] = 0;

					if( NULL != (binding = keybinding(s)) ) {
						if(binding->action.cmd != NULL) {

if(binding->action.cmd == copymode ||
	binding->action.cmd == paste
) {
	s->mode = keypress_mode;
	*bar.text = '\0';
}
							binding->action.cmd(binding->action.args);
							key_index = 0;
						} else {
							snprintf(bar.text, sizeof bar.text, "%s", s->entry_buf);
						}
					} else {
						key_index = 0;
					}
				}
			}
drawbar();
draw_all();
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

void fullscreen(void)
{
	for (struct client *c = nextvisible(clients); c; c = nextvisible(c->next))
		resize(c, main_window_x, main_window_y, available_width, available_height);
}
