/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Shared by the WANDevice modules of the MTK/Airoha product
 *	(wan_mtk.c, and the connection modules that follow it).
 */
#ifndef __WAN_MTK_H
#define __WAN_MTK_H

/*
 * The netdev carrying the uplink, what get_uplink_iface() of
 * functions/tr098/wan_device mapped from clay.opermode.uplink:
 *
 *	eth1 -> eth1, eth2 -> eth0.2, eth3 -> eth0.3, eth4 -> eth0.4,
 *	pon  -> pon,  anything else (unset included) -> eth0
 *
 * Static string, never NULL.
 */
const char *wan_uplink_iface(void);

/*
 * /sys/class/net/<iface>/statistics/<counter>, the file the shell cat'ed.
 * "" when the interface or the counter is not there -- the shell sent the
 * empty string too (cat ... 2>/dev/null).
 */
char *wan_netdev_stat(const char *iface, const char *counter);

/*
 * TR-181: the WAN port is Device.Ethernet.Interface.<n> (laneth_mtk.c owns
 * that object and its browse, the WAN instance comes after the LAN ports).
 * Its leaves are WANEthernetInterfaceConfig's (+Stats), answered here so the
 * product's values stay in one file.  leaf is the TR-181 name (Stats leaves
 * without the "Stats." prefix); 0, or FAULT_9008 for a leaf this port does
 * not let the ACS write.
 */
int wan_eth181_get(const char *leaf, char **value);
int wan_eth181_set(const char *leaf, char *value, int action);

#endif
