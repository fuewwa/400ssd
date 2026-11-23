#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <fontconfig/fontconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "pty.h"
#include "term.h"
#include "util.h"
#include "win.h"

#define CLICKMS 300
#define MAXFALLBACK 64
#define MAXMISSING 256

typedef struct {
	KeySym ks;
	int num;
	char fin;
	int ss3;
} Key;

static const Key keys[] = {
	{ XK_Up, 1, 'A', 0 },
	{ XK_Down, 1, 'B', 0 },
	{ XK_Right, 1, 'C', 0 },
	{ XK_Left, 1, 'D', 0 },
	{ XK_Home, 1, 'H', 0 },
	{ XK_End, 1, 'F', 0 },
	{ XK_F1, 1, 'P', 1 },
	{ XK_F2, 1, 'Q', 1 },
	{ XK_F3, 1, 'R', 1 },
	{ XK_F4, 1, 'S', 1 },
	{ XK_Insert, 2, '~', 0 },
	{ XK_Delete, 3, '~', 0 },
	{ XK_Prior, 5, '~', 0 },
	{ XK_Next, 6, '~', 0 },
	{ XK_F5, 15, '~', 0 },
	{ XK_F6, 17, '~', 0 },
	{ XK_F7, 18, '~', 0 },
	{ XK_F8, 19, '~', 0 },
	{ XK_F9, 20, '~', 0 },
	{ XK_F10, 21, '~', 0 },
	{ XK_F11, 23, '~', 0 },
	{ XK_F12, 24, '~', 0 }
};

static Display *dpy;
static int scr;
static Window win;
static Visual *vis;
static Colormap cmap;
static int depth;
static Pixmap buf;
static XftDraw *draw;
static GC gc;
static XftFont *fonts[4];
static FcPattern *fcbase;
static struct {
	XftFont *font;
	int style;
} fb[MAXFALLBACK];
static int nfb;
static uint32_t missing[MAXMISSING];
static int nmissing;
static int cw, chh, asc;
static int winw, winh;
static int focused;
static XIM xim;
static XIC xic;
static XftColor palette[256], deffg, defbg, curcol, selfgc, selbgc;
static Atom wmdelete, netwmname, utf8str, clipboard, targets, primary, xaprop, incratom;
static char *primarytext, *clipboardtext;

static int selvalid, seldown, selmode, clicks;
static unsigned long s_a, e_a, bs_a, be_a, click_a;
static int s_c, e_c, bs_c, be_c, click_c;
static Time lastclick;

static int cmp(unsigned long a1, int c1, unsigned long a2, int c2)
{
	if (a1 != a2)
		return a1 < a2 ? -1 : 1;
	return c1 - c2;
}

static int in_sel(unsigned long a, int col)
{
	if (!selvalid)
		return 0;
	return cmp(a, col, s_a, s_c) >= 0 && cmp(a, col, e_a, e_c) <= 0;
}

static int isword(uint32_t ch)
{
	if (ch == 0)
		return 0;
	if (ch < 128 && strchr(worddelimiters, (int)ch))
		return 0;
	return 1;
}

static XftFont *open_style(FcPattern *base, int style, FcCharSet *cs)
{
	FcPattern *q = FcPatternDuplicate(base), *m;
	FcResult r;
	XftFont *f;

	if (!q)
		return NULL;
	if (style & 1) {
		FcPatternDel(q, FC_WEIGHT);
		FcPatternAddInteger(q, FC_WEIGHT, FC_WEIGHT_BOLD);
	}
	if (style & 2) {
		FcPatternDel(q, FC_SLANT);
		FcPatternAddInteger(q, FC_SLANT, FC_SLANT_ITALIC);
	}
	if (cs)
		FcPatternAddCharSet(q, FC_CHARSET, cs);
	FcConfigSubstitute(NULL, q, FcMatchPattern);
	XftDefaultSubstitute(dpy, scr, q);
	m = FcFontMatch(NULL, q, &r);
	FcPatternDestroy(q);
	if (!m)
		return NULL;
	f = XftFontOpenPattern(dpy, m);
	if (!f)
		FcPatternDestroy(m);
	return f;
}

