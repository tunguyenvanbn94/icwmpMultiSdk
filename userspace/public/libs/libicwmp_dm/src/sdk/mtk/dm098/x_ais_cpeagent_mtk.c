/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.X_AIS_CPEagent. -- the shared secret of the
 *	operator's agent, ported from functions/tr098/X_AIS_CPEagent.
 *	  SecretKey         3rdpartyagent.3rdpartyagent.secret_key, at most 256
 *	                    characters; reads back ENCRYPTED, never in clear
 *	  SecretKeyVersion  secret_key_version, at most 64 characters
 *	A set always writes and restarts the agent (queued, see X_AIS_3rdAgent).
 *
 *	encrypt_with_specialkey() of the shell: the value zero-padded to 256
 *	bytes, AES-256-ECB with the operator's key, no padding, base64 on one
 *	line, the first 64 characters.  64 base64 characters are the first 48
 *	bytes of ciphertext, and in ECB those depend on the first 48 bytes of
 *	the padded value only -- so only those are encrypted here.  Same openssl
 *	command line as the shell (the key is on it for the moment it runs, as
 *	it was), so no crypto library is linked in.
 *
 *	The key is not in this source: the repository is public.  The package
 *	build takes it from the product's own X_AIS_CPEagent
 *	(tools/mtk-cpeagent-key.sh -> cpeagent_key_mtk.h, see feeds/libtr098).
 *	A build without that header -- the static checks, the host test -- reads
 *	SecretKey as "", what the shell answered when openssl failed.
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"

#if defined(__has_include)
#if __has_include("cpeagent_key_mtk.h")
#include "cpeagent_key_mtk.h"
#endif
#endif
#ifndef MTK_CPEAGENT_KEY_HEX
#define MTK_CPEAGENT_KEY_HEX ""
#endif

#define AGENT_PACKAGE	"3rdpartyagent"
#define AGENT_SECTION	"3rdpartyagent"
#define AGENT_RESTART	"/etc/init.d/3rdpartyagent restart"

#define CPE_PLAIN	48	/* bytes behind the 64 base64 characters read back */

static char *cpe_encrypt(const char *plain)
{
	char path[] = "/tmp/.icwmp_cpeagent_XXXXXX";
	char key[] = MTK_CPEAGENT_KEY_HEX;
	unsigned char block[CPE_PLAIN];
	size_t n = strlen(plain);
	char *out;
	int fd, ok;

	if (!*plain || !*key)
		return "";
	memset(block, 0, sizeof(block));
	memcpy(block, plain, n < sizeof(block) ? n : sizeof(block));
	fd = mkstemp(path);
	if (fd < 0)
		return "";
	ok = write(fd, block, sizeof(block)) == (ssize_t)sizeof(block);
	close(fd);
	if (!ok) {
		unlink(path);
		return "";
	}
	{
		char *argv[] = { "openssl", "enc", "-aes-256-ecb", "-K", key, "-nopad",
				 "-in", path, "-a", "-A", NULL };

		out = mtk_exec_line(argv);
	}
	unlink(path);
	if (strlen(out) > 64)
		out[64] = '\0';
	return out;
}

static int get_cpe_secret_key(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = cpe_encrypt(mtk_uci(AGENT_PACKAGE, AGENT_SECTION, "secret_key"));
	return 0;
}

static int cpe_set(const char *option, const char *value, size_t max, int action)
{
	if (!value)
		value = "";
	if (strlen(value) > max)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(AGENT_PACKAGE, AGENT_SECTION, (char *)option, (char *)value);
	mtk_apply_service_once(AGENT_RESTART);
	return 0;
}

static int set_cpe_secret_key(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return cpe_set("secret_key", value, 256, action);
}

static int get_cpe_secret_key_version(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci(AGENT_PACKAGE, AGENT_SECTION, "secret_key_version");
	return 0;
}

static int set_cpe_secret_key_version(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return cpe_set("secret_key_version", value, 64, action);
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tCpeAgentParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"SecretKey", &DMWRITE, DMT_STRING, get_cpe_secret_key, set_cpe_secret_key, NULL, NULL},
{"SecretKeyVersion", &DMWRITE, DMT_STRING, get_cpe_secret_key_version, set_cpe_secret_key_version, NULL, NULL},
{0}
};

static DMOBJ tCpeAgentRoot[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"X_AIS_CPEagent", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tCpeAgentParams, NULL},
{0}
};

static const char *const cpeagent_mtk_paths[] = {
	"InternetGatewayDevice.X_AIS_CPEagent.",
	NULL
};

static const struct dm_module cpeagent_mtk_module = {
	.name  = "mtk-x-ais-cpeagent",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tCpeAgentRoot,
	.paths = cpeagent_mtk_paths,
};
DM_MODULE_REGISTER(cpeagent_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): the same tables under Device., type C
 * of docs/plan/tr181_mtk_design.md */
static const char *const cpeagent_mtk_paths181[] = {
	"Device.X_AIS_CPEagent.",
	NULL
};

static const struct dm_module cpeagent_mtk_module181 = {
	.name  = "mtk-x-ais-cpeagent-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tCpeAgentRoot,
	.paths = cpeagent_mtk_paths181,
};
DM_MODULE_REGISTER(cpeagent_mtk_module181);
