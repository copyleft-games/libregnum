/* lrg-timeline-view.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTimelineView: a multi-track sequencer view for cutscenes, animation
 * and audio. Tracks (LrgTimelineViewTrack) hold clips, keyframes with an
 * optional value-curve lane, and the timeline holds markers, a
 * playhead with built-in playback, zoom, scrolling and editing.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Enumerations
 * ========================================================================== */

/**
 * LrgTimelineViewInterpolation:
 * @LRG_TIMELINE_VIEW_INTERPOLATION_STEP: hold the value until the next keyframe
 * @LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR: straight line to the next keyframe
 * @LRG_TIMELINE_VIEW_INTERPOLATION_EASE: smooth start and stop (smoothstep)
 * @LRG_TIMELINE_VIEW_INTERPOLATION_BEZIER: cubic Hermite through the keyframe
 *   tangents (lrg_timeline_view_set_keyframe_tangents())
 *
 * How the value travels from a keyframe to the next one. The
 * interpolation belongs to the segment that starts at the keyframe.
 */
typedef enum
{
	LRG_TIMELINE_VIEW_INTERPOLATION_STEP,
	LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR,
	LRG_TIMELINE_VIEW_INTERPOLATION_EASE,
	LRG_TIMELINE_VIEW_INTERPOLATION_BEZIER
} LrgTimelineViewInterpolation;

LRG_AVAILABLE_IN_ALL
GType lrg_timeline_view_interpolation_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TIMELINE_VIEW_INTERPOLATION (lrg_timeline_view_interpolation_get_type ())

/**
 * LrgTimelineViewTimeFormat:
 * @LRG_TIMELINE_VIEW_TIME_FORMAT_SECONDS: "12s", "1.5s", "1:05"
 * @LRG_TIMELINE_VIEW_TIME_FORMAT_TIMECODE: "MM:SS:FF" (hours added when
 *   needed); frames use the #LrgTimelineView:fps property (30 when it is 0)
 * @LRG_TIMELINE_VIEW_TIME_FORMAT_FRAMES: frame numbers
 *
 * How ruler labels and lrg_timeline_view_format_time() print a time.
 */
typedef enum
{
	LRG_TIMELINE_VIEW_TIME_FORMAT_SECONDS,
	LRG_TIMELINE_VIEW_TIME_FORMAT_TIMECODE,
	LRG_TIMELINE_VIEW_TIME_FORMAT_FRAMES
} LrgTimelineViewTimeFormat;

LRG_AVAILABLE_IN_ALL
GType lrg_timeline_view_time_format_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TIMELINE_VIEW_TIME_FORMAT (lrg_timeline_view_time_format_get_type ())

/**
 * LrgTimelineViewSnap:
 * @LRG_TIMELINE_VIEW_SNAP_NONE: no snapping
 * @LRG_TIMELINE_VIEW_SNAP_PLAYHEAD: snap to the playhead
 * @LRG_TIMELINE_VIEW_SNAP_MARKERS: snap to markers
 * @LRG_TIMELINE_VIEW_SNAP_CLIPS: snap to the edges of other clips (and keyframes)
 * @LRG_TIMELINE_VIEW_SNAP_GRID: quantize to frames (with an fps) or to the
 *   ruler's minor ticks when no magnet is in reach
 * @LRG_TIMELINE_VIEW_SNAP_ALL: everything
 *
 * What dragged clips, clip edges, keyframes and the scrubbed playhead
 * are drawn to. Holding Alt during a drag disables snapping.
 */
typedef enum /*< flags >*/
{
	LRG_TIMELINE_VIEW_SNAP_NONE     = 0,
	LRG_TIMELINE_VIEW_SNAP_PLAYHEAD = 1 << 0,
	LRG_TIMELINE_VIEW_SNAP_MARKERS  = 1 << 1,
	LRG_TIMELINE_VIEW_SNAP_CLIPS    = 1 << 2,
	LRG_TIMELINE_VIEW_SNAP_GRID     = 1 << 3,
	LRG_TIMELINE_VIEW_SNAP_ALL      = 0x0F
} LrgTimelineViewSnap;

LRG_AVAILABLE_IN_ALL
GType lrg_timeline_view_snap_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TIMELINE_VIEW_SNAP (lrg_timeline_view_snap_get_type ())

/* ==========================================================================
 * LrgTimelineViewTrack
 * ========================================================================== */

#define LRG_TYPE_TIMELINE_VIEW_TRACK (lrg_timeline_view_track_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTimelineViewTrack, lrg_timeline_view_track, LRG, TIMELINE_VIEW_TRACK, GObject)

