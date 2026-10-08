/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.LANDevice.1.WLANConfiguration.{i}. of the MTK/Airoha
 *	product -- radio, identity, channel, power, standards, counters and WPS.
 *
 *	Ported from functions/tr098/lan_device.  The instance map is fixed by the
 *	product and must not be reordered: an ACS has provisioned against these
 *	numbers, so instance 9 stays rai4 even though it looks out of sequence.
 *
 *	    1..4   ra0  ra1  ra2  ra3     2.4 GHz fronthaul
 *	    5..8   rai0 rai1 rai2 rai3    5 GHz fronthaul
 *	    9      rai4                   5 GHz backhaul
 *	    10     ra4                    2.4 GHz backhaul
 *	    11,12  ra5  rai5              MLO fronthaul pair
 *
 *	Two pairs are kept in sync by the product, and the setters here do the
 *	same: the MLO fronthaul pair (ra5/rai5 plus section apmld1) and the
 *	backhaul pair (ra4/rai4 plus apmld2).  When mesh is on, mapd's own node
 *	(mapd.1 .. mapd.12) is updated with the same value, because mapd rewrites
 *	wireless from its config on the next reload and would otherwise win.
 *
 *	Security leaves (BeaconType, the authentication and encryption modes,
 *	KeyPassphrase, PreSharedKey, WEP) are wlansec_mtk.c, merged into this
 *	same object.
 *
 *	WLANConfiguration. is read only, a deliberate difference: the shell
 *	registered it writable with lan_device_add_wlan_iface /
 *	lan_device_delete_wlan_iface, and neither works on this product.  The
 *	add takes the next instance from "wireless.@wifi-iface[N].instance",
 *	which never matches the named sections (wireless.ra0 ...), so it answers
 *	1, 2 ... -- instances the fixed map above already has -- and leaves an
 *	anonymous wifi-iface on "wl0", a radio this board does not have.  The
 *	delete runs "uci delete wireless.<iface>" on one of the twelve product
 *	interfaces.  AddObject / DeleteObject answer 9005 instead.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmmem.h"
#include "dmubus.h"
#include "dmjson.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "wlan_mtk.h"

#define RADIO_DEVICE_2G		"MT7993_1_1"
#define RADIO_DEVICE_5G		"MT7993_1_2"
#define MLO_FRONTHAUL_SECTION	"apmld1"
#define MLO_BACKHAUL_SECTION	"apmld2"

/* instance -> interface, and the mapd node that mirrors it */
static const struct wlan_iface wlan_ifaces[] = {
	{  1, "ra0",  "mapd.7"  },
	{  2, "ra1",  "mapd.10" },
	{  3, "ra2",  "mapd.11" },
	{  4, "ra3",  "mapd.12" },
	{  5, "rai0", "mapd.1"  },
	{  6, "rai1", "mapd.4"  },
	{  7, "rai2", "mapd.5"  },
	{  8, "rai3", "mapd.6"  },
	{  9, "rai4", "mapd.2"  },
	{ 10, "ra4",  "mapd.8"  },
	{ 11, "ra5",  "mapd.9"  },
	{ 12, "rai5", "mapd.3"  },
};
#define WLAN_IFACE_COUNT	((int)(sizeof(wlan_ifaces) / sizeof(wlan_ifaces[0])))

const struct wlan_iface *wlan_iface_of(void *data)
{
	return data ? (const struct wlan_iface *)data : &wlan_ifaces[0];
}

static char *iface_name(void *data)
{
	return (char *)wlan_iface_of(data)->name;
}

/* wireless.<iface>.<option> */
char *wlan_opt(void *data, char *option)
{
	return mtk_uci("wireless", iface_name(data), option);
}

/* 2.4 GHz interfaces are ra*, 5 GHz are rai* -- rai must be tested first */
static const char *radio_of_name(const char *iface)
{
	if (strncmp(iface, "rai", 3) == 0)
		return RADIO_DEVICE_5G;
	if (strncmp(iface, "ra", 2) == 0)
		return RADIO_DEVICE_2G;
	return "";
}

static const char *radio_of(void *data)
{
	return radio_of_name(iface_name(data));
}

/* what the shell read as wireless.<iface>.device, falling back to the band */
static char *radio_section(void *data)
{
	char *dev = wlan_opt(data, "device");

	return (dev && *dev) ? dev : (char *)radio_of(data);
}

static int radio_index(void *data)
{
	return strncmp(iface_name(data), "rai", 3) == 0 ? 1 : 0;
}

static int is_mlo_fronthaul(const char *iface)
{
	return strcmp(iface, "ra5") == 0 || strcmp(iface, "rai5") == 0;
}

static int is_mlo_backhaul(const char *iface)
{
	return strcmp(iface, "ra4") == 0 || strcmp(iface, "rai4") == 0;
}

static const char *mlo_peer(const char *iface)
{
	if (strcmp(iface, "ra5") == 0)
		return "rai5";
	if (strcmp(iface, "rai5") == 0)
		return "ra5";
	if (strcmp(iface, "ra4") == 0)
		return "rai4";
	if (strcmp(iface, "rai4") == 0)
		return "ra4";
	return NULL;
}

