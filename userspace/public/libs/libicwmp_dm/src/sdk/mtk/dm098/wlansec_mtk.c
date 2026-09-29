/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The security half of WLANConfiguration.{i}: beacon type, the three pairs
 *	of authentication/encryption leaves, the passphrase, PreSharedKey.1 and
 *	the four WEP keys.  Phase P3b, ported from functions/tr098/lan_device.
 *
 *	Everything hangs off one UCI option, wireless.<iface>.encryption, whose
 *	spelling is the product's:
 *
 *	    none                    open
 *	    wep+shared+64 / +128    WEP 40 bit / 104 bit, keys in key1..key4
 *	    psk                     WPA personal, TKIP
 *	    psk2+ccmp               WPA2 personal, AES
 *	    psk-mixed+ccmp          WPA/WPA2 mixed, AES
 *	    psk-mixed+tkip+ccmp     WPA/WPA2 mixed, TKIP and AES
 *	    sae / sae-mixed         WPA3 / WPA3 transition
 *
 *	Two traps kept exactly as they were, because the ACS has been writing
 *	them for years:
 *
 *	  - wireless.<iface>.key means the passphrase for WPA but the key INDEX
 *	    (1..4) for WEP.  set_wep_key_index() writes the index into the same
 *	    option, and that is deliberate here too.
 *	  - only BeaconType, BasicAuthenticationMode and WEPEncryptionLevel push
 *	    authmode/EncryptType into mapd.  The WPA and IEEE11i setters never
 *	    did, so they still do not: changing that would alter what mapd
 *	    rewrites on the next reload, which is a product decision, not ours.
 *
 *	KeyPassphrase and PreSharedKey.1.* look like the same thing and are not:
 *	the first refuses any wep+ encryption and syncs the MLO pair, the second
 *	enforces the 8..63 length, also writes the five character key1 digest the
 *	product's WebUI shows, and does not sync.  Both are kept.
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmmem.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "wlan_mtk.h"

#define MLO_FRONTHAUL_SECTION	"apmld1"
#define MLO_BACKHAUL_SECTION	"apmld2"

/* one WEP key slot of one interface */
struct wep_key_ctx {
	const struct wlan_iface *w;
	int index;
};

static char *iface_of(void *data)
{
	return (char *)wlan_iface_of(data)->name;
}

static char *encryption_of(void *data)
{
	return wlan_opt(data, "encryption");
}

static int is_wep(const char *enc)
{
	return strncmp(enc, "wep", 3) == 0 || strcmp(enc, "WEP") == 0;
}

static int is_mlo_pair(const char *iface, const char **peer, const char **section)
{
	if (strcmp(iface, "ra5") == 0 || strcmp(iface, "rai5") == 0) {
		*peer = (strcmp(iface, "ra5") == 0) ? "rai5" : "ra5";
		*section = MLO_FRONTHAUL_SECTION;
		return 1;
	}
	if (strcmp(iface, "ra4") == 0 || strcmp(iface, "rai4") == 0) {
		*peer = (strcmp(iface, "ra4") == 0) ? "rai4" : "ra4";
		*section = MLO_BACKHAUL_SECTION;
		return 1;
	}
	return 0;
}

/*
 * What mapd has to be told when the encryption changes, straight out of
 * set_beacon_type().  mapd keeps its own copy of the BSS security and would
 * otherwise put the old one back at the next reload.
 */
static void mapd_security(const char *iface, const char *enc)
{
	const char *authmode = NULL, *encrypt = NULL;

	if (strcmp(enc, "none") == 0) {
		authmode = "0x0001"; encrypt = "0x0001";
	} else if (strcmp(enc, "wep+shared+64") == 0 || strcmp(enc, "wep+shared+128") == 0) {
		authmode = "0x0001"; encrypt = "0x0002";
	} else if (strcmp(enc, "psk") == 0) {
		authmode = "0x0002"; encrypt = "0x0004";
	} else if (strcmp(enc, "psk2+ccmp") == 0) {
		authmode = "0x0020"; encrypt = "0x0008";
	} else if (strcmp(enc, "psk-mixed+tkip+ccmp") == 0) {
		authmode = "0x0022"; encrypt = "0x000c";
	} else if (strcmp(enc, "psk-mixed+ccmp") == 0) {
		authmode = "0x0022"; encrypt = "0x0008";
	} else if (strcmp(enc, "sae") == 0) {
		authmode = "0x0040"; encrypt = "0x0008";
	} else if (strcmp(enc, "sae-mixed") == 0) {
		authmode = "0x0060"; encrypt = "0x0008";
	}
	if (!authmode)
		return;
	wlan_mapd_set(iface, "authmode", (char *)authmode);
	wlan_mapd_set(iface, "EncryptType", (char *)encrypt);
}

