/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Broadcom BDK replacement of external.c.
 *
 *	external.c forks /usr/sbin/icwmp (shell, OpenWrt) and exchanges JSON over
 *	pipes for every action that touches the system: reboot, factory reset,
 *	download/upload of files, applying a downloaded firmware/config, software
 *	modules.  BDK has none of that shell tooling, so the same entry points are
 *	implemented here in C on top of icwmp_bdk.c (which calls the Broadcom
 *	libraries the way tr69c does).  The public API (inc/external.h) and the
 *	fault protocol ("0" = ok, "9xxx" = CWMP fault) are unchanged so xml.c and
 *	cwmp.c do not know the difference.
 *
 *	Not supported on BDK (return 9000/9001 faults): ChangeDUState (software
 *	modules, needs Broadcom modsw/EE), upload of system logs, web content.
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <curl/curl.h>

#include "cwmp.h"
#include "external.h"
#include "log.h"
#include "icwmp_bdk.h"

char *external_MethodFault = NULL;
char *external_MethodName = NULL;
char *external_MethodVersion = NULL;
char *external_MethodUUID = NULL;
char *external_MethodENV = NULL;

/* ---- fault plumbing: identical to external.c so xml.c keeps working ------ */

void external_downloadFaultResp(char *fault_code)
{
	FREE(external_MethodFault);
	external_MethodFault = fault_code ? strdup(fault_code) : NULL;
}

void external_fetch_downloadFaultResp(char **fault)
{
	*fault = external_MethodFault;
	external_MethodFault = NULL;
}

void external_uploadFaultResp(char *fault_code)
{
	FREE(external_MethodFault);
	external_MethodFault = fault_code ? strdup(fault_code) : NULL;
}

void external_fetch_uploadFaultResp(char **fault)
{
	*fault = external_MethodFault;
	external_MethodFault = NULL;
}

void external_uninstallFaultResp(char *fault_code)
{
	FREE(external_MethodFault);
	external_MethodFault = fault_code ? strdup(fault_code) : NULL;
}

void external_fetch_uninstallFaultResp(char **fault)
{
	*fault = external_MethodFault;
	external_MethodFault = NULL;
}

void external_du_change_stateFaultResp(char *fault_code, char *version, char *name, char *uuid, char *env)
{
	FREE(external_MethodFault);
	external_MethodFault = fault_code ? strdup(fault_code) : NULL;
	FREE(external_MethodVersion);
	external_MethodVersion = version ? strdup(version) : NULL;
	FREE(external_MethodName);
	external_MethodName = name ? strdup(name) : NULL;
	FREE(external_MethodUUID);
	external_MethodUUID = uuid ? strdup(uuid) : NULL;
	FREE(external_MethodENV);
	external_MethodENV = env ? strdup(env) : NULL;
}

void external_fetch_du_change_stateFaultResp(char **fault, char **version, char **name, char **uuid, char **env)
{
	*fault = external_MethodFault;
	external_MethodFault = NULL;
	*version = external_MethodVersion;
	external_MethodVersion = NULL;
	*name = external_MethodName;
	external_MethodName = NULL;
	*uuid = external_MethodUUID;
	external_MethodUUID = NULL;
	*env = external_MethodENV;
	external_MethodENV = NULL;
}

/* ---- lifecycle: nothing to fork on BDK ---------------------------------- */

void external_init(void)
{
	/* results are produced synchronously, there is no child to talk to */
}

void external_exit(void)
{
}

int external_handle_action(int (*external_handler)(char *msg))
{
	/* external.c would now read the JSON reply of the script and feed it to
	 * external_handler; here every action already stored its result with
	 * external_*FaultResp(), so there is nothing left to do */
	(void)external_handler;
	return 0;
}

/* ---- simple actions ------------------------------------------------------ */

int external_simple(char *command, char *arg, int c)
{
	(void)c;
	CWMP_LOG(INFO, "executing %s request", command);

	if (strcmp(command, "reboot") == 0) {
		icwmp_bdk_end_session();       /* persist before going down */
		icwmp_bdk_reboot(arg && *arg ? "icwmpd-acs" : "icwmpd");
	} else if (strcmp(command, "factory_reset") == 0 ||
	           strcmp(command, "factory_reset_soft") == 0) {
		icwmp_bdk_factory_reset();
	} else if (strcmp(command, "end_session") == 0) {
		/* END_SESSION_EXTERNAL_ACTION: nothing platform specific to run,
		 * flash save is done by icwmp_bdk_end_session() in run_session_end_func */
	} else {
		CWMP_LOG(WARNING, "unsupported external command %s on BDK", command);
	}
	return 0;
}

/* ---- download (libcurl, into ICWMP_BDK_DOWNLOAD_FILE) ---------------------- */

static size_t dl_write_cb(void *ptr, size_t size, size_t nmemb, void *stream)
{
	return fwrite(ptr, size, nmemb, (FILE *)stream);
}

