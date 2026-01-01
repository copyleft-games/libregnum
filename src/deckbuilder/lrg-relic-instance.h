/* lrg-relic-instance.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRelicInstance - Runtime instance of a relic.
 *
 * Each LrgRelicInstance represents an acquired relic during a run,
 * tracking its counter state, enabled status, and providing access
 * to its definition.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-relic-def.h"

G_BEGIN_DECLS

#define LRG_TYPE_RELIC_INSTANCE (lrg_relic_instance_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRelicInstance, lrg_relic_instance, LRG, RELIC_INSTANCE, GObject)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_relic_instance_new:
 * @def: the relic definition
 *
 * Creates a new relic instance from a definition.
 *
 * Returns: (transfer full): a new #LrgRelicInstance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRelicInstance * lrg_relic_instance_new (LrgRelicDef *def);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_relic_instance_get_def:
 * @self: a #LrgRelicInstance
 *
 * Gets the relic's definition.
 *
 * Returns: (transfer none): the definition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRelicDef * lrg_relic_instance_get_def (LrgRelicInstance *self);

/**
 * lrg_relic_instance_get_id:
 * @self: a #LrgRelicInstance
 *
 * Gets the relic's ID (from definition).
 *
 * Returns: (transfer none): the ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_relic_instance_get_id (LrgRelicInstance *self);

/**
 * lrg_relic_instance_get_name:
 * @self: a #LrgRelicInstance
 *
 * Gets the relic's name (from definition).
 *
 * Returns: (transfer none): the name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_relic_instance_get_name (LrgRelicInstance *self);

/**
 * lrg_relic_instance_get_enabled:
 * @self: a #LrgRelicInstance
 *
 * Gets whether the relic is enabled.
 *
 * Returns: %TRUE if enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_relic_instance_get_enabled (LrgRelicInstance *self);

/**
 * lrg_relic_instance_set_enabled:
 * @self: a #LrgRelicInstance
 * @enabled: whether enabled
 *
 * Sets whether the relic is enabled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_instance_set_enabled (LrgRelicInstance *self,
                                      gboolean          enabled);

/**
 * lrg_relic_instance_get_counter:
 * @self: a #LrgRelicInstance
 *
 * Gets the current counter value.
 *
 * Returns: the counter value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_relic_instance_get_counter (LrgRelicInstance *self);

/**
 * lrg_relic_instance_set_counter:
 * @self: a #LrgRelicInstance
 * @counter: the new counter value
 *
 * Sets the counter value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_instance_set_counter (LrgRelicInstance *self,
                                      gint              counter);

/**
 * lrg_relic_instance_increment_counter:
 * @self: a #LrgRelicInstance
 *
 * Increments the counter by 1.
 *
 * Returns: %TRUE if counter reached max and was reset
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_relic_instance_increment_counter (LrgRelicInstance *self);

/**
 * lrg_relic_instance_reset_counter:
 * @self: a #LrgRelicInstance
 *
 * Resets the counter to 0.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_instance_reset_counter (LrgRelicInstance *self);

/**
 * lrg_relic_instance_get_uses:
 * @self: a #LrgRelicInstance
 *
 * Gets the number of times the relic has been triggered.
 *
 * Returns: the use count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_relic_instance_get_uses (LrgRelicInstance *self);

/**
 * lrg_relic_instance_increment_uses:
 * @self: a #LrgRelicInstance
 *
 * Increments the use count.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_instance_increment_uses (LrgRelicInstance *self);

/* ==========================================================================
 * State
 * ========================================================================== */

/**
 * lrg_relic_instance_get_data:
 * @self: a #LrgRelicInstance
 * @key: the data key
 *
 * Gets custom data stored on the instance.
 *
 * Returns: (transfer none) (nullable): the data value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gpointer lrg_relic_instance_get_data (LrgRelicInstance *self,
                                       const gchar      *key);

/**
 * lrg_relic_instance_set_data:
 * @self: a #LrgRelicInstance
 * @key: the data key
 * @data: (nullable): the data value
 * @destroy: (nullable): destroy function
 *
 * Sets custom data on the instance.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_instance_set_data (LrgRelicInstance *self,
                                   const gchar      *key,
                                   gpointer          data,
                                   GDestroyNotify    destroy);

/**
 * lrg_relic_instance_get_int_data:
 * @self: a #LrgRelicInstance
 * @key: the data key
 * @default_value: default value if not found
 *
 * Gets an integer value from custom data.
 *
 * Returns: the integer value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_relic_instance_get_int_data (LrgRelicInstance *self,
                                       const gchar      *key,
                                       gint              default_value);

/**
 * lrg_relic_instance_set_int_data:
 * @self: a #LrgRelicInstance
 * @key: the data key
 * @value: the integer value
 *
 * Sets an integer value in custom data.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_instance_set_int_data (LrgRelicInstance *self,
                                       const gchar      *key,
                                       gint              value);

/* ==========================================================================
 * Convenience
 * ========================================================================== */

/**
 * lrg_relic_instance_flash:
 * @self: a #LrgRelicInstance
 *
 * Emits the "flashed" signal for visual feedback.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_instance_flash (LrgRelicInstance *self);

G_END_DECLS
