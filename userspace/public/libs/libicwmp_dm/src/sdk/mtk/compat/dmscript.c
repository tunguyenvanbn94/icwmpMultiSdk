/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Script data model provider — transport (see dmscript.h).
 *
 *	Same idea as icwmp's external.c / easycwmp's external.c (one shell child,
 *	JSON lines, prompt as terminator) but the child stays alive between
 *	requests: sourcing the 25k lines of the easycwmp function library costs
 *	seconds, doing it once per icwmpd lifetime instead of once per RPC is
 *	the whole point of the bridge.
 *
 *	NOT BUILD-TESTED YET.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "dmscript.h"

#define DMSCRIPT_MAX_LINE   (1024 * 1024)   /* a reply line bigger than this = broken child */
#define DMSCRIPT_MAX_FD     1024

static pthread_mutex_t dmscript_mutex = PTHREAD_MUTEX_INITIALIZER;
static pid_t child_pid = -1;
static int fd_to_child = -1;      /* our write end  -> child stdin  */
static int fd_from_child = -1;    /* our read end   <- child stdout */
static char script_path[256] = DMSCRIPT_PATH;
static char script_arg[64] = DMSCRIPT_ARG;
static int timeout_sec = DMSCRIPT_TIMEOUT_SEC;

/* read-side line buffer, kept across calls (a reply may arrive in chunks) */
static char *rbuf;
static size_t rlen, rcap;

static void dmscript_log(const char *fmt, ...)
{
	va_list ap;
	FILE *fp;

	/* stderr of icwmpd is /dev/null when run by procd; keep a small trace
	 * file the developer can tail (rotated by size, see below) */
	fp = fopen("/tmp/icwmp_dm.log", "a");
	if (!fp)
		return;
	if (ftell(fp) > 256 * 1024) {
		fclose(fp);
		fp = fopen("/tmp/icwmp_dm.log", "w");
		if (!fp)
			return;
	}
	fprintf(fp, "[%ld] ", (long)time(NULL));
	va_start(ap, fmt);
	vfprintf(fp, fmt, ap);
	va_end(ap);
	fputc('\n', fp);
	fclose(fp);
}

void dmscript_configure(const char *script, const char *arg, int tmo)
{
	pthread_mutex_lock(&dmscript_mutex);
	if (script && script[0])
		snprintf(script_path, sizeof(script_path), "%s", script);
	if (arg)
		snprintf(script_arg, sizeof(script_arg), "%s", arg);
	if (tmo > 0)
		timeout_sec = tmo;
	pthread_mutex_unlock(&dmscript_mutex);
}

static void child_close_locked(int kill_it)
{
	int status;

	if (fd_to_child >= 0) {
		close(fd_to_child);
		fd_to_child = -1;
	}
	if (fd_from_child >= 0) {
		close(fd_from_child);
		fd_from_child = -1;
	}
	if (child_pid > 0) {
		if (kill_it)
			kill(child_pid, SIGKILL);
		/* the child exits on EOF of its stdin; do not block forever if a
		 * grandchild (setter still running) keeps it around */
		{
			int i;

			for (i = 0; i < 20; i++) {
				pid_t r = waitpid(child_pid, &status, WNOHANG);

				if (r == child_pid || (r < 0 && errno != EINTR))
					break;
				usleep(50 * 1000);
			}
			if (i == 20) {
				kill(child_pid, SIGKILL);
				waitpid(child_pid, &status, 0);
			}
		}
		child_pid = -1;
	}
	rlen = 0;
}

static int child_spawn_locked(void)
{
	int pin[2], pout[2];
	pid_t pid;

	if (access(script_path, R_OK) != 0) {
		dmscript_log("script %s not readable: %s", script_path, strerror(errno));
		return -1;
	}
	if (pipe(pin) < 0)
		return -1;
	if (pipe(pout) < 0) {
		close(pin[0]);
		close(pin[1]);
		return -1;
	}
	/* pin: child stdout -> us, pout: us -> child stdin */
	pid = fork();
	if (pid < 0) {
		close(pin[0]); close(pin[1]);
		close(pout[0]); close(pout[1]);
		return -1;
	}
	if (pid == 0) {
		int fd, maxfd;
		const char *dbg = getenv("ICWMP_DM_DEBUG");

		dup2(pout[0], STDIN_FILENO);
		dup2(pin[1], STDOUT_FILENO);
		if (!dbg || !dbg[0]) {
			fd = open("/dev/null", O_WRONLY);
			if (fd >= 0) {
				dup2(fd, STDERR_FILENO);
				close(fd);
			}
		}
		/* do not leak icwmpd's sockets (CR server port, ubus) into a shell
		 * that outlives a restart of icwmpd */
		maxfd = (int)sysconf(_SC_OPEN_MAX);
		if (maxfd < 0 || maxfd > DMSCRIPT_MAX_FD)
			maxfd = DMSCRIPT_MAX_FD;
		for (fd = 3; fd < maxfd; fd++)
			close(fd);
		signal(SIGPIPE, SIG_DFL);
		execl("/bin/sh", "sh", script_path, script_arg, (char *)NULL);
		_exit(127);
	}
	close(pout[0]);
	close(pin[1]);
	fd_to_child = pout[1];
	fd_from_child = pin[0];
	child_pid = pid;
	rlen = 0;
	/* never die on a closed pipe: the caller sees -1 instead */
	signal(SIGPIPE, SIG_IGN);
	dmscript_log("spawned %s %s pid %d", script_path, script_arg, (int)pid);
	return 0;
}

