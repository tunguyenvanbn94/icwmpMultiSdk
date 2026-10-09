/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.Device.RouterAdvertisement.InterfaceSetting.{i}. --
 *	odhcpd's router advertisements, ported from
 *	functions/tr098/device_routeradvertisement.
 *
 *	The same "dhcp" sections as Device.DHCPv6.Server.Pool (device_dhcpv6_mtk.c)
 *	-- one whose interface has a Device.IP number -- numbered by their own
 *	dhcp.<sec>.ra_int_instance (first free, committed at once) and listed by
 *	it.  A listed section without ra_alias gets "cpe-<sec>", committed at
 *	once too: the shell did both while answering a GET.
 *	  Enable              ra == "server"; a set writes server/disabled
 *	  Status              Error_Misconfigured when both intervals are
 *	                      numbers and min > max; Disabled when not enabled;
 *	                      else the interface's "up" (Enabled when netifd
 *	                      does not answer)
 *	  Alias               ra_alias, a set takes any non-empty value
 *	  Interface           as Device.DHCPv6.Server.Pool.{i}.Interface
 *	  Prefixes            the interface's delegated prefixes, "a/len"
 *	                      separated by spaces: its ipv6-prefix-assignment
 *	                      entries, else their local addresses ("::1" read as
 *	                      "::"), else what another interface's ipv6-prefix
 *	                      assigned to it
 *	  MaxRtrAdvInterval, MinRtrAdvInterval   ra_maxinterval/ra_mininterval,
 *	                      digits, min <= max when the other one is a number
 *	                      (checked against the other's value of this SPV)
 *	  AdvDefaultLifetime  ra_lifetime, digits
 *	  AdvManagedFlag, AdvOtherConfigFlag   ra_flags holds managed-config /
 *	                      other-config (comma or space separated).  A set
 *	                      rewrites ra_flags to the normalised list ("none"
 *	                      when empty) and the stateful/stateless options as
 *	                      the shell did: both flags off -- stateless 0,
 *	                      ra_slaac 1, ra_dns 1, dhcpv6 disabled; managed on --
 *	                      stateless 0, ra_slaac 0, ra_dns 0, dhcpv6 server;
 *	                      other only -- stateless 1, ra_slaac 1, ra_dns 1,
 *	                      dhcpv6 server
 *	Every set but Alias reloads odhcpd, queued once for the end of the
 *	session.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <json-c/json.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmubus.h"
#include "dmjson.h"
#include "dmmem.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "device_ip_mtk.h"

#define RA_PKG		"dhcp"
#define RA_RELOAD	"/etc/init.d/odhcpd reload"
#define RA_IF_PREFIX	mtk_ipif_prefix()	/* TR-098 or TR-181 root */

struct ra_if {
	char *sec;
	char *inst;
};

static struct uci_package *ra_pkg(void)
{
	struct uci_ptr ptr = {0};

	if (dmuci_lookup_ptr(uci_ctx, &ptr, RA_PKG, NULL, NULL, NULL) || !ptr.p)
		return NULL;
	return ptr.p;
}

static int ra_digits(const char *v)
{
	if (!v || !*v)
		return 0;
	for (; *v; v++) {
		if (!isdigit((unsigned char)*v))
			return 0;
	}
	return 1;
}

/* ra_get_next_free_instance over every dhcp section's ra_int_instance */
static int ra_next_free(void)
{
	struct uci_package *p = ra_pkg();
	struct uci_element *e;
	unsigned char used[256] = {0};
	int i;

	if (p) {
		uci_foreach_element(&p->sections, e) {
			char *v = NULL, digits[16];
			size_t n = 0;
			const char *c;

			dmuci_get_value_by_section_string(uci_to_section(e), "ra_int_instance", &v);
			for (c = v ? v : ""; *c && n < sizeof(digits) - 1; c++) {
				if (isdigit((unsigned char)*c))
					digits[n++] = *c;
			}
			digits[n] = '\0';
			if (n && atoi(digits) < (int)sizeof(used))
				used[atoi(digits)] = 1;
		}
	}
	for (i = 1; i < (int)sizeof(used) && used[i]; i++)
		;
	return i;
}

static int ra_cmp(const void *a, const void *b)
{
	const struct ra_if *x = a, *y = b;
	long nx = strtol(x->inst, NULL, 10), ny = strtol(y->inst, NULL, 10);
	int c;

	if (nx != ny)
		return nx < ny ? -1 : 1;
	c = strcmp(x->inst, y->inst);
	return c ? c : strcmp(x->sec, y->sec);
}

#define RA_MAX	32

/* the interface settings, numbered and in instance order: what browse_ra
 * links, and what InterfaceSettingNumberOfEntries counts */
static int ra_list(struct ra_if *list, int max)
{
	struct uci_package *p = ra_pkg();
	struct uci_element *e;
	int n = 0, anon = 0;

	if (!p)
		return 0;
	uci_foreach_element(&p->sections, e) {
		struct uci_section *s = uci_to_section(e);
		char *sec, *ifn, *inst, buf[16];

		if (strcmp(s->type, "dhcp") != 0)
			continue;
		if (s->anonymous)
			dmasprintf(&sec, "@dhcp[%d]", anon);
		else
			sec = dmstrdup(section_name(s));
		anon++;
		if (!sec || n >= max)
			continue;
		/* ra_is_valid_section */
		ifn = mtk_uci(RA_PKG, sec, "interface");
		if (!*ifn)
			continue;
		dip_update_instance(ifn);
		if (!ra_digits(dip_instance_of(ifn)))
			continue;
		inst = mtk_uci(RA_PKG, sec, "ra_int_instance");
		if (!*inst) {
			snprintf(buf, sizeof(buf), "%d", ra_next_free());
			mtk_uci_set_persist(RA_PKG, sec, "ra_int_instance", buf);
			inst = dmstrdup(buf);
		}
		list[n].sec = sec;
		list[n].inst = inst;
		n++;
	}
	qsort(list, n, sizeof(*list), ra_cmp);
	return n;
}

static int browse_ra(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct ra_if *list = dmcalloc(RA_MAX, sizeof(*list));
	int n, i;

	if (!list)
		return 0;
	n = ra_list(list, RA_MAX);
	for (i = 0; i < n; i++) {
		/* ra_autofix_alias */
		if (!*mtk_uci(RA_PKG, list[i].sec, "ra_alias")) {
			char alias[96];

			snprintf(alias, sizeof(alias), "cpe-%s", list[i].sec);
			mtk_uci_set_persist(RA_PKG, list[i].sec, "ra_alias", alias);
		}
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)&list[i], list[i].inst) == DM_STOP)
			break;
	}
	return 0;
}

