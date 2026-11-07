/* lrg-animator-component3d.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnimatorComponent3D: runs an #LrgAnimGraph (.ranim) on an
 * #LrgEntity3D and drives the entity's #LrgSkinnedModelComponent: every
 * update it advances the graph, blends the layers into the skeleton, runs
 * the IK post-pass, writes the skinning palette into the skinned model
 * component and moves the entity by the extracted root motion.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../animation/lrg-anim-graph.h"
#include "lrg-skinned-model-component.h"

G_BEGIN_DECLS

#define LRG_TYPE_ANIMATOR_COMPONENT3D (lrg_animator_component3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAnimatorComponent3D, lrg_animator_component3d, LRG, ANIMATOR_COMPONENT3D, LrgComponent)

/**
 * lrg_animator_component3d_new:
 *
 * Creates an animator with no graph (speed 1, playing, IK on, root
 * motion as the graph says).
 *
 * Returns: (transfer full): a new #LrgAnimatorComponent3D
 */
LRG_AVAILABLE_IN_ALL
LrgAnimatorComponent3D * lrg_animator_component3d_new (void);

/**
 * lrg_animator_component3d_new_with_graph:
 * @graph: the graph to run (the component runs it directly: give each
 *   character its own, see lrg_anim_graph_duplicate())
 *
 * Returns: (transfer full): a new #LrgAnimatorComponent3D
 */
LRG_AVAILABLE_IN_ALL
LrgAnimatorComponent3D * lrg_animator_component3d_new_with_graph (LrgAnimGraph *graph);

/**
 * lrg_animator_component3d_get_graph:
 * @self: an #LrgAnimatorComponent3D
 *
 * Returns: (transfer none) (nullable): the running graph
 */
LRG_AVAILABLE_IN_ALL
LrgAnimGraph * lrg_animator_component3d_get_graph (LrgAnimatorComponent3D *self);

/**
 * lrg_animator_component3d_set_graph:
 * @self: an #LrgAnimatorComponent3D
 * @graph: (nullable): the graph to run
 *
 * Replaces the graph (it restarts at its default states).
 */
LRG_AVAILABLE_IN_ALL
void lrg_animator_component3d_set_graph (LrgAnimatorComponent3D *self,
                                         LrgAnimGraph           *graph);

/**
 * lrg_animator_component3d_get_graph_path:
 * @self: an #LrgAnimatorComponent3D
 *
 * Returns: (transfer none) (nullable): the .ranim path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_animator_component3d_get_graph_path (LrgAnimatorComponent3D *self);

/**
 * lrg_animator_component3d_set_graph_path:
 * @self: an #LrgAnimatorComponent3D
 * @path: (nullable) (type filename): a .ranim file
 *
 * Loads the graph lazily on the next update (and its clips through
 * lrg_anim_graph_resolve_clips(), relative to the file). Load errors are
 * logged once and leave the component idle.
 */
LRG_AVAILABLE_IN_ALL
void lrg_animator_component3d_set_graph_path (LrgAnimatorComponent3D *self,
                                              const gchar            *path);

/**
 * lrg_animator_component3d_get_skeleton:
 * @self: an #LrgAnimatorComponent3D
 *
 * Returns: (transfer none) (nullable): the skeleton posed: the one set
 *   here, else the skinned model component's
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton * lrg_animator_component3d_get_skeleton (LrgAnimatorComponent3D *self);

/**
 * lrg_animator_component3d_set_skeleton:
 * @self: an #LrgAnimatorComponent3D
 * @skeleton: (nullable): the skeleton to pose (bones numbered 0..n-1)
 *
 * Sets the skeleton explicitly (otherwise
 * lrg_skinned_model_component_get_skeleton() of the entity's skinned
 * model is used).
 */
LRG_AVAILABLE_IN_ALL
void lrg_animator_component3d_set_skeleton (LrgAnimatorComponent3D *self,
                                            LrgSkeleton            *skeleton);

/**
 * lrg_animator_component3d_get_skinned_model:
 * @self: an #LrgAnimatorComponent3D
 *
 * Returns: (transfer none) (nullable): the entity's skinned model
 *   component the palette goes to
 */
LRG_AVAILABLE_IN_ALL
LrgSkinnedModelComponent * lrg_animator_component3d_get_skinned_model (LrgAnimatorComponent3D *self);

/* --- parameters --------------------------------------------------------- */

/**
 * lrg_animator_component3d_set_float:
 * @self: an #LrgAnimatorComponent3D
 * @name: a float parameter
 * @value: the value
 *
 * Sets a graph parameter (lrg_anim_graph_set_float()).
 */