/**
 * lrg_timeline_view_track_new:
 * @name: (nullable): display name
 *
 * Creates a track that is not yet part of a timeline; add it with
 * lrg_timeline_view_append_track(). lrg_timeline_view_add_track() does both.
 *
 * Returns: (transfer full): a new #LrgTimelineViewTrack
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineViewTrack * lrg_timeline_view_track_new (const gchar *name);

/**
 * lrg_timeline_view_track_get_name:
 * @self: an #LrgTimelineViewTrack
 *
 * Returns: (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_timeline_view_track_get_name (LrgTimelineViewTrack *self);

/**
 * lrg_timeline_view_track_set_name:
 * @self: an #LrgTimelineViewTrack
 * @name: (nullable): display name
 *
 * Sets the name shown in the track header.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_track_set_name (LrgTimelineViewTrack *self,
                                  const gchar      *name);

/**
 * lrg_timeline_view_track_get_color:
 * @self: an #LrgTimelineViewTrack
 * @color: (out caller-allocates): the colour of the track's clips
 *
 * Gets the track colour: the one set with lrg_timeline_view_track_set_color(),
 * or the palette colour picked from the track's position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_track_get_color (LrgTimelineViewTrack *self,
                                   GrlColor         *color);

/**
 * lrg_timeline_view_track_set_color:
 * @self: an #LrgTimelineViewTrack
 * @color: (nullable): colour of the header stripe, clips and curve, or
 *   %NULL for the built-in palette
 *
 * Sets the track colour.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_track_set_color (LrgTimelineViewTrack *self,
                                   const GrlColor   *color);

/**
 * lrg_timeline_view_track_get_height:
 * @self: an #LrgTimelineViewTrack
 *
 * Returns: the clip lane height, 0 for the timeline default
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_timeline_view_track_get_height (LrgTimelineViewTrack *self);

/**
 * lrg_timeline_view_track_set_height:
 * @self: an #LrgTimelineViewTrack
 * @height: clip lane height in canvas units, 0 for the default
 *   (#LrgTimelineView:track-height)
 *
 * Sets the height of the track's clip lane (the curve lane is extra).
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_track_set_height (LrgTimelineViewTrack *self,
                                    gfloat            height);

/**
 * lrg_timeline_view_track_get_collapsed:
 * @self: an #LrgTimelineViewTrack
 *
 * Returns: %TRUE if the track is collapsed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_track_get_collapsed (LrgTimelineViewTrack *self);

/**
 * lrg_timeline_view_track_set_collapsed:
 * @self: an #LrgTimelineViewTrack
 * @collapsed: whether to collapse the track
 *
 * A collapsed track shrinks to a thin row (no curve lane) and hides
 * its child tracks.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_track_set_collapsed (LrgTimelineViewTrack *self,
                                       gboolean          collapsed);

/**
 * lrg_timeline_view_track_get_muted:
 * @self: an #LrgTimelineViewTrack
 *
 * Returns: %TRUE if the track is muted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_track_get_muted (LrgTimelineViewTrack *self);

/**
 * lrg_timeline_view_track_set_muted:
 * @self: an #LrgTimelineViewTrack
 * @muted: whether the track is muted
 *
 * Marks the track muted. The timeline only dims it; the application
 * decides what muting means (silence audio, skip animation, ...).
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_track_set_muted (LrgTimelineViewTrack *self,
                                   gboolean          muted);

/**
 * lrg_timeline_view_track_get_locked:
 * @self: an #LrgTimelineViewTrack
 *
 * Returns: %TRUE if the track is locked
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_track_get_locked (LrgTimelineViewTrack *self);

/**
 * lrg_timeline_view_track_set_locked:
 * @self: an #LrgTimelineViewTrack
 * @locked: whether the track is locked
 *
 * Locked tracks cannot be edited with the pointer: their clips and
 * keyframes can be selected but not dragged, resized, added or deleted.
 * The API still edits them.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_track_set_locked (LrgTimelineViewTrack *self,
                                    gboolean          locked);

/**
 * lrg_timeline_view_track_get_allow_overlap:
 * @self: an #LrgTimelineViewTrack
 *
 * Returns: %TRUE if clips on the track may overlap
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_track_get_allow_overlap (LrgTimelineViewTrack *self);

/**
 * lrg_timeline_view_track_set_allow_overlap:
 * @self: an #LrgTimelineViewTrack
 * @allow: whether clips may overlap
 *
 * By default clips on one track cannot overlap: adding, moving or
 * resizing a clip into another one fails (or clamps, for resizing).
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_track_set_allow_overlap (LrgTimelineViewTrack *self,
                                           gboolean          allow);

/**
 * lrg_timeline_view_track_get_curve_lane:
 * @self: an #LrgTimelineViewTrack
 *
 * Returns: %TRUE if the value curve lane is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_track_get_curve_lane (LrgTimelineViewTrack *self);

/**
 * lrg_timeline_view_track_set_curve_lane:
 * @self: an #LrgTimelineViewTrack
 * @show: whether to show the value curve lane under the clip lane
 *
 * The curve lane plots the keyframe values between
 * #LrgTimelineViewTrack:value-min and #LrgTimelineViewTrack:value-max. Keyframes
 * in it can be dragged in time and value; a double click adds one.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_track_set_curve_lane (LrgTimelineViewTrack *self,
                                        gboolean          show);

/**
 * lrg_timeline_view_track_get_curve_height:
 * @self: an #LrgTimelineViewTrack
 *
 * Returns: height of the curve lane
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_timeline_view_track_get_curve_height (LrgTimelineViewTrack *self);

/**
 * lrg_timeline_view_track_set_curve_height:
 * @self: an #LrgTimelineViewTrack
 * @height: height of the curve lane in canvas units
 *
 * Sets the curve lane height.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_track_set_curve_height (LrgTimelineViewTrack *self,
                                          gfloat            height);

/**
 * lrg_timeline_view_track_get_value_range:
 * @self: an #LrgTimelineViewTrack
 * @min: (out) (optional): value at the bottom of the curve lane
 * @max: (out) (optional): value at the top of the curve lane
 *
 * Gets the value range plotted by the curve lane.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_track_get_value_range (LrgTimelineViewTrack *self,
                                         gdouble          *min,
                                         gdouble          *max);

/**
 * lrg_timeline_view_track_set_value_range:
 * @self: an #LrgTimelineViewTrack
 * @min: value at the bottom of the curve lane
 * @max: value at the top of the curve lane (greater than @min)
 *
 * Sets the plotted value range. Values dragged in the curve lane are
 * clamped to it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_track_set_value_range (LrgTimelineViewTrack *self,
                                         gdouble           min,
                                         gdouble           max);

/**
 * lrg_timeline_view_track_get_default_value:
 * @self: an #LrgTimelineViewTrack
 *
 * Returns: the value lrg_timeline_view_evaluate() gives without keyframes
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timeline_view_track_get_default_value (LrgTimelineViewTrack *self);

/**
 * lrg_timeline_view_track_set_default_value:
 * @self: an #LrgTimelineViewTrack
 * @value: value of a track without keyframes
 *
 * Sets the value of the track when it has no keyframes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_track_set_default_value (LrgTimelineViewTrack *self,
                                           gdouble           value);

/**
 * lrg_timeline_view_track_get_parent:
 * @self: an #LrgTimelineViewTrack
 *
 * Returns: (transfer none) (nullable): the group track @self is nested
 *   in, or %NULL at the top level
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineViewTrack * lrg_timeline_view_track_get_parent (LrgTimelineViewTrack *self);

/* ==========================================================================
 * LrgTimelineView
 * ========================================================================== */

