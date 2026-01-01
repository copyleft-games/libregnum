/* lrg-card-event.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardEvent - Event data container implementation.
 */

#include "lrg-card-event.h"

struct _LrgCardEvent
{
    LrgCardEventType  event_type;
    gpointer          source;
    gpointer          target;
    gpointer          card;
    gint              amount;
    guint             turn;
    LrgEffectFlags    flags;
    gchar            *status_id;
    gboolean          cancelled;
};

G_DEFINE_BOXED_TYPE (LrgCardEvent, lrg_card_event,
                     lrg_card_event_copy, lrg_card_event_free)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_card_event_new:
 * @event_type: the type of event
 *
 * Creates a new event with the given type.
 *
 * Returns: (transfer full): a new #LrgCardEvent
 *
 * Since: 1.0
 */
LrgCardEvent *
lrg_card_event_new (LrgCardEventType event_type)
{
    LrgCardEvent *event;

    event = g_new0 (LrgCardEvent, 1);
    event->event_type = event_type;
    event->source = NULL;
    event->target = NULL;
    event->card = NULL;
    event->amount = 0;
    event->turn = 0;
    event->flags = LRG_EFFECT_FLAG_NONE;
    event->status_id = NULL;
    event->cancelled = FALSE;

    return event;
}

/**
 * lrg_card_event_copy:
 * @event: a #LrgCardEvent
 *
 * Creates a copy of the event.
 *
 * Returns: (transfer full): a copy of @event
 *
 * Since: 1.0
 */
LrgCardEvent *
lrg_card_event_copy (LrgCardEvent *event)
{
    LrgCardEvent *copy;

    g_return_val_if_fail (event != NULL, NULL);

    copy = lrg_card_event_new (event->event_type);
    copy->source = event->source;
    copy->target = event->target;
    copy->card = event->card;
    copy->amount = event->amount;
    copy->turn = event->turn;
    copy->flags = event->flags;
    copy->status_id = g_strdup (event->status_id);
    copy->cancelled = event->cancelled;

    return copy;
}

/**
 * lrg_card_event_free:
 * @event: a #LrgCardEvent
 *
 * Frees the event and all associated data.
 *
 * Since: 1.0
 */
void
lrg_card_event_free (LrgCardEvent *event)
{
    if (event == NULL)
        return;

    g_free (event->status_id);
    g_free (event);
}

/* ==========================================================================
 * Convenience Constructors
 * ========================================================================== */

/**
 * lrg_card_event_new_turn:
 * @event_type: LRG_CARD_EVENT_TURN_START or LRG_CARD_EVENT_TURN_END
 * @turn: the turn number
 *
 * Creates a turn start/end event.
 *
 * Returns: (transfer full): a new #LrgCardEvent
 *
 * Since: 1.0
 */
LrgCardEvent *
lrg_card_event_new_turn (LrgCardEventType event_type,
                         guint            turn)
{
    LrgCardEvent *event;

    event = lrg_card_event_new (event_type);
    event->turn = turn;

    return event;
}

/**
 * lrg_card_event_new_card:
 * @event_type: the card event type
 * @card: the card involved
 *
 * Creates a card event (drawn, played, discarded, exhausted).
 *
 * Returns: (transfer full): a new #LrgCardEvent
 *
 * Since: 1.0
 */
LrgCardEvent *
lrg_card_event_new_card (LrgCardEventType  event_type,
                         gpointer          card)
{
    LrgCardEvent *event;

    event = lrg_card_event_new (event_type);
    event->card = card;

    return event;
}

/**
 * lrg_card_event_new_damage:
 * @source: (nullable): the source of damage
 * @target: the target receiving damage
 * @amount: the damage amount
 * @flags: effect flags that applied to this damage
 *
 * Creates a damage event.
 *
 * Returns: (transfer full): a new #LrgCardEvent
 *
 * Since: 1.0
 */
LrgCardEvent *
lrg_card_event_new_damage (gpointer       source,
                           gpointer       target,
                           gint           amount,
                           LrgEffectFlags flags)
{
    LrgCardEvent *event;

    event = lrg_card_event_new (LRG_CARD_EVENT_DAMAGE_DEALT);
    event->source = source;
    event->target = target;
    event->amount = amount;
    event->flags = flags;

    return event;
}

