/* lrg-joker-instance.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_JOKER_INSTANCE (lrg_joker_instance_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgJokerInstance, lrg_joker_instance, LRG, JOKER_INSTANCE, GObject)

/**
 * LrgJokerInstance:
 *
 * Runtime instance of a joker.
 *
 * Tracks instance-specific state like:
 * - Edition (foil, holographic, polychrome, negative)
 * - Current sell value (may be modified)
 * - Times triggered
 * - Custom state for scaling jokers
 *
 * Since: 1.0
 */

/* Construction */

/**
 * lrg_joker_instance_new:
 * @def: the joker definition
 *
 * Creates a new joker instance from a definition.
 *
 * Returns: (transfer full): A new #LrgJokerInstance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgJokerInstance *
lrg_joker_instance_new (LrgJokerDef *def);

/**
 * lrg_joker_instance_new_with_edition:
 * @def: the joker definition
 * @edition: the edition
 *
 * Creates a new joker instance with a specific edition.
 *
 * Returns: (transfer full): A new #LrgJokerInstance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgJokerInstance *
lrg_joker_instance_new_with_edition (LrgJokerDef    *def,
                                     LrgJokerEdition edition);

/* Definition */

/**
 * lrg_joker_instance_get_def:
 * @self: a #LrgJokerInstance
 *
 * Gets the joker definition.
 *
 * Returns: (transfer none): the #LrgJokerDef
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgJokerDef *
lrg_joker_instance_get_def (LrgJokerInstance *self);

/**
 * lrg_joker_instance_get_id:
 * @self: a #LrgJokerInstance
 *
 * Gets the joker ID from the definition.
 *
 * Returns: (transfer none): the ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_joker_instance_get_id (LrgJokerInstance *self);

/**
 * lrg_joker_instance_get_name:
 * @self: a #LrgJokerInstance
 *
 * Gets the joker name from the definition.
 *
 * Returns: (transfer none): the name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_joker_instance_get_name (LrgJokerInstance *self);

/* Edition */

/**
 * lrg_joker_instance_get_edition:
 * @self: a #LrgJokerInstance
 *
 * Gets the edition of this joker instance.
 *
 * Returns: the #LrgJokerEdition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgJokerEdition
lrg_joker_instance_get_edition (LrgJokerInstance *self);

/**
 * lrg_joker_instance_set_edition:
 * @self: a #LrgJokerInstance
 * @edition: the new edition
 *
 * Sets the edition. Edition provides additional bonuses:
 * - Foil: +50 Chips
 * - Holographic: +10 Mult
 * - Polychrome: X1.5 Mult
 * - Negative: +1 Joker slot
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_instance_set_edition (LrgJokerInstance *self,
                                LrgJokerEdition   edition);

/* Value */

/**
 * lrg_joker_instance_get_sell_value:
 * @self: a #LrgJokerInstance
 *
 * Gets the current sell value (may differ from definition).
 *
 * Returns: the sell value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_joker_instance_get_sell_value (LrgJokerInstance *self);

/**
 * lrg_joker_instance_set_sell_value:
 * @self: a #LrgJokerInstance
 * @value: the new sell value
 *
 * Sets the sell value (for jokers that gain value).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_instance_set_sell_value (LrgJokerInstance *self,
                                   gint              value);

/**
 * lrg_joker_instance_add_sell_value:
 * @self: a #LrgJokerInstance
 * @amount: amount to add
 *
 * Adds to the sell value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_instance_add_sell_value (LrgJokerInstance *self,
                                   gint              amount);

/* Trigger Tracking */

/**
 * lrg_joker_instance_get_times_triggered:
 * @self: a #LrgJokerInstance
 *
 * Gets the number of times this joker has triggered this run.
 *
 * Returns: the trigger count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_joker_instance_get_times_triggered (LrgJokerInstance *self);

/**
 * lrg_joker_instance_increment_trigger_count:
 * @self: a #LrgJokerInstance
 *
 * Increments the trigger count.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_instance_increment_trigger_count (LrgJokerInstance *self);

/**
 * lrg_joker_instance_reset_trigger_count:
 * @self: a #LrgJokerInstance
 *
 * Resets the trigger count (e.g., at run start).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_instance_reset_trigger_count (LrgJokerInstance *self);

/* Custom State (for scaling jokers) */

/**
 * lrg_joker_instance_get_counter:
 * @self: a #LrgJokerInstance
 *
 * Gets a generic counter value (for scaling jokers).
 *
 * Returns: the counter value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_joker_instance_get_counter (LrgJokerInstance *self);

/**
 * lrg_joker_instance_set_counter:
 * @self: a #LrgJokerInstance
 * @value: the counter value
 *
 * Sets the counter value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_instance_set_counter (LrgJokerInstance *self,
                                gint64            value);

/**
 * lrg_joker_instance_add_counter:
 * @self: a #LrgJokerInstance
 * @amount: amount to add
 *
 * Adds to the counter value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_instance_add_counter (LrgJokerInstance *self,
                                gint64            amount);

/* Edition Bonuses */

/**
 * lrg_joker_instance_get_edition_chips:
 * @self: a #LrgJokerInstance
 *
 * Gets bonus chips from the edition (Foil = +50).
 *
 * Returns: bonus chips from edition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_joker_instance_get_edition_chips (LrgJokerInstance *self);

/**
 * lrg_joker_instance_get_edition_mult:
 * @self: a #LrgJokerInstance
 *
 * Gets bonus mult from the edition (Holographic = +10).
 *
 * Returns: bonus mult from edition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_joker_instance_get_edition_mult (LrgJokerInstance *self);

/**
 * lrg_joker_instance_get_edition_x_mult:
 * @self: a #LrgJokerInstance
 *
 * Gets X-mult from the edition (Polychrome = X1.5).
 *
 * Returns: X-mult from edition (1.0 = no bonus)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_joker_instance_get_edition_x_mult (LrgJokerInstance *self);

/* Unique ID */

/**
 * lrg_joker_instance_get_instance_id:
 * @self: a #LrgJokerInstance
 *
 * Gets a unique ID for this joker instance.
 *
 * Returns: the unique instance ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_joker_instance_get_instance_id (LrgJokerInstance *self);

G_END_DECLS
