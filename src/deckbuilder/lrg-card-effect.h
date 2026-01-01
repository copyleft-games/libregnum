/* lrg-card-effect.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardEffect - Effect data container.
 *
 * This is a boxed type that stores effect type, target, parameters,
 * and flags. Effects are executed by LrgCardEffectExecutor implementations
 * registered with the LrgCardEffectRegistry.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_CARD_EFFECT (lrg_card_effect_get_type ())

/**
 * LrgCardEffect:
 *
 * A data container for card effects.
 *
 * Effects are defined by:
 * - effect_type: String identifying the effect ("damage", "block", etc.)
 * - target_type: How targets are selected
 * - flags: Modifiers for effect application
 * - params: Key-value parameters (amount, duration, etc.)
 *
 * Since: 1.0
 */
typedef struct _LrgCardEffect LrgCardEffect;

LRG_AVAILABLE_IN_ALL
GType lrg_card_effect_get_type (void) G_GNUC_CONST;

/* Constructors */

/**
 * lrg_card_effect_new:
 * @effect_type: the effect type identifier
 *
 * Creates a new card effect.
 *
 * Returns: (transfer full): a new #LrgCardEffect
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardEffect * lrg_card_effect_new (const gchar *effect_type);

/**
 * lrg_card_effect_copy:
 * @effect: a #LrgCardEffect
 *
 * Creates a copy of the effect.
 *
 * Returns: (transfer full): a copy of @effect
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardEffect * lrg_card_effect_copy (LrgCardEffect *effect);

/**
 * lrg_card_effect_free:
 * @effect: a #LrgCardEffect
 *
 * Frees the effect.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_effect_free (LrgCardEffect *effect);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgCardEffect, lrg_card_effect_free)

/* Type and target */

/**
 * lrg_card_effect_get_effect_type:
 * @effect: a #LrgCardEffect
 *
 * Gets the effect type identifier.
 *
 * Returns: (transfer none): the effect type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_card_effect_get_effect_type (LrgCardEffect *effect);

/**
 * lrg_card_effect_get_target_type:
 * @effect: a #LrgCardEffect
 *
 * Gets the target type for this effect.
 *
 * Returns: the target type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardTargetType lrg_card_effect_get_target_type (LrgCardEffect *effect);

/**
 * lrg_card_effect_set_target_type:
 * @effect: a #LrgCardEffect
 * @target_type: the target type
 *
 * Sets the target type for this effect.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_effect_set_target_type (LrgCardEffect     *effect,
                                       LrgCardTargetType  target_type);

/* Flags */

/**
 * lrg_card_effect_get_flags:
 * @effect: a #LrgCardEffect
 *
 * Gets the effect flags.
 *
 * Returns: the flags
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEffectFlags lrg_card_effect_get_flags (LrgCardEffect *effect);

/**
 * lrg_card_effect_set_flags:
 * @effect: a #LrgCardEffect
 * @flags: the flags
 *
 * Sets the effect flags.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_effect_set_flags (LrgCardEffect  *effect,
                                 LrgEffectFlags  flags);

/**
 * lrg_card_effect_add_flag:
 * @effect: a #LrgCardEffect
 * @flag: the flag to add
 *
 * Adds a flag to the effect.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_effect_add_flag (LrgCardEffect  *effect,
                                LrgEffectFlags  flag);

/**
 * lrg_card_effect_has_flag:
 * @effect: a #LrgCardEffect
 * @flag: the flag to check
 *
 * Checks if the effect has a flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_effect_has_flag (LrgCardEffect  *effect,
                                    LrgEffectFlags  flag);

/* Parameters */

/**
 * lrg_card_effect_set_param_int:
 * @effect: a #LrgCardEffect
 * @key: the parameter key
 * @value: the integer value
 *
 * Sets an integer parameter.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_effect_set_param_int (LrgCardEffect *effect,
                                     const gchar   *key,
                                     gint           value);

/**
 * lrg_card_effect_get_param_int:
 * @effect: a #LrgCardEffect
 * @key: the parameter key
 * @default_value: value if key not found
 *
 * Gets an integer parameter.
 *
 * Returns: the parameter value or @default_value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_card_effect_get_param_int (LrgCardEffect *effect,
                                     const gchar   *key,
                                     gint           default_value);

/**
 * lrg_card_effect_set_param_float:
 * @effect: a #LrgCardEffect
 * @key: the parameter key
 * @value: the float value
 *
 * Sets a float parameter.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_effect_set_param_float (LrgCardEffect *effect,
                                       const gchar   *key,
                                       gfloat         value);

/**
 * lrg_card_effect_get_param_float:
 * @effect: a #LrgCardEffect
 * @key: the parameter key
 * @default_value: value if key not found
 *
 * Gets a float parameter.
 *
 * Returns: the parameter value or @default_value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_card_effect_get_param_float (LrgCardEffect *effect,
                                         const gchar   *key,
                                         gfloat         default_value);

/**
 * lrg_card_effect_set_param_string:
 * @effect: a #LrgCardEffect
 * @key: the parameter key
 * @value: the string value
 *
 * Sets a string parameter.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_effect_set_param_string (LrgCardEffect *effect,
                                        const gchar   *key,
                                        const gchar   *value);

/**
 * lrg_card_effect_get_param_string:
 * @effect: a #LrgCardEffect
 * @key: the parameter key
 * @default_value: (nullable): value if key not found
 *
 * Gets a string parameter.
 *
 * Returns: (transfer none) (nullable): the parameter value or @default_value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_card_effect_get_param_string (LrgCardEffect *effect,
                                                 const gchar   *key,
                                                 const gchar   *default_value);

/**
 * lrg_card_effect_has_param:
 * @effect: a #LrgCardEffect
 * @key: the parameter key
 *
 * Checks if a parameter exists.
 *
 * Returns: %TRUE if the parameter exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_effect_has_param (LrgCardEffect *effect,
                                     const gchar   *key);

/* Priority */

/**
 * lrg_card_effect_get_priority:
 * @effect: a #LrgCardEffect
 *
 * Gets the effect priority for ordering in the effect stack.
 * Higher values execute first.
 *
 * Returns: the priority (default 0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_card_effect_get_priority (LrgCardEffect *effect);

/**
 * lrg_card_effect_set_priority:
 * @effect: a #LrgCardEffect
 * @priority: the priority value
 *
 * Sets the effect priority.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_effect_set_priority (LrgCardEffect *effect,
                                    gint           priority);

G_END_DECLS
