/* lrg-skeleton2d-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkeleton2DComponent: plays #LrgSkeleton2D clips on an
 * #LrgGameObject (with crossfades) and draws the deformed skeleton at
 * the object's position, rotation and scale.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../lrg-component.h"
#include "../../animation/lrg-skeleton2d.h"

G_BEGIN_DECLS

#define LRG_TYPE_SKELETON2D_COMPONENT (lrg_skeleton2d_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSkeleton2DComponent, lrg_skeleton2d_component, LRG, SKELETON2D_COMPONENT, LrgComponent)

/**
 * lrg_skeleton2d_component_new:
 *
 * Creates a component without a skeleton.
 *
 * Returns: (transfer full): a new #LrgSkeleton2DComponent
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2DComponent * lrg_skeleton2d_component_new (void);

/**
 * lrg_skeleton2d_component_new_with_skeleton:
 * @skeleton: (nullable): the skeleton to animate and draw
 *
 * Creates a component for @skeleton.  The component poses the skeleton
 * every update, so a skeleton should belong to one component at a time
 * (load the file again, or copy it, for several characters).
 *
 * Returns: (transfer full): a new #LrgSkeleton2DComponent
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2DComponent * lrg_skeleton2d_component_new_with_skeleton (LrgSkeleton2D *skeleton);

/**
 * lrg_skeleton2d_component_set_skeleton:
 * @self: an #LrgSkeleton2DComponent
 * @skeleton: (nullable): the skeleton
 *
 * Replaces the skeleton and stops playback.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_component_set_skeleton (LrgSkeleton2DComponent *self,
                                            LrgSkeleton2D          *skeleton);

/**
 * lrg_skeleton2d_component_get_skeleton:
 * @self: an #LrgSkeleton2DComponent
 *
 * Returns: (transfer none) (nullable): the skeleton
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2D * lrg_skeleton2d_component_get_skeleton (LrgSkeleton2DComponent *self);

/**
 * lrg_skeleton2d_component_load:
 * @self: an #LrgSkeleton2DComponent
 * @path: a `.rskel2d` file
 * @error: return location for a #GError
 *
 * Loads @path (lrg_skeleton2d_new_from_file()) and makes it the
 * component's skeleton.  Textures load on the first draw.  Setting
 * #LrgSkeleton2DComponent:skeleton-path does the same and logs failures.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_component_load (LrgSkeleton2DComponent  *self,
                                        const gchar             *path,
                                        GError                 **error);

/**
 * lrg_skeleton2d_component_play:
 * @self: an #LrgSkeleton2DComponent
 * @clip: a clip name of the skeleton
 * @loop: whether to loop
 *
 * Starts @clip from its beginning at once (no crossfade), dropping
 * anything that was fading out.
 *
 * Returns: %TRUE if the clip exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_component_play (LrgSkeleton2DComponent *self,
                                        const gchar            *clip,
                                        gboolean                loop);

/**
 * lrg_skeleton2d_component_crossfade:
 * @self: an #LrgSkeleton2DComponent
 * @clip: a clip name of the skeleton
 * @duration: fade length in seconds, or a negative value for
 *   #LrgSkeleton2DComponent:default-mix
 * @loop: whether to loop the new clip
 *
 * Starts @clip from its beginning and fades it in over @duration while
 * the current clip keeps playing underneath and fades out.  A crossfade
 * started during another drops the clip that was fading out (two clips
 * mix at most).  Without a current clip this is lrg_skeleton2d_component_play().
 *
 * Returns: %TRUE if the clip exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_component_crossfade (LrgSkeleton2DComponent *self,
                                             const gchar            *clip,
                                             gfloat                  duration,
                                             gboolean                loop);

/**
 * lrg_skeleton2d_component_stop:
 * @self: an #LrgSkeleton2DComponent
 *
 * Stops playback; the skeleton returns to its setup pose.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_component_stop (LrgSkeleton2DComponent *self);

/**
 * lrg_skeleton2d_component_get_clip:
 * @self: an #LrgSkeleton2DComponent
 *
 * Returns: (nullable): the name of the current (fading in or playing)
 *   clip
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skeleton2d_component_get_clip (LrgSkeleton2DComponent *self);

/**
 * lrg_skeleton2d_component_is_playing:
 * @self: an #LrgSkeleton2DComponent
 *
 * Returns: %TRUE while a clip plays (a finished non-looping clip holds
 *   its last pose and is no longer playing)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_component_is_playing (LrgSkeleton2DComponent *self);

/**
 * lrg_skeleton2d_component_get_time:
 * @self: an #LrgSkeleton2DComponent
 *
 * Returns: the playback time of the current clip in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_skeleton2d_component_get_time (LrgSkeleton2DComponent *self);

/**
 * lrg_skeleton2d_component_set_time:
 * @self: an #LrgSkeleton2DComponent
 * @time: playback time in seconds
 *
 * Scrubs the current clip (the clip preview's timeline) without firing
 * events, and poses the skeleton at once.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_component_set_time (LrgSkeleton2DComponent *self,
                                        gfloat                  time);

/**
 * lrg_skeleton2d_component_get_mix_weight:
 * @self: an #LrgSkeleton2DComponent
 *
 * Returns: the weight of the current clip, 0 .. 1 (1 when no crossfade
 *   is running)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_skeleton2d_component_get_mix_weight (LrgSkeleton2DComponent *self);

/**
 * lrg_skeleton2d_component_get_speed:
 * @self: an #LrgSkeleton2DComponent
 *
 * Returns: the playback speed factor
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_skeleton2d_component_get_speed (LrgSkeleton2DComponent *self);

/**
 * lrg_skeleton2d_component_set_speed:
 * @self: an #LrgSkeleton2DComponent
 * @speed: playback speed factor (0 pauses; 1 is normal)
 *
 * Sets the playback speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_component_set_speed (LrgSkeleton2DComponent *self,
                                         gfloat                  speed);

/**
 * lrg_skeleton2d_component_set_tint:
 * @self: an #LrgSkeleton2DComponent
 * @tint: (nullable): colour multiplied into every slot, %NULL for white
 *
 * Sets the tint.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_component_set_tint (LrgSkeleton2DComponent *self,
                                        const GrlColor         *tint);

/**
 * lrg_skeleton2d_component_get_tint:
 * @self: an #LrgSkeleton2DComponent
 *
 * Returns: (transfer full): the tint
 */
LRG_AVAILABLE_IN_ALL
GrlColor * lrg_skeleton2d_component_get_tint (LrgSkeleton2DComponent *self);

/**
 * lrg_skeleton2d_component_sync_pose:
 * @self: an #LrgSkeleton2DComponent
 *
 * Places the skeleton at the owner's render pose (position, rotation,
 * entity scale times #LrgSkeleton2DComponent:scale, flips) and updates
 * world transforms.  lrg_skeleton2d_component_draw() calls it; call it
 * yourself before reading bone world positions (attaching a weapon to a
 * hand).  Without an owner the skeleton sits at the origin.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_component_sync_pose (LrgSkeleton2DComponent *self);

/**
 * lrg_skeleton2d_component_draw:
 * @self: an #LrgSkeleton2DComponent
 *
 * Draws the skeleton at the owner's render pose (and its bones when
 * #LrgSkeleton2DComponent:show-bones is set), loading the attachments'
 * textures on first use.  Called by the owning #LrgGameObject during
 * lrg_world_draw(); needs a window.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_component_draw (LrgSkeleton2DComponent *self);

G_END_DECLS
