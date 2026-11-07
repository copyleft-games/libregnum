/* lrg-editor-anim-graph-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorAnimGraphEditor: the data editor of `.ranim` animation graphs.
 *
 * One LrgNodeGraph canvas shows one state machine at a time: a layer's
 * machine, or the nested machine of a state (breadcrumbs above the
 * canvas walk back up; layer chips switch layers). States are cards
 * (a star marks the default state, a badge says whether the state plays
 * a clip, a blend tree or a nested machine), transitions are wires from
 * the "out" port of the source to the "in" port of the target, and the
 * "Any State" card carries the machine's any-state transitions. Dragging
 * a wire adds a transition, Delete removes states and transitions,
 * double click enters a nested machine.
 *
 * The side column holds a live preview of a skinned model (orbit with the
 * mouse, turntable, bones overlay, play / pause, scrub) driven by the
 * graph itself, and an inspector for the selection: state settings,
 * blend tree settings with a 2D blend-space view (children as points, the
 * current parameter as a dot you can drag), transition settings with a
 * conditions editor, or the layer and root motion settings when nothing
 * is selected. The left column lists the parameters with live values
 * (sliders, check boxes, a Fire button for triggers) and the clip slots
 * (missing clips flagged).
 *
 * The document is the truth: every gesture is an undoable command of the
 * `.ranim` #LrgEditorDocument (card positions are kept in the file as
 * `editor_pos: [x, y]` on each state, which the engine loader keeps as an
 * unknown key). After every change the text is parsed into an
 * #LrgAnimGraph (validation, preview); while previewing, the state each
 * machine is in is highlighted on its card and the transition being
 * blended is highlighted on its wire.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../animation/lrg-anim-graph.h"
#include "../../../ui/lrg-node-graph.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_ANIM_GRAPH_EDITOR (lrg_editor_anim_graph_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorAnimGraphEditor, lrg_editor_anim_graph_editor, LRG, EDITOR_ANIM_GRAPH_EDITOR,
                      LrgEditorDataEditor)

/**
 * LRG_EDITOR_ANIM_GRAPH_ANY_STATE:
 *
 * The card id (and the `from` of anim.* IPC calls) standing for the
 * machine's any-state transitions.
 */
#define LRG_EDITOR_ANIM_GRAPH_ANY_STATE "*"

/**
 * lrg_editor_anim_graph_editor_new:
 * @app: (nullable): the editor application
 * @document: an animation graph document (%LRG_EDITOR_DOCUMENT_FORMAT_ANIM_GRAPH)
 *
 * Creates the editor without docking it; lrg_editor_data_editor_open()
 * (or lrg_editor_anim_graph_editor_open()) is what the editor uses.
 *
 * Returns: (transfer full): a new #LrgEditorAnimGraphEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorAnimGraphEditor * lrg_editor_anim_graph_editor_new (LrgEditorApp      *app,
                                                             LrgEditorDocument *document);

/**
 * lrg_editor_anim_graph_editor_install:
 * @app: the editor application
 *
 * Registers the `anim.*` IPC methods on @app. Safe to call more than
 * once; the assets panel calls it when it is created, so embedders rarely
 * need to.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_anim_graph_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_anim_graph_editor_open:
 * @app: the editor application
 * @path: a `.ranim` file, absolute or relative to the project directory
 * @error: return location for a #GError
 *
 * Opens @path in an animation graph editor tab (or presents its open tab).
 *
 * Returns: (transfer none) (nullable): the editor, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgEditorAnimGraphEditor * lrg_editor_anim_graph_editor_open (LrgEditorApp  *app,
                                                              const gchar   *path,
                                                              GError       **error);

/**
 * lrg_editor_anim_graph_editor_create:
 * @app: (nullable): the editor application (resolves a relative @path
 *   against the project)
 * @path: the new `.ranim` file
 * @model: (nullable): a glTF file (`.glb` / `.gltf`) whose animations
 *   become clip slots, one state each, with the first state the default
 * @error: return location for a #GError
 *
 * Writes a new animation graph: one `base` layer with an `idle` state, or
 * with @model one state per animation of the model laid out in a row and
 * the model remembered as the preview model.
 *
 * Returns: %TRUE on success; %G_IO_ERROR_EXISTS when @path exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_create (LrgEditorApp  *app,
                                              const gchar   *path,
                                              const gchar   *model,
                                              GError       **error);

/* ==========================================================================
 * The model
 * ========================================================================== */

