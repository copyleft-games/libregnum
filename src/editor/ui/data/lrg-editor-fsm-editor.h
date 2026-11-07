/* lrg-editor-fsm-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorFsmEditor: the data editor of `.rfsm` logic state machines.
 *
 * The canvas (an #LrgNodeGraph) shows one level of the hierarchy at a
 * time: the child states of the shown state (the machine's top level at
 * first) as cards, the transitions between them as wires from the "out"
 * port of the owner to the "in" port of the target, and one extra card on
 * the left carrying the shown state's own transitions (at the top level:
 * "Any State", the machine-wide transitions; inside a state: "While in
 * <state>", which outrank its children's). A transition whose target lies
 * outside the shown level ends on a small "→ target" card. A star marks the
 * initial state; a card's body lists its enter / update / exit actions and
 * its children. Double click enters a state's sub-machine (an empty one is
 * a place to add children: that is how a state becomes hierarchical), the
 * breadcrumbs above the canvas walk back up.
 *
 * Dragging a wire adds a transition, Delete removes states (with the
 * transitions that target them) and transitions, dropping a wire on empty
 * canvas adds a state there with a transition to it. The inspector edits
 * the selection: a state's name, initial flag, its enter / update / exit
 * action lists and its outgoing transitions; a transition's target, event,
 * timer (`after`), its `when:` conditions and `do:` actions. Action and
 * condition entries are edited as one line of YAML each
 * (`{action: shout}`, `{key: hp, op: lt, value: 8}`), with buttons adding
 * every entry kind. The blackboard schema table is the same as the
 * behaviour tree editor's.
 *
 * The document is the truth (one undo step per gesture). Card positions
 * live in the top-level `editor: {positions: {PATH: [x, y]}}` block, keyed
 * by state path ("combat/attack"); the owner card's under "PATH/@owner".
 * While the level plays, the focused entity's machine is shown live: the
 * active states are tinted (the innermost brighter), the state time is on
 * the card, the last transition's wire is lit and every wire's owner card
 * lists how often each transition fired.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../ai/lrg-logic-machine.h"
#include "../../../ui/lrg-node-graph.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_FSM_EDITOR (lrg_editor_fsm_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorFsmEditor, lrg_editor_fsm_editor, LRG, EDITOR_FSM_EDITOR, LrgEditorDataEditor)

/**
 * LRG_EDITOR_FSM_EDITOR_OWNER_CARD:
 *
 * The card id of the shown state's own transitions ("Any State" at the
 * top level).
 */
#define LRG_EDITOR_FSM_EDITOR_OWNER_CARD "@owner"

/**
 * lrg_editor_fsm_editor_new:
 * @app: (nullable): the editor application
 * @document: a logic state machine document
 *
 * Creates the editor without docking it; lrg_editor_data_editor_open()
 * (or lrg_editor_fsm_editor_open()) is what the editor uses.
 *
 * Returns: (transfer full): a new #LrgEditorFsmEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorFsmEditor * lrg_editor_fsm_editor_new (LrgEditorApp      *app,
                                                LrgEditorDocument *document);

/**
 * lrg_editor_fsm_editor_install:
 * @app: the editor application
 *
 * Registers the fsm.* IPC methods (once).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_fsm_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_fsm_editor_open:
 * @app: the editor application
 * @path: a `.rfsm` file, absolute or relative to the project
 * @error: return location for a #GError
 *
 * Opens (or presents) the tab of @path.
 *
 * Returns: (transfer none) (nullable): the editor, or %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgEditorFsmEditor * lrg_editor_fsm_editor_open (LrgEditorApp  *app,
                                                 const gchar   *path,
                                                 GError       **error);

/**
 * lrg_editor_fsm_editor_create:
 * @app: the editor application
 * @path: the new `.rfsm` file, absolute or relative to the project
 * @name: (nullable): the machine's name (default: the file name)
 * @error: return location for a #GError
 *
 * Writes a starter machine (one `idle` state, initial) and opens it.
 * Refuses to replace an existing file.
 *
 * Returns: (transfer none) (nullable): the editor of the new file
 */
