/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	PortMapping under both WAN connection objects:
 *	  WANIPConnection.{i}.PortMapping.{i}.*  + PortMappingNumberOfEntries
 *	  WANPPPConnection.{i}.PortMapping.{i}.* + PortMappingNumberOfEntries
 *
 *	Ported from functions/tr098/wan_device:
 *	  sub_entry_port_mapping_wanconnectiondevice(), port_mapping_browse_instances(),
 *	  wan_device_get_port_mapping_number(), port_mapping_add_entry(),
 *	  port_mapping_delete_entry() and the twelve port_mapping_{get,set}_*.
 *
 *	The rules are "config port_forwarding" sections of UCI package
 *	firewall_clay, shared by every WAN connection.  What splits them between
 *	instances is the "interface" option, and the name it is compared against
 *	is NOT a netdev:
 *
 *	  routed IPoE : get_ipoe_interface_name() -> "pon.<vlan_id>" when
 *	                vlan_active is 1 and vlan_id is set, otherwise "pon",
 *	  PPPoE       : "pppoe-if<id>",
 *	  bridged     : nothing -- the bridge branch never registered PortMapping.
 *
 *	The instance number is the POSITION among the matching rules, counted
 *	from 1 in UCI file order.  It is not the section index and it is not
 *	stable: deleting rule 2 of three renumbers the third to 2.  That is what
 *	the shell did with its rule_index counter, and an ACS that walks the
 *	object each session sees the same thing it always has.
 *
 *	Kept verbatim, each one deliberate:
 *
 *	1. X_AIS_Name and PortMappingDescription are the SAME UCI option,
 *	   service_type.  Writing one changes the other.  They differ only when
 *	   the option is empty: Name answers "-", Description answers "".
 *	2. Every setter queues "ubus call hni.service commit" with
 *	   PortForwarding/ApplyRule for the end of the session, which is what
 *	   apply_port_forwarding_rule() did.  Nothing is applied mid-session.
 *	3. Writing RemoteHost or X_AIS_RemoteHostEndRange with an EMPTY value
 *	   clears BOTH remote_start_ip and remote_end_ip.  Only the empty case is
 *	   symmetric like that; a real address writes just its own option.
 *	4. The protocol enum is accepted case-insensitively and "udp/tcp" and
 *	   "both" are both stored as "tcp/udp" (validate_protocol()).
 *
 *	One DIFFERENCE, additive: the shell registered PortMapping only when
 *	"ip route" showed a default gateway -- a global condition, identical for
 *	every entry, evaluated once per session.  A static C tree cannot make an
 *	object appear and disappear, so PortMapping is always present here.  No
 *	value changes: the rules come from firewall_clay, not from the route
 *	table.  What changes is that GetParameterNames now lists the object on a
 *	box with no default route, where the shell omitted it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <arpa/inet.h>

#include "dmuci.h"
#include "dmubus.h"
#include "dmcommon.h"
#include "dmjson.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "wanconn_mtk.h"
#include "device_ip_mtk.h"

#define PM_PACKAGE	"firewall_clay"
#define PM_TYPE		"port_forwarding"
#define PM_MAX_RULES	32	/* the shell's "seq 0 31" */

/* ------------------------------------------------------------------ */
/* which rules belong to which connection instance                     */
/* ------------------------------------------------------------------ */

/*
 * get_ipoe_interface_name() for a routed entry, "pppoe-if<id>" for a PPP one.
 * Bridged entries get "" and therefore no rules at all, like the shell.
 */
static char *pm_iface_name(struct wan_entry *e)
{
	static char buf[64];

	if (!e || e->bridge)
		return "";
	if (e->ppp) {
		snprintf(buf, sizeof(buf), "pppoe-%s", e->if4);
		return buf;
	}
	if (strcmp(wan_entry_opt(e, "vlan_active"), "1") == 0) {
		char *vid = wan_entry_opt(e, "vlan_id");

		if (vid[0]) {
			snprintf(buf, sizeof(buf), "pon.%s", vid);
			return buf;
		}
	}
	snprintf(buf, sizeof(buf), "pon");
	return buf;
}

static void pm_apply(void)
{
	mtk_apply_service("ubus call hni.service commit "
			  "'{ \"param\": \"PortForwarding\", \"action\": \"ApplyRule\" }'");
}