static void load_fonts(const char *fontname)
{
	static const char ascii[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
	XGlyphInfo ext;
	int i, n = (int)strlen(ascii);

	fcbase = FcNameParse((const FcChar8 *)fontname);
	if (!fcbase)
		die("cannot parse font: %s", fontname);
	for (i = 0; i < 4; i++) {
		fonts[i] = open_style(fcbase, i, NULL);
		if (!fonts[i])
			die("cannot load font: %s", fontname);
	}
	XftTextExtentsUtf8(dpy, fonts[0], (const FcChar8 *)ascii, n, &ext);
	cw = (ext.xOff + n - 1) / n;
	if (cw < 1)
		cw = 1;
	asc = fonts[0]->ascent;
	chh = fonts[0]->ascent + fonts[0]->descent;
	if (chh < 1)
		chh = 1;
}

static XftFont *font_for(uint32_t cp, int style)
{
	FcCharSet *cs;
	XftFont *f;
	int i;

	if (XftCharExists(dpy, fonts[style], cp))
		return fonts[style];
	for (i = 0; i < nfb; i++) {
		if (fb[i].style == style && XftCharExists(dpy, fb[i].font, cp))
			return fb[i].font;
	}
	for (i = 0; i < nmissing; i++) {
		if (missing[i] == cp)
			return fonts[style];
	}
	if (nfb >= MAXFALLBACK)
		return fonts[style];
	cs = FcCharSetCreate();
	FcCharSetAddChar(cs, cp);
	f = open_style(fcbase, style, cs);
	FcCharSetDestroy(cs);
	if (f && XftCharExists(dpy, f, cp)) {
		fb[nfb].font = f;
		fb[nfb].style = style;
		nfb++;
		return f;
	}
	if (f)
		XftFontClose(dpy, f);
	if (nmissing < MAXMISSING)
		missing[nmissing++] = cp;
	return fonts[style];
}

static void alloc_name(XftColor *c, const char *name)
{
	if (!XftColorAllocName(dpy, vis, cmap, name, c))
		die("cannot allocate color: %s", name);
}

static void alloc_rgb(XftColor *c, int r, int g, int b)
{
	XRenderColor rc;

	rc.red = (unsigned short)(r * 257);
	rc.green = (unsigned short)(g * 257);
	rc.blue = (unsigned short)(b * 257);
	rc.alpha = 0xffff;
	if (!XftColorAllocValue(dpy, vis, cmap, &rc, c))
		die("cannot allocate color");
}

static void load_colors(void)
{
	int i, r, g, b, v;

	for (i = 0; i < 16; i++)
		alloc_name(&palette[i], colors[i]);
	for (i = 16; i < 232; i++) {
		r = (i - 16) / 36;
		g = ((i - 16) / 6) % 6;
		b = (i - 16) % 6;
		alloc_rgb(&palette[i], r ? 55 + 40 * r : 0, g ? 55 + 40 * g : 0, b ? 55 + 40 * b : 0);
	}
	for (i = 232; i < 256; i++) {
		v = 8 + 10 * (i - 232);
		alloc_rgb(&palette[i], v, v, v);
	}
	alloc_name(&deffg, foreground);
	alloc_name(&defbg, background);
	alloc_name(&curcol, cursorcolor);
	alloc_name(&selfgc, selectionfg);
	alloc_name(&selbgc, selectionbg);
}

static const XftColor *resolve(uint32_t col, const XftColor *dflt, XftColor *tmp, int *owned)
{
	XRenderColor rc;

	if (col & COL_RGB) {
		rc.red = (unsigned short)(((col >> 16) & 0xff) * 257);
		rc.green = (unsigned short)(((col >> 8) & 0xff) * 257);
		rc.blue = (unsigned short)((col & 0xff) * 257);
		rc.alpha = 0xffff;
		if (XftColorAllocValue(dpy, vis, cmap, &rc, tmp)) {
			*owned = 1;
			return tmp;
		}
		return dflt;
	}
	if (col & COL_IDX)
		return &palette[col & 0xff];
	return dflt;
}

static void make_buffer(void)
{
	if (draw)
		XftDrawDestroy(draw);
	if (buf)
		XFreePixmap(dpy, buf);
	buf = XCreatePixmap(dpy, win, (unsigned)winw, (unsigned)winh, (unsigned)depth);
	draw = XftDrawCreate(dpy, buf, vis, cmap);
}

static void present(void)
{
	XCopyArea(dpy, buf, win, gc, 0, 0, (unsigned)winw, (unsigned)winh, 0, 0);
	XFlush(dpy);
}

static void set_title(const char *title)
{
	XStoreName(dpy, win, title);
	XChangeProperty(dpy, win, netwmname, utf8str, 8, PropModeReplace, (const unsigned char *)title, (int)strlen(title));
}

void win_init(const char *fontname, const char *title, const char *wclass, int cols, int rows)
{
	XSetWindowAttributes wa;
	XClassHint hint;
	XSizeHints *sh;

	dpy = XOpenDisplay(NULL);
	if (!dpy)
		die("cannot open display");
	scr = DefaultScreen(dpy);
	vis = DefaultVisual(dpy, scr);
	cmap = DefaultColormap(dpy, scr);
	depth = DefaultDepth(dpy, scr);
	load_fonts(fontname);
	load_colors();
	winw = 2 * border + cols * cw;
	winh = 2 * border + rows * chh;
	wa.background_pixel = defbg.pixel;
	wa.border_pixel = defbg.pixel;
	wa.colormap = cmap;
	wa.event_mask = KeyPressMask | KeyReleaseMask | ExposureMask | VisibilityChangeMask |
					StructureNotifyMask | ButtonMotionMask | ButtonPressMask |
					ButtonReleaseMask | FocusChangeMask;
	win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, (unsigned)winw, (unsigned)winh, 0,
						depth, InputOutput, vis,
						CWBackPixel | CWBorderPixel | CWColormap | CWEventMask, &wa);
	gc = XCreateGC(dpy, win, 0, NULL);
	wmdelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	netwmname = XInternAtom(dpy, "_NET_WM_NAME", False);
	utf8str = XInternAtom(dpy, "UTF8_STRING", False);
	clipboard = XInternAtom(dpy, "CLIPBOARD", False);
	targets = XInternAtom(dpy, "TARGETS", False);
	primary = XA_PRIMARY;
	xaprop = XInternAtom(dpy, "400SSD_SELECTION", False);
	incratom = XInternAtom(dpy, "INCR", False);
	XSetWMProtocols(dpy, win, &wmdelete, 1);
	sh = XAllocSizeHints();
	sh->flags = PSize | PResizeInc | PBaseSize | PMinSize;
	sh->width = winw;
	sh->height = winh;
	sh->width_inc = cw;
	sh->height_inc = chh;
	sh->base_width = 2 * border;
	sh->base_height = 2 * border;
	sh->min_width = cw + 2 * border;
	sh->min_height = chh + 2 * border;
	XSetWMNormalHints(dpy, win, sh);
	XFree(sh);
	hint.res_name = (char *)wclass;
	hint.res_class = (char *)wclass;
	XSetClassHint(dpy, win, &hint);
	XDefineCursor(dpy, win, XCreateFontCursor(dpy, XC_xterm));
	set_title(title);
	XSetLocaleModifiers("");
	xim = XOpenIM(dpy, NULL, NULL, NULL);
	if (xim)
		xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
						XNClientWindow, win, XNFocusWindow, win, NULL);
	make_buffer();
}

