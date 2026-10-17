/* lrg-floating-text.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFloatingText: a GPtrArray of owned entries, oldest first.  The
 * motion is pure arithmetic of an entry's age (ease-out lift, a short
 * pop, a linear fade over the end of its life), so a HUD draws the same
 * frame from the same state and tests need no clock.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "hud/lrg-floating-text.h"

#include <math.h>

#define MAX_LIMIT        (256)
#define STACK_RADIUS     (0.5f)
#define STACK_STEP       (0.25f)
#define POP_SECONDS      (0.1f)
#define POP_SCALE        (1.4f)

/* --- the entry --- */

LrgFloatingTextEntry *
lrg_floating_text_entry_copy (const LrgFloatingTextEntry *self)
{
    LrgFloatingTextEntry *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new (LrgFloatingTextEntry, 1);
    *copy = *self;
    copy->text = g_strdup (self->text);
    return copy;
}

void
lrg_floating_text_entry_free (LrgFloatingTextEntry *self)
{
    if (self == NULL)
        return;
    g_free (self->text);
    g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgFloatingTextEntry, lrg_floating_text_entry,
                     lrg_floating_text_entry_copy, lrg_floating_text_entry_free)

/* 0 .. 1 of the life gone */
static gfloat
life_share (const LrgFloatingTextEntry *self)
{
    if (self->lifetime <= 0.0f)
        return 1.0f;
    return CLAMP (self->age / self->lifetime, 0.0f, 1.0f);
}

gfloat
lrg_floating_text_entry_get_alpha (const LrgFloatingTextEntry *self)
{
    gfloat t;

    g_return_val_if_fail (self != NULL, 0.0f);

    t = life_share (self);
    if (t <= 1.0f - LRG_FLOATING_TEXT_FADE_SHARE)
        return 1.0f;
    return CLAMP ((1.0f - t) / LRG_FLOATING_TEXT_FADE_SHARE, 0.0f, 1.0f);
}

gfloat
lrg_floating_text_entry_get_lift (const LrgFloatingTextEntry *self)
{
    gfloat t;

    g_return_val_if_fail (self != NULL, 0.0f);

    /* ease-out quadratic: 1 - (1 - t)^2 */
    t = life_share (self);
    return self->rise * (1.0f - (1.0f - t) * (1.0f - t));
}

gfloat
lrg_floating_text_entry_get_scale (const LrgFloatingTextEntry *self)
{
    g_return_val_if_fail (self != NULL, 1.0f);

    if (self->age >= POP_SECONDS)
        return 1.0f;
    return POP_SCALE - (POP_SCALE - 1.0f) * CLAMP (self->age / POP_SECONDS, 0.0f, 1.0f);
}

/* --- the queue --- */

struct _LrgFloatingText
{
    GObject    parent_instance;

    GPtrArray *entries;    /* LrgFloatingTextEntry *, oldest first */
    guint      max_count;
    guint      next_id;
};

