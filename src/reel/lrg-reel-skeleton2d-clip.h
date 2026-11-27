/* lrg-reel-skeleton2d-clip.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelSkeleton2DClip - a reel clip that renders an animated 2D
 * skeletal character (LrgSkeleton2D) on the CPU.
 *
 * Every frame the clip resets the skeleton to its setup pose, applies an
 * optional animation (LrgSkeleton2DClip) at the clip-relative time,
 * runs an optional pose callback (procedural layers: blinks, breathing,
 * look-at), places the skeleton with its root transform and draws it
 * with lrg_skeleton2d_draw_to_canvas().  No window is needed, so a
 * character renders to frames headless (CI, servers).
 *
 * The skeleton's pose is mutable state shared by every worker of
 * lrg_reel_renderer_render_parallel(), so the pose-and-draw step of every
 * skeleton clip runs under one process-wide lock.  The rest of each frame
 * (other clips, effects, compositing, encoding) stays parallel.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../animation/lrg-skeleton2d.h"
#include "lrg-reel-clip.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_SKELETON2D_CLIP (lrg_reel_skeleton2d_clip_get_type ())

/**
 * LrgReelSkeleton2DClip:
 *
 * A #LrgReelClip that poses and draws an #LrgSkeleton2D on the CPU each
 * frame.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelSkeleton2DClip, lrg_reel_skeleton2d_clip,
                      LRG, REEL_SKELETON2D_CLIP, LrgReelClip)

/**
 * LrgReelSkeleton2DPoseFunc:
 * @clip: the #LrgReelSkeleton2DClip being rendered
 * @skeleton: the skeleton, already reset to its setup pose and posed by
 *   the clip's animation (if any) for this frame
 * @seconds: the clip-relative time of the frame in seconds
 *   (lrg_reel_context_get_seconds(), sub-frame included)
 * @user_data: user data given to lrg_reel_skeleton2d_clip_set_pose_func()
 *
 * Procedural posing hook, called once per rendered frame after the
 * animation and before the world transforms are computed.  Change the
 * local pose (lrg_skeleton2d_set_bone_local(), slot colours and
 * attachments, further lrg_skeleton2d_clip_apply() layers) as a pure
 * function of @seconds so every frame renders the same no matter which
 * thread or in which order it is rendered.
 *
 * It runs under the clip's render lock: it may be called from any
 * renderer thread, but never concurrently with another skeleton clip's
 * render.
 *
 * Since: 0.14
 */
typedef void (*LrgReelSkeleton2DPoseFunc) (LrgReelSkeleton2DClip *clip,
                                           LrgSkeleton2D         *skeleton,
                                           gdouble                seconds,
                                           gpointer               user_data);

