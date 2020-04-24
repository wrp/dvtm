#include "config.h"
#include "mvtm.h"

#define TAGKEYS(KEY,TAG) \
	{ { 'v', KEY,     }, { view,           { #TAG }               } }, \
	{ { 't', KEY,     }, { tag,            { #TAG }               } }, \
	{ { 'V', KEY,     }, { toggleview,     { #TAG }               } }, \
	{ { 'T', KEY,     }, { toggletag,      { #TAG }               } }

char ctrl_g[] = { CTRL('g'), 0 };
char esc[] = { ESC, 0 };

/* Bindings following MOD */
binding_description mod_bindings[] = {
	{ ctrl_g, "transition_with_send" },
	{ esc,    "transition_no_send" },
	{ "\x0d", "transition_no_send" },
	{ "xx",  "killclient" },
	{ "qq",  "quit" },
	{ "B",   "copymode", "mvtm-pager", "bindings" },
	{ "c",   "create" },
	{ "j",   "focusnext" },
	{ "J",   "focusdown" },
	{ "k",   "focusprev" },
	{ "K",   "focusup" },
	{ "H",   "focusleft" },
	{ "L",   "focusright" },
	{ "f",   "focusn" },
	{ "i",   "incnmaster", "+1" },
	{ "d",   "incnmaster", "-1" },
	{ "hup", "change_kill_signal" },
	{ "l",   "setmfact", "+0.05" },
	{ ".",   "toggleminimize" },
	{ "m",   "zoom" },
	{ "\t",  "focuslast" },
	{ "a",   "togglerunall" },
	{ "r",   "redraw" },
	{ "e",   "copymode", "mvtm-editor" },
	{ "p",   "paste" },
	{ "?",   "create", "man dvtm", "dvtm help" },
	{ "u",   "scrollback", "-.5" },
	{ "n",   "scrollback", "+.5" },
	{ "qq",  "quit" },
	{ "v",   "view" },
	{ "V",   "viewprevtag" },
	{ "t",  "tag" },
	{ "T0",  "untag" },
	{ NULL }
};
/* Bindings that take effect in keypress mode */
char *keypress_bindings[][MAX_BIND] = {
	{ ctrl_g, "transition_no_send" },
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
	entry(focusdown),
	entry(focuslast),
	entry(focusleft),
	entry(focusn),
	entry(focusnext),
	entry(focusprev),
	entry(focusright),
	entry(focusup),
	entry(incnmaster),
	entry(killclient),
	entry(paste),
	entry(quit),
	entry(redraw),
	entry(scrollback),
	entry(setmfact),
	entry(tag),
	entry(toggleminimize),
	entry(togglerunall),
	entry(transition_no_send),
	entry(transition_with_send),
	entry(untag),
	entry(view),
	entry(viewprevtag),
	entry(zoom),
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


#undef TAGKEYS