#define LRG_TYPE_TIMELINE_VIEW (lrg_timeline_view_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTimelineView, lrg_timeline_view, LRG, TIMELINE_VIEW, LrgContainer)

/**
 * LRG_TIMELINE_VIEW_INVALID_ID:
 *
 * The id that no clip, keyframe or marker has (0): returned when an
 * add fails.
 */
#define LRG_TIMELINE_VIEW_INVALID_ID (0u)

/**
 * lrg_timeline_view_new:
 *
 * Creates an empty timeline: 10 s long, 100 canvas units per second,
 * no fps (continuous time), all snapping on.
 *
 * Returns: (transfer full): a new #LrgTimelineView
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineView * lrg_timeline_view_new (void);

/* --- Tracks ------------------------------------------------------------ */

/**
 * lrg_timeline_view_add_track:
 * @self: an #LrgTimelineView
 * @name: (nullable): display name
 * @parent: (nullable): group track to nest the new track in
 *
 * Creates a track and appends it (after the last child of @parent, or
 * at the bottom).
 *
 * Returns: (transfer none): the new track, owned by @self
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineViewTrack * lrg_timeline_view_add_track (LrgTimelineView      *self,
                                           const gchar      *name,
                                           LrgTimelineViewTrack *parent);

/**
 * lrg_timeline_view_append_track:
 * @self: an #LrgTimelineView
 * @track: a track that is not in a timeline yet
 * @parent: (nullable): group track to nest @track in
 *
 * Appends an existing track (the timeline takes a reference).
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_append_track (LrgTimelineView      *self,
                                LrgTimelineViewTrack *track,
                                LrgTimelineViewTrack *parent);

/**
 * lrg_timeline_view_remove_track:
 * @self: an #LrgTimelineView
 * @track: a track of @self
 *
 * Removes @track, its nested tracks, and all their clips and keyframes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_remove_track (LrgTimelineView      *self,
                                LrgTimelineViewTrack *track);

/**
 * lrg_timeline_view_get_n_tracks:
 * @self: an #LrgTimelineView
 *
 * Returns: number of tracks, nested ones included
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_view_get_n_tracks (LrgTimelineView *self);

/**
 * lrg_timeline_view_get_track:
 * @self: an #LrgTimelineView
 * @index: track index in display order (groups before their children)
 *
 * Returns: (transfer none) (nullable): the track, or %NULL past the end
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineViewTrack * lrg_timeline_view_get_track (LrgTimelineView *self,
                                           guint        index);

/**
 * lrg_timeline_view_find_track:
 * @self: an #LrgTimelineView
 * @name: a track name
 *
 * Returns: (transfer none) (nullable): the first track called @name
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineViewTrack * lrg_timeline_view_find_track (LrgTimelineView *self,
                                            const gchar *name);

/**
 * lrg_timeline_view_get_track_row:
 * @self: an #LrgTimelineView
 * @track: a track of @self
 * @y: (out) (optional): top of the row, world space (scroll applied)
 * @height: (out) (optional): row height (clip lane plus curve lane)
 *
 * Where the row of @track is laid out. Lays the timeline out first if
 * needed.
 *
 * Returns: %TRUE if the track has a row (it is not inside a collapsed
 *   group)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_get_track_row (LrgTimelineView      *self,
                                     LrgTimelineViewTrack *track,
                                     gfloat           *y,
                                     gfloat           *height);

/* --- Clips ------------------------------------------------------------- */

