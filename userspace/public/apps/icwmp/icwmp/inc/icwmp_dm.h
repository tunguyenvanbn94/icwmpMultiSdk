/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 */
#ifndef __ICWMP_DM_H__
#define __ICWMP_DM_H__

#include <libubox/blobmsg.h>
#include <libubus.h>

/* ubus "tr069 dm" (icwmp_dm.c): the CWMP RPCs (GPV/GPN/SPV/Add/Del/GPA/SPA,
 * Inform parameter list) on the data model from the shell, no ACS needed;
 * "file" dumps the result to a file instead of the ubus reply */
enum {
	ICWMP_DM_CMD,
	ICWMP_DM_PATH,
	ICWMP_DM_VALUE,
	ICWMP_DM_KEY,
	ICWMP_DM_NEXT_LEVEL,
	ICWMP_DM_FILE,
	__ICWMP_DM_MAX
};
extern const struct blobmsg_policy icwmp_dm_policy[__ICWMP_DM_MAX];
int icwmp_ubus_dm(struct ubus_context *ctx, struct ubus_object *obj,
		  struct ubus_request_data *req, const char *method,
		  struct blob_attr *msg);

#endif
