/* lrg-model-animator.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgModelAnimator - drives one shared skinned GrlModel for many actors.
 * The animator owns the clip table (lookup, aliases) and the model's
 * current pose; every actor keeps its own LrgModelAnimState (clip, time,
 * crossfade). apply() poses the shared model for one actor right before
 * drawing it and skips re-skinning when the pose would not change.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-gltf-info.h"

G_BEGIN_DECLS

#define LRG_TYPE_MODEL_ANIM_STATE (lrg_model_anim_state_get_type ())
#define LRG_TYPE_MODEL_ANIMATOR   (lrg_model_animator_get_type ())

/**
 * LRG_MODEL_ANIMATOR_FPS:
 *
 * Sampling rate of animation clips in frames per second. raylib samples
 * glTF animations at 60 fps, so clip frame N lies at N / 60 seconds.
 */
#define LRG_MODEL_ANIMATOR_FPS (60.0)

/**
 * LRG_MODEL_ANIMATOR_BLEND_LEVELS:
 *
 * Crossfade weights are quantized to this many steps before posing, so a
 * slow fade does not re-skin the model on every frame.
 */
#define LRG_MODEL_ANIMATOR_BLEND_LEVELS (64)

/**
 * LrgModelAnimState:
 * @clip: current clip index, -1 for none
 * @time: seconds into @clip
 * @rate: playback speed multiplier (1 = normal, negative plays backwards)
 * @loop: whether @clip wraps around (otherwise it holds its last frame)
 * @previous_clip: clip being faded out, -1 when not crossfading
 * @previous_time: seconds into @previous_clip
 * @previous_loop: loop mode of @previous_clip
 * @fade: seconds elapsed in the current crossfade
 * @fade_duration: total crossfade length in seconds, 0 when not fading
 *
 * Per-actor playback state. A plain value type: embed it in your actor
 * struct, initialise it with lrg_model_anim_state_reset() and drive it
 * with lrg_model_animator_play() and lrg_model_animator_advance().
 */
struct _LrgModelAnimState
{
    gint     clip;
    gdouble  time;
    gdouble  rate;
    gboolean loop;
    gint     previous_clip;
    gdouble  previous_time;
    gboolean previous_loop;
    gdouble  fade;
    gdouble  fade_duration;
};

LRG_AVAILABLE_IN_ALL
GType lrg_model_anim_state_get_type (void) G_GNUC_CONST;

/**
 * lrg_model_anim_state_new:
 *
 * Allocates a reset state (see lrg_model_anim_state_reset()).
 *
 * Returns: (transfer full): a new #LrgModelAnimState
 */
LRG_AVAILABLE_IN_ALL
LrgModelAnimState *
lrg_model_anim_state_new (void);

/**
 * lrg_model_anim_state_copy:
 * @self: an #LrgModelAnimState
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgModelAnimState *
lrg_model_anim_state_copy (const LrgModelAnimState *self);

/**
 * lrg_model_anim_state_free:
 * @self: (nullable): an #LrgModelAnimState
 *
 * Frees @self.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_model_anim_state_free (LrgModelAnimState *self);

/**
 * lrg_model_anim_state_reset:
 * @self: an #LrgModelAnimState
 *
 * Resets to no clip, time 0, rate 1, looping, no crossfade.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_model_anim_state_reset (LrgModelAnimState *self);

/**
 * lrg_model_anim_state_get_blend:
 * @self: an #LrgModelAnimState
 *
 * Weight of the current clip in [0, 1]: fade / fade_duration while
 * crossfading from a previous clip, 1 otherwise.
 *
 * Returns: the current clip's blend weight
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_model_anim_state_get_blend (const LrgModelAnimState *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgModelAnimState, lrg_model_anim_state_free)

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgModelAnimator, lrg_model_animator, LRG, MODEL_ANIMATOR, GObject)

/**
 * lrg_model_animator_new:
 * @model: the shared skinned model
 * @clips: (element-type GrlModelAnimation): the model's animation clips,
 *   e.g. from lrg_asset_manager_load_model_animations()
 *
 * Creates an animator for @model. The animator references @model and
 * every clip; release it before the GL context goes away.
 *
 * Returns: (transfer full): a new #LrgModelAnimator
 */
LRG_AVAILABLE_IN_ALL
LrgModelAnimator *
lrg_model_animator_new (GrlModel  *model,
                        GPtrArray *clips);

/**
 * lrg_model_animator_new_headless:
 * @names: (array length=n_clips): clip names
 * @frame_counts: (array length=n_clips): frames per clip (>= 1)
 * @n_clips: number of clips
 *
 * Creates an animator without a model, for servers and tests. Lookup,
 * state helpers and the apply() pose-key logic work normally; nothing is
 * posed or drawn.
 *
 * Returns: (transfer full): a new #LrgModelAnimator
 */
LRG_AVAILABLE_IN_ALL
LrgModelAnimator *
lrg_model_animator_new_headless (const gchar *const *names,
                                 const gint         *frame_counts,
                                 guint               n_clips);

/**
 * lrg_model_animator_get_model:
 * @self: an #LrgModelAnimator
 *
 * Returns: (transfer none) (nullable): the model, %NULL when headless
 */