static char *pm_opt(void *data, char *option)
{
	return wan_sect_opt((struct uci_section *)data, option);
}

/* every setter of this object: write the option, then queue the apply */
static int pm_write(void *data, const char *option, const char *value)
{
	struct uci_section *s = (struct uci_section *)data;

	if (!s)
		return FAULT_9002;
	dmuci_set_value_by_section(s, (char *)option, (char *)(value ? value : ""));
	pm_apply();
	return 0;
}

static int pm_port_ok(const char *value, long *out)
{
	return wan_str_is_uint(value, out) && *out <= 65535;
}

/* is_valid_ip(): IPv4 or IPv6 */
static int pm_ip_ok(const char *value)
{
	unsigned int v4;
	struct in6_addr v6;

	if (!value || !value[0])
		return 0;
	if (mtk_ipv4_parse(value, &v4) == 0)
		return 1;
	return inet_pton(AF_INET6, value, &v6) == 1;
}

/* validate_protocol(): lower case, and two spellings fold into "tcp/udp" */
static const char *pm_protocol_of(const char *value)
{
	if (!value)
		return NULL;
	if (strcasecmp(value, "udp") == 0)
		return "udp";
	if (strcasecmp(value, "tcp") == 0)
		return "tcp";
	if (strcasecmp(value, "tcp/udp") == 0 || strcasecmp(value, "udp/tcp") == 0 ||
	    strcasecmp(value, "both") == 0)
		return "tcp/udp";
	return NULL;
}

/*
 * One pass over firewall_clay in file order, counting and optionally
 * collecting the rules whose "interface" is this connection's.  Returns how
 * many there are; the shell's rule_index is the position in that list.
 */
static int pm_rules(struct wan_entry *e, struct uci_section **out, int max)
{
	const char *want = pm_iface_name(e);
	struct uci_section *s;
	int n = 0;

	if (!want[0])
		return 0;
	uci_foreach_sections(PM_PACKAGE, PM_TYPE, s) {
		if (strcmp(wan_sect_opt(s, "interface"), want) != 0)
			continue;
		if (out && n < max)
			out[n] = s;
		n++;
	}
	return n;
}

/* ------------------------------------------------------------------ */
/* leaves                                                              */
/* ------------------------------------------------------------------ */

static int get_pm_enabled(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(pm_opt(data, "enabled"), "1") == 0 ? "true" : "false";
	return 0;
}

static int set_pm_enabled(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = mtk_parse_bool(value);

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return pm_write(data, "enabled", b ? "1" : "0");
}

/* no range check in the shell either: any string lands in lease_dur */
static int get_pm_lease(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = pm_opt(data, "lease_dur");
	return 0;
}

static int set_pm_lease(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (action == VALUECHECK)
		return 0;
	return pm_write(data, "lease_dur", value);
}

#define PM_PORT_LEAF(getfn, setfn, option)					\
static int getfn(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)	\
{										\
	*value = pm_opt(data, option);						\
	return 0;								\
}										\
										\
static int setfn(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)	\
{										\
	long port = 0;								\
										\
	if (!pm_port_ok(value, &port))						\
		return FAULT_9007;						\
	if (action == VALUECHECK)						\
		return 0;							\
	return pm_write(data, option, value);					\
}

PM_PORT_LEAF(get_pm_ext_port, set_pm_ext_port, "ext_start_port")
PM_PORT_LEAF(get_pm_ext_port_end, set_pm_ext_port_end, "ext_end_port")
PM_PORT_LEAF(get_pm_int_port, set_pm_int_port, "local_start_port")
PM_PORT_LEAF(get_pm_int_port_end, set_pm_int_port_end, "local_end_port")

static int get_pm_protocol(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = pm_opt(data, "protocol");
	return 0;
}

static int set_pm_protocol(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *p = pm_protocol_of(value);

	if (!p)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return pm_write(data, "protocol", p);
}

static int get_pm_internal_client(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = pm_opt(data, "ip_address");
	return 0;
}

