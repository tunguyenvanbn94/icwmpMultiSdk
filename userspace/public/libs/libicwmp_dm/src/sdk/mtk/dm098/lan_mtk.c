/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.LANDevice.1. of the MTK/Airoha product: the object
 *	itself, its counters, and the whole LANHostConfigManagement subtree.
 *
 *	Ported from functions/tr098/lan_device, landevice_execute_params() and
 *	lanhostconfigManagement_execute_params().  Same UCI options, same reload
 *	commands and the same fault codes as the shell it replaces -- the WebUI
 *	and hal_gateway write these very options, so moving a value elsewhere
 *	would be a product change, not a CWMP change.
 *
 *	The neighbouring subtrees are their own modules, merged into this same
 *	LANDevice object by dm_registry.c: lanhosts_mtk.c (Hosts), laneth_mtk.c
 *	(LANEthernetInterfaceConfig), x_ais_mesh_mtk.c (X_AIS_Mesh).  The single
 *	LANDevice instance is browsed here, they leave that field NULL.
 *	WLANConfiguration is wlan_mtk.c, wlanassoc_mtk.c and wlansec_mtk.c (P3).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmmem.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "device_ip_mtk.h"
#include "wanconn_mtk.h"

/* wireless.<radio> of this board, functions/tr098/lan_device:14 */
#define RADIO_DEVICE_2G		"MT7993_1_1"
#define RADIO_DEVICE_5G		"MT7993_1_2"
/* uci txpower percent <-> the NBTC steps the operator's ACS uses */
#define TXPOWER_LOW		"30"
#define TXPOWER_MEDIUM		"60"
#define TXPOWER_HIGH		"100"
#define NBTC_STANDARD_50	"50"
#define NBTC_STANDARD_100	"75"
#define NBTC_STANDARD_MAX	"100"

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/* dhcp.lan.configurable = 0 means the product locked the LAN pool: every
 * shell setter below refuses then, most with 9002 and MaxAddress with 9007.
 * Kept exactly as it was, an ACS script tells the two apart. */
static int lan_locked(void)
{
	return strcmp(mtk_uci("dhcp", "lan", "configurable"), "0") == 0;
}

static void dnsmasq_reload(void)
{
	mtk_apply_service("/etc/init.d/dnsmasq reload &");
}

static void odhcpd_reload(void)
{
	mtk_apply_service("/etc/init.d/odhcpd reload &");
}

/* lan_device_network_reload() */
static void network_reload(void)
{
	mtk_apply_service("/etc/init.d/network reload");
}

/* (gateway & netmask), the first address of the LAN subnet */
static int lan_subnet_base(unsigned int *base, unsigned int *mask)
{
	unsigned int gw, nm;

	if (mtk_ipv4_parse(mtk_uci("network", "lan", "ipaddr"), &gw))
		return -1;
	if (mtk_ipv4_parse(mtk_uci("network", "lan", "netmask"), &nm))
		return -1;
	if (base)
		*base = gw & nm;
	if (mask)
		*mask = nm;
	return 0;
}

static long uci_long(char *package, char *section, char *option, long def)
{
	char *v = mtk_uci(package, section, option);
	char *end;
	long n;

	if (!v || !*v)
		return def;
	n = strtol(v, &end, 10);
	if (*end)
		return def;
	return n;
}

/* ------------------------------------------------------------------ */
/* LANDevice.1 leaves                                                   */
/* ------------------------------------------------------------------ */

/* ls /sys/class/net/br-lan/brif | grep eth | wc -l */
static int get_eth_entries(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct dirent *d;
	int n = 0;
	DIR *dir;

	dir = opendir("/sys/class/net/br-lan/brif");
	if (dir) {
		while ((d = readdir(dir)) != NULL) {
			if (strstr(d->d_name, "eth"))
				n++;
		}
		closedir(dir);
	}
	dmasprintf(value, "%d", n);
	return 0;
}

static int get_usb_entries(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "0";
	return 0;
}

