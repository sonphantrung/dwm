/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>
#include <X11/Xlib-xcb.h>
#include <xcb/res.h>
#ifdef __OpenBSD__
#include <sys/sysctl.h>
#include <kvm.h>
#endif /* __OpenBSD */
#include <Imlib2.h>

#include "drw.h"
#include "util.h"

/* Definitions */
#define NOSIGNAL 1

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define GETINC(X)               ((X) - 2000)
#define INC(X)                  ((X) + 2000)
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy))) 
#define INTERSECTC(x,y,w,h,z)   (MAX(0, MIN((x)+(w),(z)->x+(z)->w) - MAX((x),(z)->x)) \
                               * MAX(0, MIN((y)+(h),(z)->y+(z)->h) - MAX((y),(z)->y)))
#define ISINC(X)                ((X) > 1000 && (X) < 3000)
#define ISVISIBLEONTAG(C, T)    ((C->tags & T))
#define ISVISIBLE(C)            ISVISIBLEONTAG(C, C->mon->tagset[C->mon->seltags])
#define PREVSEL                 3000
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MOD(N,M)                ((N)%(M) < 0 ? (N)%(M) + (M) : (N)%(M))
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define WTYPE                   "_NET_WM_WINDOW_TYPE_"
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TAGSLENGTH              (LENGTH(tags))
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)
#define TRUNC(X,A,B)            (MAX((A), MIN((X), (B))))

#define SYSTEM_TRAY_REQUEST_DOCK    0

/* XEMBED messages */
#define XEMBED_EMBEDDED_NOTIFY      0
#define XEMBED_WINDOW_ACTIVATE      1
#define XEMBED_FOCUS_IN             4
#define XEMBED_MODALITY_ON         10

#define XEMBED_MAPPED              (1 << 0)
#define XEMBED_WINDOW_ACTIVATE      1
#define XEMBED_WINDOW_DEACTIVATE    2

#define VERSION_MAJOR               0
#define VERSION_MINOR               0
#define XEMBED_EMBEDDED_VERSION (VERSION_MAJOR << 16) | VERSION_MINOR

#define OPAQUE                  0xffU

/* enums */
enum { CurResizeBR, CurResizeBL, CurResizeTR, CurResizeTL, CurNormal, CurResize, CurMove, CurResizeHorzArrow, CurResizeVertArrow, CurLast }; /* cursor */
enum { SchemeNorm, SchemeSel }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetSystemTray, NetSystemTrayOP, NetSystemTrayOrientation, NetSystemTrayOrientationHorz, NetSystemTrayVisual,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType, NetWMWindowTypeDock,
       NetWMWindowTypeDialog, NetClientList, NetDesktopNames, NetDesktopViewport, NetNumberOfDesktops, NetCurrentDesktop, NetLast }; /* EWMH atoms */
enum { Manager, Xembed, XembedInfo, XLast }; /* Xembed atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText,
       ClkClientWin, ClkRootWin, ClkLast }; /* clicks */
// Adding an enum here is not strictly necessary, but it
// can help avoid "magic" numbers in your code base. An
// example of this is when you read the code and you go
// "wtf is 58?". Here we have that DoNothing is 0, Focus
// is 1 and Urgent is 3. Other enums have a *Last value
// at the end - this is only used when one need to loop
// through all the values. Also note that you can use
// these rather than plain numbers in your configuration
// file, e.g. defnetactiverule = Focus;
// I left the configuration with plain numbers just for
// consistency.
enum { DoNothing, Focus, FocusPlus, Urgent, ShowClient, MoveClient, FocusIfShown }; /* net active rule options */

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
	char name[256];
	float mina, maxa;
    float cfact;
	int x, y, w, h;
	int sfx, sfy, sfw, sfh; /* stored float geometry, used on mode revert */
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh;
	int bw, oldbw;
	// We need to associate a specific behaviour on
	// a per-client basis. As such we need to set a
	// value for the client and therefore we also
	// need a variable to store this in.
	int onnetactive;
	unsigned int tags;
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen, isterminal, noswallow;
	int ignorecfgreqpos, ignorecfgreqsize;
	pid_t pid;
    int ignoresizehints;
	int beingmoved;
    int fakefullscreen;
	Client *next;
	Client *snext;
	Client *swallowing;
	Monitor *mon;
	Window win;
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;


typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	const char *wintype;
	unsigned int tags;
	int isfloating;
	int isterminal;
	int noswallow;
	const char *floatpos;
	int monitor;
	// Adding our new rule option, making it an int.
	// Note that the order of the fields here are
	// important and the "columns" for the rules
	// array in your config needs to be in this
	// exact order.
	int netactiverule;
} Rule;

#define RULE(...) { .monitor = -1, __VA_ARGS__ },

/* Cross patch compatibility rule macro helper macros */
#define FLOATING , .isfloating = 1
#define CENTERED
#define PERMANENT
#define FAKEFULLSCREEN
#define NOSWALLOW
#define TERMINAL
#define SWITCHTAG

#ifdef NOSIGNAL
typedef struct {
	const char *cmd;
	int id;
} StatusCmd;
#endif

typedef struct Systray   Systray;
struct Systray {
	Window win;
	Client *icons;
};

/* Xresources preferences */
enum resource_type {
	STRING = 0,
	INTEGER = 1,
	FLOAT = 2
};

typedef struct {
	char *name;
	enum resource_type type;
	void *dst;
} ResourcePref;