/**
 * lrg_editor_anim_graph_editor_get_graph:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * The graph parsed from the document's current text, with its clips
 * resolved and the live parameter values applied. It is replaced (not
 * mutated) after every edit, so do not keep it.
 *
 * Returns: (transfer none) (nullable): the graph, or %NULL while the
 *   document does not load
 */
LRG_AVAILABLE_IN_ALL
LrgAnimGraph * lrg_editor_anim_graph_editor_get_graph (LrgEditorAnimGraphEditor *self);

/**
 * lrg_editor_anim_graph_editor_get_load_error:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * Returns: (nullable): the loader's message while the document does not
 *   load, else %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_anim_graph_editor_get_load_error (LrgEditorAnimGraphEditor *self);

/**
 * lrg_editor_anim_graph_editor_get_canvas:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * Returns: (transfer none): the state machine canvas
 */
LRG_AVAILABLE_IN_ALL
LrgNodeGraph * lrg_editor_anim_graph_editor_get_canvas (LrgEditorAnimGraphEditor *self);

/* ==========================================================================
 * Navigation
 * ========================================================================== */

/**
 * lrg_editor_anim_graph_editor_get_machine_path:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * The YAML path of the state machine on the canvas: a layer
 * (`/layers/0`) or the nested machine of a state
 * (`/layers/0/states/3/machine`).
 *
 * Returns: the path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_anim_graph_editor_get_machine_path (LrgEditorAnimGraphEditor *self);

/**
 * lrg_editor_anim_graph_editor_get_layer:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * Returns: the index of the layer shown (the canvas machine's layer)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_anim_graph_editor_get_layer (LrgEditorAnimGraphEditor *self);

/**
 * lrg_editor_anim_graph_editor_set_layer:
 * @self: an #LrgEditorAnimGraphEditor
 * @layer: a layer index
 *
 * Shows the top-level machine of @layer (the layer chips).
 *
 * Returns: %FALSE when @layer is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_set_layer (LrgEditorAnimGraphEditor *self,
                                                 guint                     layer);

/**
 * lrg_editor_anim_graph_editor_enter_state:
 * @self: an #LrgEditorAnimGraphEditor
 * @state: a state of the shown machine that plays a nested machine
 *
 * Shows the nested machine of @state (double click on its card).
 *
 * Returns: %FALSE when @state does not exist or plays no machine
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_enter_state (LrgEditorAnimGraphEditor *self,
                                                   const gchar              *state);

/**
 * lrg_editor_anim_graph_editor_go_up:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * Shows the machine containing the shown nested machine (the breadcrumb
 * one level up).
 *
 * Returns: %FALSE at a layer's top-level machine
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_go_up (LrgEditorAnimGraphEditor *self);

/**
 * lrg_editor_anim_graph_editor_get_breadcrumbs:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * Returns: (transfer full): the labels of the breadcrumb bar: the layer
 *   name, then the state of every nested machine down to the shown one
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_anim_graph_editor_get_breadcrumbs (LrgEditorAnimGraphEditor *self);

/* ==========================================================================
 * Editing (each call is one undo step of the document)
 * ========================================================================== */

