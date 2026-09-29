/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	icwmpd on Broadcom BDK — see icwmp_bdk.h.
 *
 *	Mirrors, function by function, what tr69c does:
 *	  icwmp_bdk_init          <- mainCms.c main(): cmsMsg_initOnBus(TR69_MSG_BUS),
 *	                             sleep 20 s when boot launched, cmsMdm_initWithConfig
 *	                             (NDA_ACCESS_TR69C, MDM_SHM_ATTACH_ADDR_TR69,
 *	                             TR69_KEY_OFFSET), registerInterestInEvent(...)
 *	  bdk_msg_cb              <- mainCms.c readMessageFromSmd()
 *	  icwmp_bdk_end_session   <- informer.c acsDisconnect -> saveConfigurations
 *	                             -> cmsMgm_saveConfigToFlash
 *	  icwmp_bdk_reboot        <- bcmWrapperCms.c wrapperReset
 *	  icwmp_bdk_factory_reset <- bcmWrapperCms.c wrapperFactoryReset
 *	  icwmp_bdk_apply_*       <- bcmWrapperCms.c downloadComplete
 *
 *	NOT BUILD-TESTED YET: written against the headers of
 *	bcm963xx lguplus 9f2a56abd0de9172bfe1283a8718a56cbce87e4e.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <execinfo.h>

#include <libubox/uloop.h>
#include <uci.h>

/* Broadcom CMS/BDK */
#include "cms.h"
#include "cms_util.h"          /* cmsLog_*, cmsMem_*, cmsImg_* */
#include "cms_msg.h"           /* cmsMsg_*, CMS_MSG_*, TR69_MSG_BUS */
#include "cms_core.h"          /* cmsMdm_*, cmsMgm_*, cmsLck_*, cmsPhl_* */
#include "bcm_generic_hal.h"   /* bcm_generic_databaseOp */
#include "bcm_boardutils.h"    /* bcmUtl_loggedBusybox_reboot */

/* icwmp */
#include "cwmp.h"
#include "log.h"
#include "config.h"
#include "xml.h"               /* cwmp_root_cause_event_ipdiagnostic */
#include "icwmp_bdk.h"
#include "sdk/sdk.h"

/* libtr098 BDK helpers (single MDM set with proper type lookup) */
#include <libtr098/dmbdk.h>

#define BDK_LOCK_TIMEOUT_MS   (6 * 1000)   /* TR69C_LOCK_TIMEOUT */
#define BDK_BOOT_WAIT_SEC     20           /* tr69c: wait for sysmgmt when boot launched */
#define MDM_MS                "Device.ManagementServer."

static void *msgHandle;
static SINT32 shmId = UNINITIALIZED_SHM_ID;
static int bootLaunched = 1;
static struct uloop_fd bdk_ufd;

void *icwmp_bdk_msg_handle(void)
{
	return msgHandle;
}

void icwmp_bdk_set_shm_id(int id)
{
	shmId = id;
}

void icwmp_bdk_set_boot_launched(int b)
{
	bootLaunched = b;
}

/* cwmp.cpe.datamodel = tr098 (default) | tr181, read once at init.  libtr098
 * reads the same option (dmproxy_bdk.c bdk_proxy_load_mode) to pick its root;
 * icwmpd needs it for the ManagementServer sync direction (end of session). */
static int bdkTr181;
static int uci_get_str(struct uci_context *c, const char *key, char *out, size_t outlen);
static void bdk_set_cms_log_level(void);

int icwmp_bdk_tr181_mode(void)
{
	return bdkTr181;
}

/* init and every config reload ("ubus call tr069 command reload", end of a
 * session that changed the ACS config): libtr098 re-reads the option at the
 * same moments (dm_platform_ctx_init), so a switch needs no restart */
void icwmp_bdk_load_mode(void)
{
	struct uci_context *c = uci_alloc_context();
	char v[32] = "";
	int was = bdkTr181;

	if (c) {
		uci_get_str(c, "cwmp.cpe.datamodel", v, sizeof(v));
		uci_free_context(c);
	}
	bdkTr181 = (strcasecmp(v, "tr181") == 0);
	if (bdkTr181 != was)
		cmsLog_notice("data model: %s (cwmp.cpe.datamodel='%s')",
		              bdkTr181 ? "TR-181 Device." : "TR-098 InternetGatewayDevice.", v);
	/* cwmp.cpe.log_severity may have changed too (icwmp re-reads it in
	 * global_conf_init); the CMS log level of the BDK glue follows it */
	bdk_set_cms_log_level();
}

/* ------------------------------------------------------------------------ */
/* filesystem: /data/icwmp is the persistent home, /tmp/icwmp the scratch   */
/* ------------------------------------------------------------------------ */

static void bdk_mkdir_p(const char *path)
{
	char buf[256], *p;

	snprintf(buf, sizeof(buf), "%s", path);
	for (p = buf + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(buf, 0755);
			*p = '/';
		}
	}
	mkdir(buf, 0755);
}