LRG_AVAILABLE_IN_ALL
LrgEditorFsmEditor * lrg_editor_fsm_editor_create (LrgEditorApp  *app,
                                                   const gchar   *path,
                                                   const gchar   *name,
                                                   GError       **error);

/**
 * lrg_editor_fsm_editor_get_machine:
 * @self: an #LrgEditorFsmEditor
 *
 * Returns: (transfer none) (nullable): the machine parsed from the
 *   document after the last change (never started), %NULL while the text
 *   does not load
 */
LRG_AVAILABLE_IN_ALL
LrgLogicMachine * lrg_editor_fsm_editor_get_machine (LrgEditorFsmEditor *self);

/**
 * lrg_editor_fsm_editor_get_load_error:
 * @self: an #LrgEditorFsmEditor
 *
 * Returns: (transfer none) (nullable): why the document does not load
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_fsm_editor_get_load_error (LrgEditorFsmEditor *self);

/**
 * lrg_editor_fsm_editor_get_canvas:
 * @self: an #LrgEditorFsmEditor
 *
 * Returns: (transfer none): the node graph widget
 */
LRG_AVAILABLE_IN_ALL
LrgNodeGraph * lrg_editor_fsm_editor_get_canvas (LrgEditorFsmEditor *self);

/* --- navigation ----------------------------------------------------------- */

/**
 * lrg_editor_fsm_editor_get_shown:
 * @self: an #LrgEditorFsmEditor
 *
 * Returns: (transfer none): the path of the state whose children the
 *   canvas shows ("" for the top level)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_fsm_editor_get_shown (LrgEditorFsmEditor *self);

/**
 * lrg_editor_fsm_editor_enter:
 * @self: an #LrgEditorFsmEditor
 * @state: a state path ("combat", "combat/attack"; "" the top level)
 *
 * Shows @state's children (what double clicking a card does).
 *
 * Returns: %FALSE for an unknown state
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_fsm_editor_enter (LrgEditorFsmEditor *self,
                                      const gchar        *state);

/**
 * lrg_editor_fsm_editor_go_up:
 * @self: an #LrgEditorFsmEditor
 *
 * Shows the parent level.
 *
 * Returns: %FALSE at the top level
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_fsm_editor_go_up (LrgEditorFsmEditor *self);

/**
 * lrg_editor_fsm_editor_dup_breadcrumbs:
 * @self: an #LrgEditorFsmEditor
 *
 * Returns: (transfer full) (array zero-terminated=1): the machine's name,
 *   then one segment per level down to the shown state
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_editor_fsm_editor_dup_breadcrumbs (LrgEditorFsmEditor *self);

/* --- states --------------------------------------------------------------- */