/* is_mesh_enabled(): both radios carry a non zero map_mode */
static int mesh_enabled(void)
{
	char *m2 = mtk_uci("wireless", RADIO_DEVICE_2G, "map_mode");
	char *m5 = mtk_uci("wireless", RADIO_DEVICE_5G, "map_mode");

	if (!*m2 || !*m5)
		return 0;
	return strcmp(m2, "0") != 0 && strcmp(m5, "0") != 0;
}

int wlan_mesh_enabled(void)
{
	return mesh_enabled();
}

static const char *mapd_node_of_name(const char *iface)
{
	int i;

	for (i = 0; i < WLAN_IFACE_COUNT; i++) {
		if (strcmp(wlan_ifaces[i].name, iface) == 0)
			return wlan_ifaces[i].mapd_node;
	}
	return NULL;
}

/* mapd_update_by_wireless_iface(): "mapd.<n>" is package.section in uci terms */
void wlan_mapd_set(const char *iface, char *option, char *value)
{
	const char *node = mapd_node_of_name(iface);
	char pkg[16], sec[16];
	const char *dot;

	if (!node)
		return;
	dot = strchr(node, '.');
	if (!dot)
		return;
	snprintf(pkg, sizeof(pkg), "%.*s", (int)(dot - node), node);
	snprintf(sec, sizeof(sec), "%s", dot + 1);
	dmuci_set_value(pkg, sec, option, value);
}

/* wireless_commit_reload() / mapd_wireless_commit_reload(): the engine commits
 * the changed packages at the end of the RPC, only the reload is queued */
void wlan_reload(void)
{
	mtk_apply_service("/sbin/wifi reload &");
}

/* One option on the interface, its MLO peer and the MLO section, plus mapd
 * when mesh is running -- the mlo_sync_ and backhaul_sync_ helpers of the
 * shell. */
static void wlan_sync_option(void *data, char *option, char *value, const char *mapd_option)
{
	const char *iface = iface_name(data);
	const char *peer = mlo_peer(iface);
	const char *section = NULL;

	if (is_mlo_fronthaul(iface))
		section = MLO_FRONTHAUL_SECTION;
	else if (is_mlo_backhaul(iface))
		section = MLO_BACKHAUL_SECTION;

	dmuci_set_value("wireless", (char *)iface, option, value);
	if (section && peer) {
		dmuci_set_value("wireless", (char *)peer, option, value);
		dmuci_set_value("wireless", (char *)section, option, value);
	}
	if (!mapd_option)
		return;
	if (section && peer) {
		/* the pair is one SSID for the client, so mapd sees both */
		wlan_mapd_set(iface, (char *)mapd_option, value);
		wlan_mapd_set(peer, (char *)mapd_option, value);
	} else {
		wlan_mapd_set(iface, (char *)mapd_option, value);
	}
}

/* ------------------------------------------------------------------ */
/* identity and state                                                   */
/* ------------------------------------------------------------------ */

static int get_wlan_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(wlan_opt(data, "disabled"), "1") == 0 ? "false" : "true";
	return 0;
}

/*
 * Enable and RadioEnabled are the same UCI flag in this product: the shell had
 * two setters that differ only in whether they also mirror the peer's state to
 * mapd.  Both are kept, so an ACS writing either gets what it used to get.
 */
static int set_wlan_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = mtk_parse_bool(value);
	const char *iface = iface_name(data);
	const char *peer = mlo_peer(iface);

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	wlan_sync_option(data, "disabled", b ? "0" : "1", NULL);
	if (mesh_enabled()) {
		wlan_mapd_set(iface, "disabled", b ? "0" : "1");
		if (peer && (is_mlo_fronthaul(iface) || is_mlo_backhaul(iface))) {
			char *pd = mtk_uci("wireless", (char *)peer, "disabled");

			if (*pd)
				wlan_mapd_set(peer, "disabled", pd);
		}
	}
	wlan_reload();
	return 0;
}

static int get_radio_enabled(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(wlan_opt(data, "disabled"), "0") == 0 ? "true" : "false";
	return 0;
}

static int set_radio_enabled(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = mtk_parse_bool(value);

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	wlan_sync_option(data, "disabled", b ? "0" : "1", NULL);
	if (mesh_enabled())
		wlan_mapd_set(iface_name(data), "disabled", b ? "0" : "1");
	wlan_reload();
	return 0;
}

static int get_wlan_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *d = wlan_opt(data, "disabled");

	if (strcmp(d, "1") == 0)
		*value = "Disabled";
	else if (strcmp(d, "0") == 0)
		*value = "Up";
	else
		*value = "Error";
	return 0;
}

static int get_bssid(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char path[64];

	snprintf(path, sizeof(path), "/sys/class/net/%s/address", iface_name(data));
	*value = mtk_file_line(path);
	return 0;
}

static int get_ssid(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = wlan_opt(data, "ssid");
	return 0;
}

static int set_ssid(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	size_t len = value ? strlen(value) : 0;

	if (len == 0 || len > 32)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	wlan_sync_option(data, "ssid", value, "ssid");
	wlan_reload();
	return 0;
}

static int get_ssid_advertisement(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(wlan_opt(data, "hidden"), "1") == 0 ? "false" : "true";
	return 0;
}