#define RA_SEC(data)	(((struct ra_if *)(data))->sec)

static json_object *ra_status(const char *ifn)
{
	json_object *res = NULL;
	char obj[96];

	if (!ifn || !*ifn)
		return NULL;
	snprintf(obj, sizeof(obj), "network.interface.%s", ifn);
	dmubus_call(obj, "status", UBUS_ARGS{}, 0, &res);
	return res;
}

/* ------------------------------------------------------------------ */
/* Enable, Status, Alias, Interface                                    */
/* ------------------------------------------------------------------ */

static int ra_enabled(void *data)
{
	return strcmp(mtk_uci(RA_PKG, RA_SEC(data), "ra"), "server") == 0;
}

static int get_ra_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = ra_enabled(data) ? "1" : "0";
	return 0;
}

static int set_ra_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = mtk_parse_bool(value);

	if (b < 0)
		return FAULT_9007;	/* _bool_norm */
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(RA_PKG, RA_SEC(data), "ra", b ? "server" : "disabled");
	mtk_apply_service_once(RA_RELOAD);
	return 0;
}

static int get_ra_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *min = mtk_uci(RA_PKG, RA_SEC(data), "ra_mininterval");
	char *max = mtk_uci(RA_PKG, RA_SEC(data), "ra_maxinterval");
	json_object *res;

	if (ra_digits(min) && ra_digits(max) && strtoll(min, NULL, 10) > strtoll(max, NULL, 10)) {
		*value = "Error_Misconfigured";
		return 0;
	}
	if (!ra_enabled(data)) {
		*value = "Disabled";
		return 0;
	}
	res = ra_status(mtk_uci(RA_PKG, RA_SEC(data), "interface"));
	if (!res) {
		*value = "Enabled";
		return 0;
	}
	*value = strcmp(dmjson_get_value(res, 1, "up"), "true") == 0 ? "Enabled" : "Disabled";
	return 0;
}