/**
 * lrg_timeline_view_add_clip:
 * @self: an #LrgTimelineView
 * @track: track to put the clip on
 * @start: start time in seconds (>= 0)
 * @duration: length in seconds (> 0)
 * @label: (nullable): text drawn on the clip
 *
 * Adds a clip. Fails when it would overlap another clip on a track
 * that does not allow overlaps. Emits #LrgTimelineView::clip-added.
 *
 * Returns: the clip id, or %LRG_TIMELINE_VIEW_INVALID_ID
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_view_add_clip (LrgTimelineView      *self,
                             LrgTimelineViewTrack *track,
                             gdouble           start,
                             gdouble           duration,
                             const gchar      *label);

/**
 * lrg_timeline_view_remove_clip:
 * @self: an #LrgTimelineView
 * @clip: a clip id
 *
 * Removes a clip. Emits #LrgTimelineView::clip-removed.
 *
 * Returns: %TRUE if the clip existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_remove_clip (LrgTimelineView *self,
                                   guint        clip);

/**
 * lrg_timeline_view_move_clip:
 * @self: an #LrgTimelineView
 * @clip: a clip id
 * @track: (nullable): new track, %NULL to keep the current one
 * @start: new start time
 *
 * Moves a clip. Fails (and changes nothing) when the clip would
 * overlap another one on a track that does not allow it. Emits
 * #LrgTimelineView::clip-moved.
 *
 * Returns: %TRUE if the clip moved
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_move_clip (LrgTimelineView      *self,
                                 guint             clip,
                                 LrgTimelineViewTrack *track,
                                 gdouble           start);

/**
 * lrg_timeline_view_resize_clip:
 * @self: an #LrgTimelineView
 * @clip: a clip id
 * @start: new start time
 * @duration: new length
 *
 * Sets both ends of a clip. The duration is kept at least the minimum
 * clip length (one frame, or 10 ms without an fps), the start at least
 * 0, and on tracks without overlaps the ends are clamped against the
 * neighbouring clips. Emits #LrgTimelineView::clip-resized.
 *
 * Returns: %TRUE if the clip exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_resize_clip (LrgTimelineView *self,
                                   guint        clip,
                                   gdouble      start,
                                   gdouble      duration);

/**
 * lrg_timeline_view_get_clip_start:
 * @self: an #LrgTimelineView
 * @clip: a clip id
 *
 * Returns: start time, or -1 for an unknown id
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timeline_view_get_clip_start (LrgTimelineView *self,
                                     guint        clip);

/**
 * lrg_timeline_view_get_clip_duration:
 * @self: an #LrgTimelineView
 * @clip: a clip id
 *
 * Returns: length in seconds, or 0 for an unknown id
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timeline_view_get_clip_duration (LrgTimelineView *self,
                                        guint        clip);

/**
 * lrg_timeline_view_get_clip_track:
 * @self: an #LrgTimelineView
 * @clip: a clip id
 *
 * Returns: (transfer none) (nullable): the track holding the clip
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineViewTrack * lrg_timeline_view_get_clip_track (LrgTimelineView *self,
                                                guint        clip);

/**
 * lrg_timeline_view_get_clip_label:
 * @self: an #LrgTimelineView
 * @clip: a clip id
 *
 * Returns: (nullable): the clip label
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_timeline_view_get_clip_label (LrgTimelineView *self,
                                           guint        clip);

/**
 * lrg_timeline_view_set_clip_label:
 * @self: an #LrgTimelineView
 * @clip: a clip id
 * @label: (nullable): text drawn on the clip
 *
 * Sets the clip label.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_clip_label (LrgTimelineView *self,
                                  guint        clip,
                                  const gchar *label);

/**
 * lrg_timeline_view_set_clip_color:
 * @self: an #LrgTimelineView
 * @clip: a clip id
 * @color: (nullable): clip colour, %NULL for the track colour
 *
 * Sets the colour of one clip.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_clip_color (LrgTimelineView    *self,
                                  guint           clip,
                                  const GrlColor *color);

/**
 * lrg_timeline_view_get_n_clips:
 * @self: an #LrgTimelineView
 * @track: (nullable): a track, or %NULL for all tracks
 *
 * Returns: number of clips
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_view_get_n_clips (LrgTimelineView      *self,
                                LrgTimelineViewTrack *track);

/**
 * lrg_timeline_view_get_clip_at:
 * @self: an #LrgTimelineView
 * @track: a track
 * @time: a time in seconds
 *
 * Returns: the (last added) clip on @track covering @time, or
 *   %LRG_TIMELINE_VIEW_INVALID_ID
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_view_get_clip_at (LrgTimelineView      *self,
                                LrgTimelineViewTrack *track,
                                gdouble           time);

/* --- Keyframes --------------------------------------------------------- */

