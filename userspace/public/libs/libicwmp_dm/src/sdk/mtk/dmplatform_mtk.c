/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	MTK / Airoha OpenWrt platform (--with-platform=mtk).
 *
 *	Storage is UCI like the stock iopsys build, but the TR-098 data model
 *	itself is the easycwmp shell function library of the HNI product tree
 *	(cwmpclient/ext/openwrt/scripts/functions/{common,tr098,tr143}, ~820
 *	parameters incl. X_AIS_*), run in a persistent child through
 *	sdk/mtk/compat/icwmp_dm.sh and sdk/mtk/compat/dmscript.c.  That library is
 *	what the ACS of the operator has been provisioned against; re-implementing
 *	it in C is done object by object: a C object listed in
 *	sdk/mtk/dm098/ (its .paths claim) is served by the static engine
 *	and its subtree is dropped from the script replies, everything else
 *	comes from the script.
 *
 *	Request flow (dm_platform_param_method, called by dm_entry_param_method
 *	before the static walk and by dm_entry_apply for every VALUESET):
 *
 *	  path inside a native object         -> return 0, static engine only
 *	  GPV/GPN/GPA/Inform                  -> script, then static walk when the
 *	                                         path also covers a native object
 *	                                         (root, ...), lists merged by
 *	                                         add_list_paramameter (sorted,
 *	                                         duplicates dropped).  GPV of such
 *	                                         a path asks the script only for
 *	                                         the children it still owns
 *	                                         (mtk_script_values), Inform only
 *	                                         for the forced-inform names it
 *	                                         kept the first time (mtk_inform)
 *	  SPV VALUECHECK                     -> script "set_check" (validate +
 *	                                         queue in the shell), remember in
 *	                                         ctx->set_list_tmp
 *	  SPV VALUESET                        -> nothing (queued already)
 *	  dm_platform_commit                  -> script "set_apply" <ParameterKey>
 *	                                         = run the queued setters, uci commit
 *	  dm_platform_revert                  -> script "set_abort"
 *	  SPA VALUECHECK/VALUESET             -> cwmp.@notifications[0] lists of
 *	                                         libtr098 (dm_set_parameter_notification),
 *	                                         same store as the static tree
 *	  AddObject/DeleteObject              -> script "add"/"delete"
 *	  dm_platform_restart_services        -> uci commit of the packages the
 *	                                         static tree changed + script
 *	                                         "apply_service" (ucitrack restarts)
 *
 *	ManagementServer.* is native C (dm098/managementserver_mtk.c over the
 *	portable tr098/managementserver.c), and still reads and writes the
 *	product's stores: easycwmp.@acs[0]/@local[0] stays the config of record
 *	(WebUI, DHCP option 43 parser, stuncd), STUN is stun.@stun[0].  icwmpd
 *	mirrors easycwmp into its own cwmp UCI config at start / reload / end of
 *	session (icwmp sdk/mtk/icwmp_mtk.c), so a native setter of an easycwmp
 *	leaf ends the session with END_SESSION_RELOAD, and so does a script SPV
 *	under ManagementServer.* or of DeviceInfo.ProvisioningCode (below).
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <pthread.h>

#include "dmtr098.h"
#include "dmmem.h"
#include "dmuci.h"
#include "dmubus.h"
#include "dmjson.h"
#include "dmentry.h"
#include "dmcommon.h"
#include "sdk/sdk.h"
#ifdef DM_MTK_SCRIPT_COMPAT
#include "compat/dmscript.h"
#endif
#include "dm_registry.h"
#include "dmmtk.h"
#include "root_mtk.h"

extern struct list_head head_package_change;   /* dmentry.c */
extern char *DMT_TYPE[];                       /* dmtr098.c */

#define MTK_MS_PREFIX   "InternetGatewayDevice.ManagementServer."
#define MTK_PROVCODE    "InternetGatewayDevice.DeviceInfo.ProvisioningCode"
#define MTK_CR_URL      MTK_MS_PREFIX "ConnectionRequestURL"

/* ------------------------------------------------------------------------ */
/* native claims                                                             */
/* ------------------------------------------------------------------------ */

static int mtk_is_object(const char *path)
{
	size_t l = strlen(path);

	return l > 0 && path[l - 1] == '.';
}

/* path is inside an object a data model module owns.  The claim list is the
 * union of the .paths of every module linked into this build (dm_registry.c),
 * so porting an object to C never touches this file. */
static int mtk_is_native(const char *path)
{
	return dm_registry_owns(DM_MODEL_TR098, path);
}

/* an object path above a native object ("" or "InternetGatewayDevice."):
 * the static tree must be walked after the script */
static int mtk_covers_native(const char *path)
{
	if (!path || path[0] == '\0')
		return 1;
	if (!mtk_is_object(path))
		return 0;
	return dm_registry_covers(DM_MODEL_TR098, path);
}

/* ------------------------------------------------------------------------ */
/* Từ đây tới hết mtk_merge_static() là lớp compat: mọi thứ gọi               */
/* dmscript_request().  Với --disable-dm-script-compat cả khối biến mất khỏi  */
/* translation unit, không còn implicit declaration hay undefined reference.  */
/* ------------------------------------------------------------------------ */
#ifdef DM_MTK_SCRIPT_COMPAT

/* the script wants a full path; the engine passes "" for the root */
static const char *mtk_script_path(const char *inparam)
{
	static char root[80];

	if (inparam && inparam[0])
		return inparam;
	snprintf(root, sizeof(root), "%s.", dmroot);
	return root;
}

/* "xsd:unsignedInt" of the script -> the static DMT_TYPE string (never freed) */
static char *mtk_xsd_type(const char *t)
{
	int i;

	if (!t || !t[0])
		return DMT_TYPE[DMT_STRING];
	for (i = DMT_STRING; i <= DMT_BASE64; i++)
		if (strcasecmp(t, DMT_TYPE[i]) == 0)
			return DMT_TYPE[i];
	/* xsd:IPv4Address, xsd:IPv6Address, ... are strings on the wire */
	return DMT_TYPE[DMT_STRING];
}

/* ManagementServer.ConnectionRequestURL: the script builds it from
 * easycwmp.@local[0].ip, which the easycwmpd init logic filled once at
 * start.  icwmpd tracks the address of cwmp.cpe.interface with netlink
 * (varstate cwmp.cpe.ip/ipv6) and its CR server listens on cwmp.cpe.port, so
 * the value the ACS gets is built from those, with the NAT override
 * cwmp.cpe.cr_host/cr_port of the BDK build and the path of the product
 * (easycwmp.@local[0].path, "ConnectionRequest").  NULL = keep the script's. */
