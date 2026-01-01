/* lrg-card-effect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardEffect - Effect data container implementation.
 */

#include "lrg-card-effect.h"

struct _LrgCardEffect
{
    gchar             *effect_type;
    LrgCardTargetType  target_type;
    LrgEffectFlags     flags;
    gint               priority;
    GHashTable        *params_int;
    GHashTable        *params_float;
    GHashTable        *params_string;
};

G_DEFINE_BOXED_TYPE (LrgCardEffect, lrg_card_effect,
                     lrg_card_effect_copy, lrg_card_effect_free)

/**
 * lrg_card_effect_new:
 * @effect_type: the effect type identifier
 *
 * Creates a new card effect with the given type identifier.
 * The effect type string identifies which executor will handle
 * this effect (e.g., "damage", "block", "draw").
 *
 * Returns: (transfer full): a new #LrgCardEffect
 *
 * Since: 1.0
 */
LrgCardEffect *
lrg_card_effect_new (const gchar *effect_type)
{
    LrgCardEffect *effect;

    g_return_val_if_fail (effect_type != NULL, NULL);

    effect = g_new0 (LrgCardEffect, 1);
    effect->effect_type = g_strdup (effect_type);
    effect->target_type = LRG_CARD_TARGET_NONE;
    effect->flags = LRG_EFFECT_FLAG_NONE;
    effect->priority = 0;

    effect->params_int = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 g_free, NULL);
    effect->params_float = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, g_free);
    effect->params_string = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                    g_free, g_free);

    return effect;
}

/**
 * lrg_card_effect_copy:
 * @effect: a #LrgCardEffect
 *
 * Creates a deep copy of the effect, including all parameters.
 *
 * Returns: (transfer full): a copy of @effect
 *
 * Since: 1.0
 */
LrgCardEffect *
lrg_card_effect_copy (LrgCardEffect *effect)
{
    LrgCardEffect *copy;
    GHashTableIter iter;
    gpointer key, value;

    g_return_val_if_fail (effect != NULL, NULL);

    copy = lrg_card_effect_new (effect->effect_type);
    copy->target_type = effect->target_type;
    copy->flags = effect->flags;
    copy->priority = effect->priority;

    /* Copy integer parameters */
    g_hash_table_iter_init (&iter, effect->params_int);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        g_hash_table_insert (copy->params_int,
                             g_strdup ((const gchar *)key),
                             value);
    }

    /* Copy float parameters */
    g_hash_table_iter_init (&iter, effect->params_float);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        gfloat *float_copy = g_new (gfloat, 1);
        *float_copy = *(gfloat *)value;
        g_hash_table_insert (copy->params_float,
                             g_strdup ((const gchar *)key),
                             float_copy);
    }

    /* Copy string parameters */
    g_hash_table_iter_init (&iter, effect->params_string);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        g_hash_table_insert (copy->params_string,
                             g_strdup ((const gchar *)key),
                             g_strdup ((const gchar *)value));
    }

    return copy;
}

/**
 * lrg_card_effect_free:
 * @effect: a #LrgCardEffect
 *
 * Frees the effect and all associated data.
 *
 * Since: 1.0
 */
void
lrg_card_effect_free (LrgCardEffect *effect)
{
    if (effect == NULL)
        return;

    g_free (effect->effect_type);
    g_hash_table_unref (effect->params_int);
    g_hash_table_unref (effect->params_float);
    g_hash_table_unref (effect->params_string);
    g_free (effect);
}

/**
 * lrg_card_effect_get_effect_type:
 * @effect: a #LrgCardEffect
 *
 * Gets the effect type identifier. This string is used to look up
 * the appropriate executor in the effect registry.
 *
 * Returns: (transfer none): the effect type string
 *
 * Since: 1.0
 */
const gchar *
lrg_card_effect_get_effect_type (LrgCardEffect *effect)
{
    g_return_val_if_fail (effect != NULL, NULL);
    return effect->effect_type;
}

/**
 * lrg_card_effect_get_target_type:
 * @effect: a #LrgCardEffect
 *
 * Gets the target type for this effect. This determines how
 * targets are selected when the effect is executed.
 *
 * Returns: the target type
 *
 * Since: 1.0
 */
