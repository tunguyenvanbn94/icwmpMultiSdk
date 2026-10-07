/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.DOCSIS. -- a placeholder of functions/tr098/docsis.
 *	The product has no cable modem; the shell answers fixed values so an ACS
 *	that provisions the object finds it: one Interface and one
 *	UpstreamChannel, both instance "1" and not multi-instance (no add, no
 *	delete), an empty DownstreamChannel.  18 parameters, all read only.
 */
#include "dmtr098.h"
#include "dm_registry.h"

#define DOCSIS_CONST(name, text)						\
static int get_docsis_##name(char *refparam, struct dmctx *ctx, void *data,	\
			     char *instance, char **value)			\
{										\
	*value = text;								\
	return 0;								\
}

DOCSIS_CONST(status, "Down")
DOCSIS_CONST(version, "3.0")
DOCSIS_CONST(zero, "0")
DOCSIS_CONST(one, "1")
DOCSIS_CONST(default, "default")

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tDocsisConnParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"T2Timeouts", &DMREAD, DMT_UNINT, get_docsis_zero, NULL, NULL, NULL},
{"InvalidRangingRsps", &DMREAD, DMT_UNINT, get_docsis_zero, NULL, NULL, NULL},
{"InvalidRegRsps", &DMREAD, DMT_UNINT, get_docsis_zero, NULL, NULL, NULL},
{"Resets", &DMREAD, DMT_UNINT, get_docsis_zero, NULL, NULL, NULL},
{"LostSyncs", &DMREAD, DMT_UNINT, get_docsis_zero, NULL, NULL, NULL},
{"T1Timeouts", &DMREAD, DMT_UNINT, get_docsis_zero, NULL, NULL, NULL},
{"InvalidMaps", &DMREAD, DMT_UNINT, get_docsis_zero, NULL, NULL, NULL},
{"InvalidUcds", &DMREAD, DMT_UNINT, get_docsis_zero, NULL, NULL, NULL},
{0}
};

static DMOBJ tDocsisIfChildObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"ConnectivityStatus", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tDocsisConnParams, NULL},
{0}
};

static DMLEAF tDocsisIfParams[] = {
{"Status", &DMREAD, DMT_STRING, get_docsis_status, NULL, NULL, NULL},
{"DOCSISVersion", &DMREAD, DMT_STRING, get_docsis_version, NULL, NULL, NULL},
{0}
};

static DMOBJ tDocsisIfInstObj[] = {
{"1", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDocsisIfChildObj, tDocsisIfParams, NULL},
{0}
};

static DMLEAF tDocsisUpStatusParams[] = {
{"TxPower", &DMREAD, DMT_INT, get_docsis_zero, NULL, NULL, NULL},
{"ModulationType", &DMREAD, DMT_STRING, get_docsis_default, NULL, NULL, NULL},
{"T4Timeouts", &DMREAD, DMT_UNINT, get_docsis_zero, NULL, NULL, NULL},
{"T3Timeouts", &DMREAD, DMT_UNINT, get_docsis_zero, NULL, NULL, NULL},
{"RangingAborteds", &DMREAD, DMT_UNINT, get_docsis_zero, NULL, NULL, NULL},
{0}
};

static DMOBJ tDocsisUpChildObj[] = {
{"Status", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tDocsisUpStatusParams, NULL},
{0}
};

static DMLEAF tDocsisUpParams[] = {
{"Width", &DMREAD, DMT_UNINT, get_docsis_zero, NULL, NULL, NULL},
{"ID", &DMREAD, DMT_UNINT, get_docsis_one, NULL, NULL, NULL},
{"Frequency", &DMREAD, DMT_UNINT, get_docsis_zero, NULL, NULL, NULL},
{0}
};

static DMOBJ tDocsisUpInstObj[] = {
{"1", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDocsisUpChildObj, tDocsisUpParams, NULL},
{0}
};

static DMOBJ tDocsisObj[] = {
{"Interface", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDocsisIfInstObj, NULL, NULL},
{"UpstreamChannel", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDocsisUpInstObj, NULL, NULL},
{"DownstreamChannel", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{0}
};

static DMOBJ tDocsisRoot[] = {
{"DOCSIS", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDocsisObj, NULL, NULL},
{0}
};

static const char *const docsis_mtk_paths[] = {
	"InternetGatewayDevice.DOCSIS.",
	NULL
};

static const struct dm_module docsis_mtk_module = {
	.name  = "mtk-docsis",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tDocsisRoot,
	.paths = docsis_mtk_paths,
};
DM_MODULE_REGISTER(docsis_mtk_module);