/**
 * lrg_timeline_view_add_keyframe:
 * @self: an #LrgTimelineView
 * @track: track to put the keyframe on
 * @time: time in seconds
 * @value: value at @time
 * @interpolation: how the value continues to the next keyframe
 *
 * Adds a keyframe. Emits #LrgTimelineView::keyframe-added.
 *
 * Returns: the keyframe id
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_view_add_keyframe (LrgTimelineView              *self,
                                 LrgTimelineViewTrack         *track,
                                 gdouble                   time,
                                 gdouble                   value,
                                 LrgTimelineViewInterpolation  interpolation);

/**
 * lrg_timeline_view_remove_keyframe:
 * @self: an #LrgTimelineView
 * @keyframe: a keyframe id
 *
 * Removes a keyframe. Emits #LrgTimelineView::keyframe-removed.
 *
 * Returns: %TRUE if the keyframe existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_remove_keyframe (LrgTimelineView *self,
                                       guint        keyframe);

/**
 * lrg_timeline_view_move_keyframe:
 * @self: an #LrgTimelineView
 * @keyframe: a keyframe id
 * @time: new time (clamped to >= 0)
 * @value: new value
 *
 * Moves a keyframe in time and value. Emits #LrgTimelineView::keyframe-moved.
 *
 * Returns: %TRUE if the keyframe exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_move_keyframe (LrgTimelineView *self,
                                     guint        keyframe,
                                     gdouble      time,
                                     gdouble      value);

/**
 * lrg_timeline_view_get_keyframe_time:
 * @self: an #LrgTimelineView
 * @keyframe: a keyframe id
 *
 * Returns: the keyframe time, or -1 for an unknown id
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timeline_view_get_keyframe_time (LrgTimelineView *self,
                                        guint        keyframe);

/**
 * lrg_timeline_view_get_keyframe_value:
 * @self: an #LrgTimelineView
 * @keyframe: a keyframe id
 *
 * Returns: the keyframe value, or 0 for an unknown id
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timeline_view_get_keyframe_value (LrgTimelineView *self,
                                         guint        keyframe);

/**
 * lrg_timeline_view_get_keyframe_track:
 * @self: an #LrgTimelineView
 * @keyframe: a keyframe id
 *
 * Returns: (transfer none) (nullable): the track holding the keyframe
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineViewTrack * lrg_timeline_view_get_keyframe_track (LrgTimelineView *self,
                                                    guint        keyframe);

/**
 * lrg_timeline_view_get_keyframe_interpolation:
 * @self: an #LrgTimelineView
 * @keyframe: a keyframe id
 *
 * Returns: the interpolation of the segment starting at the keyframe
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineViewInterpolation lrg_timeline_view_get_keyframe_interpolation (LrgTimelineView *self,
                                                                  guint        keyframe);

/**
 * lrg_timeline_view_set_keyframe_interpolation:
 * @self: an #LrgTimelineView
 * @keyframe: a keyframe id
 * @interpolation: the new interpolation
 *
 * Sets how the value travels from this keyframe to the next.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_keyframe_interpolation (LrgTimelineView              *self,
                                              guint                     keyframe,
                                              LrgTimelineViewInterpolation  interpolation);

/**
 * lrg_timeline_view_set_keyframe_tangents:
 * @self: an #LrgTimelineView
 * @keyframe: a keyframe id
 * @in_slope: slope arriving at the keyframe (value per second)
 * @out_slope: slope leaving the keyframe (value per second)
 *
 * Sets the tangents used by %LRG_TIMELINE_VIEW_INTERPOLATION_BEZIER
 * segments (both 0 by default: flat, like an ease).
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_keyframe_tangents (LrgTimelineView *self,
                                         guint        keyframe,
                                         gdouble      in_slope,
                                         gdouble      out_slope);

/**
 * lrg_timeline_view_get_n_keyframes:
 * @self: an #LrgTimelineView
 * @track: (nullable): a track, or %NULL for all tracks
 *
 * Returns: number of keyframes
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_view_get_n_keyframes (LrgTimelineView      *self,
                                    LrgTimelineViewTrack *track);

/**
 * lrg_timeline_view_get_keyframe:
 * @self: an #LrgTimelineView
 * @track: a track
 * @index: keyframe index on @track, in time order
 *
 * Returns: the keyframe id, or %LRG_TIMELINE_VIEW_INVALID_ID past the end
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_view_get_keyframe (LrgTimelineView      *self,
                                 LrgTimelineViewTrack *track,
                                 guint             index);

/**
 * lrg_timeline_view_evaluate:
 * @self: an #LrgTimelineView
 * @track: a track
 * @time: time in seconds
 *
 * Samples the keyframe curve of @track. Before the first keyframe the
 * value is the first keyframe's, after the last the last one's; a track
 * without keyframes gives its #LrgTimelineViewTrack:default-value.
 *
 * Returns: the value at @time
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timeline_view_evaluate (LrgTimelineView      *self,
                               LrgTimelineViewTrack *track,
                               gdouble           time);

/* --- Markers ----------------------------------------------------------- */

