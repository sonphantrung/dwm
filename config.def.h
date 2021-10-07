/* See LICENSE file for copyright and license details. */
#include <X11/XF86keysym.h>

/* appearance */
static unsigned int borderpx  = 1;        /* border pixel of windows */
#define ICONSIZE 16   /* icon size */
#define ICONSPACING 5 /* space between icon and title */
static unsigned int snap      = 32;       /* snap pixel */
static int swallowfloating    = 0;        /* 1 means swallow floating windows by default */
static unsigned int gappih    = 10;       /* horiz inner gap between windows */
static unsigned int gappiv    = 10;       /* vert inner gap between windows */
static unsigned int gappoh    = 10;       /* horiz outer gap between windows and screen edge */
static unsigned int gappov    = 10;       /* vert outer gap between windows and screen edge */
static int smartgaps          = 0;        /* 1 means no outer gap when there is only one window */
static unsigned int systraypinning = 1;   /* 0: sloppy systray follows selected monitor, >0: pin systray to monitor X */
static unsigned int systrayonleft = 0;   	/* 0: systray in the right corner, >0: systray on left of status text */
static unsigned int systrayspacing = 2;   /* systray spacing */
static int scalepreview       = 4;
static int systraypinningfailfirst = 1;   /* 1: if pinning fails, display systray on the first monitor, False: display systray on the last monitor*/
static int showsystray        = 1;     /* 0 means no systray */
static int showbar            = 1;        /* 0 means no bar */
static int topbar             = 1;        /* 0 means bottom bar */
static char slopspawnstyle[]  = "-t 0 -c 0.92,0.85,0.69,0.3 -o"; /* do NOT define -f (format) here */
static char slopresizestyle[] = "-t 0 -c 0.92,0.85,0.69,0.3"; /* do NOT define -f (format) here */
static int riodraw_borders    = 0;        /* 0 or 1, indicates whether the area drawn using slop includes the window borders */
static int riodraw_matchpid   = 1;        /* 0 or 1, indicates whether to match the PID of the client that was spawned with riospawn */
static int riodraw_spawnasync = 1;        /* 0 means that the application is only spawned after a successful selection while
                                                 * 1 means that the application is being initialised in the background while the selection is made */
static int floatposgrid_x           = 5;        /* float grid columns */
static int floatposgrid_y           = 5;        /* float grid rows */
/* Default action to take when we receive a net active signal.
 *    0 - disable / does nothing
 *    1 - focus the client (as per the focusonnetactive patch)
 *    2 - focus the client tag in addition to the current tags
 *    3 - set the urgency bit (as per dwm default)
 *    4 - client is shown on current tag in addition to its existing tags
 *    5 - client is moved to current tag
 *    6 - client receives focus only if current tag is shown
 */
static int defnetactiverule   = 1;
static int vertpad            = 0;       /* vertical padding of bar */
static int sidepad            = 0;       /* horizontal padding of bar */
static int user_bh            = 30;        /* 0 means that dwm will calculate bar height, >= 1 means dwm will user_bh as bar height */
static char font[]            = "monospace:pixelsize=14";
static const char *fonts[]          = { font, "Symbols Nerd Font:pixelsize=14:antialias=true:autohint=true", "Noto Color Emoji:pixelsize=14:antialias=true:autohint=true"};
static int nomodbuttons                  = 0;   /* allow client mouse button bindings that have no modifier */
static char normbgcolor[]       = "#282a2e";
static char normbordercolor[]       = "#282a2e";
static char selbordercolor[]       = "#822a2a";
static char normfgcolor[]       = "#d0ddd4";
static char selfgcolor[]       = "#d7dcd9";
static char selbgcolor[]        = "#8abeb7";
static const unsigned int baralpha = 0xd0;
static const unsigned int borderalpha = OPAQUE;