/* get_LANWLANConfigurationNumberOfEntries(): AP interfaces that are not disabled */
static int get_wlan_entries(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	static const char *const ifaces[] = {
		"ra0", "ra1", "ra2", "ra3", "ra4", "ra5",
		"rai0", "rai1", "rai2", "rai3", "rai4", "rai5", NULL
	};
	int i, n = 0;

	for (i = 0; ifaces[i]; i++) {
		char *mode = mtk_uci("wireless", (char *)ifaces[i], "mode");
		char *disabled = mtk_uci("wireless", (char *)ifaces[i], "disabled");

		if (strcmp(mode, "ap") == 0 && strcmp(disabled, "1") != 0)
			n++;
	}
	dmasprintf(value, "%d", n);
	return 0;
}

static int txpower_get(const char *radio, char **value)
{
	char *v = mtk_uci("wireless", (char *)radio, "txpower");

	if (!v || !*v)
		v = TXPOWER_HIGH;
	if (strcmp(v, TXPOWER_LOW) == 0)
		*value = NBTC_STANDARD_50;
	else if (strcmp(v, TXPOWER_MEDIUM) == 0)
		*value = NBTC_STANDARD_100;
	else
		*value = NBTC_STANDARD_MAX;
	return 0;
}

static int txpower_set(const char *radio, char *value, int action)
{
	const char *target;

	if (strcmp(value, NBTC_STANDARD_50) == 0)
		target = TXPOWER_LOW;
	else if (strcmp(value, NBTC_STANDARD_100) == 0)
		target = TXPOWER_MEDIUM;
	else if (strcmp(value, NBTC_STANDARD_MAX) == 0)
		target = TXPOWER_HIGH;
	else
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value("wireless", (char *)radio, "txpower", (char *)target);
	/* mapd_wireless_commit_reload(): mapd and wireless are committed by the
	 * engine at the end of the RPC, the reload is what has to be queued */
	mtk_apply_service("/sbin/wifi reload &");
	return 0;
}

static int get_txpower_2g(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return txpower_get(RADIO_DEVICE_2G, value);
}

static int set_txpower_2g(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return txpower_set(RADIO_DEVICE_2G, value, action);
}

static int get_txpower_5g(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return txpower_get(RADIO_DEVICE_5G, value);
}

static int set_txpower_5g(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return txpower_set(RADIO_DEVICE_5G, value, action);
}

/* ------------------------------------------------------------------ */
/* LANHostConfigManagement                                              */
/* ------------------------------------------------------------------ */

static int get_lanhost_mac(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *mac = mtk_file_line("/sys/class/net/br-lan/address");
	char *u;

	if (!mac || !*mac) {
		/* what get_LanHostConfig_MacAddress() did */
		char *argv[] = { "/sbin/ifconfig", "br-lan", NULL };
		char *out = mtk_exec(argv);
		char *p = out ? strstr(out, "HWaddr") : NULL;

		if (p) {
			char buf[32];
			int i = 0;

			p += 6;
			while (*p == ' ')
				p++;
			while (*p && *p != ' ' && *p != '\n' && i < (int)sizeof(buf) - 1)
				buf[i++] = *p++;
			buf[i] = '\0';
			mac = dmstrdup(buf);
		}
	}
	/* ifconfig prints HWaddr in capitals and that is what the ACS has been
	 * reading; sysfs has them in lower case */
	for (u = mac; u && *u; u++)
		*u = toupper((unsigned char)*u);
	*value = (mac && *mac) ? mac : "";
	return 0;
}

static int get_dhcp_configurable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = lan_locked() ? "false" : "true";
	return 0;
}

/*
 * "false" restores the factory LAN pool, option by option, from /rom/etc/config
 * -- the shell did the same with $UCI_GET_DEFAULT.  Anything the factory tree
 * does not carry is left to the literal defaults the shell also hardcoded.
 */
