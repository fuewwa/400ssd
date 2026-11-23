#ifndef WIN_H
#define WIN_H

#include "term.h"

void win_init(const char *fontname, const char *title, const char *wclass, int cols, int rows);
void win_show(void);
int win_fd(void);
unsigned long win_id(void);
void win_events(Term *t);
void win_sync(Term *t);
void win_draw(Term *t);

#endif