static int get_ra_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci(RA_PKG, RA_SEC(data), "ra_alias");
	return 0;
}

static int set_ra_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!*value)
		return FAULT_9007;
	if (action == VALUESET)
		dmuci_set_value(RA_PKG, RA_SEC(data), "ra_alias", value);
	return 0;
}

static int get_ra_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *ifn = mtk_uci(RA_PKG, RA_SEC(data), "interface"), *inst;

	*value = "";
	if (!*ifn)
		return 0;
	inst = dip_instance_of(ifn);
	if (ra_digits(inst))
		dmasprintf(value, "%s%s", RA_IF_PREFIX, inst);
	return 0;
}

static int set_ra_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *inst, *nsec;
	size_t len;

	if (!*value || strncmp(value, RA_IF_PREFIX, strlen(RA_IF_PREFIX)) != 0)
		return FAULT_9007;
	inst = dmstrdup(value + strlen(RA_IF_PREFIX));
	if (!inst)
		return FAULT_9002;
	len = strlen(inst);
	if (len && inst[len - 1] == '.')
		inst[len - 1] = '\0';
	if (!ra_digits(inst))
		return FAULT_9007;
	nsec = dip_section_of_instance(inst);
	if (!nsec)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(RA_PKG, RA_SEC(data), "interface", nsec);
	mtk_apply_service_once(RA_RELOAD);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Prefixes                                                            */
/* ------------------------------------------------------------------ */

/* "<address>/<mask>" of every element of <arr> (of its <inner> object when
 * inner is set) that has both, separated by spaces */
static void ra_pairs(json_object *arr, const char *inner, int trim_one, char *out, size_t sz)
{
	int i;

	out[0] = '\0';
	if (!arr || !json_object_is_type(arr, json_type_array))
		return;
	for (i = 0; i < (int)json_object_array_length(arr); i++) {
		json_object *it = json_object_array_get_idx(arr, i), *sub, *a, *m;
		char addr[64];
		size_t l;

		if (inner) {
			if (!json_object_object_get_ex(it, inner, &sub))
				continue;
			it = sub;
		}
		if (!json_object_object_get_ex(it, "address", &a) || !json_object_object_get_ex(it, "mask", &m))
			continue;
		snprintf(addr, sizeof(addr), "%s", json_object_get_string(a));
		l = strlen(addr);
		/* case "$a" in *"::1") a="${a%1}" */
		if (trim_one && l >= 3 && strcmp(addr + l - 3, "::1") == 0)
			addr[l - 1] = '\0';
		if (!*addr || !*json_object_get_string(m))
			continue;
		snprintf(out + strlen(out), sz - strlen(out), "%s%s/%s", *out ? " " : "", addr, json_object_get_string(m));
	}
}