static int set_dhcp_configurable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	static const struct {
		const char *option;
		const char *value;
	} defaults[] = {
		{ "dynamicdhcp", "1" }, { "dhcpv4", "server" }, { "dhcpv6", "server" },
		{ "ra", "server" }, { "ra_slaac", "1" }, { "ra_dns", "0" },
		{ "ra_flags", "managed-config other-config" }, { "stateless", "0" },
		{ "leasetime_ipv6", "43200" }, { "start_ipv6", "33" }, { "end_ipv6", "254" },
		{ "assignment", "0" }, { "dnsv4_type", "0" }, { "dhcp_option", "" },
	};
	int b = mtk_parse_bool(value), i;

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (b) {
		dmuci_set_value("dhcp", "lan", "configurable", "1");
		return 0;
	}
	dmuci_set_value("dhcp", "lan", "configurable", "0");
	for (i = 0; i < (int)(sizeof(defaults) / sizeof(defaults[0])); i++)
		dmuci_set_value("dhcp", "lan", (char *)defaults[i].option, (char *)defaults[i].value);
	dmuci_set_value("dhcp", "lan", "limit", mtk_uci_default("dhcp", "lan", "limit"));
	dmuci_set_value("dhcp", "lan", "start", mtk_uci_default("dhcp", "lan", "start"));
	dmuci_set_value("dhcp", "lan", "leasetime", mtk_uci_default("dhcp", "lan", "leasetime"));
	dmuci_set_value("network", "lan", "ipaddr", mtk_uci_default("network", "lan", "ipaddr"));
	dmuci_set_value("network", "lan", "netmask", mtk_uci_default("network", "lan", "netmask"));
	dnsmasq_reload();
	odhcpd_reload();
	network_reload();
	return 0;
}

static int get_dhcp_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci("dhcp", "lan", "dynamicdhcp"), "0") == 0 ? "false" : "true";
	return 0;
}

static int set_dhcp_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = mtk_parse_bool(value);

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (lan_locked())
		return FAULT_9002;
	dmuci_set_value("dhcp", "lan", "dynamicdhcp", b ? "1" : "0");
	dnsmasq_reload();
	return 0;
}

static int get_dhcp_relay(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "false";
	return 0;
}

static int get_min_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	unsigned int base;

	if (lan_subnet_base(&base, NULL))
		return 0;
	*value = mtk_ipv4_str(base + (unsigned int)uci_long("dhcp", "lan", "start", 0));
	return 0;
}

static int set_min_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	unsigned int base, mask, ip, gw;
	char buf[16];

	if (mtk_ipv4_parse(value, &ip))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (lan_locked())
		return FAULT_9002;
	if (lan_subnet_base(&base, &mask))
		return FAULT_9002;
	if (mtk_ipv4_parse(mtk_uci("network", "lan", "ipaddr"), &gw))
		return FAULT_9002;
	if ((ip & mask) != base || ip <= gw)
		return FAULT_9007;
	snprintf(buf, sizeof(buf), "%u", ip - base);
	dmuci_set_value("dhcp", "lan", "start", buf);
	dmuci_set_value("dhcp", "lan", "assignment", "0");
	dnsmasq_reload();
	return 0;
}

static int get_max_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	unsigned int base;

	if (lan_subnet_base(&base, NULL))
		return 0;
	*value = mtk_ipv4_str(base + (unsigned int)uci_long("dhcp", "lan", "start", 0)
			      + (unsigned int)uci_long("dhcp", "lan", "limit", 0) - 1);
	return 0;
}

/* the one setter of this object that answers 9007, not 9002, when locked */
static int set_max_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	unsigned int base, mask, ip;
	long start, limit;
	char buf[16];

	if (mtk_ipv4_parse(value, &ip))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (lan_locked())
		return FAULT_9007;
	if (lan_subnet_base(&base, &mask))
		return FAULT_9002;
	if ((ip & mask) != base)
		return FAULT_9007;
	start = uci_long("dhcp", "lan", "start", 0);
	limit = (long)(ip - base) - start + 1;
	if (limit < 0)
		return FAULT_9007;
	snprintf(buf, sizeof(buf), "%ld", limit);
	dmuci_set_value("dhcp", "lan", "limit", buf);
	dmuci_set_value("dhcp", "lan", "assignment", "0");
	dnsmasq_reload();
	return 0;
}

static int get_subnet_mask(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci("network", "lan", "netmask");
	return 0;
}

static int set_subnet_mask(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (mtk_ipv4_parse(value, NULL))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (lan_locked())
		return FAULT_9002;
	dmuci_set_value("network", "lan", "netmask", value);
	network_reload();
	return 0;
}

/* dhcp_option is "6,<dns>[,<dns>]"; empty means "the router itself" */
static int get_dns_servers(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *opt = mtk_uci("dhcp", "lan", "dhcp_option");
	char *comma;

	if (!opt || !*opt) {
		*value = mtk_uci("network", "lan", "ipaddr");
		return 0;
	}
	comma = strchr(opt, ',');
	*value = comma ? dmstrdup(comma + 1) : opt;
	return 0;
}

