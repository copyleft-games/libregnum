/* lrg-editor-sequencer-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSequencerPanel: the editor's .rseq sequencer.
 *
 * A docked panel (id "sequencer") that edits one #LrgSequence file at a
 * time on an #LrgTimelineView: tracks grouped by binding, keys of
 * transform, bone and property curves (with a Bézier curve editor for
 * their tangents), camera cut, audio, animation and sub-sequence
 * sections as clips, and event markers. Bindings are level nodes picked
 * by GUID; scrubbing and playback preview the sequence on the level's
 * nodes in the viewport (and fly the viewport through camera cuts when
 * "Pilot" is on). Edits are undoable inside the panel (whole-sequence
 * snapshots); the file is written by Save.
 *
 * The panel registers its actions (sequence.*) and IPC methods
 * (sequence.*) from its own constructor.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-timeline-view.h"
#include "../../animation/lrg-sequence.h"
#include "../../animation/lrg-sequence-player.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_SEQUENCER_PANEL_ID:
 *
 * The dock panel id of the sequencer ("sequencer").
 *
 * Since: 0.9.0
 */
#define LRG_EDITOR_SEQUENCER_PANEL_ID "sequencer"

#define LRG_TYPE_EDITOR_SEQUENCER_PANEL (lrg_editor_sequencer_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorSequencerPanel, lrg_editor_sequencer_panel, LRG, EDITOR_SEQUENCER_PANEL, LrgContainer)

/**
 * lrg_editor_sequencer_panel_new:
 * @app: the editor application
 *
 * Creates the sequencer panel (the content of the "sequencer" dock
 * panel) with an empty, unsaved sequence. Registers the panel's actions
 * and its sequence.* IPC methods on @app, and remembers itself on @app
 * for lrg_editor_sequencer_panel_find().
 *
 * Since: 0.9.0
 *
 * Returns: (transfer full): a new #LrgEditorSequencerPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSequencerPanel * lrg_editor_sequencer_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_sequencer_panel_find:
 * @app: the editor application
 *
 * Since: 0.9.0
 *
 * Returns: (transfer none) (nullable): the sequencer panel created for
 *   @app, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSequencerPanel * lrg_editor_sequencer_panel_find (LrgEditorApp *app);

/**
 * lrg_editor_sequencer_panel_open_file:
 * @app: the editor application
 * @path: a .rseq file, absolute or relative to the project
 * @error: return location for an error
 *
 * Opens @path in @app's sequencer panel and brings the panel to the
 * front: what double-clicking a .rseq asset does. An unsaved sequence
 * already in the panel is replaced (its edits are lost; the caller asks
 * first when it matters, see lrg_editor_sequencer_panel_get_dirty()).
 *
 * Since: 0.9.0
 *
 * Returns: %FALSE (with @error set) when @app has no sequencer panel or
 *   the file does not load
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sequencer_panel_open_file (LrgEditorApp  *app,
                                               const gchar   *path,
                                               GError       **error);

/**
 * lrg_editor_sequencer_panel_convert_timeline:
 * @app: the editor application
 * @timeline: the name of a timeline of the current level
 * @path: the .rseq file to write, absolute or relative to the project
 *   (or to the level's folder without a project); folders are created
 * @fps: frame rate of the new sequence (<= 0 for 30)
 * @error: return location for an error
 *
 * Converts a level timeline with lrg_sequence_new_from_timeline() (one
 * transform track per node, bindings named after the nodes), saves it
 * to @path and opens it in the sequencer panel when there is one.
 *
 * Since: 0.9.0
 *
 * Returns: (transfer full) (nullable): the absolute path written, or
 *   %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_sequencer_panel_convert_timeline (LrgEditorApp  *app,
                                                     const gchar   *timeline,
                                                     const gchar   *path,
                                                     gfloat         fps,
                                                     GError       **error);

/**
 * lrg_editor_sequencer_panel_load:
 * @self: an #LrgEditorSequencerPanel
 * @path: a .rseq file, absolute or relative to the project
 * @error: return location for an error
 *
 * Replaces the panel's sequence with the file's (clearing the undo
 * history and ending any preview).
 *
 * Since: 0.9.0
 *
 * Returns: whether the file loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sequencer_panel_load (LrgEditorSequencerPanel  *self,
                                          const gchar              *path,
                                          GError                  **error);

/**
 * lrg_editor_sequencer_panel_create:
 * @self: an #LrgEditorSequencerPanel
 * @path: (nullable): where the new sequence will be saved, absolute or
 *   relative to the project; %NULL for an untitled sequence
 * @error: return location for an error
 *
 * Starts a new empty sequence (5 s, 30 fps, once) and, with @path,
 * writes it there at once. An existing file is an error
 * (%G_IO_ERROR_EXISTS).
 *
 * Since: 0.9.0
 *
 * Returns: whether the sequence was created
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sequencer_panel_create (LrgEditorSequencerPanel  *self,
                                            const gchar              *path,
                                            GError                  **error);

/**
 * lrg_editor_sequencer_panel_save:
 * @self: an #LrgEditorSequencerPanel
 * @path: (nullable): save as this file (absolute or relative to the
 *   project), %NULL for the current path
 * @error: return location for an error
 *
 * Writes the sequence with lrg_sequence_save_to_file() and clears the
 * dirty flag. An untitled sequence without @path is an error.
 *
 * Since: 0.9.0
 *
 * Returns: whether the file was written
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sequencer_panel_save (LrgEditorSequencerPanel  *self,
                                          const gchar              *path,
                                          GError                  **error);

/**
 * lrg_editor_sequencer_panel_get_sequence:
 * @self: an #LrgEditorSequencerPanel
 *
 * The sequence being edited. Treat it as read-only: change it through
 * lrg_editor_sequencer_panel_commit() so the edit is undoable and the
 * view follows.
 *
 * Since: 0.9.0
 *
 * Returns: (transfer none): the edited sequence
 */
