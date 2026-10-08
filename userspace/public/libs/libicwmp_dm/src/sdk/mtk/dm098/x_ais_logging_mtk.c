/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.X_AIS_Logging. -- the product's syslog settings,
 *	ported from functions/tr098/X_AIS_Logging.  Every leaf is an option of
 *	system.syslog:
 *	  EnableLogging, EnableRemoteLogging  log_enable, log_remote ("1"/"0")
 *	  LoggingLevel          log_level, digits 0..7
 *	  RemoteLoggingAddress  log_ip, "" or an IPv4/IPv6 address
 *	  RemoteLoggingPort     log_port, "" or 1..65535
 *	  TFTPAddress           tftp_server, "" or an IPv4/IPv6 address
 *	  TFTPUploadResponse    tftp_response, 0..3, reads "0" when unset;
 *	                        1 (Requested) uploads the log NOW, see below
 *	  CleanLogging          clean_logging, 0..3, reads "0" when unset;
 *	                        1 empties /backup/log/backup/ NOW
 *	  <Level>Enable         selected_log_levels, a "|" list of emerg alert
 *	                        crit err warn notice info debug, "none" when empty
 *	  RemoteLogging.<Level>Enable  the same over selected_remote_levels
 *	A level reads "1" when its name is IN the list -- a substring match, as
 *	"grep -q" made it.  Setting one adds the name, or removes it with the
 *	shell's three seds.
 *
 *	Every set writes (no "unchanged" shortcut).  The leaves that changed the
 *	syslog configuration queue "/etc/init.d/log restart" (the shell queued
 *	it as well); TFTPAddress and the two request leaves restart nothing.
 *
 *	TFTPUploadResponse=1 and CleanLogging=1 do their work inside the setter,
 *	like the shell, and store the outcome: 2 (Responded) or 3 (Error).  The
 *	upload tars /backup/log -- with /var/log/messages linked in -- or
 *	/var/log/messages alone into /tmp/AIS_<modelname>_<YYYYMMDD>.tar.gz and
 *	sends it with "tftp -p" to TFTPAddress (set earlier in the same SPV
 *	counts).  It runs at VALUESET: a later leaf of the SPV that faults
 *	reverts tftp_response, not the upload.  One difference, on purpose: the
 *	shell removed /backup/log/messages after the tar even when its "ln -s"
 *	had failed because a real file of that name was there; only the link
 *	made here is removed.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmmem.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define LOG_PACKAGE	"system"
#define LOG_SECTION	"syslog"
#define LOG_RESTART	"/etc/init.d/log restart"

#define STATE_IDLE	0
#define STATE_REQUESTED	1
#define STATE_RESPONDED	2
#define STATE_ERROR	3

static char *log_get(const char *option)
{
	return mtk_uci(LOG_PACKAGE, LOG_SECTION, option);
}

static void log_set(const char *option, const char *value, int restart)
{
	dmuci_set_value(LOG_PACKAGE, LOG_SECTION, (char *)option, (char *)value);
	if (restart)
		mtk_apply_service_once(LOG_RESTART);
}

/* ------------------------------------------------------------------ */
/* switches, level, remote server                                      */
/* ------------------------------------------------------------------ */

#define LOG_FLAG(name, option)							\
static int get_log_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char **value)				\
{										\
	*value = strcmp(log_get(option), "1") == 0 ? "1" : "0";			\
	return 0;								\
}										\
static int set_log_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char *value, int action)		\
{										\
	int b = mtk_parse_bool(value);						\
										\
	if (b < 0)								\
		return FAULT_9007;						\
	if (action == VALUECHECK)						\
		return 0;							\
	log_set(option, b ? "1" : "0", 1);					\
	return 0;								\
}

LOG_FLAG(enable, "log_enable")
LOG_FLAG(remote, "log_remote")

static int get_log_level(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = log_get("log_level");
	return 0;
}

