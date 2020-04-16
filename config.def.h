/* valid curses attributes are listed below they can be ORed
 *
 * A_NORMAL        Normal display (no highlight)
 * A_STANDOUT      Best highlighting mode of the terminal.
 * A_UNDERLINE     Underlining
 * A_REVERSE       Reverse video
 * A_BLINK         Blinking
 * A_DIM           Half bright
 * A_BOLD          Extra bright or bold
 * A_PROTECT       Protected mode
 * A_INVIS         Invisible or blank mode
 */

enum {
	DEFAULT,
	BLUE,
	RED,
};

static Color colors[] = {
	[DEFAULT] = { .fg = -1,         .bg = -1, .fg256 = -1, .bg256 = -1, },
	[BLUE]    = { .fg = COLOR_BLUE, .bg = -1, .fg256 = 68, .bg256 = -1, },
	[RED]     = { .fg = COLOR_RED,  .bg = -1, .fg256 = 68, .bg256 = -1, },
};

#define COLOR(c)        COLOR_PAIR(colors[c].pair)
/* curses attributes for the currently focused window */
#define SELECTED_ATTR   (COLOR(BLUE) | A_NORMAL)
/* curses attributes for normal (not selected) windows */
#define NORMAL_ATTR     (COLOR(DEFAULT) | A_NORMAL)
/* curses attributes for a window with pending urgent flag */
#define URGENT_ATTR     NORMAL_ATTR
/* curses attributes for the status bar */
#define BAR_ATTR        (COLOR(BLUE) | A_NORMAL)
/* characters for beginning and end of status bar message */
#define BAR_BEGIN       '['
#define BAR_END         ']'
/* status bar (command line option -s) position */
#define BAR_POS         BAR_TOP /* BAR_BOTTOM, BAR_OFF */
/* whether status bar should be hidden if only one client exists */
#define BAR_AUTOHIDE    true
/* master width factor [0.1 .. 0.9] */
#define MFACT 0.8
/* number of clients in master area */
#define NMASTER 1
/* scroll back buffer size in lines */
#define SCROLL_HISTORY 500
/* printf format string for the tag in the status bar */
#define TAG_SYMBOL   "[%d]"
/* curses attributes for the currently selected tags */
#define TAG_SEL      (COLOR(BLUE) | A_BOLD)
/* curses attributes for not selected tags which contain no windows */
#define TAG_NORMAL   (COLOR(DEFAULT) | A_NORMAL)
/* curses attributes for not selected tags which contain windows */
#define TAG_OCCUPIED (COLOR(RED) | A_NORMAL)
/* curses attributes for not selected tags which with urgent windows */
#define TAG_URGENT (COLOR(RED) | A_NORMAL | A_BLINK)

const int tags = 8;

extern void wstack(void);
/* by default the first layout entry is used */
static Layout layouts[] = {
	{ "---", wstack },
	{ "[ ]", fullscreen },
};

