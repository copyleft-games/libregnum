/* lrg-action-slot.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgActionSlot - what one button of an action bar holds (a spell, an
 * item, a macro: a kind and an id the game understands), and
 * LrgActionSlotState - how the button looks right now (usable, a
 * count, a cooldown), which an #LrgActionSlotProvider reports.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LrgActionSlot:
 *
 * A reference-counted, immutable slot content: a kind ("spell",
 * "item", "macro"; the game's vocabulary), an id within the kind (a
 * spell name, an item's stable id), and an optional label and icon name
 * captured when it was placed (shown when the provider cannot resolve
 * the id any more, e.g. an item no longer carried).
 */
typedef struct _LrgActionSlot LrgActionSlot;

#define LRG_TYPE_ACTION_SLOT (lrg_action_slot_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_action_slot_get_type (void) G_GNUC_CONST;

/**
 * LRG_ACTION_SLOT_VARIANT_TYPE:
 *
 * The #GVariant type of lrg_action_slot_to_variant(): kind, id, label
 * and icon ("" for none).
 */
#define LRG_ACTION_SLOT_VARIANT_TYPE "(ssss)"

/**
 * lrg_action_slot_new:
 * @kind: the content's kind (not empty)
 * @id: the content's id within the kind
 * @label: (nullable): a label to show
 * @icon: (nullable): an icon name the game resolves
 *
 * Returns: (transfer full): a new slot content
 */
LRG_AVAILABLE_IN_ALL
LrgActionSlot *
lrg_action_slot_new (const gchar *kind,
                     const gchar *id,
                     const gchar *label,
                     const gchar *icon);

/**
 * lrg_action_slot_ref:
 * @self: an #LrgActionSlot
 *
 * Returns: (transfer full): @self
 */
LRG_AVAILABLE_IN_ALL
LrgActionSlot *
lrg_action_slot_ref (LrgActionSlot *self);

/**
 * lrg_action_slot_unref:
 * @self: (transfer full): an #LrgActionSlot
 */
LRG_AVAILABLE_IN_ALL
void
lrg_action_slot_unref (LrgActionSlot *self);

/**
 * lrg_action_slot_get_kind:
 * @self: an #LrgActionSlot
 *
 * Returns: (transfer none): the kind
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_action_slot_get_kind (const LrgActionSlot *self);

/**
 * lrg_action_slot_get_id:
 * @self: an #LrgActionSlot
 *
 * Returns: (transfer none): the id
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_action_slot_get_id (const LrgActionSlot *self);

/**
 * lrg_action_slot_get_label:
 * @self: an #LrgActionSlot
 *
 * Returns: (transfer none) (nullable): the label
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_action_slot_get_label (const LrgActionSlot *self);

/**
 * lrg_action_slot_get_icon:
 * @self: an #LrgActionSlot
 *
 * Returns: (transfer none) (nullable): the icon name
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_action_slot_get_icon (const LrgActionSlot *self);

/**
 * lrg_action_slot_is_kind:
 * @self: (nullable): an #LrgActionSlot
 * @kind: a kind
 *
 * Returns: %TRUE when @self holds content of @kind
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_action_slot_is_kind (const LrgActionSlot *self,
                         const gchar         *kind);

/**
 * lrg_action_slot_equal:
 * @a: (nullable): an #LrgActionSlot
 * @b: (nullable): an #LrgActionSlot
 *
 * Returns: %TRUE when both hold the same kind and id (labels and icons
 *   do not count), or both are %NULL
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_action_slot_equal (const LrgActionSlot *a,
                       const LrgActionSlot *b);

/**
 * lrg_action_slot_to_variant:
 * @self: an #LrgActionSlot
 *
 * Returns: (transfer floating): a %LRG_ACTION_SLOT_VARIANT_TYPE value
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_action_slot_to_variant (const LrgActionSlot *self);

/**
 * lrg_action_slot_new_from_variant:
 * @variant: a %LRG_ACTION_SLOT_VARIANT_TYPE value
 *
 * Returns: (transfer full) (nullable): the slot, %NULL for a value of
 *   another type or an empty kind
 */
LRG_AVAILABLE_IN_ALL
LrgActionSlot *
lrg_action_slot_new_from_variant (GVariant *variant);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgActionSlot, lrg_action_slot_unref)

/**
 * LrgActionSlotState:
 * @usable: the slot can be used now (an unusable one is greyed out:
 *   not enough magicka, nothing left)
 * @available: the content still exists (a spell in the book, an item
 *   carried); a missing one shows its stored label, dimmed
 * @active: the content is "on" (an equipment set worn, a toggle)
 * @count: a number to show (items carried, charges), -1 for none
 * @cooldown_remaining: seconds until it can be used again, 0 when ready
 * @cooldown_duration: the cooldown's full length (the sweep), 0 for none
 *
 * How a slot looks right now; providers fill it.
 */
typedef struct _LrgActionSlotState LrgActionSlotState;

struct _LrgActionSlotState
{
    gboolean usable;
    gboolean available;
    gboolean active;
    gint     count;
    gdouble  cooldown_remaining;
    gdouble  cooldown_duration;
};

#define LRG_TYPE_ACTION_SLOT_STATE (lrg_action_slot_state_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_action_slot_state_get_type (void) G_GNUC_CONST;

/**
 * lrg_action_slot_state_init:
 * @state: (out caller-allocates): the state to reset
 *
 * Usable, available, not active, no count, no cooldown.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_action_slot_state_init (LrgActionSlotState *state);

/**
 * lrg_action_slot_state_copy:
 * @state: an #LrgActionSlotState
 *
 * Returns: (transfer full): a copy
 */
LRG_AVAILABLE_IN_ALL
LrgActionSlotState *
lrg_action_slot_state_copy (const LrgActionSlotState *state);

/**
 * lrg_action_slot_state_free:
 * @state: (nullable): an #LrgActionSlotState
 */
LRG_AVAILABLE_IN_ALL
void
lrg_action_slot_state_free (LrgActionSlotState *state);

/**
 * lrg_action_slot_state_get_cooldown_fraction:
 * @state: an #LrgActionSlotState
 *
 * Returns: the part of the cooldown left, 0 (ready) .. 1 (just started)
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_action_slot_state_get_cooldown_fraction (const LrgActionSlotState *state);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgActionSlotState, lrg_action_slot_state_free)

G_END_DECLS
