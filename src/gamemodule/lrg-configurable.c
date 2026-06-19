/* lrg-configurable.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the LrgConfigurable interface.
 */

#include "lrg-configurable.h"

/**
 * SECTION:lrg-configurable
 * @title: LrgConfigurable
 * @short_description: Configure an object from a CLI-style argument vector
 *
 * The #LrgConfigurable interface lets a loader or host hand an object a
 * `main()`-style argument vector and have the object configure itself, without
 * the caller knowing the object's options. #LrgGameTemplate implements it, so
 * any loadable game module can be configured by overriding a single class vfunc.
 *
 * Since: 0.2
 */

G_DEFINE_INTERFACE (LrgConfigurable, lrg_configurable, G_TYPE_OBJECT)

static void
lrg_configurable_default_init (LrgConfigurableInterface *iface)
{
    /* No default method: an object that does not override apply_args accepts
     * any argument vector as a no-op (see lrg_configurable_apply_args). */
    (void) iface;
}

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
gboolean
lrg_configurable_apply_args (LrgConfigurable    *self,
                             const gchar *const *argv,
                             GError            **error)
{
    LrgConfigurableInterface *iface;

    g_return_val_if_fail (LRG_IS_CONFIGURABLE (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    iface = LRG_CONFIGURABLE_GET_IFACE (self);
    if (iface->apply_args == NULL)
        return TRUE;

    return iface->apply_args (self, argv, error);
}
