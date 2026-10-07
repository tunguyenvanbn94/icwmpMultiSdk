/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.UserInterface.X_AIS_WebUserInfo. -- WebUI
 *	accounts, remote access and the operator's WebUI settings, ported from
 *	functions/tr098/X_AIS_WebUserInfo.
 *
 *	  RemoteAccess, RemoteAccessTimeout   remoteaccess.remoteaccess, the
 *	      service restarted at the end of the session
 *	  AdminName/Password                  account.admin
 *	  SuperAdminName/Password/Security/Enable  account.root
 *	  Captcha_enable                      clay.captcha.enabled
 *	  AvailableLanguages, CurrentLanguage clay.language; CurrentLanguage only
 *	      checks the value against the list, nothing is stored (the product
 *	      does not switch language from the ACS)
 *	  SuperAdminAccessList                remoteaccess.remoteaccess list on
 *	      read; written through "ubus call hni setAISWhiteList"
 *	  WebIp                               the address of the netifd interface
 *	      on the default route's device
 *
 *	Setters create the named section when it is missing, like the shell's
 *	"uci set <pkg>.<section>=<type>", and do nothing when the value is
 *	already there.  Passwords read as "".
 *
 *	Differences from the shell, all on purpose:
 *	  - the commit is the engine's and service restarts are queued for the
 *	    end of the session (the shell committed and restarted inside each
 *	    setter, so a failed leaf of the same SPV did not roll them back);
 *	  - the shell compared a new password with the factory default when none
 *	    was stored, to skip the write; that default is not repeated here, so
 *	    setting the factory password on a device without one stores it;
 *	  - WebIp is applied at the end of the session (changing the WAN address
 *	    inside the session cut the session it was set in), and the default
 *	    route's device comes from /proc/net/route, where the shell's
 *	    "awk '{print $5}'" of "ip route show default" picked "link" for a
 *	    route without a gateway;
 *	  - setAISWhiteList: no reply, or a reply that is not JSON, is 9007 like
 *	    a failed ubus call; the shell made the empty reply of a successful
 *	    call 9002.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <json-c/json.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmubus.h"
#include "dmmem.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define RA_PACKAGE	"remoteaccess"
#define RA_SECTION	"remoteaccess"
#define RA_RESTART	"/etc/init.d/remoteaccess restart"

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/* "$(uci get pkg.sec.opt)" with the shell's ${v:-default} */
static char *wui_get(const char *package, const char *section, const char *option, char *dflt)
{
	char *v = mtk_uci(package, section, option);

	return *v ? v : dflt;
}

/* "[ -z $(uci get pkg.sec) ] && uci set pkg.sec=type", then the option */
static void wui_set(const char *package, const char *section, const char *type,
		    const char *option, const char *value)
{
	char *t = NULL;

	dmuci_get_section_type((char *)package, (char *)section, &t);
	if (!t || !*t)
		dmuci_set_value((char *)package, (char *)section, "", (char *)type);
	dmuci_set_value((char *)package, (char *)section, (char *)option, (char *)value);
}

/* normalize_bool_01: case-insensitive 1/true and 0/false, -1 otherwise */
static int wui_bool01(const char *value)
{
	char low[8];
	size_t i;

	if (!value || strlen(value) >= sizeof(low))
		return -1;
	for (i = 0; value[i]; i++)
		low[i] = (char)tolower((unsigned char)value[i]);
	low[i] = '\0';
	if (strcmp(low, "1") == 0 || strcmp(low, "true") == 0)
		return 1;
	if (strcmp(low, "0") == 0 || strcmp(low, "false") == 0)
		return 0;
	return -1;
}

/* "for entry in $list; ... ${a},${entry}": the words of a UCI value
 * (a list reads space separated), joined with sep */
static char *wui_words(const char *v, char sep)
{
	char *out = dmstrdup(v), *w = out;
	const char *p = v;
	int first = 1;

	if (!out)
		return "";
	while (*p) {
		while (*p && isspace((unsigned char)*p))
			p++;
		if (!*p)
			break;
		if (!first)
			*w++ = sep;
		first = 0;
		while (*p && !isspace((unsigned char)*p))
			*w++ = *p++;
	}
	*w = '\0';
	return out;
}

/* ------------------------------------------------------------------ */
/* remote access                                                       */
/* ------------------------------------------------------------------ */

