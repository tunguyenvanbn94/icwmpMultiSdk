/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	ubus "tr069 dm": drive the data model of libtr098 from the board shell,
 *	without an ACS session.  Same code path as the CWMP RPCs in xml.c
 *	(GetParameterValues / GetParameterNames / SetParameterValues / AddObject /
 *	DeleteObject / GetParameterAttributes / SetParameterAttributes and the
 *	parameter list of Inform), so what works here works from the ACS and
 *	vice versa.
 *
 *	  ubus call tr069 dm '{"cmd":"get",     "path":"InternetGatewayDevice.LANDevice.1."}'
 *	  ubus call tr069 dm '{"cmd":"names",   "path":"InternetGatewayDevice.", "next_level":true}'
 *	  ubus call tr069 dm '{"cmd":"set",     "path":"InternetGatewayDevice.LANDevice.1.LANHostConfigManagement.DHCPLeaseTime", "value":"43200", "key":"k1"}'
 *	  ubus call tr069 dm '{"cmd":"add",     "path":"InternetGatewayDevice.LANDevice.1.LANHostConfigManagement.DHCPStaticAddress."}'
 *	  ubus call tr069 dm '{"cmd":"del",     "path":"InternetGatewayDevice.LANDevice.1.LANHostConfigManagement.DHCPStaticAddress.1."}'
 *	  ubus call tr069 dm '{"cmd":"attr",    "path":"InternetGatewayDevice.ManagementServer."}'
 *	  ubus call tr069 dm '{"cmd":"setattr", "path":"InternetGatewayDevice.ManagementServer.PeriodicInformInterval", "value":"2"}'
 *	  ubus call tr069 dm '{"cmd":"inform",  "path":""}'
 *	  ubus call tr069 dm '{"cmd":"get",     "path":"InternetGatewayDevice.", "file":"/tmp/icwmp/dm_full.txt"}'
 *	BDK with cwmp.cpe.datamodel=tr181 the paths start with Device. instead:
 *	  ubus call tr069 dm '{"cmd":"get",     "path":"Device.WiFi.X_MARUSYS_COM_MloCfg."}'
 *	MTK: the same commands reach the easycwmp function library through the
 *	script bridge of libtr098 (platform/mtk), e.g.
 *	  ubus call tr069 dm '{"cmd":"get",     "path":"InternetGatewayDevice.X_AIS_Mesh."}'
 *
 *	"file": the parameter list is written to that file (one line per
 *	parameter, tab separated: path, type, value / path, writable / path,
 *	notification) and the reply carries only the count, so a whole-tree
 *	dump does not go through the ubus message size limit.
 *
 *	The handler runs on the uloop thread: it takes mutex_session_send so it
 *	never walks the data model (dmmem is a global list without lock) while
 *	the session thread does, exactly like the notify thread and
 *	bdk_reload_config().  A running ACS session therefore delays the ubus call
 *	until it ends.  After set/add/del/setattr the handler does what the end
 *	of an ACS session does for the data model: apply_end_session() +
 *	dm_entry_restart_services(), the enabled-notify file is rebuilt after
 *	setattr, icwmp_platform_end_session() (bdk: save to flash, mtk: config
 *	mirror), and an END_SESSION_RELOAD requested by a setter (X_..._Icwmp.*,
 *	TR-181 attributes, ManagementServer.* on mtk) is run right away when no
 *	session is running (same as "tr069 command reload").  Other end-session
 *	flags (reboot, factory reset, diagnostics) are left pending and reported
 *	in "end_session": they run when the next ACS session ends, like an SPV
 *	from the ACS.
 *
 *	Every platform (TR098 builds).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <libubox/blobmsg.h>
#include <libubus.h>

#include "cwmp.h"
#include "log.h"
#include "sdk/sdk.h"
#include "icwmp_dm.h"
#include <icwmp_dm/dmentry.h>

