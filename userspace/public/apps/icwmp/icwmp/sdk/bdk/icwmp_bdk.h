/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	icwmpd on Broadcom BDK (Distributed MDM) — process glue.
 *
 *	What tr69c does in main/mainCms.c and bcmLibIF/bcmWrapperCms.c, done for
 *	icwmpd:
 *	  - attach to the tr69 component MDM with the shmId tr69_md passes on the
 *	    command line (icwmpd -b -S <shmId> [-X]) and to the TR69_MSG_BUS
 *	  - subscribe to CMS events (ACS config changed, active notification,
 *	    WAN up) and feed them into icwmp's own event machinery
 *	  - mirror Device.ManagementServer.* (MDM, edited by WebUI/tr69_mdmcli)
 *	    into icwmp's UCI "cwmp" config and back
 *	  - save config to flash at end of session, reboot / factory reset,
 *	    firmware & vendor-config apply (external_bdk.c)
 *
 *	Everything here is compiled only with --enable-bdk (ICWMP_BDK).
 */
#ifndef __ICWMP_BDK_H__
#define __ICWMP_BDK_H__

#ifndef ICWMP_BDK_DATA_DIR
#define ICWMP_BDK_DATA_DIR      "/data/icwmp"
#endif
#ifndef ICWMP_BDK_TMP_DIR
#define ICWMP_BDK_TMP_DIR       "/tmp/icwmp"
#endif
#define ICWMP_BDK_UCI_CONFDIR   ICWMP_BDK_DATA_DIR "/config"
#define ICWMP_BDK_UCI_SEED      "/etc/icwmp/cwmp"
#define ICWMP_BDK_DOWNLOAD_FILE ICWMP_BDK_TMP_DIR "/download.bin"
#define ICWMP_BDK_CRASH_LOG     ICWMP_BDK_DATA_DIR "/crash.log"
/* pending BOOT event marker (OpenWrt: /etc/icwmpd/.icwmpd_boot, see event.c) */
#ifndef ICWMP_BOOT_FLAG_FILE
#define ICWMP_BOOT_FLAG_FILE    ICWMP_BDK_DATA_DIR "/.icwmpd_boot"
#endif
/* forced, not #ifndef: bin/Makefile.am used to pass the OpenWrt default
 * /etc/icwmpd/... on the command line (see the comment there), which is not
 * writable on BDK -> bkp_tree NULL -> crash in bkp_session_save() */
#ifdef CWMP_BKP_FILE
#undef CWMP_BKP_FILE
#endif
#ifndef CWMP_BKP_FILE
#define CWMP_BKP_FILE           ICWMP_BDK_DATA_DIR "/.icwmpd_backup_session.xml"
#endif

/* command line, parsed in config.c global_env_init() */
void icwmp_bdk_set_shm_id(int shm_id);
void icwmp_bdk_set_boot_launched(int boot_launched);

/* process attach / detach; init must run before the first dm_ctx_init() */
int  icwmp_bdk_init(void);
void icwmp_bdk_cleanup(void);

/* add the CMS message fd to uloop (call from the uloop thread before uloop_run) */
int  icwmp_bdk_uloop_register(void);

/* end of CWMP session: push ManagementServer changes to MDM, save config to
 * flash (what tr69c does in acsDisconnect -> saveConfigurations) */
void icwmp_bdk_end_session(void);
void icwmp_bdk_save_config(void);      /* cmsMgm_saveConfigToFlash through the generic HAL */

/* MDM Device.ManagementServer.* -> UCI cwmp.acs/cwmp.cpe (returns 1 if a
 * value changed) and the reverse direction */
int  icwmp_bdk_sync_mdm_to_uci(void);
int  icwmp_bdk_sync_uci_to_mdm(void);
/* ParameterKey + ConnectionRequestURL (owned by icwmpd) -> MDM */
int  icwmp_bdk_sync_uci_only_to_mdm(void);

/* cwmp.cpe.datamodel: 1 = tr181 (libtr098 root Device., generic HAL
 * passthrough), 0 = tr098 (InternetGatewayDevice. tree).  Read at init. */
int  icwmp_bdk_tr181_mode(void);
void icwmp_bdk_load_mode(void);        /* (re)read it: init, cwmp_config_reload() */

/* actions used by external_bdk.c */
void icwmp_bdk_reboot(const char *requestor);
void icwmp_bdk_factory_reset(void);
/* returns CWMP fault code as string ("0" = ok), static storage */
const char *icwmp_bdk_apply_firmware(const char *file);
const char *icwmp_bdk_apply_vendor_config(const char *file);

void *icwmp_bdk_msg_handle(void);

/* the icwmp_platform_* hooks of sdk/sdk.h are implemented in
 * icwmp_bdk.c on top of the functions above; ubus "tr069 dm" is platform
 * independent now (icwmp_dm.c) */

#endif
