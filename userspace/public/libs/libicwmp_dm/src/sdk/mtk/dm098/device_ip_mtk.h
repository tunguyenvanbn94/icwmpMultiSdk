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
/* the network section of an Interface.{i} instance (its browse data) */
const char *dip_section(void *data);

/* "Device.IP.Interface.<n>" -> the layer 3 device of that interface (netifd's
 * l3_device, else network.<sec>.device): NULL when ref is not such a path, ""
 * when no interface has that number or it has no device */
char *dip_netdev_of_ref(const char *ref);
/* a layer 3 device -> "Device.IP.Interface.<n>" of the lowest interface on
 * it, NULL when none */
char *dip_ref_of_netdev(const char *dev);

/*
 * The Interface of a TR-181 diagnostic around the product's getter/setter,
 * which take a device name: reads the reference of that device (the name as
 * it is when no interface carries it); a reference written goes down as its
 * device (9007 when it has none), any other value as it is -- the product's
 * own check decides, so the device names it took keep working.
 */
#define IFREF181_GET(name, getter)						\
static int get_ifref181_##name(char *refparam, struct dmctx *ctx, void *data,	\
			       char *instance, char **value)			\
{										\
	int rc = getter(refparam, ctx, data, instance, value);			\
	char *r;								\
										\
	if (rc)									\
		return rc;							\
	r = dip_ref_of_netdev(*value);						\
	if (r)									\
		*value = r;							\
	return 0;								\
}

#define IFREF181_SET(name, setter)						\
static int set_ifref181_##name(char *refparam, struct dmctx *ctx, void *data,	\
			       char *instance, char *value, int action)	\
{										\
	char *dev = dip_netdev_of_ref(value);					\
										\
	if (dev && !*dev)							\
		return FAULT_9007;						\
	return setter(refparam, ctx, data, instance, dev ? dev : value, action);	\
}

#endif
