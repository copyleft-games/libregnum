/* lrg-timeline-view.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTimelineView: a multi-track sequencer for cutscenes, animation and
 * audio.
 *
 * Model. Tracks are LrgTimelineViewTrack objects kept in display order
 * (a group is followed by its nested tracks). Each track owns its
 * clips and keyframes as plain structs in arrays sorted by time, so
 * the part of a track that is on screen is found with a binary search:
 * clips by start (any clip touching [t0, t1] starts in
 * [t0 - longest clip, t1]), keyframes by time. Every clip, keyframe
 * and marker has a unique id; the timeline maps ids to structs.
 *
 * View. The widget is split into a header column (track names and
 * mute / lock toggles), a time ruler on top, and the track area. Time
 * maps to x as  x = track_area_x + time * zoom - scroll_x  where zoom
 * (canvas units per second) is a spring and scroll_x is a spring-driven
 * scroll axis shared with the other scrolling widgets
 * (lrg-scrollbar-private.h), as is the vertical axis of the tracks.
 * Zooming about the pointer pins the time under the pointer while the
 * zoom spring moves.
 *
 * Editing. Clips are dragged (snapping to the playhead, markers, other
 * clip edges and the grid, with a visible guide), resized from their
 * edges, keyframes dragged on the clip lane (time) or curve lane (time
 * and value), marker flags dragged along the ruler (a click without a
 * drag activates the marker instead); a drag in empty space selects
 * with a rubber band. The
 * pressed widget captures the pointer, so every drag is a small state
 * machine run from MOUSE_MOVE / MOUSE_BUTTON_UP.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>
#include <yaml-glib.h>

#include "lrg-timeline-view.h"
#include "lrg-scrollbar-private.h"
#include "../paint/lrg-paint-icons.h"
#include "../paint/lrg-paint-fx.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Constants
 * ========================================================================== */

#define DEFAULT_DURATION   (10.0)
#define DEFAULT_ZOOM       (100.0)
#define MIN_ZOOM           (0.5)
#define MAX_ZOOM           (50000.0)
#define DEFAULT_HEADER_W   (180.0f)
#define DEFAULT_RULER_H    (30.0f)
#define DEFAULT_CURVE_H    (72.0f)
#define DEFAULT_SNAP_PX    (8.0f)

/* Major ruler ticks are about this far apart; minor ones at least MINOR_GAP */
#define TICK_SPACING       (80.0)
#define MINOR_GAP          (5.0)

/* Width of the clip edge zone that resizes instead of moving */
#define EDGE_ZONE          (6.0f)

/* Clips narrower than this (canvas units) merge into runs when painted */
#define TINY_CLIP          (4.0f)

/* Half size of a keyframe diamond; radius of a curve point */
#define KEY_HALF           (5.5f)
#define CURVE_POINT        (4.5f)

/* Nesting indent in the header column */
#define INDENT             (14.0f)

/* Extra scroll room past the end of the sequence, canvas units */
#define TAIL               (64.0f)

/* Pointer travel before a press on a clip / keyframe starts a drag */
#define DRAG_THRESHOLD     (3.0f)

/* Two presses within this time (canvas clock) and distance double click */
#define DOUBLE_CLICK_TIME  (0.4)
#define DOUBLE_CLICK_DIST  (5.0f)

/* Zoom factor per wheel notch */
#define WHEEL_ZOOM         (1.2)

/* Size of the header toggles and the collapse chevron */
#define TOGGLE_SIZE        (16.0f)

/* Vertical padding inside the curve lane */
#define CURVE_PAD          (8.0f)

/* ==========================================================================
 * Enum types
 * ========================================================================== */

GType
lrg_timeline_view_interpolation_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TIMELINE_VIEW_INTERPOLATION_STEP, "LRG_TIMELINE_VIEW_INTERPOLATION_STEP", "step" },
			{ LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR, "LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR", "linear" },
			{ LRG_TIMELINE_VIEW_INTERPOLATION_EASE, "LRG_TIMELINE_VIEW_INTERPOLATION_EASE", "ease" },
			{ LRG_TIMELINE_VIEW_INTERPOLATION_BEZIER, "LRG_TIMELINE_VIEW_INTERPOLATION_BEZIER", "bezier" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgTimelineViewInterpolation"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_timeline_view_time_format_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TIMELINE_VIEW_TIME_FORMAT_SECONDS, "LRG_TIMELINE_VIEW_TIME_FORMAT_SECONDS", "seconds" },
			{ LRG_TIMELINE_VIEW_TIME_FORMAT_TIMECODE, "LRG_TIMELINE_VIEW_TIME_FORMAT_TIMECODE", "timecode" },
			{ LRG_TIMELINE_VIEW_TIME_FORMAT_FRAMES, "LRG_TIMELINE_VIEW_TIME_FORMAT_FRAMES", "frames" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgTimelineViewTimeFormat"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_timeline_view_snap_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_TIMELINE_VIEW_SNAP_NONE, "LRG_TIMELINE_VIEW_SNAP_NONE", "none" },
			{ LRG_TIMELINE_VIEW_SNAP_PLAYHEAD, "LRG_TIMELINE_VIEW_SNAP_PLAYHEAD", "playhead" },
			{ LRG_TIMELINE_VIEW_SNAP_MARKERS, "LRG_TIMELINE_VIEW_SNAP_MARKERS", "markers" },
			{ LRG_TIMELINE_VIEW_SNAP_CLIPS, "LRG_TIMELINE_VIEW_SNAP_CLIPS", "clips" },
			{ LRG_TIMELINE_VIEW_SNAP_GRID, "LRG_TIMELINE_VIEW_SNAP_GRID", "grid" },
			{ LRG_TIMELINE_VIEW_SNAP_ALL, "LRG_TIMELINE_VIEW_SNAP_ALL", "all" },
			{ 0, NULL, NULL }
		};
		GType id = g_flags_register_static (g_intern_static_string ("LrgTimelineViewSnap"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Model structs
 * ========================================================================== */

typedef struct
{
	guint             id;
	LrgTimelineViewTrack *track;     /* not a reference: the track owns the clip */
	gdouble           start;
	gdouble           duration;
	gchar            *label;
	GrlColor          color;
	gboolean          has_color;
	gboolean          moving;    /* part of the current drag (not a snap target) */
} Clip;

typedef struct
{
	guint                     id;
	LrgTimelineViewTrack         *track;
	gdouble                   time;
	gdouble                   value;
	gdouble                   in_slope;
	gdouble                   out_slope;
	LrgTimelineViewInterpolation  interp;
	LrgCanvasKeyShape         shape;     /* AUTO: the timeline's */
	gboolean                  moving;
} Keyframe;

typedef struct
{
	guint    id;
	gdouble  time;
	gchar   *label;
	gboolean moving;   /* being dragged (not a snap target) */
} Marker;

/* A copied clip on the timeline's clipboard */
typedef struct
{
	LrgTimelineViewTrack *track;     /* weak: checked against the track list on paste */
	gdouble           offset;    /* start relative to the earliest copied clip */
	gdouble           duration;
	gchar            *label;
	GrlColor          color;
	gboolean          has_color;
} Copied;

static void
clip_free (gpointer data)
{
	Clip *c = data;

	g_free (c->label);
	g_free (c);
}

static void
keyframe_free (gpointer data)
{
	g_free (data);
}

static void
marker_free (gpointer data)
{
	Marker *m = data;

	g_free (m->label);
	g_free (m);
}

static void
copied_clear (gpointer data)
{
	Copied *c = data;

	g_clear_pointer (&c->label, g_free);
}

/* ==========================================================================
 * LrgTimelineViewTrack
 * ========================================================================== */

struct _LrgTimelineViewTrack
{
	GObject           parent_instance;

	LrgTimelineView      *timeline;      /* weak back pointer while added */
	LrgTimelineViewTrack *parent;        /* weak: the group, kept by the timeline */
	gchar            *name;
	GrlColor          color;
	gboolean          has_color;
	guint             palette;       /* palette slot for the default colour */
	gfloat            height;
	gfloat            curve_height;
	gboolean          collapsed;
	gboolean          muted;
	gboolean          locked;
	gboolean          allow_overlap;
	gboolean          curve_lane;
	gdouble           value_min;
	gdouble           value_max;
	gdouble           default_value;

	GPtrArray        *clips;         /* Clip *, sorted by start, owned */
	GPtrArray        *keys;          /* Keyframe *, sorted by time, owned */
	gdouble           max_duration;  /* longest clip ever on the track (cull bound) */
};

G_DEFINE_FINAL_TYPE (LrgTimelineViewTrack, lrg_timeline_view_track, G_TYPE_OBJECT)

enum
{
	TRACK_PROP_0,
	TRACK_PROP_NAME,
	TRACK_PROP_COLOR,
	TRACK_PROP_HEIGHT,
	TRACK_PROP_COLLAPSED,
	TRACK_PROP_MUTED,
	TRACK_PROP_LOCKED,
	TRACK_PROP_ALLOW_OVERLAP,
	TRACK_PROP_CURVE_LANE,
	TRACK_PROP_CURVE_HEIGHT,
	TRACK_PROP_VALUE_MIN,
	TRACK_PROP_VALUE_MAX,
	TRACK_PROP_DEFAULT_VALUE,
	TRACK_PROP_PARENT,
	TRACK_N_PROPS
};

static GParamSpec *track_props[TRACK_N_PROPS];

/* Built-in clip palette: saturated mid tones that read on light and
 * dark themes alike (label colour is picked by contrast) */
static const GrlColor track_palette[] = {
	{ 0x5B, 0x8D, 0xEF, 0xFF },
	{ 0xE0, 0x6C, 0x75, 0xFF },
	{ 0x7F, 0xC0, 0x6A, 0xFF },
	{ 0xE5, 0xB5, 0x5B, 0xFF },
	{ 0xB7, 0x7B, 0xE0, 0xFF },
	{ 0x4F, 0xB8, 0xC4, 0xFF },
	{ 0xF0, 0x8D, 0x49, 0xFF },
	{ 0x8A, 0x9B, 0xB4, 0xFF }
};

/* Forward: a property of @track changed; relayout / repaint the timeline */
static void track_changed (LrgTimelineViewTrack *track);

static void
lrg_timeline_view_track_finalize (GObject *object)
{
	LrgTimelineViewTrack *self = LRG_TIMELINE_VIEW_TRACK (object);

	g_clear_pointer (&self->clips, g_ptr_array_unref);
	g_clear_pointer (&self->keys, g_ptr_array_unref);
	g_free (self->name);

	G_OBJECT_CLASS (lrg_timeline_view_track_parent_class)->finalize (object);
}

static void
lrg_timeline_view_track_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	LrgTimelineViewTrack *self = LRG_TIMELINE_VIEW_TRACK (object);
	GrlColor          c;

	switch (prop_id)
	{
	case TRACK_PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case TRACK_PROP_COLOR:
		lrg_timeline_view_track_get_color (self, &c);
		g_value_set_boxed (value, &c);
		break;
	case TRACK_PROP_HEIGHT:
		g_value_set_float (value, self->height);
		break;
	case TRACK_PROP_COLLAPSED:
		g_value_set_boolean (value, self->collapsed);
		break;
	case TRACK_PROP_MUTED:
		g_value_set_boolean (value, self->muted);
		break;
	case TRACK_PROP_LOCKED:
		g_value_set_boolean (value, self->locked);
		break;
	case TRACK_PROP_ALLOW_OVERLAP:
		g_value_set_boolean (value, self->allow_overlap);
		break;
	case TRACK_PROP_CURVE_LANE:
		g_value_set_boolean (value, self->curve_lane);
		break;
	case TRACK_PROP_CURVE_HEIGHT:
		g_value_set_float (value, self->curve_height);
		break;
	case TRACK_PROP_VALUE_MIN:
		g_value_set_double (value, self->value_min);
		break;
	case TRACK_PROP_VALUE_MAX:
		g_value_set_double (value, self->value_max);
		break;
	case TRACK_PROP_DEFAULT_VALUE:
		g_value_set_double (value, self->default_value);
		break;
	case TRACK_PROP_PARENT:
		g_value_set_object (value, self->parent);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_timeline_view_track_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	LrgTimelineViewTrack *self = LRG_TIMELINE_VIEW_TRACK (object);

	switch (prop_id)
	{
	case TRACK_PROP_NAME:
		lrg_timeline_view_track_set_name (self, g_value_get_string (value));
		break;
	case TRACK_PROP_COLOR:
		lrg_timeline_view_track_set_color (self, g_value_get_boxed (value));
		break;
	case TRACK_PROP_HEIGHT:
		lrg_timeline_view_track_set_height (self, g_value_get_float (value));
		break;
	case TRACK_PROP_COLLAPSED:
		lrg_timeline_view_track_set_collapsed (self, g_value_get_boolean (value));
		break;
	case TRACK_PROP_MUTED:
		lrg_timeline_view_track_set_muted (self, g_value_get_boolean (value));
		break;
	case TRACK_PROP_LOCKED:
		lrg_timeline_view_track_set_locked (self, g_value_get_boolean (value));
		break;
	case TRACK_PROP_ALLOW_OVERLAP:
		lrg_timeline_view_track_set_allow_overlap (self, g_value_get_boolean (value));
		break;
	case TRACK_PROP_CURVE_LANE:
		lrg_timeline_view_track_set_curve_lane (self, g_value_get_boolean (value));
		break;
	case TRACK_PROP_CURVE_HEIGHT:
		lrg_timeline_view_track_set_curve_height (self, g_value_get_float (value));
		break;
	case TRACK_PROP_VALUE_MIN:
		lrg_timeline_view_track_set_value_range (self, g_value_get_double (value),
		                                    MAX (self->value_max, g_value_get_double (value) + 1e-6));
		break;
	case TRACK_PROP_VALUE_MAX:
		lrg_timeline_view_track_set_value_range (self, MIN (self->value_min, g_value_get_double (value) - 1e-6),
		                                    g_value_get_double (value));
		break;
	case TRACK_PROP_DEFAULT_VALUE:
		lrg_timeline_view_track_set_default_value (self, g_value_get_double (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_timeline_view_track_class_init (LrgTimelineViewTrackClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_timeline_view_track_finalize;
	object_class->get_property = lrg_timeline_view_track_get_property;
	object_class->set_property = lrg_timeline_view_track_set_property;

	/**
	 * LrgTimelineViewTrack:name:
	 *
	 * Name shown in the track header.
	 */
	track_props[TRACK_PROP_NAME] =
		g_param_spec_string ("name", "Name", "Name shown in the header", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineViewTrack:color:
	 *
	 * Colour of the header stripe, the clips and the value curve. Reads
	 * the palette colour until one is set; setting %NULL returns to it.
	 */
	track_props[TRACK_PROP_COLOR] =
		g_param_spec_boxed ("color", "Color", "Track colour", GRL_TYPE_COLOR,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineViewTrack:height:
	 *
	 * Height of the clip lane, 0 for #LrgTimelineView:track-height.
	 */
	track_props[TRACK_PROP_HEIGHT] =
		g_param_spec_float ("height", "Height", "Clip lane height (0 = default)",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineViewTrack:collapsed:
	 *
	 * Shrinks the track to a thin row and hides its nested tracks.
	 */
	track_props[TRACK_PROP_COLLAPSED] =
		g_param_spec_boolean ("collapsed", "Collapsed", "Shrink the track and hide nested tracks", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineViewTrack:muted:
	 *
	 * The track is muted (drawn dimmed; meaning is up to the app).
	 */
	track_props[TRACK_PROP_MUTED] =
		g_param_spec_boolean ("muted", "Muted", "The track is muted", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineViewTrack:locked:
	 *
	 * Pointer edits of the track's clips and keyframes are refused.
	 */
	track_props[TRACK_PROP_LOCKED] =
		g_param_spec_boolean ("locked", "Locked", "Refuse pointer edits", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineViewTrack:allow-overlap:
	 *
	 * Clips on the track may overlap.
	 */
	track_props[TRACK_PROP_ALLOW_OVERLAP] =
		g_param_spec_boolean ("allow-overlap", "Allow overlap", "Clips may overlap", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineViewTrack:curve-lane:
	 *
	 * Shows the value curve lane under the clip lane.
	 */
	track_props[TRACK_PROP_CURVE_LANE] =
		g_param_spec_boolean ("curve-lane", "Curve lane", "Show the keyframe value curve", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineViewTrack:curve-height:
	 *
	 * Height of the curve lane.
	 */
	track_props[TRACK_PROP_CURVE_HEIGHT] =
		g_param_spec_float ("curve-height", "Curve height", "Height of the curve lane",
		                    8.0f, G_MAXFLOAT, DEFAULT_CURVE_H,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineViewTrack:value-min:
	 *
	 * Value at the bottom of the curve lane.
	 */
	track_props[TRACK_PROP_VALUE_MIN] =
		g_param_spec_double ("value-min", "Value min", "Value at the bottom of the curve lane",
		                     -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineViewTrack:value-max:
	 *
	 * Value at the top of the curve lane.
	 */
	track_props[TRACK_PROP_VALUE_MAX] =
		g_param_spec_double ("value-max", "Value max", "Value at the top of the curve lane",
		                     -G_MAXDOUBLE, G_MAXDOUBLE, 1.0,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineViewTrack:default-value:
	 *
	 * Value of the track while it has no keyframes.
	 */
	track_props[TRACK_PROP_DEFAULT_VALUE] =
		g_param_spec_double ("default-value", "Default value", "Value without keyframes",
		                     -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineViewTrack:parent:
	 *
	 * The group track this track is nested in (set when it is added).
	 */
	track_props[TRACK_PROP_PARENT] =
		g_param_spec_object ("parent", "Parent", "Group track", LRG_TYPE_TIMELINE_VIEW_TRACK,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, TRACK_N_PROPS, track_props);
}

static void
lrg_timeline_view_track_init (LrgTimelineViewTrack *self)
{
	self->clips = g_ptr_array_new_with_free_func (clip_free);
	self->keys = g_ptr_array_new_with_free_func (keyframe_free);
	self->curve_height = DEFAULT_CURVE_H;
	self->value_min = 0.0;
	self->value_max = 1.0;
}

LrgTimelineViewTrack *
lrg_timeline_view_track_new (const gchar *name)
{
	return g_object_new (LRG_TYPE_TIMELINE_VIEW_TRACK, "name", name, NULL);
}

const gchar *
lrg_timeline_view_track_get_name (LrgTimelineViewTrack *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self), NULL);
	return self->name;
}

void
lrg_timeline_view_track_set_name (LrgTimelineViewTrack *self,
                             const gchar      *name)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self));

	if (g_strcmp0 (self->name, name) == 0)
		return;
	g_free (self->name);
	self->name = g_strdup (name);
	track_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), track_props[TRACK_PROP_NAME]);
}

void
lrg_timeline_view_track_get_color (LrgTimelineViewTrack *self,
                              GrlColor         *color)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self));
	g_return_if_fail (color != NULL);

	if (self->has_color)
		*color = self->color;
	else
		*color = track_palette[self->palette % G_N_ELEMENTS (track_palette)];
}

void
lrg_timeline_view_track_set_color (LrgTimelineViewTrack *self,
                              const GrlColor   *color)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self));

	if (color == NULL && !self->has_color)
		return;
	if (color != NULL && self->has_color && memcmp (color, &self->color, sizeof (GrlColor)) == 0)
		return;
	self->has_color = color != NULL;
	if (color != NULL)
		self->color = *color;
	track_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), track_props[TRACK_PROP_COLOR]);
}

gfloat
lrg_timeline_view_track_get_height (LrgTimelineViewTrack *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self), 0.0f);
	return self->height;
}

void
lrg_timeline_view_track_set_height (LrgTimelineViewTrack *self,
                               gfloat            height)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self));

	height = MAX (0.0f, height);
	if (self->height == height)
		return;
	self->height = height;
	track_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), track_props[TRACK_PROP_HEIGHT]);
}

gboolean
lrg_timeline_view_track_get_collapsed (LrgTimelineViewTrack *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self), FALSE);
	return self->collapsed;
}

void
lrg_timeline_view_track_set_collapsed (LrgTimelineViewTrack *self,
                                  gboolean          collapsed)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self));

	collapsed = !!collapsed;
	if (self->collapsed == collapsed)
		return;
	self->collapsed = collapsed;
	track_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), track_props[TRACK_PROP_COLLAPSED]);
}

gboolean
lrg_timeline_view_track_get_muted (LrgTimelineViewTrack *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self), FALSE);
	return self->muted;
}

void
lrg_timeline_view_track_set_muted (LrgTimelineViewTrack *self,
                              gboolean          muted)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self));

	muted = !!muted;
	if (self->muted == muted)
		return;
	self->muted = muted;
	track_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), track_props[TRACK_PROP_MUTED]);
}

gboolean
lrg_timeline_view_track_get_locked (LrgTimelineViewTrack *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self), FALSE);
	return self->locked;
}

void
lrg_timeline_view_track_set_locked (LrgTimelineViewTrack *self,
                               gboolean          locked)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self));

	locked = !!locked;
	if (self->locked == locked)
		return;
	self->locked = locked;
	track_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), track_props[TRACK_PROP_LOCKED]);
}

gboolean
lrg_timeline_view_track_get_allow_overlap (LrgTimelineViewTrack *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self), FALSE);
	return self->allow_overlap;
}

void
lrg_timeline_view_track_set_allow_overlap (LrgTimelineViewTrack *self,
                                      gboolean          allow)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self));

	allow = !!allow;
	if (self->allow_overlap == allow)
		return;
	self->allow_overlap = allow;
	g_object_notify_by_pspec (G_OBJECT (self), track_props[TRACK_PROP_ALLOW_OVERLAP]);
}

gboolean
lrg_timeline_view_track_get_curve_lane (LrgTimelineViewTrack *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self), FALSE);
	return self->curve_lane;
}

void
lrg_timeline_view_track_set_curve_lane (LrgTimelineViewTrack *self,
                                   gboolean          show)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self));

	show = !!show;
	if (self->curve_lane == show)
		return;
	self->curve_lane = show;
	track_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), track_props[TRACK_PROP_CURVE_LANE]);
}

gfloat
lrg_timeline_view_track_get_curve_height (LrgTimelineViewTrack *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self), DEFAULT_CURVE_H);
	return self->curve_height;
}

void
lrg_timeline_view_track_set_curve_height (LrgTimelineViewTrack *self,
                                     gfloat            height)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self));

	height = MAX (8.0f, height);
	if (self->curve_height == height)
		return;
	self->curve_height = height;
	track_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), track_props[TRACK_PROP_CURVE_HEIGHT]);
}

void
lrg_timeline_view_track_get_value_range (LrgTimelineViewTrack *self,
                                    gdouble          *min,
                                    gdouble          *max)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self));

	if (min != NULL)
		*min = self->value_min;
	if (max != NULL)
		*max = self->value_max;
}

void
lrg_timeline_view_track_set_value_range (LrgTimelineViewTrack *self,
                                    gdouble           min,
                                    gdouble           max)
{
	gboolean min_changed;
	gboolean max_changed;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self));
	g_return_if_fail (max > min);

	min_changed = self->value_min != min;
	max_changed = self->value_max != max;
	if (!min_changed && !max_changed)
		return;
	self->value_min = min;
	self->value_max = max;
	track_changed (self);
	if (min_changed)
		g_object_notify_by_pspec (G_OBJECT (self), track_props[TRACK_PROP_VALUE_MIN]);
	if (max_changed)
		g_object_notify_by_pspec (G_OBJECT (self), track_props[TRACK_PROP_VALUE_MAX]);
}

gdouble
lrg_timeline_view_track_get_default_value (LrgTimelineViewTrack *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self), 0.0);
	return self->default_value;
}

void
lrg_timeline_view_track_set_default_value (LrgTimelineViewTrack *self,
                                      gdouble           value)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self));

	if (self->default_value == value)
		return;
	self->default_value = value;
	track_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), track_props[TRACK_PROP_DEFAULT_VALUE]);
}

LrgTimelineViewTrack *
lrg_timeline_view_track_get_parent (LrgTimelineViewTrack *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (self), NULL);
	return self->parent;
}

/* ==========================================================================
 * LrgTimelineView: instance
 * ========================================================================== */

/* One laid-out track row; y is relative to the top of the track area
 * before vertical scrolling */
typedef struct
{
	LrgTimelineViewTrack *track;
	gfloat            y;
	gfloat            h;        /* whole row */
	gfloat            main_h;   /* clip lane */
	gfloat            curve_h;  /* curve lane (0 when hidden) */
	guint             depth;
	gboolean          group;    /* has nested tracks */
} Row;

typedef enum
{
	DRAG_NONE,
	DRAG_PENDING_CLIP,   /* pressed on a clip, below the drag threshold */
	DRAG_PENDING_KEY,    /* pressed on a keyframe, below the threshold */
	DRAG_PENDING_MARKER, /* pressed on a marker flag, below the threshold */
	DRAG_CLIPS,
	DRAG_RESIZE_LEFT,
	DRAG_RESIZE_RIGHT,
	DRAG_KEYS,
	DRAG_MARKER,
	DRAG_SCRUB,
	DRAG_BOX,
	DRAG_VBAR,
	DRAG_HBAR
} DragKind;

/* Saved state of something being dragged, for snapping and Esc */
typedef struct
{
	guint             id;
	LrgTimelineViewTrack *track;
	gdouble           start;     /* clip start / keyframe time */
	gdouble           length;    /* clip duration / keyframe value */
} Orig;

typedef enum
{
	HIT_NONE,
	HIT_VBAR,
	HIT_HBAR,
	HIT_PLAY,
	HIT_CORNER,
	HIT_HEADER,
	HIT_COLLAPSE,
	HIT_MUTE,
	HIT_LOCK,
	HIT_RULER,
	HIT_MARKER,
	HIT_CLIP,
	HIT_CLIP_LEFT,
	HIT_CLIP_RIGHT,
	HIT_KEY,
	HIT_CURVE_KEY,
	HIT_CURVE,
	HIT_LANE,
	HIT_EMPTY
} HitKind;

typedef struct
{
	HitKind  kind;
	gint     row;   /* row index, -1 for none */
	guint    id;    /* clip / keyframe / marker id */
} Hit;

/* Geometry of the widget for one frame, world space */
typedef struct
{
	gfloat wx;
	gfloat wy;
	gfloat w;
	gfloat h;
	gfloat cx;   /* track area left */
	gfloat cy;   /* track area top */
	gfloat vw;   /* track area width */
	gfloat vh;   /* track area height */
} Geom;

struct _LrgTimelineView
{
	LrgContainer           parent_instance;

	/* Model */
	GPtrArray             *tracks;       /* LrgTimelineViewTrack *, display order, refs */
	GHashTable            *clips;        /* id -> Clip * (owned by its track) */
	GHashTable            *keys;         /* id -> Keyframe * (owned by its track) */
	GPtrArray             *markers;      /* Marker *, sorted by time, owned */
	guint                  next_id;
	guint                  next_palette;
	GHashTable            *sel_clips;    /* set of ids */
	GHashTable            *sel_keys;     /* set of ids */
	GArray                *clipboard;    /* Copied */

	/* Layout */
	GArray                *rows;         /* Row */
	gboolean               rows_dirty;
	gfloat                 content_h;
	gboolean               in_layout;

	/* Settings */
	gdouble                time;
	gdouble                duration;
	gdouble                fps;
	gdouble                rate;
	gboolean               playing;
	gboolean               loop;
	gdouble                loop_start;
	gdouble                loop_end;
	gboolean               follow;
	gboolean               show_play;
	LrgTimelineViewTimeFormat  format;
	LrgTimelineViewSnap        snap;
	gfloat                 snap_distance;
	gfloat                 header_w;
	gfloat                 ruler_h;
	gfloat                 track_h;

	/* Look (see LrgCanvasLookParts) */
	LrgCanvasLook          look;
	LrgNodeGraphGrid       grid_style;     /* AUTO: the look's */
	LrgCanvasKeyShape      key_shape;      /* AUTO: the look's */
	gfloat                 glow_strength;  /* < 0: the look's */

	/* View */
	LrgSpring              zoom;         /* canvas units per second */
	gboolean               anchored;     /* zooming about a point */
	gdouble                anchor_time;
	gfloat                 anchor_rel;   /* anchor x relative to the track area */
	LrgScrollAxis          ax;           /* horizontal, in canvas units; ax.bar owned */
	LrgScrollAxis          ay;           /* vertical; ay.bar owned */
	LrgScrollFx            fx;
	gboolean               stepped;

	/* Interaction */
	DragKind               drag;
	gfloat                 press_x;
	gfloat                 press_y;
	gdouble                press_time;
	guint                  grab_id;      /* clip / keyframe / marker under the press */
	gdouble                marker_orig;  /* dragged marker's time before the drag */
	gboolean               grab_curve;   /* keyframe grabbed in the curve lane */
	GArray                *origs;        /* Orig */
	GHashTable            *box_base;     /* selection before a box drag (ids, clips | keys tagged) */
	gfloat                 box_x;
	gfloat                 box_y;
	gfloat                 box_x2;
	gfloat                 box_y2;
	gboolean               snap_shown;
	gdouble                snap_time;
	gboolean               track_changed_drag;
	LrgSpring              lift;         /* 0..1: dragged clips rise */
	guint                  hover_clip;
	HitKind                hover_kind;
	LrgSpring              hover;        /* 0..1 for the hovered clip */
	LrgSpring              glow;         /* 0..1 playhead handle glow */
	gboolean               handle_hot;

	gdouble                clock;
	gboolean               click_armed;
	gdouble                last_click_time;
	gfloat                 last_click_x;
	gfloat                 last_click_y;
};

G_DEFINE_FINAL_TYPE (LrgTimelineView, lrg_timeline_view, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_TIME,
	PROP_DURATION,
	PROP_ZOOM,
	PROP_FPS,
	PROP_TIME_FORMAT,
	PROP_PLAYING,
	PROP_RATE,
	PROP_LOOP,
	PROP_LOOP_START,
	PROP_LOOP_END,
	PROP_FOLLOW_PLAYHEAD,
	PROP_SNAP,
	PROP_SNAP_DISTANCE,
	PROP_HEADER_WIDTH,
	PROP_RULER_HEIGHT,
	PROP_TRACK_HEIGHT,
	PROP_SHOW_PLAY_BUTTON,
	PROP_LOOK,
	PROP_GRID_STYLE,
	PROP_KEY_SHAPE,
	PROP_GLOW,
	N_PROPS
};