static int bdk_copy_file(const char *src, const char *dst)
{
	FILE *in, *out;
	char buf[4096];
	size_t n;

	in = fopen(src, "r");
	if (!in)
		return -1;
	out = fopen(dst, "w");
	if (!out) {
		fclose(in);
		return -1;
	}
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
		fwrite(buf, 1, n, out);
	fclose(in);
	fclose(out);
	chmod(dst, 0600);
	return 0;
}

static void bdk_prepare_fs(void)
{
	bdk_mkdir_p(ICWMP_BDK_UCI_CONFDIR);
	bdk_mkdir_p(ICWMP_BDK_DATA_DIR "/tr098");
	bdk_mkdir_p(ICWMP_BDK_TMP_DIR "/.uci");
	bdk_mkdir_p(ICWMP_BDK_TMP_DIR "/.tr098");
	bdk_mkdir_p("/var/state");

	/* first boot (or after factory reset): seed the UCI cwmp config from the
	 * read-only default shipped in the image */
	if (access(ICWMP_BDK_UCI_CONFDIR "/cwmp", F_OK) != 0) {
		if (bdk_copy_file(ICWMP_BDK_UCI_SEED, ICWMP_BDK_UCI_CONFDIR "/cwmp") == 0)
			cmsLog_notice("seeded %s from %s", ICWMP_BDK_UCI_CONFDIR "/cwmp", ICWMP_BDK_UCI_SEED);
		else
			cmsLog_error("cannot seed %s (%s)", ICWMP_BDK_UCI_CONFDIR "/cwmp", strerror(errno));
	}
	if (bootLaunched) {
		/* same role as /etc/icwmpd/.icwmpd_boot of the OpenWrt init script:
		 * BOOT event pending until delivered (removed in event.c) */
		int fd = open(ICWMP_BOOT_FLAG_FILE, O_CREAT | O_WRONLY, 0600);
		if (fd >= 0)
			close(fd);
	}
}

/* ------------------------------------------------------------------------ */
/* CMS event interest (tr69c registerInterestInEvent)                        */
/* ------------------------------------------------------------------------ */

static void bdk_register_event(CmsMsgType msgType)
{
	CmsMsgHeader msg;
	CmsRet ret;

	memset(&msg, 0, sizeof(msg));
	msg.type = CMS_MSG_REGISTER_EVENT_INTEREST;
	msg.src = EID_TR69C;
	msg.dst = EID_SMD;
	msg.flags_request = 1;
	msg.wordData = msgType;

	ret = cmsMsg_sendAndGetReply(msgHandle, &msg);
	if (ret != CMSRET_SUCCESS)
		cmsLog_error("REGISTER_EVENT_INTEREST 0x%x failed ret=%d", msgType, ret);
	else
		cmsLog_debug("REGISTER_EVENT_INTEREST 0x%x ok", msgType);
}

/* ------------------------------------------------------------------------ */
/* crash handler: backtrace into /data/icwmp/crash.log                       */
/* ------------------------------------------------------------------------ */

/* There is no gdb/core dump flow on the board, so on a fatal signal write a
 * backtrace (object+offset, resolvable with addr2line on the unstripped
 * build-tree binaries, see debug-commands.md) and re-raise the signal so the
 * kernel still reports it.  backtrace_symbols_fd() is not strictly
 * async-signal-safe, acceptable for a last-gasp debug aid. */
static void bdk_crash_handler(int sig, siginfo_t *si, void *uctx)
{
	void *frames[64];
	int n, fd;
	char line[160];

	(void)uctx;
	n = backtrace(frames, 64);
	snprintf(line, sizeof(line), "icwmpd pid %d: signal %d (si_addr %p), %d frames\n",
	         (int)getpid(), sig, si ? si->si_addr : NULL, n);

	fd = open(ICWMP_BDK_CRASH_LOG, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd >= 0) {
		(void)!write(fd, line, strlen(line));
		backtrace_symbols_fd(frames, n, fd);
		(void)!write(fd, "\n", 1);
		close(fd);
	}
	(void)!write(STDERR_FILENO, line, strlen(line));
	backtrace_symbols_fd(frames, n, STDERR_FILENO);

	signal(sig, SIG_DFL);
	raise(sig);
}

static void bdk_install_crash_handler(void)
{
	struct sigaction sa;
	static const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
	unsigned int i;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = bdk_crash_handler;
	sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
	for (i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++)
		sigaction(sigs[i], &sa, NULL);
}

/* CMS log level of this process (libcms_core, libbcm_generic_hal, the BDK
 * glue and the libtr098 backend log through cmsLog_* to syslog): every GPV
 * prints ~8 cmsLog_notice lines from phl.c, so keep it at ERR unless the UCI
 * icwmp log severity is DEBUG.  Read straight from UCI because this runs
 * before global_conf_init(); called again on every config reload
 * (icwmp_bdk_load_mode). */
