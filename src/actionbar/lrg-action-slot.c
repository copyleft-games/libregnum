/* lrg-action-slot.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Slot contents and slot states.
 */

#include "config.h"

#include "lrg-action-slot.h"

#include <string.h>

struct _LrgActionSlot
{
    gint   ref_count;
    gchar *kind;
    gchar *id;
    gchar *label;
    gchar *icon;
};

/* "" is stored as NULL for the optional strings */
static gchar *
dup_optional (const gchar *text)
{
    return text != NULL && *text != '\0' ? g_strdup (text) : NULL;
}

LrgActionSlot *
lrg_action_slot_new (const gchar *kind,
                     const gchar *id,
                     const gchar *label,
                     const gchar *icon)
{
    LrgActionSlot *self;

    g_return_val_if_fail (kind != NULL && *kind != '\0', NULL);
    g_return_val_if_fail (id != NULL, NULL);

    self = g_new0 (LrgActionSlot, 1);
    self->ref_count = 1;
    self->kind = g_strdup (kind);
    self->id = g_strdup (id);
    self->label = dup_optional (label);
    self->icon = dup_optional (icon);
    return self;
}

LrgActionSlot *
lrg_action_slot_ref (LrgActionSlot *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_atomic_int_inc (&self->ref_count);
    return self;
}

void
lrg_action_slot_unref (LrgActionSlot *self)
{
    g_return_if_fail (self != NULL);
    if (!g_atomic_int_dec_and_test (&self->ref_count))
        return;
    g_free (self->kind);
    g_free (self->id);
    g_free (self->label);
    g_free (self->icon);
    g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgActionSlot, lrg_action_slot, lrg_action_slot_ref, lrg_action_slot_unref)

const gchar *
lrg_action_slot_get_kind (const LrgActionSlot *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->kind;
}

const gchar *
lrg_action_slot_get_id (const LrgActionSlot *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->id;
}

const gchar *
lrg_action_slot_get_label (const LrgActionSlot *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->label;
}

const gchar *
lrg_action_slot_get_icon (const LrgActionSlot *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->icon;
}

gboolean
lrg_action_slot_is_kind (const LrgActionSlot *self,
                         const gchar         *kind)
{
    g_return_val_if_fail (kind != NULL, FALSE);
    return self != NULL && strcmp (self->kind, kind) == 0;
}

gboolean
lrg_action_slot_equal (const LrgActionSlot *a,
                       const LrgActionSlot *b)
{
    if (a == NULL || b == NULL)
        return a == b;
    return strcmp (a->kind, b->kind) == 0 && strcmp (a->id, b->id) == 0;
}

GVariant *
lrg_action_slot_to_variant (const LrgActionSlot *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return g_variant_new ("(ssss)", self->kind, self->id,
                          self->label != NULL ? self->label : "",
                          self->icon != NULL ? self->icon : "");
}

LrgActionSlot *
lrg_action_slot_new_from_variant (GVariant *variant)
{
    const gchar *kind;
    const gchar *id;
    const gchar *label;
    const gchar *icon;

    g_return_val_if_fail (variant != NULL, NULL);

    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE (LRG_ACTION_SLOT_VARIANT_TYPE)))
        return NULL;
    g_variant_get (variant, "(&s&s&s&s)", &kind, &id, &label, &icon);
    if (*kind == '\0')
        return NULL;
    return lrg_action_slot_new (kind, id, label, icon);
}

/* --- states --- */

void
lrg_action_slot_state_init (LrgActionSlotState *state)
{
    g_return_if_fail (state != NULL);
    memset (state, 0, sizeof (*state));
    state->usable = TRUE;
    state->available = TRUE;
    state->count = -1;
}

LrgActionSlotState *
lrg_action_slot_state_copy (const LrgActionSlotState *state)
{
    g_return_val_if_fail (state != NULL, NULL);
    return (LrgActionSlotState *)g_memdup2 (state, sizeof (*state));
}

void
lrg_action_slot_state_free (LrgActionSlotState *state)
{
    g_free (state);
}

G_DEFINE_BOXED_TYPE (LrgActionSlotState, lrg_action_slot_state,
                     lrg_action_slot_state_copy, lrg_action_slot_state_free)

gdouble
lrg_action_slot_state_get_cooldown_fraction (const LrgActionSlotState *state)
{
    g_return_val_if_fail (state != NULL, 0.0);
    if (state->cooldown_remaining <= 0.0 || state->cooldown_duration <= 0.0)
        return 0.0;
    return CLAMP (state->cooldown_remaining / state->cooldown_duration, 0.0, 1.0);
}
