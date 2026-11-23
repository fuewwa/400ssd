#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "term.h"
#include "util.h"

static const uint32_t acs[32] = {
	0x20, 0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0xb0,
	0xb1, 0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c,
	0x23ba, 0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534,
	0x252c, 0x2502, 0x2264, 0x2265, 0x3c0, 0x2260, 0xa3, 0xb7
};

static Cell **alloc_grid(int rows, int cols)
{
	Cell **g = xcalloc((size_t)rows, sizeof(Cell *));
	int i;

	for (i = 0; i < rows; i++)
		g[i] = xcalloc((size_t)cols, sizeof(Cell));
	return g;
}

static Cell **grid_resize(Cell **old, int orows, int ocols, int rows, int cols, int off)
{
	Cell **g = xcalloc((size_t)rows, sizeof(Cell *));
	int i, s, keep;

	keep = ocols < cols ? ocols : cols;
	for (i = 0; i < rows; i++) {
		g[i] = xcalloc((size_t)cols, sizeof(Cell));
		s = i + off;
		if (s < orows && old[s])
			memcpy(g[i], old[s], (size_t)keep * sizeof(Cell));
	}
	for (i = 0; i < orows; i++)
		free(old[i]);
	free(old);
	return g;
}

static Cell blank(const Term *t)
{
	Cell c;

	memset(&c, 0, sizeof c);
	c.bg = t->cur.bg;
	return c;
}

static void clear_cells(Term *t, int y, int x0, int x1)
{
	Cell b = blank(t);
	int x;

	if (x0 < 0)
		x0 = 0;
	if (x1 > t->cols)
		x1 = t->cols;
	for (x = x0; x < x1; x++)
		t->screen[y][x] = b;
	t->dirty = 1;
}

static void clear_all(Term *t)
{
	int y;

	for (y = 0; y < t->rows; y++)
		clear_cells(t, y, 0, t->cols);
	t->selreset = 1;
}

static void alt_screen(Term *t, int on, int save)
{
	Cell **tmp;

	if (on == t->alt)
		return;
	if (on) {
		if (save)
			t->altsaved = t->cur;
		tmp = t->screen;
		t->screen = t->other;
		t->other = tmp;
		t->alt = 1;
		t->view = 0;
		clear_all(t);
	} else {
		tmp = t->screen;
		t->screen = t->other;
		t->other = tmp;
		t->alt = 0;
		if (save)
			t->cur = t->altsaved;
	}
	t->top = 0;
	t->bottom = t->rows - 1;
	t->selreset = 1;
	t->dirty = 1;
}

static void init_tabs(Term *t)
{
	int i;

	free(t->tabs);
	t->tabs = xcalloc((size_t)t->cols, 1);
	for (i = 0; i < t->cols; i += 8)
		t->tabs[i] = 1;
}

static void reset_term(Term *t)
{
	if (t->alt)
		alt_screen(t, 0, 0);
	memset(&t->cur, 0, sizeof t->cur);
	t->saved = t->cur;
	t->altsaved = t->cur;
	t->top = 0;
	t->bottom = t->rows - 1;
	t->autowrap = 1;
	t->appcursor = 0;
	t->showcursor = 1;
	t->bracketed = 0;
	t->state = S_GROUND;
	t->u8need = 0;
	init_tabs(t);
	clear_all(t);
}

static void history_push(Term *t, Cell *row, int len)
{
	int idx;

	if (t->hcount == t->hcap) {
		idx = t->hhead;
		free(t->hist[idx]);
		t->hhead = (t->hhead + 1) % t->hcap;
	} else {
		idx = (t->hhead + t->hcount) % t->hcap;
		t->hcount++;
	}
	t->hist[idx] = row;
	t->hlen[idx] = len;
	t->htotal++;
	if (t->view > 0) {
		if (t->autoscroll)
			t->view = 0;
		else
			t->view++;
		if (t->view > t->hcount)
			t->view = t->hcount;
	}
}

