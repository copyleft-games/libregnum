/* lrg-editor-bone-map-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorBoneMapEditor: the data editor of `.rbonemap` bone maps.
 *
 * A bone map pairs the bones of a source skeleton (the rig an animation
 * was made for) with those of a target skeleton (your character), see
 * #LrgBoneMap. The editor shows:
 *
 *  - two skeleton pickers (any `.glb` / `.gltf` of the project; the
 *    choice is kept in the file under `editor: {source, target, clip}`,
 *    a key the engine loader ignores);
 *  - the target skeleton as a tree, each bone with the source bone that
 *    drives it: an explicit pair (accent), a rule match (muted) or
 *    nothing (a warning); picking a source bone in a row writes an
 *    explicit pair;
 *  - the source bones that drive nothing;
 *  - the name rules (ignore case, separators, sides, prefixes to strip)
 *    and an Auto map button (lrg_bone_map_auto_fill());
 *  - a live retarget preview: the source model plays a clip, the target
 *    model follows through an #LrgRetargeter every frame.
 *
 * Every edit is an undoable command of the `.rbonemap` #LrgEditorDocument.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../animation/lrg-retargeter.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_BONE_MAP_EDITOR (lrg_editor_bone_map_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorBoneMapEditor, lrg_editor_bone_map_editor, LRG, EDITOR_BONE_MAP_EDITOR,
                      LrgEditorDataEditor)

/**
 * lrg_editor_bone_map_editor_new:
 * @app: (nullable): the editor application
 * @document: a bone map document (%LRG_EDITOR_DOCUMENT_FORMAT_BONE_MAP)
 *
 * Creates the editor without docking it; lrg_editor_data_editor_open()
 * (or lrg_editor_bone_map_editor_open()) is what the editor uses.
 *
 * Returns: (transfer full): a new #LrgEditorBoneMapEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorBoneMapEditor * lrg_editor_bone_map_editor_new (LrgEditorApp      *app,
                                                         LrgEditorDocument *document);

/**
 * lrg_editor_bone_map_editor_install:
 * @app: the editor application
 *
 * Registers the `bonemap.*` IPC methods on @app. Safe to call more than
 * once; the assets panel calls it when it is created.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_bone_map_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_bone_map_editor_open:
 * @app: the editor application
 * @path: a `.rbonemap` file, absolute or relative to the project directory
 * @error: return location for a #GError
 *
 * Opens @path in a bone map editor tab (or presents its open tab).
 *
 * Returns: (transfer none) (nullable): the editor, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgEditorBoneMapEditor * lrg_editor_bone_map_editor_open (LrgEditorApp  *app,
                                                          const gchar   *path,
                                                          GError       **error);

/**
 * lrg_editor_bone_map_editor_create:
 * @app: (nullable): the editor application (resolves relative paths
 *   against the project)
 * @path: the new `.rbonemap` file
 * @source: (nullable): the source model (`.glb` / `.gltf`)
 * @target: (nullable): the target model
 * @auto_fill: with both models, turn every rule match into an explicit
 *   pair (lrg_bone_map_auto_fill())
 * @n_added: (out) (optional): the number of pairs written
 * @error: return location for a #GError
 *
 * Writes a new bone map with the default rules and the two models
 * remembered for the editor.
 *
 * Returns: %TRUE on success; %G_IO_ERROR_EXISTS when @path exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bone_map_editor_create (LrgEditorApp  *app,
                                            const gchar   *path,
                                            const gchar   *source,
                                            const gchar   *target,
                                            gboolean       auto_fill,
                                            guint         *n_added,
                                            GError       **error);

/**
 * lrg_editor_bone_map_editor_get_map:
 * @self: an #LrgEditorBoneMapEditor
 *
 * The map parsed from the document's current text; replaced after every
 * edit, so do not keep it.
 *
 * Returns: (transfer none) (nullable): the map, %NULL while the document
 *   does not load
 */
LRG_AVAILABLE_IN_ALL
LrgBoneMap * lrg_editor_bone_map_editor_get_map (LrgEditorBoneMapEditor *self);