static int set_log_level(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *p;
	long long n;

	/* [ -z ] || ! grep -qE '^[0-9]+$' || [ -lt 0 ] || [ -gt 7 ] */
	if (!value || !*value)
		return FAULT_9007;
	for (p = value; *p; p++) {
		if (*p < '0' || *p > '9')
			return FAULT_9007;
	}
	if (mtk_shell_getn(value, &n) != 0 || n > 7)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	log_set("log_level", value, 1);
	return 0;
}

/* "" or is_valid_ipv4 || is_valid_ipv6 */
static int log_address_ok(const char *value)
{
	return !*value || mtk_shell_ipv4(value) || mtk_shell_ipv6(value);
}

static int get_log_remote_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = log_get("log_ip");
	return 0;
}

static int set_log_remote_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!value)
		value = "";
	if (!log_address_ok(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	log_set("log_ip", value, 1);
	return 0;
}

/* [ -n "$v" ] && { [ "$v" -lt lo ] || [ "$v" -gt hi ]; }: refused only for an
 * integer out of range -- "test" failing on a non-integer let it through
 * (the unsignedInt check in front does not) */
static int log_out_of_range(const char *value, long long lo, long long hi)
{
	long long n;

	if (!*value || mtk_shell_getn(value, &n) != 0)
		return 0;
	return n < lo || n > hi;
}

static int get_log_remote_port(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = log_get("log_port");
	return 0;
}

static int set_log_remote_port(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!value)
		value = "";
	if (log_out_of_range(value, 1, 65535))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	log_set("log_port", value, 1);
	return 0;
}

static int get_log_tftp_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = log_get("tftp_server");
	return 0;
}