enum
{
	SIGNAL_CLIP_ADDED,
	SIGNAL_CLIP_REMOVED,
	SIGNAL_CLIP_MOVED,
	SIGNAL_CLIP_RESIZED,
	SIGNAL_CLIP_SELECTED,
	SIGNAL_KEYFRAME_ADDED,
	SIGNAL_KEYFRAME_REMOVED,
	SIGNAL_KEYFRAME_MOVED,
	SIGNAL_PLAYHEAD_MOVED,
	SIGNAL_MARKER_ACTIVATED,
	SIGNAL_MARKER_MOVED,
	SIGNAL_SELECTION_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

#define ID_KEY(id) (GUINT_TO_POINTER (id))

/* Box-select base set: keyframe ids are tagged with the top bit so one
 * set holds both kinds */
#define KEY_TAG (0x80000000u)

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static gboolean
smooth (LrgTimelineView *self)
{
	return lrg_widget_get_motion (LRG_WIDGET (self)) != LRG_MOTION_NONE;
}

/* min_clip: shortest a clip may be (a frame, or 10 ms) */
static gdouble
min_clip (LrgTimelineView *self)
{
	return self->fps > 0.0 ? 1.0 / self->fps : 0.01;
}

/* pps: displayed zoom (canvas units per second) */
static gdouble
pps (LrgTimelineView *self)
{
	return MAX ((gdouble) self->zoom.value, MIN_ZOOM);
}

static void
geom (LrgTimelineView *self,
      Geom        *g)
{
	LrgWidget *w = LRG_WIDGET (self);

	g->wx = lrg_widget_get_world_x (w);
	g->wy = lrg_widget_get_world_y (w);
	g->w = lrg_widget_get_width (w);
	g->h = lrg_widget_get_height (w);
	g->cx = g->wx + MIN (self->header_w, g->w);
	g->cy = g->wy + MIN (self->ruler_h, g->h);
	g->vw = MAX (0.0f, g->w - MIN (self->header_w, g->w));
	g->vh = MAX (0.0f, g->h - MIN (self->ruler_h, g->h));
}

static gfloat
font_size (LrgTimelineView *self)
{
	return lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)), LRG_THEME_METRIC_FONT_SIZE_SMALL);
}

static GrlFont *
ui_font (LrgTimelineView *self)
{
	return lrg_theme_get_ui_font (lrg_widget_get_theme (LRG_WIDGET (self)));
}

/* default_track_h: clip lane height of a track without its own */
static gfloat
default_track_h (LrgTimelineView *self)
{
	if (self->track_h > 0.0f)
		return self->track_h;
	return MAX (28.0f, lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)),
	                                         LRG_THEME_METRIC_ROW_HEIGHT) * 1.4f);
}

static gfloat
collapsed_h (LrgTimelineView *self)
{
	return MAX (18.0f, lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)),
	                                         LRG_THEME_METRIC_ROW_HEIGHT) * 0.75f);
}

/* end_time: right end of the scrollable time (duration or last clip end) */
static gdouble
end_time (LrgTimelineView *self)
{
	gdouble end = self->duration;
	guint   i;

	for (i = 0; i < self->tracks->len; i++)
	{
		LrgTimelineViewTrack *t = g_ptr_array_index (self->tracks, i);
		guint             j;

		/* Clips are sorted by start; the last few hold the end unless
		 * overlaps let a long early clip reach further */
		for (j = t->clips->len; j > 0; j--)
		{
			Clip *c = g_ptr_array_index (t->clips, j - 1);

			end = MAX (end, c->start + c->duration);
			if (c->start + t->max_duration <= end)
				break;
		}
	}
	return end;
}

/* text_on: readable text colour on a fill */
static void
text_on (const GrlColor *fill,
         GrlColor       *out)
{
	static const GrlColor dark = { 0x16, 0x18, 0x1D, 0xFF };
	static const GrlColor light = { 0xFF, 0xFF, 0xFF, 0xFF };

	*out = lrg_paint_color_luminance (fill) > 0.5f ? dark : light;
}

static void
queue (LrgTimelineView *self)
{
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
track_changed (LrgTimelineViewTrack *track)
{
	if (track->timeline != NULL)
	{
		track->timeline->rows_dirty = TRUE;
		queue (track->timeline);
	}
}

/* is_descendant: @t is nested (at any depth) in @ancestor */
static gboolean
is_descendant (LrgTimelineViewTrack *t,
               LrgTimelineViewTrack *ancestor)
{
	LrgTimelineViewTrack *p;

	for (p = t->parent; p != NULL; p = p->parent)
		if (p == ancestor)
			return TRUE;
	return FALSE;
}

/* ==========================================================================
 * Sorted arrays
 * ========================================================================== */

/* clip_lower_bound: first index whose clip starts at or after @t */
static guint
clip_lower_bound (GPtrArray *clips,
                  gdouble    t)
{
	guint lo = 0;
	guint hi = clips->len;

	while (lo < hi)
	{
		guint mid = lo + (hi - lo) / 2;

		if (((Clip *) g_ptr_array_index (clips, mid))->start < t)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

/* clip_upper_bound: first index whose clip starts after @t */
static guint
clip_upper_bound (GPtrArray *clips,
                  gdouble    t)
{
	guint lo = 0;
	guint hi = clips->len;

	while (lo < hi)
	{
		guint mid = lo + (hi - lo) / 2;

		if (((Clip *) g_ptr_array_index (clips, mid))->start <= t)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

static guint
key_lower_bound (GPtrArray *keys,
                 gdouble    t)
{
	guint lo = 0;
	guint hi = keys->len;

	while (lo < hi)
	{
		guint mid = lo + (hi - lo) / 2;

		if (((Keyframe *) g_ptr_array_index (keys, mid))->time < t)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

static guint
key_upper_bound (GPtrArray *keys,
                 gdouble    t)
{
	guint lo = 0;
	guint hi = keys->len;

	while (lo < hi)
	{
		guint mid = lo + (hi - lo) / 2;

		if (((Keyframe *) g_ptr_array_index (keys, mid))->time <= t)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

/* clip_insert: put @c into its track's array at its sorted place (after
 * clips with the same start, so "last added" wins ties) */
static void
clip_insert (Clip *c)
{
	LrgTimelineViewTrack *t = c->track;

	g_ptr_array_insert (t->clips, (gint) clip_upper_bound (t->clips, c->start), c);
	t->max_duration = MAX (t->max_duration, c->duration);
}

/* clip_steal: take @c out of its track's array without freeing it */
static void
clip_steal (Clip *c)
{
	guint i;

	if (g_ptr_array_find (c->track->clips, c, &i))
		g_ptr_array_steal_index (c->track->clips, i);
}

static void
key_insert (Keyframe *k)
{
	GPtrArray *keys = k->track->keys;

	g_ptr_array_insert (keys, (gint) key_upper_bound (keys, k->time), k);
}

static void
key_steal (Keyframe *k)
{
	guint i;

	if (g_ptr_array_find (k->track->keys, k, &i))
		g_ptr_array_steal_index (k->track->keys, i);
}

static void
marker_sort (LrgTimelineView *self)
{
	guint i;
	guint j;

	/* insertion sort: markers are few and nearly sorted */
	for (i = 1; i < self->markers->len; i++)
	{
		for (j = i; j > 0; j--)
		{
			Marker *a = g_ptr_array_index (self->markers, j - 1);
			Marker *b = g_ptr_array_index (self->markers, j);

			if (a->time <= b->time)
				break;
			self->markers->pdata[j - 1] = b;
			self->markers->pdata[j] = a;
		}
	}
}

/*
 * track_fits:
 *
 * Whether [@start, @start + @duration) is free on @t: always on tracks
 * that allow overlaps; otherwise no clip (other than @ignore and clips
 * flagged moving) may intersect it. Touching ends are fine.
 */
static gboolean
track_fits (LrgTimelineViewTrack *t,
            gdouble           start,
            gdouble           duration,
            Clip             *ignore)
{
	gdouble end = start + duration;
	guint   i;
	guint   last;
	const gdouble eps = 1e-9;

	if (t->allow_overlap)
		return TRUE;

	i = clip_lower_bound (t->clips, start - t->max_duration);
	last = clip_lower_bound (t->clips, end);
	for (; i < last; i++)
	{
		Clip *c = g_ptr_array_index (t->clips, i);

		if (c == ignore || c->moving)
			continue;
		if (c->start < end - eps && c->start + c->duration > start + eps)
			return FALSE;
	}
	return TRUE;
}

/*
 * neighbour_bounds:
 *
 * The free room around @c on its track: end of the nearest clip before
 * it (@lo, 0 if none) and start of the nearest after it (@hi,
 * G_MAXDOUBLE if none). Used to clamp resizes.
 */
static void
neighbour_bounds (Clip    *c,
                  gdouble *lo,
                  gdouble *hi)
{
	LrgTimelineViewTrack *t = c->track;
	guint             i;

	*lo = 0.0;
	*hi = G_MAXDOUBLE;
	if (t->allow_overlap)
		return;
	for (i = 0; i < t->clips->len; i++)
	{
		Clip *o = g_ptr_array_index (t->clips, i);

		if (o == c || o->moving)
			continue;
		if (o->start + o->duration <= c->start + 1e-9)
			*lo = MAX (*lo, o->start + o->duration);
		else if (o->start >= c->start + c->duration - 1e-9)
			*hi = MIN (*hi, o->start);
	}
}

/* ==========================================================================
 * Rows
 * ========================================================================== */

/*
 * ensure_rows:
 *
 * Lays the visible tracks out top to bottom. Tracks are kept in display
 * order, so this is one pass: a track is skipped when any ancestor is
 * collapsed; its depth is its number of ancestors; it is a group when
 * the next track is nested in it. A collapsed track is a thin row; an
 * expanded one is its clip lane plus its curve lane.
 */
static void
ensure_rows (LrgTimelineView *self)
{
	gfloat y = 0.0f;
	gfloat zone;
	guint  i;

	if (!self->rows_dirty)
		return;
	self->rows_dirty = FALSE;
	g_array_set_size (self->rows, 0);

	for (i = 0; i < self->tracks->len; i++)
	{
		LrgTimelineViewTrack *t = g_ptr_array_index (self->tracks, i);
		LrgTimelineViewTrack *p;
		gboolean          hidden = FALSE;
		Row               row;

		memset (&row, 0, sizeof (Row));
		for (p = t->parent; p != NULL; p = p->parent)
		{
			if (p->collapsed)
				hidden = TRUE;
			row.depth++;
		}
		if (hidden)
			continue;

		row.track = t;
		row.group = i + 1 < self->tracks->len &&
		            ((LrgTimelineViewTrack *) g_ptr_array_index (self->tracks, i + 1))->parent == t;
		row.y = y;
		if (t->collapsed)
		{
			row.main_h = collapsed_h (self);
			row.curve_h = 0.0f;
		}
		else
		{
			row.main_h = t->height > 0.0f ? t->height : default_track_h (self);
			row.curve_h = t->curve_lane ? t->curve_height : 0.0f;
		}
		row.h = row.main_h + row.curve_h;
		y += row.h;
		g_array_append_val (self->rows, row);
	}

	/* Room under the last row so the horizontal bar never hides it */
	zone = lrg_scroll_bar_zone (LRG_WIDGET (self));
	self->content_h = y + zone;
}

/* row_at: the row under a world y in the track area, or -1 */
static gint
row_at (LrgTimelineView *self,
        gfloat       y)
{
	Geom   g;
	gfloat rel;
	guint  lo = 0;
	guint  hi;

	ensure_rows (self);
	geom (self, &g);
	if (y < g.cy || y >= g.cy + g.vh)
		return -1;
	rel = y - g.cy + lrg_scroll_axis_get_value (&self->ay);
	hi = self->rows->len;
	while (lo < hi)
	{
		guint mid = lo + (hi - lo) / 2;
		Row  *r = &g_array_index (self->rows, Row, mid);

		if (rel < r->y)
			hi = mid;
		else if (rel >= r->y + r->h)
			lo = mid + 1;
		else
			return (gint) mid;
	}
	return -1;
}

static gint
row_of_track (LrgTimelineView      *self,
              LrgTimelineViewTrack *t)
{
	guint i;

	ensure_rows (self);
	for (i = 0; i < self->rows->len; i++)
		if (g_array_index (self->rows, Row, i).track == t)
			return (gint) i;
	return -1;
}

/* row_top: world y of a row's top */
static gfloat
row_top (LrgTimelineView *self,
         const Row   *r)
{
	Geom g;

	geom (self, &g);
	return g.cy + r->y - lrg_scroll_axis_get_value (&self->ay);
}

/* value_to_y / y_to_value: curve lane mapping (lane top @ly, height @lh) */
static gfloat
value_to_y (LrgTimelineViewTrack *t,
            gdouble           v,
            gfloat            ly,
            gfloat            lh)
{
	gdouble span = t->value_max - t->value_min;
	gdouble f = span > 0.0 ? (v - t->value_min) / span : 0.5;
	gfloat  inner = MAX (1.0f, lh - CURVE_PAD * 2.0f);

	return ly + CURVE_PAD + (gfloat) (1.0 - f) * inner;
}

static gdouble
y_to_value (LrgTimelineViewTrack *t,
            gfloat            y,
            gfloat            ly,
            gfloat            lh)
{
	gfloat  inner = MAX (1.0f, lh - CURVE_PAD * 2.0f);
	gdouble f = 1.0 - (gdouble) ((y - ly - CURVE_PAD) / inner);

	f = CLAMP (f, 0.0, 1.0);
	return t->value_min + f * (t->value_max - t->value_min);
}

/* ==========================================================================
 * Scrolling and zoom
 * ========================================================================== */

/* update_extents: scrollable lengths for the displayed zoom */
static void
update_extents (LrgTimelineView *self)
{
	Geom g;

	ensure_rows (self);
	geom (self, &g);
	lrg_scroll_axis_set_extent (&self->ax, (gfloat) (end_time (self) * pps (self)) + TAIL, g.vw);
	lrg_scroll_axis_set_extent (&self->ay, self->content_h, g.vh);
}

/* apply_anchor: put the anchor time back under the anchor x */
static void
apply_anchor (LrgTimelineView *self)
{
	update_extents (self);
	lrg_scroll_axis_scroll_to (&self->ax, (gfloat) (self->anchor_time * pps (self)) - self->anchor_rel, FALSE);
}

/*
 * follow_playhead:
 *
 * When the playhead is outside the (target) view, page so it sits 10%
 * in from the left edge. Computed against the scroll target so a page
 * already gliding is not restarted every frame.
 */
static void
follow_playhead (LrgTimelineView *self)
{
	Geom   g;
	gfloat x;
	gdouble z = MAX ((gdouble) self->zoom.target, MIN_ZOOM);

	if (!self->follow || self->drag == DRAG_SCRUB)
		return;
	geom (self, &g);
	if (g.vw <= 0.0f)
		return;
	update_extents (self);
	x = (gfloat) (self->time * z) - self->ax.pos.target;
	if (x >= 0.0f && x <= g.vw - 16.0f)
		return;
	self->anchored = FALSE;
	lrg_scroll_axis_scroll_to (&self->ax, (gfloat) (self->time * z) - g.vw * 0.1f, smooth (self));
	lrg_scroll_fx_poke (&self->fx);
}

/* ==========================================================================
 * Ruler ticks and time text
 * ========================================================================== */

/*
 * tick_step:
 *
 * The smallest "nice" major step at least TICK_SPACING units wide: with
 * an fps first 1 / 2 / 5 / 10 frames (shorter than a second), then
 * 1-2-5 seconds and minute multiples. The minor count subdivides it
 * and halves while the minor marks would be closer than MINOR_GAP.
 */
static gdouble
tick_step (LrgTimelineView *self,
           gdouble      per_unit,
           guint       *minor)
{
	static const gdouble seconds[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0,
	                                   5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 1800.0,
	                                   3600.0, 7200.0, 21600.0 };
	static const guint   seconds_minor[] = { 5, 2, 5, 5, 2, 5, 5, 2, 5, 5, 2, 5, 5, 3, 3, 6, 2, 5, 5, 6,
	                                         6, 4, 6 };
	static const guint   frames[] = { 1, 2, 5, 10 };
	static const guint   frames_minor[] = { 1, 2, 5, 5 };
	gdouble              wanted = per_unit * TICK_SPACING;
	guint                i;

	/* 1. Frame steps (shorter than a second) */
	if (self->fps > 0.0)
	{
		for (i = 0; i < G_N_ELEMENTS (frames); i++)
		{
			gdouble step = (gdouble) frames[i] / self->fps;

			if (step >= wanted && (gdouble) frames[i] < self->fps)
			{
				*minor = frames_minor[i];
				return step;
			}
		}
	}

	/* 2. Seconds and up (with an fps, nothing finer than a second) */
	for (i = 0; i < G_N_ELEMENTS (seconds); i++)
	{
		if (self->fps > 0.0 && seconds[i] < 1.0)
			continue;
		if (seconds[i] >= wanted)
		{
			*minor = seconds_minor[i];
			if (self->fps > 0.0 && seconds[i] == 1.0)
				*minor = self->fps <= 30.0 ? (guint) (self->fps + 0.5)
				                           : (self->fps <= 60.0 ? (guint) (self->fps / 2.0 + 0.5) : 10);
			while (*minor > 1 && seconds[i] / (gdouble) *minor < per_unit * MINOR_GAP)
				*minor = (*minor % 2 == 0) ? *minor / 2 : 1;
			return seconds[i];
		}
	}

	*minor = 6;
	return seconds[G_N_ELEMENTS (seconds) - 1];
}

/* grid_step: what GRID snapping quantizes to */
static gdouble
grid_step (LrgTimelineView *self)
{
	guint   minor = 1;
	gdouble major;

	if (self->fps > 0.0)
		return 1.0 / self->fps;
	major = tick_step (self, 1.0 / pps (self), &minor);
	return major / (gdouble) MAX (minor, 1u);
}

static gchar *
format_time (LrgTimelineView *self,
             gdouble      time,
             gdouble      step)
{
	gdouble fps = self->fps > 0.0 ? self->fps : 30.0;
	gint64  frame;
	gint64  per_second;
	gint64  whole;
	gint64  rest;

	if (time < 0.0)
		time = 0.0;

	switch (self->format)
	{
	case LRG_TIMELINE_VIEW_TIME_FORMAT_FRAMES:
		return g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) floor (time * fps + 0.5));

	case LRG_TIMELINE_VIEW_TIME_FORMAT_TIMECODE:
		frame = (gint64) floor (time * fps + 0.5);
		per_second = MAX ((gint64) 1, (gint64) (fps + 0.5));
		whole = frame / per_second;
		rest = frame % per_second;
		if (whole >= 3600)
			return g_strdup_printf ("%" G_GINT64_FORMAT ":%02d:%02d:%02d", whole / 3600,
			                        (gint) ((whole / 60) % 60), (gint) (whole % 60), (gint) rest);
		return g_strdup_printf ("%02d:%02d:%02d", (gint) (whole / 60), (gint) (whole % 60), (gint) rest);

	case LRG_TIMELINE_VIEW_TIME_FORMAT_SECONDS:
	default:
		if (step < 1.0)
		{
			gint decimals = step < 0.01 ? 3 : (step < 0.1 ? 2 : 1);

			if (time >= 60.0)
				return g_strdup_printf ("%d:%0*.*f", (gint) (time / 60.0), decimals + 3, decimals,
				                        fmod (time, 60.0));
			return g_strdup_printf ("%.*fs", decimals, time);
		}
		whole = (gint64) floor (time + 0.5);
		if (whole >= 3600)
			return g_strdup_printf ("%" G_GINT64_FORMAT ":%02d:%02d", whole / 3600,
			                        (gint) ((whole / 60) % 60), (gint) (whole % 60));
		if (whole >= 60)
			return g_strdup_printf ("%d:%02d", (gint) (whole / 60), (gint) (whole % 60));
		return g_strdup_printf ("%ds", (gint) whole);
	}
}

/* ==========================================================================
 * Snapping
 * ========================================================================== */

typedef struct
{
	gdouble  t;
	gdouble  reach;
	gdouble  best;
	gdouble  found;
	gboolean any;
} Magnet;

static void
magnet_try (Magnet  *m,
            gdouble  candidate)
{
	gdouble d = fabs (candidate - m->t);

	if (d <= m->reach && d < m->best)
	{
		m->best = d;
		m->found = candidate;
		m->any = TRUE;
	}
}

/*
 * find_magnet:
 *
 * The nearest snap target to @t within the snap distance: the
 * sequence ends, the playhead (unless scrubbing), markers (except a
 * dragged one), and the
 * edges of clips and keyframe times that are not part of the drag.
 * Clips are searched per track with the sorted arrays, so this stays
 * cheap with thousands of clips.
 */
static gboolean
find_magnet (LrgTimelineView *self,
             gdouble      t,
             gboolean     playhead,
             gdouble     *found,
             gdouble     *distance)
{
	Magnet m;
	guint  i;

	m.t = t;
	m.reach = (gdouble) self->snap_distance / pps (self);
	m.best = G_MAXDOUBLE;
	m.found = t;
	m.any = FALSE;

	if (self->snap == LRG_TIMELINE_VIEW_SNAP_NONE)
		return FALSE;

	if (self->snap & (LRG_TIMELINE_VIEW_SNAP_CLIPS | LRG_TIMELINE_VIEW_SNAP_MARKERS))
	{
		magnet_try (&m, 0.0);
		magnet_try (&m, self->duration);
	}
	if (playhead && (self->snap & LRG_TIMELINE_VIEW_SNAP_PLAYHEAD))
		magnet_try (&m, self->time);
	if (self->snap & LRG_TIMELINE_VIEW_SNAP_MARKERS)
	{
		for (i = 0; i < self->markers->len; i++)
		{
			Marker *mk = g_ptr_array_index (self->markers, i);

			/* a dragged marker never snaps to itself */
			if (!mk->moving)
				magnet_try (&m, mk->time);
		}
	}

	if (self->snap & LRG_TIMELINE_VIEW_SNAP_CLIPS)
	{
		for (i = 0; i < self->tracks->len; i++)
		{
			LrgTimelineViewTrack *tr = g_ptr_array_index (self->tracks, i);
			guint             j;
			guint             last;

			j = clip_lower_bound (tr->clips, t - m.reach - tr->max_duration);
			last = clip_upper_bound (tr->clips, t + m.reach);
			for (; j < last; j++)
			{
				Clip *c = g_ptr_array_index (tr->clips, j);

				if (c->moving)
					continue;
				magnet_try (&m, c->start);
				magnet_try (&m, c->start + c->duration);
			}
			j = key_lower_bound (tr->keys, t - m.reach);
			last = key_upper_bound (tr->keys, t + m.reach);
			for (; j < last; j++)
			{
				Keyframe *k = g_ptr_array_index (tr->keys, j);

				if (!k->moving)
					magnet_try (&m, k->time);
			}
		}
	}

	if (m.any)
	{
		*found = m.found;
		if (distance != NULL)
			*distance = m.best;
	}
	return m.any;
}

/* quantize: round to the grid when GRID snapping is on */
static gdouble
quantize (LrgTimelineView *self,
          gdouble      t)
{
	gdouble step;

	if (!(self->snap & LRG_TIMELINE_VIEW_SNAP_GRID))
		return t;
	step = grid_step (self);
	return step > 0.0 ? floor (t / step + 0.5) * step : t;
}

/*
 * snap_time:
 *
 * Snaps a single time (a keyframe, an edge being resized, the scrubbed
 * playhead): to a magnet if one is in reach (showing the guide line),
 * else to the grid. With @enabled FALSE (Alt held) the time is
 * returned unchanged.
 */
static gdouble
snap_time (LrgTimelineView *self,
           gdouble      t,
           gboolean     playhead,
           gboolean     enabled)
{
	gdouble found;

	self->snap_shown = FALSE;
	if (!enabled)
		return t;
	if (find_magnet (self, t, playhead, &found, NULL))
	{
		self->snap_shown = TRUE;
		self->snap_time = found;
		return found;
	}
	return quantize (self, t);
}

/* ==========================================================================
 * Selection
 * ========================================================================== */

static void
selection_changed (LrgTimelineView *self)
{
	g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
}

/* clear_selection: empties both sets; returns TRUE if anything was selected */
static gboolean
clear_selection (LrgTimelineView *self)
{
	gboolean any = g_hash_table_size (self->sel_clips) > 0 || g_hash_table_size (self->sel_keys) > 0;

	g_hash_table_remove_all (self->sel_clips);
	g_hash_table_remove_all (self->sel_keys);
	return any;
}

/* snapshot_selection: the current selection as one tagged id set */
static GHashTable *
snapshot_selection (LrgTimelineView *self)
{
	GHashTable     *set = g_hash_table_new (g_direct_hash, g_direct_equal);
	GHashTableIter  it;
	gpointer        k;

	g_hash_table_iter_init (&it, self->sel_clips);
	while (g_hash_table_iter_next (&it, &k, NULL))
		g_hash_table_add (set, k);
	g_hash_table_iter_init (&it, self->sel_keys);
	while (g_hash_table_iter_next (&it, &k, NULL))
		g_hash_table_add (set, GUINT_TO_POINTER (GPOINTER_TO_UINT (k) | KEY_TAG));
	return set;
}

/* selection_equals: the current selection is exactly @set */
static gboolean
selection_equals (LrgTimelineView *self,
                  GHashTable  *set)
{
	g_autoptr(GHashTable) now = snapshot_selection (self);
	GHashTableIter        it;
	gpointer              k;

	if (g_hash_table_size (now) != g_hash_table_size (set))
		return FALSE;
	g_hash_table_iter_init (&it, set);
	while (g_hash_table_iter_next (&it, &k, NULL))
		if (!g_hash_table_contains (now, k))
			return FALSE;
	return TRUE;
}

/* ==========================================================================
 * Playhead
 * ========================================================================== */

/*
 * set_time_internal:
 *
 * Moves the playhead (clamped to [0, duration]), notifies, emits
 * playhead-moved and follows it with the view.
 */
static void
set_time_internal (LrgTimelineView *self,
                   gdouble      t)
{
	t = CLAMP (t, 0.0, self->duration);
	if (t == self->time)
		return;
	self->time = t;
	follow_playhead (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME]);
	g_signal_emit (self, signals[SIGNAL_PLAYHEAD_MOVED], 0, t);
}

/*
 * advance_playback:
 *
 * Moves the playhead by @dt * rate while playing. With a loop it wraps
 * inside the loop range (or the whole sequence when the range is
 * empty); without one it stops at either end.
 */
static void
advance_playback (LrgTimelineView *self,
                  gdouble      dt)
{
	gdouble t;
	gdouble lo = 0.0;
	gdouble hi = self->duration;
	gdouble len;

	if (!self->playing || dt <= 0.0)
		return;

	t = self->time + dt * self->rate;
	if (self->loop)
	{
		if (self->loop_end > self->loop_start)
		{
			lo = self->loop_start;
			hi = MIN (self->loop_end, self->duration);
		}
		len = hi - lo;
		if (len <= 0.0)
			t = lo;
		else if (self->rate >= 0.0 && t >= hi)
			t = lo + fmod (t - lo, len);
		else if (self->rate < 0.0 && t < lo)
			t = hi - fmod (lo - t, len);
	}
	else if ((self->rate >= 0.0 && t >= self->duration) || (self->rate < 0.0 && t <= 0.0))
	{
		set_time_internal (self, CLAMP (t, 0.0, self->duration));
		lrg_timeline_view_set_playing (self, FALSE);
		return;
	}
	set_time_internal (self, t);
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static void
lrg_timeline_view_layout_children (LrgContainer *container)
{
	LrgTimelineView *self = LRG_TIMELINE_VIEW (container);
	LrgWidget   *w = LRG_WIDGET (self);
	gfloat       zone;
	gfloat       hx;
	gfloat       ry;
	gfloat       vw;
	gfloat       vh;
	gboolean     hs;
	gboolean     vs;

	if (self->in_layout || self->ay.bar == NULL)
		return;
	self->in_layout = TRUE;

	update_extents (self);
	zone = lrg_scroll_bar_zone (w);
	hx = MIN (self->header_w, lrg_widget_get_width (w));
	ry = MIN (self->ruler_h, lrg_widget_get_height (w));
	vw = MAX (0.0f, lrg_widget_get_width (w) - hx);
	vh = MAX (0.0f, lrg_widget_get_height (w) - ry);

	/* Overlay bars along the right and bottom edges of the track area */
	vs = lrg_scroll_axis_get_max (&self->ay) > 0.0f;
	hs = lrg_scroll_axis_get_max (&self->ax) > 0.0f;
	lrg_widget_set_visible (self->ay.bar, vs);
	lrg_widget_set_visible (self->ax.bar, hs);
	lrg_widget_allocate (self->ay.bar, hx + vw - zone, ry, zone, MAX (0.0f, vh - (hs ? zone : 0.0f)));
	lrg_widget_allocate (self->ax.bar, hx, ry + vh - zone, MAX (0.0f, vw - (vs ? zone : 0.0f)), zone);

	self->in_layout = FALSE;
}

static void
lrg_timeline_view_measure (LrgWidget *widget,
                      gfloat    *preferred_width,
                      gfloat    *preferred_height)
{
	LrgTimelineView *self = LRG_TIMELINE_VIEW (widget);
	gfloat       rows;

	ensure_rows (self);
	rows = MIN (self->content_h, default_track_h (self) * 6.0f);
	*preferred_width = self->header_w + 480.0f;
	*preferred_height = self->ruler_h + MAX (rows, default_track_h (self) * 2.0f);
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

/*
 * preset_style_defaults:
 *
 * The built-in look of a part under a non-Auto preset: the preset's
 * parts (lrg_canvas_look_parts_init()) mapped onto the timeline's
 * widget parts, so theme style rules and lrg_widget_set_part_look()
 * still apply on top. Returns FALSE for parts left to the Auto look
 * (rubber band, snap guide, play button, scrollbars, focus).
 */
static gboolean
preset_style_defaults (LrgTimelineView *self,
                       LrgUIPart        part,
                       LrgWidgetState   state,
                       LrgPartLook     *look)
{
	LrgTheme          *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	LrgCanvasLookParts p;
	LrgPaintGradient   grad;
	GrlColor           c;

	lrg_canvas_look_parts_init (&p, self->look, theme);
	switch (part)
	{
	case LRG_UI_PART_BOX:
		lrg_part_look_set_background (look, &p.background);
		if (memcmp (&p.background, &p.background_end, sizeof (GrlColor)) != 0)
		{
			memset (&grad, 0, sizeof grad);
			grad.kind = LRG_PAINT_GRADIENT_LINEAR;
			grad.start = p.background;
			grad.end = p.background_end;
			grad.angle = 90.0f;
			lrg_part_look_set_gradient (look, &grad);
		}
		lrg_part_look_set_border (look, (state & LRG_WIDGET_STATE_FOCUSED) ? &p.accent : &p.lane_border, 1.0f);
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
		break;

	case LRG_UI_PART_HEADER:
		lrg_part_look_set_background (look, &p.track_header);
		lrg_part_look_set_border (look, &p.lane_border, 1.0f);
		break;

	case LRG_UI_PART_LABEL:
		lrg_part_look_set_foreground (look, &p.track_header_text);
		break;

	case LRG_UI_PART_TRACK:
		lrg_part_look_set_background (look, (state & LRG_WIDGET_STATE_SELECTED) ? &p.lane_alt : &p.lane);
		break;

	case LRG_UI_PART_ITEM:
		lrg_part_look_set_background (look, &p.node_background);
		lrg_part_look_set_radius (look, p.clip_radius);
		lrg_part_look_set_shadow (look, &p.node_shadow, p.node_shadow_blur * 0.35f,
		                          p.node_shadow_offset_y * 0.4f);
		lrg_paint_color_with_alpha (&p.text, 0.18f, &c);
		lrg_part_look_set_border (look, &c, 1.0f);
		if (p.clip_glow > 0.0f)
		{
			lrg_paint_color_with_alpha (&p.accent, 0.35f, &c);
			lrg_part_look_set_glow (look, &c, p.clip_glow);
		}
		if (state & LRG_WIDGET_STATE_SELECTED)
		{
			lrg_part_look_set_border (look, &p.selection, MAX (1.5f, p.selection_width));
			lrg_paint_color_with_alpha (&p.selection, 0.45f, &c);
			lrg_part_look_set_glow (look, &c, MAX (6.0f, p.selection_glow));
		}
		break;

	case LRG_UI_PART_MARK:
		lrg_part_look_set_background (look, (state & LRG_WIDGET_STATE_SELECTED) ? &p.key_selected : &p.key);
		lrg_part_look_set_border (look, &p.track_header, 1.5f);
		lrg_part_look_set_size (look, p.key_size);
		if (state & (LRG_WIDGET_STATE_SELECTED | LRG_WIDGET_STATE_HOVERED))
		{
			lrg_paint_color_with_alpha (&p.key_selected, 0.5f, &c);
			lrg_part_look_set_glow (look, &c, 6.0f);
		}
		break;

	case LRG_UI_PART_INDICATOR:
		lrg_part_look_set_background (look, &p.playhead);
		lrg_part_look_set_foreground (look, &p.background);
		if (p.playhead_glow > 0.0f)
		{
			lrg_paint_color_with_alpha (&p.playhead, 0.55f, &c);
			lrg_part_look_set_glow (look, &c, p.playhead_glow);
		}
		lrg_part_look_set_radius (look, G_MAXFLOAT);
		lrg_part_look_set_size (look, p.playhead_width);
		break;

	case LRG_UI_PART_TAB:
		lrg_part_look_set_background (look, &p.marker);
		lrg_part_look_set_radius (look, 3.0f);
		lrg_part_look_set_size (look, 1.0f);
		break;

	case LRG_UI_PART_FILL:
		lrg_part_look_set_background (look, &p.loop_region);
		lrg_part_look_set_border (look, &p.accent, 0.0f);
		break;

	default:
		return FALSE;
	}

	if (state & LRG_WIDGET_STATE_DISABLED)
		lrg_part_look_set_opacity (look, 0.45f);
	return TRUE;
}

static void
lrg_timeline_view_style_defaults (LrgWidget      *widget,
                             LrgUIPart       part,
                             LrgWidgetState  state,
                             LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	gfloat    radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	gfloat    bw = lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH);
	GrlColor  c;

	if (lrg_scroll_style_defaults (theme, part, state, look))
		return;

	/* Presets replace the theme-derived built-in look part by part */
	if (LRG_TIMELINE_VIEW (widget)->look != LRG_CANVAS_LOOK_AUTO &&
	    preset_style_defaults (LRG_TIMELINE_VIEW (widget), part, state, look))
		return;

	switch (part)
	{
	case LRG_UI_PART_BOX:
		/* The editor panel */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme,
		                          (state & LRG_WIDGET_STATE_FOCUSED) ? LRG_THEME_COLOR_ACCENT
		                                                             : LRG_THEME_COLOR_BORDER), bw);
		lrg_part_look_set_radius (look, radius);
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;

	case LRG_UI_PART_HEADER:
		/* Ruler band and track header column */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), bw);
		return;

	case LRG_UI_PART_TRACK:
		/* Lane background: a faint band, firmer on alternate rows (SELECTED) */
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK),
		                            (state & LRG_WIDGET_STATE_SELECTED) ? 0.30f : 0.14f, &c);
		lrg_part_look_set_background (look, &c);
		return;

	case LRG_UI_PART_ITEM:
		/* Clips: rounded, softly shadowed; colour comes from the
		 * track or clip. Selection adds an accent rim and glow. */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL));
		lrg_part_look_set_radius (look, MAX (3.0f, radius * 0.75f));
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW), 0.35f, &c);
		lrg_part_look_set_shadow (look, &c,
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR) * 0.35f,
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y) * 0.4f);
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT), 0.18f, &c);
		lrg_part_look_set_border (look, &c, bw);
		if (state & LRG_WIDGET_STATE_SELECTED)
		{
			lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT),
			                          MAX (2.0f, bw * 2.0f));
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.45f, &c);
			lrg_part_look_set_glow (look, &c, 6.0f);
		}
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;

	case LRG_UI_PART_MARK:
		/* Keyframes */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme,
		                              (state & LRG_WIDGET_STATE_SELECTED) ? LRG_THEME_COLOR_ACCENT
		                                                                  : LRG_THEME_COLOR_TEXT));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), 1.5f);
		if (state & (LRG_WIDGET_STATE_SELECTED | LRG_WIDGET_STATE_HOVERED))
		{
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.5f, &c);
			lrg_part_look_set_glow (look, &c, 6.0f);
		}
		return;

	case LRG_UI_PART_INDICATOR:
		/* Playhead line and handle */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_ON_ACCENT));
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.55f, &c);
		lrg_part_look_set_glow (look, &c, 8.0f);
		lrg_part_look_set_radius (look, G_MAXFLOAT);
		lrg_part_look_set_size (look, 2.0f);
		return;

	case LRG_UI_PART_TAB:
		/* Marker flags */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING));
		lrg_part_look_set_radius (look, 3.0f);
		lrg_part_look_set_size (look, 1.0f);
		return;

	case LRG_UI_PART_FILL:
		/* Loop range tint on the ruler and tracks */
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.14f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.0f);
		return;

	case LRG_UI_PART_VEIL:
		/* Rubber band */
		c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION);
		c.a = (guint8) MIN (c.a, 96);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 1.0f);
		lrg_part_look_set_radius (look, 2.0f);
		return;

	case LRG_UI_PART_HANDLE:
		/* Snap guide line */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING));
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING), 0.45f, &c);
		lrg_part_look_set_glow (look, &c, 5.0f);
		lrg_part_look_set_size (look, 1.5f);
		return;

	case LRG_UI_PART_KNOB:
		/* Play / pause button */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme,
		                              (state & LRG_WIDGET_STATE_PRESSED) ? LRG_THEME_COLOR_ACCENT_PRESSED :
		                              (state & LRG_WIDGET_STATE_HOVERED) ? LRG_THEME_COLOR_ACCENT_HOVER
		                                                                 : LRG_THEME_COLOR_ACCENT));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_ON_ACCENT));
		lrg_part_look_set_radius (look, G_MAXFLOAT);
		if (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_CHECKED))
		{
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.5f, &c);
			lrg_part_look_set_glow (look, &c, 8.0f);
		}
		return;

	case LRG_UI_PART_LABEL:
		/* Track names (foreground) and ruler labels (border colour slot
		 * is unused; muted text is derived in paint) */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		return;

	default:
		break;
	}

	LRG_WIDGET_CLASS (lrg_timeline_view_parent_class)->style_defaults (widget, part, state, look);
}