static char termcol0[] = "#080808"; /* black   */
static char termcol1[] = "#c13434"; /* red     */
static char termcol2[] = "#646a2f"; /* green   */
static char termcol3[] = "#c97941"; /* yellow  */
static char termcol4[] = "#495c6d"; /* blue    */
static char termcol5[] = "#6d4f77"; /* magenta */
static char termcol6[] = "#5e8d87"; /* cyan    */
static char termcol7[] = "#d7dcd9"; /* white   */
static char termcol8[]  = "#060606"; /* black   */
static char termcol9[]  = "#c13939"; /* red     */
static char termcol10[] = "#73813a"; /* green   */
static char termcol11[] = "#c28a59"; /* yellow  */
static char termcol12[] = "#6892b6"; /* blue    */
static char termcol13[] = "#b062c7"; /* magenta */
static char termcol14[] = "#8abeb7"; /* cyan    */
static char termcol15[] = "#dae1ea"; /* white   */
static char *termcolor[] = {
  termcol0,
  termcol1,
  termcol2,
  termcol3,
  termcol4,
  termcol5,
  termcol6,
  termcol7,
  termcol8,
  termcol9,
  termcol10,
  termcol11,
  termcol12,
  termcol13,
  termcol14,
  termcol15,

};
static char *colors[][3]      = {
	/*               fg         bg         border   */
	[SchemeNorm] = { normfgcolor, normbgcolor, normbordercolor },
	[SchemeSel]  = { selbgcolor, normbgcolor,  selbordercolor  },
};
static unsigned int alphas[][3]      = {
	/*               fg      bg        border     */
	[SchemeNorm] = { OPAQUE, baralpha, borderalpha },
	[SchemeSel]  = { OPAQUE, baralpha, borderalpha },
};
 
/* tagging */
static char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

static const unsigned int ulinepad	= 5;	/* horizontal padding between the underline and tag */
static const unsigned int ulinestroke	= 2;	/* thickness / height of the underline */
static const unsigned int ulinevoffset	= 0;	/* how far above the bottom of the bar the line should appear */
static const int ulineall 		= 0;	/* 1 to show underline on all tags, 0 for just the active ones */

static const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 *	WM_WINDOW_ROLE(STRING) = role
	 *	_NET_WM_WINDOW_TYPE(ATOM) = wintype
	 */
	RULE(.wintype = WTYPE "DIALOG", .isfloating = 1)
	RULE(.wintype = WTYPE "UTILITY", .isfloating = 1)
	RULE(.wintype = WTYPE "TOOLBAR", .isfloating = 1)
	RULE(.wintype = WTYPE "SPLASH", .isfloating = 1)
	RULE(.title = "Event Tester", .noswallow = 1)
	RULE(.title = "scratchpad", .tags = SCRATCHPAD_MASK, .isfloating = 1)
	RULE(.class = "eww", .unmanaged = 2)
	RULE(.class = "St", .isterminal = 1)
	RULE(.class = "Gimp", .tags = 1 << 4)
	RULE(.class = "Firefox", .tags = 1 << 7)
 };
/* layout(s) */
static float mfact     = 0.50; /* factor of master area size [0.05..0.95] */
static int nmaster     = 1;    /* number of clients in master area */
static int resizehints = 0;    /* 1 means respect size hints in tiled resizals */
static int lockfullscreen = 1; /* 1 will force focus on the fullscreen window */

#define FORCE_VSPLIT 1  /* nrowgrid layout: force two clients to always split vertically */
static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },    /* first entry is default */
	{ "[M]",      monocle },
	{ "[@]",      spiral },
	{ "[\\]",     dwindle },
	{ "H[]",      deck },
	{ "TTT",      bstack },
	{ ":::",      gaplessgrid },
	{ "|M|",      centeredmaster },
	{ ">M>",      centeredfloatingmaster },
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ NULL,       NULL },

};