static int get_wui_remote_access(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci(RA_PACKAGE, RA_SECTION, "enabled"), "1") == 0 ? "true" : "false";
	return 0;
}

static int set_wui_remote_access(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = wui_bool01(value);

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	/* no "unchanged" shortcut in the shell here: always rewritten, always
	 * restarted */
	wui_set(RA_PACKAGE, RA_SECTION, "remoteaccess", "enabled", b ? "1" : "0");
	mtk_apply_service_once(RA_RESTART);
	return 0;
}

static int get_wui_remote_timeout(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = wui_get(RA_PACKAGE, RA_SECTION, "timeout", "3600");
	return 0;
}

static int set_wui_remote_timeout(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!value || !*value)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (strcmp(wui_get(RA_PACKAGE, RA_SECTION, "timeout", "3600"), value) == 0)
		return 0;
	wui_set(RA_PACKAGE, RA_SECTION, "remoteaccess", "timeout", value);
	mtk_apply_service_once(RA_RESTART);
	return 0;
}

/* ------------------------------------------------------------------ */
/* accounts                                                            */
/* ------------------------------------------------------------------ */

/* a non-empty value, written to account.<section>.<option> unless it is the
 * current one (dflt: what the shell read for an unset option, NULL for the
 * passwords, see the header) */
static int wui_set_account(const char *section, const char *option, const char *dflt,
			   const char *value, size_t max, int action)
{
	char *current;

	if (!value || !*value)
		return FAULT_9007;
	if (max && strlen(value) > max)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	current = mtk_uci("account", section, option);
	if (!*current && dflt)
		current = (char *)dflt;
	if (strcmp(current, value) == 0)
		return 0;
	wui_set("account", section, "account", option, value);
	return 0;
}

static int get_wui_admin_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = wui_get("account", "admin", "username", "admin");
	return 0;
}

static int set_wui_admin_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return wui_set_account("admin", "username", "admin", value, 0, action);
}

static int get_wui_password(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "";
	return 0;
}

static int set_wui_admin_password(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return wui_set_account("admin", "password", NULL, value, 32, action);
}

static int get_wui_super_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = wui_get("account", "root", "username", "awnfibre");
	return 0;
}

static int set_wui_super_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return wui_set_account("root", "username", "awnfibre", value, 0, action);
}

static int set_wui_super_password(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return wui_set_account("root", "password", NULL, value, 32, action);
}

static int get_wui_super_security(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci("account", "root", "superadminsecurity"), "1") == 0 ? "true" : "false";
	return 0;
}

static int set_wui_super_security(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = wui_bool01(value);

	if (b < 0)
		return FAULT_9007;
	return wui_set_account("root", "superadminsecurity", "1", b ? "1" : "0", 0, action);
}

static int get_wui_super_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci("account", "root", "superadminenable"), "1") == 0 ? "1" : "0";
	return 0;
}

static int set_wui_super_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!value || (strcmp(value, "0") != 0 && strcmp(value, "1") != 0))
		return FAULT_9007;
	return wui_set_account("root", "superadminenable", "1", value, 0, action);
}

/* ------------------------------------------------------------------ */
/* captcha, language                                                   */
/* ------------------------------------------------------------------ */

static int get_wui_captcha(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci("clay", "captcha", "enabled"), "1") == 0 ? "true" : "false";
	return 0;
}

static int set_wui_captcha(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *v;

	if (action == VALUECHECK)
		return 0;	/* the boolean check in front is the only one */
	/* case "$1" in 1|true|True|TRUE) 1 ;; *) 0 */
	v = (value && (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
		       strcmp(value, "True") == 0 || strcmp(value, "TRUE") == 0)) ? "1" : "0";
	if (strcmp(wui_get("clay", "captcha", "enabled", "0"), v) == 0)
		return 0;
	wui_set("clay", "captcha", "captcha", "enabled", v);
	return 0;
}

static int get_wui_languages(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci("clay", "language", "available");
	return 0;
}

static int get_wui_language(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci("clay", "language", "current");
	return 0;
}

