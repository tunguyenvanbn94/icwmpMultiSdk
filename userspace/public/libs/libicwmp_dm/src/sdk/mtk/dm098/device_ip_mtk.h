/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The numbering of InternetGatewayDevice.Device.IP.Interface.{i}, shared
 *	with Device.DHCPv6.Server.Pool.{i}.Interface (device_dhcpv6_mtk.c):
 *	network.<section>.ip_int_instance, given the first time a section is
 *	seen and committed at once, as functions/tr098/device_ip did.
 */
#ifndef __DEVICE_IP_MTK_H
#define __DEVICE_IP_MTK_H

/* network.<section>.ip_int_instance as stored, "" when unset */
char *dip_instance_of(const char *nsec);
/* ip_device_update_instance <section>: the stored number, or the first free
 * one, given and committed.  "" when network.<section> does not exist (the
 * shell's "uci set" failed there and left nothing behind). */
char *dip_update_instance(const char *nsec);
/* the first network section whose ip_int_instance is <inst>, NULL if none
 * (dhcp6_find_network_sec_by_ipinst) */
char *dip_section_of_instance(const char *inst);

#endif
