/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	TR-143 throughput diagnostics:
 *	  InternetGatewayDevice.DownloadDiagnostics.   13 parameters
 *	  InternetGatewayDevice.UploadDiagnostics.     13 parameters
 *
 *	Ported from functions/tr143/download_diagnostic and upload_diagnostic.
 *	The transfers stay in functions/common/DownloadDiagnostics_launch and
 *	UploadDiagnostics_launch; see diag_mtk.h.
 *
 *	Stores: -P /var/state/downloadDiag and -P /var/state/uploadDiag.
 *
 *	HOW TR-143 STOPS, unlike every other diagnostic: the data model never
 *	kills.  downloadDiag_stop_diagnostic() queues "<launcher> stop" on the
 *	apply-service list when DiagnosticsState is Requested, and the setter
 *	goes on.  The launcher's stop kills every running "DownloadDiagnostics"
 *	process and resets state and results.  So the ORDER of the queue decides
 *	what happens after the session -- for example "DiagnosticsState=Requested"
 *	followed by "DownloadURL=..." in the same SetParameterValues queues
 *	"run &" then "stop", and the test that was just requested is killed as
 *	soon as it starts.  Kept: the calls below sit exactly where the shell's
 *	were, and icwmp applies the parameters of an SPV in their order.
 *
 *	VENDOR BUG KEPT: uploadDiag_stop_diagnostic() reads the state with
 *	$UCI_SET_VARSTATE_UPLOAD instead of the getter.  "uci set x.y.z" with no
 *	value fails (uci-2020-10-06 list.c:702 UCI_ASSERT(ptr->value)), -q hides
 *	the error, the output is empty, and "" is never "Requested".  So the
 *	UploadDiagnostics setters NEVER queue a stop: changing UploadURL while
 *	an upload runs leaves it running.
 *
 *	Other quirks kept:
 *	  - DownloadURL / UploadURL: http:// or ftp:// only (no https), the value
 *	    must equal what grep -o prints (diag_url_valid).  The input contract
 *	    in front of it (input_contract_mtk.c, is_safe_input) refuses "" and
 *	    any of # ; & | < > ` $ \ ' " first -- so a URL with a query string
 *	    ("?a=1&b=2") or a fragment was never accepted on the product;
 *	  - Interface: device name, "$(ifconfig $2)" rule, reads "" when unset
 *	    (no "default" here, unlike the other diagnostics);
 *	  - EthernetPriority lives in option EthPriority;
 *	  - TestFileLength: digits only in the setter, bounded to
 *	    0..4294967295 by the contract's xsd:unsignedInt check;
 *	  - ROMTime/BOMTime/EOMTime/TCPOpen*Time are strings, not dateTime,
 *	    reading "0000-00-00T00:00:00.000000" before the first run.
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dmuci.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "diag_mtk.h"

#define DLD	(&diag_download)
#define ULD	(&diag_upload)

#define TR143_ZERO_TIME	"0000-00-00T00:00:00.000000"

/* <x>_stop_diagnostic of each object: download queues, upload never does */
static void tr143_stop(const struct diag_store *d)
{
	if (d == DLD)
		diag_stop_queued(d);
}

/* the tail of every writable setter: stop, None unless Requested, store */
static void tr143_store(const struct diag_store *d, const char *option, const char *value)
{
	tr143_stop(d);
	diag_store_value_nostop(d, option, value);
}

static int tr143_set_state(const struct diag_store *d, char *value, int action)
{
	if (strcmp(value, "Requested") != 0)
		return FAULT_9007;
	if (action == VALUESET) {
		tr143_stop(d);
		diag_set(d, "DiagnosticsState", "Requested");
		diag_queue_run(d);
	}
	return 0;
}

static int tr143_set_interface(const struct diag_store *d, char *value, int action)
{
	if (!diag_ifconfig_prints(value))
		return FAULT_9007;
	if (action == VALUESET)
		tr143_store(d, "Interface", value);
	return 0;
}

static int tr143_set_url(const struct diag_store *d, const char *option, char *value, int action)
{
	if (!diag_url_valid(value))
		return FAULT_9007;
	if (action == VALUESET)
		tr143_store(d, option, value);
	return 0;
}

