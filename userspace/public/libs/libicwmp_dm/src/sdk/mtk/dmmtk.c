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
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <regex.h>
#include <net/if.h>

#include <uci.h>

#include "dmtr098.h"
#include "dmmem.h"
#include "dmuci.h"
#include "dmcommon.h"
#include "dmmtk.h"

#define APPLY_SERVICE_FILE	"/tmp/.easycwmp_apply_service"

char *mtk_uci(const char *package, const char *section, const char *option)
{
	char *v = NULL;

	dmuci_get_option_value_string((char *)package, (char *)section, (char *)option, &v);
	return v ? v : "";
}

char *mtk_varstate(const char *package, const char *section, const char *option)
{
	char *v = NULL;

	dmuci_get_varstate_string((char *)package, (char *)section, (char *)option, &v);
	return v ? v : "";
}

int mtk_uci_ensure_section(const char *package, const char *section, const char *type)
{
	char *t = NULL;

	dmuci_get_section_type((char *)package, (char *)section, &t);
	if (t && *t)
		return 0;
	dmuci_set_value((char *)package, (char *)section, "", (char *)type);
	t = NULL;
	dmuci_get_section_type((char *)package, (char *)section, &t);
	return (t && *t) ? 1 : -1;
}

int mtk_uci_set_persist(const char *package, const char *section, const char *option,
			const char *value)
{
	struct uci_context *c;
	struct uci_ptr ptr = {0};
	char buf[512];
	int rc = -1;

	if (!package || !section || !option || !value)
		return -1;
	dmuci_set_value((char *)package, (char *)section, (char *)option, (char *)value);
	if (snprintf(buf, sizeof(buf), "%s.%s.%s=%s", package, section, option, value) >= (int)sizeof(buf))
		return -1;
	c = uci_alloc_context();
	if (!c)
		return -1;
	if (uci_lookup_ptr(c, &ptr, buf, true) == UCI_OK &&
	    uci_set(c, &ptr) == UCI_OK && ptr.p &&
	    uci_commit(c, &ptr.p, false) == UCI_OK)
		rc = 0;
	uci_free_context(c);
	return rc;
}

int mtk_varstate_set(const char *package, const char *section, const char *option, const char *value)
{
	struct uci_ptr ptr = {0};

	if (!uci_varstate_ctx)
		return -1;
	uci_add_delta_path(uci_varstate_ctx, uci_varstate_ctx->savedir);
	uci_set_savedir(uci_varstate_ctx, VARSTATE_CONFIG);
	if (dmuci_lookup_ptr(uci_varstate_ctx, &ptr, (char *)package, (char *)section,
			     (char *)option, (char *)value))
		return -1;
	if (uci_set(uci_varstate_ctx, &ptr) != UCI_OK)
		return -1;
	if (ptr.p)
		uci_save(uci_varstate_ctx, ptr.p);
	return 0;
}

/*
 * $UCI_GET_DEFAULT reads the factory tree, not the running config, so it needs
 * its own uci context -- the engine's is bound to /etc/config.  Opened once and
 * kept: set_LanHostConfig_DHCPServerConfigurable() needs it for every "false".
 */
char *mtk_uci_default(const char *package, const char *section, const char *option)
{
	static struct uci_context *rom_ctx;
	struct uci_ptr ptr = {0};
	char buf[256];

	if (!package || !section || !option)
		return "";
	if (!rom_ctx) {
		rom_ctx = uci_alloc_context();
		if (!rom_ctx)
			return "";
		uci_set_confdir(rom_ctx, "/rom/etc/config");
	}
	if (snprintf(buf, sizeof(buf), "%s.%s.%s", package, section, option) >= (int)sizeof(buf))
		return "";
	if (uci_lookup_ptr(rom_ctx, &ptr, buf, true) != UCI_OK)
		return "";
	if (!ptr.o || !ptr.o->v.string)
		return "";
	return dmstrdup(ptr.o->v.string);
}

static struct uci_context *state_ctx(const char *dir)
{
	struct uci_context *c = uci_alloc_context();

	if (!c)
		return NULL;
	/* what "uci -P <dir>" does: the default savedir becomes a delta
	 * path, <dir> becomes the savedir */
	uci_add_delta_path(c, c->savedir);
	uci_set_savedir(c, dir);
	return c;
}