/**
 * lrg_editor_fsm_editor_dup_state_paths:
 * @self: an #LrgEditorFsmEditor
 *
 * Returns: (transfer full) (array zero-terminated=1): every state of the
 *   machine, depth first, as paths ("combat", "combat/attack")
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_editor_fsm_editor_dup_state_paths (LrgEditorFsmEditor *self);

/**
 * lrg_editor_fsm_editor_get_state_yaml_path:
 * @self: an #LrgEditorFsmEditor
 * @state: a state path ("" the root)
 *
 * Returns: (transfer full) (nullable): the state's YAML path in the
 *   document ("/states/1/states/0"; "" for the root)
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_fsm_editor_get_state_yaml_path (LrgEditorFsmEditor *self,
                                                   const gchar        *state);

/**
 * lrg_editor_fsm_editor_add_state:
 * @self: an #LrgEditorFsmEditor
 * @parent: the parent's path ("" for the top level)
 * @name: the new state's name (unique among its siblings, no '/'); %NULL
 *   for a free "stateN"
 * @x: card position
 * @y: card position
 * @error: return location for a #GError
 *
 * Adds a state (one undo step). The first child of a state becomes its
 * initial state.
 *
 * Returns: (transfer full) (nullable): the new state's path
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_fsm_editor_add_state (LrgEditorFsmEditor  *self,
                                         const gchar         *parent,
                                         const gchar         *name,
                                         gfloat               x,
                                         gfloat               y,
                                         GError             **error);

/**
 * lrg_editor_fsm_editor_remove_state:
 * @self: an #LrgEditorFsmEditor
 * @state: a state path
 * @error: return location for a #GError
 *
 * Removes a state with its children and every transition targeting it;
 * an `initial:` naming it is dropped (one undo step).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_fsm_editor_remove_state (LrgEditorFsmEditor  *self,
                                             const gchar         *state,
                                             GError             **error);

/**
 * lrg_editor_fsm_editor_rename_state:
 * @self: an #LrgEditorFsmEditor
 * @state: a state path
 * @name: the new name
 * @error: return location for a #GError
 *
 * Renames a state and rewrites the `to:` and `initial:` entries that name
 * it (by name, relative or absolute path) and its saved positions.
 *
 * Returns: (transfer full) (nullable): the new path
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_fsm_editor_rename_state (LrgEditorFsmEditor  *self,
                                           const gchar         *state,
                                           const gchar         *name,
                                           GError             **error);

/**
 * lrg_editor_fsm_editor_set_initial:
 * @self: an #LrgEditorFsmEditor
 * @state: a state path
 * @error: return location for a #GError
 *
 * Makes @state its parent's `initial:` child.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_fsm_editor_set_initial (LrgEditorFsmEditor  *self,
                                            const gchar         *state,
                                            GError             **error);

/**
 * lrg_editor_fsm_editor_set_actions:
 * @self: an #LrgEditorFsmEditor
 * @state: a state path ("" for the machine-wide lists)
 * @phase: %LRG_LOGIC_PHASE_ENTER, %LRG_LOGIC_PHASE_UPDATE or
 *   %LRG_LOGIC_PHASE_EXIT
 * @yaml: (nullable): the list as YAML ("[{action: shout}]"), %NULL or "[]"
 *   to remove it
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_fsm_editor_set_actions (LrgEditorFsmEditor  *self,
                                            const gchar         *state,
                                            LrgLogicPhase        phase,
                                            const gchar         *yaml,
                                            GError             **error);

/**
 * lrg_editor_fsm_editor_set_state_position:
 * @self: an #LrgEditorFsmEditor
 * @state: a state path
 * @x: left edge in graph units
 * @y: top edge in graph units
 *
 * Returns: %FALSE for an unknown state
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_fsm_editor_set_state_position (LrgEditorFsmEditor *self,
                                                   const gchar        *state,
                                                   gfloat              x,
                                                   gfloat              y);

/**
 * lrg_editor_fsm_editor_get_state_position:
 * @self: an #LrgEditorFsmEditor
 * @state: a state path
 * @x: (out) (optional): left edge
 * @y: (out) (optional): top edge
 *
 * Returns: %TRUE when the position is saved in the file (else the grid
 *   position the canvas uses)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_fsm_editor_get_state_position (LrgEditorFsmEditor *self,
                                                   const gchar        *state,
                                                   gfloat             *x,
                                                   gfloat             *y);

/* --- transitions ---------------------------------------------------------- */

