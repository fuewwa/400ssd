#ifndef TERM_H
#define TERM_H

#include <stddef.h>
#include <stdint.h>

#define COL_RGB 0x01000000u
#define COL_IDX 0x02000000u

enum {
	ATTR_BOLD = 1 << 0,
	ATTR_ITALIC = 1 << 1,
	ATTR_UNDER = 1 << 2,
	ATTR_REVERSE = 1 << 3,
	ATTR_STRIKE = 1 << 4,
	ATTR_WDUMMY = 1 << 5
};

enum {
	S_GROUND,
	S_ESC,
	S_CSI,
	S_OSC,
	S_OSCESC,
	S_CHARSET,
	S_STR,
	S_STRESC
};

typedef struct {
	uint32_t ch;
	uint32_t fg;
	uint32_t bg;
	uint16_t attr;
} Cell;

typedef struct {
	int x, y;
	uint16_t attr;
	uint32_t fg, bg;
	int g0lines;
	int wrapnext;
} Caret;

typedef struct {
	int cols, rows;
	Cell **screen;
	Cell **other;
	int alt;
	Caret cur, saved, altsaved;
	int top, bottom;
	unsigned char *tabs;
	int autowrap, appcursor, showcursor, bracketed, autoscroll;
	Cell **hist;
	int *hlen;
	int hcap, hhead, hcount;
	unsigned long htotal;
	int view;
	int state;
	uint32_t u8cp;
	int u8need;
	int par[32];
	int npar;
	char priv;
	int csiinterm;
	int charsetg;
	char str[1024];
	int slen;
	int ptyfd;
	int dirty, parsing, selreset, bell;
	char title[256];
	int titlechanged;
} Term;

void term_init(Term *t, int cols, int rows, int scrollback, int autoscroll);
void term_resize(Term *t, int cols, int rows);
void term_write(Term *t, const char *buf, size_t len);
void term_send(Term *t, const char *buf, size_t len);
void term_scroll_view(Term *t, int delta);
Cell *term_abs_row(Term *t, unsigned long a, int *len);
char *term_text(Term *t, unsigned long a0, int c0, unsigned long a1, int c1);

#endif