static void scroll_up(Term *t, int top, int n, int save)
{
	int bot = t->bottom, k;
	Cell *row;

	if (top > bot)
		return;
	if (n > bot - top + 1)
		n = bot - top + 1;
	save = save && !t->alt && t->hcap > 0;
	for (k = 0; k < n; k++) {
		row = t->screen[top];
		memmove(&t->screen[top], &t->screen[top + 1], (size_t)(bot - top) * sizeof(Cell *));
		if (save) {
			history_push(t, row, t->cols);
			row = xcalloc((size_t)t->cols, sizeof(Cell));
		}
		t->screen[bot] = row;
		clear_cells(t, bot, 0, t->cols);
	}
	t->dirty = 1;
}

static void scroll_down(Term *t, int top, int n)
{
	int bot = t->bottom, k;
	Cell *row;

	if (top > bot)
		return;
	if (n > bot - top + 1)
		n = bot - top + 1;
	for (k = 0; k < n; k++) {
		row = t->screen[bot];
		memmove(&t->screen[top + 1], &t->screen[top], (size_t)(bot - top) * sizeof(Cell *));
		t->screen[top] = row;
		clear_cells(t, top, 0, t->cols);
	}
	t->dirty = 1;
}

static void move_to(Term *t, int x, int y)
{
	if (x < 0)
		x = 0;
	if (x >= t->cols)
		x = t->cols - 1;
	if (y < 0)
		y = 0;
	if (y >= t->rows)
		y = t->rows - 1;
	t->cur.x = x;
	t->cur.y = y;
	t->cur.wrapnext = 0;
	t->dirty = 1;
}

static void linefeed(Term *t)
{
	if (t->cur.y == t->bottom)
		scroll_up(t, t->top, 1, t->top == 0 && t->bottom == t->rows - 1);
	else if (t->cur.y < t->rows - 1)
		t->cur.y++;
}

static void tab_forward(Term *t, int n)
{
	int x = t->cur.x;

	for (; n > 0; n--) {
		do
			x++;
		while (x < t->cols - 1 && !t->tabs[x]);
		if (x >= t->cols - 1) {
			x = t->cols - 1;
			break;
		}
	}
	move_to(t, x, t->cur.y);
}

static void tab_back(Term *t, int n)
{
	int x = t->cur.x;

	for (; n > 0 && x > 0; n--) {
		do
			x--;
		while (x > 0 && !t->tabs[x]);
	}
	move_to(t, x, t->cur.y);
}

static void putch(Term *t, uint32_t cp)
{
	int w;
	Cell *c, b;

	if (t->cur.g0lines && cp >= 0x5f && cp <= 0x7e)
		cp = acs[cp - 0x5f];
	w = wcwidth((wchar_t)cp);
	if (w == 0)
		return;
	if (w < 0)
		w = 1;
	if (w > 2)
		w = 2;
	if (w == 2 && t->cols < 2)
		w = 1;
	if (t->cur.wrapnext && t->autowrap) {
		t->cur.x = 0;
		linefeed(t);
	}
	t->cur.wrapnext = 0;
	if (w == 2 && t->cur.x == t->cols - 1) {
		if (!t->autowrap)
			return;
		clear_cells(t, t->cur.y, t->cur.x, t->cur.x + 1);
		t->cur.x = 0;
		linefeed(t);
	}
	c = &t->screen[t->cur.y][t->cur.x];
	b = blank(t);
	if ((c->attr & ATTR_WDUMMY) && t->cur.x > 0)
		c[-1] = b;
	if (t->cur.x + 1 < t->cols && (c[1].attr & ATTR_WDUMMY))
		c[1] = b;
	c->ch = cp;
	c->fg = t->cur.fg;
	c->bg = t->cur.bg;
	c->attr = (uint16_t)(t->cur.attr & ~ATTR_WDUMMY);
	if (w == 2) {
		c[1].ch = 0;
		c[1].fg = t->cur.fg;
		c[1].bg = t->cur.bg;
		c[1].attr = ATTR_WDUMMY;
	}
	if (t->cur.x + w >= t->cols) {
		t->cur.x = t->cols - 1;
		t->cur.wrapnext = 1;
	} else {
		t->cur.x += w;
	}
	t->dirty = 1;
}