static char *mtk_cr_url(void)
{
	char *ip = NULL, *ip6 = NULL, *port = NULL, *path = NULL, *host = NULL;
	char *cr_host = NULL, *cr_port = NULL, *wan_type = NULL, *url = NULL;

	dmuci_get_option_value_string("cwmp", "cpe", "cr_host", &cr_host);
	dmuci_get_option_value_string("cwmp", "cpe", "cr_port", &cr_port);
	dmuci_get_option_value_string("cwmp", "cpe", "port", &port);
	dmuci_get_option_value_string("easycwmp", "@local[0]", "path", &path);
	dmuci_get_option_value_string("easycwmp", "@local[0]", "wan_type", &wan_type);
	dmuci_get_varstate_string("cwmp", "cpe", "ip", &ip);
	dmuci_get_varstate_string("cwmp", "cpe", "ipv6", &ip6);
	if (cr_host && cr_host[0]) {
		if (strchr(cr_host, ':') && cr_host[0] != '[')
			dmasprintf(&host, "[%s]", cr_host);
		else
			host = cr_host;
	} else if (wan_type && strcmp(wan_type, "ipv6") == 0 && ip6 && ip6[0]) {
		dmasprintf(&host, "[%s]", ip6);
	} else if (ip && ip[0]) {
		host = ip;
	} else if (ip6 && ip6[0]) {
		dmasprintf(&host, "[%s]", ip6);
	}
	if (!host)
		return NULL;
	if (cr_port && cr_port[0] && strcmp(cr_port, "0") != 0)
		port = cr_port;
	if (!port || !port[0])
		port = "7547";
	dmasprintf(&url, "http://%s:%s/%s", host, port, (path && path[0]) ? path : "");
	return url;
}

/* ------------------------------------------------------------------------ */
/* reply lines -> engine lists                                               */
/* ------------------------------------------------------------------------ */

enum mtk_kind {
	MTK_KIND_VALUE,        /* {"parameter","value","type"} -> list_parameter (GPV, Inform) */
	MTK_KIND_NAME,         /* {"parameter","writable"}     -> list_parameter (GPN) */
	MTK_KIND_NOTIF,        /* {"parameter",...} names only -> list_parameter with notification (GPA) */
	MTK_KIND_STATUS,       /* {"status","instance"} / faults (set_apply, add, delete) */
};

struct mtk_reply {
	struct dmctx *ctx;
	enum mtk_kind kind;
	int fault;              /* first fault_code, 0 = none */
	int status;             /* last "status" (1 ok) */
	int count;              /* parameters added */
	int record_faults;      /* set_apply: per-parameter faults into ctx->list_fault_param */
	char instance[16];      /* add: new instance number */
	FILE *fp;               /* enabled-notify: write entries here instead of the list */
	const char *notif;      /* enabled-notify: notification level of the object/param */
	char *single_value;     /* value of the one parameter asked (plain malloc, caller frees) */
};

static const char *jstr(json_object *o, const char *key)
{
	json_object *v = NULL;

	if (!json_object_object_get_ex(o, key, &v) || !v)
		return NULL;
	return json_object_get_string(v);
}

static int mtk_line_cb(json_object *line, void *priv)
{
	struct mtk_reply *r = priv;
	const char *param = jstr(line, "parameter");
	const char *fault = jstr(line, "fault_code");
	const char *status = jstr(line, "status");
	const char *instance = jstr(line, "instance");

	if (fault && fault[0]) {
		int code = atoi(fault);

		if (code < 9000)
			code = FAULT_9002;
		if (!r->fault)
			r->fault = code;
		if (r->record_faults && param && param[0])
			add_list_fault_param(r->ctx, (char *)param, code);
		return 0;
	}
	if (status) {
		r->status = atoi(status);
		if (instance && instance[0])
			snprintf(r->instance, sizeof(r->instance), "%s", instance);
		return 0;
	}
	if (!param || !param[0])
		return 0;
	if (mtk_is_native(param))
		return 0;                                  /* the static tree owns it */

	switch (r->kind) {
	case MTK_KIND_VALUE: {
		const char *value = jstr(line, "value");
		const char *type = jstr(line, "type");
		char *ovr = NULL;

		if (strcmp(param, MTK_CR_URL) == 0 && (ovr = mtk_cr_url()) != NULL)
			value = ovr;
		if (r->fp) {
			/* DM_ENABLED_NOTIFY line, same format as enabled_notify_check_param() */
			dmjson_fprintf(r->fp, 4, DMJSON_ARGS{{"parameter", (char *)param}, {"notification", (char *)r->notif},
			                                      {"value", (char *)(value ? value : "")}, {"type", mtk_xsd_type(type)}});
		} else if (r->single_value) {
			/* value-change thread: no dmmem, one parameter */
			free(r->single_value);
			r->single_value = strdup(value ? value : "");
		} else {
			add_list_paramameter(r->ctx, dmstrdup(param), dmstrdup(value ? value : ""), mtk_xsd_type(type), NULL, 0);
		}
		r->count++;
		break;
	}
	case MTK_KIND_NAME: {
		const char *w = jstr(line, "writable");

		add_list_paramameter(r->ctx, dmstrdup(param), (w && w[0] == '1') ? "1" : "0", NULL, NULL, 0);
		r->count++;
		break;
	}
	case MTK_KIND_NOTIF:
		if (!mtk_is_object(param)) {
			char *n = dm_get_parameter_notification(r->ctx, (char *)param);

			add_list_paramameter(r->ctx, dmstrdup(param), n, NULL, NULL, 0);
			r->count++;
		}
		break;
	case MTK_KIND_STATUS:
		break;
	}
	return 0;
}

static void mtk_reply_init(struct mtk_reply *r, struct dmctx *ctx, enum mtk_kind kind)
{
	memset(r, 0, sizeof(*r));
	r->ctx = ctx;
	r->kind = kind;
	r->status = -1;
}

/* dmscript transport failure -> CWMP fault */
static int mtk_transport_fault(const char *what, const char *path)
{
	fprintf(stderr, "icwmp mtk: script %s %s failed (child dead or timeout)\n", what, path ? path : "");
	return FAULT_9002;
}

/* ------------------------------------------------------------------------ */
/* values of a subtree, minus what the C tree owns                           */
/* ------------------------------------------------------------------------ */