/* function declarations */
static void applyrules(Client *c);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachBelow(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void cyclelayout(const Arg *arg);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void dragcfact(const Arg *arg);
static void dragmfact(const Arg *arg);
static void drawbar(Monitor *m);
static void drawbars(void);
static int drawstatusbar(Monitor *m, int bh, char* text);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void floatpos(const Arg *arg);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static Atom getatomprop(Client *c, Atom prop);
static void getfloatpos(int pos, char pCh, int size, char sCh, int min_p, int max_s, int cp, int cs, int cbw, int defgrid, int *out_p, int *out_s);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
#ifdef NOSIGNAL
#else
static pid_t getstatusbarpid();
#endif
static unsigned int getsystraywidth();
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void incnmaster(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void losefullscreen(Client *next);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static void moveorplace(const Arg *arg);
static Client *nexttagged(Client *c);
static Client *nexttiled(Client *c);
static void placemouse(const Arg *arg);
static void propertynotify(XEvent *e);
static void pushstack(const Arg *arg);
static void quit(const Arg *arg);
static Client *recttoclient(int x, int y, int w, int h);
static Monitor *recttomon(int x, int y, int w, int h);
static void replaceclient(Client *old, Client *new);
static void removesystrayicon(Client *i);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizebarwin(Monitor *m);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void resizerequest(XEvent *e);
static void restack(Monitor *m);
static int riodraw(Client *c, const char slopstyle[]);
static void rioposition(Client *c, int x, int y, int w, int h);
static void rioresize(const Arg *arg);
static void riospawn(const Arg *arg);
static void run(void);
static void runautostart(void);
static void scan(void);
static void scratchpad_hide ();
static _Bool scratchpad_last_showed_is_killed (void);
static void scratchpad_remove ();
static void scratchpad_show ();
static void scratchpad_show_client (Client * c);
static void scratchpad_show_first (void);
static int sendevent(Window w, Atom proto, int m, long d0, long d1, long d2, long d3, long d4);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setcurrentdesktop(void);
static void setdesktopnames(void);
static void setfloatpos(Client *c, const char *floatpos);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setcfact(const Arg *arg);
static void setmfact(const Arg *arg);
static void setnumdesktops(void);
static void setup(void);
static void setviewport(void);
static int stackpos(const Arg *arg);
static void seturgent(Client *c, int urg);
static void shiftview(const Arg *arg);
static void shifttag(const Arg *arg);
static void showhide(Client *c);
static void showtagpreview(int tag);
static void sigchld(int unused);
#ifdef NOSIGNAL
#else
static void sigstatusbar(const Arg *arg);
#endif
static void spawn(const Arg *arg);
static pid_t spawncmd(const Arg *arg);
static void switchtag(void);
static Monitor *systraytomon(Monitor *m);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void togglebar(const Arg *arg);
static void togglefakefullscreen(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatecurrentdesktop(void);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updatepreview(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatesystray(void);
static void updatesystrayicongeom(Client *i, int w, int h);
static void updatesystrayiconstate(Client *i, XPropertyEvent *ev);
static void updatetitle(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static Client *wintosystrayicon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void load_xresources(void);
static void resource_load(XrmDatabase db, char *name, enum resource_type rtype, void *dst);
static void xinitvisual();

static int swallow(Client *p, Client *c);
static void unswallow(Client *c);
static pid_t getparentprocess(pid_t p);
static int isdescprocess(pid_t p, pid_t c);
static Client *swallowingclient(Window w);
static Client *termforwin(const Client *c);
static pid_t winpid(Window w);

/* variables */
static Systray *systray =  NULL;
static const char autostartblocksh[] = "autostart_blocking.sh";
static const char autostartsh[] = "autostart.sh";
static const char broken[] = "broken";
static const char dwmdir[] = "dwm";
static const char localshare[] = ".local/share";
static char stext[1024];
#ifdef NOSIGNAL
static int statusw;
static int statuscmdn;
static char lastbutton[] = "-";
#else
static int statussig;
static int statusw;
static pid_t statuspid = -1;
#endif
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh, blw = 0;      /* bar geometry */
static int lrpad;            /* sum of left and right padding for text */
static int vp;               /* vertical padding for bar */
static int sp;               /* side padding for bar */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static int riodimensions[4] = { -1, -1, -1, -1 };
static pid_t riopid = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[MotionNotify] = motionnotify,
	[PropertyNotify] = propertynotify,
	[ResizeRequest] = resizerequest,
	[UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast], xatom[XLast];
static int running = 1;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;
static xcb_connection_t *xcon;

/* scratchpad */
# define SCRATCHPAD_MASK (1u << sizeof tags / sizeof * tags)
static Client * scratchpad_last_showed = NULL;

static int useargb = 0;
static Visual *visual;
static int depth;
static Colormap cmap;

#include "vanitygaps.h"
/* configuration, allows nested code to access above variables */
#include "config.h"

typedef struct Pertag Pertag;
struct Monitor {
	char ltsymbol[16];
	float mfact;
	int nmaster;
	int num;
	int by;               /* bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	int gappih;           /* horizontal gap between windows */
	int gappiv;           /* vertical gap between windows */
	int gappoh;           /* horizontal outer gaps */
	int gappov;           /* vertical outer gaps */
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	int showbar;
	int topbar;
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	Window barwin;
    Window tagwin;
    Pixmap tagmap[LENGTH(tags)];
    int previewshow;
    const Layout *lt[2];
	Pertag *pertag;
};

#include "shiftview.c"

struct Pertag {
	unsigned int curtag, prevtag; /* current and previous tag */
	int nmasters[LENGTH(tags) + 1]; /* number of windows in master area */
	float mfacts[LENGTH(tags) + 1]; /* mfacts per tag */
	unsigned int sellts[LENGTH(tags) + 1]; /* selected layouts */
	const Layout *ltidxs[LENGTH(tags) + 1][2]; /* matrix of tags and layouts indexes  */
	int showbars[LENGTH(tags) + 1]; /* display bar for the current tag */
    int enablegaps[LENGTH(tags) + 1];
    int gappihs[LENGTH(tags) + 1];
    int gappivs[LENGTH(tags) + 1];
    int gappohs[LENGTH(tags) + 1];
    int gappovs[LENGTH(tags) + 1];
};

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 30 ? -1 : 1]; };

/* function implementations */
void
applyrules(Client *c)
{
	const char *class, *instance;
	Atom wintype;
	unsigned int i;
	const Rule *r;
	Monitor *m;
	XClassHint ch = { NULL, NULL };

	/* rule matching */
	c->isfloating = 0;
	c->tags = 0;
	// In case we have no rule set up for our client, or
	// the rule value is -1, then we'll want to set the
	// default action to take when we receive a net active
	// signal.
	c->onnetactive = defnetactiverule;
	XGetClassHint(dpy, c->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;
	wintype  = getatomprop(c, netatom[NetWMWindowType]);

	for (i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if ((!r->title || strstr(c->name, r->title))
		&& (!r->class || strstr(class, r->class))
		&& (!r->instance || strstr(instance, r->instance))
		&& (!r->wintype || wintype == XInternAtom(dpy, r->wintype, False)))
		{
			c->isterminal = r->isterminal;
			c->noswallow  = r->noswallow;
			c->isfloating = r->isfloating;
			c->tags |= r->tags;
			for (m = mons; m && m->num != r->monitor; m = m->next);
			if (m)
				c->mon = m;
			if (c->isfloating && r->floatpos)
				setfloatpos(c, r->floatpos);
			// If our net active rule is not -1, then associate
			// that action value with our client.
			if (r->netactiverule > -1)
				c->onnetactive = r->netactiverule;

		}
	}
	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);
	if (c->tags != SCRATCHPAD_MASK)
		c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *m = c->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) {
		if (*x > sw)
			*x = sw - WIDTH(c);
		if (*y > sh)
			*y = sh - HEIGHT(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else {
		if (*x >= m->wx + m->ww)
			*x = m->wx + m->ww - WIDTH(c);
		if (*y >= m->wy + m->wh)
			*y = m->wy + m->wh - HEIGHT(c);
		if (*x + *w + 2 * c->bw <= m->wx)
			*x = m->wx;
		if (*y + *h + 2 * c->bw <= m->wy)
			*y = m->wy;
	}
	if (*h < bh)
		*h = bh;
	if (*w < bh)
		*w = bh;
	if (!c->ignoresizehints && (resizehints || c->isfloating || !c->mon->lt[c->mon->sellt]->arrange)) {
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if (!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if (c->mina > 0 && c->maxa > 0) {
			if (c->maxa < (float)*w / *h)
				*w = *h * c->maxa + 0.5;
			else if (c->mina < (float)*h / *w)
				*h = *w * c->mina + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		if (c->incw)
			*w -= *w % c->incw;
		if (c->inch)
			*h -= *h % c->inch;
		/* restore base dimensions */
		*w = MAX(*w + c->basew, c->minw);
		*h = MAX(*h + c->baseh, c->minh);
		if (c->maxw)
			*w = MIN(*w, c->maxw);
		if (c->maxh)
			*h = MIN(*h, c->maxh);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor *m)
{
	if (m)
		showhide(m->stack);
	else for (m = mons; m; m = m->next)
		showhide(m->stack);
	if (m) {
		arrangemon(m);
		restack(m);
	} else for (m = mons; m; m = m->next)
		arrangemon(m);
}

void
arrangemon(Monitor *m)
{
    updatebarpos(m);
    updatesystray();
    XMoveWindow(dpy, m->tagwin, m->wx + m->gappov, m->by + (m->topbar ? (bh + m->gappoh) : (- (m->mh / scalepreview) - m->gappoh)));
    strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);
	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
}

void
attach(Client *c)
{
	c->next = c->mon->clients;
	c->mon->clients = c;
}
void
attachBelow(Client *c)
{
	//If there is nothing on the monitor or the selected client is floating, attach as normal
	if(c->mon->sel == NULL || c->mon->sel->isfloating) {
        Client *at = nexttagged(c);
        if(!at) {
            attach(c);
            return;
            }
        c->next = at->next;
        at->next = c;
		return;
	}

	//Set the new client's next property to the same as the currently selected clients next
	c->next = c->mon->sel->next;
	//Set the currently selected clients next property to the new client
	c->mon->sel->next = c;

}

void
attachstack(Client *c)
{
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

void
buttonpress(XEvent *e)
{
	unsigned int i, x, click, occ = 0;
	Arg arg = {0};
	Client *c;
	Monitor *m;
	XButtonPressedEvent *ev = &e->xbutton;

	click = ClkRootWin;
	/* focus monitor if necessary */
	if ((m = wintomon(ev->window)) && m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
    if (ev->window == selmon->barwin) {
    	if (selmon->previewshow) {
		XUnmapWindow(dpy, selmon->tagwin);
			selmon->previewshow = 0;
        }
		i = x = 0;
		for (c = m->clients; c; c = c->next)
			occ |= c->tags == 255 ? 0 : c->tags;
		do {
			/* do not reserve space for vacant tags */
			if (!(occ & 1 << i || m->tagset[m->seltags] & 1 << i))
				continue;
			x += TEXTW(tags[i]);
		} while (ev->x >= x && ++i < LENGTH(tags));
		if (i < LENGTH(tags)) {
			click = ClkTagBar;
			arg.ui = 1 << i;
		} else if (ev->x < x + blw)
			click = ClkLtSymbol;
		else if (ev->x > selmon->ww - statusw){
			x = selmon->ww - statusw;
            click = ClkStatusText;
			char *text, *s, ch;
        #ifdef NOSIGNAL
			*lastbutton = '0' + ev->button;
			statuscmdn = 0;
        #else
			statussig = 0;
        #endif
			for (text = s = stext; *s && x <= ev->x; s++) {
				if ((unsigned char)(*s) < ' ') {
					ch = *s;
					*s = '\0';
					x += TEXTW(text) - lrpad;
					*s = ch;
					text = s + 1;
					if (x >= ev->x)
						break;
                #ifdef NOSIGNAL
                    statuscmdn = ch;
                #else
					statussig = ch;
                #endif
				} else if (*s == '^') {
                #ifdef NOSIGNAL
                #else
					*s = '\0';
					x += TEXTW(text) - lrpad;
					*s = '^';
					if (*(++s) == 'f')
						x += atoi(++s);
					while (*(s++) != '^');
					text = s;
					s--;
                #endif
				}
			}
		}
    } else if ((c = wintoclient(ev->window))) {
		focus(c);
		restack(selmon);
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		click = ClkClientWin;
	}
	for (i = 0; i < LENGTH(buttons); i++)
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	/* this causes an error if some other window manager is running */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}

void
cleanup(void)
{
	Arg a = {.ui = ~0};
	Layout foo = { "", NULL };
	Monitor *m;
	size_t i;

	view(&a);
	selmon->lt[selmon->sellt] = &foo;
	for (m = mons; m; m = m->next)
		while (m->stack)
			unmanage(m->stack, 0);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	while (mons)
		cleanupmon(mons);
	if (showsystray) {
		XUnmapWindow(dpy, systray->win);
		XDestroyWindow(dpy, systray->win);
		free(systray);
	}
	for (i = 0; i < CurLast; i++)
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colors) + 1; i++)
		free(scheme[i]);
	XDestroyWindow(dpy, wmcheckwin);
	drw_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void
cleanupmon(Monitor *mon)
{
	Monitor *m;
    size_t i;

	if (mon == mons)
		mons = mons->next;
	else {
		for (m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
  for (i = 0; i < LENGTH(tags); i++) {
       if (mon->tagmap[i])
	         XFreePixmap(dpy, mon->tagmap[i]);
}
	XUnmapWindow(dpy, mon->barwin);
	XDestroyWindow(dpy, mon->barwin);
    XUnmapWindow(dpy, mon->tagwin);
    XDestroyWindow(dpy, mon->tagwin);
    free(mon);
}

void
clientmessage(XEvent *e)
{
	XWindowAttributes wa;
	XSetWindowAttributes swa;
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);
	unsigned int i;

	if (showsystray && cme->window == systray->win && cme->message_type == netatom[NetSystemTrayOP]) {
		/* add systray icons */
		if (cme->data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
			if (!(c = (Client *)calloc(1, sizeof(Client))))
				die("fatal: could not malloc() %u bytes\n", sizeof(Client));
			if (!(c->win = cme->data.l[2])) {
				free(c);
				return;
			}

			/* Clear status bar to avoid artifacts beneath systray icons */
			drw_rect(drw, 0, 0, selmon->ww, bh, 1, 1);
			drw_map(drw, selmon->barwin, 0, 0, selmon->ww, bh);

			c->mon = selmon;
			c->next = systray->icons;
			systray->icons = c;
			if (!XGetWindowAttributes(dpy, c->win, &wa)) {
				/* use sane defaults */
				wa.width = bh;
				wa.height = bh;
				wa.border_width = 0;
			}
			c->x = c->oldx = c->y = c->oldy = 0;
			c->w = c->oldw = wa.width;
			c->h = c->oldh = wa.height;
			c->oldbw = wa.border_width;
			c->bw = 0;
			c->isfloating = True;
			/* reuse tags field as mapped status */
			c->tags = 1;
			updatesizehints(c);
			updatesystrayicongeom(c, wa.width, wa.height);
			XAddToSaveSet(dpy, c->win);
			XSelectInput(dpy, c->win, StructureNotifyMask | PropertyChangeMask | ResizeRedirectMask);
			XReparentWindow(dpy, c->win, systray->win, 0, 0);
			/* use parents background color */
			swa.background_pixel  = scheme[SchemeNorm][ColBg].pixel;
			XChangeWindowAttributes(dpy, c->win, CWBackPixel, &swa);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_EMBEDDED_NOTIFY, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			/* FIXME not sure if I have to send these events, too */
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_FOCUS_IN, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_MODALITY_ON, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			XSync(dpy, False);
			resizebarwin(selmon);
			updatesystray();
			setclientstate(c, NormalState);
		}
		return;
	}
	if (!c)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen]) {
			if (c->fakefullscreen == 2 && c->isfullscreen)
				c->fakefullscreen = 3;
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
		}
	} else if (cme->message_type == netatom[NetActiveWindow]) {
		// OK, so we have received the net active window signal,
		// let's decide what to do about it.
		switch(c->onnetactive) {
		default:
			break;
		case DoNothing:
			// Yes let's just not do anything. This is
			// redudnant as we could have just left it
			// for the default, but at last this is
			// explicit and sort of readable.
			break;
		case Focus:
			// This is lifted straight from the original
			// focusonnetactive patch.
			for (i = 0; i < LENGTH(tags) && !((1 << i) & c->tags); i++);
			if (i < LENGTH(tags)) {
				const Arg a = {.ui = 1 << i};
				selmon = c->mon;
				view(&a);
				focus(c);
				restack(selmon);
			}
			break;
		case FocusPlus:
			// Similar to the original focusonnetactive
			// logic, but shows the client's tag in
			// addition to your current tags.
			for (i = 0; i < LENGTH(tags) && !((1 << i) & c->tags); i++);
			if (i < LENGTH(tags)) {
				selmon = c->mon;
				view(&((Arg) {.ui = c->mon->seltags | (1 << i)}));
				focus(c);
				restack(selmon);
			}
			break;
		case ShowClient:
			// If client is not already on any of the currently
			// viewed tags, then let the client be shown on the
			// currently viewed tag(s) in addition to the client's
			// existing tags.
			if (!(c->mon->tagset[c->mon->seltags] & c->tags))
				c->tags |= c->mon->tagset[c->mon->seltags];
			focus(c);
			arrange(c->mon);
			break;
		case MoveClient:
			// If client is not already on any of the currently
			// viewed tags, then move the client to the currently
			// viewed tag(s).
			if (!(c->mon->tagset[c->mon->seltags] & c->tags))
				c->tags = c->mon->tagset[c->mon->seltags];
			focus(c);
			arrange(c->mon);
			break;
		case FocusIfShown:
			// If client is already shown on the currently viewed
			// tag then focus it, otherwise do nothing.
			if ((c->mon->tagset[c->mon->seltags] & c->tags))
				focus(c);
			break;
		case Urgent:
			// This is simply the original code.
			if (c != selmon->sel && !c->isurgent)
				seturgent(c, 1);
			break;
		// You could easily extend this to add other behaviours
		// should you want it.
		}
	}
}

void
configure(Client *c)
{
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent *e)
{
	Monitor *m;
	Client *c;
	XConfigureEvent *ev = &e->xconfigure;
	int dirty;

	/* TODO: updategeom handling sucks, needs to be simplified */
	if (ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if (updategeom() || dirty) {
			drw_resize(drw, sw, bh);
			updatebars();
			for (m = mons; m; m = m->next) {
				for (c = m->clients; c; c = c->next)
					if (c->isfullscreen && c->fakefullscreen != 1)
						resizeclient(c, m->mx, m->my, m->mw, m->mh);
				resizebarwin(m);
			}
			focus(NULL);
			arrange(NULL);
		}
	}
}

void
configurerequest(XEvent *e)
{
	Client *c;
	Monitor *m;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if ((c = wintoclient(ev->window))) {
		if (ev->value_mask & CWBorderWidth)
			c->bw = ev->border_width;
		else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange) {
			if (c->ignorecfgreqpos && c->ignorecfgreqsize)
				return;

			m = c->mon;
			if (!c->ignorecfgreqpos) {
				if (ev->value_mask & CWX) {
					c->oldx = c->x;
					c->x = m->mx + ev->x;
				}
				if (ev->value_mask & CWY) {
					c->oldy = c->y;
					c->y = m->my + ev->y;
				}
			}
			if (!c->ignorecfgreqsize) {
				if (ev->value_mask & CWWidth) {
					c->oldw = c->w;
					c->w = ev->width;
				}
				if (ev->value_mask & CWHeight) {
					c->oldh = c->h;
					c->h = ev->height;
				}
			}
			if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
				c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
			if ((c->y + c->h) > m->my + m->mh && c->isfloating)
				c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			if (ISVISIBLE(c))
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		} else
			configure(c);
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

Monitor *
createmon(void)
{
	Monitor *m;
	unsigned int i;

	m = ecalloc(1, sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->mfact = mfact;
	m->nmaster = nmaster;
	m->showbar = showbar;
	m->topbar = topbar;
	m->gappih = gappih;
	m->gappiv = gappiv;
	m->gappoh = gappoh;
	m->gappov = gappov;
	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[1 % LENGTH(layouts)];
    for (i = 0; i < LENGTH(tags); i++)
	  m->tagmap[i] = 0;
    m->previewshow = 0;
    strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
	m->pertag = ecalloc(1, sizeof(Pertag));
	m->pertag->curtag = m->pertag->prevtag = 1;

	for (i = 0; i <= LENGTH(tags); i++) {
		m->pertag->nmasters[i] = m->nmaster;
		m->pertag->mfacts[i] = m->mfact;

		m->pertag->ltidxs[i][0] = m->lt[0];
		m->pertag->ltidxs[i][1] = m->lt[1];
		m->pertag->sellts[i] = m->sellt;

		m->pertag->showbars[i] = m->showbar;
        m->pertag->enablegaps[i] = 1;
        m->pertag->gappihs[i] = m->gappih;
        m->pertag->gappivs[i] = m->gappiv;
        m->pertag->gappohs[i] = m->gappoh;
        m->pertag->gappovs[i] = m->gappov;
	}

	return m;
}

void
cyclelayout(const Arg *arg) {
	Layout *l;
	for(l = (Layout *)layouts; l != selmon->lt[selmon->sellt]; l++);
	if(arg->i > 0) {
		if(l->symbol && (l + 1)->symbol)
			setlayout(&((Arg) { .v = (l + 1) }));
		else
			setlayout(&((Arg) { .v = layouts }));
	} else {
		if(l != layouts && (l - 1)->symbol)
			setlayout(&((Arg) { .v = (l - 1) }));
		else
			setlayout(&((Arg) { .v = &layouts[LENGTH(layouts) - 2] }));
	}
}

void
destroynotify(XEvent *e)
{
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if ((c = wintoclient(ev->window)))
		unmanage(c, 1);
	else if ((c = swallowingclient(ev->window)))
		unmanage(c->swallowing, 1);
	else if ((c = wintosystrayicon(ev->window))) {
		removesystrayicon(c);
		resizebarwin(selmon);
		updatesystray();
	}
}

void
detach(Client *c)
{
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void
detachstack(Client *c)
{
	Client **tc, *t;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if (c == c->mon->sel) {
		for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
		c->mon->sel = t;
	}
}

Monitor *
dirtomon(int dir)
{
	Monitor *m = NULL;

	if (dir > 0) {
		if (!(m = selmon->next))
			m = mons;
	} else if (selmon == mons)
		for (m = mons; m->next; m = m->next);
	else
		for (m = mons; m->next != selmon; m = m->next);
	return m;
}

int
drawstatusbar(Monitor *m, int bh, char* stext) {
	int ret, i, j, w, x, len;
	short isCode = 0;
	char *text;
	char *p;
	Clr oldbg, oldfg;

	len = strlen(stext) + 1 ;
	if (!(text = (char*) malloc(sizeof(char)*len)))
		die("malloc");
	p = text;

	i = -1, j = 0;
	while (stext[++i])
		if ((unsigned char)stext[i] >= ' ')
			text[j++] = stext[i];
	text[j] = '\0';

	/* compute width of the status text */
	w = 0;
	i = -1;
	while (text[++i]) {
		if (text[i] == '^') {
			if (!isCode) {
				isCode = 1;
				text[i] = '\0';
				w += TEXTW(text) - lrpad;
				text[i] = '^';
				if (text[++i] == 'f')
					w += atoi(text + ++i);
			} else {
				isCode = 0;
				text = text + i + 1;
				i = -1;
			}
		}
	}
	if (!isCode)
		w += TEXTW(text) - lrpad;
	else
		isCode = 0;
	text = p;

	w += 2; /* 1px padding on both sides */
	if (showsystray && m == systraytomon(m) && !systrayonleft)
		ret = x = m->ww - w - 2 * sp - getsystraywidth();
	else
		ret = x = m->ww - w - 2 * sp;

	drw_setscheme(drw, scheme[LENGTH(colors)]);
	drw->scheme[ColFg] = scheme[SchemeNorm][ColFg];
	drw->scheme[ColBg] = scheme[SchemeNorm][ColBg];
	drw_rect(drw, x, 0, w, bh, 1, 1);
	x++;

	/* process status text */
	i = -1;
	while (text[++i]) {
		if (text[i] == '^' && !isCode) {
			isCode = 1;

			text[i] = '\0';
			w = TEXTW(text) - lrpad;
			drw_text(drw, x, 0, w, bh, 0, text, 0);

			x += w;

			/* process code */
			while (text[++i] != '^') {
				if (text[i] == 'c') {
					char buf[8];
					memcpy(buf, (char*)text+i+1, 7);
					buf[7] = '\0';
					drw_clr_create(drw, &drw->scheme[ColFg], buf, 0xff);
					i += 7;
				} else if (text[i] == 'b') {
					char buf[8];
					memcpy(buf, (char*)text+i+1, 7);
					buf[7] = '\0';
					drw_clr_create(drw, &drw->scheme[ColBg], buf, 0xff);
					i += 7;
				} else if (text[i] == 'C') {
					int c = atoi(text + ++i);
					drw_clr_create(drw, &drw->scheme[ColFg], termcolor[c], 0xff);
				} else if (text[i] == 'B') {
					int c = atoi(text + ++i);
					drw_clr_create(drw, &drw->scheme[ColBg], termcolor[c], 0xff);				
                } else if (text[i] == 'd') {
					drw->scheme[ColFg] = scheme[SchemeNorm][ColFg];
					drw->scheme[ColBg] = scheme[SchemeNorm][ColBg];
				} else if (text[i] == 'w') {
					Clr swp;
					swp = drw->scheme[ColFg];
					drw->scheme[ColFg] = drw->scheme[ColBg];
					drw->scheme[ColBg] = swp;
				} else if (text[i] == 'v') {
					oldfg = drw->scheme[ColFg];
					oldbg = drw->scheme[ColBg];
				} else if (text[i] == 't') {
					drw->scheme[ColFg] = oldfg;
					drw->scheme[ColBg] = oldbg;
                } else if (text[i] == 'r') {
					int rx = atoi(text + ++i);
					while (text[++i] != ',');
					int ry = atoi(text + ++i);
					while (text[++i] != ',');
					int rw = atoi(text + ++i);
					while (text[++i] != ',');
					int rh = atoi(text + ++i);

					drw_rect(drw, rx + x, ry, rw, rh, 1, 0);
				} else if (text[i] == 'f') {
					x += atoi(text + ++i);
				}
			}

			text = text + i + 1;
			i=-1;
			isCode = 0;
		}
	}

	if (!isCode) {
		w = TEXTW(text) - lrpad;
		drw_text(drw, x, 0, w, bh, 0, text, 0);
	}

	drw_setscheme(drw, scheme[SchemeNorm]);
	free(p);

	return ret;
}

void
dragmfact(const Arg *arg)
{
	unsigned int n;
	int py, px; // pointer coordinates
	int ax, ay, aw, ah; // area position, width and height
	int center = 0, horizontal = 0, mirror = 0, fixed = 0; // layout configuration
	double fact;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	m = selmon;

	int oh, ov, ih, iv;
	getgaps(m, &oh, &ov, &ih, &iv, &n);

	ax = m->wx;
	ay = m->wy;
	ah = m->wh;
	aw = m->ww;

	if (!n)
		return;
	else if (m->lt[m->sellt]->arrange == &centeredmaster && (fixed || n - m->nmaster > 1))
		center = 1;
	else if (m->lt[m->sellt]->arrange == &centeredfloatingmaster)
		center = 1;
	else if (m->lt[m->sellt]->arrange == &bstack)
		horizontal = 1;

	/* do not allow mfact to be modified under certain conditions */
	if (!m->lt[m->sellt]->arrange                            // floating layout
		|| (!fixed && m->nmaster && n <= m->nmaster)         // no master
		|| m->lt[m->sellt]->arrange == &monocle
		|| m->lt[m->sellt]->arrange == &gaplessgrid
	)
		return;

	ay += oh;
	ax += ov;
	aw -= 2*ov;
	ah -= 2*oh;

	if (center) {
		if (horizontal) {
			px = ax + aw / 2;
			py = ay + ah / 2 + (ah - 2*ih) * (m->mfact / 2.0) + ih / 2;
		} else { // vertical split
			px = ax + aw / 2 + (aw - 2*iv) * m->mfact / 2.0 + iv / 2;
			py = ay + ah / 2;
		}
	} else if (horizontal) {
		px = ax + aw / 2;
		if (mirror)
			py = ay + (ah - ih) * (1.0 - m->mfact) + ih / 2;
		else
			py = ay + ((ah - ih) * m->mfact) + ih / 2;
	} else { // vertical split
		if (mirror)
			px = ax + (aw - iv) * (1.0 - m->mfact) + iv / 2;
		else
			px = ax + ((aw - iv) * m->mfact) + iv / 2;
		py = ay + ah / 2;
	}

	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[horizontal ? CurResizeVertArrow : CurResizeHorzArrow]->cursor, CurrentTime) != GrabSuccess)
		return;
	XWarpPointer(dpy, None, root, 0, 0, 0, 0, px, py);

	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 40))
				continue;
			if (lasttime != 0) {
				px = ev.xmotion.x;
				py = ev.xmotion.y;
			}
			lasttime = ev.xmotion.time;

			if (center)
				if (horizontal)
					if (py - ay > ah / 2)
						fact = (double) 1.0 - (ay + ah - py - ih / 2) * 2 / (double) (ah - 2*ih);
					else
						fact = (double) 1.0 - (py - ay - ih / 2) * 2 / (double) (ah - 2*ih);
				else
					if (px - ax > aw / 2)
						fact = (double) 1.0 - (ax + aw - px - iv / 2) * 2 / (double) (aw - 2*iv);
					else
						fact = (double) 1.0 - (px - ax - iv / 2) * 2 / (double) (aw - 2*iv);
			else
				if (horizontal)
					fact = (double) (py - ay - ih / 2) / (double) (ah - ih);
				else
					fact = (double) (px - ax - iv / 2) / (double) (aw - iv);

			if (!center && mirror)
				fact = 1.0 - fact;

			setmfact(&((Arg) { .f = 1.0 + fact }));
			px = ev.xmotion.x;
			py = ev.xmotion.y;
			break;
		}
	} while (ev.type != ButtonRelease);

	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
dragcfact(const Arg *arg)
{
	int prev_x, prev_y, dist_x, dist_y;
	float fact;
	Client *c;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfloating) {
		resizemouse(arg);
		return;
	}
	#if !FAKEFULLSCREEN_PATCH
	#if FAKEFULLSCREEN_CLIENT_PATCH
	if (c->isfullscreen && !c->fakefullscreen) /* no support resizing fullscreen windows by mouse */
		return;
	#else
	if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
		return;
	#endif // FAKEFULLSCREEN_CLIENT_PATCH
	#endif // !FAKEFULLSCREEN_PATCH
	restack(selmon);

	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
		return;
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w/2, c->h/2);

	prev_x = prev_y = -999999;

	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;
			if (prev_x == -999999) {
				prev_x = ev.xmotion.x_root;
				prev_y = ev.xmotion.y_root;
			}

			dist_x = ev.xmotion.x - prev_x;
			dist_y = ev.xmotion.y - prev_y;

			if (abs(dist_x) > abs(dist_y)) {
				fact = (float) 4.0 * dist_x / c->mon->ww;
			} else {
				fact = (float) -4.0 * dist_y / c->mon->wh;
			}

			if (fact)
				setcfact(&((Arg) { .f = fact }));

			prev_x = ev.xmotion.x;
			prev_y = ev.xmotion.y;
			break;
		}
	} while (ev.type != ButtonRelease);


	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w/2, c->h/2);

	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