static int set_dns_servers(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *copy, *tok, *save = NULL, *opt;
	int n = 0;

	copy = dmstrdup(value ? value : "");
	for (tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
		if (mtk_ipv4_parse(tok, NULL))
			return FAULT_9007;
		n++;
	}
	/* the shell counted the lines of "tr ',' '\n'", so an empty string is
	 * one entry and more than two is refused */
	if (n > 2)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (lan_locked())
		return FAULT_9002;
	dmasprintf(&opt, "6,%s", value ? value : "");
	dmuci_set_value("dhcp", "lan", "dhcp_option", opt);
	dmuci_set_value("dhcp", "lan", "dnsv4_type", "1");
	dnsmasq_reload();
	return 0;
}

static int get_domain_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci("dhcp", "lan", "domain");
	return 0;
}

static int set_domain_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (action == VALUECHECK)
		return 0;
	if (lan_locked())
		return FAULT_9002;
	dmuci_set_value("dhcp", "lan", "domain", value);
	dmuci_set_value("dhcp", "@dnsmasq[0]", "domain", value);
	dnsmasq_reload();
	odhcpd_reload();
	return 0;
}

static int get_ip_routers(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci("network", "lan", "ipaddr");
	return 0;
}

/*
 * Moving the LAN address invalidates a DMZ target that pointed into the old
 * subnet, so the shell cleared it and asked hni.service to re-apply.  The ubus
 * call is queued, not made here: it has to run after the engine commits.
 */
static int set_ip_routers(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *current;

	if (mtk_ipv4_parse(value, NULL))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (lan_locked())
		return FAULT_9002;
	current = mtk_uci("network", "lan", "ipaddr");
	dmuci_set_value("network", "lan", "ipaddr", value);
	network_reload();
	if (strcmp(current, value) != 0) {
		dmuci_set_value("firewall_clay", "@dmz[0]", "active", "no");
		dmuci_set_value("firewall_clay", "@dmz[0]", "dmz_ip", "0.0.0.0");
		mtk_apply_service("ubus call hni.service commit '{ \"param\": \"DMZ\", \"action\": \"updateConfig\" }'");
	}
	return 0;
}

/* uci keeps "12h"/"30m"/"600"; the data model wants seconds */
static int get_lease_time(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = mtk_uci("dhcp", "lan", "leasetime");
	char *end;
	long n;

	if (!v || !*v) {
		*value = "43200";
		return 0;
	}
	n = strtol(v, &end, 10);
	switch (*end) {
	case 'h':
		n *= 3600;
		break;
	case 'm':
		n *= 60;
		break;
	case 's':
	case '\0':
		break;
	default:
		n = 43200;
		break;
	}
	dmasprintf(value, "%ld", n);
	return 0;
}

static int set_lease_time(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *end;

	if (!value || !*value)
		return FAULT_9007;
	strtol(value, &end, 10);
	if (*end)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (lan_locked())
		return FAULT_9002;
	dmuci_set_value("dhcp", "lan", "leasetime", value);
	dnsmasq_reload();
	return 0;
}

/*
 * Writable in the product's tree but with no setter behind them: the shell
 * registered the name with permission 1 and no set command, so an ACS write
 * was accepted and dropped.  Kept identical on purpose -- turning them into
 * 9008 would fault provisioning scripts that have been writing them for years.
 */
static int set_accept_and_drop(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return 0;
}

static int get_empty_string(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "";
	return 0;
}

static int get_use_allocated_wan(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "Normal";
	return 0;
}

static int get_passthrough_lease(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "600";
	return 0;
}

static int get_one(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "1";
	return 0;
}

static int get_zero(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "0";
	return 0;
}

/* IPInterface.1 ------------------------------------------------------ */

static int get_ipif_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "false";
	return 0;
}

static int get_ipif_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "cpe-ipif1";
	return 0;
}

static int get_ipif_addressing(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "Static";
	return 0;
}

static int browseIPInterfaceInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	char *idx, *idx_last = NULL;

	idx = handle_update_instance(2, dmctx, &idx_last, update_instance_without_section, 1, 1);
	DM_LINK_INST_OBJ(dmctx, parent_node, NULL, idx);
	return 0;
}

/* LANDevice instance: this product has exactly one, hardcoded as 1 in every
 * path of the shell library. */