void win_show(void)
{
	XMapWindow(dpy, win);
	XSync(dpy, False);
}

int win_fd(void)
{
	return ConnectionNumber(dpy);
}

unsigned long win_id(void)
{
	return (unsigned long)win;
}

void win_sync(Term *t)
{
	if (t->titlechanged) {
		t->titlechanged = 0;
		set_title(t->title);
	}
	if (t->bell) {
		t->bell = 0;
		if (bell)
			XBell(dpy, 0);
	}
}

void win_draw(Term *t)
{
	int row, col, len, w, style, iscur, sel, ofg, obg, x, y, uy;
	unsigned long a;
	Cell c, *line;
	const XftColor *fg, *bg, *sw;
	XftColor tf, tb;
	FcChar32 g;
	XftFont *f;

	if (t->selreset) {
		selvalid = 0;
		t->selreset = 0;
	}
	XftDrawRect(draw, &defbg, 0, 0, (unsigned)winw, (unsigned)winh);
	for (row = 0; row < t->rows; row++) {
		a = t->htotal - (unsigned long)t->view + (unsigned long)row;
		len = 0;
		line = term_abs_row(t, a, &len);
		y = border + row * chh;
		for (col = 0; col < t->cols; col++) {
			if (line && col < len)
				c = line[col];
			else
				memset(&c, 0, sizeof c);
			if (c.attr & ATTR_WDUMMY)
				continue;
			w = 1;
			if (line && col + 1 < len && col + 1 < t->cols && (line[col + 1].attr & ATTR_WDUMMY))
				w = 2;
			sel = in_sel(a, col);
			iscur = t->showcursor && row == t->cur.y + t->view && col == t->cur.x;
			if (!sel && !iscur && c.ch <= ' ' && c.bg == 0 &&
				!(c.attr & (ATTR_REVERSE | ATTR_UNDER | ATTR_STRIKE)))
				continue;
			ofg = 0;
			obg = 0;
			fg = resolve(c.fg, &deffg, &tf, &ofg);
			bg = resolve(c.bg, &defbg, &tb, &obg);
			if (c.attr & ATTR_REVERSE) {
				sw = fg;
				fg = bg;
				bg = sw;
			}
			if (sel) {
				fg = &selfgc;
				bg = &selbgc;
			}
			if (iscur && focused) {
				fg = &defbg;
				bg = &curcol;
			}
			x = border + col * cw;
			if (bg != &defbg)
				XftDrawRect(draw, bg, x, y, (unsigned)(w * cw), (unsigned)chh);
			if (c.ch > ' ') {
				style = ((c.attr & ATTR_BOLD) ? 1 : 0) | ((c.attr & ATTR_ITALIC) ? 2 : 0);
				f = font_for(c.ch, style);
				g = c.ch;
				XftDrawString32(draw, fg, f, x, y + asc, &g, 1);
			}
			if (c.attr & ATTR_UNDER) {
				uy = asc + 1;
				if (uy >= chh)
					uy = chh - 1;
				XftDrawRect(draw, fg, x, y + uy, (unsigned)(w * cw), 1);
			}
			if (c.attr & ATTR_STRIKE)
				XftDrawRect(draw, fg, x, y + chh / 2, (unsigned)(w * cw), 1);
			if (iscur && !focused) {
				XftDrawRect(draw, &curcol, x, y, (unsigned)(w * cw), 1);
				XftDrawRect(draw, &curcol, x, y + chh - 1, (unsigned)(w * cw), 1);
				XftDrawRect(draw, &curcol, x, y, 1, (unsigned)chh);
				XftDrawRect(draw, &curcol, x + w * cw - 1, y, 1, (unsigned)chh);
			}
			if (ofg)
				XftColorFree(dpy, vis, cmap, &tf);
			if (obg)
				XftColorFree(dpy, vis, cmap, &tb);
		}
	}
	present();
}

