/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Data model module registry -- see dm_registry.h for the contract.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "dmtr098.h"
#include "dm_registry.h"

#define DM_REGISTRY_MAX	64

struct dm_model_state {
	const struct dm_module *mods[DM_REGISTRY_MAX];
	int nmods;
	DMOBJ *root;
	DMLEAF *root_params;
	DMOBJ entry[2];
	int built;
	int conflicts;
};

static struct dm_model_state models[__DM_MODEL_MAX];
static pthread_mutex_t registry_lock = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ */
/* registration                                                        */
/* ------------------------------------------------------------------ */

void dm_registry_add(const struct dm_module *mod)
{
	struct dm_model_state *st;
	int i;

	if (!mod || !mod->name || mod->model >= __DM_MODEL_MAX)
		return;

	st = &models[mod->model];

	pthread_mutex_lock(&registry_lock);
	if (st->nmods >= DM_REGISTRY_MAX || st->built) {
		/* built already: a module registered too late would be invisible,
		 * which is a build time mistake, not a runtime condition */
		pthread_mutex_unlock(&registry_lock);
		return;
	}
	for (i = 0; i < st->nmods; i++) {
		if (strcmp(st->mods[i]->name, mod->name) == 0) {
			pthread_mutex_unlock(&registry_lock);
			return;		/* same module linked twice */
		}
	}
	/* insertion sort on (order, name): the merge order is what decides
	 * which module wins a field, it must not depend on link order */
	for (i = st->nmods; i > 0; i--) {
		const struct dm_module *p = st->mods[i - 1];

		if (p->order < mod->order ||
		    (p->order == mod->order && strcmp(p->name, mod->name) <= 0))
			break;
		st->mods[i] = p;
	}
	st->mods[i] = mod;
	st->nmods++;
	pthread_mutex_unlock(&registry_lock);
}

/* ------------------------------------------------------------------ */
/* merge                                                               */
/* ------------------------------------------------------------------ */

static int count_obj(DMOBJ *t)
{
	int n = 0;

	if (!t)
		return 0;
	while (t[n].obj)
		n++;
	return n;
}

static int count_leaf(DMLEAF *t)
{
	int n = 0;

	if (!t)
		return 0;
	while (t[n].parameter)
		n++;
	return n;
}

static DMLEAF *merge_leaf(DMLEAF *a, DMLEAF *b)
{
	int na = count_leaf(a), nb = count_leaf(b), i, j, n = 0;
	DMLEAF *out;

	if (!na)
		return b;
	if (!nb)
		return a;

	out = calloc(na + nb + 1, sizeof(DMLEAF));
	if (!out)
		return a;
	for (i = 0; i < na; i++)
		out[n++] = a[i];
	for (j = 0; j < nb; j++) {
		for (i = 0; i < n; i++) {
			if (strcmp(out[i].parameter, b[j].parameter) == 0)
				break;
		}
		if (i < n)
			out[i] = b[j];	/* later module wins */
		else
			out[n++] = b[j];
	}
	memset(&out[n], 0, sizeof(DMLEAF));
	return out;
}

static DMOBJ *merge_obj(DMOBJ *a, DMOBJ *b);

static void merge_entry(DMOBJ *dst, DMOBJ *src)
{
	dst->nextobj = merge_obj(dst->nextobj, src->nextobj);
	dst->leaf = merge_leaf(dst->leaf, src->leaf);
	if (src->permission)
		dst->permission = src->permission;
	if (src->addobj)
		dst->addobj = src->addobj;
	if (src->delobj)
		dst->delobj = src->delobj;
	if (src->checkobj)
		dst->checkobj = src->checkobj;
	if (src->browseinstobj)
		dst->browseinstobj = src->browseinstobj;
	if (src->forced_inform)
		dst->forced_inform = src->forced_inform;
	if (src->notification)
		dst->notification = src->notification;
	if (src->get_linker)
		dst->get_linker = src->get_linker;
}

static DMOBJ *merge_obj(DMOBJ *a, DMOBJ *b)
{
	int na = count_obj(a), nb = count_obj(b), i, j, n = 0;
	DMOBJ *out;

	if (!na)
		return b;
	if (!nb)
		return a;

	out = calloc(na + nb + 1, sizeof(DMOBJ));
	if (!out)
		return a;
	for (i = 0; i < na; i++)
		out[n++] = a[i];
	for (j = 0; j < nb; j++) {
		for (i = 0; i < n; i++) {
			if (strcmp(out[i].obj, b[j].obj) == 0)
				break;
		}
		if (i < n)
			merge_entry(&out[i], &b[j]);
		else
			out[n++] = b[j];
	}
	memset(&out[n], 0, sizeof(DMOBJ));
	return out;
}

/* ------------------------------------------------------------------ */
/* claim check                                                         */
/* ------------------------------------------------------------------ */

static const char *const model_names[__DM_MODEL_MAX] = { "tr098", "tr181" };

/* Two modules claiming the same path is a build time mistake: dm_registry_owns()
 * answers for whichever module is scanned first, so the compat bridge would
 * filter on an arbitrary owner and one of the two trees would silently lose.
 * The intended way to extend an object another module owns is to declare no
 * .paths at all and let the merge do it (sdk/mtk/dm098/managementserver_core_mtk.c).
 * Reported once, at build time, never fatal -- a wrong tree at runtime is worse
 * than a noisy log on a developer build. */
