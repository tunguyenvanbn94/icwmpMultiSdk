/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	<CUSTOM_PREFIX>Icwmp. — icwmpd settings the ACS may want to read or
 *	change and that neither TR-098/TR-181 nor the platform data model
 *	model: they live in the cwmp UCI config of icwmpd (BDK:
 *	/data/icwmp/config/cwmp, OpenWrt: /etc/config/cwmp).  Platform
 *	independent (tr098/common/), linked on every platform:
 *	  bdk  ManagementServer.X_MARUSYS_COM_Icwmp. under both roots
 *	       (tr098/bdk/root_bdk.c, root181_bdk.c)
 *	  mtk  InternetGatewayDevice.X_HNI_Icwmp. (tr098/mtk/root_mtk.c), a
 *	       top level object because ManagementServer. is served by the script
 *	Every write ends with END_SESSION_RELOAD, i.e. icwmpd reloads its config
 *	at the end of the session (cwmp_apply_acs_changes): libtr098 re-reads
 *	cwmp.cpe.datamodel at the next dm context, icwmpd re-reads the rest in
 *	global_conf_init(); nothing is applied half way inside the session.
 *
 *	  DataModel             RW  tr098 | tr181   root the ACS talks to
 *	                            (cwmp.cpe.datamodel), effective next session,
 *	                            BDK only (9001 elsewhere: the mtk platform has
 *	                            no TR-181 tree)
 *	  AmdVersion            RW  1..5            CWMP amendment in the Inform
 *	                            namespace (cwmp.cpe.amd_version; 3 = cwmp-1-2)
 *	  LogSeverity           RW  EMERG..DEBUG    /var/log/icwmpd.log level
 *	  SessionTimeout        RW  seconds         cwmp.cpe.session_timeout
 *	  ConnectionRequestPort RO                  cwmp.cpe.port (the CR server listens here)
 *	  ConnectionRequestHost RW  host or ""      cwmp.cpe.cr_host: address the ACS can
 *	                            reach when the AP sits behind NAT (ONT port forward),
 *	                            "" = own address of cwmp.cpe.interface
 *	  ConnectionRequestExternalPort RW 1..65535 cwmp.cpe.cr_port: port on that host,
 *	                            "" = ConnectionRequestPort
 *	  DataModelBackend      RO                  dm_platform_name() ("bdk", "mtk-script", "uci")
 *
 *	Names are provisional (CUSTOM_PREFIX), to be renamed when agreed with
 *	the ACS.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "dmtr098.h"
#include "dmmem.h"
#include "dmuci.h"
#include "sdk/sdk.h"
#include "icwmpcfg.h"

static const char *const icwmp_log_levels[] = {
	"EMERG", "ALERT", "CRITIC", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG", NULL
};

static int get_icwmp_uci(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	const char *leaf = strrchr(refparam, '.');
	const char *opt;

	(void)ctx; (void)data; (void)instance;
	leaf = leaf ? leaf + 1 : refparam;
	if (strcmp(leaf, "DataModel") == 0)
		opt = "datamodel";
	else if (strcmp(leaf, "AmdVersion") == 0)
		opt = "amd_version";
	else if (strcmp(leaf, "LogSeverity") == 0)
		opt = "log_severity";
	else if (strcmp(leaf, "SessionTimeout") == 0)
		opt = "session_timeout";
	else if (strcmp(leaf, "ConnectionRequestPort") == 0)
		opt = "port";
	else if (strcmp(leaf, "ConnectionRequestHost") == 0)
		opt = "cr_host";
	else if (strcmp(leaf, "ConnectionRequestExternalPort") == 0)
		opt = "cr_port";
	else {
		*value = "";
		return 0;
	}
	dmuci_get_option_value_string("cwmp", "cpe", (char *)opt, value);
	if (strcmp(leaf, "DataModel") == 0 && (*value)[0] == '\0')
		*value = "tr098";                      /* absent option = default root */
	return 0;
}

static int set_icwmp_uci(const char *opt, char *value, int action)
{
	if (action == VALUESET) {
		dmuci_set_value("cwmp", "cpe", (char *)opt, value);
		cwmp_set_end_session(END_SESSION_RELOAD);
	}
	return 0;
}