/* ==========================================================================
 * Painting
 * ========================================================================== */

/* Looks resolved once per paint and shared by every element */
typedef struct
{
	LrgPartLook clip;
	LrgPartLook clip_hover;
	LrgPartLook clip_sel;
	LrgPartLook key;
	LrgPartLook key_sel;
	LrgPartLook lane;
	LrgPartLook lane_alt;
	LrgPartLook header;
	LrgPartLook label;
	LrgPartLook indicator;
	LrgPartLook marker;
	LrgPartLook loop;
	LrgPartLook veil;
	LrgPartLook guide;
	LrgPartLook box;
	LrgPartLook ruler;
	LrgCanvasLookParts p;   /* every part, resolved (see resolve_parts) */
	GrlColor    text;
	GrlColor    muted;
	GrlColor    tick;
	GrlColor    accent;
	GrlFont    *font;
	gfloat      fs;
} Looks;

/*
 * resolve_parts:
 *
 * Builds the resolved LrgCanvasLookParts, lowest precedence first:
 * the preset (Auto: the theme), then the timeline's widget parts as
 * resolved through style_defaults + theme style rules + part-look
 * overrides, then the timeline's look properties. Clip colours and
 * keyframe shapes are per item and applied while painting.
 */
static void
resolve_parts (LrgTimelineView    *self,
               const Looks        *l,
               LrgCanvasLookParts *p)
{
	lrg_canvas_look_parts_init (p, self->look, lrg_widget_get_theme (LRG_WIDGET (self)));

	/* --- theme rules and part looks, through the widget parts --- */
	if (l->box.gradient.kind == LRG_PAINT_GRADIENT_LINEAR)
	{
		p->background = l->box.gradient.start;
		p->background_end = l->box.gradient.end;
	}
	else
	{
		p->background = l->box.background;
		p->background_end = l->box.background;
	}
	p->track_header = l->header.background;
	p->track_header_text = l->label.foreground;
	p->lane = l->lane.background;
	p->lane_alt = l->lane_alt.background;
	p->clip_radius = l->clip.radii.top_left;
	p->clip_glow = l->clip.glow.a > 0 ? l->clip.glow_size : 0.0f;
	p->selection = l->clip_sel.border;
	p->selection_width = l->clip_sel.border_width;
	p->selection_glow = l->clip_sel.glow.a > 0 ? l->clip_sel.glow_size : 0.0f;
	p->key = l->key.background;
	p->key_selected = l->key_sel.background;
	if (l->key.size > 0.0f)
		p->key_size = l->key.size;
	p->playhead = l->indicator.background;
	p->playhead_width = l->indicator.size;
	p->playhead_glow = l->indicator.glow.a > 0 ? l->indicator.glow_size : 0.0f;
	p->marker = l->marker.background;
	p->loop_region = l->loop.background;
	if (self->look == LRG_CANVAS_LOOK_AUTO)
	{
		/* The classic look: the ruler is a header band, no time grid */
		p->ruler = l->header.background;
		p->ruler_text = l->muted;
		p->ruler_tick = l->tick;
		p->grid_style = LRG_NODE_GRAPH_GRID_NONE;
	}

	/* --- the timeline's own look properties --- */
	if (self->grid_style != LRG_NODE_GRAPH_GRID_AUTO)
		p->grid_style = self->grid_style;
	if (self->key_shape != LRG_CANVAS_KEY_SHAPE_AUTO)
		p->key_shape = self->key_shape;
	if (self->glow_strength >= 0.0f)
		p->glow = self->glow_strength;
}

static void
resolve_looks (LrgTimelineView *self,
               Looks       *l)
{
	LrgWidget *w = LRG_WIDGET (self);
	LrgTheme  *theme = lrg_widget_get_theme (w);

	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_ITEM, LRG_WIDGET_STATE_NONE, &l->clip);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_ITEM, LRG_WIDGET_STATE_HOVERED, &l->clip_hover);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_ITEM, LRG_WIDGET_STATE_SELECTED, &l->clip_sel);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_MARK, LRG_WIDGET_STATE_NONE, &l->key);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_MARK, LRG_WIDGET_STATE_SELECTED, &l->key_sel);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_TRACK, LRG_WIDGET_STATE_NONE, &l->lane);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_TRACK, LRG_WIDGET_STATE_SELECTED, &l->lane_alt);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_HEADER, LRG_WIDGET_STATE_NONE, &l->header);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE, &l->label);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_INDICATOR, LRG_WIDGET_STATE_NONE, &l->indicator);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_TAB, LRG_WIDGET_STATE_NONE, &l->marker);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_FILL, LRG_WIDGET_STATE_NONE, &l->loop);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_VEIL, LRG_WIDGET_STATE_NONE, &l->veil);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_HANDLE, LRG_WIDGET_STATE_NONE, &l->guide);

	lrg_widget_resolve_look (w, LRG_UI_PART_BOX, &l->box);

	l->text = l->label.foreground;
	l->muted = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	lrg_paint_color_with_alpha (&l->muted, 0.55f, &l->tick);
	l->accent = l->indicator.background;
	l->font = ui_font (self);
	l->fs = font_size (self);

	resolve_parts (self, l, &l->p);

	/* Presets bring their own ruler band and ruler ink */
	l->ruler = l->header;
	if (self->look != LRG_CANVAS_LOOK_AUTO)
	{
		l->ruler.background = l->p.ruler;
		l->muted = l->p.ruler_text;
		l->tick = l->p.ruler_tick;
	}

	/* Overall glow strength scales every halo (1: as resolved) */
	if (l->p.glow != 1.0f)
	{
		LrgPartLook *glowing[] = { &l->clip, &l->clip_hover, &l->clip_sel, &l->key, &l->key_sel,
		                           &l->indicator, &l->guide };
		guint        i;

		for (i = 0; i < G_N_ELEMENTS (glowing); i++)
		{
			glowing[i]->glow_size *= l->p.glow;
			if (l->p.glow <= 0.0f)
				glowing[i]->glow.a = 0;
		}
	}
}

void
lrg_timeline_view_get_look_parts (LrgTimelineView    *self,
                                  LrgCanvasLookParts *parts)
{
	Looks l;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));
	g_return_if_fail (parts != NULL);

	resolve_looks (self, &l);
	*parts = l.p;
}

/* paint_glow_line: a vertical line with a soft halo (playhead, guides) */
static void
paint_glow_line (LrgPaintList      *list,
                 const LrgPartLook *look,
                 gfloat             x,
                 gfloat             y0,
                 gfloat             y1,
                 gfloat             glow)
{
	gfloat thick = MAX (1.0f, look->size);

	if (glow > 0.01f && look->glow.a > 0)
	{
		GrlColor g;

		lrg_paint_color_with_alpha (&look->glow, ((gfloat) look->glow.a / 255.0f) * glow, &g);
		lrg_paint_list_shadow (list, x - thick * 0.5f, y0, thick, y1 - y0, thick * 0.5f,
		                       look->glow_size, 0.0f, 0.0f, 0.0f, &g);
	}
	lrg_paint_list_rect (list, x - thick * 0.5f, y0, thick, y1 - y0, thick * 0.5f, &look->background);
}

/* paint_diamond: a keyframe diamond centred at (@x, @y) */
static void
paint_diamond (LrgPaintList      *list,
               const LrgPartLook *look,
               gfloat             x,
               gfloat             y,
               gfloat             half)
{
	gfloat ob = half + MAX (1.0f, look->border_width);

	if (look->glow.a > 0)
		lrg_paint_list_shadow (list, x - half, y - half, half * 2.0f, half * 2.0f, half,
		                       look->glow_size, 0.0f, 0.0f, 0.0f, &look->glow);
	/* Outline: a slightly larger diamond in the border colour */
	if (look->border_width > 0.0f && look->border.a > 0)
	{
		lrg_paint_list_triangle (list, x - ob, y, x, y - ob, x + ob, y, &look->border);
		lrg_paint_list_triangle (list, x - ob, y, x + ob, y, x, y + ob, &look->border);
	}
	lrg_paint_list_triangle (list, x - half, y, x, y - half, x + half, y, &look->background);
	lrg_paint_list_triangle (list, x - half, y, x + half, y, x, y + half, &look->background);
}

/*
 * paint_key:
 *
 * A keyframe in @shape centred at (@x, @y): a diamond, a dot, a
 * rounded square or a thin bar, each with an outline in the look's
 * border colour and the look's glow.
 */
static void
paint_key (LrgPaintList      *list,
           const LrgPartLook *look,
           LrgCanvasKeyShape  shape,
           gfloat             x,
           gfloat             y,
           gfloat             half)
{
	gfloat ob = MAX (1.0f, look->border_width);
	gfloat bw;

	switch (shape)
	{
	case LRG_CANVAS_KEY_SHAPE_CIRCLE:
		if (look->glow.a > 0)
			lrg_paint_list_shadow (list, x - half, y - half, half * 2.0f, half * 2.0f, half,
			                       look->glow_size, 0.0f, 0.0f, 0.0f, &look->glow);
		if (look->border_width > 0.0f && look->border.a > 0)
			lrg_paint_list_circle (list, x, y, half + ob * 0.5f, &look->border);
		lrg_paint_list_circle (list, x, y, half - ob * 0.5f + 0.5f, &look->background);
		break;

	case LRG_CANVAS_KEY_SHAPE_SQUARE:
		half *= 0.85f;
		if (look->glow.a > 0)
			lrg_paint_list_shadow (list, x - half, y - half, half * 2.0f, half * 2.0f, half * 0.35f,
			                       look->glow_size, 0.0f, 0.0f, 0.0f, &look->glow);
		lrg_paint_list_rect_ex (list, x - half, y - half, half * 2.0f, half * 2.0f,
		                        &(LrgCornerRadii){ half * 0.35f, half * 0.35f, half * 0.35f, half * 0.35f },
		                        &look->background, NULL, look->border.a > 0 ? look->border_width : 0.0f,
		                        &look->border);
		break;

	case LRG_CANVAS_KEY_SHAPE_BAR:
		bw = MAX (2.0f, half * 0.45f);
		if (look->glow.a > 0)
			lrg_paint_list_shadow (list, x - bw * 0.5f, y - half * 1.5f, bw, half * 3.0f, bw * 0.5f,
			                       look->glow_size, 0.0f, 0.0f, 0.0f, &look->glow);
		lrg_paint_list_rect_ex (list, x - bw * 0.5f, y - half * 1.5f, bw, half * 3.0f,
		                        &(LrgCornerRadii){ bw * 0.5f, bw * 0.5f, bw * 0.5f, bw * 0.5f },
		                        &look->background, NULL, look->border.a > 0 ? MIN (1.0f, look->border_width) : 0.0f,
		                        &look->border);
		break;

	case LRG_CANVAS_KEY_SHAPE_DIAMOND:
	case LRG_CANVAS_KEY_SHAPE_AUTO:
	default:
		paint_diamond (list, look, x, y, half);
		break;
	}
}

/*
 * paint_time_grid:
 *
 * The time grid behind the lanes, at the ruler's ticks: full-height
 * lines (major ones with an optional halo), dotted major lines, or
 * small crosses where major ticks meet row boundaries. One instance
 * (two with a halo) per line, batched with the rest of the frame.
 */
static void
paint_time_grid (LrgTimelineView *self,
                 LrgPaintList    *list,
                 const Looks     *l,
                 const Geom      *g)
{
	const LrgCanvasLookParts *p = &l->p;
	gdouble                   per = 1.0 / pps (self);
	gdouble                   t0;
	gdouble                   t1;
	gdouble                   major;
	gdouble                   minor_step;
	guint                     minor;
	gint64                    k;
	gint64                    k1;
	gfloat                    halo = p->grid_glow * p->glow;
	guint                     budget = 4000;

	if (p->grid_style == LRG_NODE_GRAPH_GRID_NONE || p->grid_style == LRG_NODE_GRAPH_GRID_AUTO)
		return;

	lrg_timeline_view_get_visible_range (self, &t0, &t1);
	major = tick_step (self, per, &minor);
	minor_step = major / (gdouble) MAX (minor, 1u);
	k = (gint64) floor (MAX (0.0, t0) / minor_step);
	k1 = (gint64) ceil (t1 / minor_step);
	for (; k <= k1 && budget > 0; k++)
	{
		gboolean is_major = minor <= 1 || k % (gint64) minor == 0;
		gfloat   x = roundf (lrg_timeline_view_time_to_x (self, (gdouble) k * minor_step)) + 0.5f;
		gfloat   y;

		switch (p->grid_style)
		{
		case LRG_NODE_GRAPH_GRID_LINES:
			if (is_major && halo > 0.0f)
			{
				GrlColor h = p->grid_major;

				h.a = (guint8) ((gfloat) h.a * 0.35f);
				lrg_paint_list_line (list, x, g->cy, x, g->cy + g->vh, 1.0f + halo * 2.0f, &h);
			}
			lrg_paint_list_line (list, x, g->cy, x, g->cy + g->vh, 1.0f,
			                     is_major ? &p->grid_major : &p->grid_minor);
			budget--;
			break;

		case LRG_NODE_GRAPH_GRID_DOTS:
			if (!is_major)
				break;
			for (y = g->cy + 4.0f; y < g->cy + g->vh && budget > 0; y += 8.0f, budget--)
				lrg_paint_list_circle (list, x, y, 1.0f, &p->grid_major);
			break;

		case LRG_NODE_GRAPH_GRID_CROSSES:
			{
				gfloat off = lrg_scroll_axis_get_value (&self->ay);
				guint  i;

				if (!is_major)
					break;
				for (i = 0; i < self->rows->len && budget > 0; i++)
				{
					Row *r = &g_array_index (self->rows, Row, i);

					y = g->cy + r->y - off;
					if (y < g->cy - 4.0f || y > g->cy + g->vh + 4.0f)
						continue;
					lrg_paint_list_line (list, x - 3.0f, y, x + 3.0f, y, 1.0f, &p->grid_major);
					lrg_paint_list_line (list, x, y - 3.0f, x, y + 3.0f, 1.0f, &p->grid_major);
					budget--;
				}
			}
			break;

		default:
			return;
		}
	}
}

/*
 * paint_clip:
 *
 * One clip: its colour (clip, else track) in the ITEM look, blended to
 * the hovered look by the hover spring, the selected look when
 * selected; lifted (bigger, lower shadow, raised) while dragged; a
 * sheen on the top half; the label, clipped to the clip; and a grip
 * on the edge under the pointer when it would resize.
 */
static void
paint_clip (LrgTimelineView  *self,
            LrgPaintList *list,
            const Looks  *l,
            Clip         *c,
            gfloat        x0,
            gfloat        x1,
            gfloat        y,
            gfloat        h,
            gfloat        clip_l,
            gfloat        clip_r)
{
	LrgPartLook look;
	GrlColor    fill;
	GrlColor    ink;
	GrlColor    sheen;
	gboolean    sel = g_hash_table_contains (self->sel_clips, ID_KEY (c->id));
	gfloat      w = MAX (2.0f, x1 - x0);
	gfloat      lift = c->moving ? self->lift.value : 0.0f;

	look = sel ? l->clip_sel : l->clip;
	if (c->has_color)
		fill = c->color;
	else
		lrg_timeline_view_track_get_color (c->track, &fill);

	if (c->id == self->hover_clip && self->hover.value > 0.001f && !sel)
		lrg_part_look_lerp (&look, &l->clip_hover, CLAMP (self->hover.value, 0.0f, 1.0f), &look);
	if (c->id == self->hover_clip && self->hover.value > 0.001f)
		lrg_paint_color_shade (&fill, 0.12f * CLAMP (self->hover.value, 0.0f, 1.0f), &fill);
	look.background = fill;
	look.gradient.kind = LRG_PAINT_GRADIENT_NONE;

	/* --- look variants: gradient body, outlined (Neon), coloured glow --- */
	if (l->p.clip_gradient && !l->p.clip_outline)
	{
		look.gradient.kind = LRG_PAINT_GRADIENT_LINEAR;
		look.gradient.angle = 90.0f;
		lrg_paint_color_shade (&fill, 0.10f, &look.gradient.start);
		lrg_paint_color_shade (&fill, -0.14f, &look.gradient.end);
	}
	if (l->p.clip_outline)
	{
		/* An opaque dark tint (the panel mixed with the clip colour), so
		 * the glow under it never washes the body */
		lrg_paint_color_mix (&l->p.background, &fill, 0.2f, &look.background);
		look.background.a = 255;
		if (!sel)
		{
			look.border = fill;
			look.border_width = MAX (1.5f, look.border_width);
		}
	}
	if (look.glow.a > 0 && !sel)
		lrg_paint_color_with_alpha (&fill, (gfloat) look.glow.a / 255.0f, &look.glow);

	/* Lifted: shadow spreads and drops, the clip rises a little */
	if (lift > 0.001f)
	{
		look.shadow_blur += 14.0f * lift;
		look.shadow_offset_y += 6.0f * lift;
		look.shadow.a = (guint8) MIN (255.0f, (gfloat) look.shadow.a * (1.0f + lift));
		y -= 2.0f * lift;
	}
	if (c->track->muted)
		look.opacity *= 0.4f;

	lrg_widget_paint_box (LRG_WIDGET (self), list, &look, x0, y, w, h);

	/* Sheen on the upper half (not on outlined clips) */
	lrg_paint_color_with_alpha (&(GrlColor){ 255, 255, 255, 255 }, 0.10f * look.opacity, &sheen);
	if (h > 8.0f && w > 6.0f && !l->p.clip_outline)
	{
		LrgCornerRadii r = look.radii;

		r.bottom_left = 0.0f;
		r.bottom_right = 0.0f;
		lrg_paint_list_rect_ex (list, x0 + 1.0f, y + 1.0f, w - 2.0f, h * 0.45f, &r, &sheen, NULL, 0.0f, NULL);
	}

	/* Label inside the visible part of the clip */
	if (c->label != NULL && w > 18.0f)
	{
		gfloat vx0 = MAX (x0, clip_l);
		gfloat vx1 = MIN (x0 + w, clip_r);

		if (vx1 - vx0 > 12.0f)
		{
			if (c->track->muted)
			{
				/* Read the faded fill as it lands on the panel */
				GrlColor seen;

				lrg_paint_color_mix (lrg_theme_get_color (lrg_widget_get_theme (LRG_WIDGET (self)),
				                                          LRG_THEME_COLOR_PANEL), &fill, 0.4f, &seen);
				seen.a = 255;
				text_on (&seen, &ink);
				ink.a = (guint8) ((gfloat) ink.a * 0.7f);
			}
			else if (l->p.clip_outline)
			{
				/* Bright ink of the clip's own colour on its dark tint */
				lrg_paint_color_mix (&fill, &(GrlColor){ 255, 255, 255, 255 }, 0.55f, &ink);
				ink.a = 255;
			}
			else
				text_on (&fill, &ink);
			lrg_paint_list_push_clip (list, vx0 + 2.0f, y, vx1 - vx0 - 4.0f, h);
			lrg_paint_list_text (list, l->font, c->label, vx0 + 6.0f,
			                     roundf (y + (h - l->fs) * 0.5f), l->fs, 0.0f, &ink);
			lrg_paint_list_pop_clip (list);
		}
	}

	/* Resize grip on the hovered edge */
	if (c->id == self->hover_clip &&
	    (self->hover_kind == HIT_CLIP_LEFT || self->hover_kind == HIT_CLIP_RIGHT) &&
	    !c->track->locked)
	{
		gfloat gx = self->hover_kind == HIT_CLIP_LEFT ? x0 + 2.0f : x0 + w - 5.0f;

		text_on (&fill, &ink);
		ink.a = 200;
		lrg_paint_list_rect (list, gx, y + h * 0.25f, 3.0f, h * 0.5f, 1.5f, &ink);
	}
}

/* paint_curve_segment: the curve from key @a to @b (or a flat run) */
static void
paint_curve_segment (LrgTimelineView              *self,
                     LrgPaintList             *list,
                     const LrgPaintCurveStyle *style,
                     LrgTimelineViewTrack         *t,
                     const Keyframe           *a,
                     const Keyframe           *b,
                     gfloat                    ly,
                     gfloat                    lh)
{
	gfloat  x0 = lrg_timeline_view_time_to_x (self, a->time);
	gfloat  x1 = lrg_timeline_view_time_to_x (self, b->time);
	gfloat  y0 = value_to_y (t, a->value, ly, lh);
	gfloat  y1 = value_to_y (t, b->value, ly, lh);
	gdouble d = b->time - a->time;
	gfloat  c1y;
	gfloat  c2y;

	switch (a->interp)
	{
	case LRG_TIMELINE_VIEW_INTERPOLATION_STEP:
		/* Hold, then jump */
		lrg_paint_list_curve (list, x0, y0, x0 + (x1 - x0) / 3.0f, y0, x0 + (x1 - x0) * 2.0f / 3.0f, y0,
		                      x1, y0, 2.0f, style);
		lrg_paint_list_curve (list, x1, y0, x1, y0 + (y1 - y0) / 3.0f, x1, y0 + (y1 - y0) * 2.0f / 3.0f,
		                      x1, y1, 2.0f, style);
		return;

	case LRG_TIMELINE_VIEW_INTERPOLATION_EASE:
		/* smoothstep = Bezier with flat control points */
		c1y = y0;
		c2y = y1;
		break;

	case LRG_TIMELINE_VIEW_INTERPOLATION_BEZIER:
		/* Hermite tangents as Bezier controls a third of the way in */
		c1y = value_to_y (t, a->value + a->out_slope * d / 3.0, ly, lh);
		c2y = value_to_y (t, b->value - b->in_slope * d / 3.0, ly, lh);
		break;

	case LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR:
	default:
		c1y = y0 + (y1 - y0) / 3.0f;
		c2y = y0 + (y1 - y0) * 2.0f / 3.0f;
		break;
	}
	lrg_paint_list_curve (list, x0, y0, x0 + (x1 - x0) / 3.0f, c1y, x0 + (x1 - x0) * 2.0f / 3.0f, c2y,
	                      x1, y1, 2.0f, style);
}

/*
 * paint_curve_lane:
 *
 * The value curve of a track between its keyframes inside the visible
 * time range (one segment before and after it so lines enter and leave
 * the view), flat runs before the first and after the last keyframe,
 * a faint mid line, and a point per keyframe.
 */
