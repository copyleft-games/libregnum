/* lrg-demo-gatable.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the LrgDemoGatable interface.
 */

#include "lrg-demo-gatable.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DEMO
#include "../lrg-log.h"

/**
 * SECTION:lrg-demo-gatable
 * @title: LrgDemoGatable
 * @short_description: Interface for demo-gatable content
 *
 * The #LrgDemoGatable interface provides methods for objects that
 * represent content that can be gated (restricted) in demo mode.
 *
 * Objects implementing this interface can be checked by the demo
 * manager to determine if they should be accessible in demo mode.
 *
 * ## Implementing the Interface
 *
 * To implement this interface, provide at minimum the get_content_id
 * virtual method. The other methods have sensible defaults.
 *
 * ```c
 * static void
 * my_level_demo_gatable_init (LrgDemoGatableInterface *iface)
 * {
 *     iface->get_content_id = my_level_get_content_id;
 *     iface->is_demo_content = my_level_is_demo_content;
 * }
 * ```
 *
 * Since: 1.0
 */

G_DEFINE_INTERFACE (LrgDemoGatable, lrg_demo_gatable, G_TYPE_OBJECT)

/* ==========================================================================
 * Default Implementations
 * ========================================================================== */

static gboolean
lrg_demo_gatable_default_is_demo_content (LrgDemoGatable *self)
{
    /* By default, all content is available in demo mode */
    return TRUE;
}

static const gchar *
lrg_demo_gatable_default_get_unlock_message (LrgDemoGatable *self)
{
    return "This content is only available in the full version.";
}

/* ==========================================================================
 * Interface Initialization
 * ========================================================================== */

static void
lrg_demo_gatable_default_init (LrgDemoGatableInterface *iface)
{
    /* Set default implementations */
    iface->is_demo_content = lrg_demo_gatable_default_is_demo_content;
    iface->get_unlock_message = lrg_demo_gatable_default_get_unlock_message;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_demo_gatable_get_content_id:
 * @self: a #LrgDemoGatable
 *
 * Gets the unique content identifier for this gatable object.
 *
 * This ID is used by the demo manager to determine if content
 * is available in demo mode.
 *
 * Returns: (transfer none): the content ID string
 *
 * Since: 1.0
 */
const gchar *
lrg_demo_gatable_get_content_id (LrgDemoGatable *self)
{
    LrgDemoGatableInterface *iface;

    g_return_val_if_fail (LRG_IS_DEMO_GATABLE (self), NULL);

    iface = LRG_DEMO_GATABLE_GET_IFACE (self);
    g_return_val_if_fail (iface->get_content_id != NULL, NULL);

    return iface->get_content_id (self);
}

/**
 * lrg_demo_gatable_is_demo_content:
 * @self: a #LrgDemoGatable
 *
 * Checks if this content should be available in demo mode.
 *
 * Returns: %TRUE if content is available in demo mode
 *
 * Since: 1.0
 */
gboolean
lrg_demo_gatable_is_demo_content (LrgDemoGatable *self)
{
    LrgDemoGatableInterface *iface;

    g_return_val_if_fail (LRG_IS_DEMO_GATABLE (self), FALSE);

    iface = LRG_DEMO_GATABLE_GET_IFACE (self);
    g_return_val_if_fail (iface->is_demo_content != NULL, FALSE);

    return iface->is_demo_content (self);
}

/**
 * lrg_demo_gatable_get_unlock_message:
 * @self: a #LrgDemoGatable
 *
 * Gets a user-facing message explaining why content is locked.
 *
 * Returns: (transfer none) (nullable): the unlock message, or %NULL
 *
 * Since: 1.0
 */
const gchar *
lrg_demo_gatable_get_unlock_message (LrgDemoGatable *self)
{
    LrgDemoGatableInterface *iface;

    g_return_val_if_fail (LRG_IS_DEMO_GATABLE (self), NULL);

    iface = LRG_DEMO_GATABLE_GET_IFACE (self);
    if (iface->get_unlock_message == NULL)
        return NULL;

    return iface->get_unlock_message (self);
}