static int browseLanDeviceInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	char *idx, *idx_last = NULL;

	idx = handle_update_instance(1, dmctx, &idx_last, update_instance_without_section, 1, 1);
	DM_LINK_INST_OBJ(dmctx, parent_node, NULL, idx);
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tIPInterfaceParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_ipif_enable, set_accept_and_drop, NULL, NULL},
{"Alias", &DMWRITE, DMT_STRING, get_ipif_alias, set_accept_and_drop, NULL, NULL},
{"IPInterfaceIPAddress", &DMWRITE, DMT_STRING, get_ip_routers, set_ip_routers, NULL, NULL},
{"IPInterfaceSubnetMask", &DMWRITE, DMT_STRING, get_subnet_mask, set_subnet_mask, NULL, NULL},
{"IPInterfaceAddressingType", &DMWRITE, DMT_STRING, get_ipif_addressing, set_accept_and_drop, NULL, NULL},
{0}
};

static DMLEAF tLanHostCfgParam[] = {
{"MACAddress", &DMREAD, DMT_STRING, get_lanhost_mac, NULL, NULL, NULL},
{"DHCPServerConfigurable", &DMWRITE, DMT_BOOL, get_dhcp_configurable, set_dhcp_configurable, NULL, NULL},
{"DHCPServerEnable", &DMWRITE, DMT_BOOL, get_dhcp_enable, set_dhcp_enable, NULL, NULL},
{"DHCPRelay", &DMREAD, DMT_BOOL, get_dhcp_relay, NULL, NULL, NULL},
{"MinAddress", &DMWRITE, DMT_STRING, get_min_address, set_min_address, NULL, NULL},
{"MaxAddress", &DMWRITE, DMT_STRING, get_max_address, set_max_address, NULL, NULL},
{"ReservedAddresses", &DMWRITE, DMT_STRING, get_empty_string, set_accept_and_drop, NULL, NULL},
{"SubnetMask", &DMWRITE, DMT_STRING, get_subnet_mask, set_subnet_mask, NULL, NULL},
{"DNSServers", &DMWRITE, DMT_STRING, get_dns_servers, set_dns_servers, NULL, NULL},
{"DomainName", &DMWRITE, DMT_STRING, get_domain_name, set_domain_name, NULL, NULL},
{"IPRouters", &DMWRITE, DMT_STRING, get_ip_routers, set_ip_routers, NULL, NULL},
{"DHCPLeaseTime", &DMWRITE, DMT_INT, get_lease_time, set_lease_time, NULL, NULL},
{"UseAllocatedWAN", &DMWRITE, DMT_STRING, get_use_allocated_wan, set_accept_and_drop, NULL, NULL},
{"AssociatedConnection", &DMWRITE, DMT_STRING, get_empty_string, set_accept_and_drop, NULL, NULL},
{"PassthroughLease", &DMWRITE, DMT_INT, get_passthrough_lease, set_accept_and_drop, NULL, NULL},
{"PassthroughMACAddress", &DMWRITE, DMT_STRING, get_empty_string, set_accept_and_drop, NULL, NULL},
{"AllowedMACAddresses", &DMWRITE, DMT_STRING, get_empty_string, set_accept_and_drop, NULL, NULL},
{"IPInterfaceNumberOfEntries", &DMREAD, DMT_UNINT, get_one, NULL, NULL, NULL},
{"DHCPStaticAddressNumberOfEntries", &DMREAD, DMT_UNINT, get_zero, NULL, NULL, NULL},
{"DHCPOptionNumberOfEntries", &DMREAD, DMT_UNINT, get_zero, NULL, NULL, NULL},
{"DHCPConditionalPoolNumberOfEntries", &DMREAD, DMT_UNINT, get_zero, NULL, NULL, NULL},
{0}
};

static DMOBJ tLanHostCfgObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"DHCPStaticAddress", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{"DHCPOption", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{"IPInterface", &DMREAD, NULL, NULL, NULL, browseIPInterfaceInst, NULL, NULL, NULL, tIPInterfaceParam, NULL},
{0}
};

