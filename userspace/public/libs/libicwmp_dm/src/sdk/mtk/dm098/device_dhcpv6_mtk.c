/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.Device.DHCPv6.Server.Pool.{i}. -- odhcpd's view of
 *	the dhcp config, ported from functions/tr098/device_dhcpv6_server.
 *
 *	A pool is a "dhcp" section of the dhcp config whose interface option
 *	names a network section that has (or is now given, see device_ip_mtk.h)
 *	a Device.IP.Interface number.  Its own number is
 *	dhcp.<sec>.dhcpv6_int_instance, given and committed the first time,
 *	the first free one; pools are listed by that number.
 *	  Enable     dhcpv6 == "server".  A set rewrites the odhcpd options the
 *	             shell wrote: on -- dhcpv6 server, ra_flags other-config +
 *	             managed-config, ra_slaac 0, ra_dns 0, stateless 0; off --
 *	             ra_flags none, dhcpv6 disabled, ra_slaac 1, ra_dns 1.
 *	             odhcpd reloaded at the end of the session
 *	  Status     Error_Misconfigured without an interface or without its
 *	             Device.IP number, else Enabled/Disabled
 *	  Interface  "InternetGatewayDevice.Device.IP.Interface.<n>" of the
 *	             interface option; a set takes such a path (the trailing dot
 *	             optional) whose <n> belongs to a network section, 9007
 *	             otherwise, and reloads odhcpd
 *	  DUID       DUID-LL "00030001" + the MAC of the interface's l3 device
 *	             (or its device, or br-lan as a last resort), upper case
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmubus.h"
#include "dmjson.h"
#include "dmmem.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "device_ip_mtk.h"

#define D6_PKG		"dhcp"
#define D6_RELOAD	"/etc/init.d/odhcpd reload"
#define D6_IF_PREFIX	mtk_ipif_prefix()	/* TR-098 or TR-181 root */

struct d6_pool {
	char *sec;	/* section name, "@dhcp[n]" for an anonymous one */
	char *inst;
};

static struct uci_package *d6_pkg(void)
{
	struct uci_ptr ptr = {0};

	if (dmuci_lookup_ptr(uci_ctx, &ptr, D6_PKG, NULL, NULL, NULL) || !ptr.p)
		return NULL;
	return ptr.p;
}

/* dhcp6_get_next_free_instance over every dhcp section's
 * dhcpv6_int_instance (non-digits removed) */
static int d6_next_free(void)
{
	struct uci_package *p = d6_pkg();
	struct uci_element *e;
	unsigned char used[256] = {0};
	int i;

	if (p) {
		uci_foreach_element(&p->sections, e) {
			char *v = NULL, digits[16];
			size_t n = 0;
			const char *c;

			dmuci_get_value_by_section_string(uci_to_section(e), "dhcpv6_int_instance", &v);
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

/* dhcp6_is_valid_section: an interface whose network section has (or now
 * gets) an all-digit Device.IP number */
static int d6_valid(const char *sec)
{
	char *ifn = mtk_uci(D6_PKG, sec, "interface"), *inst;
	const char *c;

	if (!*ifn)
		return 0;
	dip_update_instance(ifn);
	inst = dip_instance_of(ifn);
	if (!*inst)
		return 0;
	for (c = inst; *c; c++) {
		if (!isdigit((unsigned char)*c))
			return 0;
	}
	return 1;
}

static int d6_cmp(const void *a, const void *b)
{
	const struct d6_pool *x = a, *y = b;
	long nx = strtol(x->inst, NULL, 10), ny = strtol(y->inst, NULL, 10);
	int c;

	if (nx != ny)
		return nx < ny ? -1 : 1;
	c = strcmp(x->inst, y->inst);
	return c ? c : strcmp(x->sec, y->sec);
}

#define D6_MAX	32

/* the pools, numbered and in instance order: what browse_d6 links, and
 * what Server.PoolNumberOfEntries counts */
static int d6_list(struct d6_pool *list, int max)
{
	struct uci_package *p = d6_pkg();
	struct uci_element *e;
	int n = 0, anon = 0;

	if (!p)
		return 0;
	uci_foreach_element(&p->sections, e) {
		struct uci_section *s = uci_to_section(e);
		char *sec, *inst, buf[16];

		if (strcmp(s->type, "dhcp") != 0)
			continue;
		if (s->anonymous)
			dmasprintf(&sec, "@dhcp[%d]", anon);
		else
			sec = dmstrdup(section_name(s));
		anon++;
		if (!sec || n >= max || !d6_valid(sec))
			continue;
		inst = mtk_uci(D6_PKG, sec, "dhcpv6_int_instance");
		if (!*inst) {
			snprintf(buf, sizeof(buf), "%d", d6_next_free());
			mtk_uci_set_persist(D6_PKG, sec, "dhcpv6_int_instance", buf);
			inst = dmstrdup(buf);
		}
		list[n].sec = sec;
		list[n].inst = inst;
		n++;
	}
	qsort(list, n, sizeof(*list), d6_cmp);
	return n;
}

static int browse_d6(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct d6_pool *list = dmcalloc(D6_MAX, sizeof(*list));
	int n, i;

	if (!list)
		return 0;
	n = d6_list(list, D6_MAX);
	for (i = 0; i < n; i++) {
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)&list[i], list[i].inst) == DM_STOP)
			break;
	}
	return 0;
}

