/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.XMPP. and InternetGatewayDevice.LTE. -- the two
 *	placeholder objects of functions/tr098/xmpp.  The product has neither an
 *	XMPP client nor an LTE modem; the shell answers fixed values so the ACS
 *	finds the objects it provisions.
 *
 *	XMPP (P6, 15 parameters): one connection and one server, both instance
 *	"1" and not multi-instance (no add, no delete, as in the shell).  The
 *	writable leaves accept any value of their type and store nothing
 *	(xmpp_set_fake; the type check in front is mtk_input_contract).
 *
 *	LTE (P8, 12 parameters): radio figures, read only.
 *
 *	Both are part of a whole-tree request, like in the shell.
 */
#include "dmtr098.h"
#include "dm_registry.h"

#define XMPP_CONST(name, text)							\
static int get_xmpp_##name(char *refparam, struct dmctx *ctx, void *data,	\
			   char *instance, char **value)			\
{										\
	*value = text;								\
	return 0;								\
}

XMPP_CONST(status, "Disabled")
XMPP_CONST(username, "dummy")
XMPP_CONST(off, "false")
XMPP_CONST(jabber_id, "dummy@xmpp.local")
XMPP_CONST(domain, "xmpp.local")
XMPP_CONST(resource, "cwmp")
XMPP_CONST(algorithm, "ServerTable")
XMPP_CONST(last_change, "0001-01-01T00:00:00Z")
XMPP_CONST(keep_alive, "60")
XMPP_CONST(empty, "")
XMPP_CONST(zero, "0")
XMPP_CONST(server_port, "5222")

/* LTE figures of the shell, in the order it listed them */
XMPP_CONST(lte_rsrp0, "-95")
XMPP_CONST(lte_rsrp1, "-96")
XMPP_CONST(lte_rssi, "-70")
XMPP_CONST(lte_rsrq, "-10")
XMPP_CONST(lte_twenty, "20")

static int set_xmpp_fake(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tXmppServerParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"ServerAddress", &DMWRITE, DMT_STRING, get_xmpp_domain, set_xmpp_fake, NULL, NULL},
{"Port", &DMWRITE, DMT_UNINT, get_xmpp_server_port, set_xmpp_fake, NULL, NULL},
{"Enable", &DMWRITE, DMT_BOOL, get_xmpp_off, set_xmpp_fake, NULL, NULL},
{0}
};

static DMOBJ tXmppServerInstObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"1", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tXmppServerParams, NULL},
{0}
};

static DMLEAF tXmppConnectionParams[] = {
{"Status", &DMREAD, DMT_STRING, get_xmpp_status, NULL, NULL, NULL},
{"Username", &DMWRITE, DMT_STRING, get_xmpp_username, set_xmpp_fake, NULL, NULL},
{"UseTLS", &DMWRITE, DMT_BOOL, get_xmpp_off, set_xmpp_fake, NULL, NULL},
{"JabberID", &DMWRITE, DMT_STRING, get_xmpp_jabber_id, set_xmpp_fake, NULL, NULL},
{"Enable", &DMWRITE, DMT_BOOL, get_xmpp_off, set_xmpp_fake, NULL, NULL},
{"Domain", &DMWRITE, DMT_STRING, get_xmpp_domain, set_xmpp_fake, NULL, NULL},
{"Resource", &DMWRITE, DMT_STRING, get_xmpp_resource, set_xmpp_fake, NULL, NULL},
{"ServerConnectAlgorithm", &DMWRITE, DMT_STRING, get_xmpp_algorithm, set_xmpp_fake, NULL, NULL},
{"LastChangeDate", &DMREAD, DMT_TIME, get_xmpp_last_change, NULL, NULL, NULL},
{"KeepAliveInterval", &DMWRITE, DMT_UNINT, get_xmpp_keep_alive, set_xmpp_fake, NULL, NULL},
{"Password", &DMWRITE, DMT_STRING, get_xmpp_empty, set_xmpp_fake, NULL, NULL},
{"ServerConnectAttempts", &DMREAD, DMT_UNINT, get_xmpp_zero, NULL, NULL, NULL},
{0}
};

static DMOBJ tXmppConnectionChildObj[] = {
{"Server", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tXmppServerInstObj, NULL, NULL},
{0}
};

static DMOBJ tXmppConnectionInstObj[] = {
{"1", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tXmppConnectionChildObj, tXmppConnectionParams, NULL},
{0}
};

static DMOBJ tXmppObj[] = {
{"Connection", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tXmppConnectionInstObj, NULL, NULL},
{0}
};

static DMLEAF tLteParams[] = {
{"RSRP1", &DMREAD, DMT_INT, get_xmpp_lte_rsrp1, NULL, NULL, NULL},
{"UL_Frequency", &DMREAD, DMT_UNINT, get_xmpp_zero, NULL, NULL, NULL},
{"SINR", &DMREAD, DMT_INT, get_xmpp_lte_twenty, NULL, NULL, NULL},
{"DL_Frequency", &DMREAD, DMT_UNINT, get_xmpp_zero, NULL, NULL, NULL},
{"RSSI", &DMREAD, DMT_INT, get_xmpp_lte_rssi, NULL, NULL, NULL},
{"RSRQ", &DMREAD, DMT_INT, get_xmpp_lte_rsrq, NULL, NULL, NULL},
{"CellID", &DMREAD, DMT_UNINT, get_xmpp_zero, NULL, NULL, NULL},
{"TXPower", &DMREAD, DMT_INT, get_xmpp_zero, NULL, NULL, NULL},
{"RSRP0", &DMREAD, DMT_INT, get_xmpp_lte_rsrp0, NULL, NULL, NULL},
{"CINR0", &DMREAD, DMT_INT, get_xmpp_lte_twenty, NULL, NULL, NULL},
{"PCI", &DMREAD, DMT_UNINT, get_xmpp_zero, NULL, NULL, NULL},
{"CINR1", &DMREAD, DMT_INT, get_xmpp_lte_twenty, NULL, NULL, NULL},
{0}
};

static DMOBJ tXmppRootObj[] = {
{"XMPP", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tXmppObj, NULL, NULL},
{"LTE", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tLteParams, NULL},
{0}
};

static const char *const xmpp_mtk_paths[] = {
	"InternetGatewayDevice.XMPP.",
	"InternetGatewayDevice.LTE.",
	NULL
};

static const struct dm_module xmpp_mtk_module = {
	.name  = "mtk-xmpp-lte",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tXmppRootObj,
	.paths = xmpp_mtk_paths,
};
DM_MODULE_REGISTER(xmpp_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): Device.XMPP., the same tables (type A
 * of docs/plan/tr181_mtk_design.md).  LTE. is the product's own object
 * without a vendor prefix, kept as Device.LTE like Device.Account; the
 * standard Device.Cellular.Interface.{i} has another shape (T7). */
static DMOBJ tXmpp181RootObj[] = {
{"XMPP", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tXmppObj, NULL, NULL},
{"LTE", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tLteParams, NULL},
{0}
};

static const char *const xmpp181_mtk_paths[] = {
	"Device.XMPP.",
	"Device.LTE.",
	NULL
};

static const struct dm_module xmpp181_mtk_module = {
	.name  = "mtk-xmpp-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tXmpp181RootObj,
	.paths = xmpp181_mtk_paths,
};
DM_MODULE_REGISTER(xmpp181_mtk_module);