LRG_AVAILABLE_IN_ALL
LrgSequence * lrg_editor_sequencer_panel_get_sequence (LrgEditorSequencerPanel *self);

/**
 * lrg_editor_sequencer_panel_commit:
 * @self: an #LrgEditorSequencerPanel
 * @edited: (transfer none): an edited copy of the sequence (from
 *   lrg_sequence_copy() of lrg_editor_sequencer_panel_get_sequence())
 * @label: (nullable): the undo label
 *
 * Makes @edited the panel's sequence as one undo step. Nothing is
 * recorded when it equals the current sequence (lrg_sequence_equal()).
 *
 * Since: 0.9.0
 *
 * Returns: whether anything changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sequencer_panel_commit (LrgEditorSequencerPanel *self,
                                            LrgSequence             *edited,
                                            const gchar             *label);

/**
 * lrg_editor_sequencer_panel_undo:
 * @self: an #LrgEditorSequencerPanel
 *
 * Since: 0.9.0
 *
 * Returns: whether an edit was undone
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sequencer_panel_undo (LrgEditorSequencerPanel *self);

/**
 * lrg_editor_sequencer_panel_redo:
 * @self: an #LrgEditorSequencerPanel
 *
 * Since: 0.9.0
 *
 * Returns: whether an edit was redone
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sequencer_panel_redo (LrgEditorSequencerPanel *self);

/**
 * lrg_editor_sequencer_panel_get_path:
 * @self: an #LrgEditorSequencerPanel
 *
 * Since: 0.9.0
 *
 * Returns: (nullable): the absolute path of the sequence file, %NULL
 *   while untitled
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_sequencer_panel_get_path (LrgEditorSequencerPanel *self);

/**
 * lrg_editor_sequencer_panel_get_dirty:
 * @self: an #LrgEditorSequencerPanel
 *
 * Since: 0.9.0
 *
 * Returns: whether the sequence differs from the file
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sequencer_panel_get_dirty (LrgEditorSequencerPanel *self);

/**
 * lrg_editor_sequencer_panel_get_view:
 * @self: an #LrgEditorSequencerPanel
 *
 * Since: 0.9.0
 *
 * Returns: (transfer none): the panel's timeline view
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineView * lrg_editor_sequencer_panel_get_view (LrgEditorSequencerPanel *self);

/**
 * lrg_editor_sequencer_panel_get_player:
 * @self: an #LrgEditorSequencerPanel
 *
 * The world-less player that drives the preview: its time is the
 * playhead, its signals report crossed events.
 *
 * Since: 0.9.0
 *
 * Returns: (transfer none): the preview player
 */