static void bdk_set_cms_log_level(void)
{
	struct uci_context *c = uci_alloc_context();
	struct uci_ptr ptr;
	char *k;
	CmsLogLevel level = LOG_LEVEL_ERR;

	if (!c)
		goto out;
	k = strdup("cwmp.cpe.log_severity");
	if (k && uci_lookup_ptr(c, &ptr, k, true) == UCI_OK && (ptr.flags & UCI_LOOKUP_COMPLETE) &&
	    ptr.o && ptr.o->type == UCI_TYPE_STRING && ptr.o->v.string &&
	    strcasecmp(ptr.o->v.string, "DEBUG") == 0)
		level = LOG_LEVEL_NOTICE;
	free(k);
	uci_free_context(c);
out:
	cmsLog_setLevel(level);
}

/* ------------------------------------------------------------------------ */
/* init / cleanup                                                            */
/* ------------------------------------------------------------------------ */

int icwmp_bdk_init(void)
{
	MdmInitConfig mdmConfig;
	CmsRet ret;

	cmsLog_initWithName(EID_TR69C, "icwmpd");
	cmsLog_setHeaderMask(CMSLOG_HDRMASK_APPNAME | CMSLOG_HDRMASK_LEVEL |
	                     CMSLOG_HDRMASK_TIMESTAMP | CMSLOG_HDRMASK_LOCATION);
	cmsLog_setDestination(LOG_DEST_SYSLOG);
	bdk_install_crash_handler();

	if (shmId == UNINITIALIZED_SHM_ID) {
		cmsLog_error("no MDM shmId (-S <id>); icwmpd must be launched by tr69_md on BDK");
		return -1;
	}

	bdk_prepare_fs();
	bdk_set_cms_log_level();
	CWMP_LOG(INFO, "icwmpd " CWMP_VERSION " starting (shmId=%d, boot=%d), crash log %s",
	         shmId, bootLaunched, ICWMP_BDK_CRASH_LOG);

	/* message bus of the tr69 component (bcm_msgd started by tr69_md) */
	ret = cmsMsg_initOnBusWithTimeout(EID_TR69C, 0, TR69_MSG_BUS, 5000, &msgHandle);
	if (ret != CMSRET_SUCCESS) {
		cmsLog_error("cmsMsg_initOnBus(%s) failed ret=%d", TR69_MSG_BUS, ret);
		return -1;
	}

	if (bootLaunched) {
		/* tr69_md starts before sysmgmt_md; like tr69c, do not touch the
		 * sysmgmt MDM (DeviceInfo, IP, WiFi...) until it is fully up */
		cmsLog_notice("boot launched: waiting %d s for sysmgmt", BDK_BOOT_WAIT_SEC);
		sleep(BDK_BOOT_WAIT_SEC);
	}

	memset(&mdmConfig, 0, sizeof(mdmConfig));
	mdmConfig.eid = EID_TR69C;
	mdmConfig.accessBit = NDA_ACCESS_TR69C;
	mdmConfig.shmAttachAddr = (void *)MDM_SHM_ATTACH_ADDR_TR69;
	mdmConfig.lockKeyOffset = TR69_KEY_OFFSET;
	ret = cmsMdm_initWithConfig(&mdmConfig, msgHandle, &shmId);
	if (ret != CMSRET_SUCCESS) {
		cmsLog_error("cmsMdm_initWithConfig(shmId=%d) failed ret=%d", shmId, ret);
		cmsMsg_cleanup(&msgHandle);
		return -1;
	}
	cmsLog_notice("attached to tr69 MDM shmId=%d, data model is %s",
	              shmId, cmsMdm_isDataModelDevice2() ? "TR-181" : "TR-098");

	bdk_register_event(CMS_MSG_ACS_CONFIG_CHANGED);
	bdk_register_event(CMS_MSG_TR69C_CONFIG_CHANGED);
	bdk_register_event(CMS_MSG_TR69_ACTIVE_NOTIFICATION);
	bdk_register_event(CMS_MSG_WAN_CONNECTION_UP);

	/* tr69c exits when Device.ManagementServer.EnableCWMP is false (mainCms.c);
	 * tr69_md will relaunch us after EnableCWMP is set (ACS_CONFIG_CHANGED) */
	{
		char v[16] = "1";
		if (bdk_get_value_buf(MDM_MS "EnableCWMP", v, sizeof(v)) != 0)
			snprintf(v, sizeof(v), "1");
		if (strcmp(v, "0") == 0 || strcasecmp(v, "false") == 0) {
			cmsLog_notice("EnableCWMP is false, exiting like tr69c");
			icwmp_bdk_cleanup();
			exit(EXIT_SUCCESS);
		}
	}

	bdkTr181 = -1;                 /* force the notice on the first load */
	icwmp_bdk_load_mode();

	/* ManagementServer: MDM (WebUI / tr69_mdmcli) wins where it has a value,
	 * a value only present in UCI (uci set ... + commit on the board) is kept
	 * and pushed into the MDM so both sides agree and it survives a reboot */
	icwmp_bdk_sync_mdm_to_uci();
	if (icwmp_bdk_sync_uci_to_mdm())
		icwmp_bdk_save_config();
	return 0;
}

