#include "config.h"
#include "mvtm.h"

#define TAGKEYS(KEY,TAG) \
	{ { 'v', KEY,     }, { view,           { #TAG }               } }, \
	{ { 't', KEY,     }, { tag,            { #TAG }               } }, \
	{ { 'V', KEY,     }, { toggleview,     { #TAG }               } }, \
	{ { 'T', KEY,     }, { toggletag,      { #TAG }               } }

char *binding_desc[] = {
	"xx\0killclient\0\0\0",
	"qq\0quit\0\0\0",
	"c\0create\0\0\0master",
	"j\0focusnext\0\0\0",
	"J\0focusdown\0\0\0",
	"k\0focusprev\0\0\0",
	"K\0focusup\0\0\0",
	"H\0focusleft\0\0\0",
};
size_t binding_descr_length = LENGTH(binding_desc);

int
parse_binding(struct action *a, const char *d)
{
	const char *keys = d;
	const char *func_name = strchr(keys, '\0') + 1;
	if( strcmp(func_name, "create") == 0 ) a->cmd = create;
	else if( strcmp(func_name, "killclient") == 0 ) a->cmd = killclient;
	else if( strcmp(func_name, "focusnext") == 0 ) a->cmd = focusnext;
	else if( strcmp(func_name, "focusdown") == 0 ) a->cmd = focusdown;
	else if( strcmp(func_name, "focusprev") == 0 ) a->cmd = focusprev;
	else if( strcmp(func_name, "focusup") == 0 ) a->cmd = focusup;
	else if( strcmp(func_name, "focusleft") == 0 ) a->cmd = focusleft;
	else if( strcmp(func_name, "quit") == 0 ) a->cmd = quit;
	else return -1;

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