static DMLEAF tLanDeviceParam[] = {
{"LANEthernetInterfaceNumberOfEntries", &DMREAD, DMT_UNINT, get_eth_entries, NULL, NULL, NULL},
{"LANUSBInterfaceNumberOfEntries", &DMREAD, DMT_UNINT, get_usb_entries, NULL, NULL, NULL},
{"LANWLANConfigurationNumberOfEntries", &DMREAD, DMT_UNINT, get_wlan_entries, NULL, NULL, NULL},
{"X-AIS_2-4GHzTransmitPower", &DMWRITE, DMT_UNINT, get_txpower_2g, set_txpower_2g, NULL, NULL},
{"X-AIS_5GHzTransmitPower", &DMWRITE, DMT_UNINT, get_txpower_5g, set_txpower_5g, NULL, NULL},
{0}
};

static DMOBJ tLanDeviceObj[] = {
{"LANHostConfigManagement", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tLanHostCfgObj, tLanHostCfgParam, NULL},
{"DHCPStaticAddress", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{"LANUSBInterfaceConfig", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{0}
};

static DMOBJ tLanDeviceRoot[] = {
{"LANDevice", &DMREAD, NULL, NULL, NULL, browseLanDeviceInst, NULL, NULL, tLanDeviceObj, tLanDeviceParam, NULL},
{0}
};

/*
 * Claimed path by path, not the whole LANDevice branch: WLANConfiguration is
 * still the shell's until phase P3, and dm_registry_owns() is what keeps the
 * bridge from answering twice.
 */
static const char *const lan_mtk_paths[] = {
	"InternetGatewayDevice.LANDevice.1.LANHostConfigManagement.",
	"InternetGatewayDevice.LANDevice.1.DHCPStaticAddress.",
	"InternetGatewayDevice.LANDevice.1.LANUSBInterfaceConfig.",
	"InternetGatewayDevice.LANDevice.1.LANEthernetInterfaceNumberOfEntries",
	"InternetGatewayDevice.LANDevice.1.LANUSBInterfaceNumberOfEntries",
	"InternetGatewayDevice.LANDevice.1.LANWLANConfigurationNumberOfEntries",
	"InternetGatewayDevice.LANDevice.1.X-AIS_2-4GHzTransmitPower",
	"InternetGatewayDevice.LANDevice.1.X-AIS_5GHzTransmitPower",
	NULL
};

static const struct dm_module lan_mtk_module = {
	.name  = "mtk-landevice",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tLanDeviceRoot,
	.paths = lan_mtk_paths,
};
DM_MODULE_REGISTER(lan_mtk_module);

/* ------------------------------------------------------------------ */
/* TR-181 (cwmp.cpe.datamodel=tr181)                                    */
/* ------------------------------------------------------------------ */

/*
 * LANHostConfigManagement is Device.DHCPv4.Server.Pool.1 and its IPInterface.1
 * is IPv4Address.1 of the LAN's Device.IP.Interface (device_ip_mtk.c numbers
 * it, network.lan.ip_int_instance).  Same getters and setters as above, so the
 * same UCI options, reloads and faults.  Left out, no TR-181 counterpart:
 * MACAddress (Ethernet.Link, not built), DHCPServerConfigurable, DHCPRelay,
 * UseAllocatedWAN, AssociatedConnection, Passthrough*, AllowedMACAddresses,
 * IPInterfaceNumberOfEntries, DHCPConditionalPoolNumberOfEntries
 * (docs/issue/tr181_mapping.tsv).
 */

/* the TR-098 IPInterface.1.Enable is a "false" placeholder; the LAN address
 * of this product is always on */
static int get_ipv4_enable181(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "true";
	return 0;
}

/* Device.IP.Interface.<n> of network.lan, numbered the way device_ip_mtk.c
 * numbers it (given and committed the first time, as the DHCPv6 pool does) */
static int get_pool_interface181(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *inst = dip_update_instance("lan");

	if (*inst)
		dmasprintf(value, "%s%s", mtk_ipif_prefix(), inst);
	else
		*value = "";
	return 0;
}

/* IPv4Address.1 hangs under the IP.Interface instance of network.lan (data
 * NULL, the LAN getters above) and under the one of a routed or PPP WAN
 * connection (data its wan_entry, the getters of wanip_mtk.c through
 * wan181_get/set: ExternalIPAddress, SubnetMask, AddressingType).  A bridged
 * connection has no address of its own. */
static int browseLanIpv4Inst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	char *sec = dip_section_of_instance(prev_instance), *idx, *idx_last = NULL;
	struct wan_entry *e;

	if (!sec)
		return 0;
	if (strcmp(sec, "lan") == 0)
		return browseIPInterfaceInst(dmctx, parent_node, NULL, prev_instance);
	e = dmcalloc(1, sizeof(*e));
	if (!e || !wan_entry_of_sec(sec, e) || e->bridge)
		return 0;
	idx = handle_update_instance(2, dmctx, &idx_last, update_instance_without_section, 1, 1);
	DM_LINK_INST_OBJ(dmctx, parent_node, (void *)e, idx);
	return 0;
}

/* TR-181 Alias (T7 S3): what an ACS set (dmmtk.h mtk_alias181_*), else the
 * CPE's own: the LAN's cpe-ipif1 as before, cpe-ipv4-<n> on a connection
 * (it had none); writes used to be dropped */
static char *ipv4_alias181_default(void *data, char *instance)
{
	char *d;

	if (!data)
		return "cpe-ipif1";
	dmasprintf(&d, "cpe-ipv4-%s", instance);
	return d;
}

static int get_ipv4_alias181(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_alias181_get(refparam, ipv4_alias181_default(data, instance));
	return 0;
}

static int set_ipv4_alias181(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return mtk_alias181_set(refparam, ipv4_alias181_default(data, instance), value, action);
}

static int get_ipv4_ipaddr181(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	if (data)
		return wan181_get((struct wan_entry *)data, "ExternalIPAddress", value);
	return get_ip_routers(refparam, ctx, data, instance, value);
}

static int set_ipv4_ipaddr181(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (data)
		return wan181_set((struct wan_entry *)data, "ExternalIPAddress", value, action);
	return set_ip_routers(refparam, ctx, data, instance, value, action);
}

static int get_ipv4_mask181(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	if (data)
		return wan181_get((struct wan_entry *)data, "SubnetMask", value);
	return get_subnet_mask(refparam, ctx, data, instance, value);
}

static int set_ipv4_mask181(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (data)
		return wan181_set((struct wan_entry *)data, "SubnetMask", value, action);
	return set_subnet_mask(refparam, ctx, data, instance, value, action);
}

static int get_ipv4_addressing181(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	/* TR-181: PPP negotiates its address, IPCP (as the default route's
	 * Origin); the product's WANPPPConnection leaf reads v4_mode as DHCP */
	if (data && ((struct wan_entry *)data)->ppp) {
		*value = "IPCP";
		return 0;
	}
	if (data)
		return wan181_get((struct wan_entry *)data, "AddressingType", value);
	return get_ipif_addressing(refparam, ctx, data, instance, value);
}

/* T7 S5b: the address is configured and used: Enabled */
static int get_ipv4_status181(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "Enabled";
	return 0;
}

/* T7 S5b: writes that used to be dropped take the current value only: the
 * address is always enabled (no switch for it), and dnsmasq has no list of
 * reserved addresses here */
MTK_SET_SAME_BOOL(ipv4_enable181, get_ipv4_enable181)

static DMLEAF tLanIpv4181Param[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_ipv4_enable181, set_same_ipv4_enable181, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_ipv4_status181, NULL, NULL, NULL},
{"Alias", &DMWRITE, DMT_STRING, get_ipv4_alias181, set_ipv4_alias181, NULL, NULL},
{"IPAddress", &DMWRITE, DMT_STRING, get_ipv4_ipaddr181, set_ipv4_ipaddr181, NULL, NULL},
{"SubnetMask", &DMWRITE, DMT_STRING, get_ipv4_mask181, set_ipv4_mask181, NULL, NULL},
{"AddressingType", &DMREAD, DMT_STRING, get_ipv4_addressing181, NULL, NULL, NULL},
{0}
};

