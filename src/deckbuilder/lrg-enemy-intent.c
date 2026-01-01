/* lrg-enemy-intent.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-enemy-intent.h"
#include "../lrg-log.h"

/**
 * SECTION:lrg-enemy-intent
 * @title: LrgEnemyIntent
 * @short_description: Enemy's intended action
 *
 * #LrgEnemyIntent represents what an enemy plans to do on their
 * turn. This information is displayed to the player so they can
 * make strategic decisions.
 *
 * Intent types include:
 * - Attack: deals damage (possibly multiple hits)
 * - Defend: gains block
 * - Buff: applies positive status to self
 * - Debuff: applies negative status to player
 * - Attack+Buff/Debuff: combination actions
 * - Special: escape, sleep, stun, unknown
 *
 * Since: 1.0
 */

struct _LrgEnemyIntent
{
    LrgIntentType type;
    gint          damage;
    gint          times;
    gint          block;
    gchar        *status_id;
    gint          stacks;
};

G_DEFINE_BOXED_TYPE (LrgEnemyIntent, lrg_enemy_intent,
                     lrg_enemy_intent_copy,
                     lrg_enemy_intent_free)

/**
 * lrg_enemy_intent_new:
 * @type: the intent type
 *
 * Creates a new enemy intent with the given type.
 *
 * Returns: (transfer full): a new #LrgEnemyIntent
 *
 * Since: 1.0
 */
LrgEnemyIntent *
lrg_enemy_intent_new (LrgIntentType type)
{
    LrgEnemyIntent *self;

    self = g_slice_new0 (LrgEnemyIntent);
    self->type = type;
    self->damage = 0;
    self->times = 1;
    self->block = 0;
    self->status_id = NULL;
    self->stacks = 0;

    return self;
}

/**
 * lrg_enemy_intent_new_attack:
 * @damage: base damage amount
 * @times: number of attacks (1 for single hit)
 *
 * Creates an attack intent with the specified damage and hit count.
 *
 * Returns: (transfer full): a new attack #LrgEnemyIntent
 *
 * Since: 1.0
 */
LrgEnemyIntent *
lrg_enemy_intent_new_attack (gint damage,
                             gint times)
{
    LrgEnemyIntent *self;

    self = lrg_enemy_intent_new (LRG_INTENT_ATTACK);
    self->damage = MAX (0, damage);
    self->times = MAX (1, times);

    return self;
}

/**
 * lrg_enemy_intent_new_defend:
 * @block: block amount
 *
 * Creates a defend intent with the specified block amount.
 *
 * Returns: (transfer full): a new defend #LrgEnemyIntent
 *
 * Since: 1.0
 */
LrgEnemyIntent *
lrg_enemy_intent_new_defend (gint block)
{
    LrgEnemyIntent *self;

    self = lrg_enemy_intent_new (LRG_INTENT_DEFEND);
    self->block = MAX (0, block);

    return self;
}

/**
 * lrg_enemy_intent_new_buff:
 * @status_id: (nullable): status effect to apply
 * @stacks: number of stacks
 *
 * Creates a buff intent (enemy buffing self).
 *
 * Returns: (transfer full): a new buff #LrgEnemyIntent
 *
 * Since: 1.0
 */
LrgEnemyIntent *
lrg_enemy_intent_new_buff (const gchar *status_id,
                           gint         stacks)
{
    LrgEnemyIntent *self;

    self = lrg_enemy_intent_new (LRG_INTENT_BUFF);
    self->status_id = g_strdup (status_id);
    self->stacks = MAX (0, stacks);

    return self;
}

/**
 * lrg_enemy_intent_new_debuff:
 * @status_id: (nullable): status effect to apply
 * @stacks: number of stacks
 *
 * Creates a debuff intent (enemy debuffing player).
 *
 * Returns: (transfer full): a new debuff #LrgEnemyIntent
 *
 * Since: 1.0
 */
LrgEnemyIntent *
lrg_enemy_intent_new_debuff (const gchar *status_id,
                             gint         stacks)
{
    LrgEnemyIntent *self;

    self = lrg_enemy_intent_new (LRG_INTENT_DEBUFF);
    self->status_id = g_strdup (status_id);
    self->stacks = MAX (0, stacks);

    return self;
}

