/* lrg-editor-post-stack-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPostStackEditor: the lrgeditor data editor of `.rpost` post
 * stacks (#LrgPostStack files).
 *
 * The left column lists the effects in chain order: an enable box, the
 * effect (click to select), its weight and move up / move down / remove
 * buttons, plus an "Add effect" picker of every registered effect type.
 * The right column is a property grid: the stack's name, quality tier and
 * HDR flag, then the selected effect's enabled flag, weight and every
 * parameter of its type with the type's ranges and defaults. Edits are
 * document commands (Ctrl+Z inside the panel). With "Preview" on (the
 * default) the viewport renders through the edited stack while the tab is
 * open, updating on every edit; "Look" replaces the stack with what the
 * viewport shows now (save current look).
 *
 * Opened by lrg_editor_data_editor_open() for `.rpost` files (format
 * %LRG_EDITOR_DOCUMENT_FORMAT_POST_STACK) and by the post.* IPC methods,
 * which lrg_editor_post_stack_editor_install() registers.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../postprocess/lrg-post-stack.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_POST_STACK_EDITOR (lrg_editor_post_stack_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorPostStackEditor, lrg_editor_post_stack_editor, LRG, EDITOR_POST_STACK_EDITOR,
                      LrgEditorDataEditor)

/**
 * lrg_editor_post_stack_editor_new:
 * @app: (nullable): the editor application
 * @document: a post-stack document
 *
 * Creates an editor for @document without docking it (the data editor
 * registry and lrg_editor_data_editor_open() normally do that).
 *
 * Returns: (transfer full): a new #LrgEditorPostStackEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPostStackEditor * lrg_editor_post_stack_editor_new (LrgEditorApp      *app,
                                                             LrgEditorDocument *document);

/**
 * lrg_editor_post_stack_editor_install:
 * @app: the editor application
 *
 * Registers the post.* IPC methods (post.effect_types, post.open,
 * post.create, post.get, post.add, post.remove, post.move, post.set,
 * post.save, post.preview, post.save_look, post.assign) and the
 * "post.save-look" action on @app. Safe to call more than once; the
 * environment and viewport panels call it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_post_stack_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_post_stack_editor_get_n_effects:
 * @self: an #LrgEditorPostStackEditor
 *
 * Returns: the number of effects in the document
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_post_stack_editor_get_n_effects (LrgEditorPostStackEditor *self);

/**
 * lrg_editor_post_stack_editor_get_effect_type:
 * @self: an #LrgEditorPostStackEditor
 * @index: an effect index
 *
 * Returns: (transfer none) (nullable): the `type:` of effect @index
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_post_stack_editor_get_effect_type (LrgEditorPostStackEditor *self,
                                                            guint                     index);

/**
 * lrg_editor_post_stack_editor_get_selected:
 * @self: an #LrgEditorPostStackEditor
 *
 * Returns: the selected effect index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_post_stack_editor_get_selected (LrgEditorPostStackEditor *self);

/**
 * lrg_editor_post_stack_editor_select:
 * @self: an #LrgEditorPostStackEditor
 * @index: an effect index, or -1 for none
 *
 * Selects an effect: the grid shows its parameters.
 *
 * Returns: %FALSE when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_post_stack_editor_select (LrgEditorPostStackEditor *self,
                                              gint                      index);

/**
 * lrg_editor_post_stack_editor_add_effect:
 * @self: an #LrgEditorPostStackEditor
 * @type_name: a registered effect type ("bloom", "tonemap", ...)
 * @index: where it goes; -1 appends
 * @error: return location for a #GError
 *
 * Inserts an effect with default parameters (one undo step) and selects
 * it.
 *
 * Returns: the new effect's index, or -1 with @error set (unknown type)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_post_stack_editor_add_effect (LrgEditorPostStackEditor  *self,
                                              const gchar               *type_name,
                                              gint                       index,
                                              GError                   **error);

/**
 * lrg_editor_post_stack_editor_remove_effect:
 * @self: an #LrgEditorPostStackEditor
 * @index: an effect index
 * @error: return location for a #GError
 *
 * Returns: %TRUE when the effect was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_post_stack_editor_remove_effect (LrgEditorPostStackEditor  *self,
                                                     guint                      index,
                                                     GError                   **error);

/**
 * lrg_editor_post_stack_editor_move_effect:
 * @self: an #LrgEditorPostStackEditor
 * @from: an effect index
 * @to: its index after the move
 * @error: return location for a #GError
 *
 * Reorders the chain (one undo step); the selection follows the effect.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_post_stack_editor_move_effect (LrgEditorPostStackEditor  *self,
                                                   guint                      from,
                                                   guint                      to,
                                                   GError                   **error);

/**
 * lrg_editor_post_stack_editor_set_value:
 * @self: an #LrgEditorPostStackEditor
 * @index: an effect index
 * @key: "enabled", "weight", or a parameter (property) name of the
 *   effect's type (dashes or underscores)
 * @value: the value (booleans, numbers, enum nicks as strings)
 * @error: return location for a #GError
 *
 * Writes one value of an effect into the document (one undo step).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_post_stack_editor_set_value (LrgEditorPostStackEditor  *self,
                                                 guint                      index,
                                                 const gchar               *key,
                                                 const GValue              *value,
                                                 GError                   **error);

/**
 * lrg_editor_post_stack_editor_build_stack:
 * @self: an #LrgEditorPostStackEditor
 * @error: return location for a #GError
 *
 * Parses the document as it stands (unsaved edits included).
 *
 * Returns: (transfer full) (nullable): the stack, or %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgPostStack * lrg_editor_post_stack_editor_build_stack (LrgEditorPostStackEditor  *self,
                                                         GError                   **error);

/**
 * lrg_editor_post_stack_editor_set_preview:
 * @self: an #LrgEditorPostStackEditor
 * @preview: whether the viewport renders through this stack
 *
 * Turns the live viewport preview on or off. Only one editor previews at
 * a time: turning it on here turns it off in the others. The preview
 * also switches on the active viewport's post-processing overlay.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_post_stack_editor_set_preview (LrgEditorPostStackEditor *self,
                                               gboolean                  preview);

/**
 * lrg_editor_post_stack_editor_get_preview:
 * @self: an #LrgEditorPostStackEditor
 *
 * Returns: whether this editor drives the viewport preview
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_post_stack_editor_get_preview (LrgEditorPostStackEditor *self);

/**
 * lrg_editor_post_stack_editor_save_current_look:
 * @self: an #LrgEditorPostStackEditor
 * @error: return location for a #GError
 *
 * Replaces the document's effects, quality and HDR flag with the look the
 * active viewport renders now (lrg_editor_post_stack_capture_look()), as
 * one undo step. Save the document to keep it.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_post_stack_editor_save_current_look (LrgEditorPostStackEditor  *self,
                                                         GError                   **error);

/**
 * lrg_editor_post_stack_capture_look:
 * @app: the editor application
 * @viewport: a viewport index, or -1 for the active one
 *
 * The post-processing look a viewport renders now: the stack the
 * viewport's runtime renderer runs (a previewed `.rpost` or the level's
 * look) when there is one, else the viewport's preview processor
 * (lrg_post_stack_new_from_processor()).
 *
 * Returns: (transfer full) (nullable): the look, %NULL without a renderer
 */
LRG_AVAILABLE_IN_ALL
LrgPostStack * lrg_editor_post_stack_capture_look (LrgEditorApp *app,
                                                   gint          viewport);

G_END_DECLS