/**
 * lrg_timeline_view_add_marker:
 * @self: an #LrgTimelineView
 * @time: time in seconds
 * @label: (nullable): flag text
 *
 * Adds a marker: a flag on the ruler and a line across the tracks.
 * Clicking the flag activates the marker (#LrgTimelineView::marker-activated);
 * dragging it moves the marker (#LrgTimelineView::marker-moved at release).
 *
 * Returns: the marker id
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_view_add_marker (LrgTimelineView *self,
                               gdouble      time,
                               const gchar *label);

/**
 * lrg_timeline_view_remove_marker:
 * @self: an #LrgTimelineView
 * @marker: a marker id
 *
 * Returns: %TRUE if the marker existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_remove_marker (LrgTimelineView *self,
                                     guint        marker);

/**
 * lrg_timeline_view_move_marker:
 * @self: an #LrgTimelineView
 * @marker: a marker id
 * @time: new time
 *
 * Moves a marker (clamped at 0). Does not emit
 * #LrgTimelineView::marker-moved, so an undo stack can call it to
 * revert a drag.
 *
 * Returns: %TRUE if the marker exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_move_marker (LrgTimelineView *self,
                                   guint        marker,
                                   gdouble      time);

/**
 * lrg_timeline_view_get_marker_time:
 * @self: an #LrgTimelineView
 * @marker: a marker id
 *
 * Returns: the marker time, or -1 for an unknown id
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timeline_view_get_marker_time (LrgTimelineView *self,
                                      guint        marker);

/**
 * lrg_timeline_view_get_marker_label:
 * @self: an #LrgTimelineView
 * @marker: a marker id
 *
 * Returns: (nullable): the marker label
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_timeline_view_get_marker_label (LrgTimelineView *self,
                                             guint        marker);

/**
 * lrg_timeline_view_get_n_markers:
 * @self: an #LrgTimelineView
 *
 * Returns: number of markers
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_view_get_n_markers (LrgTimelineView *self);

/* --- Selection and editing --------------------------------------------- */

/**
 * lrg_timeline_view_select_clip:
 * @self: an #LrgTimelineView
 * @clip: a clip id
 * @extend: keep the current selection (%FALSE selects only @clip)
 *
 * Selects a clip. Emits #LrgTimelineView::selection-changed if anything
 * changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_select_clip (LrgTimelineView *self,
                               guint        clip,
                               gboolean     extend);

/**
 * lrg_timeline_view_select_keyframe:
 * @self: an #LrgTimelineView
 * @keyframe: a keyframe id
 * @extend: keep the current selection
 *
 * Selects a keyframe.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_select_keyframe (LrgTimelineView *self,
                                   guint        keyframe,
                                   gboolean     extend);

/**
 * lrg_timeline_view_unselect_all:
 * @self: an #LrgTimelineView
 *
 * Clears the selection of clips and keyframes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_unselect_all (LrgTimelineView *self);

/**
 * lrg_timeline_view_is_clip_selected:
 * @self: an #LrgTimelineView
 * @clip: a clip id
 *
 * Returns: %TRUE if the clip is selected
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_is_clip_selected (LrgTimelineView *self,
                                        guint        clip);

/**
 * lrg_timeline_view_is_keyframe_selected:
 * @self: an #LrgTimelineView
 * @keyframe: a keyframe id
 *
 * Returns: %TRUE if the keyframe is selected
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_is_keyframe_selected (LrgTimelineView *self,
                                            guint        keyframe);

/**
 * lrg_timeline_view_get_selected_clips:
 * @self: an #LrgTimelineView
 *
 * Returns: (transfer full) (element-type guint): ids of the selected
 *   clips, in ascending order
 */
LRG_AVAILABLE_IN_ALL
GArray * lrg_timeline_view_get_selected_clips (LrgTimelineView *self);

/**
 * lrg_timeline_view_get_selected_keyframes:
 * @self: an #LrgTimelineView
 *
 * Returns: (transfer full) (element-type guint): ids of the selected
 *   keyframes, in ascending order
 */
LRG_AVAILABLE_IN_ALL
GArray * lrg_timeline_view_get_selected_keyframes (LrgTimelineView *self);

/**
 * lrg_timeline_view_delete_selection:
 * @self: an #LrgTimelineView
 *
 * Removes the selected clips and keyframes that are not on locked
 * tracks (what Delete does).
 *
 * Returns: number of removed items
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_view_delete_selection (LrgTimelineView *self);

/**
 * lrg_timeline_view_copy_selection:
 * @self: an #LrgTimelineView
 *
 * Copies the selected clips to the timeline's own clipboard (Ctrl+C).
 *
 * Returns: number of copied clips
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_view_copy_selection (LrgTimelineView *self);

/**
 * lrg_timeline_view_paste:
 * @self: an #LrgTimelineView
 * @time: where the earliest copied clip starts
 *
 * Pastes the copied clips onto their original tracks, keeping their
 * relative offsets (Ctrl+V pastes at the playhead). Clips that would
 * overlap are skipped. The pasted clips become the selection.
 *
 * Returns: number of pasted clips
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timeline_view_paste (LrgTimelineView *self,
                          gdouble      time);

/* --- Playhead and playback --------------------------------------------- */

