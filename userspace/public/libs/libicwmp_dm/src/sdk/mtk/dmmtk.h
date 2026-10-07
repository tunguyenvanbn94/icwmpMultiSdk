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
char *mtk_uci(const char *package, const char *section, const char *option);
/* /var/state, what the shell calls $UCI_GET_VARSTATE / $UCI_SET_VARSTATE:
 * runtime values that must not survive a reboot. */
char *mtk_varstate(const char *package, const char *section, const char *option);
int mtk_varstate_set(const char *package, const char *section, const char *option,
                     const char *value);

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
/* Same, unless that exact line is already queued: one service reload per
 * session however many leaves of the service an RPC sets. */
int mtk_apply_service_once(const char *cmd);
/* Run everything queued there and empty the file.  Called at the end of a
 * session by dm_platform_restart_services() when the shell fallback is not
 * compiled in (--disable-dm-script-compat). */
void mtk_run_apply_service(void);
/* The queue size before a SetParameterValues batch writes anything, and the
 * cut back to it when the batch faults (dm_platform_revert): commands a
 * reverted batch queued are not run. */
long mtk_apply_service_size(void);
void mtk_apply_service_truncate(long size);

/* Factory defaults, what the shell calls $UCI_GET_DEFAULT
 * ("uci -q -c /rom/etc/config get").  Never NULL. */
char *mtk_uci_default(const char *package, const char *section, const char *option);

/* "uci -P <dir> get/set": runtime state kept in a savedir of its own
 * (/var/state/traceroute, /var/state/nslookup, ...).  Each diagnostic of the
 * shell has its own <dir>, so DiagnosticsState of one never reads as the
 * DiagnosticsState of another.  A fresh uci context per call: the engine's
 * varstate context already carries /var/state as a delta path and would leak
 * those deltas in.  Getter never NULL, "" when unset. */
char *mtk_state(const char *dir, const char *package, const char *section,
		const char *option);
int mtk_state_set(const char *dir, const char *package, const char *section,
		  const char *option, const char *value);

/* Processes ---------------------------------------------------------- */
/* "pgrep -f <pattern> | xargs kill -9": every process whose command line
 * contains pattern, this one excepted.  Returns how many were signalled. */
int mtk_kill_cmdline(const char *pattern);
/* "ifconfig <name>" succeeds: the network device exists, up or down. */
int mtk_netdev_exists(const char *name);
/* "echo <s> | grep -E -q <re>": POSIX ERE, ^ and $ bound to each line like
 * grep does.  1 on a match, 0 otherwise (a bad pattern never matches). */
int mtk_ere_match(const char *re, const char *s);
/* "$(echo "$s" | grep -E -o <re>)": every non-empty match of every line,
 * joined with newlines, trailing newlines stripped the way command
 * substitution strips them.  dm-allocated, "" when nothing matches or the
 * pattern does not compile. */
char *mtk_grep_o(const char *re, const char *s);

/* Input contract --------------------------------------------------- */
/* common_set_value_check_param() of the shell, in front of every native
 * setter: is_safe_input + the check of the parameter's shell type
 * (input_contract_mtk.c, shelltypes_mtk.h).  0, or FAULT_9007. */
int mtk_input_contract(const char *path, const char *value);
/* is_safe_input alone: 1 when the value passes */
int mtk_shell_safe_input(const char *v);
/* is_valid_domain || is_valid_ip of the shell: 1 when the value passes */
int mtk_shell_valid_host(const char *v);

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
