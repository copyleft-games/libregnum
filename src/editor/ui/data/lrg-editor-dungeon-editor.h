/* lrg-editor-dungeon-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorDungeonEditor: the data editor of #LrgProcgenDungeon recipes.
 *
 * A parameter form and a map preview side by side. The form (an
 * #LrgPropertyGrid over the document) edits the generator, the size, the
 * seed, the post-processing switches, every writable property of the
 * chosen generator class (under `params:`) and the spawn rules; tile rules
 * and wave function collapse models stay in the YAML. The preview runs the
 * engine's own pipeline (lrg_procgen_dungeon_new_from_data() then
 * lrg_procgen_dungeon_generate()) on the document and draws the grid:
 * walls, floors, corridors, doors, water and custom tags in theme colours,
 * room outlines, the start and exit cells and the spawn points, with a
 * line of statistics. A recipe the loader rejects keeps the last map and
 * shows the error over it.
 *
 * Toolbar: Generate (run again now), Randomize (a new seed written into
 * the document, one undo step), Add Spawn, and Auto (regenerate after each
 * edit, on by default; the regeneration waits for edits to pause).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../procgen/lrg-procgen-grid.h"
#include "../../../ui/lrg-property-grid.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_DUNGEON_EDITOR (lrg_editor_dungeon_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorDungeonEditor, lrg_editor_dungeon_editor, LRG, EDITOR_DUNGEON_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_dungeon_editor_new:
 * @app: (nullable): the editor application
 * @document: a procgen dungeon document
 *
 * Creates the editor of @document. lrg_editor_data_editor_open() is the
 * usual way to get one; this is for embedding and tests.
 *
 * Returns: (transfer full): a new #LrgEditorDungeonEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDungeonEditor * lrg_editor_dungeon_editor_new (LrgEditorApp      *app,
                                                        LrgEditorDocument *document);

/**
 * lrg_editor_dungeon_editor_get_form:
 * @self: an #LrgEditorDungeonEditor
 *
 * Returns: (transfer none): the parameter form
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_dungeon_editor_get_form (LrgEditorDungeonEditor *self);

/**
 * lrg_editor_dungeon_editor_generate:
 * @self: an #LrgEditorDungeonEditor
 * @error: return location for a #GError
 *
 * Loads the recipe from the document and generates the preview with the
 * document's seed now.
 *
 * Returns: %TRUE when a map was generated
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_dungeon_editor_generate (LrgEditorDungeonEditor  *self,
                                             GError                 **error);

/**
 * lrg_editor_dungeon_editor_randomize:
 * @self: an #LrgEditorDungeonEditor
 *
 * Writes a new random seed into the document (one undo step) and
 * generates.
 *
 * Returns: the new seed
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_editor_dungeon_editor_randomize (LrgEditorDungeonEditor *self);

/**
 * lrg_editor_dungeon_editor_get_seed:
 * @self: an #LrgEditorDungeonEditor
 *
 * Returns: the seed of the document (0 when it has none)
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_editor_dungeon_editor_get_seed (LrgEditorDungeonEditor *self);

/**
 * lrg_editor_dungeon_editor_get_grid:
 * @self: an #LrgEditorDungeonEditor
 *
 * Returns: (transfer none) (nullable): the map the preview shows
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenGrid * lrg_editor_dungeon_editor_get_grid (LrgEditorDungeonEditor *self);

/**
 * lrg_editor_dungeon_editor_get_error:
 * @self: an #LrgEditorDungeonEditor
 *
 * Returns: (nullable): why the last generation failed, %NULL after a
 *   success
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_dungeon_editor_get_error (LrgEditorDungeonEditor *self);

/**
 * lrg_editor_dungeon_editor_get_pending:
 * @self: an #LrgEditorDungeonEditor
 *
 * Returns: whether an automatic regeneration waits for edits to pause
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_dungeon_editor_get_pending (LrgEditorDungeonEditor *self);

/**
 * lrg_editor_dungeon_editor_get_auto:
 * @self: an #LrgEditorDungeonEditor
 *
 * Returns: whether edits regenerate the preview automatically
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_dungeon_editor_get_auto (LrgEditorDungeonEditor *self);

/**
 * lrg_editor_dungeon_editor_set_auto:
 * @self: an #LrgEditorDungeonEditor
 * @auto_generate: regenerate after edits
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_dungeon_editor_set_auto (LrgEditorDungeonEditor *self,
                                         gboolean                auto_generate);

/**
 * lrg_editor_dungeon_editor_add_spawn:
 * @self: an #LrgEditorDungeonEditor
 * @kind: what the rule spawns
 * @count: how many
 * @error: return location for a #GError
 *
 * Appends a spawn rule (one undo step).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_dungeon_editor_add_spawn (LrgEditorDungeonEditor  *self,
                                              const gchar             *kind,
                                              guint                    count,
                                              GError                 **error);

G_END_DECLS