static void
paint_curve_lane (LrgTimelineView  *self,
                  LrgPaintList *list,
                  const Looks  *l,
                  const Row    *r,
                  gfloat        ly,
                  gfloat        lh,
                  gdouble       t0,
                  gdouble       t1,
                  const Geom   *g)
{
	LrgTimelineViewTrack  *t = r->track;
	LrgPaintCurveStyle style;
	GrlColor           col;
	GrlColor           mid;
	guint              first;
	guint              last;
	guint              i;

	lrg_timeline_view_track_get_color (t, &col);
	lrg_paint_curve_style_init (&style, &col);
	lrg_paint_color_with_alpha (&col, 0.35f, &style.glow);
	style.glow_size = 4.0f;

	/* Centre guide */
	lrg_paint_color_with_alpha (&l->tick, 0.35f, &mid);
	lrg_paint_list_rect (list, g->cx, roundf (ly + lh * 0.5f), g->vw, 1.0f, 0.0f, &mid);

	if (t->keys->len == 0)
	{
		gfloat y = value_to_y (t, t->default_value, ly, lh);

		lrg_paint_color_with_alpha (&col, 0.5f, &col);
		lrg_paint_list_rect (list, g->cx, y - 1.0f, g->vw, 2.0f, 1.0f, &col);
		return;
	}

	first = key_lower_bound (t->keys, t0);
	first = first > 0 ? first - 1 : 0;
	last = MIN (t->keys->len, key_upper_bound (t->keys, t1) + 1);

	/* Flat runs outside the keyed range */
	{
		Keyframe *a = g_ptr_array_index (t->keys, 0);
		Keyframe *z = g_ptr_array_index (t->keys, t->keys->len - 1);
		gfloat    ax = lrg_timeline_view_time_to_x (self, a->time);
		gfloat    zx = lrg_timeline_view_time_to_x (self, z->time);
		gfloat    ay = value_to_y (t, a->value, ly, lh);
		gfloat    zy = value_to_y (t, z->value, ly, lh);

		if (ax > g->cx)
			lrg_paint_list_curve (list, g->cx, ay, g->cx, ay, ax, ay, ax, ay, 2.0f, &style);
		if (zx < g->cx + g->vw)
			lrg_paint_list_curve (list, zx, zy, zx, zy, g->cx + g->vw, zy, g->cx + g->vw, zy, 2.0f, &style);
	}

	for (i = first; i + 1 < last; i++)
		paint_curve_segment (self, list, &style, t,
		                     g_ptr_array_index (t->keys, i), g_ptr_array_index (t->keys, i + 1), ly, lh);

	/* Points */
	for (i = first; i < last; i++)
	{
		Keyframe          *k = g_ptr_array_index (t->keys, i);
		const LrgPartLook *kl = g_hash_table_contains (self->sel_keys, ID_KEY (k->id)) ? &l->key_sel : &l->key;
		gfloat             x = lrg_timeline_view_time_to_x (self, k->time);
		gfloat             y = value_to_y (t, k->value, ly, lh);

		if (kl->glow.a > 0)
			lrg_paint_list_shadow (list, x - CURVE_POINT, y - CURVE_POINT, CURVE_POINT * 2.0f,
			                       CURVE_POINT * 2.0f, CURVE_POINT, kl->glow_size, 0.0f, 0.0f, 0.0f, &kl->glow);
		lrg_paint_list_circle (list, x, y, CURVE_POINT + 1.5f, &kl->border);
		lrg_paint_list_circle (list, x, y, CURVE_POINT, kl == &l->key ? &col : &kl->background);
	}
}

/* paint_run: a merged run of clips too narrow to draw one by one */
static void
paint_run (LrgPaintList         *list,
           LrgTimelineViewTrack *t,
           gfloat                x0,
           gfloat                x1,
           gfloat                y,
           gfloat                h)
{
	GrlColor col;

	lrg_timeline_view_track_get_color (t, &col);
	if (t->muted)
		col.a = (guint8) ((gfloat) col.a * 0.4f);
	lrg_paint_list_rect (list, x0, y, MAX (2.0f, x1 - x0), h, 1.0f, &col);
}

/*
 * paint_row:
 *
 * A track row inside the track area: lane background, clips and
 * keyframes on screen (found with binary searches), and the curve lane.
 */
static void
paint_row (LrgTimelineView  *self,
           LrgPaintList *list,
           const Looks  *l,
           const Row    *r,
           guint         index,
           gfloat        top,
           gdouble       t0,
           gdouble       t1,
           const Geom   *g)
{
	LrgTimelineViewTrack *t = r->track;
	gfloat            pad = r->main_h > 22.0f ? 4.0f : 2.0f;
	gboolean          run;
	gfloat            run_x0;
	gfloat            run_x1;
	guint             i;
	guint             last;

	/* 1. Lane bands */
	lrg_widget_paint_box (LRG_WIDGET (self), list, (index % 2) ? &l->lane_alt : &l->lane,
	                      g->cx, top, g->vw, r->h);
	if (r->curve_h > 0.0f)
	{
		GrlColor sep;

		lrg_paint_color_with_alpha (&l->tick, 0.3f, &sep);
		lrg_paint_list_rect (list, g->cx, top + r->main_h, g->vw, 1.0f, 0.0f, &sep);
	}

	/* 2. Clips in [t0, t1] (moving clips are drawn last, above the rest).
	 *    Zoomed far out, clips narrower than TINY_CLIP are merged into
	 *    runs drawn as one plain bar each, so a row costs a handful of
	 *    instances however many clips it holds. */
	i = clip_lower_bound (t->clips, t0 - t->max_duration);
	last = clip_upper_bound (t->clips, t1);
	run = FALSE;
	run_x0 = 0.0f;
	run_x1 = 0.0f;
	for (; i < last; i++)
	{
		Clip  *c = g_ptr_array_index (t->clips, i);
		gfloat x0;
		gfloat x1;

		if (c->start + c->duration < t0 || c->moving)
			continue;
		x0 = lrg_timeline_view_time_to_x (self, c->start);
		x1 = lrg_timeline_view_time_to_x (self, c->start + c->duration);

		if (x1 - x0 < TINY_CLIP && !g_hash_table_contains (self->sel_clips, ID_KEY (c->id)))
		{
			if (run && x0 <= run_x1 + 2.0f)
			{
				run_x1 = MAX (run_x1, x1);
				continue;
			}
			if (run)
				paint_run (list, t, run_x0, run_x1, top + pad, r->main_h - pad * 2.0f);
			run = TRUE;
			run_x0 = x0;
			run_x1 = x1;
			continue;
		}
		if (run)
			paint_run (list, t, run_x0, run_x1, top + pad, r->main_h - pad * 2.0f);
		run = FALSE;
		paint_clip (self, list, l, c, x0, x1, top + pad, r->main_h - pad * 2.0f, g->cx, g->cx + g->vw);
	}
	if (run)
		paint_run (list, t, run_x0, run_x1, top + pad, r->main_h - pad * 2.0f);

	/* 3. Keyframe diamonds across the middle of the clip lane */
	i = key_lower_bound (t->keys, t0 - (gdouble) (KEY_HALF * 2.0f) / pps (self));
	last = key_upper_bound (t->keys, t1 + (gdouble) (KEY_HALF * 2.0f) / pps (self));
	for (; i < last; i++)
	{
		Keyframe *k = g_ptr_array_index (t->keys, i);
		gboolean  sel = g_hash_table_contains (self->sel_keys, ID_KEY (k->id));
		gfloat    half = MIN (l->p.key_size, r->main_h * 0.3f);

		/* Per-key shape beats the timeline's and the look's */
		paint_key (list, sel ? &l->key_sel : &l->key,
		           k->shape != LRG_CANVAS_KEY_SHAPE_AUTO ? k->shape : l->p.key_shape,
		           lrg_timeline_view_time_to_x (self, k->time), top + r->main_h * 0.5f, half);
	}

	/* 4. Curve lane */
	if (r->curve_h > 0.0f)
	{
		lrg_paint_list_push_clip (list, g->cx, top + r->main_h, g->vw, r->curve_h);
		paint_curve_lane (self, list, l, r, top + r->main_h, r->curve_h, t0, t1, g);
		lrg_paint_list_pop_clip (list);
	}
}

/* paint_moving_clips: the dragged clips on top of every row */
static void
paint_moving_clips (LrgTimelineView  *self,
                    LrgPaintList *list,
                    const Looks  *l,
                    const Geom   *g)
{
	guint i;

	if (self->drag != DRAG_CLIPS)
		return;
	for (i = 0; i < self->origs->len; i++)
	{
		Orig  *o = &g_array_index (self->origs, Orig, i);
		Clip  *c = g_hash_table_lookup (self->clips, ID_KEY (o->id));
		gint   ri;
		Row   *r;
		gfloat pad;
		gfloat top;

		if (c == NULL || !c->moving)
			continue;
		ri = row_of_track (self, c->track);
		if (ri < 0)
			continue;
		r = &g_array_index (self->rows, Row, ri);
		pad = r->main_h > 22.0f ? 4.0f : 2.0f;
		top = row_top (self, r);
		paint_clip (self, list, l, c, lrg_timeline_view_time_to_x (self, c->start),
		            lrg_timeline_view_time_to_x (self, c->start + c->duration), top + pad,
		            r->main_h - pad * 2.0f, g->cx, g->cx + g->vw);
	}
}

/*
 * paint_header_row:
 *
 * One row of the header column: band, colour stripe, collapse chevron
 * (pointing right when collapsed), name, and the mute (speaker) and
 * lock toggles, bright when on.
 */
static void
paint_header_row (LrgTimelineView  *self,
                  LrgPaintList *list,
                  const Looks  *l,
                  const Row    *r,
                  gfloat        top,
                  const Geom   *g)
{
	LrgTimelineViewTrack *t = r->track;
	GrlColor          col;
	GrlColor          dim;
	GrlColor          sep;
	gfloat            indent = (gfloat) r->depth * INDENT;
	gfloat            hw = MIN (self->header_w, g->w);
	gfloat            mid = top + r->main_h * 0.5f;
	gfloat            ts = MIN (TOGGLE_SIZE, r->main_h - 2.0f);
	gfloat            lx = g->wx + hw - 6.0f - ts;
	gfloat            mx = lx - 4.0f - ts;
	gfloat            nx;

	lrg_timeline_view_track_get_color (t, &col);
	lrg_paint_color_with_alpha (&l->tick, 0.5f, &sep);
	lrg_paint_list_rect (list, g->wx, top + r->h - 1.0f, hw, 1.0f, 0.0f, &sep);

	/* Stripe and chevron */
	lrg_paint_list_rect (list, g->wx + 3.0f + indent, top + 3.0f, 3.0f, r->h - 6.0f, 1.5f, &col);
	lrg_paint_list_icon (list, t->collapsed ? LRG_ICON_CHEVRON_RIGHT : LRG_ICON_CHEVRON_DOWN,
	                     g->wx + 9.0f + indent, mid - ts * 0.5f, ts, 0.0f, &l->muted);

	/* Name, clipped before the toggles */
	nx = g->wx + 11.0f + indent + ts;
	if (t->name != NULL && mx - nx > 8.0f)
	{
		lrg_paint_list_push_clip (list, nx, top, mx - nx - 4.0f, r->main_h);
		lrg_paint_list_text (list, l->font, t->name, nx, roundf (mid - l->fs * 0.5f), l->fs, 0.0f,
		                     t->muted ? &l->muted : &l->text);
		lrg_paint_list_pop_clip (list);
	}

	if (mx < nx)
		return;

	/* Mute toggle: a speaker, crossed when muted */
	if (t->muted)
		dim = *lrg_theme_get_color (lrg_widget_get_theme (LRG_WIDGET (self)), LRG_THEME_COLOR_DANGER);
	else
		lrg_paint_color_with_alpha (&l->muted, 0.6f, &dim);
	lrg_paint_list_rect (list, mx + ts * 0.18f, mid - ts * 0.14f, ts * 0.18f, ts * 0.28f, 1.0f, &dim);
	lrg_paint_list_triangle (list, mx + ts * 0.30f, mid - ts * 0.14f, mx + ts * 0.55f, mid - ts * 0.34f,
	                         mx + ts * 0.55f, mid + ts * 0.34f, &dim);
	lrg_paint_list_triangle (list, mx + ts * 0.30f, mid - ts * 0.14f, mx + ts * 0.55f, mid + ts * 0.34f,
	                         mx + ts * 0.30f, mid + ts * 0.14f, &dim);
	if (t->muted)
		lrg_paint_list_icon (list, LRG_ICON_CROSS, mx + ts * 0.5f, mid - ts * 0.25f, ts * 0.5f, 0.0f, &dim);
	else
		lrg_paint_list_rect (list, mx + ts * 0.66f, mid - ts * 0.2f, 1.5f, ts * 0.4f, 0.75f, &dim);

	/* Lock toggle */
	if (t->locked)
		dim = *lrg_theme_get_color (lrg_widget_get_theme (LRG_WIDGET (self)), LRG_THEME_COLOR_WARNING);
	else
		lrg_paint_color_with_alpha (&l->muted, 0.6f, &dim);
	lrg_paint_list_icon (list, LRG_ICON_LOCK, lx, mid - ts * 0.5f, ts, 0.0f, &dim);
}

/*
 * paint_ruler:
 *
 * The ruler band: loop range tint, minor and major ticks with labels,
 * marker flags, and the playhead handle (a pill with a glow that grows
 * while hovered or scrubbed).
 */
static void
paint_ruler (LrgTimelineView  *self,
             LrgPaintList *list,
             const Looks  *l,
             const Geom   *g)
{
	gdouble per = 1.0 / pps (self);
	gdouble t0;
	gdouble t1;
	gdouble major;
	gdouble minor_step;
	guint   minor;
	gint64  k;
	gint64  k1;
	gfloat  rh = g->cy - g->wy;
	guint   i;

	lrg_timeline_view_get_visible_range (self, &t0, &t1);
	major = tick_step (self, per, &minor);
	minor_step = major / (gdouble) MAX (minor, 1u);

	lrg_paint_list_push_clip (list, g->cx, g->wy, g->vw, rh);

	/* Loop range */
	if (self->loop && self->loop_end > self->loop_start)
	{
		gfloat a = lrg_timeline_view_time_to_x (self, self->loop_start);
		gfloat b = lrg_timeline_view_time_to_x (self, self->loop_end);

		lrg_widget_paint_box (LRG_WIDGET (self), list, &l->loop, a, g->wy, b - a, rh);
		lrg_paint_list_rect (list, a, g->wy, 2.0f, rh, 0.0f, &l->accent);
		lrg_paint_list_rect (list, b - 2.0f, g->wy, 2.0f, rh, 0.0f, &l->accent);
	}

	/* Minor ticks, then major ticks with labels */
	k = (gint64) floor (MAX (0.0, t0) / minor_step);
	k1 = (gint64) ceil (t1 / minor_step);
	for (; k <= k1; k++)
	{
		gdouble t = (gdouble) k * minor_step;
		gfloat  x = roundf (lrg_timeline_view_time_to_x (self, t));
		gboolean is_major = minor <= 1 || k % (gint64) minor == 0;

		if (is_major)
		{
			g_autofree gchar *text = format_time (self, t, major);

			lrg_paint_list_rect (list, x, g->wy + rh * 0.45f, 1.0f, rh * 0.55f, 0.0f, &l->muted);
			lrg_paint_list_text (list, l->font, text, x + 4.0f, g->wy + 3.0f, l->fs * 0.9f, 0.0f, &l->muted);
		}
		else
		{
			lrg_paint_list_rect (list, x, g->wy + rh * 0.75f, 1.0f, rh * 0.25f, 0.0f, &l->tick);
		}
	}

	/* Marker flags */
	for (i = 0; i < self->markers->len; i++)
	{
		Marker  *m = g_ptr_array_index (self->markers, i);
		gfloat   x = lrg_timeline_view_time_to_x (self, m->time);
		gfloat   fw = 8.0f;
		gfloat   fh = MIN (16.0f, rh * 0.5f);
		GrlColor ink;

		if (m->label != NULL)
		{
			gfloat tw = 0.0f;

			lrg_paint_measure_text (l->font, m->label, l->fs * 0.85f, 0.0f, &tw, NULL);
			fw = tw + 10.0f;
		}
		if (x + fw < g->cx || x > g->cx + g->vw)
			continue;
		lrg_widget_paint_box (LRG_WIDGET (self), list, &l->marker, x, g->wy + rh - fh, fw, fh);
		lrg_paint_list_rect (list, x, g->wy + rh - fh, 2.0f, fh, 0.0f, &l->marker.background);
		if (m->label != NULL)
		{
			text_on (&l->marker.background, &ink);
			lrg_paint_list_text (list, l->font, m->label, x + 5.0f,
			                     roundf (g->wy + rh - fh + (fh - l->fs * 0.85f) * 0.5f),
			                     l->fs * 0.85f, 0.0f, &ink);
		}
	}

	/* Playhead handle */
	{
		gfloat     x = lrg_timeline_view_time_to_x (self, self->time);
		gfloat     hw = 12.0f;
		gfloat     hh = MAX (8.0f, rh * 0.55f);
		gfloat     glow = 0.35f + 0.65f * CLAMP (self->glow.value, 0.0f, 1.0f);
		LrgPartLook hl = l->indicator;

		if (x > g->cx - hw && x < g->cx + g->vw + hw)
		{
			hl.glow_size *= 0.6f + 0.8f * glow;
			hl.glow.a = (guint8) ((gfloat) hl.glow.a * glow);
			lrg_widget_paint_box (LRG_WIDGET (self), list, &hl, x - hw * 0.5f, g->wy + 2.0f, hw, hh);
			lrg_paint_list_triangle (list, x - hw * 0.5f, g->wy + 2.0f + hh * 0.7f, x + hw * 0.5f,
			                         g->wy + 2.0f + hh * 0.7f, x, g->wy + hh + 6.0f, &hl.background);
			lrg_paint_list_rect (list, x - 0.5f, g->wy + hh + 4.0f, 1.0f, rh - hh - 4.0f, 0.0f, &hl.background);
		}
	}

	lrg_paint_list_pop_clip (list);
}

/* paint_corner: the box above the headers: play button and time readout */
static void
paint_corner (LrgTimelineView  *self,
              LrgPaintList *list,
              const Looks  *l,
              const Geom   *g)
{
	gfloat rh = g->cy - g->wy;
	gfloat hw = g->cx - g->wx;
	gfloat bs = MIN (22.0f, rh - 8.0f);
	gfloat tx = g->wx + 8.0f;
	g_autofree gchar *now = NULL;

	if (hw <= 0.0f || rh <= 0.0f)
		return;

	lrg_paint_list_push_clip (list, g->wx, g->wy, hw, rh);
	if (self->show_play && bs > 6.0f)
	{
		LrgPartLook    look;
		LrgWidgetState st = self->playing ? LRG_WIDGET_STATE_CHECKED : LRG_WIDGET_STATE_NONE;

		if (self->hover_kind == HIT_PLAY)
			st |= LRG_WIDGET_STATE_HOVERED;
		lrg_widget_resolve_look_for_state (LRG_WIDGET (self), LRG_UI_PART_KNOB, st, &look);
		lrg_widget_paint_box (LRG_WIDGET (self), list, &look, tx, g->wy + (rh - bs) * 0.5f, bs, bs);
		lrg_paint_list_icon (list, self->playing ? LRG_ICON_PAUSE : LRG_ICON_PLAY,
		                     tx + bs * 0.2f, g->wy + (rh - bs) * 0.5f + bs * 0.2f, bs * 0.6f, 0.0f,
		                     &look.foreground);
		tx += bs + 8.0f;
	}

	/* Current time, in the ruler's format with fine precision */
	now = format_time (self, self->time, 0.01);
	lrg_paint_list_text (list, l->font, now, tx, roundf (g->wy + (rh - l->fs) * 0.5f), l->fs, 0.0f, &l->text);
	lrg_paint_list_pop_clip (list);
}

static void
lrg_timeline_view_paint (LrgWidget    *widget,
                    LrgPaintList *list)
{
	LrgTimelineView *self = LRG_TIMELINE_VIEW (widget);
	LrgPartLook  box;
	Looks        l;
	Geom         g;
	gdouble      t0;
	gdouble      t1;
	gfloat       off;
	guint        i;

	update_extents (self);
	geom (self, &g);
	resolve_looks (self, &l);

	/* 1. Body */
	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, g.wx, g.wy, g.w, g.h);
	lrg_paint_list_push_clip (list, g.wx, g.wy, g.w, g.h);

	/* 2. Header column and ruler bands */
	lrg_widget_paint_box (widget, list, &l.header, g.wx, g.cy, g.cx - g.wx, g.vh);
	lrg_widget_paint_box (widget, list, &l.ruler, g.wx, g.wy, g.w, g.cy - g.wy);

	lrg_timeline_view_get_visible_range (self, &t0, &t1);
	off = lrg_scroll_axis_get_value (&self->ay);

	/* 3. Time grid (looks), visible rows: lanes, clips, keyframes,
	 *    curves; then headers */
	lrg_paint_list_push_clip (list, g.cx, g.cy, g.vw, g.vh);
	paint_time_grid (self, list, &l, &g);
	for (i = 0; i < self->rows->len; i++)
	{
		Row   *r = &g_array_index (self->rows, Row, i);
		gfloat top = g.cy + r->y - off;

		if (top + r->h < g.cy)
			continue;
		if (top > g.cy + g.vh)
			break;
		paint_row (self, list, &l, r, i, top, t0, t1, &g);
	}

	/* 4. Loop range over the tracks, marker lines, dragged clips,
	 *    snap guide, playhead, rubber band */
	if (self->loop && self->loop_end > self->loop_start)
	{
		gfloat a = lrg_timeline_view_time_to_x (self, self->loop_start);
		gfloat b = lrg_timeline_view_time_to_x (self, self->loop_end);
		LrgPartLook dim = l.loop;

		dim.background.a = (guint8) ((gfloat) dim.background.a * 0.5f);
		lrg_widget_paint_box (widget, list, &dim, a, g.cy, b - a, g.vh);
	}
	for (i = 0; i < self->markers->len; i++)
	{
		Marker  *m = g_ptr_array_index (self->markers, i);
		gfloat   x = lrg_timeline_view_time_to_x (self, m->time);
		GrlColor mc;

		if (x < g.cx || x > g.cx + g.vw)
			continue;
		lrg_paint_color_with_alpha (&l.marker.background, 0.6f, &mc);
		lrg_paint_list_rect (list, x, g.cy, 1.0f, g.vh, 0.0f, &mc);
	}
	paint_moving_clips (self, list, &l, &g);
	if (self->snap_shown && self->drag != DRAG_NONE)
		paint_glow_line (list, &l.guide, lrg_timeline_view_time_to_x (self, self->snap_time), g.cy, g.cy + g.vh, 1.0f);
	paint_glow_line (list, &l.indicator, lrg_timeline_view_time_to_x (self, self->time), g.cy, g.cy + g.vh,
	                 0.5f + 0.5f * CLAMP (self->glow.value, 0.0f, 1.0f));
	if (self->drag == DRAG_BOX)
	{
		gfloat bx = MIN (self->box_x, self->box_x2);
		gfloat by = MIN (self->box_y, self->box_y2);

		lrg_widget_paint_box (widget, list, &l.veil, bx, by, fabsf (self->box_x2 - self->box_x),
		                      fabsf (self->box_y2 - self->box_y));
	}
	lrg_paint_list_pop_clip (list);

	/* 5. Header rows, clipped to the header column */
	lrg_paint_list_push_clip (list, g.wx, g.cy, g.cx - g.wx, g.vh);
	for (i = 0; i < self->rows->len; i++)
	{
		Row   *r = &g_array_index (self->rows, Row, i);
		gfloat top = g.cy + r->y - off;

		if (top + r->h < g.cy)
			continue;
		if (top > g.cy + g.vh)
			break;
		paint_header_row (self, list, &l, r, top, &g);
	}
	lrg_paint_list_pop_clip (list);

	/* 6. Ruler and corner */
	paint_ruler (self, list, &l, &g);
	paint_corner (self, list, &l, &g);

	/* Separators between the header / ruler and the tracks */
	lrg_paint_list_rect (list, g.cx - 1.0f, g.wy, 1.0f, g.h, 0.0f, &l.header.border);
	lrg_paint_list_rect (list, g.wx, g.cy - 1.0f, g.w, 1.0f, 0.0f, &l.header.border);

	/* 7. Scrollbars */
	lrg_scroll_bar_paint (widget, list, &self->ay, TRUE, self->fx.fade.value);
	lrg_scroll_bar_paint (widget, list, &self->ax, FALSE, self->fx.fade.value);

	lrg_paint_list_pop_clip (list);
}

/* ==========================================================================
 * Animation
 * ========================================================================== */

/*
 * lrg_timeline_view_step:
 *
 * Advances, in order: the canvas clock (double clicks); on the first
 * frame every spring snaps to its target; the zoom spring (while it
 * moves about an anchor the horizontal offset is derived from it, so
 * the anchored time stays under the pointer); the scroll axes; the lift,
 * hover and handle glow springs; playback; the scrollbar fade.
 */
static gboolean
lrg_timeline_view_step (LrgWidget *widget,
                   gfloat     delta_time)
{
	LrgTimelineView *self = LRG_TIMELINE_VIEW (widget);
	gboolean     moving = FALSE;
	gboolean     zmove;
	gboolean     keep;

	if (delta_time > 0.0f)
		self->clock += (gdouble) delta_time;
	if (self->ay.bar == NULL)
		return FALSE;

	if (!self->stepped)
	{
		self->stepped = TRUE;
		lrg_spring_snap (&self->zoom, self->zoom.target);
		lrg_spring_snap (&self->lift, self->lift.target);
		lrg_spring_snap (&self->hover, self->hover.target);
		lrg_spring_snap (&self->glow, self->glow.target);
		if (self->anchored)
		{
			apply_anchor (self);
			self->anchored = FALSE;
		}
	}

	/* Zoom (and the anchored offset) */
	zmove = lrg_spring_step (&self->zoom, delta_time);
	update_extents (self);
	if (self->anchored)
	{
		apply_anchor (self);
		if (!zmove)
			self->anchored = FALSE;
		moving |= zmove;
	}
	else
	{
		moving |= lrg_scroll_axis_step_snap (&self->ax, delta_time);
	}
	moving |= zmove;
	moving |= lrg_scroll_axis_step_snap (&self->ay, delta_time);
	if (moving)
		lrg_scroll_fx_poke (&self->fx);

	moving |= lrg_spring_step (&self->lift, delta_time);
	moving |= lrg_spring_step (&self->hover, delta_time);
	moving |= lrg_spring_step (&self->glow, delta_time);

	/* Playback keeps frames coming while it runs */
	if (self->playing)
	{
		advance_playback (self, (gdouble) delta_time);
		moving |= self->playing;
	}

	keep = self->ay.dragging || self->ax.dragging ||
	       (lrg_widget_get_state_flags (self->ay.bar) & LRG_WIDGET_STATE_HOVERED) ||
	       (lrg_widget_get_state_flags (self->ax.bar) & LRG_WIDGET_STATE_HOVERED);
	moving |= lrg_scroll_fx_update (&self->fx, keep, delta_time);
	return moving;
}

/* ==========================================================================
 * Hit testing
 * ========================================================================== */

/* marker_flag_hit: the marker whose ruler flag is under (@x, @y) */
static guint
marker_flag_hit (LrgTimelineView *self,
                 gfloat       x,
                 gfloat       y,
                 const Geom  *g)
{
	gfloat rh = g->cy - g->wy;
	gfloat fh = MIN (16.0f, rh * 0.5f);
	gfloat fs = font_size (self) * 0.85f;
	guint  i;

	if (y < g->wy + rh - fh || y > g->cy)
		return LRG_TIMELINE_VIEW_INVALID_ID;
	for (i = self->markers->len; i > 0; i--)
	{
		Marker *m = g_ptr_array_index (self->markers, i - 1);
		gfloat  mx = lrg_timeline_view_time_to_x (self, m->time);
		gfloat  fw = 8.0f;

		if (m->label != NULL)
		{
			gfloat tw = 0.0f;

			lrg_paint_measure_text (ui_font (self), m->label, fs, 0.0f, &tw, NULL);
			fw = tw + 10.0f;
		}
		if (x >= mx - 3.0f && x <= mx + fw)
			return m->id;
	}
	return LRG_TIMELINE_VIEW_INVALID_ID;
}

/*
 * hit_test:
 *
 * What is under a world point, front to back: scrollbar zones (by the
 * event target), the corner (play button), the header column (collapse
 * chevron, mute and lock toggles), the ruler (marker flags), then the
 * track area: curve lane points / curve, keyframe diamonds, clip edges
 * and bodies, and the empty lane.
 */
static Hit
hit_test (LrgTimelineView *self,
          gfloat       x,
          gfloat       y,
          LrgWidget   *target)
{
	Hit    hit = { HIT_NONE, -1, LRG_TIMELINE_VIEW_INVALID_ID };
	Geom   g;
	Row   *r;
	gfloat top;
	gfloat ts;
	gfloat mid;
	gdouble t;
	guint  i;
	guint  last;

	geom (self, &g);
	if (target != NULL && target == self->ay.bar)
	{
		hit.kind = HIT_VBAR;
		return hit;
	}
	if (target != NULL && target == self->ax.bar)
	{
		hit.kind = HIT_HBAR;
		return hit;
	}
	if (x < g.wx || y < g.wy || x >= g.wx + g.w || y >= g.wy + g.h)
		return hit;

	/* Corner */
	if (x < g.cx && y < g.cy)
	{
		gfloat rh = g.cy - g.wy;
		gfloat bs = MIN (22.0f, rh - 8.0f);

		hit.kind = HIT_CORNER;
		if (self->show_play && x >= g.wx + 8.0f && x <= g.wx + 8.0f + bs &&
		    y >= g.wy + (rh - bs) * 0.5f && y <= g.wy + (rh + bs) * 0.5f)
			hit.kind = HIT_PLAY;
		return hit;
	}

	/* Ruler */
	if (y < g.cy)
	{
		hit.id = marker_flag_hit (self, x, y, &g);
		hit.kind = hit.id != LRG_TIMELINE_VIEW_INVALID_ID ? HIT_MARKER : HIT_RULER;
		return hit;
	}

	hit.row = row_at (self, y);

	/* Header column */
	if (x < g.cx)
	{
		if (hit.row < 0)
		{
			hit.kind = HIT_NONE;
			return hit;
		}
		r = &g_array_index (self->rows, Row, hit.row);
		top = row_top (self, r);
		ts = MIN (TOGGLE_SIZE, r->main_h - 2.0f);
		mid = top + r->main_h * 0.5f;
		hit.kind = HIT_HEADER;
		if (fabsf (y - mid) <= ts * 0.5f + 2.0f)
		{
			gfloat hw = MIN (self->header_w, g.w);
			gfloat lx = g.wx + hw - 6.0f - ts;
			gfloat mx = lx - 4.0f - ts;
			gfloat cx0 = g.wx + 9.0f + (gfloat) r->depth * INDENT;

			if (x >= lx - 2.0f && x <= lx + ts + 2.0f)
				hit.kind = HIT_LOCK;
			else if (x >= mx - 2.0f && x <= mx + ts + 2.0f)
				hit.kind = HIT_MUTE;
			else if (x >= cx0 - 2.0f && x <= cx0 + ts + 2.0f)
				hit.kind = HIT_COLLAPSE;
		}
		return hit;
	}

	if (hit.row < 0)
	{
		hit.kind = HIT_EMPTY;
		return hit;
	}
	r = &g_array_index (self->rows, Row, hit.row);
	top = row_top (self, r);
	t = lrg_timeline_view_x_to_time (self, x);

	/* Curve lane: points, else the lane itself */
	if (r->curve_h > 0.0f && y >= top + r->main_h)
	{
		gfloat ly = top + r->main_h;
		gdouble reach = (gdouble) (CURVE_POINT + 3.0f) / pps (self);

		i = key_lower_bound (r->track->keys, t - reach);
		last = key_upper_bound (r->track->keys, t + reach);
		for (; i < last; i++)
		{
			Keyframe *k = g_ptr_array_index (r->track->keys, i);
			gfloat    kx = lrg_timeline_view_time_to_x (self, k->time);
			gfloat    ky = value_to_y (r->track, k->value, ly, r->curve_h);

			if ((x - kx) * (x - kx) + (y - ky) * (y - ky) <= (CURVE_POINT + 3.0f) * (CURVE_POINT + 3.0f))
			{
				hit.kind = HIT_CURVE_KEY;
				hit.id = k->id;
			}
		}
		if (hit.kind == HIT_NONE)
			hit.kind = HIT_CURVE;
		return hit;
	}

	/* Keyframe diamonds (drawn above clips, so hit first) */
	mid = top + r->main_h * 0.5f;
	{
		gfloat  half = MIN (KEY_HALF, r->main_h * 0.3f) + 2.0f;
		gdouble reach = (gdouble) half / pps (self);

		i = key_lower_bound (r->track->keys, t - reach);
		last = key_upper_bound (r->track->keys, t + reach);
		for (; i < last; i++)
		{
			Keyframe *k = g_ptr_array_index (r->track->keys, i);
			gfloat    kx = lrg_timeline_view_time_to_x (self, k->time);

			if (fabsf (x - kx) + fabsf (y - mid) <= half)
			{
				hit.kind = HIT_KEY;
				hit.id = k->id;
				return hit;
			}
		}
	}

	/* Clips: the topmost (last in the array) under the pointer */
	{
		LrgTimelineViewTrack *tr = r->track;
		gdouble           edge = (gdouble) EDGE_ZONE / pps (self);

		i = clip_lower_bound (tr->clips, t - edge - tr->max_duration);
		last = clip_upper_bound (tr->clips, t + edge);
		for (; i < last; i++)
		{
			Clip  *c = g_ptr_array_index (tr->clips, i);
			gfloat x0 = lrg_timeline_view_time_to_x (self, c->start);
			gfloat x1 = lrg_timeline_view_time_to_x (self, c->start + c->duration);
			gfloat zone = MIN (EDGE_ZONE, (x1 - x0) * 0.3f);

			if (x < x0 - 1.0f || x > x1 + 1.0f)
				continue;
			hit.id = c->id;
			if (x <= x0 + zone)
				hit.kind = HIT_CLIP_LEFT;
			else if (x >= x1 - zone)
				hit.kind = HIT_CLIP_RIGHT;
			else
				hit.kind = HIT_CLIP;
		}
		if (hit.kind != HIT_NONE)
			return hit;
	}

	hit.kind = HIT_LANE;
	return hit;
}

