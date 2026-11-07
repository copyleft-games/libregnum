/* lrg-editor-sequencer-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The Sequencer panel: edits one .rseq file (LrgSequence).
 *
 * Document: the panel owns the edited sequence (self->seq). Every edit
 * makes a copy, changes it and commits it; the replaced sequence goes on
 * the panel's undo stack (whole-sequence snapshots, like level timelines:
 * sequences are small). A copy of the sequence as last loaded or saved
 * (self->saved) tells whether it is dirty.
 *
 * Model -> view: the LrgTimelineView is rebuilt from the sequence after
 * every change. One group row per binding (tracks without a binding
 * under "Sequence"), one row per track: transform tracks get one child
 * row per keyed channel, property tracks show their curve's keys, event
 * tracks their events (bar keys plus ruler markers), section tracks
 * (camera cut, audio, animation, sub-sequence) their sections as clips.
 * Tables map view ids back to (track, channel, time) and (track,
 * section start).
 *
 * View -> model: the view edits its own copy first and reports drags and
 * deletes with keyframe-moved / -removed / -added, clip-moved / -resized /
 * -removed and marker-moved; they are queued and applied on the next
 * step as ONE commit (one undo step for a multi-key drag), outside the
 * view's signal emission. Times snap to the sequence's frames while
 * "Snap" is on (else to 1 ms).
 *
 * Curve editor: an LrgCustomWidget under the tracks draws the selected
 * curve as the engine evaluates it, with the Bézier handles of
 * lrg_sequence_curve_get_slopes(); dragging keys and handles edits the
 * live sequence and records one undo step on release.
 *
 * Preview: a world-less LrgSequencePlayer owns the playhead (scrub,
 * play, loop, ping-pong, events). After each move the panel evaluates
 * the transform, bone and channel-property tracks itself and writes the
 * values onto the bound level nodes with lrg_timeline_channel_write(),
 * recording each channel's rest value in the app's timeline rest table:
 * so editor_timeline_end_preview() (saving, play mode, loading a level,
 * Stop) puts the level back exactly as the level timeline preview does.
 * With "Pilot" on, the active viewport looks through the camera node of
 * the cut at the playhead.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-sequencer-panel.h"
#include "lrg-editor-console-panel.h"
#include "lrg-editor-app-private.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-combo-box.h"
#include "../../ui/lrg-spin-box.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-separator.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../ui/lrg-menu.h"
#include "../../ui/lrg-theme.h"
#include "../../paint/lrg-paint-list.h"
#include <json-glib/json-glib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PANEL_KEY       "lrg-editor-sequencer-panel"
#define TIME_EPSILON    (0.001f)
#define STRIP_HEIGHT    (214.0f)
#define DETAIL_WIDTH    (300.0f)
#define KEY_RADIUS      (5.0f)
#define HANDLE_SIZE     (7.0f)
#define HIT_RADIUS      (8.0f)
#define DRAG_THRESHOLD  (3.0f)
#define DOUBLE_CLICK    (0.4f)
#define CURVE_SAMPLES   (200)
#define MAX_UNDO        (200)

/* KeyRef.channel values that are not transform channels */
#define CH_CURVE        (-1)    /* a property track's curve */
#define CH_EVENT        (-2)    /* an event of an event track */

enum
{
	PROP_0,
	PROP_DIRTY,
	PROP_PLAYING,
	PROP_SNAP,
	PROP_PILOT,
	PROP_PREVIEW_MODE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* A view keyframe: a curve key or an event */
typedef struct
{
	guint  track;
	gint   channel;   /* 0..8, CH_CURVE or CH_EVENT */
	gfloat time;
	gfloat value;
} KeyRef;

/* A view clip: a section */
typedef struct
{
	guint    track;
	gfloat   start;
	gfloat   duration; /* drawn duration */
	gboolean open;     /* duration 0 in the file (until the next section) */
} ClipRef;

/* A queued edit from the view */
typedef enum
{
	OP_KEY_MOVE,
	OP_KEY_REMOVE,
	OP_KEY_ADD,
	OP_SECTION_MOVE,
	OP_SECTION_REMOVE
} OpKind;

typedef struct
{
	OpKind   kind;
	guint    track;
	gint     channel;
	gfloat   from;
	gfloat   to;
	gfloat   value;
	gboolean value_changed;
	gfloat   duration;  /* sections: new duration, < 0 keeps it */
} Op;

/* What the curve editor is dragging */
typedef enum
{
	DRAG_NONE,
	DRAG_KEY,
	DRAG_IN,
	DRAG_OUT
} DragKind;

struct _LrgEditorSequencerPanel
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;          /* weak */
	LrgEditor            *editor;       /* ref */
	LrgEditorViewState   *view_state;   /* ref */

	/* Document */
	LrgSequence          *seq;          /* the edited sequence */
	LrgSequence          *saved;        /* as loaded / saved: dirty = !equal */
	gchar                *path;         /* absolute, NULL while untitled */
	GPtrArray            *undo;         /* LrgSequence* (oldest first) */
	GPtrArray            *undo_labels;  /* gchar* */
	GPtrArray            *redo;
	GPtrArray            *redo_labels;
	gboolean              dirty;

	/* Preview */
	LrgSequencePlayer    *player;
	LrgSequencePlayMode   mode;
	gboolean              playing;
	gboolean              previewing;   /* nodes are posed */
	gboolean              snap;
	gboolean              pilot;
	gboolean              pilot_saved;
	LrgEditorViewCamera   saved_camera;
	gchar                *cut_camera;
	JsonArray            *fired;        /* events of the last scrub / advance (IPC) */
	gdouble               clock;        /* panel time, for double clicks */

	/* Widgets */
	LrgWidget            *top;
	LrgWidget            *row1;
	LrgWidget            *row2;
	LrgWidget            *title;
	LrgTextInput         *path_input;
	LrgSpinBox           *length;
	LrgSpinBox           *fps;
	LrgWidget            *undo_chip;
	LrgWidget            *redo_chip;
	LrgWidget            *play;
	LrgComboBox          *mode_combo;
	LrgWidget            *snap_chip;
	LrgWidget            *pilot_chip;
	LrgWidget            *curves_chip;
	LrgWidget            *add_track;
	LrgWidget            *time_label;
	LrgTimelineView      *view;
	LrgWidget            *hint;
	LrgMenu              *track_menu;

	/* Bottom strip: curve editor + key fields | detail fields */
	LrgWidget            *strip;
	LrgCustomWidget      *curve;
	LrgSpinBox           *key_time;
	LrgSpinBox           *key_value;
	LrgComboBox          *key_tangent;
	LrgSpinBox           *key_in;
	LrgSpinBox           *key_out;
	LrgWidget            *key_row;
	LrgWidget            *detail_title;
	LrgWidget            *track_box;
	LrgTextInput         *track_name;
	LrgComboBox          *track_binding;
	LrgWidget            *track_mute;
	LrgWidget            *bone_row;
	LrgTextInput         *track_bone;
	LrgWidget            *prop_row;
	LrgTextInput         *track_component;
	LrgTextInput         *track_property;
	LrgWidget            *section_box;
	LrgTextInput         *sec_name;
	LrgSpinBox           *sec_start;
	LrgSpinBox           *sec_duration;
	LrgSpinBox           *sec_offset;
	LrgSpinBox           *sec_rate;
	LrgSpinBox           *sec_weight;
	LrgWidget            *sec_loop;
	LrgWidget            *payload_row;
	LrgTextInput         *sec_payload;

	/* Model <-> view */
	GHashTable           *keys;         /* view keyframe id -> KeyRef* */
	GHashTable           *clips;        /* view clip id -> ClipRef* */
	GHashTable           *markers;      /* view marker id -> KeyRef* (events) */
	GHashTable           *rows;         /* "t:N", "c:N:CH", "b:BINDING" -> LrgTimelineViewTrack* */
	GHashTable           *collapsed;    /* row key -> GINT_TO_POINTER (collapsed) */
	GPtrArray            *ops;          /* Op* */
	gboolean              view_dirty;
	gboolean              syncing;
	gboolean              fit_pending;
	gchar                *binding_sig;  /* bindings the binding picker lists */

	/* Selection: the detail strip's track / section and the curve */
	gint                  sel_track;
	gint                  sel_section;
	gint                  curve_track;
	gint                  curve_channel;
	gint                  curve_key;

	/* Curve editor drag */
	DragKind              drag;
	gboolean              drag_moved;
	gfloat                press_x;
	gfloat                press_y;
	gdouble               last_press;
	LrgSequence          *drag_before;
	LrgSequenceKey        drag_key;     /* the key as it was when pressed */
	gfloat                view_t0;      /* curve editor ranges, frozen while dragging */
	gfloat                view_t1;
	gfloat                view_v0;
	gfloat                view_v1;
};

G_DEFINE_FINAL_TYPE (LrgEditorSequencerPanel, lrg_editor_sequencer_panel, LRG_TYPE_CONTAINER)

static void rebuild (LrgEditorSequencerPanel *self);
static void apply_preview (LrgEditorSequencerPanel *self);
static void sync_detail (LrgEditorSequencerPanel *self);
static void set_dirty (LrgEditorSequencerPanel *self);

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static JsonNode *
obj_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static gchar *
enum_nick (GType  type,
           gint   value)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	GEnumValue           *v = g_enum_get_value (klass, value);

	return g_strdup (v != NULL ? v->value_nick : "?");
}

/* Parses an enum nick; -1 when unknown. */
static gint
enum_from_nick (GType        type,
                const gchar *nick)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	GEnumValue           *v = nick != NULL ? g_enum_get_value_by_nick (klass, nick) : NULL;

	return v != NULL ? v->value : -1;
}

static gfloat
seq_fps (LrgEditorSequencerPanel *self)
{
	gfloat fps = lrg_sequence_get_fps (self->seq);

	return fps > 0.0f ? fps : 30.0f;
}

/* Frames while snapping, else milliseconds; never below 0. */
static gfloat
snap_time (LrgEditorSequencerPanel *self,
           gfloat                   t)
{
	if (!isfinite (t))
		t = 0.0f;
	t = MAX (t, 0.0f);
	if (self->snap)
		return roundf (t * seq_fps (self)) / seq_fps (self);
	return roundf (t * 1000.0f) / 1000.0f;
}

static gchar *
row_key_track (guint track)
{
	return g_strdup_printf ("t:%u", track);
}

static gchar *
row_key_channel (guint track,
                 gint  channel)
{
	return g_strdup_printf ("c:%u:%d", track, channel);
}

/* The display name of a binding: the sequence's name for it, else the
 * level node's name, else the raw binding. */
static gchar *
binding_label (LrgEditorSequencerPanel *self,
               const gchar             *binding)
{
	const gchar *name;
	LrgNode     *node;

	if (binding == NULL || *binding == '\0')
		return g_strdup ("Sequence");
	name = lrg_sequence_get_binding_name (self->seq, binding);
	if (name != NULL && *name != '\0')
		return g_strdup (name);
	node = lrg_editor_find_node (self->editor, binding);
	if (node != NULL)
		return g_strdup (lrg_node_get_name (node));
	return g_strdup (binding);
}

static const gchar *
kind_label (LrgSequenceTrackKind kind)
{
	switch (kind)
	{
	case LRG_SEQUENCE_TRACK_TRANSFORM:   return "Transform";
	case LRG_SEQUENCE_TRACK_PROPERTY:    return "Property";
	case LRG_SEQUENCE_TRACK_CAMERA_CUT:  return "Camera Cuts";
	case LRG_SEQUENCE_TRACK_AUDIO:       return "Audio";
	case LRG_SEQUENCE_TRACK_EVENT:       return "Events";
	case LRG_SEQUENCE_TRACK_ANIMATION:   return "Animation";
	case LRG_SEQUENCE_TRACK_SUBSEQUENCE: return "Sub-sequences";
	default:                             return "Track";
	}
}

/* The row title of a track: its name (or kind), the bone of a bone track,
 * the property of a property track. */
static gchar *
track_title (LrgSequenceTrack *track)
{
	const gchar *name = lrg_sequence_track_get_name (track);
	const gchar *base = (name != NULL && *name != '\0') ? name : kind_label (lrg_sequence_track_get_kind (track));

	if (lrg_sequence_track_get_kind (track) == LRG_SEQUENCE_TRACK_TRANSFORM &&
	    lrg_sequence_track_get_bone (track) != NULL)
		return g_strdup_printf ("%s  [bone %s]", base, lrg_sequence_track_get_bone (track));
	if (lrg_sequence_track_get_kind (track) == LRG_SEQUENCE_TRACK_PROPERTY &&
	    lrg_sequence_track_get_property_name (track) != NULL && (name == NULL || *name == '\0'))
		return g_strdup_printf ("%s%s%s", lrg_sequence_track_get_component_type (track) != NULL
		                                  ? lrg_sequence_track_get_component_type (track) : "",
		                        lrg_sequence_track_get_component_type (track) != NULL ? "." : "",
		                        lrg_sequence_track_get_property_name (track));
	return g_strdup (base);
}

/* Fixed colour wheels (theme independent, readable on dark and light) */
static void
wheel_color (guint     index,
             GrlColor *out)
{
	static const guint8 wheel[][3] = {
		{ 137, 180, 250 }, { 166, 227, 161 }, { 249, 226, 175 }, { 243, 139, 168 },
		{ 203, 166, 247 }, { 148, 226, 213 }, { 250, 179, 135 }, { 245, 194, 231 }
	};
	const guint8 *c = wheel[index % G_N_ELEMENTS (wheel)];

	out->r = c[0];
	out->g = c[1];
	out->b = c[2];
	out->a = 255;
}

static void
kind_color (LrgSequenceTrackKind kind,
            GrlColor            *out)
{
	switch (kind)
	{
	case LRG_SEQUENCE_TRACK_CAMERA_CUT:  wheel_color (4, out); break;
	case LRG_SEQUENCE_TRACK_AUDIO:       wheel_color (5, out); break;
	case LRG_SEQUENCE_TRACK_EVENT:       wheel_color (2, out); break;
	case LRG_SEQUENCE_TRACK_ANIMATION:   wheel_color (1, out); break;
	case LRG_SEQUENCE_TRACK_SUBSEQUENCE: wheel_color (6, out); break;
	default:                             wheel_color (0, out); break;
	}
}

/* The curve a KeyRef-style (track, channel) pair names in @seq, or NULL. */
static LrgSequenceCurve *
curve_of (LrgSequence *seq,
          gint         track,
          gint         channel)
{
	LrgSequenceTrack *t;

	if (seq == NULL || track < 0 || (guint) track >= lrg_sequence_get_n_tracks (seq))
		return NULL;
	t = lrg_sequence_get_track (seq, (guint) track);
	if (channel == CH_CURVE)
		return lrg_sequence_track_get_kind (t) == LRG_SEQUENCE_TRACK_PROPERTY ? lrg_sequence_track_get_curve (t)
		                                                                      : NULL;
	if (channel < 0 || channel >= LRG_SEQUENCE_N_CHANNELS || lrg_sequence_track_get_kind (t) != LRG_SEQUENCE_TRACK_TRANSFORM)
		return NULL;
	return lrg_sequence_track_get_channel_curve (t, (LrgSequenceTransformChannel) channel);
}

/* Index of the section of @track starting at @start (first match), or -1. */
static gint
find_section (LrgSequenceTrack *track,
              gfloat            start)
{
	guint i;

	for (i = 0; i < lrg_sequence_track_get_n_sections (track); i++)
		if (fabsf (lrg_sequence_track_get_section (track, i)->start - start) < TIME_EPSILON)
			return (gint) i;
	return -1;
}

/* Replaces section @index of @track with a changed copy (keeps the start
 * order); returns the new index. */
static guint
replace_section (LrgSequenceTrack   *track,
                 guint               index,
                 LrgSequenceSection *changed)
{
	lrg_sequence_track_remove_section (track, index);
	return lrg_sequence_track_add_section (track, changed);
}

/* The level channel a transform track's curve drives on a node. */
static gchar *
node_channel (LrgSequenceTrack *track,
              gint              channel)
{
	const gchar *name = lrg_sequence_transform_channel_to_string ((LrgSequenceTransformChannel) channel);

	if (lrg_sequence_track_get_bone (track) != NULL)
		return g_strdup_printf ("bone.%s.%s", lrg_sequence_track_get_bone (track), name);
	return g_strdup (name);
}

/* ==========================================================================
 * Document: commit, undo, load, save
 * ========================================================================== */

static void
set_dirty (LrgEditorSequencerPanel *self)
{
	gboolean dirty = self->saved == NULL || !lrg_sequence_equal (self->seq, self->saved);

	if (dirty == self->dirty)
		return;
	self->dirty = dirty;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRTY]);
}

/* Points the player at the current sequence, keeping the playhead. */
static void
attach_player (LrgEditorSequencerPanel *self)
{
	gfloat t = lrg_sequence_player_get_time (self->player);

	lrg_sequence_player_set_sequence (self->player, self->seq);
	lrg_sequence_player_set_play_mode (self->player, self->mode);
	lrg_sequence_player_set_time (self->player, MIN (t, lrg_sequence_get_length (self->seq)));
	if (self->playing)
		lrg_sequence_player_play (self->player);
}

/* After the sequence object changed: view, player, preview, flags. */
static void
sequence_replaced (LrgEditorSequencerPanel *self)
{
	attach_player (self);
	if (self->previewing)
		apply_preview (self);
	self->view_dirty = TRUE;
	set_dirty (self);
}

static void
trim_stack (GPtrArray *stack,
            GPtrArray *labels)
{
	while (stack->len > MAX_UNDO)
	{
		g_ptr_array_remove_index (stack, 0);
		g_ptr_array_remove_index (labels, 0);
	}
}

/* Pushes @before (transfer full) as the undo state of an edit already
 * made to self->seq. */
static void
record_undo (LrgEditorSequencerPanel *self,
             LrgSequence             *before,
             const gchar             *label)
{
	g_ptr_array_add (self->undo, before);
	g_ptr_array_add (self->undo_labels, g_strdup (label != NULL ? label : "Edit Sequence"));
	trim_stack (self->undo, self->undo_labels);
	g_ptr_array_set_size (self->redo, 0);
	g_ptr_array_set_size (self->redo_labels, 0);
}

/* Base dir follows the file, so relative sub-sequence paths resolve. */
static void
fix_base_dir (LrgEditorSequencerPanel *self,
              LrgSequence             *seq)
{
	g_autofree gchar *dir = NULL;

	if (self->path == NULL)
		return;
	dir = g_path_get_dirname (self->path);
	lrg_sequence_set_base_dir (seq, dir);
}

/* Internal commit: takes ownership of @edited. */
static gboolean
commit_take (LrgEditorSequencerPanel *self,
             LrgSequence             *edited,
             const gchar             *label)
{
	if (lrg_sequence_equal (self->seq, edited))
	{
		g_object_unref (edited);
		return FALSE;
	}
	fix_base_dir (self, edited);
	record_undo (self, self->seq, label);
	self->seq = edited;
	sequence_replaced (self);
	if (self->app != NULL)
		editor_app_set_status (self->app, "%s", label != NULL ? label : "Edit Sequence");
	return TRUE;
}

static LrgSequence *
working_copy (LrgEditorSequencerPanel *self)
{
	LrgSequence *copy = lrg_sequence_copy (self->seq);

	fix_base_dir (self, copy);
	return copy;
}

/* Replaces the whole document (load, create): no undo, clean. */
static void
reset_document (LrgEditorSequencerPanel *self,
                LrgSequence             *seq,
                const gchar             *abs_path)
{
	if (self->previewing && self->app != NULL)
		editor_timeline_end_preview (self->app);
	self->playing = FALSE;
	{
		/* @abs_path may be self->path itself (re-reset of the open file):
		 * copy before freeing */
		gchar *copy = g_strdup (abs_path);

		g_free (self->path);
		self->path = copy;
	}
	g_set_object (&self->seq, seq);
	fix_base_dir (self, self->seq);
	g_clear_object (&self->saved);
	self->saved = lrg_sequence_copy (self->seq);
	g_ptr_array_set_size (self->undo, 0);
	g_ptr_array_set_size (self->undo_labels, 0);
	g_ptr_array_set_size (self->redo, 0);
	g_ptr_array_set_size (self->redo_labels, 0);
	g_ptr_array_set_size (self->ops, 0);
	self->mode = lrg_sequence_get_play_mode (self->seq);
	lrg_sequence_player_stop (self->player);
	lrg_sequence_player_set_sequence (self->player, self->seq);
	lrg_sequence_player_set_play_mode (self->player, self->mode);
	self->sel_track = -1;
	self->sel_section = -1;
	self->curve_track = -1;
	self->curve_channel = 0;
	self->curve_key = -1;
	self->fit_pending = TRUE;
	self->view_dirty = TRUE;
	set_dirty (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PREVIEW_MODE]);
}

static gchar *
abs_path (LrgEditorSequencerPanel *self,
          const gchar             *path)
{
	if (path == NULL)
		return NULL;
	if (self->app != NULL)
		return editor_app_project_path (self->app, path);
	return g_canonicalize_filename (path, NULL);
}

gboolean
lrg_editor_sequencer_panel_load (LrgEditorSequencerPanel  *self,
                                 const gchar              *path,
                                 GError                  **error)
{
	g_autofree gchar      *abs = NULL;
	g_autoptr(LrgSequence) seq = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	abs = abs_path (self, path);
	seq = lrg_sequence_new_from_file (abs, error);
	if (seq == NULL)
		return FALSE;
	reset_document (self, seq, abs);
	if (self->app != NULL)
		editor_app_set_status (self->app, "Opened sequence %s", abs);
	return TRUE;
}

gboolean
lrg_editor_sequencer_panel_create (LrgEditorSequencerPanel  *self,
                                   const gchar              *path,
                                   GError                  **error)
{
	g_autofree gchar      *abs = NULL;
	g_autofree gchar      *dir = NULL;
	g_autoptr(LrgSequence) seq = lrg_sequence_new ();

	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), FALSE);

	if (path != NULL)
	{
		abs = abs_path (self, path);
		if (!g_str_has_suffix (abs, ".rseq"))
		{
			gchar *with = g_strconcat (abs, ".rseq", NULL);

			g_free (abs);
			abs = with;
		}
		if (g_file_test (abs, G_FILE_TEST_EXISTS))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s already exists", abs);
			return FALSE;
		}
		dir = g_path_get_dirname (abs);
		g_mkdir_with_parents (dir, 0755);
		{
			g_autofree gchar *base = g_path_get_basename (abs);

			base[strlen (base) - 5] = '\0';   /* drop ".rseq" */
			lrg_sequence_set_name (seq, base);
		}
		if (!lrg_sequence_save_to_file (seq, abs, error))
			return FALSE;
	}
	reset_document (self, seq, abs);
	return TRUE;
}

gboolean
lrg_editor_sequencer_panel_save (LrgEditorSequencerPanel  *self,
                                 const gchar              *path,
                                 GError                  **error)
{
	g_autofree gchar *abs = NULL;
	g_autofree gchar *dir = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), FALSE);

	abs = path != NULL ? abs_path (self, path) : g_strdup (self->path);
	if (abs == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME, "The sequence has no file yet: give a path");
		return FALSE;
	}
	dir = g_path_get_dirname (abs);
	g_mkdir_with_parents (dir, 0755);
	if (!lrg_sequence_save_to_file (self->seq, abs, error))
		return FALSE;
	g_free (self->path);
	self->path = g_steal_pointer (&abs);
	fix_base_dir (self, self->seq);
	g_clear_object (&self->saved);
	self->saved = lrg_sequence_copy (self->seq);
	set_dirty (self);
	self->view_dirty = TRUE;
	if (self->app != NULL)
		editor_app_set_status (self->app, "Saved %s", self->path);
	return TRUE;
}

gboolean
lrg_editor_sequencer_panel_commit (LrgEditorSequencerPanel *self,
                                   LrgSequence             *edited,
                                   const gchar             *label)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), FALSE);
	g_return_val_if_fail (LRG_IS_SEQUENCE (edited), FALSE);

	/* the caller keeps its reference: commit a copy */
	return commit_take (self, lrg_sequence_copy (edited), label);
}

static gboolean
step_history (LrgEditorSequencerPanel *self,
              GPtrArray               *from,
              GPtrArray               *from_labels,
              GPtrArray               *to,
              GPtrArray               *to_labels)
{
	LrgSequence *state;
	gchar       *label;

	if (from->len == 0)
		return FALSE;
	state = g_ptr_array_steal_index (from, from->len - 1);
	label = g_ptr_array_steal_index (from_labels, from_labels->len - 1);
	g_ptr_array_add (to, self->seq);
	g_ptr_array_add (to_labels, label);
	self->seq = state;
	fix_base_dir (self, self->seq);
	sequence_replaced (self);
	return TRUE;
}