LrgCardTargetType
lrg_card_effect_get_target_type (LrgCardEffect *effect)
{
    g_return_val_if_fail (effect != NULL, LRG_CARD_TARGET_NONE);
    return effect->target_type;
}

/**
 * lrg_card_effect_set_target_type:
 * @effect: a #LrgCardEffect
 * @target_type: the target type
 *
 * Sets the target type for this effect.
 *
 * Since: 1.0
 */
void
lrg_card_effect_set_target_type (LrgCardEffect     *effect,
                                 LrgCardTargetType  target_type)
{
    g_return_if_fail (effect != NULL);
    effect->target_type = target_type;
}

/**
 * lrg_card_effect_get_flags:
 * @effect: a #LrgCardEffect
 *
 * Gets the effect flags. Flags modify how the effect is applied
 * (e.g., unblockable, piercing, lifesteal).
 *
 * Returns: the effect flags
 *
 * Since: 1.0
 */
LrgEffectFlags
lrg_card_effect_get_flags (LrgCardEffect *effect)
{
    g_return_val_if_fail (effect != NULL, LRG_EFFECT_FLAG_NONE);
    return effect->flags;
}

/**
 * lrg_card_effect_set_flags:
 * @effect: a #LrgCardEffect
 * @flags: the flags to set
 *
 * Sets the effect flags, replacing any existing flags.
 *
 * Since: 1.0
 */
void
lrg_card_effect_set_flags (LrgCardEffect  *effect,
                           LrgEffectFlags  flags)
{
    g_return_if_fail (effect != NULL);
    effect->flags = flags;
}

/**
 * lrg_card_effect_add_flag:
 * @effect: a #LrgCardEffect
 * @flag: the flag to add
 *
 * Adds a flag to the effect's existing flags using bitwise OR.
 *
 * Since: 1.0
 */
void
lrg_card_effect_add_flag (LrgCardEffect  *effect,
                          LrgEffectFlags  flag)
{
    g_return_if_fail (effect != NULL);
    effect->flags |= flag;
}

/**
 * lrg_card_effect_has_flag:
 * @effect: a #LrgCardEffect
 * @flag: the flag to check
 *
 * Checks if the effect has a specific flag set.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 1.0
 */
gboolean
lrg_card_effect_has_flag (LrgCardEffect  *effect,
                          LrgEffectFlags  flag)
{
    g_return_val_if_fail (effect != NULL, FALSE);
    return (effect->flags & flag) != 0;
}

/**
 * lrg_card_effect_set_param_int:
 * @effect: a #LrgCardEffect
 * @key: the parameter key
 * @value: the integer value
 *
 * Sets an integer parameter on the effect. Common parameters
 * include "amount" for damage/block, "count" for card draw, etc.
 *
 * Since: 1.0
 */
void
lrg_card_effect_set_param_int (LrgCardEffect *effect,
                               const gchar   *key,
                               gint           value)
{
    g_return_if_fail (effect != NULL);
    g_return_if_fail (key != NULL);

    g_hash_table_insert (effect->params_int,
                         g_strdup (key),
                         GINT_TO_POINTER (value));
}

/**
 * lrg_card_effect_get_param_int:
 * @effect: a #LrgCardEffect
 * @key: the parameter key
 * @default_value: value to return if key not found
 *
 * Gets an integer parameter from the effect.
 *
 * Returns: the parameter value or @default_value if not found
 *
 * Since: 1.0
 */
gint
lrg_card_effect_get_param_int (LrgCardEffect *effect,
                               const gchar   *key,
                               gint           default_value)
{
    gpointer value;

    g_return_val_if_fail (effect != NULL, default_value);
    g_return_val_if_fail (key != NULL, default_value);

    if (g_hash_table_lookup_extended (effect->params_int, key, NULL, &value))
        return GPOINTER_TO_INT (value);

    return default_value;
}

/**
 * lrg_card_effect_set_param_float:
 * @effect: a #LrgCardEffect
 * @key: the parameter key
 * @value: the float value
 *
 * Sets a float parameter on the effect. Useful for multipliers,
 * percentages, or other fractional values.
 *
 * Since: 1.0
 */
