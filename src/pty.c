#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "pty.h"
#include "util.h"

void pty_resize(int fd, int cols, int rows)
{
	struct winsize ws;

	if (fd < 0)
		return;
	memset(&ws, 0, sizeof ws);
	ws.ws_col = (unsigned short)cols;
	ws.ws_row = (unsigned short)rows;
	ioctl(fd, TIOCSWINSZ, &ws);
}

static const char *pick_shell(const char *fallback)
{
	const char *s = getenv("SHELL");
	struct passwd *pw;

	if (s && *s)
		return s;
	pw = getpwuid(getuid());
	if (pw && pw->pw_shell && *pw->pw_shell)
		return pw->pw_shell;
	return fallback;
}

int pty_spawn(char **cmd, const char *shell, const char *termname, int cols, int rows, unsigned long winid, pid_t *pid)
{
	int m, s;
	char *name;
	char id[32];
	const char *sh;

	m = posix_openpt(O_RDWR | O_NOCTTY);
	if (m < 0)
		die("posix_openpt failed");
	if (grantpt(m) < 0 || unlockpt(m) < 0)
		die("cannot unlock pty");
	name = ptsname(m);
	if (!name)
		die("ptsname failed");
	pty_resize(m, cols, rows);
	*pid = fork();
	if (*pid < 0)
		die("fork failed");
	if (*pid == 0) {
		setsid();
		s = open(name, O_RDWR);
		if (s < 0)
			_exit(1);
		ioctl(s, TIOCSCTTY, 0);
		dup2(s, 0);
		dup2(s, 1);
		dup2(s, 2);
		if (s > 2)
			close(s);
		close(m);
		snprintf(id, sizeof id, "%lu", winid);
		setenv("TERM", termname, 1);
		setenv("COLORTERM", "truecolor", 1);
		setenv("WINDOWID", id, 1);
		unsetenv("COLUMNS");
		unsetenv("LINES");
		if (cmd) {
			execvp(cmd[0], cmd);
			perror(cmd[0]);
			_exit(127);
		}
		sh = pick_shell(shell);
		execlp(sh, sh, (char *)NULL);
		perror(sh);
		_exit(127);
	}
	fcntl(m, F_SETFL, fcntl(m, F_GETFL) | O_NONBLOCK);
	return m;
}