void icwmp_bdk_cleanup(void)
{
	if (msgHandle) {
		cmsMdm_cleanup();
		cmsMsg_cleanup(&msgHandle);
		msgHandle = NULL;
	}
}

/* ------------------------------------------------------------------------ */
/* CMS message loop, hooked into uloop                                       */
/* ------------------------------------------------------------------------ */

static void bdk_trigger_notify(void)
{
	bool send_signal = false;

	pthread_mutex_lock(&(cwmp_main.mutex_handle_notify));
	if (!cwmp_main.count_handle_notify)
		send_signal = true;
	cwmp_main.count_handle_notify++;
	pthread_mutex_unlock(&(cwmp_main.mutex_handle_notify));
	if (send_signal)
		pthread_cond_signal(&(cwmp_main.threshold_handle_notify));
}

/* Reload the icwmp config from UCI (cwmp_apply_acs_changes: global_conf_init
 * + dm_entry_reload_enabled_notify = a full data model walk).  This runs on
 * the uloop thread, so it must be serialized with the session thread exactly
 * like icwmp's own notify thread (event.c cwmp_add_notification): take
 * mutex_session_send, which the session thread holds for the whole session
 * and releases while waiting for the next one.  Without it the walk's
 * dm_ctx_clean()/dmcleanmem() frees the strings the session thread is
 * putting into the Inform (crash 2026-09-19, mxmlNewOpaque <-
 * cwmp_rpc_acs_prepare_message_inform).  Never hold bdk_lock() here: the
 * session thread takes bdk_lock inside every getter while owning
 * mutex_session_send. */
static void bdk_reload_config(const char *why)
{
	pthread_mutex_lock(&(cwmp_main.mutex_session_send));
	cmsLog_notice("%s: reloading icwmpd config", why);
	cwmp_apply_acs_changes();
	pthread_mutex_unlock(&(cwmp_main.mutex_session_send));
}

static void bdk_handle_msg(CmsMsgHeader *msg)
{
	switch ((UINT32)msg->type) {
	case CMS_MSG_ACS_CONFIG_CHANGED:
	case CMS_MSG_TR69C_CONFIG_CHANGED:
		/* tr69_md forwards this (distributeMessageToTr69c) whenever
		 * Device.ManagementServer.* changes in the MDM, including our own
		 * sync_uci_to_mdm() at start */
		if (icwmp_bdk_sync_mdm_to_uci())
			bdk_reload_config("ACS config changed in MDM");
		break;

	case CMS_MSG_TR69_ACTIVE_NOTIFICATION:
		/* a parameter with active notification changed in some component:
		 * let icwmp's notify thread diff the enabled-notify list */
		cmsLog_notice("TR69_ACTIVE_NOTIFICATION from MDM");
		bdk_trigger_notify();
		break;

	case CMS_MSG_WAN_CONNECTION_UP: {
		char *data = (msg->dataLength > 0) ? (char *)(msg + 1) : "";
		cmsLog_notice("WAN_CONNECTION_UP %s", data);
		/* BoundIfName may have been resolved now; the netlink watcher of
		 * icwmp picks the new IP up, we only refresh the interface name */
		if (icwmp_bdk_sync_mdm_to_uci())
			bdk_reload_config("WAN up");
		break;
	}

	case CMS_MSG_PING_STATE_CHANGED:
	case CMS_MSG_TRACERT_STATE_CHANGED:
		/* diag_md finished IPPing / TraceRoute (DiagnosticsState set to
		 * Requested by the ACS through IPPingDiagnostics./TraceRouteDiagnostics.)
		 * and tr69_md forwarded the pubsub event to EID_TR69C: same event as
		 * the ubus handler of the OpenWrt build (diagnostic.c) */
		cmsLog_notice("diagnostics complete (msg 0x%x) -> 8 DIAGNOSTICS COMPLETE", (UINT32)msg->type);
		cwmp_root_cause_event_ipdiagnostic();
		break;

	case CMS_MSG_SYSTEM_BOOT:
		break;

	default:
		cmsLog_debug("ignoring msg type 0x%x", (UINT32)msg->type);
		break;
	}
}

