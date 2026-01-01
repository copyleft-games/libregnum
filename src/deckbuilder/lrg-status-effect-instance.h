/* lrg-status-effect-instance.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgStatusEffectInstance - Runtime status effect instance.
 *
 * This is a boxed type that represents an active status effect on a combatant.
 * It holds a reference to the definition and tracks the current stack count.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-status-effect-def.h"

G_BEGIN_DECLS

#define LRG_TYPE_STATUS_EFFECT_INSTANCE (lrg_status_effect_instance_get_type ())

/**
 * LrgStatusEffectInstance:
 *
 * An active status effect instance on a combatant.
 *
 * Since: 1.0
 */
typedef struct _LrgStatusEffectInstance LrgStatusEffectInstance;

LRG_AVAILABLE_IN_ALL
GType lrg_status_effect_instance_get_type (void) G_GNUC_CONST;

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_status_effect_instance_new:
 * @def: (transfer none): the status effect definition
 * @stacks: initial stack count
 *
 * Creates a new status effect instance.
 *
 * Returns: (transfer full): a new #LrgStatusEffectInstance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgStatusEffectInstance * lrg_status_effect_instance_new (LrgStatusEffectDef *def,
                                                           gint                stacks);

/**
 * lrg_status_effect_instance_copy:
 * @self: a #LrgStatusEffectInstance
 *
 * Creates a copy of a status effect instance.
 *
 * Returns: (transfer full): a copy of @self
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgStatusEffectInstance * lrg_status_effect_instance_copy (LrgStatusEffectInstance *self);

/**
 * lrg_status_effect_instance_free:
 * @self: (nullable): a #LrgStatusEffectInstance
 *
 * Frees a status effect instance.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_instance_free (LrgStatusEffectInstance *self);

/* ==========================================================================
 * Accessors
 * ========================================================================== */

/**
 * lrg_status_effect_instance_get_def:
 * @self: a #LrgStatusEffectInstance
 *
 * Gets the status effect definition.
 *
 * Returns: (transfer none): the definition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgStatusEffectDef * lrg_status_effect_instance_get_def (LrgStatusEffectInstance *self);

/**
 * lrg_status_effect_instance_get_id:
 * @self: a #LrgStatusEffectInstance
 *
 * Gets the status effect ID (from definition).
 *
 * Returns: (transfer none): the status ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_status_effect_instance_get_id (LrgStatusEffectInstance *self);

/**
 * lrg_status_effect_instance_get_stacks:
 * @self: a #LrgStatusEffectInstance
 *
 * Gets the current stack count.
 *
 * Returns: the stack count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_status_effect_instance_get_stacks (LrgStatusEffectInstance *self);

/**
 * lrg_status_effect_instance_set_stacks:
 * @self: a #LrgStatusEffectInstance
 * @stacks: the new stack count
 *
 * Sets the stack count. Respects max stacks from definition.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_instance_set_stacks (LrgStatusEffectInstance *self,
                                             gint                     stacks);

/**
 * lrg_status_effect_instance_add_stacks:
 * @self: a #LrgStatusEffectInstance
 * @amount: stacks to add (can be negative)
 *
 * Adds stacks to the current count. Respects max stacks.
 *
 * Returns: the new stack count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_status_effect_instance_add_stacks (LrgStatusEffectInstance *self,
                                             gint                     amount);

/**
 * lrg_status_effect_instance_remove_stacks:
 * @self: a #LrgStatusEffectInstance
 * @amount: stacks to remove
 *
 * Removes stacks from the current count.
 *
 * Returns: the new stack count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_status_effect_instance_remove_stacks (LrgStatusEffectInstance *self,
                                                gint                     amount);

/**
 * lrg_status_effect_instance_is_expired:
 * @self: a #LrgStatusEffectInstance
 *
 * Checks if the status has expired (stacks <= 0).
 *
 * Returns: %TRUE if expired
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_status_effect_instance_is_expired (LrgStatusEffectInstance *self);

/* ==========================================================================
 * Convenience Accessors (from definition)
 * ========================================================================== */

/**
 * lrg_status_effect_instance_get_name:
 * @self: a #LrgStatusEffectInstance
 *
 * Gets the display name (from definition).
 *
 * Returns: (transfer none): the status name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_status_effect_instance_get_name (LrgStatusEffectInstance *self);

/**
 * lrg_status_effect_instance_get_effect_type:
 * @self: a #LrgStatusEffectInstance
 *
 * Gets the effect type (from definition).
 *
 * Returns: the effect type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgStatusEffectType lrg_status_effect_instance_get_effect_type (LrgStatusEffectInstance *self);

/**
 * lrg_status_effect_instance_is_buff:
 * @self: a #LrgStatusEffectInstance
 *
 * Checks if this is a buff.
 *
 * Returns: %TRUE if buff
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_status_effect_instance_is_buff (LrgStatusEffectInstance *self);

/**
 * lrg_status_effect_instance_is_debuff:
 * @self: a #LrgStatusEffectInstance
 *
 * Checks if this is a debuff.
 *
 * Returns: %TRUE if debuff
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_status_effect_instance_is_debuff (LrgStatusEffectInstance *self);

/**
 * lrg_status_effect_instance_get_tooltip:
 * @self: a #LrgStatusEffectInstance
 *
 * Gets the tooltip text for current state.
 *
 * Returns: (transfer full): the tooltip text
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_status_effect_instance_get_tooltip (LrgStatusEffectInstance *self);

G_END_DECLS
