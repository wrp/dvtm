#include "config.h"
#include "mvtm.h"

#define TAGKEYS(KEY,TAG) \
	{ { 'v', KEY,     }, { view,           { #TAG }               } }, \
	{ { 't', KEY,     }, { tag,            { #TAG }               } }, \
	{ { 'V', KEY,     }, { toggleview,     { #TAG }               } }, \
	{ { 'T', KEY,     }, { toggletag,      { #TAG }               } }

char *binding_desc[][2] = {
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
	{ "f",   "setlayout ---" },
	{ "m",   "setlayout [ ]" },
	{ " ",   "setlayout" },
	{ "i",   "incnmaster +1" },
	{ "d",   "incnmaster -1" },
	{ "h",   "setmfact -0.05" },
	{ "l",   "setmfact +0.05" },
	{ ".",   "toggleminimize" },
	{ "s",   "togglebar" },
	{ "\n",  "zoom" },
	{ "1",   "focusn 1" },
	{ "2",   "focusn 2" },
	{ "3",   "focusn 3" },
	{ "4",   "focusn 4" },
	{ "5",   "focusn 5" },
	{ "6",   "focusn 6" },
	{ "7",   "focusn 7" },
	{ "8",   "focusn 8" },
	{ "9",   "focusn 9" },
	{ "\t",  "focuslast" },
	{ "a",   "togglerunall" },
	{ "r",   "redraw" },
	{ "e",   "copymode mvtm-editor" },
	{ "p",   "paste" },
	{ "?",   "create \"man dvtm\" \"dvtm help\"" },
	{ "u",   "scrollback -1" },
	{ "n",   "scrollback 1" },
	{ "0",   "view" },
	{ "qq",  "quit" },
	{ "v0",  "view" },
	{ "v\t", "viewprevtag" },
	{ "t0",  "tag" },
	{ "T0",  "untag" },
};
size_t binding_descr_length = LENGTH(binding_desc);

struct command_name {
	command *cmd;
	char *name;
};
#define entry(x) { x, #x }
static struct command_name names[] = {
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
	entry(setlayout),
	entry(setmfact),
	entry(tag),
	entry(togglebar),
	entry(toggleminimize),
	entry(togglerunall),
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
	a->args[3] = NULL;

	/* Do not allow empty args.  Treat as null.  TODO: fix this */
	for(int i = 0; i < 3; i++) {
		if(a->args[i][0] == '\0') {
			a->args[i] = NULL;
		}
	}
	return 0;
}


#undef TAGKEYS