/**
 * lrg_editor_anim_graph_editor_add_layer:
 * @self: an #LrgEditorAnimGraphEditor
 * @name: the new layer's name (unique)
 * @error: return location for a #GError
 *
 * Appends an override layer with weight 1 and one empty `idle` state.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_add_layer (LrgEditorAnimGraphEditor  *self,
                                                 const gchar               *name,
                                                 GError                   **error);

/**
 * lrg_editor_anim_graph_editor_remove_layer:
 * @self: an #LrgEditorAnimGraphEditor
 * @name: a layer name
 *
 * Removes a layer (never the last one).
 *
 * Returns: %TRUE when the layer was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_remove_layer (LrgEditorAnimGraphEditor *self,
                                                    const gchar              *name);

/**
 * lrg_editor_anim_graph_editor_add_state:
 * @self: an #LrgEditorAnimGraphEditor
 * @name: the state name (unique in the shown machine)
 * @kind: (nullable): "clip", "blend_tree", "machine" or "empty"
 *   (%NULL: "clip" when @clip is given, else "empty")
 * @clip: (nullable): the clip key a "clip" state plays
 * @x: the card's X position in graph units
 * @y: the card's Y position
 * @error: return location for a #GError
 *
 * Adds a state to the shown machine. A blend tree state starts as a 1D
 * tree on the first numeric parameter; a machine state starts with one
 * empty `entry` state. The first state of a machine becomes its default.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_add_state (LrgEditorAnimGraphEditor  *self,
                                                 const gchar               *name,
                                                 const gchar               *kind,
                                                 const gchar               *clip,
                                                 gfloat                     x,
                                                 gfloat                     y,
                                                 GError                   **error);

/**
 * lrg_editor_anim_graph_editor_remove_state:
 * @self: an #LrgEditorAnimGraphEditor
 * @name: a state of the shown machine
 *
 * Removes the state, every transition from or to it and, when it was
 * the default, the machine's `default:` key.
 *
 * Returns: %TRUE when the state was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_remove_state (LrgEditorAnimGraphEditor *self,
                                                    const gchar              *name);

/**
 * lrg_editor_anim_graph_editor_rename_state:
 * @self: an #LrgEditorAnimGraphEditor
 * @name: a state of the shown machine
 * @new_name: its new name (unique)
 * @error: return location for a #GError
 *
 * Renames a state and every reference to it in the machine (transitions,
 * `default:`).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_rename_state (LrgEditorAnimGraphEditor  *self,
                                                    const gchar               *name,
                                                    const gchar               *new_name,
                                                    GError                   **error);

/**
 * lrg_editor_anim_graph_editor_set_default_state:
 * @self: an #LrgEditorAnimGraphEditor
 * @name: a state of the shown machine
 *
 * Makes @name the machine's start state.
 *
 * Returns: %FALSE when the state does not exist
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_set_default_state (LrgEditorAnimGraphEditor *self,
                                                         const gchar              *name);

/**
 * lrg_editor_anim_graph_editor_set_state_position:
 * @self: an #LrgEditorAnimGraphEditor
 * @name: a state of the shown machine
 * @x: X in graph units
 * @y: Y in graph units
 *
 * Moves the state's card (written as `editor_pos`).
 *
 * Returns: %FALSE when the state does not exist
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_set_state_position (LrgEditorAnimGraphEditor *self,
                                                          const gchar              *name,
                                                          gfloat                    x,
                                                          gfloat                    y);

/**
 * lrg_editor_anim_graph_editor_get_state_path:
 * @self: an #LrgEditorAnimGraphEditor
 * @name: a state of the shown machine
 *
 * Returns: (transfer full) (nullable): the YAML path of the state's
 *   mapping (`/layers/0/states/2`), or %NULL when there is none
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_anim_graph_editor_get_state_path (LrgEditorAnimGraphEditor *self,
                                                     const gchar              *name);

/**
 * lrg_editor_anim_graph_editor_add_transition:
 * @self: an #LrgEditorAnimGraphEditor
 * @from: the source state, or %LRG_EDITOR_ANIM_GRAPH_ANY_STATE for an
 *   any-state transition
 * @to: the target state
 * @duration: the cross-fade in seconds
 * @error: return location for a #GError
 *
 * Adds a transition to the shown machine (no conditions, no exit time).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_add_transition (LrgEditorAnimGraphEditor  *self,
                                                      const gchar               *from,
                                                      const gchar               *to,
                                                      gfloat                     duration,
                                                      GError                   **error);

/**
 * lrg_editor_anim_graph_editor_remove_transition:
 * @self: an #LrgEditorAnimGraphEditor
 * @from: the source state or %LRG_EDITOR_ANIM_GRAPH_ANY_STATE
 * @to: the target state
 *
 * Removes the first transition from @from to @to.
 *
 * Returns: %TRUE when a transition was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_remove_transition (LrgEditorAnimGraphEditor *self,
                                                         const gchar              *from,
                                                         const gchar              *to);

/**
 * lrg_editor_anim_graph_editor_get_transition_path:
 * @self: an #LrgEditorAnimGraphEditor
 * @from: the source state or %LRG_EDITOR_ANIM_GRAPH_ANY_STATE
 * @to: the target state
 *
 * Returns: (transfer full) (nullable): the YAML path of the first
 *   transition from @from to @to in the shown machine
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_anim_graph_editor_get_transition_path (LrgEditorAnimGraphEditor *self,
                                                          const gchar              *from,
                                                          const gchar              *to);

/**
 * lrg_editor_anim_graph_editor_add_condition:
 * @self: an #LrgEditorAnimGraphEditor
 * @from: the source state or %LRG_EDITOR_ANIM_GRAPH_ANY_STATE
 * @to: the target state
 * @parameter: a declared parameter
 * @op: (nullable): "==", "!=", ">", "<", ">=" or "<=" (%NULL for the
 *   short bool / trigger form)
 * @value: (nullable): the compared value as YAML text ("0.1", "true", "2")
 * @error: return location for a #GError
 *
 * Appends a condition to a transition (the conditions editor's "+").
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_add_condition (LrgEditorAnimGraphEditor  *self,
                                                     const gchar               *from,
                                                     const gchar               *to,
                                                     const gchar               *parameter,
                                                     const gchar               *op,
                                                     const gchar               *value,
                                                     GError                   **error);

/**
 * lrg_editor_anim_graph_editor_add_parameter:
 * @self: an #LrgEditorAnimGraphEditor
 * @name: the parameter name (unique)
 * @type: "float", "int", "bool" or "trigger"
 * @error: return location for a #GError
 *
 * Declares a parameter (default 0 / false).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_add_parameter (LrgEditorAnimGraphEditor  *self,
                                                     const gchar               *name,
                                                     const gchar               *type,
                                                     GError                   **error);

/**
 * lrg_editor_anim_graph_editor_remove_parameter:
 * @self: an #LrgEditorAnimGraphEditor
 * @name: a declared parameter
 *
 * Removes the declaration (conditions still naming it are reported by
 * validation).
 *
 * Returns: %TRUE when it was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_remove_parameter (LrgEditorAnimGraphEditor *self,
                                                        const gchar              *name);

/**
 * lrg_editor_anim_graph_editor_declare_model_clips:
 * @self: an #LrgEditorAnimGraphEditor
 * @model: (nullable): a glTF file (relative to the project or absolute);
 *   %NULL for the preview model
 *
 * Adds a clip slot for every animation of @model that has none yet
 * (keyed by the animation name), as one undo step.
 *
 * Returns: the number of slots added
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_anim_graph_editor_declare_model_clips (LrgEditorAnimGraphEditor *self,
                                                        const gchar              *model);

/* ==========================================================================
 * Selection
 * ========================================================================== */

