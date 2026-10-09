/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	TR-181 interface stack of the MTK / Airoha product (stack181_mtk.c):
 *	the LowerLayers of Device.IP.Interface and Device.PPP.Interface.
 */
#ifndef __STACK181_MTK_H
#define __STACK181_MTK_H

/* LowerLayers of the IP.Interface on network section <sec>: Ethernet.Link.1
 * for the LAN, the connection's PPP.Interface, VLANTermination or uplink
 * link, the bridge's link when bridged; "" when nothing is known */
char *stack181_ipif_lower(const char *sec);
/* LowerLayers of the PPP.Interface on wan section "@entry[<n>]" */
char *stack181_ppp_lower(const char *wan_sec);
/* a LowerLayers written there -> the product's value ("pon", "pon.<vid>"),
 * NULL when the product cannot take it */
char *stack181_ppp_lower_to_product(const char *wan_sec, const char *ref);

#endif
