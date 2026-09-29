/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	TR-098 root for Broadcom BDK.  Only objects that have a BDK
 *	implementation are listed: GetParameterNames must never advertise a
 *	subtree the device cannot serve.  Add objects here as they are ported
 *	(see docs/icwmp_bdk_port_design.md, phase table).
 */
#include "dmtr098.h"
#include "dm_registry.h"
#include "root_bdk.h"
#include "deviceinfo.h"
#include "managementserver.h"
#include "landevice_bdk.h"
#include "wandevice_bdk.h"
#include "system_bdk.h"
#include "mlo_bdk.h"
#include "icwmpcfg.h"
#ifdef BDK_SAMPLE_OBJECT
#include "sample_bdk.h"
#endif

void deviceinfo_bdk_register(void);

DMOBJ tRoot_098_Obj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"DeviceInfo", &DMREAD, NULL, NULL, NULL, NULL, &DMFINFRM, &DMNONE, tDeviceInfoObj, tDeviceInfoParams, NULL},
/* ManagementServer keeps the stock iopsys implementation on top of the UCI
 * "cwmp" config of icwmpd; icwmpd (bdk/icwmp_bdk.c) mirrors it to/from
 * Device.ManagementServer.* of the MDM so WebUI and ACS see the same values */
{"ManagementServer", &DMREAD, NULL, NULL, NULL, NULL, &DMFINFRM, &DMNONE, tManagementServerIcwmpObj, tManagementServerParams, NULL},
{"LANDevice", &DMREAD, NULL, NULL, NULL, browselandeviceInst, &DMFINFRM, &DMNONE, tLANDeviceObj, tLANDeviceParam, NULL},
{"WANDevice", &DMREAD, NULL, NULL, NULL, browsewandeviceInst, &DMFINFRM, &DMNONE, tWANDeviceObj, tWANDeviceParam, NULL},
{"Time", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tTimeParams, NULL},
{"IPPingDiagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tIPPingDiagnosticsParam, NULL},
{"TraceRouteDiagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tTraceRouteDiagnosticsObj, tTraceRouteDiagnosticsParam, NULL},
{"Layer3Forwarding", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tLayer3ForwardingObj, tLayer3ForwardingParam, NULL},
/* TR-181 proxy (platform/bdk/dmproxy_bdk.c): the object is listed here so the
 * ACS discovers it; its content is served by dm_platform_param_method() and
 * never walked by the static engine (no leaf table, no children) */
{CUSTOM_PREFIX"Device", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, NULL, NULL},
/* Wi-Fi 7 MLO AP configuration, implemented in icwmpd (tr098/bdk/mlo_bdk.c);
 * the same leaves sit at Device.WiFi.X_MARUSYS_COM_MloCfg. in TR-181 mode */
{CUSTOM_PREFIX"MloCfg", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tMloCfgParam, NULL},
#ifdef BDK_SAMPLE_OBJECT
/* developer template (tr098/bdk/sample_bdk.c): hidden unless cwmp.sample.enable=1 */
{CUSTOM_PREFIX"Sample", &DMREAD, NULL, NULL, sample_enabled, NULL, NULL, &DMNONE, tSampleObj, tSampleParam, NULL},
#endif
{0}
};

/* dmplatform_bdk.c gọi hàm này khi attach vào MDM, registry cũng gọi qua
 * .init -- cờ "done" bên trong làm cho lần thứ hai không tốn gì */
void tr098_bdk_register_all(void)
{
	static int done;

	if (done)
		return;
	done = 1;
	deviceinfo_bdk_register();
	landevice_bdk_register();
	wandevice_bdk_register();
	system_bdk_register();
#ifdef BDK_SAMPLE_OBJECT
	sample_bdk_register();
#endif
}

/* One module: the TR-098 view of the BDK MDM.  .init runs the per object
 * registrations the BDK tree needs before the tree is walked. */
static const struct dm_module tr098_bdk_module = {
	.name  = "bdk-tr098",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tRoot_098_Obj,
	.params = tRoot_098_Params,
	.init  = tr098_bdk_register_all,
};
DM_MODULE_REGISTER(tr098_bdk_module);
