/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	TR-181 root for Broadcom BDK, selected at runtime by
 *	cwmp.cpe.datamodel=tr181 (platform/bdk/dmproxy_bdk.c
 *	dm_platform_select_root()).
 *
 *	The Distributed MDM of the SDK already is a TR-181 data model, so the
 *	static tree here only carries what icwmpd adds on top of it: the
 *	objects implemented in libtr098 itself (WiFi.X_MARUSYS_COM_MloCfg,
 *	ManagementServer.X_MARUSYS_COM_Icwmp) and the ManagementServer leaves
 *	icwmpd owns that the MDM lacks (tManagementServer181Params).
 *	Everything else under Device. (GPV/GPN/SPV/Add/Del, attributes, forced
 *	Inform parameters, the enabled-notify list) is served by
 *	dm_platform_param_method() straight from libbcm_generic_hal, and the
 *	static tree is merged into the HAL answer for the paths that contain
 *	both (Device., Device.WiFi.).  A path outside Device. gets 9005.
 */
#include "dmtr098.h"
#include "dm_registry.h"
#include "root_bdk.h"
#include "mlo_bdk.h"
#include "icwmpcfg.h"
#include "managementserver.h"
#ifdef BDK_SAMPLE_OBJECT
#include "sample_bdk.h"
#endif

DMLEAF tRoot_181_Params[] = {
{0}
};

/* Device.ManagementServer.: the MDM (HAL) has URL/Username/Password/
 * PeriodicInform*, ConnectionRequest*, ParameterKey, CWMPRetry*, UpgradesManaged,
 * EnableCWMP (cms-dm-tr181-managementserver.xml); these are the ones it does
 * not have and icwmpd implements in its UCI config (stock managementserver.c
 * getters), merged into the HAL answer of Device.ManagementServer. */
DMLEAF tManagementServer181Params[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"HTTPCompressionSupported", &DMREAD, DMT_STRING, get_management_server_http_compression_supportted, NULL, NULL, NULL},
{"HTTPCompression", &DMWRITE, DMT_STRING, get_management_server_http_compression, set_management_server_http_compression, NULL, NULL},
{"LightweightNotificationProtocolsSupported", &DMREAD, DMT_STRING, get_lwn_protocol_supported, NULL, NULL, NULL},
{"LightweightNotificationProtocolsUsed", &DMWRITE, DMT_STRING, get_lwn_protocol_used, set_lwn_protocol_used, NULL, NULL},
{"UDPLightweightNotificationHost", &DMWRITE, DMT_STRING, get_lwn_host, set_lwn_host, NULL, NULL},
{"UDPLightweightNotificationPort", &DMWRITE, DMT_UNINT, get_lwn_port, set_lwn_port, NULL, NULL},
{"AliasBasedAddressing", &DMREAD, DMT_BOOL, get_alias_based_addressing, NULL, NULL, NULL},
{"InstanceMode", &DMWRITE, DMT_STRING, get_instance_mode, set_instance_mode, NULL, NULL},
{0}
};

/* Device.WiFi.: only our own child, the SDK part comes from the HAL */
DMOBJ tWiFi181Obj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{CUSTOM_PREFIX"MloCfg", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tMloCfgParam, NULL},
{0}
};

/* Device.DeviceInfo.: HAL object, plus static leaves of our own (case 7 of
 * tr098/bdk/sample_bdk.c).  Listed in proxy_static_leaves[] (dmproxy_bdk.c)
 * so the proxy hands these names to the engine and merges the object. */
DMLEAF tDeviceInfo181Params[] = {
#ifdef BDK_SAMPLE_OBJECT
{CUSTOM_PREFIX"SampleUpTimeMinutes", &DMREAD, DMT_UNINT, get_sample_uptime_minutes, NULL, NULL, NULL},
#endif
{0}
};

DMOBJ tRoot_181_Obj[] = {
{"DeviceInfo", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tDeviceInfo181Params, NULL},
{"ManagementServer", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tManagementServerIcwmpObj, tManagementServer181Params, NULL},
{"WiFi", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tWiFi181Obj, NULL, NULL},
#ifdef BDK_SAMPLE_OBJECT
/* developer template (tr098/bdk/sample_bdk.c): hidden unless cwmp.sample.enable=1 */
{CUSTOM_PREFIX"Sample", &DMREAD, NULL, NULL, sample_enabled, NULL, NULL, &DMNONE, tSampleObj, tSampleParam, NULL},
#endif
{0}
};

/* The TR-181 model of this build.  Only what icwmpd adds itself is static,
 * everything else under Device. is served by dmproxy_bdk.c from the MDM. */
static const struct dm_module tr181_bdk_module = {
	.name  = "bdk-tr181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tRoot_181_Obj,
	.params = tRoot_181_Params,
};
DM_MODULE_REGISTER(tr181_bdk_module);