char *mtk_state(const char *dir, const char *package, const char *section,
		const char *option)
{
	struct uci_context *c;
	struct uci_ptr ptr = {0};
	char buf[256];
	char *v = "";

	if (!dir || !package || !section || !option)
		return "";
	if (snprintf(buf, sizeof(buf), "%s.%s.%s", package, section, option) >= (int)sizeof(buf))
		return "";
	c = state_ctx(dir);
	if (!c)
		return "";
	if (uci_lookup_ptr(c, &ptr, buf, true) == UCI_OK &&
	    (ptr.flags & UCI_LOOKUP_COMPLETE) && ptr.o &&
	    ptr.o->type == UCI_TYPE_STRING && ptr.o->v.string)
		v = dmstrdup(ptr.o->v.string);
	uci_free_context(c);
	return v;
}

int mtk_state_set(const char *dir, const char *package, const char *section,
		  const char *option, const char *value)
{
	struct uci_context *c;
	struct uci_ptr ptr = {0};
	char buf[1024];
	int rc = -1;

	if (!dir || !package || !section || !option || !value)
		return -1;
	if (snprintf(buf, sizeof(buf), "%s.%s.%s=%s", package, section, option, value) >= (int)sizeof(buf))
		return -1;
	c = state_ctx(dir);
	if (!c)
		return -1;
	if (uci_lookup_ptr(c, &ptr, buf, true) == UCI_OK &&
	    uci_set(c, &ptr) == UCI_OK && ptr.p &&
	    uci_save(c, ptr.p) == UCI_OK)
		rc = 0;
	uci_free_context(c);
	return rc;
}

int mtk_kill_cmdline(const char *pattern)
{
	DIR *d;
	struct dirent *de;
	pid_t self = getpid();
	int n = 0;

	if (!pattern || !*pattern)
		return 0;
	d = opendir("/proc");
	if (!d)
		return 0;
	while ((de = readdir(d)) != NULL) {
		char path[64], buf[1024];
		ssize_t len, i;
		pid_t pid;
		int fd;

		if (!isdigit((unsigned char)de->d_name[0]))
			continue;
		pid = (pid_t)atoi(de->d_name);
		if (pid <= 1 || pid == self)
			continue;
		snprintf(path, sizeof(path), "/proc/%s/cmdline", de->d_name);
		fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;
		len = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (len <= 0)
			continue;	/* kernel thread, or already gone */
		/* argv is NUL separated: join it with spaces, as pgrep -f sees it */
		for (i = 0; i < len; i++)
			if (buf[i] == '\0')
				buf[i] = ' ';
		buf[len] = '\0';
		if (strstr(buf, pattern) && kill(pid, SIGKILL) == 0)
			n++;
	}
	closedir(d);
	return n;
}

int mtk_netdev_exists(const char *name)
{
	if (!name || !*name || strlen(name) >= IFNAMSIZ)
		return 0;
	return if_nametoindex(name) != 0;
}

char *mtk_grep_o(const char *re, const char *s)
{
	regex_t rx;
	char *out = NULL, *line;
	size_t olen = 0;
	const char *p, *nl;

	if (!re || !s)
		return "";
	if (regcomp(&rx, re, REG_EXTENDED) != 0)
		return "";
	/* echo "$s": the lines of s, the last one included even when empty */
	for (p = s;; p = nl + 1) {
		regmatch_t m;
		size_t ll;
		char *q;
		int eflags = 0;

		nl = strchr(p, '\n');
		ll = nl ? (size_t)(nl - p) : strlen(p);
		line = dmmalloc(ll + 1);
		if (!line)
			break;
		memcpy(line, p, ll);
		line[ll] = '\0';
		for (q = line; regexec(&rx, q, 1, &m, eflags) == 0; eflags = REG_NOTBOL) {
			size_t n = (size_t)(m.rm_eo - m.rm_so);

			if (n == 0) {		/* grep -o prints no empty match */
				if (!q[m.rm_eo])
					break;
				q += m.rm_eo + 1;
				continue;
			}
			out = dmrealloc(out, olen + n + 2);
			if (!out)
				break;
			memcpy(out + olen, q + m.rm_so, n);
			olen += n;
			out[olen++] = '\n';
			out[olen] = '\0';
			q += m.rm_eo;
			if (!*q)
				break;
		}
		if (!nl)
			break;
	}
	regfree(&rx);
	if (!out)
		return "";
	while (olen && out[olen - 1] == '\n')
		out[--olen] = '\0';
	return out;
}

