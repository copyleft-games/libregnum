/* lrg-card-event.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardEvent - Event data container.
 *
 * Events are emitted during gameplay and can be listened to by
 * trigger listeners (relics, powers, status effects). Each event
 * carries contextual data about what happened.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_CARD_EVENT (lrg_card_event_get_type ())

/**
 * LrgCardEvent:
 *
 * A data container for game events.
 *
 * Events carry information about gameplay occurrences:
 * - event_type: The type of event (LrgCardEventType)
 * - source: The entity that caused the event (nullable)
 * - target: The entity affected by the event (nullable)
 * - card: The card involved, if any (nullable)
 * - amount: Numeric value (damage, block, heal amount, etc.)
 * - turn: The turn number when the event occurred
 * - cancelled: Whether the event was cancelled by a listener
 *
 * Since: 1.0
 */
typedef struct _LrgCardEvent LrgCardEvent;

LRG_AVAILABLE_IN_ALL
GType lrg_card_event_get_type (void) G_GNUC_CONST;

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
LRG_AVAILABLE_IN_ALL
LrgCardEvent * lrg_card_event_new (LrgCardEventType event_type);

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
LRG_AVAILABLE_IN_ALL
LrgCardEvent * lrg_card_event_copy (LrgCardEvent *event);

/**
 * lrg_card_event_free:
 * @event: a #LrgCardEvent
 *
 * Frees the event.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_event_free (LrgCardEvent *event);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgCardEvent, lrg_card_event_free)

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
LRG_AVAILABLE_IN_ALL
LrgCardEvent * lrg_card_event_new_turn (LrgCardEventType event_type,
                                        guint            turn);

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
LRG_AVAILABLE_IN_ALL
LrgCardEvent * lrg_card_event_new_card (LrgCardEventType  event_type,
                                        gpointer          card);

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
LRG_AVAILABLE_IN_ALL
LrgCardEvent * lrg_card_event_new_damage (gpointer       source,
                                          gpointer       target,
                                          gint           amount,
                                          LrgEffectFlags flags);

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
LRG_AVAILABLE_IN_ALL
LrgCardEvent * lrg_card_event_new_block (gpointer target,
                                         gint     amount);

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
LRG_AVAILABLE_IN_ALL
LrgCardEvent * lrg_card_event_new_heal (gpointer target,
                                        gint     amount);

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
LRG_AVAILABLE_IN_ALL
LrgCardEvent * lrg_card_event_new_status (LrgCardEventType  event_type,
                                          gpointer          target,
                                          const gchar      *status_id,
                                          gint              stacks);

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
LRG_AVAILABLE_IN_ALL
LrgCardEventType lrg_card_event_get_event_type (LrgCardEvent *event);

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
LRG_AVAILABLE_IN_ALL
gpointer lrg_card_event_get_source (LrgCardEvent *event);

/**
 * lrg_card_event_set_source:
 * @event: a #LrgCardEvent
 * @source: (nullable): the source entity
 *
 * Sets the source entity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_event_set_source (LrgCardEvent *event,
                                gpointer      source);

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
LRG_AVAILABLE_IN_ALL
gpointer lrg_card_event_get_target (LrgCardEvent *event);

/**
 * lrg_card_event_set_target:
 * @event: a #LrgCardEvent
 * @target: (nullable): the target entity
 *
 * Sets the target entity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_event_set_target (LrgCardEvent *event,
                                gpointer      target);

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
LRG_AVAILABLE_IN_ALL
gpointer lrg_card_event_get_card (LrgCardEvent *event);

/**
 * lrg_card_event_set_card:
 * @event: a #LrgCardEvent
 * @card: (nullable): the card
 *
 * Sets the card involved in this event.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_event_set_card (LrgCardEvent *event,
                              gpointer      card);

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
LRG_AVAILABLE_IN_ALL
gint lrg_card_event_get_amount (LrgCardEvent *event);

/**
 * lrg_card_event_set_amount:
 * @event: a #LrgCardEvent
 * @amount: the amount
 *
 * Sets the numeric amount.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_event_set_amount (LrgCardEvent *event,
                                gint          amount);

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
LRG_AVAILABLE_IN_ALL
guint lrg_card_event_get_turn (LrgCardEvent *event);

/**
 * lrg_card_event_set_turn:
 * @event: a #LrgCardEvent
 * @turn: the turn number
 *
 * Sets the turn number.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_event_set_turn (LrgCardEvent *event,
                              guint         turn);

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
LRG_AVAILABLE_IN_ALL
LrgEffectFlags lrg_card_event_get_flags (LrgCardEvent *event);

/**
 * lrg_card_event_set_flags:
 * @event: a #LrgCardEvent
 * @flags: the flags
 *
 * Sets the effect flags.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_event_set_flags (LrgCardEvent   *event,
                               LrgEffectFlags  flags);

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
LRG_AVAILABLE_IN_ALL
const gchar * lrg_card_event_get_status_id (LrgCardEvent *event);

/**
 * lrg_card_event_set_status_id:
 * @event: a #LrgCardEvent
 * @status_id: (nullable): the status ID
 *
 * Sets the status effect ID.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_event_set_status_id (LrgCardEvent *event,
                                   const gchar  *status_id);

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
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_event_is_cancelled (LrgCardEvent *event);

/**
 * lrg_card_event_cancel:
 * @event: a #LrgCardEvent
 *
 * Cancels the event. Cancelled events may be ignored by
 * the combat system depending on the event type.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_event_cancel (LrgCardEvent *event);

G_END_DECLS
