/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	X_AIS_ServiceList on both WAN connection objects.  Two parameters, and
 *	the most consequential ones of the whole WANDevice branch: the setter
 *	decides whether this WAN carries customer traffic, TR-069 management, or
 *	both -- and on the way it rewrites the firewall and the CWMP client's own
 *	binding.
 *
 *	Ported from functions/tr098/wan_device:
 *	  wan_device_get_x_ais_service_list(), wan_device_set_x_ais_service_list(),
 *	  wan_device_update_internet_access(), wan_device_configure_easycwmpd(),
 *	  wan_device_get_physical_interface().
 *
 *	Values map to wan.@entry[i].service_type: 1 INTERNET, 2 TR069,
 *	3 INTERNET_TR069, 4 OTHER.  Anything else reads as OTHER, and a bridged
 *	entry always reads OTHER whatever the option says.
 *
 *	WHAT THE SETTER TOUCHES, all of it kept:
 *
 *	  - wan.@entry[i].service_type,
 *	  - easycwmp.@acs[0].enablecwmp on the transitions that cross the TR-069
 *	    boundary,
 *	  - easycwmp.@local[0].interface and .network, which bind the CWMP client
 *	    to one WAN,
 *	  - a firewall rule "tr069_block_<wan_if>" plus the matching iptables
 *	    FORWARD DROP, which is how TR069-only blocks LAN traffic.
 *
 *	THE ONE DIFFERENCE, and it is the reason this phase was left for last:
 *	the shell ran the disruptive half INLINE -- "iptables -I FORWARD",
 *	"/etc/init.d/firewall reload &" and "/etc/init.d/easycwmpd restart &" --
 *	from inside the SetParameterValues it was answering.  Restarting the CWMP
 *	client mid-session means the ACS never sees the response to the very call
 *	that asked for it.
 *
 *	Here the UCI half runs immediately, in the same transaction as the rest
 *	of the session, and only the imperative half is queued on the
 *	apply-service list, which runs after the session closes.  The commands
 *	queued are the shell's own, verbatim.
 *
 *	"/etc/init.d/easycwmpd restart" is correct in this build and is NOT a
 *	leftover: sdk/mtk/files/easycwmpd is a compatibility shim whose only line
 *	is "exec /etc/init.d/icwmpd "$@"".
 *
 *	KNOWN GAP, left alone on purpose: easycwmp.@acs[0].enablecwmp is written
 *	exactly as before, but the easycwmp -> cwmp mirror in
 *	apps/icwmp/sdk/mtk/icwmp_mtk.c does not carry that option, so icwmpd
 *	itself is not gated by it in this build the way easycwmpd was.  Adding it
 *	to the mirror would let an ACS switch the CWMP client off by writing
 *	X_AIS_ServiceList -- a decision for the product, not for this port.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dmuci.h"
#include "dmubus.h"
#include "dmcommon.h"
#include "dmjson.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "wanconn_mtk.h"

#define SVC_INTERNET		"1"
#define SVC_TR069		"2"
#define SVC_INTERNET_TR069	"3"
#define SVC_OTHER		"4"

/* ------------------------------------------------------------------ */
/* names                                                               */
/* ------------------------------------------------------------------ */

static const char *svc_name_of(const char *num)
{
	if (strcmp(num, SVC_INTERNET) == 0)
		return "INTERNET";
	if (strcmp(num, SVC_TR069) == 0)
		return "TR069";
	if (strcmp(num, SVC_INTERNET_TR069) == 0)
		return "INTERNET_TR069";
	return "OTHER";		/* 4, unset, and anything unexpected */
}

static const char *svc_num_of(const char *name)
{
	if (!name)
		return NULL;
	if (strcmp(name, "INTERNET") == 0)
		return SVC_INTERNET;
	if (strcmp(name, "TR069") == 0)
		return SVC_TR069;
	if (strcmp(name, "INTERNET_TR069") == 0)
		return SVC_INTERNET_TR069;
	if (strcmp(name, "OTHER") == 0)
		return SVC_OTHER;
	return NULL;
}

/* ------------------------------------------------------------------ */
/* the three side effects                                              */
/* ------------------------------------------------------------------ */

/* wan_device_get_physical_interface(): the l3_device of if<id> */
static char *svc_wan_if(struct wan_entry *e)
{
	return e ? wan_iface_l3_device(e->if4) : "";
}

static void svc_acs_enablecwmp(int on)
{
	dmuci_set_value("easycwmp", "@acs[0]", "enablecwmp", on ? "1" : "0");
}

