/* lrg-skinned-model-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkinnedModelComponent: an animated glTF model.  Plays one clip of an
 * #LrgModelAnimator (by name, speed, loop, crossfade) in update() and
 * submits the model as an %LRG_RENDER_ITEM_MODEL item, posed for this
 * component.  Animation graphs arrive in 0.9 M4.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../graphics/lrg-model-animator.h"
#include "../lrg-world3d.h"
#include "../../graphics/lrg-material.h"

G_BEGIN_DECLS

#define LRG_TYPE_SKINNED_MODEL_COMPONENT (lrg_skinned_model_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSkinnedModelComponent, lrg_skinned_model_component,
                      LRG, SKINNED_MODEL_COMPONENT, LrgComponent)

/**
 * lrg_skinned_model_component_new:
 *
 * Creates an empty skinned model component (speed 1, looping, playing,
 * no crossfade).
 *
 * Returns: (transfer full): a new #LrgSkinnedModelComponent
 */
LRG_AVAILABLE_IN_ALL
LrgSkinnedModelComponent * lrg_skinned_model_component_new (void);

/**
 * lrg_skinned_model_component_new_with_model:
 * @model_path: a glTF/GLB path or asset GUID
 * @clip: (nullable): the clip to play
 *
 * Returns: (transfer full): a new #LrgSkinnedModelComponent
 */
LRG_AVAILABLE_IN_ALL
LrgSkinnedModelComponent * lrg_skinned_model_component_new_with_model (const gchar *model_path,
                                                                       const gchar *clip);

/**
 * lrg_skinned_model_component_get_model_path:
 * @self: an #LrgSkinnedModelComponent
 *
 * Returns: (transfer none) (nullable): the model path or GUID
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skinned_model_component_get_model_path (LrgSkinnedModelComponent *self);

/**
 * lrg_skinned_model_component_set_model_path:
 * @self: an #LrgSkinnedModelComponent
 * @model_path: (nullable): a glTF/GLB path or asset GUID
 *
 * Sets the model.  With #LrgSkinnedModelComponent:asset-manager set, the
 * model and its clips load on the next update (the model needs a graphics
 * context) and an #LrgModelAnimator is created for them.  Drops an
 * animator created for the previous path.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skinned_model_component_set_model_path (LrgSkinnedModelComponent *self,
                                                 const gchar              *model_path);

/**
 * lrg_skinned_model_component_set_asset_manager:
 * @self: an #LrgSkinnedModelComponent
 * @manager: (nullable): an asset manager
 *
 * Sets the asset manager the model path loads through.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skinned_model_component_set_asset_manager (LrgSkinnedModelComponent *self,
                                                    LrgAssetManager          *manager);

/**
 * lrg_skinned_model_component_get_animator:
 * @self: an #LrgSkinnedModelComponent
 *
 * Returns: (transfer none) (nullable): the animator (explicit or created
 *   from the model path)
 */
LRG_AVAILABLE_IN_ALL
LrgModelAnimator * lrg_skinned_model_component_get_animator (LrgSkinnedModelComponent *self);

/**
 * lrg_skinned_model_component_set_animator:
 * @self: an #LrgSkinnedModelComponent
 * @animator: (nullable): an animator (referenced); several components
 *   may share one animator and its model, each with its own playback
 *   state
 *
 * Uses an existing animator (a headless one works for servers and tests)
 * instead of loading the model path.  Resets the playback state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skinned_model_component_set_animator (LrgSkinnedModelComponent *self,
                                               LrgModelAnimator         *animator);

/**
 * lrg_skinned_model_component_get_clip:
 * @self: an #LrgSkinnedModelComponent
 *
 * Returns: (transfer none) (nullable): the requested clip name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skinned_model_component_get_clip (LrgSkinnedModelComponent *self);

/**
 * lrg_skinned_model_component_set_clip:
 * @self: an #LrgSkinnedModelComponent
 * @clip: (nullable): a clip name or alias (lrg_model_animator_find_clip()),
 *   %NULL to stop
 *
 * Requests a clip; the switch (crossfaded by
 * #LrgSkinnedModelComponent:crossfade) happens on the next update.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skinned_model_component_set_clip (LrgSkinnedModelComponent *self,
                                           const gchar              *clip);

/**
 * lrg_skinned_model_component_play:
 * @self: an #LrgSkinnedModelComponent
 * @clip: a clip name or alias
 * @loop: whether the clip wraps around
 *
 * Sets the clip and loop mode, resumes playback, and applies the switch
 * now when an animator is available.
 *
 * Returns: %TRUE if @clip was found (always %FALSE without an animator)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skinned_model_component_play (LrgSkinnedModelComponent *self,
                                           const gchar              *clip,
                                           gboolean                  loop);

/**
 * lrg_skinned_model_component_get_state:
 * @self: an #LrgSkinnedModelComponent
 *
 * Returns: (transfer none): the playback state (clip index, time, rate,
 *   crossfade)
 */
