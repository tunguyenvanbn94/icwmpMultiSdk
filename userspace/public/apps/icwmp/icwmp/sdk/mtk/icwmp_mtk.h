/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	icwmpd on the MediaTek/Airoha OpenWrt product tree (HNI) — process glue,
 *	compiled only with --with-platform=mtk (ICWMP_MTK).
 *
 *	The product keeps its TR-069 settings in the UCI config "easycwmp"
 *	(sections @local[0], @acs[0], @device[0], @notifications[0]) and every
 *	other component reads or writes that file: the WebUI backend
 *	(hal_unify hal_gateway.c), the DHCP option 43 parser and the WAN-up
 *	watcher of the easycwmpd init script, stuncd, the easycwmp function
 *	library that serves the data model (libtr098 platform/mtk).  icwmpd
 *	itself reads "cwmp" (config.c UCI_*_PATH), so this file mirrors:
 *
 *	  easycwmp -> cwmp   at start, at every config reload (ubus "tr069
 *	                     command reload", END_SESSION_RELOAD after the ACS
 *	                     changed ManagementServer.* through the script) and
 *	                     at the end of every session
 *	  cwmp -> easycwmp   the values icwmpd owns: ParameterKey and the
 *	                     connection request IP (netlink) the script uses to
 *	                     build ManagementServer.ConnectionRequestURL
 *
 *	The identity (DeviceId of the Inform) is read from easycwmp.@device[0]
 *	by libtr098 tr098/mtk/deviceinfo_mtk.c, nothing to mirror.
 */
#ifndef __ICWMP_MTK_H__
#define __ICWMP_MTK_H__

/* persistent state of icwmpd on the writable overlay, same places as the
 * stock OpenWrt build (bin/Makefile.am CWMP_BKP_FILE, event.c boot flag) */
#ifndef ICWMP_MTK_STATE_DIR
#define ICWMP_MTK_STATE_DIR     "/etc/icwmpd"
#endif

/* easycwmp -> cwmp; returns 1 when a cwmp option changed (committed) */
int icwmp_mtk_sync_easycwmp_to_cwmp(void);
/* cwmp -> easycwmp for the icwmpd-owned values; 1 when something changed */
int icwmp_mtk_sync_cwmp_to_easycwmp(void);

/* the icwmp_platform_* hooks of sdk/sdk.h are implemented in
 * icwmp_mtk.c on top of the two functions above */

#endif