LRG_AVAILABLE_IN_ALL
GrlModel *
lrg_model_animator_get_model (LrgModelAnimator *self);

/**
 * lrg_model_animator_get_clip_count:
 * @self: an #LrgModelAnimator
 *
 * Returns: the number of clips
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_model_animator_get_clip_count (LrgModelAnimator *self);

/**
 * lrg_model_animator_get_clip_name:
 * @self: an #LrgModelAnimator
 * @clip: clip index
 *
 * Returns: (transfer none) (nullable): the clip name, %NULL when out of
 *   range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_model_animator_get_clip_name (LrgModelAnimator *self,
                                  gint              clip);

/**
 * lrg_model_animator_get_clip_frame_count:
 * @self: an #LrgModelAnimator
 * @clip: clip index
 *
 * Returns: the clip's frame count, 0 when out of range
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_model_animator_get_clip_frame_count (LrgModelAnimator *self,
                                         gint              clip);

/**
 * lrg_model_animator_get_clip_duration:
 * @self: an #LrgModelAnimator
 * @clip: clip index
 *
 * Returns: frame count / %LRG_MODEL_ANIMATOR_FPS seconds, 0 when out of
 *   range
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_model_animator_get_clip_duration (LrgModelAnimator *self,
                                      gint              clip);

/**
 * lrg_model_animator_clip_is_valid:
 * @self: an #LrgModelAnimator
 * @clip: clip index
 *
 * Whether the clip can pose the model: in range, at least one frame and,
 * with a model, a skeleton matching the model's bone count. apply() never
 * uses an invalid clip.
 *
 * Returns: %TRUE if usable
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_model_animator_clip_is_valid (LrgModelAnimator *self,
                                  gint              clip);

/**
 * lrg_model_animator_set_clip_names_from_info:
 * @self: an #LrgModelAnimator
 * @info: the #LrgGltfInfo of the same file
 *
 * raylib truncates clip names to 31 bytes, which can make several clips
 * indistinguishable ("AnimalArmature|AnimalArmature|A..."). When @info has
 * the same number of animations and every current name is a prefix of the
 * corresponding full name, the full names replace the truncated ones.
 *
 * Returns: %TRUE if the names were replaced
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_model_animator_set_clip_names_from_info (LrgModelAnimator *self,
                                             LrgGltfInfo      *info);

/**
 * lrg_model_animator_set_alias:
 * @self: an #LrgModelAnimator
 * @alias: name the game uses ("walk")
 * @target: (nullable): clip name to resolve it to ("Walking_A"), or %NULL
 *   to remove the alias
 *
 * Adds or replaces an alias consulted by lrg_model_animator_find_clip()
 * after exact matches. @target is itself resolved by exact then suffix
 * matching (aliases do not chain).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_model_animator_set_alias (LrgModelAnimator *self,
                              const gchar      *alias,
                              const gchar      *target);

/**
 * lrg_model_animator_clear_aliases:
 * @self: an #LrgModelAnimator
 *
 * Removes every alias.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_model_animator_clear_aliases (LrgModelAnimator *self);

/**
 * lrg_model_animator_find_clip:
 * @self: an #LrgModelAnimator
 * @name: clip name, alias, or name suffix
 *
 * Looks a clip up in three steps:
 *
 * 1. exact, case-sensitive name match (lowest index wins);
 * 2. the alias table (lrg_model_animator_set_alias());
 * 3. case-insensitive suffix match at a word boundary: the clip name ends
 *    with @name and the character before it is not a letter or digit, so
 *    "walk" finds "Armature|Walk" and "Rat_Walk" but not "Sidewalk".
 *    Candidates rank by what precedes the suffix: nothing (whole name,
 *    ignoring case), then '|' (an exporter prefix), then any other
 *    separator; so "idle" prefers "Armature|Idle" over "Jump_Idle". Within
 *    a rank the shortest name wins, then the lowest index.
 *
 * Returns: the clip index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_model_animator_find_clip (LrgModelAnimator *self,
                              const gchar      *name);

/**
 * lrg_model_animator_play:
 * @self: an #LrgModelAnimator
 * @state: the actor's state
 * @clip: clip index, or -1 to stop
 * @loop: whether the clip wraps around
 * @fade_seconds: crossfade length from the current clip; 0 cuts
 *
 * Switches @state to @clip. Playing the clip that is already current only
 * updates @loop, so calling this every tick is cheap and does not restart
 * the clip (use lrg_model_animator_restart() for that). A positive fade
 * from a valid current clip keeps the old clip running as the previous
 * clip and blends toward the new one.
 *
 * Returns: %FALSE (and leaves @state untouched) if @clip is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_model_animator_play (LrgModelAnimator  *self,
                         LrgModelAnimState *state,
                         gint               clip,
                         gboolean           loop,
                         gdouble            fade_seconds);

/**
 * lrg_model_animator_restart:
 * @self: an #LrgModelAnimator
 * @state: the actor's state
 *
 * Rewinds the current clip to time 0 without crossfading.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_model_animator_restart (LrgModelAnimator  *self,
                            LrgModelAnimState *state);

/**
 * lrg_model_animator_advance:
 * @self: an #LrgModelAnimator
 * @state: the actor's state
 * @dt: elapsed seconds (finite, >= 0; other values are ignored)
 *
 * Advances the current and previous clips by @dt times the rate and the
 * crossfade by @dt. Looping clips wrap into [0, duration); non-looping
 * clips clamp to [0, time of the last frame]. The crossfade ends (the
 * previous clip is dropped) once fade reaches fade_duration.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_model_animator_advance (LrgModelAnimator  *self,
                            LrgModelAnimState *state,
                            gdouble            dt);

/**
 * lrg_model_animator_frame_of:
 * @self: an #LrgModelAnimator
 * @state: the actor's state
 *
 * Returns: the current clip's frame, floor(time * 60) (with a 1e-6 frame
 *   tolerance for rounding) wrapped (looping) or clamped to the last
 *   frame, or -1 without a valid clip
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_model_animator_frame_of (LrgModelAnimator        *self,
                             const LrgModelAnimState *state);

/**
 * lrg_model_animator_previous_frame_of:
 * @self: an #LrgModelAnimator
 * @state: the actor's state
 *
 * Returns: the previous clip's frame while crossfading, or -1
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_model_animator_previous_frame_of (LrgModelAnimator        *self,
                                      const LrgModelAnimState *state);

/**
 * lrg_model_animator_is_finished:
 * @self: an #LrgModelAnimator
 * @state: the actor's state
 *
 * Returns: %TRUE when a non-looping clip has reached its last frame (or
 *   its first, when playing backwards)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_model_animator_is_finished (LrgModelAnimator        *self,
                                const LrgModelAnimState *state);

/**
 * lrg_model_animator_apply:
 * @self: an #LrgModelAnimator
 * @state: the actor about to be drawn
 *
 * Poses the shared model for @state: grl_model_animation_update() for a
 * single clip, grl_model_animation_blend() while crossfading (with the
 * blend weight quantized to %LRG_MODEL_ANIMATOR_BLEND_LEVELS steps).
 * Re-skinning is skipped when the pose key (clip, frame, previous clip,
 * previous frame, quantized blend) equals the last applied key, so actors
 * sharing a frame cost one skinning pass. Headless animators track the
 * key without posing anything.
 *
 * Returns: %TRUE if the model was (or, headless, would be) re-posed;
 *   %FALSE when the key was unchanged or @state has no valid clip
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_model_animator_apply (LrgModelAnimator        *self,
                          const LrgModelAnimState *state);

/**
 * lrg_model_animator_invalidate:
 * @self: an #LrgModelAnimator
 *
 * Forgets the last applied pose key so the next apply() re-poses. Call it
 * when something else changed the model's pose.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_model_animator_invalidate (LrgModelAnimator *self);

/**
 * lrg_model_animator_build_node_mask:
 * @self: an #LrgModelAnimator
 * @info: the #LrgGltfInfo of the model's file
 * @hidden_nodes: (array zero-terminated=1) (nullable): node names to hide
 *
 * Builds a draw_masked() mask hiding every mesh whose glTF node is listed
 * in @hidden_nodes (see lrg_gltf_info_build_node_mask()).
 *
 * Returns: (transfer full) (nullable): the mask, or %NULL when @info
 *   describes a different number of meshes than the model has
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_model_animator_build_node_mask (LrgModelAnimator   *self,
                                    LrgGltfInfo        *info,
                                    const gchar *const *hidden_nodes);

/**
 * lrg_model_animator_draw_masked:
 * @self: an #LrgModelAnimator
 * @mask: (nullable): one byte per mesh, 0 hides the mesh; missing bytes
 *   and a %NULL mask draw everything
 * @transform: world transform, applied after the model's own transform
 * @tint: (nullable): colour multiplied into each material's diffuse
 *   colour; %NULL is white
 *
 * Draws the model's visible meshes with its current pose (call
 * lrg_model_animator_apply() first). Uploads bone matrices for GPU
 * skinning shaders exactly like raylib's DrawModelEx(). Must be called
 * inside a 3D mode; does nothing when headless.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_model_animator_draw_masked (LrgModelAnimator *self,
                                GBytes           *mask,
                                const GrlMatrix  *transform,
                                const GrlColor   *tint);

/**
 * lrg_model_animator_draw_masked_ex:
 * @self: an #LrgModelAnimator
 * @mask: (nullable): per-mesh visibility, see
 *   lrg_model_animator_draw_masked()
 * @x: world X
 * @y: world Y
 * @z: world Z
 * @yaw: rotation about +Y in radians (counter-clockwise seen from above)
 * @scale: uniform scale
 * @tint: (nullable): tint colour, %NULL is white
 *
 * Convenience wrapper building the transform scale, then yaw, then
 * translation (raylib's DrawModelEx order).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_model_animator_draw_masked_ex (LrgModelAnimator *self,
                                   GBytes           *mask,
                                   gfloat            x,
                                   gfloat            y,
                                   gfloat            z,
                                   gfloat            yaw,
                                   gfloat            scale,
                                   const GrlColor   *tint);

G_END_DECLS