drawbar(Monitor *m)
{
	int x, w, tw = 0, stw = 0;
	unsigned int i, occ = 0, urg = 0;
	Client *c;

	/* draw status first so it can be overdrawn by tags later */
	if (m == selmon) { /* status is only drawn on selected monitor */
		tw = statusw = m->ww - drawstatusbar(m, bh, stext);
	}

	resizebarwin(m);
	for (c = m->clients; c; c = c->next) {
		occ |= c->tags == 255 ? 0 : c->tags;
		if (c->isurgent)
			urg |= c->tags;
	}
	x = 0;
	for (i = 0; i < LENGTH(tags); i++) {
		/* do not draw vacant tags */
		if (!(occ & 1 << i || m->tagset[m->seltags] & 1 << i))
		continue;

		w = TEXTW(tags[i]);
		drw_setscheme(drw, scheme[m->tagset[m->seltags] & 1 << i ? SchemeSel : SchemeNorm]);
		drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
		if (ulineall || m->tagset[m->seltags] & 1 << i) /* if there are conflicts, just move these lines directly underneath both 'drw_setscheme' and 'drw_text' :) */
			drw_rect(drw, x + ulinepad, bh - ulinestroke - ulinevoffset, w - (ulinepad * 2), ulinestroke, 1, 0);
		x += w;
	}
	w = blw = TEXTW(m->ltsymbol);
	drw_setscheme(drw, scheme[SchemeNorm]);
	x = drw_text(drw, x, 0, w, bh, lrpad / 2, m->ltsymbol, 0);

	if ((w = m->ww - tw - stw - x) > bh) {
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_rect(drw, x, 0, w, bh, 1, 1);
	}
	drw_map(drw, m->barwin, 0, 0, m->ww - stw, bh);
}

void
drawbars(void)
{
	Monitor *m;

	for (m = mons; m; m = m->next)
		drawbar(m);
}

void
enternotify(XEvent *e)
{
	Client *c;
	Monitor *m;
	XCrossingEvent *ev = &e->xcrossing;

	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
		return;
	c = wintoclient(ev->window);
	m = c ? c->mon : wintomon(ev->window);
	if (m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
	} else if (!c || c == selmon->sel)
		return;
	focus(c);
}

void
expose(XEvent *e)
{
	Monitor *m;
	XExposeEvent *ev = &e->xexpose;

	if (ev->count == 0 && (m = wintomon(ev->window))) {
		drawbar(m);
		if (m == selmon)
			updatesystray();
	}
}

void
floatpos(const Arg *arg)
{
	Client *c = selmon->sel;

	if (!c || (selmon->lt[selmon->sellt]->arrange && !c->isfloating))
		return;

	setfloatpos(c, (char *)arg->v);
	resizeclient(c, c->x, c->y, c->w, c->h);

	XRaiseWindow(dpy, c->win);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w/2, c->h/2);
}

