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
/* ensure_<x>_section() of the shell: "uci get <pkg>.<sec>", else
 * "uci set <pkg>.<sec>=<type>".  1 when it was added, 0 when it was there,
 * -1 when it cannot be added (no such package: E_INTERNAL_ERROR there). */
int mtk_uci_ensure_section(const char *package, const char *section, const char *type);
/* "$UCI_SET <pkg>.<sec>.<opt>=<v>; $UCI_COMMIT <pkg>" in the middle of a
 * walk: the shell numbered instances that way while answering a GET.  The
 * value goes into this session's copy (later getters of the same RPC see it)
 * and is committed at once through a context of its own, so a GET keeps the
 * number and the session's other pending changes are not committed with it.
 * 0, or -1 when the commit failed. */
int mtk_uci_set_persist(const char *package, const char *section, const char *option,
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
/* Exit status of argv, its output discarded: the "$?" of the shell's
 * "cmd >/dev/null 2>&1".  -1 when it could not run or did not exit. */
int mtk_run(char *const argv[]);

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
/* is_valid_ipv4 / is_valid_ipv6 of the shell: 1 when the value passes */
int mtk_shell_ipv4(const char *v);
int mtk_shell_ipv6(const char *v);
/* an integer operand of busybox "test": strtoll, blanks around allowed.
 * 0 and *out set, or -1 when "[ $v -lt N ]" would fail with an error */
int mtk_shell_getn(const char *v, long long *out);

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
/* The TR-181 branches the product grafted into its TR-098 tree sit under
 * InternetGatewayDevice.Device.; with cwmp.cpe.datamodel=tr181 they are at
 * the root.  References between them (Interface = ...IP.Interface.<n>) follow
 * the root of the running context: "Device." or "InternetGatewayDevice.Device.",
 * and the same with "IP.Interface." appended. */
const char *mtk_dev_prefix(void);
const char *mtk_ipif_prefix(void);
/* Accepts the CWMP spellings, returns 0/1, -1 when not a boolean. */
int mtk_parse_bool(const char *v);

/*
 * TR-181 Alias of an instance (T7 S3), refparam being the leaf's full path
 * (Device.Ethernet.Interface.1.Alias): what an ACS set, kept in
 * cwmp.tr181_alias.<Ethernet_Interface_1>, else dflt -- the CPE's own value,
 * "cpe-..." (TR-069 Alias rules).  A set takes 1..64 letters, digits, '_'
 * and '-', a letter first, not "cpe-...", unique among the instances of the
 * table; the value the instance has already is taken.
 */
char *mtk_alias181_get(const char *refparam, const char *dflt);
int mtk_alias181_set(const char *refparam, const char *dflt, char *value, int action);

/*
 * A TR-181 leaf the standard makes writable but the product cannot change
 * (a fixed reference, a constant, a property of the hardware): the value it
 * reads now is taken (nothing to do), any other is 9007 -- the CPE refusing
 * a value it does not support.  _BOOL compares as booleans ("1" = "true").
 */
#define MTK_SET_SAME(name, getter)						\
static int set_same_##name(char *refparam, struct dmctx *ctx, void *data,	\
			   char *instance, char *value, int action)		\
{										\
	char *cur = NULL;							\
										\
	if (getter(refparam, ctx, data, instance, &cur) != 0 || !cur ||		\
	    strcmp(cur, value ? value : "") != 0)				\
		return FAULT_9007;						\
	return 0;								\
}

#define MTK_SET_SAME_BOOL(name, getter)						\
static int set_same_##name(char *refparam, struct dmctx *ctx, void *data,	\
			   char *instance, char *value, int action)		\
{										\
	char *cur = NULL;							\
	int b = mtk_parse_bool(value);						\
										\
	if (b < 0 || getter(refparam, ctx, data, instance, &cur) != 0 || !cur ||	\
	    mtk_parse_bool(cur) != b)						\
		return FAULT_9007;						\
	return 0;								\
}

#endif