#define MOD  CTRL('g')
#define TAGKEYS(KEY,TAG) \
	{ { MOD, 'v', KEY,     }, { view,           { #TAG }               } }, \
	{ { MOD, 't', KEY,     }, { tag,            { #TAG }               } }, \
	{ { MOD, 'V', KEY,     }, { toggleview,     { #TAG }               } }, \
	{ { MOD, 'T', KEY,     }, { toggletag,      { #TAG }               } }

/* you can specifiy at most 3 arguments */
static KeyBinding bindings[] = {
	{ { MOD, 'c',          }, { create,         { NULL, NULL, "master" }    } },
	{ { MOD, 'C',          }, { create,         { NULL, NULL, "$CWD" }      } },
	{ { MOD, 'x', 'x',     }, { killclient,     { NULL }                    } },
	{ { MOD, 'j',          }, { focusnext,      { NULL }                    } },
	{ { MOD, 'J',          }, { focusdown,      { NULL }                    } },
	{ { MOD, 'K',          }, { focusup,        { NULL }                    } },
	{ { MOD, 'H',          }, { focusleft,      { NULL }                    } },
	{ { MOD, 'L',          }, { focusright,     { NULL }                    } },
	{ { MOD, 'k',          }, { focusprev,      { NULL }                    } },
	{ { MOD, 'f',          }, { setlayout,      { "---" }                   } },
	{ { MOD, 'm',          }, { setlayout,      { "[ ]" }                   } },
	{ { MOD, ' ',          }, { setlayout,      { NULL }                    } },
	{ { MOD, 'i',          }, { incnmaster,     { "+1" }                    } },
	{ { MOD, 'd',          }, { incnmaster,     { "-1" }                    } },
	{ { MOD, 'h',          }, { setmfact,       { "-0.05" }                 } },
	{ { MOD, 'l',          }, { setmfact,       { "+0.05" }                 } },
	{ { MOD, '.',          }, { toggleminimize, { NULL }                    } },
	{ { MOD, 's',          }, { togglebar,      { NULL }                    } },
	{ { MOD, 'S',          }, { togglebarpos,   { NULL }                    } },
	{ { MOD, '\n',         }, { zoom ,          { NULL }                    } },
	{ { MOD, '\r',         }, { zoom ,          { NULL }                    } },
	{ { MOD, '1',          }, { focusn,         { "1" }                     } },
	{ { MOD, '2',          }, { focusn,         { "2" }                     } },
	{ { MOD, '3',          }, { focusn,         { "3" }                     } },
	{ { MOD, '4',          }, { focusn,         { "4" }                     } },
	{ { MOD, '5',          }, { focusn,         { "5" }                     } },
	{ { MOD, '6',          }, { focusn,         { "6" }                     } },
	{ { MOD, '7',          }, { focusn,         { "7" }                     } },
	{ { MOD, '8',          }, { focusn,         { "8" }                     } },
	{ { MOD, '9',          }, { focusn,         { "9" }                     } },
	{ { MOD, '\t',         }, { focuslast,      { NULL }                    } },
	{ { MOD, 'q', 'q',     }, { quit,           { NULL }                    } },
	{ { MOD, 'a',          }, { togglerunall,   { NULL }                    } },
	{ { MOD, CTRL('L'),    }, { redraw,         { NULL }                    } },
	{ { MOD, 'r',          }, { redraw,         { NULL }                    } },
	{ { MOD, 'e',          }, { copymode,       { "dvtm-editor" }           } },
	{ { MOD, 'E',          }, { copymode,       { "dvtm-pager" }            } },
	{ { MOD, '/',          }, { copymode,       { "dvtm-pager", "/" }       } },
	{ { MOD, 'p',          }, { paste,          { NULL }                    } },
	{ { MOD, KEY_PPAGE,    }, { scrollback,     { "-1" }                    } },
	{ { MOD, KEY_NPAGE,    }, { scrollback,     { "1"  }                    } },
	{ { MOD, '?',          }, { create,         { "man dvtm", "dvtm help" } } },
	{ { MOD, MOD,          }, { send,           { (const char []){MOD, 0} } } },
	{ { KEY_SPREVIOUS,     }, { scrollback,     { "-1" }                    } },
	{ { KEY_SNEXT,         }, { scrollback,     { "1"  }                    } },
	{ { MOD, '0',          }, { view,           { NULL }                    } },
	{ { MOD, KEY_F(1),     }, { view,           { "1" }                 } },
	{ { MOD, KEY_F(2),     }, { view,           { "2" }                 } },
	{ { MOD, KEY_F(3),     }, { view,           { "3" }                 } },
	{ { MOD, KEY_F(4),     }, { view,           { "4" }                 } },
	{ { MOD, KEY_F(5),     }, { view,           { "5" }                 } },
	{ { MOD, 'v', '0'      }, { view,           { NULL }                    } },
	{ { MOD, 'v', '\t',    }, { viewprevtag,    { NULL }                    } },
	{ { MOD, 't', '0'      }, { tag,            { NULL }                    } },
	{ { MOD, 'T', '0'      }, { untag,          { NULL }                    } },
	TAGKEYS( '1',                              1),
	TAGKEYS( '2',                              2),
	TAGKEYS( '3',                              3),
	TAGKEYS( '4',                              4),
	TAGKEYS( '5',                              5),
	TAGKEYS( '6',                              6),
	TAGKEYS( '7',                              7),
	TAGKEYS( '8',                              8),
};

static const ColorRule colorrules[] = {
	{ "", A_NORMAL, &colors[DEFAULT] }, /* default */
};



static Cmd commands[] = {
	/* create [cmd]: create a new window, run `cmd` in the shell if specified */
	{ "create", { create,	{ NULL } } },
	/* focus <win_id>: focus the window whose `DVTM_WINDOW_ID` is `win_id` */
	{ "focus",  { focusid,	{ NULL } } },
	/* tag <win_id> <tag> [tag ...]: add +tag, remove -tag or set tag of the window with the given identifier */
	{ "tag",    { tagid,	{ NULL } } },
};

static char const * const keytable[] = {
	/* add your custom key escape sequences */
};