static int write_all(int fd, const char *buf, size_t len)
{
	while (len) {
		ssize_t w = write(fd, buf, len);

		if (w < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		buf += w;
		len -= (size_t)w;
	}
	return 0;
}

/* deliver every complete line in rbuf; returns 1 when the prompt was seen */
static int drain_lines_locked(dmscript_line_cb cb, void *priv)
{
	size_t start = 0;

	for (;;) {
		char *nl = memchr(rbuf + start, '\n', rlen - start);
		size_t n;

		if (!nl)
			break;
		n = (size_t)(nl - (rbuf + start));
		*nl = '\0';
		if (n && rbuf[start + n - 1] == '\r')
			rbuf[start + n - 1] = '\0';
		if (strcmp(rbuf + start, DMSCRIPT_PROMPT) == 0) {
			start += n + 1;
			memmove(rbuf, rbuf + start, rlen - start);
			rlen -= start;
			return 1;
		}
		if (n && cb) {
			json_object *jo = json_tokener_parse(rbuf + start);

			if (jo) {
				if (json_object_is_type(jo, json_type_object))
					cb(jo, priv);
				json_object_put(jo);
			} else {
				dmscript_log("ignored non-JSON line: %.120s", rbuf + start);
			}
		}
		start += n + 1;
	}
	if (start) {
		memmove(rbuf, rbuf + start, rlen - start);
		rlen -= start;
	}
	return 0;
}

static int read_reply_locked(dmscript_line_cb cb, void *priv)
{
	struct pollfd pfd = { .fd = fd_from_child, .events = POLLIN };
	time_t deadline = time(NULL) + timeout_sec;

	/* a prompt may already be buffered from a previous (aborted) call */
	if (rlen && drain_lines_locked(cb, priv))
		return 0;
	for (;;) {
		time_t now = time(NULL);
		int ms = (deadline > now) ? (int)(deadline - now) * 1000 : 0;
		int r;
		ssize_t got;

		if (ms <= 0) {
			dmscript_log("timeout (%d s) waiting for the prompt, killing pid %d", timeout_sec, (int)child_pid);
			return -1;
		}
		r = poll(&pfd, 1, ms);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (r == 0)
			continue;                              /* re-check the deadline */
		if (rlen + 4096 > rcap) {
			size_t ncap = rcap ? rcap * 2 : 16384;
			char *nb;

			if (ncap > DMSCRIPT_MAX_LINE + 4096) {
				dmscript_log("reply line over %d bytes, killing pid %d", DMSCRIPT_MAX_LINE, (int)child_pid);
				return -1;
			}
			nb = realloc(rbuf, ncap);
			if (!nb)
				return -1;
			rbuf = nb;
			rcap = ncap;
		}
		got = read(fd_from_child, rbuf + rlen, rcap - rlen - 1);
		if (got < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return -1;
		}
		if (got == 0) {
			dmscript_log("child pid %d closed its stdout", (int)child_pid);
			return -1;                             /* child died */
		}
		rlen += (size_t)got;
		rbuf[rlen] = '\0';
		if (drain_lines_locked(cb, priv))
			return 0;
	}
}

int dmscript_alive(void)
{
	int a;

	pthread_mutex_lock(&dmscript_mutex);
	a = child_pid > 0;
	pthread_mutex_unlock(&dmscript_mutex);
	return a;
}

int dmscript_call(json_object *req, dmscript_line_cb cb, void *priv)
{
	const char *s;
	char *line;
	size_t len;
	int rc = -1, attempt;

	if (!req)
		return -1;
	s = json_object_to_json_string(req);
	if (!s)
		return -1;
	len = strlen(s);
	line = malloc(len + 2);
	if (!line)
		return -1;
	memcpy(line, s, len);
	line[len] = '\n';
	line[len + 1] = '\0';

	pthread_mutex_lock(&dmscript_mutex);
	/* one retry: the child may have died since the last call (reboot of
	 * the WAN, killed by the OOM killer, ...) */
	for (attempt = 0; attempt < 2; attempt++) {
		if (child_pid < 0 && child_spawn_locked() != 0)
			break;
		if (write_all(fd_to_child, line, len + 1) != 0) {
			dmscript_log("write to pid %d failed: %s", (int)child_pid, strerror(errno));
			child_close_locked(1);
			continue;
		}
		if (read_reply_locked(cb, priv) == 0) {
			rc = 0;
			break;
		}
		child_close_locked(1);
		/* a timeout is not retried: the request itself hangs */
		break;
	}
	pthread_mutex_unlock(&dmscript_mutex);
	free(line);
	return rc;
}

int dmscript_request(dmscript_line_cb cb, void *priv, const char *cmd, ...)
{
	json_object *req = json_object_new_object();
	va_list ap;
	const char *k, *v;
	int rc;

	if (!req)
		return -1;
	json_object_object_add(req, "cmd", json_object_new_string(cmd ? cmd : ""));
	va_start(ap, cmd);
	while ((k = va_arg(ap, const char *)) != NULL) {
		v = va_arg(ap, const char *);
		if (v)
			json_object_object_add(req, k, json_object_new_string(v));
	}
	va_end(ap);
	rc = dmscript_call(req, cb, priv);
	json_object_put(req);
	return rc;
}

void dmscript_shutdown(void)
{
	pthread_mutex_lock(&dmscript_mutex);
	if (child_pid > 0 && fd_to_child >= 0) {
		static const char bye[] = "{\"cmd\":\"exit\"}\n";

		(void)write_all(fd_to_child, bye, sizeof(bye) - 1);
	}
	child_close_locked(0);
	pthread_mutex_unlock(&dmscript_mutex);
}
