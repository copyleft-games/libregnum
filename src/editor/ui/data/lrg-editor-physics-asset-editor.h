/* lrg-editor-physics-asset-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPhysicsAssetEditor: the data editor of `.rphysasset` physics
 * assets (ragdolls, see #LrgPhysicsAsset and #LrgRagdoll).
 *
 * The editor shows:
 *  - a model picker (any skinned `.glb` / `.gltf` of the project; the
 *    choice is editor state kept in the asset's `.meta` sidecar under the
 *    `preview_model` setting, since the engine loader is strict and the
 *    `.rphysasset` file has no room for it);
 *  - the parts, one row per bone with a part (bone, shape, mass), an
 *    "add part" bone picker listing the bones without one, Remove,
 *    Stabilize masses, and Generate from skeleton (total mass, radius
 *    ratio);
 *  - the selected part in a property grid: shape kind and sizes, offset
 *    and rotation in the bone frame, mass, and the joint to its parent
 *    part (kind, frame, twist and swing limits, friction, motor);
 *  - the collision exclusions (pairs of bones that never collide);
 *  - a live 3D preview: the skinned model, every part's shape on its
 *    bone, the selected part's joint frame with twist arcs and swing
 *    cones; Simulate drops the model as a passive #LrgRagdoll on a ground
 *    plane (project gravity and solver settings) and draws the bodies
 *    with lrg_dynamics_world_debug_draw(); Reset puts it back.
 *
 * Every edit is an undoable command of the `.rphysasset`
 * #LrgEditorDocument; a drag in the grid is one undo step.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../physics/lrg-ragdoll.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_PHYSICS_ASSET_EDITOR (lrg_editor_physics_asset_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorPhysicsAssetEditor, lrg_editor_physics_asset_editor, LRG,
                      EDITOR_PHYSICS_ASSET_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_physics_asset_editor_new:
 * @app: (nullable): the editor application
 * @document: a physics asset document
 *   (%LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET)
 *
 * Creates the editor without docking it; lrg_editor_data_editor_open()
 * (or lrg_editor_physics_asset_editor_open()) is what the editor uses.
 *
 * Returns: (transfer full): a new #LrgEditorPhysicsAssetEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPhysicsAssetEditor * lrg_editor_physics_asset_editor_new (LrgEditorApp      *app,
                                                                   LrgEditorDocument *document);

/**
 * lrg_editor_physics_asset_editor_install:
 * @app: the editor application
 *
 * Registers the `physasset.*` and `physics.layers.*` IPC methods on @app.
 * Safe to call more than once; the assets panel calls it when it is
 * created.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_physics_asset_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_physics_asset_editor_open:
 * @app: the editor application
 * @path: a `.rphysasset` file, absolute or relative to the project
 *   directory
 * @error: return location for a #GError
 *
 * Opens @path in a physics asset editor tab (or presents its open tab).
 *
 * Returns: (transfer none) (nullable): the editor, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPhysicsAssetEditor * lrg_editor_physics_asset_editor_open (LrgEditorApp  *app,
                                                                    const gchar   *path,
                                                                    GError       **error);

/**
 * lrg_editor_physics_asset_editor_create:
 * @app: (nullable): the editor application (resolves relative paths
 *   against the project)
 * @path: the new `.rphysasset` file
 * @model: (nullable): a skinned `.glb` / `.gltf`: the asset is generated
 *   from its skeleton (lrg_physics_asset_new_from_skeleton()) and the
 *   model is remembered as the preview model
 * @total_mass: the generated asset's mass in kg (ignored without @model)
 * @radius_ratio: capsule radius as a fraction of the bone length, 0.05
 *   to 1 (ignored without @model)
 * @n_parts: (out) (optional): the number of parts written
 * @error: return location for a #GError
 *
 * Writes a new physics asset: an empty one (`version: 1`, no parts), or
 * one part per bone of @model's skeleton.
 *
 * Returns: %TRUE on success; %G_IO_ERROR_EXISTS when @path exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_asset_editor_create (LrgEditorApp  *app,
                                                 const gchar   *path,
                                                 const gchar   *model,
                                                 gfloat         total_mass,
                                                 gfloat         radius_ratio,
                                                 guint         *n_parts,
                                                 GError       **error);

/**
 * lrg_editor_physics_asset_editor_get_asset:
 * @self: an #LrgEditorPhysicsAssetEditor
 *
 * The asset parsed from the document's current text; replaced after every
 * edit, so do not keep it.
 *
 * Returns: (transfer none) (nullable): the asset, %NULL while the document
 *   does not load (see lrg_editor_physics_asset_editor_get_load_error())
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsAsset * lrg_editor_physics_asset_editor_get_asset (LrgEditorPhysicsAssetEditor *self);

/**
 * lrg_editor_physics_asset_editor_get_load_error:
 * @self: an #LrgEditorPhysicsAssetEditor
 *
 * Returns: (nullable): why the document does not load as a physics asset
 *   (the strict loader's message), or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_physics_asset_editor_get_load_error (LrgEditorPhysicsAssetEditor *self);

/**
 * lrg_editor_physics_asset_editor_set_model:
 * @self: an #LrgEditorPhysicsAssetEditor
 * @model: (nullable): a skinned model, project-relative or absolute;
 *   %NULL clears the choice
 * @error: return location for a #GError
 *
 * Picks the preview model (its skeleton is what parts are placed on and
 * what Generate, Stabilize and Simulate use). The choice is written to
 * the asset's `.meta` sidecar (`preview_model`, relative to the asset);
 * it is not part of the document's undo history.
 *
 * Returns: %TRUE when the model loaded with a skeleton
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_asset_editor_set_model (LrgEditorPhysicsAssetEditor  *self,
                                                    const gchar                  *model,
                                                    GError                      **error);

/**
 * lrg_editor_physics_asset_editor_get_model:
 * @self: an #LrgEditorPhysicsAssetEditor
 *
 * Returns: (nullable): the absolute path of the preview model
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_physics_asset_editor_get_model (LrgEditorPhysicsAssetEditor *self);

/**
 * lrg_editor_physics_asset_editor_get_skeleton:
 * @self: an #LrgEditorPhysicsAssetEditor
 *
 * Returns: (transfer none) (nullable): the preview model's skeleton (posed
 *   by the ragdoll while simulating)
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton * lrg_editor_physics_asset_editor_get_skeleton (LrgEditorPhysicsAssetEditor *self);

/**
 * lrg_editor_physics_asset_editor_select_part:
 * @self: an #LrgEditorPhysicsAssetEditor
 * @index: a part index, or -1 for none
 *
 * Shows the part in the property grid and highlights it (and its joint
 * gizmos) in the preview.
 *
 * Returns: %FALSE when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_asset_editor_select_part (LrgEditorPhysicsAssetEditor *self,
                                                      gint                         index);

/**
 * lrg_editor_physics_asset_editor_get_selected_part:
 * @self: an #LrgEditorPhysicsAssetEditor
 *
 * Returns: the selected part index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_physics_asset_editor_get_selected_part (LrgEditorPhysicsAssetEditor *self);

/**
 * lrg_editor_physics_asset_editor_add_part:
 * @self: an #LrgEditorPhysicsAssetEditor
 * @bone: a bone without a part
 * @error: return location for a #GError
 *
 * Appends a part with the format defaults (a capsule; with a skeleton it
 * is sized and aligned along the bone toward its first child) and
 * selects it. One undo step.
 *
 * Returns: the new part index, or -1 with @error set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_physics_asset_editor_add_part (LrgEditorPhysicsAssetEditor  *self,
                                               const gchar                  *bone,
                                               GError                      **error);

/**
 * lrg_editor_physics_asset_editor_remove_part:
 * @self: an #LrgEditorPhysicsAssetEditor
 * @index: a part index
 * @error: return location for a #GError
 *
 * Removes a part and the exclusions that name its bone. One undo step.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_asset_editor_remove_part (LrgEditorPhysicsAssetEditor  *self,
                                                      guint                         index,
                                                      GError                      **error);

/**
 * lrg_editor_physics_asset_editor_set_part:
 * @self: an #LrgEditorPhysicsAssetEditor
 * @index: a part index
 * @part: the new values (copied; validated like
 *   lrg_physics_asset_set_part())
 * @error: return location for a #GError
 *
 * Replaces a part, written in its canonical form (defaults left out).
 * One undo step.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_asset_editor_set_part (LrgEditorPhysicsAssetEditor  *self,
                                                   guint                         index,
                                                   const LrgPhysicsAssetPart    *part,
                                                   GError                      **error);

/**
 * lrg_editor_physics_asset_editor_generate:
 * @self: an #LrgEditorPhysicsAssetEditor
 * @total_mass: mass shared by the parts, kg (positive)
 * @radius_ratio: capsule radius as a fraction of the bone length, 0.05
 *   to 1
 * @error: return location for a #GError
 *
 * Replaces every part and exclusion with lrg_physics_asset_new_from_skeleton()
 * on the preview model's skeleton, keeping the asset name. One undo step.
 *
 * Returns: %TRUE on success (needs a preview model)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_asset_editor_generate (LrgEditorPhysicsAssetEditor  *self,
                                                   gfloat                        total_mass,
                                                   gfloat                        radius_ratio,
                                                   GError                      **error);

/**
 * lrg_editor_physics_asset_editor_stabilize:
 * @self: an #LrgEditorPhysicsAssetEditor
 * @error: return location for a #GError
 *
 * Evens out the masses along each chain of parts
 * (lrg_physics_asset_stabilize()). One undo step; nothing is written when
 * nothing changes.
 *
 * Returns: %TRUE on success (needs a preview model)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_asset_editor_stabilize (LrgEditorPhysicsAssetEditor  *self,
                                                    GError                      **error);

/**
 * lrg_editor_physics_asset_editor_set_exclusion:
 * @self: an #LrgEditorPhysicsAssetEditor
 * @bone_a: a bone with a part
 * @bone_b: another bone with a part
 * @excluded: %TRUE to add the pair, %FALSE to remove it
 * @error: return location for a #GError
 *
 * Adds or removes one collision exclusion. One undo step.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_asset_editor_set_exclusion (LrgEditorPhysicsAssetEditor  *self,
                                                        const gchar                  *bone_a,
                                                        const gchar                  *bone_b,
                                                        gboolean                      excluded,
                                                        GError                      **error);

/**
 * lrg_editor_physics_asset_editor_set_simulating:
 * @self: an #LrgEditorPhysicsAssetEditor
 * @simulating: %TRUE to drop the model as a passive ragdoll, %FALSE to
 *   stop and put it back in its bind pose
 * @error: return location for a #GError
 *
 * Starts or stops the preview simulation: an #LrgDynamicsWorld with the
 * project's gravity and solver settings, a ground plane under the model
 * and an #LrgRagdoll of the current asset. Any edit of the document stops
 * it.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_asset_editor_set_simulating (LrgEditorPhysicsAssetEditor  *self,
                                                         gboolean                      simulating,
                                                         GError                      **error);

/**
 * lrg_editor_physics_asset_editor_get_simulating:
 * @self: an #LrgEditorPhysicsAssetEditor
 *
 * Returns: whether the preview simulation runs
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_asset_editor_get_simulating (LrgEditorPhysicsAssetEditor *self);

/**
 * lrg_editor_physics_asset_editor_advance:
 * @self: an #LrgEditorPhysicsAssetEditor
 * @seconds: simulation time to run now, in 1/60 s frames
 *
 * Steps a running simulation by @seconds at once (tests, IPC), as the
 * preview does every frame. Does nothing when not simulating.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_physics_asset_editor_advance (LrgEditorPhysicsAssetEditor *self,
                                              gfloat                       seconds);

/**
 * lrg_editor_physics_asset_editor_get_ragdoll:
 * @self: an #LrgEditorPhysicsAssetEditor
 *
 * Returns: (transfer none) (nullable): the simulated ragdoll, %NULL when
 *   not simulating
 */
