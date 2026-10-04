/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	UCI platform (--with-platform=uci): keeps the original iopsys behaviour.
 *	All storage is UCI, dmuci_commit()/dmuci_revert() in dmentry.c already do
 *	the transaction, so the hooks here are almost empty.  The MTK platform
 *	(platform/mtk/dmplatform_mtk.c) builds on the same UCI storage and adds
 *	the script data model on top.
 */
#include "sdk/sdk.h"
#include "dmuci.h"
#include "dmubus.h"

extern struct list_head head_package_change;


int dm_platform_ctx_init(struct dmctx *ctx)
{
	(void)ctx;
	return 0;
}

int dm_platform_ctx_clean(struct dmctx *ctx)
{
	(void)ctx;
	return 0;
}

int dm_platform_commit(struct dmctx *ctx, const char *parameter_key)
{
	(void)ctx; (void)parameter_key;
	return 0;
}

void dm_platform_revert(struct dmctx *ctx)
{
	(void)ctx;
}

int dm_platform_restart_services(void)
{
	struct package_change *pc;

	list_for_each_entry(pc, &head_package_change, list) {
		if(strcmp(pc->package, "cwmp") == 0)
			continue;
		dmubus_call_set("uci", "commit", UBUS_ARGS{{"config", pc->package, String}}, 1);
	}
	free_all_list_package_change(&head_package_change);

	return 0;
}

const char *dm_platform_name(void)
{
	return "uci";
}

int dm_platform_param_method(struct dmctx *ctx, int cmd, char *inparam, char *arg1, int *fault)
{
	(void)ctx; (void)cmd; (void)inparam; (void)arg1; (void)fault;
	return 0;
}

int dm_platform_select_root(struct dmctx *ctx)
{
	(void)ctx;
	return 0;
}

int dm_platform_enabled_notify(struct dmctx *ctx)
{
	(void)ctx;
	return 0;
}

int dm_platform_enabled_notify_check_value_change(struct dmctx *ctx)
{
	(void)ctx;
	return 0;
}

/* GET_VALUE is in-process here: nothing to fetch ahead */
int dm_platform_prefetch_values(char **params, int n)
{
	(void)params;
	(void)n;
	return 0;
}

void dm_platform_prefetch_drop(void)
{
}
