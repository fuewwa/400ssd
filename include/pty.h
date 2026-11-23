#ifndef PTY_H
#define PTY_H

#include <sys/types.h>

int pty_spawn(char **cmd, const char *shell, const char *termname, int cols, int rows, unsigned long winid, pid_t *pid);
void pty_resize(int fd, int cols, int rows);

#endif