/**
 * lrg_editor_anim_graph_editor_select_state:
 * @self: an #LrgEditorAnimGraphEditor
 * @name: (nullable): a state of the shown machine, %NULL for none
 *
 * Selects a state (the inspector shows it).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_anim_graph_editor_select_state (LrgEditorAnimGraphEditor *self,
                                                const gchar              *name);

/**
 * lrg_editor_anim_graph_editor_select_transition:
 * @self: an #LrgEditorAnimGraphEditor
 * @from: the source state or %LRG_EDITOR_ANIM_GRAPH_ANY_STATE
 * @to: the target state
 *
 * Selects a transition (the inspector shows it and its conditions).
 *
 * Returns: %FALSE when there is no such transition
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_select_transition (LrgEditorAnimGraphEditor *self,
                                                         const gchar              *from,
                                                         const gchar              *to);

/**
 * lrg_editor_anim_graph_editor_get_selected_state:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * Returns: (nullable): the selected state, %NULL when none (or a
 *   transition) is selected
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_anim_graph_editor_get_selected_state (LrgEditorAnimGraphEditor *self);

/**
 * lrg_editor_anim_graph_editor_get_selected_transition:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * Returns: (transfer full) (nullable): the YAML path of the selected
 *   transition, %NULL when none is selected
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_anim_graph_editor_get_selected_transition (LrgEditorAnimGraphEditor *self);

/* ==========================================================================
 * Live preview
 * ========================================================================== */