void
focus(Client *c)
{
	if (!c || !ISVISIBLE(c))
		for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
	if (selmon->sel && selmon->sel != c) {
		losefullscreen(c);
		unfocus(selmon->sel, 0);
	}
	if (c) {
		if (c->mon != selmon)
			selmon = c->mon;
		if (c->isurgent)
			seturgent(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
		setfocus(c);
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	selmon->sel = c;
	drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;

	if (selmon->sel && ev->window != selmon->sel->win)
		setfocus(selmon->sel);
}

void
focusmon(const Arg *arg)
{
	Monitor *m;

	if (!mons->next)
		return;
	if ((m = dirtomon(arg->i)) == selmon)
		return;
	unfocus(selmon->sel, 0);
	selmon = m;
	focus(NULL);
}

void
focusstack(const Arg *arg)
{
	int i = stackpos(arg);
	Client *c, *p;

	if (i < 0 || (selmon->sel->isfullscreen && lockfullscreen))
		return;

	for(p = NULL, c = selmon->clients; c && (i || !ISVISIBLE(c));
	    i -= ISVISIBLE(c) ? 1 : 0, p = c, c = c->next);
	focus(c ? c : p);
	restack(selmon);
}

Atom
getatomprop(Client *c, Atom prop)
{
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;
	/* FIXME getatomprop should return the number of items and a pointer to
	 * the stored data instead of this workaround */
	Atom req = XA_ATOM;
	if (prop == xatom[XembedInfo])
		req = xatom[XembedInfo];

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, req,
		&da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		if (da == xatom[XembedInfo] && dl == 2)
			atom = ((Atom *)p)[1];
		XFree(p);
	}
	return atom;
}
#ifdef NOSIGNAL
#else
pid_t
getstatusbarpid()
{
	char buf[32], *str = buf, *c;
	FILE *fp;

	if (statuspid > 0) {
		snprintf(buf, sizeof(buf), "/proc/%u/cmdline", statuspid);
		if ((fp = fopen(buf, "r"))) {
			fgets(buf, sizeof(buf), fp);
			while ((c = strchr(str, '/')))
				str = c + 1;
			fclose(fp);
			if (!strcmp(str, STATUSBAR))
				return statuspid;
		}
	}
	if (!(fp = popen("pidof -s "STATUSBAR, "r")))
		return -1;
	fgets(buf, sizeof(buf), fp);
	pclose(fp);
	return strtoul(buf, NULL, 10);
}
#endif

void
getfloatpos(int pos, char pCh, int size, char sCh, int min_p, int max_s, int cp, int cs, int cbw, int defgrid, int *out_p, int *out_s)
{
	int abs_p, abs_s, i, delta, rest;

	abs_p = pCh == 'A' || pCh == 'a';
	abs_s = sCh == 'A' || sCh == 'a';

	cs += 2*cbw;

	switch(pCh) {
	case 'A': // absolute position
		cp = pos;
		break;
	case 'a': // absolute relative position
		cp += pos;
		break;
	case 'y':
	case 'x': // client relative position
		cp = MIN(cp + pos, min_p + max_s);
		break;
	case 'Y':
	case 'X': // client position relative to monitor
		cp = min_p + MIN(pos, max_s);
		break;
	case 'S': // fixed client position (sticky)
	case 'C': // fixed client position (center)
	case 'Z': // fixed client right-hand position (position + size)
		if (pos == -1)
			break;
		pos = MAX(MIN(pos, max_s), 0);
		if (pCh == 'Z')
			cs = abs((cp + cs) - (min_p + pos));
		else if (pCh == 'C')
			cs = abs((cp + cs / 2) - (min_p + pos));
		else
			cs = abs(cp - (min_p + pos));
		cp = min_p + pos;
		sCh = 0; // size determined by position, override defined size
		break;
	case 'G': // grid
		if (pos <= 0)
			pos = defgrid; // default configurable
		if (size == 0 || pos < 2 || (sCh != 'p' && sCh != 'P'))
			break;
		delta = (max_s - cs) / (pos - 1);
		rest = max_s - cs - delta * (pos - 1);
		if (sCh == 'P') {
			if (size < 1 || size > pos)
				break;
			cp = min_p + delta * (size - 1);
		} else {
			for (i = 0; i < pos && cp >= min_p + delta * i + (i > pos - rest ? i + rest - pos + 1 : 0); i++);
			cp = min_p + delta * (MAX(MIN(i + size, pos), 1) - 1) + (i > pos - rest ? i + rest - pos + 1 : 0);
		}
		break;
	}

	switch(sCh) {
	case 'A': // absolute size
		cs = size;
		break;
	case 'a': // absolute relative size
		cs = MAX(1, cs + size);
		break;
	case '%': // client size percentage in relation to monitor window area size
		if (size <= 0)
			break;
		size = max_s * MIN(size, 100) / 100;
		/* falls through */
	case 'h':
	case 'w': // size relative to client
		if (sCh == 'w' || sCh == 'h') {
			if (size == 0)
				break;
			size += cs;
		}
		/* falls through */
	case 'H':
	case 'W': // normal size, position takes precedence
		if (pCh == 'S' && cp + size > min_p + max_s)
			size = min_p + max_s - cp;
		else if (size > max_s)
			size = max_s;

		if (pCh == 'C') { // fixed client center, expand or contract client
			delta = size - cs;
			if (delta < 0 || (cp - delta / 2 + size <= min_p + max_s))
				cp -= delta / 2;
			else if (cp - delta / 2 < min_p)
				cp = min_p;
			else if (delta)
				cp = min_p + max_s;
		} else if (pCh == 'Z')
			cp -= size - cs;

		cs = size;
		break;
	}

	if (pCh == '%') // client mid-point position in relation to monitor window area size
		cp = min_p + max_s * MAX(MIN(pos, 100), 0) / 100 - (cs) / 2;
	if (pCh == 'm' || pCh == 'M')
		cp = pos - cs / 2;

	if (!abs_p && cp < min_p)
		cp = min_p;
	if (cp + cs > min_p + max_s && !(abs_p && abs_s)) {
		if (abs_p || cp == min_p)
			cs = min_p + max_s - cp;
		else
			cp = min_p + max_s - cs;
	}

	*out_p = cp;
	*out_s = MAX(cs - 2*cbw, 1);
}

int
getrootptr(int *x, int *y)
{
	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, (unsigned char **)&p) != Success)
		return -1;
	if (n != 0)
		result = *p;
	XFree(p);
	return result;
}

unsigned int
getsystraywidth()
{
	unsigned int w = 0;
	Client *i;
	if(showsystray)
		for(i = systray->icons; i; w += i->w + systrayspacing, i = i->next) ;
	return w ? w + systrayspacing : 1;
}

int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
		return 0;
	if (name.encoding == XA_STRING)
		strncpy(text, (char *)name.value, size - 1);
	else {
		if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
			strncpy(text, *list, size - 1);
			XFreeStringList(list);
		}
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return 1;
}

void
grabbuttons(Client *c, int focused)
{
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		if (!focused)
			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
				BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
		for (i = 0; i < LENGTH(buttons); i++)
			if (buttons[i].click == ClkClientWin)
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabButton(dpy, buttons[i].button,
						buttons[i].mask | modifiers[j],
						c->win, False, BUTTONMASK,
						GrabModeAsync, GrabModeSync, None, None);
	}
}

void
grabkeys(void)
{
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		KeyCode code;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		for (i = 0; i < LENGTH(keys); i++)
			if ((code = XKeysymToKeycode(dpy, keys[i].keysym)))
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabKey(dpy, code, keys[i].mod | modifiers[j], root,
						True, GrabModeAsync, GrabModeAsync);
	}
}

void
incnmaster(const Arg *arg)
{
	selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag] = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
}

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */

void
keypress(XEvent *e)
{
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
}

void
killclient(const Arg *arg)
{
	if (!selmon->sel)
		return;
	if (!sendevent(selmon->sel->win, wmatom[WMDelete], NoEventMask, wmatom[WMDelete], CurrentTime, 0 , 0, 0)) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, selmon->sel->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

void
manage(Window w, XWindowAttributes *wa)
{
	Client *c, *t = NULL, *term = NULL;
	Window trans = None;
	XWindowChanges wc;
	int focusclient = 1;

	c = ecalloc(1, sizeof(Client));
	c->win = w;
	c->pid = winpid(w);
	/* geometry */
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->ignoresizehints = 0;
	c->oldbw = wa->border_width;
    c->cfact = 1.0;
    updatetitle(c);
	c->bw = borderpx;

	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		c->mon = t->mon;
		c->tags = t->tags;
	} else {
		c->mon = selmon;
		applyrules(c);
		term = termforwin(c);
	}

	if (c->x + WIDTH(c) > c->mon->mx + c->mon->mw)
		c->x = c->mon->mx + c->mon->mw - WIDTH(c);
	if (c->y + HEIGHT(c) > c->mon->my + c->mon->mh)
		c->y = c->mon->my + c->mon->mh - HEIGHT(c);
	c->x = MAX(c->x, c->mon->mx);
	/* only fix client y-offset, if the client center might cover the bar */
	c->y = MAX(c->y, ((c->mon->by == c->mon->my) && (c->x + (c->w / 2) >= c->mon->wx)
		&& (c->x + (c->w / 2) < c->mon->wx + c->mon->ww)) ? bh : c->mon->my);

	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
	configure(c); /* propagates border_width, if size doesn't change */
	updatewindowtype(c);
	updatesizehints(c);
	updatewmhints(c);
    c->x = c->mon->mx + (c->mon->mw - WIDTH(c)) / 2;
    c->y = c->mon->my + (c->mon->mh - HEIGHT(c)) / 2;
	c->sfx = c->x;
	c->sfy = c->y;
	c->sfw = c->w;
	c->sfh = c->h;
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0);
	if (!c->isfloating)
		c->isfloating = c->oldstate = trans != None || c->isfixed;
	if (c->isfloating)
		XRaiseWindow(dpy, c->win);

	/* Do not attach client if it is being swallowed */
	if (term && swallow(term, c)) {
		/* Do not let swallowed client steal focus unless the terminal has focus */
		focusclient = (term == selmon->sel);
	} else {
		attachBelow(c);

		if (focusclient || !c->mon->sel || !c->mon->stack)
			attachstack(c);
		else {
			c->snext = c->mon->sel->snext;
			c->mon->sel->snext = c;
		}
	}

	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
	setclientstate(c, NormalState);
	if (focusclient) {
		if (c->mon == selmon) {
			unfocus(selmon->sel, 0);
		    losefullscreen(c);
        }
		c->mon->sel = c;
	}
	arrange(c->mon);
	XMapWindow(dpy, c->win);
	if (focusclient)
		focus(NULL);
}

void
mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard)
		grabkeys();
}

void
maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;
	Client *i;
	if ((i = wintosystrayicon(ev->window))) {
		sendevent(i->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0, systray->win, XEMBED_EMBEDDED_VERSION);
		resizebarwin(selmon);
		updatesystray();
	}

	if (!XGetWindowAttributes(dpy, ev->window, &wa))
		return;
	if (wa.override_redirect)
		return;
	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
}

void
monocle(Monitor *m)
{
	unsigned int n = 0;
	Client *c;

	for (c = m->clients; c; c = c->next)
		if (ISVISIBLE(c))
			n++;
	if (n > 0) /* override layout symbol */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
	for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
		resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
}

void
motionnotify(XEvent *e)
{
    unsigned int i, x;
	static Monitor *mon = NULL;
	Monitor *m;
	XMotionEvent *ev = &e->xmotion;

  if (ev->window == selmon->barwin) {
		i = x = 0;
		do
			x += TEXTW(tags[i]);
		while (ev->x >= x && ++i < LENGTH(tags));
		if (i < LENGTH(tags)) {
                  if ((i + 1) != selmon->previewshow && !(selmon->tagset[selmon->seltags] & 1 << i)) {
				selmon->previewshow = i + 1;
				showtagpreview(i);
                  } else if (selmon->tagset[selmon->seltags] & 1 << i) {
				selmon->previewshow = 0;
				showtagpreview(0);
		  }
		} else if (selmon->previewshow != 0) {
			selmon->previewshow = 0;
			showtagpreview(0);
		}
	} else if (selmon->previewshow != 0) {
		selmon->previewshow = 0;
		showtagpreview(0);
   }

	if (ev->window != root)
		return;
	if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	mon = m;
}

void
moveorplace(const Arg *arg) {
	if ((!selmon->lt[selmon->sellt]->arrange || (selmon->sel && selmon->sel->isfloating)))
		movemouse(arg);
	else
		placemouse(arg);
}

void
movemouse(const Arg *arg)
{
	int x, y, ocx, ocy, nx, ny;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen && c->fakefullscreen != 1) /* no support moving fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
		return;
	if (!getrootptr(&x, &y))
		return;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);
			if (abs(selmon->wx - nx) < snap)
				nx = selmon->wx;
			else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
				nx = selmon->wx + selmon->ww - WIDTH(c);
			if (abs(selmon->wy - ny) < snap)
				ny = selmon->wy;
			else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
				ny = selmon->wy + selmon->wh - HEIGHT(c);
			if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
			&& (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
				togglefloating(NULL);
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

Client *
nexttagged(Client *c) {
	Client *walked = c->mon->clients;
	for(;
		walked && (walked->isfloating || !ISVISIBLEONTAG(walked, c->tags));
		walked = walked->next
	);
	return walked;
}

void
replaceclient(Client *old, Client *new)
{
	Client *c = NULL;
	Monitor *mon = old->mon;

	new->mon = mon;
	new->tags = old->tags;
	new->isfloating = old->isfloating;

	new->next = old->next;
	new->snext = old->snext;

	if (old == mon->clients)
		mon->clients = new;
	else {
		for (c = mon->clients; c && c->next != old; c = c->next);
		c->next = new;
	}

	if (old == mon->stack)
		mon->stack = new;
	else {
		for (c = mon->stack; c && c->snext != old; c = c->snext);
		c->snext = new;
	}

	old->next = NULL;
	old->snext = NULL;

	XMoveWindow(dpy, old->win, WIDTH(old) * -2, old->y);

	if (ISVISIBLE(new)) {
		if (new->isfloating)
			resize(new, old->x, old->y, new->w - 2*new->bw, new->h - 2*new->bw, 0);
		else
			resize(new, old->x, old->y, old->w - 2*new->bw, old->h - 2*new->bw, 0);
	}
}

Client *
nexttiled(Client *c)
{
	for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
}

void
placemouse(const Arg *arg)
{
	int x, y, px, py, ocx, ocy, nx = -9999, ny = -9999, freemove = 0;
	Client *c, *r = NULL, *at, *prevr;
	Monitor *m;
	XEvent ev;
	XWindowAttributes wa;
	Time lasttime = 0;
	int attachmode, prevattachmode;
	attachmode = prevattachmode = -1;

	if (!(c = selmon->sel) || !c->mon->lt[c->mon->sellt]->arrange) /* no support for placemouse when floating layout is used */
		return;
	if (c->isfullscreen) /* no support placing fullscreen windows by mouse */
		return;
	restack(selmon);
	prevr = c;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
		return;

	c->isfloating = 0;
	c->beingmoved = 1;

	XGetWindowAttributes(dpy, c->win, &wa);
	ocx = wa.x;
	ocy = wa.y;

	if (arg->i == 2) // warp cursor to client center
		XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, WIDTH(c) / 2, HEIGHT(c) / 2);

	if (!getrootptr(&x, &y))
		return;

	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch (ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);

			if (!freemove && (abs(nx - ocx) > snap || abs(ny - ocy) > snap))
				freemove = 1;

			if (freemove)
				XMoveWindow(dpy, c->win, nx, ny);

			if ((m = recttomon(ev.xmotion.x, ev.xmotion.y, 1, 1)) && m != selmon)
				selmon = m;

			if (arg->i == 1) { // tiled position is relative to the client window center point
				px = nx + wa.width / 2;
				py = ny + wa.height / 2;
			} else { // tiled position is relative to the mouse cursor
				px = ev.xmotion.x;
				py = ev.xmotion.y;
			}

			r = recttoclient(px, py, 1, 1);

			if (!r || r == c)
				break;

			attachmode = 0; // below
			if (((float)(r->y + r->h - py) / r->h) > ((float)(r->x + r->w - px) / r->w)) {
				if (abs(r->y - py) < r->h / 2)
					attachmode = 1; // above
			} else if (abs(r->x - px) < r->w / 2)
				attachmode = 1; // above

			if ((r && r != prevr) || (attachmode != prevattachmode)) {
				detachstack(c);
				detach(c);
				if (c->mon != r->mon) {
					arrangemon(c->mon);
					c->tags = r->mon->tagset[r->mon->seltags];
				}

				c->mon = r->mon;
				r->mon->sel = r;

				if (attachmode) {
					if (r == r->mon->clients)
						attach(c);
					else {
						for (at = r->mon->clients; at->next != r; at = at->next);
						c->next = at->next;
						at->next = c;
					}
				} else {
					c->next = r->next;
					r->next = c;
				}

				attachstack(c);
				arrangemon(r->mon);
				prevr = r;
				prevattachmode = attachmode;
			}
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);

	if ((m = recttomon(ev.xmotion.x, ev.xmotion.y, 1, 1)) && m != c->mon) {
		detach(c);
		detachstack(c);
		arrangemon(c->mon);
		c->mon = m;
		c->tags = m->tagset[m->seltags];
		attach(c);
		attachstack(c);
		selmon = m;
	}

	focus(c);
	c->beingmoved = 0;

	if (nx != -9999)
		resize(c, nx, ny, c->w, c->h, 0);
	arrangemon(c->mon);
}