static void bdk_msg_cb(struct uloop_fd *u, unsigned int events)
{
	CmsMsgHeader *msg = NULL;

	(void)u;
	(void)events;
	/* drain everything that is queued; timeout 0 = do not block.  Only the
	 * msgHandle access is under bdk_lock, the handlers take their own locks
	 * (see bdk_reload_config) */
	for (;;) {
		CmsRet ret;

		bdk_lock();
		ret = cmsMsg_receiveWithTimeout(msgHandle, &msg, 0);
		bdk_unlock();
		if (ret != CMSRET_SUCCESS)
			break;
		bdk_handle_msg(msg);
		CMSMEM_FREE_BUF_AND_NULL_PTR(msg);
	}
}

int icwmp_bdk_uloop_register(void)
{
	SINT32 fd = CMS_INVALID_FD;

	if (!msgHandle)
		return -1;
	cmsMsg_getEventHandle(msgHandle, &fd);
	if (fd == CMS_INVALID_FD) {
		cmsLog_error("no event fd on msgHandle");
		return -1;
	}
	memset(&bdk_ufd, 0, sizeof(bdk_ufd));
	bdk_ufd.fd = fd;
	bdk_ufd.cb = bdk_msg_cb;
	uloop_fd_add(&bdk_ufd, ULOOP_READ);
	cmsLog_notice("CMS msg fd %d added to uloop", fd);
	return 0;
}

/* ------------------------------------------------------------------------ */
/* Device.ManagementServer.* <-> UCI cwmp                                    */
/*                                                                           */
/* MDM is the source of truth (WebUI, tr69_mdmcli, configure script write   */
/* there).  icwmp keeps reading its config from UCI, so the two are kept in */
/* sync at start, on CMS_MSG_ACS_CONFIG_CHANGED and after every session.    */
/* ------------------------------------------------------------------------ */

struct ms_map {
	const char *mdm;        /* leaf under Device.ManagementServer. */
	const char *uci;        /* cwmp.<section>.<option> */
	int to_mdm;             /* also pushed back UCI -> MDM after a session */
	int is_bool;
};

static const struct ms_map ms_maps[] = {
	{"URL",                           "cwmp.acs.url",                        1, 0},
	{"Username",                      "cwmp.acs.userid",                     1, 0},
	{"Password",                      "cwmp.acs.passwd",                     1, 0},
	{"PeriodicInformEnable",          "cwmp.acs.periodic_inform_enable",     1, 1},
	{"PeriodicInformInterval",        "cwmp.acs.periodic_inform_interval",   1, 0},
	{"PeriodicInformTime",            "cwmp.acs.periodic_inform_time",       1, 0},
	{"ConnectionRequestUsername",     "cwmp.cpe.userid",                     1, 0},
	{"ConnectionRequestPassword",     "cwmp.cpe.passwd",                     1, 0},
	{"CWMPRetryMinimumWaitInterval",  "cwmp.acs.retry_min_wait_interval",    1, 0},
	{"CWMPRetryIntervalMultiplier",   "cwmp.acs.retry_interval_multiplier",  1, 0},
	{"X_BROADCOM_COM_BoundIfName",    "cwmp.cpe.interface",                  0, 0},
	{NULL, NULL, 0, 0}
};


static int uci_get_str(struct uci_context *c, const char *key, char *out, size_t outlen)
{
	struct uci_ptr ptr;
	char *k = strdup(key);
	int rc = -1;

	out[0] = '\0';
	if (!k)
		return -1;
	if (uci_lookup_ptr(c, &ptr, k, true) == UCI_OK && (ptr.flags & UCI_LOOKUP_COMPLETE) &&
	    ptr.o && ptr.o->type == UCI_TYPE_STRING) {
		snprintf(out, outlen, "%s", ptr.o->v.string);
		rc = 0;
	}
	free(k);
	return rc;
}

static int uci_set_str(struct uci_context *c, const char *key, const char *value)
{
	struct uci_ptr ptr;
	char *kv;
	int rc = -1;

	if (asprintf(&kv, "%s=%s", key, value ? value : "") < 0)
		return -1;
	if (uci_lookup_ptr(c, &ptr, kv, true) == UCI_OK && uci_set(c, &ptr) == UCI_OK &&
	    uci_save(c, ptr.p) == UCI_OK)
		rc = 0;
	free(kv);
	return rc;
}

/* BDK returns booleans as 1/0 (or TRUE/FALSE from some STL), UCI cwmp
 * config uses 1/0 (periodic_inform_enable) and "enable" strings elsewhere */
static const char *norm_bool(const char *v)
{
	if (!v)
		return "0";
	if (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0)
		return "1";
	return "0";
}

/* "Any_WAN" / "" in X_BROADCOM_COM_BoundIfName means: not bound, keep the
 * default interface from the seed config */
static int usable_ifname(const char *v)
{
	return v && v[0] && strcasecmp(v, "Any_WAN") != 0 && strcasecmp(v, "Any_LAN") != 0;
}