static int set_log_tftp_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!value)
		value = "";
	if (!log_address_ok(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	log_set("tftp_server", value, 0);
	return 0;
}

/* ------------------------------------------------------------------ */
/* the two requests                                                    */
/* ------------------------------------------------------------------ */

#define LOG_DIR		"/backup/log"
#define LOG_MESSAGES	"/var/log/messages"

/* the Requested branch of set_tftp_upload_response: the state it ends in */
static int log_tftp_upload(void)
{
	char *server = log_get("tftp_server");
	char *model, name[160], local[192], date[16];
	time_t now = time(NULL);
	struct tm tm;
	struct stat st;
	int rc;

	if (!*server)
		return STATE_ERROR;		/* no TFTP server configured */
	model = mtk_uci(LOG_PACKAGE, "@devinfo[0]", "modelname");
	if (!*model)
		model = "UNKNOWN";
	localtime_r(&now, &tm);
	strftime(date, sizeof(date), "%Y%m%d", &tm);
	snprintf(name, sizeof(name), "AIS_%s_%s.tar.gz", model, date);
	snprintf(local, sizeof(local), "/tmp/%s", name);

	if (stat(LOG_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
		int linked = symlink(LOG_MESSAGES, LOG_DIR "/messages") == 0;
		char *tar_argv[] = { "tar", "-czhf", local, "-C", LOG_DIR, ".", NULL };

		mtk_run(tar_argv);
		if (linked)
			unlink(LOG_DIR "/messages");
	} else {
		char *tar_argv[] = { "tar", "-czf", local, "-C", "/var/log", "messages", NULL };

		mtk_run(tar_argv);
	}
	if (stat(local, &st) != 0)
		return STATE_ERROR;		/* failed to create the tar.gz */
	{
		char *tftp_argv[] = { "tftp", "-p", "-l", local, "-r", name, server, NULL };

		rc = mtk_run(tftp_argv);
	}
	unlink(local);
	return rc == 0 ? STATE_RESPONDED : STATE_ERROR;
}

/* the Requested branch of set_clean_logging */
static int log_clean(void)
{
	char *argv[] = { "/bin/sh", "-c", "rm -rf " LOG_DIR "/backup/*", NULL };

	return mtk_run(argv) == 0 ? STATE_RESPONDED : STATE_ERROR;
}

static int log_set_request(const char *option, const char *value, int action, int (*work)(void))
{
	char state[8];

	if (!value)
		value = "";
	if (log_out_of_range(value, STATE_IDLE, STATE_ERROR))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (strcmp(value, "1") == 0) {
		snprintf(state, sizeof(state), "%d", work());
		value = state;
	}
	log_set(option, value, 0);
	return 0;
}

static int get_log_tftp_response(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = log_get("tftp_response");

	*value = *v ? v : "0";
	return 0;
}

static int set_log_tftp_response(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return log_set_request("tftp_response", value, action, log_tftp_upload);
}

static int get_log_clean(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = log_get("clean_logging");

	*value = *v ? v : "0";
	return 0;
}

static int set_log_clean(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return log_set_request("clean_logging", value, action, log_clean);
}

/* ------------------------------------------------------------------ */
/* level lists                                                         */
/* ------------------------------------------------------------------ */

/* sed "s/<pat>//g" with a literal pattern, in place */
static void log_cut_all(char *s, const char *pat)
{
	size_t n = strlen(pat);
	char *p;

	while ((p = strstr(s, pat)) != NULL)
		memmove(p, p + n, strlen(p + n) + 1);
}

static char *log_levels_with(const char *current, const char *level, int on)
{
	const char *cur = strcmp(current, "none") == 0 ? "" : current;
	char *out = NULL;

	if (on) {
		if (strstr(cur, level))
			out = dmstrdup(cur);
		else if (!*cur)
			out = dmstrdup(level);
		else
			dmasprintf(&out, "%s|%s", cur, level);
	} else {
		char pat[16];

		out = dmstrdup(cur);
		if (out) {
			snprintf(pat, sizeof(pat), "|%s", level);
			log_cut_all(out, pat);
			snprintf(pat, sizeof(pat), "%s|", level);
			log_cut_all(out, pat);
			if (strcmp(out, level) == 0)	/* s/^level$// */
				out[0] = '\0';
		}
	}
	return (out && *out) ? out : "none";
}

static int log_get_level(const char *option, const char *level, char **value)
{
	*value = strstr(log_get(option), level) ? "1" : "0";
	return 0;
}

static int log_set_level(const char *option, const char *level, const char *value, int action)
{
	int on;

	if (!value)
		return FAULT_9007;
	/* [ "$val" = "true" ] || [ "$val" = "1" ]; anything else removes --
	 * the boolean check in front leaves false and 0 only */
	on = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	if (action == VALUECHECK)
		return 0;
	log_set(option, log_levels_with(log_get(option), level, on), 1);
	return 0;
}

#define LOG_LEVEL(name, level)							\
static int get_log_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char **value)				\
{										\
	return log_get_level("selected_log_levels", level, value);		\
}										\
static int set_log_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char *value, int action)		\
{										\
	return log_set_level("selected_log_levels", level, value, action);	\
}										\
static int get_rlog_##name(char *refparam, struct dmctx *ctx, void *data,	\
			   char *instance, char **value)			\
{										\
	return log_get_level("selected_remote_levels", level, value);		\
}										\
static int set_rlog_##name(char *refparam, struct dmctx *ctx, void *data,	\
			   char *instance, char *value, int action)		\
{										\
	return log_set_level("selected_remote_levels", level, value, action);	\
}