/*
 * wan_device_configure_easycwmpd().  Bind points the client at this WAN,
 * unbind puts it back on if0 and blanks the interface with a single space --
 * the shell's own comment says "To avoid deleting parameter", and an empty
 * string would drop the option.
 *
 * The restart is queued, not run: it is the CWMP daemon answering the ACS.
 */
static void svc_configure_cwmpd(struct wan_entry *e, int bind)
{
	char buf[64];

	if (bind) {
		char *wan_if = svc_wan_if(e);

		if (wan_if[0])
			dmuci_set_value("easycwmp", "@local[0]", "interface", wan_if);
		snprintf(buf, sizeof(buf), "if%d", e->id);
		dmuci_set_value("easycwmp", "@local[0]", "network", buf);
	} else {
		dmuci_set_value("easycwmp", "@local[0]", "network", "if0");
		dmuci_set_value("easycwmp", "@local[0]", "interface", " ");
	}
	if (strcmp(mtk_uci("easycwmp", "@local[0]", "enable"), "1") == 0)
		mtk_apply_service("(/etc/init.d/easycwmpd restart 2>/dev/null; "
				  "/etc/init.d/stuncd restart 2>/dev/null) &");
}

static struct uci_section *svc_block_rule(const char *rule_name)
{
	struct uci_section *s;

	uci_foreach_sections("firewall", "rule", s) {
		if (strcmp(wan_sect_opt(s, "name"), rule_name) == 0)
			return s;
	}
	return NULL;
}

/*
 * wan_device_update_internet_access().  block = 1 cuts LAN -> this WAN so the
 * link carries TR-069 only; block = 0 removes the cut.  Two layers, both kept:
 * a live iptables FORWARD rule and a UCI firewall rule for the next boot.
 *
 * Only the iptables lines and the firewall reload are queued.  The shell
 * checked with "iptables -C" before inserting, which is what keeps this
 * idempotent when the rule is already there.
 */
static void svc_internet_access(const char *wan_if, int block)
{
	char rule_name[64], cmd[320];
	struct uci_section *s;
	/* dmuci_add_section() writes the new section name through this on every
	 * path, error ones included -- it must not be NULL */
	char *added_name = NULL;

	if (!wan_if || !wan_if[0])
		return;
	snprintf(rule_name, sizeof(rule_name), "tr069_block_%s", wan_if);
	s = svc_block_rule(rule_name);
	if (block) {
		snprintf(cmd, sizeof(cmd),
			 "iptables -C FORWARD -i br-lan -o %s -j DROP 2>/dev/null || "
			 "iptables -I FORWARD 1 -i br-lan -o %s -j DROP 2>/dev/null",
			 wan_if, wan_if);
		mtk_apply_service(cmd);
		if (s)
			return;		/* already persisted, nothing to add */
		dmuci_add_section("firewall", "rule", &s, &added_name);
		if (!s)
			return;
		dmuci_set_value_by_section(s, "name", rule_name);
		dmuci_set_value_by_section(s, "src", "lan");
		dmuci_set_value_by_section(s, "dest", "wan");
		dmuci_set_value_by_section(s, "proto", "all");
		dmuci_set_value_by_section(s, "target", "DROP");
		dmuci_set_value_by_section(s, "enabled", "1");
		mtk_apply_service("/etc/init.d/firewall reload 2>/dev/null &");
		return;
	}
	snprintf(cmd, sizeof(cmd),
		 "iptables -D FORWARD -i br-lan -o %s -j DROP 2>/dev/null", wan_if);
	mtk_apply_service(cmd);
	if (!s)
		return;
	dmuci_delete_by_section(s, NULL, NULL);
	mtk_apply_service("/etc/init.d/firewall reload 2>/dev/null &");
}

/* ------------------------------------------------------------------ */
/* leaves                                                              */
/* ------------------------------------------------------------------ */

static int get_service_list(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;

	if (e && e->bridge) {
		*value = "OTHER";	/* whatever service_type says */
		return 0;
	}
	*value = (char *)svc_name_of(wan_entry_opt(data, "service_type"));
	return 0;
}

/*
 * The state machine of wan_device_set_x_ais_service_list(), transition by
 * transition.  Two of them are not what the name suggests:
 *
 *   - in bridge mode only OTHER is accepted at all,
 *   - in router mode OTHER is silently STORED AS INTERNET (num = 1).  The
 *     shell did that and the ACS has been reading INTERNET back ever since.
 */
