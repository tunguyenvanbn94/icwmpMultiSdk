/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Helpers shared by the C data model modules of the MTK/Airoha SDK
 *	(sdk/mtk/dm098/).  They exist so a ported object reads the way the
 *	easycwmp function it replaces read, without every module re-inventing
 *	/proc parsing or the apply-service queue.
 *
 *	Rule for every module in sdk/mtk/dm098/: same UCI option, same ubus
 *	call, same file as the shell function it replaces.  The product (WebUI,
 *	hal_gateway, ubusmon) writes those same options -- changing where a
 *	value lives is a product change, not a CWMP change.
 */
#ifndef __DMMTK_H
#define __DMMTK_H

#include "dmtr098.h"

/* Config ------------------------------------------------------------ */
/* Never NULL: an unset option reads as "". */
char *mtk_uci(char *package, char *section, char *option);
/* /var/state, what the shell calls $UCI_GET_VARSTATE / $UCI_SET_VARSTATE:
 * runtime values that must not survive a reboot. */
char *mtk_varstate(char *package, char *section, char *option);
int mtk_varstate_set(char *package, char *section, char *option, char *value);

/* System ------------------------------------------------------------ */
/* First line of a file, trimmed, dm-allocated, "" when unreadable. */
char *mtk_file_line(const char *path);
/* Write one line to a /proc or /sys control file, what the shell wrote with
 * "echo <value> > <path>".  Returns 0 on success, -1 when the file cannot be
 * written (the caller keeps going: the shell ignored that too). */
int mtk_file_write(const char *path, const char *value);
/* A /proc/meminfo row in kB, -1 when absent. */
long mtk_meminfo_kb(const char *key);
/* /proc/uptime, whole seconds. */
long mtk_uptime(void);
/* stdout of argv (NULL terminated), dm-allocated, "" on failure.  Capped at
 * 64 kB: these are data model values, not logs. */
char *mtk_exec(char *const argv[]);
/* First line of it, trimmed. */
char *mtk_exec_line(char *const argv[]);

/* Delayed work ------------------------------------------------------ */
/* Queue a shell command for the end of the session, exactly what
 * common_execute_command_in_apply_service() does: appended to
 * /tmp/.easycwmp_apply_service, run by sdk/mtk/compat/icwmp_dm.sh
 * "apply_service" from dm_platform_restart_services(). */
int mtk_apply_service(const char *cmd);
/* Run everything queued there and empty the file.  Called at the end of a
 * session by dm_platform_restart_services() when the shell fallback is not
 * compiled in (--disable-dm-script-compat). */
void mtk_run_apply_service(void);

/* Factory defaults, what the shell calls $UCI_GET_DEFAULT
 * ("uci -q -c /rom/etc/config get").  Never NULL. */
char *mtk_uci_default(char *package, char *section, char *option);

/* IPv4 --------------------------------------------------------------- */
/* Dotted quad -> host order integer, mirroring is_valid_ipv4 + ipstr2int
 * of functions/common: every octet decimal and <= 255, exactly four of them.
 * Returns 0 on success, -1 when the string is not an IPv4 address. */
int mtk_ipv4_parse(const char *s, unsigned int *out);
/* Back to dotted quad, dm-allocated (int2ipstr). */
char *mtk_ipv4_str(unsigned int v);

/* Value shaping ----------------------------------------------------- */
/* "1"/"on"/"true"/"yes"/"enabled" -> true, anything else false. */
int mtk_bool(const char *v);
/* CWMP boolean spelling of a UCI flag, and back. */
char *mtk_bool_str(int on);
/* Accepts the CWMP spellings, returns 0/1, -1 when not a boolean. */
int mtk_parse_bool(const char *v);

#endif
