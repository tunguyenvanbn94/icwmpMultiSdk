/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The WAN entry model, shared by every module under
 *	WANDevice.{i}.WANConnectionDevice.{i}.
 *
 *	It lives in one place because the product does: functions/tr098/wan_device
 *	enumerates the entries once, in wan_device_get_total_entry(), and hands
 *	the same $uci_wan_idx / $wan_id / $iface4 / $iface6 to the IP branch, the
 *	PPP branch and everything under them.  A second copy of this loop would
 *	be a second chance to get the instance numbering wrong.
 *
 *	Defined in wanip_mtk.c, which owns both connection objects.
 */
#ifndef __WANCONN_MTK_H
#define __WANCONN_MTK_H

#include <json-c/json.h>

#include "dmtr098.h"

struct wan_entry {
	struct uci_section *s;
	int idx;		/* position among "entry" sections = wan.@entry[idx] */
	int id;			/* the "id" option; instance = id + 1 */
	int bridge;		/* switch_mode == 1 */
	int ppp;		/* switch_mode == 0 && conn_type == 2 */
	int tr069;		/* service_type & 2 */
	char if4[32];		/* "if<id>" or, bridged, "if_wanbr<id>" */
	char if6[32];		/* "if<id>_6", empty when bridged */
	char dev[32];		/* "dev_wanbr<id>", empty when routed */
};

/* the shell's $targe_conn_type */
#define WAN_KIND_IP	0
#define WAN_KIND_PPP	1
#define WAN_MAX_ENTRIES	32

/*
 * One pass over wan.@entry[], stopped at the first section without an id.
 * Returns how many entries of that kind exist and, when out is not NULL,
 * fills it with up to max of them.
 */
int wan_entries_kind(struct wan_entry **out, int max, int kind);

/* Option of a section / of the entry, never NULL: unset reads as "". */
char *wan_sect_opt(struct uci_section *s, char *option);
char *wan_entry_opt(void *data, char *option);
/* Decimal, non-negative; 0 when the string is anything else. */
int wan_str_is_uint(const char *s, long *out);

/* ubus call network.interface.<iface> status, NULL when it fails */
json_object *wan_iface_status(const char *iface);
/* "@.l3_device" of that status */
char *wan_iface_l3_device(const char *iface);

/* queue the network reload for the end of the session */
void wan_reload(void);
/* ubus call hni.wan set; 0 only on "result":"SUCCESS" */
int wan_ubus_set(int index, const char *action, const char *param, const char *value);
/* that call with action "modify", plus the reload; a FAULT_ code on failure */
int wan_modify(struct wan_entry *e, const char *param, const char *value);

/*
 * TR-181 (cwmp.cpe.datamodel=tr181).  A connection is anchored on the
 * Device.IP.Interface of its network section (if<id>, if_wanbr<id>); the
 * TR-181 tables of the other objects borrow its leaves from here, by their
 * TR-098 name, so the IP/PPP differences of wanip_mtk.c stay where they are.
 */
/* every connection of both objects, in config order */
int wan_entries_all(struct wan_entry **out, int max);
/* the connection whose network section is <sec>; 1 and *out when found */
int wan_entry_of_sec(const char *sec, struct wan_entry *out);
/* the connection at wan.@entry[idx]; 1 and *out when found */
int wan_entry_of_idx(int idx, struct wan_entry *out);
/* a leaf of WANIPConnection or WANPPPConnection (the entry's kind) */
int wan181_get(struct wan_entry *e, const char *leaf, char **value);
int wan181_set(struct wan_entry *e, const char *leaf, char *value, int action);
/* "Device.IP.Interface.<n>" of the connection, "" when not numbered */
char *wan181_ipif(struct wan_entry *e);
/* the connection on a Device.IP.Interface instance (its browse data), NULL
 * when that interface carries none */
struct wan_entry *wan181_of_ipif(void *ipif_data);
/* <leaf> of a TR-098 leaf table t of a connection, called on e: "" when t
 * has no such leaf (or t is NULL), 9008 when it is not writable there */
int wan181_tbl_get(DMLEAF *t, struct wan_entry *e, const char *leaf, char **value);
int wan181_tbl_set(DMLEAF *t, struct wan_entry *e, const char *leaf, char *value, int action);

/*
 * A leaf of Device.IP.Interface.{i} answered by the connection on it: the
 * TR-098 leaf <leaf> of ip_t (IPoE/bridge) or ppp_t (PPP).  Empty, and not
 * writable, on an interface without a connection.
 */
#define IPIF181_GET(name, ip_t, ppp_t, leaf)						\
static int get_ipif181_##name(char *refparam, struct dmctx *ctx, void *data,		\
			      char *instance, char **value)				\
{											\
	struct wan_entry *e = wan181_of_ipif(data);					\
											\
	*value = "";									\
	return e ? wan181_tbl_get(e->ppp ? (ppp_t) : (ip_t), e, leaf, value) : 0;	\
}

#define IPIF181_SET(name, ip_t, ppp_t, leaf)						\
static int set_ipif181_##name(char *refparam, struct dmctx *ctx, void *data,		\
			      char *instance, char *value, int action)			\
{											\
	struct wan_entry *e = wan181_of_ipif(data);					\
											\
	return e ? wan181_tbl_set(e->ppp ? (ppp_t) : (ip_t), e, leaf, value, action)	\
		 : FAULT_9008;								\
}

#endif
