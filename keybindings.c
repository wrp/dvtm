#include "config.h"
#include "mvtm.h"

#define TAGKEYS(KEY,TAG) \
	{ { 'v', KEY,     }, { view,           { #TAG }               } }, \
	{ { 't', KEY,     }, { tag,            { #TAG }               } }, \
	{ { 'V', KEY,     }, { toggleview,     { #TAG }               } }, \
	{ { 'T', KEY,     }, { toggletag,      { #TAG }               } }

char *binding_desc[][5] = {
	{ "xx",  "killclient" },
	{ "qq",  "quit" },
	{ "c",   "create" },
	{ "j",   "focusnext" },
	{ "J",   "focusdown" },
	{ "k",   "focusprev" },
	{ "K",   "focusup" },
	{ "H",   "focusleft" },
	{ "L",   "focusright" },
	{ "k",   "focusprev" },
	{ "f",   "focusn" },
	{ "i",   "incnmaster", "+1" },
	{ "d",   "incnmaster", "-1" },
	{ "h",   "setmfact", "-0.05" },
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
	{ "0",   "view" },
	{ "qq",  "quit" },
	{ "v0",  "view" },
	{ "v\t", "viewprevtag" },
	{ "t",  "tag" },
	{ "T0",  "untag" },
	{ NULL }
};

struct command_meta {
	command *cmd;
	char *name;
	int switch_mode; /* true if this function should force mode transition */
};
#define entry(x, mode) { x, #x, mode }
static struct command_meta names[] = {
	entry(copymode, 1),
	entry(create, 0),
	entry(focusdown, 0),
	entry(focuslast, 0),
	entry(focusleft, 0),
	entry(focusn, 0),
	entry(focusnext, 0),
	entry(focusprev, 0),
	entry(focusright, 0),
	entry(focusup, 0),
	entry(incnmaster, 0),
	entry(killclient, 0),
	entry(paste, 1),
	entry(quit, 0),
	entry(redraw, 0),
	entry(scrollback, 0),
	entry(setmfact, 0),
	entry(tag, 0),
	entry(toggleminimize, 0),
	entry(togglerunall, 0),
	entry(untag, 0),
	entry(view, 0),
	entry(viewprevtag, 0),
	entry(zoom, 0),
	{NULL},
};
#undef entry

int
should_switch(command *k) {
	for( struct command_meta *n = names; n->cmd; n++ ) {
		if( k == n->cmd ) {
			return n->switch_mode;
		}
	}
	return -1;
}

command *
get_function(const char *name)
{
	command *rv = NULL;
	for( struct command_meta *n = names; n->cmd; n++ ) {
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
