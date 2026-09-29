/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Shared diagnostics machinery.  See diag_mtk.h.
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "dmmem.h"
#include "dmmtk.h"
#include "diag_mtk.h"

#define DIAG_PKG	"easycwmp"
#define DIAG_SEC	"@local[0]"

const struct diag_store diag_ipping     = { "/var/state",            "ipping_launch" };
const struct diag_store diag_traceroute = { "/var/state/traceroute", "traceroute_launch" };
const struct diag_store diag_nslookup   = { "/var/state/nslookup",   "nslookup_launch" };
const struct diag_store diag_dns        = { "/var/state/dnsDiagnostics", "dnsDiagnostics_launch" };
const struct diag_store diag_download   = { "/var/state/downloadDiag", "DownloadDiagnostics_launch" };
const struct diag_store diag_upload     = { "/var/state/uploadDiag",   "UploadDiagnostics_launch" };

char *diag_get(const struct diag_store *d, const char *option, const char *def)
{
	char *v = mtk_state(d->dir, DIAG_PKG, DIAG_SEC, option);

	if (!*v && def)
		return (char *)def;
	return v;
}

int diag_set(const struct diag_store *d, const char *option, const char *value)
{
	return mtk_state_set(d->dir, DIAG_PKG, DIAG_SEC, option, value);
}

void diag_stop(const struct diag_store *d)
{
	if (mtk_kill_cmdline(d->launcher) > 0)
		diag_set(d, "DiagnosticsState", "None");
}

void diag_store_value(const struct diag_store *d, const char *option, const char *value)
{
	diag_stop(d);
	if (strcmp(diag_get(d, "DiagnosticsState", NULL), "Requested") != 0)
		diag_set(d, "DiagnosticsState", "None");
	diag_set(d, option, value);
}

void diag_store_value_nostop(const struct diag_store *d, const char *option, const char *value)
{
	if (strcmp(diag_get(d, "DiagnosticsState", NULL), "Requested") != 0)
		diag_set(d, "DiagnosticsState", "None");
	diag_set(d, option, value);
}

char *diag_result_get(const char *dir, int idx, const char *option)
{
	char sec[32];

	snprintf(sec, sizeof(sec), "@local[%d]", idx);
	return mtk_state(dir, DIAG_PKG, sec, option);
}

int diag_ifconfig_prints(const char *v)
{
	if (!*v || strcmp(v, "-a") == 0)
		return 1;
	if (strpbrk(v, " \t\n"))
		return 0;
	return mtk_netdev_exists(v);
}

void diag_request(const struct diag_store *d)
{
	char cmd[256];

	diag_stop(d);
	diag_set(d, "DiagnosticsState", "Requested");
	snprintf(cmd, sizeof(cmd), "/bin/sh %s/%s run &", DIAG_FUNCTION_PATH, d->launcher);
	mtk_apply_service(cmd);
}

int diag_check_uint(const char *v, long long min, long long max)
{
	const char *p;
	long long n;

	if (!v || !*v)
		return -1;
	for (p = v; *p; p++)
		if (*p < '0' || *p > '9')
			return -1;
	errno = 0;
	n = strtoll(v, NULL, 10);
	if (errno == ERANGE)
		return 0;	/* busybox "[": out of range -> test false -> stored */
	if (n < min)
		return -1;
	if (max >= 0 && n > max)
		return -1;
	return 0;
}

/* the three patterns of the shell, with "\/" written as "/" */
#define RE_IPV4_ANYWHERE \
	"(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\." \
	"(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)"
#define RE_HOSTNAME \
	"^(http(s)?://)?[a-zA-Z0-9][-a-zA-Z0-9]{0,62}(\\.[a-zA-Z0-9][-a-zA-Z0-9]{0,62})+(:[0-9]{1,5})?$"
#define RE_IPV6 \
	"^([0-9a-fA-F]{0,4}:){1,7}[0-9a-fA-F]{0,4}$"

int diag_host_valid(const struct diag_store *d, const char *host)
{
	const char *version = diag_get(d, "ProtocolVersion", "IPv4");

	if (strcmp(version, "IPv4") == 0) {
		if (!mtk_ere_match(RE_IPV4_ANYWHERE, host) &&
		    !mtk_ere_match(RE_HOSTNAME, host))
			return 0;
	}
	if (strcmp(version, "IPv6") == 0) {
		if (!mtk_ere_match(RE_IPV6, host) || strstr(host, ":::")) {
			if (!mtk_ere_match(RE_HOSTNAME, host))
				return 0;
		}
	}
	return 1;
}

static void diag_queue(const struct diag_store *d, const char *verb)
{
	char cmd[256];

	snprintf(cmd, sizeof(cmd), "/bin/sh %s/%s %s", DIAG_FUNCTION_PATH, d->launcher, verb);
	mtk_apply_service(cmd);
}

void diag_stop_queued(const struct diag_store *d)
{
	if (strcmp(diag_get(d, "DiagnosticsState", NULL), "Requested") == 0)
		diag_queue(d, "stop");
}

void diag_queue_run(const struct diag_store *d)
{
	diag_queue(d, "run &");
}

/* The shell's pattern, as grep receives it once the double quotes are
 * undone: "\\[" -> "\[", "\\]" -> "\]", "\+" stays.  Inside a bracket
 * expression a backslash is an ordinary character, so the two brackets
 * accept "\" as well -- kept. */
#define RE_TR143_URL \
	"(http|ftp)://[-A-Za-z0-9\\[\\+&@#/%?=~_|! /:/,.;]*(\\])*[-A-Za-z0-9\\+&@#/%=~_|]*"

int diag_url_valid(const char *url)
{
	return strcmp(mtk_grep_o(RE_TR143_URL, url), url) == 0;
}