/* validation only: the product does not change language from the ACS */
static int set_wui_language(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *list, *w;
	size_t n;

	if (action != VALUECHECK)
		return 0;
	if (!value || !*value)
		return FAULT_9007;
	list = wui_words(mtk_uci("clay", "language", "available"), ' ');
	if (!*list)
		return FAULT_9001;	/* E_REQUEST_DENIED */
	n = strlen(value);
	for (w = list; *w; ) {
		size_t l = strcspn(w, " ");

		if (l == n && strncmp(w, value, n) == 0)
			return 0;
		w += l;
		if (*w)
			w++;
	}
	return FAULT_9007;
}

/* ------------------------------------------------------------------ */
/* super admin access list                                             */
/* ------------------------------------------------------------------ */

static int get_wui_access_list(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = wui_words(mtk_uci(RA_PACKAGE, RA_SECTION, "SuperAdminAccessList"), ',');
	return 0;
}

/* hni owns the whitelist: set it, check its result, reload it -- in the
 * setter, as the shell did */
static int set_wui_access_list(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *reload_argv[] = { "ubus", "call", "hni", "reloadAISWhiteList", NULL };
	json_object *payload, *reply, *result;
	const char *res;
	char *out;
	int flags = JSON_C_TO_STRING_PLAIN, fault = 0;

	if (action == VALUECHECK)
		return 0;
#ifdef JSON_C_TO_STRING_NOSLASHESCAPE
	flags |= JSON_C_TO_STRING_NOSLASHESCAPE;
#endif
	payload = json_object_new_object();
	if (!payload)
		return FAULT_9002;
	json_object_object_add(payload, "whiteList", json_object_new_string(value ? value : ""));
	json_object_object_add(payload, "jsonResult", json_object_new_boolean(1));
	{
		char *set_argv[] = { "ubus", "call", "hni", "setAISWhiteList",
				     (char *)json_object_to_json_string_ext(payload, flags), NULL };

		out = mtk_exec(set_argv);
	}
	json_object_put(payload);
	reply = *out ? json_tokener_parse(out) : NULL;
	if (!reply)
		return FAULT_9007;	/* the ubus call failed */
	if (!json_object_object_get_ex(reply, "result", &result) ||
	    !(res = json_object_get_string(result)) || strcmp(res, "0") != 0)
		fault = FAULT_9002;	/* E_INTERNAL_ERROR */
	json_object_put(reply);
	if (fault)
		return fault;
	mtk_exec(reload_argv);
	return 0;
}

/* ------------------------------------------------------------------ */
/* WebIp                                                               */
/* ------------------------------------------------------------------ */

/* device of the first IPv4 default route, else of the first IPv6 one ("" when
 * there is none) -- "ip route show default", then "ip -6 route show default" */
static char *wui_default_dev(void)
{
	char line[512], dev[64], dst[64], mask[64];
	unsigned int flags;
	FILE *f;

	f = fopen("/proc/net/route", "r");
	if (f) {
		while (fgets(line, sizeof(line), f)) {
			if (sscanf(line, "%63s %63s %*s %x %*s %*s %*s %63s", dev, dst, &flags, mask) == 4 &&
			    strcmp(dst, "00000000") == 0 && strcmp(mask, "00000000") == 0 && (flags & 0x1)) {
				fclose(f);
				return dmstrdup(dev);
			}
		}
		fclose(f);
	}
	f = fopen("/proc/net/ipv6_route", "r");
	if (f) {
		char plen[8];

		while (fgets(line, sizeof(line), f)) {
			if (sscanf(line, "%63s %7s %*s %*s %*s %*s %*s %*s %x %63s", dst, plen, &flags, dev) == 4 &&
			    strspn(dst, "0") == 32 && strcmp(plen, "00") == 0 && (flags & 0x1) &&
			    strcmp(dev, "lo") != 0) {
				fclose(f);
				return dmstrdup(dev);
			}
		}
		fclose(f);
	}
	return "";
}

static const char *wui_first_address(json_object *iface, const char *family)
{
	json_object *arr, *a, *addr;

	if (!json_object_object_get_ex(iface, family, &arr) ||
	    json_object_get_type(arr) != json_type_array || json_object_array_length(arr) < 1)
		return NULL;
	a = json_object_array_get_idx(arr, 0);
	if (!a || !json_object_object_get_ex(a, "address", &addr))
		return NULL;
	return json_object_get_string(addr);
}

