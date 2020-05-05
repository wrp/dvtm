#include "main.h"
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
	enum { up, left, down, right } dir;
	struct window *w = state.current_view->vfocus;
	if( w == NULL ) {
		return 1;
	}
	if( !strcmp(args[0], "up") ) {
		dir = up;
	} else if( !strcmp(args[0], "down") ) {
		dir = down;
	} else if( !strcmp(args[0], "left") ) {
		dir = left;
	} else if( !strcmp(args[0], "right") ) {
		dir = right;
	}
	int forward = (dir == down) || (dir == right);
	int vertical = (dir == up) || (dir == down);
	assert( w->enclosing_layout != NULL );
	assert( state.current_view->layout != NULL );
	if( state.buf.count == 0 ) {
		state.buf.count =1;
	}
	while( state.buf.count-- ) {
		while( w != NULL && (
				(forward && w->next == NULL)
				|| (!forward && w->prev == NULL )
				|| (vertical && ( w->enclosing_layout->type == row_layout ))
				|| (!vertical && ( w->enclosing_layout->type == column_layout ))
			)
		) {
			w = w->enclosing_layout->parent;
		}
		if( w != NULL ) {
			switch(dir) {
			case right:
			case down:
				w = w->next;
				break;
			case left:
			case up:
				w = w->prev;
			}
		}
		if( w && w->layout ) {
			w = w->layout->windows;
		}
		if( w && w->enclosing_layout == state.current_view->layout
			&& (( forward && w->next == NULL)
				|| ( !forward && w->prev == NULL)) ) {
			break;
		}
	}
	if( w != NULL ) {
		focus(w);
	}
	return w == NULL; /* 0 success, 1 failure */
}

int
change_state(const char * const args[])
{
	if( ! strcmp(args[0], "command" )) {
		toggle_mode(command_mode);
	} else {
		toggle_mode(keypress_mode);
		keypress(state.code);
	}
	return 0;
}