LOG_LEVEL(emerg, "emerg")
LOG_LEVEL(alert, "alert")
LOG_LEVEL(crit, "crit")
LOG_LEVEL(err, "err")
LOG_LEVEL(warn, "warn")
LOG_LEVEL(notice, "notice")
LOG_LEVEL(info, "info")
LOG_LEVEL(debug, "debug")

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tLoggingParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"EnableLogging", &DMWRITE, DMT_BOOL, get_log_enable, set_log_enable, NULL, NULL},
{"EnableRemoteLogging", &DMWRITE, DMT_BOOL, get_log_remote, set_log_remote, NULL, NULL},
{"LoggingLevel", &DMWRITE, DMT_UNINT, get_log_level, set_log_level, NULL, NULL},
{"RemoteLoggingAddress", &DMWRITE, DMT_STRING, get_log_remote_address, set_log_remote_address, NULL, NULL},
{"RemoteLoggingPort", &DMWRITE, DMT_UNINT, get_log_remote_port, set_log_remote_port, NULL, NULL},
{"TFTPAddress", &DMWRITE, DMT_STRING, get_log_tftp_address, set_log_tftp_address, NULL, NULL},
{"TFTPUploadResponse", &DMWRITE, DMT_UNINT, get_log_tftp_response, set_log_tftp_response, NULL, NULL},
{"CleanLogging", &DMWRITE, DMT_UNINT, get_log_clean, set_log_clean, NULL, NULL},
{"EmergencyEnable", &DMWRITE, DMT_BOOL, get_log_emerg, set_log_emerg, NULL, NULL},
{"AlertEnable", &DMWRITE, DMT_BOOL, get_log_alert, set_log_alert, NULL, NULL},
{"CriticalEnable", &DMWRITE, DMT_BOOL, get_log_crit, set_log_crit, NULL, NULL},
{"ErrorEnable", &DMWRITE, DMT_BOOL, get_log_err, set_log_err, NULL, NULL},
{"WarningEnable", &DMWRITE, DMT_BOOL, get_log_warn, set_log_warn, NULL, NULL},
{"NoticeEnable", &DMWRITE, DMT_BOOL, get_log_notice, set_log_notice, NULL, NULL},
{"InformationEnable", &DMWRITE, DMT_BOOL, get_log_info, set_log_info, NULL, NULL},
{"DebugEnable", &DMWRITE, DMT_BOOL, get_log_debug, set_log_debug, NULL, NULL},
{0}
};

static DMLEAF tRemoteLoggingParams[] = {
{"EmergencyEnable", &DMWRITE, DMT_BOOL, get_rlog_emerg, set_rlog_emerg, NULL, NULL},
{"AlertEnable", &DMWRITE, DMT_BOOL, get_rlog_alert, set_rlog_alert, NULL, NULL},
{"CriticalEnable", &DMWRITE, DMT_BOOL, get_rlog_crit, set_rlog_crit, NULL, NULL},
{"ErrorEnable", &DMWRITE, DMT_BOOL, get_rlog_err, set_rlog_err, NULL, NULL},
{"WarningEnable", &DMWRITE, DMT_BOOL, get_rlog_warn, set_rlog_warn, NULL, NULL},
{"NoticeEnable", &DMWRITE, DMT_BOOL, get_rlog_notice, set_rlog_notice, NULL, NULL},
{"InformationEnable", &DMWRITE, DMT_BOOL, get_rlog_info, set_rlog_info, NULL, NULL},
{"DebugEnable", &DMWRITE, DMT_BOOL, get_rlog_debug, set_rlog_debug, NULL, NULL},
{0}
};

static DMOBJ tLoggingObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"RemoteLogging", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tRemoteLoggingParams, NULL},
{0}
};

static DMOBJ tLoggingRoot[] = {
{"X_AIS_Logging", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tLoggingObj, tLoggingParams, NULL},
{0}
};

static const char *const logging_mtk_paths[] = {
	"InternetGatewayDevice.X_AIS_Logging.",
	NULL
};

static const struct dm_module logging_mtk_module = {
	.name  = "mtk-x-ais-logging",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tLoggingRoot,
	.paths = logging_mtk_paths,
};
DM_MODULE_REGISTER(logging_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): the same tables under Device., type C
 * of docs/plan/tr181_mtk_design.md */
static const char *const logging_mtk_paths181[] = {
	"Device.X_AIS_Logging.",
	NULL
};

static const struct dm_module logging_mtk_module181 = {
	.name  = "mtk-x-ais-logging-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tLoggingRoot,
	.paths = logging_mtk_paths181,
};
DM_MODULE_REGISTER(logging_mtk_module181);
