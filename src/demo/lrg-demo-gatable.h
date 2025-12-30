/* lrg-demo-gatable.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for objects that can be gated in demo mode.
 *
 * Any object that represents content that should be restricted
 * in demo mode should implement this interface. The demo manager
 * will check these objects before allowing access.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_DEMO_GATABLE (lrg_demo_gatable_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgDemoGatable, lrg_demo_gatable, LRG, DEMO_GATABLE, GObject)

/**
 * LrgDemoGatableInterface:
 * @parent_iface: parent interface
 * @get_content_id: returns the unique content identifier
 * @is_demo_content: checks if this content is available in demo
 * @get_unlock_message: gets a message explaining why content is locked
 *
 * Interface structure for #LrgDemoGatable.
 *
 * Implementors should provide at minimum the get_content_id method.
 * The other methods have default implementations.
 */
struct _LrgDemoGatableInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /**
     * LrgDemoGatableInterface::get_content_id:
     * @self: a #LrgDemoGatable
     *
     * Gets the unique content identifier for this gatable object.
     *
     * This ID is used by the demo manager to determine if content
     * is available in demo mode.
     *
     * Returns: (transfer none): the content ID string
     */
    const gchar * (*get_content_id) (LrgDemoGatable *self);

    /**
     * LrgDemoGatableInterface::is_demo_content:
     * @self: a #LrgDemoGatable
     *
     * Checks if this content should be available in demo mode.
     *
     * The default implementation returns %TRUE (content available).
     * Override to implement custom logic.
     *
     * Returns: %TRUE if content is available in demo mode
     */
    gboolean (*is_demo_content) (LrgDemoGatable *self);

    /**
     * LrgDemoGatableInterface::get_unlock_message:
     * @self: a #LrgDemoGatable
     *
     * Gets a user-facing message explaining why content is locked.
     *
     * The default implementation returns a generic message.
     *
     * Returns: (transfer none) (nullable): the unlock message, or %NULL
     */
    const gchar * (*get_unlock_message) (LrgDemoGatable *self);
};

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_demo_gatable_get_content_id:
 * @self: a #LrgDemoGatable
 *
 * Gets the unique content identifier for this gatable object.
 *
 * Returns: (transfer none): the content ID string
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_demo_gatable_get_content_id (LrgDemoGatable *self);

/**
 * lrg_demo_gatable_is_demo_content:
 * @self: a #LrgDemoGatable
 *
 * Checks if this content should be available in demo mode.
 *
 * Returns: %TRUE if content is available in demo mode
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_demo_gatable_is_demo_content (LrgDemoGatable *self);

/**
 * lrg_demo_gatable_get_unlock_message:
 * @self: a #LrgDemoGatable
 *
 * Gets a user-facing message explaining why content is locked.
 *
 * Returns: (transfer none) (nullable): the unlock message, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_demo_gatable_get_unlock_message (LrgDemoGatable *self);

G_END_DECLS