static int set_ssid_advertisement(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = mtk_parse_bool(value);

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	/* mapd spells it Y/N, wireless spells it 1/0, and the sense is inverted */
	dmuci_set_value("wireless", iface_name(data), "hidden", b ? "0" : "1");
	wlan_mapd_set(iface_name(data), "hidden", b ? "N" : "Y");
	wlan_reload();
	return 0;
}

/* ------------------------------------------------------------------ */
/* channel and power                                                    */
/* ------------------------------------------------------------------ */

static char *ubus_hni_radio_field(int radio, char *method, char *field)
{
	json_object *res = NULL;
	char idx[8];
	char *v;

	snprintf(idx, sizeof(idx), "%d", radio);
	dmubus_call("hni", method, UBUS_ARGS{{"radio", idx, Integer}}, 1, &res);
	if (!res)
		return "";
	v = dmjson_get_value(res, 1, field);
	return v ? v : "";
}

static int get_channels_in_use(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v;

	if (!*radio_section(data)) {
		*value = "0";
		return 0;
	}
	v = ubus_hni_radio_field(radio_index(data), "getCurrentChannel", "channel");
	*value = (v && *v) ? v : "0";
	return 0;
}

static int get_possible_channels(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = ubus_hni_radio_field(radio_index(data), "getChannelList", "channel_list");
	return 0;
}

static int get_channel(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *c = mtk_uci("wireless", radio_section(data), "channel");

	*value = (c && *c) ? c : "0";
	return 0;
}

/* 20/40/80/160 out of EHT160, HE80, HT40, NOHT ... */
static int bandwidth_of_htmode(const char *htmode)
{
	size_t l = htmode ? strlen(htmode) : 0;

	if (l >= 3 && strcmp(htmode + l - 3, "160") == 0)
		return 160;
	if (l >= 2 && strcmp(htmode + l - 2, "80") == 0)
		return 80;
	if (l >= 2 && strcmp(htmode + l - 2, "40") == 0)
		return 40;
	if (l >= 2 && strcmp(htmode + l - 2, "20") == 0)
		return 20;
	if (strcmp(htmode, "NOHT") == 0)
		return 20;
	return 0;
}

static int valid_5g_channel(int ch)
{
	static const int list[] = { 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108,
				    112, 116, 120, 124, 128, 132, 136, 140, 144,
				    149, 153, 157, 161, 165 };
	int i;

	for (i = 0; i < (int)(sizeof(list) / sizeof(list[0])); i++) {
		if (list[i] == ch)
			return 1;
	}
	return 0;
}

static int set_channel(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *device = wlan_opt(data, "device");
	const char *p;
	int ch, bw;

	if (!value || !*value)
		return FAULT_9007;
	for (p = value; *p; p++) {
		if (*p < '0' || *p > '9')
			return FAULT_9007;
	}
	ch = atoi(value);
	if (!device || !*device)
		return FAULT_9005;
	if (strcmp(device, RADIO_DEVICE_2G) == 0) {
		if (ch < 1 || ch > 13)
			return FAULT_9007;
	} else if (strcmp(device, RADIO_DEVICE_5G) == 0) {
		if (!valid_5g_channel(ch))
			return FAULT_9007;
		/* a 160 MHz carrier only fits on the two wide blocks */
		bw = bandwidth_of_htmode(mtk_uci("wireless", device, "htmode"));
		if (bw == 160 && !((ch >= 36 && ch <= 64) || (ch >= 100 && ch <= 128)))
			return FAULT_9007;
		if (bw != 0 && bw != 20 && bw != 40 && bw != 80 && bw != 160)
			return FAULT_9007;
	} else {
		return FAULT_9007;
	}
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value("wireless", device, "channel", value);
	wlan_reload();
	return 0;
}

static int get_auto_channel(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci("wireless", radio_section(data), "channel"), "0") == 0 ? "true" : "false";
	return 0;
}

static int set_auto_channel(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *device = wlan_opt(data, "device");
	int b = mtk_parse_bool(value);
	char *channel;

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (!device || !*device)
		return FAULT_9005;
	if (b) {
		dmuci_set_value("wireless", device, "channel", "0");
	} else {
		/* leaving auto needs a concrete channel, the first of the band */
		channel = mtk_uci("wireless", device, "channel");
		if (!*channel || strcmp(channel, "0") == 0) {
			if (strcmp(device, RADIO_DEVICE_2G) == 0)
				dmuci_set_value("wireless", device, "channel", "1");
			else if (strcmp(device, RADIO_DEVICE_5G) == 0)
				dmuci_set_value("wireless", device, "channel", "36");
			else
				return FAULT_9007;
		}
	}
	wlan_reload();
	return 0;
}

static int get_transmit_power(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v;

	if (!*radio_section(data)) {
		*value = "100";
		return 0;
	}
	v = mtk_uci("wireless", radio_section(data), "txpower");
	*value = (v && *v) ? v : "100";
	return 0;
}

static int set_transmit_power(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *device = wlan_opt(data, "device");
	const char *p;
	int n;

	if (!value || !*value)
		return FAULT_9007;
	for (p = value; *p; p++) {
		if (*p < '0' || *p > '9')
			return FAULT_9007;
	}
	n = atoi(value);
	if (n < 1 || n > 100)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (!device || !*device)
		return FAULT_9005;
	dmuci_set_value("wireless", device, "txpower", value);
	wlan_reload();
	return 0;
}