static int get_wui_web_ip(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *dev = wui_default_dev();
	json_object *res = NULL, *ifaces, *iface, *l3;
	size_t i, n;

	*value = "0.0.0.0";
	if (!*dev)
		return 0;
	dmubus_call("network.interface", "dump", UBUS_ARGS{}, 0, &res);
	if (!res || !json_object_object_get_ex(res, "interface", &ifaces) ||
	    json_object_get_type(ifaces) != json_type_array)
		return 0;
	n = json_object_array_length(ifaces);
	for (i = 0; i < n; i++) {
		const char *addr, *l3dev;

		iface = json_object_array_get_idx(ifaces, i);
		if (!iface || !json_object_object_get_ex(iface, "l3_device", &l3) ||
		    !(l3dev = json_object_get_string(l3)) || strcmp(l3dev, dev) != 0)
			continue;
		addr = wui_first_address(iface, "ipv4-address");
		if (!addr || !*addr)
			addr = wui_first_address(iface, "ipv6-address");
		if (addr && *addr) {
			*value = dmstrdup(addr);
			break;
		}
	}
	return 0;
}

static int set_wui_web_ip(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char cmd[256];
	char *dev;

	/* no address is longer; the shell had no limit and ifconfig failed */
	if (!value || !*value || strlen(value) > 64)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dev = wui_default_dev();
	if (!*dev)
		return FAULT_9002;	/* E_INTERNAL_ERROR: no WAN device */
	/* quoted: the input contract in front refuses a single quote */
	if (strchr(value, ':'))
		snprintf(cmd, sizeof(cmd), "ip -6 addr add '%s/64' dev '%s'", value, dev);
	else
		snprintf(cmd, sizeof(cmd), "ifconfig '%s' '%s' netmask 255.255.255.0 up", dev, value);
	mtk_apply_service(cmd);
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tWebUserInfoParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"RemoteAccess", &DMWRITE, DMT_BOOL, get_wui_remote_access, set_wui_remote_access, NULL, NULL},
{"RemoteAccessTimeout", &DMWRITE, DMT_UNINT, get_wui_remote_timeout, set_wui_remote_timeout, NULL, NULL},
{"AdminName", &DMWRITE, DMT_STRING, get_wui_admin_name, set_wui_admin_name, NULL, NULL},
{"AdminPassword", &DMWRITE, DMT_STRING, get_wui_password, set_wui_admin_password, NULL, NULL},
{"SuperAdminName", &DMWRITE, DMT_STRING, get_wui_super_name, set_wui_super_name, NULL, NULL},
{"SuperAdminPassword", &DMWRITE, DMT_STRING, get_wui_password, set_wui_super_password, NULL, NULL},
{"SuperAdminSecurity", &DMWRITE, DMT_BOOL, get_wui_super_security, set_wui_super_security, NULL, NULL},
{"Captcha_enable", &DMWRITE, DMT_BOOL, get_wui_captcha, set_wui_captcha, NULL, NULL},
{"AvailableLanguages", &DMREAD, DMT_STRING, get_wui_languages, NULL, NULL, NULL},
{"CurrentLanguage", &DMWRITE, DMT_STRING, get_wui_language, set_wui_language, NULL, NULL},
{"SuperAdminAccessList", &DMWRITE, DMT_STRING, get_wui_access_list, set_wui_access_list, NULL, NULL},
{"SuperAdminEnable", &DMWRITE, DMT_STRING, get_wui_super_enable, set_wui_super_enable, NULL, NULL},
{"WebIp", &DMWRITE, DMT_STRING, get_wui_web_ip, set_wui_web_ip, NULL, NULL},
{0}
};

static DMOBJ tUserInterfaceWuiObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"X_AIS_WebUserInfo", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tWebUserInfoParams, NULL},
{0}
};

static DMOBJ tWebUserInfoRoot[] = {
{"UserInterface", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tUserInterfaceWuiObj, NULL, NULL},
{0}
};

static const char *const webuserinfo_mtk_paths[] = {
	"InternetGatewayDevice.UserInterface.X_AIS_WebUserInfo.",
	NULL
};

static const struct dm_module webuserinfo_mtk_module = {
	.name  = "mtk-x-ais-webuserinfo",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tWebUserInfoRoot,
	.paths = webuserinfo_mtk_paths,
};
DM_MODULE_REGISTER(webuserinfo_mtk_module);