static int set_pm_internal_client(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!pm_ip_ok(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return pm_write(data, "ip_address", value);
}

/*
 * RemoteHost and X_AIS_RemoteHostEndRange: an EMPTY value clears both ends of
 * the range, a real address writes only its own option.  Asymmetric on
 * purpose, it is what both shell setters did.
 */
static int pm_set_remote(void *data, const char *option, const char *value, int action)
{
	struct uci_section *s = (struct uci_section *)data;

	if (!s)
		return FAULT_9002;
	if (!value || !value[0]) {
		if (action == VALUECHECK)
			return 0;
		dmuci_set_value_by_section(s, "remote_start_ip", "");
		dmuci_set_value_by_section(s, "remote_end_ip", "");
		pm_apply();
		return 0;
	}
	if (!pm_ip_ok(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return pm_write(data, option, value);
}

static int get_pm_remote_host(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = pm_opt(data, "remote_start_ip");
	return 0;
}

static int set_pm_remote_host(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return pm_set_remote(data, "remote_start_ip", value, action);
}

static int get_pm_remote_host_end(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = pm_opt(data, "remote_end_ip");
	return 0;
}

static int set_pm_remote_host_end(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return pm_set_remote(data, "remote_end_ip", value, action);
}

/*
 * Both of these are service_type.  Writing one overwrites the other; they
 * differ only in what an unset option reads as, and in the length they
 * accept: 128 for the name, 256 for the description.
 */
static int get_pm_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = pm_opt(data, "service_type");

	*value = v[0] ? v : "-";
	return 0;
}

static int set_pm_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (value && strlen(value) > 128)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return pm_write(data, "service_type", value);
}

static int get_pm_description(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = pm_opt(data, "service_type");
	return 0;
}

static int set_pm_description(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (value && strlen(value) > 256)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return pm_write(data, "service_type", value);
}

/* ------------------------------------------------------------------ */
/* object level                                                        */
/* ------------------------------------------------------------------ */

static int get_pm_entries(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char buf[16];

	snprintf(buf, sizeof(buf), "%d", pm_rules((struct wan_entry *)data, NULL, 0));
	*value = dmstrdup(buf);
	return 0;
}

/*
 * port_mapping_add_entry(): refuse past 32 rules, then append a section with
 * the product's defaults and report the TOTAL number of port_forwarding
 * sections -- not the number of this connection's rules, and not the new
 * rule's position.  Copied as is: that is the instance number the ACS was
 * handed before.
 */
static int pm_add(const char *iface, const char *proto, char **instance)
{
	struct uci_section *s, *added = NULL;
	char buf[16];
	/* dmuci_add_section() writes the new section name through this on every
	 * path, success or failure -- it must not be NULL (dmuci.c:...) */
	char *added_name = NULL;
	int n = 0;

	uci_foreach_sections(PM_PACKAGE, PM_TYPE, s)
		n++;
	if (n >= PM_MAX_RULES)
		return FAULT_9004;	/* E_RESOURCES_EXCEEDED */
	dmuci_add_section(PM_PACKAGE, PM_TYPE, &added, &added_name);
	if (!added)
		return FAULT_9002;
	dmuci_set_value_by_section(added, "enabled", "0");
	dmuci_set_value_by_section(added, "lease_dur", "0");
	dmuci_set_value_by_section(added, "interface", (char *)iface);
	dmuci_set_value_by_section(added, "service_type", "Default");
	dmuci_set_value_by_section(added, "local_start_port", "0");
	dmuci_set_value_by_section(added, "local_end_port", "0");
	dmuci_set_value_by_section(added, "ext_start_port", "0");
	dmuci_set_value_by_section(added, "ext_end_port", "0");
	dmuci_set_value_by_section(added, "protocol", (char *)proto);
	snprintf(buf, sizeof(buf), "%d", n + 1);
	*instance = dmstrdup(buf);
	return 0;
}

static int add_pm_instance(char *refparam, struct dmctx *ctx, void *data, char **instance)
{
	const char *iface = pm_iface_name((struct wan_entry *)data);

	if (!iface[0])
		return FAULT_9002;
	return pm_add(iface, "tcp/udp", instance);
}

static int del_pm_instance(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	struct uci_section *s = (struct uci_section *)data;

	if (del_action != DEL_INST)
		return FAULT_9005;	/* the shell had no "delete all" here */
	if (!s)
		return FAULT_9002;
	dmuci_delete_by_section(s, NULL, NULL);
	pm_apply();
	return 0;
}

/* instance = position among the matching rules, from 1, in UCI file order */
static int browsePortMappingInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct uci_section *list[PM_MAX_RULES];
	char *idx, *idx_last = NULL;
	int n, i;

	n = pm_rules((struct wan_entry *)prev_data, list, PM_MAX_RULES);
	if (n > PM_MAX_RULES)
		n = PM_MAX_RULES;
	for (i = 0; i < n; i++) {
		idx = handle_update_instance(4, dmctx, &idx_last, update_instance_without_section,
					     1, i + 1);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)list[i], idx) == DM_STOP)
			break;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tPortMappingParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"PortMappingEnabled", &DMWRITE, DMT_BOOL, get_pm_enabled, set_pm_enabled, NULL, NULL},
{"PortMappingLeaseDuration", &DMWRITE, DMT_UNINT, get_pm_lease, set_pm_lease, NULL, NULL},
{"RemoteHost", &DMWRITE, DMT_STRING, get_pm_remote_host, set_pm_remote_host, NULL, NULL},
{"InternalPort", &DMWRITE, DMT_UNINT, get_pm_int_port, set_pm_int_port, NULL, NULL},
{"ExternalPort", &DMWRITE, DMT_UNINT, get_pm_ext_port, set_pm_ext_port, NULL, NULL},
{"ExternalPortEndRange", &DMWRITE, DMT_UNINT, get_pm_ext_port_end, set_pm_ext_port_end, NULL, NULL},
{"PortMappingProtocol", &DMWRITE, DMT_STRING, get_pm_protocol, set_pm_protocol, NULL, NULL},
{"InternalClient", &DMWRITE, DMT_STRING, get_pm_internal_client, set_pm_internal_client, NULL, NULL},
{"PortMappingDescription", &DMWRITE, DMT_STRING, get_pm_description, set_pm_description, NULL, NULL},
{"X_AIS_Name", &DMWRITE, DMT_STRING, get_pm_name, set_pm_name, NULL, NULL},
{"X_AIS_InternalPortEndRange", &DMWRITE, DMT_UNINT, get_pm_int_port_end, set_pm_int_port_end, NULL, NULL},
{"X_AIS_RemoteHostEndRange", &DMWRITE, DMT_STRING, get_pm_remote_host_end, set_pm_remote_host_end, NULL, NULL},
{0}
};