LRG_AVAILABLE_IN_ALL
void     lrg_animator_component3d_set_float   (LrgAnimatorComponent3D *self,
                                               const gchar            *name,
                                               gfloat                  value);

/**
 * lrg_animator_component3d_get_float:
 * @self: an #LrgAnimatorComponent3D
 * @name: a float parameter
 *
 * Returns: the value
 */
LRG_AVAILABLE_IN_ALL
gfloat   lrg_animator_component3d_get_float   (LrgAnimatorComponent3D *self,
                                               const gchar            *name);

/**
 * lrg_animator_component3d_set_int:
 * @self: an #LrgAnimatorComponent3D
 * @name: an int parameter
 * @value: the value
 *
 * Sets a graph parameter.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_animator_component3d_set_int     (LrgAnimatorComponent3D *self,
                                               const gchar            *name,
                                               gint                    value);

/**
 * lrg_animator_component3d_get_int:
 * @self: an #LrgAnimatorComponent3D
 * @name: an int parameter
 *
 * Returns: the value
 */
LRG_AVAILABLE_IN_ALL
gint     lrg_animator_component3d_get_int     (LrgAnimatorComponent3D *self,
                                               const gchar            *name);

/**
 * lrg_animator_component3d_set_bool:
 * @self: an #LrgAnimatorComponent3D
 * @name: a bool parameter
 * @value: the value
 *
 * Sets a graph parameter.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_animator_component3d_set_bool    (LrgAnimatorComponent3D *self,
                                               const gchar            *name,
                                               gboolean                value);

/**
 * lrg_animator_component3d_get_bool:
 * @self: an #LrgAnimatorComponent3D
 * @name: a bool or trigger parameter
 *
 * Returns: the value
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_animator_component3d_get_bool    (LrgAnimatorComponent3D *self,
                                               const gchar            *name);

/**
 * lrg_animator_component3d_set_trigger:
 * @self: an #LrgAnimatorComponent3D
 * @name: a trigger parameter
 *
 * Raises a trigger (consumed by the transition it fires).
 */
LRG_AVAILABLE_IN_ALL
void     lrg_animator_component3d_set_trigger (LrgAnimatorComponent3D *self,
                                               const gchar            *name);

/**
 * lrg_animator_component3d_reset_trigger:
 * @self: an #LrgAnimatorComponent3D
 * @name: a trigger parameter
 *
 * Lowers a trigger nothing consumed.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_animator_component3d_reset_trigger (LrgAnimatorComponent3D *self,
                                                 const gchar            *name);

/* --- state -------------------------------------------------------------- */

/**
 * lrg_animator_component3d_get_current_state:
 * @self: an #LrgAnimatorComponent3D
 * @layer_name: (nullable): a layer, or %NULL for the base layer
 *
 * Returns: (transfer none) (nullable): the layer's current state name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_animator_component3d_get_current_state (LrgAnimatorComponent3D *self,
                                                          const gchar            *layer_name);

/**
 * lrg_animator_component3d_set_layer_weight:
 * @self: an #LrgAnimatorComponent3D
 * @layer_name: a layer
 * @weight: 0..1
 *
 * Fades a layer in or out (an upper-body aim layer while aiming).
 */
LRG_AVAILABLE_IN_ALL
void     lrg_animator_component3d_set_layer_weight (LrgAnimatorComponent3D *self,
                                                    const gchar            *layer_name,
                                                    gfloat                  weight);

/**
 * lrg_animator_component3d_get_speed:
 * @self: an #LrgAnimatorComponent3D
 *
 * Returns: the playback rate multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat   lrg_animator_component3d_get_speed   (LrgAnimatorComponent3D *self);

/**
 * lrg_animator_component3d_set_speed:
 * @self: an #LrgAnimatorComponent3D
 * @speed: rate multiplier (0 freezes time but keeps posing)
 *
 * Scales the time the graph advances by.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_animator_component3d_set_speed   (LrgAnimatorComponent3D *self,
                                               gfloat                  speed);

/**
 * lrg_animator_component3d_get_playing:
 * @self: an #LrgAnimatorComponent3D
 *
 * Returns: whether time advances
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_animator_component3d_get_playing (LrgAnimatorComponent3D *self);

/**
 * lrg_animator_component3d_set_playing:
 * @self: an #LrgAnimatorComponent3D
 * @playing: whether time advances
 *
 * Pauses or resumes (a paused animator keeps posing, so IK targets still
 * move the skeleton).
 */
LRG_AVAILABLE_IN_ALL
void     lrg_animator_component3d_set_playing (LrgAnimatorComponent3D *self,
                                               gboolean                playing);

/* --- root motion -------------------------------------------------------- */