static int get_power_supported(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "10,25,50,80,100";
	return 0;
}

/* ------------------------------------------------------------------ */
/* standards and rates                                                  */
/* ------------------------------------------------------------------ */

static char *htmode_of(void *data)
{
	return mtk_uci("wireless", radio_of(data), "htmode");
}

/* Standard and SupportedStandards shared one getter in the shell: the coarse
 * 802.11 letter, from the htmode the radio is running. */
static int get_standard(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *ht = htmode_of(data);
	int five = strncmp(iface_name(data), "rai", 3) == 0;

	if (strncmp(ht, "EHT", 3) == 0 || strncmp(ht, "HE", 2) == 0 ||
	    strncmp(ht, "HT", 2) == 0 || (five && strncmp(ht, "VHT", 3) == 0))
		*value = "n";
	else if (strcmp(ht, "NOHT") == 0)
		*value = five ? "a" : "g";
	else
		*value = "";
	return 0;
}

static int get_transmit_rates(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strncmp(iface_name(data), "rai", 3) == 0 ? "6,12,24" : "1,2,5.5,11";
	return 0;
}

static int get_max_bitrate(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "Auto";
	return 0;
}

static int get_regulatory_domain(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *country = mtk_uci("wireless", radio_of(data), "country");

	/* the shell appended a space, and the ACS stores what it is given */
	if (country && *country)
		dmasprintf(value, "%s ", country);
	else
		*value = "";
	return 0;
}

static int set_regulatory_domain(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char country[3];

	if (!value || strlen(value) < 2)
		return FAULT_9007;
	snprintf(country, sizeof(country), "%.2s", value);
	if (action == VALUECHECK)
		return 0;
	if (!*radio_of(data))
		return 0;
	dmuci_set_value("wireless", (char *)radio_of(data), "country", country);
	wlan_reload();
	return 0;
}

/* X_AIS_WlanStandard: the full letter list, and writing it picks an htmode */
static int get_wlan_standard(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	const char *iface = iface_name(data);
	char *ht = htmode_of(data);

	*value = "";
	if (strcmp(iface, "ra0") == 0) {
		if (strncmp(ht, "EHT", 3) == 0)
			*value = "b,g,n,ax,be";
		else if (strncmp(ht, "HE", 2) == 0)
			*value = "b,g,n,ax";
		else if (strncmp(ht, "HT", 2) == 0)
			*value = "b,g,n";
		else if (strcmp(ht, "NOHT") == 0)
			*value = "b,g";
	} else if (strcmp(iface, "rai0") == 0) {
		if (strncmp(ht, "EHT", 3) == 0)
			*value = "a,n,ac,ax,be";
		else if (strncmp(ht, "HE", 2) == 0)
			*value = "a,n,ac,ax";
		else if (strncmp(ht, "VHT", 3) == 0)
			*value = "a,n,ac";
		else if (strncmp(ht, "HT", 2) == 0)
			*value = "a,n";
		else if (strcmp(ht, "NOHT") == 0)
			*value = "a";
	}
	return 0;
}

static int set_wlan_standard(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *iface = iface_name(data);
	const char *radio, *htmode;
	int two_g;

	/* only the first interface of each band carries the radio setting */
	if (strcmp(iface, "ra0") == 0)
		radio = RADIO_DEVICE_2G;
	else if (strcmp(iface, "rai0") == 0)
		radio = RADIO_DEVICE_5G;
	else
		return 0;
	if (!value)
		return FAULT_9007;
	two_g = strcmp(radio, RADIO_DEVICE_2G) == 0;
	if (strstr(value, "be"))
		htmode = two_g ? "EHT40" : "EHT160";
	else if (strstr(value, "ax"))
		htmode = two_g ? "HE40" : "HE160";
	else if (!two_g && strstr(value, "ac"))
		htmode = "VHT160";
	else if (strstr(value, "n"))
		htmode = "HT40";
	else if (two_g && strstr(value, "b,g"))
		htmode = "NOHT";
	else if (!two_g && strstr(value, "a"))
		htmode = "NOHT";
	else
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value("wireless", (char *)radio, "htmode", (char *)htmode);
	wlan_reload();
	return 0;
}

/* ------------------------------------------------------------------ */
/* MU-OFDMA, AP module                                                  */
/* ------------------------------------------------------------------ */

static int get_mru_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	int ul = strcmp(wlan_opt(data, "muofdmaul_enable"), "1") == 0;
	int dl = strcmp(wlan_opt(data, "muofdmadl_enable"), "1") == 0;

	*value = (ul && dl) ? "1" : "0";
	return 0;
}

/*
 * MU-OFDMA is a radio property, so the product writes it on every fronthaul
 * interface of that band and on the radio section too.  The backhaul pair is
 * left out on purpose, like update_mru_enable_for_radio().
 */