int icwmp_bdk_sync_mdm_to_uci(void)
{
	struct uci_context *c;
	struct uci_ptr ptr;
	char path[128], cur[512], vbuf[512];
	const char *v;
	const struct ms_map *m;
	int changed = 0;

	c = uci_alloc_context();
	if (!c)
		return 0;

	for (m = ms_maps; m->mdm; m++) {
		snprintf(path, sizeof(path), MDM_MS "%s", m->mdm);
		/* bdk_get_value_buf, not bdk_get_value: this also runs on the uloop
		 * thread and must not touch the dmmem arena of libtr098 */
		if (bdk_get_value_buf(path, vbuf, sizeof(vbuf)) != 0)
			continue;                      /* param not in this image, skip */
		v = m->is_bool ? norm_bool(vbuf) : vbuf;
		if (strcmp(m->uci, "cwmp.cpe.interface") == 0 && !usable_ifname(v))
			continue;
		if (uci_get_str(c, m->uci, cur, sizeof(cur)) == 0 && strcmp(cur, v) == 0)
			continue;
		/* MDM empty (factory default, ACS never configured through the WebUI)
		 * but UCI has a value: keep UCI, sync_uci_to_mdm() pushes it up */
		if (v[0] == '\0' && cur[0] != '\0')
			continue;
		if (uci_set_str(c, m->uci, v) == 0) {
			changed = 1;
			/* never log ACS/CR passwords */
			if (strstr(m->uci, "passwd"))
				cmsLog_notice("sync MDM->UCI %s (masked)", m->uci);
			else
				cmsLog_notice("sync MDM->UCI %s=%s", m->uci, v);
		}
	}
	if (changed) {
		char *k = strdup("cwmp");
		if (k && uci_lookup_ptr(c, &ptr, k, true) == UCI_OK)
			uci_commit(c, &ptr.p, false);
		free(k);
	}
	uci_free_context(c);
	return changed;
}

int icwmp_bdk_sync_uci_to_mdm(void)
{
	struct uci_context *c;
	char path[128], cur[512], v[512];
	const struct ms_map *m;
	int changed = 0;

	c = uci_alloc_context();
	if (!c)
		return 0;

	for (m = ms_maps; m->mdm; m++) {
		if (!m->to_mdm)
			continue;
		if (uci_get_str(c, m->uci, cur, sizeof(cur)) != 0)
			continue;
		snprintf(path, sizeof(path), MDM_MS "%s", m->mdm);
		if (bdk_get_value_buf(path, v, sizeof(v)) != 0)
			continue;
		if (m->is_bool && strcmp(norm_bool(v), norm_bool(cur)) == 0)
			continue;
		if (!m->is_bool && strcmp(v, cur) == 0)
			continue;
		if (bdk_set_value_now(path, NULL, m->is_bool ? norm_bool(cur) : cur) == 0) {
			changed = 1;
			if (strstr(m->uci, "passwd"))
				cmsLog_notice("sync UCI->MDM %s (masked)", path);
			else
				cmsLog_notice("sync UCI->MDM %s=%s", path, cur);
		}
	}
	uci_free_context(c);
	return changed;
}

/* ------------------------------------------------------------------------ */
/* end of session / actions                                                  */
/* ------------------------------------------------------------------------ */

/* tr69c: acsDisconnect(eAcsDone) -> saveConfigurations() ->
 * cmsMgm_saveConfigToFlash() (remote components first, local last) */
void icwmp_bdk_save_config(void)
{
	char *out = NULL;
	BcmRet ret;

	bdk_lock();
	ret = bcm_generic_databaseOp(BCM_DATABASEOP_SAVECONFIG, NULL, &out);
	bdk_unlock();
	if (ret != BCMRET_SUCCESS)
		cmsLog_error("saveConfig failed ret=%d", ret);
	else
		cmsLog_notice("config saved to flash");
	if (out)
		bcm_generic_freeDatabaseOutput(&out);
}

/* The two ManagementServer leaves icwmpd owns (they never come from the MDM):
 * ParameterKey (UCI cwmp.acs.ParameterKey, written by libtr098 on every
 * SPV/AddObject/DeleteObject) and ConnectionRequestURL (netlink IP of
 * cwmp.cpe.interface + connection request port).  Pushed into the MDM so the
 * WebUI / tr69_mdmcli / a TR-181 GPV through the generic HAL show what the
 * ACS was told (in TR-181 mode libtr098 also overrides them on read). */