static void control(Term *t, unsigned char c)
{
	switch (c) {
	case '\a':
		t->bell = 1;
		break;
	case '\b':
		move_to(t, t->cur.x - 1, t->cur.y);
		break;
	case '\t':
		tab_forward(t, 1);
		break;
	case '\n':
	case '\v':
	case '\f':
		t->cur.wrapnext = 0;
		linefeed(t);
		break;
	case '\r':
		move_to(t, 0, t->cur.y);
		break;
	default:
		break;
	}
}

static int arg(const Term *t, int i, int def)
{
	if (i < t->npar && t->par[i] > 0)
		return t->par[i];
	return def;
}

static void reply(Term *t, const char *s)
{
	term_send(t, s, strlen(s));
}

static int color_arg(const Term *t, int i, uint32_t *out)
{
	if (i + 2 < t->npar && t->par[i + 1] == 5) {
		*out = COL_IDX | (uint32_t)(t->par[i + 2] & 0xff);
		return i + 2;
	}
	if (i + 4 < t->npar && t->par[i + 1] == 2) {
		*out = COL_RGB | ((uint32_t)(t->par[i + 2] & 0xff) << 16) |
			   ((uint32_t)(t->par[i + 3] & 0xff) << 8) |
			   (uint32_t)(t->par[i + 4] & 0xff);
		return i + 4;
	}
	return -1;
}

static void sgr(Term *t)
{
	int i, p, j;
	uint32_t col;

	if (t->npar == 0) {
		t->cur.attr = 0;
		t->cur.fg = 0;
		t->cur.bg = 0;
		return;
	}
	for (i = 0; i < t->npar; i++) {
		p = t->par[i];
		switch (p) {
		case 0:
			t->cur.attr = 0;
			t->cur.fg = 0;
			t->cur.bg = 0;
			break;
		case 1:
			t->cur.attr |= ATTR_BOLD;
			break;
		case 3:
			t->cur.attr |= ATTR_ITALIC;
			break;
		case 4:
			t->cur.attr |= ATTR_UNDER;
			break;
		case 7:
			t->cur.attr |= ATTR_REVERSE;
			break;
		case 9:
			t->cur.attr |= ATTR_STRIKE;
			break;
		case 22:
			t->cur.attr &= (uint16_t)~ATTR_BOLD;
			break;
		case 23:
			t->cur.attr &= (uint16_t)~ATTR_ITALIC;
			break;
		case 24:
			t->cur.attr &= (uint16_t)~ATTR_UNDER;
			break;
		case 27:
			t->cur.attr &= (uint16_t)~ATTR_REVERSE;
			break;
		case 29:
			t->cur.attr &= (uint16_t)~ATTR_STRIKE;
			break;
		case 38:
		case 48:
			j = color_arg(t, i, &col);
			if (j < 0)
				return;
			i = j;
			if (p == 38)
				t->cur.fg = col;
			else
				t->cur.bg = col;
			break;
		case 39:
			t->cur.fg = 0;
			break;
		case 49:
			t->cur.bg = 0;
			break;
		default:
			if (p >= 30 && p <= 37)
				t->cur.fg = COL_IDX | (uint32_t)(p - 30);
			else if (p >= 40 && p <= 47)
				t->cur.bg = COL_IDX | (uint32_t)(p - 40);
			else if (p >= 90 && p <= 97)
				t->cur.fg = COL_IDX | (uint32_t)(p - 90 + 8);
			else if (p >= 100 && p <= 107)
				t->cur.bg = COL_IDX | (uint32_t)(p - 100 + 8);
			break;
		}
	}
}

static void set_private(Term *t, int on)
{
	int i;

	for (i = 0; i < t->npar; i++) {
		switch (t->par[i]) {
		case 1:
			t->appcursor = on;
			break;
		case 7:
			t->autowrap = on;
			break;
		case 25:
			t->showcursor = on;
			t->dirty = 1;
			break;
		case 47:
		case 1047:
			alt_screen(t, on, 0);
			break;
		case 1049:
			alt_screen(t, on, 1);
			break;
		case 2004:
			t->bracketed = on;
			break;
		default:
			break;
		}
	}
}