/* ==========================================================================
 * Drags
 * ========================================================================== */

static void
clear_origs (LrgTimelineView *self)
{
	guint i;

	for (i = 0; i < self->origs->len; i++)
	{
		Orig     *o = &g_array_index (self->origs, Orig, i);
		Clip     *c = g_hash_table_lookup (self->clips, ID_KEY (o->id));
		Keyframe *k = g_hash_table_lookup (self->keys, ID_KEY (o->id));

		if (c != NULL)
			c->moving = FALSE;
		if (k != NULL)
			k->moving = FALSE;
	}
	g_array_set_size (self->origs, 0);
}

/* begin_clip_drag: remember every selected clip on an unlocked track */
static void
begin_clip_drag (LrgTimelineView *self)
{
	GHashTableIter it;
	gpointer       key;

	clear_origs (self);
	g_hash_table_iter_init (&it, self->sel_clips);
	while (g_hash_table_iter_next (&it, &key, NULL))
	{
		Clip *c = g_hash_table_lookup (self->clips, key);
		Orig  o;

		if (c == NULL || c->track->locked)
			continue;
		o.id = c->id;
		o.track = c->track;
		o.start = c->start;
		o.length = c->duration;
		c->moving = TRUE;
		g_array_append_val (self->origs, o);
	}
	self->track_changed_drag = FALSE;
	lrg_spring_set_target (&self->lift, 1.0f);
	if (!smooth (self))
		lrg_spring_snap (&self->lift, 1.0f);
}

/* begin_key_drag: remember every selected keyframe on an unlocked track */
static void
begin_key_drag (LrgTimelineView *self)
{
	GHashTableIter it;
	gpointer       key;

	clear_origs (self);
	g_hash_table_iter_init (&it, self->sel_keys);
	while (g_hash_table_iter_next (&it, &key, NULL))
	{
		Keyframe *k = g_hash_table_lookup (self->keys, key);
		Orig      o;

		if (k == NULL || k->track->locked)
			continue;
		o.id = k->id;
		o.track = k->track;
		o.start = k->time;
		o.length = k->value;
		k->moving = TRUE;
		g_array_append_val (self->origs, o);
	}
}

static Orig *
find_orig (LrgTimelineView *self,
           guint        id)
{
	guint i;

	for (i = 0; i < self->origs->len; i++)
		if (g_array_index (self->origs, Orig, i).id == id)
			return &g_array_index (self->origs, Orig, i);
	return NULL;
}

/*
 * drag_clips:
 *
 * Moves every dragged clip by the pointer's time offset. The offset is
 * snapped so that either edge of the grabbed clip lands on a magnet
 * (whichever is nearer), or its start on the grid; it never takes a
 * clip below time 0. A single clip follows the pointer onto another
 * unlocked track. The move is all or nothing: if any clip would
 * overlap a resting clip, the previous position stays.
 */
static void
drag_clips (LrgTimelineView       *self,
            gfloat             x,
            gfloat             y,
            LrgInputModifiers  mods)
{
	Orig             *grab = find_orig (self, self->grab_id);
	gdouble           dt = lrg_timeline_view_x_to_time (self, x) - self->press_time;
	gdouble           min_start = G_MAXDOUBLE;
	LrgTimelineViewTrack *target = NULL;
	gboolean          ok = TRUE;
	gboolean          snapping = !(mods & LRG_INPUT_MODIFIER_ALT);
	guint             i;

	if (grab == NULL)
		return;

	/* 1. Snap the grabbed clip's edges */
	self->snap_shown = FALSE;
	if (snapping)
	{
		gdouble s = grab->start + dt;
		gdouble e = s + grab->length;
		gdouble fs = 0.0;
		gdouble fe = 0.0;
		gdouble ds = G_MAXDOUBLE;
		gdouble de = G_MAXDOUBLE;
		gboolean hs = find_magnet (self, s, TRUE, &fs, &ds);
		gboolean he = find_magnet (self, e, TRUE, &fe, &de);

		if (hs && (!he || ds <= de))
		{
			dt = fs - grab->start;
			self->snap_shown = TRUE;
			self->snap_time = fs;
		}
		else if (he)
		{
			dt = fe - grab->length - grab->start;
			self->snap_shown = TRUE;
			self->snap_time = fe;
		}
		else
		{
			dt = quantize (self, s) - grab->start;
		}
	}

	/* 2. Nothing before time 0 */
	for (i = 0; i < self->origs->len; i++)
		min_start = MIN (min_start, g_array_index (self->origs, Orig, i).start);
	if (min_start + dt < 0.0)
		dt = -min_start;

	/* 3. Track under the pointer (single clip only) */
	if (self->origs->len == 1)
	{
		gint ri = row_at (self, CLAMP (y, lrg_widget_get_world_y (LRG_WIDGET (self)) + self->ruler_h,
		                                  lrg_widget_get_world_y (LRG_WIDGET (self)) +
		                                  lrg_widget_get_height (LRG_WIDGET (self)) - 1.0f));

		if (ri >= 0)
		{
			LrgTimelineViewTrack *t = g_array_index (self->rows, Row, ri).track;

			if (!t->locked)
				target = t;
		}
	}

	/* 4. Validate, then apply */
	for (i = 0; i < self->origs->len && ok; i++)
	{
		Orig             *o = &g_array_index (self->origs, Orig, i);
		LrgTimelineViewTrack *t = target != NULL ? target : o->track;

		ok = track_fits (t, o->start + dt, o->length, NULL);
	}
	if (!ok && target != NULL)
	{
		/* Try staying on the current track instead */
		target = NULL;
		ok = TRUE;
		for (i = 0; i < self->origs->len && ok; i++)
		{
			Orig *o = &g_array_index (self->origs, Orig, i);
			Clip *c = g_hash_table_lookup (self->clips, ID_KEY (o->id));

			ok = c != NULL && track_fits (c->track, o->start + dt, o->length, NULL);
		}
		if (ok && self->origs->len == 1)
		{
			Clip *c = g_hash_table_lookup (self->clips, ID_KEY (grab->id));

			target = c != NULL ? c->track : NULL;
		}
	}
	if (!ok)
	{
		self->snap_shown = FALSE;
		return;
	}

	for (i = 0; i < self->origs->len; i++)
	{
		Orig *o = &g_array_index (self->origs, Orig, i);
		Clip *c = g_hash_table_lookup (self->clips, ID_KEY (o->id));

		if (c == NULL)
			continue;
		clip_steal (c);
		c->start = o->start + dt;
		if (target != NULL)
		{
			if (c->track != target)
				self->track_changed_drag = TRUE;
			c->track = target;
		}
		clip_insert (c);
	}
	queue (self);
}

/*
 * drag_resize:
 *
 * Moves one edge of the grabbed clip to the (snapped) pointer time,
 * keeping at least the minimum length, not before 0, and on tracks
 * without overlaps not into the neighbouring clips.
 */
static void
drag_resize (LrgTimelineView       *self,
             gfloat             x,
             LrgInputModifiers  mods)
{
	Clip   *c = g_hash_table_lookup (self->clips, ID_KEY (self->grab_id));
	Orig   *o = find_orig (self, self->grab_id);
	gdouble t;
	gdouble lo;
	gdouble hi;
	gdouble end;

	if (c == NULL || o == NULL)
		return;
	t = o->start + (lrg_timeline_view_x_to_time (self, x) - self->press_time);
	if (self->drag == DRAG_RESIZE_RIGHT)
		t += o->length;
	c->moving = FALSE;
	t = snap_time (self, t, TRUE, !(mods & LRG_INPUT_MODIFIER_ALT));
	c->moving = TRUE;

	/* neighbour_bounds() skips moving clips; compute for the original span */
	c->start = o->start;
	c->duration = o->length;
	c->moving = FALSE;
	neighbour_bounds (c, &lo, &hi);
	c->moving = TRUE;

	clip_steal (c);
	if (self->drag == DRAG_RESIZE_RIGHT)
	{
		end = CLAMP (t, o->start + min_clip (self), MAX (hi, o->start + min_clip (self)));
		c->start = o->start;
		c->duration = end - o->start;
	}
	else
	{
		end = o->start + o->length;
		t = CLAMP (t, MAX (0.0, lo), end - min_clip (self));
		c->start = t;
		c->duration = end - t;
	}
	clip_insert (c);
	if (self->snap_shown && fabs ((self->drag == DRAG_RESIZE_RIGHT ? c->start + c->duration : c->start) -
	                              self->snap_time) > 1e-9)
		self->snap_shown = FALSE;
	queue (self);
}

/*
 * drag_keys:
 *
 * Moves the dragged keyframes by the pointer's time offset (snapping
 * the grabbed one). A single keyframe grabbed in the curve lane also
 * follows the pointer's value.
 */
static void
drag_keys (LrgTimelineView       *self,
           gfloat             x,
           gfloat             y,
           LrgInputModifiers  mods)
{
	Orig    *grab = find_orig (self, self->grab_id);
	gdouble  dt = lrg_timeline_view_x_to_time (self, x) - self->press_time;
	gdouble  min_t = G_MAXDOUBLE;
	gdouble  t;
	guint    i;

	if (grab == NULL)
		return;
	t = snap_time (self, grab->start + dt, TRUE, !(mods & LRG_INPUT_MODIFIER_ALT));
	dt = t - grab->start;
	for (i = 0; i < self->origs->len; i++)
		min_t = MIN (min_t, g_array_index (self->origs, Orig, i).start);
	if (min_t + dt < 0.0)
		dt = -min_t;

	for (i = 0; i < self->origs->len; i++)
	{
		Orig     *o = &g_array_index (self->origs, Orig, i);
		Keyframe *k = g_hash_table_lookup (self->keys, ID_KEY (o->id));

		if (k == NULL)
			continue;
		key_steal (k);
		k->time = o->start + dt;
		if (self->grab_curve && self->origs->len == 1)
		{
			gint ri = row_of_track (self, k->track);

			if (ri >= 0)
			{
				Row   *r = &g_array_index (self->rows, Row, ri);
				gfloat ly = row_top (self, r) + r->main_h;

				k->value = y_to_value (k->track, y, ly, r->curve_h);
			}
		}
		key_insert (k);
	}
	queue (self);
}

/*
 * update_box:
 *
 * Selection while rubber-banding: the base selection (empty, or the
 * one from before the drag with Shift / Ctrl) plus every clip that
 * intersects the band and every keyframe (diamond or curve point)
 * inside it. Only rows under the band are searched.
 */
static void
update_box (LrgTimelineView *self)
{
	gfloat         bx0 = MIN (self->box_x, self->box_x2);
	gfloat         bx1 = MAX (self->box_x, self->box_x2);
	gfloat         by0 = MIN (self->box_y, self->box_y2);
	gfloat         by1 = MAX (self->box_y, self->box_y2);
	gdouble        t0 = lrg_timeline_view_x_to_time (self, bx0);
	gdouble        t1 = lrg_timeline_view_x_to_time (self, bx1);
	GHashTableIter it;
	gpointer       key;
	guint          i;

	clear_selection (self);
	g_hash_table_iter_init (&it, self->box_base);
	while (g_hash_table_iter_next (&it, &key, NULL))
	{
		guint id = GPOINTER_TO_UINT (key);

		if (id & KEY_TAG)
			g_hash_table_add (self->sel_keys, ID_KEY (id & ~KEY_TAG));
		else
			g_hash_table_add (self->sel_clips, key);
	}

	ensure_rows (self);
	for (i = 0; i < self->rows->len; i++)
	{
		Row              *r = &g_array_index (self->rows, Row, i);
		LrgTimelineViewTrack *t = r->track;
		gfloat            top = row_top (self, r);
		gfloat            mid = top + r->main_h * 0.5f;
		guint             j;
		guint             last;

		if (top + r->h < by0 || top > by1)
			continue;

		/* Clips overlap the band's time span and the clip lane */
		if (top + r->main_h >= by0 && top <= by1)
		{
			j = clip_lower_bound (t->clips, t0 - t->max_duration);
			last = clip_upper_bound (t->clips, t1);
			for (; j < last; j++)
			{
				Clip *c = g_ptr_array_index (t->clips, j);

				if (c->start + c->duration >= t0)
					g_hash_table_add (self->sel_clips, ID_KEY (c->id));
			}
		}

		/* Keyframes: the diamond centre or the curve point */
		j = key_lower_bound (t->keys, t0);
		last = key_upper_bound (t->keys, t1);
		for (; j < last; j++)
		{
			Keyframe *k = g_ptr_array_index (t->keys, j);
			gboolean  in = mid >= by0 && mid <= by1;

			if (!in && r->curve_h > 0.0f)
			{
				gfloat ky = value_to_y (t, k->value, top + r->main_h, r->curve_h);

				in = ky >= by0 && ky <= by1;
			}
			if (in)
				g_hash_table_add (self->sel_keys, ID_KEY (k->id));
		}
	}
	queue (self);
}

static Marker *lookup_marker (LrgTimelineView *self,
                              guint            id);

/*
 * begin_marker_drag:
 *
 * The press on a marker flag went past the drag threshold: remember
 * the marker's time (for Esc and for the marker-moved signal) and take
 * it out of the snap targets.
 */
static void
begin_marker_drag (LrgTimelineView *self)
{
	Marker *m = lookup_marker (self, self->grab_id);

	if (m == NULL)
	{
		self->drag = DRAG_NONE;
		return;
	}
	self->marker_orig = m->time;
	m->moving = TRUE;
	self->drag = DRAG_MARKER;
}

/*
 * drag_marker:
 *
 * Moves the grabbed marker by the pointer's time offset since the
 * press (so the flag keeps its grab point), snapped exactly like a
 * keyframe: to the playhead, other markers, clip edges and keyframes
 * in reach, else to the grid; Alt drags freely. The marker is clamped
 * to the sequence and the array re-sorted so it paints at the drag
 * time at once.
 */
static void
drag_marker (LrgTimelineView       *self,
             gfloat             x,
             LrgInputModifiers  mods)
{
	Marker  *m = lookup_marker (self, self->grab_id);
	gdouble  t;

	if (m == NULL)
		return;
	t = self->marker_orig + (lrg_timeline_view_x_to_time (self, x) - self->press_time);
	t = snap_time (self, CLAMP (t, 0.0, self->duration), TRUE, !(mods & LRG_INPUT_MODIFIER_ALT));
	t = CLAMP (t, 0.0, self->duration);
	if (t != m->time)
	{
		m->time = t;
		marker_sort (self);
	}
	queue (self);
}

/* scrub: playhead to the pointer time, snapped to markers / clip edges */
static void
scrub (LrgTimelineView       *self,
       gfloat             x,
       LrgInputModifiers  mods)
{
	gdouble t = lrg_timeline_view_x_to_time (self, x);

	t = snap_time (self, CLAMP (t, 0.0, self->duration), FALSE, !(mods & LRG_INPUT_MODIFIER_ALT));
	set_time_internal (self, t);
}

/*
 * finish_drag:
 *
 * Ends the gesture: emits one clip-moved / clip-resized /
 * keyframe-moved per item that really changed, or selection-changed
 * after a rubber band; a marker drag emits marker-moved if the time
 * changed, and a press on a flag that never became a drag activates
 * the marker (the playhead jumps to it). Drops the lift and the snap
 * guide.
 */
static void
finish_drag (LrgTimelineView *self)
{
	DragKind kind = self->drag;
	g_autoptr(GArray) moved = g_array_new (FALSE, FALSE, sizeof (guint));
	guint    i;
	Marker  *mk = NULL;

	/* Markers: read the dragged / clicked marker before the state goes */
	if (kind == DRAG_MARKER || kind == DRAG_PENDING_MARKER)
	{
		mk = lookup_marker (self, self->grab_id);
		if (mk != NULL)
			mk->moving = FALSE;
	}

	self->drag = DRAG_NONE;
	self->snap_shown = FALSE;
	self->ax.dragging = FALSE;
	self->ay.dragging = FALSE;
	lrg_spring_set_target (&self->lift, 0.0f);
	if (!smooth (self))
		lrg_spring_snap (&self->lift, 0.0f);

	/* Collect what changed before clearing the moving flags */
	for (i = 0; i < self->origs->len; i++)
	{
		Orig     *o = &g_array_index (self->origs, Orig, i);
		Clip     *c = g_hash_table_lookup (self->clips, ID_KEY (o->id));
		Keyframe *k = g_hash_table_lookup (self->keys, ID_KEY (o->id));

		if (c != NULL && (c->start != o->start || c->duration != o->length || c->track != o->track))
			g_array_append_val (moved, o->id);
		else if (k != NULL && (k->time != o->start || k->value != o->length))
			g_array_append_val (moved, o->id);
	}

	/* Dragged clips drop back into their rows */
	clear_origs (self);

	for (i = 0; i < moved->len; i++)
	{
		guint     id = g_array_index (moved, guint, i);
		Keyframe *k = g_hash_table_lookup (self->keys, ID_KEY (id));

		if (kind == DRAG_CLIPS)
			g_signal_emit (self, signals[SIGNAL_CLIP_MOVED], 0, id);
		else if (kind == DRAG_RESIZE_LEFT || kind == DRAG_RESIZE_RIGHT)
			g_signal_emit (self, signals[SIGNAL_CLIP_RESIZED], 0, id);
		else if (kind == DRAG_KEYS && k != NULL)
			g_signal_emit (self, signals[SIGNAL_KEYFRAME_MOVED], 0, k->track, id);
	}

	if (kind == DRAG_PENDING_MARKER && mk != NULL)
	{
		guint id = mk->id;

		set_time_internal (self, mk->time);
		g_signal_emit (self, signals[SIGNAL_MARKER_ACTIVATED], 0, id);
	}
	else if (kind == DRAG_MARKER && mk != NULL && mk->time != self->marker_orig)
	{
		g_signal_emit (self, signals[SIGNAL_MARKER_MOVED], 0, mk->id, self->marker_orig, mk->time);
	}

	if (kind == DRAG_BOX && self->box_base != NULL)
	{
		gboolean same;

		same = selection_equals (self, self->box_base);
		g_clear_pointer (&self->box_base, g_hash_table_unref);
		if (!same)
			selection_changed (self);
	}
	g_clear_pointer (&self->box_base, g_hash_table_unref);
	queue (self);
}