gboolean
lrg_editor_sequencer_panel_undo (LrgEditorSequencerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), FALSE);
	return step_history (self, self->undo, self->undo_labels, self->redo, self->redo_labels);
}

gboolean
lrg_editor_sequencer_panel_redo (LrgEditorSequencerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), FALSE);
	return step_history (self, self->redo, self->redo_labels, self->undo, self->undo_labels);
}

/* ==========================================================================
 * Preview: nodes, camera, events
 * ========================================================================== */

/* Remembers a channel's pre-preview value in the app's rest table (the
 * same table and key format the level timeline preview uses, so
 * editor_timeline_end_preview() restores both). */
static void
record_rest (LrgEditorSequencerPanel *self,
             LrgNode                 *node,
             const gchar             *channel)
{
	gchar  *key;
	gfloat  value;

	if (self->app == NULL || self->app->timeline_rest == NULL)
		return;
	key = g_strconcat (lrg_node_get_guid (node), "\n", channel, NULL);
	if (g_hash_table_contains (self->app->timeline_rest, key) || !lrg_timeline_channel_read (node, channel, &value))
	{
		g_free (key);
		return;
	}
	g_hash_table_insert (self->app->timeline_rest, key, g_memdup2 (&value, sizeof value));
}

static void
write_channel (LrgEditorSequencerPanel *self,
               LrgNode                 *node,
               const gchar             *channel,
               gfloat                   value)
{
	record_rest (self, node, channel);
	lrg_timeline_channel_write (node, channel, value);
}

static void
restore_camera (LrgEditorSequencerPanel *self)
{
	if (!self->pilot_saved)
		return;
	self->pilot_saved = FALSE;
	lrg_editor_view_state_set_camera (self->view_state, -1, &self->saved_camera);
}

/* Looks through @node (a camera node: its -Z axis, its "fov" param). */
static void
pilot_through (LrgEditorSequencerPanel *self,
               LrgNode                 *node)
{
	LrgEditorViewCamera  cam;
	LrgNodeVisual       *visual = lrg_node_get_visual (node);
	gfloat               m[16];
	gfloat               len;
	gfloat               fwd[3];

	if (!self->pilot_saved)
	{
		self->saved_camera = *lrg_editor_view_state_get_camera (self->view_state, -1);
		self->pilot_saved = TRUE;
	}
	cam = *lrg_editor_view_state_get_camera (self->view_state, -1);
	lrg_node_get_world_matrix (node, m);
	fwd[0] = -m[8];
	fwd[1] = -m[9];
	fwd[2] = -m[10];
	len = sqrtf (fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
	if (len < 1e-6f)
		return;
	cam.view.position[0] = m[12];
	cam.view.position[1] = m[13];
	cam.view.position[2] = m[14];
	cam.view.target[0] = m[12] + fwd[0] / len;
	cam.view.target[1] = m[13] + fwd[1] / len;
	cam.view.target[2] = m[14] + fwd[2] / len;
	cam.view.up[0] = m[4];
	cam.view.up[1] = m[5];
	cam.view.up[2] = m[6];
	cam.view.orthographic = FALSE;
	if (visual != NULL)
		cam.view.fovy = (gfloat) CLAMP (lrg_node_visual_get_param_double (visual, "fov", cam.view.fovy), 1.0, 179.0);
	cam.view_2d = FALSE;
	cam.ortho_from_align = FALSE;
	cam.axis_view = LRG_EDITOR_AXIS_VIEW_NONE;
	lrg_editor_view_state_set_camera (self->view_state, -1, &cam);
}

/* Viewports draw on demand: they redraw on notify::timeline-time (the
 * level timeline preview's signal), so posing nodes outside the history
 * announces itself the same way. */
static void
viewports_dirty (LrgEditorSequencerPanel *self)
{
	if (self->view_state != NULL)
		g_object_notify (G_OBJECT (self->view_state), "timeline-time");
}

/* The camera binding of the last unmuted camera cut track at @t. */
static const gchar *
cut_at_time (LrgSequence *seq,
             gfloat       t)
{
	const gchar *cut = NULL;
	guint        i;

	for (i = 0; i < lrg_sequence_get_n_tracks (seq); i++)
	{
		LrgSequenceTrack *track = lrg_sequence_get_track (seq, i);
		gint              s;

		if (lrg_sequence_track_get_kind (track) != LRG_SEQUENCE_TRACK_CAMERA_CUT || lrg_sequence_track_get_muted (track))
			continue;
		s = lrg_sequence_track_find_section_at (track, t, lrg_sequence_get_length (seq));
		if (s >= 0)
			cut = lrg_sequence_track_get_section (track, (guint) s)->name;
	}
	return cut;
}

/*
 * apply_preview:
 *
 * Poses the level at the player's time: every unmuted transform track
 * writes its keyed channels (bone tracks the bone offset channels) onto
 * its bound node, property tracks whose property is a level channel
 * ("roughness", "color.r", "param.intensity", ...) and that name no
 * component write it too; camera cuts pick the viewport camera when
 * piloting. Runtime-only tracks (audio, animation clips, sub-sequences,
 * component properties) are not previewed on level nodes.
 */
static void
apply_preview (LrgEditorSequencerPanel *self)
{
	gfloat       t = lrg_sequence_player_get_time (self->player);
	const gchar *cut;
	guint        i;
	gint         c;

	if (self->app != NULL && editor_play_is_playing (self->app))
		return;
	if (!self->previewing)
	{
		self->previewing = TRUE;
		lrg_editor_view_state_set_timeline_previewing (self->view_state, TRUE);
	}

	for (i = 0; i < lrg_sequence_get_n_tracks (self->seq); i++)
	{
		LrgSequenceTrack *track = lrg_sequence_get_track (self->seq, i);
		LrgNode          *node;

		if (lrg_sequence_track_get_muted (track) ||
		    (node = lrg_editor_find_node (self->editor, lrg_sequence_track_get_binding (track))) == NULL)
			continue;
		switch (lrg_sequence_track_get_kind (track))
		{
		case LRG_SEQUENCE_TRACK_TRANSFORM:
			for (c = 0; c < LRG_SEQUENCE_N_CHANNELS; c++)
			{
				LrgSequenceCurve *curve = lrg_sequence_track_get_channel_curve (track, (LrgSequenceTransformChannel) c);
				g_autofree gchar *channel = NULL;

				if (curve == NULL || lrg_sequence_curve_get_n_keys (curve) == 0)
					continue;
				channel = node_channel (track, c);
				write_channel (self, node, channel, lrg_sequence_curve_evaluate (curve, t));
			}
			break;
		case LRG_SEQUENCE_TRACK_PROPERTY:
			{
				const gchar      *prop = lrg_sequence_track_get_property_name (track);
				LrgSequenceCurve *curve = lrg_sequence_track_get_curve (track);

				if (lrg_sequence_track_get_component_type (track) == NULL && lrg_timeline_channel_is_valid (prop) &&
				    curve != NULL && lrg_sequence_curve_get_n_keys (curve) > 0)
					write_channel (self, node, prop, lrg_sequence_curve_evaluate (curve, t));
			}
			break;
		default:
			break;
		}
	}

	/* Camera cut at the playhead (the viewport looks through it when piloting) */
	cut = cut_at_time (self->seq, t);
	g_free (self->cut_camera);
	self->cut_camera = g_strdup (cut);
	if (self->pilot)
	{
		LrgNode *cam = cut != NULL ? lrg_editor_find_node (self->editor, cut) : NULL;

		if (cam != NULL)
			pilot_through (self, cam);
		else
			restore_camera (self);
	}
	viewports_dirty (self);
}

/* The app ended the preview (Stop, save, play mode, another timeline):
 * the nodes are already back; forget ours and give the camera back. */
static void
on_previewing_changed (GObject    *object,
                       GParamSpec *pspec,
                       gpointer    user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);

	if (lrg_editor_view_state_get_timeline_previewing (self->view_state) || !self->previewing)
		return;
	self->previewing = FALSE;
	restore_camera (self);
	viewports_dirty (self);
	if (self->playing)
	{
		self->playing = FALSE;
		lrg_sequence_player_pause (self->player);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
	}
}

/* Events crossed by scrubbing or playback: logged and kept for IPC. */
static void
on_player_event (LrgSequencePlayer *player,
                 LrgSequenceTrack  *track,
                 const gchar       *name,
                 const gchar       *payload,
                 gfloat             time,
                 gboolean           reverse,
                 gpointer           user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	g_autofree gchar        *msg = NULL;
	JsonObject              *o = json_object_new ();

	json_object_set_string_member (o, "name", name);
	if (payload != NULL)
		json_object_set_string_member (o, "payload", payload);
	else
		json_object_set_null_member (o, "payload");
	json_object_set_double_member (o, "time", time);
	json_object_set_boolean_member (o, "reverse", reverse);
	json_array_add_object_element (self->fired, o);

	msg = g_strdup_printf ("Event %s%s%s%s at %.3f s%s", name, payload != NULL ? " (" : "",
	                       payload != NULL ? payload : "", payload != NULL ? ")" : "", time,
	                       reverse ? " (backwards)" : "");
	if (self->app != NULL)
		lrg_editor_log_append (self->app->log, G_LOG_LEVEL_MESSAGE, "sequence", msg);
}

static void
on_player_finished (LrgSequencePlayer *player,
                    gpointer           user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);

	if (!self->playing)
		return;
	self->playing = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
}

void
lrg_editor_sequencer_panel_scrub (LrgEditorSequencerPanel *self,
                                  gfloat                   time)
{
	g_return_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self));

	time = CLAMP (snap_time (self, time), 0.0f, lrg_sequence_get_length (self->seq));
	json_array_unref (self->fired);
	self->fired = json_array_new ();
	lrg_sequence_player_scrub (self->player, time);
	apply_preview (self);
}

gfloat
lrg_editor_sequencer_panel_get_time (LrgEditorSequencerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), 0.0f);
	return lrg_sequence_player_get_time (self->player);
}

gboolean
lrg_editor_sequencer_panel_get_playing (LrgEditorSequencerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), FALSE);
	return self->playing;
}

void
lrg_editor_sequencer_panel_set_playing (LrgEditorSequencerPanel *self,
                                        gboolean                 playing)
{
	g_return_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self));

	playing = !!playing;
	if (self->playing == playing)
		return;
	if (playing && self->app != NULL && editor_play_is_playing (self->app))
		return;
	self->playing = playing;
	if (playing)
	{
		/* a finished once preview starts over */
		if (self->mode == LRG_SEQUENCE_PLAY_ONCE &&
		    lrg_sequence_player_get_time (self->player) >= lrg_sequence_get_length (self->seq) - 1e-4f)
			lrg_sequence_player_set_time (self->player, 0.0f);
		lrg_sequence_player_set_play_mode (self->player, self->mode);
		lrg_sequence_player_play (self->player);
		apply_preview (self);
	}
	else
		lrg_sequence_player_pause (self->player);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
}

void
lrg_editor_sequencer_panel_stop (LrgEditorSequencerPanel *self)
{
	g_return_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self));

	lrg_editor_sequencer_panel_set_playing (self, FALSE);
	lrg_sequence_player_stop (self->player);
	lrg_sequence_player_set_time (self->player, 0.0f);
	if (self->previewing && self->app != NULL)
		editor_timeline_end_preview (self->app);
	self->previewing = FALSE;
	restore_camera (self);
	g_clear_pointer (&self->cut_camera, g_free);
	viewports_dirty (self);
}

LrgSequencePlayMode
lrg_editor_sequencer_panel_get_preview_mode (LrgEditorSequencerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), LRG_SEQUENCE_PLAY_ONCE);
	return self->mode;
}

void
lrg_editor_sequencer_panel_set_preview_mode (LrgEditorSequencerPanel *self,
                                             LrgSequencePlayMode      mode)
{
	g_return_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self));

	if (self->mode == mode)
		return;
	self->mode = mode;
	lrg_sequence_player_set_play_mode (self->player, mode);
	self->syncing = TRUE;
	lrg_combo_box_set_selected (self->mode_combo, (gint) mode);
	self->syncing = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PREVIEW_MODE]);
}

gboolean
lrg_editor_sequencer_panel_get_snap (LrgEditorSequencerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), FALSE);
	return self->snap;
}

void
lrg_editor_sequencer_panel_set_snap (LrgEditorSequencerPanel *self,
                                     gboolean                 snap)
{
	g_return_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self));

	snap = !!snap;
	if (self->snap == snap)
		return;
	self->snap = snap;
	editor_chip_set_checked (self->snap_chip, snap);
	self->view_dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SNAP]);
}

gboolean
lrg_editor_sequencer_panel_get_pilot (LrgEditorSequencerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), FALSE);
	return self->pilot;
}

void
lrg_editor_sequencer_panel_set_pilot (LrgEditorSequencerPanel *self,
                                      gboolean                 pilot)
{
	g_return_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self));

	pilot = !!pilot;
	if (self->pilot == pilot)
		return;
	self->pilot = pilot;
	editor_chip_set_checked (self->pilot_chip, pilot);
	if (!pilot)
		restore_camera (self);
	else if (self->previewing)
		apply_preview (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PILOT]);
}

const gchar *
lrg_editor_sequencer_panel_get_cut_camera (LrgEditorSequencerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), NULL);
	return self->cut_camera;
}

/* ==========================================================================
 * Model -> view
 * ========================================================================== */

/* Fills the binding picker of the detail strip when the bindings changed. */
static void
sync_binding_picker (LrgEditorSequencerPanel *self)
{
	GString *sig = g_string_new (NULL);
	guint    i, n = lrg_sequence_get_n_bindings (self->seq);

	for (i = 0; i < n; i++)
	{
		const gchar *guid, *name;

		lrg_sequence_get_binding (self->seq, i, &guid, &name);
		g_string_append_printf (sig, "%s=%s\n", guid, name != NULL ? name : "");
	}
	if (g_strcmp0 (sig->str, self->binding_sig) != 0)
	{
		self->syncing = TRUE;
		lrg_combo_box_remove_all (self->track_binding);
		lrg_combo_box_append (self->track_binding, "(none)");
		for (i = 0; i < n; i++)
		{
			const gchar      *guid;
			g_autofree gchar *label = NULL;

			lrg_sequence_get_binding (self->seq, i, &guid, NULL);
			label = binding_label (self, guid);
			lrg_combo_box_append (self->track_binding, label);
		}
		g_free (self->binding_sig);
		self->binding_sig = g_strdup (sig->str);
		self->syncing = FALSE;
	}
	g_string_free (sig, TRUE);
}

/* Keyframes of one curve on @row. */
static void
add_curve_keys (LrgEditorSequencerPanel *self,
                LrgTimelineViewTrack    *row,
                LrgSequenceCurve        *curve,
                guint                    track,
                gint                     channel)
{
	gdouble lo = G_MAXDOUBLE, hi = -G_MAXDOUBLE;
	guint   k;

	for (k = 0; k < lrg_sequence_curve_get_n_keys (curve); k++)
	{
		LrgSequenceKey               key;
		KeyRef                      *ref = g_new0 (KeyRef, 1);
		LrgTimelineViewInterpolation interp;
		LrgCanvasKeyShape            shape;
		gfloat                       in_s = 0.0f, out_s = 0.0f;
		guint                        id;

		lrg_sequence_curve_get_key (curve, k, &key);
		lrg_sequence_curve_get_slopes (curve, k, &in_s, &out_s);
		ref->track = track;
		ref->channel = channel;
		ref->time = key.time;
		ref->value = key.value;
		switch (key.tangent_mode)
		{
		case LRG_SEQUENCE_TANGENT_CONSTANT:
			interp = LRG_TIMELINE_VIEW_INTERPOLATION_STEP;
			shape = LRG_CANVAS_KEY_SHAPE_SQUARE;
			break;
		case LRG_SEQUENCE_TANGENT_LINEAR:
			interp = LRG_TIMELINE_VIEW_INTERPOLATION_BEZIER;
			shape = LRG_CANVAS_KEY_SHAPE_DIAMOND;
			break;
		default:
			interp = LRG_TIMELINE_VIEW_INTERPOLATION_BEZIER;
			shape = LRG_CANVAS_KEY_SHAPE_CIRCLE;
			break;
		}
		id = lrg_timeline_view_add_keyframe (self->view, row, key.time, key.value, interp);
		lrg_timeline_view_set_keyframe_tangents (self->view, id, in_s, out_s);
		lrg_timeline_view_set_keyframe_shape (self->view, id, shape);
		g_hash_table_insert (self->keys, GUINT_TO_POINTER (id), ref);
		lo = MIN (lo, key.value);
		hi = MAX (hi, key.value);
	}
	if (lo <= hi)
		lrg_timeline_view_track_set_value_range (row, lo - MAX (0.5, (hi - lo) * 0.15),
		                                         hi + MAX (0.5, (hi - lo) * 0.15));
}

/* The label drawn on a section's clip. */
static gchar *
section_label (LrgEditorSequencerPanel  *self,
               LrgSequenceTrackKind      kind,
               const LrgSequenceSection *s)
{
	switch (kind)
	{
	case LRG_SEQUENCE_TRACK_CAMERA_CUT:
		return binding_label (self, s->name);
	case LRG_SEQUENCE_TRACK_AUDIO:
		return s->weight != 1.0f ? g_strdup_printf ("%s  %.0f%%", s->name, s->weight * 100.0f) : g_strdup (s->name);
	case LRG_SEQUENCE_TRACK_ANIMATION:
	case LRG_SEQUENCE_TRACK_SUBSEQUENCE:
		return s->rate != 1.0f ? g_strdup_printf ("%s  x%.2g", s->name, s->rate) : g_strdup (s->name);
	default:
		return g_strdup (s->name);
	}
}

/* Rows, keys, clips and markers of track @i under @parent. */
static void
add_track_rows (LrgEditorSequencerPanel *self,
                guint                    i,
                LrgTimelineViewTrack    *parent,
                const GrlColor          *group_color)
{
	LrgSequenceTrack     *track = lrg_sequence_get_track (self->seq, i);
	LrgSequenceTrackKind  kind = lrg_sequence_track_get_kind (track);
	g_autofree gchar     *title = track_title (track);
	g_autofree gchar     *key = row_key_track (i);
	gboolean              curves = editor_chip_get_checked (self->curves_chip);
	LrgTimelineViewTrack *row;
	GrlColor              color;
	guint                 s;
	gint                  c;

	row = lrg_timeline_view_add_track (self->view, title, parent);
	if (kind == LRG_SEQUENCE_TRACK_TRANSFORM || kind == LRG_SEQUENCE_TRACK_PROPERTY)
		color = *group_color;
	else
		kind_color (kind, &color);
	lrg_timeline_view_track_set_color (row, &color);
	lrg_timeline_view_track_set_muted (row, lrg_sequence_track_get_muted (track));
	if (g_hash_table_contains (self->collapsed, key))
		lrg_timeline_view_track_set_collapsed (row, GPOINTER_TO_INT (g_hash_table_lookup (self->collapsed, key)));
	g_hash_table_insert (self->rows, g_strdup (key), row);

	switch (kind)
	{
	case LRG_SEQUENCE_TRACK_TRANSFORM:
		for (c = 0; c < LRG_SEQUENCE_N_CHANNELS; c++)
		{
			LrgSequenceCurve     *curve = lrg_sequence_track_get_channel_curve (track, (LrgSequenceTransformChannel) c);
			LrgTimelineViewTrack *crow;

			if (curve == NULL)
				continue;
			crow = lrg_timeline_view_add_track (self->view,
			                                    lrg_sequence_transform_channel_to_string ((LrgSequenceTransformChannel) c),
			                                    row);
			lrg_timeline_view_track_set_color (crow, &color);
			lrg_timeline_view_track_set_curve_lane (crow, curves);
			g_hash_table_insert (self->rows, row_key_channel (i, c), crow);
			add_curve_keys (self, crow, curve, i, c);
		}
		break;

	case LRG_SEQUENCE_TRACK_PROPERTY:
		lrg_timeline_view_track_set_curve_lane (row, curves);
		add_curve_keys (self, row, lrg_sequence_track_get_curve (track), i, CH_CURVE);
		break;

	case LRG_SEQUENCE_TRACK_EVENT:
		for (s = 0; s < lrg_sequence_track_get_n_sections (track); s++)
		{
			const LrgSequenceSection *sec = lrg_sequence_track_get_section (track, s);
			KeyRef                   *ref = g_new0 (KeyRef, 1);
			KeyRef                   *mref;
			guint                     id;

			ref->track = i;
			ref->channel = CH_EVENT;
			ref->time = sec->start;
			id = lrg_timeline_view_add_keyframe (self->view, row, sec->start, 0.0,
			                                     LRG_TIMELINE_VIEW_INTERPOLATION_STEP);
			lrg_timeline_view_set_keyframe_shape (self->view, id, LRG_CANVAS_KEY_SHAPE_BAR);
			g_hash_table_insert (self->keys, GUINT_TO_POINTER (id), ref);

			/* and a flag on the ruler */
			mref = g_memdup2 (ref, sizeof *ref);
			id = lrg_timeline_view_add_marker (self->view, sec->start, sec->name);
			g_hash_table_insert (self->markers, GUINT_TO_POINTER (id), mref);
		}
		break;

	default:
		/* camera cut, audio, animation, sub-sequence: sections as clips */
		lrg_timeline_view_track_set_allow_overlap (row, TRUE);
		for (s = 0; s < lrg_sequence_track_get_n_sections (track); s++)
		{
			const LrgSequenceSection *sec = lrg_sequence_track_get_section (track, s);
			gfloat                    end = lrg_sequence_track_get_section_end (track, s,
			                                                                    lrg_sequence_get_length (self->seq));
			g_autofree gchar         *label = section_label (self, kind, sec);
			ClipRef                  *ref = g_new0 (ClipRef, 1);
			guint                     id;

			/* a sound with no duration plays to its end: draw one second */
			if (end <= sec->start + TIME_EPSILON)
				end = sec->start + MAX (1.0f / seq_fps (self), kind == LRG_SEQUENCE_TRACK_AUDIO ? 1.0f : 0.0f);
			ref->track = i;
			ref->start = sec->start;
			ref->duration = end - sec->start;
			ref->open = sec->duration <= 0.0f;
			id = lrg_timeline_view_add_clip (self->view, row, sec->start, ref->duration, label);
			if (id == LRG_TIMELINE_VIEW_INVALID_ID)
			{
				g_free (ref);
				continue;
			}
			lrg_timeline_view_set_clip_color (self->view, id, &color);
			g_hash_table_insert (self->clips, GUINT_TO_POINTER (id), ref);
		}
		break;
	}
}

/* Selected view keys / clips as model references, for a rebuild. */
typedef struct
{
	GArray *keys;    /* KeyRef */
	GArray *clips;   /* ClipRef */
} Selection;

static void
save_selection (LrgEditorSequencerPanel *self,
                Selection               *sel)
{
	g_autoptr(GArray) ids = lrg_timeline_view_get_selected_keyframes (self->view);
	g_autoptr(GArray) cids = lrg_timeline_view_get_selected_clips (self->view);
	guint             i;

	sel->keys = g_array_new (FALSE, FALSE, sizeof (KeyRef));
	sel->clips = g_array_new (FALSE, FALSE, sizeof (ClipRef));
	for (i = 0; ids != NULL && i < ids->len; i++)
	{
		KeyRef *k = g_hash_table_lookup (self->keys, GUINT_TO_POINTER (g_array_index (ids, guint, i)));

		if (k != NULL)
			g_array_append_val (sel->keys, *k);
	}
	for (i = 0; cids != NULL && i < cids->len; i++)
	{
		ClipRef *c = g_hash_table_lookup (self->clips, GUINT_TO_POINTER (g_array_index (cids, guint, i)));

		if (c != NULL)
			g_array_append_val (sel->clips, *c);
	}
}

static guint
find_key_id (LrgEditorSequencerPanel *self,
             guint                    track,
             gint                     channel,
             gfloat                   time)
{
	GHashTableIter iter;
	gpointer       id, value;

	g_hash_table_iter_init (&iter, self->keys);
	while (g_hash_table_iter_next (&iter, &id, &value))
	{
		KeyRef *k = value;

		if (k->track == track && k->channel == channel && fabsf (k->time - time) < TIME_EPSILON)
			return GPOINTER_TO_UINT (id);
	}
	return LRG_TIMELINE_VIEW_INVALID_ID;
}