/* The script does not know which paths a C module owns.  Asked get_value for
 * an object above a native one ("InternetGatewayDevice.", "...WANDevice.")
 * it ran the getter of every parameter below, the native ones included, and
 * mtk_line_cb() dropped those lines: a GetParameterValues of the root cost
 * the whole easycwmp walk plus the C walk.
 *
 * For such an object the script is now asked for its children (get_name,
 * next_level 1: names only, no getter runs).  A native child is skipped, a
 * child above a native object is descended into, every other child gets one
 * get_value.  The script still answers every path it owns; only the getters
 * whose line was going to be dropped no longer run. */

#define MTK_PRUNE_DEPTH 12      /* deeper than the tree; past it, the whole subtree */

/* non-native parameter names of a reply, strictly below parent */
struct mtk_children {
	const char *parent;
	size_t plen;
	char **names;
	int n, cap;
	int fault;              /* first fault_code of the reply, 0 = none */
	int oom;
};

static void mtk_children_add(struct mtk_children *c, const char *name)
{
	if (c->n == c->cap) {
		int cap = c->cap ? c->cap * 2 : 16;
		char **nn = realloc(c->names, cap * sizeof(*nn));

		if (!nn) {
			c->oom = 1;
			return;
		}
		c->names = nn;
		c->cap = cap;
	}
	if ((c->names[c->n] = strdup(name)) == NULL) {
		c->oom = 1;
		return;
	}
	c->n++;
}

static int mtk_child_cb(json_object *line, void *priv)
{
	struct mtk_children *c = priv;
	const char *param = jstr(line, "parameter");
	const char *fault = jstr(line, "fault_code");

	if (fault && fault[0]) {
		int code = atoi(fault);

		if (!c->fault)
			c->fault = code < 9000 ? FAULT_9002 : code;
		return 0;
	}
	if (!param || strncmp(param, c->parent, c->plen) != 0 || !param[c->plen])
		return 0;
	if (mtk_is_native(param))
		return 0;
	mtk_children_add(c, param);
	return 0;
}

static void mtk_children_free(struct mtk_children *c)
{
	int i;

	for (i = 0; i < c->n; i++)
		free(c->names[i]);
	free(c->names);
	c->names = NULL;
	c->n = c->cap = 0;
}

/* get_value of path into r (any kind that takes value lines: list, fp or
 * single_value).  Same return as dmscript_request(); an unknown path leaves
 * its fault in r->fault like a plain get_value does. */
/* paths per get_name_list / get_value_list request */
#define MTK_LIST_MAX 64

/* Does the driver know the *_list commands?  -1 not asked yet.  An older
 * icwmp_dm.sh answers an unknown command with fault 9000 and nothing else. */
static int mtk_list_ok = -1;

static int mtk_list_probe_cb(json_object *line, void *priv)
{
	if (jstr(line, "fault_code"))
		*(int *)priv = 0;
	return 0;
}

/* {"cmd":cmd,"params":[paths[0..n)],"next_level":nl} */
static int mtk_list_request(const char *cmd, char **paths, int n, const char *nl,
			    dmscript_line_cb cb, void *priv)
{
	json_object *req = json_object_new_object();
	json_object *arr = json_object_new_array();
	int i, rc;

	if (!req || !arr) {
		if (req) json_object_put(req);
		if (arr) json_object_put(arr);
		return -1;
	}
	json_object_object_add(req, "cmd", json_object_new_string(cmd));
	for (i = 0; i < n; i++)
		json_object_array_add(arr, json_object_new_string(paths[i]));
	json_object_object_add(req, "params", arr);
	if (nl)
		json_object_object_add(req, "next_level", json_object_new_string(nl));
	rc = dmscript_call(req, cb, priv);
	json_object_put(req);
	return rc;
}

static int mtk_list_supported(void)
{
	if (mtk_list_ok < 0) {
		int ok = 1;

		if (mtk_list_request("get_value_list", NULL, 0, NULL, mtk_list_probe_cb, &ok) != 0)
			return 0;		/* ask again next time */
		mtk_list_ok = ok;
	}
	return mtk_list_ok;
}

/* name is strictly below one of the objects in level */
static int mtk_below_one_of(const char *name, struct mtk_children *level)
{
	int i;

	for (i = 0; i < level->n; i++) {
		size_t l = strlen(level->names[i]);

		if (strncmp(name, level->names[i], l) == 0 && name[l])
			return 1;
	}
	return 0;
}

static int mtk_script_values_walk(struct mtk_reply *r, const char *path, int depth);

/* The walk of mtk_script_values_walk(), one level of the tree at a time:
 * one get_name_list for all the objects of a level, one get_value_list for
 * all the leaves and plain objects it turned up.  A request costs the
 * script a json_load, a subshell and a round trip whatever it carries; the
 * walk of InternetGatewayDevice. took ~70 of them, a few per level now. */
static int mtk_script_values_levels(struct mtk_reply *r, const char *path, int depth)
{
	struct mtk_children level, kids, next, leaves;
	int i, d, rc = 0;

	memset(&level, 0, sizeof(level));
	mtk_children_add(&level, path);
	for (d = depth; level.n && rc == 0; d++) {
		if (level.oom) {
			rc = -1;
			break;
		}
		if (d >= MTK_PRUNE_DEPTH) {
			/* deeper than the tree: the whole subtrees */
			for (i = 0; i < level.n && rc == 0; i += MTK_LIST_MAX)
				rc = mtk_list_request("get_value_list", level.names + i,
						      level.n - i < MTK_LIST_MAX ? level.n - i : MTK_LIST_MAX,
						      NULL, mtk_line_cb, r);
			break;
		}
		memset(&kids, 0, sizeof(kids));
		kids.parent = "";
		for (i = 0; i < level.n && rc == 0; i += MTK_LIST_MAX)
			rc = mtk_list_request("get_name_list", level.names + i,
					      level.n - i < MTK_LIST_MAX ? level.n - i : MTK_LIST_MAX,
					      "1", mtk_child_cb, &kids);
		if (rc == 0 && kids.oom)
			rc = -1;
		if (d == depth && kids.fault && !kids.n && !r->fault)
			r->fault = kids.fault;	/* unknown to the script, like get_value */

		memset(&next, 0, sizeof(next));
		memset(&leaves, 0, sizeof(leaves));
		for (i = 0; rc == 0 && i < kids.n; i++) {
			const char *k = kids.names[i];

			if (!mtk_below_one_of(k, &level))
				continue;	/* the object itself, or a stray line */
			if (mtk_is_object(k) && mtk_covers_native(k))
				mtk_children_add(&next, k);
			else
				mtk_children_add(&leaves, k);
		}
		if (rc == 0 && (next.oom || leaves.oom))
			rc = -1;
		for (i = 0; rc == 0 && i < leaves.n; i += MTK_LIST_MAX)
			rc = mtk_list_request("get_value_list", leaves.names + i,
					      leaves.n - i < MTK_LIST_MAX ? leaves.n - i : MTK_LIST_MAX,
					      NULL, mtk_line_cb, r);
		mtk_children_free(&kids);
		mtk_children_free(&leaves);
		mtk_children_free(&level);
		level = next;
	}
	mtk_children_free(&level);
	return rc;
}

