/* lrg-editor-play-session.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPlaySession: play-in-editor without a UI.
 *
 * start() materialises the editor's level into a private #LrgWorld with
 * lrg_level_instantiate(); every runtime object carries its node's GUID
 * as its entity tag, so after each world tick the session copies what the
 * simulation moved back onto the authoring nodes and the viewport shows
 * the game running in place. stop() puts everything back: edits made
 * while playing are undone (they stay on the redo stack, so "keep the
 * change" is one redo away), the transforms the simulation wrote are
 * restored from a per-node snapshot and the document's dirty state is
 * restored. Values pushed with lrg_editor_play_session_apply_to_level()
 * survive Stop as one undo step.
 *
 * For live inspection the session keeps a node GUID <-> #LrgGameObject
 * map and a #GListModel of the live objects.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-editor-view-state.h"
#include "../world3d/lrg-world3d.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_PLAY_STEP_SECONDS:
 *
 * The fixed time step of lrg_editor_play_session_step() (1/60 s).
 */
#define LRG_EDITOR_PLAY_STEP_SECONDS (1.0f / 60.0f)

/**
 * LRG_EDITOR_PLAY_MAX_TICK_SECONDS:
 *
 * The longest delta one tick simulates (0.1 s); longer frames are clamped
 * so a stall does not tunnel objects through walls.
 */
#define LRG_EDITOR_PLAY_MAX_TICK_SECONDS (0.1f)

/**
 * LRG_EDITOR_PLAY_PROFILER_SECTION:
 *
 * The #LrgProfiler section each tick is timed under ("play.tick").
 */
#define LRG_EDITOR_PLAY_PROFILER_SECTION "play.tick"

#define LRG_TYPE_EDITOR_PLAY_SESSION (lrg_editor_play_session_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorPlaySession, lrg_editor_play_session, LRG, EDITOR_PLAY_SESSION, GObject)

/**
 * lrg_editor_play_session_new:
 * @editor: the editor whose level is played
 *
 * Returns: (transfer full): a new, stopped #LrgEditorPlaySession
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPlaySession * lrg_editor_play_session_new (LrgEditor *editor);

/**
 * lrg_editor_play_session_get_editor:
 * @self: an #LrgEditorPlaySession
 *
 * Returns: (transfer none): the editor
 */