/**
 * lrg_card_event_new_block:
 * @target: the entity gaining block
 * @amount: the block amount
 *
 * Creates a block gained event.
 *
 * Returns: (transfer full): a new #LrgCardEvent
 *
 * Since: 1.0
 */
LrgCardEvent *
lrg_card_event_new_block (gpointer target,
                          gint     amount)
{
    LrgCardEvent *event;

    event = lrg_card_event_new (LRG_CARD_EVENT_BLOCK_GAINED);
    event->target = target;
    event->amount = amount;

    return event;
}

/**
 * lrg_card_event_new_heal:
 * @target: the entity being healed
 * @amount: the heal amount
 *
 * Creates a heal event.
 *
 * Returns: (transfer full): a new #LrgCardEvent
 *
 * Since: 1.0
 */
LrgCardEvent *
lrg_card_event_new_heal (gpointer target,
                         gint     amount)
{
    LrgCardEvent *event;

    event = lrg_card_event_new (LRG_CARD_EVENT_HEAL);
    event->target = target;
    event->amount = amount;

    return event;
}

/**
 * lrg_card_event_new_status:
 * @event_type: LRG_CARD_EVENT_STATUS_APPLIED or LRG_CARD_EVENT_STATUS_REMOVED
 * @target: the entity affected
 * @status_id: the status effect identifier
 * @stacks: the number of stacks
 *
 * Creates a status effect event.
 *
 * Returns: (transfer full): a new #LrgCardEvent
 *
 * Since: 1.0
 */
