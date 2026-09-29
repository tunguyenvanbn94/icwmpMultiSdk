/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	What every diagnostics object of the easycwmp shell shares
 *	(functions/tr098/<x>_diagnostic, functions/tr143/<x>_diagnostic).
 *
 *	A shell diagnostic is a thin front for a launcher script:
 *
 *	  - every parameter lives in easycwmp.@local[0] of a PRIVATE uci
 *	    savedir, "uci -P <dir>", never committed, gone at reboot;
 *	  - every writable setter ends the same way: kill the launcher if it
 *	    runs, drop DiagnosticsState back to None unless a test is pending,
 *	    store the value;
 *	  - DiagnosticsState=Requested queues "/bin/sh <functions>/<launcher>
 *	    run &" on the apply-service list, so the test starts after the
 *	    session closes;
 *	  - the launcher does the measuring, writes the results into the same
 *	    savedir and reports with
 *	      ubus call tr069 inform '{"event":"8 DIAGNOSTICS COMPLETE"}'
 *	    which icwmpd serves (ubus.c cwmp_handle_inform ->
 *	    cwmp_get_int_event_code: first char '8' ->
 *	    EVENT_IDX_8DIAGNOSTICS_COMPLETE).
 *
 *	The launchers stay shell scripts, installed where they always were.
 *	They are the measuring engine, not the data model, and the ACS-facing
 *	contract (states, error strings, result fields) is theirs.
 */
#ifndef __DIAG_MTK_H
#define __DIAG_MTK_H

/* $FUNCTION_PATH of easycwmp.sh, where the launchers are installed */
#define DIAG_FUNCTION_PATH	"/usr/share/easycwmp/functions"

struct diag_store {
	const char *dir;	/* uci -P <dir> */
	const char *launcher;	/* <DIAG_FUNCTION_PATH>/<launcher>, also the
				 * pattern its stop function greps for */
};

/* one per shell file; the directory is the file's own UCI_*_VARSTATE* */
extern const struct diag_store diag_ipping;		/* -P /var/state */
extern const struct diag_store diag_traceroute;	/* -P /var/state/traceroute */
extern const struct diag_store diag_nslookup;		/* -P /var/state/nslookup */
extern const struct diag_store diag_dns;		/* -P /var/state/dnsDiagnostics */
extern const struct diag_store diag_download;		/* -P /var/state/downloadDiag */
extern const struct diag_store diag_upload;		/* -P /var/state/uploadDiag */

/* where the launchers of the two lookup diagnostics put Result.{i}: the
 * launcher "uci -P <dir> add"s one "local" section per answer, so Result.<n>
 * is easycwmp.@local[<n>] of that store (@local[0] is the section already in
 * /etc/config/easycwmp).  "uci -P ... commit" in the launcher is a no-op --
 * uci-2020-10-06 cli.c: -P sets CLI_FLAG_NOCOMMIT -- so nothing reaches
 * flash and the store is emptied at the start of every run. */
#define DIAG_NSLOOKUP_RESULT_DIR	"/var/state/nslookup_result"
#define DIAG_DNS_RESULT_DIR		"/var/state/dnsDiagnostics_result"

/* <x>_get: "${val:-def}" of easycwmp.@local[0].<option> */
char *diag_get(const struct diag_store *d, const char *option, const char *def);
/* plain $UCI_SET_VARSTATE* of easycwmp.@local[0].<option> */
int diag_set(const struct diag_store *d, const char *option, const char *value);

/* <x>_stop_diagnostic: kill -9 the running launcher; when one was found,
 * DiagnosticsState=None */
void diag_stop(const struct diag_store *d);
/* the tail every <x>_set_* shares:
 *	<x>_stop_diagnostic
 *	[ DiagnosticsState != Requested ] && DiagnosticsState=None
 *	<option>=<value> */
void diag_store_value(const struct diag_store *d, const char *option, const char *value);
/* the same tail WITHOUT the stop: nslookup_set / dnslookup_set (Timeout,
 * NumberOfRepetitions) leave a running launcher alone */
void diag_store_value_nostop(const struct diag_store *d, const char *option, const char *value);
/* <x>_get_result: "${val:-}" of easycwmp.@local[<idx>].<option> in <dir> */
char *diag_result_get(const char *dir, int idx, const char *option);
/* <x>_set_diagnostic_state Requested: stop, Requested, queue the launcher */
void diag_request(const struct diag_store *d);
/* TR-143 (downloadDiag_stop_diagnostic): no kill from the data model --
 * when DiagnosticsState is Requested, queue "<launcher> stop" on the
 * apply-service list, and do nothing else.  The launcher's stop kills the
 * running test and resets the results; the ORDER of the queue therefore
 * decides what runs, and it is kept by calling this exactly where the shell
 * did. */
void diag_stop_queued(const struct diag_store *d);
/* queue "<launcher> run &" -- the tail of the TR-143 Requested setters */
void diag_queue_run(const struct diag_store *d);

/* "case $v in (*[^0-9]*|'') fault", then the "[ $v -lt MIN -o $v -gt MAX ]"
 * range test.  Returns 0 when the value is accepted.
 *
 * A number too large for busybox test is accepted: "[" prints "out of range"
 * and returns 2, which "if" reads as false, so the range check never fires
 * and the setter goes on to store the value.  Kept, so an ACS sees the same
 * answer as before.  max < 0 means no upper bound. */
int diag_check_uint(const char *v, long long min, long long max);

/* <x>_set_host: the host check of ipping_diagnostic and
 * traceroute_diagnostic, verbatim.  Depends on the ProtocolVersion stored
 * in the same savedir (default IPv4):
 *	IPv4: a dotted quad ANYWHERE in the value, or a host name
 *	IPv6: an IPv6 literal without ":::", or a host name
 *	anything else: not checked */
int diag_host_valid(const struct diag_store *d, const char *host);

/* <x>_set_interface of traceroute, nslookup and dns: accepted when
 * "$(ifconfig $2)" -- unquoted -- prints something:
 *	""     ifconfig alone lists the up interfaces      -> accepted
 *	"-a"   lists all of them                           -> accepted
 *	"a b"  two words: ifconfig tries to CONFIGURE a    -> rejected
 * The third case also ran that configuration ("eth0 down" took eth0 down);
 * the rejection is kept, the side effect is not. */
int diag_ifconfig_prints(const char *v);

/* <x>Diag_set_DownloadURL / _UploadURL: the value must equal what
 *	echo "$url" | grep -E -o "(http|ftp)://[...]*(\])*[...]*"
 * prints -- a whole-string match, http or ftp only (not https). */
int diag_url_valid(const char *url);

#endif
