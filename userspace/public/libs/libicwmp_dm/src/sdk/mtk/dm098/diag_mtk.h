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
/* <x>_set_diagnostic_state Requested: stop, Requested, queue the launcher */
void diag_request(const struct diag_store *d);

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

#endif