/* one table pair, merged into both connection objects by dm_registry */
static DMOBJ tConnPortMappingObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"PortMapping", &DMWRITE, add_pm_instance, del_pm_instance, NULL, browsePortMappingInst,
 NULL, NULL, NULL, tPortMappingParam, NULL},
{0}
};

static DMLEAF tConnPortMappingParam[] = {
{"PortMappingNumberOfEntries", &DMREAD, DMT_UNINT, get_pm_entries, NULL, NULL, NULL},
{0}
};

static DMOBJ tWanCxDevPmObj[] = {
{"WANIPConnection", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 tConnPortMappingObj, tConnPortMappingParam, NULL},
{"WANPPPConnection", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 tConnPortMappingObj, tConnPortMappingParam, NULL},
{0}
};

static DMOBJ tWanDevicePmObj[] = {
{"WANConnectionDevice", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 tWanCxDevPmObj, NULL, NULL},
{0}
};

static DMOBJ tWanDevicePmRoot[] = {
{"WANDevice", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tWanDevicePmObj, NULL, NULL},
{0}
};

/*
 * No .paths: wan_mtk.c claims the whole WANDevice branch (K8), dm_registry
 * merges this tree into it.
 */
static const struct dm_module portmapping_mtk_module = {
	.name  = "mtk-portmapping",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tWanDevicePmRoot,
};
DM_MODULE_REGISTER(portmapping_mtk_module);

/* ------------------------------------------------------------------ */
/* TR-181 (cwmp.cpe.datamodel=tr181)                                    */
/* ------------------------------------------------------------------ */

