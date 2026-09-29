/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Data model module registry.
 *
 *	The root of a data model is not written down anywhere: it is whatever
 *	modules were linked into this build, merged by object name.  A module is
 *	a piece of tree (a few DMOBJ rows and their DMLEAF tables) plus the list
 *	of paths it owns.  Portable modules live in tr098/, SDK specific ones in
 *	sdk/<name>/dm098/ -- deleting an SDK directory removes its modules from
 *	the build and nothing else has to be edited.
 *
 *	Writing one:
 *
 *	    static DMLEAF tFooParam[] = { ... {0} };
 *	    static DMOBJ  tFooObj[]   = {
 *	        {"Foo", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tFooParam, NULL},
 *	        {0}
 *	    };
 *	    static const char *const foo_paths[] = { "InternetGatewayDevice.Foo.", NULL };
 *	    static const struct dm_module foo_module = {
 *	        .name = "mtk-foo", .model = DM_MODEL_TR098, .order = DM_ORDER_SDK,
 *	        .objs = tFooObj, .paths = foo_paths,
 *	    };
 *	    DM_MODULE_REGISTER(foo_module);
 *
 *	Merge rules, applied in (order, name) sequence:
 *	  - two modules may declare the same object name: their children and
 *	    leaves are merged recursively, so "tr098-core" can own DeviceInfo and
 *	    an SDK module can add DeviceInfo.X_VENDOR_Foo without touching it,
 *	  - a later module (higher .order) wins on a field both modules set:
 *	    that is how an SDK overrides one getter of a portable object,
 *	  - a leaf declared twice keeps the later module's row.
 *
 *	The merged tree is built once, on first use, and never freed.  It uses
 *	plain calloc() on purpose: dmcalloc() memory dies with the dm context.
 */
#ifndef __DM_REGISTRY_H__
#define __DM_REGISTRY_H__

#include <stddef.h>
#include "dmtr098.h"

enum dm_model {
	DM_MODEL_TR098 = 0,
	DM_MODEL_TR181,
	__DM_MODEL_MAX
};

/* conventional .order values; any int works, lower is merged first */
#define DM_ORDER_CORE	0	/* portable tr098/ modules */
#define DM_ORDER_SDK	100	/* sdk/<name>/dm098/ modules, may override core */
#define DM_ORDER_LATE	200	/* product/vendor extensions on top of the SDK */

struct dm_module {
	const char *name;		/* unique, kebab case, shown by dm_registry_dump() */
	enum dm_model model;
	int order;
	DMOBJ *objs;			/* rows merged under the root object */
	DMLEAF *params;			/* leaves merged at the root level */
	const char *const *paths;	/* full paths owned, NULL terminated; see below */
	void (*init)(void);		/* optional, called once before the tree is built */
};

void dm_registry_add(const struct dm_module *mod);

/* Entry table for ctx->dm_entryobj: one row, the root object, whose children
 * are the merged modules of that model.  Never NULL (an empty model gives an
 * empty root, GetParameterNames then returns nothing instead of crashing). */
DMOBJ *dm_registry_entry(enum dm_model model);

/* Merged children of the root, for code that walks the tree itself. */
DMOBJ *dm_registry_root(enum dm_model model);
DMLEAF *dm_registry_root_params(enum dm_model model);

/* Claim check for SDKs that also serve part of the tree from outside the
 * static tables (sdk/mtk/compat/): 1 when path is inside a registered
 * module's owned path, 0 otherwise.  Both "IGD.Foo." and "IGD.Foo.Bar" match
 * the owned path "IGD.Foo.". */
int dm_registry_owns(enum dm_model model, const char *path);
/*
 * Spelling of .paths:
 *   "IGD.Foo."            the object and everything under it
 *   "IGD.Foo.Bar"         that one leaf, exactly (no prefix swallowing)
 *   "IGD.Foo.{i}.Bar"     that leaf of every instance of Foo
 * The wildcard is what lets a module own part of an instanced object without
 * listing the instances, and without claiming leaves a bridge still serves.
 */
/* 1 when an owned path starts with the given prefix, i.e. a request for
 * prefix would also return rows the static tree answers itself. */
int dm_registry_covers(enum dm_model model, const char *prefix);

/* Number of registered modules, and a one line log of each (LOG level INFO). */
int dm_registry_count(enum dm_model model);
/* Number of overlapping .paths claims found when the model was built.  Two
 * modules claiming the same path (or one claiming a prefix of the other) make
 * dm_registry_owns() answer for an arbitrary one of them; each pair is logged
 * to stderr at build time.  Extend an object owned by another module by
 * declaring no .paths and letting the merge do it. */
int dm_registry_conflicts(enum dm_model model);
void dm_registry_dump(void);

#define DM_MODULE_REGISTER(sym)						\
	static void __attribute__((constructor)) sym##_register(void)	\
	{								\
		dm_registry_add(&sym);					\
	}

#endif