static void erase_display(Term *t, int mode)
{
	int i, x = t->cur.x, y = t->cur.y;

	switch (mode) {
	case 0:
		clear_cells(t, y, x, t->cols);
		for (i = y + 1; i < t->rows; i++)
			clear_cells(t, i, 0, t->cols);
		break;
	case 1:
		for (i = 0; i < y; i++)
			clear_cells(t, i, 0, t->cols);
		clear_cells(t, y, 0, x + 1);
		break;
	case 2:
		clear_all(t);
		break;
	case 3:
		for (i = 0; i < t->hcount; i++) {
			free(t->hist[(t->hhead + i) % t->hcap]);
			t->hist[(t->hhead + i) % t->hcap] = NULL;
		}
		t->hcount = 0;
		t->hhead = 0;
		t->view = 0;
		t->selreset = 1;
		t->dirty = 1;
		break;
	default:
		break;
	}
}

static void erase_line(Term *t, int mode)
{
	switch (mode) {
	case 0:
		clear_cells(t, t->cur.y, t->cur.x, t->cols);
		break;
	case 1:
		clear_cells(t, t->cur.y, 0, t->cur.x + 1);
		break;
	case 2:
		clear_cells(t, t->cur.y, 0, t->cols);
		break;
	default:
		break;
	}
}

static void csi_dispatch(Term *t, unsigned char f)
{
	int n, x = t->cur.x, y = t->cur.y, top, bot;
	char buf[32];
	Cell *row;

	if (t->csiinterm)
		return;
	if (t->priv) {
		if (t->priv == '?' && (f == 'h' || f == 'l'))
			set_private(t, f == 'h');
		else if (t->priv == '>' && f == 'c')
			reply(t, "\033[>0;10;1c");
		return;
	}
	n = arg(t, 0, 1);
	switch (f) {
	case '@':
		if (n > t->cols - x)
			n = t->cols - x;
		row = t->screen[y];
		memmove(&row[x + n], &row[x], (size_t)(t->cols - x - n) * sizeof(Cell));
		clear_cells(t, y, x, x + n);
		break;
	case 'A':
		move_to(t, x, y - n);
		break;
	case 'B':
	case 'e':
		move_to(t, x, y + n);
		break;
	case 'C':
	case 'a':
		move_to(t, x + n, y);
		break;
	case 'D':
		move_to(t, x - n, y);
		break;
	case 'E':
		move_to(t, 0, y + n);
		break;
	case 'F':
		move_to(t, 0, y - n);
		break;
	case 'G':
	case '`':
		move_to(t, n - 1, y);
		break;
	case 'H':
	case 'f':
		move_to(t, arg(t, 1, 1) - 1, arg(t, 0, 1) - 1);
		break;
	case 'I':
		tab_forward(t, n);
		break;
	case 'Z':
		tab_back(t, n);
		break;
	case 'J':
		erase_display(t, t->npar > 0 ? t->par[0] : 0);
		break;
	case 'K':
		erase_line(t, t->npar > 0 ? t->par[0] : 0);
		break;
	case 'L':
		if (y >= t->top && y <= t->bottom)
			scroll_down(t, y, n);
		break;
	case 'M':
		if (y >= t->top && y <= t->bottom)
			scroll_up(t, y, n, 0);
		break;
	case 'P':
		if (n > t->cols - x)
			n = t->cols - x;
		row = t->screen[y];
		memmove(&row[x], &row[x + n], (size_t)(t->cols - x - n) * sizeof(Cell));
		clear_cells(t, y, t->cols - n, t->cols);
		break;
	case 'S':
		scroll_up(t, t->top, n, 0);
		break;
	case 'T':
		scroll_down(t, t->top, n);
		break;
	case 'X':
		if (n > t->cols - x)
			n = t->cols - x;
		clear_cells(t, y, x, x + n);
		break;
	case 'd':
		move_to(t, x, n - 1);
		break;
	case 'g':
		if (t->npar == 0 || t->par[0] == 0)
			t->tabs[x] = 0;
		else if (t->par[0] == 3)
			memset(t->tabs, 0, (size_t)t->cols);
		break;
	case 'm':
		sgr(t);
		break;
	case 'n':
		if (t->npar > 0 && t->par[0] == 5) {
			reply(t, "\033[0n");
		} else if (t->npar > 0 && t->par[0] == 6) {
			snprintf(buf, sizeof buf, "\033[%d;%dR", y + 1, x + 1);
			reply(t, buf);
		}
		break;
	case 'c':
		reply(t, "\033[?6c");
		break;
	case 'r':
		top = arg(t, 0, 1) - 1;
		bot = arg(t, 1, t->rows) - 1;
		if (bot >= t->rows)
			bot = t->rows - 1;
		if (top < bot) {
			t->top = top;
			t->bottom = bot;
			move_to(t, 0, 0);
		}
		break;
	case 's':
		t->saved = t->cur;
		break;
	case 'u':
		t->cur = t->saved;
		move_to(t, t->cur.x, t->cur.y);
		break;
	default:
		break;
	}
}