static int get_ra_prefixes(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *ifn = mtk_uci(RA_PKG, RA_SEC(data), "interface"), out[1024], *list, *line, *save = NULL;
	json_object *res, *arr;

	*value = "";
	if (!*ifn)
		return 0;
	res = ra_status(ifn);
	if (res && json_object_object_get_ex(res, "ipv6-prefix-assignment", &arr)) {
		ra_pairs(arr, NULL, 0, out, sizeof(out));
		if (*out) {
			*value = dmstrdup(out);
			return 0;
		}
		ra_pairs(arr, "local-address", 1, out, sizeof(out));
		if (*out) {
			*value = dmstrdup(out);
			return 0;
		}
	}
	/* what an upstream interface's ipv6-prefix assigned to this one */
	{
		char *argv[] = { "ubus", "-S", "list", "network.interface.*", NULL };

		list = mtk_exec(argv);
	}
	for (line = strtok_r(list, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		json_object *r2, *pfx;
		int i;

		if (strncmp(line, "network.interface.", 18) != 0)
			continue;
		r2 = ra_status(line + 18);
		if (!r2 || !json_object_object_get_ex(r2, "ipv6-prefix", &pfx) ||
		    !json_object_is_type(pfx, json_type_array))
			continue;
		out[0] = '\0';
		for (i = 0; i < (int)json_object_array_length(pfx); i++) {
			json_object *assigned, *mine, *a, *m;

			if (!json_object_object_get_ex(json_object_array_get_idx(pfx, i), "assigned", &assigned) ||
			    !json_object_object_get_ex(assigned, ifn, &mine) ||
			    !json_object_object_get_ex(mine, "address", &a) ||
			    !json_object_object_get_ex(mine, "mask", &m))
				continue;
			snprintf(out + strlen(out), sizeof(out) - strlen(out), "%s%s/%s", *out ? " " : "",
				 json_object_get_string(a), json_object_get_string(m));
		}
		if (*out) {
			*value = dmstrdup(out);
			return 0;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* intervals, lifetime                                                 */
/* ------------------------------------------------------------------ */

#define RA_GET(name, option)							\
static int get_ra_##name(char *refparam, struct dmctx *ctx, void *data,	\
			 char *instance, char **value)				\
{										\
	*value = mtk_uci(RA_PKG, RA_SEC(data), option);				\
	return 0;								\
}

RA_GET(maxinterval, "ra_maxinterval")
RA_GET(mininterval, "ra_mininterval")
RA_GET(lifetime, "ra_lifetime")

/* <option>=<value> after the digits check and, for the intervals, the order
 * against the other one -- read at VALUESET, so a value set earlier in the
 * same SPV is the one compared with, as the shell's sequential setters did */
static int ra_set_uint(void *data, const char *option, const char *value, const char *other,
		       int value_is_max, int action)
{
	if (!ra_digits(value))
		return FAULT_9007;	/* _is_uint */
	if (action == VALUECHECK)
		return 0;
	if (other) {
		char *o = mtk_uci(RA_PKG, RA_SEC(data), other);

		if (ra_digits(o)) {
			long long v = strtoll(value, NULL, 10), w = strtoll(o, NULL, 10);

			if (value_is_max ? w > v : v > w)
				return FAULT_9007;
		}
	}
	dmuci_set_value(RA_PKG, RA_SEC(data), (char *)option, (char *)value);
	mtk_apply_service_once(RA_RELOAD);
	return 0;
}

static int set_ra_maxinterval(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return ra_set_uint(data, "ra_maxinterval", value, "ra_mininterval", 1, action);
}

static int set_ra_mininterval(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return ra_set_uint(data, "ra_mininterval", value, "ra_maxinterval", 0, action);
}

static int set_ra_lifetime(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return ra_set_uint(data, "ra_lifetime", value, NULL, 0, action);
}

/* ------------------------------------------------------------------ */
/* flags                                                               */
/* ------------------------------------------------------------------ */

/* _ra_flags_has: <key> is one of the comma/space separated words */
static int ra_flags_has(const char *flags, const char *key)
{
	char *dup = dmstrdup(flags), *w, *save = NULL;

	if (!dup)
		return 0;
	for (w = strtok_r(dup, ", \t", &save); w; w = strtok_r(NULL, ", \t", &save)) {
		if (strcmp(w, key) == 0)
			return 1;
	}
	return 0;
}

static int get_ra_managed(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = ra_flags_has(mtk_uci(RA_PKG, RA_SEC(data), "ra_flags"), "managed-config") ? "1" : "0";
	return 0;
}

static int get_ra_other(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = ra_flags_has(mtk_uci(RA_PKG, RA_SEC(data), "ra_flags"), "other-config") ? "1" : "0";
	return 0;
}

static void ra_mode(void *data, const char *stateless, const char *slaac_dns, const char *dhcpv6)
{
	dmuci_set_value(RA_PKG, RA_SEC(data), "stateless", (char *)stateless);
	dmuci_set_value(RA_PKG, RA_SEC(data), "ra_slaac", (char *)slaac_dns);
	dmuci_set_value(RA_PKG, RA_SEC(data), "ra_dns", (char *)slaac_dns);
	dmuci_set_value(RA_PKG, RA_SEC(data), "dhcpv6", (char *)dhcpv6);
}

/* ra_set_managedflag / ra_set_otherflag; key is the flag set, other the
 * other flag; managed tells which mode table applies */
static int ra_set_flag(void *data, const char *key, const char *other, int managed,
		       const char *value, int action)
{
	char *f = mtk_uci(RA_PKG, RA_SEC(data), "ra_flags");
	int b = mtk_parse_bool(value), o, has_m, has_o;

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	o = ra_flags_has(f, other);
	if (!b && !o)
		ra_mode(data, "0", "1", "disabled");
	else if (managed ? b : o)	/* managed on: stateful */
		ra_mode(data, "0", "0", "server");
	else
		ra_mode(data, "1", "1", "server");
	/* the new list: this flag added or removed, then _ra_flags_norm */
	has_m = strcmp(key, "managed-config") == 0 ? b : ra_flags_has(f, "managed-config");
	has_o = strcmp(key, "other-config") == 0 ? b : ra_flags_has(f, "other-config");
	dmuci_set_value(RA_PKG, RA_SEC(data), "ra_flags",
			has_m && has_o ? "managed-config other-config" :
			has_m ? "managed-config" : has_o ? "other-config" : "none");
	mtk_apply_service_once(RA_RELOAD);
	return 0;
}

static int set_ra_managed(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return ra_set_flag(data, "managed-config", "other-config", 1, value, action);
}

static int set_ra_other(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return ra_set_flag(data, "other-config", "managed-config", 0, value, action);
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tRaIfParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_ra_enable, set_ra_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_ra_status, NULL, NULL, NULL},
{"Alias", &DMWRITE, DMT_STRING, get_ra_alias, set_ra_alias, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_ra_interface, set_ra_interface, NULL, NULL},
{"Prefixes", &DMREAD, DMT_STRING, get_ra_prefixes, NULL, NULL, NULL},
{"MaxRtrAdvInterval", &DMWRITE, DMT_UNINT, get_ra_maxinterval, set_ra_maxinterval, NULL, NULL},
{"MinRtrAdvInterval", &DMWRITE, DMT_UNINT, get_ra_mininterval, set_ra_mininterval, NULL, NULL},
{"AdvDefaultLifetime", &DMWRITE, DMT_UNINT, get_ra_lifetime, set_ra_lifetime, NULL, NULL},
{"AdvManagedFlag", &DMWRITE, DMT_BOOL, get_ra_managed, set_ra_managed, NULL, NULL},
{"AdvOtherConfigFlag", &DMWRITE, DMT_BOOL, get_ra_other, set_ra_other, NULL, NULL},
{0}
};

static DMOBJ tRaObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"InterfaceSetting", &DMREAD, NULL, NULL, NULL, browse_ra, NULL, NULL, NULL, tRaIfParams, NULL},
{0}
};

