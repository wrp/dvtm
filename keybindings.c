#include "config.h"
#include "dvtm.h"

#define TAGKEYS(KEY,TAG) \
	{ { 'v', KEY,     }, { view,           { #TAG }               } }, \
	{ { 't', KEY,     }, { tag,            { #TAG }               } }, \
	{ { 'V', KEY,     }, { toggleview,     { #TAG }               } }, \
	{ { 'T', KEY,     }, { toggletag,      { #TAG }               } }

struct key_binding bindings[] = {
	{ { 'c',          }, { create,         { NULL, NULL, "master" }    } },
	{ { 'C',          }, { create,         { NULL, NULL, "$CWD" }      } },
	{ { 'x', 'x',     }, { killclient,     { NULL }                    } },
	{ { 'j',          }, { focusnext,      { NULL }                    } },
	{ { 'J',          }, { focusdown,      { NULL }                    } },
	{ { 'K',          }, { focusup,        { NULL }                    } },
	{ { 'H',          }, { focusleft,      { NULL }                    } },
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
	{ { 'E',          }, { copymode,       { "dvtm-pager" }            } },
	{ { '/',          }, { copymode,       { "dvtm-pager", "/" }       } },
	{ { 'p',          }, { paste,          { NULL }                    } },
	{ { KEY_PPAGE,    }, { scrollback,     { "-1" }                    } },
	{ { KEY_NPAGE,    }, { scrollback,     { "1"  }                    } },
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
size_t key_binding_length = LENGTH(bindings);

#undef TAGKEYS
