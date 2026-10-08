/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The two name lookup diagnostics:
 *	  InternetGatewayDevice.NSLookupDiagnostics.   13 parameters + Result.{i}
 *	  InternetGatewayDevice.DNSDiagnostics.         7 parameters
 *
 *	Ported from functions/tr098/nslookup_diagnostic and dns_diagnostics.  The
 *	lookups stay in functions/common/nslookup_launch and
 *	dnsDiagnostics_launch, queued by DiagnosticsState=Requested; see
 *	diag_mtk.h.
 *
 *	Stores: -P /var/state/nslookup and -P /var/state/dnsDiagnostics for the
 *	parameters, -P /var/state/nslookup_result for NSLookup's Result.{i}.
 *
 *	WHAT DNSDiagnostics REALLY IS ON THE PRODUCT: seven parameters.  The
 *	shell has the lines for DNSServer, ResultNumberOfEntries and the
 *	Result. object commented out (dns_diagnostics:15, :18, :21), so
 *	Result.{i} was never reachable even though its sub_entry function and
 *	its launcher half are still there.  The coverage matrix, extracted from
 *	the text, lists the five Result.{i} leaves anyway; verify-dm-paths.py
 *	reports them as unreachable instead of missing.  Not ported.
 *
 *	Every SPV first meets the shell's input contract (input_contract_mtk.c:
 *	is_safe_input + the check by shell type), then the setters below.
 *
 *	VENDOR QUIRKS KEPT:
 *
 *	  - Timeout and NumberOfRepetitions go through nslookup_set /
 *	    dnslookup_set: the setter checks nothing -- only the contract's
 *	    xsd:unsignedInt range (0..4294967295) applies -- and does NOT stop a
 *	    running lookup, unlike every other writable leaf here.
 *	  - DNSServer takes the same host check as HostName, and the contract
 *	    refuses "" anyway: an empty value ("use the system resolver", which
 *	    the launcher supports) cannot be written back once set.
 *	  - Interface takes a network device name, "$(ifconfig $2)" rule
 *	    (diag_ifconfig_prints).
 *
 *	ONE DELIBERATE DIFFERENCE: Result.{i} stops at 256 instances.  The shell
 *	ran "seq 1 $ResultNumberOfEntries" on whatever the store held; the
 *	launcher writes the number of answer lines of one nslookup.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "dmuci.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "diag_mtk.h"

#define NSL	(&diag_nslookup)
#define DNSD	(&diag_dns)

#define LOOKUP_MAX_RESULTS	256

/* ------------------------------------------------------------------ */
/* shared setters                                                      */
/* ------------------------------------------------------------------ */

static int lookup_set_state(const struct diag_store *d, char *value, int action)
{
	if (strcmp(value, "Requested") != 0)
		return FAULT_9007;
	if (action == VALUESET)
		diag_request(d);
	return 0;
}

static int lookup_set_host(const struct diag_store *d, const char *option, char *value, int action)
{
	if (!diag_host_valid(d, value))
		return FAULT_9007;
	if (action == VALUESET)
		diag_store_value(d, option, value);
	return 0;
}

static int lookup_set_interface(const struct diag_store *d, char *value, int action)
{
	if (!diag_ifconfig_prints(value))
		return FAULT_9007;
	if (action == VALUESET)
		diag_store_value(d, "Interface", value);
	return 0;
}

/* nslookup_set / dnslookup_set: no check, no stop */
static int lookup_set_plain(const struct diag_store *d, const char *option, char *value, int action)
{
	if (action == VALUESET)
		diag_store_value_nostop(d, option, value);
	return 0;
}

/* ------------------------------------------------------------------ */
/* NSLookupDiagnostics                                                 */
/* ------------------------------------------------------------------ */

#define NSL_GET(fn, option, def)						\
static int fn(char *refparam, struct dmctx *ctx, void *data,			\
	      char *instance, char **value)					\
{										\
	*value = diag_get(NSL, option, def);					\
	return 0;								\
}

NSL_GET(get_nsl_state,       "DiagnosticsState",      "None")
NSL_GET(get_nsl_hostname,    "HostName",              NULL)
NSL_GET(get_nsl_dnsserver,   "DNSServer",             NULL)
NSL_GET(get_nsl_interface,   "Interface",             "default")
NSL_GET(get_nsl_timeout,     "Timeout",               "5000")
NSL_GET(get_nsl_resultcount, "ResultNumberOfEntries", "0")
NSL_GET(get_nsl_repetitions, "NumberOfRepetitions",   "3")
NSL_GET(get_nsl_success,     "SuccessCount",          "0")