/* ------------------------------------------------------------------ */
/* BeaconType                                                           */
/* ------------------------------------------------------------------ */

static int get_beacon_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *enc = encryption_of(data);

	if (!*enc || strcmp(enc, "none") == 0 || strcmp(enc, "OPEN/NONE") == 0)
		*value = "None";
	else if (is_wep(enc))
		*value = "Basic";
	else if (strcmp(enc, "psk") == 0 || strcmp(enc, "WPA") == 0)
		*value = "WPA";
	else if (strncmp(enc, "psk2", 4) == 0 || strcmp(enc, "WPA2") == 0 || strcmp(enc, "sae") == 0)
		*value = "11i";
	else if (strncmp(enc, "psk-mixed", 9) == 0 || strcmp(enc, "sae-mixed") == 0 ||
		 strcmp(enc, "WPA1WPA2") == 0)
		*value = "WPAand11i";
	else
		*value = "None";
	return 0;
}

static int set_beacon_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *iface = iface_of(data);
	char *cur = encryption_of(data);
	const char *enc;

	if (!value)
		return FAULT_9007;
	if (strcmp(value, "None") == 0) {
		enc = "none";
	} else if (strcmp(value, "Basic") == 0) {
		enc = "wep+shared+64";
	} else if (strcmp(value, "WPA") == 0) {
		enc = "psk";
	} else if (strcmp(value, "11i") == 0) {
		/* WPA3 stays WPA3: dropping to psk2 would lock out SAE only clients */
		enc = (strcmp(cur, "sae") == 0 || strcmp(cur, "sae-mixed") == 0) ? cur : "psk2+ccmp";
	} else if (strcmp(value, "WPAand11i") == 0) {
		enc = (strcmp(cur, "sae") == 0 || strcmp(cur, "sae-mixed") == 0) ?
			"sae-mixed" : "psk-mixed+ccmp";
	} else {
		return FAULT_9007;
	}
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value("wireless", iface, "encryption", (char *)enc);
	mapd_security(iface, enc);
	wlan_reload();
	return 0;
}

/* ------------------------------------------------------------------ */
/* authentication and encryption modes                                  */
/* ------------------------------------------------------------------ */

/* WPA and IEEE11i read the same option and answered the same list */
static int psk_authentication(char *enc, char **value)
{
	if (strncmp(enc, "psk", 3) == 0 || strncmp(enc, "sae", 3) == 0)
		*value = "PSKAuthentication";
	else if (strcmp(enc, "WPA") == 0 || strcmp(enc, "WPA2") == 0 || strcmp(enc, "WPA1WPA2") == 0)
		*value = "EAPAuthentication";
	else
		*value = "None";
	return 0;
}

static int get_wpa_auth(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return psk_authentication(encryption_of(data), value);
}

/* IEEE11i counts only the WPA2 and later spellings as PSK */
static int get_ieee11i_auth(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *enc = encryption_of(data);

	if (strncmp(enc, "psk2+", 5) == 0 || strncmp(enc, "psk-mixed", 9) == 0 ||
	    strcmp(enc, "sae") == 0 || strcmp(enc, "sae-mixed") == 0)
		*value = "PSKAuthentication";
	else if (strcmp(enc, "WPA") == 0 || strcmp(enc, "WPA2") == 0 || strcmp(enc, "WPA1WPA2") == 0)
		*value = "EAPAuthentication";
	else
		*value = "None";
	return 0;
}