static void cellat(const Term *t, int px, int py, unsigned long *a, int *col)
{
	int c = px < border ? 0 : (px - border) / cw;
	int r = py < border ? 0 : (py - border) / chh;

	if (c >= t->cols)
		c = t->cols - 1;
	if (r >= t->rows)
		r = t->rows - 1;
	*a = t->htotal - (unsigned long)t->view + (unsigned long)r;
	*col = c;
}

static void expand(Term *t, unsigned long a, int c, int mode, unsigned long *sa, int *sc, unsigned long *ea, int *ec)
{
	int len = 0, l, r, cls;
	Cell *row = term_abs_row(t, a, &len);

	*sa = a;
	*ea = a;
	if (mode == 3 || !row || len <= 0) {
		*sc = 0;
		*ec = t->cols - 1;
		return;
	}
	if (c >= len)
		c = len - 1;
	cls = isword(row[c].ch);
	l = c;
	r = c;
	while (l > 0 && isword(row[l - 1].ch) == cls)
		l--;
	while (r + 1 < len && isword(row[r + 1].ch) == cls)
		r++;
	*sc = l;
	*ec = r;
}

static void update_sel(Term *t, unsigned long a, int c)
{
	unsigned long ca, cea;
	int cc, cec;

	if (selmode == 1) {
		ca = a;
		cea = a;
		cc = c;
		cec = c;
	} else {
		expand(t, a, c, selmode, &ca, &cc, &cea, &cec);
	}
	if (cmp(ca, cc, bs_a, bs_c) < 0) {
		s_a = ca;
		s_c = cc;
	} else {
		s_a = bs_a;
		s_c = bs_c;
	}
	if (cmp(cea, cec, be_a, be_c) > 0) {
		e_a = cea;
		e_c = cec;
	} else {
		e_a = be_a;
		e_c = be_c;
	}
	selvalid = selmode > 1 || cmp(s_a, s_c, e_a, e_c) != 0;
}

static void own_selection(Term *t, Atom sel, Time time)
{
	char *text;

	if (!selvalid)
		return;
	text = term_text(t, s_a, s_c, e_a, e_c);
	if (!text || !*text) {
		free(text);
		return;
	}
	if (sel == primary) {
		free(primarytext);
		primarytext = text;
	} else {
		free(clipboardtext);
		clipboardtext = text;
	}
	XSetSelectionOwner(dpy, sel, win, time);
}

