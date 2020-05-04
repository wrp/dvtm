#include "config.h"
#include "mvtm.h"

char esc[] = { ESC, 0 };

/* Bindings following MOD */
binding_description mod_bindings[] = {
	{ esc,    "transition_no_send" },
	{ "\x0d", "transition_no_send" },
	{ "xx",  "killclient" },
	{ "qq",  "quit" },
	{ "B",   "copymode", PACKAGE "-pager", "bindings" },
	{ "c",   "split" },
	{ "v",   "split", "v" },
	{ "f",   "focus_transition" },
	{ "g",   "focusn" },
	{ "h",   "mov", "left" },
	{ "j",   "mov", "down" },
	{ "k",   "mov", "up" },
	{ "l",   "mov", "right" },
	{ "r",   "redraw" },
	{ "e",   "copymode", PACKAGE "-editor" },
	{ "p",   "paste" },
	{ "?",   "create", "man dvtm", "dvtm help" },
	{ "u",   "scrollback", "-.5" },
	{ "n",   "scrollback", "+.5" },
	{ "qq",  "quit" },
	{ NULL }
};
/* Bindings that take effect in keypress mode */
char *keypress_bindings[][MAX_BIND] = {
	{ NULL }
};

struct command_name {
	command *cmd;
	char *name;
};
#define entry(x) { x, #x }
static struct command_name names[] = {
	entry(change_kill_signal),
	entry(copymode),
	entry(create),
	entry(focusn),
	entry(focus_transition),
	entry(killclient),
	entry(mov),
	entry(paste),
	entry(quit),
	entry(redraw),
	entry(scrollback),
	entry(transition_no_send),
	entry(transition_with_send),
	entry(split),
	{NULL},
};
#undef entry


command *
get_function(const char *name)
{
	command *rv = NULL;
	for( struct command_name *n = names; n->cmd; n++ ) {
		if( strcmp(n->name, name) == 0 ) {
			rv = n->cmd;
			break;
		}
	}
	return rv;
}


int
parse_binding(struct action *a, const char *d)
{
	const char *keys = d;
	const char *func_name = strchr(keys, '\0') + 1;
	if( (a->cmd = get_function(func_name)) == NULL ) {
		return -1;
	}

	a->next = NULL;
	a->args[0] = strchr(func_name, '\0') + 1;
	a->args[1] = strchr(a->args[0], '\0') + 1;
	a->args[2] = strchr(a->args[1], '\0') + 1;

	/* Do not allow empty args.  Treat as null.  TODO: fix this */
	for(int i = 0; i < 3; i++) {
		if(a->args[i][0] == '\0') {
			a->args[i] = NULL;
		}
	}
	return 0;
}