static int set_mru_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	static const char *const band_2g[] = { "ra0", "ra1", "ra2", "ra3", "ra5", NULL };
	static const char *const band_5g[] = { "rai0", "rai1", "rai2", "rai3", "rai5", NULL };
	const char *iface = iface_name(data);
	const char *const *list = NULL;
	int b = mtk_parse_bool(value);
	int i;

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	for (i = 0; band_2g[i]; i++) {
		if (strcmp(band_2g[i], iface) == 0)
			list = band_2g;
	}
	for (i = 0; band_5g[i]; i++) {
		if (strcmp(band_5g[i], iface) == 0)
			list = band_5g;
	}
	if (list) {
		for (i = 0; list[i]; i++) {
			dmuci_set_value("wireless", (char *)list[i], "muofdmaul_enable", b ? "1" : "0");
			dmuci_set_value("wireless", (char *)list[i], "muofdmadl_enable", b ? "1" : "0");
		}
	} else {
		dmuci_set_value("wireless", (char *)iface, "muofdmaul_enable", b ? "1" : "0");
		dmuci_set_value("wireless", (char *)iface, "muofdmadl_enable", b ? "1" : "0");
	}
	if (!is_mlo_backhaul(iface)) {
		dmuci_set_value("wireless", (char *)radio_of(data), "muofdmaul_enable", b ? "1" : "0");
		dmuci_set_value("wireless", (char *)radio_of(data), "muofdmadl_enable", b ? "1" : "0");
	}
	wlan_reload();
	return 0;
}

static int get_apmodule_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(wlan_opt(data, "disabled"), "1") == 0 ? "false" : "true";
	return 0;
}

static int set_apmodule_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = mtk_parse_bool(value);

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	/* common_set_bool on the interface only, no MLO or mapd mirroring */
	dmuci_set_value("wireless", iface_name(data), "disabled", b ? "0" : "1");
	wlan_reload();
	return 0;
}

/* ------------------------------------------------------------------ */
/* counters                                                             */
/* ------------------------------------------------------------------ */

/* br-lan row of /proc/net/dev, the same four fields the shell picked */
static int br_lan_stat(char **value, int field)
{
	char line[512];
	FILE *f;

	*value = "0";
	f = fopen("/proc/net/dev", "r");
	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f)) {
		char *p = strstr(line, "br-lan:");
		unsigned long long v[16];
		int n;

		if (!p)
			continue;
		p += 7;
		n = sscanf(p, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
			   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]);
		/* rx_bytes rx_packets ... then tx_bytes tx_packets at 9 and 10 */
		if (n >= 10)
			dmasprintf(value, "%llu", v[field]);
		break;
	}
	fclose(f);
	return 0;
}

static int get_total_bytes_received(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return br_lan_stat(value, 0);
}

static int get_total_packets_received(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return br_lan_stat(value, 1);
}

static int get_total_bytes_sent(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return br_lan_stat(value, 8);
}

static int get_total_packets_sent(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return br_lan_stat(value, 9);
}

static int get_total_associations(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	dmasprintf(value, "%d", wlan_assoc_count(wlan_iface_of(data)));
	return 0;
}

static int get_zero(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "0";
	return 0;
}

static int get_false(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "false";
	return 0;
}

/* writable in the product's tree, no setter behind it: accepted and dropped,
 * exactly what a permission of 1 with an empty set command did */
static int set_accept_and_drop(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return 0;
}

/* ------------------------------------------------------------------ */
/* WPS -- constants in this product                                     */
/* ------------------------------------------------------------------ */

static int get_wps_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "false";
	return 0;
}

static int get_wps_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "Configured";
	return 0;
}

static int get_wps_methods(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "PushButton,Label,Display";
	return 0;
}

static int get_wps_password(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "12345670";
	return 0;
}

static int browseWlanInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	char *idx, *idx_last = NULL;
	int i;

	for (i = 0; i < WLAN_IFACE_COUNT; i++) {
		idx = handle_update_instance(2, dmctx, &idx_last, update_instance_without_section,
					     1, wlan_ifaces[i].index);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)&wlan_ifaces[i], idx) == DM_STOP)
			break;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tWlanStatsParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"ErrorsReceived", &DMREAD, DMT_UNINT, get_zero, NULL, NULL, NULL},
{"ErrorsSent", &DMREAD, DMT_UNINT, get_zero, NULL, NULL, NULL},
{0}
};

static DMLEAF tWpsParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_wps_enable, set_accept_and_drop, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_wps_status, NULL, NULL, NULL},
{"ConfigMethodsSupported", &DMREAD, DMT_STRING, get_wps_methods, NULL, NULL, NULL},
{"ConfigMethodsEnabled", &DMWRITE, DMT_STRING, get_wps_methods, set_accept_and_drop, NULL, NULL},
{"DevicePassword", &DMWRITE, DMT_STRING, get_wps_password, set_accept_and_drop, NULL, NULL},
{0}
};

