/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	icwmpd SDK contract (the counterpart of libtr098 sdk/sdk.h).
 *
 *	icwmpd keeps its own UCI config "cwmp" on every SDK.  What differs is
 *	where the ACS settings of record live and what has to happen around a
 *	session; that goes through the hooks below.  Exactly one implementation
 *	is linked, selected by ./configure --with-sdk=<name>.
 *
 *	One directory per SDK, self contained, nothing outside it names it:
 *
 *	    sdk/<name>/sdk.m4     configure fragment
 *	    sdk/<name>/sdk.mk     automake fragment (sources, flags, install)
 *	    sdk/<name>/*.c        the icwmp_platform_* hooks below
 *	    sdk/<name>/scripts/   the /usr/sbin/icwmp action script, if any
 *	    sdk/<name>/files/     init scripts, UCI defaults, helper daemons
 *	    sdk/<name>/README.md
 *
 *	    ./tools/sdk-scan.sh           rebuild sdk/enabled.m4 and sdk/enabled.mk
 *	    ./tools/sdk-prune.sh <name>   keep one SDK, delete the others
 *
 *	SDKs in this tree (see each README.md):
 *
 *	  uci  stock OpenWrt: nothing to do, the cwmp config is the config of
 *	       record, /usr/sbin/icwmp shell script for downloads and reboot
 *	  bdk  Broadcom BDK: attach to the Distributed MDM, mirror
 *	       Device.ManagementServer.* <-> cwmp config, save to flash at the
 *	       end of a session, actions in C
 *	  mtk  MediaTek/Airoha OpenWrt (HNI product): mirror the product's
 *	       easycwmp UCI config (config of record: WebUI, DHCP option 43,
 *	       stuncd) <-> cwmp config, action script with the product's
 *	       upgrade / reboot logic
 *
 *	SDK specific defines (CWMP_BKP_FILE, boot flag file) live in the header
 *	the SDK names with -DICWMP_SDK_HEADER in its sdk.mk.
 */
#ifndef __ICWMP_SDK_H__
#define __ICWMP_SDK_H__

/* set by sdk/<name>/sdk.mk, e.g. -DICWMP_SDK_HEADER='"icwmp_bdk.h"' */
#ifdef ICWMP_SDK_HEADER
#include ICWMP_SDK_HEADER
#endif

struct cwmp;

/* cwmp_init(), before global_conf_init() reads the cwmp config: attach to
 * the platform, seed / mirror the config of record.  Non-zero = fatal. */
int icwmp_platform_init(void);

/* cwmp_config_reload() (ubus "tr069 command reload", END_SESSION_RELOAD),
 * before global_conf_init(): refresh the cwmp config from the config of
 * record.  Returns 1 when something changed (informative). */
int icwmp_platform_config_reload(void);

/* after global_conf_init() at reload: the platform may have changed the
 * identity (DeviceId of the Inform) or the data model root */
void icwmp_platform_config_reloaded(struct cwmp *cwmp);

/* uloop thread, before uloop_run(): platform fds/events */
int icwmp_platform_uloop_register(void);

/* end of every ACS session (run_session_end_func) and after a change made
 * through ubus "tr069 dm": push icwmpd-owned values to the config of
 * record, persist, reload the config when the ACS changed ManagementServer.* */
void icwmp_platform_end_session(void);

/* process exit */
void icwmp_platform_cleanup(void);

#endif
