/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Shared between the two halves of WLANConfiguration: wlan_mtk.c (radio,
 *	identity, channel, counters, WPS) and wlanassoc_mtk.c (AssociatedDevice).
 *	Both are merged into the same object by dm_registry.c.
 */
#ifndef __WLAN_MTK_H
#define __WLAN_MTK_H

/* One row of the product's fixed instance map, handed to the getters as data.
 * The numbering is what the ACS has provisioned against and must not move. */
struct wlan_iface {
	int index;			/* WLANConfiguration.{i} */
	const char *name;		/* ra0 ... rai5 */
	const char *mapd_node;		/* "mapd.<n>", the node mapd rewrites from */
};

const struct wlan_iface *wlan_iface_of(void *data);
/* the interface of WLANConfiguration.<index> / WiFi.SSID.<index>, "" if none */
const char *wlan_ifname_of_index(int index);
/* wireless.<iface>.<option>, never NULL */
char *wlan_opt(void *data, char *option);
/* mirror one option into mapd's own node, ignored when the iface has none */
void wlan_mapd_set(const char *iface, char *option, char *value);
/* queue "wifi reload" for the end of the session */
void wlan_reload(void);
/* is_mesh_enabled(): both radios run EasyMesh (map_mode not 0) */
int wlan_mesh_enabled(void);

/* Number of stations associated to this interface right now.  Lives in
 * wlanassoc_mtk.c because it is one ubus call shared with the instance browse. */
int wlan_assoc_count(const struct wlan_iface *w);

#endif