void
propertynotify(XEvent *e)
{
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	if ((c = wintosystrayicon(ev->window))) {
		if (ev->atom == XA_WM_NORMAL_HINTS) {
			updatesizehints(c);
			updatesystrayicongeom(c, c->w, c->h);
		}
		else
			updatesystrayiconstate(c, ev);
		resizebarwin(selmon);
		updatesystray();
	}
	if ((ev->window == root) && (ev->atom == XA_WM_NAME))
		updatestatus();
	else if (ev->state == PropertyDelete)
		return; /* ignore */
	else if ((c = wintoclient(ev->window))) {
		switch(ev->atom) {
		default: break;
		case XA_WM_TRANSIENT_FOR:
			if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
				(c->isfloating = (wintoclient(trans)) != NULL))
				arrange(c->mon);
			break;
		case XA_WM_NORMAL_HINTS:
			updatesizehints(c);
			break;
		case XA_WM_HINTS:
			updatewmhints(c);
			drawbars();
			break;
		}
               if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName])
                       updatetitle(c);
		if (ev->atom == netatom[NetWMWindowType])
			updatewindowtype(c);
	}
}

void
pushstack(const Arg *arg) {
	int i = stackpos(arg);
	Client *sel = selmon->sel, *c, *p;

	if(i < 0)
		return;
	else if(i == 0) {
		detach(sel);
		attach(sel);
	}
	else {
		for(p = NULL, c = selmon->clients; c; p = c, c = c->next)
			if(!(i -= (ISVISIBLE(c) && c != sel)))
				break;
		c = c ? c : p;
		detach(sel);
		sel->next = c->next;
		c->next = sel;
	}
	arrange(selmon);
}

void
quit(const Arg *arg)
{
	running = 0;
}

Client *
recttoclient(int x, int y, int w, int h)
{
	Client *c, *r = NULL;
	int a, area = 0;

	for (c = nexttiled(selmon->clients); c; c = nexttiled(c->next)) {
		if ((a = INTERSECTC(x, y, w, h, c)) > area) {
			area = a;
			r = c;
		}
	}
	return r;
}

Monitor *
recttomon(int x, int y, int w, int h)
{
	Monitor *m, *r = selmon;
	int a, area = 0;

	for (m = mons; m; m = m->next)
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	return r;
}

void
removesystrayicon(Client *i)
{
	Client **ii;

	if (!showsystray || !i)
		return;
	for (ii = &systray->icons; *ii && *ii != i; ii = &(*ii)->next);
	if (ii)
		*ii = i->next;
	free(i);
}


void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	if (applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

void
resizebarwin(Monitor *m) {
	unsigned int w = m->ww;
	XMoveResizeWindow(dpy, m->barwin, m->wx + sp, m->by + vp, w - 2 * sp, bh);
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;
       unsigned int n;
       Client *nbc;
	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = wc.height = h;

	if (c->beingmoved)
		return;

	wc.border_width = c->bw;

       for (n = 0, nbc = nexttiled(selmon->clients); nbc; nbc = nexttiled(nbc->next), n++);

       if (c->isfloating || selmon->lt[selmon->sellt]->arrange == NULL) {
       } else {
               if (selmon->lt[selmon->sellt]->arrange == monocle || n == 1 || c->fakefullscreen == 1) {
                       wc.border_width = 0;
                       c->w = wc.width += c->bw * 2;
                       c->h = wc.height += c->bw * 2;
               }
       }

	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}

void
resizemouse(const Arg *arg)
{
	int opx, opy, ocx, ocy, och, ocw, nx, ny, nw, nh;
	Client *c;
	Monitor *m;
	XEvent ev;
	int horizcorner, vertcorner;
	unsigned int dui;
	Window dummy;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen && c->fakefullscreen != 1) /* no support resizing fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	och = c->h;
	ocw = c->w;
	if (!XQueryPointer(dpy, c->win, &dummy, &dummy, &opx, &opy, &nx, &ny, &dui))
		return;
	horizcorner = nx < c->w / 2;
	vertcorner  = ny < c->h / 2;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[horizcorner | (vertcorner << 1)]->cursor, CurrentTime) != GrabSuccess)
		return;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nx = horizcorner ? (ocx + ev.xmotion.x - opx) : c->x;
			ny = vertcorner ? (ocy + ev.xmotion.y - opy) : c->y;
			nw = MAX(horizcorner ? (ocx + ocw - nx) : (ocw + (ev.xmotion.x - opx)), 1);
			nh = MAX(vertcorner ? (ocy + och - ny) : (och + (ev.xmotion.y - opy)), 1);
			if (c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww
			&& c->mon->wy + nh >= selmon->wy && c->mon->wy + nh <= selmon->wy + selmon->wh)
			{
				if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
				&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
					togglefloating(NULL);
			}
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resizeclient(c, nx, ny, nw, nh);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

void
resizerequest(XEvent *e)
{
	XResizeRequestEvent *ev = &e->xresizerequest;
	Client *i;

	if ((i = wintosystrayicon(ev->window))) {
		updatesystrayicongeom(i, ev->width, ev->height);
		resizebarwin(selmon);
		updatesystray();
	}
}

void
restack(Monitor *m)
{
	Client *c;
	XEvent ev;
	XWindowChanges wc;

	drawbar(m);
	if (!m->sel)
		return;
	if (m->sel->isfloating || !m->lt[m->sellt]->arrange)
		XRaiseWindow(dpy, m->sel->win);
	if (m->lt[m->sellt]->arrange) {
		wc.stack_mode = Below;
		wc.sibling = m->barwin;
		for (c = m->stack; c; c = c->snext)
			if (!c->isfloating && ISVISIBLE(c)) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
	}
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
run(void)
{
	XEvent ev;
	/* main event loop */
	XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev))
		if (handler[ev.type])
			handler[ev.type](&ev); /* call handler */
}

void
runautostart(void)
{
	char *pathpfx;
	char *path;
	char *xdgdatahome;
	char *home;
	struct stat sb;

	if ((home = getenv("HOME")) == NULL)
		/* this is almost impossible */
		return;

	/* if $XDG_DATA_HOME is set and not empty, use $XDG_DATA_HOME/dwm,
	 * otherwise use ~/.local/share/dwm as autostart script directory
	 */
	xdgdatahome = getenv("XDG_DATA_HOME");
	if (xdgdatahome != NULL && *xdgdatahome != '\0') {
		/* space for path segments, separators and nul */
		pathpfx = ecalloc(1, strlen(xdgdatahome) + strlen(dwmdir) + 2);

		if (sprintf(pathpfx, "%s/%s", xdgdatahome, dwmdir) <= 0) {
			free(pathpfx);
			return;
		}
	} else {
		/* space for path segments, separators and nul */
		pathpfx = ecalloc(1, strlen(home) + strlen(localshare)
		                     + strlen(dwmdir) + 3);

		if (sprintf(pathpfx, "%s/%s/%s", home, localshare, dwmdir) < 0) {
			free(pathpfx);
			return;
		}
	}

	/* check if the autostart script directory exists */
	if (! (stat(pathpfx, &sb) == 0 && S_ISDIR(sb.st_mode))) {
		/* the XDG conformant path does not exist or is no directory
		 * so we try ~/.dwm instead
		 */
		char *pathpfx_new = realloc(pathpfx, strlen(home) + strlen(dwmdir) + 3);
		if(pathpfx_new == NULL) {
			free(pathpfx);
			return;
		}
   pathpfx = pathpfx_new;

		if (sprintf(pathpfx, "%s/.%s", home, dwmdir) <= 0) {
			free(pathpfx);
			return;
		}
	}

	/* try the blocking script first */
	path = ecalloc(1, strlen(pathpfx) + strlen(autostartblocksh) + 2);
	if (sprintf(path, "%s/%s", pathpfx, autostartblocksh) <= 0) {
		free(path);
		free(pathpfx);
	}

	if (access(path, X_OK) == 0)
		system(path);

	/* now the non-blocking script */
	if (sprintf(path, "%s/%s", pathpfx, autostartsh) <= 0) {
		free(path);
		free(pathpfx);
	}

	if (access(path, X_OK) == 0)
		system(strcat(path, " &"));

	free(pathpfx);
	free(path);
}

void
scan(void)
{
	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
				manage(wins[i], &wa);
		}
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
				manage(wins[i], &wa);
		}
		if (wins)
			XFree(wins);
	}
}

static void scratchpad_hide ()
{
	if (selmon -> sel)
	{
		selmon -> sel -> tags = SCRATCHPAD_MASK;
		selmon -> sel -> isfloating = 1;
		focus(NULL);
		arrange(selmon);
	}
}

static _Bool scratchpad_last_showed_is_killed (void)
{
	_Bool killed = 1;
	for (Client * c = selmon -> clients; c != NULL; c = c -> next)
	{
		if (c == scratchpad_last_showed)
		{
			killed = 0;
			break;
		}
	}
	return killed;
}

static void scratchpad_remove ()
{
	if (selmon -> sel && scratchpad_last_showed != NULL && selmon -> sel == scratchpad_last_showed)
		scratchpad_last_showed = NULL;
}

static void scratchpad_show ()
{
	if (scratchpad_last_showed == NULL || scratchpad_last_showed_is_killed ())
		scratchpad_show_first ();
	else
	{
		if (scratchpad_last_showed -> tags != SCRATCHPAD_MASK)
		{
			scratchpad_last_showed -> tags = SCRATCHPAD_MASK;
			focus(NULL);
			arrange(selmon);
		}
		else
		{
			_Bool found_current = 0;
			_Bool found_next = 0;
			for (Client * c = selmon -> clients; c != NULL; c = c -> next)
			{
				if (found_current == 0)
				{
					if (c == scratchpad_last_showed)
					{
						found_current = 1;
						continue;
					}
				}
				else
				{
					if (c -> tags == SCRATCHPAD_MASK)
					{
						found_next = 1;
						scratchpad_show_client (c);
						break;
					}
				}
			}
			if (found_next == 0) scratchpad_show_first ();
		}
	}
}

static void scratchpad_show_client (Client * c)
{
	scratchpad_last_showed = c;
	c -> tags = selmon->tagset[selmon->seltags];
	focus(c);
	arrange(selmon);
}

static void scratchpad_show_first (void)
{
	for (Client * c = selmon -> clients; c != NULL; c = c -> next)
	{
		if (c -> tags == SCRATCHPAD_MASK)
		{
			scratchpad_show_client (c);
			break;
		}
	}
}

void
sendmon(Client *c, Monitor *m)
{
	if (c->mon == m)
		return;
	unfocus(c, 1);
	detach(c);
	detachstack(c);
	c->mon = m;
	c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
	attachBelow(c);
	attachstack(c);
	focus(NULL);
	arrange(NULL);
}

void
setclientstate(Client *c, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char *)data, 2);
}
void
setcurrentdesktop(void){
	long data[] = { 0 };
	XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)data, 1);
}
void setdesktopnames(void){
	XTextProperty text;
	Xutf8TextListToTextProperty(dpy, tags, TAGSLENGTH, XUTF8StringStyle, &text);
	XSetTextProperty(dpy, root, &text, netatom[NetDesktopNames]);
}

int
sendevent(Window w, Atom proto, int mask, long d0, long d1, long d2, long d3, long d4)
{
	int n;
	Atom *protocols, mt;
	int exists = 0;
	XEvent ev;

	if (proto == wmatom[WMTakeFocus] || proto == wmatom[WMDelete]) {
		mt = wmatom[WMProtocols];
		if (XGetWMProtocols(dpy, w, &protocols, &n)) {
			while (!exists && n--)
				exists = protocols[n] == proto;
			XFree(protocols);
		}
	}
	else {
		exists = True;
		mt = proto;
	}
	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = w;
		ev.xclient.message_type = mt;
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = d0;
		ev.xclient.data.l[1] = d1;
		ev.xclient.data.l[2] = d2;
		ev.xclient.data.l[3] = d3;
		ev.xclient.data.l[4] = d4;
		XSendEvent(dpy, w, False, mask, &ev);
	}
	return exists;
}

