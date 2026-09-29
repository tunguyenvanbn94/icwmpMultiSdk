/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Stock OpenWrt platform (--with-platform=uci): the cwmp UCI config is the
 *	config of record, nothing to mirror.  See sdk/sdk.h.
 */
#include "cwmp.h"
#include "sdk/sdk.h"

int icwmp_platform_init(void)
{
	return 0;
}

int icwmp_platform_config_reload(void)
{
	return 0;
}

void icwmp_platform_config_reloaded(struct cwmp *cwmp)
{
	(void)cwmp;
}

int icwmp_platform_uloop_register(void)
{
	return 0;
}

void icwmp_platform_end_session(void)
{
}

void icwmp_platform_cleanup(void)
{
}