void
lrg_card_effect_set_param_float (LrgCardEffect *effect,
                                 const gchar   *key,
                                 gfloat         value)
{
    gfloat *float_ptr;

    g_return_if_fail (effect != NULL);
    g_return_if_fail (key != NULL);

    float_ptr = g_new (gfloat, 1);
    *float_ptr = value;
    g_hash_table_insert (effect->params_float,
                         g_strdup (key),
                         float_ptr);
}

/**
 * lrg_card_effect_get_param_float:
 * @effect: a #LrgCardEffect
 * @key: the parameter key
 * @default_value: value to return if key not found
 *
 * Gets a float parameter from the effect.
 *
 * Returns: the parameter value or @default_value if not found
 *
 * Since: 1.0
 */
gfloat
lrg_card_effect_get_param_float (LrgCardEffect *effect,
                                 const gchar   *key,
                                 gfloat         default_value)
{
    gfloat *value;

    g_return_val_if_fail (effect != NULL, default_value);
    g_return_val_if_fail (key != NULL, default_value);

    value = g_hash_table_lookup (effect->params_float, key);
    if (value != NULL)
        return *value;

    return default_value;
}

/**
 * lrg_card_effect_set_param_string:
 * @effect: a #LrgCardEffect
 * @key: the parameter key
 * @value: the string value
 *
 * Sets a string parameter on the effect. Useful for status effect
 * names, card IDs, or other textual data.
 *
 * Since: 1.0
 */
void
lrg_card_effect_set_param_string (LrgCardEffect *effect,
                                  const gchar   *key,
                                  const gchar   *value)
{
    g_return_if_fail (effect != NULL);
    g_return_if_fail (key != NULL);

    g_hash_table_insert (effect->params_string,
                         g_strdup (key),
                         g_strdup (value));
}

/**
 * lrg_card_effect_get_param_string:
 * @effect: a #LrgCardEffect
 * @key: the parameter key
 * @default_value: (nullable): value to return if key not found
 *
 * Gets a string parameter from the effect.
 *
 * Returns: (transfer none) (nullable): the parameter value or @default_value
 *
 * Since: 1.0
 */
const gchar *
lrg_card_effect_get_param_string (LrgCardEffect *effect,
                                  const gchar   *key,
                                  const gchar   *default_value)
{
    const gchar *value;

    g_return_val_if_fail (effect != NULL, default_value);
    g_return_val_if_fail (key != NULL, default_value);

    value = g_hash_table_lookup (effect->params_string, key);
    if (value != NULL)
        return value;

    return default_value;
}

/**
 * lrg_card_effect_has_param:
 * @effect: a #LrgCardEffect
 * @key: the parameter key
 *
 * Checks if a parameter exists in any of the parameter tables
 * (int, float, or string).
 *
 * Returns: %TRUE if the parameter exists
 *
 * Since: 1.0
 */
gboolean
lrg_card_effect_has_param (LrgCardEffect *effect,
                           const gchar   *key)
{
    g_return_val_if_fail (effect != NULL, FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    return g_hash_table_contains (effect->params_int, key) ||
           g_hash_table_contains (effect->params_float, key) ||
           g_hash_table_contains (effect->params_string, key);
}

/**
 * lrg_card_effect_get_priority:
 * @effect: a #LrgCardEffect
 *
 * Gets the effect priority for ordering in the effect stack.
 * Higher priority effects are resolved first. Default is 0.
 *
 * Returns: the priority value
 *
 * Since: 1.0
 */
gint
lrg_card_effect_get_priority (LrgCardEffect *effect)
{
    g_return_val_if_fail (effect != NULL, 0);
    return effect->priority;
}

/**
 * lrg_card_effect_set_priority:
 * @effect: a #LrgCardEffect
 * @priority: the priority value
 *
 * Sets the effect priority. Effects with higher priority values
 * are resolved before those with lower values.
 *
 * Since: 1.0
 */
void
lrg_card_effect_set_priority (LrgCardEffect *effect,
                              gint           priority)
{
    g_return_if_fail (effect != NULL);
    effect->priority = priority;
}