#define D6_SEC(data)	(((struct d6_pool *)(data))->sec)

static int get_d6_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci(D6_PKG, D6_SEC(data), "dhcpv6"), "server") == 0 ? "1" : "0";
	return 0;
}

static int set_d6_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *sec = D6_SEC(data);
	int b = mtk_parse_bool(value);

	if (b < 0)
		return FAULT_9007;	/* _bool_norm */
	if (action == VALUECHECK)
		return 0;
	dmuci_delete(D6_PKG, sec, "ra_flags", NULL);
	if (b) {
		dmuci_set_value(D6_PKG, sec, "dhcpv6", "server");
		dmuci_add_list_value(D6_PKG, sec, "ra_flags", "other-config");
		dmuci_add_list_value(D6_PKG, sec, "ra_flags", "managed-config");
		dmuci_set_value(D6_PKG, sec, "ra_slaac", "0");
		dmuci_set_value(D6_PKG, sec, "ra_dns", "0");
		dmuci_set_value(D6_PKG, sec, "stateless", "0");
	} else {
		dmuci_add_list_value(D6_PKG, sec, "ra_flags", "none");
		dmuci_set_value(D6_PKG, sec, "dhcpv6", "disabled");
		dmuci_set_value(D6_PKG, sec, "ra_slaac", "1");
		dmuci_set_value(D6_PKG, sec, "ra_dns", "1");
	}
	mtk_apply_service_once(D6_RELOAD);
	return 0;
}

static int d6_all_digits(const char *v)
{
	if (!v || !*v)
		return 0;
	for (; *v; v++) {
		if (!isdigit((unsigned char)*v))
			return 0;
	}
	return 1;
}

static int get_d6_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *ifn = mtk_uci(D6_PKG, D6_SEC(data), "interface");

	if (!*ifn || !d6_all_digits(dip_instance_of(ifn))) {
		*value = "Error_Misconfigured";
		return 0;
	}
	*value = strcmp(mtk_uci(D6_PKG, D6_SEC(data), "dhcpv6"), "server") == 0 ? "Enabled" : "Disabled";
	return 0;
}

static int get_d6_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *ifn = mtk_uci(D6_PKG, D6_SEC(data), "interface"), *inst;

	*value = "";
	if (!*ifn)
		return 0;
	inst = dip_instance_of(ifn);
	if (d6_all_digits(inst))
		dmasprintf(value, "%s%s", D6_IF_PREFIX, inst);
	return 0;
}

/* dhcp6_ifref_to_inst: the <n> of "...Device.IP.Interface.<n>[.]" */
static char *d6_ref_inst(const char *ref)
{
	char *n;
	size_t len;

	if (!ref || strncmp(ref, D6_IF_PREFIX, strlen(D6_IF_PREFIX)) != 0)
		return NULL;
	n = dmstrdup(ref + strlen(D6_IF_PREFIX));
	if (!n)
		return NULL;
	len = strlen(n);
	if (len && n[len - 1] == '.')
		n[len - 1] = '\0';
	return d6_all_digits(n) ? n : NULL;
}

static int set_d6_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *inst = d6_ref_inst(value), *nsec;

	if (!value || !*value || !inst)
		return FAULT_9007;
	nsec = dip_section_of_instance(inst);
	if (!nsec)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(D6_PKG, D6_SEC(data), "interface", nsec);
	mtk_apply_service_once(D6_RELOAD);
	return 0;
}

/* "00030001" + the MAC of /sys/class/net/<dev>/address, upper case, ""
 * when the file is not there */