void
setfloatpos(Client *c, const char *floatpos)
{
	char xCh, yCh, wCh, hCh;
	int x, y, w, h, wx, ww, wy, wh;

	if (!c || !floatpos)
		return;
	if (selmon->lt[selmon->sellt]->arrange && !c->isfloating)
		return;
	switch(sscanf(floatpos, "%d%c %d%c %d%c %d%c", &x, &xCh, &y, &yCh, &w, &wCh, &h, &hCh)) {
		case 4:
			if (xCh == 'w' || xCh == 'W') {
				w = x; wCh = xCh;
				h = y; hCh = yCh;
				x = -1; xCh = 'C';
				y = -1; yCh = 'C';
			} else if (xCh == 'p' || xCh == 'P') {
				w = x; wCh = xCh;
				h = y; hCh = yCh;
				x = 0; xCh = 'G';
				y = 0; yCh = 'G';
			} else if (xCh == 'm' || xCh == 'M') {
				getrootptr(&x, &y);
			} else {
				w = 0; wCh = 0;
				h = 0; hCh = 0;
			}
			break;
		case 8:
			if (xCh == 'm' || xCh == 'M')
				getrootptr(&x, &y);
			break;
		default:
			return;
	}

	wx = c->mon->wx;
	wy = c->mon->wy;
	ww = c->mon->ww;
	wh = c->mon->wh;
	c->ignoresizehints = 1;

	getfloatpos(x, xCh, w, wCh, wx, ww, c->x, c->w, c->bw, floatposgrid_x, &c->x, &c->w);
	getfloatpos(y, yCh, h, hCh, wy, wh, c->y, c->h, c->bw, floatposgrid_y, &c->y, &c->h);
}

void
setnumdesktops(void){
	long data[] = { TAGSLENGTH };
	XChangeProperty(dpy, root, netatom[NetNumberOfDesktops], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)data, 1);
}

void
setfocus(Client *c)
{
	if (!c->neverfocus) {
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, netatom[NetActiveWindow],
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *) &(c->win), 1);
	}
	sendevent(c->win, wmatom[WMTakeFocus], NoEventMask, wmatom[WMTakeFocus], CurrentTime, 0, 0, 0);
}

 void
 setfullscreen(Client *c, int fullscreen)
 {
	XEvent ev;
	int savestate = 0, restorestate = 0, restorefakefullscreen = 0;

	if ((c->fakefullscreen == 0 && fullscreen && !c->isfullscreen) // normal fullscreen
			|| (c->fakefullscreen == 2 && fullscreen)) // fake fullscreen --> actual fullscreen
		savestate = 1; // go actual fullscreen
	else if ((c->fakefullscreen == 0 && !fullscreen && c->isfullscreen) // normal fullscreen exit
			|| (c->fakefullscreen >= 2 && !fullscreen)) // fullscreen exit --> fake fullscreen
		restorestate = 1; // go back into tiled

	/* If leaving fullscreen and the window was previously fake fullscreen (2), then restore
	 * that while staying in fullscreen. The exception to this is if we are in said state, but
	 * the client itself disables fullscreen (3) then we let the client go out of fullscreen
	 * while keeping fake fullscreen enabled (as otherwise there will be a mismatch between the
	 * client and the window manager's perception of the client's fullscreen state). */
	if (c->fakefullscreen == 2 && !fullscreen && c->isfullscreen) {
		restorefakefullscreen = 1;
		c->isfullscreen = 1;
		fullscreen = 1;
	}

	if (fullscreen != c->isfullscreen) { // only send property change if necessary
		if (fullscreen)
			XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
				PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		else
			XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
				PropModeReplace, (unsigned char*)0, 0);
	}

	c->isfullscreen = fullscreen;

	/* Some clients, e.g. firefox, will send a client message informing the window manager
	 * that it is going into fullscreen after receiving the above signal. This has the side
	 * effect of this function (setfullscreen) sometimes being called twice when toggling
	 * fullscreen on and off via the window manager as opposed to the application itself.
	 * To protect against obscure issues where the client settings are stored or restored
	 * when they are not supposed to we add an additional bit-lock on the old state so that
	 * settings can only be stored and restored in that precise order. */
	if (savestate && !(c->oldstate & (1 << 1))) {
		c->oldbw = c->bw;
		c->oldstate = c->isfloating | (1 << 1);
		c->bw = 0;
		c->isfloating = 1;
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		XRaiseWindow(dpy, c->win);
	} else if (restorestate && (c->oldstate & (1 << 1))) {
		c->bw = c->oldbw;
		c->isfloating = c->oldstate = c->oldstate & 1;
		if (restorefakefullscreen || c->fakefullscreen == 3)
			c->fakefullscreen = 1;
		/* The client may have been moved to another monitor whilst in fullscreen which if tiled
		 * we address by doing a full arrange of tiled clients. If the client is floating then the
		 * height and width may be larger than the monitor's window area, so we cap that by
		 * ensuring max / min values. */
		if (c->isfloating) {
			c->x = MAX(c->mon->wx, c->oldx);
			c->y = MAX(c->mon->wy, c->oldy);
			c->w = MIN(c->mon->ww - c->x - 2*c->bw, c->oldw);
			c->h = MIN(c->mon->wh - c->y - 2*c->bw, c->oldh);
			resizeclient(c, c->x, c->y, c->w, c->h);
			restack(c->mon);
		} else
			arrange(c->mon);
	} else
		resizeclient(c, c->x, c->y, c->w, c->h);

	/* Exception: if the client was in actual fullscreen and we exit out to fake fullscreen
	 * mode, then the focus would sometimes drift to whichever window is under the mouse cursor
	 * at the time. To avoid this we ask X for all EnterNotify events and just ignore them.
	 */
	if (!c->isfullscreen)
		while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
 }

void
setlayout(const Arg *arg)
{
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag] ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt] = (Layout *)arg->v;
	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol);
	if (selmon->sel)
		arrange(selmon);
	else
		drawbar(selmon);
}

void
setcfact(const Arg *arg)
{
	float f;
	Client *c;

	c = selmon->sel;

	if (!arg || !c || !selmon->lt[selmon->sellt]->arrange)
		return;
	if (!arg->f)
		f = 1.0;
	else if (arg->f > 4.0) // set fact absolutely
		f = arg->f - 4.0;
	else
		f = arg->f + c->cfact;
	if (f < 0.25)
		f = 0.25;
	else if (f > 4.0)
		f = 4.0;
 	c->cfact = f;
 	arrange(selmon);
 }

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
	if (f < 0.05 || f > 0.95)
		return;
	selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag] = f;
	arrange(selmon);
}

void
setup(void)
{
	int i;
	XSetWindowAttributes wa;
	Atom utf8string;

	/* clean up any zombies immediately */
	sigchld(0);

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	xinitvisual();
	drw = drw_create(dpy, screen, root, sw, sh, visual, depth, cmap);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;
	bh = user_bh ? user_bh : drw->fonts->h + 2;
	updategeom();
	sp = sidepad;
	vp = (topbar == 1) ? vertpad : - vertpad;

	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetSystemTray] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_S0", False);
	netatom[NetSystemTrayOP] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
	netatom[NetSystemTrayOrientation] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
	netatom[NetSystemTrayOrientationHorz] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION_HORZ", False);
	netatom[NetSystemTrayVisual] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_VISUAL", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDock] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	netatom[NetDesktopViewport] = XInternAtom(dpy, "_NET_DESKTOP_VIEWPORT", False);
	netatom[NetNumberOfDesktops] = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	netatom[NetCurrentDesktop] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	netatom[NetDesktopNames] = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);
	xatom[Manager] = XInternAtom(dpy, "MANAGER", False);
	xatom[Xembed] = XInternAtom(dpy, "_XEMBED", False);
	xatom[XembedInfo] = XInternAtom(dpy, "_XEMBED_INFO", False);
	/* init cursors */
	cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
	cursor[CurResizeBR] = drw_cur_create(drw, XC_bottom_right_corner);
	cursor[CurResizeBL] = drw_cur_create(drw, XC_bottom_left_corner);
	cursor[CurResizeTR] = drw_cur_create(drw, XC_top_right_corner);
	cursor[CurResizeTL] = drw_cur_create(drw, XC_top_left_corner);
	cursor[CurMove] = drw_cur_create(drw, XC_fleur);
	cursor[CurResizeHorzArrow] = drw_cur_create(drw, XC_sb_h_double_arrow);
	cursor[CurResizeVertArrow] = drw_cur_create(drw, XC_sb_v_double_arrow);
	/* init appearance */
	scheme = ecalloc(LENGTH(colors) + 1, sizeof(Clr *));
	scheme[LENGTH(colors)] = drw_scm_create(drw, colors[0], alphas[0], 3);
	for (i = 0; i < LENGTH(colors); i++)
		scheme[i] = drw_scm_create(drw, colors[i], alphas[i], 3);
	/* init system tray */
	updatesystray();
	/* init bars */
	updatebars();
	updatestatus();
	updatebarpos(selmon);
    updatepreview();
	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "dwm", 3);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	setnumdesktops();
	setcurrentdesktop();
	setdesktopnames();
	setviewport();
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	/* select events */
	wa.cursor = cursor[CurNormal]->cursor;
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
	focus(NULL);
}
void
setviewport(void){
	long data[] = { 0, 0 };
	XChangeProperty(dpy, root, netatom[NetDesktopViewport], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)data, 2);
}


void
seturgent(Client *c, int urg)
{
	XWMHints *wmh;

	c->isurgent = urg;
	if (!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

void
showhide(Client *c)
{
	if (!c)
		return;
	if (ISVISIBLE(c)) {
		/* show clients top down */
		XMoveWindow(dpy, c->win, c->x, c->y);
		if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating) && !c->isfullscreen)
			resize(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext);
	} else {
		/* hide clients bottom up */
		showhide(c->snext);
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
	}
}

void
showtagpreview(int tag)
{
	if (!selmon->previewshow) {
		XUnmapWindow(dpy, selmon->tagwin);
		return;
	}

        if (selmon->tagmap[tag]) {
		XSetWindowBackgroundPixmap(dpy, selmon->tagwin, selmon->tagmap[tag]);
		XCopyArea(dpy, selmon->tagmap[tag], selmon->tagwin, drw->gc, 0, 0, selmon->mw / scalepreview, selmon->mh / scalepreview, 0, 0);
		XSync(dpy, False);
		XMapWindow(dpy, selmon->tagwin);
	} else
		XUnmapWindow(dpy, selmon->tagwin);
}

void
sigchld(int unused)
{
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		die("can't install SIGCHLD handler:");
	while (0 < waitpid(-1, NULL, WNOHANG));
}

#ifdef NOSIGNAL
#else
void
sigstatusbar(const Arg *arg)
{
	union sigval sv;

	if (!statussig)
		return;
	sv.sival_int = arg->i;
	if ((statuspid = getstatusbarpid()) <= 0)
		return;

	sigqueue(statuspid, SIGRTMIN+statussig, sv);
}
#endif

void
spawn(const Arg *arg)
{
	spawncmd(arg);
}

pid_t
spawncmd(const Arg *arg)
{
	pid_t pid;
	if (arg->v == dmenucmd)
		dmenumon[0] = '0' + selmon->num;
	if ((pid = fork()) == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		if (arg->v == statuscmd) {
			for (int i = 0; i < LENGTH(statuscmds); i++) {
				if (statuscmdn == statuscmds[i].id) {
					statuscmd[2] = statuscmds[i].cmd;
					setenv("BUTTON", lastbutton, 1);
					break;
				}
			}
			if (!statuscmd[2])
				exit(EXIT_SUCCESS);
		}
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(EXIT_SUCCESS);
	}
}

void switchtag(void) {
  	int i;
	unsigned int occ = 0;
	Client *c;
        Imlib_Image image;

	for (c = selmon->clients; c; c = c->next)
		occ |= c->tags;
	for (i = 0; i < LENGTH(tags); i++) {
		if (selmon->tagset[selmon->seltags] & 1 << i) {
                  	if (selmon->tagmap[i] != 0) {
 				XFreePixmap(dpy, selmon->tagmap[i]);
 				selmon->tagmap[i] = 0;
 			}
			if (occ & 1 << i) {
                          	image = imlib_create_image(sw, sh);
				imlib_context_set_image(image);
				imlib_context_set_display(dpy);
				imlib_context_set_visual(visual);
				imlib_context_set_drawable(RootWindow(dpy, screen));
				imlib_copy_drawable_to_image(0, selmon->mx, selmon->my, selmon->mw ,selmon->mh, 0, 0, 1);
                                selmon->tagmap[i] = XCreatePixmap(dpy, selmon->tagwin, selmon->mw / scalepreview, selmon->mh / scalepreview, depth);
				imlib_context_set_drawable(selmon->tagmap[i]);
				imlib_render_image_part_on_drawable_at_size(0, 0, selmon->mw, selmon->mh, 0, 0, selmon->mw / scalepreview, selmon->mh / scalepreview);
				imlib_free_image();
			}                         
		}
	}
}

int
stackpos(const Arg *arg) {
	int n, i;
	Client *c, *l;

	if(!selmon->clients)
		return -1;

	if(arg->i == PREVSEL) {
		for(l = selmon->stack; l && (!ISVISIBLE(l) || l == selmon->sel); l = l->snext);
		if(!l)
			return -1;
		for(i = 0, c = selmon->clients; c != l; i += ISVISIBLE(c) ? 1 : 0, c = c->next);
		return i;
	}
	else if(ISINC(arg->i)) {
		if(!selmon->sel)
			return -1;
		for(i = 0, c = selmon->clients; c != selmon->sel; i += ISVISIBLE(c) ? 1 : 0, c = c->next);
		for(n = i; c; n += ISVISIBLE(c) ? 1 : 0, c = c->next);
		return MOD(i + GETINC(arg->i), n);
	}
	else if(arg->i < 0) {
		for(i = 0, c = selmon->clients; c; i += ISVISIBLE(c) ? 1 : 0, c = c->next);
		return MAX(i + arg->i, 0);
	}
	else
		return arg->i;
}

