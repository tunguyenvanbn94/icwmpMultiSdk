/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The input contract every SetParameterValues of the easycwmp product went
 *	through BEFORE any setter: common_set_value_check_param() of
 *	functions/common/common.
 *
 *	  1. is_safe_input: a value is refused (9007) when
 *	       - no line of it is plain printable ASCII, or
 *	       - any line is empty or only spaces -- so "" is refused, or
 *	       - any line holds one of  # ; & | < > ` $ \ ' "
 *	     This is the product's guard against command injection: the values
 *	     land in UCI and from there in shell scripts (hni_wan_reload.sh,
 *	     firewall hooks, the diagnostics launchers).
 *	  2. a check by the parameter's SHELL type (shelltypes_mtk.h, generated
 *	     from the coverage matrix):
 *	       xsd:unsignedInt  [ "$v" -gt -1 ] && [ "$v" -lt 4294967296 ]
 *	       xsd:int          [ "$v" -gt -2147483649 ] && [ "$v" -lt 2147483648 ]
 *	       xsd:boolean      true, 1, false, 0
 *	       xsd:IPv4Address  a dotted quad somewhere in the value (grep -o)
 *	       xsd:IPv6Address  is_valid_ipv6
 *	       xsd:dateTime     NOT emulated (busybox "date -d"); the one writable
 *	                        dateTime, ManagementServer.PeriodicInformTime, is
 *	                        the engine's own and checks itself
 *	     numbers read the way busybox "[" reads them (strtoll base 10,
 *	     blanks around allowed, anything else or out of range fails).
 *	     Any other type string -- "", xsd:string, and the misspelt
 *	     "xsd:Int" / "xsd:unsignedint" -- is not checked, as in the shell.
 *
 *	The C modules of sdk/mtk/dm098 port the SETTERS; this file ports the
 *	layer in front of them, once, for every native path.  Paths still served
 *	by the shell bridge go through the shell's own copy of it.
 *
 *	Applied to every native path, including the parameters icwmp added that
 *	the product never had (ManagementServer extras): the same guard, for the
 *	same reason.
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fnmatch.h>

#include "dmtr098.h"
#include "dmmtk.h"
#include "shelltypes_mtk.h"

/* ------------------------------------------------------------------ */
/* is_safe_input                                                       */
/* ------------------------------------------------------------------ */

/* the lines grep sees in `echo "$v"`: every '\n'-separated piece of v,
 * the last one included even when empty */
typedef int (*line_fn)(const char *line, size_t len);

static int any_line(const char *v, line_fn fn)
{
	const char *p = v, *nl;

	for (;;) {
		nl = strchr(p, '\n');
		if (fn(p, nl ? (size_t)(nl - p) : strlen(p)))
			return 1;
		if (!nl)
			return 0;
		p = nl + 1;
	}
}

static int line_printable(const char *l, size_t n)	/* ^[ -~]*$ */
{
	size_t i;

	for (i = 0; i < n; i++)
		if ((unsigned char)l[i] < 0x20 || (unsigned char)l[i] > 0x7e)
			return 0;
	return 1;
}

static int line_blank(const char *l, size_t n)		/* ^ *$ */
{
	size_t i;

	for (i = 0; i < n; i++)
		if (l[i] != ' ')
			return 0;
	return 1;
}

static int line_unsafe(const char *l, size_t n)		/* [#;&|<>`$\'"] */
{
	size_t i;

	for (i = 0; i < n; i++)
		if (strchr("#;&|<>`$\\'\"", l[i]) && l[i])
			return 1;
	return 0;
}

int mtk_shell_safe_input(const char *v)
{
	if (!v)
		return 0;
	if (!any_line(v, line_printable))
		return 0;
	if (any_line(v, line_blank))
		return 0;
	if (any_line(v, line_unsafe))
		return 0;
	return 1;
}

/* ------------------------------------------------------------------ */
/* type checks                                                         */
/* ------------------------------------------------------------------ */

/* getn() of busybox test: strtoll, blanks before and after, else error */
static int bb_getn(const char *v, long long *out)
{
	char *end;

	errno = 0;
	*out = strtoll(v, &end, 10);
	if (end == v || errno)
		return -1;
	while (isspace((unsigned char)*end))
		end++;
	return *end ? -1 : 0;
}

static int check_between(const char *v, long long gt, long long lt)
{
	long long n;

	return bb_getn(v, &n) == 0 && n > gt && n < lt;
}

#define RE_QUAD	"(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\." \
		"(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)"