static DMLEAF tWlanParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_wlan_enable, set_wlan_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_wlan_status, NULL, NULL, NULL},
{"BSSID", &DMREAD, DMT_STRING, get_bssid, NULL, NULL, NULL},
{"SSID", &DMWRITE, DMT_STRING, get_ssid, set_ssid, NULL, NULL},
{"SSIDAdvertisementEnabled", &DMWRITE, DMT_BOOL, get_ssid_advertisement, set_ssid_advertisement, NULL, NULL},
{"RadioEnabled", &DMWRITE, DMT_BOOL, get_radio_enabled, set_radio_enabled, NULL, NULL},
{"Channel", &DMWRITE, DMT_UNINT, get_channel, set_channel, NULL, NULL},
{"AutoChannelEnable", &DMWRITE, DMT_BOOL, get_auto_channel, set_auto_channel, NULL, NULL},
{"ChannelsInUse", &DMREAD, DMT_STRING, get_channels_in_use, NULL, NULL, NULL},
{"PossibleChannels", &DMREAD, DMT_STRING, get_possible_channels, NULL, NULL, NULL},
{"TransmitPower", &DMWRITE, DMT_UNINT, get_transmit_power, set_transmit_power, NULL, NULL},
{"TransmitPowerSupported", &DMREAD, DMT_STRING, get_power_supported, NULL, NULL, NULL},
{"MaxBitRate", &DMREAD, DMT_STRING, get_max_bitrate, NULL, NULL, NULL},
{"BasicDataTransmitRates", &DMREAD, DMT_STRING, get_transmit_rates, NULL, NULL, NULL},
{"OperationalDataTransmitRates", &DMREAD, DMT_STRING, get_transmit_rates, NULL, NULL, NULL},
{"Standard", &DMREAD, DMT_STRING, get_standard, NULL, NULL, NULL},
{"SupportedStandards", &DMREAD, DMT_STRING, get_standard, NULL, NULL, NULL},
{"RegulatoryDomain", &DMWRITE, DMT_STRING, get_regulatory_domain, set_regulatory_domain, NULL, NULL},
{"MruEnable", &DMWRITE, DMT_BOOL, get_mru_enable, set_mru_enable, NULL, NULL},
{"TotalAssociations", &DMREAD, DMT_UNINT, get_total_associations, NULL, NULL, NULL},
{"TotalBytesReceived", &DMREAD, DMT_UNINT, get_total_bytes_received, NULL, NULL, NULL},
{"TotalBytesSent", &DMREAD, DMT_UNINT, get_total_bytes_sent, NULL, NULL, NULL},
{"TotalPacketsReceived", &DMREAD, DMT_UNINT, get_total_packets_received, NULL, NULL, NULL},
{"TotalPacketsSent", &DMREAD, DMT_UNINT, get_total_packets_sent, NULL, NULL, NULL},
{"BeaconAdvertisementEnabled", &DMWRITE, DMT_BOOL, get_false, set_accept_and_drop, NULL, NULL},
{"MACAddressControlEnabled", &DMWRITE, DMT_BOOL, get_false, set_accept_and_drop, NULL, NULL},
{"UAPSDEnable", &DMWRITE, DMT_BOOL, get_false, set_accept_and_drop, NULL, NULL},
{"WMMEnable", &DMWRITE, DMT_BOOL, get_false, set_accept_and_drop, NULL, NULL},
{"X_AIS_APModuleEnable", &DMWRITE, DMT_BOOL, get_apmodule_enable, set_apmodule_enable, NULL, NULL},
{"X_AIS_WlanStandard", &DMWRITE, DMT_STRING, get_wlan_standard, set_wlan_standard, NULL, NULL},
{0}
};

static DMOBJ tWlanObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tWlanStatsParam, NULL},
{"WPS", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tWpsParam, NULL},
{0}
};

static DMOBJ tLanDeviceWlanObj[] = {
{"WLANConfiguration", &DMREAD, NULL, NULL, NULL, browseWlanInst, NULL, NULL, tWlanObj, tWlanParam, NULL},
{0}
};

/* browseinstobj left NULL on purpose: lan_mtk.c owns the LANDevice instance */
static DMOBJ tLanDeviceWlanRoot[] = {
{"LANDevice", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tLanDeviceWlanObj, NULL, NULL},
{0}
};

/*
 * The whole object is C now that wlansec_mtk.c carries the security leaves,
 * so one branch claim replaces the thirty leaf claims this module needed
 * while half of it was still answered by the shell bridge.  The two sibling
 * modules declare no paths: a second claim on the same branch is exactly what
 * check_claims() reports as a conflict.
 */
static const char *const wlan_mtk_paths[] = {
	"InternetGatewayDevice.LANDevice.1.WLANConfiguration.",
	NULL
};

static const struct dm_module wlan_mtk_module = {
	.name  = "mtk-wlan",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tLanDeviceWlanRoot,
	.paths = wlan_mtk_paths,
};
DM_MODULE_REGISTER(wlan_mtk_module);

/* ------------------------------------------------------------------ */
/* TR-181 (cwmp.cpe.datamodel=tr181)                                    */
/* ------------------------------------------------------------------ */

/*
 * WLANConfiguration.{i} splits into Device.WiFi.SSID.{i} and
 * Device.WiFi.AccessPoint.{i}, the same twelve numbers of the fixed map
 * above, and Device.WiFi.Radio.1 (2.4 GHz) / .2 (5 GHz).  The radio-wide
 * leaves of WLANConfiguration already read the radio section through the
 * interface they are given, so a Radio instance is handed the first interface
 * of its band (ra0, rai0) and the same getters and setters serve it.
 *
 * Spelled the TR-181 way: SSID.Status (Disabled -> Down), Radio.
 * OperatingStandards (the letter list X_AIS_WlanStandard reads for the
 * band's first interface, written the same way) and SupportedStandards.
 * RadioEnabled is the interface's own "disabled" flag on this product, so it
 * is AccessPoint.{i}.Enable, not Radio.Enable.  No TR-181 counterpart:
 * MaxBitRate ("Auto"), BeaconAdvertisementEnabled, and MruEnable (a product
 * name without a vendor prefix, which a standard TR-181 object cannot carry).
 * Security is wlansec_mtk.c, AssociatedDevice wlanassoc_mtk.c.
 */