/**
 * lrg_editor_fsm_editor_add_transition:
 * @self: an #LrgEditorFsmEditor
 * @from: the owner's path ("" for an any-state transition)
 * @to: the target as written in the file (a name, a relative path or an
 *   absolute "/a/b")
 * @event: (nullable): the triggering event, %NULL for event-less
 * @after: the timer in seconds, negative for none
 * @error: return location for a #GError
 *
 * Returns: the new transition's index in @from's list, -1 on error
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_fsm_editor_add_transition (LrgEditorFsmEditor  *self,
                                           const gchar         *from,
                                           const gchar         *to,
                                           const gchar         *event,
                                           gdouble              after,
                                           GError             **error);

/**
 * lrg_editor_fsm_editor_remove_transition:
 * @self: an #LrgEditorFsmEditor
 * @from: the owner's path
 * @index: the transition's index in the owner's list
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_fsm_editor_remove_transition (LrgEditorFsmEditor  *self,
                                                  const gchar         *from,
                                                  guint                index,
                                                  GError             **error);

/**
 * lrg_editor_fsm_editor_set_transition_field:
 * @self: an #LrgEditorFsmEditor
 * @from: the owner's path
 * @index: the transition's index
 * @key: "to", "event", "after", "when" or "do"
 * @yaml: (nullable): the value as YAML, %NULL to remove the key (not "to")
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_fsm_editor_set_transition_field (LrgEditorFsmEditor  *self,
                                                     const gchar         *from,
                                                     guint                index,
                                                     const gchar         *key,
                                                     const gchar         *yaml,
                                                     GError             **error);

/**
 * lrg_editor_fsm_editor_get_transition_count:
 * @self: an #LrgEditorFsmEditor
 * @from: the owner's path ("" for the any-state list)
 *
 * Returns: the number of transitions @from owns
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_fsm_editor_get_transition_count (LrgEditorFsmEditor *self,
                                                  const gchar        *from);

/* --- blackboard ----------------------------------------------------------- */

/**
 * lrg_editor_fsm_editor_add_key:
 * @self: an #LrgEditorFsmEditor
 * @name: the key
 * @type: "int", "float", "bool", "string" or "object"
 * @default_yaml: (nullable): the default as YAML text
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_fsm_editor_add_key (LrgEditorFsmEditor  *self,
                                        const gchar         *name,
                                        const gchar         *type,
                                        const gchar         *default_yaml,
                                        GError             **error);

/**
 * lrg_editor_fsm_editor_remove_key:
 * @self: an #LrgEditorFsmEditor
 * @name: a declared key
 *
 * Returns: %TRUE if the key was declared
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_fsm_editor_remove_key (LrgEditorFsmEditor *self,
                                           const gchar        *name);

/* --- selection ------------------------------------------------------------ */

/**
 * lrg_editor_fsm_editor_select_state:
 * @self: an #LrgEditorFsmEditor
 * @state: (nullable): a state path (its level is shown), %NULL to clear
 *
 * Returns: %FALSE for an unknown state
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_fsm_editor_select_state (LrgEditorFsmEditor *self,
                                             const gchar        *state);

/**
 * lrg_editor_fsm_editor_select_transition:
 * @self: an #LrgEditorFsmEditor
 * @from: the owner's path
 * @index: the transition's index
 *
 * Returns: %FALSE when there is no such transition
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_fsm_editor_select_transition (LrgEditorFsmEditor *self,
                                                  const gchar        *from,
                                                  guint               index);

/**
 * lrg_editor_fsm_editor_get_selected_state:
 * @self: an #LrgEditorFsmEditor
 *
 * Returns: (transfer none) (nullable): the selected state's path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_fsm_editor_get_selected_state (LrgEditorFsmEditor *self);

/**
 * lrg_editor_fsm_editor_get_selected_transition:
 * @self: an #LrgEditorFsmEditor
 * @index: (out) (optional): the transition's index
 *
 * Returns: (transfer none) (nullable): the selected transition's owner
 *   path, %NULL when no transition is selected
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_fsm_editor_get_selected_transition (LrgEditorFsmEditor *self,
                                                             guint              *index);

/* --- live state ----------------------------------------------------------- */

/**
 * lrg_editor_fsm_editor_get_live_machine:
 * @self: an #LrgEditorFsmEditor
 *
 * Returns: (transfer none) (nullable): the running machine of the live
 *   entity the canvas follows, %NULL when stopped or not attached
 */
LRG_AVAILABLE_IN_ALL
LrgLogicMachine * lrg_editor_fsm_editor_get_live_machine (LrgEditorFsmEditor *self);

/**
 * lrg_editor_fsm_editor_send_live_event:
 * @self: an #LrgEditorFsmEditor
 * @event: an event name
 *
 * Queues @event on the live machine (handled at its next update).
 *
 * Returns: %FALSE when nothing is live
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_fsm_editor_send_live_event (LrgEditorFsmEditor *self,
                                                const gchar        *event);

G_END_DECLS
