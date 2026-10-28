/* lrg-type-check-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Cached instance type checks for per-object hot paths. Not installed,
 * not introspected.
 *
 * G_TYPE_CHECK_INSTANCE_TYPE() only has an inline fast path for an exact
 * type match. Checking a subclass instance against an abstract or
 * derivable base (every LrgTween is an LrgTweenBase, every game object a
 * GrlEntity) or against an interface always calls into the type system.
 * Conformance never changes once true -- a type does not stop being a
 * subtype, or stop implementing an interface -- so remembering the last
 * instance type that passed is exact: the check still runs, and still
 * fails, for anything that does not conform. A source file opts in by
 * redefining its own LRG_IS_* macro over lrg_type_check_instance_cached(),
 * which keeps the text of g_return_*_if_fail() messages unchanged.
 *
 * The cache is one GType-sized word read and written atomically, so it is
 * thread-safe without a lock; a lost race only costs a full check.
 * (graylib carries the same helper for its own types.)
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

/*
 * lrg_type_check_instance_cached:
 * @instance: (nullable): a #GTypeInstance, or %NULL
 * @type: the type @instance must conform to
 * @cache: the caller's cache word, zero-initialised
 *
 * Same result as G_TYPE_CHECK_INSTANCE_TYPE (@instance, @type), skipping
 * the type system call when @instance's type matches the cached one.
 *
 * Returns: %TRUE if @instance is non-%NULL and conforms to @type
 */
static inline gboolean
lrg_type_check_instance_cached (gconstpointer  instance,
                                GType          type,
                                gsize         *cache)
{
    const GTypeInstance *inst = instance;
    GType                itype;

    if (G_UNLIKELY (inst == NULL || inst->g_class == NULL))
        return FALSE;

    itype = inst->g_class->g_type;
    if (G_LIKELY (itype == type ||
                  itype == (GType) g_atomic_pointer_get (cache)))
        return TRUE;

    /* Miss: ask the type system, and remember a pass. */
    if (!g_type_check_instance_is_a ((GTypeInstance *) inst, type))
        return FALSE;
    g_atomic_pointer_set (cache, (gsize) itype);
    return TRUE;
}

G_END_DECLS