/**
 * lrg_editor_anim_graph_editor_set_parameter_value:
 * @self: an #LrgEditorAnimGraphEditor
 * @name: a declared parameter
 * @value: the value: a double for float, an int (or double) for int, a
 *   boolean for bool and trigger (%TRUE fires a trigger)
 *
 * Sets a parameter of the preview (not saved; survives edits and
 * reloads of the graph while the editor is open).
 *
 * Returns: %FALSE for an unknown parameter or a value of the wrong kind
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_set_parameter_value (LrgEditorAnimGraphEditor *self,
                                                           const gchar              *name,
                                                           GVariant                 *value);

/**
 * lrg_editor_anim_graph_editor_get_parameter_value:
 * @self: an #LrgEditorAnimGraphEditor
 * @name: a declared parameter
 *
 * Returns: (transfer full) (nullable): the parameter's current value in
 *   the preview graph (a double, an int32 or a boolean)
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_editor_anim_graph_editor_get_parameter_value (LrgEditorAnimGraphEditor *self,
                                                             const gchar              *name);

/**
 * lrg_editor_anim_graph_editor_set_preview_model:
 * @self: an #LrgEditorAnimGraphEditor
 * @model: (nullable): a glTF file, relative to the project or absolute;
 *   %NULL for the first clip slot's source
 * @error: return location for a #GError
 *
 * Loads the skinned model the preview animates and remembers it in the
 * document (`editor_preview:`, relative to the `.ranim`) as one undo
 * step.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_set_preview_model (LrgEditorAnimGraphEditor  *self,
                                                         const gchar               *model,
                                                         GError                   **error);

/**
 * lrg_editor_anim_graph_editor_get_preview_model:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * Returns: (nullable): the absolute path of the model the preview shows
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_anim_graph_editor_get_preview_model (LrgEditorAnimGraphEditor *self);

/**
 * lrg_editor_anim_graph_editor_set_playing:
 * @self: an #LrgEditorAnimGraphEditor
 * @playing: whether preview time runs
 *
 * Plays or pauses the preview (the Play chip).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_anim_graph_editor_set_playing (LrgEditorAnimGraphEditor *self,
                                               gboolean                  playing);

/**
 * lrg_editor_anim_graph_editor_get_playing:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * Returns: whether preview time runs
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_get_playing (LrgEditorAnimGraphEditor *self);

/**
 * lrg_editor_anim_graph_editor_restart:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * Restarts the preview graph: parameters back to the live values, every
 * machine in its default state, time 0.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_anim_graph_editor_restart (LrgEditorAnimGraphEditor *self);

/**
 * lrg_editor_anim_graph_editor_advance:
 * @self: an #LrgEditorAnimGraphEditor
 * @seconds: time to advance (>= 0)
 *
 * Steps the preview graph by @seconds (also while paused) and poses the
 * preview model, the way a frame of playback does.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_anim_graph_editor_advance (LrgEditorAnimGraphEditor *self,
                                           gfloat                    seconds);

/**
 * lrg_editor_anim_graph_editor_scrub:
 * @self: an #LrgEditorAnimGraphEditor
 * @normalized: 0..1 through the base layer's current state
 *
 * Pauses the preview and moves the base layer's current state to
 * @normalized of its length (the scrub bar), then poses the model.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_anim_graph_editor_scrub (LrgEditorAnimGraphEditor *self,
                                         gfloat                    normalized);

/**
 * lrg_editor_anim_graph_editor_get_preview_time:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * Returns: seconds of preview played since the last restart
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_anim_graph_editor_get_preview_time (LrgEditorAnimGraphEditor *self);

/**
 * lrg_editor_anim_graph_editor_get_active_state:
 * @self: an #LrgEditorAnimGraphEditor
 * @layer: (nullable): a layer name, %NULL for the layer shown
 *
 * Returns: (nullable): the state the preview graph's layer is in
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_anim_graph_editor_get_active_state (LrgEditorAnimGraphEditor *self,
                                                             const gchar              *layer);

/**
 * lrg_editor_anim_graph_editor_get_preview_skeleton:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * Returns: (transfer none) (nullable): the posed skeleton of the preview
 *   model, %NULL without a model
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton * lrg_editor_anim_graph_editor_get_preview_skeleton (LrgEditorAnimGraphEditor *self);

/**
 * lrg_editor_anim_graph_editor_set_show_bones:
 * @self: an #LrgEditorAnimGraphEditor
 * @show: whether the preview draws the skeleton over the model
 *
 * Toggles the bone overlay of the preview (its Bones chip).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_anim_graph_editor_set_show_bones (LrgEditorAnimGraphEditor *self,
                                                  gboolean                  show);

/**
 * lrg_editor_anim_graph_editor_get_show_bones:
 * @self: an #LrgEditorAnimGraphEditor
 *
 * Returns: whether the preview draws the skeleton
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_anim_graph_editor_get_show_bones (LrgEditorAnimGraphEditor *self);

/**
 * lrg_editor_anim_graph_editor_render_preview:
 * @self: an #LrgEditorAnimGraphEditor
 * @size: the edge of the square image in pixels
 *
 * Renders the preview (the posed model, and the bones when shown) into
 * an image, off screen. Needs a window (a GL context).
 *
 * Returns: (transfer full) (nullable): the image, %NULL without a
 *   context or a model
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_editor_anim_graph_editor_render_preview (LrgEditorAnimGraphEditor *self,
                                                        guint                     size);

G_END_DECLS

/*
 * The skinned model preview shared by the animation graph editor, the
 * bone map editor, the assets panel's clip preview and the inspector's
 * animator section; not part of the public API.
 */