static void csi_byte(Term *t, unsigned char c)
{
	int cap = (int)(sizeof t->par / sizeof t->par[0]);

	if (c >= '0' && c <= '9') {
		if (t->npar == 0) {
			t->npar = 1;
			t->par[0] = 0;
		}
		if (t->par[t->npar - 1] < 99999)
			t->par[t->npar - 1] = t->par[t->npar - 1] * 10 + (c - '0');
	} else if (c == ';' || c == ':') {
		if (t->npar == 0) {
			t->npar = 1;
			t->par[0] = 0;
		}
		if (t->npar < cap)
			t->par[t->npar++] = 0;
	} else if (c >= 0x3c && c <= 0x3f) {
		t->priv = (char)c;
	} else if (c >= 0x20 && c <= 0x2f) {
		t->csiinterm = 1;
	} else if (c >= 0x40 && c <= 0x7e) {
		t->state = S_GROUND;
		csi_dispatch(t, c);
	}
}

static void osc_dispatch(Term *t)
{
	char *p = t->str;
	int code = 0;

	t->str[t->slen] = '\0';
	while (*p >= '0' && *p <= '9')
		code = code * 10 + (*p++ - '0');
	if (*p != ';')
		return;
	p++;
	if (code == 0 || code == 1 || code == 2) {
		strncpy(t->title, p, sizeof t->title - 1);
		t->title[sizeof t->title - 1] = '\0';
		t->titlechanged = 1;
	}
}

static void esc_dispatch(Term *t, unsigned char c)
{
	t->state = S_GROUND;
	switch (c) {
	case '[':
		t->state = S_CSI;
		t->npar = 0;
		t->priv = 0;
		t->csiinterm = 0;
		break;
	case ']':
		t->state = S_OSC;
		t->slen = 0;
		break;
	case 'P':
	case '_':
	case '^':
	case 'X':
		t->state = S_STR;
		break;
	case '(':
		t->state = S_CHARSET;
		t->charsetg = 0;
		break;
	case ')':
	case '*':
	case '+':
	case '#':
	case ' ':
	case '%':
		t->state = S_CHARSET;
		t->charsetg = 1;
		break;
	case '7':
		t->saved = t->cur;
		break;
	case '8':
		t->cur = t->saved;
		move_to(t, t->cur.x, t->cur.y);
		break;
	case 'D':
		t->cur.wrapnext = 0;
		linefeed(t);
		break;
	case 'E':
		move_to(t, 0, t->cur.y);
		linefeed(t);
		break;
	case 'M':
		if (t->cur.y == t->top)
			scroll_down(t, t->top, 1);
		else
			move_to(t, t->cur.x, t->cur.y - 1);
		break;
	case 'H':
		t->tabs[t->cur.x] = 1;
		break;
	case 'c':
		reset_term(t);
		break;
	default:
		break;
	}
}