/* ------------------------------------------------------------------------ */
/* prefetch (sdk/sdk.h dm_platform_prefetch_values)                          */
/* ------------------------------------------------------------------------ */

/* The value-change check asks GET_VALUE of every DM_ENABLED_NOTIFY entry in
 * turn, at the start of every session and on every "ubus call tr069
 * notify" (value_monitoring, 30 s): with an ACS notification on an object
 * of the script, a hundred requests each time.  The values are fetched with
 * get_value_list beforehand and the GET_VALUE of one of them is answered
 * from here.  Plain malloc: outlives the dmctx of the caller. */
struct mtk_prefetch {
	char **name, **value, **type;
	int n, cap;
	int active;
};
static struct mtk_prefetch mtk_pf;

static int mtk_prefetch_cb(json_object *line, void *priv)
{
	struct mtk_prefetch *pf = priv;
	const char *param = jstr(line, "parameter");
	const char *value = jstr(line, "value");
	const char *type = jstr(line, "type");

	if (jstr(line, "fault_code") || !param || !param[0] || mtk_is_object(param) || mtk_is_native(param))
		return 0;
	if (pf->n == pf->cap) {
		int cap = pf->cap ? pf->cap * 2 : 32;
		char **nn = realloc(pf->name, cap * sizeof(char *));
		char **nv, **nt;

		if (!nn)
			return 0;
		pf->name = nn;
		nv = realloc(pf->value, cap * sizeof(char *));
		if (!nv)
			return 0;
		pf->value = nv;
		nt = realloc(pf->type, cap * sizeof(char *));
		if (!nt)
			return 0;
		pf->type = nt;
		pf->cap = cap;
	}
	pf->name[pf->n] = strdup(param);
	pf->value[pf->n] = strdup(value ? value : "");
	pf->type[pf->n] = strdup(type ? type : "");
	if (!pf->name[pf->n] || !pf->value[pf->n] || !pf->type[pf->n]) {
		free(pf->name[pf->n]);
		free(pf->value[pf->n]);
		free(pf->type[pf->n]);
		return 0;
	}
	pf->n++;
	return 0;
}

static void mtk_prefetch_clear(void)
{
	int i;

	for (i = 0; i < mtk_pf.n; i++) {
		free(mtk_pf.name[i]);
		free(mtk_pf.value[i]);
		free(mtk_pf.type[i]);
	}
	free(mtk_pf.name);
	free(mtk_pf.value);
	free(mtk_pf.type);
	memset(&mtk_pf, 0, sizeof(mtk_pf));
}

static int mtk_prefetch(char **params, int n)
{
	char **want;
	int i, m = 0, rc = 0;

	mtk_prefetch_clear();
	if (!mtk_list_supported())
		return 0;
	want = malloc(n * sizeof(char *));
	if (!want)
		return 0;
	for (i = 0; i < n; i++) {
		/* leaves the script serves; objects and native ones go the usual way */
		if (params[i] && params[i][0] && !mtk_is_object(params[i]) && !mtk_is_native(params[i]))
			want[m++] = params[i];
	}
	for (i = 0; i < m && rc == 0; i += MTK_LIST_MAX)
		rc = mtk_list_request("get_value_list", want + i, m - i < MTK_LIST_MAX ? m - i : MTK_LIST_MAX,
				      NULL, mtk_prefetch_cb, &mtk_pf);
	free(want);
	if (rc != 0) {
		mtk_prefetch_clear();	/* half a prefetch: ask one by one */
		return 0;
	}
	mtk_pf.active = 1;
	return mtk_pf.n;
}

/* the prefetched value of path, NULL when it was not prefetched */
static const char *mtk_prefetch_value(const char *path)
{
	int i;

	if (!mtk_pf.active)
		return NULL;
	for (i = 0; i < mtk_pf.n; i++)
		if (strcmp(mtk_pf.name[i], path) == 0)
			return mtk_pf.value[i];
	return NULL;
}

/* the prefetched value of path into ctx; 0 when it was not prefetched */
static int mtk_prefetch_get(struct dmctx *ctx, const char *path)
{
	int i;

	if (!mtk_pf.active)
		return 0;
	for (i = 0; i < mtk_pf.n; i++) {
		if (strcmp(mtk_pf.name[i], path) == 0) {
			add_list_paramameter(ctx, dmstrdup(mtk_pf.name[i]), dmstrdup(mtk_pf.value[i]),
					     mtk_xsd_type(mtk_pf.type[i]), NULL, 0);
			return 1;
		}
	}
	return 0;
}

static int mtk_script_values(struct mtk_reply *r, const char *path, int depth)
{
	if (!mtk_is_object(path) || !mtk_covers_native(path) || depth >= MTK_PRUNE_DEPTH)
		return dmscript_request(mtk_line_cb, r, "get_value", "param", path, NULL);
	if (mtk_list_supported())
		return mtk_script_values_levels(r, path, depth);
	return mtk_script_values_walk(r, path, depth);
}