static char *d6_duid_of(const char *dev)
{
	char path[128], *mac, *out, *o;
	const char *c;

	if (!dev || !*dev)
		return NULL;
	snprintf(path, sizeof(path), "/sys/class/net/%s/address", dev);
	mac = mtk_file_line(path);
	if (!*mac)
		return NULL;
	out = dmcalloc(1, strlen(mac) + 9);
	if (!out)
		return NULL;
	strcpy(out, "00030001");
	for (c = mac, o = out + 8; *c; c++) {
		if (*c != ':')
			*o++ = (char)toupper((unsigned char)*c);
	}
	*o = '\0';
	return out;
}

static int get_d6_duid(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *ifn = mtk_uci(D6_PKG, D6_SEC(data), "interface"), *dev = "", *duid, obj[96];
	json_object *res = NULL;

	*value = "";
	if (!*ifn)
		return 0;
	snprintf(obj, sizeof(obj), "network.interface.%s", ifn);
	dmubus_call(obj, "status", UBUS_ARGS{}, 0, &res);
	if (res) {
		dev = dmjson_get_value(res, 1, "l3_device");
		if (!dev || !*dev)
			dev = dmjson_get_value(res, 1, "device");
	}
	duid = d6_duid_of(dev);
	if (!duid)
		duid = d6_duid_of("br-lan");
	if (duid)
		*value = duid;
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tD6PoolParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_d6_enable, set_d6_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_d6_status, NULL, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_d6_interface, set_d6_interface, NULL, NULL},
{"DUID", &DMREAD, DMT_STRING, get_d6_duid, NULL, NULL, NULL},
{0}
};

static DMOBJ tD6ServerObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Pool", &DMREAD, NULL, NULL, NULL, browse_d6, NULL, NULL, NULL, tD6PoolParams, NULL},
{0}
};

static DMOBJ tD6Obj[] = {
{"Server", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tD6ServerObj, NULL, NULL},
{0}
};

static DMOBJ tD6DeviceObj[] = {
{"DHCPv6", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tD6Obj, NULL, NULL},
{0}
};

static DMOBJ tD6Root[] = {
{"Device", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tD6DeviceObj, NULL, NULL},
{0}
};

static const char *const device_dhcpv6_mtk_paths[] = {
	"InternetGatewayDevice.Device.DHCPv6.",
	NULL
};

static const struct dm_module device_dhcpv6_mtk_module = {
	.name  = "mtk-device-dhcpv6",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tD6Root,
	.paths = device_dhcpv6_mtk_paths,
};
DM_MODULE_REGISTER(device_dhcpv6_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): this branch is TR-181 already, the
 * product grafted it under InternetGatewayDevice.Device.; the same leaves at
 * the root (type A of docs/plan/tr181_mtk_design.md).  References to
 * IP.Interface follow the root (mtk_ipif_prefix()).  DUID is xsd:hexBinary
 * there (the DUID-LL is hex digits already): tD6Pool181Params is a copy of
 * tD6PoolParams, keep in step (T7 S2). */
/* T7 S3: standard readWrite leaves the product cannot change (dmmtk.h MTK_SET_SAME) */
MTK_SET_SAME(d6_duid, get_d6_duid)

static DMLEAF tD6Pool181Params[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_d6_enable, set_d6_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_d6_status, NULL, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_d6_interface, set_d6_interface, NULL, NULL},
{"DUID", &DMWRITE, DMT_HEXBIN, get_d6_duid, set_same_d6_duid, NULL, NULL},
{0}
};

static DMOBJ tD6Server181Obj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Pool", &DMREAD, NULL, NULL, NULL, browse_d6, NULL, NULL, NULL, tD6Pool181Params, NULL},
{0}
};

/* T7 S4b: the count of the table, standard in TR-181 */
static int get_d6181_pool_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct d6_pool *list = dmcalloc(D6_MAX, sizeof(*list));

	dmasprintf(value, "%d", list ? d6_list(list, D6_MAX) : 0);
	return 0;
}

static DMLEAF tD6Server181Params[] = {
{"PoolNumberOfEntries", &DMREAD, DMT_UNINT, get_d6181_pool_count, NULL, NULL, NULL},
{0}
};

static DMOBJ tD6181Obj[] = {
{"Server", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tD6Server181Obj, tD6Server181Params, NULL},
{0}
};

static DMOBJ tD6Device181Obj[] = {
{"DHCPv6", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tD6181Obj, NULL, NULL},
{0}
};

static const char *const device_dhcpv6_mtk_paths181[] = {
	"Device.DHCPv6.",
	NULL
};

static const struct dm_module device_dhcpv6_mtk_module181 = {
	.name  = "mtk-device-dhcpv6-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tD6Device181Obj,
	.paths = device_dhcpv6_mtk_paths181,
};
DM_MODULE_REGISTER(device_dhcpv6_mtk_module181);