int mtk_ere_match(const char *re, const char *s)
{
	regex_t rx;
	int hit;

	if (!re || !s)
		return 0;
	if (regcomp(&rx, re, REG_EXTENDED | REG_NEWLINE | REG_NOSUB) != 0)
		return 0;
	hit = regexec(&rx, s, 0, NULL, 0) == 0;
	regfree(&rx);
	return hit;
}

int mtk_ipv4_parse(const char *s, unsigned int *out)
{
	unsigned int v = 0;
	int octet, digits, i;

	if (!s)
		return -1;
	for (i = 0; i < 4; i++) {
		octet = 0;
		digits = 0;
		while (*s >= '0' && *s <= '9') {
			octet = octet * 10 + (*s - '0');
			if (octet > 255)
				return -1;
			digits++;
			s++;
		}
		if (!digits || digits > 3)
			return -1;
		v = (v << 8) | (unsigned int)octet;
		if (i < 3) {
			if (*s != '.')
				return -1;
			s++;
		}
	}
	if (*s)
		return -1;
	if (out)
		*out = v;
	return 0;
}

char *mtk_ipv4_str(unsigned int v)
{
	char buf[16];

	snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
		 (v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
	return dmstrdup(buf);
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

int mtk_file_write(const char *path, const char *value)
{
	FILE *f;
	int rc;

	if (!path || !value)
		return -1;
	f = fopen(path, "w");
	if (!f)
		return -1;
	rc = fprintf(f, "%s\n", value) < 0 ? -1 : 0;
	if (fclose(f))
		rc = -1;
	return rc;
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

int mtk_run(char *const argv[])
{
	int status;
	pid_t pid;

	if (!argv || !argv[0])
		return -1;
	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		int fd, maxfd = (int)sysconf(_SC_OPEN_MAX);

		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, STDIN_FILENO);
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
		}
		/* the agent's sockets stay with the agent, as in mtk_exec() */
		if (maxfd < 3)
			maxfd = 1024;
		for (fd = 3; fd < maxfd; fd++)
			close(fd);
		execvp(argv[0], argv);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) != pid)
		return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
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

int mtk_apply_service_once(const char *cmd)
{
	char line[512];
	FILE *f;

	if (!cmd || !*cmd)
		return -1;
	f = fopen(APPLY_SERVICE_FILE, "r");
	if (f) {
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\r\n")] = '\0';
			if (strcmp(line, cmd) == 0) {
				fclose(f);
				return 0;
			}
		}
		fclose(f);
	}
	return mtk_apply_service(cmd);
}

/* size of the queue now: the mark a SetParameterValues batch starts from */
long mtk_apply_service_size(void)
{
	struct stat st;

	return stat(APPLY_SERVICE_FILE, &st) == 0 ? (long)st.st_size : 0;
}

/* forget what was queued after the mark: the batch faulted and was reverted */
void mtk_apply_service_truncate(long size)
{
	if (size <= 0) {
		remove(APPLY_SERVICE_FILE);
		return;
	}
	if (truncate(APPLY_SERVICE_FILE, (off_t)size) != 0 && errno != ENOENT)
		fprintf(stderr, "icwmp mtk: truncate %s: %s\n", APPLY_SERVICE_FILE, strerror(errno));
}

void mtk_run_apply_service(void)
{
	char line[512];
	FILE *f;

	f = fopen(APPLY_SERVICE_FILE, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char cmd[sizeof(line) + 48];
		char *argv[] = { "/bin/sh", "-c", cmd, NULL };

		line[strcspn(line, "\r\n")] = '\0';
		if (!line[0])
			continue;
		/* output discarded on purpose: these are init scripts, their
		 * logs belong in syslog, not in a CWMP reply.
		 *
		 * The shell detaches its own stdio BEFORE running the line.  Lines
		 * end in "&" (the diagnostics launchers, "easycwmpd restart &"):
		 * a background child would otherwise inherit the pipe mtk_exec()
		 * reads to EOF, and the end of the session would wait for a
		 * traceroute or a TR-143 download to finish -- while the launcher
		 * retries "ubus call tr069 inform" against this very agent. */
		snprintf(cmd, sizeof(cmd), "exec </dev/null >/dev/null 2>&1; %s", line);
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
