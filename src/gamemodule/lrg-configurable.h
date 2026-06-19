/* lrg-configurable.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for objects that can be configured from a CLI-style argument
 * vector. A generic loader (lrgldr), the standalone runner, or any embedding
 * host can hand a loaded module a `(const gchar *const *) argv` and let the
 * module parse it however it likes (GOption, hand-rolled, etc.) — without the
 * caller knowing any of the module's options.
 *
 * #LrgGameTemplate implements this interface and bridges it to an overridable
 * class vfunc, so every loadable game can opt in by overriding one method.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_CONFIGURABLE (lrg_configurable_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgConfigurable, lrg_configurable, LRG, CONFIGURABLE, GObject)

/**
 * LrgConfigurableInterface:
 * @parent_iface: parent interface
 * @apply_args: apply a CLI-style argument vector to the object
 *
 * Interface for objects configurable from an argument vector.
 *
 * Implementors parse @argv (a %NULL-terminated vector whose first element is a
 * program name, mirroring `main()`'s argv, so options begin at element 1) and
 * apply it to themselves. On a parse/validation error they should return %FALSE
 * and set @error. An implementor that does not override @apply_args is treated
 * as a no-op that accepts any arguments.
 *
 * Since: 0.2
 */
struct _LrgConfigurableInterface
{
    GTypeInterface parent_iface;

    /*< public >*/
    gboolean (*apply_args) (LrgConfigurable    *self,
                            const gchar *const *argv,
                            GError            **error);

    /*< private >*/
    gpointer _reserved[4];
};

/**
 * lrg_configurable_apply_args:
 * @self: a #LrgConfigurable
 * @argv: (array zero-terminated=1) (element-type utf8): a %NULL-terminated,
 *   `main()`-style argument vector (element 0 is the program name)
 * @error: (nullable): return location for an error
 *
 * Applies @argv to @self. If the implementor does not provide an @apply_args
 * method this is a successful no-op.
 *
 * Returns: %TRUE on success, %FALSE (with @error set) on a parse/validation
 *   failure.
 *
 * Since: 0.2
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_configurable_apply_args (LrgConfigurable    *self,
                             const gchar *const *argv,
                             GError            **error);

G_END_DECLS