/**
 * lrg_timeline_view_get_time:
 * @self: an #LrgTimelineView
 *
 * Returns: the playhead time in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timeline_view_get_time (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_time:
 * @self: an #LrgTimelineView
 * @time: playhead time (clamped to [0, duration])
 *
 * Moves the playhead. With #LrgTimelineView:follow-playhead the view
 * scrolls to keep it visible. Emits #LrgTimelineView::playhead-moved.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_time (LrgTimelineView *self,
                            gdouble      time);

/**
 * lrg_timeline_view_get_duration:
 * @self: an #LrgTimelineView
 *
 * Returns: the sequence length in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timeline_view_get_duration (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_duration:
 * @self: an #LrgTimelineView
 * @duration: the sequence length in seconds (> 0)
 *
 * Sets the length of the sequence: the playhead range and the
 * scrollable extent (which also grows to fit the last clip).
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_duration (LrgTimelineView *self,
                                gdouble      duration);

/**
 * lrg_timeline_view_get_playing:
 * @self: an #LrgTimelineView
 *
 * Returns: %TRUE while the built-in playback runs
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_get_playing (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_playing:
 * @self: an #LrgTimelineView
 * @playing: start or stop playback
 *
 * Starts or stops the built-in playback: while playing, every canvas
 * frame advances the playhead by the frame time times
 * #LrgTimelineView:rate. Apps driving the playhead from their own clock
 * leave this off and call lrg_timeline_view_set_time().
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_playing (LrgTimelineView *self,
                               gboolean     playing);

/**
 * lrg_timeline_view_get_rate:
 * @self: an #LrgTimelineView
 *
 * Returns: the playback speed multiplier
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timeline_view_get_rate (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_rate:
 * @self: an #LrgTimelineView
 * @rate: playback speed (1 is real time, negative plays backwards)
 *
 * Sets the playback rate.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_rate (LrgTimelineView *self,
                            gdouble      rate);

/**
 * lrg_timeline_view_get_loop:
 * @self: an #LrgTimelineView
 *
 * Returns: %TRUE if playback loops
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_get_loop (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_loop:
 * @self: an #LrgTimelineView
 * @loop: whether playback wraps around the loop range
 *
 * Without a loop, playback stops at the end (or start) of the sequence.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_loop (LrgTimelineView *self,
                            gboolean     loop);

/**
 * lrg_timeline_view_get_loop_range:
 * @self: an #LrgTimelineView
 * @start: (out) (optional): loop start
 * @end: (out) (optional): loop end
 *
 * Gets the loop range. An empty range (end <= start) loops the whole
 * sequence.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_get_loop_range (LrgTimelineView *self,
                                  gdouble     *start,
                                  gdouble     *end);

/**
 * lrg_timeline_view_set_loop_range:
 * @self: an #LrgTimelineView
 * @start: loop start
 * @end: loop end; <= @start loops the whole sequence
 *
 * Sets the range playback wraps around when #LrgTimelineView:loop is set.
 * The range is tinted on the ruler.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_loop_range (LrgTimelineView *self,
                                  gdouble      start,
                                  gdouble      end);

/* --- View ------------------------------------------------------------- */

/**
 * lrg_timeline_view_get_zoom:
 * @self: an #LrgTimelineView
 *
 * Returns: the target zoom in canvas units per second
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timeline_view_get_zoom (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_zoom:
 * @self: an #LrgTimelineView
 * @zoom: canvas units per second (clamped to 0.5 .. 50000)
 *
 * Sets the zoom at once, keeping the time at the left edge of the
 * view in place.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_zoom (LrgTimelineView *self,
                            gdouble      zoom);

/**
 * lrg_timeline_view_zoom_to:
 * @self: an #LrgTimelineView
 * @zoom: canvas units per second
 * @anchor_x: world x that keeps showing the same time
 * @animate: glide there on the zoom spring
 *
 * Zooms about a point (what Ctrl + wheel and pinch do).
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_zoom_to (LrgTimelineView *self,
                           gdouble      zoom,
                           gfloat       anchor_x,
                           gboolean     animate);

/**
 * lrg_timeline_view_get_scroll_time:
 * @self: an #LrgTimelineView
 *
 * Returns: the (displayed) time at the left edge of the track area
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timeline_view_get_scroll_time (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_scroll_time:
 * @self: an #LrgTimelineView
 * @time: time to show at the left edge of the track area
 * @animate: glide there
 *
 * Scrolls horizontally (clamped to the scrollable extent).
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_scroll_time (LrgTimelineView *self,
                                   gdouble      time,
                                   gboolean     animate);

/**
 * lrg_timeline_view_get_scroll_y:
 * @self: an #LrgTimelineView
 *
 * Returns: the (displayed) vertical scroll offset of the tracks
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_timeline_view_get_scroll_y (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_scroll_y:
 * @self: an #LrgTimelineView
 * @offset: vertical offset of the tracks
 * @animate: glide there
 *
 * Scrolls the tracks vertically (clamped).
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_scroll_y (LrgTimelineView *self,
                                gfloat       offset,
                                gboolean     animate);

/**
 * lrg_timeline_view_time_to_x:
 * @self: an #LrgTimelineView
 * @time: a time in seconds
 *
 * Maps a time to a world x with the displayed zoom and scroll.
 *
 * Returns: the world x
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_timeline_view_time_to_x (LrgTimelineView *self,
                               gdouble      time);

/**
 * lrg_timeline_view_x_to_time:
 * @self: an #LrgTimelineView
 * @x: a world x
 *
 * Inverse of lrg_timeline_view_time_to_x().
 *
 * Returns: the time in seconds (may be negative left of time 0)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timeline_view_x_to_time (LrgTimelineView *self,
                                gfloat       x);

/**
 * lrg_timeline_view_get_visible_range:
 * @self: an #LrgTimelineView
 * @start: (out) (optional): time at the left edge of the track area
 * @end: (out) (optional): time at the right edge
 *
 * Gets the range of time currently on screen.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_get_visible_range (LrgTimelineView *self,
                                     gdouble     *start,
                                     gdouble     *end);

/**
 * lrg_timeline_view_get_tick_step:
 * @self: an #LrgTimelineView
 * @major: (out) (optional): seconds between labelled ticks
 * @minor: (out) (optional): subdivisions of a major step (1 = none)
 *
 * The ruler's tick spacing for the displayed zoom: the smallest
 * "nice" step (frames, then 1-2-5 seconds, then minutes) that keeps
 * labels about 80 canvas units apart. Minor ticks are dropped when
 * they would be closer than 5 units.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_get_tick_step (LrgTimelineView *self,
                                 gdouble     *major,
                                 guint       *minor);

/**
 * lrg_timeline_view_format_time:
 * @self: an #LrgTimelineView
 * @time: a time in seconds
 *
 * Prints @time in the #LrgTimelineView:time-format, with as many decimals
 * as the current ruler step needs.
 *
 * Returns: (transfer full): the text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_timeline_view_format_time (LrgTimelineView *self,
                                  gdouble      time);

/* --- Settings ---------------------------------------------------------- */

