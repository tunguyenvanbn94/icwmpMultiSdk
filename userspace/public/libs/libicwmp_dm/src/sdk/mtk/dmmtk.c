/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Helpers shared by the C data model modules of the MTK/Airoha SDK.
 *	See dmmtk.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <uci.h>

#include "dmtr098.h"
#include "dmmem.h"
#include "dmuci.h"
#include "dmcommon.h"
#include "dmmtk.h"

#define APPLY_SERVICE_FILE	"/tmp/.easycwmp_apply_service"

char *mtk_uci(char *package, char *section, char *option)
{
	char *v = NULL;

	dmuci_get_option_value_string(package, section, option, &v);
	return v ? v : "";
}

char *mtk_varstate(char *package, char *section, char *option)
{
	char *v = NULL;

	dmuci_get_varstate_string(package, section, option, &v);
	return v ? v : "";
}

int mtk_varstate_set(char *package, char *section, char *option, char *value)
{
	struct uci_ptr ptr = {0};

	if (!uci_varstate_ctx)
		return -1;
	uci_add_delta_path(uci_varstate_ctx, uci_varstate_ctx->savedir);
	uci_set_savedir(uci_varstate_ctx, VARSTATE_CONFIG);
	if (dmuci_lookup_ptr(uci_varstate_ctx, &ptr, package, section, option, value))
		return -1;
	if (uci_set(uci_varstate_ctx, &ptr) != UCI_OK)
		return -1;
	if (ptr.p)
		uci_save(uci_varstate_ctx, ptr.p);
	return 0;
}

char *mtk_file_line(const char *path)
{
	char buf[512];
	FILE *f;
	size_t l;

	f = fopen(path, "r");
	if (!f)
		return "";
	if (!fgets(buf, sizeof(buf), f)) {
		fclose(f);
		return "";
	}
	fclose(f);
	l = strlen(buf);
	while (l && (buf[l - 1] == '\n' || buf[l - 1] == '\r' || buf[l - 1] == ' '))
		buf[--l] = '\0';
	return dmstrdup(buf);
}

long mtk_meminfo_kb(const char *key)
{
	char line[256];
	FILE *f;
	size_t kl;
	long val = -1;

	f = fopen("/proc/meminfo", "r");
	if (!f)
		return -1;
	kl = strlen(key);
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, key, kl) == 0 && line[kl] == ':') {
			val = strtol(line + kl + 1, NULL, 10);
			break;
		}
	}
	fclose(f);
	return val;
}

long mtk_uptime(void)
{
	char buf[64];
	FILE *f;
	long up = 0;

	f = fopen("/proc/uptime", "r");
	if (!f)
		return 0;
	if (fgets(buf, sizeof(buf), f))
		up = strtol(buf, NULL, 10);
	fclose(f);
	return up < 0 ? 0 : up;
}

/* Full stdout of argv, dm-allocated.  Reads before reaping, so a helper
 * that prints more than one pipe buffer cannot deadlock us (dmcmd() of
 * dmcommon.c waits first and would). */
char *mtk_exec(char *const argv[])
{
	int pfd[2], status;
	pid_t pid;
	char buf[512];
	char *out = NULL;
	size_t len = 0;
	ssize_t n;

	if (!argv || !argv[0])
		return "";
	if (pipe(pfd) < 0)
		return "";
	pid = fork();
	if (pid < 0) {
		close(pfd[0]);
		close(pfd[1]);
		return "";
	}
	if (pid == 0) {
		int fd, maxfd = (int)sysconf(_SC_OPEN_MAX);

		close(pfd[0]);
		dup2(pfd[1], STDOUT_FILENO);
		if (pfd[1] != STDOUT_FILENO)
			close(pfd[1]);
		fd = open("/dev/null", O_WRONLY);
		if (fd >= 0) {
			dup2(fd, STDERR_FILENO);
			if (fd > STDERR_FILENO)
				close(fd);
		}
		/* never hand the agent's sockets (connection request listener,
		 * ubus) to a helper: it would keep the port after a restart */
		if (maxfd < 3)
			maxfd = 1024;
		for (fd = 3; fd < maxfd; fd++)
			close(fd);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(pfd[1]);
	while ((n = read(pfd[0], buf, sizeof(buf))) > 0) {
		out = dmrealloc(out, len + (size_t)n + 1);
		if (!out)
			break;
		memcpy(out + len, buf, (size_t)n);
		len += (size_t)n;
		out[len] = '\0';
		if (len > 64 * 1024)		/* a data model value, not a log */
			break;
	}
	close(pfd[0]);
	waitpid(pid, &status, 0);
	return out ? out : "";
}

char *mtk_exec_line(char *const argv[])
{
	char *out = mtk_exec(argv);
	size_t l;

	if (!out || !out[0])
		return "";
	l = strcspn(out, "\n");
	out[l] = '\0';
	while (l && (out[l - 1] == '\r' || out[l - 1] == ' '))
		out[--l] = '\0';
	return out;
}

int mtk_apply_service(const char *cmd)
{
	FILE *f;

	if (!cmd || !*cmd)
		return -1;
	f = fopen(APPLY_SERVICE_FILE, "a");
	if (!f)
		return -1;
	fprintf(f, "%s\n", cmd);
	fclose(f);
	return 0;
}

void mtk_run_apply_service(void)
{
	char line[512];
	FILE *f;

	f = fopen(APPLY_SERVICE_FILE, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char *argv[] = { "/bin/sh", "-c", line, NULL };

		line[strcspn(line, "\r\n")] = '\0';
		if (!line[0])
			continue;
		/* output discarded on purpose: these are init scripts, their
		 * logs belong in syslog, not in a CWMP reply */
		(void)mtk_exec(argv);
	}
	fclose(f);
	remove(APPLY_SERVICE_FILE);
}

int mtk_bool(const char *v)
{
	if (!v || !*v)
		return 0;
	return (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 ||
		strcasecmp(v, "on") == 0 || strcasecmp(v, "yes") == 0 ||
		strcasecmp(v, "enabled") == 0);
}

char *mtk_bool_str(int on)
{
	return on ? "true" : "false";
}

int mtk_parse_bool(const char *v)
{
	if (!v)
		return -1;
	if (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0)
		return 1;
	if (strcmp(v, "0") == 0 || strcasecmp(v, "false") == 0)
		return 0;
	return -1;
}