/**
 * lrg_enemy_intent_copy:
 * @self: an #LrgEnemyIntent
 *
 * Creates a copy of an enemy intent.
 *
 * Returns: (transfer full): a copy of @self
 *
 * Since: 1.0
 */
LrgEnemyIntent *
lrg_enemy_intent_copy (const LrgEnemyIntent *self)
{
    LrgEnemyIntent *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_slice_new0 (LrgEnemyIntent);
    copy->type = self->type;
    copy->damage = self->damage;
    copy->times = self->times;
    copy->block = self->block;
    copy->status_id = g_strdup (self->status_id);
    copy->stacks = self->stacks;

    return copy;
}

/**
 * lrg_enemy_intent_free:
 * @self: an #LrgEnemyIntent
 *
 * Frees an enemy intent.
 *
 * Since: 1.0
 */
void
lrg_enemy_intent_free (LrgEnemyIntent *self)
{
    if (self == NULL)
        return;

    g_free (self->status_id);
    g_slice_free (LrgEnemyIntent, self);
}

LrgIntentType
lrg_enemy_intent_get_intent_type (const LrgEnemyIntent *self)
{
    g_return_val_if_fail (self != NULL, LRG_INTENT_UNKNOWN);
    return self->type;
}

void
lrg_enemy_intent_set_intent_type (LrgEnemyIntent *self,
                                  LrgIntentType   type)
{
    g_return_if_fail (self != NULL);
    self->type = type;
}

gint
lrg_enemy_intent_get_damage (const LrgEnemyIntent *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->damage;
}

void
lrg_enemy_intent_set_damage (LrgEnemyIntent *self,
                             gint            damage)
{
    g_return_if_fail (self != NULL);
    self->damage = MAX (0, damage);
}

gint
lrg_enemy_intent_get_times (const LrgEnemyIntent *self)
{
    g_return_val_if_fail (self != NULL, 1);
    return self->times;
}

void
lrg_enemy_intent_set_times (LrgEnemyIntent *self,
                            gint            times)
{
    g_return_if_fail (self != NULL);
    self->times = MAX (1, times);
}

gint
lrg_enemy_intent_get_block (const LrgEnemyIntent *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->block;
}

void
lrg_enemy_intent_set_block (LrgEnemyIntent *self,
                            gint            block)
{
    g_return_if_fail (self != NULL);
    self->block = MAX (0, block);
}

const gchar *
lrg_enemy_intent_get_status_id (const LrgEnemyIntent *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->status_id;
}

void
lrg_enemy_intent_set_status_id (LrgEnemyIntent *self,
                                const gchar    *status_id)
{
    g_return_if_fail (self != NULL);

    g_free (self->status_id);
    self->status_id = g_strdup (status_id);
}

gint
lrg_enemy_intent_get_stacks (const LrgEnemyIntent *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->stacks;
}

void
lrg_enemy_intent_set_stacks (LrgEnemyIntent *self,
                             gint            stacks)
{
    g_return_if_fail (self != NULL);
    self->stacks = MAX (0, stacks);
}

/**
 * lrg_enemy_intent_is_attack:
 * @self: an #LrgEnemyIntent
 *
 * Checks if this intent involves attacking.
 *
 * Returns: %TRUE if intent type is attack or attack combo
 *
 * Since: 1.0
 */
gboolean
lrg_enemy_intent_is_attack (const LrgEnemyIntent *self)
{
    g_return_val_if_fail (self != NULL, FALSE);

    switch (self->type)
    {
    case LRG_INTENT_ATTACK:
    case LRG_INTENT_ATTACK_BUFF:
    case LRG_INTENT_ATTACK_DEBUFF:
        return TRUE;
    default:
        return FALSE;
    }
}

/**
 * lrg_enemy_intent_get_total_damage:
 * @self: an #LrgEnemyIntent
 *
 * Gets the total damage for multi-hit attacks.
 *
 * Returns: damage × times, or 0 for non-attacks
 *
 * Since: 1.0
 */
gint
lrg_enemy_intent_get_total_damage (const LrgEnemyIntent *self)
{
    g_return_val_if_fail (self != NULL, 0);

    if (!lrg_enemy_intent_is_attack (self))
        return 0;

    return self->damage * self->times;
}
