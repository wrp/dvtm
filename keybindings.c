#include "config.h"
#include "mvtm.h"

#define TAGKEYS(KEY,TAG) \
	{ { 'v', KEY,     }, { view,           { #TAG }               } }, \
	{ { 't', KEY,     }, { tag,            { #TAG }               } }, \
	{ { 'V', KEY,     }, { toggleview,     { #TAG }               } }, \
	{ { 'T', KEY,     }, { toggletag,      { #TAG }               } }

char *binding_desc[][2] = {
	{ "xx", "killclient" },
	{ "qq", "quit" },
	{ "c", "create" },
	{ "j", "focusnext" },
	{ "J", "focusdown" },
	{ "k", "focusprev" },
	{ "K", "focusup" },
	{ "H", "focusleft" },
};
size_t binding_descr_length = LENGTH(binding_desc);

command
get_function(const char *name)
{
	command *rv = NULL;
	if( strcmp(name, "create") == 0 ) rv = create;
	else if( strcmp(name, "killclient") == 0 ) rv = killclient;
	else if( strcmp(name, "focusnext") == 0 ) rv = focusnext;
	else if( strcmp(name, "focusdown") == 0 ) rv = focusdown;
	else if( strcmp(name, "focusprev") == 0 ) rv = focusprev;
	else if( strcmp(name, "focusup") == 0 ) rv = focusup;
	else if( strcmp(name, "focusleft") == 0 ) rv = focusleft;
	else if( strcmp(name, "quit") == 0 ) rv = quit;
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

struct old_key_binding old_bindings[] = {
	{ { 'L',          }, { focusright,     { NULL }                    } },
	{ { 'k',          }, { focusprev,      { NULL }                    } },
	{ { 'f',          }, { setlayout,      { "---" }                   } },
	{ { 'm',          }, { setlayout,      { "[ ]" }                   } },
	{ { ' ',          }, { setlayout,      { NULL }                    } },
	{ { 'i',          }, { incnmaster,     { "+1" }                    } },
	{ { 'd',          }, { incnmaster,     { "-1" }                    } },
	{ { 'h',          }, { setmfact,       { "-0.05" }                 } },
	{ { 'l',          }, { setmfact,       { "+0.05" }                 } },
	{ { '.',          }, { toggleminimize, { NULL }                    } },
	{ { 's',          }, { togglebar,      { NULL }                    } },
	{ { '\n',         }, { zoom ,          { NULL }                    } },
	{ { '\r',         }, { zoom ,          { NULL }                    } },
	{ { '1',          }, { focusn,         { "1" }                     } },
	{ { '2',          }, { focusn,         { "2" }                     } },
	{ { '3',          }, { focusn,         { "3" }                     } },
	{ { '4',          }, { focusn,         { "4" }                     } },
	{ { '5',          }, { focusn,         { "5" }                     } },
	{ { '6',          }, { focusn,         { "6" }                     } },
	{ { '7',          }, { focusn,         { "7" }                     } },
	{ { '8',          }, { focusn,         { "8" }                     } },
	{ { '9',          }, { focusn,         { "9" }                     } },
	{ { '\t',         }, { focuslast,      { NULL }                    } },
	{ { 'q', 'q',     }, { quit,           { NULL }                    } },
	{ { 'a',          }, { togglerunall,   { NULL }                    } },
	{ { CTRL('L'),    }, { redraw,         { NULL }                    } },
	{ { 'r',          }, { redraw,         { NULL }                    } },
	{ { 'e',          }, { copymode,       { "mvtm-editor" }           } },
	{ { 'p',          }, { paste,          { NULL }                    } },
	{ { '?',          }, { create,         { "man dvtm", "dvtm help" } } },
	{ { 'u',          }, { scrollback,     { "-1" }                    } },
	{ { 'n',          }, { scrollback,     { "1"  }                    } },
	{ { '0',          }, { view,           { NULL }                    } },
	{ { 'v', '0'      }, { view,           { NULL }                    } },
	{ { 'v', '\t',    }, { viewprevtag,    { NULL }                    } },
	{ { 't', '0'      }, { tag,            { NULL }                    } },
	{ { 'T', '0'      }, { untag,          { NULL }                    } },
	TAGKEYS( '1', 1),
	TAGKEYS( '2', 2),
	TAGKEYS( '3', 3),
	TAGKEYS( '4', 4),
	TAGKEYS( '5', 5),
	TAGKEYS( '6', 6),
	TAGKEYS( '7', 7),
	TAGKEYS( '8', 8),
};
size_t old_key_binding_length = LENGTH(old_bindings);

#undef TAGKEYS
