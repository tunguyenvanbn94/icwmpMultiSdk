/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.LANDevice.1.Hosts. of the MTK/Airoha product.
 *
 *	Ported from functions/tr098/lan_device, hosts_* helpers.  The host list
 *	is the product's own UCI config "lanhost" (sections of type "host",
 *	written by the LAN host tracker), in section order, numbered from 1 --
 *	exactly what hosts_lanhost_sections() piped into awk.
 *
 *	LeaseTimeRemaining still comes from /tmp/dhcp.leases, the dnsmasq lease
 *	file, because "lanhost" does not carry the expiry.
 *
 *	The LANDevice object and its single instance are browsed in lan_mtk.c,
 *	dm_registry.c merges this subtree into it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <ctype.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmmem.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define LEASE_FILE	"/tmp/dhcp.leases"

static char *host_opt(void *data, char *option)
{
	struct uci_section *s = (struct uci_section *)data;
	char *v = NULL;

	if (!s)
		return "";
	dmuci_get_value_by_section_string(s, option, &v);
	return v ? v : "";
}

/* ------------------------------------------------------------------ */
/* leaves                                                              */
/* ------------------------------------------------------------------ */

static int get_host_active(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	/* the shell answered a constant "true" here, the tracker only keeps
	 * sections for hosts it still sees */
	*value = "1";
	return 0;
}

static int get_host_ip(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = host_opt(data, "ip");
	return 0;
}

static int get_host_addresssource(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = host_opt(data, "addressSrc");
	return 0;
}

static int get_host_mac(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = host_opt(data, "mac");
	return 0;
}

/* get_lan_device_hostname(): the tracker may have quoted the name, and "*"
 * means "not known" and is passed through as is */
static int get_host_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *host = host_opt(data, "hostname");
	size_t l;

	if (!host || !*host) {
		*value = "*";
		return 0;
	}
	if (host[0] == '\'') {
		host = dmstrdup(host + 1);
		l = strlen(host);
		if (l && host[l - 1] == '\'')
			host[l - 1] = '\0';
	}
	*value = host;
	return 0;
}

static int get_host_interfacetype(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *iface = host_opt(data, "interface");

	if (strcmp(iface, "Ethernet") == 0 || strcmp(iface, "802.11") == 0)
		*value = iface;
	else
		*value = "other";
	return 0;
}

/* "LAN3" -> ...LANEthernetInterfaceConfig.3., "SSID2" -> ...WLANConfiguration.2.
 * A prefix followed by anything but digits was answered as empty. */
static int get_host_layer2(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *l2 = host_opt(data, "layer2interface");
	const char *suffix = NULL, *obj = NULL;
	const char *p;

	if (strncmp(l2, "LAN", 3) == 0) {
		suffix = l2 + 3;
		obj = "LANEthernetInterfaceConfig";
	} else if (strncmp(l2, "SSID", 4) == 0) {
		suffix = l2 + 4;
		obj = "WLANConfiguration";
	}
	if (!obj) {
		*value = l2;
		return 0;
	}
	if (!*suffix) {
		*value = "";
		return 0;
	}
	for (p = suffix; *p; p++) {
		if (!isdigit((unsigned char)*p)) {
			*value = "";
			return 0;
		}
	}
	dmasprintf(value, "%s.LANDevice.1.%s.%s.", dmroot, obj, suffix);
	return 0;
}

/* dnsmasq writes "<expiry epoch> <mac> <ip> <name> <clientid>" per line */
static long lease_expiry(const char *mac)
{
	char line[256];
	FILE *f;
	long expiry = -1;

	if (!mac || !*mac)
		return -1;
	f = fopen(LEASE_FILE, "r");
	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		char ts[32], lmac[64];

		if (sscanf(line, "%31s %63s", ts, lmac) != 2)
			continue;
		if (strcasecmp(lmac, mac) != 0)
			continue;
		if (ts[strspn(ts, "0123456789")] == '\0')
			expiry = strtol(ts, NULL, 10);
		break;
	}
	fclose(f);
	return expiry;
}

static int get_host_leasetime(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	long expiry, remain;

	if (strcmp(host_opt(data, "addressSrc"), "Static") == 0) {
		*value = "0";
		return 0;
	}
	expiry = lease_expiry(host_opt(data, "mac"));
	if (expiry < 0) {
		*value = "0";
		return 0;
	}
	remain = expiry - (long)time(NULL);
	if (remain < 0)
		remain = 0;
	dmasprintf(value, "%ld", remain);
	return 0;
}

static int get_host_empty(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "";
	return 0;
}

static int get_host_entries(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct uci_section *s;
	int n = 0;

	uci_foreach_sections("lanhost", "host", s)
		n++;
	if (n == 0) {
		/* what the tracker leaves behind when it has no section yet */
		char *total = mtk_uci("lanhost", "common", "total_hosts");

		*value = (total && *total) ? total : "0";
		return 0;
	}
	dmasprintf(value, "%d", n);
	return 0;
}

static int browseHostInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct uci_section *s;
	char *idx, *idx_last = NULL;
	int id = 0;

	uci_foreach_sections("lanhost", "host", s) {
		idx = handle_update_instance(2, dmctx, &idx_last, update_instance_without_section, 1, ++id);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)s, idx) == DM_STOP)
			break;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tHostParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Active", &DMREAD, DMT_BOOL, get_host_active, NULL, NULL, NULL},
{"IPAddress", &DMREAD, DMT_STRING, get_host_ip, NULL, NULL, NULL},
{"AddressSource", &DMREAD, DMT_STRING, get_host_addresssource, NULL, NULL, NULL},
{"LeaseTimeRemaining", &DMREAD, DMT_INT, get_host_leasetime, NULL, NULL, NULL},
{"MACAddress", &DMREAD, DMT_STRING, get_host_mac, NULL, NULL, NULL},
{"HostName", &DMREAD, DMT_STRING, get_host_name, NULL, NULL, NULL},
{"Layer2Interface", &DMREAD, DMT_STRING, get_host_layer2, NULL, NULL, NULL},
{"InterfaceType", &DMREAD, DMT_STRING, get_host_interfacetype, NULL, NULL, NULL},
{"VendorClassID", &DMREAD, DMT_STRING, get_host_empty, NULL, NULL, NULL},
{"UserClassID", &DMREAD, DMT_STRING, get_host_empty, NULL, NULL, NULL},
{"ClientID", &DMREAD, DMT_STRING, get_host_empty, NULL, NULL, NULL},
{0}
};

static DMOBJ tHostsObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Host", &DMREAD, NULL, NULL, NULL, browseHostInst, NULL, NULL, NULL, tHostParam, NULL},
{0}
};

static DMLEAF tHostsParam[] = {
{"HostNumberOfEntries", &DMREAD, DMT_UNINT, get_host_entries, NULL, NULL, NULL},
{0}
};

static DMOBJ tLanDeviceHostsObj[] = {
{"Hosts", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tHostsObj, tHostsParam, NULL},
{0}
};

/* browseinstobj left NULL on purpose: lan_mtk.c owns the LANDevice instance */
static DMOBJ tLanDeviceHostsRoot[] = {
{"LANDevice", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tLanDeviceHostsObj, NULL, NULL},
{0}
};

static const char *const lanhosts_mtk_paths[] = {
	"InternetGatewayDevice.LANDevice.1.Hosts.",
	NULL
};

static const struct dm_module lanhosts_mtk_module = {
	.name  = "mtk-lanhosts",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tLanDeviceHostsRoot,
	.paths = lanhosts_mtk_paths,
};
DM_MODULE_REGISTER(lanhosts_mtk_module);
