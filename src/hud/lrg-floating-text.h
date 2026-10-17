/* lrg-floating-text.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFloatingText - floating combat text: short texts (damage numbers,
 * "Miss", "+12") that appear at a point of the world, rise and fade.
 * The queue ages them and drops the expired ones; the game projects
 * their positions to the screen and draws them.
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
 * LRG_FLOATING_TEXT_DEFAULT_LIFETIME:
 *
 * Seconds a text added with a lifetime of 0 lives.
 */
#define LRG_FLOATING_TEXT_DEFAULT_LIFETIME (1.2f)

/**
 * LRG_FLOATING_TEXT_DEFAULT_RISE:
 *
 * World units a text rises over its whole life by default.
 */
#define LRG_FLOATING_TEXT_DEFAULT_RISE (0.8f)

/**
 * LRG_FLOATING_TEXT_FADE_SHARE:
 *
 * The share of a text's life at its end over which it fades out.
 */
#define LRG_FLOATING_TEXT_FADE_SHARE (0.35f)

/**
 * LRG_FLOATING_TEXT_STACK_SECONDS:
 *
 * Texts added at nearly the same point within this time stack above
 * each other instead of overlapping.
 */
#define LRG_FLOATING_TEXT_STACK_SECONDS (0.4f)

/**
 * LrgFloatingTextEntry:
 * @id: the queue's number for the text (increasing, never 0)
 * @text: the text
 * @position: (array fixed-size=3): where it appeared, world units
 * @color: (array fixed-size=4): RGBA, 0 .. 255
 * @kind: the caller's category (a damage type, a heal, ...)
 * @age: seconds since it appeared
 * @lifetime: seconds it lives
 * @rise: world units it rises over its life
 * @stack: how many texts of the same point it stands above
 *
 * One text of an #LrgFloatingText queue (a snapshot).
 */
typedef struct
{
    guint   id;
    gchar  *text;
    gfloat  position[3];
    guint8  color[4];
    gint    kind;
    gfloat  age;
    gfloat  lifetime;
    gfloat  rise;
    guint   stack;
} LrgFloatingTextEntry;

#define LRG_TYPE_FLOATING_TEXT_ENTRY (lrg_floating_text_entry_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_floating_text_entry_get_type (void) G_GNUC_CONST;

/**
 * lrg_floating_text_entry_copy:
 * @self: an entry
 *
 * Returns: (transfer full): a copy
 */
LRG_AVAILABLE_IN_ALL
LrgFloatingTextEntry *
lrg_floating_text_entry_copy (const LrgFloatingTextEntry *self);

/**
 * lrg_floating_text_entry_free:
 * @self: (transfer full): an entry
 */
LRG_AVAILABLE_IN_ALL
void
lrg_floating_text_entry_free (LrgFloatingTextEntry *self);

/**
 * lrg_floating_text_entry_get_alpha:
 * @self: an entry
 *
 * Returns: the opacity now, 1 until the last
 *   %LRG_FLOATING_TEXT_FADE_SHARE of its life, then down to 0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_floating_text_entry_get_alpha (const LrgFloatingTextEntry *self);

/**
 * lrg_floating_text_entry_get_lift:
 * @self: an entry
 *
 * How far the text has risen, world units: an ease-out over its life
 * (fast at first, slowing), so numbers pop up and settle.
 *
 * Returns: the lift, 0 .. @self's rise
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_floating_text_entry_get_lift (const LrgFloatingTextEntry *self);

/**
 * lrg_floating_text_entry_get_scale:
 * @self: an entry
 *
 * A pop: 1.4 when it appears, easing to 1 over the first tenth of a
 * second.
 *
 * Returns: the size factor
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_floating_text_entry_get_scale (const LrgFloatingTextEntry *self);

#define LRG_TYPE_FLOATING_TEXT (lrg_floating_text_get_type ())

#pragma GCC visibility push(default)
G_DECLARE_FINAL_TYPE (LrgFloatingText, lrg_floating_text, LRG, FLOATING_TEXT, GObject)
#pragma GCC visibility pop

/**
 * LrgFloatingText:
 *
 * A queue of at most #LrgFloatingText:max-count texts, oldest first; a
 * text added to a full queue pushes the oldest out.
 */

/**
 * lrg_floating_text_new:
 * @max_count: the most texts alive at once (clamped to 1 .. 256)
 *
 * Returns: (transfer full): an empty queue
 */
LRG_AVAILABLE_IN_ALL
LrgFloatingText *
lrg_floating_text_new (guint max_count);

/**
 * lrg_floating_text_add:
 * @self: an #LrgFloatingText
 * @text: the text
 * @x: world X
 * @y: world Y
 * @z: world Z
 * @color: (array fixed-size=4) (nullable): RGBA 0 .. 255, %NULL for white
 * @kind: the caller's category
 * @lifetime: seconds, 0 or less for %LRG_FLOATING_TEXT_DEFAULT_LIFETIME
 *
 * Adds a text rising %LRG_FLOATING_TEXT_DEFAULT_RISE.  A text added
 * within %LRG_FLOATING_TEXT_STACK_SECONDS of another at nearly the same
 * point (half a unit) stacks above it.
 *
 * Returns: the text's id, 0 for an empty text or a non-finite point
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_floating_text_add (LrgFloatingText *self,
                       const gchar     *text,
                       gfloat           x,
                       gfloat           y,
                       gfloat           z,
                       const guint8    *color,
                       gint             kind,
                       gfloat           lifetime);

/**
 * lrg_floating_text_update:
 * @self: an #LrgFloatingText
 * @delta: seconds passed
 *
 * Ages every text and drops those past their lifetime.
 *
 * Returns: the texts dropped
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_floating_text_update (LrgFloatingText *self,
                          gfloat           delta);

/**
 * lrg_floating_text_get_count:
 * @self: an #LrgFloatingText
 *
 * Returns: the texts alive
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_floating_text_get_count (LrgFloatingText *self);

/**
 * lrg_floating_text_peek:
 * @self: an #LrgFloatingText
 * @index: 0 for the oldest
 *
 * Returns: (transfer none) (nullable): the entry, valid until the next
 *   change of the queue
 */
LRG_AVAILABLE_IN_ALL
const LrgFloatingTextEntry *
lrg_floating_text_peek (LrgFloatingText *self,
                        guint            index);

/**
 * lrg_floating_text_get_world_position:
 * @self: an #LrgFloatingText
 * @index: 0 for the oldest
 * @position: (out caller-allocates) (array fixed-size=3): where the
 *   text stands now: its point lifted by its rise so far and its stack
 *   (a quarter unit per text below it)
 *
 * Returns: %TRUE for an index in range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_floating_text_get_world_position (LrgFloatingText *self,
                                      guint            index,
                                      gfloat          *position);

/**
 * lrg_floating_text_clear:
 * @self: an #LrgFloatingText
 */
LRG_AVAILABLE_IN_ALL
void
lrg_floating_text_clear (LrgFloatingText *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgFloatingTextEntry, lrg_floating_text_entry_free)

G_END_DECLS