LRG_AVAILABLE_IN_ALL
LrgSequencePlayer * lrg_editor_sequencer_panel_get_player (LrgEditorSequencerPanel *self);

/**
 * lrg_editor_sequencer_panel_find_track:
 * @self: an #LrgEditorSequencerPanel
 * @track: a sequence track index
 * @channel: (nullable): a transform channel name ("location.x") of a
 *   transform track, %NULL for the track's own row
 *
 * For tests and tools: the view row showing a track or channel.
 *
 * Since: 0.9.0
 *
 * Returns: (transfer none) (nullable): the view track
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineViewTrack * lrg_editor_sequencer_panel_find_track (LrgEditorSequencerPanel *self,
                                                              guint                    track,
                                                              const gchar             *channel);

/**
 * lrg_editor_sequencer_panel_find_keyframe:
 * @self: an #LrgEditorSequencerPanel
 * @track: a sequence track index
 * @channel: (nullable): a transform channel name, %NULL for the curve of
 *   a property track or the events of an event track
 * @time: the key or event time (matched within 1 ms)
 *
 * Since: 0.9.0
 *
 * Returns: the view keyframe id, or %LRG_TIMELINE_VIEW_INVALID_ID
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_sequencer_panel_find_keyframe (LrgEditorSequencerPanel *self,
                                                guint                    track,
                                                const gchar             *channel,
                                                gfloat                   time);

/**
 * lrg_editor_sequencer_panel_find_clip:
 * @self: an #LrgEditorSequencerPanel
 * @track: a sequence track index
 * @section: a section index of that track
 *
 * Since: 0.9.0
 *
 * Returns: the view clip id of a section, or %LRG_TIMELINE_VIEW_INVALID_ID
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_sequencer_panel_find_clip (LrgEditorSequencerPanel *self,
                                            guint                    track,
                                            guint                    section);

/**
 * lrg_editor_sequencer_panel_select_curve:
 * @self: an #LrgEditorSequencerPanel
 * @track: a sequence track index, or -1 for none
 * @channel: (nullable): a transform channel name of a transform track
 *   (%NULL: the track's first keyed channel, or a property track's curve)
 *
 * Shows a curve in the Bézier curve editor under the tracks.
 *
 * Since: 0.9.0
 *
 * Returns: whether a curve is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sequencer_panel_select_curve (LrgEditorSequencerPanel *self,
                                                  gint                     track,
                                                  const gchar             *channel);

/**
 * lrg_editor_sequencer_panel_get_curve_editor:
 * @self: an #LrgEditorSequencerPanel
 *
 * The Bézier curve editor (a #LrgCustomWidget named "sequencer-curve"):
 * keys are dots, their tangent handles are squares one third of the
 * neighbouring segment away (lrg_sequence_curve_get_slopes()). Dragging
 * a key moves it in time and value; dragging a handle sets that slope
 * (an auto or linear key becomes a user key, Shift breaks the handles);
 * double click adds a key on the curve; Delete removes the selected
 * key. Each drag is one undo step.
 *
 * Since: 0.9.0
 *
 * Returns: (transfer none): the curve editor widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_editor_sequencer_panel_get_curve_editor (LrgEditorSequencerPanel *self);

/**
 * lrg_editor_sequencer_panel_get_curve_point:
 * @self: an #LrgEditorSequencerPanel
 * @key: a key index of the shown curve
 * @handle: 0 for the key itself, -1 for its in handle, 1 for its out
 *   handle
 * @x: (out) (optional): canvas X
 * @y: (out) (optional): canvas Y
 *
 * For tests and tutorials: where the curve editor draws a key or one of
 * its handles.
 *
 * Since: 0.9.0
 *
 * Returns: whether the point exists (a handle needs a neighbouring key
 *   on its side)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sequencer_panel_get_curve_point (LrgEditorSequencerPanel *self,
                                                     guint                    key,
                                                     gint                     handle,
                                                     gfloat                  *x,
                                                     gfloat                  *y);

/**
 * lrg_editor_sequencer_panel_get_time:
 * @self: an #LrgEditorSequencerPanel
 *
 * Since: 0.9.0
 *
 * Returns: the playhead in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_sequencer_panel_get_time (LrgEditorSequencerPanel *self);

/**
 * lrg_editor_sequencer_panel_scrub:
 * @self: an #LrgEditorSequencerPanel
 * @time: the new playhead in seconds (clamped to the sequence; snapped
 *   to frames when #LrgEditorSequencerPanel:snap is on)
 *
 * Moves the playhead like dragging it: events between the old and the
 * new time are reported (log and the player's #LrgSequencePlayer::event),
 * sounds stay silent, and the level's bound nodes are posed in the
 * viewport (outside the undo history; restored by
 * lrg_editor_sequencer_panel_stop(), saving the level or play mode).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sequencer_panel_scrub (LrgEditorSequencerPanel *self,
                                       gfloat                   time);

/**
 * lrg_editor_sequencer_panel_get_playing:
 * @self: an #LrgEditorSequencerPanel
 *
 * Since: 0.9.0
 *
 * Returns: whether the preview is playing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sequencer_panel_get_playing (LrgEditorSequencerPanel *self);

/**
 * lrg_editor_sequencer_panel_set_playing:
 * @self: an #LrgEditorSequencerPanel
 * @playing: %TRUE to play the preview from the playhead, %FALSE to pause
 *
 * Plays or pauses the preview in the panel's
 * #LrgEditorSequencerPanel:preview-mode. A once preview at the end
 * starts over.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sequencer_panel_set_playing (LrgEditorSequencerPanel *self,
                                             gboolean                 playing);

/**
 * lrg_editor_sequencer_panel_stop:
 * @self: an #LrgEditorSequencerPanel
 *
 * Stops the preview, rewinds to 0 and puts every posed node (and the
 * piloted viewport camera) back.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sequencer_panel_stop (LrgEditorSequencerPanel *self);

/**
 * lrg_editor_sequencer_panel_get_preview_mode:
 * @self: an #LrgEditorSequencerPanel
 *
 * Since: 0.9.0
 *
 * Returns: how the preview plays
 */