/**
 * lrg_animator_component3d_get_apply_root_motion:
 * @self: an #LrgAnimatorComponent3D
 *
 * Returns: whether the entity is moved by the root motion
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_animator_component3d_get_apply_root_motion (LrgAnimatorComponent3D *self);

/**
 * lrg_animator_component3d_set_apply_root_motion:
 * @self: an #LrgAnimatorComponent3D
 * @apply: whether to move the entity
 *
 * Overrides the graph's `root_motion.apply` (a character controller that
 * moves the entity itself reads lrg_animator_component3d_get_root_motion()
 * instead).
 */
LRG_AVAILABLE_IN_ALL
void     lrg_animator_component3d_set_apply_root_motion (LrgAnimatorComponent3D *self,
                                                         gboolean                apply);

/**
 * lrg_animator_component3d_get_root_motion:
 * @self: an #LrgAnimatorComponent3D
 * @out_translation: (out caller-allocates) (array fixed-size=3): the last
 *   update's root motion, in the entity's parent space (what was or
 *   would have been added to the position)
 * @out_yaw: (out) (optional): the last update's turn, radians
 *
 * Reads the last root motion delta.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_animator_component3d_get_root_motion (LrgAnimatorComponent3D *self,
                                                   gfloat                 *out_translation,
                                                   gfloat                 *out_yaw);

/* --- IK -------------------------------------------------------------------- */

/**
 * lrg_animator_component3d_get_ik_enabled:
 * @self: an #LrgAnimatorComponent3D
 *
 * Returns: whether the IK post-pass runs
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_animator_component3d_get_ik_enabled (LrgAnimatorComponent3D *self);

/**
 * lrg_animator_component3d_set_ik_enabled:
 * @self: an #LrgAnimatorComponent3D
 * @enabled: whether the IK post-pass runs
 *
 * Turns the whole IK post-pass on or off.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_animator_component3d_set_ik_enabled (LrgAnimatorComponent3D *self,
                                                  gboolean                enabled);

/**
 * lrg_animator_component3d_set_ik_target:
 * @self: an #LrgAnimatorComponent3D
 * @goal_name: a goal of the graph's `ik:` list
 * @x: world X
 * @y: world Y
 * @z: world Z
 *
 * Sets a goal's world-space target (a hand on a door handle, the head
 * looking at the camera).
 *
 * Returns: %TRUE if the goal exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_animator_component3d_set_ik_target (LrgAnimatorComponent3D *self,
                                                 const gchar            *goal_name,
                                                 gfloat                  x,
                                                 gfloat                  y,
                                                 gfloat                  z);

/**
 * lrg_animator_component3d_set_ik_weight:
 * @self: an #LrgAnimatorComponent3D
 * @goal_name: a goal
 * @weight: 0..1
 *
 * Fades a goal in or out.
 *
 * Returns: %TRUE if the goal exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_animator_component3d_set_ik_weight (LrgAnimatorComponent3D *self,
                                                 const gchar            *goal_name,
                                                 gfloat                  weight);

/**
 * lrg_animator_component3d_set_raycast_func:
 * @self: an #LrgAnimatorComponent3D
 * @func: (nullable) (scope notified) (closure user_data) (destroy destroy):
 *   the ground probe foot goals use
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data
 *
 * Sets the world raycast for foot placement (usually a closure over the
 * game's physics world). Without one, foot goals do nothing.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_animator_component3d_set_raycast_func (LrgAnimatorComponent3D *self,
                                                    LrgIKRaycastFunc        func,
                                                    gpointer                user_data,
                                                    GDestroyNotify          destroy);

/* --- evaluation ------------------------------------------------------------ */

/**
 * lrg_animator_component3d_evaluate:
 * @self: an #LrgAnimatorComponent3D
 * @delta_time: seconds to advance (0 re-poses without moving time)
 *
 * Runs one animator step by hand: advance (when playing), blend, IK,
 * palette, root motion. update() calls it with the frame delta; the
 * editor's preview and tests call it directly.
 *
 * Returns: %TRUE when a pose was produced (a graph and a skeleton exist)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_animator_component3d_evaluate (LrgAnimatorComponent3D *self,
                                            gfloat                  delta_time);

/**
 * lrg_animator_component3d_get_palette:
 * @self: an #LrgAnimatorComponent3D
 * @out_n_bones: (out) (optional): number of matrices
 *
 * Returns: (transfer none) (nullable) (array) (element-type gfloat): the
 *   last palette (16 floats per bone), %NULL before the first pose
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_animator_component3d_get_palette (LrgAnimatorComponent3D *self,
                                                     guint                  *out_n_bones);

G_END_DECLS