static guint
find_clip_id (LrgEditorSequencerPanel *self,
              guint                    track,
              gfloat                   start)
{
	GHashTableIter iter;
	gpointer       id, value;

	g_hash_table_iter_init (&iter, self->clips);
	while (g_hash_table_iter_next (&iter, &id, &value))
	{
		ClipRef *c = value;

		if (c->track == track && fabsf (c->start - start) < TIME_EPSILON)
			return GPOINTER_TO_UINT (id);
	}
	return LRG_TIMELINE_VIEW_INVALID_ID;
}

/* Rebuilds every row, key, clip and marker of the view. */
static void
rebuild (LrgEditorSequencerPanel *self)
{
	g_autoptr(GPtrArray) groups = g_ptr_array_new_with_free_func (g_free);
	Selection            sel;
	GHashTableIter       iter;
	gpointer             key, value;
	guint                i, g;

	self->view_dirty = FALSE;
	self->syncing = TRUE;
	save_selection (self, &sel);

	/* Remember collapsed rows */
	g_hash_table_iter_init (&iter, self->rows);
	while (g_hash_table_iter_next (&iter, &key, &value))
		g_hash_table_replace (self->collapsed, g_strdup (key),
		                      GINT_TO_POINTER (lrg_timeline_view_track_get_collapsed (value)));

	/* Empty the view */
	while (lrg_timeline_view_get_n_tracks (self->view) > 0)
		lrg_timeline_view_remove_track (self->view,
		                                lrg_timeline_view_get_track (self->view,
		                                                             lrg_timeline_view_get_n_tracks (self->view) - 1));
	g_hash_table_iter_init (&iter, self->markers);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		lrg_timeline_view_remove_marker (self->view, GPOINTER_TO_UINT (key));
	g_hash_table_remove_all (self->markers);
	g_hash_table_remove_all (self->keys);
	g_hash_table_remove_all (self->clips);
	g_hash_table_remove_all (self->rows);

	lrg_timeline_view_set_duration (self->view, MAX (lrg_sequence_get_length (self->seq), 0.05f));
	lrg_timeline_view_set_fps (self->view, seq_fps (self));
	lrg_timeline_view_set_snap (self->view, self->snap
	                            ? (LRG_TIMELINE_VIEW_SNAP_GRID | LRG_TIMELINE_VIEW_SNAP_PLAYHEAD |
	                               LRG_TIMELINE_VIEW_SNAP_CLIPS | LRG_TIMELINE_VIEW_SNAP_MARKERS)
	                            : (LRG_TIMELINE_VIEW_SNAP_PLAYHEAD | LRG_TIMELINE_VIEW_SNAP_CLIPS |
	                               LRG_TIMELINE_VIEW_SNAP_MARKERS));

	/* Groups: declared bindings first, then bindings only tracks use, then
	 * the unbound tracks ("") */
	for (i = 0; i < lrg_sequence_get_n_bindings (self->seq); i++)
	{
		const gchar *guid;

		lrg_sequence_get_binding (self->seq, i, &guid, NULL);
		g_ptr_array_add (groups, g_strdup (guid));
	}
	for (i = 0; i < lrg_sequence_get_n_tracks (self->seq); i++)
	{
		const gchar *b = lrg_sequence_track_get_binding (lrg_sequence_get_track (self->seq, i));
		gboolean     seen = FALSE;

		if (b == NULL)
			b = "";
		for (g = 0; g < groups->len && !seen; g++)
			seen = g_strcmp0 (g_ptr_array_index (groups, g), b) == 0;
		if (!seen)
			g_ptr_array_add (groups, g_strdup (b));
	}

	for (g = 0; g < groups->len; g++)
	{
		const gchar          *b = g_ptr_array_index (groups, g);
		g_autofree gchar     *label = binding_label (self, *b != '\0' ? b : NULL);
		g_autofree gchar     *gkey = g_strconcat ("b:", b, NULL);
		LrgTimelineViewTrack *group;
		GrlColor              color;

		group = lrg_timeline_view_add_track (self->view, label, NULL);
		wheel_color (g, &color);
		lrg_timeline_view_track_set_color (group, &color);
		if (g_hash_table_contains (self->collapsed, gkey))
			lrg_timeline_view_track_set_collapsed (group, GPOINTER_TO_INT (g_hash_table_lookup (self->collapsed, gkey)));
		g_hash_table_insert (self->rows, g_strdup (gkey), group);

		for (i = 0; i < lrg_sequence_get_n_tracks (self->seq); i++)
		{
			const gchar *tb = lrg_sequence_track_get_binding (lrg_sequence_get_track (self->seq, i));

			if (g_strcmp0 (tb != NULL ? tb : "", b) == 0)
				add_track_rows (self, i, group, &color);
		}
	}

	/* Restore the selection */
	for (i = 0; i < sel.keys->len; i++)
	{
		KeyRef *k = &g_array_index (sel.keys, KeyRef, i);
		guint   id = find_key_id (self, k->track, k->channel, k->time);

		if (id != LRG_TIMELINE_VIEW_INVALID_ID)
			lrg_timeline_view_select_keyframe (self->view, id, TRUE);
	}
	for (i = 0; i < sel.clips->len; i++)
	{
		ClipRef *c = &g_array_index (sel.clips, ClipRef, i);
		guint    id = find_clip_id (self, c->track, c->start);

		if (id != LRG_TIMELINE_VIEW_INVALID_ID)
			lrg_timeline_view_select_clip (self->view, id, TRUE);
	}
	g_array_unref (sel.keys);
	g_array_unref (sel.clips);
	lrg_timeline_view_set_time (self->view, lrg_sequence_player_get_time (self->player));

	/* Selections pointing past the end are dropped */
	if (self->sel_track >= (gint) lrg_sequence_get_n_tracks (self->seq))
		self->sel_track = -1;
	if (self->sel_track < 0 || self->sel_section >= (gint) lrg_sequence_track_get_n_sections (
	        lrg_sequence_get_track (self->seq, (guint) MAX (self->sel_track, 0))))
		self->sel_section = -1;
	if (curve_of (self->seq, self->curve_track, self->curve_channel) == NULL)
		self->curve_track = -1;

	lrg_widget_set_visible (self->hint, lrg_sequence_get_n_tracks (self->seq) == 0);
	lrg_widget_set_visible (LRG_WIDGET (self->view), lrg_sequence_get_n_tracks (self->seq) > 0);
	sync_binding_picker (self);
	self->syncing = FALSE;
	sync_detail (self);
}

/* Toolbar widgets mirroring the document, per frame. */
static void
sync_toolbar (LrgEditorSequencerPanel *self)
{
	g_autofree gchar *title = NULL;
	g_autofree gchar *text = NULL;
	gfloat            t = lrg_sequence_player_get_time (self->player);
	g_autofree gchar *base = self->path != NULL ? g_path_get_basename (self->path) : g_strdup ("Untitled.rseq");

	title = g_strdup_printf ("%s%s", base, self->dirty ? " *" : "");
	if (g_strcmp0 (lrg_label_get_text (LRG_LABEL (self->title)), title) != 0)
		lrg_label_set_text (LRG_LABEL (self->title), title);

	self->syncing = TRUE;
	if (fabs (lrg_spin_box_get_value (self->length) - lrg_sequence_get_length (self->seq)) > 1e-4)
		lrg_spin_box_set_value (self->length, lrg_sequence_get_length (self->seq));
	if (fabs (lrg_spin_box_get_value (self->fps) - lrg_sequence_get_fps (self->seq)) > 1e-4)
		lrg_spin_box_set_value (self->fps, lrg_sequence_get_fps (self->seq));
	if (lrg_combo_box_get_selected (self->mode_combo) != (gint) self->mode)
		lrg_combo_box_set_selected (self->mode_combo, (gint) self->mode);
	if (lrg_timeline_view_get_playing (self->view))
		lrg_timeline_view_set_playing (self->view, FALSE);
	if (fabs (lrg_timeline_view_get_time (self->view) - t) > 1e-5)
		lrg_timeline_view_set_time (self->view, t);
	self->syncing = FALSE;

	lrg_widget_set_enabled (self->undo_chip, self->undo->len > 0);
	lrg_widget_set_enabled (self->redo_chip, self->redo->len > 0);
	editor_chip_set_checked (self->play, self->playing);
	editor_chip_set_icon (self->play, self->playing ? LRG_ICON_PAUSE : LRG_ICON_PLAY);
	editor_chip_set_label (self->play, self->playing ? "Pause" : "Play");
	text = g_strdup_printf ("%d / %d  ·  %.2f s", lrg_sequence_time_to_frame (self->seq, t),
	                        lrg_sequence_time_to_frame (self->seq, lrg_sequence_get_length (self->seq)), t);
	if (g_strcmp0 (lrg_label_get_text (LRG_LABEL (self->time_label)), text) != 0)
		lrg_label_set_text (LRG_LABEL (self->time_label), text);
}

/* ==========================================================================
 * Detail strip (track, section and key fields)
 * ========================================================================== */

static LrgSequenceTrack *
sel_track (LrgEditorSequencerPanel *self)
{
	if (self->sel_track < 0 || (guint) self->sel_track >= lrg_sequence_get_n_tracks (self->seq))
		return NULL;
	return lrg_sequence_get_track (self->seq, (guint) self->sel_track);
}

static const LrgSequenceSection *
sel_section (LrgEditorSequencerPanel *self)
{
	LrgSequenceTrack *t = sel_track (self);

	if (t == NULL || self->sel_section < 0 || (guint) self->sel_section >= lrg_sequence_track_get_n_sections (t))
		return NULL;
	return lrg_sequence_track_get_section (t, (guint) self->sel_section);
}

static void
set_spin (LrgSpinBox *spin,
          gdouble     value)
{
	if (fabs (lrg_spin_box_get_value (spin) - value) > 1e-6)
		lrg_spin_box_set_value (spin, value);
}

static void
set_text (LrgTextInput *input,
          const gchar  *text)
{
	if (g_strcmp0 (lrg_text_input_get_text (input), text != NULL ? text : "") != 0)
		lrg_text_input_set_text (input, text != NULL ? text : "");
}

/* Fills the detail strip from the selection. */
static void
sync_detail (LrgEditorSequencerPanel *self)
{
	LrgSequenceTrack         *t = sel_track (self);
	const LrgSequenceSection *s = sel_section (self);
	LrgSequenceCurve         *curve = curve_of (self->seq, self->curve_track, self->curve_channel);
	gboolean                  has_key;
	g_autofree gchar         *title = NULL;

	self->syncing = TRUE;

	/* Title: what the fields edit */
	if (t == NULL)
		title = g_strdup ("Select a track, key or section");
	else if (s != NULL)
		title = g_strdup_printf ("%s  ·  section at %.2f s", kind_label (lrg_sequence_track_get_kind (t)), s->start);
	else
	{
		g_autofree gchar *tt = track_title (t);

		title = g_strdup_printf ("%s  ·  %s", tt, kind_label (lrg_sequence_track_get_kind (t)));
	}
	lrg_label_set_text (LRG_LABEL (self->detail_title), title);

	/* Track fields */
	lrg_widget_set_visible (self->track_box, t != NULL && s == NULL);
	if (t != NULL)
	{
		const gchar *b = lrg_sequence_track_get_binding (t);
		gint         row = 0;
		guint        i;

		set_text (self->track_name, lrg_sequence_track_get_name (t));
		for (i = 0; b != NULL && i < lrg_sequence_get_n_bindings (self->seq); i++)
		{
			const gchar *guid;

			lrg_sequence_get_binding (self->seq, i, &guid, NULL);
			if (g_strcmp0 (guid, b) == 0)
				row = (gint) i + 1;
		}
		if (lrg_combo_box_get_selected (self->track_binding) != row)
			lrg_combo_box_set_selected (self->track_binding, row);
		editor_chip_set_checked (self->track_mute, lrg_sequence_track_get_muted (t));
		lrg_widget_set_visible (self->bone_row, lrg_sequence_track_get_kind (t) == LRG_SEQUENCE_TRACK_TRANSFORM);
		lrg_widget_set_visible (self->prop_row, lrg_sequence_track_get_kind (t) == LRG_SEQUENCE_TRACK_PROPERTY);
		set_text (self->track_bone, lrg_sequence_track_get_bone (t));
		set_text (self->track_component, lrg_sequence_track_get_component_type (t));
		set_text (self->track_property, lrg_sequence_track_get_property_name (t));
	}

	/* Section fields */
	lrg_widget_set_visible (self->section_box, s != NULL);
	if (s != NULL)
	{
		set_text (self->sec_name, s->name);
		set_spin (self->sec_start, s->start);
		set_spin (self->sec_duration, s->duration);
		set_spin (self->sec_offset, s->offset);
		set_spin (self->sec_rate, s->rate);
		set_spin (self->sec_weight, s->weight);
		editor_chip_set_checked (self->sec_loop, s->loop);
		lrg_widget_set_visible (self->payload_row, lrg_sequence_track_get_kind (t) == LRG_SEQUENCE_TRACK_EVENT);
		set_text (self->sec_payload, s->payload);
	}

	/* Key fields (the curve editor's selected key) */
	has_key = curve != NULL && self->curve_key >= 0 && (guint) self->curve_key < lrg_sequence_curve_get_n_keys (curve);
	if (!has_key)
		self->curve_key = -1;
	lrg_widget_set_enabled (LRG_WIDGET (self->key_time), has_key);
	lrg_widget_set_enabled (LRG_WIDGET (self->key_value), has_key);
	lrg_widget_set_enabled (LRG_WIDGET (self->key_tangent), has_key);
	lrg_widget_set_enabled (LRG_WIDGET (self->key_in), has_key);
	lrg_widget_set_enabled (LRG_WIDGET (self->key_out), has_key);
	if (has_key)
	{
		LrgSequenceKey key;
		gfloat         in_s = 0.0f, out_s = 0.0f;

		lrg_sequence_curve_get_key (curve, (guint) self->curve_key, &key);
		lrg_sequence_curve_get_slopes (curve, (guint) self->curve_key, &in_s, &out_s);
		set_spin (self->key_time, key.time);
		set_spin (self->key_value, key.value);
		if (lrg_combo_box_get_selected (self->key_tangent) != (gint) key.tangent_mode)
			lrg_combo_box_set_selected (self->key_tangent, (gint) key.tangent_mode);
		set_spin (self->key_in, in_s);
		set_spin (self->key_out, out_s);
	}
	self->syncing = FALSE;
}

/* ==========================================================================
 * View -> model
 * ========================================================================== */

static void
queue (LrgEditorSequencerPanel *self,
       OpKind                   kind,
       guint                    track,
       gint                     channel,
       gfloat                   from,
       gfloat                   to,
       gfloat                   value,
       gboolean                 value_changed,
       gfloat                   duration)
{
	Op *op = g_new0 (Op, 1);

	op->kind = kind;
	op->track = track;
	op->channel = channel;
	op->from = from;
	op->to = to;
	op->value = value;
	op->value_changed = value_changed;
	op->duration = duration;
	g_ptr_array_add (self->ops, op);
}

static void
on_keyframe_moved (LrgTimelineView      *view,
                   LrgTimelineViewTrack *row,
                   guint                 id,
                   gpointer              user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	KeyRef                  *ref = g_hash_table_lookup (self->keys, GUINT_TO_POINTER (id));
	gfloat                   v;

	if (self->syncing || ref == NULL)
		return;
	v = (gfloat) lrg_timeline_view_get_keyframe_value (view, id);
	queue (self, OP_KEY_MOVE, ref->track, ref->channel, ref->time,
	       snap_time (self, (gfloat) lrg_timeline_view_get_keyframe_time (view, id)), v,
	       ref->channel != CH_EVENT && fabsf (v - ref->value) > 1e-6f, -1.0f);
}

static void
on_keyframe_removed (LrgTimelineView      *view,
                     LrgTimelineViewTrack *row,
                     guint                 id,
                     gpointer              user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	KeyRef                  *ref = g_hash_table_lookup (self->keys, GUINT_TO_POINTER (id));

	if (self->syncing || ref == NULL)
		return;
	queue (self, OP_KEY_REMOVE, ref->track, ref->channel, ref->time, ref->time, ref->value, FALSE, -1.0f);
}

/* A key added in the view (double click on a curve lane). */
static void
on_keyframe_added (LrgTimelineView      *view,
                   LrgTimelineViewTrack *row,
                   guint                 id,
                   gpointer              user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	GHashTableIter           iter;
	gpointer                 key, value;

	if (self->syncing)
		return;
	g_hash_table_iter_init (&iter, self->rows);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		guint track;
		gint  channel;

		if (value != row)
			continue;
		if (sscanf (key, "c:%u:%d", &track, &channel) == 2 ||
		    (sscanf (key, "t:%u", &track) == 1 && (channel = CH_CURVE) == CH_CURVE &&
		     curve_of (self->seq, (gint) track, CH_CURVE) != NULL))
		{
			gfloat t = snap_time (self, (gfloat) lrg_timeline_view_get_keyframe_time (view, id));

			queue (self, OP_KEY_ADD, track, channel, t, t, (gfloat) lrg_timeline_view_get_keyframe_value (view, id),
			       TRUE, -1.0f);
		}
		return;
	}
}

static void
on_clip_changed (LrgTimelineView *view,
                 guint            id,
                 gboolean         resized,
                 gpointer         user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	ClipRef                 *ref = g_hash_table_lookup (self->clips, GUINT_TO_POINTER (id));
	gfloat                   start, dur;

	if (self->syncing || ref == NULL)
		return;
	start = snap_time (self, (gfloat) lrg_timeline_view_get_clip_start (view, id));
	dur = (gfloat) lrg_timeline_view_get_clip_duration (view, id);
	/* a move keeps an open-ended section open; resizing gives it a length */
	queue (self, OP_SECTION_MOVE, ref->track, 0, ref->start, start, 0.0f, FALSE,
	       resized ? MAX (snap_time (self, start + dur) - start, 1.0f / seq_fps (self))
	               : (ref->open ? -1.0f : ref->duration));
}

static void
on_clip_moved (LrgTimelineView *view,
               guint            id,
               gpointer         user_data)
{
	on_clip_changed (view, id, FALSE, user_data);
}

static void
on_clip_resized (LrgTimelineView *view,
                 guint            id,
                 gpointer         user_data)
{
	on_clip_changed (view, id, TRUE, user_data);
}

static void
on_clip_removed (LrgTimelineView *view,
                 guint            id,
                 gpointer         user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	ClipRef                 *ref = g_hash_table_lookup (self->clips, GUINT_TO_POINTER (id));

	if (self->syncing || ref == NULL)
		return;
	queue (self, OP_SECTION_REMOVE, ref->track, 0, ref->start, ref->start, 0.0f, FALSE, -1.0f);
}

/* An event flag dragged on the ruler. */
static void
on_marker_moved (LrgTimelineView *view,
                 guint            marker,
                 gdouble          old_time,
                 gdouble          new_time,
                 gpointer         user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	KeyRef                  *ref = g_hash_table_lookup (self->markers, GUINT_TO_POINTER (marker));

	if (self->syncing || ref == NULL)
		return;
	queue (self, OP_KEY_MOVE, ref->track, CH_EVENT, ref->time, snap_time (self, (gfloat) new_time), 0.0f, FALSE,
	       -1.0f);
}

static void
on_playhead_moved (LrgTimelineView *view,
                   gdouble          time,
                   gpointer         user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);

	if (self->syncing)
		return;
	lrg_editor_sequencer_panel_scrub (self, (gfloat) time);
}

/* Selection in the view picks the detail strip's track, section and curve. */
static void
on_selection_changed (LrgTimelineView *view,
                      gpointer         user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	g_autoptr(GArray)        ids = NULL;
	g_autoptr(GArray)        cids = NULL;
	KeyRef                  *k = NULL;
	ClipRef                 *c = NULL;

	if (self->syncing)
		return;
	ids = lrg_timeline_view_get_selected_keyframes (view);
	cids = lrg_timeline_view_get_selected_clips (view);
	if (ids != NULL && ids->len > 0)
		k = g_hash_table_lookup (self->keys, GUINT_TO_POINTER (g_array_index (ids, guint, 0)));
	if (cids != NULL && cids->len > 0)
		c = g_hash_table_lookup (self->clips, GUINT_TO_POINTER (g_array_index (cids, guint, 0)));
	if (k != NULL)
	{
		self->sel_track = (gint) k->track;
		self->sel_section = -1;
		if (k->channel == CH_EVENT)
			self->sel_section = find_section (lrg_sequence_get_track (self->seq, k->track), k->time);
		else
		{
			LrgSequenceCurve *curve = curve_of (self->seq, (gint) k->track, k->channel);

			self->curve_track = (gint) k->track;
			self->curve_channel = k->channel;
			self->curve_key = curve != NULL ? lrg_sequence_curve_find_key (curve, k->time, TIME_EPSILON) : -1;
		}
	}
	else if (c != NULL)
	{
		self->sel_track = (gint) c->track;
		self->sel_section = find_section (lrg_sequence_get_track (self->seq, c->track), c->start);
	}
	sync_detail (self);
}

/* Applies one queued op to @edit; returns whether it did anything. */
static gboolean
apply_op (LrgEditorSequencerPanel *self,
          LrgSequence             *edit,
          Op                      *op)
{
	LrgSequenceTrack *track;

	if (op->track >= lrg_sequence_get_n_tracks (edit))
		return FALSE;
	track = lrg_sequence_get_track (edit, op->track);

	if (op->kind == OP_SECTION_MOVE || op->kind == OP_SECTION_REMOVE || op->channel == CH_EVENT)
	{
		gint                          s = find_section (track, op->from);
		g_autoptr(LrgSequenceSection) copy = NULL;

		if (s < 0)
			return FALSE;
		if (op->kind == OP_SECTION_REMOVE || op->kind == OP_KEY_REMOVE)
			return lrg_sequence_track_remove_section (track, (guint) s);
		copy = lrg_sequence_section_copy (lrg_sequence_track_get_section (track, (guint) s));
		copy->start = op->to;
		if (op->duration >= 0.0f)
			copy->duration = op->duration;
		replace_section (track, (guint) s, copy);
		return TRUE;
	}

	{
		LrgSequenceCurve *curve = curve_of (edit, (gint) op->track, op->channel);
		LrgSequenceKey    key;
		gint              k;

		if (curve == NULL && op->kind == OP_KEY_ADD && op->channel >= 0 &&
		    lrg_sequence_track_get_kind (track) == LRG_SEQUENCE_TRACK_TRANSFORM)
			curve = lrg_sequence_track_ensure_channel_curve (track, (LrgSequenceTransformChannel) op->channel);
		if (curve == NULL)
			return FALSE;
		if (op->kind == OP_KEY_ADD)
		{
			lrg_sequence_curve_set_key (curve, op->to, op->value, LRG_SEQUENCE_TANGENT_AUTO);
			return TRUE;
		}
		k = lrg_sequence_curve_find_key (curve, op->from, TIME_EPSILON);
		if (k < 0)
			return FALSE;
		if (op->kind == OP_KEY_REMOVE)
			return lrg_sequence_curve_remove_key (curve, (guint) k);
		/* move: keep the tangents; a value change re-inserts the key */
		lrg_sequence_curve_get_key (curve, (guint) k, &key);
		if (op->value_changed)
		{
			lrg_sequence_curve_remove_key (curve, (guint) k);
			key.time = op->to;
			key.value = op->value;
			lrg_sequence_curve_insert_key (curve, &key);
		}
		else if (fabsf (op->to - op->from) >= TIME_EPSILON)
			lrg_sequence_curve_move_key (curve, (guint) k, op->to);
		return TRUE;
	}
}

/* Applies the queued view edits as one undo step. */
static void
flush_ops (LrgEditorSequencerPanel *self)
{
	LrgSequence *edit;
	const gchar *label;
	guint        i;

	if (self->ops->len == 0)
		return;
	switch (((Op *) g_ptr_array_index (self->ops, 0))->kind)
	{
	case OP_KEY_MOVE:       label = "Move Keys"; break;
	case OP_KEY_REMOVE:     label = "Delete Keys"; break;
	case OP_KEY_ADD:        label = "Add Key"; break;
	case OP_SECTION_MOVE:   label = "Move Section"; break;
	default:                label = "Delete Section"; break;
	}
	edit = working_copy (self);
	for (i = 0; i < self->ops->len; i++)
		apply_op (self, edit, g_ptr_array_index (self->ops, i));
	g_ptr_array_set_size (self->ops, 0);
	if (!commit_take (self, edit, label))
		self->view_dirty = TRUE;   /* a no-op drag: put the view back */
}