G_DEFINE_TYPE (LrgFloatingText, lrg_floating_text, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_MAX_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_floating_text_finalize (GObject *object)
{
    LrgFloatingText *self = LRG_FLOATING_TEXT (object);

    g_ptr_array_unref (self->entries);
    G_OBJECT_CLASS (lrg_floating_text_parent_class)->finalize (object);
}

static void
lrg_floating_text_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgFloatingText *self = LRG_FLOATING_TEXT (object);

    switch (prop_id)
    {
    case PROP_MAX_COUNT:
        g_value_set_uint (value, self->max_count);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_floating_text_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgFloatingText *self = LRG_FLOATING_TEXT (object);

    switch (prop_id)
    {
    case PROP_MAX_COUNT:
        self->max_count = CLAMP (g_value_get_uint (value), 1u, (guint) MAX_LIMIT);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_floating_text_class_init (LrgFloatingTextClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_floating_text_finalize;
    object_class->get_property = lrg_floating_text_get_property;
    object_class->set_property = lrg_floating_text_set_property;

    /**
     * LrgFloatingText:max-count:
     *
     * The most texts alive at once.
     */
    properties[PROP_MAX_COUNT] = g_param_spec_uint ("max-count", NULL, NULL, 1, MAX_LIMIT, 32,
                                                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                    G_PARAM_STATIC_STRINGS);
    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_floating_text_init (LrgFloatingText *self)
{
    self->entries = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_floating_text_entry_free);
    self->max_count = 32;
    self->next_id = 1;
}

LrgFloatingText *
lrg_floating_text_new (guint max_count)
{
    return g_object_new (LRG_TYPE_FLOATING_TEXT, "max-count",
                         CLAMP (max_count, 1u, (guint) MAX_LIMIT), NULL);
}

guint
lrg_floating_text_add (LrgFloatingText *self,
                       const gchar     *text,
                       gfloat           x,
                       gfloat           y,
                       gfloat           z,
                       const guint8    *color,
                       gint             kind,
                       gfloat           lifetime)
{
    LrgFloatingTextEntry *entry;
    guint stack;
    guint i;

    g_return_val_if_fail (LRG_IS_FLOATING_TEXT (self), 0);

    if (text == NULL || *text == '\0' || !isfinite (x) || !isfinite (y) || !isfinite (z))
        return 0;

    /* --- recent texts at the same point push this one up --- */
    stack = 0;
    for (i = 0; i < self->entries->len; i++)
    {
        const LrgFloatingTextEntry *other = g_ptr_array_index (self->entries, i);
        gfloat dx = other->position[0] - x;
        gfloat dy = other->position[1] - y;
        gfloat dz = other->position[2] - z;

        if (other->age <= LRG_FLOATING_TEXT_STACK_SECONDS &&
            dx * dx + dy * dy + dz * dz <= STACK_RADIUS * STACK_RADIUS)
            stack = MAX (stack, other->stack + 1);
    }

    /* --- a full queue lets the oldest go --- */
    while (self->entries->len >= self->max_count)
        g_ptr_array_remove_index (self->entries, 0);

    entry = g_new0 (LrgFloatingTextEntry, 1);
    entry->id = self->next_id++;
    if (self->next_id == 0)
        self->next_id = 1;
    entry->text = g_strdup (text);
    entry->position[0] = x;
    entry->position[1] = y;
    entry->position[2] = z;
    for (i = 0; i < 4; i++)
        entry->color[i] = color != NULL ? color[i] : 255;
    entry->kind = kind;
    entry->lifetime = lifetime > 0.0f && isfinite (lifetime) ? lifetime : LRG_FLOATING_TEXT_DEFAULT_LIFETIME;
    entry->rise = LRG_FLOATING_TEXT_DEFAULT_RISE;
    entry->stack = stack;
    g_ptr_array_add (self->entries, entry);
    return entry->id;
}

guint
lrg_floating_text_update (LrgFloatingText *self,
                          gfloat           delta)
{
    guint dropped;
    guint i;

    g_return_val_if_fail (LRG_IS_FLOATING_TEXT (self), 0);

    if (!(delta > 0.0f) || !isfinite (delta))
        return 0;
    dropped = 0;
    i = 0;
    while (i < self->entries->len)
    {
        LrgFloatingTextEntry *entry = g_ptr_array_index (self->entries, i);

        entry->age += delta;
        if (entry->age >= entry->lifetime)
        {
            g_ptr_array_remove_index (self->entries, i);
            dropped++;
            continue;
        }
        i++;
    }
    return dropped;
}

guint
lrg_floating_text_get_count (LrgFloatingText *self)
{
    g_return_val_if_fail (LRG_IS_FLOATING_TEXT (self), 0);
    return self->entries->len;
}

const LrgFloatingTextEntry *
lrg_floating_text_peek (LrgFloatingText *self,
                        guint            index)
{
    g_return_val_if_fail (LRG_IS_FLOATING_TEXT (self), NULL);

    if (index >= self->entries->len)
        return NULL;
    return g_ptr_array_index (self->entries, index);
}

gboolean
lrg_floating_text_get_world_position (LrgFloatingText *self,
                                      guint            index,
                                      gfloat          *position)
{
    const LrgFloatingTextEntry *entry;

    g_return_val_if_fail (LRG_IS_FLOATING_TEXT (self), FALSE);
    g_return_val_if_fail (position != NULL, FALSE);

    entry = lrg_floating_text_peek (self, index);
    if (entry == NULL)
        return FALSE;
    position[0] = entry->position[0];
    position[1] = entry->position[1] + lrg_floating_text_entry_get_lift (entry) +
                  STACK_STEP * (gfloat) entry->stack;
    position[2] = entry->position[2];
    return TRUE;
}

void
lrg_floating_text_clear (LrgFloatingText *self)
{
    g_return_if_fail (LRG_IS_FLOATING_TEXT (self));
    g_ptr_array_set_size (self->entries, 0);
}