const struct blobmsg_policy icwmp_dm_policy[__ICWMP_DM_MAX] = {
	[ICWMP_DM_CMD]        = { .name = "cmd",        .type = BLOBMSG_TYPE_STRING },
	[ICWMP_DM_PATH]       = { .name = "path",       .type = BLOBMSG_TYPE_STRING },
	[ICWMP_DM_VALUE]      = { .name = "value",      .type = BLOBMSG_TYPE_STRING },
	[ICWMP_DM_KEY]        = { .name = "key",        .type = BLOBMSG_TYPE_STRING },
	[ICWMP_DM_NEXT_LEVEL] = { .name = "next_level", .type = BLOBMSG_TYPE_BOOL },
	[ICWMP_DM_FILE]       = { .name = "file",       .type = BLOBMSG_TYPE_STRING },
};

static struct blob_buf bb;

static void dm_add_fault_list(struct dmctx *dmctx)
{
	struct param_fault *p;
	void *a, *t;

	if (dmctx->list_fault_param.next == &dmctx->list_fault_param)
		return;
	a = blobmsg_open_array(&bb, "faults");
	list_for_each_entry(p, &dmctx->list_fault_param, list) {
		t = blobmsg_open_table(&bb, NULL);
		blobmsg_add_string(&bb, "parameter", p->name ? p->name : "");
		blobmsg_add_u32(&bb, "fault", p->fault);
		blobmsg_close_table(&bb, t);
	}
	blobmsg_close_array(&bb, a);
}

/* the list of a GET_VALUE/GET_NAME/GET_NOTIFICATION/INFORM into the reply */
static void dm_add_param_list(struct dmctx *dmctx, int cmd)
{
	struct dm_parameter *n;
	void *a, *t;

	a = blobmsg_open_array(&bb, "parameters");
	list_for_each_entry(n, &dmctx->list_parameter, list) {
		t = blobmsg_open_table(&bb, NULL);
		blobmsg_add_string(&bb, "parameter", n->name ? n->name : "");
		if (cmd == CMD_GET_NAME)
			blobmsg_add_string(&bb, "writable", n->data ? n->data : "");
		else if (cmd == CMD_GET_NOTIFICATION)
			blobmsg_add_string(&bb, "notification", n->data ? n->data : "");
		else {
			blobmsg_add_string(&bb, "value", n->data ? n->data : "");
			blobmsg_add_string(&bb, "type", n->type ? n->type : "");
		}
		blobmsg_close_table(&bb, t);
	}
	blobmsg_close_array(&bb, a);
}

/* ... or into a file: "<path>\t<type>\t<value>", "<path>\t<writable>",
 * "<path>\t<notification>"; -1 when the file cannot be written */
static int dm_write_param_file(struct dmctx *dmctx, int cmd, const char *file)
{
	struct dm_parameter *n;
	FILE *fp = fopen(file, "w");

	if (!fp) {
		CWMP_LOG(ERROR, "ubus dm: cannot write %s", file);
		return -1;
	}
	list_for_each_entry(n, &dmctx->list_parameter, list) {
		if (cmd == CMD_GET_NAME || cmd == CMD_GET_NOTIFICATION)
			fprintf(fp, "%s\t%s\n", n->name ? n->name : "", n->data ? n->data : "");
		else
			fprintf(fp, "%s\t%s\t%s\n", n->name ? n->name : "", n->type ? n->type : "",
			        n->data ? n->data : "");
	}
	fclose(fp);
	return 0;
}

/* end-session flags a setter left behind (libtr098 end_session_flag) */
static void dm_add_end_session(unsigned int flags)
{
	static const struct { unsigned int bit; const char *name; } names[] = {
		{ END_SESSION_RELOAD, "reload" }, { END_SESSION_REBOOT, "reboot" },
		{ END_SESSION_FACTORY_RESET, "factory_reset" },
		{ END_SESSION_X_FACTORY_RESET_SOFT, "factory_reset_soft" },
		{ END_SESSION_IPPING_DIAGNOSTIC, "ipping_diagnostic" },
		{ END_SESSION_TRACEROUTE_DIAGNOSTIC, "traceroute_diagnostic" },
		{ END_SESSION_EXTERNAL_ACTION, "external_action" },
	};
	void *a = blobmsg_open_array(&bb, "end_session");
	unsigned int i;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (flags & names[i].bit)
			blobmsg_add_string(&bb, NULL, names[i].name);
	blobmsg_close_array(&bb, a);
}