/**
 * lrg_editor_bone_map_editor_set_models:
 * @self: an #LrgEditorBoneMapEditor
 * @source: (nullable): the source model, project-relative or absolute
 *   (%NULL keeps the current one)
 * @target: (nullable): the target model (%NULL keeps the current one)
 * @error: return location for a #GError
 *
 * Picks the skeletons (one undo step: the paths are written under
 * `editor:`, relative to the bone map).
 *
 * Returns: %TRUE when every given model loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bone_map_editor_set_models (LrgEditorBoneMapEditor  *self,
                                                const gchar             *source,
                                                const gchar             *target,
                                                GError                 **error);

/**
 * lrg_editor_bone_map_editor_get_source_model:
 * @self: an #LrgEditorBoneMapEditor
 *
 * Returns: (nullable): the absolute path of the source model
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_bone_map_editor_get_source_model (LrgEditorBoneMapEditor *self);

/**
 * lrg_editor_bone_map_editor_get_target_model:
 * @self: an #LrgEditorBoneMapEditor
 *
 * Returns: (nullable): the absolute path of the target model
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_bone_map_editor_get_target_model (LrgEditorBoneMapEditor *self);

/**
 * lrg_editor_bone_map_editor_get_source_skeleton:
 * @self: an #LrgEditorBoneMapEditor
 *
 * Returns: (transfer none) (nullable): the source skeleton (posed by the
 *   preview clip)
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton * lrg_editor_bone_map_editor_get_source_skeleton (LrgEditorBoneMapEditor *self);

/**
 * lrg_editor_bone_map_editor_get_target_skeleton:
 * @self: an #LrgEditorBoneMapEditor
 *
 * Returns: (transfer none) (nullable): the target skeleton (posed by the
 *   live retargeting)
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton * lrg_editor_bone_map_editor_get_target_skeleton (LrgEditorBoneMapEditor *self);

/**
 * lrg_editor_bone_map_editor_set_pair:
 * @self: an #LrgEditorBoneMapEditor
 * @source: a source bone name
 * @target: (nullable): the target bone it drives, %NULL to remove the
 *   explicit pair of @source
 * @error: return location for a #GError
 *
 * Writes an explicit pair (one undo step). Another explicit pair onto
 * @target is removed, since a target bone has one driver.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bone_map_editor_set_pair (LrgEditorBoneMapEditor  *self,
                                              const gchar             *source,
                                              const gchar             *target,
                                              GError                 **error);

/**
 * lrg_editor_bone_map_editor_auto_fill:
 * @self: an #LrgEditorBoneMapEditor
 *
 * Turns every rule match between the two skeletons into an explicit pair
 * (one undo step).
 *
 * Returns: the number of pairs added (0 without both skeletons)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_bone_map_editor_auto_fill (LrgEditorBoneMapEditor *self);

/**
 * lrg_editor_bone_map_editor_get_driver:
 * @self: an #LrgEditorBoneMapEditor
 * @target: a target bone name
 * @explicit_pair: (out) (optional): whether an explicit pair (rather than
 *   a name rule) decides it
 *
 * Returns: (nullable): the source bone driving @target, %NULL when the
 *   bone is unmapped (or there are no skeletons)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_bone_map_editor_get_driver (LrgEditorBoneMapEditor *self,
                                                     const gchar            *target,
                                                     gboolean               *explicit_pair);

/**
 * lrg_editor_bone_map_editor_get_unmapped_targets:
 * @self: an #LrgEditorBoneMapEditor
 *
 * Returns: (transfer full): the target bones nothing drives (they keep
 *   their bind pose), in skeleton order
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_bone_map_editor_get_unmapped_targets (LrgEditorBoneMapEditor *self);

/**
 * lrg_editor_bone_map_editor_get_unmapped_sources:
 * @self: an #LrgEditorBoneMapEditor
 *
 * Returns: (transfer full): the source bones that drive no target bone,
 *   in skeleton order
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_bone_map_editor_get_unmapped_sources (LrgEditorBoneMapEditor *self);

/**
 * lrg_editor_bone_map_editor_get_retargeter:
 * @self: an #LrgEditorBoneMapEditor
 *
 * Returns: (transfer none) (nullable): the retargeter of the live
 *   preview, %NULL without both skeletons or a loading map
 */
LRG_AVAILABLE_IN_ALL
LrgRetargeter * lrg_editor_bone_map_editor_get_retargeter (LrgEditorBoneMapEditor *self);

/**
 * lrg_editor_bone_map_editor_set_preview_clip:
 * @self: an #LrgEditorBoneMapEditor
 * @clip: (nullable): an animation of the source model, %NULL for the
 *   first one
 *
 * Picks the clip the source model plays (remembered under `editor:`).
 *
 * Returns: %FALSE when the source model has no such clip
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bone_map_editor_set_preview_clip (LrgEditorBoneMapEditor *self,
                                                      const gchar            *clip);

/**
 * lrg_editor_bone_map_editor_get_preview_clip:
 * @self: an #LrgEditorBoneMapEditor
 *
 * Returns: (nullable): the clip the source model plays
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_bone_map_editor_get_preview_clip (LrgEditorBoneMapEditor *self);

/**
 * lrg_editor_bone_map_editor_set_preview_time:
 * @self: an #LrgEditorBoneMapEditor
 * @time: seconds into the clip
 *
 * Pauses the preview at @time and poses both models (the scrub bar).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_bone_map_editor_set_preview_time (LrgEditorBoneMapEditor *self,
                                                  gfloat                  time);

/**
 * lrg_editor_bone_map_editor_set_playing:
 * @self: an #LrgEditorBoneMapEditor
 * @playing: whether the preview clip plays
 *
 * Plays or pauses the retarget preview.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_bone_map_editor_set_playing (LrgEditorBoneMapEditor *self,
                                             gboolean                playing);

/**
 * lrg_editor_bone_map_editor_get_playing:
 * @self: an #LrgEditorBoneMapEditor
 *
 * Returns: whether the preview clip plays
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bone_map_editor_get_playing (LrgEditorBoneMapEditor *self);

G_END_DECLS