/* cancel_drag: Esc puts everything back where the gesture found it */
static void
cancel_drag (LrgTimelineView *self)
{
	guint i;

	for (i = 0; i < self->origs->len; i++)
	{
		Orig     *o = &g_array_index (self->origs, Orig, i);
		Clip     *c = g_hash_table_lookup (self->clips, ID_KEY (o->id));
		Keyframe *k = g_hash_table_lookup (self->keys, ID_KEY (o->id));

		if (c != NULL)
		{
			clip_steal (c);
			c->track = o->track;
			c->start = o->start;
			c->duration = o->length;
			clip_insert (c);
		}
		else if (k != NULL)
		{
			key_steal (k);
			k->time = o->start;
			k->value = o->length;
			key_insert (k);
		}
	}
	g_array_set_size (self->origs, 0);

	/* A dragged marker goes back to its time; a pending press on a
	 * flag is dropped without activating it. Either way no signal. */
	if (self->drag == DRAG_MARKER || self->drag == DRAG_PENDING_MARKER)
	{
		Marker *m = lookup_marker (self, self->grab_id);

		if (m != NULL)
		{
			if (self->drag == DRAG_MARKER)
				m->time = self->marker_orig;
			m->moving = FALSE;
			marker_sort (self);
		}
		self->drag = DRAG_NONE;
	}
	finish_drag (self);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/* is_double_click: a second press near the first within the time window */
static gboolean
is_double_click (LrgTimelineView *self,
                 gfloat       x,
                 gfloat       y)
{
	gboolean dbl = self->click_armed && self->clock - self->last_click_time <= DOUBLE_CLICK_TIME &&
	               fabsf (x - self->last_click_x) <= DOUBLE_CLICK_DIST &&
	               fabsf (y - self->last_click_y) <= DOUBLE_CLICK_DIST;

	self->click_armed = !dbl;
	self->last_click_time = self->clock;
	self->last_click_x = x;
	self->last_click_y = y;
	return dbl;
}

/*
 * click_select:
 *
 * Selection on a press over a clip or keyframe: Ctrl / Shift toggles
 * it in the selection; a plain press on an unselected item selects
 * only it (a press on a selected one keeps the selection so a group
 * can be dragged).
 */
static void
click_select (LrgTimelineView       *self,
              gboolean           is_key,
              guint              id,
              LrgInputModifiers  mods)
{
	GHashTable *set = is_key ? self->sel_keys : self->sel_clips;
	gboolean    was = g_hash_table_contains (set, ID_KEY (id));

	if (mods & (LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT))
	{
		if (was)
			g_hash_table_remove (set, ID_KEY (id));
		else
			g_hash_table_add (set, ID_KEY (id));
	}
	else if (was)
	{
		return;
	}
	else
	{
		clear_selection (self);
		g_hash_table_add (set, ID_KEY (id));
	}

	if (!is_key && g_hash_table_contains (set, ID_KEY (id)))
		g_signal_emit (self, signals[SIGNAL_CLIP_SELECTED], 0, id);
	selection_changed (self);
	queue (self);
}

static gboolean
handle_press (LrgTimelineView       *self,
              gfloat             x,
              gfloat             y,
              LrgWidget         *target,
              LrgInputModifiers  mods)
{
	Hit      hit = hit_test (self, x, y, target);
	gboolean dbl = is_double_click (self, x, y);
	Row     *r = hit.row >= 0 ? &g_array_index (self->rows, Row, hit.row) : NULL;
	gboolean locked = r != NULL && r->track->locked;

	self->press_x = x;
	self->press_y = y;
	self->press_time = lrg_timeline_view_x_to_time (self, x);
	self->grab_id = hit.id;
	self->grab_curve = FALSE;

	switch (hit.kind)
	{
	case HIT_VBAR:
		update_extents (self);
		lrg_scroll_bar_press (&self->ay, TRUE, y, smooth (self));
		self->drag = self->ay.dragging ? DRAG_VBAR : DRAG_NONE;
		lrg_scroll_fx_poke (&self->fx);
		return TRUE;

	case HIT_HBAR:
		update_extents (self);
		self->anchored = FALSE;
		lrg_scroll_bar_press (&self->ax, FALSE, x, smooth (self));
		self->drag = self->ax.dragging ? DRAG_HBAR : DRAG_NONE;
		lrg_scroll_fx_poke (&self->fx);
		return TRUE;

	case HIT_PLAY:
		lrg_timeline_view_set_playing (self, !self->playing);
		return TRUE;

	case HIT_COLLAPSE:
		lrg_timeline_view_track_set_collapsed (r->track, !r->track->collapsed);
		return TRUE;

	case HIT_MUTE:
		lrg_timeline_view_track_set_muted (r->track, !r->track->muted);
		return TRUE;

	case HIT_LOCK:
		lrg_timeline_view_track_set_locked (r->track, !r->track->locked);
		return TRUE;

	case HIT_HEADER:
		if (dbl)
			lrg_timeline_view_track_set_collapsed (r->track, !r->track->collapsed);
		return TRUE;

	case HIT_MARKER:
		/* The flag wins over the scrub the rest of the ruler starts:
		 * past the drag threshold it drags the marker, a release
		 * before that is a click that activates it (finish_drag) */
		self->drag = DRAG_PENDING_MARKER;
		return TRUE;

	case HIT_RULER:
		self->drag = DRAG_SCRUB;
		lrg_spring_set_target (&self->glow, 1.0f);
		scrub (self, x, mods);
		return TRUE;

	case HIT_KEY:
	case HIT_CURVE_KEY:
		click_select (self, TRUE, hit.id, mods);
		if (!locked && g_hash_table_contains (self->sel_keys, ID_KEY (hit.id)))
		{
			self->drag = DRAG_PENDING_KEY;
			self->grab_curve = hit.kind == HIT_CURVE_KEY;
		}
		return TRUE;

	case HIT_CLIP:
		click_select (self, FALSE, hit.id, mods);
		if (!locked && g_hash_table_contains (self->sel_clips, ID_KEY (hit.id)))
			self->drag = DRAG_PENDING_CLIP;
		return TRUE;

	case HIT_CLIP_LEFT:
	case HIT_CLIP_RIGHT:
		if (locked)
		{
			click_select (self, FALSE, hit.id, mods);
			return TRUE;
		}
		if (!g_hash_table_contains (self->sel_clips, ID_KEY (hit.id)) || g_hash_table_size (self->sel_clips) != 1)
		{
			clear_selection (self);
			g_hash_table_add (self->sel_clips, ID_KEY (hit.id));
			g_signal_emit (self, signals[SIGNAL_CLIP_SELECTED], 0, hit.id);
			selection_changed (self);
		}
		begin_clip_drag (self);
		lrg_spring_set_target (&self->lift, 0.0f);
		lrg_spring_snap (&self->lift, 0.0f);
		self->drag = hit.kind == HIT_CLIP_LEFT ? DRAG_RESIZE_LEFT : DRAG_RESIZE_RIGHT;
		queue (self);
		return TRUE;

	case HIT_CURVE:
		if (dbl && !locked)
		{
			gfloat  ly = row_top (self, r) + r->main_h;
			gdouble t = snap_time (self, MAX (0.0, self->press_time), TRUE, !(mods & LRG_INPUT_MODIFIER_ALT));
			guint   id;

			self->snap_shown = FALSE;
			id = lrg_timeline_view_add_keyframe (self, r->track, t, y_to_value (r->track, y, ly, r->curve_h),
			                                LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
			lrg_timeline_view_select_keyframe (self, id, FALSE);
			return TRUE;
		}
		/* fall through: a single press starts a rubber band */
		G_GNUC_FALLTHROUGH;

	case HIT_LANE:
	case HIT_EMPTY:
		self->box_base = (mods & (LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT))
		                 ? snapshot_selection (self)
		                 : g_hash_table_new (g_direct_hash, g_direct_equal);
		/* A plain press in empty space drops the selection at once */
		if (!(mods & (LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT)) && clear_selection (self))
			selection_changed (self);
		self->drag = DRAG_BOX;
		self->box_x = self->box_x2 = x;
		self->box_y = self->box_y2 = y;
		queue (self);
		return TRUE;

	case HIT_CORNER:
		return TRUE;

	case HIT_NONE:
	default:
		return FALSE;
	}
}

static gboolean
handle_move (LrgTimelineView       *self,
             gfloat             x,
             gfloat             y,
             LrgWidget         *target,
             LrgInputModifiers  mods)
{
	gboolean past = fabsf (x - self->press_x) > DRAG_THRESHOLD || fabsf (y - self->press_y) > DRAG_THRESHOLD;

	switch (self->drag)
	{
	case DRAG_PENDING_CLIP:
		if (!past)
			return TRUE;
		begin_clip_drag (self);
		self->drag = DRAG_CLIPS;
		drag_clips (self, x, y, mods);
		return TRUE;

	case DRAG_PENDING_KEY:
		if (!past)
			return TRUE;
		begin_key_drag (self);
		self->drag = DRAG_KEYS;
		drag_keys (self, x, y, mods);
		return TRUE;

	case DRAG_PENDING_MARKER:
		if (!past)
			return TRUE;
		begin_marker_drag (self);
		drag_marker (self, x, mods);
		return TRUE;

	case DRAG_MARKER:
		drag_marker (self, x, mods);
		return TRUE;

	case DRAG_CLIPS:
		drag_clips (self, x, y, mods);
		return TRUE;

	case DRAG_RESIZE_LEFT:
	case DRAG_RESIZE_RIGHT:
		drag_resize (self, x, mods);
		return TRUE;

	case DRAG_KEYS:
		drag_keys (self, x, y, mods);
		return TRUE;

	case DRAG_SCRUB:
		scrub (self, x, mods);
		return TRUE;

	case DRAG_BOX:
		self->box_x2 = x;
		self->box_y2 = y;
		update_box (self);
		return TRUE;

	case DRAG_VBAR:
		lrg_scroll_bar_drag (&self->ay, TRUE, y);
		lrg_scroll_fx_poke (&self->fx);
		return TRUE;

	case DRAG_HBAR:
		lrg_scroll_bar_drag (&self->ax, FALSE, x);
		lrg_scroll_fx_poke (&self->fx);
		return TRUE;

	case DRAG_NONE:
	default:
		{
			/* Hover: clip (and edge), play button, playhead handle */
			Hit    hit = hit_test (self, x, y, target);
			guint  clip = (hit.kind == HIT_CLIP || hit.kind == HIT_CLIP_LEFT || hit.kind == HIT_CLIP_RIGHT)
			              ? hit.id : LRG_TIMELINE_VIEW_INVALID_ID;
			gboolean hot = hit.kind == HIT_RULER &&
			               fabsf (x - lrg_timeline_view_time_to_x (self, self->time)) <= 10.0f;

			if (clip != self->hover_clip)
			{
				self->hover_clip = clip;
				lrg_spring_snap (&self->hover, 0.0f);
				lrg_spring_set_target (&self->hover, clip != LRG_TIMELINE_VIEW_INVALID_ID ? 1.0f : 0.0f);
			}
			if (hit.kind != self->hover_kind)
				queue (self);
			self->hover_kind = hit.kind;
			if (hot != self->handle_hot)
			{
				self->handle_hot = hot;
				lrg_spring_set_target (&self->glow, hot ? 1.0f : 0.0f);
			}
			return FALSE;
		}
	}
}

/* step_frames: Left / Right move the playhead a frame (or 1/10 s) */
static void
step_frames (LrgTimelineView *self,
             gint         n)
{
	gdouble step = self->fps > 0.0 ? 1.0 / self->fps : 0.1;
	gdouble t = self->fps > 0.0 ? floor (self->time * self->fps + 0.5) / self->fps : self->time;

	set_time_internal (self, t + step * (gdouble) n);
}

static gboolean
handle_key (LrgTimelineView       *self,
            GrlKey             key,
            LrgInputModifiers  mods)
{
	gboolean ctrl = (mods & LRG_INPUT_MODIFIER_CTRL) != 0;

	switch (key)
	{
	case GRL_KEY_DELETE:
	case GRL_KEY_BACKSPACE:
		/* consumed even with nothing selected (no focus move / back nav) */
		lrg_timeline_view_delete_selection (self);
		return TRUE;
	case GRL_KEY_LEFT:
		step_frames (self, (mods & LRG_INPUT_MODIFIER_SHIFT) ? -10 : -1);
		return TRUE;
	case GRL_KEY_RIGHT:
		step_frames (self, (mods & LRG_INPUT_MODIFIER_SHIFT) ? 10 : 1);
		return TRUE;
	case GRL_KEY_HOME:
		set_time_internal (self, 0.0);
		return TRUE;
	case GRL_KEY_END:
		set_time_internal (self, self->duration);
		return TRUE;
	case GRL_KEY_C:
		if (!ctrl)
			return FALSE;
		lrg_timeline_view_copy_selection (self);
		return TRUE;
	case GRL_KEY_V:
		if (!ctrl)
			return FALSE;
		lrg_timeline_view_paste (self, self->time);
		return TRUE;
	case GRL_KEY_A:
		if (!ctrl)
			return FALSE;
		{
			GHashTableIter it;
			gpointer       k;

			g_hash_table_iter_init (&it, self->clips);
			while (g_hash_table_iter_next (&it, &k, NULL))
				g_hash_table_add (self->sel_clips, k);
			g_hash_table_iter_init (&it, self->keys);
			while (g_hash_table_iter_next (&it, &k, NULL))
				g_hash_table_add (self->sel_keys, k);
			selection_changed (self);
			queue (self);
		}
		return TRUE;
	default:
		return FALSE;
	}
}

static gboolean
lrg_timeline_view_handle_event (LrgWidget        *widget,
                           const LrgUIEvent *event)
{
	LrgTimelineView       *self = LRG_TIMELINE_VIEW (widget);
	LrgWidget         *target = lrg_ui_event_get_target (event);
	LrgInputModifiers  mods = lrg_ui_event_get_modifiers (event);
	gfloat             x = lrg_ui_event_get_x (event);
	gfloat             y = lrg_ui_event_get_y (event);
	Geom               g;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_POINTER_ENTER:
		lrg_scroll_fx_poke (&self->fx);
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		if (self->drag == DRAG_NONE)
		{
			self->hover_clip = LRG_TIMELINE_VIEW_INVALID_ID;
			self->hover_kind = HIT_NONE;
			self->handle_hot = FALSE;
			lrg_spring_set_target (&self->hover, 0.0f);
			lrg_spring_set_target (&self->glow, 0.0f);
		}
		return FALSE;

	case LRG_UI_EVENT_MOUSE_MOVE:
		lrg_scroll_fx_poke (&self->fx);
		return handle_move (self, x, y, target, mods);

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		ensure_rows (self);
		update_extents (self);
		return handle_press (self, x, y, target, mods);

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0 || self->drag == DRAG_NONE)
			return FALSE;
		if (self->drag == DRAG_SCRUB)
			lrg_spring_set_target (&self->glow, self->handle_hot ? 1.0f : 0.0f);
		finish_drag (self);
		return TRUE;

	case LRG_UI_EVENT_CANCEL:
		if (self->drag == DRAG_NONE)
			return FALSE;
		cancel_drag (self);
		return TRUE;

	case LRG_UI_EVENT_SCROLL:
		{
			gfloat   sy = lrg_ui_event_get_scroll_y (event);
			gfloat   sx = lrg_ui_event_get_scroll_x (event);
			gboolean handled = FALSE;

			update_extents (self);
			geom (self, &g);
			if (mods & LRG_INPUT_MODIFIER_CTRL)
			{
				/* Zoom about the pointer (inside the track area) */
				gfloat ax = CLAMP (x, g.cx, g.cx + g.vw);

				lrg_timeline_view_zoom_to (self, (gdouble) self->zoom.target * pow (WHEEL_ZOOM, (gdouble) sy),
				                      ax, smooth (self));
				return TRUE;
			}
			if (sx != 0.0f)
			{
				self->anchored = FALSE;
				handled |= lrg_scroll_axis_wheel (&self->ax, -sx * LRG_SCROLL_WHEEL_STEP, smooth (self));
			}
			if (sy != 0.0f)
			{
				gboolean horizontal = (mods & LRG_INPUT_MODIFIER_SHIFT) ||
				                      lrg_scroll_axis_get_max (&self->ay) <= 0.0f;

				if (horizontal)
				{
					self->anchored = FALSE;
					handled |= lrg_scroll_axis_wheel (&self->ax, -sy * LRG_SCROLL_WHEEL_STEP, smooth (self));
				}
				else
				{
					handled |= lrg_scroll_axis_wheel (&self->ay, -sy * LRG_SCROLL_WHEEL_STEP, smooth (self));
				}
			}
			if (handled)
				lrg_scroll_fx_poke (&self->fx);
			return handled;
		}

	case LRG_UI_EVENT_ZOOM:
		geom (self, &g);
		lrg_timeline_view_zoom_to (self, (gdouble) self->zoom.target * (gdouble) lrg_ui_event_get_scale (event),
		                      CLAMP (x, g.cx, g.cx + g.vw), FALSE);
		return TRUE;

	case LRG_UI_EVENT_PAN:
		{
			gboolean a;
			gboolean b;

			if (self->drag != DRAG_NONE)
				return FALSE;
			update_extents (self);
			self->anchored = FALSE;
			a = lrg_scroll_axis_pan (&self->ax, lrg_ui_event_get_delta_x (event));
			b = lrg_scroll_axis_pan (&self->ay, lrg_ui_event_get_delta_y (event));
			if (a || b)
				lrg_scroll_fx_poke (&self->fx);
			return a || b;
		}

	case LRG_UI_EVENT_PAN_END:
		lrg_scroll_axis_flick (&self->ax, smooth (self) ? lrg_ui_event_get_velocity_x (event) : 0.0f);
		lrg_scroll_axis_flick (&self->ay, smooth (self) ? lrg_ui_event_get_velocity_y (event) : 0.0f);
		lrg_scroll_fx_poke (&self->fx);
		return TRUE;

	case LRG_UI_EVENT_KEY_DOWN:
		return handle_key (self, lrg_ui_event_get_key (event), mods);

	case LRG_UI_EVENT_ACTIVATE:
		lrg_timeline_view_set_playing (self, !self->playing);
		return TRUE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_timeline_view_constructed (GObject *object)
{
	LrgTimelineView *self = LRG_TIMELINE_VIEW (object);

	G_OBJECT_CLASS (lrg_timeline_view_parent_class)->constructed (object);

	self->in_layout = TRUE;
	self->ax.bar = lrg_scroll_bar_new ("hscrollbar");
	self->ay.bar = lrg_scroll_bar_new ("vscrollbar");
	lrg_container_add_child (LRG_CONTAINER (self), self->ax.bar);
	lrg_container_add_child (LRG_CONTAINER (self), self->ay.bar);
	self->in_layout = FALSE;
}

static void
detach_tracks (LrgTimelineView *self)
{
	guint i;

	for (i = 0; i < self->tracks->len; i++)
	{
		LrgTimelineViewTrack *t = g_ptr_array_index (self->tracks, i);

		t->timeline = NULL;
		t->parent = NULL;
	}
}

static void
lrg_timeline_view_dispose (GObject *object)
{
	LrgTimelineView *self = LRG_TIMELINE_VIEW (object);

	if (self->tracks != NULL)
	{
		detach_tracks (self);
		g_hash_table_remove_all (self->clips);
		g_hash_table_remove_all (self->keys);
		g_ptr_array_set_size (self->tracks, 0);
	}
	g_clear_object (&self->ax.bar);
	g_clear_object (&self->ay.bar);
	g_clear_pointer (&self->box_base, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_timeline_view_parent_class)->dispose (object);
}

static void
lrg_timeline_view_finalize (GObject *object)
{
	LrgTimelineView *self = LRG_TIMELINE_VIEW (object);

	g_clear_pointer (&self->tracks, g_ptr_array_unref);
	g_clear_pointer (&self->clips, g_hash_table_unref);
	g_clear_pointer (&self->keys, g_hash_table_unref);
	g_clear_pointer (&self->markers, g_ptr_array_unref);
	g_clear_pointer (&self->sel_clips, g_hash_table_unref);
	g_clear_pointer (&self->sel_keys, g_hash_table_unref);
	g_clear_pointer (&self->clipboard, g_array_unref);
	g_clear_pointer (&self->rows, g_array_unref);
	g_clear_pointer (&self->origs, g_array_unref);

	G_OBJECT_CLASS (lrg_timeline_view_parent_class)->finalize (object);
}

static void
lrg_timeline_view_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgTimelineView *self = LRG_TIMELINE_VIEW (object);

	switch (prop_id)
	{
	case PROP_TIME:
		g_value_set_double (value, self->time);
		break;
	case PROP_DURATION:
		g_value_set_double (value, self->duration);
		break;
	case PROP_ZOOM:
		g_value_set_double (value, lrg_timeline_view_get_zoom (self));
		break;
	case PROP_FPS:
		g_value_set_double (value, self->fps);
		break;
	case PROP_TIME_FORMAT:
		g_value_set_enum (value, self->format);
		break;
	case PROP_PLAYING:
		g_value_set_boolean (value, self->playing);
		break;
	case PROP_RATE:
		g_value_set_double (value, self->rate);
		break;
	case PROP_LOOP:
		g_value_set_boolean (value, self->loop);
		break;
	case PROP_LOOP_START:
		g_value_set_double (value, self->loop_start);
		break;
	case PROP_LOOP_END:
		g_value_set_double (value, self->loop_end);
		break;
	case PROP_FOLLOW_PLAYHEAD:
		g_value_set_boolean (value, self->follow);
		break;
	case PROP_SNAP:
		g_value_set_flags (value, self->snap);
		break;
	case PROP_SNAP_DISTANCE:
		g_value_set_float (value, self->snap_distance);
		break;
	case PROP_HEADER_WIDTH:
		g_value_set_float (value, self->header_w);
		break;
	case PROP_RULER_HEIGHT:
		g_value_set_float (value, self->ruler_h);
		break;
	case PROP_TRACK_HEIGHT:
		g_value_set_float (value, self->track_h);
		break;
	case PROP_SHOW_PLAY_BUTTON:
		g_value_set_boolean (value, self->show_play);
		break;
	case PROP_LOOK:
		g_value_set_enum (value, self->look);
		break;
	case PROP_GRID_STYLE:
		g_value_set_enum (value, self->grid_style);
		break;
	case PROP_KEY_SHAPE:
		g_value_set_enum (value, self->key_shape);
		break;
	case PROP_GLOW:
		g_value_set_float (value, self->glow_strength);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_timeline_view_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgTimelineView *self = LRG_TIMELINE_VIEW (object);

	switch (prop_id)
	{
	case PROP_TIME:
		lrg_timeline_view_set_time (self, g_value_get_double (value));
		break;
	case PROP_DURATION:
		lrg_timeline_view_set_duration (self, g_value_get_double (value));
		break;
	case PROP_ZOOM:
		lrg_timeline_view_set_zoom (self, g_value_get_double (value));
		break;
	case PROP_FPS:
		lrg_timeline_view_set_fps (self, g_value_get_double (value));
		break;
	case PROP_TIME_FORMAT:
		lrg_timeline_view_set_time_format (self, g_value_get_enum (value));
		break;
	case PROP_PLAYING:
		lrg_timeline_view_set_playing (self, g_value_get_boolean (value));
		break;
	case PROP_RATE:
		lrg_timeline_view_set_rate (self, g_value_get_double (value));
		break;
	case PROP_LOOP:
		lrg_timeline_view_set_loop (self, g_value_get_boolean (value));
		break;
	case PROP_LOOP_START:
		lrg_timeline_view_set_loop_range (self, g_value_get_double (value), self->loop_end);
		break;
	case PROP_LOOP_END:
		lrg_timeline_view_set_loop_range (self, self->loop_start, g_value_get_double (value));
		break;
	case PROP_FOLLOW_PLAYHEAD:
		lrg_timeline_view_set_follow_playhead (self, g_value_get_boolean (value));
		break;
	case PROP_SNAP:
		lrg_timeline_view_set_snap (self, g_value_get_flags (value));
		break;
	case PROP_SNAP_DISTANCE:
		lrg_timeline_view_set_snap_distance (self, g_value_get_float (value));
		break;
	case PROP_HEADER_WIDTH:
		lrg_timeline_view_set_header_width (self, g_value_get_float (value));
		break;
	case PROP_RULER_HEIGHT:
		lrg_timeline_view_set_ruler_height (self, g_value_get_float (value));
		break;
	case PROP_TRACK_HEIGHT:
		lrg_timeline_view_set_track_height (self, g_value_get_float (value));
		break;
	case PROP_SHOW_PLAY_BUTTON:
		lrg_timeline_view_set_show_play_button (self, g_value_get_boolean (value));
		break;
	case PROP_LOOK:
		lrg_timeline_view_set_look (self, (LrgCanvasLook) g_value_get_enum (value));
		break;
	case PROP_GRID_STYLE:
		lrg_timeline_view_set_grid_style (self, (LrgNodeGraphGrid) g_value_get_enum (value));
		break;
	case PROP_KEY_SHAPE:
		lrg_timeline_view_set_key_shape (self, (LrgCanvasKeyShape) g_value_get_enum (value));
		break;
	case PROP_GLOW:
		lrg_timeline_view_set_glow (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_timeline_view_class_init (LrgTimelineViewClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->constructed = lrg_timeline_view_constructed;
	object_class->dispose = lrg_timeline_view_dispose;
	object_class->finalize = lrg_timeline_view_finalize;
	object_class->get_property = lrg_timeline_view_get_property;
	object_class->set_property = lrg_timeline_view_set_property;

	widget_class->measure = lrg_timeline_view_measure;
	widget_class->paint = lrg_timeline_view_paint;
	widget_class->step = lrg_timeline_view_step;
	widget_class->style_defaults = lrg_timeline_view_style_defaults;
	widget_class->handle_event = lrg_timeline_view_handle_event;

	container_class->layout_children = lrg_timeline_view_layout_children;

	/**
	 * LrgTimelineView:time:
	 *
	 * Playhead time in seconds, within [0, #LrgTimelineView:duration].
	 */
	properties[PROP_TIME] =
		g_param_spec_double ("time", "Time", "Playhead time in seconds", 0.0, G_MAXDOUBLE, 0.0,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:duration:
	 *
	 * Length of the sequence in seconds.
	 */
	properties[PROP_DURATION] =
		g_param_spec_double ("duration", "Duration", "Sequence length in seconds", 0.001, G_MAXDOUBLE,
		                     DEFAULT_DURATION,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:zoom:
	 *
	 * Horizontal scale in canvas units per second (the target while the
	 * zoom spring moves).
	 */
	properties[PROP_ZOOM] =
		g_param_spec_double ("zoom", "Zoom", "Canvas units per second", MIN_ZOOM, MAX_ZOOM, DEFAULT_ZOOM,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:fps:
	 *
	 * Frames per second; 0 is continuous time.
	 */
	properties[PROP_FPS] =
		g_param_spec_double ("fps", "FPS", "Frames per second (0 = continuous)", 0.0, 1000.0, 0.0,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:time-format:
	 *
	 * How ruler labels print times.
	 */
	properties[PROP_TIME_FORMAT] =
		g_param_spec_enum ("time-format", "Time format", "How times are printed",
		                   LRG_TYPE_TIMELINE_VIEW_TIME_FORMAT, LRG_TIMELINE_VIEW_TIME_FORMAT_SECONDS,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:playing:
	 *
	 * The built-in playback runs (the playhead advances every frame).
	 */
	properties[PROP_PLAYING] =
		g_param_spec_boolean ("playing", "Playing", "Built-in playback runs", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:rate:
	 *
	 * Playback speed multiplier (negative plays backwards).
	 */
	properties[PROP_RATE] =
		g_param_spec_double ("rate", "Rate", "Playback speed", -1000.0, 1000.0, 1.0,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:loop:
	 *
	 * Playback wraps around the loop range instead of stopping.
	 */
	properties[PROP_LOOP] =
		g_param_spec_boolean ("loop", "Loop", "Playback loops", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:loop-start:
	 *
	 * Start of the loop range.
	 */
	properties[PROP_LOOP_START] =
		g_param_spec_double ("loop-start", "Loop start", "Start of the loop range", 0.0, G_MAXDOUBLE, 0.0,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:loop-end:
	 *
	 * End of the loop range; at or before the start loops the whole
	 * sequence.
	 */
	properties[PROP_LOOP_END] =
		g_param_spec_double ("loop-end", "Loop end", "End of the loop range", 0.0, G_MAXDOUBLE, 0.0,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:follow-playhead:
	 *
	 * The view scrolls to keep a moving playhead visible.
	 */
	properties[PROP_FOLLOW_PLAYHEAD] =
		g_param_spec_boolean ("follow-playhead", "Follow playhead", "Keep the playhead in view", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:snap:
	 *
	 * What drags snap to.
	 */
	properties[PROP_SNAP] =
		g_param_spec_flags ("snap", "Snap", "Snapping targets", LRG_TYPE_TIMELINE_VIEW_SNAP,
		                    LRG_TIMELINE_VIEW_SNAP_ALL,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:snap-distance:
	 *
	 * Magnet reach in canvas units.
	 */
	properties[PROP_SNAP_DISTANCE] =
		g_param_spec_float ("snap-distance", "Snap distance", "Magnet reach in canvas units",
		                    0.0f, 1000.0f, DEFAULT_SNAP_PX,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:header-width:
	 *
	 * Width of the track header column.
	 */
	properties[PROP_HEADER_WIDTH] =
		g_param_spec_float ("header-width", "Header width", "Width of the track header column",
		                    0.0f, G_MAXFLOAT, DEFAULT_HEADER_W,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:ruler-height:
	 *
	 * Height of the time ruler.
	 */
	properties[PROP_RULER_HEIGHT] =
		g_param_spec_float ("ruler-height", "Ruler height", "Height of the time ruler",
		                    0.0f, G_MAXFLOAT, DEFAULT_RULER_H,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:track-height:
	 *
	 * Clip lane height of tracks without their own; 0 derives it from
	 * the theme's ROW_HEIGHT.
	 */
	properties[PROP_TRACK_HEIGHT] =
		g_param_spec_float ("track-height", "Track height", "Default clip lane height (0 = theme)",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:show-play-button:
	 *
	 * Shows the play / pause button above the track headers.
	 */
	properties[PROP_SHOW_PLAY_BUTTON] =
		g_param_spec_boolean ("show-play-button", "Show play button", "Show the play / pause button", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:look:
	 *
	 * The visual preset (#LrgCanvasLook); Auto follows the theme.
	 */
	properties[PROP_LOOK] =
		g_param_spec_enum ("look", "Look", "Canvas look preset", LRG_TYPE_CANVAS_LOOK,
		                   LRG_CANVAS_LOOK_AUTO,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:grid-style:
	 *
	 * Time grid behind the lanes, at the ruler's ticks; Auto uses the
	 * look's (none for the Auto look).
	 */
	properties[PROP_GRID_STYLE] =
		g_param_spec_enum ("grid-style", "Grid style", "Time grid", LRG_TYPE_NODE_GRAPH_GRID,
		                   LRG_NODE_GRAPH_GRID_AUTO,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:key-shape:
	 *
	 * Shape of every keyframe without its own shape; Auto uses the
	 * look's.
	 */
	properties[PROP_KEY_SHAPE] =
		g_param_spec_enum ("key-shape", "Key shape", "Keyframe shape", LRG_TYPE_CANVAS_KEY_SHAPE,
		                   LRG_CANVAS_KEY_SHAPE_AUTO,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimelineView:glow:
	 *
	 * Strength of every glow: 0 off, 1 the look's, up to 3; negative
	 * (the default) follows the look.
	 */
	properties[PROP_GLOW] =
		g_param_spec_float ("glow", "Glow", "Glow strength", -1.0f, 3.0f, -1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgTimelineView::clip-added:
	 * @self: the timeline
	 * @clip: the new clip id
	 *
	 * Emitted when a clip was added (API or paste).
	 */
	signals[SIGNAL_CLIP_ADDED] =
		g_signal_new ("clip-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * LrgTimelineView::clip-removed:
	 * @self: the timeline
	 * @clip: the removed clip id (no longer valid)
	 *
	 * Emitted when a clip was removed (API or Delete).
	 */
	signals[SIGNAL_CLIP_REMOVED] =
		g_signal_new ("clip-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * LrgTimelineView::clip-moved:
	 * @self: the timeline
	 * @clip: the clip id
	 *
	 * Emitted when a clip moved: once per clip at the end of a drag, or
	 * by lrg_timeline_view_move_clip().
	 */
	signals[SIGNAL_CLIP_MOVED] =
		g_signal_new ("clip-moved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * LrgTimelineView::clip-resized:
	 * @self: the timeline
	 * @clip: the clip id
	 *
	 * Emitted when a clip's ends changed: at the end of an edge drag,
	 * or by lrg_timeline_view_resize_clip().
	 */
	signals[SIGNAL_CLIP_RESIZED] =
		g_signal_new ("clip-resized", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * LrgTimelineView::clip-selected:
	 * @self: the timeline
	 * @clip: the clip id
	 *
	 * Emitted when a click selected a clip.
	 */
	signals[SIGNAL_CLIP_SELECTED] =
		g_signal_new ("clip-selected", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * LrgTimelineView::keyframe-added:
	 * @self: the timeline
	 * @track: the track
	 * @keyframe: the new keyframe id
	 *
	 * Emitted when a keyframe was added (API or double click on a
	 * curve lane).
	 */
	signals[SIGNAL_KEYFRAME_ADDED] =
		g_signal_new ("keyframe-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, LRG_TYPE_TIMELINE_VIEW_TRACK, G_TYPE_UINT);

	/**
	 * LrgTimelineView::keyframe-removed:
	 * @self: the timeline
	 * @track: the track
	 * @keyframe: the removed keyframe id (no longer valid)
	 *
	 * Emitted when a keyframe was removed (API or Delete).
	 */
	signals[SIGNAL_KEYFRAME_REMOVED] =
		g_signal_new ("keyframe-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, LRG_TYPE_TIMELINE_VIEW_TRACK, G_TYPE_UINT);

	/**
	 * LrgTimelineView::keyframe-moved:
	 * @self: the timeline
	 * @track: the track
	 * @keyframe: the keyframe id
	 *
	 * Emitted when a keyframe's time or value changed: at the end of a
	 * drag, or by lrg_timeline_view_move_keyframe().
	 */
	signals[SIGNAL_KEYFRAME_MOVED] =
		g_signal_new ("keyframe-moved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, LRG_TYPE_TIMELINE_VIEW_TRACK, G_TYPE_UINT);

	/**
	 * LrgTimelineView::playhead-moved:
	 * @self: the timeline
	 * @time: the new playhead time
	 *
	 * Emitted whenever the playhead moves: scrubbing, playback, keys,
	 * markers or lrg_timeline_view_set_time().
	 */
	signals[SIGNAL_PLAYHEAD_MOVED] =
		g_signal_new ("playhead-moved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_DOUBLE);

	/**
	 * LrgTimelineView::marker-activated:
	 * @self: the timeline
	 * @marker: the marker id
	 *
	 * Emitted when a marker flag was clicked: pressed and released
	 * without dragging it (the playhead jumps to it first).
	 */
	signals[SIGNAL_MARKER_ACTIVATED] =
		g_signal_new ("marker-activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * LrgTimelineView::marker-moved:
	 * @self: the timeline
	 * @marker: the marker id
	 * @old_time: the marker's time before the drag, in seconds
	 * @new_time: the marker's time after the drag, in seconds
	 *
	 * Emitted once when a marker drag on the ruler is released and the
	 * marker's time changed (never for Esc, which puts it back, nor
	 * for lrg_timeline_view_move_marker()). The marker already sits at
	 * @new_time; an undo stack can record (@marker, @old_time,
	 * @new_time) and revert with lrg_timeline_view_move_marker().
	 */
	signals[SIGNAL_MARKER_MOVED] =
		g_signal_new ("marker-moved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_DOUBLE, G_TYPE_DOUBLE);

	/**
	 * LrgTimelineView::selection-changed:
	 * @self: the timeline
	 *
	 * Emitted when clips or keyframes were selected or unselected.
	 */
	signals[SIGNAL_SELECTION_CHANGED] =
		g_signal_new ("selection-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_timeline_view_init (LrgTimelineView *self)
{
	self->tracks = g_ptr_array_new_with_free_func (g_object_unref);
	self->clips = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->keys = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->markers = g_ptr_array_new_with_free_func (marker_free);
	self->sel_clips = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->sel_keys = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->clipboard = g_array_new (FALSE, TRUE, sizeof (Copied));
	g_array_set_clear_func (self->clipboard, copied_clear);
	self->rows = g_array_new (FALSE, TRUE, sizeof (Row));
	self->origs = g_array_new (FALSE, TRUE, sizeof (Orig));
	self->rows_dirty = TRUE;
	self->next_id = 1;

	self->duration = DEFAULT_DURATION;
	self->rate = 1.0;
	self->follow = TRUE;
	self->show_play = TRUE;
	self->format = LRG_TIMELINE_VIEW_TIME_FORMAT_SECONDS;
	self->snap = LRG_TIMELINE_VIEW_SNAP_ALL;
	self->snap_distance = DEFAULT_SNAP_PX;
	self->header_w = DEFAULT_HEADER_W;
	self->ruler_h = DEFAULT_RULER_H;
	self->grid_style = LRG_NODE_GRAPH_GRID_AUTO;
	self->glow_strength = -1.0f;

	lrg_spring_init (&self->zoom, (gfloat) DEFAULT_ZOOM, LRG_MOTION_SMOOTH);
	lrg_spring_init (&self->lift, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->hover, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->glow, 0.0f, LRG_MOTION_SMOOTH);
	lrg_scroll_axis_init (&self->ax);
	lrg_scroll_axis_init (&self->ay);
	lrg_scroll_fx_init (&self->fx);

	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API: tracks
 * ========================================================================== */

LrgTimelineView *
lrg_timeline_view_new (void)
{
	return g_object_new (LRG_TYPE_TIMELINE_VIEW, NULL);
}

LrgTimelineViewTrack *
lrg_timeline_view_add_track (LrgTimelineView      *self,
                        const gchar      *name,
                        LrgTimelineViewTrack *parent)
{
	g_autoptr(LrgTimelineViewTrack) track = NULL;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), NULL);

	track = lrg_timeline_view_track_new (name);
	lrg_timeline_view_append_track (self, track, parent);
	return track->timeline == self ? track : NULL;
}

void
lrg_timeline_view_append_track (LrgTimelineView      *self,
                           LrgTimelineViewTrack *track,
                           LrgTimelineViewTrack *parent)
{
	guint index;
	guint i;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));
	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (track));
	g_return_if_fail (track->timeline == NULL);
	g_return_if_fail (parent == NULL || (LRG_IS_TIMELINE_VIEW_TRACK (parent) && parent->timeline == self));

	/* After the last descendant of @parent, or at the end */
	index = self->tracks->len;
	if (parent != NULL && g_ptr_array_find (self->tracks, parent, &i))
	{
		index = i + 1;
		while (index < self->tracks->len &&
		       is_descendant (g_ptr_array_index (self->tracks, index), parent))
			index++;
	}

	track->timeline = self;
	track->parent = parent;
	if (!track->has_color)
		track->palette = self->next_palette;
	self->next_palette++;
	g_ptr_array_insert (self->tracks, (gint) index, g_object_ref (track));

	/* Clips / keyframes added to the track before it was attached */
	for (i = 0; i < track->clips->len; i++)
	{
		Clip *c = g_ptr_array_index (track->clips, i);

		c->id = self->next_id++;
		g_hash_table_insert (self->clips, ID_KEY (c->id), c);
	}
	for (i = 0; i < track->keys->len; i++)
	{
		Keyframe *k = g_ptr_array_index (track->keys, i);

		k->id = self->next_id++;
		g_hash_table_insert (self->keys, ID_KEY (k->id), k);
	}

	self->rows_dirty = TRUE;
	queue (self);
	if (parent != NULL)
		g_object_notify_by_pspec (G_OBJECT (track), track_props[TRACK_PROP_PARENT]);
}

/* detach_track: forget one track's clips, keyframes and selection */
static void
detach_track (LrgTimelineView      *self,
              LrgTimelineViewTrack *t)
{
	guint i;

	for (i = 0; i < t->clips->len; i++)
	{
		guint id = ((Clip *) g_ptr_array_index (t->clips, i))->id;

		g_hash_table_remove (self->clips, ID_KEY (id));
		g_hash_table_remove (self->sel_clips, ID_KEY (id));
	}
	for (i = 0; i < t->keys->len; i++)
	{
		guint id = ((Keyframe *) g_ptr_array_index (t->keys, i))->id;

		g_hash_table_remove (self->keys, ID_KEY (id));
		g_hash_table_remove (self->sel_keys, ID_KEY (id));
	}
	for (i = 0; i < self->clipboard->len; i++)
		if (g_array_index (self->clipboard, Copied, i).track == t)
			g_array_index (self->clipboard, Copied, i).track = NULL;
	t->timeline = NULL;
	t->parent = NULL;
}

void
lrg_timeline_view_remove_track (LrgTimelineView      *self,
                           LrgTimelineViewTrack *track)
{
	guint index;
	guint end;
	guint i;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));
	g_return_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (track));

	if (track->timeline != self || !g_ptr_array_find (self->tracks, track, &index))
		return;
	if (self->drag != DRAG_NONE)
		cancel_drag (self);

	/* The track and its descendants are contiguous */
	end = index + 1;
	while (end < self->tracks->len && is_descendant (g_ptr_array_index (self->tracks, end), track))
		end++;
	for (i = end; i > index; i--)
		detach_track (self, g_ptr_array_index (self->tracks, i - 1));
	g_ptr_array_remove_range (self->tracks, index, end - index);

	self->hover_clip = LRG_TIMELINE_VIEW_INVALID_ID;
	self->rows_dirty = TRUE;
	queue (self);
}

guint
lrg_timeline_view_get_n_tracks (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0);
	return self->tracks->len;
}

LrgTimelineViewTrack *
lrg_timeline_view_get_track (LrgTimelineView *self,
                        guint        index)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), NULL);
	return index < self->tracks->len ? g_ptr_array_index (self->tracks, index) : NULL;
}

LrgTimelineViewTrack *
lrg_timeline_view_find_track (LrgTimelineView *self,
                         const gchar *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), NULL);

	for (i = 0; i < self->tracks->len; i++)
	{
		LrgTimelineViewTrack *t = g_ptr_array_index (self->tracks, i);

		if (g_strcmp0 (t->name, name) == 0)
			return t;
	}
	return NULL;
}

gboolean
lrg_timeline_view_get_track_row (LrgTimelineView      *self,
                            LrgTimelineViewTrack *track,
                            gfloat           *y,
                            gfloat           *height)
{
	gint ri;
	Row *r;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (track), FALSE);

	update_extents (self);
	ri = row_of_track (self, track);
	if (ri < 0)
		return FALSE;
	r = &g_array_index (self->rows, Row, ri);
	if (y != NULL)
		*y = row_top (self, r);
	if (height != NULL)
		*height = r->h;
	return TRUE;
}

/* ==========================================================================
 * Public API: clips
 * ========================================================================== */

static Clip *
lookup_clip (LrgTimelineView *self,
             guint        id)
{
	return g_hash_table_lookup (self->clips, ID_KEY (id));
}

guint
lrg_timeline_view_add_clip (LrgTimelineView      *self,
                       LrgTimelineViewTrack *track,
                       gdouble           start,
                       gdouble           duration,
                       const gchar      *label)
{
	Clip *c;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), LRG_TIMELINE_VIEW_INVALID_ID);
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (track) && track->timeline == self, LRG_TIMELINE_VIEW_INVALID_ID);

	start = MAX (0.0, start);
	duration = MAX (duration, min_clip (self));
	if (!track_fits (track, start, duration, NULL))
		return LRG_TIMELINE_VIEW_INVALID_ID;

	c = g_new0 (Clip, 1);
	c->id = self->next_id++;
	c->track = track;
	c->start = start;
	c->duration = duration;
	c->label = g_strdup (label);
	clip_insert (c);
	g_hash_table_insert (self->clips, ID_KEY (c->id), c);

	queue (self);
	g_signal_emit (self, signals[SIGNAL_CLIP_ADDED], 0, c->id);
	return c->id;
}

gboolean
lrg_timeline_view_remove_clip (LrgTimelineView *self,
                          guint        clip)
{
	Clip *c;
	guint id = clip;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);

	c = lookup_clip (self, clip);
	if (c == NULL)
		return FALSE;
	g_hash_table_remove (self->clips, ID_KEY (id));
	if (g_hash_table_remove (self->sel_clips, ID_KEY (id)))
		selection_changed (self);
	if (self->hover_clip == id)
		self->hover_clip = LRG_TIMELINE_VIEW_INVALID_ID;
	g_ptr_array_remove (c->track->clips, c);   /* frees it */
	queue (self);
	g_signal_emit (self, signals[SIGNAL_CLIP_REMOVED], 0, id);
	return TRUE;
}

gboolean
lrg_timeline_view_move_clip (LrgTimelineView      *self,
                        guint             clip,
                        LrgTimelineViewTrack *track,
                        gdouble           start)
{
	Clip *c;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);
	g_return_val_if_fail (track == NULL || (LRG_IS_TIMELINE_VIEW_TRACK (track) && track->timeline == self), FALSE);

	c = lookup_clip (self, clip);
	if (c == NULL)
		return FALSE;
	if (track == NULL)
		track = c->track;
	start = MAX (0.0, start);
	if (!track_fits (track, start, c->duration, c))
		return FALSE;

	clip_steal (c);
	c->track = track;
	c->start = start;
	clip_insert (c);
	queue (self);
	g_signal_emit (self, signals[SIGNAL_CLIP_MOVED], 0, clip);
	return TRUE;
}

gboolean
lrg_timeline_view_resize_clip (LrgTimelineView *self,
                          guint        clip,
                          gdouble      start,
                          gdouble      duration)
{
	Clip   *c;
	gdouble lo;
	gdouble hi;
	gdouble end;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);

	c = lookup_clip (self, clip);
	if (c == NULL)
		return FALSE;

	/* Room between the neighbours of the current span */
	neighbour_bounds (c, &lo, &hi);
	start = MAX (start, MAX (0.0, lo));
	end = start + MAX (duration, min_clip (self));
	end = MIN (end, hi);
	if (end - start < min_clip (self))
	{
		/* squeezed against the right neighbour: keep the minimum length */
		start = MAX (lo, end - min_clip (self));
		end = start + min_clip (self);
	}

	clip_steal (c);
	c->start = start;
	c->duration = end - start;
	clip_insert (c);
	queue (self);
	g_signal_emit (self, signals[SIGNAL_CLIP_RESIZED], 0, clip);
	return TRUE;
}

gdouble
lrg_timeline_view_get_clip_start (LrgTimelineView *self,
                             guint        clip)
{
	Clip *c;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), -1.0);
	c = lookup_clip (self, clip);
	return c != NULL ? c->start : -1.0;
}

gdouble
lrg_timeline_view_get_clip_duration (LrgTimelineView *self,
                                guint        clip)
{
	Clip *c;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0.0);
	c = lookup_clip (self, clip);
	return c != NULL ? c->duration : 0.0;
}

LrgTimelineViewTrack *
lrg_timeline_view_get_clip_track (LrgTimelineView *self,
                             guint        clip)
{
	Clip *c;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), NULL);
	c = lookup_clip (self, clip);
	return c != NULL ? c->track : NULL;
}

const gchar *
lrg_timeline_view_get_clip_label (LrgTimelineView *self,
                             guint        clip)
{
	Clip *c;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), NULL);
	c = lookup_clip (self, clip);
	return c != NULL ? c->label : NULL;
}

void
lrg_timeline_view_set_clip_label (LrgTimelineView *self,
                             guint        clip,
                             const gchar *label)
{
	Clip *c;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));
	c = lookup_clip (self, clip);
	if (c == NULL)
		return;
	g_free (c->label);
	c->label = g_strdup (label);
	queue (self);
}

void
lrg_timeline_view_set_clip_color (LrgTimelineView    *self,
                             guint           clip,
                             const GrlColor *color)
{
	Clip *c;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));
	c = lookup_clip (self, clip);
	if (c == NULL)
		return;
	c->has_color = color != NULL;
	if (color != NULL)
		c->color = *color;
	queue (self);
}

gboolean
lrg_timeline_view_get_clip_color (LrgTimelineView *self,
                                  guint            clip,
                                  GrlColor        *color)
{
	Clip *c;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);
	c = lookup_clip (self, clip);
	if (c == NULL || !c->has_color)
		return FALSE;
	if (color != NULL)
		*color = c->color;
	return TRUE;
}

guint
lrg_timeline_view_get_n_clips (LrgTimelineView      *self,
                          LrgTimelineViewTrack *track)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0);

	if (track == NULL)
		return g_hash_table_size (self->clips);
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (track), 0);
	return track->clips->len;
}

guint
lrg_timeline_view_get_clip_at (LrgTimelineView      *self,
                          LrgTimelineViewTrack *track,
                          gdouble           time)
{
	guint i;
	guint last;
	guint found = LRG_TIMELINE_VIEW_INVALID_ID;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), LRG_TIMELINE_VIEW_INVALID_ID);
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (track), LRG_TIMELINE_VIEW_INVALID_ID);

	i = clip_lower_bound (track->clips, time - track->max_duration);
	last = clip_upper_bound (track->clips, time);
	for (; i < last; i++)
	{
		Clip *c = g_ptr_array_index (track->clips, i);

		if (time >= c->start && time < c->start + c->duration)
			found = c->id;
	}
	return found;
}

/* ==========================================================================
 * Public API: keyframes
 * ========================================================================== */

static Keyframe *
lookup_key (LrgTimelineView *self,
            guint        id)
{
	return g_hash_table_lookup (self->keys, ID_KEY (id));
}

guint
lrg_timeline_view_add_keyframe (LrgTimelineView              *self,
                           LrgTimelineViewTrack         *track,
                           gdouble                   time,
                           gdouble                   value,
                           LrgTimelineViewInterpolation  interpolation)
{
	Keyframe *k;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), LRG_TIMELINE_VIEW_INVALID_ID);
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (track) && track->timeline == self, LRG_TIMELINE_VIEW_INVALID_ID);

	k = g_new0 (Keyframe, 1);
	k->id = self->next_id++;
	k->track = track;
	k->time = MAX (0.0, time);
	k->value = value;
	k->interp = interpolation;
	key_insert (k);
	g_hash_table_insert (self->keys, ID_KEY (k->id), k);

	queue (self);
	g_signal_emit (self, signals[SIGNAL_KEYFRAME_ADDED], 0, track, k->id);
	return k->id;
}