static const struct wlan_iface *const radio181_ifaces[] = { &wlan_ifaces[0], &wlan_ifaces[4] };	/* ra0, rai0 */
#define RADIO181_COUNT	2

static int radio181_number(void *data)
{
	return strncmp(iface_name(data), "rai", 3) == 0 ? 2 : 1;
}

static int get_radio181_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci("wireless", radio_section(data), "disabled"), "1") == 0 ? "false" : "true";
	return 0;
}

static int get_radio181_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci("wireless", radio_section(data), "disabled"), "1") == 0 ? "Down" : "Up";
	return 0;
}

static int get_radio181_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = radio_section(data);
	return 0;
}

static int get_radio181_band(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = radio181_number(data) == 2 ? "5GHz" : "2.4GHz";
	return 0;
}

static int get_radio181_supported(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = radio181_number(data) == 2 ? "a,n,ac,ax,be" : "b,g,n,ax,be";
	return 0;
}

static int get_ssid181_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = NULL;

	get_wlan_status(refparam, ctx, data, instance, &v);
	*value = (v && strcmp(v, "Disabled") == 0) ? "Down" : (v ? v : "Error");
	return 0;
}

static int get_ssid181_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = iface_name(data);
	return 0;
}

static int get_ssid181_lowerlayers(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	dmasprintf(value, "Device.WiFi.Radio.%d", radio181_number(data));
	return 0;
}

static int get_ap181_ssidref(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	dmasprintf(value, "Device.WiFi.SSID.%d", wlan_iface_of(data)->index);
	return 0;
}

static int get_wifi181_radio_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	dmasprintf(value, "%d", RADIO181_COUNT);
	return 0;
}

static int get_wifi181_iface_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	dmasprintf(value, "%d", WLAN_IFACE_COUNT);
	return 0;
}

static int browseRadio181Inst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	char *idx, *idx_last = NULL;
	int i;

	for (i = 0; i < RADIO181_COUNT; i++) {
		idx = handle_update_instance(1, dmctx, &idx_last, update_instance_without_section, 1, i + 1);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)radio181_ifaces[i], idx) == DM_STOP)
			break;
	}
	return 0;
}

/* SSID.{i} and AccessPoint.{i}: the fixed map, instance level 1 */
static int browseWlan181Inst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	char *idx, *idx_last = NULL;
	int i;

	for (i = 0; i < WLAN_IFACE_COUNT; i++) {
		idx = handle_update_instance(1, dmctx, &idx_last, update_instance_without_section,
					     1, wlan_ifaces[i].index);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)&wlan_ifaces[i], idx) == DM_STOP)
			break;
	}
	return 0;
}

static DMLEAF tRadio181Param[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMREAD, DMT_BOOL, get_radio181_enable, NULL, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_radio181_status, NULL, NULL, NULL},
{"Name", &DMREAD, DMT_STRING, get_radio181_name, NULL, NULL, NULL},
{"OperatingFrequencyBand", &DMREAD, DMT_STRING, get_radio181_band, NULL, NULL, NULL},
{"SupportedStandards", &DMREAD, DMT_STRING, get_radio181_supported, NULL, NULL, NULL},
{"OperatingStandards", &DMWRITE, DMT_STRING, get_wlan_standard, set_wlan_standard, NULL, NULL},
{"PossibleChannels", &DMREAD, DMT_STRING, get_possible_channels, NULL, NULL, NULL},
{"ChannelsInUse", &DMREAD, DMT_STRING, get_channels_in_use, NULL, NULL, NULL},
{"Channel", &DMWRITE, DMT_UNINT, get_channel, set_channel, NULL, NULL},
{"AutoChannelEnable", &DMWRITE, DMT_BOOL, get_auto_channel, set_auto_channel, NULL, NULL},
{"TransmitPowerSupported", &DMREAD, DMT_STRING, get_power_supported, NULL, NULL, NULL},
{"TransmitPower", &DMWRITE, DMT_UNINT, get_transmit_power, set_transmit_power, NULL, NULL},
{"RegulatoryDomain", &DMWRITE, DMT_STRING, get_regulatory_domain, set_regulatory_domain, NULL, NULL},
{"BasicDataTransmitRates", &DMREAD, DMT_STRING, get_transmit_rates, NULL, NULL, NULL},
{"OperationalDataTransmitRates", &DMREAD, DMT_STRING, get_transmit_rates, NULL, NULL, NULL},
{0}
};