/**
 * lrg_reel_skeleton2d_clip_new:
 * @skeleton: (nullable): the skeleton to draw (a reference is kept)
 * @animation: (nullable): the animation to play, or %NULL to show the
 *   setup pose (and whatever the pose callback does)
 *
 * Creates a clip that draws @skeleton playing @animation from the clip's
 * first frame, looping, at normal speed, with the skeleton origin at the
 * canvas origin.  Place it with lrg_reel_skeleton2d_clip_set_placement().
 *
 * The clip owns the skeleton's pose and root transform while it renders:
 * do not pose or draw the same skeleton elsewhere during a render.
 *
 * Returns: (transfer full): a new #LrgReelSkeleton2DClip
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
LrgReelSkeleton2DClip *
lrg_reel_skeleton2d_clip_new (LrgSkeleton2D     *skeleton,
                              LrgSkeleton2DClip *animation);

/**
 * lrg_reel_skeleton2d_clip_new_from_file:
 * @path: a `.rskel2d` file
 * @animation: (nullable): name of one of the skeleton's clips to play, or
 *   %NULL for the setup pose
 * @error: return location for a #GError
 *
 * Loads a skeleton with lrg_skeleton2d_new_from_file() and creates a clip
 * for it.  Attachment images load on first draw, relative to the file.
 *
 * Returns: (transfer full) (nullable): a new #LrgReelSkeleton2DClip, or
 *   %NULL if the file does not load or has no clip named @animation
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
LrgReelSkeleton2DClip *
lrg_reel_skeleton2d_clip_new_from_file (const gchar  *path,
                                        const gchar  *animation,
                                        GError      **error);

/**
 * lrg_reel_skeleton2d_clip_get_skeleton:
 * @self: an #LrgReelSkeleton2DClip
 *
 * Returns: (transfer none) (nullable): the skeleton
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2D *
lrg_reel_skeleton2d_clip_get_skeleton (LrgReelSkeleton2DClip *self);

/**
 * lrg_reel_skeleton2d_clip_set_skeleton:
 * @self: an #LrgReelSkeleton2DClip
 * @skeleton: (nullable): the skeleton to draw
 *
 * Replaces the skeleton.  When #LrgReelSkeleton2DClip:animation-name is
 * set, the animation is looked up again in the new skeleton.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_skeleton2d_clip_set_skeleton (LrgReelSkeleton2DClip *self,
                                       LrgSkeleton2D         *skeleton);

/**
 * lrg_reel_skeleton2d_clip_get_animation:
 * @self: an #LrgReelSkeleton2DClip
 *
 * Returns: (transfer none) (nullable): the animation played
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2DClip *
lrg_reel_skeleton2d_clip_get_animation (LrgReelSkeleton2DClip *self);

/**
 * lrg_reel_skeleton2d_clip_set_animation:
 * @self: an #LrgReelSkeleton2DClip
 * @animation: (nullable): the animation to play, or %NULL for none
 *
 * Sets the animation (it need not belong to the skeleton: any clip whose
 * bone and slot names match works).
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_skeleton2d_clip_set_animation (LrgReelSkeleton2DClip *self,
                                        LrgSkeleton2DClip     *animation);

/**
 * lrg_reel_skeleton2d_clip_set_animation_name:
 * @self: an #LrgReelSkeleton2DClip
 * @name: (nullable): name of a clip of the skeleton, or %NULL
 *
 * Plays the skeleton's clip called @name (lrg_skeleton2d_get_clip()).
 * The name is remembered and looked up again when the skeleton changes;
 * until a skeleton with such a clip is set no animation plays.
 *
 * Returns: %TRUE if the current skeleton has a clip called @name (or
 *   @name is %NULL)
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_skeleton2d_clip_set_animation_name (LrgReelSkeleton2DClip *self,
                                             const gchar           *name);

/**
 * lrg_reel_skeleton2d_clip_get_speed:
 * @self: an #LrgReelSkeleton2DClip
 *
 * Returns: the playback speed (1 = normal)
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_skeleton2d_clip_get_speed (LrgReelSkeleton2DClip *self);

/**
 * lrg_reel_skeleton2d_clip_set_speed:
 * @self: an #LrgReelSkeleton2DClip
 * @speed: playback speed: animation seconds per clip second (2 plays
 *   twice as fast, a negative speed plays backwards from the start)
 *
 * Sets the playback speed.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_skeleton2d_clip_set_speed (LrgReelSkeleton2DClip *self,
                                    gdouble                speed);

/**
 * lrg_reel_skeleton2d_clip_get_loop:
 * @self: an #LrgReelSkeleton2DClip
 *
 * Returns: whether the animation loops
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_skeleton2d_clip_get_loop (LrgReelSkeleton2DClip *self);

/**
 * lrg_reel_skeleton2d_clip_set_loop:
 * @self: an #LrgReelSkeleton2DClip
 * @loop: %TRUE to wrap the animation time, %FALSE to hold the last pose
 *
 * Sets whether the animation loops (the default) or holds its end.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_skeleton2d_clip_set_loop (LrgReelSkeleton2DClip *self,
                                   gboolean               loop);

/**
 * lrg_reel_skeleton2d_clip_set_placement:
 * @self: an #LrgReelSkeleton2DClip
 * @x: canvas X of the skeleton origin in pixels
 * @y: canvas Y of the skeleton origin in pixels
 * @scale: uniform scale of the skeleton (1 = skeleton units are pixels)
 *
 * Places the skeleton on the frame through its root transform, as
 * #LrgSkeleton2DComponent does from its game object.  This is drawn
 * directly; the inherited clip transform (lrg_reel_clip_set_transform())
 * also works but composites through an off-screen layer.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_skeleton2d_clip_set_placement (LrgReelSkeleton2DClip *self,
                                        gdouble                x,
                                        gdouble                y,
                                        gdouble                scale);

/**
 * lrg_reel_skeleton2d_clip_get_placement:
 * @self: an #LrgReelSkeleton2DClip
 * @x: (out) (optional): canvas X of the skeleton origin
 * @y: (out) (optional): canvas Y of the skeleton origin
 * @scale: (out) (optional): uniform scale
 *
 * Reads the placement.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_skeleton2d_clip_get_placement (LrgReelSkeleton2DClip *self,
                                        gdouble               *x,
                                        gdouble               *y,
                                        gdouble               *scale);

/**
 * lrg_reel_skeleton2d_clip_get_flip_x:
 * @self: an #LrgReelSkeleton2DClip
 *
 * Returns: whether the skeleton is mirrored horizontally
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_skeleton2d_clip_get_flip_x (LrgReelSkeleton2DClip *self);

/**
 * lrg_reel_skeleton2d_clip_set_flip_x:
 * @self: an #LrgReelSkeleton2DClip
 * @flip_x: %TRUE to mirror the skeleton about its origin
 *
 * Mirrors the skeleton horizontally (a negative root scale X).
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_skeleton2d_clip_set_flip_x (LrgReelSkeleton2DClip *self,
                                     gboolean               flip_x);

/**
 * lrg_reel_skeleton2d_clip_set_pose_func:
 * @self: an #LrgReelSkeleton2DClip
 * @func: (nullable) (scope notified) (closure user_data): the pose
 *   callback, or %NULL to remove it
 * @user_data: user data for @func
 * @destroy: (nullable): destroy notifier for @user_data, called when the
 *   callback is replaced or the clip is finalized
 *
 * Installs a procedural posing layer, run each frame after the animation
 * (see #LrgReelSkeleton2DPoseFunc).
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_skeleton2d_clip_set_pose_func (LrgReelSkeleton2DClip     *self,
                                        LrgReelSkeleton2DPoseFunc  func,
                                        gpointer                   user_data,
                                        GDestroyNotify             destroy);

/**
 * lrg_reel_skeleton2d_clip_get_animation_time:
 * @self: an #LrgReelSkeleton2DClip
 * @seconds: clip-relative time in seconds
 *
 * Maps clip time to animation time: @seconds times the speed, wrapped
 * into the animation's duration when looping, clamped to it otherwise.
 * This is the time the clip applies the animation at.
 *
 * Returns: the animation time in seconds (0 without an animation)
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_skeleton2d_clip_get_animation_time (LrgReelSkeleton2DClip *self,
                                             gdouble                seconds);

G_END_DECLS