/* ==========================================================================
 * Curve editor (LrgCustomWidget)
 * ========================================================================== */

/* The plot area inside the widget: room for value labels on the left,
 * a title line above and time labels below. */
static void
curve_graph (LrgEditorSequencerPanel *self,
             gfloat                  *gx,
             gfloat                  *gy,
             gfloat                  *gw,
             gfloat                  *gh)
{
	LrgWidget *w = LRG_WIDGET (self->curve);

	*gx = lrg_widget_get_world_x (w) + 48.0f;
	*gy = lrg_widget_get_world_y (w) + 24.0f;
	*gw = MAX (10.0f, lrg_widget_get_width (w) - 58.0f);
	*gh = MAX (10.0f, lrg_widget_get_height (w) - 44.0f);
}

/* Handle points of key @i: the in handle a third of the previous segment
 * back along the in slope, the out handle a third of the next segment
 * ahead along the out slope (the cubic Bézier control points). */
static gboolean
handle_point (LrgSequenceCurve *curve,
              guint             i,
              gint              side,
              gfloat           *t,
              gfloat           *v)
{
	LrgSequenceKey key, other;
	gfloat         in_s = 0.0f, out_s = 0.0f, dt;
	guint          n = lrg_sequence_curve_get_n_keys (curve);

	if (i >= n || !lrg_sequence_curve_get_key (curve, i, &key))
		return FALSE;
	if (side == 0)
	{
		*t = key.time;
		*v = key.value;
		return TRUE;
	}
	if ((side < 0 && i == 0) || (side > 0 && i + 1 >= n))
		return FALSE;
	lrg_sequence_curve_get_key (curve, side < 0 ? i - 1 : i + 1, &other);
	lrg_sequence_curve_get_slopes (curve, i, &in_s, &out_s);
	dt = fabsf (other.time - key.time) / 3.0f;
	*t = key.time + (side < 0 ? -dt : dt);
	*v = key.value + (side < 0 ? -in_s * dt : out_s * dt);
	return TRUE;
}

/* Time and value ranges shown (frozen while dragging). */
static void
curve_ranges (LrgEditorSequencerPanel *self,
              LrgSequenceCurve        *curve)
{
	gfloat lo = G_MAXFLOAT, hi = -G_MAXFLOAT, t1 = lrg_sequence_get_length (self->seq), pad;
	guint  i, n;
	gint   side;

	if (self->drag != DRAG_NONE && self->drag_moved)
		return;
	n = curve != NULL ? lrg_sequence_curve_get_n_keys (curve) : 0;
	for (i = 0; i < n; i++)
		for (side = -1; side <= 1; side++)
		{
			gfloat t, v;

			if (!handle_point (curve, i, side, &t, &v))
				continue;
			lo = MIN (lo, v);
			hi = MAX (hi, v);
			t1 = MAX (t1, t);
		}
	if (lo > hi)
	{
		lo = 0.0f;
		hi = 1.0f;
	}
	pad = MAX (0.25f, (hi - lo) * 0.12f);
	self->view_t0 = 0.0f;
	self->view_t1 = MAX (t1, 0.1f);
	self->view_v0 = lo - pad;
	self->view_v1 = hi + pad;
}

static void
curve_to_screen (LrgEditorSequencerPanel *self,
                 gfloat                   t,
                 gfloat                   v,
                 gfloat                  *x,
                 gfloat                  *y)
{
	gfloat gx, gy, gw, gh;

	curve_graph (self, &gx, &gy, &gw, &gh);
	*x = gx + (t - self->view_t0) / (self->view_t1 - self->view_t0) * gw;
	*y = gy + gh - (v - self->view_v0) / (self->view_v1 - self->view_v0) * gh;
}

static void
curve_from_screen (LrgEditorSequencerPanel *self,
                   gfloat                   x,
                   gfloat                   y,
                   gfloat                  *t,
                   gfloat                  *v)
{
	gfloat gx, gy, gw, gh;

	curve_graph (self, &gx, &gy, &gw, &gh);
	*t = self->view_t0 + (x - gx) / gw * (self->view_t1 - self->view_t0);
	*v = self->view_v0 + (gy + gh - y) / gh * (self->view_v1 - self->view_v0);
}

gboolean
lrg_editor_sequencer_panel_get_curve_point (LrgEditorSequencerPanel *self,
                                            guint                    key,
                                            gint                     handle,
                                            gfloat                  *x,
                                            gfloat                  *y)
{
	LrgSequenceCurve *curve;
	gfloat            t, v, px, py;

	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), FALSE);

	curve = curve_of (self->seq, self->curve_track, self->curve_channel);
	if (curve == NULL || !handle_point (curve, key, CLAMP (handle, -1, 1), &t, &v))
		return FALSE;
	curve_ranges (self, curve);
	curve_to_screen (self, t, v, &px, &py);
	if (x != NULL)
		*x = px;
	if (y != NULL)
		*y = py;
	return TRUE;
}

static gchar *
curve_title (LrgEditorSequencerPanel *self)
{
	LrgSequenceTrack *t;
	g_autofree gchar *tt = NULL;

	if (self->curve_track < 0 || (guint) self->curve_track >= lrg_sequence_get_n_tracks (self->seq))
		return g_strdup ("Curve: select a key of a transform or property track");
	t = lrg_sequence_get_track (self->seq, (guint) self->curve_track);
	tt = track_title (t);
	if (self->curve_channel == CH_CURVE)
		return g_strdup_printf ("Curve: %s", tt);
	return g_strdup_printf ("Curve: %s  %s", tt,
	                        lrg_sequence_transform_channel_to_string ((LrgSequenceTransformChannel) self->curve_channel));
}

static void
curve_paint (LrgCustomWidget *widget,
             LrgPaintList    *list,
             gfloat           x,
             gfloat           y,
             gfloat           width,
             gfloat           height,
             gpointer         user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	LrgTheme                *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	LrgSequenceCurve        *curve = curve_of (self->seq, self->curve_track, self->curve_channel);
	GrlFont                 *font = lrg_theme_get_ui_font (theme);
	gfloat                   small = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                   radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	const GrlColor          *bg = lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT);
	const GrlColor          *border = lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER);
	const GrlColor          *grid = lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);
	const GrlColor          *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	const GrlColor          *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	const GrlColor          *handle_c = lrg_theme_get_color (theme, LRG_THEME_COLOR_HANDLE);
	const GrlColor          *warn = lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING);
	g_autofree gchar        *title = curve_title (self);
	gfloat                   gx, gy, gw, gh, px = 0.0f, py = 0.0f;
	guint                    i, n;

	if (small <= 0.0f)
		small = 11.0f;
	lrg_paint_list_rect (list, x, y, width, height, radius, border);
	lrg_paint_list_rect (list, x + 1.0f, y + 1.0f, width - 2.0f, height - 2.0f, MAX (radius - 1.0f, 0.0f), bg);
	lrg_paint_list_text (list, font, title, x + 8.0f, y + 5.0f, small, 1.0f, muted);
	curve_ranges (self, curve);
	curve_graph (self, &gx, &gy, &gw, &gh);

	/* 1. Grid: value quarters with labels, a line per second */
	for (i = 0; i <= 4; i++)
	{
		gfloat           fy = gy + gh * (gfloat) i / 4.0f;
		gfloat           v = self->view_v1 - (self->view_v1 - self->view_v0) * (gfloat) i / 4.0f;
		g_autofree gchar *lab = g_strdup_printf ("%.2f", v);

		lrg_paint_list_line (list, gx, fy, gx + gw, fy, 1.0f, grid);
		lrg_paint_list_text (list, font, lab, x + 6.0f, fy - small * 0.5f, small, 1.0f, muted);
	}
	{
		gfloat step = (self->view_t1 - self->view_t0) > 12.0f ? 2.0f : 1.0f;
		gfloat t;

		if ((self->view_t1 - self->view_t0) < 2.5f)
			step = 0.25f;
		for (t = 0.0f; t <= self->view_t1 + 1e-4f; t += step)
		{
			gfloat           fx, fy;
			g_autofree gchar *lab = g_strdup_printf ("%g", t);

			curve_to_screen (self, t, self->view_v0, &fx, &fy);
			lrg_paint_list_line (list, fx, gy, fx, gy + gh, 1.0f, grid);
			lrg_paint_list_text (list, font, lab, fx + 2.0f, gy + gh + 3.0f, small, 1.0f, muted);
		}
	}

	/* 2. The playhead */
	{
		gfloat fx, fy;

		curve_to_screen (self, lrg_sequence_player_get_time (self->player), 0.0f, &fx, &fy);
		if (fx >= gx && fx <= gx + gw)
			lrg_paint_list_line (list, fx, gy, fx, gy + gh, 1.5f, warn);
	}

	n = curve != NULL ? lrg_sequence_curve_get_n_keys (curve) : 0;
	if (n == 0)
	{
		lrg_paint_list_text (list, font, curve != NULL ? "No keys: double click to add one" : "", gx + 8.0f,
		                     gy + gh * 0.5f - small * 0.5f, small, 1.0f, muted);
		return;
	}

	/* 3. The curve, sampled exactly as the engine evaluates it */
	lrg_paint_list_push_clip (list, gx, gy - 4.0f, gw, gh + 8.0f);
	for (i = 0; i <= CURVE_SAMPLES; i++)
	{
		gfloat t = self->view_t0 + (self->view_t1 - self->view_t0) * (gfloat) i / (gfloat) CURVE_SAMPLES;
		gfloat sx, sy;

		curve_to_screen (self, t, lrg_sequence_curve_evaluate (curve, t), &sx, &sy);
		if (i > 0)
			lrg_paint_list_line (list, px, py, sx, sy, 2.0f, accent);
		px = sx;
		py = sy;
	}

	/* 4. Handles: the selected key's, or every key's on short curves */
	for (i = 0; i < n; i++)
	{
		gint side;

		if (n > 12 && (gint) i != self->curve_key)
			continue;
		for (side = -1; side <= 1; side += 2)
		{
			gfloat ht, hv, kx = 0.0f, ky = 0.0f, hx, hy;

			if (!handle_point (curve, i, side, &ht, &hv))
				continue;
			handle_point (curve, i, 0, &kx, &ky);
			curve_to_screen (self, kx, ky, &kx, &ky);
			curve_to_screen (self, ht, hv, &hx, &hy);
			/* the selected key's handles in the accent (readable on light
			 * and dark inputs), the others muted */
			lrg_paint_list_line (list, kx, ky, hx, hy, 1.0f, (gint) i == self->curve_key ? accent : muted);
			lrg_paint_list_rect (list, hx - HANDLE_SIZE * 0.5f, hy - HANDLE_SIZE * 0.5f, HANDLE_SIZE, HANDLE_SIZE,
			                     1.5f, (gint) i == self->curve_key ? accent : muted);
		}
	}

	/* 5. Keys, the selected one filled with the accent */
	for (i = 0; i < n; i++)
	{
		LrgSequenceKey key;
		gfloat         kx, ky;

		lrg_sequence_curve_get_key (curve, i, &key);
		curve_to_screen (self, key.time, key.value, &kx, &ky);
		lrg_paint_list_circle (list, kx, ky, KEY_RADIUS + 1.5f, accent);
		lrg_paint_list_circle (list, kx, ky, KEY_RADIUS, (gint) i == self->curve_key ? accent : handle_c);
	}
	lrg_paint_list_pop_clip (list);
}

static gint
curve_key_at (LrgEditorSequencerPanel *self,
              LrgSequenceCurve        *curve,
              gfloat                   x,
              gfloat                   y,
              gint                    *side)
{
	guint i, n = lrg_sequence_curve_get_n_keys (curve);

	/* the selected key's handles win over keys */
	if (self->curve_key >= 0)
	{
		gint s;

		for (s = -1; s <= 1; s += 2)
		{
			gfloat t, v, hx, hy;

			if (!handle_point (curve, (guint) self->curve_key, s, &t, &v))
				continue;
			curve_to_screen (self, t, v, &hx, &hy);
			if (fabsf (hx - x) <= HIT_RADIUS && fabsf (hy - y) <= HIT_RADIUS)
			{
				*side = s;
				return self->curve_key;
			}
		}
	}
	for (i = 0; i < n; i++)
	{
		LrgSequenceKey key;
		gfloat         kx, ky;

		lrg_sequence_curve_get_key (curve, i, &key);
		curve_to_screen (self, key.time, key.value, &kx, &ky);
		if (fabsf (kx - x) <= HIT_RADIUS && fabsf (ky - y) <= HIT_RADIUS)
		{
			*side = 0;
			return (gint) i;
		}
	}
	return -1;
}

/* One-shot curve edit through a commit (add, delete, field edits). */
static gboolean
curve_commit (LrgEditorSequencerPanel *self,
              const gchar             *label,
              void                   (*edit) (LrgSequenceCurve *curve, gpointer data, gint *key),
              gpointer                 data)
{
	LrgSequence      *copy = working_copy (self);
	LrgSequenceCurve *curve = curve_of (copy, self->curve_track, self->curve_channel);
	gint              key = self->curve_key;

	if (curve == NULL)
	{
		g_object_unref (copy);
		return FALSE;
	}
	edit (curve, data, &key);
	if (!commit_take (self, copy, label))
		return FALSE;
	self->curve_key = key;
	sync_detail (self);
	return TRUE;
}

typedef struct
{
	gfloat t;
	gfloat v;
} KeyAdd;

static void
edit_add_key (LrgSequenceCurve *curve,
              gpointer          data,
              gint             *key)
{
	KeyAdd *a = data;

	*key = (gint) lrg_sequence_curve_set_key (curve, a->t, a->v, LRG_SEQUENCE_TANGENT_AUTO);
}

static void
edit_remove_key (LrgSequenceCurve *curve,
                 gpointer          data,
                 gint             *key)
{
	if (*key >= 0)
		lrg_sequence_curve_remove_key (curve, (guint) *key);
	*key = -1;
}

/* Live drag of a key or a handle on the edited sequence itself. */
static void
curve_drag_to (LrgEditorSequencerPanel *self,
               LrgSequenceCurve        *curve,
               gfloat                   x,
               gfloat                   y,
               gboolean                 shift)
{
	gfloat t, v;

	curve_from_screen (self, x, y, &t, &v);
	if (self->drag == DRAG_KEY)
	{
		LrgSequenceKey key = self->drag_key;

		key.time = CLAMP (snap_time (self, t), 0.0f, lrg_sequence_get_length (self->seq));
		key.value = v;
		lrg_sequence_curve_remove_key (curve, (guint) self->curve_key);
		self->curve_key = (gint) lrg_sequence_curve_insert_key (curve, &key);
	}
	else
	{
		LrgSequenceKey key;
		gfloat         in_s = 0.0f, out_s = 0.0f, slope, dt;
		gboolean       broken;

		lrg_sequence_curve_get_key (curve, (guint) self->curve_key, &key);
		lrg_sequence_curve_get_slopes (curve, (guint) self->curve_key, &in_s, &out_s);
		dt = t - key.time;
		if (self->drag == DRAG_IN)
			dt = MIN (dt, -1e-3f);
		else
			dt = MAX (dt, 1e-3f);
		slope = (v - key.value) / dt;
		broken = shift || key.tangent_mode == LRG_SEQUENCE_TANGENT_BROKEN;
		if (broken)
			lrg_sequence_curve_set_key_tangents (curve, (guint) self->curve_key, LRG_SEQUENCE_TANGENT_BROKEN,
			                                     self->drag == DRAG_IN ? slope : in_s,
			                                     self->drag == DRAG_OUT ? slope : out_s);
		else
			lrg_sequence_curve_set_key_tangents (curve, (guint) self->curve_key, LRG_SEQUENCE_TANGENT_USER, slope,
			                                     slope);
	}
	if (self->previewing)
		apply_preview (self);
	sync_detail (self);
}

static gboolean
curve_event (LrgCustomWidget  *widget,
             const LrgUIEvent *event,
             gpointer          user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	LrgSequenceCurve        *curve = curve_of (self->seq, self->curve_track, self->curve_channel);
	gfloat                   x = lrg_ui_event_get_x (event);
	gfloat                   y = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		{
			gint     side = 0, hit;
			gboolean dbl;

			if (lrg_ui_event_get_button (event) != 0 || curve == NULL)
				return FALSE;
			curve_ranges (self, curve);
			dbl = (self->clock - self->last_press) < DOUBLE_CLICK && fabsf (x - self->press_x) < 6.0f &&
			      fabsf (y - self->press_y) < 6.0f;
			self->last_press = self->clock;
			self->press_x = x;
			self->press_y = y;
			hit = curve_key_at (self, curve, x, y, &side);
			if (hit >= 0)
			{
				self->curve_key = hit;
				self->drag = side < 0 ? DRAG_IN : side > 0 ? DRAG_OUT : DRAG_KEY;
				self->drag_moved = FALSE;
				lrg_sequence_curve_get_key (curve, (guint) hit, &self->drag_key);
				sync_detail (self);
				return TRUE;
			}
			if (dbl)
			{
				KeyAdd a;

				curve_from_screen (self, x, y, &a.t, &a.v);
				a.t = CLAMP (snap_time (self, a.t), 0.0f, lrg_sequence_get_length (self->seq));
				a.v = lrg_sequence_curve_evaluate (curve, a.t);
				curve_commit (self, "Add Key", edit_add_key, &a);
				self->last_press = -1.0;
				return TRUE;
			}
			self->curve_key = -1;
			sync_detail (self);
			return TRUE;
		}

	case LRG_UI_EVENT_MOUSE_MOVE:
		if (self->drag == DRAG_NONE || curve == NULL || self->curve_key < 0)
			return FALSE;
		if (!self->drag_moved)
		{
			if (fabsf (x - self->press_x) < DRAG_THRESHOLD && fabsf (y - self->press_y) < DRAG_THRESHOLD)
				return TRUE;
			self->drag_moved = TRUE;
			g_clear_object (&self->drag_before);
			self->drag_before = lrg_sequence_copy (self->seq);
		}
		curve_drag_to (self, curve, x, y, (lrg_ui_event_get_modifiers (event) & LRG_INPUT_MODIFIER_SHIFT) != 0);
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (self->drag == DRAG_NONE)
			return FALSE;
		if (self->drag_moved && self->drag_before != NULL)
		{
			LrgSequence *before = g_steal_pointer (&self->drag_before);

			if (lrg_sequence_equal (before, self->seq))
				g_object_unref (before);
			else
			{
				record_undo (self, before, self->drag == DRAG_KEY ? "Move Key" : "Edit Tangents");
				set_dirty (self);
				self->view_dirty = TRUE;
			}
		}
		self->drag = DRAG_NONE;
		self->drag_moved = FALSE;
		return TRUE;

	case LRG_UI_EVENT_CANCEL:
		if (self->drag != DRAG_NONE && self->drag_before != NULL)
		{
			/* put the curve back */
			g_set_object (&self->seq, self->drag_before);
			g_clear_object (&self->drag_before);
			attach_player (self);
			self->view_dirty = TRUE;
		}
		self->drag = DRAG_NONE;
		return self->drag_moved;

	case LRG_UI_EVENT_KEY_DOWN:
		if ((lrg_ui_event_get_key (event) == GRL_KEY_DELETE || lrg_ui_event_get_key (event) == GRL_KEY_BACKSPACE) &&
		    self->curve_key >= 0)
			return curve_commit (self, "Delete Key", edit_remove_key, NULL);
		return FALSE;

	default:
		return FALSE;
	}
}

static void
curve_measure (LrgCustomWidget     *widget,
               gfloat               avail_width,
               gfloat               avail_height,
               LrgCustomWidgetSize *size,
               gpointer             user_data)
{
	size->width = 320.0f;
	size->height = 80.0f;
}

gboolean
lrg_editor_sequencer_panel_select_curve (LrgEditorSequencerPanel *self,
                                         gint                     track,
                                         const gchar             *channel)
{
	LrgSequenceTrack           *t;
	LrgSequenceTransformChannel ch;
	gint                        c;

	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), FALSE);

	self->curve_key = -1;
	self->curve_track = -1;
	if (track < 0 || (guint) track >= lrg_sequence_get_n_tracks (self->seq))
	{
		sync_detail (self);
		return FALSE;
	}
	t = lrg_sequence_get_track (self->seq, (guint) track);
	self->sel_track = track;
	self->sel_section = -1;
	if (lrg_sequence_track_get_kind (t) == LRG_SEQUENCE_TRACK_PROPERTY)
	{
		self->curve_track = track;
		self->curve_channel = CH_CURVE;
	}
	else if (lrg_sequence_track_get_kind (t) == LRG_SEQUENCE_TRACK_TRANSFORM)
	{
		if (channel != NULL)
		{
			if (lrg_sequence_transform_channel_from_string (channel, &ch) &&
			    lrg_sequence_track_get_channel_curve (t, ch) != NULL)
			{
				self->curve_track = track;
				self->curve_channel = (gint) ch;
			}
		}
		else
			for (c = 0; c < LRG_SEQUENCE_N_CHANNELS && self->curve_track < 0; c++)
				if (lrg_sequence_track_get_channel_curve (t, (LrgSequenceTransformChannel) c) != NULL)
				{
					self->curve_track = track;
					self->curve_channel = c;
				}
	}
	sync_detail (self);
	return self->curve_track >= 0;
}

/* ==========================================================================
 * Track editing: add tracks, key here, bind, delete
 * ========================================================================== */

/* Adds @node's GUID to @edit's bindings (named after the node). */
static const gchar *
ensure_binding (LrgSequence *edit,
                LrgNode     *node)
{
	const gchar *guid = lrg_node_get_guid (node);

	if (guid == NULL)
		return NULL;
	if (lrg_sequence_get_binding_name (edit, guid) == NULL)
		lrg_sequence_add_binding (edit, guid, lrg_node_get_name (node));
	return guid;
}

/* The hierarchy's first selected node, or NULL. */
static LrgNode *
selected_node (LrgEditorSequencerPanel *self)
{
	GPtrArray *nodes = lrg_editor_selection_get_nodes (lrg_editor_get_selection (self->editor));

	return nodes->len > 0 ? g_ptr_array_index (nodes, 0) : NULL;
}

/*
 * add_track_kind:
 *
 * Adds a track of @kind (a bone transform track when @bone is set) as one
 * undo step. Entity tracks (transform, property, audio, animation) bind
 * to the selected track's binding, else to the hierarchy selection
 * (which becomes a binding), else to nothing (bind them later). Camera
 * cut, event and sub-sequence tracks are not bound. Returns the index.
 */
static gint
add_track_kind (LrgEditorSequencerPanel *self,
                LrgSequenceTrackKind     kind,
                const gchar             *bone,
                const gchar             *name,
                const gchar             *binding)
{
	LrgSequence                *edit = working_copy (self);
	g_autoptr(LrgSequenceTrack) track = NULL;
	g_autofree gchar           *target = NULL;
	gboolean                    entity = kind == LRG_SEQUENCE_TRACK_TRANSFORM || kind == LRG_SEQUENCE_TRACK_PROPERTY ||
	                                     kind == LRG_SEQUENCE_TRACK_AUDIO || kind == LRG_SEQUENCE_TRACK_ANIMATION;
	gint                        index;

	if (binding != NULL)
	{
		LrgNode *node = lrg_editor_find_node (self->editor, binding);

		target = g_strdup (node != NULL ? ensure_binding (edit, node) : binding);
	}
	else if (entity)
	{
		LrgSequenceTrack *cur = sel_track (self);
		LrgNode          *node;

		if (cur != NULL && lrg_sequence_track_get_binding (cur) != NULL)
			target = g_strdup (lrg_sequence_track_get_binding (cur));
		else if ((node = selected_node (self)) != NULL)
			target = g_strdup (ensure_binding (edit, node));
	}

	track = lrg_sequence_track_new (kind, name != NULL ? name
	                                      : (bone != NULL ? "Bone" : kind_label (kind)));
	if (target != NULL && (entity || binding != NULL))
		lrg_sequence_track_set_binding (track, target);
	if (kind == LRG_SEQUENCE_TRACK_TRANSFORM && bone != NULL)
		lrg_sequence_track_set_bone (track, bone);
	lrg_sequence_add_track (edit, track);
	index = (gint) lrg_sequence_get_n_tracks (edit) - 1;
	commit_take (self, edit, "Add Track");
	self->sel_track = index;
	self->sel_section = -1;
	lrg_editor_sequencer_panel_select_curve (self, index, NULL);
	return index;
}