LRG_AVAILABLE_IN_ALL
LrgSequencePlayMode lrg_editor_sequencer_panel_get_preview_mode (LrgEditorSequencerPanel *self);

/**
 * lrg_editor_sequencer_panel_set_preview_mode:
 * @self: an #LrgEditorSequencerPanel
 * @mode: once, loop or ping-pong
 *
 * Sets how the preview plays (not saved; the sequence's own play mode is
 * edited with sequence.set or the mode picker's "Save as default").
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sequencer_panel_set_preview_mode (LrgEditorSequencerPanel *self,
                                                  LrgSequencePlayMode      mode);

/**
 * lrg_editor_sequencer_panel_get_snap:
 * @self: an #LrgEditorSequencerPanel
 *
 * Since: 0.9.0
 *
 * Returns: whether times snap to the sequence's frames
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sequencer_panel_get_snap (LrgEditorSequencerPanel *self);

/**
 * lrg_editor_sequencer_panel_set_snap:
 * @self: an #LrgEditorSequencerPanel
 * @snap: %TRUE to snap keys, sections and the playhead to frames
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sequencer_panel_set_snap (LrgEditorSequencerPanel *self,
                                          gboolean                 snap);

/**
 * lrg_editor_sequencer_panel_get_pilot:
 * @self: an #LrgEditorSequencerPanel
 *
 * Since: 0.9.0
 *
 * Returns: whether the preview flies the viewport through camera cuts
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sequencer_panel_get_pilot (LrgEditorSequencerPanel *self);

/**
 * lrg_editor_sequencer_panel_set_pilot:
 * @self: an #LrgEditorSequencerPanel
 * @pilot: %TRUE to look through the cut camera while previewing
 *
 * With pilot on, the active viewport looks through the camera node of
 * the camera cut at the playhead; turning it off (or ending the
 * preview) gives the viewport its own camera back.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sequencer_panel_set_pilot (LrgEditorSequencerPanel *self,
                                           gboolean                 pilot);

/**
 * lrg_editor_sequencer_panel_get_cut_camera:
 * @self: an #LrgEditorSequencerPanel
 *
 * Since: 0.9.0
 *
 * Returns: (nullable): the binding of the camera cut at the playhead
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_sequencer_panel_get_cut_camera (LrgEditorSequencerPanel *self);

G_END_DECLS