LRG_AVAILABLE_IN_ALL
LrgRagdoll * lrg_editor_physics_asset_editor_get_ragdoll (LrgEditorPhysicsAssetEditor *self);

/**
 * lrg_editor_physics_asset_editor_render_preview:
 * @self: an #LrgEditorPhysicsAssetEditor
 * @size: width and height in pixels
 *
 * Renders the preview (model, parts, joint gizmos, bodies while
 * simulating) into an image, as the panel draws it. Needs a graphics
 * context.
 *
 * Returns: (transfer full) (nullable): the image, %NULL without a graphics
 *   context
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_editor_physics_asset_editor_render_preview (LrgEditorPhysicsAssetEditor *self,
                                                           guint                        size);

#ifdef LRG_EDITOR_PANEL_INTERNAL
/*
 * EditorPhysicsSettings:
 * The world settings of project.ryaml's `physics:` block next to the
 * layer table: the engine's #LrgPhysicsSettings, kept on the open
 * LrgProject (lrg_project_get_physics_settings()). The editor reads them
 * with editor_physics_settings_load() and writes them with
 * editor_physics_project_save() (which is lrg_project_save()); the
 * runtime reads the same block.
 */
typedef LrgPhysicsSettings EditorPhysicsSettings;

G_GNUC_INTERNAL void     editor_physics_settings_defaults (EditorPhysicsSettings *settings);
G_GNUC_INTERNAL gboolean editor_physics_settings_load     (LrgEditorApp *app, EditorPhysicsSettings *settings);
G_GNUC_INTERNAL gboolean editor_physics_settings_validate (const EditorPhysicsSettings *settings, GError **error);
G_GNUC_INTERNAL gboolean editor_physics_project_save      (LrgEditorApp *app, const EditorPhysicsSettings *settings,
                                                           GError **error);
/* defined in lrg-editor-dialogs.c: Project Settings on its Physics page */
G_GNUC_INTERNAL void     editor_dialog_project_settings_page (LrgEditorApp *app, const gchar *page);
#endif

G_END_DECLS