/* one get_name per object, recursively: drivers without the *_list commands */
static int mtk_script_values_walk(struct mtk_reply *r, const char *path, int depth)
{
	struct mtk_children c;
	int i, rc = 0;

	if (!mtk_is_object(path) || !mtk_covers_native(path) || depth >= MTK_PRUNE_DEPTH)
		return dmscript_request(mtk_line_cb, r, "get_value", "param", path, NULL);

	memset(&c, 0, sizeof(c));
	c.parent = path;
	c.plen = strlen(path);
	if (dmscript_request(mtk_child_cb, &c, "get_name", "param", path, "next_level", "1", NULL) != 0) {
		mtk_children_free(&c);
		return -1;
	}
	if (c.oom) {
		mtk_children_free(&c);
		return dmscript_request(mtk_line_cb, r, "get_value", "param", path, NULL);
	}
	if (c.fault && !c.n && !r->fault)
		r->fault = c.fault;
	for (i = 0; i < c.n && rc == 0; i++) {
		if (!mtk_is_object(c.names[i]) || !mtk_covers_native(c.names[i]) || depth + 1 >= MTK_PRUNE_DEPTH)
			rc = dmscript_request(mtk_line_cb, r, "get_value", "param", c.names[i], NULL);
		else
			rc = mtk_script_values_walk(r, c.names[i], depth + 1);
	}
	mtk_children_free(&c);
	return rc;
}

/* ------------------------------------------------------------------------ */
/* commands                                                                  */
/* ------------------------------------------------------------------------ */

/* A set, add or delete went through the script since its last
 * "apply_service": only then has the script packages of its own to restart
 * (its changed-packages file).  Otherwise the end of the session runs the
 * queued commands in C and leaves the script alone.  Callers are serialised
 * by icwmpd's mutex_session_send. */
static int mtk_script_changed;

static int mtk_get_value(struct dmctx *ctx, const char *path)
{
	struct mtk_reply r;

	if (path && path[0] && !mtk_is_object(path) && mtk_prefetch_get(ctx, path))
		return 0;
	mtk_reply_init(&r, ctx, MTK_KIND_VALUE);
	if (mtk_script_values(&r, mtk_script_path(path), 0) != 0)
		return mtk_transport_fault("get_value", path);
	if (r.fault && !r.count)
		return r.fault;
	return 0;
}

static int mtk_get_name(struct dmctx *ctx, const char *path, int nextlevel)
{
	struct mtk_reply r;

	mtk_reply_init(&r, ctx, MTK_KIND_NAME);
	if (dmscript_request(mtk_line_cb, &r, "get_name", "param", mtk_script_path(path),
	                     "next_level", nextlevel ? "1" : "0", NULL) != 0)
		return mtk_transport_fault("get_name", path);
	if (r.fault && !r.count)
		return r.fault;
	return 0;
}

/* GPA: names come from get_name (nextlevel 0), the notification from the
 * cwmp UCI lists */
static int mtk_get_notification(struct dmctx *ctx, const char *path)
{
	struct mtk_reply r;

	mtk_reply_init(&r, ctx, MTK_KIND_NOTIF);
	if (dmscript_request(mtk_line_cb, &r, "get_name", "param", mtk_script_path(path), "next_level", "0", NULL) != 0)
		return mtk_transport_fault("get_name", path);
	if (r.fault && !r.count)
		return r.fault;
	return 0;
}

/* does the script know this path (parameter or object)?  0 or a fault */
static int mtk_exists(struct dmctx *ctx, const char *path)
{
	struct mtk_reply r;

	mtk_reply_init(&r, ctx, MTK_KIND_STATUS);   /* names not added anywhere */
	if (dmscript_request(mtk_line_cb, &r, "get_name", "param", mtk_script_path(path), "next_level", "0", NULL) != 0)
		return mtk_transport_fault("get_name", path);
	return r.fault;                              /* 0 when at least the object answered */
}

static int mtk_set_value(struct dmctx *ctx, const char *inparam)
{
	struct mtk_reply r;

	if (!inparam || !inparam[0] || mtk_is_object(inparam))
		return FAULT_9005;
	if (ctx->setaction == VALUESET)
		return 0;                                  /* queued in the shell at VALUECHECK */
	mtk_reply_init(&r, ctx, MTK_KIND_STATUS);
	mtk_script_changed = 1;
	if (dmscript_request(mtk_line_cb, &r, "set_check", "param", inparam, "value", ctx->in_value, NULL) != 0)
		return mtk_transport_fault("set_check", inparam);
	if (r.fault)
		return r.fault;
	add_set_list_tmp(ctx, (char *)inparam, ctx->in_value, 0);
	return 0;
}

static int mtk_set_notification(struct dmctx *ctx, const char *inparam)
{
	if (!inparam || !inparam[0])
		return FAULT_9009;
	if (ctx->setaction == VALUECHECK) {
		int fault;

		if (dmcommon_check_notification_value(ctx->in_notification) < 0)
			return FAULT_9003;
		fault = mtk_exists(ctx, inparam);
		if (fault)
			return fault;
		if (ctx->notification_change)
			add_set_list_tmp(ctx, (char *)inparam, ctx->in_notification, 0);
		/* the object may also cover native leaves (root): let the engine
		 * record those too, "nothing static" is not an error */
		if (mtk_covers_native(inparam))
			dm_entry_set_notification(ctx);
		return 0;
	}
	if (!ctx->notification_change)
		return 0;
	if (dm_set_parameter_notification(ctx, (char *)inparam, ctx->in_notification) < 0)
		return FAULT_9003;
	/* rebuild DM_ENABLED_NOTIFY at the end of the session (dm_entry_reload_enabled_notify) */
	cwmp_set_end_session(END_SESSION_RELOAD);
	return 0;
}

static int mtk_add_object(struct dmctx *ctx, const char *inparam, const char *key)
{
	struct mtk_reply r;

	if (!inparam || !inparam[0] || !mtk_is_object(inparam))
		return FAULT_9005;
	mtk_reply_init(&r, ctx, MTK_KIND_STATUS);
	mtk_script_changed = 1;
	if (dmscript_request(mtk_line_cb, &r, "add", "param", inparam, "key", key ? key : "", NULL) != 0)
		return mtk_transport_fault("add", inparam);
	if (r.fault)
		return r.fault;
	if (r.status != 1 || !r.instance[0])
		return FAULT_9002;
	ctx->addobj_instance = dmstrdup(r.instance);
	dmuci_set_value("cwmp", "acs", "ParameterKey", (char *)(key ? key : ""));
	dmuci_commit();
	return 0;
}

static int mtk_del_object(struct dmctx *ctx, const char *inparam, const char *key)
{
	struct mtk_reply r;

	if (!inparam || !inparam[0] || !mtk_is_object(inparam))
		return FAULT_9005;
	mtk_reply_init(&r, ctx, MTK_KIND_STATUS);
	mtk_script_changed = 1;
	if (dmscript_request(mtk_line_cb, &r, "delete", "param", inparam, "key", key ? key : "", NULL) != 0)
		return mtk_transport_fault("delete", inparam);
	if (r.fault)
		return r.fault;
	if (r.status != 1)
		return FAULT_9002;
	dmuci_set_value("cwmp", "acs", "ParameterKey", (char *)(key ? key : ""));
	dmuci_commit();
	return 0;
}