LrgCardEvent *
lrg_card_event_new_status (LrgCardEventType  event_type,
                           gpointer          target,
                           const gchar      *status_id,
                           gint              stacks)
{
    LrgCardEvent *event;

    event = lrg_card_event_new (event_type);
    event->target = target;
    event->status_id = g_strdup (status_id);
    event->amount = stacks;

    return event;
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

/**
 * lrg_card_event_get_event_type:
 * @event: a #LrgCardEvent
 *
 * Gets the event type.
 *
 * Returns: the event type
 *
 * Since: 1.0
 */
LrgCardEventType
lrg_card_event_get_event_type (LrgCardEvent *event)
{
    g_return_val_if_fail (event != NULL, LRG_CARD_EVENT_COMBAT_START);
    return event->event_type;
}

/**
 * lrg_card_event_get_source:
 * @event: a #LrgCardEvent
 *
 * Gets the source entity.
 *
 * Returns: (transfer none) (nullable): the source
 *
 * Since: 1.0
 */
gpointer
lrg_card_event_get_source (LrgCardEvent *event)
{
    g_return_val_if_fail (event != NULL, NULL);
    return event->source;
}

/**
 * lrg_card_event_set_source:
 * @event: a #LrgCardEvent
 * @source: (nullable): the source entity
 *
 * Sets the source entity.
 *
 * Since: 1.0
 */
void
lrg_card_event_set_source (LrgCardEvent *event,
                           gpointer      source)
{
    g_return_if_fail (event != NULL);
    event->source = source;
}

/**
 * lrg_card_event_get_target:
 * @event: a #LrgCardEvent
 *
 * Gets the target entity.
 *
 * Returns: (transfer none) (nullable): the target
 *
 * Since: 1.0
 */
gpointer
lrg_card_event_get_target (LrgCardEvent *event)
{
    g_return_val_if_fail (event != NULL, NULL);
    return event->target;
}

/**
 * lrg_card_event_set_target:
 * @event: a #LrgCardEvent
 * @target: (nullable): the target entity
 *
 * Sets the target entity.
 *
 * Since: 1.0
 */
void
lrg_card_event_set_target (LrgCardEvent *event,
                           gpointer      target)
{
    g_return_if_fail (event != NULL);
    event->target = target;
}

/**
 * lrg_card_event_get_card:
 * @event: a #LrgCardEvent
 *
 * Gets the card involved in this event.
 *
 * Returns: (transfer none) (nullable): the card
 *
 * Since: 1.0
 */
gpointer
lrg_card_event_get_card (LrgCardEvent *event)
{
    g_return_val_if_fail (event != NULL, NULL);
    return event->card;
}

/**
 * lrg_card_event_set_card:
 * @event: a #LrgCardEvent
 * @card: (nullable): the card
 *
 * Sets the card involved in this event.
 *
 * Since: 1.0
 */
void
lrg_card_event_set_card (LrgCardEvent *event,
                         gpointer      card)
{
    g_return_if_fail (event != NULL);
    event->card = card;
}

/**
 * lrg_card_event_get_amount:
 * @event: a #LrgCardEvent
 *
 * Gets the numeric amount (damage, block, heal, etc.).
 *
 * Returns: the amount
 *
 * Since: 1.0
 */
gint
lrg_card_event_get_amount (LrgCardEvent *event)
{
    g_return_val_if_fail (event != NULL, 0);
    return event->amount;
}

/**
 * lrg_card_event_set_amount:
 * @event: a #LrgCardEvent
 * @amount: the amount
 *
 * Sets the numeric amount.
 *
 * Since: 1.0
 */
void
lrg_card_event_set_amount (LrgCardEvent *event,
                           gint          amount)
{
    g_return_if_fail (event != NULL);
    event->amount = amount;
}

/**
 * lrg_card_event_get_turn:
 * @event: a #LrgCardEvent
 *
 * Gets the turn number when this event occurred.
 *
 * Returns: the turn number
 *
 * Since: 1.0
 */
guint
lrg_card_event_get_turn (LrgCardEvent *event)
{
    g_return_val_if_fail (event != NULL, 0);
    return event->turn;
}

/**
 * lrg_card_event_set_turn:
 * @event: a #LrgCardEvent
 * @turn: the turn number
 *
 * Sets the turn number.
 *
 * Since: 1.0
 */
void
lrg_card_event_set_turn (LrgCardEvent *event,
                         guint         turn)
{
    g_return_if_fail (event != NULL);
    event->turn = turn;
}

/**
 * lrg_card_event_get_flags:
 * @event: a #LrgCardEvent
 *
 * Gets the effect flags associated with this event.
 *
 * Returns: the flags
 *
 * Since: 1.0
 */
LrgEffectFlags
lrg_card_event_get_flags (LrgCardEvent *event)
{
    g_return_val_if_fail (event != NULL, LRG_EFFECT_FLAG_NONE);
    return event->flags;
}

/**
 * lrg_card_event_set_flags:
 * @event: a #LrgCardEvent
 * @flags: the flags
 *
 * Sets the effect flags.
 *
 * Since: 1.0
 */
void
lrg_card_event_set_flags (LrgCardEvent   *event,
                          LrgEffectFlags  flags)
{
    g_return_if_fail (event != NULL);
    event->flags = flags;
}

/**
 * lrg_card_event_get_status_id:
 * @event: a #LrgCardEvent
 *
 * Gets the status effect ID for status events.
 *
 * Returns: (transfer none) (nullable): the status ID
 *
 * Since: 1.0
 */
const gchar *
lrg_card_event_get_status_id (LrgCardEvent *event)
{
    g_return_val_if_fail (event != NULL, NULL);
    return event->status_id;
}

/**
 * lrg_card_event_set_status_id:
 * @event: a #LrgCardEvent
 * @status_id: (nullable): the status ID
 *
 * Sets the status effect ID.
 *
 * Since: 1.0
 */
void
lrg_card_event_set_status_id (LrgCardEvent *event,
                              const gchar  *status_id)
{
    g_return_if_fail (event != NULL);

    g_free (event->status_id);
    event->status_id = g_strdup (status_id);
}

/* ==========================================================================
 * Cancellation
 * ========================================================================== */

/**
 * lrg_card_event_is_cancelled:
 * @event: a #LrgCardEvent
 *
 * Checks if the event has been cancelled.
 *
 * Returns: %TRUE if cancelled
 *
 * Since: 1.0
 */
gboolean
lrg_card_event_is_cancelled (LrgCardEvent *event)
{
    g_return_val_if_fail (event != NULL, FALSE);
    return event->cancelled;
}

/**
 * lrg_card_event_cancel:
 * @event: a #LrgCardEvent
 *
 * Cancels the event. Cancelled events may be ignored by
 * the combat system depending on the event type.
 *
 * Since: 1.0
 */
void
lrg_card_event_cancel (LrgCardEvent *event)
{
    g_return_if_fail (event != NULL);
    event->cancelled = TRUE;
}