/* is_valid_ipv4 */
static int shell_ipv4(const char *ip)
{
	char buf[64], *o[4], *p;
	int i;

	if (fnmatch("[0-9]*.[0-9]*.[0-9]*.[0-9]*", ip, 0) != 0)
		return 0;
	if (strlen(ip) >= sizeof(buf))
		return 0;
	strcpy(buf, ip);
	/* IFS='.' read o1 o2 o3 o4: the last one takes the rest */
	p = buf;
	for (i = 0; i < 3; i++) {
		o[i] = p;
		p = strchr(p, '.');
		*p++ = '\0';		/* the pattern guarantees three dots */
	}
	o[3] = p;
	for (i = 0; i < 4; i++) {
		long long n;

		if (!*o[i] || strspn(o[i], "0123456789") != strlen(o[i]))
			return 0;
		if (bb_getn(o[i], &n) != 0 || n < 0 || n > 255)
			return 0;
	}
	return 1;
}

static int is_hex(const char *s, size_t n)
{
	size_t i;

	if (!n)
		return 0;
	for (i = 0; i < n; i++)
		if (!isxdigit((unsigned char)s[i]))
			return 0;
	return 1;
}

/* is_numeric: not "", digits only, no leading 0 followed by a digit */
static int is_numeric(const char *s)
{
	if (!*s || strspn(s, "0123456789") != strlen(s))
		return 0;
	return !(s[0] == '0' && s[1]);
}

/* is_valid_ipv6 */
static int shell_ipv6(const char *in)
{
	char ip6[128], *slash;
	const char *p, *q;
	int count = 0, dc = 0;

	if (!*in)
		return 0;
	if (in[0] == ':' && in[1] != ':')
		return 0;		/* [[ :* && != ::* ]] */
	if (strlen(in) >= sizeof(ip6))
		return 0;
	strcpy(ip6, in);
	slash = strchr(ip6, '/');
	if (slash) {
		const char *mask = strrchr(in, '/') + 1;	/* after the LAST '/' */
		long long m;

		*slash = '\0';				/* before the FIRST '/' */
		if (!is_numeric(mask) || bb_getn(mask, &m) != 0 || m > 128)
			return 0;
	}
	for (p = ip6; (q = strstr(p, "::")) != NULL; p = q + 2)	/* grep -o '::' | wc -l */
		dc++;
	if (dc > 1 || strstr(ip6, ":::"))
		return 0;
	for (p = ip6; *p; p = *q ? q + 1 : q) {		/* IFS=":" */
		size_t n;
		char part[64];

		q = strchr(p, ':');
		if (!q)
			q = p + strlen(p);
		n = (size_t)(q - p);
		if (!n)
			continue;
		if (memchr(p, '.', n)) {
			if (n >= sizeof(part))
				return 0;
			memcpy(part, p, n);
			part[n] = '\0';
			if (!shell_ipv4(part))
				return 0;
			count += 2;
		} else {
			if (n > 4 || !is_hex(p, n))
				return 0;
			count += 1;
		}
	}
	if (strstr(ip6, "::"))
		return count <= 7;
	return count == 8;
}

static int shell_type_ok(const char *type, const char *v)
{
	if (strcmp(type, "xsd:unsignedInt") == 0)
		return check_between(v, -1, 4294967296LL);
	if (strcmp(type, "xsd:int") == 0)
		return check_between(v, -2147483649LL, 2147483648LL);
	if (strcmp(type, "xsd:boolean") == 0)
		return !strcmp(v, "true") || !strcmp(v, "1") ||
		       !strcmp(v, "false") || !strcmp(v, "0");
	if (strcmp(type, "xsd:IPv4Address") == 0)
		return mtk_ere_match(RE_QUAD, v);
	if (strcmp(type, "xsd:IPv6Address") == 0)
		return shell_ipv6(v);
	return 1;	/* xsd:dateTime: see the header */
}

/* ------------------------------------------------------------------ */
/* entry                                                               */
/* ------------------------------------------------------------------ */

/* instance numbers -> {i}, the spelling of shelltypes_mtk.h */
static void norm_path(const char *in, char *out, size_t sz)
{
	const char *p = in;
	size_t o = 0;

	while (*p && o + 4 < sz) {
		const char *e = strchr(p, '.');
		size_t n = e ? (size_t)(e - p) : strlen(p);

		if (n && strspn(p, "0123456789") >= n) {
			memcpy(out + o, "{i}", 3);
			o += 3;
		} else {
			if (o + n + 1 >= sz)
				break;
			memcpy(out + o, p, n);
			o += n;
		}
		if (!e)
			break;
		out[o++] = '.';
		p = e + 1;
	}
	out[o] = '\0';
}

int mtk_input_contract(const char *path, const char *value)
{
	char np[512];
	int i;

	if (!value)
		value = "";
	if (!mtk_shell_safe_input(value))
		return FAULT_9007;
	norm_path(path ? path : "", np, sizeof(np));
	for (i = 0; mtk_shell_types[i].path; i++) {
		if (strcmp(mtk_shell_types[i].path, np) == 0)
			return shell_type_ok(mtk_shell_types[i].type, value) ? 0 : FAULT_9007;
	}
	return 0;
}