/* only PSK can be configured from CWMP, EAP needs a RADIUS server the
 * product does not carry -- the shell answered 9007 and so do we */
static int set_psk_authentication(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!value || strcmp(value, "PSKAuthentication") != 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value("wireless", iface_of(data), "encryption", "psk2+ccmp");
	wlan_reload();
	return 0;
}

static int get_encryption_modes(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	static const char *const aes[] = { "psk+ccmp", "psk2+ccmp", "sae+ccmp", "psk-mixed+ccmp",
					   "psk2+sae+ccmp", "sae-mixed+ccmp", "sae", "sae-mixed", NULL };
	static const char *const both[] = { "psk-mixed+tkip+ccmp", "psk+tkip+ccmp",
					    "psk2+tkip+ccmp", NULL };
	static const char *const tkip[] = { "psk", "psk+tkip", "psk2+tkip", "psk-mixed+tkip", NULL };
	char *enc = encryption_of(data);
	int i;

	for (i = 0; aes[i]; i++) {
		if (strcmp(enc, aes[i]) == 0) {
			*value = "AESEncryption";
			return 0;
		}
	}
	for (i = 0; both[i]; i++) {
		if (strcmp(enc, both[i]) == 0) {
			*value = "TKIPandAESEncryption";
			return 0;
		}
	}
	for (i = 0; tkip[i]; i++) {
		if (strcmp(enc, tkip[i]) == 0) {
			*value = "TKIPEncryption";
			return 0;
		}
	}
	*value = "None";
	return 0;
}

static int set_encryption_modes(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *enc;

	if (!value)
		return FAULT_9007;
	if (strcmp(value, "AESEncryption") == 0)
		enc = "psk2+ccmp";
	else if (strcmp(value, "TKIPandAESEncryption") == 0)
		enc = "psk-mixed+tkip+ccmp";
	else if (strcmp(value, "TKIPEncryption") == 0)
		enc = "psk-mixed+tkip";
	else
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value("wireless", iface_of(data), "encryption", (char *)enc);
	wlan_reload();
	return 0;
}

static int get_basic_auth(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *enc = encryption_of(data);

	*value = (strcmp(enc, "wep+shared+64") == 0 || strcmp(enc, "wep+shared+128") == 0) ?
		"SharedAuthentication" : "None";
	return 0;
}

static int set_basic_auth(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *iface = iface_of(data);

	if (!value)
		return FAULT_9007;
	/* EAPAuthentication was accepted and ignored, not refused */
	if (strcmp(value, "EAPAuthentication") == 0) {
		if (action == VALUECHECK)
			return 0;
		wlan_reload();
		return 0;
	}
	if (strcmp(value, "None") != 0 && strcmp(value, "SharedAuthentication") != 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (strcmp(value, "None") == 0) {
		dmuci_set_value("wireless", iface, "encryption", "none");
		mapd_security(iface, "none");
	} else {
		dmuci_set_value("wireless", iface, "encryption", "wep+shared+64");
		mapd_security(iface, "wep+shared+64");
	}
	wlan_reload();
	return 0;
}

static int get_basic_encryption(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = is_wep(encryption_of(data)) ? "WEPEncryption" : "None";
	return 0;
}

/* refuses to touch a BSS that is not already WEP */
static int set_basic_encryption(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!is_wep(encryption_of(data)))
		return FAULT_9007;
	if (!value || (strcmp(value, "None") != 0 && strcmp(value, "WEPEncryption") != 0))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (strcmp(value, "WEPEncryption") == 0)
		dmuci_set_value("wireless", iface_of(data), "encryption", "wep+shared+64");
	wlan_reload();
	return 0;
}

/* ------------------------------------------------------------------ */
/* WEP                                                                  */
/* ------------------------------------------------------------------ */

static int get_wep_level(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *enc = encryption_of(data);

	if (strcmp(enc, "wep+shared+64") == 0)
		*value = "40-bit";
	else if (strcmp(enc, "wep+shared+128") == 0)
		*value = "104-bit";
	else
		*value = "Disabled";
	return 0;
}