/*
 * Device.NAT.PortMapping.{i}: every port_forwarding rule of firewall_clay, in
 * file order, numbered by position like the TR-098 PortMapping of a
 * connection (so not stable either), with the same getters and setters.
 * Interface is the Device.IP.Interface of the connection whose name
 * (pm_iface_name()) the rule's "interface" option holds; writing it writes
 * that name.  An AddObject makes the rule with the product's defaults and no
 * connection yet: the ACS sets Interface, which TR-181 has for that.
 * PortMappingNumberOfEntries of a connection has no TR-181 counterpart (the
 * TR-181 count is global).
 *
 * T7 S5d: Protocol is TCP or UDP in TR-181, the product also stores
 * "tcp/udp".  Such a rule is two rows, TCP then UDP, next to each other.
 * Writing a leaf of one of them first splits the rule: the section becomes
 * the TCP rule and a UDP copy is put right after it (uci_reorder_section), so
 * every row keeps its number and protocol for the rest of the session, then
 * the write goes to the rule of the row written.  Deleting one of the two rows leaves
 * the rule with the other protocol.  AddObject makes one TCP row, not the
 * product's tcp/udp default, which would add two.
 */

/* half: 0 the whole rule, 1 the TCP row of a tcp/udp rule, 2 its UDP row */
struct pm181_row {
	struct uci_section *s;
	int half;
};

typedef int (*pm_setfn)(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action);

static int pm_both(struct uci_section *s)
{
	const char *p = pm_protocol_of(wan_sect_opt(s, "protocol"));

	return p && strcmp(p, "tcp/udp") == 0;
}

static int pm_sections(void)
{
	struct uci_section *s;
	int n = 0;

	uci_foreach_sections(PM_PACKAGE, PM_TYPE, s)
		n++;
	return n;
}

/* the rows in instance order; returns how many (out may be NULL) */
static int pm181_rows(struct pm181_row *out, int max)
{
	struct uci_section *s;
	int n = 0, k = 0, h, last;

	uci_foreach_sections(PM_PACKAGE, PM_TYPE, s) {
		if (k++ >= PM_MAX_RULES)
			break;
		last = pm_both(s) ? 2 : 0;
		for (h = last ? 1 : 0; h <= last; h++) {
			if (out && n < max) {
				out[n].s = s;
				out[n].half = h;
			}
			n++;
		}
	}
	return n;
}

/* a row of a tcp/udp rule becomes a rule of its own, see above */
static int pm181_split(struct pm181_row *r)
{
	struct uci_section *copy = NULL;
	struct uci_element *e, *li;
	char *name = NULL;
	int pos = 0;

	if (!r->half)
		return 0;
	if (pm_sections() >= PM_MAX_RULES)
		return FAULT_9004;
	dmuci_add_section(PM_PACKAGE, PM_TYPE, &copy, &name);
	if (!copy)
		return FAULT_9002;
	uci_foreach_element(&r->s->options, e) {
		struct uci_option *o = uci_to_option(e);

		if (o->type == UCI_TYPE_STRING) {
			dmuci_set_value_by_section(copy, e->name, o->v.string);
		} else {
			uci_foreach_element(&o->v.list, li)
				dmuci_add_list_value_by_section(copy, e->name, li->name);
		}
	}
	/* the rows keep their order: the rule stays the TCP one, the copy
	 * after it is the UDP one, and the row now names its own rule */
	dmuci_set_value_by_section(copy, "protocol", "udp");
	dmuci_set_value_by_section(r->s, "protocol", "tcp");
	/* uci_reorder_section() counts from 0 among every section of the
	 * package, the copy taken out: the original's index + 1 */
	uci_foreach_element(&r->s->package->sections, e) {
		pos++;
		if (uci_to_section(e) == r->s)
			break;
	}
	uci_reorder_section(uci_ctx, copy, pos);
	if (r->half == 2)
		r->s = copy;
	r->half = 0;
	pm_apply();
	return 0;
}

/* every writable leaf of a row: check, split the rule if needed, write */
static int pm181_set(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action,
		     pm_setfn fn)
{
	struct pm181_row *r = (struct pm181_row *)data;
	int f;

	if (!r || !r->s)
		return FAULT_9002;
	if (action == VALUECHECK) {
		f = fn(refparam, ctx, r->s, instance, value, action);
		if (f)
			return f;
		return r->half && pm_sections() >= PM_MAX_RULES ? FAULT_9004 : 0;
	}
	f = pm181_split(r);
	if (f)
		return f;
	return fn(refparam, ctx, r->s, instance, value, action);
}

#define PM181_LEAF(name, getbase, setbase)					\
static int get_pm181_##name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)	\
{										\
	return getbase(refparam, ctx, ((struct pm181_row *)data)->s, instance, value);	\
}										\
										\