/* The script's "inform" runs the getter of every forced-inform parameter of
 * the library, and all of them but DeviceSummary are native by now, so their
 * lines were dropped on every Inform.  The first Inform of this process runs
 * the full "inform" and remembers the names the script kept; later ones ask
 * get_value for just those, and nothing at all once the last one is ported.
 * Nothing is remembered when a kept name carries an instance number (the set
 * would follow the instances) or the reply had a fault.  A remembered name
 * the script no longer answers sends that Inform back to the full walk. */
static pthread_mutex_t mtk_inform_lock = PTHREAD_MUTEX_INITIALIZER;
static int mtk_inform_known;            /* mtk_inform_names is the script's whole set */
static char **mtk_inform_names;
static int mtk_inform_n;

struct mtk_inform_learn {
	struct mtk_reply *r;
	struct mtk_children c;
};

static int mtk_inform_learn_cb(json_object *line, void *priv)
{
	struct mtk_inform_learn *l = priv;

	mtk_line_cb(line, l->r);
	return mtk_child_cb(line, &l->c);
}

/* a path segment made of digits only */
static int mtk_has_instance(const char *path)
{
	const char *p = path;

	while (*p) {
		size_t n = strcspn(p, ".");

		if (n && strspn(p, "0123456789") >= n)
			return 1;
		p += n;
		if (*p)
			p++;
	}
	return 0;
}

static void mtk_inform_forget(void)
{
	int i;

	for (i = 0; i < mtk_inform_n; i++)
		free(mtk_inform_names[i]);
	free(mtk_inform_names);
	mtk_inform_names = NULL;
	mtk_inform_n = 0;
	mtk_inform_known = 0;
}

/* mtk_inform_lock held */
static int mtk_inform_full(struct mtk_reply *r)
{
	struct mtk_inform_learn l;
	int i, keep;

	memset(&l, 0, sizeof(l));
	l.r = r;
	l.c.parent = "";
	if (dmscript_request(mtk_inform_learn_cb, &l, "inform", NULL) != 0) {
		mtk_children_free(&l.c);
		return -1;
	}
	keep = !l.c.fault && !l.c.oom;
	for (i = 0; keep && i < l.c.n; i++)
		if (mtk_has_instance(l.c.names[i]))
			keep = 0;
	mtk_inform_forget();
	if (keep) {
		mtk_inform_names = l.c.names;
		mtk_inform_n = l.c.n;
		mtk_inform_known = 1;
	} else {
		mtk_children_free(&l.c);
	}
	return 0;
}

static int mtk_inform(struct dmctx *ctx)
{
	struct mtk_reply r;
	int i, rc = 0;

	mtk_reply_init(&r, ctx, MTK_KIND_VALUE);
	pthread_mutex_lock(&mtk_inform_lock);
	if (!mtk_inform_known) {
		rc = mtk_inform_full(&r);
	} else {
		for (i = 0; i < mtk_inform_n; i++) {
			int before = r.count;

			if (dmscript_request(mtk_line_cb, &r, "get_value", "param", mtk_inform_names[i], NULL) != 0) {
				rc = -1;
				break;
			}
			if (r.count == before) {
				rc = mtk_inform_full(&r);
				break;
			}
		}
	}
	pthread_mutex_unlock(&mtk_inform_lock);
	if (rc)
		return mtk_transport_fault("inform", NULL);
	return 0;
}

/* script answered (or faulted) for the path; the static tree may hold more
 * of it (root): walk it and merge.  9005 from the walk means "nothing static
 * here", not an error. */
static int mtk_merge_static(struct dmctx *ctx, int sf, int (*walk)(struct dmctx *))
{
	int nf;

	if (!mtk_covers_native(ctx->in_param))
		return sf;
	nf = walk(ctx);
	if (sf == 0 || nf == 0)
		return 0;
	return sf;
}

#endif /* DM_MTK_SCRIPT_COMPAT */

/* ------------------------------------------------------------------------ */
/* engine hooks                                                              */
/* ------------------------------------------------------------------------ */

int dm_platform_ctx_init(struct dmctx *ctx)
{
	char *dbg = NULL;

	(void)ctx;
	/* cwmp.cpe.dm_script_debug=1: the script child keeps its stderr (shell
	 * errors of the function library) -> /tmp/icwmp_dm.log; read here so a
	 * "tr069 command reload" picks a change up for the next respawn */
	dmuci_get_option_value_string("cwmp", "cpe", "dm_script_debug", &dbg);
	if (dbg && dbg[0] == '1')
		setenv("ICWMP_DM_DEBUG", "1", 1);
	else
		unsetenv("ICWMP_DM_DEBUG");
	return 0;
}

int dm_platform_ctx_clean(struct dmctx *ctx)
{
	(void)ctx;
	return 0;
}

/* SPV phase 2: run the setters the shell queued.  Per-parameter faults land
 * in ctx->list_fault_param (record_faults), the RPC is faulted with the
 * first one.  On success the DM_ENABLED_NOTIFY values of the parameters
 * just set are refreshed like mparam_set_value() does for static leaves,
 * and a change of the ACS settings schedules a config reload of icwmpd. */
int dm_platform_commit(struct dmctx *ctx, const char *parameter_key)
{
#ifndef DM_MTK_SCRIPT_COMPAT
	/* all C build: every leaf was applied by its own setter, the engine
	 * commits UCI in dm_apply_config() */
	(void)ctx;
	(void)parameter_key;
	return 0;
#else
	struct mtk_reply r;
	struct set_tmp *n;
	int script_params = 0, reload = 0;

	list_for_each_entry(n, &ctx->set_list_tmp, list) {
		if (mtk_is_native(n->name))
			continue;
		script_params++;
		if (strncmp(n->name, MTK_MS_PREFIX, sizeof(MTK_MS_PREFIX) - 1) == 0 || strcmp(n->name, MTK_PROVCODE) == 0)
			reload = 1;
	}
	if (!script_params)
		return 0;
	mtk_reply_init(&r, ctx, MTK_KIND_STATUS);
	r.record_faults = 1;
	if (dmscript_request(mtk_line_cb, &r, "set_apply", "key", parameter_key ? parameter_key : "", NULL) != 0)
		return mtk_transport_fault("set_apply", NULL);
	if (r.fault)
		return r.fault;
	if (r.status != 1)
		return FAULT_9002;
	list_for_each_entry(n, &ctx->set_list_tmp, list) {
		if (mtk_is_native(n->name))
			continue;
		dm_update_enabled_notify_byname(n->name, n->value ? n->value : "");
	}
	if (reload)
		cwmp_set_end_session(END_SESSION_RELOAD);
	return 0;
#endif
}

