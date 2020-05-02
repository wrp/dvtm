#include "mvtm.h"
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
	if( !strcmp(args[0], "down") ) {
		struct window *w = state.current_view->vfocus;
		if( w == NULL ) {
			;
		} else if( w->next && w->next->layout ) {
			w = w->next->layout->windows;
		} else {
			w = w->next;
		}
		state.current_view->vfocus = w;
	}
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
