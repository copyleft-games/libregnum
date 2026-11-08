/* lrg-editor-node-source.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorNodeSource: one section (header, transform, visual, one
 * component, scripts) of one #LrgNode exposed as an #LrgPropertySource,
 * so the inspector can hand an #LrgPropertyGrid one source per selected
 * node (multi-edit). Every edit goes through the #LrgEditor undo stack
 * (and so marks prefab overrides); gestures turn a whole property-grid
 * drag over N nodes into exactly one undo step.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "../ui/lrg-property-source.h"
#include "../ui/lrg-property-grid.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgEditorNodeSection
 * ========================================================================== */

/**
 * LrgEditorNodeSection:
 * @LRG_EDITOR_NODE_SECTION_HEADER: name, visible, locked, 2D flag, plus
 *   the read-only path and guid
 * @LRG_EDITOR_NODE_SECTION_TRANSFORM: local location, rotation (degrees)
 *   and scale as vector3 rows, plus a read-only world position
 * @LRG_EDITOR_NODE_SECTION_VISUAL: visual kind, primitive, asset,
 *   material and the visual's parameter bag
 * @LRG_EDITOR_NODE_SECTION_COMPONENT: the properties of one component
 *   description (named by the source's component type)
 * @LRG_EDITOR_NODE_SECTION_SCRIPTS: language, path and enabled flag of
 *   every script binding
 *
 * Which part of a node an #LrgEditorNodeSource exposes. The inspector
 * shows one #LrgPropertyGrid per section.
 */
typedef enum
{
	LRG_EDITOR_NODE_SECTION_HEADER,
	LRG_EDITOR_NODE_SECTION_TRANSFORM,
	LRG_EDITOR_NODE_SECTION_VISUAL,
	LRG_EDITOR_NODE_SECTION_COMPONENT,
	LRG_EDITOR_NODE_SECTION_SCRIPTS
} LrgEditorNodeSection;

#define LRG_TYPE_EDITOR_NODE_SECTION (lrg_editor_node_section_get_type ())

/**
 * lrg_editor_node_section_get_type:
 *
 * Returns: the #GType of #LrgEditorNodeSection
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_node_section_get_type (void) G_GNUC_CONST;

/* ==========================================================================
 * LrgEditorNodeSource
 * ========================================================================== */

#define LRG_TYPE_EDITOR_NODE_SOURCE (lrg_editor_node_source_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorNodeSource, lrg_editor_node_source, LRG, EDITOR_NODE_SOURCE, GObject)

/**
 * lrg_editor_node_source_new:
 * @editor: the editor whose undo stack records the edits
 * @node: the node to expose
 * @section: which part of @node to expose
 * @component_type: (nullable): for %LRG_EDITOR_NODE_SECTION_COMPONENT, the
 *   component's type name ("Health"; "Health#2" for the second component
 *   of that type, like prefab override paths); ignored otherwise
 *
 * Creates a property source over one section of @node. The source keeps
 * references to @editor and @node, follows #LrgEditor::changed,
 * #LrgEditor::level-replaced and #LrgLevel::node-changed, and emits
 * #LrgPropertySource::property-changed for each property whose value or
 * state really changed (and with a %NULL name when the property list
 * changed, e.g. after the visual kind changed).
 *
 * Returns: (transfer full): a new #LrgEditorNodeSource
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNodeSource * lrg_editor_node_source_new (LrgEditor            *editor,
                                                  LrgNode              *node,
                                                  LrgEditorNodeSection  section,
                                                  const gchar          *component_type);

/**
 * lrg_editor_node_source_new_for_nodes:
 * @editor: the editor
 * @nodes: (element-type LrgNode): the nodes, e.g. the selection
 * @section: which part of the nodes to expose
 * @component_type: (nullable): the component type for
 *   %LRG_EDITOR_NODE_SECTION_COMPONENT
 *
 * One source per node, ready for lrg_property_grid_set_sources(). For the
 * component section, nodes without a component of @component_type are
 * left out.
 *
 * Returns: (transfer full) (element-type LrgEditorNodeSource): the sources;
 *   the array frees them
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_node_source_new_for_nodes (LrgEditor            *editor,
                                                  GPtrArray            *nodes,
                                                  LrgEditorNodeSection  section,
                                                  const gchar          *component_type);

/**
 * lrg_editor_node_source_get_editor:
 * @self: an #LrgEditorNodeSource
 *
 * Returns: (transfer none): the editor
 */