static void utf8_start(Term *t, unsigned char c)
{
	if (c >= 0xc2 && c <= 0xdf) {
		t->u8cp = c & 0x1f;
		t->u8need = 1;
	} else if ((c & 0xf0) == 0xe0) {
		t->u8cp = c & 0x0f;
		t->u8need = 2;
	} else if (c >= 0xf0 && c <= 0xf4) {
		t->u8cp = c & 0x07;
		t->u8need = 3;
	} else {
		putch(t, 0xfffd);
	}
}

static void feed(Term *t, unsigned char c)
{
	if (t->u8need) {
		if ((c & 0xc0) == 0x80) {
			t->u8cp = (t->u8cp << 6) | (c & 0x3f);
			if (--t->u8need == 0)
				putch(t, t->u8cp > 0x10ffff ? 0xfffd : t->u8cp);
			return;
		}
		t->u8need = 0;
		putch(t, 0xfffd);
	}
	switch (t->state) {
	case S_GROUND:
		if (c == 0x1b)
			t->state = S_ESC;
		else if (c < 0x20)
			control(t, c);
		else if (c == 0x7f)
			break;
		else if (c < 0x80)
			putch(t, c);
		else
			utf8_start(t, c);
		break;
	case S_ESC:
		if (c == 0x1b)
			break;
		if (c < 0x20) {
			control(t, c);
			break;
		}
		esc_dispatch(t, c);
		break;
	case S_CSI:
		if (c == 0x1b)
			t->state = S_ESC;
		else if (c < 0x20)
			control(t, c);
		else
			csi_byte(t, c);
		break;
	case S_OSC:
		if (c == 0x07) {
			osc_dispatch(t);
			t->state = S_GROUND;
		} else if (c == 0x1b) {
			t->state = S_OSCESC;
		} else if (c >= 0x20 && t->slen < (int)sizeof t->str - 1) {
			t->str[t->slen++] = (char)c;
		}
		break;
	case S_OSCESC:
		if (c == '\\') {
			osc_dispatch(t);
			t->state = S_GROUND;
		} else {
			t->state = S_ESC;
			feed(t, c);
		}
		break;
	case S_STR:
		if (c == 0x07)
			t->state = S_GROUND;
		else if (c == 0x1b)
			t->state = S_STRESC;
		break;
	case S_STRESC:
		if (c == '\\') {
			t->state = S_GROUND;
		} else {
			t->state = S_ESC;
			feed(t, c);
		}
		break;
	case S_CHARSET:
		if (t->charsetg == 0)
			t->cur.g0lines = (c == '0');
		t->state = S_GROUND;
		break;
	default:
		t->state = S_GROUND;
		break;
	}
}

void term_init(Term *t, int cols, int rows, int scrollback, int autoscroll)
{
	memset(t, 0, sizeof *t);
	t->cols = cols;
	t->rows = rows;
	t->screen = alloc_grid(rows, cols);
	t->other = alloc_grid(rows, cols);
	if (scrollback > 0) {
		t->hcap = scrollback;
		t->hist = xcalloc((size_t)scrollback, sizeof(Cell *));
		t->hlen = xcalloc((size_t)scrollback, sizeof(int));
	}
	t->autoscroll = autoscroll;
	t->ptyfd = -1;
	reset_term(t);
	t->dirty = 1;
}

void term_resize(Term *t, int cols, int rows)
{
	int off = 0, i;

	if (cols < 1)
		cols = 1;
	if (rows < 1)
		rows = 1;
	if (cols == t->cols && rows == t->rows)
		return;
	if (!t->alt && t->cur.y >= rows) {
		off = t->cur.y - rows + 1;
		if (t->hcap > 0) {
			for (i = 0; i < off; i++) {
				history_push(t, t->screen[i], t->cols);
				t->screen[i] = NULL;
			}
		}
		t->cur.y -= off;
	}
	t->screen = grid_resize(t->screen, t->rows, t->cols, rows, cols, off);
	t->other = grid_resize(t->other, t->rows, t->cols, rows, cols, 0);
	t->cols = cols;
	t->rows = rows;
	t->top = 0;
	t->bottom = rows - 1;
	init_tabs(t);
	if (t->cur.x >= cols)
		t->cur.x = cols - 1;
	if (t->cur.y >= rows)
		t->cur.y = rows - 1;
	if (t->saved.x >= cols)
		t->saved.x = cols - 1;
	if (t->saved.y >= rows)
		t->saved.y = rows - 1;
	if (t->altsaved.x >= cols)
		t->altsaved.x = cols - 1;
	if (t->altsaved.y >= rows)
		t->altsaved.y = rows - 1;
	t->cur.wrapnext = 0;
	if (t->view > t->hcount)
		t->view = t->hcount;
	t->selreset = 1;
	t->dirty = 1;
}