static int check_claims(struct dm_model_state *st, const char *model_name)
{
	int i, j, k, l, n = 0;

	for (i = 0; i < st->nmods; i++) {
		const char *const *a = st->mods[i]->paths;

		if (!a)
			continue;
		for (j = i + 1; j < st->nmods; j++) {
			const char *const *b = st->mods[j]->paths;

			if (!b)
				continue;
			for (k = 0; a[k]; k++) {
				for (l = 0; b[l]; l++) {
					size_t la = strlen(a[k]), lb = strlen(b[l]);
					size_t shortest = la < lb ? la : lb;

					if (!shortest || strncmp(a[k], b[l], shortest) != 0)
						continue;
					/* equal, or one is a prefix of the other */
					fprintf(stderr,
						"libtr098: dm %s: path claim conflict \"%s\" (%s) vs \"%s\" (%s)\n",
						model_name, a[k], st->mods[i]->name,
						b[l], st->mods[j]->name);
					n++;
				}
			}
		}
	}
	if (n)
		fprintf(stderr, "libtr098: dm %s: %d path claim conflict(s), "
				"dm_registry_owns() is ambiguous for them\n", model_name, n);
	return n;
}

static void build(enum dm_model model)
{
	struct dm_model_state *st = &models[model];
	int i;

	if (st->built)
		return;

	for (i = 0; i < st->nmods; i++) {
		if (st->mods[i]->init)
			st->mods[i]->init();
	}
	for (i = 0; i < st->nmods; i++) {
		st->root = merge_obj(st->root, st->mods[i]->objs);
		st->root_params = merge_leaf(st->root_params, st->mods[i]->params);
	}

	st->conflicts = check_claims(st, model < __DM_MODEL_MAX ? model_names[model] : "?");

	st->entry[0].obj = (char *)&dmroot;
	st->entry[0].permission = &DMREAD;
	st->entry[0].forced_inform = &DMFINFRM;
	st->entry[0].notification = &DMNONE;
	st->entry[0].nextobj = st->root;
	st->entry[0].leaf = st->root_params;
	memset(&st->entry[1], 0, sizeof(DMOBJ));

	st->built = 1;
}

static struct dm_model_state *state_of(enum dm_model model)
{
	struct dm_model_state *st;

	if (model >= __DM_MODEL_MAX)
		model = DM_MODEL_TR098;
	st = &models[model];
	if (!st->built) {
		pthread_mutex_lock(&registry_lock);
		build(model);
		pthread_mutex_unlock(&registry_lock);
	}
	return st;
}

/* ------------------------------------------------------------------ */
/* lookup                                                              */
/* ------------------------------------------------------------------ */

DMOBJ *dm_registry_entry(enum dm_model model)
{
	return state_of(model)->entry;
}

DMOBJ *dm_registry_root(enum dm_model model)
{
	return state_of(model)->root;
}

DMLEAF *dm_registry_root_params(enum dm_model model)
{
	return state_of(model)->root_params;
}

int dm_registry_owns(enum dm_model model, const char *path)
{
	struct dm_model_state *st = state_of(model);
	int i, j;

	if (!path || !*path)
		return 0;
	for (i = 0; i < st->nmods; i++) {
		const char *const *p = st->mods[i]->paths;

		if (!p)
			continue;
		for (j = 0; p[j]; j++) {
			size_t l = strlen(p[j]);

			if (strncmp(path, p[j], l) == 0)
				return 1;
			/* "IGD.Foo" (no trailing dot) also belongs to "IGD.Foo." */
			if (l && p[j][l - 1] == '.' && strlen(path) == l - 1 &&
			    strncmp(path, p[j], l - 1) == 0)
				return 1;
		}
	}
	return 0;
}

int dm_registry_covers(enum dm_model model, const char *prefix)
{
	struct dm_model_state *st = state_of(model);
	size_t l;
	int i, j;

	if (!prefix)
		return 0;
	l = strlen(prefix);
	if (!l)
		return st->nmods ? 1 : 0;
	for (i = 0; i < st->nmods; i++) {
		const char *const *p = st->mods[i]->paths;

		if (!p)
			continue;
		for (j = 0; p[j]; j++) {
			if (strncmp(p[j], prefix, l) == 0)
				return 1;
		}
	}
	return 0;
}

int dm_registry_conflicts(enum dm_model model)
{
	if (model >= __DM_MODEL_MAX)
		return 0;
	return state_of(model)->conflicts;
}

int dm_registry_count(enum dm_model model)
{
	if (model >= __DM_MODEL_MAX)
		return 0;
	return models[model].nmods;
}

void dm_registry_dump(void)
{
	int m, i, j;

	for (m = 0; m < __DM_MODEL_MAX; m++) {
		struct dm_model_state *st = &models[m];

		for (i = 0; i < st->nmods; i++) {
			const struct dm_module *mod = st->mods[i];
			int np = 0;

			if (mod->paths) {
				for (j = 0; mod->paths[j]; j++)
					np++;
			}
			fprintf(stderr, "libtr098: dm module %-8s %-24s order %3d  %d root obj  %d root leaf  %d owned path\n",
				model_names[m], mod->name, mod->order,
				count_obj(mod->objs), count_leaf(mod->params), np);
		}
	}
}
