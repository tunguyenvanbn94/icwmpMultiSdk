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
#include <arpa/inet.h>

#include "dmuci.h"
#include "dmubus.h"
#include "dmcommon.h"
#include "dmjson.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "wanconn_mtk.h"

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
static int add_pm_instance(char *refparam, struct dmctx *ctx, void *data, char **instance)
{
	struct wan_entry *e = (struct wan_entry *)data;
	const char *iface = pm_iface_name(e);
	struct uci_section *s, *added = NULL;
	char buf[16];
	/* dmuci_add_section() writes the new section name through this on every
	 * path, success or failure -- it must not be NULL (dmuci.c:...) */
	char *added_name = NULL;
	int n = 0;

	if (!iface[0])
		return FAULT_9002;
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
	dmuci_set_value_by_section(added, "protocol", "tcp/udp");
	snprintf(buf, sizeof(buf), "%d", n + 1);
	*instance = dmstrdup(buf);
	return 0;
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
 * The PortMapping object path IS claimed here, unlike the connection objects
 * above it: AddObject and DeleteObject on a port mapping now go to this
 * module instead of sdk/mtk/compat/, because both are implemented here.
 */
static const char *const portmapping_mtk_paths[] = {
	"InternetGatewayDevice.WANDevice.{i}.WANConnectionDevice.{i}.WANIPConnection.{i}.PortMapping.",
	"InternetGatewayDevice.WANDevice.{i}.WANConnectionDevice.{i}.WANIPConnection.{i}.PortMappingNumberOfEntries",
	"InternetGatewayDevice.WANDevice.{i}.WANConnectionDevice.{i}.WANPPPConnection.{i}.PortMapping.",
	"InternetGatewayDevice.WANDevice.{i}.WANConnectionDevice.{i}.WANPPPConnection.{i}.PortMappingNumberOfEntries",
	NULL
};

static const struct dm_module portmapping_mtk_module = {
	.name  = "mtk-portmapping",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tWanDevicePmRoot,
	.paths = portmapping_mtk_paths,
};
DM_MODULE_REGISTER(portmapping_mtk_module);