static void paste(Term *t, const char *s, size_t n)
{
	char *b = xcalloc(n + 1, 1);
	size_t i, m = 0;

	for (i = 0; i < n; i++) {
		if (s[i] == '\033')
			continue;
		b[m++] = s[i] == '\n' ? '\r' : s[i];
	}
	if (t->bracketed)
		term_send(t, "\033[200~", 6);
	term_send(t, b, m);
	if (t->bracketed)
		term_send(t, "\033[201~", 6);
	free(b);
}

static void user_input(Term *t)
{
	if (t->view) {
		t->view = 0;
		t->dirty = 1;
	}
	if (selvalid && !seldown) {
		selvalid = 0;
		t->dirty = 1;
	}
}

static void keypress(Term *t, XKeyEvent *e)
{
	char b[64], seq[32];
	KeySym ks = NoSymbol;
	Status st = XLookupBoth;
	int n, i, mod, len;
	int shift = (e->state & ShiftMask) != 0;
	int ctrl = (e->state & ControlMask) != 0;
	int alt = (e->state & Mod1Mask) != 0;

	if (xic) {
		n = Xutf8LookupString(xic, e, b, (int)sizeof b, &ks, &st);
		if (st == XBufferOverflow || st == XLookupNone)
			return;
		if (st == XLookupKeySym)
			n = 0;
	} else {
		n = XLookupString(e, b, (int)sizeof b, &ks, NULL);
	}
	if (shift && ctrl && (ks == XK_C || ks == XK_c)) {
		own_selection(t, clipboard, e->time);
		return;
	}
	if (shift && ctrl && (ks == XK_V || ks == XK_v)) {
		XConvertSelection(dpy, clipboard, utf8str, xaprop, win, e->time);
		return;
	}
	if (shift && ks == XK_Insert) {
		XConvertSelection(dpy, primary, utf8str, xaprop, win, e->time);
		return;
	}
	if (shift && ks == XK_Prior) {
		term_scroll_view(t, t->rows - 1);
		return;
	}
	if (shift && ks == XK_Next) {
		term_scroll_view(t, -(t->rows - 1));
		return;
	}
	for (i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++) {
		if (keys[i].ks != ks)
			continue;
		mod = 1 + shift + 2 * alt + 4 * ctrl;
		if (keys[i].fin == '~') {
			if (mod > 1)
				len = snprintf(seq, sizeof seq, "\033[%d;%d~", keys[i].num, mod);
			else
				len = snprintf(seq, sizeof seq, "\033[%d~", keys[i].num);
		} else if (mod > 1) {
			len = snprintf(seq, sizeof seq, "\033[1;%d%c", mod, keys[i].fin);
		} else if (keys[i].ss3 || t->appcursor) {
			len = snprintf(seq, sizeof seq, "\033O%c", keys[i].fin);
		} else {
			len = snprintf(seq, sizeof seq, "\033[%c", keys[i].fin);
		}
		user_input(t);
		term_send(t, seq, (size_t)len);
		return;
	}
	if (ks == XK_BackSpace) {
		b[0] = 0x7f;
		n = 1;
	} else if (ks == XK_ISO_Left_Tab) {
		memcpy(b, "\033[Z", 3);
		n = 3;
		alt = 0;
	}
	if (n <= 0)
		return;
	user_input(t);
	if (alt)
		term_send(t, "\033", 1);
	term_send(t, b, (size_t)n);
}

static void wheel(Term *t, int dir)
{
	const char *s;
	int i;

	if (t->alt) {
		if (dir > 0)
			s = t->appcursor ? "\033OA" : "\033[A";
		else
			s = t->appcursor ? "\033OB" : "\033[B";
		for (i = 0; i < wheelstep; i++)
			term_send(t, s, 3);
		return;
	}
	term_scroll_view(t, dir * wheelstep);
}

static void bpress(Term *t, XButtonEvent *e)
{
	unsigned long a;
	int c;

	switch (e->button) {
	case Button1:
		cellat(t, e->x, e->y, &a, &c);
		if (e->time - lastclick < CLICKMS && a == click_a && c == click_c)
			clicks = clicks % 3 + 1;
		else
			clicks = 1;
		lastclick = e->time;
		click_a = a;
		click_c = c;
		selmode = clicks;
		seldown = 1;
		if (selmode == 1) {
			bs_a = a;
			be_a = a;
			bs_c = c;
			be_c = c;
		} else {
			expand(t, a, c, selmode, &bs_a, &bs_c, &be_a, &be_c);
		}
		update_sel(t, a, c);
		t->dirty = 1;
		break;
	case Button2:
		XConvertSelection(dpy, primary, utf8str, xaprop, win, e->time);
		break;
	case Button4:
		wheel(t, 1);
		break;
	case Button5:
		wheel(t, -1);
		break;
	default:
		break;
	}
}