int icwmp_ubus_dm(struct ubus_context *ctx, struct ubus_object *obj,
		      struct ubus_request_data *req, const char *method,
		      struct blob_attr *msg)
{
	struct blob_attr *tb[__ICWMP_DM_MAX];
	struct dmctx dmctx = {0};
	const char *cmd, *path, *value = "", *key = "", *file = NULL;
	int fault = 0, dmcmd = -1, changed = 0, reloaded = 0, file_err = 0;
	unsigned int count = 0, pending;
	struct dm_parameter *n;

	(void)obj; (void)method;
	blobmsg_parse(icwmp_dm_policy, __ICWMP_DM_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[ICWMP_DM_CMD] || !tb[ICWMP_DM_PATH])
		return UBUS_STATUS_INVALID_ARGUMENT;
	cmd = blobmsg_data(tb[ICWMP_DM_CMD]);
	path = blobmsg_data(tb[ICWMP_DM_PATH]);
	if (tb[ICWMP_DM_VALUE])
		value = blobmsg_data(tb[ICWMP_DM_VALUE]);
	if (tb[ICWMP_DM_KEY])
		key = blobmsg_data(tb[ICWMP_DM_KEY]);
	if (tb[ICWMP_DM_FILE])
		file = blobmsg_data(tb[ICWMP_DM_FILE]);

	CWMP_LOG(INFO, "ubus dm %s %s", cmd, path);

	pthread_mutex_lock(&(cwmp_main.mutex_session_send));
	cwmp_dm_ctx_init(&cwmp_main, &dmctx);
	blob_buf_init(&bb, 0);

	if (strcmp(cmd, "get") == 0) {
		dmcmd = CMD_GET_VALUE;
		fault = dm_entry_param_method(&dmctx, CMD_GET_VALUE, (char *)path, NULL, NULL);
	} else if (strcmp(cmd, "names") == 0) {
		int next = tb[ICWMP_DM_NEXT_LEVEL] ? blobmsg_get_bool(tb[ICWMP_DM_NEXT_LEVEL]) : 0;

		dmcmd = CMD_GET_NAME;
		fault = dm_entry_param_method(&dmctx, CMD_GET_NAME, (char *)path, next ? "1" : "0", NULL);
	} else if (strcmp(cmd, "set") == 0) {
		dmcmd = CMD_SET_VALUE;
		if (!tb[ICWMP_DM_VALUE]) {
			fault = FAULT_9003;
		} else {
			fault = dm_entry_param_method(&dmctx, CMD_SET_VALUE, (char *)path, (char *)value, NULL);
			if (!fault)
				fault = dm_entry_apply(&dmctx, CMD_SET_VALUE, (char *)key, NULL);
			changed = !fault;
		}
	} else if (strcmp(cmd, "add") == 0) {
		dmcmd = CMD_ADD_OBJECT;
		fault = dm_entry_param_method(&dmctx, CMD_ADD_OBJECT, (char *)path, (char *)key, NULL);
		changed = !fault;
	} else if (strcmp(cmd, "del") == 0) {
		dmcmd = CMD_DEL_OBJECT;
		fault = dm_entry_param_method(&dmctx, CMD_DEL_OBJECT, (char *)path, (char *)key, NULL);
		changed = !fault;
	} else if (strcmp(cmd, "attr") == 0) {
		/* GetParameterAttributes: "notification" per parameter */
		dmcmd = CMD_GET_NOTIFICATION;
		fault = dm_entry_param_method(&dmctx, CMD_GET_NOTIFICATION, (char *)path, NULL, NULL);
	} else if (strcmp(cmd, "setattr") == 0) {
		/* SetParameterAttributes with NotificationChange=true, value = 0|1|2 */
		dmcmd = CMD_SET_NOTIFICATION;
		if (!tb[ICWMP_DM_VALUE]) {
			fault = FAULT_9003;
		} else {
			fault = dm_entry_param_method(&dmctx, CMD_SET_NOTIFICATION, (char *)path, (char *)value, "1");
			if (!fault)
				fault = dm_entry_apply(&dmctx, CMD_SET_NOTIFICATION, NULL, NULL);
			changed = !fault;
		}
	} else if (strcmp(cmd, "inform") == 0) {
		/* the forced-inform parameters of the next Inform (xml.c
		 * xml_prepare_msg_inform), DeviceId aside */
		dmcmd = CMD_INFORM;
		fault = dm_entry_param_method(&dmctx, CMD_INFORM, NULL, NULL, NULL);
	} else {
		fault = FAULT_9003;
	}