gboolean
lrg_timeline_view_remove_keyframe (LrgTimelineView *self,
                              guint        keyframe)
{
	g_autoptr(LrgTimelineViewTrack) track = NULL;
	Keyframe *k;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);

	k = lookup_key (self, keyframe);
	if (k == NULL)
		return FALSE;
	track = g_object_ref (k->track);
	g_hash_table_remove (self->keys, ID_KEY (keyframe));
	if (g_hash_table_remove (self->sel_keys, ID_KEY (keyframe)))
		selection_changed (self);
	g_ptr_array_remove (track->keys, k);   /* frees it */
	queue (self);
	g_signal_emit (self, signals[SIGNAL_KEYFRAME_REMOVED], 0, track, keyframe);
	return TRUE;
}

gboolean
lrg_timeline_view_move_keyframe (LrgTimelineView *self,
                            guint        keyframe,
                            gdouble      time,
                            gdouble      value)
{
	Keyframe *k;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);

	k = lookup_key (self, keyframe);
	if (k == NULL)
		return FALSE;
	key_steal (k);
	k->time = MAX (0.0, time);
	k->value = value;
	key_insert (k);
	queue (self);
	g_signal_emit (self, signals[SIGNAL_KEYFRAME_MOVED], 0, k->track, keyframe);
	return TRUE;
}

gdouble
lrg_timeline_view_get_keyframe_time (LrgTimelineView *self,
                                guint        keyframe)
{
	Keyframe *k;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), -1.0);
	k = lookup_key (self, keyframe);
	return k != NULL ? k->time : -1.0;
}

gdouble
lrg_timeline_view_get_keyframe_value (LrgTimelineView *self,
                                 guint        keyframe)
{
	Keyframe *k;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0.0);
	k = lookup_key (self, keyframe);
	return k != NULL ? k->value : 0.0;
}

LrgTimelineViewTrack *
lrg_timeline_view_get_keyframe_track (LrgTimelineView *self,
                                 guint        keyframe)
{
	Keyframe *k;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), NULL);
	k = lookup_key (self, keyframe);
	return k != NULL ? k->track : NULL;
}

LrgTimelineViewInterpolation
lrg_timeline_view_get_keyframe_interpolation (LrgTimelineView *self,
                                         guint        keyframe)
{
	Keyframe *k;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
	k = lookup_key (self, keyframe);
	return k != NULL ? k->interp : LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR;
}

void
lrg_timeline_view_set_keyframe_interpolation (LrgTimelineView              *self,
                                         guint                     keyframe,
                                         LrgTimelineViewInterpolation  interpolation)
{
	Keyframe *k;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));
	k = lookup_key (self, keyframe);
	if (k == NULL)
		return;
	k->interp = interpolation;
	queue (self);
}

LrgCanvasKeyShape
lrg_timeline_view_get_keyframe_shape (LrgTimelineView *self,
                                      guint            keyframe)
{
	Keyframe *k;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), LRG_CANVAS_KEY_SHAPE_AUTO);
	k = lookup_key (self, keyframe);
	return k != NULL ? k->shape : LRG_CANVAS_KEY_SHAPE_AUTO;
}

void
lrg_timeline_view_set_keyframe_shape (LrgTimelineView   *self,
                                      guint              keyframe,
                                      LrgCanvasKeyShape  shape)
{
	Keyframe *k;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));
	g_return_if_fail (shape <= LRG_CANVAS_KEY_SHAPE_BAR);
	k = lookup_key (self, keyframe);
	if (k == NULL)
		return;
	k->shape = shape;
	queue (self);
}

void
lrg_timeline_view_set_keyframe_tangents (LrgTimelineView *self,
                                    guint        keyframe,
                                    gdouble      in_slope,
                                    gdouble      out_slope)
{
	Keyframe *k;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));
	k = lookup_key (self, keyframe);
	if (k == NULL)
		return;
	k->in_slope = in_slope;
	k->out_slope = out_slope;
	queue (self);
}

guint
lrg_timeline_view_get_n_keyframes (LrgTimelineView      *self,
                              LrgTimelineViewTrack *track)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0);

	if (track == NULL)
		return g_hash_table_size (self->keys);
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (track), 0);
	return track->keys->len;
}

guint
lrg_timeline_view_get_keyframe (LrgTimelineView      *self,
                           LrgTimelineViewTrack *track,
                           guint             index)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), LRG_TIMELINE_VIEW_INVALID_ID);
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (track), LRG_TIMELINE_VIEW_INVALID_ID);

	if (index >= track->keys->len)
		return LRG_TIMELINE_VIEW_INVALID_ID;
	return ((Keyframe *) g_ptr_array_index (track->keys, index))->id;
}

/*
 * lrg_timeline_view_evaluate:
 *
 * Finds the segment [a, b] around @time with a binary search and
 * interpolates by a's interpolation: step holds a's value; linear
 * mixes by u; ease by smoothstep(u); bezier is the cubic Hermite
 * through a's out slope and b's in slope (scaled by the segment
 * length). These match the curves paint_curve_segment() draws.
 */
gdouble
lrg_timeline_view_evaluate (LrgTimelineView      *self,
                       LrgTimelineViewTrack *track,
                       gdouble           time)
{
	GPtrArray *keys;
	Keyframe  *a;
	Keyframe  *b;
	guint      i;
	gdouble    d;
	gdouble    u;
	gdouble    u2;
	gdouble    u3;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0.0);
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW_TRACK (track), 0.0);

	keys = track->keys;
	if (keys->len == 0)
		return track->default_value;

	i = key_upper_bound (keys, time);
	if (i == 0)
		return ((Keyframe *) g_ptr_array_index (keys, 0))->value;
	if (i >= keys->len)
		return ((Keyframe *) g_ptr_array_index (keys, keys->len - 1))->value;

	a = g_ptr_array_index (keys, i - 1);
	b = g_ptr_array_index (keys, i);
	d = b->time - a->time;
	if (d <= 0.0)
		return b->value;
	u = CLAMP ((time - a->time) / d, 0.0, 1.0);

	switch (a->interp)
	{
	case LRG_TIMELINE_VIEW_INTERPOLATION_STEP:
		return a->value;
	case LRG_TIMELINE_VIEW_INTERPOLATION_EASE:
		return a->value + (b->value - a->value) * (u * u * (3.0 - 2.0 * u));
	case LRG_TIMELINE_VIEW_INTERPOLATION_BEZIER:
		u2 = u * u;
		u3 = u2 * u;
		return (2.0 * u3 - 3.0 * u2 + 1.0) * a->value + (u3 - 2.0 * u2 + u) * a->out_slope * d +
		       (-2.0 * u3 + 3.0 * u2) * b->value + (u3 - u2) * b->in_slope * d;
	case LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR:
	default:
		return a->value + (b->value - a->value) * u;
	}
}

/* ==========================================================================
 * Public API: markers
 * ========================================================================== */

static Marker *
lookup_marker (LrgTimelineView *self,
               guint        id)
{
	guint i;

	for (i = 0; i < self->markers->len; i++)
		if (((Marker *) g_ptr_array_index (self->markers, i))->id == id)
			return g_ptr_array_index (self->markers, i);
	return NULL;
}

guint
lrg_timeline_view_add_marker (LrgTimelineView *self,
                         gdouble      time,
                         const gchar *label)
{
	Marker *m;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), LRG_TIMELINE_VIEW_INVALID_ID);

	m = g_new0 (Marker, 1);
	m->id = self->next_id++;
	m->time = MAX (0.0, time);
	m->label = g_strdup (label);
	g_ptr_array_add (self->markers, m);
	marker_sort (self);
	queue (self);
	return m->id;
}

gboolean
lrg_timeline_view_remove_marker (LrgTimelineView *self,
                            guint        marker)
{
	Marker *m;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);
	m = lookup_marker (self, marker);
	if (m == NULL)
		return FALSE;

	/* Removing the marker under a drag ends that drag quietly */
	if ((self->drag == DRAG_MARKER || self->drag == DRAG_PENDING_MARKER) && self->grab_id == marker)
	{
		self->drag = DRAG_NONE;
		self->snap_shown = FALSE;
	}
	g_ptr_array_remove (self->markers, m);
	queue (self);
	return TRUE;
}

gboolean
lrg_timeline_view_move_marker (LrgTimelineView *self,
                          guint        marker,
                          gdouble      time)
{
	Marker *m;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);
	m = lookup_marker (self, marker);
	if (m == NULL)
		return FALSE;
	m->time = MAX (0.0, time);
	marker_sort (self);
	queue (self);
	return TRUE;
}

gdouble
lrg_timeline_view_get_marker_time (LrgTimelineView *self,
                              guint        marker)
{
	Marker *m;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), -1.0);
	m = lookup_marker (self, marker);
	return m != NULL ? m->time : -1.0;
}

const gchar *
lrg_timeline_view_get_marker_label (LrgTimelineView *self,
                               guint        marker)
{
	Marker *m;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), NULL);
	m = lookup_marker (self, marker);
	return m != NULL ? m->label : NULL;
}

guint
lrg_timeline_view_get_n_markers (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0);
	return self->markers->len;
}

/* ==========================================================================
 * Public API: selection and editing
 * ========================================================================== */

void
lrg_timeline_view_select_clip (LrgTimelineView *self,
                          guint        clip,
                          gboolean     extend)
{
	gboolean changed = FALSE;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	if (lookup_clip (self, clip) == NULL)
		return;
	if (!extend && !(g_hash_table_size (self->sel_clips) == 1 && g_hash_table_size (self->sel_keys) == 0 &&
	                 g_hash_table_contains (self->sel_clips, ID_KEY (clip))))
		changed = clear_selection (self);
	changed |= g_hash_table_add (self->sel_clips, ID_KEY (clip));
	if (changed)
	{
		selection_changed (self);
		queue (self);
	}
}

void
lrg_timeline_view_select_keyframe (LrgTimelineView *self,
                              guint        keyframe,
                              gboolean     extend)
{
	gboolean changed = FALSE;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	if (lookup_key (self, keyframe) == NULL)
		return;
	if (!extend && !(g_hash_table_size (self->sel_keys) == 1 && g_hash_table_size (self->sel_clips) == 0 &&
	                 g_hash_table_contains (self->sel_keys, ID_KEY (keyframe))))
		changed = clear_selection (self);
	changed |= g_hash_table_add (self->sel_keys, ID_KEY (keyframe));
	if (changed)
	{
		selection_changed (self);
		queue (self);
	}
}

void
lrg_timeline_view_unselect_all (LrgTimelineView *self)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	if (clear_selection (self))
	{
		selection_changed (self);
		queue (self);
	}
}

gboolean
lrg_timeline_view_is_clip_selected (LrgTimelineView *self,
                               guint        clip)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);
	return g_hash_table_contains (self->sel_clips, ID_KEY (clip));
}

gboolean
lrg_timeline_view_is_keyframe_selected (LrgTimelineView *self,
                                   guint        keyframe)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);
	return g_hash_table_contains (self->sel_keys, ID_KEY (keyframe));
}

static gint
compare_uint (gconstpointer a,
              gconstpointer b)
{
	guint x = *(const guint *) a;
	guint y = *(const guint *) b;

	return x < y ? -1 : (x > y ? 1 : 0);
}

static GArray *
set_to_array (GHashTable *set)
{
	GArray        *out = g_array_sized_new (FALSE, FALSE, sizeof (guint), g_hash_table_size (set));
	GHashTableIter it;
	gpointer       k;

	g_hash_table_iter_init (&it, set);
	while (g_hash_table_iter_next (&it, &k, NULL))
	{
		guint id = GPOINTER_TO_UINT (k);

		g_array_append_val (out, id);
	}
	g_array_sort (out, compare_uint);
	return out;
}

GArray *
lrg_timeline_view_get_selected_clips (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), NULL);
	return set_to_array (self->sel_clips);
}

GArray *
lrg_timeline_view_get_selected_keyframes (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), NULL);
	return set_to_array (self->sel_keys);
}

guint
lrg_timeline_view_delete_selection (LrgTimelineView *self)
{
	g_autoptr(GArray) clips = NULL;
	g_autoptr(GArray) keys = NULL;
	guint             n = 0;
	guint             i;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0);

	if (self->drag != DRAG_NONE)
		cancel_drag (self);
	clips = set_to_array (self->sel_clips);
	keys = set_to_array (self->sel_keys);
	for (i = 0; i < clips->len; i++)
	{
		guint id = g_array_index (clips, guint, i);
		Clip *c = lookup_clip (self, id);

		if (c != NULL && !c->track->locked && lrg_timeline_view_remove_clip (self, id))
			n++;
	}
	for (i = 0; i < keys->len; i++)
	{
		guint     id = g_array_index (keys, guint, i);
		Keyframe *k = lookup_key (self, id);

		if (k != NULL && !k->track->locked && lrg_timeline_view_remove_keyframe (self, id))
			n++;
	}
	return n;
}

guint
lrg_timeline_view_copy_selection (LrgTimelineView *self)
{
	g_autoptr(GArray) clips = NULL;
	gdouble           first = G_MAXDOUBLE;
	guint             i;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0);

	clips = set_to_array (self->sel_clips);
	if (clips->len == 0)
		return 0;
	g_array_set_size (self->clipboard, 0);
	for (i = 0; i < clips->len; i++)
		first = MIN (first, lookup_clip (self, g_array_index (clips, guint, i))->start);
	for (i = 0; i < clips->len; i++)
	{
		Clip  *c = lookup_clip (self, g_array_index (clips, guint, i));
		Copied cp;

		cp.track = c->track;
		cp.offset = c->start - first;
		cp.duration = c->duration;
		cp.label = g_strdup (c->label);
		cp.color = c->color;
		cp.has_color = c->has_color;
		g_array_append_val (self->clipboard, cp);
	}
	return clips->len;
}

guint
lrg_timeline_view_paste (LrgTimelineView *self,
                    gdouble      time)
{
	guint n = 0;
	guint i;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0);

	if (self->clipboard->len == 0)
		return 0;
	clear_selection (self);
	for (i = 0; i < self->clipboard->len; i++)
	{
		Copied *cp = &g_array_index (self->clipboard, Copied, i);
		guint   id;

		if (cp->track == NULL || cp->track->timeline != self || cp->track->locked)
			continue;
		id = lrg_timeline_view_add_clip (self, cp->track, MAX (0.0, time) + cp->offset, cp->duration, cp->label);
		if (id == LRG_TIMELINE_VIEW_INVALID_ID)
			continue;
		if (cp->has_color)
			lrg_timeline_view_set_clip_color (self, id, &cp->color);
		g_hash_table_add (self->sel_clips, ID_KEY (id));
		n++;
	}
	selection_changed (self);
	queue (self);
	return n;
}

/* ==========================================================================
 * Public API: playhead and playback
 * ========================================================================== */

gdouble
lrg_timeline_view_get_time (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0.0);
	return self->time;
}

void
lrg_timeline_view_set_time (LrgTimelineView *self,
                       gdouble      time)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));
	set_time_internal (self, time);
}

gdouble
lrg_timeline_view_get_duration (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), DEFAULT_DURATION);
	return self->duration;
}

void
lrg_timeline_view_set_duration (LrgTimelineView *self,
                           gdouble      duration)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	duration = MAX (duration, 0.001);
	if (self->duration == duration)
		return;
	self->duration = duration;
	if (self->time > duration)
		set_time_internal (self, duration);
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DURATION]);
}

gboolean
lrg_timeline_view_get_playing (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);
	return self->playing;
}

void
lrg_timeline_view_set_playing (LrgTimelineView *self,
                          gboolean     playing)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	playing = !!playing;
	if (self->playing == playing)
		return;
	self->playing = playing;

	/* Starting at the end (forwards, no loop) restarts from the top */
	if (playing && !self->loop && self->rate >= 0.0 && self->time >= self->duration)
		set_time_internal (self, 0.0);
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
}

gdouble
lrg_timeline_view_get_rate (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 1.0);
	return self->rate;
}

void
lrg_timeline_view_set_rate (LrgTimelineView *self,
                       gdouble      rate)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	if (self->rate == rate)
		return;
	self->rate = rate;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RATE]);
}

gboolean
lrg_timeline_view_get_loop (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);
	return self->loop;
}

void
lrg_timeline_view_set_loop (LrgTimelineView *self,
                       gboolean     loop)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	loop = !!loop;
	if (self->loop == loop)
		return;
	self->loop = loop;
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOP]);
}

void
lrg_timeline_view_get_loop_range (LrgTimelineView *self,
                             gdouble     *start,
                             gdouble     *end)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	if (start != NULL)
		*start = self->loop_start;
	if (end != NULL)
		*end = self->loop_end;
}

void
lrg_timeline_view_set_loop_range (LrgTimelineView *self,
                             gdouble      start,
                             gdouble      end)
{
	gboolean sc;
	gboolean ec;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	start = MAX (0.0, start);
	end = MAX (0.0, end);
	sc = self->loop_start != start;
	ec = self->loop_end != end;
	self->loop_start = start;
	self->loop_end = end;
	if (sc || ec)
		queue (self);
	if (sc)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOP_START]);
	if (ec)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOP_END]);
}

/* ==========================================================================
 * Public API: view
 * ========================================================================== */

gdouble
lrg_timeline_view_get_zoom (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), DEFAULT_ZOOM);
	return (gdouble) self->zoom.target;
}

void
lrg_timeline_view_set_zoom (LrgTimelineView *self,
                       gdouble      zoom)
{
	gdouble left;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	zoom = CLAMP (zoom, MIN_ZOOM, MAX_ZOOM);
	if ((gdouble) self->zoom.target == zoom && self->zoom.value == self->zoom.target)
		return;

	/* Keep the time at the left edge */
	left = lrg_timeline_view_get_scroll_time (self);
	self->anchored = FALSE;
	lrg_spring_snap (&self->zoom, (gfloat) zoom);
	update_extents (self);
	lrg_scroll_axis_scroll_to (&self->ax, (gfloat) (left * zoom), FALSE);
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ZOOM]);
}

void
lrg_timeline_view_zoom_to (LrgTimelineView *self,
                      gdouble      zoom,
                      gfloat       anchor_x,
                      gboolean     animate)
{
	Geom g;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	zoom = CLAMP (zoom, MIN_ZOOM, MAX_ZOOM);
	geom (self, &g);

	/* Pin the time under the anchor (unless already gliding about it) */
	if (!self->anchored || fabsf ((anchor_x - g.cx) - self->anchor_rel) > 0.5f)
	{
		self->anchor_time = lrg_timeline_view_x_to_time (self, anchor_x);
		self->anchor_rel = anchor_x - g.cx;
	}
	self->anchored = TRUE;

	if (animate && smooth (self))
	{
		lrg_spring_set_target (&self->zoom, (gfloat) zoom);
	}
	else
	{
		lrg_spring_snap (&self->zoom, (gfloat) zoom);
		apply_anchor (self);
		self->anchored = FALSE;
	}
	lrg_scroll_fx_poke (&self->fx);
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ZOOM]);
}

gdouble
lrg_timeline_view_get_scroll_time (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0.0);
	return (gdouble) lrg_scroll_axis_get_value (&self->ax) / pps (self);
}

void
lrg_timeline_view_set_scroll_time (LrgTimelineView *self,
                              gdouble      time,
                              gboolean     animate)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	self->anchored = FALSE;
	update_extents (self);
	lrg_scroll_axis_scroll_to (&self->ax, (gfloat) (time * pps (self)), animate && smooth (self));
	queue (self);
}

gfloat
lrg_timeline_view_get_scroll_y (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0.0f);
	return lrg_scroll_axis_get_value (&self->ay);
}

void
lrg_timeline_view_set_scroll_y (LrgTimelineView *self,
                           gfloat       offset,
                           gboolean     animate)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	update_extents (self);
	lrg_scroll_axis_scroll_to (&self->ay, offset, animate && smooth (self));
	queue (self);
}

gfloat
lrg_timeline_view_time_to_x (LrgTimelineView *self,
                        gdouble      time)
{
	Geom g;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0.0f);
	geom (self, &g);
	return g.cx + (gfloat) (time * pps (self)) - lrg_scroll_axis_get_value (&self->ax);
}

gdouble
lrg_timeline_view_x_to_time (LrgTimelineView *self,
                        gfloat       x)
{
	Geom g;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0.0);
	geom (self, &g);
	return (gdouble) (x - g.cx + lrg_scroll_axis_get_value (&self->ax)) / pps (self);
}

void
lrg_timeline_view_get_visible_range (LrgTimelineView *self,
                                gdouble     *start,
                                gdouble     *end)
{
	Geom g;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));
	geom (self, &g);
	if (start != NULL)
		*start = lrg_timeline_view_x_to_time (self, g.cx);
	if (end != NULL)
		*end = lrg_timeline_view_x_to_time (self, g.cx + g.vw);
}

void
lrg_timeline_view_get_tick_step (LrgTimelineView *self,
                            gdouble     *major,
                            guint       *minor)
{
	guint   m = 1;
	gdouble s;

	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));
	s = tick_step (self, 1.0 / pps (self), &m);
	if (major != NULL)
		*major = s;
	if (minor != NULL)
		*minor = m;
}

