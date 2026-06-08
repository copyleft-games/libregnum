/* lrg-reel.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReel - a video composition.
 *
 * A reel is the registered template for a piece of video: an identifier, a
 * resolution, a frame rate, a duration in frames, an ordered stack of
 * #LrgReelClip layers (drawn back-to-front), and an optional bag of named
 * default properties for parameterising the composition.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL (lrg_reel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReel, lrg_reel, LRG, REEL, GObject)

/**
 * lrg_reel_new:
 * @id: a URL-safe identifier for the composition.
 * @width: width in pixels (> 0).
 * @height: height in pixels (> 0).
 * @fps: frames per second (> 0).
 * @duration_in_frames: total length in frames (> 0).
 *
 * Creates a new, empty reel.
 *
 * Returns: (transfer full): a new #LrgReel
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReel *
lrg_reel_new (const gchar *id,
              gint         width,
              gint         height,
              gdouble      fps,
              gint         duration_in_frames);

LRG_AVAILABLE_IN_ALL
const gchar *
lrg_reel_get_id (LrgReel *self);

LRG_AVAILABLE_IN_ALL
gint
lrg_reel_get_width (LrgReel *self);

LRG_AVAILABLE_IN_ALL
gint
lrg_reel_get_height (LrgReel *self);

LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_get_fps (LrgReel *self);

LRG_AVAILABLE_IN_ALL
gint
lrg_reel_get_duration_in_frames (LrgReel *self);

/**
 * lrg_reel_frames_to_seconds:
 * @self: a #LrgReel
 * @frame: a frame number.
 *
 * Returns: @frame converted to seconds (frame / fps)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_frames_to_seconds (LrgReel *self,
                            gint     frame);

/**
 * lrg_reel_seconds_to_frames:
 * @self: a #LrgReel
 * @seconds: a time in seconds.
 *
 * Returns: @seconds converted to whole frames (rounded)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_seconds_to_frames (LrgReel *self,
                            gdouble  seconds);

/* ==========================================================================
 * Clips (z-order = insertion order)
 * ========================================================================== */

/**
 * lrg_reel_add_clip:
 * @self: a #LrgReel
 * @clip: (transfer none): a #LrgReelClip to append on top.
 *
 * Appends @clip as the topmost layer.  The reel takes its own reference.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_add_clip (LrgReel     *self,
                   LrgReelClip *clip);

/**
 * lrg_reel_insert_clip:
 * @self: a #LrgReel
 * @clip: (transfer none): a #LrgReelClip.
 * @index: z-index at which to insert (0 = bottom).
 *
 * Inserts @clip at @index in the z-order.  The reel takes its own reference.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_insert_clip (LrgReel     *self,
                      LrgReelClip *clip,
                      guint        index);

/**
 * lrg_reel_remove_clip:
 * @self: a #LrgReel
 * @clip: (transfer none): the clip to remove.
 *
 * Removes @clip from the reel.
 *
 * Returns: %TRUE if @clip was present and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_remove_clip (LrgReel     *self,
                      LrgReelClip *clip);

LRG_AVAILABLE_IN_ALL
guint
lrg_reel_get_n_clips (LrgReel *self);

/**
 * lrg_reel_get_clip:
 * @self: a #LrgReel
 * @index: a z-index.
 *
 * Returns: (transfer none) (nullable): the clip at @index, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelClip *
lrg_reel_get_clip (LrgReel *self,
                   guint    index);

/**
 * lrg_reel_get_clips:
 * @self: a #LrgReel
 *
 * Returns the live ordered clip array.  Do not modify it directly; use the
 * add/insert/remove functions.
 *
 * Returns: (transfer none) (element-type LrgReelClip): the clip array
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_reel_get_clips (LrgReel *self);

/* ==========================================================================
 * Default properties (parameterisation)
 * ========================================================================== */

LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_has_prop (LrgReel     *self,
                   const gchar *key);

LRG_AVAILABLE_IN_ALL
void
lrg_reel_set_prop_double (LrgReel     *self,
                          const gchar *key,
                          gdouble      value);

LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_get_prop_double (LrgReel     *self,
                          const gchar *key,
                          gdouble      fallback);

LRG_AVAILABLE_IN_ALL
void
lrg_reel_set_prop_int (LrgReel     *self,
                       const gchar *key,
                       gint         value);

LRG_AVAILABLE_IN_ALL
gint
lrg_reel_get_prop_int (LrgReel     *self,
                       const gchar *key,
                       gint         fallback);

LRG_AVAILABLE_IN_ALL
void
lrg_reel_set_prop_boolean (LrgReel     *self,
                           const gchar *key,
                           gboolean     value);

LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_get_prop_boolean (LrgReel     *self,
                           const gchar *key,
                           gboolean     fallback);

LRG_AVAILABLE_IN_ALL
void
lrg_reel_set_prop_string (LrgReel     *self,
                          const gchar *key,
                          const gchar *value);

/**
 * lrg_reel_get_prop_string:
 * @self: a #LrgReel
 * @key: the property name.
 *
 * Returns: (transfer none) (nullable): the stored string, or %NULL if unset
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_reel_get_prop_string (LrgReel     *self,
                          const gchar *key);

G_END_DECLS