LRG_AVAILABLE_IN_ALL
const LrgModelAnimState * lrg_skinned_model_component_get_state (LrgSkinnedModelComponent *self);

/**
 * lrg_skinned_model_component_get_current_clip_name:
 * @self: an #LrgSkinnedModelComponent
 *
 * Returns: (transfer none) (nullable): the name of the clip actually
 *   playing, %NULL when none
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skinned_model_component_get_current_clip_name (LrgSkinnedModelComponent *self);

/**
 * lrg_skinned_model_component_is_finished:
 * @self: an #LrgSkinnedModelComponent
 *
 * Returns: %TRUE when a non-looping clip reached its end
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skinned_model_component_is_finished (LrgSkinnedModelComponent *self);

/**
 * lrg_skinned_model_component_get_model:
 * @self: an #LrgSkinnedModelComponent
 *
 * Returns: (transfer none) (nullable): the drawn model (the animator's)
 */
LRG_AVAILABLE_IN_ALL
GrlModel * lrg_skinned_model_component_get_model (LrgSkinnedModelComponent *self);

/**
 * lrg_skinned_model_component_apply_pose:
 * @self: an #LrgSkinnedModelComponent
 *
 * Poses the shared model for this component's state
 * (lrg_model_animator_apply()).  collect_render() calls it; a renderer
 * drawing several components that share one model calls it again right
 * before drawing each %LRG_RENDER_ITEM_MODEL item whose component is an
 * #LrgSkinnedModelComponent (re-posing is skipped when the pose is
 * unchanged).
 *
 * Returns: %TRUE if the model was re-posed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skinned_model_component_apply_pose (LrgSkinnedModelComponent *self);
/**
 * lrg_skinned_model_component_get_material:
 * @self: an #LrgSkinnedModelComponent
 *
 * Returns: (transfer none) (nullable): the material every mesh draws with
 *   (unless a submesh material is set), or %NULL for the model's own
 *   materials
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial * lrg_skinned_model_component_get_material (LrgSkinnedModelComponent *self);

/**
 * lrg_skinned_model_component_set_material:
 * @self: an #LrgSkinnedModelComponent
 * @material: (nullable): a runtime material (the component keeps a
 *   reference), %NULL for the model's own materials
 *
 * Sets #LrgSkinnedModelComponent:material. The item then carries
 * @material, its base colour (times the component colour) and the
 * transparent / cutout / double-sided / unlit flags it implies.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skinned_model_component_set_material (LrgSkinnedModelComponent *self,
                                               LrgMaterial              *material);

/**
 * lrg_skinned_model_component_set_submesh_material:
 * @self: an #LrgSkinnedModelComponent
 * @index: the model mesh index
 * @material: (nullable): the material of that mesh, %NULL to use
 *   #LrgSkinnedModelComponent:material
 *
 * Gives one mesh of the model its own material. With any submesh
 * material set, collect_render() submits one posed item per mesh
 * (mesh_index = i) instead of one for the whole model.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skinned_model_component_set_submesh_material (LrgSkinnedModelComponent *self,
                                                       guint                     index,
                                                       LrgMaterial              *material);

/**
 * lrg_skinned_model_component_get_submesh_material:
 * @self: an #LrgSkinnedModelComponent
 * @index: the model mesh index
 *
 * Returns: (transfer none) (nullable): the material of mesh @index, or
 *   %NULL when it uses #LrgSkinnedModelComponent:material
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial * lrg_skinned_model_component_get_submesh_material (LrgSkinnedModelComponent *self,
                                                                guint                     index);

/**
 * lrg_skinned_model_component_get_n_submesh_materials:
 * @self: an #LrgSkinnedModelComponent
 *
 * Returns: one past the highest mesh index with a submesh material
 */
LRG_AVAILABLE_IN_ALL
guint lrg_skinned_model_component_get_n_submesh_materials (LrgSkinnedModelComponent *self);

G_END_DECLS