int
swallow(Client *t, Client *c)
{
	if (c->noswallow || c->isterminal)
		return 0;
	if (!swallowfloating && c->isfloating)
		return 0;

	replaceclient(t, c);
	c->ignorecfgreqpos = 1;
	c->swallowing = t;

	return 1;
}

void
unswallow(Client *c)
{
	replaceclient(c, c->swallowing);
	c->swallowing = NULL;
}

void
tag(const Arg *arg)
{
	if (selmon->sel && arg->ui & TAGMASK) {
		selmon->sel->tags = arg->ui & TAGMASK;
		focus(NULL);
		arrange(selmon);
	}
	updatecurrentdesktop();
}

void
tagmon(const Arg *arg)
{
	Client *c = selmon->sel;
	if (!c || !mons->next)
		return;
	if (c->isfullscreen) {
		c->isfullscreen = 0;
		sendmon(c, dirtomon(arg->i));
		c->isfullscreen = 1;
		if (c->fakefullscreen != 1) {
			resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
			XRaiseWindow(dpy, c->win);
		}
	} else
		sendmon(c, dirtomon(arg->i));
}

void
togglebar(const Arg *arg)
{
	selmon->showbar = selmon->pertag->showbars[selmon->pertag->curtag] = !selmon->showbar;
	updatebarpos(selmon);
	resizebarwin(selmon);
	if (showsystray) {
		XWindowChanges wc;
		if (!selmon->showbar)
			wc.y = -bh;
		else if (selmon->showbar) {
			wc.y = vp;
			if (!selmon->topbar)
				wc.y = selmon->mh - bh - vp;
		}
		XConfigureWindow(dpy, systray->win, CWY, &wc);
	}
	arrange(selmon);
}

#include "vanitygaps.c"

void
togglefakefullscreen(const Arg *arg)
{
	Client *c = selmon->sel;
	if (!c)
		return;

	if (c->fakefullscreen != 1 && c->isfullscreen) { // exit fullscreen --> fake fullscreen
		c->fakefullscreen = 2;
		setfullscreen(c, 0);
	} else if (c->fakefullscreen == 1) {
		setfullscreen(c, 0);
		c->fakefullscreen = 0;
	} else {
		c->fakefullscreen = 1;
		setfullscreen(c, 1);
	}
}

void
togglefloating(const Arg *arg)
{
	Client *c = selmon->sel;
	if (!c)
		return;
	if (c->isfullscreen && c->fakefullscreen != 1) /* no support for fullscreen windows */
		return;
	c->isfloating = !c->isfloating || c->isfixed;
	if (selmon->sel->isfloating)
		/* restore last known float dimensions */
		resize(c, c->sfx, c->sfx, c->sfw, c->sfh, False);
	else {
		/* save last known float dimensions */
		c->sfx = c->x;
		c->sfy = c->y;
		c->sfw = c->w;
		c->sfh = c->h;
	}

	if (!c->swallowing) {
		if (riopid && (!riodraw_matchpid || isdescprocess(riopid, c->pid))) {
			if (riodimensions[3] != -1)
				rioposition(c, riodimensions[0], riodimensions[1], riodimensions[2], riodimensions[3]);
			else {
				killclient(&((Arg) { .v = c }));
				return;
			}
		}
	}

	arrange(c->mon);
}

void
togglefullscreen(const Arg *arg)
{
	Client *c = selmon->sel;
	if (!c)
		return;

	if (c->fakefullscreen == 1) { // fake fullscreen --> fullscreen
		c->fakefullscreen = 2;
		setfullscreen(c, 1);
	} else
		setfullscreen(c, !c->isfullscreen);
}

void
toggletag(const Arg *arg)
{
	unsigned int newtags;

	/* Note that this patch is made for dwm 6.2. If applying this on top of the latest master
	 * then the https://dwm.suckless.org/patches/alwaysfullscreen/ patch has been merged
	 * upstream, which prevents focus to drift from windows that are in fake fullscreen.
	 * To address this issue use this if statement instead:
	 *   if (!selmon->sel || (selmon->sel->isfullscreen && selmon->sel->fakefullscreen != 1))
	 */
	if (!selmon->sel)
		return;
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) {
		selmon->sel->tags = newtags;
		focus(NULL);
		arrange(selmon);
	}
	updatecurrentdesktop();
}

void
toggleview(const Arg *arg)
{
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);
	int i;

	if (newtagset) {
        switchtag();
		selmon->tagset[selmon->seltags] = newtagset;

		if (newtagset == ~0) {
			selmon->pertag->prevtag = selmon->pertag->curtag;
			selmon->pertag->curtag = 0;
		}

		/* test if the user did not select the same tag */
		if (!(newtagset & 1 << (selmon->pertag->curtag - 1))) {
			selmon->pertag->prevtag = selmon->pertag->curtag;
			for (i = 0; !(newtagset & 1 << i); i++) ;
			selmon->pertag->curtag = i + 1;
		}

		/* apply settings for this view */
        selmon->gappiv = selmon->pertag->gappivs[selmon->pertag->curtag];
        selmon->gappih = selmon->pertag->gappihs[selmon->pertag->curtag];
        selmon->gappov = selmon->pertag->gappovs[selmon->pertag->curtag];
        selmon->gappoh = selmon->pertag->gappohs[selmon->pertag->curtag];
		selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
		selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag];
		selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
		selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
		selmon->lt[selmon->sellt^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt^1];
		if (selmon->showbar != selmon->pertag->showbars[selmon->pertag->curtag])
			togglebar(NULL);

		focus(NULL);
		arrange(selmon);
	}
}

void
unfocus(Client *c, int setfocus)
{
	if (!c)
		return;
	grabbuttons(c, 0);
	XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void
unmanage(Client *c, int destroyed)
{
	Client *s;
	Monitor *m = c->mon;
	XWindowChanges wc;

	if (c->swallowing)
		unswallow(c);

	s = swallowingclient(c->win);
	if (s)
		s->swallowing = NULL;

	detach(c);
	detachstack(c);
	if (!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	if (scratchpad_last_showed == c)
		scratchpad_last_showed = NULL;
	free(c);
	focus(NULL);
	updateclientlist();
	arrange(m);
}

void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	if ((c = wintoclient(ev->window))) {
		if (ev->send_event)
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, 0);
	}
	else if ((c = wintosystrayicon(ev->window))) {
		/* KLUDGE! sometimes icons occasionally unmap their windows, but do
		 * _not_ destroy them. We map those windows back */
		XMapRaised(dpy, c->win);
		updatesystray();
	}
}

void
updatebars(void)
{
	unsigned int w;
	Monitor *m;
	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixel = 0,
		.border_pixel = 0,
		.colormap = cmap,
		.event_mask = ButtonPressMask|ExposureMask|PointerMotionMask
	};
	XClassHint ch = {"dwm", "dwm"};
	for (m = mons; m; m = m->next) {
		if (m->barwin)
			continue;
		w = m->ww;
		if (showsystray && m == systraytomon(m))
			w -= getsystraywidth();
		m->barwin = XCreateWindow(dpy, root, m->wx + sp, m->by + vp, w - 2 * sp, bh, 0, depth,
		                          InputOutput, visual,
		                          CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &wa);
		XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
		if (showsystray && m == systraytomon(m))
			XMapRaised(dpy, systray->win);
		XMapRaised(dpy, m->barwin);
		XSetClassHint(dpy, m->barwin, &ch);
	}
}

void
updatepreview(void)
{
	Monitor *m;

	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixel = 0,
		.border_pixel = 0,
		.colormap = cmap,
		.event_mask = ButtonPressMask|ExposureMask
	};
	for (m = mons; m; m = m->next) {
		m->tagwin = XCreateWindow(dpy, root, m->wx, m->by + bh, m->mw / 4, m->mh / 4, 0, depth,
				                  InputOutput, visual,
		                          CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &wa);
		XDefineCursor(dpy, m->tagwin, cursor[CurNormal]->cursor);
		XMapRaised(dpy, m->tagwin);
		XUnmapWindow(dpy, m->tagwin);
	}
}

void
updatebarpos(Monitor *m)
{
	m->wy = m->my;
	m->wh = m->mh;
	if (m->showbar) {
		m->wh = m->wh - vertpad - bh;
		m->by = m->topbar ? m->wy : m->wy + m->wh + vertpad;
		m->wy = m->topbar ? m->wy + bh + vp : m->wy;
	} else
		m->by = -bh - vp;
}

void
losefullscreen(Client *next)
{
	Client *sel = selmon->sel;
	if (!sel || !next)
		return;
	if (sel->isfullscreen && sel->fakefullscreen != 1 && ISVISIBLE(sel) && sel->mon == next->mon && !next->isfloating)
		setfullscreen(sel, 0);
}

void
updateclientlist()
{
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
				XA_WINDOW, 32, PropModeAppend,
				(unsigned char *) &(c->win), 1);
}
void updatecurrentdesktop(void){
	long rawdata[] = { selmon->tagset[selmon->seltags] };
	int i=0;
	while(*rawdata >> (i+1)){
		i++;
	}
	long data[] = { i };
	XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)data, 1);
}

int
updategeom(void)
{
	int dirty = 0;

#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for (n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;
		if (n <= nn) { /* new monitors available */
			for (i = 0; i < (nn - n); i++) {
				for (m = mons; m && m->next; m = m->next);
				if (m)
					m->next = createmon();
				else
					mons = createmon();
			}
			for (i = 0, m = mons; i < nn && m; m = m->next, i++)
				if (i >= n
				|| unique[i].x_org != m->mx || unique[i].y_org != m->my
				|| unique[i].width != m->mw || unique[i].height != m->mh)
				{
					dirty = 1;
					m->num = i;
					m->mx = m->wx = unique[i].x_org;
					m->my = m->wy = unique[i].y_org;
					m->mw = m->ww = unique[i].width;
					m->mh = m->wh = unique[i].height;
					updatebarpos(m);
				}
		} else { /* less monitors available nn < n */
			for (i = nn; i < n; i++) {
				for (m = mons; m && m->next; m = m->next);
				while ((c = m->clients)) {
					dirty = 1;
					m->clients = c->next;
					detachstack(c);
					c->mon = mons;
					attach(c);
					attachBelow(c);
					attachstack(c);
				}
				if (m == selmon)
					selmon = mons;
				cleanupmon(m);
			}
		}
		free(unique);
	} else
#endif /* XINERAMA */
	{ /* default monitor setup */
		if (!mons)
			mons = createmon();
		if (mons->mw != sw || mons->mh != sh) {
			dirty = 1;
			mons->mw = mons->ww = sw;
			mons->mh = mons->wh = sh;
			updatebarpos(mons);
		}
	}
	if (dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}
	return dirty;
}

void
updatenumlockmask(void)
{
	unsigned int i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy);
	for (i = 0; i < 8; i++)
		for (j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
				== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	if (size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	} else if (size.flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	} else
		c->basew = c->baseh = 0;
	if (size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	} else
		c->incw = c->inch = 0;
	if (size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	} else
		c->maxw = c->maxh = 0;
	if (size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	} else if (size.flags & PBaseSize) {
		c->minw = size.base_width;
		c->minh = size.base_height;
	} else
		c->minw = c->minh = 0;
	if (size.flags & PAspect) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	} else
		c->maxa = c->mina = 0.0;
	c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
}

int
riodraw(Client *c, const char slopstyle[])
{
	int i;
	char str[100];
	char strout[100];
	char tmpstring[30] = {0};
	char slopcmd[100] = "slop -f x%xx%yx%wx%hx ";
	int firstchar = 0;
	int counter = 0;

	strcat(slopcmd, slopstyle);
	FILE *fp = popen(slopcmd, "r");

	while (fgets(str, 100, fp) != NULL)
		strcat(strout, str);

	pclose(fp);

	if (strlen(strout) < 6)
		return 0;

	for (i = 0; i < strlen(strout); i++){
		if (!firstchar) {
			if (strout[i] == 'x')
				firstchar = 1;
			continue;
		}

		if (strout[i] != 'x')
			tmpstring[strlen(tmpstring)] = strout[i];
		else {
			riodimensions[counter] = atoi(tmpstring);
			counter++;
			memset(tmpstring,0,strlen(tmpstring));
		}
	}

	if (riodimensions[0] <= -40 || riodimensions[1] <= -40 || riodimensions[2] <= 50 || riodimensions[3] <= 50) {
		riodimensions[3] = -1;
		return 0;
	}

	if (c) {
		rioposition(c, riodimensions[0], riodimensions[1], riodimensions[2], riodimensions[3]);
		return 0;
	}

	return 1;
}

void
rioposition(Client *c, int x, int y, int w, int h)
{
	Monitor *m;
	if ((m = recttomon(x, y, w, h)) && m != c->mon) {
		detach(c);
		detachstack(c);
		arrange(c->mon);
		c->mon = m;
		c->tags = m->tagset[m->seltags];
		attach(c);
		attachstack(c);
		selmon = m;
		focus(c);
	}

	c->isfloating = 1;
	if (riodraw_borders)
		resizeclient(c, x, y, w - (c->bw * 2), h - (c->bw * 2));
	else
		resizeclient(c, x - c->bw, y - c->bw, w, h);
	drawbar(c->mon);
	arrange(c->mon);

	riodimensions[3] = -1;
	riopid = 0;
}

/* drag out an area using slop and resize the selected window to it */
void
rioresize(const Arg *arg)
{
	Client *c = (arg && arg->v ? (Client*)arg->v : selmon->sel);
	if (c)
		riodraw(c, slopresizestyle);
}

/* spawn a new window and drag out an area using slop to postiion it */
void
riospawn(const Arg *arg)
{
	if (riodraw_spawnasync) {
		riopid = spawncmd(arg);
		riodraw(NULL, slopspawnstyle);
	} else if (riodraw(NULL, slopspawnstyle))
		riopid = spawncmd(arg);
}