LRG_AVAILABLE_IN_ALL
LrgEditor * lrg_editor_play_session_get_editor (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_get_engine:
 * @self: an #LrgEditorPlaySession
 *
 * Returns: (transfer none) (nullable): the engine whose registry builds
 *   components, or %NULL for the default engine
 */
LRG_AVAILABLE_IN_ALL
LrgEngine * lrg_editor_play_session_get_engine (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_set_engine:
 * @self: an #LrgEditorPlaySession
 * @engine: (nullable): the engine, or %NULL for lrg_engine_get_default()
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_play_session_set_engine (LrgEditorPlaySession *self,
                                         LrgEngine            *engine);

/**
 * lrg_editor_play_session_get_profiler:
 * @self: an #LrgEditorPlaySession
 *
 * Returns: (transfer none) (nullable): the profiler ticks are timed with
 */
LRG_AVAILABLE_IN_ALL
LrgProfiler * lrg_editor_play_session_get_profiler (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_set_profiler:
 * @self: an #LrgEditorPlaySession
 * @profiler: (nullable): a profiler; each tick is wrapped in its
 *   %LRG_EDITOR_PLAY_PROFILER_SECTION section
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_play_session_set_profiler (LrgEditorPlaySession *self,
                                           LrgProfiler          *profiler);

/* --- Control ------------------------------------------------------------------ */

/**
 * lrg_editor_play_session_start:
 * @self: an #LrgEditorPlaySession
 * @error: (nullable): return location for an instantiation error
 *
 * Snapshots the level and starts playing (does nothing when already
 * playing). End a timeline preview before calling this so the preview
 * does not leak into the snapshot. Emits #LrgEditorPlaySession::started.
 *
 * Returns: %TRUE if the session is playing afterwards
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_play_session_start (LrgEditorPlaySession  *self,
                                        GError               **error);

/**
 * lrg_editor_play_session_stop:
 * @self: an #LrgEditorPlaySession
 *
 * Stops playing and restores the level exactly as it was at start (plus
 * values applied with lrg_editor_play_session_apply_to_level(), as one
 * undo step). Emits #LrgEditorPlaySession::stopped.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_play_session_stop (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_set_paused:
 * @self: an #LrgEditorPlaySession
 * @paused: whether the simulation is paused
 *
 * Does nothing while stopped. Emits #LrgEditorPlaySession::paused on a
 * change.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_play_session_set_paused (LrgEditorPlaySession *self,
                                         gboolean              paused);

/**
 * lrg_editor_play_session_toggle_pause:
 * @self: an #LrgEditorPlaySession
 *
 * play.pause (F7).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_play_session_toggle_pause (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_step:
 * @self: an #LrgEditorPlaySession
 * @error: (nullable): return location for an error when starting fails
 *
 * play.step (F10): starts playing if needed, pauses, and lets the next
 * tick simulate exactly %LRG_EDITOR_PLAY_STEP_SECONDS.
 *
 * Returns: %TRUE if the session is playing afterwards
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_play_session_step (LrgEditorPlaySession  *self,
                                       GError               **error);

/**
 * lrg_editor_play_session_tick:
 * @self: an #LrgEditorPlaySession
 * @delta: seconds since the last frame
 *
 * Advances the simulation (clamped to %LRG_EDITOR_PLAY_MAX_TICK_SECONDS;
 * a pending step uses %LRG_EDITOR_PLAY_STEP_SECONDS), writes moved
 * transforms back onto the nodes and refreshes the live object model.
 * Emits #LrgEditorPlaySession::ticked.
 *
 * Returns: %TRUE if the world was updated
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_play_session_tick (LrgEditorPlaySession *self,
                                       gfloat                delta);

/* --- State ---------------------------------------------------------------------- */

/**
 * lrg_editor_play_session_get_state:
 * @self: an #LrgEditorPlaySession
 *
 * Returns: stopped, playing or paused
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPlayState lrg_editor_play_session_get_state (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_is_playing:
 * @self: an #LrgEditorPlaySession
 *
 * Returns: %TRUE while playing or paused (play.state "playing")
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_play_session_is_playing (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_is_paused:
 * @self: an #LrgEditorPlaySession
 *
 * Returns: %TRUE while paused (play.state "paused")
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_play_session_is_paused (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_get_time:
 * @self: an #LrgEditorPlaySession
 *
 * Returns: simulated seconds since start (play.state "time")
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_editor_play_session_get_time (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_get_world:
 * @self: an #LrgEditorPlaySession
 *
 * Returns: (transfer none) (nullable): the play world, %NULL when stopped
 */
LRG_AVAILABLE_IN_ALL
LrgWorld * lrg_editor_play_session_get_world (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_get_world3d:
 * @self: an #LrgEditorPlaySession
 *
 * The 3D play world. A level whose lrg_level_get_default_2d() is %FALSE
 * plays in an #LrgWorld3D built with lrg_level_instantiate_3d(); the
 * editor viewport draws this world while playing.
 *
 * Returns: (transfer none) (nullable): the 3D play world, %NULL when
 *   stopped or when the level plays in 2D
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgWorld3D * lrg_editor_play_session_get_world3d (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_get_is_3d:
 * @self: an #LrgEditorPlaySession
 *
 * Returns: %TRUE while a 3D play world runs (see
 *   lrg_editor_play_session_get_world3d())
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_play_session_get_is_3d (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_get_n_objects:
 * @self: an #LrgEditorPlaySession
 *
 * Returns: the number of live objects: game objects in 2D, entities
 *   (without the world root) in 3D, 0 when stopped
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_play_session_get_n_objects (LrgEditorPlaySession *self);

/* --- Live objects ------------------------------------------------------------------ */

/**
 * lrg_editor_play_session_get_objects:
 * @self: an #LrgEditorPlaySession
 *
 * The live 2D game objects in world order (empty while stopped and while
 * a 3D world plays; use lrg_editor_play_session_get_live_model() for
 * both). The model is the same object for the session's lifetime and
 * emits items-changed when play starts, stops, or a tick adds or removes
 * objects.
 *
 * Returns: (transfer none): a #GListModel of #LrgGameObject
 */
LRG_AVAILABLE_IN_ALL
GListModel * lrg_editor_play_session_get_objects (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_sync_objects:
 * @self: an #LrgEditorPlaySession
 *
 * Rescans the world into the object models and the GUID map (ticks do
 * this already; call it after changing the world from outside a tick).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_play_session_sync_objects (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_get_live_model:
 * @self: an #LrgEditorPlaySession
 *
 * The live tree's top level: the game objects in world order when the
 * level plays in 2D, the top-level #LrgEntity3D entities (the children of
 * the world root) in 3D, empty while stopped. Pair it with
 * lrg_editor_play_session_get_live_children() as a tree view's children
 * function. The model is the same object for the session's lifetime and
 * emits items-changed when play starts, stops, or the world changes.
 *
 * Returns: (transfer none): a #GListModel of #GObject
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GListModel * lrg_editor_play_session_get_live_model (LrgEditorPlaySession *self);

/**
 * lrg_editor_play_session_get_live_children:
 * @self: an #LrgEditorPlaySession
 * @item: an item of the live tree
 *
 * The children of an #LrgEntity3D of the 3D play world, in child order.
 * The model is created on first use, kept up to date by every tick and
 * lrg_editor_play_session_sync_objects(), and emptied when @item leaves
 * the world or play stops.
 *
 * Returns: (transfer full) (nullable): a #GListModel of #LrgEntity3D, or
 *   %NULL for 2D game objects and for items not in the play world
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GListModel * lrg_editor_play_session_get_live_children (LrgEditorPlaySession *self,
                                                        GObject              *item);

/**
 * lrg_editor_play_session_get_live_for_guid:
 * @self: an #LrgEditorPlaySession
 * @guid: a node GUID
 *
 * Looks up the live object made from the node with @guid: an
 * #LrgGameObject in 2D, an #LrgEntity3D in 3D (the world's GUID map).
 *
 * Returns: (transfer none) (nullable): the live object
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_editor_play_session_get_live_for_guid (LrgEditorPlaySession *self,
                                                     const gchar          *guid);

/**
 * lrg_editor_play_session_get_live_for_node:
 * @self: an #LrgEditorPlaySession
 * @node: an authoring node
 *
 * Returns: (transfer none) (nullable): the live object (#LrgGameObject or
 *   #LrgEntity3D) made from @node
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_editor_play_session_get_live_for_node (LrgEditorPlaySession *self,
                                                     LrgNode              *node);

/**
 * lrg_editor_play_session_get_guid_for_live:
 * @self: an #LrgEditorPlaySession
 * @object: a live #LrgGameObject or #LrgEntity3D
 *
 * Returns: (transfer none) (nullable): the GUID of the node @object was
 *   made from, %NULL for objects the simulation spawned
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_play_session_get_guid_for_live (LrgEditorPlaySession *self,
                                                         GObject              *object);

/**
 * lrg_editor_play_session_get_node_for_live:
 * @self: an #LrgEditorPlaySession
 * @object: a live #LrgGameObject or #LrgEntity3D
 *
 * Returns: (transfer none) (nullable): the authoring node @object was
 *   made from
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_play_session_get_node_for_live (LrgEditorPlaySession *self,
                                                     GObject              *object);

/**
 * lrg_editor_play_session_get_object_for_guid:
 * @self: an #LrgEditorPlaySession
 * @guid: a node GUID
 *
 * Returns: (transfer none) (nullable): the live object made from that
 *   node
 *
 * 2D only: returns %NULL while a 3D world plays.
 *
 * Deprecated: 0.9.0: Use lrg_editor_play_session_get_live_for_guid(), which works for both 2D and 3D.
 */
LRG_AVAILABLE_IN_ALL
LrgGameObject * lrg_editor_play_session_get_object_for_guid (LrgEditorPlaySession *self,
                                                             const gchar          *guid);

/**
 * lrg_editor_play_session_get_object_for_node:
 * @self: an #LrgEditorPlaySession
 * @node: an authoring node
 *
 * Returns: (transfer none) (nullable): the live object made from @node
 *
 * 2D only: returns %NULL while a 3D world plays.
 *
 * Deprecated: 0.9.0: Use lrg_editor_play_session_get_live_for_node(), which works for both 2D and 3D.
 */
LRG_AVAILABLE_IN_ALL
LrgGameObject * lrg_editor_play_session_get_object_for_node (LrgEditorPlaySession *self,
                                                             LrgNode              *node);

/**
 * lrg_editor_play_session_get_guid_for_object:
 * @self: an #LrgEditorPlaySession
 * @object: a live object
 *
 * Returns: (transfer none) (nullable): the GUID of the node @object was
 *   made from
 *
 * 2D only: returns %NULL while a 3D world plays.
 *
 * Deprecated: 0.9.0: Use lrg_editor_play_session_get_guid_for_live(), which works for both 2D and 3D.
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_play_session_get_guid_for_object (LrgEditorPlaySession *self,
                                                           LrgGameObject        *object);

/**
 * lrg_editor_play_session_get_node_for_object:
 * @self: an #LrgEditorPlaySession
 * @object: a live object
 *
 * Returns: (transfer none) (nullable): the authoring node @object was
 *   made from
 *
 * 2D only: returns %NULL while a 3D world plays.
 *
 * Deprecated: 0.9.0: Use lrg_editor_play_session_get_node_for_live(), which works for both 2D and 3D.
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_play_session_get_node_for_object (LrgEditorPlaySession *self,
                                                       LrgGameObject        *object);

/**
 * lrg_editor_play_session_apply_to_level:
 * @self: an #LrgEditorPlaySession
 * @object: a live object
 * @component: a component of @object
 * @property: a property of @component
 * @error: (nullable): return location for an error
 *
 * "Apply to level": copies the live value of @property into the matching
 * #LrgComponentDesc of the authoring node, as one undoable editor
 * command. The component is matched by its registry type name and its
 * position among the object's components of that type. The value is
 * re-applied after Stop (all applied values together as one undo step),
 * so it survives the restore.
 *
 * Returns: %TRUE on success
 *
 * Deprecated: 0.9.0: Use lrg_editor_play_session_apply_live(), which also
 *   takes 3D entities.
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_play_session_apply_to_level (LrgEditorPlaySession  *self,
                                                 LrgGameObject         *object,
                                                 LrgComponent          *component,
                                                 const gchar           *property,
                                                 GError               **error);

/**
 * lrg_editor_play_session_apply_live:
 * @self: an #LrgEditorPlaySession
 * @object: a live #LrgGameObject or #LrgEntity3D
 * @component: a component of @object
 * @property: a property of @component
 * @error: (nullable): return location for an error
 *
 * "Apply to level" for both play modes: copies the live value of
 * @property into the authoring node as one undoable editor command, and
 * re-applies it after Stop (all applied values together as one undo
 * step) so it survives the restore.
 *
 * - A registry component (2D or 3D) maps to the #LrgComponentDesc with
 *   its registry type name, matched by its position among the object's
 *   components of that type.
 * - In 3D, the component made from the node's visual (mesh renderer,
 *   light, camera, audio source, billboard, tilemap) maps to the visual:
 *   the property is stored into its param bag or material with
 *   lrg_level_visual_store_property(), the inverse of what
 *   lrg_level_instantiate_3d() reads.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_play_session_apply_live (LrgEditorPlaySession  *self,
                                             GObject               *object,
                                             LrgComponent          *component,
                                             const gchar           *property,
                                             GError               **error);

G_END_DECLS
