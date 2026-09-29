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

#endif
