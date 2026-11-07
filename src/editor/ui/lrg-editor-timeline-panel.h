/* lrg-editor-timeline-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorTimelinePanel: the level's animation timelines on an
 * #LrgTimelineView.
 *
 * A toolbar picks, creates, renames and deletes the level's
 * #LrgTimeline s and sets loop, autoplay and length; a transport row
 * plays, stops, steps between keys, keys the selection and sets the
 * easing of new and selected keys. The view shows one group per node
 * with one track per animated channel (location / rotation / scale per
 * axis, colour, roughness, metallic, param.NAME), keys shaped by their
 * easing. Dragging keys retimes them (snapped to 10 ms, one undo step
 * per drag), Delete removes them, the ruler scrubs. Every edit goes
 * through the undoable lrg_editor_timeline_* API, and scrubbing and
 * playback go through the editor's non-destructive preview (the view
 * state's timeline properties), so the panel, IPC (timeline.*) and the
 * actions always agree.
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
#include "lrg-editor-app.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_TIMELINE_PANEL (lrg_editor_timeline_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorTimelinePanel, lrg_editor_timeline_panel, LRG, EDITOR_TIMELINE_PANEL, LrgContainer)

/**
 * LRG_EDITOR_TIMELINE_PANEL_INVALID_KEY:
 *
 * Returned by lrg_editor_timeline_panel_find_keyframe() when there is
 * no such key.
 */
#define LRG_EDITOR_TIMELINE_PANEL_INVALID_KEY (G_MAXUINT)

/**
 * lrg_editor_timeline_panel_new:
 * @app: the editor application it presents
 *
 * Creates the timeline panel. It registers the "timeline.rename" and
 * "timeline.delete" actions (and gives "timeline.delete-key" the
 * view's multi-key selection).
 *
 * Returns: (transfer full): a new #LrgEditorTimelinePanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTimelinePanel * lrg_editor_timeline_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_timeline_panel_get_view:
 * @self: an #LrgEditorTimelinePanel
 *
 * Returns: (transfer none): the timeline view
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineView * lrg_editor_timeline_panel_get_view (LrgEditorTimelinePanel *self);

/**
 * lrg_editor_timeline_panel_get_look:
 * @self: an #LrgEditorTimelinePanel
 *
 * Returns: the view's visual preset
 */
LRG_AVAILABLE_IN_ALL
LrgCanvasLook lrg_editor_timeline_panel_get_look (LrgEditorTimelinePanel *self);

/**
 * lrg_editor_timeline_panel_set_look:
 * @self: an #LrgEditorTimelinePanel
 * @look: a visual preset; %LRG_CANVAS_LOOK_AUTO (the default) follows
 *   the editor theme
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_timeline_panel_set_look (LrgEditorTimelinePanel *self,
                                         LrgCanvasLook           look);

/**
 * lrg_editor_timeline_panel_refresh:
 * @self: an #LrgEditorTimelinePanel
 *
 * Rebuilds the view from the current timeline now (the panel does this
 * by itself on the next frame after any change).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_timeline_panel_refresh (LrgEditorTimelinePanel *self);

/**
 * lrg_editor_timeline_panel_find_track:
 * @self: an #LrgEditorTimelinePanel
 * @node_guid: a node GUID
 * @channel: (nullable): a channel, or %NULL for the node's group track
 *
 * Returns: (transfer none) (nullable): the view track showing @channel
 *   of @node_guid, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineViewTrack * lrg_editor_timeline_panel_find_track (LrgEditorTimelinePanel *self,
                                                             const gchar            *node_guid,
                                                             const gchar            *channel);

/**
 * lrg_editor_timeline_panel_find_keyframe:
 * @self: an #LrgEditorTimelinePanel
 * @node_guid: a node GUID
 * @channel: a channel
 * @time: a key time in seconds (matched within 1 ms)
 *
 * Returns: the view keyframe id of that key, or
 *   %LRG_EDITOR_TIMELINE_PANEL_INVALID_KEY
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_timeline_panel_find_keyframe (LrgEditorTimelinePanel *self,
                                               const gchar            *node_guid,
                                               const gchar            *channel,
                                               gfloat                  time);

/**
 * lrg_editor_timeline_panel_set_key_easing:
 * @self: an #LrgEditorTimelinePanel
 * @easing: an easing
 *
 * Sets the easing used for new keys and re-eases every selected key
 * (one undo step), as the easing picker and the key context menu do.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_timeline_panel_set_key_easing (LrgEditorTimelinePanel *self,
                                               LrgEasingType           easing);

/**
 * lrg_editor_timeline_panel_set_key_tangents:
 * @self: an #LrgEditorTimelinePanel
 * @bezier: %TRUE to make the selected keys Bézier keys, %FALSE to make
 *   them eased keys again
 * @mode: the tangent mode when @bezier
 *
 * What the tangent picker and the key context menu's Key Tangents do:
 * every selected key (a node summary key stands for all channel keys of
 * its node at that time) becomes a Bézier key of @mode, as one undo step.
 * The keys keep the slopes they are drawn with, so switching an auto key
 * to user or broken does not change its shape until a handle is edited.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_timeline_panel_set_key_tangents (LrgEditorTimelinePanel *self,
                                                 gboolean                bezier,
                                                 LrgSequenceTangentMode  mode);

G_END_DECLS