static int set_pm181_##name(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)	\
{										\
	return pm181_set(refparam, ctx, data, instance, value, action, setbase);	\
}

PM181_LEAF(enabled, get_pm_enabled, set_pm_enabled)
PM181_LEAF(lease, get_pm_lease, set_pm_lease)
PM181_LEAF(remote_host, get_pm_remote_host, set_pm_remote_host)
PM181_LEAF(ext_port, get_pm_ext_port, set_pm_ext_port)
PM181_LEAF(ext_port_end, get_pm_ext_port_end, set_pm_ext_port_end)
PM181_LEAF(int_port, get_pm_int_port, set_pm_int_port)
PM181_LEAF(internal_client, get_pm_internal_client, set_pm_internal_client)
PM181_LEAF(description, get_pm_description, set_pm_description)
PM181_LEAF(name, get_pm_name, set_pm_name)
PM181_LEAF(int_port_end, get_pm_int_port_end, set_pm_int_port_end)
PM181_LEAF(remote_host_end, get_pm_remote_host_end, set_pm_remote_host_end)

static int browsePm181Inst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct pm181_row *rows = dmcalloc(2 * PM_MAX_RULES, sizeof(*rows));
	char *idx, *idx_last = NULL;
	int n, i;

	if (!rows)
		return 0;
	n = pm181_rows(rows, 2 * PM_MAX_RULES);
	for (i = 0; i < n; i++) {
		idx = handle_update_instance(1, dmctx, &idx_last, update_instance_without_section, 1, i + 1);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)&rows[i], idx) == DM_STOP)
			break;
	}
	return 0;
}

/* the connection a rule belongs to, NULL when its interface names none */
static struct wan_entry *pm181_conn(struct uci_section *s)
{
	struct wan_entry *list = dmcalloc(2 * WAN_MAX_ENTRIES, sizeof(*list)), *e;
	const char *want = wan_sect_opt(s, "interface");
	int n, i;

	if (!list || !want[0])
		return NULL;
	n = wan_entries_all(&list, 2 * WAN_MAX_ENTRIES);
	for (i = 0; i < n; i++) {
		if (strcmp(pm_iface_name(&list[i]), want) != 0)
			continue;
		e = dmcalloc(1, sizeof(*e));
		if (e)
			*e = list[i];
		return e;
	}
	return NULL;
}

static int get_pm181_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = pm181_conn(((struct pm181_row *)data)->s);

	*value = e ? wan181_ipif(e) : "";
	return 0;
}

/* data is the rule's section */
static int pm181_interface_of(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *prefix = mtk_ipif_prefix();
	size_t l = strlen(prefix);
	struct wan_entry e;
	char *sec;

	if (!value || strncmp(value, prefix, l) != 0)
		return FAULT_9007;
	sec = dip_section_of_instance(value + l);
	if (!sec || !wan_entry_of_sec(sec, &e) || e.bridge)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return pm_write(data, "interface", pm_iface_name(&e));
}

static int set_pm181_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return pm181_set(refparam, ctx, data, instance, value, action, pm181_interface_of);
}

static int get_pm181_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct uci_section *s = ((struct pm181_row *)data)->s;

	if (!pm181_conn(s))
		*value = "Error_Misconfigured";
	else
		*value = strcmp(wan_sect_opt(s, "enabled"), "1") == 0 ? "Enabled" : "Disabled";
	return 0;
}

static int get_pm181_protocol(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct pm181_row *r = (struct pm181_row *)data;
	char *u;

	if (r->half) {
		*value = r->half == 1 ? "TCP" : "UDP";
		return 0;
	}
	*value = dmstrdup(wan_sect_opt(r->s, "protocol"));
	for (u = *value; u && *u; u++)
		*u = (char)toupper((unsigned char)*u);
	return 0;
}

/* TCP or UDP, any case; not the product's tcp/udp (data is the section) */
static int pm181_protocol_one(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *p = pm_protocol_of(value);

	if (!p || strcmp(p, "tcp/udp") == 0)
		return FAULT_9007;
	return set_pm_protocol(refparam, ctx, data, instance, value, action);
}

static int set_pm181_protocol(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return pm181_set(refparam, ctx, data, instance, value, action, pm181_protocol_one);
}