static DMOBJ tLanIpInterface181Obj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"IPv4Address", &DMREAD, NULL, NULL, NULL, browseLanIpv4Inst, NULL, NULL, NULL, tLanIpv4181Param, NULL},
{0}
};

/* browseinstobj left NULL: device_ip_mtk.c makes the Interface instances */
static DMOBJ tLanIp181Obj[] = {
{"Interface", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tLanIpInterface181Obj, NULL, NULL},
{0}
};

/* T7 S3: standard readWrite leaves the product cannot change (dmmtk.h MTK_SET_SAME) */
MTK_SET_SAME(pool_interface181, get_pool_interface181)
MTK_SET_SAME(dhcp4_pool181_order, get_one)
MTK_SET_SAME(pool181_reserved, get_empty_string)

static DMLEAF tDhcp4Pool181Param[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_dhcp_enable, set_dhcp_enable, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_pool_interface181, set_same_pool_interface181, NULL, NULL},
{"MinAddress", &DMWRITE, DMT_STRING, get_min_address, set_min_address, NULL, NULL},
{"MaxAddress", &DMWRITE, DMT_STRING, get_max_address, set_max_address, NULL, NULL},
{"ReservedAddresses", &DMWRITE, DMT_STRING, get_empty_string, set_same_pool181_reserved, NULL, NULL},
{"SubnetMask", &DMWRITE, DMT_STRING, get_subnet_mask, set_subnet_mask, NULL, NULL},
{"DNSServers", &DMWRITE, DMT_STRING, get_dns_servers, set_dns_servers, NULL, NULL},
{"DomainName", &DMWRITE, DMT_STRING, get_domain_name, set_domain_name, NULL, NULL},
{"IPRouters", &DMWRITE, DMT_STRING, get_ip_routers, set_ip_routers, NULL, NULL},
{"LeaseTime", &DMWRITE, DMT_INT, get_lease_time, set_lease_time, NULL, NULL},
{"StaticAddressNumberOfEntries", &DMREAD, DMT_UNINT, get_zero, NULL, NULL, NULL},
{"OptionNumberOfEntries", &DMREAD, DMT_UNINT, get_zero, NULL, NULL, NULL},
{"Order", &DMWRITE, DMT_UNINT, get_one, set_same_dhcp4_pool181_order, NULL, NULL},
{0}
};