static void
on_add_track_action (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	const gchar             *nick = g_variant_get_string (parameter, NULL);
	gint                     kind;

	if (g_strcmp0 (nick, "bone") == 0)
	{
		add_track_kind (self, LRG_SEQUENCE_TRACK_TRANSFORM, "root", NULL, NULL);
		return;
	}
	kind = enum_from_nick (LRG_TYPE_SEQUENCE_TRACK_KIND, nick);
	if (kind >= 0)
		add_track_kind (self, (LrgSequenceTrackKind) kind, NULL, NULL, NULL);
}

static void
on_add_track_chip (LrgWidget *chip,
                   gpointer   user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);

	if (self->track_menu == NULL)
	{
		g_autoptr(GMenu) menu = g_menu_new ();
		g_autoptr(GMenu) entity = g_menu_new ();
		g_autoptr(GMenu) global = g_menu_new ();

		g_menu_append (entity, "Transform", "seq.add-track::transform");
		g_menu_append (entity, "Bone Transform", "seq.add-track::bone");
		g_menu_append (entity, "Property", "seq.add-track::property");
		g_menu_append (entity, "Audio", "seq.add-track::audio");
		g_menu_append (entity, "Animation Clip", "seq.add-track::animation");
		g_menu_append (global, "Camera Cut", "seq.add-track::camera-cut");
		g_menu_append (global, "Event", "seq.add-track::event");
		g_menu_append (global, "Sub-sequence", "seq.add-track::subsequence");
		g_menu_append_section (menu, "Bound to an entity", G_MENU_MODEL (entity));
		g_menu_append_section (menu, "Sequence", G_MENU_MODEL (global));
		self->track_menu = g_object_ref_sink (lrg_menu_new_from_model (G_MENU_MODEL (menu)));
	}
	lrg_menu_popup_at_widget (self->track_menu, chip, LRG_POPUP_PLACEMENT_BELOW);
}

/*
 * key_here:
 *
 * "Key" on the selected track at the playhead, one undo step:
 *  - transform: the bound node's current values of the keyed channels
 *    (all nine when none is keyed yet; a bone track's bone offsets);
 *  - property: the node's channel value, else the curve's own value;
 *  - camera cut: a cut to the selected camera node (else the track's
 *    binding); audio, event, animation, sub-sequence: a placeholder
 *    section to rename in the detail fields.
 * Returns the number of keys or sections added.
 */
static guint
key_here (LrgEditorSequencerPanel *self,
          gint                     track_index)
{
	LrgSequence      *edit;
	LrgSequenceTrack *track;
	LrgNode          *node;
	gfloat            t = snap_time (self, lrg_sequence_player_get_time (self->player));
	guint             added = 0;
	gint              c;

	if (track_index < 0 || (guint) track_index >= lrg_sequence_get_n_tracks (self->seq))
		return 0;
	edit = working_copy (self);
	track = lrg_sequence_get_track (edit, (guint) track_index);
	node = lrg_editor_find_node (self->editor, lrg_sequence_track_get_binding (track));

	switch (lrg_sequence_track_get_kind (track))
	{
	case LRG_SEQUENCE_TRACK_TRANSFORM:
		{
			gboolean any = FALSE;

			for (c = 0; c < LRG_SEQUENCE_N_CHANNELS; c++)
				any |= lrg_sequence_track_get_channel_curve (track, (LrgSequenceTransformChannel) c) != NULL;
			for (c = 0; c < LRG_SEQUENCE_N_CHANNELS; c++)
			{
				LrgSequenceCurve *curve = lrg_sequence_track_get_channel_curve (track, (LrgSequenceTransformChannel) c);
				g_autofree gchar *channel = node_channel (track, c);
				gfloat            v;

				if (any && curve == NULL)
					continue;
				if (node == NULL || !lrg_timeline_channel_read (node, channel, &v))
				{
					if (curve == NULL)
						continue;
					v = lrg_sequence_curve_evaluate (curve, t);
				}
				curve = lrg_sequence_track_ensure_channel_curve (track, (LrgSequenceTransformChannel) c);
				lrg_sequence_curve_set_key (curve, t, v, LRG_SEQUENCE_TANGENT_AUTO);
				added++;
			}
		}
		break;

	case LRG_SEQUENCE_TRACK_PROPERTY:
		{
			LrgSequenceCurve *curve = lrg_sequence_track_get_curve (track);
			const gchar      *prop = lrg_sequence_track_get_property_name (track);
			gfloat            v;

			if (node == NULL || lrg_sequence_track_get_component_type (track) != NULL ||
			    !lrg_timeline_channel_is_valid (prop) || !lrg_timeline_channel_read (node, prop, &v))
				v = lrg_sequence_curve_evaluate (curve, t);
			lrg_sequence_curve_set_key (curve, t, v, LRG_SEQUENCE_TANGENT_AUTO);
			added++;
		}
		break;

	case LRG_SEQUENCE_TRACK_CAMERA_CUT:
		{
			LrgNode     *cam = selected_node (self);
			const gchar *guid = cam != NULL ? ensure_binding (edit, cam) : lrg_sequence_track_get_binding (track);

			if (guid != NULL)
			{
				lrg_sequence_track_add_camera_cut (track, t, guid);
				added++;
			}
			else if (self->app != NULL)
				editor_app_set_status (self->app, "Select a camera node to cut to");
		}
		break;

	case LRG_SEQUENCE_TRACK_AUDIO:
		lrg_sequence_track_add_audio (track, t, "sfx:sound", 1.0f, 0.0f);
		added++;
		break;

	case LRG_SEQUENCE_TRACK_EVENT:
		lrg_sequence_track_add_event (track, t, "event", NULL);
		added++;
		break;

	case LRG_SEQUENCE_TRACK_ANIMATION:
		lrg_sequence_track_add_clip (track, t, 1.0f, "Idle", 0.0f, 1.0f, FALSE, 1.0f);
		added++;
		break;

	case LRG_SEQUENCE_TRACK_SUBSEQUENCE:
		lrg_sequence_track_add_subsequence (track, t, 0.0f, "shot.rseq", 0.0f, 1.0f, FALSE);
		added++;
		break;

	default:
		break;
	}
	if (added == 0)
	{
		g_object_unref (edit);
		return 0;
	}
	commit_take (self, edit, "Key");
	self->sel_track = track_index;
	{
		LrgSequenceTrack *live = lrg_sequence_get_track (self->seq, (guint) track_index);

		self->sel_section = lrg_sequence_track_get_n_sections (live) > 0 ? find_section (live, t) : -1;
	}
	if (self->curve_track != track_index)
		lrg_editor_sequencer_panel_select_curve (self, track_index, NULL);
	else
		sync_detail (self);
	return added;
}

/* Binds every selected hierarchy node (one undo step). */
static guint
bind_selection (LrgEditorSequencerPanel *self)
{
	GPtrArray   *nodes = lrg_editor_selection_get_nodes (lrg_editor_get_selection (self->editor));
	LrgSequence *edit;
	guint        i;

	if (nodes->len == 0)
	{
		if (self->app != NULL)
			editor_app_set_status (self->app, "Select nodes in the level to bind them");
		return 0;
	}
	edit = working_copy (self);
	for (i = 0; i < nodes->len; i++)
		ensure_binding (edit, g_ptr_array_index (nodes, i));
	commit_take (self, edit, "Bind");
	return nodes->len;
}

/* Delete: the selected view keys / sections, else the curve's selected
 * key, else the selected track. */
static void
delete_selection (LrgEditorSequencerPanel *self)
{
	g_autoptr(GArray) ids = lrg_timeline_view_get_selected_keyframes (self->view);
	g_autoptr(GArray) cids = lrg_timeline_view_get_selected_clips (self->view);
	guint             i;

	for (i = 0; ids != NULL && i < ids->len; i++)
	{
		KeyRef *ref = g_hash_table_lookup (self->keys, GUINT_TO_POINTER (g_array_index (ids, guint, i)));

		if (ref != NULL)
			queue (self, OP_KEY_REMOVE, ref->track, ref->channel, ref->time, ref->time, 0.0f, FALSE, -1.0f);
	}
	for (i = 0; cids != NULL && i < cids->len; i++)
	{
		ClipRef *ref = g_hash_table_lookup (self->clips, GUINT_TO_POINTER (g_array_index (cids, guint, i)));

		if (ref != NULL)
			queue (self, OP_SECTION_REMOVE, ref->track, 0, ref->start, ref->start, 0.0f, FALSE, -1.0f);
	}
	if (self->ops->len > 0)
	{
		flush_ops (self);
		return;
	}
	if (self->curve_key >= 0)
	{
		curve_commit (self, "Delete Key", edit_remove_key, NULL);
		return;
	}
	if (sel_track (self) != NULL)
	{
		LrgSequence *edit = working_copy (self);

		lrg_sequence_remove_track (edit, (guint) self->sel_track);
		commit_take (self, edit, "Delete Track");
		self->sel_track = -1;
		self->sel_section = -1;
		self->curve_track = -1;
		sync_detail (self);
	}
}

/* ==========================================================================
 * Toolbar and detail callbacks
 * ========================================================================== */

static void
notify_error (LrgEditorSequencerPanel *self,
              const gchar             *what,
              GError                  *error)
{
	if (self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "%s: %s", what, error != NULL ? error->message : "failed");
}

static void
on_new (LrgWidget *chip,
        gpointer   user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	const gchar             *typed = lrg_text_input_get_text (self->path_input);
	g_autoptr(GError)        error = NULL;

	if (!lrg_editor_sequencer_panel_create (self, (typed != NULL && *typed != '\0') ? typed : NULL, &error))
		notify_error (self, "New sequence", error);
}

static void
on_open (LrgWidget *chip,
         gpointer   user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	const gchar             *typed = lrg_text_input_get_text (self->path_input);
	g_autoptr(GError)        error = NULL;

	if (typed == NULL || *typed == '\0')
	{
		if (self->app != NULL)
			editor_app_set_status (self->app, "Type a .rseq path, or double click one in the Assets panel");
		return;
	}
	if (!lrg_editor_sequencer_panel_load (self, typed, &error))
		notify_error (self, "Open sequence", error);
}

static void
on_path_submitted (LrgTextInput *input,
                   gpointer      user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	const gchar             *typed = lrg_text_input_get_text (input);
	g_autofree gchar        *abs = abs_path (self, typed);

	if (abs != NULL && g_file_test (abs, G_FILE_TEST_EXISTS))
		on_open (NULL, user_data);
	else
		on_new (NULL, user_data);
}

static void
do_save (LrgEditorSequencerPanel *self)
{
	const gchar      *typed = lrg_text_input_get_text (self->path_input);
	g_autoptr(GError) error = NULL;

	/* untitled: the typed path; else the file (Save As by typing a new one) */
	if (!lrg_editor_sequencer_panel_save (self, (typed != NULL && *typed != '\0') ? typed : NULL, &error))
		notify_error (self, "Save sequence", error);
	else
		lrg_text_input_set_text (self->path_input, "");
}

static void on_save (LrgWidget *c, gpointer d)  { do_save (LRG_EDITOR_SEQUENCER_PANEL (d)); }
static void on_undo (LrgWidget *c, gpointer d)  { lrg_editor_sequencer_panel_undo (LRG_EDITOR_SEQUENCER_PANEL (d)); }
static void on_redo (LrgWidget *c, gpointer d)  { lrg_editor_sequencer_panel_redo (LRG_EDITOR_SEQUENCER_PANEL (d)); }
static void on_stop (LrgWidget *c, gpointer d)  { lrg_editor_sequencer_panel_stop (LRG_EDITOR_SEQUENCER_PANEL (d)); }
static void on_bind (LrgWidget *c, gpointer d)  { bind_selection (LRG_EDITOR_SEQUENCER_PANEL (d)); }
static void on_delete (LrgWidget *c, gpointer d) { delete_selection (LRG_EDITOR_SEQUENCER_PANEL (d)); }

static void
on_key (LrgWidget *chip,
        gpointer   user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);

	if (self->sel_track < 0 && self->app != NULL)
		editor_app_set_status (self->app, "Select a track to key");
	key_here (self, self->sel_track);
}

static void
on_play (LrgWidget *chip,
         gpointer   user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);

	lrg_editor_sequencer_panel_set_playing (self, !self->playing);
}

static void
step_frame (LrgEditorSequencerPanel *self,
            gint                     frames)
{
	gint f = lrg_sequence_time_to_frame (self->seq, lrg_sequence_player_get_time (self->player)) + frames;

	lrg_editor_sequencer_panel_set_playing (self, FALSE);
	lrg_editor_sequencer_panel_scrub (self, lrg_sequence_frame_to_time (self->seq, MAX (f, 0)));
}

static void on_prev (LrgWidget *c, gpointer d) { step_frame (LRG_EDITOR_SEQUENCER_PANEL (d), -1); }
static void on_next (LrgWidget *c, gpointer d) { step_frame (LRG_EDITOR_SEQUENCER_PANEL (d), 1); }

static void
on_mode (LrgComboBox *combo,
         gpointer     user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	gint                     i = lrg_combo_box_get_selected (combo);

	if (!self->syncing && i >= 0)
		lrg_editor_sequencer_panel_set_preview_mode (self, (LrgSequencePlayMode) i);
}

static void
on_snap (LrgWidget *chip,
         gpointer   user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);

	lrg_editor_sequencer_panel_set_snap (self, !self->snap);
}

static void
on_pilot (LrgWidget *chip,
          gpointer   user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);

	lrg_editor_sequencer_panel_set_pilot (self, !self->pilot);
}

static void
on_curves (LrgWidget *chip,
           gpointer   user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);

	editor_chip_set_checked (chip, !editor_chip_get_checked (chip));
	self->view_dirty = TRUE;
}

static void
on_length (LrgSpinBox *spin,
           gdouble     value,
           gpointer    user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	LrgSequence             *edit;

	if (self->syncing || value < 0.01 || fabs (value - lrg_sequence_get_length (self->seq)) < 1e-5)
		return;
	edit = working_copy (self);
	lrg_sequence_set_length (edit, (gfloat) value);
	commit_take (self, edit, "Sequence Length");
	self->fit_pending = TRUE;
}

static void
on_fps (LrgSpinBox *spin,
        gdouble     value,
        gpointer    user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	LrgSequence             *edit;

	if (self->syncing || value < 1.0 || fabs (value - lrg_sequence_get_fps (self->seq)) < 1e-5)
		return;
	edit = working_copy (self);
	lrg_sequence_set_fps (edit, (gfloat) value);
	commit_take (self, edit, "Frame Rate");
}

/* --- Track fields ---------------------------------------------------------- */

typedef void (*TrackEdit) (LrgEditorSequencerPanel *self, LrgSequenceTrack *track, gpointer data);

static void
edit_sel_track (LrgEditorSequencerPanel *self,
                const gchar             *label,
                TrackEdit                func,
                gpointer                 data)
{
	LrgSequence *edit;

	if (self->syncing || sel_track (self) == NULL)
		return;
	edit = working_copy (self);
	func (self, lrg_sequence_get_track (edit, (guint) self->sel_track), data);
	commit_take (self, edit, label);
}

static void
te_name (LrgEditorSequencerPanel *self, LrgSequenceTrack *t, gpointer data)
{
	const gchar *s = data;

	lrg_sequence_track_set_name (t, (s != NULL && *s != '\0') ? s : NULL);
}

static void
te_bone (LrgEditorSequencerPanel *self, LrgSequenceTrack *t, gpointer data)
{
	const gchar *s = data;

	lrg_sequence_track_set_bone (t, (s != NULL && *s != '\0') ? s : NULL);
}

static void
te_component (LrgEditorSequencerPanel *self, LrgSequenceTrack *t, gpointer data)
{
	const gchar *s = data;

	lrg_sequence_track_set_component_type (t, (s != NULL && *s != '\0') ? s : NULL);
}

static void
te_property (LrgEditorSequencerPanel *self, LrgSequenceTrack *t, gpointer data)
{
	const gchar *s = data;

	lrg_sequence_track_set_property_name (t, (s != NULL && *s != '\0') ? s : NULL);
}

static void
te_mute (LrgEditorSequencerPanel *self, LrgSequenceTrack *t, gpointer data)
{
	lrg_sequence_track_set_muted (t, !lrg_sequence_track_get_muted (t));
}

static void
te_binding (LrgEditorSequencerPanel *self, LrgSequenceTrack *t, gpointer data)
{
	lrg_sequence_track_set_binding (t, data);
}

static void
on_track_name (LrgTextInput *in, gpointer d)
{
	edit_sel_track (LRG_EDITOR_SEQUENCER_PANEL (d), "Rename Track", te_name, (gpointer) lrg_text_input_get_text (in));
}

static void
on_track_bone (LrgTextInput *in, gpointer d)
{
	edit_sel_track (LRG_EDITOR_SEQUENCER_PANEL (d), "Track Bone", te_bone, (gpointer) lrg_text_input_get_text (in));
}

static void
on_track_component (LrgTextInput *in, gpointer d)
{
	edit_sel_track (LRG_EDITOR_SEQUENCER_PANEL (d), "Track Component", te_component,
	                (gpointer) lrg_text_input_get_text (in));
}

static void
on_track_property (LrgTextInput *in, gpointer d)
{
	edit_sel_track (LRG_EDITOR_SEQUENCER_PANEL (d), "Track Property", te_property,
	                (gpointer) lrg_text_input_get_text (in));
}

static void
on_track_mute (LrgWidget *chip, gpointer d)
{
	edit_sel_track (LRG_EDITOR_SEQUENCER_PANEL (d), "Mute Track", te_mute, NULL);
}

static void
on_track_binding (LrgComboBox *combo,
                  gpointer     user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);
	gint                     row = lrg_combo_box_get_selected (combo);
	const gchar             *guid = NULL;

	if (self->syncing || row < 0)
		return;
	if (row > 0)
		lrg_sequence_get_binding (self->seq, (guint) row - 1, &guid, NULL);
	edit_sel_track (self, "Track Binding", te_binding, (gpointer) guid);
}

/* --- Section fields -------------------------------------------------------- */

typedef enum
{
	SF_NAME,
	SF_START,
	SF_DURATION,
	SF_OFFSET,
	SF_RATE,
	SF_WEIGHT,
	SF_LOOP,
	SF_PAYLOAD
} SectionField;

static void
edit_section_field (LrgEditorSequencerPanel *self,
                    SectionField             field,
                    gdouble                  number,
                    const gchar             *text)
{
	const LrgSequenceSection     *cur = sel_section (self);
	LrgSequence                  *edit;
	LrgSequenceTrack             *track;
	g_autoptr(LrgSequenceSection) copy = NULL;

	if (self->syncing || cur == NULL)
		return;
	edit = working_copy (self);
	track = lrg_sequence_get_track (edit, (guint) self->sel_track);
	copy = lrg_sequence_section_copy (lrg_sequence_track_get_section (track, (guint) self->sel_section));
	switch (field)
	{
	case SF_NAME:
		if (text == NULL || *text == '\0')
		{
			g_object_unref (edit);
			return;
		}
		g_free (copy->name);
		copy->name = g_strdup (text);
		break;
	case SF_START:    copy->start = snap_time (self, (gfloat) number); break;
	case SF_DURATION: copy->duration = MAX ((gfloat) number, 0.0f); break;
	case SF_OFFSET:   copy->offset = (gfloat) number; break;
	case SF_RATE:     copy->rate = (gfloat) number; break;
	case SF_WEIGHT:   copy->weight = MAX ((gfloat) number, 0.0f); break;
	case SF_LOOP:     copy->loop = !copy->loop; break;
	case SF_PAYLOAD:
		g_free (copy->payload);
		copy->payload = (text != NULL && *text != '\0') ? g_strdup (text) : NULL;
		break;
	default:
		break;
	}
	self->sel_section = (gint) replace_section (track, (guint) self->sel_section, copy);
	commit_take (self, edit, "Edit Section");
	sync_detail (self);
}

static void on_sec_name (LrgTextInput *i, gpointer d)    { edit_section_field (d, SF_NAME, 0.0, lrg_text_input_get_text (i)); }
static void on_sec_payload (LrgTextInput *i, gpointer d) { edit_section_field (d, SF_PAYLOAD, 0.0, lrg_text_input_get_text (i)); }
static void on_sec_start (LrgSpinBox *s, gdouble v, gpointer d)    { edit_section_field (d, SF_START, v, NULL); }
static void on_sec_duration (LrgSpinBox *s, gdouble v, gpointer d) { edit_section_field (d, SF_DURATION, v, NULL); }
static void on_sec_offset (LrgSpinBox *s, gdouble v, gpointer d)   { edit_section_field (d, SF_OFFSET, v, NULL); }
static void on_sec_rate (LrgSpinBox *s, gdouble v, gpointer d)     { edit_section_field (d, SF_RATE, v, NULL); }
static void on_sec_weight (LrgSpinBox *s, gdouble v, gpointer d)   { edit_section_field (d, SF_WEIGHT, v, NULL); }
static void on_sec_loop (LrgWidget *c, gpointer d)                 { edit_section_field (d, SF_LOOP, 0.0, NULL); }

/* --- Key fields ------------------------------------------------------------ */

typedef struct
{
	gint   field;   /* 0 time, 1 value, 2 tangent, 3 in, 4 out */
	gfloat number;
} KeyField;

static void
edit_key_field (LrgSequenceCurve *curve,
                gpointer          data,
                gint             *key)
{
	KeyField       *f = data;
	LrgSequenceKey  k;
	gfloat          in_s = 0.0f, out_s = 0.0f;

	if (*key < 0 || !lrg_sequence_curve_get_key (curve, (guint) *key, &k))
		return;
	lrg_sequence_curve_get_slopes (curve, (guint) *key, &in_s, &out_s);
	switch (f->field)
	{
	case 0:
		*key = lrg_sequence_curve_move_key (curve, (guint) *key, MAX (f->number, 0.0f));
		break;
	case 1:
		lrg_sequence_curve_remove_key (curve, (guint) *key);
		k.value = f->number;
		*key = (gint) lrg_sequence_curve_insert_key (curve, &k);
		break;
	case 2:
		/* switching mode keeps the drawn slopes as the user slopes */
		lrg_sequence_curve_set_key_tangents (curve, (guint) *key, (LrgSequenceTangentMode) (gint) f->number, in_s,
		                                     out_s);
		break;
	case 3:
		lrg_sequence_curve_set_key_tangents (curve, (guint) *key,
		                                     k.tangent_mode == LRG_SEQUENCE_TANGENT_BROKEN ? LRG_SEQUENCE_TANGENT_BROKEN
		                                                                                 : LRG_SEQUENCE_TANGENT_USER,
		                                     f->number, k.tangent_mode == LRG_SEQUENCE_TANGENT_BROKEN ? out_s
		                                                                                              : f->number);
		break;
	case 4:
		lrg_sequence_curve_set_key_tangents (curve, (guint) *key, LRG_SEQUENCE_TANGENT_BROKEN, in_s, f->number);
		break;
	default:
		break;
	}
}

static void
key_field (LrgEditorSequencerPanel *self,
           gint                     field,
           gdouble                  number)
{
	KeyField f;

	if (self->syncing || self->curve_key < 0)
		return;
	f.field = field;
	f.number = (gfloat) number;
	if (field == 0)
		f.number = snap_time (self, f.number);
	curve_commit (self, field == 2 ? "Key Tangent" : "Edit Key", edit_key_field, &f);
}

static void on_key_time (LrgSpinBox *s, gdouble v, gpointer d)  { key_field (d, 0, v); }
static void on_key_value (LrgSpinBox *s, gdouble v, gpointer d) { key_field (d, 1, v); }
static void on_key_in (LrgSpinBox *s, gdouble v, gpointer d)    { key_field (d, 3, v); }
static void on_key_out (LrgSpinBox *s, gdouble v, gpointer d)   { key_field (d, 4, v); }

static void
on_key_tangent (LrgComboBox *combo,
                gpointer     user_data)
{
	gint i = lrg_combo_box_get_selected (combo);

	if (i >= 0)
		key_field (LRG_EDITOR_SEQUENCER_PANEL (user_data), 2, (gdouble) i);
}

/* ==========================================================================
 * Widget
 * ========================================================================== */

