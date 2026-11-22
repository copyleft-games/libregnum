/* lrg-emote.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEmote: a glyph ("!", "?", "zZ", "...") or an icon from a texture
 * floating over an entity, with a pop when it appears and a gentle bob.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-world-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_EMOTE (lrg_emote_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEmote, lrg_emote, LRG, EMOTE, LrgWorldWidget)

/**
 * lrg_emote_new_glyph:
 * @glyph: the text drawn in the emote's circle (short: "!", "?", "zZ")
 *
 * Creates a glyph emote: 24 px, a white circle with dark text, bobbing
 * 3 px at 1.5 Hz.
 *
 * Returns: (transfer full): a new #LrgEmote
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgEmote * lrg_emote_new_glyph (const gchar *glyph);

/**
 * lrg_emote_new_icon:
 * @texture: the texture holding the icon
 * @source: (nullable): the icon's rectangle in @texture; %NULL for the
 *   whole texture
 *
 * Creates an icon emote drawn from a texture (an atlas region).
 *
 * Returns: (transfer full): a new #LrgEmote
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgEmote * lrg_emote_new_icon (GrlTexture         *texture,
                               const GrlRectangle *source);

/**
 * lrg_emote_set_glyph:
 * @self: an #LrgEmote
 * @glyph: the text (replaces an icon)
 *
 * Shows a glyph.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_emote_set_glyph (LrgEmote    *self,
                          const gchar *glyph);

/**
 * lrg_emote_get_glyph:
 * @self: an #LrgEmote
 *
 * Returns: (transfer none) (nullable): the glyph, %NULL for an icon
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_emote_get_glyph (LrgEmote *self);

/**
 * lrg_emote_set_size:
 * @self: an #LrgEmote
 * @size: diameter in pixels at scale 1 (> 0)
 *
 * Sets the emote's size.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_emote_set_size (LrgEmote *self,
                         gfloat    size);

/**
 * lrg_emote_set_bob:
 * @self: an #LrgEmote
 * @amplitude: vertical travel in pixels (0: still)
 * @frequency: bobs per second
 *
 * Sets the idle bob.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_emote_set_bob (LrgEmote *self,
                        gfloat    amplitude,
                        gfloat    frequency);

/**
 * lrg_emote_get_bob_offset:
 * @self: an #LrgEmote
 *
 * Returns: the current vertical bob in pixels (positive is up)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_emote_get_bob_offset (LrgEmote *self);

/**
 * lrg_emote_get_pop_scale:
 * @self: an #LrgEmote
 *
 * The emote grows from half size with a slight overshoot over 0.2 s
 * after lrg_emote_pop().
 *
 * Returns: the current pop scale (1 when settled)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_emote_get_pop_scale (LrgEmote *self);

/**
 * lrg_emote_pop:
 * @self: an #LrgEmote
 * @hold_seconds: how long it stays before fading out; 0 keeps it
 *
 * Shows the emote with a pop, then fades it out after @hold_seconds.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_emote_pop (LrgEmote *self,
                    gfloat    hold_seconds);

G_END_DECLS