void dm_platform_revert(struct dmctx *ctx)
{
#ifdef DM_MTK_SCRIPT_COMPAT
	struct mtk_reply r;

	(void)ctx;
	mtk_reply_init(&r, NULL, MTK_KIND_STATUS);
	(void)dmscript_request(mtk_line_cb, &r, "set_abort", NULL);
#else
	(void)ctx;
#endif
}

int dm_platform_restart_services(void)
{
	struct package_change *pc;
#ifdef DM_MTK_SCRIPT_COMPAT
	struct mtk_reply r;
#endif

	/* packages the static tree changed (cwmp itself is committed by the engine) */
	list_for_each_entry(pc, &head_package_change, list) {
		if (strcmp(pc->package, "cwmp") == 0)
			continue;
		/* easycwmp: dmuci_commit() has written it already, so the ubus
		 * commit would only raise config.change -- and the reload trigger
		 * of /etc/init.d/icwmpd on easycwmp then stops and restarts this
		 * very daemon (tr069 absent from ubus for ~1 s, a new start with
		 * -g) after every ManagementServer write.  The shell skipped the
		 * package the same way (common_restart_services) and asked for a
		 * config load instead: END_SESSION_RELOAD here. */
		if (strcmp(pc->package, "easycwmp") == 0)
			continue;
		dmubus_call_set("uci", "commit", UBUS_ARGS{{"config", pc->package, String}}, 1);
	}
	free_all_list_package_change(&head_package_change);

	/* what easycwmpd did with "apply service" after every session: ucitrack
	 * restarts for the packages the setters changed + the delayed commands
	 * they queued (mtk_apply_service() / the shell equivalent) */
#ifdef DM_MTK_SCRIPT_COMPAT
	/* the script's "apply_service" = ucitrack restarts of the packages its
	 * setters changed + the queued commands.  Nothing of the first kind
	 * without a script set/add/delete: run the queue here, one shell round
	 * trip less at the end of every session. */
	if (mtk_script_changed) {
		mtk_reply_init(&r, NULL, MTK_KIND_STATUS);
		(void)dmscript_request(mtk_line_cb, &r, "apply_service", NULL);
		mtk_script_changed = 0;
	} else {
		mtk_run_apply_service();
	}
#else
	mtk_run_apply_service();
#endif
	return 0;
}

int dm_platform_prefetch_values(char **params, int n)
{
#ifdef DM_MTK_SCRIPT_COMPAT
	return mtk_prefetch(params, n);
#else
	(void)params;
	(void)n;
	return 0;		/* all C: a GET_VALUE is in-process */
#endif
}

void dm_platform_prefetch_drop(void)
{
#ifdef DM_MTK_SCRIPT_COMPAT
	mtk_prefetch_clear();
#endif
}

const char *dm_platform_name(void)
{
#ifdef DM_MTK_SCRIPT_COMPAT
	return "mtk-c+script";
#else
	return "mtk-c";
#endif
}

int dm_platform_select_root(struct dmctx *ctx)
{
	(void)ctx;
	return 0;
}

int dm_platform_param_method(struct dmctx *ctx, int cmd, char *inparam, char *arg1, int *fault)
{
	bool nextlevel = false;

	/* The shell's input contract (common_set_value_check_param: is_safe_input
	 * + the shell type check) in front of every NATIVE setter; a path still
	 * served by the script gets the script's own copy.  VALUECHECK only --
	 * at VALUESET the value has passed already.  See input_contract_mtk.c. */
	if (cmd == CMD_SET_VALUE && ctx && ctx->setaction == VALUECHECK) {
#ifdef DM_MTK_SCRIPT_COMPAT
		int native = mtk_is_native(inparam ? inparam : "");
#else
		int native = 1;		/* all C: every path is native */
#endif
		if (native) {
			int f = mtk_input_contract(inparam, arg1);

			if (f) {
				*fault = f;
				return 1;
			}
		}
	}

#ifndef DM_MTK_SCRIPT_COMPAT
	/* all C build: the static tree is the whole data model, a path no
	 * module owns is an unknown parameter (9005), not a shell call */
	(void)nextlevel;
	return 0;
#else
	if (!inparam)
		inparam = "";
	if (mtk_is_native(inparam))
		return 0;                                  /* static engine only */
	*fault = 0;
	switch (cmd) {
	case CMD_GET_VALUE:
		*fault = mtk_merge_static(ctx, mtk_get_value(ctx, inparam), dm_entry_get_value);
		break;
	case CMD_GET_NAME:
		if (arg1 && string_to_bool(arg1, &nextlevel)) {
			*fault = FAULT_9003;
		} else {
			ctx->nextlevel = nextlevel;
			*fault = mtk_merge_static(ctx, mtk_get_name(ctx, inparam, nextlevel), dm_entry_get_name);
		}
		break;
	case CMD_GET_NOTIFICATION:
		*fault = mtk_merge_static(ctx, mtk_get_notification(ctx, inparam), dm_entry_get_notification);
		break;
	case CMD_SET_VALUE:
		*fault = mtk_set_value(ctx, inparam);
		break;
	case CMD_SET_NOTIFICATION:
		*fault = mtk_set_notification(ctx, inparam);
		break;
	case CMD_ADD_OBJECT:
		*fault = mtk_add_object(ctx, inparam, arg1);
		break;
	case CMD_DEL_OBJECT:
		*fault = mtk_del_object(ctx, inparam, arg1);
		break;
	case CMD_INFORM:
		*fault = mtk_merge_static(ctx, mtk_inform(ctx), dm_entry_inform);
		break;
	default:
		return 0;                                  /* UPnP etc.: static engine */
	}
	return 1;
#endif
}

/* ------------------------------------------------------------------------ */
/* enabled notify (value change tracking)                                    */
/* ------------------------------------------------------------------------ */

/* one DM_ENABLED_NOTIFY entry per script parameter listed (or covered by an
 * object listed) in cwmp.@notifications[0]; returns 0 so the engine appends
 * the static tree's own entries afterwards */