static int set_wep_level(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *iface = iface_of(data);
	const char *enc;

	if (strncmp(encryption_of(data), "wep+shared+", 11) != 0)
		return FAULT_9007;
	if (!value)
		return FAULT_9007;
	if (strcmp(value, "40-bit") == 0)
		enc = "wep+shared+64";
	else if (strcmp(value, "104-bit") == 0)
		enc = "wep+shared+128";
	else if (strcmp(value, "Disabled") == 0)
		enc = "none";
	else
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value("wireless", iface, "encryption", (char *)enc);
	mapd_security(iface, enc);
	wlan_reload();
	return 0;
}

static int get_wep_key_index(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	/* the shell answered a constant 1 whatever the config says */
	*value = "1";
	return 0;
}

static int set_wep_key_index(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (strncmp(encryption_of(data), "wep+shared+", 11) != 0)
		return FAULT_9007;
	if (!value || strlen(value) != 1 || value[0] < '1' || value[0] > '4')
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	/* for WEP this option holds the index, not a passphrase (see the file
	 * header): the product stores the four keys in key1..key4 */
	dmuci_set_value("wireless", iface_of(data), "key", value);
	return 0;
}


/* key1..key4, length decided by the WEP level: 5 or 10 for 40 bit, 13 or 26
 * for 104 bit, the longer form being hex */
static int set_wep_key(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wep_key_ctx *k = (struct wep_key_ctx *)data;
	char *iface = k ? (char *)k->w->name : NULL;
	char *enc = iface ? mtk_uci("wireless", iface, "encryption") : "";
	size_t len = value ? strlen(value) : 0;
	char option[8];
	size_t i;
	int hex = 0;

	if (!iface)
		return FAULT_9002;
	if (strcmp(enc, "wep+shared+64") == 0) {
		if (len == 10)
			hex = 1;
		else if (len != 5)
			return FAULT_9007;
	} else if (strcmp(enc, "wep+shared+128") == 0) {
		if (len == 26)
			hex = 1;
		else if (len != 13)
			return FAULT_9007;
	} else {
		return FAULT_9007;
	}
	if (hex) {
		for (i = 0; i < len; i++) {
			char c = value[i];

			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
				return FAULT_9007;
		}
	}
	if (action == VALUECHECK)
		return 0;
	snprintf(option, sizeof(option), "key%d", k->index);
	dmuci_set_value("wireless", iface, option, value);
	wlan_mapd_set(iface, "PSK", value);
	wlan_reload();
	return 0;
}

/* ------------------------------------------------------------------ */
/* passphrases                                                          */
/* ------------------------------------------------------------------ */

static int get_key_passphrase(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = wlan_opt(data, "key");
	return 0;
}

/* WLANConfiguration.KeyPassphrase: no length check, refuses WEP, syncs MLO */
static int set_key_passphrase(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *iface = iface_of(data);
	const char *peer = NULL, *section = NULL;

	if (strncmp(encryption_of(data), "wep+", 4) == 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (is_mlo_pair(iface, &peer, &section)) {
		dmuci_set_value("wireless", iface, "key", value);
		dmuci_set_value("wireless", (char *)peer, "key", value);
		dmuci_set_value("wireless", (char *)section, "key", value);
		wlan_mapd_set(iface, "PSK", value);
		wlan_mapd_set(peer, "PSK", value);
	} else {
		dmuci_set_value("wireless", iface, "key", value);
		wlan_mapd_set(iface, "PSK", value);
	}
	wlan_reload();
	return 0;
}

/* PreSharedKey.1.*: enforces 8..63, writes the key1 digest, no MLO sync */
static int set_presharedkey(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *iface = iface_of(data);
	char *enc = encryption_of(data);
	size_t len = value ? strlen(value) : 0;
	char digest[6];

	if (strcmp(enc, "wep+shared+64") == 0 || strcmp(enc, "wep+shared+128") == 0)
		return FAULT_9007;
	if (len < 8 || len > 63)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	snprintf(digest, sizeof(digest), "%.5s", value);
	dmuci_set_value("wireless", iface, "key1", digest);
	dmuci_set_value("wireless", iface, "key", value);
	wlan_mapd_set(iface, "PSK", value);
	wlan_reload();
	return 0;
}

/* ------------------------------------------------------------------ */
/* instances                                                            */
/* ------------------------------------------------------------------ */

/* one PreSharedKey entry, carrying the parent's interface unchanged */
static int browsePreSharedKeyInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	char *idx, *idx_last = NULL;

	idx = handle_update_instance(3, dmctx, &idx_last, update_instance_without_section, 1, 1);
	DM_LINK_INST_OBJ(dmctx, parent_node, prev_data, idx);
	return 0;
}