static int set_nsl_state(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return lookup_set_state(NSL, value, action);
}

static int set_nsl_hostname(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return lookup_set_host(NSL, "HostName", value, action);
}

static int set_nsl_dnsserver(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return lookup_set_host(NSL, "DNSServer", value, action);
}

static int set_nsl_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return lookup_set_interface(NSL, value, action);
}

static int set_nsl_timeout(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return lookup_set_plain(NSL, "Timeout", value, action);
}

static int set_nsl_repetitions(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return lookup_set_plain(NSL, "NumberOfRepetitions", value, action);
}

/* Result.<n> -> easycwmp.@local[<n>] of the result store; data = n */
#define NSL_RESULT_GET(fn, option)						\
static int fn(char *refparam, struct dmctx *ctx, void *data,			\
	      char *instance, char **value)					\
{										\
	*value = diag_result_get(DIAG_NSLOOKUP_RESULT_DIR,			\
				 (int)(uintptr_t)data, option);			\
	return 0;								\
}

NSL_RESULT_GET(get_nsl_res_status,   "Status")
NSL_RESULT_GET(get_nsl_res_hostname, "HostNameReturned")
NSL_RESULT_GET(get_nsl_res_ips,      "IPAddresses")
NSL_RESULT_GET(get_nsl_res_server,   "DNSServerIP")
NSL_RESULT_GET(get_nsl_res_time,     "ResponseTime")

/* dnsdiag_nslookupresult_browse_instances: "seq 1 $ResultNumberOfEntries",
 * the count read from the parameter store, not from the result store */
static int browseNSLookupResultInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	const char *count = diag_get(NSL, "ResultNumberOfEntries", NULL);
	char *idx, *idx_last = NULL;
	long n;
	int i;

	if (diag_check_uint(count, 0, -1) != 0)
		return 0;		/* seq: invalid number -> no line */
	n = strtol(count, NULL, 10);	/* digits only: overflow saturates */
	if (n > LOOKUP_MAX_RESULTS)
		n = LOOKUP_MAX_RESULTS;
	for (i = 1; i <= n; i++) {
		idx = handle_update_instance(4, dmctx, &idx_last, update_instance_without_section,
					     1, i);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)(uintptr_t)i, idx) == DM_STOP)
			break;
	}
	return 0;
}

static DMLEAF tNSLookupResultParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Status", &DMREAD, DMT_STRING, get_nsl_res_status, NULL, NULL, NULL},
{"HostNameReturned", &DMREAD, DMT_STRING, get_nsl_res_hostname, NULL, NULL, NULL},
{"IPAddresses", &DMREAD, DMT_STRING, get_nsl_res_ips, NULL, NULL, NULL},
{"DNSServerIP", &DMREAD, DMT_STRING, get_nsl_res_server, NULL, NULL, NULL},
{"ResponseTime", &DMREAD, DMT_UNINT, get_nsl_res_time, NULL, NULL, NULL},
{0}
};

static DMLEAF tNSLookupParams[] = {
{"DiagnosticsState", &DMWRITE, DMT_STRING, get_nsl_state, set_nsl_state, NULL, NULL},
{"HostName", &DMWRITE, DMT_STRING, get_nsl_hostname, set_nsl_hostname, NULL, NULL},
{"DNSServer", &DMWRITE, DMT_STRING, get_nsl_dnsserver, set_nsl_dnsserver, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_nsl_interface, set_nsl_interface, NULL, NULL},
{"Timeout", &DMWRITE, DMT_UNINT, get_nsl_timeout, set_nsl_timeout, NULL, NULL},
{"ResultNumberOfEntries", &DMREAD, DMT_UNINT, get_nsl_resultcount, NULL, NULL, NULL},
{"NumberOfRepetitions", &DMWRITE, DMT_UNINT, get_nsl_repetitions, set_nsl_repetitions, NULL, NULL},
{"SuccessCount", &DMREAD, DMT_UNINT, get_nsl_success, NULL, NULL, NULL},
{0}
};

static DMOBJ tNSLookupChildObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Result", &DMREAD, NULL, NULL, NULL, browseNSLookupResultInst, NULL, NULL,
 NULL, tNSLookupResultParams, NULL},
{0}
};