static DMOBJ tRaDeviceObj[] = {
{"RouterAdvertisement", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tRaObj, NULL, NULL},
{0}
};

static DMOBJ tRaRoot[] = {
{"Device", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tRaDeviceObj, NULL, NULL},
{0}
};

static const char *const device_ra_mtk_paths[] = {
	"InternetGatewayDevice.Device.RouterAdvertisement.",
	NULL
};

static const struct dm_module device_ra_mtk_module = {
	.name  = "mtk-device-routeradvertisement",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tRaRoot,
	.paths = device_ra_mtk_paths,
};
DM_MODULE_REGISTER(device_ra_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): this branch is TR-181 already, the
 * product grafted it under InternetGatewayDevice.Device.; the same tables at
 * the root (type A of docs/plan/tr181_mtk_design.md).  References to
 * IP.Interface follow the root (mtk_ipif_prefix()). */
static const char *const device_ra_mtk_paths181[] = {
	"Device.RouterAdvertisement.",
	NULL
};

/* T7 S4b: the count of the table, standard in TR-181 (the TR-098 graft
 * keeps the product's leaves) */
static int get_ra181_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct ra_if *list = dmcalloc(RA_MAX, sizeof(*list));

	dmasprintf(value, "%d", list ? ra_list(list, RA_MAX) : 0);
	return 0;
}

static DMLEAF tRa181Params[] = {
{"InterfaceSettingNumberOfEntries", &DMREAD, DMT_UNINT, get_ra181_count, NULL, NULL, NULL},
{0}
};

static DMOBJ tRaDevice181Obj[] = {
{"RouterAdvertisement", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tRaObj, tRa181Params, NULL},
{0}
};

static const struct dm_module device_ra_mtk_module181 = {
	.name  = "mtk-device-routeradvertisement-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tRaDevice181Obj,
	.paths = device_ra_mtk_paths181,
};
DM_MODULE_REGISTER(device_ra_mtk_module181);
