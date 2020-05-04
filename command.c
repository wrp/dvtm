#include "package.h"
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
	return push_binding(bindings, binding, &a);
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
mov(const char * const args[])
{
	struct window *w = state.current_view->vfocus;
	int up = !strcmp(args[0], "up");
	int down = !strcmp(args[0], "down");
	if( w == NULL ) {
		return 1;
	}
	assert( w->enclosing_layout != NULL );
	assert( state.current_view->layout != NULL );
	if(
		( down && w->next == NULL )
		|| ( up && w->prev == NULL )
	) {
		w = w->enclosing_layout->parent;
	}
	while( w && ( up || down )
			&& ( w->enclosing_layout->type == row_layout )) {
		w = w->enclosing_layout->parent;
	}
	if( w != NULL ) {
		if( down ) {
			w = w->next;
		} else if( up ) {
			w = w->prev;
		}
		if( w->layout ) {
			w = w->layout->windows;
		}
	}
	if( w != NULL ) {
		state.current_view->vfocus = w;
	}
	return w == NULL; /* 0 success, 1 failure */
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