/* ------------------------------------------------------------------ */
/* DNSDiagnostics                                                      */
/* ------------------------------------------------------------------ */

#define DNS_GET(fn, option, def)						\
static int fn(char *refparam, struct dmctx *ctx, void *data,			\
	      char *instance, char **value)					\
{										\
	*value = diag_get(DNSD, option, def);					\
	return 0;								\
}

DNS_GET(get_dns_state,       "DiagnosticsState",    "None")
DNS_GET(get_dns_host,        "Host",                NULL)
DNS_GET(get_dns_interface,   "Interface",           "default")
DNS_GET(get_dns_timeout,     "Timeout",             "5000")
DNS_GET(get_dns_repetitions, "NumberOfRepetitions", "3")
DNS_GET(get_dns_success,     "SuccessCount",        "0")
DNS_GET(get_dns_failure,     "FailureCount",        "0")

static int set_dns_state(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return lookup_set_state(DNSD, value, action);
}

static int set_dns_host(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return lookup_set_host(DNSD, "Host", value, action);
}

static int set_dns_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return lookup_set_interface(DNSD, value, action);
}

static int set_dns_timeout(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return lookup_set_plain(DNSD, "Timeout", value, action);
}

static int set_dns_repetitions(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return lookup_set_plain(DNSD, "NumberOfRepetitions", value, action);
}

static DMLEAF tDNSDiagParams[] = {
{"DiagnosticsState", &DMWRITE, DMT_STRING, get_dns_state, set_dns_state, NULL, NULL},
{"Host", &DMWRITE, DMT_STRING, get_dns_host, set_dns_host, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_dns_interface, set_dns_interface, NULL, NULL},
{"Timeout", &DMWRITE, DMT_UNINT, get_dns_timeout, set_dns_timeout, NULL, NULL},
{"NumberOfRepetitions", &DMWRITE, DMT_UNINT, get_dns_repetitions, set_dns_repetitions, NULL, NULL},
{"SuccessCount", &DMREAD, DMT_UNINT, get_dns_success, NULL, NULL, NULL},
{"FailureCount", &DMREAD, DMT_UNINT, get_dns_failure, NULL, NULL, NULL},
{0}
};

/* ------------------------------------------------------------------ */
/* registration                                                        */
/* ------------------------------------------------------------------ */

static DMOBJ tLookupDiagMtkObj[] = {
{"NSLookupDiagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 tNSLookupChildObj, tNSLookupParams, NULL},
{"DNSDiagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 NULL, tDNSDiagParams, NULL},
{0}
};

static const char *const lookupdiag_mtk_paths[] = {
	"InternetGatewayDevice.NSLookupDiagnostics.",
	"InternetGatewayDevice.DNSDiagnostics.",
	NULL
};

static const struct dm_module lookupdiag_mtk_module = {
	.name  = "mtk-lookupdiag",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tLookupDiagMtkObj,
	.paths = lookupdiag_mtk_paths,
};
DM_MODULE_REGISTER(lookupdiag_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): NSLookupDiagnostics is
 * Device.DNS.Diagnostics.NSLookupDiagnostics, the same store, launcher and
 * tables (Result.{i} included).  DNSDiagnostics is the product's own object
 * without a vendor prefix, kept as Device.DNSDiagnostics like
 * Device.Account.  Interface stays a network device name as on the product. */
static DMOBJ tLookup181DiagObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"NSLookupDiagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 tNSLookupChildObj, tNSLookupParams, NULL},
{0}
};

static DMOBJ tLookup181DnsObj[] = {
{"Diagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tLookup181DiagObj, NULL, NULL},
{0}
};

static DMOBJ tLookup181Root[] = {
{"DNS", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tLookup181DnsObj, NULL, NULL},
{"DNSDiagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 NULL, tDNSDiagParams, NULL},
{0}
};

static const char *const lookupdiag181_mtk_paths[] = {
	"Device.DNS.Diagnostics.",
	"Device.DNSDiagnostics.",
	NULL
};

static const struct dm_module lookupdiag181_mtk_module = {
	.name  = "mtk-lookupdiag-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tLookup181Root,
	.paths = lookupdiag181_mtk_paths,
};
DM_MODULE_REGISTER(lookupdiag181_mtk_module);