static gboolean
lrg_editor_sequencer_panel_step (LrgWidget *widget,
                                 gfloat     delta_time)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (widget);

	self->clock += delta_time;
	flush_ops (self);

	/* Playback: the player wraps, turns and fires events; we pose */
	if (self->playing)
	{
		if (self->app != NULL && editor_play_is_playing (self->app))
			lrg_editor_sequencer_panel_set_playing (self, FALSE);
		else
		{
			json_array_unref (self->fired);
			self->fired = json_array_new ();
			lrg_sequence_player_advance (self->player, CLAMP (delta_time, 0.0f, 0.1f));
			apply_preview (self);
			if (!lrg_sequence_player_is_playing (self->player) && self->playing)
			{
				self->playing = FALSE;
				g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
			}
		}
	}

	if (self->view_dirty && self->drag == DRAG_NONE)
		rebuild (self);
	sync_toolbar (self);

	/* Zoom a newly shown sequence so its whole length fits */
	if (self->fit_pending)
	{
		gfloat avail = lrg_widget_get_width (LRG_WIDGET (self->view)) - lrg_timeline_view_get_header_width (self->view)
		               - 40.0f;

		if (avail > 50.0f)
		{
			lrg_timeline_view_set_zoom (self->view, avail / MAX (lrg_sequence_get_length (self->seq), 0.05f));
			lrg_timeline_view_set_scroll_time (self->view, 0.0, FALSE);
			self->fit_pending = FALSE;
		}
	}
	return FALSE;
}

static void
lrg_editor_sequencer_panel_layout_children (LrgContainer *container)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (container);
	gfloat                   w = lrg_widget_get_width (LRG_WIDGET (self)) - 16.0f;

	/* children arrive during init: lay out once they all exist */
	if (self->strip == NULL || self->hint == NULL || self->view == NULL)
		return;
	editor_panel_fit_toolbar (self->row1, w);
	editor_panel_fit_toolbar (self->row2, w);
	editor_panel_fit_toolbar (self->key_row, MAX (100.0f, w - DETAIL_WIDTH - 16.0f));
	{
		LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));
		gfloat    pw, ph, top_h, strip_h, y;
		gfloat    h = lrg_widget_get_height (LRG_WIDGET (self));
		gfloat    pad = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_Y));
		gfloat    gap = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SPACING) * 0.75f);

		/* Top rows, then the strip at the bottom: its full height when the
		 * panel is tall, else at most half of what is left, so the tracks
		 * always keep room; in a very short dock the strip hides */
		lrg_widget_get_preferred_size (self->top, &pw, &top_h);
		lrg_widget_place_in_slot (self->top, pad, pad, MAX (0.0f, w), top_h);
		y = pad + top_h + gap;
		lrg_widget_get_preferred_size (self->strip, &pw, &ph);
		strip_h = MIN (MAX (ph, STRIP_HEIGHT), (h - y - pad) * 0.5f);
		/* too short for a usable curve editor: give the tracks everything */
		if (lrg_widget_get_visible (self->strip) != (strip_h >= 96.0f))
			lrg_widget_set_visible (self->strip, strip_h >= 96.0f);
		if (strip_h < 96.0f)
			strip_h = -gap;
		else
			lrg_widget_place_in_slot (self->strip, pad, MAX (y, h - pad - strip_h), MAX (0.0f, w), strip_h);
		lrg_widget_place_in_slot (LRG_WIDGET (self->view), pad, y, MAX (0.0f, w), MAX (0.0f, h - y - strip_h - gap - pad));
		lrg_widget_place_in_slot (self->hint, pad, y, MAX (0.0f, w), MAX (0.0f, h - y - strip_h - gap - pad));
	}
}

static void
on_level_replaced (LrgEditor *editor,
                   LrgLevel  *level,
                   gpointer   user_data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (user_data);

	/* the app ended the preview before replacing the level */
	self->previewing = FALSE;
	self->pilot_saved = FALSE;
	self->view_dirty = TRUE;
}

static void
lrg_editor_sequencer_panel_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (object);

	switch (prop_id)
	{
	case PROP_DIRTY:        g_value_set_boolean (value, self->dirty); break;
	case PROP_PLAYING:      g_value_set_boolean (value, self->playing); break;
	case PROP_SNAP:         g_value_set_boolean (value, self->snap); break;
	case PROP_PILOT:        g_value_set_boolean (value, self->pilot); break;
	case PROP_PREVIEW_MODE: g_value_set_enum (value, self->mode); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_editor_sequencer_panel_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (object);

	switch (prop_id)
	{
	case PROP_SNAP:         lrg_editor_sequencer_panel_set_snap (self, g_value_get_boolean (value)); break;
	case PROP_PILOT:        lrg_editor_sequencer_panel_set_pilot (self, g_value_get_boolean (value)); break;
	case PROP_PREVIEW_MODE: lrg_editor_sequencer_panel_set_preview_mode (self, g_value_get_enum (value)); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_editor_sequencer_panel_dispose (GObject *object)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (object);

	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), PANEL_KEY) == self)
		g_object_set_data (G_OBJECT (self->app), PANEL_KEY, NULL);
	if (self->editor != NULL)
		g_signal_handlers_disconnect_by_data (self->editor, self);
	if (self->view_state != NULL)
		g_signal_handlers_disconnect_by_data (self->view_state, self);
	if (self->player != NULL)
		g_signal_handlers_disconnect_by_data (self->player, self);
	if (self->track_menu != NULL)
		lrg_menu_close (self->track_menu);
	g_clear_object (&self->track_menu);
	g_clear_object (&self->player);
	g_clear_object (&self->editor);
	g_clear_object (&self->view_state);
	g_clear_object (&self->drag_before);
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_sequencer_panel_parent_class)->dispose (object);
}

static void
lrg_editor_sequencer_panel_finalize (GObject *object)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (object);

	g_clear_object (&self->seq);
	g_clear_object (&self->saved);
	g_free (self->path);
	g_free (self->cut_camera);
	g_free (self->binding_sig);
	g_clear_pointer (&self->undo, g_ptr_array_unref);
	g_clear_pointer (&self->undo_labels, g_ptr_array_unref);
	g_clear_pointer (&self->redo, g_ptr_array_unref);
	g_clear_pointer (&self->redo_labels, g_ptr_array_unref);
	g_clear_pointer (&self->keys, g_hash_table_unref);
	g_clear_pointer (&self->clips, g_hash_table_unref);
	g_clear_pointer (&self->markers, g_hash_table_unref);
	g_clear_pointer (&self->rows, g_hash_table_unref);
	g_clear_pointer (&self->collapsed, g_hash_table_unref);
	g_clear_pointer (&self->ops, g_ptr_array_unref);
	g_clear_pointer (&self->fired, json_array_unref);
	G_OBJECT_CLASS (lrg_editor_sequencer_panel_parent_class)->finalize (object);
}

static void
lrg_editor_sequencer_panel_class_init (LrgEditorSequencerPanelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = lrg_editor_sequencer_panel_get_property;
	object_class->set_property = lrg_editor_sequencer_panel_set_property;
	object_class->dispose = lrg_editor_sequencer_panel_dispose;
	object_class->finalize = lrg_editor_sequencer_panel_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_sequencer_panel_step;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_sequencer_panel_layout_children;

	/**
	 * LrgEditorSequencerPanel:dirty:
	 *
	 * Whether the sequence differs from its file (or is unsaved).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_DIRTY] =
		g_param_spec_boolean ("dirty", "Dirty", "Unsaved changes", FALSE,
		                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorSequencerPanel:playing:
	 *
	 * Whether the preview is playing.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_PLAYING] =
		g_param_spec_boolean ("playing", "Playing", "Preview playing", FALSE,
		                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorSequencerPanel:snap:
	 *
	 * Whether keys, sections and the playhead snap to the sequence's
	 * frames (else to milliseconds).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_SNAP] =
		g_param_spec_boolean ("snap", "Snap", "Snap to frames", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorSequencerPanel:pilot:
	 *
	 * Whether previewing flies the active viewport through the camera
	 * cuts.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_PILOT] =
		g_param_spec_boolean ("pilot", "Pilot", "Look through cut cameras", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorSequencerPanel:preview-mode:
	 *
	 * How the preview plays (once, loop, ping-pong); starts as the
	 * sequence's own play mode.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_PREVIEW_MODE] =
		g_param_spec_enum ("preview-mode", "Preview mode", "How the preview plays", LRG_TYPE_SEQUENCE_PLAY_MODE,
		                   LRG_SEQUENCE_PLAY_ONCE,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

static LrgWidget *
hbox (const gchar *name,
      gfloat       spacing)
{
	LrgWidget *row = LRG_WIDGET (lrg_hbox_new ());

	if (name != NULL)
		lrg_widget_set_name (row, name);
	lrg_container_set_spacing (LRG_CONTAINER (row), spacing);
	return row;
}

static LrgWidget *
vbox (const gchar *name,
      gfloat       spacing)
{
	LrgWidget *col = LRG_WIDGET (lrg_vbox_new ());

	if (name != NULL)
		lrg_widget_set_name (col, name);
	lrg_container_set_spacing (LRG_CONTAINER (col), spacing);
	return col;
}

static void
add_separator (LrgWidget *box)
{
	LrgSeparator   *sep = lrg_separator_new_with_orientation (LRG_ORIENTATION_VERTICAL);
	const LrgInsets ins = { 4.0f, 4.0f, 4.0f, 4.0f };

	lrg_widget_set_margin (LRG_WIDGET (sep), &ins);
	add_to (box, LRG_WIDGET (sep));
}

static LrgWidget *
caption (const gchar *text,
         gfloat       width)
{
	LrgWidget *label = LRG_WIDGET (lrg_label_new (text));

	lrg_widget_set_min_size (label, width, 0.0f);
	lrg_widget_set_valign (label, LRG_ALIGN_CENTER);
	return label;
}

/* "Label  [widget]" in a row of @box; returns the row. */
static LrgWidget *
field_row (LrgWidget   *box,
           const gchar *label,
           LrgWidget   *field)
{
	LrgWidget *row = hbox (NULL, 6.0f);

	add_to (row, caption (label, 70.0f));
	lrg_widget_set_fill_weight (field, 1.0f);
	add_to (row, field);
	lrg_container_add_child (LRG_CONTAINER (box), row);
	g_object_unref (row);
	return row;
}

static LrgSpinBox *
spin (const gchar *name,
      gdouble      min,
      gdouble      max,
      gdouble      step,
      guint        digits,
      gfloat       width)
{
	LrgSpinBox *s = lrg_spin_box_new_with_range (min, max, step);

	lrg_spin_box_set_digits (s, digits);
	lrg_widget_set_name (LRG_WIDGET (s), name);
	lrg_widget_set_min_size (LRG_WIDGET (s), width, 0.0f);
	/* wide ranges would make the spin as wide as its longest value */
	lrg_widget_set_max_size (LRG_WIDGET (s), width + 16.0f, 0.0f);
	return s;
}

static LrgTextInput *
text_field (const gchar *name,
            const gchar *placeholder)
{
	LrgTextInput *t = lrg_text_input_new_with_placeholder (placeholder);

	lrg_widget_set_name (LRG_WIDGET (t), name);
	lrg_widget_set_min_size (LRG_WIDGET (t), 80.0f, 0.0f);
	return t;
}

static void
build_toolbars (LrgEditorSequencerPanel *self)
{
	static const gchar *modes[] = { "Once", "Loop", "Ping-pong" };
	LrgWidget          *chip;
	guint               i;

	self->top = vbox (NULL, 4.0f);

	/* Row 1: file | length, fps | undo, redo */
	self->row1 = hbox ("sequencer-toolbar", 6.0f);
	self->title = LRG_WIDGET (lrg_label_new ("Untitled.rseq"));
	lrg_widget_set_name (self->title, "sequencer-title");
	lrg_widget_set_valign (self->title, LRG_ALIGN_CENTER);
	lrg_widget_set_min_size (self->title, 130.0f, 0.0f);
	lrg_container_add_child (LRG_CONTAINER (self->row1), self->title);
	g_object_unref (self->title);
	self->path_input = text_field ("sequencer-path", "sequences/intro.rseq");
	lrg_widget_set_min_size (LRG_WIDGET (self->path_input), 170.0f, 0.0f);
	g_signal_connect (self->path_input, "submitted", G_CALLBACK (on_path_submitted), self);
	add_to (self->row1, LRG_WIDGET (self->path_input));
	add_to (self->row1, editor_chip_new ("sequencer-new", LRG_ICON_PLUS, "New", on_new, self));
	add_to (self->row1, editor_chip_new ("sequencer-open", LRG_ICON_FOLDER, "Open", on_open, self));
	chip = editor_chip_new ("sequencer-save", LRG_ICON_DOWNLOAD, "Save", on_save, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_ACCENT);
	add_to (self->row1, chip);
	add_separator (self->row1);
	add_to (self->row1, caption ("Length", 0.0f));
	self->length = spin ("sequencer-length", 0.05, 36000.0, 0.5, 2, 100.0f);
	g_signal_connect (self->length, "value-changed", G_CALLBACK (on_length), self);
	add_to (self->row1, LRG_WIDGET (self->length));
	add_to (self->row1, caption ("FPS", 0.0f));
	self->fps = spin ("sequencer-fps", 1.0, 240.0, 1.0, 0, 84.0f);
	g_signal_connect (self->fps, "value-changed", G_CALLBACK (on_fps), self);
	add_to (self->row1, LRG_WIDGET (self->fps));
	add_separator (self->row1);
	self->undo_chip = editor_chip_new ("sequencer-undo", LRG_ICON_ARROW_LEFT, "Undo", on_undo, self);
	add_to (self->row1, self->undo_chip);
	self->redo_chip = editor_chip_new ("sequencer-redo", LRG_ICON_ARROW_RIGHT, "Redo", on_redo, self);
	add_to (self->row1, self->redo_chip);
	add_to (self->top, self->row1);

	/* Row 2: transport, mode | snap, pilot, curves | add track, bind, key, delete | time */
	self->row2 = hbox ("sequencer-transport", 6.0f);
	add_to (self->row2, editor_chip_new ("sequencer-prev", LRG_ICON_CHEVRON_LEFT, NULL, on_prev, self));
	self->play = editor_chip_new ("sequencer-play", LRG_ICON_PLAY, "Play", on_play, self);
	editor_chip_set_tint (self->play, LRG_THEME_COLOR_SUCCESS);
	add_to (self->row2, self->play);
	add_to (self->row2, editor_chip_new ("sequencer-stop", LRG_ICON_STOP, "Stop", on_stop, self));
	add_to (self->row2, editor_chip_new ("sequencer-next", LRG_ICON_CHEVRON_RIGHT, NULL, on_next, self));
	self->mode_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->mode_combo), "sequencer-mode");
	for (i = 0; i < G_N_ELEMENTS (modes); i++)
		lrg_combo_box_append (self->mode_combo, modes[i]);
	lrg_combo_box_set_selected (self->mode_combo, 0);
	lrg_widget_set_min_size (LRG_WIDGET (self->mode_combo), 100.0f, 0.0f);
	g_signal_connect (self->mode_combo, "changed", G_CALLBACK (on_mode), self);
	add_to (self->row2, LRG_WIDGET (self->mode_combo));
	add_separator (self->row2);
	self->snap_chip = editor_chip_new ("sequencer-snap", LRG_ICON_CHECK, "Snap", on_snap, self);
	editor_chip_set_checked (self->snap_chip, self->snap);
	add_to (self->row2, self->snap_chip);
	self->pilot_chip = editor_chip_new ("sequencer-pilot", LRG_ICON_EYE, "Pilot", on_pilot, self);
	add_to (self->row2, self->pilot_chip);
	self->curves_chip = editor_chip_new ("sequencer-curves", LRG_ICON_STAR_HALF, "Curves", on_curves, self);
	add_to (self->row2, self->curves_chip);
	add_separator (self->row2);
	self->add_track = editor_chip_new ("sequencer-add-track", LRG_ICON_PLUS, "Track", on_add_track_chip, self);
	editor_chip_set_tint (self->add_track, LRG_THEME_COLOR_ACCENT);
	add_to (self->row2, self->add_track);
	add_to (self->row2, editor_chip_new ("sequencer-bind", LRG_ICON_USER, "Bind", on_bind, self));
	chip = editor_chip_new ("sequencer-key", LRG_ICON_PLUS, "Key", on_key, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_WARNING);
	add_to (self->row2, chip);
	chip = editor_chip_new ("sequencer-delete", LRG_ICON_TRASH, "Delete", on_delete, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
	add_to (self->row2, chip);
	chip = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_fill_weight (chip, 1.0f);
	add_to (self->row2, chip);
	self->time_label = LRG_WIDGET (lrg_label_new ("0 / 0"));
	lrg_widget_set_name (self->time_label, "sequencer-time");
	lrg_widget_set_valign (self->time_label, LRG_ALIGN_CENTER);
	add_to (self->row2, self->time_label);
	add_to (self->top, self->row2);

	lrg_container_add_child (LRG_CONTAINER (self), self->top);
	g_object_unref (self->top);
}

static void
build_strip (LrgEditorSequencerPanel *self)
{
	static const gchar *tangents[] = { "Auto", "User", "Broken", "Linear", "Constant" };
	LrgWidget          *left, *detail, *row;
	guint               i;

	self->strip = hbox ("sequencer-strip", 8.0f);

	/* Left: the curve editor and the selected key's fields */
	left = vbox (NULL, 4.0f);
	lrg_widget_set_fill_weight (left, 1.0f);
	self->curve = lrg_custom_widget_new ();
	lrg_widget_set_name (LRG_WIDGET (self->curve), "sequencer-curve");
	lrg_widget_set_focusable (LRG_WIDGET (self->curve), TRUE);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->curve), 1.0f);
	{
		LrgCustomWidgetFuncs funcs;

		memset (&funcs, 0, sizeof funcs);
		funcs.measure = curve_measure;
		funcs.paint = curve_paint;
		funcs.event = curve_event;
		lrg_custom_widget_set_funcs (self->curve, &funcs, self, NULL);
	}
	lrg_container_add_child (LRG_CONTAINER (left), LRG_WIDGET (self->curve));
	g_object_unref (self->curve);

	self->key_row = hbox ("sequencer-key-row", 6.0f);
	self->key_time = spin ("sequencer-key-time", 0.0, 36000.0, 1.0 / 30.0, 2, 88.0f);
	g_signal_connect (self->key_time, "value-changed", G_CALLBACK (on_key_time), self);
	add_to (self->key_row, LRG_WIDGET (self->key_time));
	self->key_value = spin ("sequencer-key-value", -1e6, 1e6, 0.1, 2, 88.0f);
	g_signal_connect (self->key_value, "value-changed", G_CALLBACK (on_key_value), self);
	add_to (self->key_row, LRG_WIDGET (self->key_value));
	self->key_tangent = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->key_tangent), "sequencer-key-tangent");
	for (i = 0; i < G_N_ELEMENTS (tangents); i++)
		lrg_combo_box_append (self->key_tangent, tangents[i]);
	lrg_combo_box_set_selected (self->key_tangent, 0);
	lrg_widget_set_min_size (LRG_WIDGET (self->key_tangent), 92.0f, 0.0f);
	lrg_widget_set_max_size (LRG_WIDGET (self->key_tangent), 110.0f, 0.0f);
	g_signal_connect (self->key_tangent, "changed", G_CALLBACK (on_key_tangent), self);
	add_to (self->key_row, LRG_WIDGET (self->key_tangent));
	add_to (self->key_row, caption ("In", 0.0f));
	self->key_in = spin ("sequencer-key-in", -1e6, 1e6, 0.1, 2, 88.0f);
	g_signal_connect (self->key_in, "value-changed", G_CALLBACK (on_key_in), self);
	add_to (self->key_row, LRG_WIDGET (self->key_in));
	add_to (self->key_row, caption ("Out", 0.0f));
	self->key_out = spin ("sequencer-key-out", -1e6, 1e6, 0.1, 2, 88.0f);
	g_signal_connect (self->key_out, "value-changed", G_CALLBACK (on_key_out), self);
	add_to (self->key_row, LRG_WIDGET (self->key_out));
	lrg_container_add_child (LRG_CONTAINER (left), self->key_row);
	g_object_unref (self->key_row);
	add_to (self->strip, left);

	/* Right: track or section fields */
	detail = vbox ("sequencer-detail", 4.0f);
	lrg_widget_set_min_size (detail, DETAIL_WIDTH, 0.0f);
	self->detail_title = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_name (self->detail_title, "sequencer-detail-title");
	lrg_container_add_child (LRG_CONTAINER (detail), self->detail_title);
	g_object_unref (self->detail_title);

	self->track_box = vbox ("sequencer-track-box", 4.0f);
	self->track_name = text_field ("sequencer-track-name", "Track name");
	g_signal_connect (self->track_name, "submitted", G_CALLBACK (on_track_name), self);
	field_row (self->track_box, "Name", LRG_WIDGET (self->track_name));
	self->track_binding = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->track_binding), "sequencer-track-binding");
	lrg_combo_box_set_placeholder (self->track_binding, "(none)");
	g_signal_connect (self->track_binding, "changed", G_CALLBACK (on_track_binding), self);
	field_row (self->track_box, "Binding", LRG_WIDGET (self->track_binding));
	self->track_bone = text_field ("sequencer-track-bone", "bone name");
	g_signal_connect (self->track_bone, "submitted", G_CALLBACK (on_track_bone), self);
	self->bone_row = field_row (self->track_box, "Bone", LRG_WIDGET (self->track_bone));
	row = hbox (NULL, 6.0f);
	self->track_component = text_field ("sequencer-track-component", "Component");
	g_signal_connect (self->track_component, "submitted", G_CALLBACK (on_track_component), self);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->track_component), 1.0f);
	add_to (row, LRG_WIDGET (self->track_component));
	self->track_property = text_field ("sequencer-track-property", "property");
	g_signal_connect (self->track_property, "submitted", G_CALLBACK (on_track_property), self);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->track_property), 1.0f);
	add_to (row, LRG_WIDGET (self->track_property));
	self->prop_row = field_row (self->track_box, "Property", row);
	self->track_mute = editor_chip_new ("sequencer-track-mute", LRG_ICON_CLOSE, "Mute", on_track_mute, self);
	add_to (self->track_box, self->track_mute);
	lrg_container_add_child (LRG_CONTAINER (detail), self->track_box);
	g_object_unref (self->track_box);

	self->section_box = vbox ("sequencer-section-box", 4.0f);
	self->sec_name = text_field ("sequencer-section-name", "camera / sound / clip / sequence / event");
	g_signal_connect (self->sec_name, "submitted", G_CALLBACK (on_sec_name), self);
	field_row (self->section_box, "Name", LRG_WIDGET (self->sec_name));
	row = hbox (NULL, 6.0f);
	self->sec_start = spin ("sequencer-section-start", 0.0, 36000.0, 1.0 / 30.0, 2, 100.0f);
	g_signal_connect (self->sec_start, "value-changed", G_CALLBACK (on_sec_start), self);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->sec_start), 1.0f);
	add_to (row, LRG_WIDGET (self->sec_start));
	self->sec_duration = spin ("sequencer-section-duration", 0.0, 36000.0, 0.1, 2, 100.0f);
	g_signal_connect (self->sec_duration, "value-changed", G_CALLBACK (on_sec_duration), self);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->sec_duration), 1.0f);
	add_to (row, LRG_WIDGET (self->sec_duration));
	field_row (self->section_box, "Start, len", row);
	row = hbox (NULL, 6.0f);
	self->sec_offset = spin ("sequencer-section-offset", -36000.0, 36000.0, 0.1, 2, 100.0f);
	g_signal_connect (self->sec_offset, "value-changed", G_CALLBACK (on_sec_offset), self);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->sec_offset), 1.0f);
	add_to (row, LRG_WIDGET (self->sec_offset));
	self->sec_rate = spin ("sequencer-section-rate", -100.0, 100.0, 0.1, 2, 100.0f);
	g_signal_connect (self->sec_rate, "value-changed", G_CALLBACK (on_sec_rate), self);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->sec_rate), 1.0f);
	add_to (row, LRG_WIDGET (self->sec_rate));
	field_row (self->section_box, "Offset, rate", row);
	row = hbox (NULL, 6.0f);
	self->sec_weight = spin ("sequencer-section-weight", 0.0, 10.0, 0.05, 2, 100.0f);
	g_signal_connect (self->sec_weight, "value-changed", G_CALLBACK (on_sec_weight), self);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->sec_weight), 1.0f);
	add_to (row, LRG_WIDGET (self->sec_weight));
	self->sec_loop = editor_chip_new ("sequencer-section-loop", LRG_ICON_REFRESH, "Loop", on_sec_loop, self);
	add_to (row, self->sec_loop);
	field_row (self->section_box, "Weight", row);
	self->sec_payload = text_field ("sequencer-section-payload", "payload");
	g_signal_connect (self->sec_payload, "submitted", G_CALLBACK (on_sec_payload), self);
	self->payload_row = field_row (self->section_box, "Payload", LRG_WIDGET (self->sec_payload));
	lrg_container_add_child (LRG_CONTAINER (detail), self->section_box);
	g_object_unref (self->section_box);

	add_to (self->strip, detail);
	lrg_container_add_child (LRG_CONTAINER (self), self->strip);
	g_object_unref (self->strip);
}