static DMOBJ tDhcp4Pool181Obj[] = {
{"StaticAddress", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{"Option", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{0}
};

/* T7 S5b: DHCPv4.Server.Enable -- dnsmasq serves DHCP on the LAN unless
 * dhcp.lan.ignore is set; the product has no switch of its own for the
 * whole server (Pool.1.Enable is dynamicdhcp), so it takes its value only.
 * Pool.1.Order 1: the one pool. */
static int get_dhcp4_server181_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_bool(mtk_uci("dhcp", "lan", "ignore")) ? "false" : "true";
	return 0;
}

MTK_SET_SAME_BOOL(dhcp4_server181_enable, get_dhcp4_server181_enable)

static DMLEAF tDhcp4Server181Param[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_dhcp4_server181_enable, set_same_dhcp4_server181_enable, NULL, NULL},
{"PoolNumberOfEntries", &DMREAD, DMT_UNINT, get_one, NULL, NULL, NULL},
{0}
};

/* one pool, instance 1 like the single LANDevice it comes from */
static DMOBJ tDhcp4Server181Obj[] = {
{"Pool", &DMREAD, NULL, NULL, NULL, browseLanDeviceInst, NULL, NULL, tDhcp4Pool181Obj, tDhcp4Pool181Param, NULL},
{0}
};

static DMOBJ tDhcp4181Obj[] = {
{"Server", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDhcp4Server181Obj, tDhcp4Server181Param, NULL},
{0}
};

/* The operator's band power steps (LANDevice.1.X-AIS_2-4GHzTransmitPower,
 * X-AIS_5GHzTransmitPower) are not in the TR-181 tree: "X-AIS_" is no
 * vendor prefix (TR-106 wants X_<id>_), and the standard
 * Device.WiFi.Radio.{i}.TransmitPower (wlan_mtk.c) reads and writes the
 * same wireless.<radio>.txpower, as a percentage instead of the NBTC
 * step codes. */

static DMOBJ tLan181Root[] = {
{"DHCPv4", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDhcp4181Obj, NULL, NULL},
{"IP", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tLanIp181Obj, NULL, NULL},
{0}
};

/* Device.IP. is device_ip_mtk.c's claim; IPv4Address joins it by the merge,
 * unclaimed, the way managementserver_core_mtk.c extends ManagementServer */
static const char *const lan181_mtk_paths[] = {
	"Device.DHCPv4.Server.",
	NULL
};

static const struct dm_module lan181_mtk_module = {
	.name  = "mtk-landevice-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tLan181Root,
	.paths = lan181_mtk_paths,
};
DM_MODULE_REGISTER(lan181_mtk_module);
