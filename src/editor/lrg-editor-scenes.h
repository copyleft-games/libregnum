/* lrg-editor-scenes.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The set of levels open in one editor window and which of them is
 * active. Loading the level document itself stays with the editor;
 * this object is only the selector.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_SCENES (lrg_editor_scenes_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorScenes, lrg_editor_scenes, LRG, EDITOR_SCENES, GObject)

/**
 * lrg_editor_scenes_new:
 *
 * Returns: (transfer full): an empty scene list
 */
LRG_AVAILABLE_IN_ALL
LrgEditorScenes * lrg_editor_scenes_new (void);

/**
 * lrg_editor_scenes_add:
 * @self: a scene list
 * @path: (type filename): a level path
 *
 * Appends @path and makes it the active scene.
 *
 * Returns: the new index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_scenes_add (LrgEditorScenes *self,
                             const gchar     *path);

/**
 * lrg_editor_scenes_remove:
 * @self: a scene list
 * @index: which scene
 *
 * Returns: %FALSE when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_scenes_remove (LrgEditorScenes *self,
                                   guint            index);

/**
 * lrg_editor_scenes_set_active:
 * @self: a scene list
 * @index: which scene is edited
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_scenes_set_active (LrgEditorScenes *self,
                                   guint            index);

/**
 * lrg_editor_scenes_get_active:
 * @self: a scene list
 *
 * Returns: the active index, or -1 when the list is empty
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_scenes_get_active (LrgEditorScenes *self);

/**
 * lrg_editor_scenes_get_count:
 * @self: a scene list
 *
 * Returns: how many scenes are open
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_scenes_get_count (LrgEditorScenes *self);

/**
 * lrg_editor_scenes_get_path:
 * @self: a scene list
 * @index: which scene
 *
 * Returns: (transfer none) (nullable): the path, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_scenes_get_path (LrgEditorScenes *self,
                                          guint            index);

/**
 * lrg_editor_scenes_ensure:
 * @owner: the object that keeps the open-scene list (the editor)
 *
 * The list stored on @owner under `lrg-editor-scenes`, created on the
 * first call. The panel and the IPC methods share this object.
 *
 * Returns: (transfer none): the scene list
 */
LRG_AVAILABLE_IN_ALL
LrgEditorScenes * lrg_editor_scenes_ensure (GObject *owner);

G_END_DECLS