static int browseWepKeyInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	const struct wlan_iface *w = wlan_iface_of(prev_data);
	char *idx, *idx_last = NULL;
	int i;

	for (i = 1; i <= 4; i++) {
		struct wep_key_ctx k = { w, i };

		idx = handle_update_instance(3, dmctx, &idx_last, update_instance_without_section, 1, i);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)&k, idx) == DM_STOP)
			break;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tPreSharedKeyParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"PreSharedKey", &DMWRITE, DMT_STRING, get_empty, set_presharedkey, NULL, NULL},
{"KeyPassphrase", &DMWRITE, DMT_STRING, get_empty, set_presharedkey, NULL, NULL},
{0}
};

static DMLEAF tWepKeyParam[] = {
{"WEPKey", &DMWRITE, DMT_STRING, get_empty, set_wep_key, NULL, NULL},
{0}
};

static DMLEAF tWlanSecParam[] = {
{"BeaconType", &DMWRITE, DMT_STRING, get_beacon_type, set_beacon_type, NULL, NULL},
{"KeyPassphrase", &DMWRITE, DMT_STRING, get_key_passphrase, set_key_passphrase, NULL, NULL},
{"WEPKeyIndex", &DMWRITE, DMT_UNINT, get_wep_key_index, set_wep_key_index, NULL, NULL},
{"WEPEncryptionLevel", &DMWRITE, DMT_STRING, get_wep_level, set_wep_level, NULL, NULL},
{"BasicEncryptionModes", &DMWRITE, DMT_STRING, get_basic_encryption, set_basic_encryption, NULL, NULL},
{"BasicAuthenticationMode", &DMWRITE, DMT_STRING, get_basic_auth, set_basic_auth, NULL, NULL},
{"WPAEncryptionModes", &DMWRITE, DMT_STRING, get_encryption_modes, set_encryption_modes, NULL, NULL},
{"WPAAuthenticationMode", &DMWRITE, DMT_STRING, get_wpa_auth, set_psk_authentication, NULL, NULL},
{"IEEE11iEncryptionModes", &DMWRITE, DMT_STRING, get_encryption_modes, set_encryption_modes, NULL, NULL},
{"IEEE11iAuthenticationMode", &DMWRITE, DMT_STRING, get_ieee11i_auth, set_psk_authentication, NULL, NULL},
{0}
};

static DMOBJ tWlanSecObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"PreSharedKey", &DMREAD, NULL, NULL, NULL, browsePreSharedKeyInst, NULL, &DMNONE, NULL, tPreSharedKeyParam, NULL},
{"WEPKey", &DMREAD, NULL, NULL, NULL, browseWepKeyInst, NULL, &DMNONE, NULL, tWepKeyParam, NULL},
{0}
};

static DMOBJ tLanDeviceWlanSecObj[] = {
{"WLANConfiguration", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tWlanSecObj, tWlanSecParam, NULL},
{0}
};

/* browseinstobj left NULL twice on purpose: lan_mtk.c owns the LANDevice
 * instance and wlan_mtk.c owns the WLANConfiguration instance */
static DMOBJ tLanDeviceWlanSecRoot[] = {
{"LANDevice", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tLanDeviceWlanSecObj, NULL, NULL},
{0}
};

/* No .paths here: with this module linked the whole object is C, and
 * wlan_mtk.c carries the single branch claim for all three modules. */
static const struct dm_module wlansec_mtk_module = {
	.name  = "mtk-wlan-security",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tLanDeviceWlanSecRoot,
};
DM_MODULE_REGISTER(wlansec_mtk_module);