static int set_icwmp_datamodel(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	if (strcmp(dm_platform_name(), "bdk") != 0)
		return FAULT_9001;                     /* only the BDK build has a TR-181 root */
	if (strcasecmp(value, "tr098") != 0 && strcasecmp(value, "tr181") != 0)
		return FAULT_9007;
	return set_icwmp_uci("datamodel", strcasecmp(value, "tr181") == 0 ? "tr181" : "tr098", action);
}

static int set_icwmp_amd_version(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *end;
	unsigned long v = strtoul(value, &end, 10);

	(void)refparam; (void)ctx; (void)data; (void)instance;
	if (*end || v < 1 || v > 5)
		return FAULT_9007;
	return set_icwmp_uci("amd_version", value, action);
}

static int set_icwmp_log_severity(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int i;

	(void)refparam; (void)ctx; (void)data; (void)instance;
	for (i = 0; icwmp_log_levels[i]; i++)
		if (strcasecmp(value, icwmp_log_levels[i]) == 0)
			return set_icwmp_uci("log_severity", (char *)icwmp_log_levels[i], action);
	return FAULT_9007;
}

static int set_icwmp_session_timeout(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *end;
	unsigned long v = strtoul(value, &end, 10);

	(void)refparam; (void)ctx; (void)data; (void)instance;
	if (*end || v < 1 || v > 3600)
		return FAULT_9007;
	return set_icwmp_uci("session_timeout", value, action);
}

/* host name / IPv4 / IPv6 literal, no scheme, no path; "" clears the override */
static int set_icwmp_cr_host(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	size_t i, len = strlen(value);

	(void)refparam; (void)ctx; (void)data; (void)instance;
	if (len > 127)
		return FAULT_9007;
	for (i = 0; i < len; i++)
		if (!(isalnum((unsigned char)value[i]) || value[i] == '.' || value[i] == '-' || value[i] == ':' ||
		      value[i] == '[' || value[i] == ']'))
			return FAULT_9007;
	return set_icwmp_uci("cr_host", value, action);
}

static int set_icwmp_cr_port(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *end;
	unsigned long v;

	(void)refparam; (void)ctx; (void)data; (void)instance;
	if (value[0] == '\0' || strcmp(value, "0") == 0)
		return set_icwmp_uci("cr_port", "", action);      /* back to ConnectionRequestPort */
	v = strtoul(value, &end, 10);
	if (*end || v < 1 || v > 65535)
		return FAULT_9007;
	return set_icwmp_uci("cr_port", value, action);
}

static int get_icwmp_backend(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	*value = (char *)dm_platform_name();
	return 0;
}

DMLEAF tIcwmpCfgParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"DataModel",             &DMWRITE, DMT_STRING, get_icwmp_uci,     set_icwmp_datamodel,       NULL, NULL},
{"AmdVersion",            &DMWRITE, DMT_UNINT,  get_icwmp_uci,     set_icwmp_amd_version,     NULL, NULL},
{"LogSeverity",           &DMWRITE, DMT_STRING, get_icwmp_uci,     set_icwmp_log_severity,    NULL, NULL},
{"SessionTimeout",        &DMWRITE, DMT_UNINT,  get_icwmp_uci,     set_icwmp_session_timeout, NULL, NULL},
{"ConnectionRequestPort", &DMREAD,  DMT_UNINT,  get_icwmp_uci,     NULL,                      NULL, NULL},
{"ConnectionRequestHost", &DMWRITE, DMT_STRING, get_icwmp_uci,     set_icwmp_cr_host,         NULL, NULL},
{"ConnectionRequestExternalPort", &DMWRITE, DMT_STRING, get_icwmp_uci, set_icwmp_cr_port,     NULL, NULL},
{"DataModelBackend",      &DMREAD,  DMT_STRING, get_icwmp_backend, NULL,                      NULL, NULL},
{0}
};

/* BDK: child object of ManagementServer under both roots */
DMOBJ tManagementServerIcwmpObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{CUSTOM_PREFIX"Icwmp", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tIcwmpCfgParam, NULL},
{0}
};