static int tr143_set_range(const struct diag_store *d, const char *option, long long lo,
			   long long hi, char *value, int action)
{
	if (diag_check_uint(value, lo, hi) != 0)
		return FAULT_9007;
	if (action == VALUESET)
		tr143_store(d, option, value);
	return 0;
}

/* ------------------------------------------------------------------ */
/* getters, one pair of tables per object                              */
/* ------------------------------------------------------------------ */

#define TR143_GET(fn, store, option, def)					\
static int fn(char *refparam, struct dmctx *ctx, void *data,			\
	      char *instance, char **value)					\
{										\
	*value = diag_get(store, option, def);					\
	return 0;								\
}

TR143_GET(get_dld_state,      DLD, "DiagnosticsState",    "None")
TR143_GET(get_dld_interface,  DLD, "Interface",           NULL)
TR143_GET(get_dld_transports, DLD, "DownloadTransports",  "HTTP,FTP")
TR143_GET(get_dld_dscp,       DLD, "DSCP",                "0")
TR143_GET(get_dld_url,        DLD, "DownloadURL",         NULL)
TR143_GET(get_dld_ethprio,    DLD, "EthPriority",         "0")
TR143_GET(get_dld_rom,        DLD, "ROMTime",             TR143_ZERO_TIME)
TR143_GET(get_dld_bom,        DLD, "BOMTime",             TR143_ZERO_TIME)
TR143_GET(get_dld_eom,        DLD, "EOMTime",             TR143_ZERO_TIME)
TR143_GET(get_dld_tcpreq,     DLD, "TCPOpenRequestTime",  TR143_ZERO_TIME)
TR143_GET(get_dld_tcpresp,    DLD, "TCPOpenResponseTime", TR143_ZERO_TIME)
TR143_GET(get_dld_testbytes,  DLD, "TestBytesReceived",   "0")
TR143_GET(get_dld_totalbytes, DLD, "TotalBytesReceived",  "0")

TR143_GET(get_uld_state,      ULD, "DiagnosticsState",    "None")
TR143_GET(get_uld_interface,  ULD, "Interface",           NULL)
TR143_GET(get_uld_transports, ULD, "UploadTransports",    "HTTP,FTP")
TR143_GET(get_uld_dscp,       ULD, "DSCP",                "0")
TR143_GET(get_uld_url,        ULD, "UploadURL",           NULL)
TR143_GET(get_uld_ethprio,    ULD, "EthPriority",         "0")
TR143_GET(get_uld_rom,        ULD, "ROMTime",             TR143_ZERO_TIME)
TR143_GET(get_uld_bom,        ULD, "BOMTime",             TR143_ZERO_TIME)
TR143_GET(get_uld_eom,        ULD, "EOMTime",             TR143_ZERO_TIME)
TR143_GET(get_uld_tcpreq,     ULD, "TCPOpenRequestTime",  TR143_ZERO_TIME)
TR143_GET(get_uld_tcpresp,    ULD, "TCPOpenResponseTime", TR143_ZERO_TIME)
TR143_GET(get_uld_filelen,    ULD, "TestFileLength",      "0")
TR143_GET(get_uld_totalsent,  ULD, "TotalBytesSent",      "0")

/* ------------------------------------------------------------------ */
/* setters                                                             */
/* ------------------------------------------------------------------ */

#define TR143_SETTER(fn, body)							\
static int fn(char *refparam, struct dmctx *ctx, void *data,			\
	      char *instance, char *value, int action)				\
{										\
	return body;								\
}

TR143_SETTER(set_dld_state,     tr143_set_state(DLD, value, action))
TR143_SETTER(set_dld_interface, tr143_set_interface(DLD, value, action))
TR143_SETTER(set_dld_dscp,      tr143_set_range(DLD, "DSCP", 0, 63, value, action))
TR143_SETTER(set_dld_url,       tr143_set_url(DLD, "DownloadURL", value, action))
TR143_SETTER(set_dld_ethprio,   tr143_set_range(DLD, "EthPriority", 0, 7, value, action))