int dm_platform_enabled_notify(struct dmctx *ctx)
{
#ifndef DM_MTK_SCRIPT_COMPAT
	/* all C build: every notified parameter is a static leaf, the engine
	 * writes DM_ENABLED_NOTIFY itself */
	(void)ctx;
	return 0;
#else
	/* the lists that mean a CWMP notification (enabled_notify_check_param()
	 * writes 1, 2, 4, 6; 3 and 5 are lightweight-only) */
	static const struct { const char *list; const char *notif; } lists[] = {
		{"passive", "1"}, {"active", "2"},
		{"passive_passive_lw", "4"}, {"passive_active_lw", "6"}, {NULL, NULL}
	};
	FILE *fp = NULL;
	int i, n = 0;

	for (i = 0; lists[i].list; i++) {
		struct uci_list *ul = NULL;
		struct uci_element *e;

		dmuci_get_option_value_list("cwmp", "@notifications[0]", (char *)lists[i].list, &ul);
		if (!ul)
			continue;
		uci_foreach_element(ul, e) {
			struct mtk_reply r;
			const char *path = e->name;

			if (!path || !path[0] || mtk_is_native(path))
				continue;
			/* an entry may be a more specific override of a wider object
			 * entry; the engine's rule is "longest prefix wins", applied
			 * per parameter by dm_get_parameter_notification() below */
			if (!fp) {
				fp = fopen(DM_ENABLED_NOTIFY, "a");
				if (!fp)
					return 0;
			}
			mtk_reply_init(&r, ctx, MTK_KIND_VALUE);
			r.fp = fp;
			r.notif = lists[i].notif;
			if (mtk_script_values(&r, path, 0) != 0)
				break;
			n += r.count;
		}
	}
	if (fp)
		fclose(fp);
	if (n) {
		/* entries written with the notification of the list they came from;
		 * fix the ones a longer prefix overrides (rare, but the engine's
		 * check_value_change trusts the file) */
		FILE *in = fopen(DM_ENABLED_NOTIFY, "r");
		FILE *out = in ? fopen(DM_ENABLED_NOTIFY_TEMPORARY, "w") : NULL;
		char buf[512];

		if (in && out) {
			while (fgets(buf, sizeof(buf), in)) {
				char *jval, *p, *v, *nt, *ty, *real;
				size_t len = strlen(buf);

				if (len && buf[len - 1] == '\n')
					buf[len - 1] = '\0';
				dmjson_parse_init(buf);
				dmjson_get_var("parameter", &jval); p = dmstrdup(jval);
				dmjson_get_var("value", &jval); v = dmstrdup(jval);
				dmjson_get_var("notification", &jval); nt = dmstrdup(jval);
				dmjson_get_var("type", &jval); ty = dmstrdup(jval);
				dmjson_parse_fini();
				real = mtk_is_native(p) ? nt : dm_get_parameter_notification(ctx, p);
				if (real[0] != '0')
					dmjson_fprintf(out, 4, DMJSON_ARGS{{"parameter", p}, {"notification", real}, {"value", v}, {"type", ty}});
			}
			fclose(in);
			fclose(out);
			/* not rename(): /tmp -> /etc/tr098 crosses filesystems (EXDEV)
			 * and the file would just be gone */
			if (copy_temporary_file_to_original_file(DM_ENABLED_NOTIFY, DM_ENABLED_NOTIFY_TEMPORARY))
				remove(DM_ENABLED_NOTIFY_TEMPORARY);
		} else {
			if (in) fclose(in);
			if (out) fclose(out);
		}
	}
	return 0;
#endif
}

/* start-up diff of DM_ENABLED_NOTIFY for the script parameters; the engine
 * then does the same for the static tree's entries (returns 0) */
int dm_platform_enabled_notify_check_value_change(struct dmctx *ctx)
{
#ifndef DM_MTK_SCRIPT_COMPAT
	(void)ctx;
	return 0;
#else
	FILE *fp;
	char buf[512];
	char *jval, *parameter, *value, *notification, *type;
	char **names = NULL;
	int nn = 0, cap = 0;

	fp = fopen(DM_ENABLED_NOTIFY, "r");
	if (fp == NULL)
		return 0;
	/* first pass: the script's entries, fetched in one go */
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		int len = strlen(buf);

		if (len)
			buf[len - 1] = '\0';
		dmjson_parse_init(buf);
		dmjson_get_var("parameter", &jval);
		if (jval[0] && !mtk_is_native(jval) && !mtk_is_object(jval)) {
			if (nn == cap) {
				char **n2 = realloc(names, (cap ? cap * 2 : 32) * sizeof(char *));

				if (n2) {
					names = n2;
					cap = cap ? cap * 2 : 32;
				}
			}
			if (nn < cap && (names[nn] = strdup(jval)) != NULL)
				nn++;
		}
		dmjson_parse_fini();
	}
	if (nn)
		mtk_prefetch(names, nn);
	while (nn)
		free(names[--nn]);
	free(names);
	rewind(fp);

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		struct mtk_reply r;
		const char *pv;
		int len = strlen(buf);

		if (len)
			buf[len - 1] = '\0';
		dmjson_parse_init(buf);
		dmjson_get_var("parameter", &jval);
		parameter = dmstrdup(jval);
		dmjson_get_var("value", &jval);
		value = dmstrdup(jval);
		dmjson_get_var("notification", &jval);
		notification = dmstrdup(jval);
		dmjson_get_var("type", &jval);
		type = dmstrdup(jval);
		dmjson_parse_fini();

		if (mtk_is_native(parameter) || mtk_is_object(parameter))
			continue;
		mtk_reply_init(&r, ctx, MTK_KIND_VALUE);
		if ((pv = mtk_prefetch_value(parameter)) != NULL) {
			r.single_value = strdup(pv);
			r.count = 1;
		} else {
			r.single_value = strdup("");
		}
		if (!r.count &&
		    (dmscript_request(mtk_line_cb, &r, "get_value", "param", parameter, NULL) != 0 || r.count == 0)) {
			free(r.single_value);
			continue;
		}
		if (strcmp(r.single_value, value) != 0) {
			if (ctx->add_list_value_change)
				ctx->add_list_value_change(parameter, r.single_value, type);
			if (notification[0] == '2' && ctx->send_active_value_change)
				ctx->send_active_value_change();
		}
		free(r.single_value);
	}
	fclose(fp);
	mtk_prefetch_clear();
	return 0;
#endif
}