static void
lrg_editor_sequencer_panel_init (LrgEditorSequencerPanel *self)
{
	self->keys = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	self->clips = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	self->markers = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	self->rows = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->collapsed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->ops = g_ptr_array_new_with_free_func (g_free);
	self->undo = g_ptr_array_new_with_free_func (g_object_unref);
	self->undo_labels = g_ptr_array_new_with_free_func (g_free);
	self->redo = g_ptr_array_new_with_free_func (g_object_unref);
	self->redo_labels = g_ptr_array_new_with_free_func (g_free);
	self->fired = json_array_new ();
	self->seq = lrg_sequence_new ();
	self->saved = NULL;
	self->snap = TRUE;
	self->mode = LRG_SEQUENCE_PLAY_ONCE;
	self->sel_track = -1;
	self->sel_section = -1;
	self->curve_track = -1;
	self->curve_key = -1;
	self->last_press = -10.0;
	self->view_dirty = TRUE;
	self->fit_pending = TRUE;

	self->player = lrg_sequence_player_new (self->seq);
	g_signal_connect (self->player, "event", G_CALLBACK (on_player_event), self);
	g_signal_connect (self->player, "finished", G_CALLBACK (on_player_finished), self);

	build_toolbars (self);

	/* The tracks */
	self->view = lrg_timeline_view_new ();
	lrg_widget_set_name (LRG_WIDGET (self->view), "sequencer-view");
	lrg_timeline_view_set_show_play_button (self->view, FALSE);
	lrg_timeline_view_set_look (self->view, LRG_CANVAS_LOOK_AUTO);
	lrg_timeline_view_set_header_width (self->view, 190.0f);
	lrg_timeline_view_set_time_format (self->view, LRG_TIMELINE_VIEW_TIME_FORMAT_SECONDS);
	g_signal_connect (self->view, "keyframe-moved", G_CALLBACK (on_keyframe_moved), self);
	g_signal_connect (self->view, "keyframe-removed", G_CALLBACK (on_keyframe_removed), self);
	g_signal_connect (self->view, "keyframe-added", G_CALLBACK (on_keyframe_added), self);
	g_signal_connect (self->view, "clip-moved", G_CALLBACK (on_clip_moved), self);
	g_signal_connect (self->view, "clip-resized", G_CALLBACK (on_clip_resized), self);
	g_signal_connect (self->view, "clip-removed", G_CALLBACK (on_clip_removed), self);
	g_signal_connect (self->view, "marker-moved", G_CALLBACK (on_marker_moved), self);
	g_signal_connect (self->view, "playhead-moved", G_CALLBACK (on_playhead_moved), self);
	g_signal_connect (self->view, "selection-changed", G_CALLBACK (on_selection_changed), self);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->view));
	g_object_unref (self->view);

	self->hint = editor_panel_hint_new ("sequencer-empty", "No tracks yet. Select nodes in the level and press Bind, "
	                                                       "then add tracks with + Track and key them with Key.");
	lrg_container_add_child (LRG_CONTAINER (self), self->hint);
	g_object_unref (self->hint);

	build_strip (self);

	/* "seq.add-track::<kind>" for the Add Track menu */
	{
		g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
		g_autoptr(GSimpleAction)      add = g_simple_action_new ("add-track", G_VARIANT_TYPE_STRING);

		g_signal_connect (add, "activate", G_CALLBACK (on_add_track_action), self);
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (add));
		lrg_widget_insert_action_group (LRG_WIDGET (self), "seq", G_ACTION_GROUP (group));
	}
	set_dirty (self);
}

/* ==========================================================================
 * IPC: sequence.*
 * ========================================================================== */

#define IPC_FAIL(code, ...) \
	G_STMT_START { g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_##code, __VA_ARGS__); \
	               return NULL; } G_STMT_END

static LrgEditorSequencerPanel *
ipc_panel (gpointer   user_data,
           GError   **error)
{
	LrgEditorSequencerPanel *self = lrg_editor_sequencer_panel_find (LRG_EDITOR_APP (user_data));

	if (self == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No sequencer panel");
	return self;
}

static const gchar *
opt_string (JsonObject  *params,
            const gchar *member)
{
	JsonNode *n = params != NULL ? json_object_get_member (params, member) : NULL;

	return (n != NULL && JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
	       ? json_node_get_string (n) : NULL;
}

static gboolean
opt_number (JsonObject  *params,
            const gchar *member,
            gdouble     *out)
{
	JsonNode *n = params != NULL ? json_object_get_member (params, member) : NULL;

	if (n == NULL || !JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	switch (json_node_get_value_type (n))
	{
	case G_TYPE_INT64:
		*out = (gdouble) json_node_get_int (n);
		return TRUE;
	case G_TYPE_DOUBLE:
		*out = json_node_get_double (n);
		return isfinite (*out);
	default:
		return FALSE;
	}
}

/*
 * ipc_document:
 *
 * The panel, with the sequence of @params' "path" open: a path other
 * than the open one is loaded first (sequence methods "open the file's
 * editor when it is not open", like material.*).
 */
static LrgEditorSequencerPanel *
ipc_document (gpointer     user_data,
              JsonObject  *params,
              GError     **error)
{
	LrgEditorSequencerPanel *self = ipc_panel (user_data, error);
	const gchar             *path = opt_string (params, "path");
	g_autofree gchar        *abs = NULL;
	g_autoptr(GError)        local = NULL;

	if (self == NULL || path == NULL)
		return self;
	abs = abs_path (self, path);
	if (g_strcmp0 (abs, self->path) == 0)
		return self;
	if (!g_file_test (abs, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such sequence: %s", abs);
		return NULL;
	}
	if (!lrg_editor_sequencer_panel_load (self, abs, &local))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s", local->message);
		return NULL;
	}
	return self;
}

/* The "track" parameter as a valid index, or -1 with @error set. */
static gint
ipc_track (LrgEditorSequencerPanel  *self,
           JsonObject               *params,
           GError                  **error)
{
	gdouble v;

	if (!opt_number (params, "track", &v))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "track (index) is required");
		return -1;
	}
	if (v < 0 || v >= lrg_sequence_get_n_tracks (self->seq))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No track %g", v);
		return -1;
	}
	return (gint) v;
}

static JsonObject *
key_json (LrgSequenceCurve *curve,
          guint             index)
{
	JsonObject       *o = json_object_new ();
	LrgSequenceKey    key;
	gfloat            in_s = 0.0f, out_s = 0.0f;
	g_autofree gchar *mode = NULL;

	lrg_sequence_curve_get_key (curve, index, &key);
	lrg_sequence_curve_get_slopes (curve, index, &in_s, &out_s);
	mode = enum_nick (LRG_TYPE_SEQUENCE_TANGENT_MODE, key.tangent_mode);
	json_object_set_int_member (o, "index", index);
	json_object_set_double_member (o, "time", key.time);
	json_object_set_double_member (o, "value", key.value);
	json_object_set_string_member (o, "tangent", mode);
	json_object_set_double_member (o, "in", in_s);
	json_object_set_double_member (o, "out", out_s);
	return o;
}

static JsonArray *
curve_json (LrgSequenceCurve *curve)
{
	JsonArray *a = json_array_new ();
	guint      k;

	for (k = 0; curve != NULL && k < lrg_sequence_curve_get_n_keys (curve); k++)
		json_array_add_object_element (a, key_json (curve, k));
	return a;
}

static JsonObject *
sequence_json (LrgEditorSequencerPanel *self)
{
	JsonObject       *o = json_object_new ();
	JsonArray        *bindings = json_array_new ();
	JsonArray        *tracks = json_array_new ();
	g_autofree gchar *mode = enum_nick (LRG_TYPE_SEQUENCE_PLAY_MODE, lrg_sequence_get_play_mode (self->seq));
	guint             i, s;
	gint              c;

	if (self->path != NULL)
		json_object_set_string_member (o, "path", self->path);
	else
		json_object_set_null_member (o, "path");
	json_object_set_string_member (o, "name", lrg_sequence_get_name (self->seq) != NULL
	                                          ? lrg_sequence_get_name (self->seq) : "");
	json_object_set_double_member (o, "length", lrg_sequence_get_length (self->seq));
	json_object_set_double_member (o, "fps", lrg_sequence_get_fps (self->seq));
	json_object_set_string_member (o, "play_mode", mode);
	json_object_set_boolean_member (o, "dirty", self->dirty);
	json_object_set_double_member (o, "time", lrg_sequence_player_get_time (self->player));
	json_object_set_boolean_member (o, "playing", self->playing);

	for (i = 0; i < lrg_sequence_get_n_bindings (self->seq); i++)
	{
		JsonObject  *b = json_object_new ();
		const gchar *guid, *name;

		lrg_sequence_get_binding (self->seq, i, &guid, &name);
		json_object_set_string_member (b, "guid", guid);
		json_object_set_string_member (b, "name", name != NULL ? name : "");
		json_array_add_object_element (bindings, b);
	}
	json_object_set_array_member (o, "bindings", bindings);

	for (i = 0; i < lrg_sequence_get_n_tracks (self->seq); i++)
	{
		LrgSequenceTrack *t = lrg_sequence_get_track (self->seq, i);
		LrgSequenceTrackKind kind = lrg_sequence_track_get_kind (t);
		JsonObject       *to = json_object_new ();
		g_autofree gchar *nick = enum_nick (LRG_TYPE_SEQUENCE_TRACK_KIND, kind);
		g_autofree gchar *bl = NULL;
		const gchar      *b = lrg_sequence_track_get_binding (t);

		json_object_set_int_member (to, "index", i);
		json_object_set_string_member (to, "kind", nick);
		json_object_set_string_member (to, "name", lrg_sequence_track_get_name (t) != NULL
		                                           ? lrg_sequence_track_get_name (t) : "");
		if (b != NULL)
		{
			bl = binding_label (self, b);
			json_object_set_string_member (to, "binding", b);
			json_object_set_string_member (to, "binding_name", bl);
		}
		else
		{
			json_object_set_null_member (to, "binding");
			json_object_set_null_member (to, "binding_name");
		}
		json_object_set_boolean_member (to, "muted", lrg_sequence_track_get_muted (t));
		if (kind == LRG_SEQUENCE_TRACK_TRANSFORM)
		{
			JsonObject *channels = json_object_new ();

			if (lrg_sequence_track_get_bone (t) != NULL)
				json_object_set_string_member (to, "bone", lrg_sequence_track_get_bone (t));
			for (c = 0; c < LRG_SEQUENCE_N_CHANNELS; c++)
			{
				LrgSequenceCurve *curve = lrg_sequence_track_get_channel_curve (t, (LrgSequenceTransformChannel) c);

				if (curve != NULL)
					json_object_set_array_member (channels,
					                              lrg_sequence_transform_channel_to_string ((LrgSequenceTransformChannel) c),
					                              curve_json (curve));
			}
			json_object_set_object_member (to, "channels", channels);
		}
		else if (kind == LRG_SEQUENCE_TRACK_PROPERTY)
		{
			if (lrg_sequence_track_get_component_type (t) != NULL)
				json_object_set_string_member (to, "component", lrg_sequence_track_get_component_type (t));
			if (lrg_sequence_track_get_property_name (t) != NULL)
				json_object_set_string_member (to, "property", lrg_sequence_track_get_property_name (t));
			json_object_set_array_member (to, "keys", curve_json (lrg_sequence_track_get_curve (t)));
		}
		else
		{
			JsonArray *sections = json_array_new ();

			for (s = 0; s < lrg_sequence_track_get_n_sections (t); s++)
			{
				const LrgSequenceSection *sec = lrg_sequence_track_get_section (t, s);
				JsonObject               *so = json_object_new ();

				json_object_set_int_member (so, "index", s);
				json_object_set_double_member (so, "start", sec->start);
				json_object_set_double_member (so, "duration", sec->duration);
				json_object_set_double_member (so, "end",
				                               lrg_sequence_track_get_section_end (t, s, lrg_sequence_get_length (self->seq)));
				json_object_set_string_member (so, "name", sec->name != NULL ? sec->name : "");
				if (sec->payload != NULL)
					json_object_set_string_member (so, "payload", sec->payload);
				json_object_set_double_member (so, "offset", sec->offset);
				json_object_set_double_member (so, "rate", sec->rate);
				json_object_set_boolean_member (so, "loop", sec->loop);
				json_object_set_double_member (so, "weight", sec->weight);
				json_array_add_object_element (sections, so);
			}
			json_object_set_array_member (to, "sections", sections);
		}
		json_array_add_object_element (tracks, to);
	}
	json_object_set_array_member (o, "tracks", tracks);
	return o;
}

static JsonNode *
m_list (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel *self = ipc_panel (user_data, error);
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	if (self->path != NULL)
		json_object_set_string_member (o, "open", self->path);
	else
		json_object_set_null_member (o, "open");
	json_object_set_boolean_member (o, "dirty", self->dirty);
	json_object_set_int_member (o, "tracks", lrg_sequence_get_n_tracks (self->seq));
	return obj_node (o);
}

static JsonNode *
m_open (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel *self;
	JsonObject              *o;

	if (opt_string (params, "path") == NULL)
		IPC_FAIL (INVALID_PARAMS, "path is required");
	self = ipc_document (user_data, params, error);
	if (self == NULL)
		return NULL;
	editor_app_show_panel (LRG_EDITOR_APP (user_data), LRG_EDITOR_SEQUENCER_PANEL_ID);
	o = json_object_new ();
	json_object_set_string_member (o, "path", self->path);
	json_object_set_string_member (o, "panel", LRG_EDITOR_SEQUENCER_PANEL_ID);
	return obj_node (o);
}

static JsonNode *
m_create (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel *self = ipc_panel (user_data, error);
	const gchar             *path = opt_string (params, "path");
	const gchar             *mode = opt_string (params, "play_mode");
	g_autoptr(GError)        local = NULL;
	LrgSequence             *edit;
	gdouble                  v;
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	if (path == NULL)
		IPC_FAIL (INVALID_PARAMS, "path is required");
	if (mode != NULL && enum_from_nick (LRG_TYPE_SEQUENCE_PLAY_MODE, mode) < 0)
		IPC_FAIL (INVALID_PARAMS, "play_mode must be once, loop or ping-pong");
	if (!lrg_editor_sequencer_panel_create (self, path, &local))
		IPC_FAIL (FAILED, "%s", local->message);

	/* settings go into the new file at once (a clean document) */
	edit = lrg_sequence_copy (self->seq);
	if (opt_string (params, "name") != NULL)
		lrg_sequence_set_name (edit, opt_string (params, "name"));
	if (opt_number (params, "length", &v) && v > 0.0)
		lrg_sequence_set_length (edit, (gfloat) v);
	if (opt_number (params, "fps", &v) && v > 0.0)
		lrg_sequence_set_fps (edit, (gfloat) v);
	if (mode != NULL)
		lrg_sequence_set_play_mode (edit, (LrgSequencePlayMode) enum_from_nick (LRG_TYPE_SEQUENCE_PLAY_MODE, mode));
	if (!lrg_sequence_equal (edit, self->seq))
	{
		reset_document (self, edit, self->path);
		lrg_editor_sequencer_panel_save (self, NULL, NULL);
	}
	g_object_unref (edit);
	editor_app_show_panel (LRG_EDITOR_APP (user_data), LRG_EDITOR_SEQUENCER_PANEL_ID);
	o = json_object_new ();
	json_object_set_string_member (o, "path", self->path);
	return obj_node (o);
}

static JsonNode *
m_get (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel *self = ipc_document (user_data, params, error);

	return self != NULL ? obj_node (sequence_json (self)) : NULL;
}

static JsonNode *
m_set (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel *self = ipc_document (user_data, params, error);
	const gchar             *mode = opt_string (params, "play_mode");
	LrgSequence             *edit;
	gdouble                  v;

	if (self == NULL)
		return NULL;
	if (mode != NULL && enum_from_nick (LRG_TYPE_SEQUENCE_PLAY_MODE, mode) < 0)
		IPC_FAIL (INVALID_PARAMS, "play_mode must be once, loop or ping-pong");
	edit = working_copy (self);
	if (opt_string (params, "name") != NULL)
		lrg_sequence_set_name (edit, opt_string (params, "name"));
	if (opt_number (params, "length", &v))
	{
		if (v <= 0.0)
		{
			g_object_unref (edit);
			IPC_FAIL (INVALID_PARAMS, "length must be > 0");
		}
		lrg_sequence_set_length (edit, (gfloat) v);
		self->fit_pending = TRUE;
	}
	if (opt_number (params, "fps", &v))
	{
		if (v <= 0.0)
		{
			g_object_unref (edit);
			IPC_FAIL (INVALID_PARAMS, "fps must be > 0");
		}
		lrg_sequence_set_fps (edit, (gfloat) v);
	}
	if (mode != NULL)
	{
		lrg_sequence_set_play_mode (edit, (LrgSequencePlayMode) enum_from_nick (LRG_TYPE_SEQUENCE_PLAY_MODE, mode));
		lrg_editor_sequencer_panel_set_preview_mode (self, lrg_sequence_get_play_mode (edit));
	}
	commit_take (self, edit, "Sequence Settings");
	return obj_node (sequence_json (self));
}

static JsonNode *
m_add_track (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel *self = ipc_document (user_data, params, error);
	const gchar             *kind_nick = opt_string (params, "kind");
	gint                     kind, index;
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	kind = enum_from_nick (LRG_TYPE_SEQUENCE_TRACK_KIND, kind_nick);
	if (kind < 0)
		IPC_FAIL (INVALID_PARAMS, "kind must be transform, property, camera-cut, audio, event, animation or subsequence");
	index = add_track_kind (self, (LrgSequenceTrackKind) kind,
	                        kind == LRG_SEQUENCE_TRACK_TRANSFORM ? opt_string (params, "bone") : NULL,
	                        opt_string (params, "name"), opt_string (params, "binding"));
	if (kind == LRG_SEQUENCE_TRACK_PROPERTY &&
	    (opt_string (params, "property") != NULL || opt_string (params, "component") != NULL))
	{
		LrgSequence      *edit = working_copy (self);
		LrgSequenceTrack *t = lrg_sequence_get_track (edit, (guint) index);

		lrg_sequence_track_set_property_name (t, opt_string (params, "property"));
		lrg_sequence_track_set_component_type (t, opt_string (params, "component"));
		/* part of the same edit: replace the "Add Track" step's result */
		commit_take (self, edit, "Track Property");
	}
	o = json_object_new ();
	json_object_set_int_member (o, "index", index);
	return obj_node (o);
}

/* The curve named by "track" + "channel" (transform) of @seq. */
static LrgSequenceCurve *
ipc_curve (LrgEditorSequencerPanel  *self,
           LrgSequence              *seq,
           JsonObject               *params,
           gint                      track,
           gboolean                  create,
           gint                     *out_channel,
           GError                  **error)
{
	LrgSequenceTrack           *t = lrg_sequence_get_track (seq, (guint) track);
	const gchar                *channel = opt_string (params, "channel");
	LrgSequenceTransformChannel ch;

	switch (lrg_sequence_track_get_kind (t))
	{
	case LRG_SEQUENCE_TRACK_PROPERTY:
		*out_channel = CH_CURVE;
		return lrg_sequence_track_get_curve (t);
	case LRG_SEQUENCE_TRACK_TRANSFORM:
		if (channel == NULL || !lrg_sequence_transform_channel_from_string (channel, &ch))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "channel (location.x ... scale.z) is required for a transform track");
			return NULL;
		}
		*out_channel = (gint) ch;
		if (create)
			return lrg_sequence_track_ensure_channel_curve (t, ch);
		if (lrg_sequence_track_get_channel_curve (t, ch) == NULL)
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "%s is not keyed", channel);
		return lrg_sequence_track_get_channel_curve (t, ch);
	default:
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "track %d has sections, not keys (use sequence.add_section)", track);
		return NULL;
	}
}

/* Applies "tangent", "in", "out" / "slope" of @params to key @k. */
static gboolean
ipc_tangents (LrgSequenceCurve  *curve,
              guint              k,
              JsonObject        *params,
              GError           **error)
{
	const gchar   *nick = opt_string (params, "tangent");
	LrgSequenceKey key;
	gfloat         in_s = 0.0f, out_s = 0.0f;
	gdouble        v;
	gint           mode;
	gboolean       any = FALSE;

	lrg_sequence_curve_get_key (curve, k, &key);
	lrg_sequence_curve_get_slopes (curve, k, &in_s, &out_s);
	mode = key.tangent_mode;
	if (nick != NULL)
	{
		mode = enum_from_nick (LRG_TYPE_SEQUENCE_TANGENT_MODE, nick);
		if (mode < 0)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "tangent must be auto, user, broken, linear or constant");
			return FALSE;
		}
		any = TRUE;
	}
	if (opt_number (params, "slope", &v))
	{
		in_s = out_s = (gfloat) v;
		any = TRUE;
		if (nick == NULL)
			mode = LRG_SEQUENCE_TANGENT_USER;
	}
	if (opt_number (params, "in", &v))
	{
		in_s = (gfloat) v;
		any = TRUE;
		if (nick == NULL && mode != LRG_SEQUENCE_TANGENT_BROKEN)
			mode = LRG_SEQUENCE_TANGENT_USER;
	}
	if (opt_number (params, "out", &v))
	{
		out_s = (gfloat) v;
		any = TRUE;
		if (nick == NULL)
			mode = LRG_SEQUENCE_TANGENT_BROKEN;
	}
	if (any)
		lrg_sequence_curve_set_key_tangents (curve, k, (LrgSequenceTangentMode) mode, in_s,
		                                     mode == LRG_SEQUENCE_TANGENT_BROKEN ? out_s : in_s);
	return TRUE;
}

static JsonNode *
m_add_key (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel *self = ipc_document (user_data, params, error);
	LrgSequence             *edit;
	LrgSequenceCurve        *curve;
	LrgSequenceTrack        *t;
	LrgNode                 *node;
	gint                     track, channel = CH_CURVE;
	gdouble                  time, value;
	guint                    k;
	JsonObject              *o;

	if (self == NULL || (track = ipc_track (self, params, error)) < 0)
		return NULL;
	if (!opt_number (params, "time", &time))
		IPC_FAIL (INVALID_PARAMS, "time is required");
	edit = working_copy (self);
	curve = ipc_curve (self, edit, params, track, TRUE, &channel, error);
	if (curve == NULL)
	{
		g_object_unref (edit);
		return NULL;
	}
	t = lrg_sequence_get_track (edit, (guint) track);
	time = MAX (snap_time (self, (gfloat) time), 0.0f);

	/* default value: the bound node's current channel value, else the curve */
	if (!opt_number (params, "value", &value))
	{
		gfloat           v = lrg_sequence_curve_evaluate (curve, (gfloat) time);
		g_autofree gchar *nc = NULL;

		node = lrg_editor_find_node (self->editor, lrg_sequence_track_get_binding (t));
		if (channel >= 0)
			nc = node_channel (t, channel);
		else if (lrg_sequence_track_get_component_type (t) == NULL &&
		         lrg_timeline_channel_is_valid (lrg_sequence_track_get_property_name (t)))
			nc = g_strdup (lrg_sequence_track_get_property_name (t));
		if (node != NULL && nc != NULL)
			lrg_timeline_channel_read (node, nc, &v);
		value = v;
	}
	k = lrg_sequence_curve_set_key (curve, (gfloat) time, (gfloat) value, LRG_SEQUENCE_TANGENT_AUTO);
	if (!ipc_tangents (curve, k, params, error))
	{
		g_object_unref (edit);
		return NULL;
	}
	commit_take (self, edit, "Add Key");
	self->curve_track = track;
	self->curve_channel = channel;
	self->curve_key = (gint) k;
	self->view_dirty = TRUE;
	o = json_object_new ();
	json_object_set_int_member (o, "index", k);
	return obj_node (o);
}