TR143_SETTER(set_uld_state,     tr143_set_state(ULD, value, action))
TR143_SETTER(set_uld_interface, tr143_set_interface(ULD, value, action))
TR143_SETTER(set_uld_dscp,      tr143_set_range(ULD, "DSCP", 0, 63, value, action))
TR143_SETTER(set_uld_url,       tr143_set_url(ULD, "UploadURL", value, action))
TR143_SETTER(set_uld_ethprio,   tr143_set_range(ULD, "EthPriority", 0, 7, value, action))
TR143_SETTER(set_uld_filelen,   tr143_set_range(ULD, "TestFileLength", 0, -1, value, action))

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tDownloadDiagParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"DiagnosticsState", &DMWRITE, DMT_STRING, get_dld_state, set_dld_state, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_dld_interface, set_dld_interface, NULL, NULL},
{"DownloadTransports", &DMREAD, DMT_STRING, get_dld_transports, NULL, NULL, NULL},
{"DSCP", &DMWRITE, DMT_UNINT, get_dld_dscp, set_dld_dscp, NULL, NULL},
{"DownloadURL", &DMWRITE, DMT_STRING, get_dld_url, set_dld_url, NULL, NULL},
{"EthernetPriority", &DMWRITE, DMT_UNINT, get_dld_ethprio, set_dld_ethprio, NULL, NULL},
{"ROMTime", &DMREAD, DMT_STRING, get_dld_rom, NULL, NULL, NULL},
{"BOMTime", &DMREAD, DMT_STRING, get_dld_bom, NULL, NULL, NULL},
{"EOMTime", &DMREAD, DMT_STRING, get_dld_eom, NULL, NULL, NULL},
{"TCPOpenRequestTime", &DMREAD, DMT_STRING, get_dld_tcpreq, NULL, NULL, NULL},
{"TCPOpenResponseTime", &DMREAD, DMT_STRING, get_dld_tcpresp, NULL, NULL, NULL},
{"TestBytesReceived", &DMREAD, DMT_UNINT, get_dld_testbytes, NULL, NULL, NULL},
{"TotalBytesReceived", &DMREAD, DMT_UNINT, get_dld_totalbytes, NULL, NULL, NULL},
{0}
};

static DMLEAF tUploadDiagParams[] = {
{"DiagnosticsState", &DMWRITE, DMT_STRING, get_uld_state, set_uld_state, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_uld_interface, set_uld_interface, NULL, NULL},
{"UploadTransports", &DMREAD, DMT_STRING, get_uld_transports, NULL, NULL, NULL},
{"DSCP", &DMWRITE, DMT_UNINT, get_uld_dscp, set_uld_dscp, NULL, NULL},
{"UploadURL", &DMWRITE, DMT_STRING, get_uld_url, set_uld_url, NULL, NULL},
{"EthernetPriority", &DMWRITE, DMT_UNINT, get_uld_ethprio, set_uld_ethprio, NULL, NULL},
{"ROMTime", &DMREAD, DMT_STRING, get_uld_rom, NULL, NULL, NULL},
{"BOMTime", &DMREAD, DMT_STRING, get_uld_bom, NULL, NULL, NULL},
{"EOMTime", &DMREAD, DMT_STRING, get_uld_eom, NULL, NULL, NULL},
{"TCPOpenRequestTime", &DMREAD, DMT_STRING, get_uld_tcpreq, NULL, NULL, NULL},
{"TCPOpenResponseTime", &DMREAD, DMT_STRING, get_uld_tcpresp, NULL, NULL, NULL},
{"TestFileLength", &DMWRITE, DMT_UNINT, get_uld_filelen, set_uld_filelen, NULL, NULL},
{"TotalBytesSent", &DMREAD, DMT_UNINT, get_uld_totalsent, NULL, NULL, NULL},
{0}
};

static DMOBJ tTr143DiagMtkObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"DownloadDiagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tDownloadDiagParams, NULL},
{"UploadDiagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tUploadDiagParams, NULL},
{0}
};

static const char *const tr143diag_mtk_paths[] = {
	"InternetGatewayDevice.DownloadDiagnostics.",
	"InternetGatewayDevice.UploadDiagnostics.",
	NULL
};

static const struct dm_module tr143diag_mtk_module = {
	.name  = "mtk-tr143diag",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tTr143DiagMtkObj,
	.paths = tr143diag_mtk_paths,
};
DM_MODULE_REGISTER(tr143diag_mtk_module);