/* one TCP row, numbered after every row there is */
static int add_pm181_instance(char *refparam, struct dmctx *ctx, void *data, char **instance)
{
	int rows = pm181_rows(NULL, 0);
	char *unused = NULL;
	int f = pm_add("", "tcp", &unused);

	if (f)
		return f;
	dmasprintf(instance, "%d", rows + 1);
	return 0;
}

static int del_pm181_instance(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	struct pm181_row *r = (struct pm181_row *)data;

	if (del_action != DEL_INST)
		return FAULT_9005;
	if (!r || !r->s)
		return FAULT_9002;
	if (r->half)
		return pm_write(r->s, "protocol", r->half == 1 ? "udp" : "tcp");
	return del_pm_instance(refparam, ctx, r->s, instance, del_action);
}

static int get_pm181_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	dmasprintf(value, "%d", pm181_rows(NULL, 0));
	return 0;
}

/* T7 S5b: a rule of the product is on its connection (Interface); one
 * without an interface applies on all of them.  Its value only: the
 * product has no rule that is on every WAN */
static int get_pm181_allif(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *ifc = NULL;

	get_pm181_interface(refparam, ctx, data, instance, &ifc);
	*value = (ifc && *ifc) ? "false" : "true";
	return 0;
}

MTK_SET_SAME_BOOL(pm181_allif, get_pm181_allif)

static DMLEAF tPm181Param[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_pm181_enabled, set_pm181_enabled, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_pm181_status, NULL, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_pm181_interface, set_pm181_interface, NULL, NULL},
{"AllInterfaces", &DMWRITE, DMT_BOOL, get_pm181_allif, set_same_pm181_allif, NULL, NULL},
{"LeaseDuration", &DMWRITE, DMT_UNINT, get_pm181_lease, set_pm181_lease, NULL, NULL},
{"RemoteHost", &DMWRITE, DMT_STRING, get_pm181_remote_host, set_pm181_remote_host, NULL, NULL},
{"ExternalPort", &DMWRITE, DMT_UNINT, get_pm181_ext_port, set_pm181_ext_port, NULL, NULL},
{"ExternalPortEndRange", &DMWRITE, DMT_UNINT, get_pm181_ext_port_end, set_pm181_ext_port_end, NULL, NULL},
{"InternalPort", &DMWRITE, DMT_UNINT, get_pm181_int_port, set_pm181_int_port, NULL, NULL},
{"Protocol", &DMWRITE, DMT_STRING, get_pm181_protocol, set_pm181_protocol, NULL, NULL},
{"InternalClient", &DMWRITE, DMT_STRING, get_pm181_internal_client, set_pm181_internal_client, NULL, NULL},
{"Description", &DMWRITE, DMT_STRING, get_pm181_description, set_pm181_description, NULL, NULL},
{"X_AIS_Name", &DMWRITE, DMT_STRING, get_pm181_name, set_pm181_name, NULL, NULL},
{"X_AIS_InternalPortEndRange", &DMWRITE, DMT_UNINT, get_pm181_int_port_end, set_pm181_int_port_end, NULL, NULL},
{"X_AIS_RemoteHostEndRange", &DMWRITE, DMT_STRING, get_pm181_remote_host_end, set_pm181_remote_host_end, NULL, NULL},
{0}
};

static DMLEAF tNat181PmCountParam[] = {
{"PortMappingNumberOfEntries", &DMREAD, DMT_UNINT, get_pm181_count, NULL, NULL, NULL},
{0}
};

static DMOBJ tNat181PmObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"PortMapping", &DMWRITE, add_pm181_instance, del_pm181_instance, NULL, browsePm181Inst, NULL, NULL, NULL, tPm181Param, NULL},
{0}
};

static DMOBJ tPm181Root[] = {
{"NAT", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tNat181PmObj, tNat181PmCountParam, NULL},
{0}
};

static const char *const pm181_mtk_paths[] = {
	"Device.NAT.PortMapping.",
	"Device.NAT.PortMappingNumberOfEntries",
	NULL
};

static const struct dm_module pm181_mtk_module = {
	.name  = "mtk-portmapping-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tPm181Root,
	.paths = pm181_mtk_paths,
};
DM_MODULE_REGISTER(pm181_mtk_module);