void term_write(Term *t, const char *buf, size_t len)
{
	size_t i;

	t->parsing = 1;
	if (t->autoscroll && t->view)
		t->view = 0;
	for (i = 0; i < len; i++)
		feed(t, (unsigned char)buf[i]);
	t->parsing = 0;
	t->dirty = 1;
}

void term_send(Term *t, const char *buf, size_t len)
{
	struct pollfd p;
	char tmp[8192];
	ssize_t n;

	if (t->ptyfd < 0)
		return;
	while (len > 0) {
		p.fd = t->ptyfd;
		p.events = (short)(POLLOUT | (t->parsing ? 0 : POLLIN));
		p.revents = 0;
		if (poll(&p, 1, -1) < 0) {
			if (errno == EINTR)
				continue;
			return;
		}
		if (p.revents & POLLIN) {
			n = read(t->ptyfd, tmp, sizeof tmp);
			if (n > 0)
				term_write(t, tmp, (size_t)n);
		}
		if (p.revents & POLLOUT) {
			n = write(t->ptyfd, buf, len);
			if (n < 0) {
				if (errno == EINTR || errno == EAGAIN)
					continue;
				return;
			}
			buf += n;
			len -= (size_t)n;
		} else if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			return;
		}
	}
}

void term_scroll_view(Term *t, int delta)
{
	int v = t->view + delta;

	if (t->alt)
		v = 0;
	if (v > t->hcount)
		v = t->hcount;
	if (v < 0)
		v = 0;
	if (v != t->view) {
		t->view = v;
		t->dirty = 1;
	}
}

Cell *term_abs_row(Term *t, unsigned long a, int *len)
{
	unsigned long oldest;
	int idx;

	if (a >= t->htotal) {
		a -= t->htotal;
		if (a >= (unsigned long)t->rows)
			return NULL;
		*len = t->cols;
		return t->screen[a];
	}
	if (t->alt || t->hcap == 0)
		return NULL;
	oldest = t->htotal - (unsigned long)t->hcount;
	if (a < oldest)
		return NULL;
	idx = (t->hhead + (int)(a - oldest)) % t->hcap;
	*len = t->hlen[idx];
	return t->hist[idx];
}

static void sbuf_put(char **buf, size_t *len, size_t *cap, uint32_t cp)
{
	if (*len + 8 > *cap) {
		*cap *= 2;
		*buf = realloc(*buf, *cap);
		if (!*buf)
			die("out of memory");
	}
	*len += utf8_encode(cp, *buf + *len);
	(*buf)[*len] = '\0';
}

char *term_text(Term *t, unsigned long a0, int c0, unsigned long a1, int c1)
{
	size_t cap = 256, len = 0;
	char *out = xcalloc(cap, 1);
	unsigned long a;
	int rl, s, e, last, x;
	Cell *row;

	for (a = a0; a <= a1; a++) {
		rl = 0;
		row = term_abs_row(t, a, &rl);
		s = a == a0 ? c0 : 0;
		e = a == a1 ? c1 : t->cols - 1;
		if (row) {
			if (e >= rl)
				e = rl - 1;
			last = e;
			while (last >= s && (row[last].ch == 0 || row[last].ch == ' ') && !(row[last].attr & ATTR_WDUMMY))
				last--;
			for (x = s; x <= last; x++) {
				if (row[x].attr & ATTR_WDUMMY)
					continue;
				sbuf_put(&out, &len, &cap, row[x].ch ? row[x].ch : ' ');
			}
		}
		if (a < a1)
			sbuf_put(&out, &len, &cap, '\n');
	}
	return out;
}