int external_download(char *url, char *size, char *type, char *user, char *pass, time_t c)
{
	CURL *curl;
	CURLcode res;
	FILE *fp;
	long http_code = 0;
	struct cwmp *cwmp = &cwmp_main;
	struct config *conf = &(cwmp->conf);
	const char *fault = "0";
	struct stat st;

	(void)c;
	CWMP_LOG(INFO, "executing download url '%s' type '%s' size %s", url, type ? type : "", size ? size : "?");

	/* only what BDK can apply: 1 firmware, 3 vendor config */
	if (!type || (type[0] != '1' && type[0] != '3')) {
		external_downloadFaultResp("9010");
		return 0;
	}

	mkdir(ICWMP_BDK_TMP_DIR, 0755);
	fp = fopen(ICWMP_BDK_DOWNLOAD_FILE, "w");
	if (!fp) {
		CWMP_LOG(ERROR, "cannot create %s: %s", ICWMP_BDK_DOWNLOAD_FILE, strerror(errno));
		external_downloadFaultResp("9010");
		return 0;
	}

	curl = curl_easy_init();
	if (!curl) {
		fclose(fp);
		external_downloadFaultResp("9010");
		return 0;
	}
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 120L);
	if (user && *user) {
		curl_easy_setopt(curl, CURLOPT_USERNAME, user);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, pass ? pass : "");
		curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	}
	/* same TLS policy as the ACS session: verify unless insecure_enable */
	if (conf->https_ssl_capath && conf->https_ssl_capath[0])
		curl_easy_setopt(curl, CURLOPT_CAPATH, conf->https_ssl_capath);
	if (conf->insecure_enable) {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}

	res = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_easy_cleanup(curl);
	fclose(fp);

	if (res != CURLE_OK) {
		CWMP_LOG(ERROR, "download failed: %s (http %ld)", curl_easy_strerror(res), http_code);
		if (http_code == 401 || http_code == 403)
			fault = "9012";
		else if (res == CURLE_COULDNT_RESOLVE_HOST || res == CURLE_COULDNT_CONNECT)
			fault = "9015";
		else if (res == CURLE_UNSUPPORTED_PROTOCOL || res == CURLE_URL_MALFORMAT)
			fault = "9013";
		else
			fault = "9010";
		unlink(ICWMP_BDK_DOWNLOAD_FILE);
	} else if (stat(ICWMP_BDK_DOWNLOAD_FILE, &st) != 0 || st.st_size == 0) {
		fault = "9016";
		unlink(ICWMP_BDK_DOWNLOAD_FILE);
	} else if (size && atol(size) > 0 && st.st_size != atol(size)) {
		CWMP_LOG(ERROR, "downloaded %ld bytes, ACS announced %s", (long)st.st_size, size);
		fault = "9017";
		unlink(ICWMP_BDK_DOWNLOAD_FILE);
	}

	external_downloadFaultResp((char *)fault);
	return 0;
}

int external_upload(char *url, char *type, char *user, char *pass, char *name)
{
	(void)url; (void)type; (void)user; (void)pass; (void)name;
	CWMP_LOG(WARNING, "Upload RPC not supported on BDK yet");
	external_uploadFaultResp("9000");
	return 0;
}

/* ---- apply --------------------------------------------------------------- */

int external_apply(char *action, char *arg, time_t c)
{
	(void)c;
	CWMP_LOG(INFO, "executing apply %s (%s)", action, arg ? arg : "");

	if (strcmp(action, "download") == 0) {
		const char *fault;
		if (arg && arg[0] == '1')
			fault = icwmp_bdk_apply_firmware(ICWMP_BDK_DOWNLOAD_FILE);
		else if (arg && arg[0] == '3')
			fault = icwmp_bdk_apply_vendor_config(ICWMP_BDK_DOWNLOAD_FILE);
		else
			fault = "9010";
		external_downloadFaultResp((char *)fault);
	} else if (strcmp(action, "notification") == 0 || strcmp(action, "value") == 0) {
		/* nothing to do: libtr098 BDK backend applied through the MDM */
	}
	return 0;
}

/* ---- software modules: not available on BDK ------------------------------ */

int external_change_du_state_install(char *url, char *uuid, char *user, char *pass, char *env)
{
	(void)url; (void)user; (void)pass;
	external_du_change_stateFaultResp("9001", NULL, NULL, uuid, env);
	return 0;
}

int external_change_du_state_update(char *uuid, char *url, char *version, char *user, char *pass)
{
	(void)url; (void)user; (void)pass;
	external_du_change_stateFaultResp("9001", version, NULL, uuid, NULL);
	return 0;
}

int external_change_du_state_uninstall(char *name, char *env)
{
	external_du_change_stateFaultResp("9001", NULL, name, NULL, env);
	return 0;
}