void
updatestatus(void)
{
	if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext))) {
		strcpy(stext, "dwm-"VERSION);
    #ifdef NOSIGNAL
        statusw = TEXTW(stext) - lrpad + 2;
    #endif
	} else {
#ifdef NOSIGNAL
		char *text, *s, ch;
		statusw = 0;
		for (text = s = stext; *s; s++) {
			if ((unsigned char)(*s) < ' ') {
				ch = *s;
				*s = '\0';
				statusw += TEXTW(text) - lrpad;
				*s = ch;
				text = s + 1;
			}
		}
		statusw += TEXTW(text) - lrpad + 2;
#endif
	}
	drawbar(selmon);
	updatesystray();
}

void
updatesystrayicongeom(Client *i, int w, int h)
{
	if (i) {
        if (bh >= 30)
		    i->h = bh - 2; /* Fixing the icon disappearing when setting the bar height */
        else
            i->h = bh;
		if (w == h)
			i->w = bh;
		else if (h == bh)
			i->w = w;
		else
			i->w = (int) ((float)bh * ((float)w / (float)h));
		applysizehints(i, &(i->x), &(i->y), &(i->w), &(i->h), False);
		/* force icons into the systray dimensions if they don't want to */
		if (i->h > bh) {
			if (i->w == i->h)
				i->w = bh;
			else
				i->w = (int) ((float)bh * ((float)i->w / (float)i->h));
        if (bh >= 30)
		    i->h = bh - 2; /* Fixing the icon disappearing when setting the bar height */
        else
            i->h = bh;
		}
	}
}

void
updatesystrayiconstate(Client *i, XPropertyEvent *ev)
{
	long flags;
	int code = 0;

	if (!showsystray || !i || ev->atom != xatom[XembedInfo] ||
			!(flags = getatomprop(i, xatom[XembedInfo])))
		return;

	if (flags & XEMBED_MAPPED && !i->tags) {
		i->tags = 1;
		code = XEMBED_WINDOW_ACTIVATE;
		XMapRaised(dpy, i->win);
		setclientstate(i, NormalState);
	}
	else if (!(flags & XEMBED_MAPPED) && i->tags) {
		i->tags = 0;
		code = XEMBED_WINDOW_DEACTIVATE;
		XUnmapWindow(dpy, i->win);
		setclientstate(i, WithdrawnState);
	}
	else
		return;
	sendevent(i->win, xatom[Xembed], StructureNotifyMask, CurrentTime, code, 0,
			systray->win, XEMBED_EMBEDDED_VERSION);
}

void
updatesystray(void)
{
	XSetWindowAttributes wa;
	XWindowChanges wc;
	Client *i;
	Monitor *m = systraytomon(NULL);
	unsigned int x = m->mx + m->mw - sp;
    unsigned int sw = statusw - lrpad + systrayspacing;
    unsigned int y = m->by + vp;
	unsigned int w = 1;

	if (!showsystray)
		return;
	if (systrayonleft)
		x -= sw + sp;
	if (!systray) {
		/* init systray */
		if (!(systray = (Systray *)calloc(1, sizeof(Systray))))
			die("fatal: could not malloc() %u bytes\n", sizeof(Systray));
		wa.event_mask        = ButtonPressMask | ExposureMask;
		wa.override_redirect = True;
		wa.background_pixel = 0;
        wa.border_pixel = 0;
		wa.colormap = cmap;
		systray->win = XCreateWindow(dpy, root, x, y, w, bh, 0, depth,
                        InputOutput, visual,
						CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &wa);
		XSelectInput(dpy, systray->win, SubstructureNotifyMask);
		XChangeProperty(dpy, systray->win, netatom[NetSystemTrayOrientation], XA_CARDINAL, 32,
				PropModeReplace, (unsigned char *)&netatom[NetSystemTrayOrientationHorz], 1);
		XChangeProperty(dpy, systray->win, netatom[NetSystemTrayVisual], XA_VISUALID, 32,
				PropModeReplace, (unsigned char *)&visual->visualid, 1);
		XChangeProperty(dpy, systray->win, netatom[NetWMWindowType], XA_ATOM, 32,
				PropModeReplace, (unsigned char *)&netatom[NetWMWindowTypeDock], 1);
		XMapRaised(dpy, systray->win);
		XSetSelectionOwner(dpy, netatom[NetSystemTray], systray->win, CurrentTime);
		if (XGetSelectionOwner(dpy, netatom[NetSystemTray]) == systray->win) {
			sendevent(root, xatom[Manager], StructureNotifyMask, CurrentTime, netatom[NetSystemTray], systray->win, 0, 0);
			XSync(dpy, False);
		}
		else {
			fprintf(stderr, "dwm: unable to obtain system tray.\n");
			free(systray);
			systray = NULL;
			return;
		}
	}
	for (w = 0, i = systray->icons; i; i = i->next) {
		/* make sure the background color stays the same */
		wa.background_pixel  = 0;
		XChangeWindowAttributes(dpy, i->win, CWBackPixel, &wa);
		XMapRaised(dpy, i->win);
		w += systrayspacing;
		i->x = w;
		XMoveResizeWindow(dpy, i->win, i->x, 0, i->w, i->h);
		w += i->w;
		if (i->mon != m)
			i->mon = m;
	}
	w = w ? w + systrayspacing : 1;
	x -= w;
	XMoveResizeWindow(dpy, systray->win, x, y, w, bh);
	wc.x = x; wc.y = y; wc.width = w; wc.height = bh;
	wc.stack_mode = Above; wc.sibling = m->barwin;
	XConfigureWindow(dpy, systray->win, CWX|CWY|CWWidth|CWHeight|CWSibling|CWStackMode, &wc);
	XMapWindow(dpy, systray->win);
	XMapSubwindows(dpy, systray->win);
	XSync(dpy, False);
    drawbar(m);
}

void
updatetitle(Client *c)
{
       if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
               gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
       if (c->name[0] == '\0') /* hack to mark broken clients */
               strcpy(c->name, broken);
}


void
updatewindowtype(Client *c)
{
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen])
		setfullscreen(c, 1);
	if (wtype == netatom[NetWMWindowTypeDialog])
		c->isfloating = 1;
}

void
updatewmhints(Client *c)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		} else
			c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
		if (wmh->flags & InputHint)
			c->neverfocus = !wmh->input;
		else
			c->neverfocus = 0;
		XFree(wmh);
	}
}

void
view(const Arg *arg)
{
	int i;
	unsigned int tmptag;

	if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
    switchtag();
    selmon->seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & TAGMASK) {
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
		selmon->pertag->prevtag = selmon->pertag->curtag;

		if (arg->ui == ~0)
			selmon->pertag->curtag = 0;
		else {
			for (i = 0; !(arg->ui & 1 << i); i++) ;
			selmon->pertag->curtag = i + 1;
		}
	} else {
		tmptag = selmon->pertag->prevtag;
		selmon->pertag->prevtag = selmon->pertag->curtag;
		selmon->pertag->curtag = tmptag;
	}

    selmon->gappiv = selmon->pertag->gappivs[selmon->pertag->curtag];
    selmon->gappih = selmon->pertag->gappihs[selmon->pertag->curtag];
    selmon->gappov = selmon->pertag->gappovs[selmon->pertag->curtag];
    selmon->gappoh = selmon->pertag->gappohs[selmon->pertag->curtag];
	selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
	selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag];
	selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
	selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
	selmon->lt[selmon->sellt^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt^1];

	if (selmon->showbar != selmon->pertag->showbars[selmon->pertag->curtag])
		togglebar(NULL);

	focus(NULL);
	arrange(selmon);
	updatecurrentdesktop();
}

pid_t
winpid(Window w)
{
	pid_t result = 0;

#ifdef __linux__
	xcb_res_client_id_spec_t spec = {0};
	spec.client = w;
	spec.mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID;

	xcb_generic_error_t *e = NULL;
	xcb_res_query_client_ids_cookie_t c = xcb_res_query_client_ids(xcon, 1, &spec);
	xcb_res_query_client_ids_reply_t *r = xcb_res_query_client_ids_reply(xcon, c, &e);

	if (!r)
		return (pid_t)0;

	xcb_res_client_id_value_iterator_t i = xcb_res_query_client_ids_ids_iterator(r);
	for (; i.rem; xcb_res_client_id_value_next(&i)) {
		spec = i.data->spec;
		if (spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID) {
			uint32_t *t = xcb_res_client_id_value_value(i.data);
			result = *t;
			break;
		}
	}

	free(r);

	if (result == (pid_t)-1)
		result = 0;
#endif /* __linux__ */
#ifdef __OpenBSD__
	Atom type;
	int format;
	unsigned long len, bytes;
	unsigned char *prop;
	pid_t ret;

	if (XGetWindowProperty(dpy, w, XInternAtom(dpy, "_NET_WM_PID", 0), 0, 1, False, AnyPropertyType, &type, &format, &len, &bytes, &prop) != Success || !prop)
		return 0;

	ret = *(pid_t*)prop;
	XFree(prop);
	result = ret;
#endif /* __OpenBSD__ */

	return result;
}

pid_t
getparentprocess(pid_t p)
{
	unsigned int v = 0;

#ifdef __linux__
	FILE *f;
	char buf[256];
	snprintf(buf, sizeof(buf) - 1, "/proc/%u/stat", (unsigned)p);

	if (!(f = fopen(buf, "r")))
		return 0;

	fscanf(f, "%*u %*s %*c %u", &v);
	fclose(f);
#endif /* __linux__*/

#ifdef __OpenBSD__
	int n;
	kvm_t *kd;
	struct kinfo_proc *kp;

	kd = kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, NULL);
	if (!kd)
		return 0;

	kp = kvm_getprocs(kd, KERN_PROC_PID, p, sizeof(*kp), &n);
	v = kp->p_ppid;
#endif /* __OpenBSD__ */

	return (pid_t)v;
}

int
isdescprocess(pid_t p, pid_t c)
{
	while (p != c && c != 0)
		c = getparentprocess(c);

	return (int)c;
}

Client *
termforwin(const Client *w)
{
	Client *c;
	Monitor *m;

	if (!w->pid || w->isterminal)
		return NULL;

	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next) {
			if (c->isterminal && !c->swallowing && c->pid && isdescprocess(c->pid, w->pid))
				return c;
		}
	}

	return NULL;
}

Client *
swallowingclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next) {
			if (c->swallowing && c->swallowing->win == w)
				return c;
		}
	}

	return NULL;
}

Client *
wintoclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->win == w)
				return c;
	return NULL;
}

Client *
wintosystrayicon(Window w) {
	Client *i = NULL;

	if (!showsystray || !w)
		return i;
	for (i = systray->icons; i && i->win != w; i = i->next) ;
	return i;
}

Monitor *
wintomon(Window w)
{
	int x, y;
	Client *c;
	Monitor *m;

	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	for (m = mons; m; m = m->next)
		if (w == m->barwin)
			return m;
	if ((c = wintoclient(w)))
		return c->mon;
	return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
		return 0;
	fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
	die("dwm: another window manager is already running");
	return -1;
}

Monitor *
systraytomon(Monitor *m) {
	Monitor *t;
	int i, n;
	if(!systraypinning) {
		if(!m)
			return selmon;
		return m == selmon ? m : NULL;
	}
	for(n = 1, t = mons; t && t->next; n++, t = t->next) ;
	for(i = 1, t = mons; t && t->next && i < systraypinning; i++, t = t->next) ;
	if(systraypinningfailfirst && n < systraypinning)
		return mons;
	return t;
}

void
xinitvisual()
{
	XVisualInfo *infos;
	XRenderPictFormat *fmt;
	int nitems;
	int i;

	XVisualInfo tpl = {
		.screen = screen,
		.depth = 32,
		.class = TrueColor
	};
	long masks = VisualScreenMask | VisualDepthMask | VisualClassMask;

	infos = XGetVisualInfo(dpy, masks, &tpl, &nitems);
	visual = NULL;
	for(i = 0; i < nitems; i ++) {
		fmt = XRenderFindVisualFormat(dpy, infos[i].visual);
		if (fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
			visual = infos[i].visual;
			depth = infos[i].depth;
			cmap = XCreateColormap(dpy, root, visual, AllocNone);
			useargb = 1;
			break;
		}
	}

	XFree(infos);

	if (! visual) {
		visual = DefaultVisual(dpy, screen);
		depth = DefaultDepth(dpy, screen);
		cmap = DefaultColormap(dpy, screen);
	}
}

void
resource_load(XrmDatabase db, char *name, enum resource_type rtype, void *dst)
{
	char *sdst = NULL;
	int *idst = NULL;
	float *fdst = NULL;

	sdst = dst;
	idst = dst;
	fdst = dst;

	char fullname[256];
	char *type;
	XrmValue ret;

	snprintf(fullname, sizeof(fullname), "%s.%s", "dwm", name);
	fullname[sizeof(fullname) - 1] = '\0';

	XrmGetResource(db, fullname, "*", &type, &ret);
	if (!(ret.addr == NULL || strncmp("String", type, 64)))
	{
		switch (rtype) {
		case STRING:
			strcpy(sdst, ret.addr);
			break;
		case INTEGER:
			*idst = strtoul(ret.addr, NULL, 10);
			break;
		case FLOAT:
			*fdst = strtof(ret.addr, NULL);
			break;
		}
	}
}

void
load_xresources(void)
{
	Display *display;
	char *resm;
	XrmDatabase db;
	ResourcePref *p;

	display = XOpenDisplay(NULL);
	resm = XResourceManagerString(display);
	if (!resm)
		return;

	db = XrmGetStringDatabase(resm);
	for (p = resources; p < resources + LENGTH(resources); p++)
		resource_load(db, p->name, p->type, p->dst);
	XCloseDisplay(display);
}

int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp("-v", argv[1]))
		die("dwm-"VERSION);
	else if (argc != 1)
		die("usage: dwm [-v]");
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("dwm: cannot open display");
	if (!(xcon = XGetXCBConnection(dpy)))
		die("dwm: cannot get xcb connection\n");
	checkotherwm();
	XrmInitialize();
	load_xresources();
	setup();
	scan();
	runautostart();
	run();
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