/* key definitions */
#define MODKEY Mod4Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask|ShiftMask, KEY,      toggletag,      {.ui = 1 << TAG} },
#define STACKKEYS(MOD,ACTION) \
	{ MOD, XK_j,     ACTION##stack, {.i = INC(+1) } }, \
	{ MOD, XK_k,     ACTION##stack, {.i = INC(-1) } }, \
	{ MOD, XK_v,     ACTION##stack, {.i = 0 } },
/*	{ MOD, XK_Menu, ACTION##stack, {.i = PREVSEL } }, \
	{ MOD, XK_a,     ACTION##stack, {.i = 1 } }, \
	{ MOD, XK_z,     ACTION##stack, {.i = 2 } }, \
	{ MOD, XK_x,     ACTION##stack, {.i = -1 } }, */

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

#define STATUSBAR "dwmblocks"

/* commands */
static char dmenumon[2] = "0"; /* component of dmenucmd, manipulated in spawn() */
static const char *dmenucmd[] = { "dmenu_run", "-m", dmenumon, "-l", "20", "-p", "Run:", "-vrtf", "-c", "-bw", "2", NULL };
static const char *termcmd[]  = { "st", NULL };
static const char *scratchpadcmd[] = {"st", "-t", "scratchpad", NULL}; 

/*
 * Xresources preferences to load at startup
 */
ResourcePref resources[] = {
		{ "font",               STRING,  &font },
		{ "background",		STRING,	&normbordercolor },
		{ "color8",		STRING,	&selbordercolor },
		{ "background",		STRING,	&normbgcolor },
		{ "foreground",		STRING,	&normfgcolor },
		{ "background",		STRING,	&selfgcolor },
		{ "color4",		STRING,	&selbgcolor },
		{ "color0",		STRING,	&termcol0 },
		{ "color1",		STRING,	&termcol1 },
		{ "color2",		STRING,	&termcol2 },
		{ "color3",		STRING,	&termcol3 },
		{ "color4",		STRING,	&termcol4 },
		{ "color5",		STRING,	&termcol5 },
		{ "color6",		STRING,	&termcol6 },
		{ "color7",		STRING,	&termcol7 },
		{ "color8",		STRING,	&termcol8 },
		{ "color9",		STRING,	&termcol9 },
		{ "color10",		STRING,	&termcol10 },
		{ "color11",		STRING,	&termcol11 },
		{ "color12",		STRING,	&termcol12 },
		{ "color13",		STRING,	&termcol13 },
		{ "color14",		STRING,	&termcol14 },
		{ "color15",		STRING,	&termcol15 },
		{ "lockfullscreen",     INTEGER, &lockfullscreen },
		{ "floatposgrid_x",     INTEGER, &floatposgrid_x },
		{ "floatposgrid_y",     INTEGER, &floatposgrid_y },
		{ "borderpx",          	INTEGER, &borderpx },
		{ "snap",          		INTEGER, &snap },
		{ "showbar",          	INTEGER, &showbar },
		{ "gappih",        		INTEGER, &gappih },
		{ "gappiv",        		INTEGER, &gappiv },
		{ "gappoh",        		INTEGER, &gappoh },
		{ "gappov",        		INTEGER, &gappov },
		{ "swallowfloating",    INTEGER, &swallowfloating },
		{ "topbar",          	INTEGER, &topbar },
		{ "nmaster",          	INTEGER, &nmaster },
		{ "resizehints",       	INTEGER, &resizehints },
		{ "mfact",      	 	FLOAT,   &mfact },
};

static Key keys[] = {
	/* modifier                     key        function        argument */
	{ MODKEY,                       XK_p,      spawn,          {.v = dmenucmd } },
	{ MODKEY,             XK_Return, spawn,          {.v = termcmd } },
	{ MODKEY|ControlMask,           XK_Return, riospawn,       {.v = termcmd } },
	{ MODKEY,                       XK_s,      rioresize,      {0} },
	{ MODKEY,                       XK_grave,  spawn,  {.v = scratchpadcmd } },
	{ MODKEY|ShiftMask,		XK_w,		spawn,		SHCMD("st -e nmtui; kill -45 $(pidof dwmblocks)") },
	{ MODKEY|ShiftMask,		XK_p,		spawn,		SHCMD("st -e pulsemixer; kill -44 $(pidof dwmblocks)") },
	{ MODKEY|ShiftMask,			XK_l,		spawn,		SHCMD("st -e lf-ueberzug") },
	{ MODKEY|ShiftMask|ControlMask,		XK_w,		spawn,		SHCMD("wallsetter") },
	{ MODKEY|ShiftMask,		XK_h,		spawn,		SHCMD("st -e htop") },
	{ MODKEY|ShiftMask,		XK_c,		spawn,		SHCMD("clipmenu && notify-send 'Copied'") },
	{ Mod1Mask|ShiftMask,		XK_c,		spawn,		SHCMD("sysact") },
	{ 0, XK_Print,	spawn,		SHCMD("maim ~/shots/screen-$(date +%Y.%m.%d-%H.%M.%S).png && notify-send 'Screenshot taken'") },
	{ MODKEY,                       XK_b,      togglebar,      {0} },
	STACKKEYS(MODKEY,                          focus)
	STACKKEYS(MODKEY|ShiftMask,                push)
	{ MODKEY,                       XK_i,      incnmaster,     {.i = +1 } },
	{ MODKEY,                       XK_d,      incnmaster,     {.i = -1 } },
	{ MODKEY,                       XK_h,      setmfact,       {.f = -0.05} },
	{ MODKEY,                       XK_l,      setmfact,       {.f = +0.05} },
    { Mod1Mask|ShiftMask,             XK_h,      setcfact,       {.f = +0.25} },
	{ Mod1Mask|ShiftMask,             XK_l,      setcfact,       {.f = -0.25} },
	{ Mod1Mask|ShiftMask,             XK_o,      setcfact,       {.f =  0.00} },
	{ MODKEY|Mod1Mask,              XK_z,      incrgaps,       {.i = +1 } },
	{ MODKEY|Mod1Mask|ShiftMask,    XK_z,      incrgaps,       {.i = -1 } },
/*	{ MODKEY|Mod1Mask,              XK_i,      incrigaps,      {.i = +1 } },
	{ MODKEY|Mod1Mask|ShiftMask,    XK_i,      incrigaps,      {.i = -1 } },
	{ MODKEY|Mod1Mask,              XK_o,      incrogaps,      {.i = +1 } },
	{ MODKEY|Mod1Mask|ShiftMask,    XK_o,      incrogaps,      {.i = -1 } },
	{ MODKEY|Mod1Mask,              XK_6,      incrihgaps,     {.i = +1 } },
	{ MODKEY|Mod1Mask|ShiftMask,    XK_6,      incrihgaps,     {.i = -1 } },
	{ MODKEY|Mod1Mask,              XK_7,      incrivgaps,     {.i = +1 } },
	{ MODKEY|Mod1Mask|ShiftMask,    XK_7,      incrivgaps,     {.i = -1 } },
	{ MODKEY|Mod1Mask,              XK_8,      incrohgaps,     {.i = +1 } },
	{ MODKEY|Mod1Mask|ShiftMask,    XK_8,      incrohgaps,     {.i = -1 } },
	{ MODKEY|Mod1Mask,              XK_9,      incrovgaps,     {.i = +1 } },
	{ MODKEY|Mod1Mask|ShiftMask,    XK_9,      incrovgaps,     {.i = -1 } }, */
	{ MODKEY|Mod1Mask,              XK_0,      togglegaps,     {0} },
	{ MODKEY|Mod1Mask|ShiftMask,    XK_0,      defaultgaps,    {0} },
	{ Mod1Mask,                       XK_Tab,    view,           {0} },
	{ MODKEY,             XK_q,      killclient,     {0} },
	{ MODKEY,                       XK_t,      setlayout,      {.v = &layouts[0]} },
	{ MODKEY,                       XK_e,      setlayout,      {.v = &layouts[9]} },
	{ MODKEY,                       XK_m,      setlayout,      {.v = &layouts[1]} },
	{ MODKEY|ControlMask,		XK_comma,  cyclelayout,    {.i = -1 } },
	{ MODKEY|ControlMask,           XK_period, cyclelayout,    {.i = +1 } },
	{ MODKEY,                       XK_space,  setlayout,      {0} },
	{ MODKEY|ShiftMask,             XK_space,  togglefloating, {0} },
	{ MODKEY,                       XK_f,      togglefullscreen, {0} },
	{ MODKEY|ShiftMask,             XK_f,      togglefakefullscreen, {0} },
	{ MODKEY,                       XK_0,      view,           {.ui = ~0 } },
	{ MODKEY|ShiftMask,             XK_0,      tag,            {.ui = ~0 } },
	{ MODKEY,                       XK_comma,  focusmon,       {.i = -1 } },
	{ MODKEY,                       XK_period, focusmon,       {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_comma,  tagmon,         {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_period, tagmon,         {.i = +1 } },

	/* Client position is limited to monitor window area */
	{ Mod1Mask,                     XK_Up,      floatpos,       {.v = "  0x -26y" } }, // ↑
	{ Mod1Mask,                     XK_Left,      floatpos,       {.v = "-26x   0y" } }, // ←
	{ Mod1Mask,                     XK_Right,      floatpos,       {.v = " 26x   0y" } }, // →
	{ Mod1Mask,                     XK_Down,  floatpos,       {.v = "  0x  26y" } }, // ↓
	/* Absolute positioning (allows moving windows between monitors) */
	{ Mod1Mask|ControlMask,         XK_Up,    floatpos,       {.v = "  0a -26a" } }, // ↑
	{ Mod1Mask|ControlMask,         XK_Left,  floatpos,       {.v = "-26a   0a" } }, // ←
	{ Mod1Mask|ControlMask,         XK_Right,  floatpos,       {.v = " 26a   0a" } }, // →
	{ Mod1Mask|ControlMask,         XK_Down,  floatpos,       {.v = "  0a  26a" } }, // ↓
	/* Resize client, client center position is fixed which means that client expands in all directions */
	{ Mod1Mask|ShiftMask,           XK_Up,    floatpos,       {.v = "  0w -26h" } }, // ↑
	{ Mod1Mask|ShiftMask,           XK_Left,  floatpos,       {.v = "-26w   0h" } }, // ←
	{ Mod1Mask|ShiftMask,           XK_Right,  floatpos,       {.v = " 26w   0h" } }, // →
	{ Mod1Mask|ShiftMask,           XK_Down,  floatpos,       {.v = "  0w  26h" } }, // ↓
	/* Client is positioned in a floating grid, movement is relative to client's current position */
	{ Mod4Mask|Mod1Mask,            XK_Up,     floatpos,       {.v = " 0p -1p" } }, // ↑
	{ Mod4Mask|Mod1Mask,            XK_Left,   floatpos,       {.v = "-1p  0p" } }, // ←
	{ Mod4Mask|Mod1Mask,            XK_Right,  floatpos,       {.v = " 1p  0p" } }, // →
	{ Mod4Mask|Mod1Mask,            XK_Down,   floatpos,       {.v = " 0p  1p" } }, // ↓

	{ MODKEY|ShiftMask,             XK_Escape,     togglenomodbuttons,     {0} },
	TAGKEYS(                        XK_1,                      0)
	TAGKEYS(                        XK_2,                      1)
	TAGKEYS(                        XK_3,                      2)
	TAGKEYS(                        XK_4,                      3)
	TAGKEYS(                        XK_5,                      4)
	TAGKEYS(                        XK_6,                      5)
	TAGKEYS(                        XK_7,                      6)
	TAGKEYS(                        XK_8,                      7)
	TAGKEYS(                        XK_9,                      8)
	{ MODKEY,			XK_Page_Up,	shiftview,	{ .i = -1 } },
	{ MODKEY|ShiftMask,		XK_Page_Up,	shifttag,	{ .i = -1 } },
	{ MODKEY,			XK_Page_Down,	shiftview,	{ .i = +1 } },
	{ MODKEY|ShiftMask,		XK_Page_Down,	shifttag,	{ .i = +1 } },
	{ MODKEY|ShiftMask,             XK_BackSpace, quit,        {0} },
	{ MODKEY|ControlMask|ShiftMask, XK_q,      quit,           {1} }, 
	{ MODKEY,                       XK_minus, scratchpad_show, {0} },
	{ MODKEY|ShiftMask,             XK_minus, scratchpad_hide, {0} },
	{ MODKEY,                       XK_equal,scratchpad_remove,{0} },
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static Button buttons[] = {
	/* click                event mask      button          function        argument */
	{ ClkLtSymbol,          0,              Button1,        cyclelayout,    {.i = +1 } },
	{ ClkStatusText,        0,              Button1,        sigstatusbar,   {.i = 1} },
	{ ClkStatusText,        0,              Button2,        sigstatusbar,   {.i = 2} },
	{ ClkStatusText,        0,              Button3,        sigstatusbar,   {.i = 3} },
	/* placemouse options, choose which feels more natural:
	 *    0 - tiled position is relative to mouse cursor
	 *    1 - tiled postiion is relative to window center
	 *    2 - mouse pointer warps to window center
	 *
	 * The moveorplace uses movemouse or placemouse depending on the floating state
	 * of the selected client. Set up individual keybindings for the two if you want
	 * to control these separately (i.e. to retain the feature to move a tiled window
	 * into a floating position).
	 */
	{ ClkClientWin,         MODKEY,         Button1,        moveorplace,    {.i = 1} },
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
	{ ClkClientWin,         0,         Button1,        moveorplace,    {.i = 1} },
	{ ClkClientWin,         0,         Button2,        togglefloating, {0} },
	{ ClkClientWin,         0,         Button3,        resizemouse,    {0} },
	{ ClkClientWin,         MODKEY|ShiftMask, Button3,      dragcfact,      {0} },
	{ ClkClientWin,         MODKEY|ShiftMask, Button1,      dragmfact,      {0} },
	{ ClkTagBar,            0,              Button1,        view,           {0} },
	{ ClkTagBar,            0,              Button3,        toggleview,     {0} },
	{ ClkTagBar,            MODKEY,         Button1,        tag,            {0} },
	{ ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} },
};