static int set_service_list(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	const char *want = svc_num_of(value);
	const char *old;
	char *wan_if;
	int old_known;

	if (!e)
		return FAULT_9002;
	if (!want)
		return FAULT_9007;
	old = wan_entry_opt(data, "service_type");

	if (e->bridge) {
		if (strcmp(want, SVC_OTHER) != 0)
			return FAULT_9007;	/* bridge carries nothing else */
		if (action == VALUECHECK)
			return 0;
		if (strcmp(old, SVC_OTHER) != 0) {
			if (strcmp(old, SVC_TR069) == 0 ||
			    strcmp(old, SVC_INTERNET_TR069) == 0)
				svc_acs_enablecwmp(0);
			svc_configure_cwmpd(e, 0);
		}
		dmuci_set_value_by_section(e->s, "service_type", (char *)want);
		wan_reload();
		return 0;
	}

	if (action == VALUECHECK)
		return 0;

	wan_if = svc_wan_if(e);
	if (!wan_if[0]) {
		/* interface not up yet: the shell only recorded the value */
		dmuci_set_value_by_section(e->s, "service_type", (char *)want);
		wan_reload();
		return 0;
	}

	/*
	 * Every branch of the shell is a "case $old_service_type in 3|2|1|4)"
	 * with NO default arm.  An entry whose service_type has never been set
	 * therefore falls through all of them: the value is recorded and not
	 * one side effect runs.  Reproduced with old_known -- a fresh entry
	 * must not have its firewall or the CWMP binding rewritten on the first
	 * write an ACS makes.
	 */
	old_known = strcmp(old, SVC_INTERNET) == 0 || strcmp(old, SVC_TR069) == 0 ||
		    strcmp(old, SVC_INTERNET_TR069) == 0 || strcmp(old, SVC_OTHER) == 0;

	if (!old_known) {
		dmuci_set_value_by_section(e->s, "service_type", (char *)want);
		wan_reload();
		return 0;
	}

	if (strcmp(want, SVC_INTERNET_TR069) == 0) {
		svc_internet_access(wan_if, 0);
		if (strcmp(old, SVC_INTERNET) == 0 || strcmp(old, SVC_OTHER) == 0)
			svc_acs_enablecwmp(1);
		svc_configure_cwmpd(e, 1);
	} else if (strcmp(want, SVC_TR069) == 0) {
		svc_internet_access(wan_if, 0);
		if (strcmp(old, SVC_INTERNET) == 0 || strcmp(old, SVC_OTHER) == 0) {
			svc_acs_enablecwmp(1);
			svc_configure_cwmpd(e, 1);
			svc_internet_access(wan_if, 1);
		} else {
			svc_internet_access(wan_if, 1);
			svc_configure_cwmpd(e, 1);
		}
	} else if (strcmp(want, SVC_INTERNET) == 0) {
		svc_internet_access(wan_if, 0);
		if (strcmp(old, SVC_TR069) == 0 || strcmp(old, SVC_INTERNET_TR069) == 0)
			svc_acs_enablecwmp(0);
		svc_configure_cwmpd(e, 0);
	} else {	/* OTHER, in router mode */
		if (strcmp(old, SVC_OTHER) == 0) {
			dmuci_set_value_by_section(e->s, "service_type", (char *)want);
			wan_reload();
			return 0;	/* maintain: nothing else touched */
		}
		if (strcmp(old, SVC_TR069) == 0 || strcmp(old, SVC_INTERNET_TR069) == 0)
			svc_acs_enablecwmp(0);
		svc_internet_access(wan_if, 0);
		svc_configure_cwmpd(e, 0);
		want = SVC_INTERNET;	/* stored as INTERNET, not OTHER */
	}

	dmuci_set_value_by_section(e->s, "service_type", (char *)want);
	wan_reload();
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tConnServiceListParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"X_AIS_ServiceList", &DMWRITE, DMT_STRING, get_service_list, set_service_list, NULL, NULL},
{0}
};

static DMOBJ tWanCxDevSvcObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"WANIPConnection", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 NULL, tConnServiceListParam, NULL},
{"WANPPPConnection", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 NULL, tConnServiceListParam, NULL},
{0}
};

static DMOBJ tWanDeviceSvcObj[] = {
{"WANConnectionDevice", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 tWanCxDevSvcObj, NULL, NULL},
{0}
};

static DMOBJ tWanDeviceSvcRoot[] = {
{"WANDevice", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tWanDeviceSvcObj, NULL, NULL},
{0}
};

/*
 * No .paths: wan_mtk.c claims the whole WANDevice branch (K8), dm_registry
 * merges this tree into it.
 */
static const struct dm_module servicelist_mtk_module = {
	.name  = "mtk-servicelist",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tWanDeviceSvcRoot,
};
DM_MODULE_REGISTER(servicelist_mtk_module);