int icwmp_bdk_sync_uci_only_to_mdm(void)
{
	struct uci_context *c;
	char key[512] = "", url[128] = "", cur[512];
	char cr_host[128] = "", cr_port[16] = "";
	int changed = 0, port;

	c = uci_alloc_context();
	if (c) {
		uci_get_str(c, "cwmp.acs.ParameterKey", key, sizeof(key));
		/* NAT override, same rule as libtr098 managementserver.c (BDK) */
		uci_get_str(c, "cwmp.cpe.cr_host", cr_host, sizeof(cr_host));
		uci_get_str(c, "cwmp.cpe.cr_port", cr_port, sizeof(cr_port));
		uci_free_context(c);
	}
	port = cr_port[0] ? atoi(cr_port) : cwmp_main.conf.connection_request_port;
	if (bdk_get_value_buf(MDM_MS "ParameterKey", cur, sizeof(cur)) == 0 && strcmp(cur, key) != 0 &&
	    bdk_set_value_now(MDM_MS "ParameterKey", NULL, key) == 0) {
		cmsLog_notice("sync UCI->MDM ParameterKey='%s'", key);
		changed = 1;
	}

	if (cr_host[0])
		snprintf(url, sizeof(url), strchr(cr_host, ':') && cr_host[0] != '[' ? "http://[%s]:%d/" : "http://%s:%d/", cr_host, port);
	else if (cwmp_main.conf.ip && cwmp_main.conf.ip[0])
		snprintf(url, sizeof(url), "http://%s:%d/", cwmp_main.conf.ip, port);
	else if (cwmp_main.conf.ipv6 && cwmp_main.conf.ipv6[0])
		snprintf(url, sizeof(url), "http://[%s]:%d/", cwmp_main.conf.ipv6, port);
	if (url[0] && bdk_get_value_buf(MDM_MS "ConnectionRequestURL", cur, sizeof(cur)) == 0 &&
	    strcmp(cur, url) != 0 && bdk_set_value_now(MDM_MS "ConnectionRequestURL", NULL, url) == 0) {
		cmsLog_notice("sync UCI->MDM ConnectionRequestURL=%s", url);
		changed = 1;
	}
	return changed;
}

void icwmp_bdk_end_session(void)
{
	int reload = 0;

	if (!msgHandle)
		return;

	bdk_lock();
	if (bdkTr181) {
		/* TR-181: the ACS wrote Device.ManagementServer.* straight into the
		 * MDM (libtr098 dmproxy_bdk.c).  Pull it into the UCI config icwmpd
		 * reads (never the other way round: UCI is stale here) and reload
		 * the config below if something changed.  CMS_MSG_ACS_CONFIG_CHANGED
		 * from the MDM does the same asynchronously; doing it here makes the
		 * new URL/username/interval certain for the next session. */
		icwmp_bdk_sync_uci_only_to_mdm();
		reload = icwmp_bdk_sync_mdm_to_uci();
	} else {
		/* TR-098: ManagementServer.* changed by SPV is stored in UCI by
		 * libtr098's managementserver.c: reflect it in the MDM first */
		icwmp_bdk_sync_uci_to_mdm();
		icwmp_bdk_sync_uci_only_to_mdm();
	}
	bdk_unlock();
	icwmp_bdk_save_config();
	if (reload) {
		/* session thread, same as the END_SESSION_RELOAD branch of
		 * run_session_end_func() that ran just before us */
		cmsLog_notice("ACS changed ManagementServer.* in the MDM: reloading icwmpd config");
		cwmp_apply_acs_changes();
	}
}

void icwmp_bdk_reboot(const char *requestor)
{
	cmsLog_notice("reboot requested by %s", requestor ? requestor : "icwmpd");
	sync();
	bcmUtl_loggedBusybox_reboot(requestor ? requestor : "icwmpd", REBOOT_REASON_MANAGEMENT_REBOOT);
}

void icwmp_bdk_factory_reset(void)
{
	CmsRet ret;

	cmsLog_notice("factory reset: invalidating config flash");
	bdk_lock();
	ret = cmsLck_acquireLockWithTimeout(BDK_LOCK_TIMEOUT_MS);
	if (ret == CMSRET_SUCCESS) {
		cmsMgm_invalidateConfigFlash();
		cmsLck_releaseLock();
	} else {
		cmsLog_error("could not get MDM lock for factory reset, ret=%d", ret);
	}
	bdk_unlock();

	/* icwmp's own persistent state must go too, otherwise the old ACS URL /
	 * notifications / dmmap survive the reset */
	unlink(ICWMP_BDK_UCI_CONFDIR "/cwmp");
	unlink(ICWMP_BDK_DATA_DIR "/tr098/.dm_enabled_notify");
	unlink(CWMP_BKP_FILE);
	unlink(ICWMP_BOOT_FLAG_FILE);

	icwmp_bdk_reboot("icwmpd-factoryreset");
}

/* read a whole file into a cmsMem buffer (firmware images are tens of MB,
 * tr69c also holds the entire image in RAM before cmsImg_writeImageIncremental) */