	blobmsg_add_string(&bb, "cmd", cmd);
	blobmsg_add_string(&bb, "root", dmroot);
	blobmsg_add_u32(&bb, "fault", fault);
	dm_add_fault_list(&dmctx);
	if (!fault) {
		switch (dmcmd) {
		case CMD_GET_VALUE:
		case CMD_GET_NAME:
		case CMD_GET_NOTIFICATION:
		case CMD_INFORM:
			list_for_each_entry(n, &dmctx.list_parameter, list)
				count++;
			blobmsg_add_u32(&bb, "count", count);
			if (file) {
				file_err = dm_write_param_file(&dmctx, dmcmd, file);
				blobmsg_add_string(&bb, "file", file_err ? "" : file);
			} else {
				dm_add_param_list(&dmctx, dmcmd);
			}
			if (dmcmd == CMD_INFORM) {
				void *t = blobmsg_open_table(&bb, "deviceid");

				blobmsg_add_string(&bb, "manufacturer", cwmp_main.deviceid.manufacturer ? cwmp_main.deviceid.manufacturer : "");
				blobmsg_add_string(&bb, "oui", cwmp_main.deviceid.oui ? cwmp_main.deviceid.oui : "");
				blobmsg_add_string(&bb, "product_class", cwmp_main.deviceid.productclass ? cwmp_main.deviceid.productclass : "");
				blobmsg_add_string(&bb, "serial_number", cwmp_main.deviceid.serialnumber ? cwmp_main.deviceid.serialnumber : "");
				blobmsg_close_table(&bb, t);
			}
			break;
		case CMD_ADD_OBJECT:
			blobmsg_add_string(&bb, "instance", dmctx.addobj_instance ? dmctx.addobj_instance : "");
			break;
		default:
			break;
		}
	}
	cwmp_dm_ctx_clean(&cwmp_main, &dmctx);

	if (changed) {
		/* what run_session_end_func() does for the data model at the end
		 * of an ACS session (cwmp.c), minus reboot/factory reset/diagnostics */
		apply_end_session();
		dm_entry_restart_services();
		if (dmcmd == CMD_SET_NOTIFICATION)
			dm_entry_reload_enabled_notify(DM_CWMP, cwmp_main.conf.amd_version, cwmp_main.conf.instance_mode);
	}
	pthread_mutex_unlock(&(cwmp_main.mutex_session_send));

	if (changed) {
		icwmp_platform_end_session();  /* bdk: ManagementServer sync + save to flash, mtk: config mirror */
		if ((end_session_flag & END_SESSION_RELOAD) &&
		    cwmp_main.session_status.last_status != SESSION_RUNNING) {
			CWMP_LOG(INFO, "ubus dm: config reload requested by the setter, reloading now");
			pthread_mutex_lock(&(cwmp_main.mutex_session_queue));
			cwmp_apply_acs_changes();
			pthread_mutex_unlock(&(cwmp_main.mutex_session_queue));
			end_session_flag &= ~END_SESSION_RELOAD;
			reloaded = 1;
		}
	}
	pending = (unsigned int)end_session_flag;
	if (changed) {
		blobmsg_add_u8(&bb, "reloaded", reloaded);
		dm_add_end_session(pending);
	}

	ubus_send_reply(ctx, req, bb.head);
	blob_buf_free(&bb);
	return 0;
}