/**
 * lrg_timeline_view_get_fps:
 * @self: an #LrgTimelineView
 *
 * Returns: frames per second, 0 for continuous time
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timeline_view_get_fps (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_fps:
 * @self: an #LrgTimelineView
 * @fps: frames per second, 0 for continuous time
 *
 * With an fps the ruler counts frames when zoomed in, grid snapping
 * lands on frames and Left / Right step one frame.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_fps (LrgTimelineView *self,
                           gdouble      fps);

/**
 * lrg_timeline_view_get_time_format:
 * @self: an #LrgTimelineView
 *
 * Returns: how times are printed
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineViewTimeFormat lrg_timeline_view_get_time_format (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_time_format:
 * @self: an #LrgTimelineView
 * @format: how ruler labels print times
 *
 * Sets the time format.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_time_format (LrgTimelineView           *self,
                                   LrgTimelineViewTimeFormat  format);

/**
 * lrg_timeline_view_get_snap:
 * @self: an #LrgTimelineView
 *
 * Returns: the snapping targets
 */
LRG_AVAILABLE_IN_ALL
LrgTimelineViewSnap lrg_timeline_view_get_snap (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_snap:
 * @self: an #LrgTimelineView
 * @snap: what drags snap to
 *
 * Sets the snapping targets.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_snap (LrgTimelineView     *self,
                            LrgTimelineViewSnap  snap);

/**
 * lrg_timeline_view_get_snap_distance:
 * @self: an #LrgTimelineView
 *
 * Returns: magnet reach in canvas units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_timeline_view_get_snap_distance (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_snap_distance:
 * @self: an #LrgTimelineView
 * @distance: how close (in canvas units) a drag must come to a target
 *   to snap to it
 *
 * Sets the magnet reach.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_snap_distance (LrgTimelineView *self,
                                     gfloat       distance);

/**
 * lrg_timeline_view_get_follow_playhead:
 * @self: an #LrgTimelineView
 *
 * Returns: %TRUE if the view follows the playhead
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_get_follow_playhead (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_follow_playhead:
 * @self: an #LrgTimelineView
 * @follow: scroll to keep the playhead visible while it moves (not
 *   while it is scrubbed)
 *
 * Sets whether the view follows the playhead.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_follow_playhead (LrgTimelineView *self,
                                       gboolean     follow);

/**
 * lrg_timeline_view_get_header_width:
 * @self: an #LrgTimelineView
 *
 * Returns: width of the track header column
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_timeline_view_get_header_width (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_header_width:
 * @self: an #LrgTimelineView
 * @width: width of the track header column (names and toggles)
 *
 * Sets the header column width.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_header_width (LrgTimelineView *self,
                                    gfloat       width);

/**
 * lrg_timeline_view_get_ruler_height:
 * @self: an #LrgTimelineView
 *
 * Returns: height of the time ruler
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_timeline_view_get_ruler_height (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_ruler_height:
 * @self: an #LrgTimelineView
 * @height: height of the time ruler
 *
 * Sets the ruler height.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_ruler_height (LrgTimelineView *self,
                                    gfloat       height);

/**
 * lrg_timeline_view_get_track_height:
 * @self: an #LrgTimelineView
 *
 * Returns: default clip lane height (0 derives it from the theme)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_timeline_view_get_track_height (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_track_height:
 * @self: an #LrgTimelineView
 * @height: clip lane height of tracks without their own, 0 for
 *   1.4 x the theme's ROW_HEIGHT
 *
 * Sets the default track height.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_track_height (LrgTimelineView *self,
                                    gfloat       height);

/**
 * lrg_timeline_view_get_show_play_button:
 * @self: an #LrgTimelineView
 *
 * Returns: %TRUE if the play / pause button is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timeline_view_get_show_play_button (LrgTimelineView *self);

/**
 * lrg_timeline_view_set_show_play_button:
 * @self: an #LrgTimelineView
 * @show: show a play / pause button in the corner above the headers
 *
 * Sets whether the built-in play / pause button is shown.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timeline_view_set_show_play_button (LrgTimelineView *self,
                                        gboolean     show);

G_END_DECLS