gchar *
lrg_timeline_view_format_time (LrgTimelineView *self,
                          gdouble      time)
{
	guint   m = 1;
	gdouble s;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), NULL);
	s = tick_step (self, 1.0 / pps (self), &m);
	return format_time (self, time, s);
}

/* ==========================================================================
 * Public API: settings
 * ========================================================================== */

gdouble
lrg_timeline_view_get_fps (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0.0);
	return self->fps;
}

void
lrg_timeline_view_set_fps (LrgTimelineView *self,
                      gdouble      fps)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	fps = MAX (0.0, fps);
	if (self->fps == fps)
		return;
	self->fps = fps;
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FPS]);
}

LrgTimelineViewTimeFormat
lrg_timeline_view_get_time_format (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), LRG_TIMELINE_VIEW_TIME_FORMAT_SECONDS);
	return self->format;
}

void
lrg_timeline_view_set_time_format (LrgTimelineView           *self,
                              LrgTimelineViewTimeFormat  format)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	if (self->format == format)
		return;
	self->format = format;
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME_FORMAT]);
}

LrgTimelineViewSnap
lrg_timeline_view_get_snap (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), LRG_TIMELINE_VIEW_SNAP_NONE);
	return self->snap;
}

void
lrg_timeline_view_set_snap (LrgTimelineView     *self,
                       LrgTimelineViewSnap  snap)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	snap &= LRG_TIMELINE_VIEW_SNAP_ALL;
	if (self->snap == snap)
		return;
	self->snap = snap;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SNAP]);
}

gfloat
lrg_timeline_view_get_snap_distance (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), DEFAULT_SNAP_PX);
	return self->snap_distance;
}

void
lrg_timeline_view_set_snap_distance (LrgTimelineView *self,
                                gfloat       distance)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	distance = MAX (0.0f, distance);
	if (self->snap_distance == distance)
		return;
	self->snap_distance = distance;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SNAP_DISTANCE]);
}

gboolean
lrg_timeline_view_get_follow_playhead (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);
	return self->follow;
}

void
lrg_timeline_view_set_follow_playhead (LrgTimelineView *self,
                                  gboolean     follow)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	follow = !!follow;
	if (self->follow == follow)
		return;
	self->follow = follow;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOLLOW_PLAYHEAD]);
}

gfloat
lrg_timeline_view_get_header_width (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), DEFAULT_HEADER_W);
	return self->header_w;
}

void
lrg_timeline_view_set_header_width (LrgTimelineView *self,
                               gfloat       width)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	width = MAX (0.0f, width);
	if (self->header_w == width)
		return;
	self->header_w = width;
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEADER_WIDTH]);
}

gfloat
lrg_timeline_view_get_ruler_height (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), DEFAULT_RULER_H);
	return self->ruler_h;
}

void
lrg_timeline_view_set_ruler_height (LrgTimelineView *self,
                               gfloat       height)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	height = MAX (0.0f, height);
	if (self->ruler_h == height)
		return;
	self->ruler_h = height;
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RULER_HEIGHT]);
}

gfloat
lrg_timeline_view_get_track_height (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), 0.0f);
	return self->track_h;
}

void
lrg_timeline_view_set_track_height (LrgTimelineView *self,
                               gfloat       height)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	height = MAX (0.0f, height);
	if (self->track_h == height)
		return;
	self->track_h = height;
	self->rows_dirty = TRUE;
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRACK_HEIGHT]);
}

gboolean
lrg_timeline_view_get_show_play_button (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);
	return self->show_play;
}

void
lrg_timeline_view_set_show_play_button (LrgTimelineView *self,
                                   gboolean     show)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	show = !!show;
	if (self->show_play == show)
		return;
	self->show_play = show;
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_PLAY_BUTTON]);
}

/* ==========================================================================
 * Looks: properties
 * ========================================================================== */

LrgCanvasLook
lrg_timeline_view_get_look (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), LRG_CANVAS_LOOK_AUTO);
	return self->look;
}

void
lrg_timeline_view_set_look (LrgTimelineView *self,
                            LrgCanvasLook    look)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));
	g_return_if_fail (look <= LRG_CANVAS_LOOK_NEON);

	if (self->look == look)
		return;
	self->look = look;
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOK]);
}

LrgNodeGraphGrid
lrg_timeline_view_get_grid_style (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), LRG_NODE_GRAPH_GRID_AUTO);
	return self->grid_style;
}

void
lrg_timeline_view_set_grid_style (LrgTimelineView  *self,
                                  LrgNodeGraphGrid  style)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));
	g_return_if_fail (style <= LRG_NODE_GRAPH_GRID_CROSSES);

	if (self->grid_style == style)
		return;
	self->grid_style = style;
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRID_STYLE]);
}

LrgCanvasKeyShape
lrg_timeline_view_get_key_shape (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), LRG_CANVAS_KEY_SHAPE_AUTO);
	return self->key_shape;
}

void
lrg_timeline_view_set_key_shape (LrgTimelineView   *self,
                                 LrgCanvasKeyShape  shape)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));
	g_return_if_fail (shape <= LRG_CANVAS_KEY_SHAPE_BAR);

	if (self->key_shape == shape)
		return;
	self->key_shape = shape;
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KEY_SHAPE]);
}

gfloat
lrg_timeline_view_get_glow (LrgTimelineView *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), -1.0f);
	return self->glow_strength;
}

void
lrg_timeline_view_set_glow (LrgTimelineView *self,
                            gfloat           glow)
{
	g_return_if_fail (LRG_IS_TIMELINE_VIEW (self));

	/* Every negative value means "the look's" */
	glow = glow < 0.0f ? -1.0f : MIN (glow, 3.0f);
	if (self->glow_strength == glow)
		return;
	self->glow_strength = glow;
	queue (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GLOW]);
}

/* ==========================================================================
 * YAML
 * ========================================================================== */

G_DEFINE_QUARK (lrg-timeline-view-error-quark, lrg_timeline_view_error)

/* yaml_color: "#rrggbbaa" */
static gchar *
yaml_color (const GrlColor *c)
{
	return g_strdup_printf ("#%02x%02x%02x%02x", c->r, c->g, c->b, c->a);
}

/* yaml_parse_color: "#rrggbb" or "#rrggbbaa" */
static gboolean
yaml_parse_color (const gchar *text,
                  GrlColor    *out)
{
	guint64 v;
	gchar  *end = NULL;
	gsize   len;

	if (text == NULL || text[0] != '#')
		return FALSE;
	len = strlen (text + 1);
	if (len != 6 && len != 8)
		return FALSE;
	v = g_ascii_strtoull (text + 1, &end, 16);
	if (end == NULL || *end != '\0')
		return FALSE;
	if (len == 6)
		v = (v << 8) | 0xff;
	out->r = (guint8) ((v >> 24) & 0xff);
	out->g = (guint8) ((v >> 16) & 0xff);
	out->b = (guint8) ((v >> 8) & 0xff);
	out->a = (guint8) (v & 0xff);
	return TRUE;
}

/* yaml_nick: the nick of @value in the enum @type */
static const gchar *
yaml_nick (GType type,
           gint  value)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	GEnumValue           *ev = g_enum_get_value (klass, value);

	return ev != NULL ? ev->value_nick : "auto";
}

/* yaml_enum: @text as a nick or name of the enum @type */
static gboolean
yaml_enum (GType        type,
           const gchar *text,
           gint        *out)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	GEnumValue           *ev;

	if (text == NULL)
		return FALSE;
	ev = g_enum_get_value_by_nick (klass, text);
	if (ev == NULL)
		ev = g_enum_get_value_by_name (klass, text);
	if (ev == NULL)
		return FALSE;
	*out = ev->value;
	return TRUE;
}

/* --- writing ------------------------------------------------------------- */

static YamlSequence *
tracks_to_yaml (LrgTimelineView      *self,
                LrgTimelineViewTrack *parent);

/*
 * track_to_yaml:
 *
 * One track: its name, the properties that differ from a new track's,
 * its clips and keyframes in time order, then its nested tracks.
 */
static YamlMapping *
track_to_yaml (LrgTimelineView      *self,
               LrgTimelineViewTrack *t)
{
	YamlMapping  *m = yaml_mapping_new ();
	YamlSequence *children;
	guint         i;

	if (t->name != NULL)
		yaml_mapping_set_string_member (m, "name", t->name);
	if (t->has_color)
	{
		g_autofree gchar *hex = yaml_color (&t->color);

		yaml_mapping_set_string_member (m, "color", hex);
	}
	if (t->height != 0.0f)
		yaml_mapping_set_double_member (m, "height", (gdouble) t->height);
	if (t->collapsed)
		yaml_mapping_set_boolean_member (m, "collapsed", TRUE);
	if (t->muted)
		yaml_mapping_set_boolean_member (m, "muted", TRUE);
	if (t->locked)
		yaml_mapping_set_boolean_member (m, "locked", TRUE);
	if (t->allow_overlap)
		yaml_mapping_set_boolean_member (m, "allow-overlap", TRUE);
	if (t->curve_lane)
		yaml_mapping_set_boolean_member (m, "curve-lane", TRUE);
	if (t->curve_height != DEFAULT_CURVE_H)
		yaml_mapping_set_double_member (m, "curve-height", (gdouble) t->curve_height);
	if (t->value_min != 0.0)
		yaml_mapping_set_double_member (m, "value-min", t->value_min);
	if (t->value_max != 1.0)
		yaml_mapping_set_double_member (m, "value-max", t->value_max);
	if (t->default_value != 0.0)
		yaml_mapping_set_double_member (m, "default-value", t->default_value);

	/* --- clips --- */
	if (t->clips->len > 0)
	{
		YamlSequence *clips = yaml_sequence_new ();

		for (i = 0; i < t->clips->len; i++)
		{
			Clip        *c = g_ptr_array_index (t->clips, i);
			YamlMapping *cm = yaml_mapping_new ();

			yaml_mapping_set_double_member (cm, "start", c->start);
			yaml_mapping_set_double_member (cm, "duration", c->duration);
			if (c->label != NULL)
				yaml_mapping_set_string_member (cm, "label", c->label);
			if (c->has_color)
			{
				g_autofree gchar *hex = yaml_color (&c->color);

				yaml_mapping_set_string_member (cm, "color", hex);
			}
			yaml_sequence_add_mapping_element (clips, cm);
			yaml_mapping_unref (cm);
		}
		yaml_mapping_set_sequence_member (m, "clips", clips);
		yaml_sequence_unref (clips);
	}

	/* --- keyframes --- */
	if (t->keys->len > 0)
	{
		YamlSequence *keys = yaml_sequence_new ();

		for (i = 0; i < t->keys->len; i++)
		{
			Keyframe    *k = g_ptr_array_index (t->keys, i);
			YamlMapping *km = yaml_mapping_new ();

			yaml_mapping_set_double_member (km, "time", k->time);
			yaml_mapping_set_double_member (km, "value", k->value);
			yaml_mapping_set_string_member (km, "interpolation",
			                                yaml_nick (LRG_TYPE_TIMELINE_VIEW_INTERPOLATION, (gint) k->interp));
			if (k->in_slope != 0.0 || k->out_slope != 0.0)
			{
				YamlSequence *tan = yaml_sequence_new ();

				yaml_sequence_add_double_element (tan, k->in_slope);
				yaml_sequence_add_double_element (tan, k->out_slope);
				yaml_mapping_set_sequence_member (km, "tangents", tan);
				yaml_sequence_unref (tan);
			}
			if (k->shape != LRG_CANVAS_KEY_SHAPE_AUTO)
				yaml_mapping_set_string_member (km, "shape",
				                                yaml_nick (LRG_TYPE_CANVAS_KEY_SHAPE, (gint) k->shape));
			yaml_sequence_add_mapping_element (keys, km);
			yaml_mapping_unref (km);
		}
		yaml_mapping_set_sequence_member (m, "keyframes", keys);
		yaml_sequence_unref (keys);
	}

	/* --- nested tracks --- */
	children = tracks_to_yaml (self, t);
	if (yaml_sequence_get_length (children) > 0)
		yaml_mapping_set_sequence_member (m, "tracks", children);
	yaml_sequence_unref (children);
	return m;
}

/* tracks_to_yaml: the tracks whose parent is @parent, in display order */
static YamlSequence *
tracks_to_yaml (LrgTimelineView      *self,
                LrgTimelineViewTrack *parent)
{
	YamlSequence *seq = yaml_sequence_new ();
	guint         i;

	for (i = 0; i < self->tracks->len; i++)
	{
		LrgTimelineViewTrack *t = g_ptr_array_index (self->tracks, i);
		YamlMapping          *m;

		if (t->parent != parent)
			continue;
		m = track_to_yaml (self, t);
		yaml_sequence_add_mapping_element (seq, m);
		yaml_mapping_unref (m);
	}
	return seq;
}

gchar *
lrg_timeline_view_to_yaml (LrgTimelineView  *self,
                           gsize            *length,
                           GError          **error)
{
	g_autoptr(YamlGenerator) generator = NULL;
	g_autoptr(YamlNode)      root_node = NULL;
	YamlMapping             *root;
	YamlSequence            *tracks;
	YamlSequence            *markers;
	gsize                    len = 0;
	gchar                   *data;
	guint                    i;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	root = yaml_mapping_new ();
	yaml_mapping_set_int_member (root, "version", 1);
	yaml_mapping_set_double_member (root, "duration", self->duration);
	if (self->fps > 0.0)
		yaml_mapping_set_double_member (root, "fps", self->fps);

	tracks = tracks_to_yaml (self, NULL);
	yaml_mapping_set_sequence_member (root, "tracks", tracks);
	yaml_sequence_unref (tracks);

	markers = yaml_sequence_new ();
	for (i = 0; i < self->markers->len; i++)
	{
		Marker      *mk = g_ptr_array_index (self->markers, i);
		YamlMapping *mm = yaml_mapping_new ();

		yaml_mapping_set_double_member (mm, "time", mk->time);
		if (mk->label != NULL)
			yaml_mapping_set_string_member (mm, "label", mk->label);
		yaml_sequence_add_mapping_element (markers, mm);
		yaml_mapping_unref (mm);
	}
	yaml_mapping_set_sequence_member (root, "markers", markers);
	yaml_sequence_unref (markers);

	root_node = yaml_node_new_mapping (root);
	yaml_mapping_unref (root);
	generator = yaml_generator_new ();
	yaml_generator_set_root (generator, root_node);
	data = yaml_generator_to_data (generator, &len, error);
	if (data != NULL && length != NULL)
		*length = len;
	return data;
}

/* --- reading ------------------------------------------------------------- */

/* Parsed document, built before the timeline is touched */
typedef struct
{
	gdouble   start;
	gdouble   duration;
	gchar    *label;
	gboolean  has_color;
	GrlColor  color;
} StagedClip;

typedef struct
{
	gdouble   time;
	gdouble   value;
	gint      interp;
	gdouble   in_slope;
	gdouble   out_slope;
	gint      shape;
} StagedKey;

typedef struct
{
	LrgTimelineViewTrack *track;      /* detached, properties set */
	GArray               *clips;      /* StagedClip */
	GArray               *keys;       /* StagedKey */
	GPtrArray            *children;   /* StagedTrack * */
} StagedTrack;

static void
staged_clip_clear (gpointer data)
{
	g_free (((StagedClip *) data)->label);
}

static void
staged_track_free (gpointer data)
{
	StagedTrack *t = data;

	g_clear_object (&t->track);
	g_clear_pointer (&t->clips, g_array_unref);
	g_clear_pointer (&t->keys, g_array_unref);
	g_clear_pointer (&t->children, g_ptr_array_unref);
	g_free (t);
}

static gboolean
parse_fail (GError      **error,
            const gchar  *path,
            const gchar  *what)
{
	g_set_error (error, LRG_TIMELINE_VIEW_ERROR, LRG_TIMELINE_VIEW_ERROR_PARSE,
	             "timeline YAML: %s: %s", path, what);
	return FALSE;
}

/* scalar_of: the text of a scalar node, %NULL for a null node; FALSE otherwise */
static gboolean
scalar_of (YamlNode     *node,
           const gchar **out)
{
	*out = NULL;
	if (node == NULL || yaml_node_get_node_type (node) == YAML_NODE_NULL)
		return TRUE;
	if (yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return FALSE;
	*out = yaml_node_get_scalar (node);
	return TRUE;
}

/* number_of: a finite number from a scalar node */
static gboolean
number_of (YamlNode *node,
           gdouble  *out)
{
	const gchar *text;
	gchar       *end = NULL;

	if (!scalar_of (node, &text) || text == NULL)
		return FALSE;
	*out = g_ascii_strtod (text, &end);
	return end != NULL && end != text && *end == '\0' && isfinite (*out);
}

/* number_in: member @key of @m as a number, @fallback when absent */
static gboolean
number_in (YamlMapping *m,
           const gchar *key,
           gdouble      fallback,
           gboolean     required,
           gdouble     *out)
{
	if (!yaml_mapping_has_member (m, key))
	{
		*out = fallback;
		return !required;
	}
	return number_of (yaml_mapping_get_member (m, key), out);
}

/* list_in: member @key of @m as a sequence (NULL when absent); FALSE when not a list */
static gboolean
list_in (YamlMapping   *m,
         const gchar   *key,
         YamlSequence **out)
{
	YamlNode *n;

	*out = NULL;
	if (!yaml_mapping_has_member (m, key))
		return TRUE;
	n = yaml_mapping_get_member (m, key);
	if (n == NULL || yaml_node_get_node_type (n) == YAML_NODE_NULL)
		return TRUE;
	if (yaml_node_get_node_type (n) != YAML_NODE_SEQUENCE)
		return FALSE;
	*out = yaml_node_get_sequence (n);
	return TRUE;
}

/*
 * set_track_property:
 *
 * Sets a track property from a YAML scalar: booleans, numbers, text
 * and colours ("#rrggbb[aa]"). "parent" is structural and refused.
 */
static gboolean
set_track_property (LrgTimelineViewTrack *track,
                    const gchar          *key,
                    YamlNode             *node)
{
	GParamSpec   *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (track), key);
	g_auto(GValue) value = G_VALUE_INIT;
	const gchar  *text;
	gdouble       d;

	if (pspec == NULL || !(pspec->flags & G_PARAM_WRITABLE) || g_strcmp0 (key, "parent") == 0)
		return FALSE;
	if (!scalar_of (node, &text))
		return FALSE;
	g_value_init (&value, pspec->value_type);
	if (pspec->value_type == G_TYPE_BOOLEAN)
	{
		if (g_strcmp0 (text, "true") == 0 || g_strcmp0 (text, "yes") == 0)
			g_value_set_boolean (&value, TRUE);
		else if (g_strcmp0 (text, "false") == 0 || g_strcmp0 (text, "no") == 0)
			g_value_set_boolean (&value, FALSE);
		else
			return FALSE;
	}
	else if (pspec->value_type == G_TYPE_FLOAT || pspec->value_type == G_TYPE_DOUBLE)
	{
		if (!number_of (node, &d))
			return FALSE;
		if (pspec->value_type == G_TYPE_FLOAT)
			g_value_set_float (&value, (gfloat) d);
		else
			g_value_set_double (&value, d);
	}
	else if (pspec->value_type == G_TYPE_STRING)
		g_value_set_string (&value, text);
	else if (pspec->value_type == GRL_TYPE_COLOR)
	{
		GrlColor c;

		if (text != NULL && !yaml_parse_color (text, &c))
			return FALSE;
		g_value_set_boxed (&value, text != NULL ? &c : NULL);
	}
	else
		return FALSE;
	if (g_param_value_validate (pspec, &value))
		return FALSE;
	g_object_set_property (G_OBJECT (track), key, &value);
	return TRUE;
}

static gboolean stage_tracks (YamlSequence  *seq,
                              const gchar   *path,
                              guint          depth,
                              GPtrArray     *out,
                              GError       **error);

/* stage_track: parses one `tracks:` entry */
static gboolean
stage_track (YamlNode     *item,
             const gchar  *path,
             guint         depth,
             StagedTrack  *st,
             GError      **error)
{
	YamlMapping  *m;
	YamlSequence *seq;
	const gchar  *name = NULL;
	guint         i;

	if (item == NULL || yaml_node_get_node_type (item) != YAML_NODE_MAPPING)
		return parse_fail (error, path, "a track must be a mapping");
	m = yaml_node_get_mapping (item);
	if (yaml_mapping_has_member (m, "name") && !scalar_of (yaml_mapping_get_member (m, "name"), &name))
		return parse_fail (error, path, "bad name");
	st->track = lrg_timeline_view_track_new (name);
	st->clips = g_array_new (FALSE, TRUE, sizeof (StagedClip));
	g_array_set_clear_func (st->clips, staged_clip_clear);
	st->keys = g_array_new (FALSE, TRUE, sizeof (StagedKey));
	st->children = g_ptr_array_new_with_free_func (staged_track_free);

	/* --- track properties: every other scalar key --- */
	for (i = 0; i < yaml_mapping_get_size (m); i++)
	{
		const gchar *key = yaml_mapping_get_key (m, i);

		if (g_strcmp0 (key, "name") == 0 || g_strcmp0 (key, "clips") == 0 ||
		    g_strcmp0 (key, "keyframes") == 0 || g_strcmp0 (key, "tracks") == 0)
			continue;
		if (!set_track_property (st->track, key, yaml_mapping_get_value (m, i)))
		{
			g_autofree gchar *msg = g_strdup_printf ("bad or unknown track key '%s'", key);

			return parse_fail (error, path, msg);
		}
	}

	/* --- clips --- */
	if (!list_in (m, "clips", &seq))
		return parse_fail (error, path, "'clips' must be a list");
	for (i = 0; seq != NULL && i < yaml_sequence_get_length (seq); i++)
	{
		YamlNode    *cn = yaml_sequence_get_element (seq, i);
		YamlMapping *cm;
		StagedClip   c;
		const gchar *text = NULL;

		memset (&c, 0, sizeof c);
		if (cn == NULL || yaml_node_get_node_type (cn) != YAML_NODE_MAPPING)
			return parse_fail (error, path, "a clip must be a mapping");
		cm = yaml_node_get_mapping (cn);
		if (!number_in (cm, "start", 0.0, TRUE, &c.start) || !number_in (cm, "duration", 0.0, TRUE, &c.duration) ||
		    c.duration <= 0.0)
			return parse_fail (error, path, "a clip needs a start and a positive duration");
		if (yaml_mapping_has_member (cm, "label") && !scalar_of (yaml_mapping_get_member (cm, "label"), &text))
			return parse_fail (error, path, "bad clip label");
		c.label = g_strdup (text);
		g_array_append_val (st->clips, c);
		if (yaml_mapping_has_member (cm, "color"))
		{
			StagedClip *sc = &g_array_index (st->clips, StagedClip, st->clips->len - 1);

			if (!scalar_of (yaml_mapping_get_member (cm, "color"), &text) ||
			    (text != NULL && !yaml_parse_color (text, &sc->color)))
				return parse_fail (error, path, "bad clip color");
			sc->has_color = text != NULL;
		}
	}

	/* --- keyframes --- */
	if (!list_in (m, "keyframes", &seq))
		return parse_fail (error, path, "'keyframes' must be a list");
	for (i = 0; seq != NULL && i < yaml_sequence_get_length (seq); i++)
	{
		YamlNode     *kn = yaml_sequence_get_element (seq, i);
		YamlMapping  *km;
		YamlSequence *tan;
		StagedKey     k;
		const gchar  *text = NULL;

		memset (&k, 0, sizeof k);
		k.interp = LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR;
		if (kn == NULL || yaml_node_get_node_type (kn) != YAML_NODE_MAPPING)
			return parse_fail (error, path, "a keyframe must be a mapping");
		km = yaml_node_get_mapping (kn);
		if (!number_in (km, "time", 0.0, TRUE, &k.time) || !number_in (km, "value", 0.0, FALSE, &k.value))
			return parse_fail (error, path, "a keyframe needs a time and a numeric value");
		if (yaml_mapping_has_member (km, "interpolation") &&
		    (!scalar_of (yaml_mapping_get_member (km, "interpolation"), &text) ||
		     !yaml_enum (LRG_TYPE_TIMELINE_VIEW_INTERPOLATION, text, &k.interp)))
			return parse_fail (error, path, "bad keyframe interpolation");
		if (yaml_mapping_has_member (km, "shape") &&
		    (!scalar_of (yaml_mapping_get_member (km, "shape"), &text) ||
		     !yaml_enum (LRG_TYPE_CANVAS_KEY_SHAPE, text, &k.shape)))
			return parse_fail (error, path, "bad keyframe shape");
		if (!list_in (km, "tangents", &tan) ||
		    (tan != NULL && (yaml_sequence_get_length (tan) != 2 ||
		                     !number_of (yaml_sequence_get_element (tan, 0), &k.in_slope) ||
		                     !number_of (yaml_sequence_get_element (tan, 1), &k.out_slope))))
			return parse_fail (error, path, "'tangents' must be [in, out]");
		g_array_append_val (st->keys, k);
	}

	/* --- nested tracks --- */
	if (!list_in (m, "tracks", &seq))
		return parse_fail (error, path, "'tracks' must be a list");
	return seq == NULL || stage_tracks (seq, path, depth + 1, st->children, error);
}

/* stage_tracks: parses a `tracks:` list (nesting at most 16 deep) */
static gboolean
stage_tracks (YamlSequence  *seq,
              const gchar   *path,
              guint          depth,
              GPtrArray     *out,
              GError       **error)
{
	guint i;

	if (depth > 16)
		return parse_fail (error, path, "tracks nest too deeply");
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		g_autofree gchar *sub = g_strdup_printf ("%s.tracks[%u]", path, i);
		StagedTrack      *st = g_new0 (StagedTrack, 1);

		g_ptr_array_add (out, st);
		if (!stage_track (yaml_sequence_get_element (seq, i), sub, depth, st, error))
			return FALSE;
	}
	return TRUE;
}

/* apply_tracks: adds the staged tracks (under @parent) with their content */
static void
apply_tracks (LrgTimelineView      *self,
              GPtrArray            *staged,
              LrgTimelineViewTrack *parent)
{
	guint i;
	guint j;

	for (i = 0; i < staged->len; i++)
	{
		StagedTrack *st = g_ptr_array_index (staged, i);

		lrg_timeline_view_append_track (self, st->track, parent);
		for (j = 0; j < st->clips->len; j++)
		{
			StagedClip *c = &g_array_index (st->clips, StagedClip, j);
			guint       id = lrg_timeline_view_add_clip (self, st->track, c->start, c->duration, c->label);

			if (id != LRG_TIMELINE_VIEW_INVALID_ID && c->has_color)
				lrg_timeline_view_set_clip_color (self, id, &c->color);
		}
		for (j = 0; j < st->keys->len; j++)
		{
			StagedKey *k = &g_array_index (st->keys, StagedKey, j);
			guint      id = lrg_timeline_view_add_keyframe (self, st->track, k->time, k->value,
			                                                (LrgTimelineViewInterpolation) k->interp);

			if (id == LRG_TIMELINE_VIEW_INVALID_ID)
				continue;
			if (k->in_slope != 0.0 || k->out_slope != 0.0)
				lrg_timeline_view_set_keyframe_tangents (self, id, k->in_slope, k->out_slope);
			lrg_timeline_view_set_keyframe_shape (self, id, (LrgCanvasKeyShape) k->shape);
		}
		apply_tracks (self, st->children, st->track);
	}
}

gboolean
lrg_timeline_view_load_yaml (LrgTimelineView  *self,
                             const gchar      *data,
                             gssize            length,
                             GError          **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GPtrArray)  tracks = NULL;
	g_autoptr(GArray)     marks = NULL;
	g_autoptr(GError)     local = NULL;
	YamlNode             *root_node;
	YamlMapping          *root;
	YamlSequence         *seq;
	gdouble               version;
	gdouble               duration;
	gdouble               fps;
	guint                 i;

	g_return_val_if_fail (LRG_IS_TIMELINE_VIEW (self), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* --- the document --- */
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
		return parse_fail (error, "document", local->message);
	root_node = yaml_parser_get_root (parser);
	if (root_node == NULL || yaml_node_get_node_type (root_node) != YAML_NODE_MAPPING)
		return parse_fail (error, "document", "not a mapping");
	root = yaml_node_get_mapping (root_node);
	if (!number_in (root, "version", 1.0, FALSE, &version) || version != 1.0)
		return parse_fail (error, "version", "unsupported version");
	if (!number_in (root, "duration", self->duration, FALSE, &duration) || duration <= 0.0)
		return parse_fail (error, "duration", "must be a positive number");
	if (!number_in (root, "fps", self->fps, FALSE, &fps) || fps < 0.0)
		return parse_fail (error, "fps", "must be a number >= 0");

	/* --- tracks and markers, staged --- */
	tracks = g_ptr_array_new_with_free_func (staged_track_free);
	if (!list_in (root, "tracks", &seq))
		return parse_fail (error, "tracks", "must be a list");
	if (seq != NULL && !stage_tracks (seq, "document", 0, tracks, error))
		return FALSE;

	marks = g_array_new (FALSE, TRUE, sizeof (StagedClip));
	g_array_set_clear_func (marks, staged_clip_clear);
	if (!list_in (root, "markers", &seq))
		return parse_fail (error, "markers", "must be a list");
	for (i = 0; seq != NULL && i < yaml_sequence_get_length (seq); i++)
	{
		YamlNode    *mn = yaml_sequence_get_element (seq, i);
		YamlMapping *mm;
		StagedClip   mk;
		const gchar *text = NULL;

		memset (&mk, 0, sizeof mk);
		if (mn == NULL || yaml_node_get_node_type (mn) != YAML_NODE_MAPPING)
			return parse_fail (error, "markers", "a marker must be a mapping");
		mm = yaml_node_get_mapping (mn);
		if (!number_in (mm, "time", 0.0, TRUE, &mk.start))
			return parse_fail (error, "markers", "a marker needs a time");
		if (yaml_mapping_has_member (mm, "label") && !scalar_of (yaml_mapping_get_member (mm, "label"), &text))
			return parse_fail (error, "markers", "bad marker label");
		mk.label = g_strdup (text);
		g_array_append_val (marks, mk);
	}

	/* --- valid: replace the content --- */
	while (self->tracks->len > 0)
		lrg_timeline_view_remove_track (self, g_ptr_array_index (self->tracks, 0));
	while (self->markers->len > 0)
		lrg_timeline_view_remove_marker (self, ((Marker *) g_ptr_array_index (self->markers, 0))->id);
	lrg_timeline_view_set_duration (self, duration);
	lrg_timeline_view_set_fps (self, fps);
	apply_tracks (self, tracks, NULL);
	for (i = 0; i < marks->len; i++)
	{
		StagedClip *mk = &g_array_index (marks, StagedClip, i);

		lrg_timeline_view_add_marker (self, mk->start, mk->label);
	}
	return TRUE;
}
