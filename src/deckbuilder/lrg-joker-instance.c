/* lrg-joker-instance.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-joker-instance.h"
#include "lrg-joker-def.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER

/**
 * LrgJokerInstance:
 *
 * Runtime instance of a joker.
 *
 * Joker instances track state that can change during a run:
 * - Edition (provides additional bonuses)
 * - Sell value (some jokers gain value)
 * - Trigger count (for statistics)
 * - Counter (for scaling jokers like Ice Cream)
 *
 * Edition bonuses (Balatro-style):
 * - Base: No bonus
 * - Foil: +50 Chips
 * - Holographic: +10 Mult
 * - Polychrome: X1.5 Mult
 * - Negative: +1 Joker slot (handled elsewhere)
 *
 * Since: 1.0
 */
struct _LrgJokerInstance
{
    GObject parent_instance;

    LrgJokerDef    *def;
    LrgJokerEdition edition;
    gint            sell_value;
    guint           times_triggered;
    gint64          counter;
    guint64         instance_id;
};

G_DEFINE_FINAL_TYPE (LrgJokerInstance, lrg_joker_instance, G_TYPE_OBJECT)

static guint64 next_instance_id = 1;

static void
lrg_joker_instance_finalize (GObject *object)
{
    LrgJokerInstance *self = LRG_JOKER_INSTANCE (object);

    g_clear_object (&self->def);

    G_OBJECT_CLASS (lrg_joker_instance_parent_class)->finalize (object);
}

static void
lrg_joker_instance_class_init (LrgJokerInstanceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_joker_instance_finalize;
}

static void
lrg_joker_instance_init (LrgJokerInstance *self)
{
    self->def = NULL;
    self->edition = LRG_JOKER_EDITION_BASE;
    self->sell_value = 0;
    self->times_triggered = 0;
    self->counter = 0;
    self->instance_id = next_instance_id++;
}

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
LrgJokerInstance *
lrg_joker_instance_new (LrgJokerDef *def)
{
    LrgJokerInstance *self;

    g_return_val_if_fail (LRG_IS_JOKER_DEF (def), NULL);

    self = g_object_new (LRG_TYPE_JOKER_INSTANCE, NULL);
    self->def = g_object_ref (def);
    self->sell_value = lrg_joker_def_get_sell_value (def);

    return self;
}

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
LrgJokerInstance *
lrg_joker_instance_new_with_edition (LrgJokerDef    *def,
                                     LrgJokerEdition edition)
{
    LrgJokerInstance *self;

    g_return_val_if_fail (LRG_IS_JOKER_DEF (def), NULL);

    self = lrg_joker_instance_new (def);
    self->edition = edition;

    return self;
}

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
LrgJokerDef *
lrg_joker_instance_get_def (LrgJokerInstance *self)
{
    g_return_val_if_fail (LRG_IS_JOKER_INSTANCE (self), NULL);

    return self->def;
}

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
const gchar *
lrg_joker_instance_get_id (LrgJokerInstance *self)
{
    g_return_val_if_fail (LRG_IS_JOKER_INSTANCE (self), NULL);

    return lrg_joker_def_get_id (self->def);
}

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
const gchar *
lrg_joker_instance_get_name (LrgJokerInstance *self)
{
    g_return_val_if_fail (LRG_IS_JOKER_INSTANCE (self), NULL);

    return lrg_joker_def_get_name (self->def);
}

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
LrgJokerEdition
lrg_joker_instance_get_edition (LrgJokerInstance *self)
{
    g_return_val_if_fail (LRG_IS_JOKER_INSTANCE (self), LRG_JOKER_EDITION_BASE);

    return self->edition;
}

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
void
lrg_joker_instance_set_edition (LrgJokerInstance *self,
                                LrgJokerEdition   edition)
{
    g_return_if_fail (LRG_IS_JOKER_INSTANCE (self));

    self->edition = edition;
}

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
gint
lrg_joker_instance_get_sell_value (LrgJokerInstance *self)
{
    g_return_val_if_fail (LRG_IS_JOKER_INSTANCE (self), 0);

    return self->sell_value;
}

