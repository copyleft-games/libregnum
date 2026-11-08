/*
 * libregnum
 * Copyright (C) 2026 Zach Podbielniak
 *
 * This file is part of libregnum.
 *
 * libregnum is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * libregnum is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public
 * License along with libregnum. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef LRG_EDITOR_VSCRIPT_EDITOR_H
#define LRG_EDITOR_VSCRIPT_EDITOR_H

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../ui/lrg-node-graph.h"
#include "../../../ui/lrg-block-editor.h"
#include "../../../scripting/lrg-vscript-program.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_VSCRIPT_EDITOR (lrg_editor_vscript_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorVscriptEditor, lrg_editor_vscript_editor, LRG, EDITOR_VSCRIPT_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_vscript_editor_new:
 * @app: (transfer none): the editor application
 * @document: (transfer none): a visual-script document
 *
 * Creates a visual-script editor bound to @document.
 *
 * Returns: (transfer full): a new #LrgEditorVscriptEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorVscriptEditor *
lrg_editor_vscript_editor_new (LrgEditorApp      *app,
                               LrgEditorDocument *document);

/**
 * lrg_editor_vscript_editor_install:
 * @app: (transfer none): the editor application
 *
 * Registers every `vscript.*` IPC method on @app. Safe to call twice.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_editor_vscript_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_vscript_editor_open:
 * @app: (transfer none): the editor application
 * @path: (type filename): project-relative or absolute `.rvscript` path
 * @error: (nullable): return location for a #GError
 *
 * Opens @path in a visual-script editor.
 *
 * Returns: (transfer none) (nullable): the open editor, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgEditorVscriptEditor *
lrg_editor_vscript_editor_open (LrgEditorApp  *app,
                                const gchar   *path,
                                GError       **error);

/**
 * lrg_editor_vscript_editor_create:
 * @app: (transfer none): the editor application
 * @path: (type filename): path of the new `.rvscript`
 * @view: (nullable): `"graph"` or `"blocks"`; %NULL means graph
 * @error: (nullable): return location for a #GError
 *
 * Writes a starter visual script (an `event.start` wired to `debug.log`)
 * and refuses to overwrite an existing file.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_editor_vscript_editor_create (LrgEditorApp  *app,
                                  const gchar   *path,
                                  const gchar   *view,
                                  GError       **error);

/**
 * lrg_editor_vscript_editor_get_program:
 * @self: an #LrgEditorVscriptEditor
 *
 * Returns: (transfer none): the program this editor is showing
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptProgram *
lrg_editor_vscript_editor_get_program (LrgEditorVscriptEditor *self);

/**
 * lrg_editor_vscript_editor_get_canvas:
 * @self: an #LrgEditorVscriptEditor
 *
 * Returns: (transfer none): the node-graph canvas
 */
LRG_AVAILABLE_IN_ALL
LrgNodeGraph *
lrg_editor_vscript_editor_get_canvas (LrgEditorVscriptEditor *self);

/**
 * lrg_editor_vscript_editor_get_blocks:
 * @self: an #LrgEditorVscriptEditor
 *
 * Returns: (transfer none): the block editor
 */
LRG_AVAILABLE_IN_ALL
LrgBlockEditor *
lrg_editor_vscript_editor_get_blocks (LrgEditorVscriptEditor *self);

/**
 * lrg_editor_vscript_editor_add_node_at:
 * @self: an #LrgEditorVscriptEditor
 * @kind: a catalogued node kind ("flow.branch")
 * @x: left edge in graph units
 * @y: top edge in graph units
 * @error: return location for a #GError
 *
 * Adds a node of @kind to the shown graph as one undo step and selects
 * it; what the Add Node picker does. When a wire was dropped on empty
 * canvas (or lrg_editor_vscript_editor_set_drop() recorded one), the new
 * node is wired to that pin: the first compatible pin on the other side,
 * exec to exec first.
 *
 * Returns: (transfer full) (nullable): the new node's id, or %NULL with
 *   @error
 *
 * Since: 0.9.2
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_editor_vscript_editor_add_node_at (LrgEditorVscriptEditor  *self,
                                       const gchar             *kind,
                                       gdouble                  x,
                                       gdouble                  y,
                                       GError                 **error);

/**
 * lrg_editor_vscript_editor_set_drop:
 * @self: an #LrgEditorVscriptEditor
 * @node: (nullable): the node a wire was dragged from; %NULL clears
 * @pin: (nullable): the pin it was dragged from
 * @output: whether @pin is an output pin
 *
 * Records a wire dropped on empty canvas, so the next
 * lrg_editor_vscript_editor_add_node_at() connects the new node to it.
 *
 * Since: 0.9.2
 */
LRG_AVAILABLE_IN_ALL
void
lrg_editor_vscript_editor_set_drop (LrgEditorVscriptEditor *self,
                                    const gchar            *node,
                                    const gchar            *pin,
                                    gboolean                output);

/**
 * lrg_editor_vscript_editor_flush:
 * @self: an #LrgEditorVscriptEditor
 *
 * Mouse edits on the canvas (moving, deleting, duplicating, wiring)
 * are committed to the program from an idle, one undo step per
 * gesture. This commits pending ones now.
 *
 * Since: 0.9.2
 */
LRG_AVAILABLE_IN_ALL
void
lrg_editor_vscript_editor_flush (LrgEditorVscriptEditor *self);

G_END_DECLS

#endif /* LRG_EDITOR_VSCRIPT_EDITOR_H */