static DMLEAF tSsid181StatsParam[] = {
{"BytesSent", &DMREAD, DMT_UNINT, get_total_bytes_sent, NULL, NULL, NULL},
{"BytesReceived", &DMREAD, DMT_UNINT, get_total_bytes_received, NULL, NULL, NULL},
{"PacketsSent", &DMREAD, DMT_UNINT, get_total_packets_sent, NULL, NULL, NULL},
{"PacketsReceived", &DMREAD, DMT_UNINT, get_total_packets_received, NULL, NULL, NULL},
{"ErrorsSent", &DMREAD, DMT_UNINT, get_zero, NULL, NULL, NULL},
{"ErrorsReceived", &DMREAD, DMT_UNINT, get_zero, NULL, NULL, NULL},
{0}
};

static DMOBJ tSsid181Obj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tSsid181StatsParam, NULL},
{0}
};

static DMLEAF tSsid181Param[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_wlan_enable, set_wlan_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_ssid181_status, NULL, NULL, NULL},
{"Name", &DMREAD, DMT_STRING, get_ssid181_name, NULL, NULL, NULL},
{"LowerLayers", &DMREAD, DMT_STRING, get_ssid181_lowerlayers, NULL, NULL, NULL},
{"BSSID", &DMREAD, DMT_STRING, get_bssid, NULL, NULL, NULL},
{"MACAddress", &DMREAD, DMT_STRING, get_bssid, NULL, NULL, NULL},
{"SSID", &DMWRITE, DMT_STRING, get_ssid, set_ssid, NULL, NULL},
{"X_AIS_APModuleEnable", &DMWRITE, DMT_BOOL, get_apmodule_enable, set_apmodule_enable, NULL, NULL},
{"X_AIS_WlanStandard", &DMWRITE, DMT_STRING, get_wlan_standard, set_wlan_standard, NULL, NULL},
{0}
};

static DMLEAF tWps181Param[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_wps_enable, set_accept_and_drop, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_wps_status, NULL, NULL, NULL},
{"ConfigMethodsSupported", &DMREAD, DMT_STRING, get_wps_methods, NULL, NULL, NULL},
{"ConfigMethodsEnabled", &DMWRITE, DMT_STRING, get_wps_methods, set_accept_and_drop, NULL, NULL},
{"PIN", &DMWRITE, DMT_STRING, get_wps_password, set_accept_and_drop, NULL, NULL},
{0}
};

static DMOBJ tAp181Obj[] = {
{"WPS", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tWps181Param, NULL},
{0}
};

static DMLEAF tAp181Param[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_radio_enabled, set_radio_enabled, NULL, NULL},
{"SSIDReference", &DMREAD, DMT_STRING, get_ap181_ssidref, NULL, NULL, NULL},
{"SSIDAdvertisementEnabled", &DMWRITE, DMT_BOOL, get_ssid_advertisement, set_ssid_advertisement, NULL, NULL},
{"WMMEnable", &DMWRITE, DMT_BOOL, get_false, set_accept_and_drop, NULL, NULL},
{"UAPSDEnable", &DMWRITE, DMT_BOOL, get_false, set_accept_and_drop, NULL, NULL},
{"MACAddressControlEnabled", &DMWRITE, DMT_BOOL, get_false, set_accept_and_drop, NULL, NULL},
{"AssociatedDeviceNumberOfEntries", &DMREAD, DMT_UNINT, get_total_associations, NULL, NULL, NULL},
{0}
};

static DMLEAF tWifi181Param[] = {
{"RadioNumberOfEntries", &DMREAD, DMT_UNINT, get_wifi181_radio_count, NULL, NULL, NULL},
{"SSIDNumberOfEntries", &DMREAD, DMT_UNINT, get_wifi181_iface_count, NULL, NULL, NULL},
{"AccessPointNumberOfEntries", &DMREAD, DMT_UNINT, get_wifi181_iface_count, NULL, NULL, NULL},
{0}
};

static DMOBJ tWifi181Obj[] = {
{"Radio", &DMREAD, NULL, NULL, NULL, browseRadio181Inst, NULL, NULL, NULL, tRadio181Param, NULL},
{"SSID", &DMREAD, NULL, NULL, NULL, browseWlan181Inst, NULL, NULL, tSsid181Obj, tSsid181Param, NULL},
{"AccessPoint", &DMREAD, NULL, NULL, NULL, browseWlan181Inst, NULL, NULL, tAp181Obj, tAp181Param, NULL},
{0}
};

static DMOBJ tWifi181Root[] = {
{"WiFi", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tWifi181Obj, tWifi181Param, NULL},
{0}
};

/* the three instance branches and the counts; X_AIS_Mesh, the X-AIS_ power
 * leaves and NeighboringWiFiDiagnostic are claimed by their own modules, and
 * wlansec/wlanassoc join AccessPoint unclaimed, as for TR-098 */
static const char *const wlan181_mtk_paths[] = {
	"Device.WiFi.Radio.",
	"Device.WiFi.SSID.",
	"Device.WiFi.AccessPoint.",
	"Device.WiFi.RadioNumberOfEntries",
	"Device.WiFi.SSIDNumberOfEntries",
	"Device.WiFi.AccessPointNumberOfEntries",
	NULL
};

static const struct dm_module wlan181_mtk_module = {
	.name  = "mtk-wlan-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tWifi181Root,
	.paths = wlan181_mtk_paths,
};
DM_MODULE_REGISTER(wlan181_mtk_module);