/**
 * lrg_joker_instance_set_sell_value:
 * @self: a #LrgJokerInstance
 * @value: the new sell value
 *
 * Sets the sell value (for jokers that gain value).
 *
 * Since: 1.0
 */
void
lrg_joker_instance_set_sell_value (LrgJokerInstance *self,
                                   gint              value)
{
    g_return_if_fail (LRG_IS_JOKER_INSTANCE (self));

    self->sell_value = value;
}

/**
 * lrg_joker_instance_add_sell_value:
 * @self: a #LrgJokerInstance
 * @amount: amount to add
 *
 * Adds to the sell value.
 *
 * Since: 1.0
 */
void
lrg_joker_instance_add_sell_value (LrgJokerInstance *self,
                                   gint              amount)
{
    g_return_if_fail (LRG_IS_JOKER_INSTANCE (self));

    self->sell_value += amount;
}

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
guint
lrg_joker_instance_get_times_triggered (LrgJokerInstance *self)
{
    g_return_val_if_fail (LRG_IS_JOKER_INSTANCE (self), 0);

    return self->times_triggered;
}

/**
 * lrg_joker_instance_increment_trigger_count:
 * @self: a #LrgJokerInstance
 *
 * Increments the trigger count.
 *
 * Since: 1.0
 */
void
lrg_joker_instance_increment_trigger_count (LrgJokerInstance *self)
{
    g_return_if_fail (LRG_IS_JOKER_INSTANCE (self));

    self->times_triggered++;
}

/**
 * lrg_joker_instance_reset_trigger_count:
 * @self: a #LrgJokerInstance
 *
 * Resets the trigger count (e.g., at run start).
 *
 * Since: 1.0
 */
void
lrg_joker_instance_reset_trigger_count (LrgJokerInstance *self)
{
    g_return_if_fail (LRG_IS_JOKER_INSTANCE (self));

    self->times_triggered = 0;
}

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
gint64
lrg_joker_instance_get_counter (LrgJokerInstance *self)
{
    g_return_val_if_fail (LRG_IS_JOKER_INSTANCE (self), 0);

    return self->counter;
}

/**
 * lrg_joker_instance_set_counter:
 * @self: a #LrgJokerInstance
 * @value: the counter value
 *
 * Sets the counter value.
 *
 * Since: 1.0
 */
void
lrg_joker_instance_set_counter (LrgJokerInstance *self,
                                gint64            value)
{
    g_return_if_fail (LRG_IS_JOKER_INSTANCE (self));

    self->counter = value;
}

/**
 * lrg_joker_instance_add_counter:
 * @self: a #LrgJokerInstance
 * @amount: amount to add
 *
 * Adds to the counter value.
 *
 * Since: 1.0
 */
void
lrg_joker_instance_add_counter (LrgJokerInstance *self,
                                gint64            amount)
{
    g_return_if_fail (LRG_IS_JOKER_INSTANCE (self));

    self->counter += amount;
}

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
gint64
lrg_joker_instance_get_edition_chips (LrgJokerInstance *self)
{
    g_return_val_if_fail (LRG_IS_JOKER_INSTANCE (self), 0);

    if (self->edition == LRG_JOKER_EDITION_FOIL)
        return 50;

    return 0;
}

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
gint64
lrg_joker_instance_get_edition_mult (LrgJokerInstance *self)
{
    g_return_val_if_fail (LRG_IS_JOKER_INSTANCE (self), 0);

    if (self->edition == LRG_JOKER_EDITION_HOLOGRAPHIC)
        return 10;

    return 0;
}

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
gdouble
lrg_joker_instance_get_edition_x_mult (LrgJokerInstance *self)
{
    g_return_val_if_fail (LRG_IS_JOKER_INSTANCE (self), 1.0);

    if (self->edition == LRG_JOKER_EDITION_POLYCHROME)
        return 1.5;

    return 1.0;
}

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
guint64
lrg_joker_instance_get_instance_id (LrgJokerInstance *self)
{
    g_return_val_if_fail (LRG_IS_JOKER_INSTANCE (self), 0);

    return self->instance_id;
}