static void brelease(Term *t, XButtonEvent *e)
{
	if (e->button == Button1 && seldown) {
		seldown = 0;
		if (selvalid)
			own_selection(t, primary, e->time);
	}
}

static void bmotion(Term *t, XMotionEvent *e)
{
	unsigned long a;
	int c;

	if (!seldown)
		return;
	cellat(t, e->x, e->y, &a, &c);
	update_sel(t, a, c);
	t->dirty = 1;
}

static void configure(Term *t, int w, int h)
{
	int c, r;

	if (w == winw && h == winh)
		return;
	winw = w;
	winh = h;
	c = (w - 2 * border) / cw;
	r = (h - 2 * border) / chh;
	if (c < 1)
		c = 1;
	if (r < 1)
		r = 1;
	make_buffer();
	if (c != t->cols || r != t->rows) {
		term_resize(t, c, r);
		pty_resize(t->ptyfd, c, r);
	}
	t->dirty = 1;
}

static void selnotify(Term *t, XSelectionEvent *e)
{
	Atom type;
	int fmt;
	unsigned long n, rest;
	unsigned char *data = NULL;

	if (e->property == None)
		return;
	if (XGetWindowProperty(dpy, win, e->property, 0, 1 << 20, True, AnyPropertyType,
						   &type, &fmt, &n, &rest, &data) != Success)
		return;
	if (type != incratom && data && n && fmt == 8)
		paste(t, (const char *)data, n);
	if (data)
		XFree(data);
}

static void selrequest(XSelectionRequestEvent *e)
{
	XSelectionEvent r;
	Atom tg[3];
	const char *text = NULL;

	if (e->selection == primary)
		text = primarytext;
	else if (e->selection == clipboard)
		text = clipboardtext;
	memset(&r, 0, sizeof r);
	r.type = SelectionNotify;
	r.requestor = e->requestor;
	r.selection = e->selection;
	r.target = e->target;
	r.time = e->time;
	r.property = e->property == None ? e->target : e->property;
	if (e->target == targets) {
		tg[0] = utf8str;
		tg[1] = XA_STRING;
		tg[2] = targets;
		XChangeProperty(dpy, e->requestor, r.property, XA_ATOM, 32, PropModeReplace,
						(unsigned char *)tg, 3);
	} else if (text && (e->target == utf8str || e->target == XA_STRING)) {
		XChangeProperty(dpy, e->requestor, r.property, e->target, 8, PropModeReplace,
						(const unsigned char *)text, (int)strlen(text));
	} else {
		r.property = None;
	}
	XSendEvent(dpy, e->requestor, True, 0, (XEvent *)&r);
}

void win_events(Term *t)
{
	XEvent ev;

	while (XPending(dpy)) {
		XNextEvent(dpy, &ev);
		if (XFilterEvent(&ev, None))
			continue;
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				present();
			break;
		case ConfigureNotify:
			configure(t, ev.xconfigure.width, ev.xconfigure.height);
			break;
		case KeyPress:
			keypress(t, &ev.xkey);
			break;
		case ButtonPress:
			bpress(t, &ev.xbutton);
			break;
		case ButtonRelease:
			brelease(t, &ev.xbutton);
			break;
		case MotionNotify:
			bmotion(t, &ev.xmotion);
			break;
		case FocusIn:
			if (xic)
				XSetICFocus(xic);
			focused = 1;
			t->dirty = 1;
			break;
		case FocusOut:
			if (xic)
				XUnsetICFocus(xic);
			focused = 0;
			t->dirty = 1;
			break;
		case SelectionNotify:
			selnotify(t, &ev.xselection);
			break;
		case SelectionRequest:
			selrequest(&ev.xselectionrequest);
			break;
		case SelectionClear:
			if (ev.xselectionclear.selection == primary) {
				free(primarytext);
				primarytext = NULL;
				selvalid = 0;
				t->dirty = 1;
			} else if (ev.xselectionclear.selection == clipboard) {
				free(clipboardtext);
				clipboardtext = NULL;
			}
			break;
		case ClientMessage:
			if ((Atom)ev.xclient.data.l[0] == wmdelete)
				exit(0);
			break;
		default:
			break;
		}
	}
}