LRG_AVAILABLE_IN_ALL
LrgEditor * lrg_editor_node_source_get_editor (LrgEditorNodeSource *self);

/**
 * lrg_editor_node_source_get_node:
 * @self: an #LrgEditorNodeSource
 *
 * Returns: (transfer none): the node
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_node_source_get_node (LrgEditorNodeSource *self);

/**
 * lrg_editor_node_source_get_section:
 * @self: an #LrgEditorNodeSource
 *
 * Returns: the exposed section
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNodeSection lrg_editor_node_source_get_section (LrgEditorNodeSource *self);

/**
 * lrg_editor_node_source_get_component_type:
 * @self: an #LrgEditorNodeSource
 *
 * Returns: (transfer none) (nullable): the component type of a component
 *   section, %NULL for other sections
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_node_source_get_component_type (LrgEditorNodeSource *self);

/**
 * lrg_editor_node_source_get_asset_root:
 * @self: an #LrgEditorNodeSource
 *
 * Returns: (transfer none) (nullable): the directory relative asset and
 *   script paths are checked against, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_node_source_get_asset_root (LrgEditorNodeSource *self);

/**
 * lrg_editor_node_source_set_asset_root:
 * @self: an #LrgEditorNodeSource
 * @root: (nullable): the project root, or %NULL
 *
 * Sets the directory relative asset paths (visual asset, script path) are
 * resolved against for the %LRG_PROPERTY_STATE_INVALID check. With %NULL
 * the directory of the editor's level file and the current directory
 * are tried. Values that are not paths (asset guids) are never invalid.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_node_source_set_asset_root (LrgEditorNodeSource *self,
                                            const gchar         *root);

/**
 * lrg_editor_node_source_get_override_path:
 * @self: an #LrgEditorNodeSource
 * @name: a property name of the source
 *
 * The prefab property path a property maps to ("transform/location",
 * "visual/params/intensity", "components/Health/max-hp",
 * "scripts/ai.lua/enabled"; see `docs/editor/prefabs.org`).
 *
 * Returns: (transfer full) (nullable): the path, or %NULL for properties
 *   that are not part of a prefab instance's state (locked, guid, ...)
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_node_source_get_override_path (LrgEditorNodeSource *self,
                                                  const gchar         *name);

/**
 * lrg_editor_node_source_revert_override:
 * @self: an #LrgEditorNodeSource
 * @name: a property name of the source
 * @error: (nullable): return location for an error
 *
 * For a node of a linked prefab instance whose property @name is
 * overridden: puts the prefab's value back and drops the override, as
 * one undo step (lrg_editor_prefab_revert()). When a parent path covers
 * the property (`visual` covers `visual/asset`), that override is
 * reverted.
 *
 * Returns: %TRUE if an override was reverted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_node_source_revert_override (LrgEditorNodeSource  *self,
                                                 const gchar          *name,
                                                 GError              **error);

/**
 * lrg_editor_node_source_apply_override:
 * @self: an #LrgEditorNodeSource
 * @name: a property name of the source
 * @error: (nullable): return location for an error
 *
 * For an overridden property of a linked prefab instance: writes the
 * instance's value into the source prefab, saves it and syncs the other
 * instances, as one undo step (lrg_editor_prefab_apply()).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_node_source_apply_override (LrgEditorNodeSource  *self,
                                                const gchar          *name,
                                                GError              **error);

/**
 * lrg_editor_node_source_begin_gesture:
 * @self: an #LrgEditorNodeSource
 *
 * Starts a gesture: the node's state is snapshotted, and every
 * lrg_property_source_set_value() until
 * lrg_editor_node_source_end_gesture() changes the node in place as a
 * preview, without touching the undo stack. Gestures nest; only the
 * outermost pair counts.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_node_source_begin_gesture (LrgEditorNodeSource *self);

/**
 * lrg_editor_node_source_end_gesture:
 * @self: an #LrgEditorNodeSource
 * @commit: %TRUE to keep the edit, %FALSE to cancel it
 * @label: (nullable): the undo label, e.g. "Set Location"
 *
 * Ends a gesture. With @commit the difference from the snapshot is
 * recorded as one undoable command (lrg_editor_commit_node_state()), which
 * also marks the prefab overrides it touches; call it for N sources
 * between lrg_editor_begin_group() and lrg_editor_end_group() to make one
 * undo step of an edit of N nodes. Without @commit the node is restored
 * exactly and nothing is recorded.
 *
 * Returns: %TRUE if a command was recorded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_node_source_end_gesture (LrgEditorNodeSource *self,
                                             gboolean             commit,
                                             const gchar         *label);

/**
 * lrg_editor_node_source_in_gesture:
 * @self: an #LrgEditorNodeSource
 *
 * Returns: %TRUE between lrg_editor_node_source_begin_gesture() and the
 *   matching lrg_editor_node_source_end_gesture()
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_node_source_in_gesture (LrgEditorNodeSource *self);

/**
 * lrg_editor_node_source_refresh:
 * @self: an #LrgEditorNodeSource
 *
 * Re-reads the node now and emits #LrgPropertySource::property-changed for
 * what changed since the last emission. The source does this itself on
 * the editor's and level's change signals; call it after mutating the
 * node behind the editor's back.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_node_source_refresh (LrgEditorNodeSource *self);

/**
 * lrg_editor_node_source_connect_grid:
 * @grid: an #LrgPropertyGrid
 * @editor: the editor that records the grid's edits
 *
 * Wires @grid to @editor's undo stack, whatever sources @grid holds at
 * the time of each edit: it switches @grid to
 * %LRG_PROPERTY_APPLY_DELEGATE and handles its signals.
 *
 * - #LrgPropertyGrid::edit-begin starts a gesture on every
 *   #LrgEditorNodeSource.
 * - #LrgPropertyGrid::edit-value writes
 *   lrg_property_grid_get_value_for_source() into every source as a live
 *   preview (vectors keep each node's own other axes).
 * - #LrgPropertyGrid::edit-end with commit records all sources inside one
 *   lrg_editor_begin_group(): a whole scrub over N nodes is ONE undo step.
 *   A cancelled gesture (Escape) restores every node exactly and records
 *   nothing.
 * - #LrgPropertyGrid::reset-requested reverts the prefab override of
 *   overridden sources and resets the others to their default, as one
 *   undo step.
 *
 * Other sources (an #LrgObjectPropertySource over a live play object) are
 * written directly, not undoably, and restored on cancel. Connecting a
 * grid twice does nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_node_source_connect_grid (LrgPropertyGrid *grid,
                                          LrgEditor       *editor);

/**
 * lrg_editor_node_source_disconnect_grid:
 * @grid: an #LrgPropertyGrid
 *
 * Undoes lrg_editor_node_source_connect_grid(). An open gesture is
 * committed first.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_node_source_disconnect_grid (LrgPropertyGrid *grid);

/**
 * lrg_editor_node_source_get_suggested_double:
 * @kind: a visual kind
 * @param: a parameter name (for example "radius")
 * @out: (out) (optional): the suggested value
 *
 * Reads the inspector's suggested numeric parameter for @kind. The
 * audio emitter's "radius" is the audible distance, the same default
 * as #LrgAudioSourceComponent:max-distance (50).
 *
 * Returns: %TRUE when @kind lists @param
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_node_source_get_suggested_double (LrgNodeVisualKind  kind,
                                                      const gchar       *param,
                                                      gdouble           *out);

G_END_DECLS