#ifdef LRG_EDITOR_PANEL_INTERNAL
#include "../../../animation/lrg-skeleton.h"
#include "../../../animation/lrg-animation-clip.h"
#include "../../../graphics/lrg-skinned-mesh.h"

/*
 * EditorAnimPoseFunc:
 * Poses @skeleton (local poses) for this frame; @dt is the frame time
 * (0 while paused). The view then computes world poses and the palette.
 * Without a pose function the view plays its own clip.
 */
typedef void (*EditorAnimPoseFunc) (gpointer        user_data,
                                    LrgSkeleton    *skeleton,
                                    LrgSkinnedMesh *mesh,
                                    gfloat          dt);

G_GNUC_INTERNAL LrgWidget *      editor_anim_view_new           (LrgEditorApp *app, const gchar *name);
G_GNUC_INTERNAL gboolean         editor_anim_view_set_model     (LrgWidget *view, const gchar *abs_path,
                                                                 GError **error);
G_GNUC_INTERNAL const gchar *    editor_anim_view_get_model     (LrgWidget *view);
G_GNUC_INTERNAL LrgSkeleton *    editor_anim_view_get_skeleton  (LrgWidget *view);
G_GNUC_INTERNAL LrgSkinnedMesh * editor_anim_view_get_mesh      (LrgWidget *view);
G_GNUC_INTERNAL GPtrArray *      editor_anim_view_get_clips     (LrgWidget *view);
G_GNUC_INTERNAL void             editor_anim_view_set_pose_func (LrgWidget *view, EditorAnimPoseFunc func,
                                                                 gpointer user_data);
G_GNUC_INTERNAL void             editor_anim_view_set_clip      (LrgWidget *view, gint index);
G_GNUC_INTERNAL gint             editor_anim_view_get_clip      (LrgWidget *view);
G_GNUC_INTERNAL void             editor_anim_view_set_time      (LrgWidget *view, gfloat time);
G_GNUC_INTERNAL gfloat           editor_anim_view_get_time      (LrgWidget *view);
G_GNUC_INTERNAL void             editor_anim_view_set_playing   (LrgWidget *view, gboolean playing);
G_GNUC_INTERNAL gboolean         editor_anim_view_get_playing   (LrgWidget *view);
G_GNUC_INTERNAL void             editor_anim_view_set_show_bones (LrgWidget *view, gboolean show);
G_GNUC_INTERNAL gboolean         editor_anim_view_get_show_bones (LrgWidget *view);
G_GNUC_INTERNAL void             editor_anim_view_set_caption   (LrgWidget *view, const gchar *text);
G_GNUC_INTERNAL void             editor_anim_view_set_morph     (LrgWidget *view, guint target, gfloat weight);
G_GNUC_INTERNAL void             editor_anim_view_set_targets   (LrgWidget *view, const gfloat *xyz, guint n);
G_GNUC_INTERNAL void             editor_anim_view_pose_now      (LrgWidget *view, gfloat dt);
G_GNUC_INTERNAL void             editor_anim_view_set_tint      (LrgWidget *view, guint8 r, guint8 g, guint8 b);
G_GNUC_INTERNAL GrlImage *       editor_anim_view_render        (LrgWidget *view, guint size);
G_GNUC_INTERNAL GPtrArray *      editor_anim_list_models        (LrgEditorApp *app);
#endif