/* The key named by "key" (index) or "at" (time) in @curve, or -1. */
static gint
ipc_key_index (LrgSequenceCurve  *curve,
               JsonObject        *params,
               GError           **error)
{
	gdouble v;
	gint    k = -1;

	if (opt_number (params, "key", &v))
		k = (v >= 0 && v < lrg_sequence_curve_get_n_keys (curve)) ? (gint) v : -1;
	else if (opt_number (params, "at", &v))
		k = lrg_sequence_curve_find_key (curve, (gfloat) v, TIME_EPSILON);
	else
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "key (index) or at (time)");
		return -1;
	}
	if (k < 0)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such key");
	return k;
}

static JsonNode *
m_set_key (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel *self = ipc_document (user_data, params, error);
	LrgSequence             *edit;
	LrgSequenceCurve        *curve;
	LrgSequenceKey           key;
	gint                     track, channel = CH_CURVE, k;
	gdouble                  v;
	JsonObject              *o;

	if (self == NULL || (track = ipc_track (self, params, error)) < 0)
		return NULL;
	edit = working_copy (self);
	curve = ipc_curve (self, edit, params, track, FALSE, &channel, error);
	if (curve == NULL || (k = ipc_key_index (curve, params, error)) < 0)
	{
		g_object_unref (edit);
		return NULL;
	}
	if (opt_number (params, "value", &v))
	{
		lrg_sequence_curve_get_key (curve, (guint) k, &key);
		lrg_sequence_curve_remove_key (curve, (guint) k);
		key.value = (gfloat) v;
		k = (gint) lrg_sequence_curve_insert_key (curve, &key);
	}
	if (opt_number (params, "time", &v))
		k = lrg_sequence_curve_move_key (curve, (guint) k, MAX (snap_time (self, (gfloat) v), 0.0f));
	if (k < 0 || !ipc_tangents (curve, (guint) k, params, error))
	{
		g_object_unref (edit);
		return NULL;
	}
	commit_take (self, edit, "Edit Key");
	self->curve_track = track;
	self->curve_channel = channel;
	self->curve_key = k;
	sync_detail (self);
	o = json_object_new ();
	json_object_set_int_member (o, "index", k);
	return obj_node (o);
}

static JsonNode *
m_add_section (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel      *self = ipc_document (user_data, params, error);
	g_autoptr(LrgSequenceSection) sec = NULL;
	LrgSequence                  *edit;
	LrgSequenceTrack             *t;
	const gchar                  *name = opt_string (params, "name");
	g_autofree gchar             *target = NULL;
	gint                          track;
	gdouble                       v;
	guint                         index;
	JsonObject                   *o;

	if (self == NULL || (track = ipc_track (self, params, error)) < 0)
		return NULL;
	if (!opt_number (params, "start", &v))
		IPC_FAIL (INVALID_PARAMS, "start is required");
	if (name == NULL || *name == '\0')
		IPC_FAIL (INVALID_PARAMS, "name is required (camera, sound, clip, sequence or event name)");
	t = lrg_sequence_get_track (self->seq, (guint) track);
	if (lrg_sequence_track_get_kind (t) == LRG_SEQUENCE_TRACK_TRANSFORM ||
	    lrg_sequence_track_get_kind (t) == LRG_SEQUENCE_TRACK_PROPERTY)
		IPC_FAIL (INVALID_PARAMS, "track %d has keys, not sections (use sequence.add_key)", track);

	edit = working_copy (self);
	t = lrg_sequence_get_track (edit, (guint) track);
	target = g_strdup (name);
	if (lrg_sequence_track_get_kind (t) == LRG_SEQUENCE_TRACK_CAMERA_CUT)
	{
		/* a camera named by a node becomes its GUID (and a binding) */
		LrgNode *node = lrg_editor_find_node (self->editor, name);

		if (node != NULL && lrg_node_get_guid (node) != NULL)
		{
			g_free (target);
			target = g_strdup (ensure_binding (edit, node));
		}
	}
	sec = lrg_sequence_section_new (MAX (snap_time (self, (gfloat) v), 0.0f), target);
	if (opt_number (params, "duration", &v))
		sec->duration = MAX ((gfloat) v, 0.0f);
	if (opt_number (params, "offset", &v))
		sec->offset = (gfloat) v;
	if (opt_number (params, "rate", &v))
		sec->rate = (gfloat) v;
	if (opt_number (params, "weight", &v) || opt_number (params, "volume", &v))
		sec->weight = (gfloat) v;
	if (json_object_has_member (params, "loop"))
		sec->loop = json_object_get_boolean_member (params, "loop");
	if (opt_string (params, "payload") != NULL)
		sec->payload = g_strdup (opt_string (params, "payload"));
	index = lrg_sequence_track_add_section (t, sec);
	commit_take (self, edit, "Add Section");
	self->sel_track = track;
	self->sel_section = (gint) index;
	sync_detail (self);
	o = json_object_new ();
	json_object_set_int_member (o, "index", index);
	return obj_node (o);
}

static JsonNode *
m_remove (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel *self = ipc_document (user_data, params, error);
	const gchar             *binding = opt_string (params, "binding");
	LrgSequence             *edit;
	JsonObject              *o;
	gboolean                 removed = FALSE;
	gdouble                  v;
	gint                     track;

	if (self == NULL)
		return NULL;
	edit = working_copy (self);
	if (binding != NULL && !json_object_has_member (params, "track"))
	{
		LrgNode *node = lrg_editor_find_node (self->editor, binding);

		removed = lrg_sequence_remove_binding (edit, node != NULL && lrg_node_get_guid (node) != NULL
		                                             ? lrg_node_get_guid (node) : binding);
	}
	else
	{
		if ((track = ipc_track (self, params, error)) < 0)
		{
			g_object_unref (edit);
			return NULL;
		}
		if (opt_number (params, "section", &v))
		{
			LrgSequenceTrack *t = lrg_sequence_get_track (edit, (guint) track);

			removed = v >= 0 && v < lrg_sequence_track_get_n_sections (t) &&
			          lrg_sequence_track_remove_section (t, (guint) v);
		}
		else if (json_object_has_member (params, "key") || json_object_has_member (params, "at"))
		{
			gint              channel = CH_CURVE, k;
			LrgSequenceCurve *curve = ipc_curve (self, edit, params, track, FALSE, &channel, error);

			if (curve == NULL || (k = ipc_key_index (curve, params, error)) < 0)
			{
				g_object_unref (edit);
				return NULL;
			}
			removed = lrg_sequence_curve_remove_key (curve, (guint) k);
		}
		else if (opt_string (params, "channel") != NULL)
		{
			LrgSequenceTransformChannel ch;

			if (!lrg_sequence_transform_channel_from_string (opt_string (params, "channel"), &ch))
			{
				g_object_unref (edit);
				IPC_FAIL (INVALID_PARAMS, "unknown channel");
			}
			removed = lrg_sequence_track_remove_channel_curve (lrg_sequence_get_track (edit, (guint) track), ch);
		}
		else
			removed = lrg_sequence_remove_track (edit, (guint) track);
	}
	if (removed)
		commit_take (self, edit, "Remove");
	else
		g_object_unref (edit);
	self->curve_key = -1;
	self->view_dirty = TRUE;
	o = json_object_new ();
	json_object_set_boolean_member (o, "removed", removed);
	return obj_node (o);
}

static JsonNode *
m_bind (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel *self = ipc_document (user_data, params, error);
	const gchar             *ref = opt_string (params, "node");
	const gchar             *name = opt_string (params, "name");
	LrgSequence             *edit;
	LrgNode                 *node;
	g_autofree gchar        *guid = NULL;
	gdouble                  v;
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	if (ref == NULL)
		IPC_FAIL (INVALID_PARAMS, "node (GUID, name path or override name such as \"self\") is required");
	node = lrg_editor_find_node (self->editor, ref);
	guid = g_strdup (node != NULL && lrg_node_get_guid (node) != NULL ? lrg_node_get_guid (node) : ref);
	if (node == NULL && opt_string (params, "name") == NULL && strchr (ref, '/') != NULL)
		IPC_FAIL (NOT_FOUND, "No node %s", ref);
	edit = working_copy (self);
	lrg_sequence_remove_binding (edit, guid);
	lrg_sequence_add_binding (edit, guid, name != NULL ? name : (node != NULL ? lrg_node_get_name (node) : ref));
	if (opt_number (params, "track", &v))
	{
		if (v < 0 || v >= lrg_sequence_get_n_tracks (edit))
		{
			g_object_unref (edit);
			IPC_FAIL (NOT_FOUND, "No track %g", v);
		}
		lrg_sequence_track_set_binding (lrg_sequence_get_track (edit, (guint) v), guid);
	}
	commit_take (self, edit, "Bind");
	o = json_object_new ();
	json_object_set_string_member (o, "guid", guid);
	json_object_set_string_member (o, "name", lrg_sequence_get_binding_name (self->seq, guid));
	return obj_node (o);
}

static JsonObject *
transport_json (LrgEditorSequencerPanel *self)
{
	JsonObject       *o = json_object_new ();
	g_autofree gchar *mode = enum_nick (LRG_TYPE_SEQUENCE_PLAY_MODE, self->mode);
	gfloat            t = lrg_sequence_player_get_time (self->player);

	json_object_set_double_member (o, "time", t);
	json_object_set_int_member (o, "frame", lrg_sequence_time_to_frame (self->seq, t));
	json_object_set_boolean_member (o, "playing", self->playing);
	json_object_set_string_member (o, "mode", mode);
	if (self->cut_camera != NULL)
		json_object_set_string_member (o, "camera", self->cut_camera);
	else
		json_object_set_null_member (o, "camera");
	json_object_set_array_member (o, "events", json_array_ref (self->fired));
	return o;
}

static JsonNode *
m_play (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel *self = ipc_document (user_data, params, error);
	const gchar             *mode = opt_string (params, "mode");

	if (self == NULL)
		return NULL;
	if (mode != NULL)
	{
		gint m = enum_from_nick (LRG_TYPE_SEQUENCE_PLAY_MODE, mode);

		if (m < 0)
			IPC_FAIL (INVALID_PARAMS, "mode must be once, loop or ping-pong");
		lrg_editor_sequencer_panel_set_preview_mode (self, (LrgSequencePlayMode) m);
	}
	if (json_object_has_member (params, "pilot"))
		lrg_editor_sequencer_panel_set_pilot (self, json_object_get_boolean_member (params, "pilot"));
	if (json_object_has_member (params, "stop") && json_object_get_boolean_member (params, "stop"))
		lrg_editor_sequencer_panel_stop (self);
	else
		lrg_editor_sequencer_panel_set_playing (self, !json_object_has_member (params, "playing") ||
		                                              json_object_get_boolean_member (params, "playing"));
	return obj_node (transport_json (self));
}

static JsonNode *
m_scrub (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel *self = ipc_document (user_data, params, error);
	gdouble                  v;

	if (self == NULL)
		return NULL;
	if (opt_number (params, "frame", &v))
		v = lrg_sequence_frame_to_time (self->seq, (gint) v);
	else if (!opt_number (params, "time", &v))
		IPC_FAIL (INVALID_PARAMS, "time or frame is required");
	lrg_editor_sequencer_panel_set_playing (self, FALSE);
	lrg_editor_sequencer_panel_scrub (self, (gfloat) v);
	return obj_node (transport_json (self));
}

static JsonNode *
m_save (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel *self = ipc_panel (user_data, error);
	g_autoptr(GError)        local = NULL;
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	/* "path" names the document (it must be the open one); "save_as" writes elsewhere */
	if (opt_string (params, "path") != NULL && (self = ipc_document (user_data, params, error)) == NULL)
		return NULL;
	if (!lrg_editor_sequencer_panel_save (self, opt_string (params, "save_as"), &local))
		IPC_FAIL (FAILED, "%s", local->message);
	o = json_object_new ();
	json_object_set_string_member (o, "path", self->path);
	return obj_node (o);
}

static JsonNode *
m_undo (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorSequencerPanel *self = ipc_panel (user_data, error);
	gboolean                 redo = params != NULL && json_object_has_member (params, "redo") &&
	                                json_object_get_boolean_member (params, "redo");
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_boolean_member (o, "ok", redo ? lrg_editor_sequencer_panel_redo (self)
	                                              : lrg_editor_sequencer_panel_undo (self));
	json_object_set_boolean_member (o, "dirty", self->dirty);
	return obj_node (o);
}

static JsonNode *
m_from_timeline (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	const gchar      *timeline = opt_string (params, "timeline");
	const gchar      *path = opt_string (params, "path");
	g_autoptr(GError) local = NULL;
	g_autofree gchar *written = NULL;
	gdouble           fps = 30.0;
	JsonObject       *o;

	if (timeline == NULL || path == NULL)
		IPC_FAIL (INVALID_PARAMS, "timeline and path are required");
	opt_number (params, "fps", &fps);
	written = lrg_editor_sequencer_panel_convert_timeline (LRG_EDITOR_APP (user_data), timeline, path, (gfloat) fps,
	                                                       &local);
	if (written == NULL)
	{
		if (g_error_matches (local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			IPC_FAIL (NOT_FOUND, "%s", local->message);
		IPC_FAIL (FAILED, "%s", local->message);
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", written);
	return obj_node (o);
}

/* ==========================================================================
 * Actions
 * ========================================================================== */

static void
a_show (gpointer data)
{
	LrgEditorSequencerPanel *self = LRG_EDITOR_SEQUENCER_PANEL (data);

	if (self->app != NULL)
		editor_app_show_panel (self->app, LRG_EDITOR_SEQUENCER_PANEL_ID);
}

static void a_new (gpointer d)    { on_new (NULL, d); }
static void a_save (gpointer d)   { do_save (LRG_EDITOR_SEQUENCER_PANEL (d)); }
static void a_undo (gpointer d)   { lrg_editor_sequencer_panel_undo (LRG_EDITOR_SEQUENCER_PANEL (d)); }
static void a_redo (gpointer d)   { lrg_editor_sequencer_panel_redo (LRG_EDITOR_SEQUENCER_PANEL (d)); }
static void a_play (gpointer d)   { on_play (NULL, d); }
static void a_stop (gpointer d)   { lrg_editor_sequencer_panel_stop (LRG_EDITOR_SEQUENCER_PANEL (d)); }
static void a_key (gpointer d)    { on_key (NULL, d); }
static void a_bind (gpointer d)   { bind_selection (LRG_EDITOR_SEQUENCER_PANEL (d)); }
static void a_delete (gpointer d) { delete_selection (LRG_EDITOR_SEQUENCER_PANEL (d)); }

static gboolean
can_undo (gpointer data)
{
	return LRG_EDITOR_SEQUENCER_PANEL (data)->undo->len > 0;
}

static gboolean
can_redo (gpointer data)
{
	return LRG_EDITOR_SEQUENCER_PANEL (data)->redo->len > 0;
}

static gboolean
is_playing (gpointer data)
{
	return LRG_EDITOR_SEQUENCER_PANEL (data)->playing;
}

static gboolean
has_track (gpointer data)
{
	return sel_track (LRG_EDITOR_SEQUENCER_PANEL (data)) != NULL;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorSequencerPanel *
lrg_editor_sequencer_panel_new (LrgEditorApp *app)
{
	static const struct { const gchar *name; LrgEditorServerMethod method; } methods[] = {
		{ "sequence.list", m_list },
		{ "sequence.open", m_open },
		{ "sequence.create", m_create },
		{ "sequence.get", m_get },
		{ "sequence.set", m_set },
		{ "sequence.add_track", m_add_track },
		{ "sequence.add_key", m_add_key },
		{ "sequence.set_key", m_set_key },
		{ "sequence.add_section", m_add_section },
		{ "sequence.remove", m_remove },
		{ "sequence.bind", m_bind },
		{ "sequence.play", m_play },
		{ "sequence.scrub", m_scrub },
		{ "sequence.save", m_save },
		{ "sequence.undo", m_undo },
		{ "sequence.from_timeline", m_from_timeline },
	};
	LrgEditorSequencerPanel *self;
	LrgEditorActions        *actions;
	guint                    i;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_SEQUENCER_PANEL, NULL);
	lrg_widget_set_name (LRG_WIDGET (self), "sequencer-panel");
	g_set_weak_pointer (&self->app, app);
	self->editor = g_object_ref (lrg_editor_app_get_editor (app));
	self->view_state = g_object_ref (lrg_editor_app_get_view_state (app));
	g_object_set_data (G_OBJECT (app), PANEL_KEY, self);
	g_signal_connect (self->editor, "level-replaced", G_CALLBACK (on_level_replaced), self);
	g_signal_connect (self->view_state, "notify::timeline-previewing", G_CALLBACK (on_previewing_changed), self);

	/* Actions and IPC from this file */
	actions = lrg_editor_app_get_actions (app);
	lrg_editor_actions_add (actions, "sequence.show", "Sequencer Panel", "View", NULL, a_show, NULL, NULL, self);
	lrg_editor_actions_add (actions, "sequence.new", "New Sequence", "Animation", NULL, a_new, NULL, NULL, self);
	lrg_editor_actions_add (actions, "sequence.save", "Save Sequence", "Animation", NULL, a_save, NULL, NULL, self);
	lrg_editor_actions_add (actions, "sequence.undo", "Undo Sequence Edit", "Animation", NULL, a_undo, can_undo, NULL,
	                        self);
	lrg_editor_actions_add (actions, "sequence.redo", "Redo Sequence Edit", "Animation", NULL, a_redo, can_redo, NULL,
	                        self);
	lrg_editor_actions_add (actions, "sequence.play", "Play/Pause Sequence", "Animation", NULL, a_play, NULL,
	                        is_playing, self);
	lrg_editor_actions_add (actions, "sequence.stop", "Stop Sequence", "Animation", NULL, a_stop, NULL, NULL, self);
	lrg_editor_actions_add (actions, "sequence.key", "Key Sequence Track", "Animation", NULL, a_key, has_track, NULL,
	                        self);
	lrg_editor_actions_add (actions, "sequence.bind", "Bind Selection to Sequence", "Animation", NULL, a_bind, NULL,
	                        NULL, self);
	lrg_editor_actions_add (actions, "sequence.delete", "Delete in Sequence", "Animation", NULL, a_delete, NULL, NULL,
	                        self);
	if (app->server != NULL)
		for (i = 0; i < G_N_ELEMENTS (methods); i++)
			lrg_editor_server_register_method (app->server, methods[i].name, methods[i].method, app, NULL);

	rebuild (self);
	return self;
}

LrgEditorSequencerPanel *
lrg_editor_sequencer_panel_find (LrgEditorApp *app)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	return g_object_get_data (G_OBJECT (app), PANEL_KEY);
}

gboolean
lrg_editor_sequencer_panel_open_file (LrgEditorApp  *app,
                                      const gchar   *path,
                                      GError       **error)
{
	LrgEditorSequencerPanel *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	self = lrg_editor_sequencer_panel_find (app);
	if (self == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "The editor has no sequencer panel");
		return FALSE;
	}
	if (!lrg_editor_sequencer_panel_load (self, path, error))
		return FALSE;
	editor_app_show_panel (app, LRG_EDITOR_SEQUENCER_PANEL_ID);
	return TRUE;
}

gchar *
lrg_editor_sequencer_panel_convert_timeline (LrgEditorApp  *app,
                                             const gchar   *timeline,
                                             const gchar   *path,
                                             gfloat         fps,
                                             GError       **error)
{
	LrgLevel              *level;
	LrgTimeline           *tl;
	g_autoptr(LrgSequence) seq = NULL;
	g_autofree gchar      *abs = NULL;
	g_autofree gchar      *dir = NULL;
	LrgEditorSequencerPanel *self;
	gint                   idx;
	guint                  i;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (timeline != NULL && path != NULL, NULL);

	level = lrg_editor_get_level (lrg_editor_app_get_editor (app));
	idx = lrg_level_find_timeline (level, timeline);
	if (idx < 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No timeline \"%s\" in the level", timeline);
		return NULL;
	}
	tl = lrg_level_get_timeline (level, (guint) idx);
	seq = lrg_sequence_new_from_timeline (tl, fps > 0.0f ? fps : 30.0f);

	/* bindings get the node names (the converter only knows GUIDs) */
	for (i = 0; i < lrg_sequence_get_n_tracks (seq); i++)
	{
		const gchar *b = lrg_sequence_track_get_binding (lrg_sequence_get_track (seq, i));
		LrgNode     *node = lrg_editor_find_node (lrg_editor_app_get_editor (app), b);

		if (b == NULL)
			continue;
		lrg_sequence_remove_binding (seq, b);
		lrg_sequence_add_binding (seq, b, node != NULL ? lrg_node_get_name (node) : b);
		if (lrg_sequence_track_get_name (lrg_sequence_get_track (seq, i)) == NULL && node != NULL)
			lrg_sequence_track_set_name (lrg_sequence_get_track (seq, i), lrg_node_get_name (node));
	}
	if (lrg_sequence_get_name (seq) == NULL)
		lrg_sequence_set_name (seq, timeline);

	abs = editor_app_project_path (app, path);
	dir = g_path_get_dirname (abs);
	g_mkdir_with_parents (dir, 0755);
	if (!lrg_sequence_save_to_file (seq, abs, error))
		return NULL;
	self = lrg_editor_sequencer_panel_find (app);
	if (self != NULL)
	{
		lrg_editor_sequencer_panel_load (self, abs, NULL);
		editor_app_show_panel (app, LRG_EDITOR_SEQUENCER_PANEL_ID);
	}
	return g_steal_pointer (&abs);
}

LrgSequence *
lrg_editor_sequencer_panel_get_sequence (LrgEditorSequencerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), NULL);
	return self->seq;
}

const gchar *
lrg_editor_sequencer_panel_get_path (LrgEditorSequencerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), NULL);
	return self->path;
}

gboolean
lrg_editor_sequencer_panel_get_dirty (LrgEditorSequencerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), FALSE);
	return self->dirty;
}

LrgTimelineView *
lrg_editor_sequencer_panel_get_view (LrgEditorSequencerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), NULL);
	return self->view;
}

LrgSequencePlayer *
lrg_editor_sequencer_panel_get_player (LrgEditorSequencerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), NULL);
	return self->player;
}

LrgWidget *
lrg_editor_sequencer_panel_get_curve_editor (LrgEditorSequencerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), NULL);
	return LRG_WIDGET (self->curve);
}

LrgTimelineViewTrack *
lrg_editor_sequencer_panel_find_track (LrgEditorSequencerPanel *self,
                                       guint                    track,
                                       const gchar             *channel)
{
	g_autofree gchar           *key = NULL;
	LrgSequenceTransformChannel ch;

	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), NULL);

	flush_ops (self);
	if (self->view_dirty)
		rebuild (self);
	if (channel == NULL)
		key = row_key_track (track);
	else if (lrg_sequence_transform_channel_from_string (channel, &ch))
		key = row_key_channel (track, (gint) ch);
	else
		return NULL;
	return g_hash_table_lookup (self->rows, key);
}

guint
lrg_editor_sequencer_panel_find_keyframe (LrgEditorSequencerPanel *self,
                                          guint                    track,
                                          const gchar             *channel,
                                          gfloat                   time)
{
	LrgSequenceTransformChannel ch;
	gint                        c;

	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), LRG_TIMELINE_VIEW_INVALID_ID);

	flush_ops (self);
	if (self->view_dirty)
		rebuild (self);
	if (channel != NULL)
	{
		if (!lrg_sequence_transform_channel_from_string (channel, &ch))
			return LRG_TIMELINE_VIEW_INVALID_ID;
		return find_key_id (self, track, (gint) ch, time);
	}
	c = (track < lrg_sequence_get_n_tracks (self->seq) &&
	     lrg_sequence_track_get_kind (lrg_sequence_get_track (self->seq, track)) == LRG_SEQUENCE_TRACK_EVENT)
	    ? CH_EVENT : CH_CURVE;
	return find_key_id (self, track, c, time);
}

guint
lrg_editor_sequencer_panel_find_clip (LrgEditorSequencerPanel *self,
                                      guint                    track,
                                      guint                    section)
{
	LrgSequenceTrack *t;

	g_return_val_if_fail (LRG_IS_EDITOR_SEQUENCER_PANEL (self), LRG_TIMELINE_VIEW_INVALID_ID);

	flush_ops (self);
	if (self->view_dirty)
		rebuild (self);
	if (track >= lrg_sequence_get_n_tracks (self->seq))
		return LRG_TIMELINE_VIEW_INVALID_ID;
	t = lrg_sequence_get_track (self->seq, track);
	if (section >= lrg_sequence_track_get_n_sections (t))
		return LRG_TIMELINE_VIEW_INVALID_ID;
	return find_clip_id (self, track, lrg_sequence_track_get_section (t, section)->start);
}
