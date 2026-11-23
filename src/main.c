#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "pty.h"
#include "term.h"
#include "util.h"
#include "win.h"

#define DRAWMS 16

static Term term;
static pid_t child;

static long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void on_child(int sig)
{
	int st;
	pid_t p;

	(void)sig;
	p = waitpid(child, &st, WNOHANG);
	if (child > 0 && p == child)
		_exit(WIFEXITED(st) ? WEXITSTATUS(st) : 1);
}

static void usage(void)
{
	fputs("usage: 400ssd [-v] [-f font] [-t title] [-c class] [-e command [args ...]]\n", stderr);
	exit(1);
}

static void drain(void)
{
	char b[65536];
	ssize_t n;
	int i;

	for (i = 0; i < 16; i++) {
		n = read(term.ptyfd, b, sizeof b);
		if (n > 0) {
			term_write(&term, b, (size_t)n);
			if ((size_t)n < sizeof b)
				break;
		} else if (n == 0) {
			exit(0);
		} else if (errno == EINTR) {
			continue;
		} else if (errno == EAGAIN) {
			break;
		} else {
			exit(0);
		}
	}
}

int main(int argc, char **argv)
{
	const char *fontname = font;
	const char *title = "400 SSD Term";
	const char *wclass = "400ssd";
	char **cmd = NULL;
	struct sigaction sa;
	struct timeval tv;
	fd_set rfd;
	long last = 0, wait;
	int i, xfd, maxfd;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) {
			puts("400ssd " VERSION);
			return 0;
		} else if (!strcmp(argv[i], "-f") && i + 1 < argc) {
			fontname = argv[++i];
		} else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
			title = argv[++i];
		} else if (!strcmp(argv[i], "-c") && i + 1 < argc) {
			wclass = argv[++i];
		} else if (!strcmp(argv[i], "-e")) {
			if (i + 1 >= argc)
				usage();
			cmd = &argv[i + 1];
			break;
		} else {
			usage();
		}
	}
	setlocale(LC_CTYPE, "");
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_child;
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGCHLD, &sa, NULL);
	win_init(fontname, title, wclass, defcols, defrows);
	term_init(&term, defcols, defrows, scrollback, autoscroll);
	term.ptyfd = pty_spawn(cmd, shell, termname, defcols, defrows, win_id(), &child);
	win_draw(&term);
	term.dirty = 0;
	win_show();
	xfd = win_fd();
	maxfd = xfd > term.ptyfd ? xfd : term.ptyfd;
	for (;;) {
		win_events(&term);
		FD_ZERO(&rfd);
		FD_SET(term.ptyfd, &rfd);
		FD_SET(xfd, &rfd);
		if (term.dirty) {
			wait = DRAWMS - (now_ms() - last);
			if (wait < 0)
				wait = 0;
			tv.tv_sec = 0;
			tv.tv_usec = (suseconds_t)(wait * 1000);
		}
		if (select(maxfd + 1, &rfd, NULL, NULL, term.dirty ? &tv : NULL) < 0) {
			if (errno == EINTR)
				continue;
			die("select failed");
		}
		if (FD_ISSET(term.ptyfd, &rfd))
			drain();
		win_events(&term);
		if (term.dirty && now_ms() - last >= DRAWMS) {
			win_sync(&term);
			win_draw(&term);
			term.dirty = 0;
			last = now_ms();
		}
	}
}