static char *bdk_read_file(const char *file, UINT32 *len)
{
	struct stat st;
	char *buf;
	FILE *fp;

	*len = 0;
	if (stat(file, &st) != 0 || st.st_size <= 0)
		return NULL;
	buf = cmsMem_alloc((UINT32)st.st_size, 0);
	if (!buf)
		return NULL;
	fp = fopen(file, "r");
	if (!fp) {
		cmsMem_free(buf);
		return NULL;
	}
	if (fread(buf, 1, (size_t)st.st_size, fp) != (size_t)st.st_size) {
		fclose(fp);
		cmsMem_free(buf);
		return NULL;
	}
	fclose(fp);
	*len = (UINT32)st.st_size;
	return buf;
}

/* FileType "1 Firmware Upgrade Image": same sequence as tr69c
 * bcmWrapperCms.c downloadComplete(): validateImage -> writeImageIncremental
 * -> reboot with REBOOT_REASON_SOFTWARE_UPGRADE.
 * TODO(phase 4): rutFwImg_updateFirmwareObject() (Device.DeviceInfo.
 * FirmwareImage bookkeeping, cms_core private) is not called yet. */
const char *icwmp_bdk_apply_firmware(const char *file)
{
	UINT32 len = 0;
	char *buf;
	CmsImageFormat format;
	CmsRet ret;

	buf = bdk_read_file(file, &len);
	if (!buf) {
		cmsLog_error("cannot read downloaded image %s", file);
		return "9010";
	}
	bdk_lock();
	format = cmsImg_validateImage(buf, len, msgHandle);
	if (format != CMS_IMAGE_FORMAT_BROADCOM && format != CMS_IMAGE_FORMAT_FLASH) {
		bdk_unlock();
		cmsMem_free(buf);
		cmsLog_error("downloaded file is not a firmware image (format=%d)", format);
		return "9018";
	}
	ret = cmsImg_writeImageIncremental(buf, len);
	bdk_unlock();
	cmsMem_free(buf);
	unlink(file);
	if (ret != CMSRET_SUCCESS) {
		cmsLog_error("writeImageIncremental failed ret=%d", ret);
		return "9017";
	}
	cmsLog_notice("firmware written, rebooting");
	sync();
	bcmUtl_loggedBusybox_reboot("icwmpd", REBOOT_REASON_SOFTWARE_UPGRADE);
	return "0";
}

/* FileType "3 Vendor Configuration File": Broadcom XML config */
const char *icwmp_bdk_apply_vendor_config(const char *file)
{
	UINT32 len = 0;
	char *buf;
	CmsImageFormat format;
	CmsRet ret;

	buf = bdk_read_file(file, &len);
	if (!buf)
		return "9010";
	bdk_lock();
	format = cmsImg_validateImage(buf, len, msgHandle);
	if (format != CMS_IMAGE_FORMAT_XML_CFG) {
		bdk_unlock();
		cmsMem_free(buf);
		cmsLog_error("downloaded file is not a CMS XML config (format=%d)", format);
		return "9018";
	}
	ret = cmsImg_writeValidatedImage_noReboot(buf, len, format, msgHandle);
	bdk_unlock();
	cmsMem_free(buf);
	unlink(file);
	if (ret != CMSRET_SUCCESS) {
		cmsLog_error("config write failed ret=%d", ret);
		return "9017";
	}
	cmsLog_notice("vendor config written, rebooting to apply");
	sync();
	bcmUtl_loggedBusybox_reboot("icwmpd", REBOOT_REASON_MANAGEMENT_REBOOT);
	return "0";
}

/* ------------------------------------------------------------------------ */
/* sdk/sdk.h hooks                                                */
/* ------------------------------------------------------------------------ */

int icwmp_platform_init(void)
{
	return icwmp_bdk_init();
}

int icwmp_platform_config_reload(void)
{
	/* CMS_MSG_ACS_CONFIG_CHANGED already mirrored the MDM into the cwmp
	 * config (bdk_handle_msg); nothing to pull here */
	return 0;
}

void icwmp_platform_config_reloaded(struct cwmp *cwmp)
{
	/* the Inform DeviceId is cached at init; on BDK it can be overridden
	 * from UCI (cwmp.cpe.manufacturer/oui/product_class/serial_number, see
	 * libtr098 tr098/bdk/deviceinfo_bdk.c), so a "ubus call tr069 command
	 * reload" must pick the new identity up as well */
	FREE(cwmp->deviceid.manufacturer);
	FREE(cwmp->deviceid.oui);
	FREE(cwmp->deviceid.serialnumber);
	FREE(cwmp->deviceid.productclass);
	FREE(cwmp->deviceid.softwareversion);
	cwmp_get_deviceid(cwmp);
	/* cwmp.cpe.datamodel may have been switched (tr098 <-> tr181) */
	icwmp_bdk_load_mode();
}

int icwmp_platform_uloop_register(void)
{
	return icwmp_bdk_uloop_register();
}

void icwmp_platform_end_session(void)
{
	icwmp_bdk_end_session();
}

void icwmp_platform_cleanup(void)
{
	icwmp_bdk_cleanup();
}
