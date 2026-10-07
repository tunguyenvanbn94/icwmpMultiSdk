/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.X_AIS_Conf. -- the operator's configuration
 *	backup (aisbackup), ported from functions/tr098/X_AIS_Conf.  Options of
 *	the named section aisbackup.params, added when missing:
 *	  upload_server, download_server   any string ("" removes the option)
 *	  upload_to_server,                stored "true"/"false", read "1"/"0";
 *	  download_from_server             true|1|false|0, never empty
 *	  auto_upload_delay                any non-empty value (the int check
 *	  auto_upload_interval_time        in front holds it to an integer);
 *	                                   the latter is option auto_upload_interval
 *	  auto_download_status,            read only
 *	  auto_upload_status
 *	aisbackup is not restarted: the shell only committed.  The value is
 *	checked before the section is added here; the shell added it first, so a
 *	refused value could leave an empty section behind.
 */
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define CONF_PACKAGE	"aisbackup"
#define CONF_SECTION	"params"

static void conf_write(const char *option, const char *value)
{
	/* a package that cannot take the section: the shell's uci set failed
	 * silently and the setter still answered success */
	mtk_uci_ensure_section(CONF_PACKAGE, CONF_SECTION, "aisbackup");
	dmuci_set_value(CONF_PACKAGE, CONF_SECTION, (char *)option, (char *)value);
}

#define CONF_TEXT(name, option, nonempty)					\
static int get_conf_##name(char *refparam, struct dmctx *ctx, void *data,	\
			   char *instance, char **value)			\
{										\
	*value = mtk_uci(CONF_PACKAGE, CONF_SECTION, option);			\
	return 0;								\
}										\
static int set_conf_##name(char *refparam, struct dmctx *ctx, void *data,	\
			   char *instance, char *value, int action)		\
{										\
	if (nonempty && (!value || !*value))					\
		return FAULT_9007;						\
	if (action == VALUECHECK)						\
		return 0;							\
	conf_write(option, value ? value : "");					\
	return 0;								\
}

CONF_TEXT(upload_server, "upload_server", 0)
CONF_TEXT(download_server, "download_server", 0)
CONF_TEXT(auto_upload_delay, "auto_upload_delay", 1)
CONF_TEXT(auto_upload_interval, "auto_upload_interval", 1)

#define CONF_FLAG(name, option)							\
static int get_conf_##name(char *refparam, struct dmctx *ctx, void *data,	\
			   char *instance, char **value)			\
{										\
	*value = strcmp(mtk_uci(CONF_PACKAGE, CONF_SECTION, option), "true") == 0 ? "1" : "0"; \
	return 0;								\
}										\
static int set_conf_##name(char *refparam, struct dmctx *ctx, void *data,	\
			   char *instance, char *value, int action)		\
{										\
	const char *v;								\
										\
	if (!value || !*value)							\
		return FAULT_9007;						\
	if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0)		\
		v = "true";							\
	else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0)	\
		v = "false";							\
	else									\
		return FAULT_9007;						\
	if (action == VALUECHECK)						\
		return 0;							\
	conf_write(option, v);							\
	return 0;								\
}

CONF_FLAG(upload_to_server, "upload_to_server")
CONF_FLAG(download_from_server, "download_from_server")

static int get_conf_auto_download_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci(CONF_PACKAGE, CONF_SECTION, "auto_download_status");
	return 0;
}

static int get_conf_auto_upload_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci(CONF_PACKAGE, CONF_SECTION, "auto_upload_status");
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tConfParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"upload_server", &DMWRITE, DMT_STRING, get_conf_upload_server, set_conf_upload_server, NULL, NULL},
{"download_server", &DMWRITE, DMT_STRING, get_conf_download_server, set_conf_download_server, NULL, NULL},
{"upload_to_server", &DMWRITE, DMT_BOOL, get_conf_upload_to_server, set_conf_upload_to_server, NULL, NULL},
{"download_from_server", &DMWRITE, DMT_BOOL, get_conf_download_from_server, set_conf_download_from_server, NULL, NULL},
{"auto_upload_delay", &DMWRITE, DMT_INT, get_conf_auto_upload_delay, set_conf_auto_upload_delay, NULL, NULL},
{"auto_upload_interval_time", &DMWRITE, DMT_INT, get_conf_auto_upload_interval, set_conf_auto_upload_interval, NULL, NULL},
{"auto_download_status", &DMREAD, DMT_STRING, get_conf_auto_download_status, NULL, NULL, NULL},
{"auto_upload_status", &DMREAD, DMT_STRING, get_conf_auto_upload_status, NULL, NULL, NULL},
{0}
};

static DMOBJ tConfRoot[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"X_AIS_Conf", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tConfParams, NULL},
{0}
};

static const char *const conf_mtk_paths[] = {
	"InternetGatewayDevice.X_AIS_Conf.",
	NULL
};

static const struct dm_module conf_mtk_module = {
	.name  = "mtk-x-ais-conf",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tConfRoot,
	.paths = conf_mtk_paths,
};
DM_MODULE_REGISTER(conf_mtk_module);
