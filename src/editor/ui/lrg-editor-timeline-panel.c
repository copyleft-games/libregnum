/* lrg-editor-timeline-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The Timeline panel.
 *
 * Model -> view: the current timeline (the view state's timeline-name,
 * resolved by editor_timeline_current()) is rebuilt into the
 * LrgTimelineView whenever the editor changes or another timeline is
 * picked: one group track per node (in the order the timeline first
 * mentions it), one child track per channel, one view keyframe per key
 * (diamonds for linear keys, circles for eased ones). A table maps view
 * keyframe ids back to (node GUID, channel, time, value). The view's
 * selection and the groups' collapsed state survive rebuilds.
 *
 * View -> model: the view edits its own copy first (a key drag moves
 * the diamonds, Delete removes them) and reports the result with
 * keyframe-moved / -removed / -added. Those are queued and applied on
 * the next step as ONE undo group through lrg_editor_timeline_*, so a
 * drag of several keys is one undo step, and the rebuild that follows
 * the editor's "changed" never runs inside the view's own signal
 * emission. Times are snapped to 10 ms.
 *
 * Playhead: the view state's timeline time is the truth. Scrubbing the
 * ruler (playhead-moved) previews through editor_timeline_seek(); the
 * view's own playback is never used (the transport drives the editor's
 * preview playback, ticked by the app), so the panel, IPC timeline.*
 * and the actions always agree.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-timeline-panel.h"
#include "lrg-editor-console-panel.h"
#include "lrg-editor-sequencer-panel.h"
#include "lrg-editor-app-private.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-combo-box.h"
#include "../../ui/lrg-spin-box.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-separator.h"
#include <math.h>
#include <string.h>

/* Key times are snapped to 10 ms (as the 0.8.0 panel did) */
#define SNAP_STEP     (0.01f)
#define TIME_EPSILON  (0.001f)

enum
{
	PROP_0,
	PROP_LOOK,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* A view keyframe and the model key it stands for */
typedef struct
{
	gchar                  *guid;
	gchar                  *channel;
	gfloat                  time;
	gfloat                  value;
	LrgEasingType           easing;
	gboolean                bezier;   /* a Bézier key (lrg_timeline_track_set_key_tangents) */
	LrgSequenceTangentMode  mode;     /* its tangent mode when @bezier */
	gfloat                  in_slope; /* resolved slopes (what the handles show) */
	gfloat                  out_slope;
} KeyRef;

static void
key_ref_free (gpointer data)
{
	KeyRef *k = data;

	g_free (k->guid);
	g_free (k->channel);
	g_free (k);
}

/* A queued edit from the view */
typedef enum
{
	OP_MOVE,
	OP_REMOVE,
	OP_ADD
} OpKind;

typedef struct
{
	OpKind  kind;
	gchar  *guid;
	gchar  *channel;
	gfloat  from;
	gfloat  to;
	gfloat  value;
	gboolean value_changed;
} Op;

static void
op_free (gpointer data)
{
	Op *op = data;

	g_free (op->guid);
	g_free (op->channel);
	g_free (op);
}

struct _LrgEditorTimelinePanel
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;         /* weak */
	LrgEditor            *editor;      /* ref */
	LrgEditorViewState   *view_state;  /* ref */

	/* Widgets */
	LrgWidget            *top;         /* vbox of the two toolbars */
	LrgComboBox          *picker;
	LrgTextInput         *name;
	LrgWidget            *delete_tl;
	LrgWidget            *loop;
	LrgWidget            *autoplay;
	LrgSpinBox           *length;
	LrgWidget            *play;
	LrgComboBox          *easing;
	LrgComboBox          *tangent;     /* Eased, Auto, User, Broken, Linear, Constant */
	LrgWidget            *curves;      /* curve lanes on the channel rows */
	LrgTextInput         *bone;        /* bone name for Key Bone */
	LrgWidget            *time_label;
	LrgTimelineView      *view;
	LrgWidget            *hint;

	/* Model <-> view */
	GHashTable           *keys;        /* view keyframe id -> KeyRef* */
	GHashTable           *tracks;      /* "guid\nchannel" (or "guid") -> LrgTimelineViewTrack* */
	GHashTable           *collapsed;   /* guid -> GINT_TO_POINTER (collapsed) */
	GPtrArray            *ops;         /* Op*, applied on the next step */
	gchar                *shown_name;  /* timeline the view was built from */
	gchar                *picker_sig;  /* timeline names the picker lists */
	gboolean              dirty;
	gboolean              syncing;     /* writing the view from code */
	gboolean              fit_pending; /* zoom the view to the timeline's length */
	LrgWidget            *row1;
	LrgWidget            *row2;
	LrgCanvasLook         look;
};

G_DEFINE_FINAL_TYPE (LrgEditorTimelinePanel, lrg_editor_timeline_panel, LRG_TYPE_CONTAINER)

static gchar *
track_key (const gchar *guid,
           const gchar *channel)
{
	return channel != NULL ? g_strconcat (guid, "\n", channel, NULL) : g_strdup (guid);
}

static gfloat
snap_time (gfloat t)
{
	return roundf (t / SNAP_STEP) * SNAP_STEP;
}

/* Colours of node groups: a fixed, theme-independent wheel */
static void
group_color (guint     index,
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

/* "bone.head.rotation.y" -> "head  rotation.y"; other channels as is. */
static gchar *
channel_title (const gchar *channel)
{
	g_autofree gchar           *bone = NULL;
	LrgSequenceTransformChannel ch;

	if (lrg_timeline_channel_get_bone (channel, &bone, &ch))
		return g_strdup_printf ("%s  %s", bone, lrg_sequence_transform_channel_to_string (ch));
	return g_strdup (channel);
}

/*
 * resolve_track:
 *
 * The slopes a level track's keys evaluate with, for drawing Bézier
 * handles: the keys copied into an #LrgSequenceCurve (eased keys as
 * linear keys, which is what a Bézier neighbour sees of them: the
 * straight secant), so lrg_sequence_curve_get_slopes() resolves auto
 * and linear tangents with the engine's own rules.
 */
static LrgSequenceCurve *
resolve_track (LrgTimelineTrack *track)
{
	LrgSequenceCurve *curve = lrg_sequence_curve_new ();
	guint             k;

	for (k = 0; k < lrg_timeline_track_get_n_keys (track); k++)
	{
		LrgSequenceKey key;

		memset (&key, 0, sizeof key);
		lrg_timeline_track_get_key (track, k, &key.time, &key.value, NULL);
		if (!lrg_timeline_track_get_key_tangents (track, k, &key.tangent_mode, &key.in_tangent, &key.out_tangent))
			key.tangent_mode = LRG_SEQUENCE_TANGENT_LINEAR;
		lrg_sequence_curve_insert_key (curve, &key);
	}
	return curve;
}

/* How the view draws the segment after a key. */
static LrgTimelineViewInterpolation
key_interpolation (const KeyRef *ref)
{
	if (ref->bezier)
		return ref->mode == LRG_SEQUENCE_TANGENT_CONSTANT ? LRG_TIMELINE_VIEW_INTERPOLATION_STEP
		                                                  : LRG_TIMELINE_VIEW_INTERPOLATION_BEZIER;
	return ref->easing == LRG_EASING_LINEAR ? LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR
	                                        : LRG_TIMELINE_VIEW_INTERPOLATION_EASE;
}

/* Diamonds for linear keys, dots for eased and smooth Bézier keys, bars
 * for constant (stepped) keys; squares are the node summary keys. */
static LrgCanvasKeyShape
key_shape (const KeyRef *ref)
{
	if (ref->bezier)
	{
		switch (ref->mode)
		{
		case LRG_SEQUENCE_TANGENT_LINEAR:
			return LRG_CANVAS_KEY_SHAPE_DIAMOND;
		case LRG_SEQUENCE_TANGENT_CONSTANT:
			return LRG_CANVAS_KEY_SHAPE_BAR;
		default:
			return LRG_CANVAS_KEY_SHAPE_CIRCLE;
		}
	}
	return ref->easing == LRG_EASING_LINEAR ? LRG_CANVAS_KEY_SHAPE_DIAMOND : LRG_CANVAS_KEY_SHAPE_CIRCLE;
}

/* Tangent picker rows: index 0 is "Eased" (not a Bézier key), then the
 * LrgSequenceTangentMode values in enum order. */
static const gchar * const tangent_labels[] = { "Eased", "Auto", "User", "Broken", "Linear", "Constant" };
static const gchar * const tangent_nicks[] = { "eased", "auto", "user", "broken", "linear", "constant" };

static gint
tangent_row (const KeyRef *ref)
{
	return ref->bezier ? (gint) ref->mode + 1 : 0;
}

static LrgTimeline *
current_timeline (LrgEditorTimelinePanel *self)
{
	return self->app != NULL ? editor_timeline_current (self->app) : NULL;
}

/* ==========================================================================
 * Model -> view
 * ========================================================================== */

/* Fills the timeline picker when the set of timelines changed. */
static void
sync_picker (LrgEditorTimelinePanel *self,
             LrgTimeline            *tl)
{
	LrgLevel *level = lrg_editor_get_level (self->editor);
	GString  *sig = g_string_new (NULL);
	guint     i, n = lrg_level_get_n_timelines (level);
	gint      selected = -1;

	for (i = 0; i < n; i++)
	{
		const gchar *name = lrg_timeline_get_name (lrg_level_get_timeline (level, i));

		g_string_append (sig, name);
		g_string_append_c (sig, '\n');
		if (tl != NULL && g_strcmp0 (name, lrg_timeline_get_name (tl)) == 0)
			selected = (gint) i;
	}

	self->syncing = TRUE;
	if (g_strcmp0 (sig->str, self->picker_sig) != 0)
	{
		lrg_combo_box_remove_all (self->picker);
		for (i = 0; i < n; i++)
			lrg_combo_box_append (self->picker, lrg_timeline_get_name (lrg_level_get_timeline (level, i)));
		g_free (self->picker_sig);
		self->picker_sig = g_strdup (sig->str);
	}
	if (lrg_combo_box_get_selected (self->picker) != selected)
		lrg_combo_box_set_selected (self->picker, selected);
	self->syncing = FALSE;
	g_string_free (sig, TRUE);
}

/* Toolbar widgets that mirror the timeline's settings. */
static void
sync_settings (LrgEditorTimelinePanel *self,
               LrgTimeline            *tl)
{
	gboolean has = (tl != NULL);

	self->syncing = TRUE;
	lrg_widget_set_enabled (self->delete_tl, has);
	lrg_widget_set_enabled (self->loop, has);
	lrg_widget_set_enabled (self->autoplay, has);
	lrg_widget_set_enabled (LRG_WIDGET (self->length), has);
	lrg_widget_set_enabled (self->play, has);
	editor_chip_set_checked (self->loop, has && lrg_timeline_get_loop (tl));
	editor_chip_set_checked (self->autoplay, has && lrg_timeline_get_autoplay (tl));
	if (has && fabs (lrg_spin_box_get_value (self->length) - lrg_timeline_get_length (tl)) > 1e-4)
		lrg_spin_box_set_value (self->length, lrg_timeline_get_length (tl));
	self->syncing = FALSE;

	lrg_widget_set_visible (self->hint, !has);
	lrg_widget_set_visible (LRG_WIDGET (self->view), has);
}

/* Remembers the selection as model keys so a rebuild can restore it. */
static GPtrArray *
save_selection (LrgEditorTimelinePanel *self)
{
	g_autoptr(GArray) ids = lrg_timeline_view_get_selected_keyframes (self->view);
	GPtrArray        *out = g_ptr_array_new_with_free_func (key_ref_free);
	guint             i;

	for (i = 0; ids != NULL && i < ids->len; i++)
	{
		KeyRef *k = g_hash_table_lookup (self->keys, GUINT_TO_POINTER (g_array_index (ids, guint, i)));
		KeyRef *copy;

		if (k == NULL)
			continue;
		copy = g_new0 (KeyRef, 1);
		copy->guid = g_strdup (k->guid);
		copy->channel = g_strdup (k->channel);
		copy->time = k->time;
		g_ptr_array_add (out, copy);
	}
	return out;
}

static guint
find_key_id (LrgEditorTimelinePanel *self,
             const gchar            *guid,
             const gchar            *channel,
             gfloat                  time)
{
	GHashTableIter iter;
	gpointer       id, value;

	g_hash_table_iter_init (&iter, self->keys);
	while (g_hash_table_iter_next (&iter, &id, &value))
	{
		KeyRef *k = value;

		if (g_strcmp0 (k->guid, guid) == 0 && g_strcmp0 (k->channel, channel) == 0 &&
		    fabsf (k->time - time) < TIME_EPSILON)
			return GPOINTER_TO_UINT (id);
	}
	return LRG_EDITOR_TIMELINE_PANEL_INVALID_KEY;
}

/* Copies the view's first selected key into the app's "selected key"
 * (what timeline.delete-key and the 0.8.0 easing picker act on). */
static void
publish_selected_key (LrgEditorTimelinePanel *self)
{
	g_autoptr(GArray) ids = lrg_timeline_view_get_selected_keyframes (self->view);
	KeyRef           *k = NULL;
	LrgEditorApp     *app = self->app;
	guint             i;

	if (app == NULL)
		return;
	for (i = 0; ids != NULL && i < ids->len && k == NULL; i++)
	{
		KeyRef *cand = g_hash_table_lookup (self->keys, GUINT_TO_POINTER (g_array_index (ids, guint, i)));

		/* The app's single key is a channel key (summaries have none) */
		if (cand != NULL && cand->channel != NULL)
			k = cand;
	}
	g_free (app->timeline_key_guid);
	g_free (app->timeline_key_channel);
	app->timeline_key_guid = k != NULL ? g_strdup (k->guid) : NULL;
	app->timeline_key_channel = k != NULL ? g_strdup (k->channel) : NULL;
	app->timeline_key_time = k != NULL ? k->time : 0.0f;
	app->timeline_key_valid = (k != NULL);

	/* The tangent picker shows the first selected key's mode */
	if (k != NULL && self->tangent != NULL && lrg_combo_box_get_selected (self->tangent) != tangent_row (k))
	{
		gboolean was = self->syncing;

		self->syncing = TRUE;
		lrg_combo_box_set_selected (self->tangent, tangent_row (k));
		self->syncing = was;
	}
}

/* Rebuilds every track and key of the view from the current timeline. */
static void
rebuild (LrgEditorTimelinePanel *self)
{
	LrgTimeline         *tl = current_timeline (self);
	g_autoptr(GPtrArray) selection = save_selection (self);
	g_autoptr(GPtrArray) groups = g_ptr_array_new_with_free_func (g_free);
	guint                i, k, g;

	self->dirty = FALSE;
	self->syncing = TRUE;

	/* Remember which groups the user collapsed */
	{
		GHashTableIter iter;
		gpointer       key, value;

		g_hash_table_iter_init (&iter, self->tracks);
		while (g_hash_table_iter_next (&iter, &key, &value))
			if (strchr (key, '\n') == NULL)
				g_hash_table_replace (self->collapsed, g_strdup (key),
				                      GINT_TO_POINTER (lrg_timeline_view_track_get_collapsed (value)));
	}

	/* Empty the view */
	while (lrg_timeline_view_get_n_tracks (self->view) > 0)
		lrg_timeline_view_remove_track (self->view,
		                                lrg_timeline_view_get_track (self->view,
		                                                             lrg_timeline_view_get_n_tracks (self->view) - 1));
	g_hash_table_remove_all (self->keys);
	g_hash_table_remove_all (self->tracks);
	if (g_strcmp0 (self->shown_name, tl != NULL ? lrg_timeline_get_name (tl) : NULL) != 0)
		self->fit_pending = TRUE;
	g_free (self->shown_name);
	self->shown_name = tl != NULL ? g_strdup (lrg_timeline_get_name (tl)) : NULL;

	sync_picker (self, tl);
	self->syncing = TRUE;
	sync_settings (self, tl);
	self->syncing = TRUE;
	if (tl == NULL)
	{
		self->syncing = FALSE;
		publish_selected_key (self);
		return;
	}

	lrg_timeline_view_set_duration (self->view, MAX (lrg_timeline_get_length (tl), 0.01f));

	/* Node groups, in the order the timeline first mentions them */
	for (i = 0; i < lrg_timeline_get_n_tracks (tl); i++)
	{
		const gchar *guid = lrg_timeline_track_get_node_guid (lrg_timeline_get_track (tl, i));
		gboolean     seen = FALSE;

		for (g = 0; g < groups->len && !seen; g++)
			seen = g_strcmp0 (g_ptr_array_index (groups, g), guid) == 0;
		if (!seen)
			g_ptr_array_add (groups, g_strdup (guid));
	}

	for (g = 0; g < groups->len; g++)
	{
		const gchar          *guid = g_ptr_array_index (groups, g);
		LrgNode              *node = lrg_editor_find_node (self->editor, guid);
		g_autofree gchar     *title = NULL;
		LrgTimelineViewTrack *group;
		GrlColor              color;

		title = node != NULL ? g_strdup (lrg_node_get_name (node)) : g_strdup_printf ("(missing %s)", guid);
		group = lrg_timeline_view_add_track (self->view, title, NULL);
		group_color (g, &color);
		lrg_timeline_view_track_set_color (group, &color);
		lrg_timeline_view_track_set_collapsed (group, GPOINTER_TO_INT (g_hash_table_lookup (self->collapsed, guid)));
		g_hash_table_insert (self->tracks, g_strdup (guid), group);

		for (i = 0; i < lrg_timeline_get_n_tracks (tl); i++)
		{
			LrgTimelineTrack            *t = lrg_timeline_get_track (tl, i);
			const gchar                 *channel = lrg_timeline_track_get_channel (t);
			g_autoptr(LrgSequenceCurve)  resolved = NULL;
			g_autofree gchar            *title = NULL;
			LrgTimelineViewTrack        *row;
			gdouble                      lo = G_MAXDOUBLE, hi = -G_MAXDOUBLE;

			if (g_strcmp0 (lrg_timeline_track_get_node_guid (t), guid) != 0)
				continue;
			title = channel_title (channel);
			row = lrg_timeline_view_add_track (self->view, title, group);
			lrg_timeline_view_track_set_color (row, &color);
			if (self->curves != NULL && editor_chip_get_checked (self->curves))
				lrg_timeline_view_track_set_curve_lane (row, TRUE);
			g_hash_table_insert (self->tracks, track_key (guid, channel), row);
			resolved = resolve_track (t);

			for (k = 0; k < lrg_timeline_track_get_n_keys (t); k++)
			{
				KeyRef *ref = g_new0 (KeyRef, 1);
				guint   id;

				lrg_timeline_track_get_key (t, k, &ref->time, &ref->value, &ref->easing);
				ref->bezier = lrg_timeline_track_get_key_tangents (t, k, &ref->mode, NULL, NULL);
				lrg_sequence_curve_get_slopes (resolved, k, &ref->in_slope, &ref->out_slope);
				ref->guid = g_strdup (guid);
				ref->channel = g_strdup (channel);
				id = lrg_timeline_view_add_keyframe (self->view, row, ref->time, ref->value, key_interpolation (ref));
				if (ref->bezier)
					lrg_timeline_view_set_keyframe_tangents (self->view, id, ref->in_slope, ref->out_slope);
				lrg_timeline_view_set_keyframe_shape (self->view, id, key_shape (ref));
				g_hash_table_insert (self->keys, GUINT_TO_POINTER (id), ref);
				lo = MIN (lo, ref->value);
				hi = MAX (hi, ref->value);
			}
			if (lo <= hi)
				lrg_timeline_view_track_set_value_range (row, lo - MAX (0.5, (hi - lo) * 0.1),
				                                         hi + MAX (0.5, (hi - lo) * 0.1));
		}

		/* Summary keys on the group row: one per distinct key time of the
		 * node (channel NULL); moving or deleting one acts on every
		 * channel key of the node at that time */
		{
			g_autoptr(GArray) times = g_array_new (FALSE, FALSE, sizeof (gfloat));
			GHashTableIter    iter;
			gpointer          value;

			g_hash_table_iter_init (&iter, self->keys);
			while (g_hash_table_iter_next (&iter, NULL, &value))
			{
				KeyRef  *ref = value;
				gboolean seen = FALSE;

				if (ref->channel == NULL || g_strcmp0 (ref->guid, guid) != 0)
					continue;
				for (k = 0; k < times->len && !seen; k++)
					seen = fabsf (g_array_index (times, gfloat, k) - ref->time) < TIME_EPSILON;
				if (!seen)
					g_array_append_val (times, ref->time);
			}
			for (k = 0; k < times->len; k++)
			{
				KeyRef *sum = g_new0 (KeyRef, 1);
				guint   id;

				sum->guid = g_strdup (guid);
				sum->time = g_array_index (times, gfloat, k);
				id = lrg_timeline_view_add_keyframe (self->view, group, sum->time, 0.0,
				                                     LRG_TIMELINE_VIEW_INTERPOLATION_STEP);
				lrg_timeline_view_set_keyframe_shape (self->view, id, LRG_CANVAS_KEY_SHAPE_SQUARE);
				g_hash_table_insert (self->keys, GUINT_TO_POINTER (id), sum);
			}
		}
	}

	/* Restore the selection */
	for (i = 0; i < selection->len; i++)
	{
		KeyRef *k2 = g_ptr_array_index (selection, i);
		guint   id = find_key_id (self, k2->guid, k2->channel, k2->time);

		if (id != LRG_EDITOR_TIMELINE_PANEL_INVALID_KEY)
			lrg_timeline_view_select_keyframe (self->view, id, TRUE);
	}
	lrg_timeline_view_set_time (self->view, lrg_editor_view_state_get_timeline_time (self->view_state));
	self->syncing = FALSE;
	publish_selected_key (self);
}

/* Per frame: playhead, transport state, readout. */
static void
sync_transport (LrgEditorTimelinePanel *self)
{
	gfloat            t = lrg_editor_view_state_get_timeline_time (self->view_state);
	gboolean          playing = lrg_editor_view_state_get_timeline_playing (self->view_state);
	g_autofree gchar *text = NULL;
	LrgTimeline      *tl = current_timeline (self);

	self->syncing = TRUE;
	if (lrg_timeline_view_get_playing (self->view))
		lrg_timeline_view_set_playing (self->view, FALSE);
	if (fabs (lrg_timeline_view_get_time (self->view) - t) > 1e-5)
		lrg_timeline_view_set_time (self->view, t);
	self->syncing = FALSE;

	editor_chip_set_checked (self->play, playing);
	editor_chip_set_icon (self->play, playing ? LRG_ICON_PAUSE : LRG_ICON_PLAY);
	editor_chip_set_label (self->play, playing ? "Pause" : "Play");
	text = g_strdup_printf ("%.2f / %.2f s", t, tl != NULL ? lrg_timeline_get_length (tl) : 0.0f);
	if (g_strcmp0 (lrg_label_get_text (LRG_LABEL (self->time_label)), text) != 0)
		lrg_label_set_text (LRG_LABEL (self->time_label), text);
}

/* ==========================================================================
 * View -> model
 * ========================================================================== */

static void
queue_op (LrgEditorTimelinePanel *self,
          OpKind                  kind,
          KeyRef                 *ref,
          gfloat                  to,
          gfloat                  value)
{
	Op *op = g_new0 (Op, 1);

	op->kind = kind;
	op->guid = g_strdup (ref->guid);
	op->channel = g_strdup (ref->channel);
	op->from = ref->time;
	op->to = to;
	op->value = value;
	op->value_changed = fabsf (value - ref->value) > 1e-6f;
	g_ptr_array_add (self->ops, op);
}

/* Already queued (a key dragged together with its summary)? */
static gboolean
op_queued (LrgEditorTimelinePanel *self,
           OpKind                  kind,
           KeyRef                 *ref)
{
	guint i;

	for (i = 0; i < self->ops->len; i++)
	{
		Op *op = g_ptr_array_index (self->ops, i);

		if (op->kind == kind && g_strcmp0 (op->guid, ref->guid) == 0 && g_strcmp0 (op->channel, ref->channel) == 0 &&
		    fabsf (op->from - ref->time) < TIME_EPSILON)
			return TRUE;
	}
	return FALSE;
}

/*
 * queue_ref:
 *
 * Queues @kind for @ref; a summary key (no channel) stands for every
 * channel key of its node at its time, which move or go together
 * (moved keys keep their own values).
 */
static void
queue_ref (LrgEditorTimelinePanel *self,
           OpKind                  kind,
           KeyRef                 *ref,
           gfloat                  to,
           gfloat                  value)
{
	GHashTableIter iter;
	gpointer       v;

	if (ref->channel != NULL)
	{
		if (!op_queued (self, kind, ref))
			queue_op (self, kind, ref, to, value);
		return;
	}
	g_hash_table_iter_init (&iter, self->keys);
	while (g_hash_table_iter_next (&iter, NULL, &v))
	{
		KeyRef *member = v;

		if (member->channel != NULL && g_strcmp0 (member->guid, ref->guid) == 0 &&
		    fabsf (member->time - ref->time) < TIME_EPSILON && !op_queued (self, kind, member))
			queue_op (self, kind, member, to, member->value);
	}
}

static void
on_keyframe_moved (LrgTimelineView      *view,
                   LrgTimelineViewTrack *track,
                   guint                 id,
                   gpointer              user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);
	KeyRef                 *ref = g_hash_table_lookup (self->keys, GUINT_TO_POINTER (id));

	if (self->syncing || ref == NULL)
		return;
	queue_ref (self, OP_MOVE, ref, snap_time ((gfloat) lrg_timeline_view_get_keyframe_time (view, id)),
	           ref->channel != NULL ? (gfloat) lrg_timeline_view_get_keyframe_value (view, id) : 0.0f);
}

static void
on_keyframe_removed (LrgTimelineView      *view,
                     LrgTimelineViewTrack *track,
                     guint                 id,
                     gpointer              user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);
	KeyRef                 *ref = g_hash_table_lookup (self->keys, GUINT_TO_POINTER (id));

	if (self->syncing || ref == NULL)
		return;
	queue_ref (self, OP_REMOVE, ref, ref->time, ref->value);
}

/* A key added in the view (double click on a curve lane) */
static void
on_keyframe_added (LrgTimelineView      *view,
                   LrgTimelineViewTrack *track,
                   guint                 id,
                   gpointer              user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);
	GHashTableIter          iter;
	gpointer                key, value;

	if (self->syncing)
		return;
	g_hash_table_iter_init (&iter, self->tracks);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		g_auto(GStrv) parts = NULL;
		KeyRef        ref;

		if (value != track || strchr (key, '\n') == NULL)
			continue;
		parts = g_strsplit (key, "\n", 2);
		ref.guid = parts[0];
		ref.channel = parts[1];
		ref.time = snap_time ((gfloat) lrg_timeline_view_get_keyframe_time (view, id));
		ref.value = (gfloat) lrg_timeline_view_get_keyframe_value (view, id);
		queue_op (self, OP_ADD, &ref, ref.time, ref.value);
		return;
	}
}

static void
on_playhead_moved (LrgTimelineView *view,
                   gdouble          time,
                   gpointer         user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);

	if (self->syncing || self->app == NULL)
		return;
	editor_timeline_seek (self->app, (gfloat) time);
}

static void
on_selection_changed (LrgTimelineView *view,
                      gpointer         user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);

	if (!self->syncing)
		publish_selected_key (self);
}

/* Applies the queued view edits as one undo step. */
static void
flush_ops (LrgEditorTimelinePanel *self)
{
	LrgTimeline      *tl = current_timeline (self);
	const gchar      *label;
	g_autofree gchar *name = NULL;
	guint             i;

	if (self->ops->len == 0)
		return;
	if (tl == NULL)
	{
		g_ptr_array_set_size (self->ops, 0);
		return;
	}
	/* A copy: every edit replaces the level's timeline object */
	name = g_strdup (lrg_timeline_get_name (tl));
	label = ((Op *) g_ptr_array_index (self->ops, 0))->kind == OP_MOVE ? "Move Keyframes"
	        : ((Op *) g_ptr_array_index (self->ops, 0))->kind == OP_REMOVE ? "Delete Keyframes" : "Insert Keyframe";

	lrg_editor_begin_group (self->editor, label);
	for (i = 0; i < self->ops->len; i++)
	{
		Op      *op = g_ptr_array_index (self->ops, i);
		LrgNode *node = lrg_editor_find_node (self->editor, op->guid);

		if (node == NULL)
			continue;
		switch (op->kind)
		{
		case OP_MOVE:
			if (fabsf (op->to - op->from) >= SNAP_STEP * 0.5f)
				lrg_editor_timeline_move_key (self->editor, name, node, op->channel, op->from, op->to, FALSE);
			else
				op->to = op->from;
			if (op->value_changed)
			{
				LrgTimelineTrack *t = NULL;
				LrgEasingType     easing = LRG_EASING_LINEAR;
				gint              idx;
				gint              ki;

				tl = current_timeline (self);
				idx = (tl != NULL) ? lrg_timeline_find_track (tl, op->guid, op->channel) : -1;

				if (idx >= 0)
					t = lrg_timeline_get_track (tl, (guint) idx);
				if (t != NULL && (ki = lrg_timeline_track_find_key (t, op->to, TIME_EPSILON)) >= 0)
					lrg_timeline_track_get_key (t, (guint) ki, NULL, NULL, &easing);
				lrg_editor_timeline_set_key (self->editor, name, node, op->channel, op->to, op->value, easing, FALSE);
			}
			break;
		case OP_REMOVE:
			lrg_editor_timeline_remove_key (self->editor, name, node, op->channel, op->from);
			break;
		case OP_ADD:
			lrg_editor_timeline_set_key (self->editor, name, node, op->channel, op->to, op->value,
			                             self->app != NULL ? (LrgEasingType) self->app->timeline_easing
			                                               : LRG_EASING_LINEAR, FALSE);
			break;
		default:
			break;
		}
	}
	lrg_editor_end_group (self->editor);
	g_ptr_array_set_size (self->ops, 0);

	/* The preview pose follows the new keys */
	if (self->app != NULL && lrg_editor_view_state_get_timeline_previewing (self->view_state))
		editor_timeline_seek (self->app, lrg_editor_view_state_get_timeline_time (self->view_state));
	rebuild (self);
}

/* ==========================================================================
 * Toolbar
 * ========================================================================== */

/* A new timeline named after the name field (made unique). */
static void
on_new (LrgWidget *chip,
        gpointer   user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);
	LrgLevel               *level = lrg_editor_get_level (self->editor);
	const gchar            *typed = lrg_text_input_get_text (self->name);
	const gchar            *base = (typed != NULL && *typed != '\0') ? typed : "Timeline";
	g_autofree gchar       *name = g_strdup (base);
	guint                   n = 2;

	if (self->app == NULL)
		return;
	while (lrg_level_find_timeline (level, name) >= 0)
	{
		g_free (name);
		name = g_strdup_printf ("%s %u", base, n++);
	}
	if (lrg_editor_timeline_add (self->editor, name, 2.0f, FALSE))
	{
		editor_timeline_end_preview (self->app);
		lrg_editor_view_state_set_timeline_name (self->view_state, name);
		lrg_editor_view_state_set_timeline_time (self->view_state, 0.0f);
		lrg_text_input_set_text (self->name, "");
		rebuild (self);
	}
}

static void
on_name_submitted (LrgTextInput *input,
                   gpointer      user_data)
{
	on_new (NULL, user_data);
}

static void
timeline_rename (LrgEditorTimelinePanel *self)
{
	LrgTimeline *tl = current_timeline (self);
	const gchar *typed = lrg_text_input_get_text (self->name);

	if (tl == NULL || typed == NULL || *typed == '\0' || self->app == NULL)
		return;
	if (lrg_level_find_timeline (lrg_editor_get_level (self->editor), typed) >= 0)
	{
		editor_app_set_status (self->app, "A timeline named \"%s\" exists", typed);
		return;
	}
	if (lrg_editor_timeline_configure (self->editor, lrg_timeline_get_name (tl), typed, 0.0f, -1, -1))
	{
		lrg_editor_view_state_set_timeline_name (self->view_state, typed);
		lrg_text_input_set_text (self->name, "");
		rebuild (self);
	}
}

static void
timeline_delete (LrgEditorTimelinePanel *self)
{
	LrgTimeline      *tl = current_timeline (self);
	g_autofree gchar *name = NULL;

	if (tl == NULL || self->app == NULL)
		return;
	name = g_strdup (lrg_timeline_get_name (tl));
	editor_timeline_end_preview (self->app);
	lrg_editor_timeline_remove (self->editor, name);
	lrg_editor_view_state_set_timeline_time (self->view_state, 0.0f);
	rebuild (self);
}

static void on_rename (LrgWidget *c, gpointer d)          { timeline_rename (LRG_EDITOR_TIMELINE_PANEL (d)); }
static void on_delete_timeline (LrgWidget *c, gpointer d) { timeline_delete (LRG_EDITOR_TIMELINE_PANEL (d)); }

static void
on_picked (LrgComboBox *combo,
           gpointer     user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);
	const gchar            *name = lrg_combo_box_get_selected_text (combo);

	if (self->syncing || name == NULL || self->app == NULL ||
	    g_strcmp0 (name, lrg_editor_view_state_get_timeline_name (self->view_state)) == 0)
		return;
	editor_timeline_end_preview (self->app);
	lrg_editor_view_state_set_timeline_name (self->view_state, name);
	lrg_editor_view_state_set_timeline_time (self->view_state, 0.0f);
	rebuild (self);
}

static void
on_loop (LrgWidget *chip,
         gpointer   user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);
	LrgTimeline            *tl = current_timeline (self);

	if (tl != NULL)
		lrg_editor_timeline_configure (self->editor, lrg_timeline_get_name (tl), NULL, 0.0f,
		                               !lrg_timeline_get_loop (tl), -1);
	self->dirty = TRUE;
}

static void
on_autoplay (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);
	LrgTimeline            *tl = current_timeline (self);

	if (tl != NULL)
		lrg_editor_timeline_configure (self->editor, lrg_timeline_get_name (tl), NULL, 0.0f, -1,
		                               !lrg_timeline_get_autoplay (tl));
	self->dirty = TRUE;
}

static void
on_length (LrgSpinBox *spin,
           gdouble     value,
           gpointer    user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);
	LrgTimeline            *tl = current_timeline (self);

	if (self->syncing || tl == NULL || value < 0.05 || fabs (value - lrg_timeline_get_length (tl)) < 1e-5)
		return;
	lrg_editor_timeline_configure (self->editor, lrg_timeline_get_name (tl), NULL, (gfloat) value, -1, -1);
	self->fit_pending = TRUE;
}

/* Transport buttons run the registry actions (shortcuts, menus, IPC agree) */
static void
run_action (LrgEditorTimelinePanel *self,
            const gchar            *id)
{
	if (self->app != NULL && lrg_editor_actions_is_enabled (self->app->actions, id))
		lrg_editor_actions_activate (self->app->actions, id);
}

static void on_play (LrgWidget *c, gpointer d)       { run_action (LRG_EDITOR_TIMELINE_PANEL (d), "timeline.play"); }
static void on_stop (LrgWidget *c, gpointer d)       { run_action (LRG_EDITOR_TIMELINE_PANEL (d), "timeline.stop"); }
static void on_prev (LrgWidget *c, gpointer d)       { run_action (LRG_EDITOR_TIMELINE_PANEL (d), "timeline.prev-key"); }
static void on_next (LrgWidget *c, gpointer d)       { run_action (LRG_EDITOR_TIMELINE_PANEL (d), "timeline.next-key"); }
static void on_key (LrgWidget *c, gpointer d)        { run_action (LRG_EDITOR_TIMELINE_PANEL (d), "timeline.key"); }

/* Tangent picker: row 0 makes the selected keys eased again, the others
 * make them Bézier keys of that mode. */
static void
on_tangent (LrgComboBox *combo,
            gpointer     user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);
	gint                    i = lrg_combo_box_get_selected (combo);

	if (self->syncing || i < 0)
		return;
	lrg_editor_timeline_panel_set_key_tangents (self, i > 0, i > 0 ? (LrgSequenceTangentMode) (i - 1)
	                                                                : LRG_SEQUENCE_TANGENT_AUTO);
}

/* The key context menu's "panel.tangent" (string target: a nick). */
static void
on_tangent_action (GSimpleAction *action,
                   GVariant      *parameter,
                   gpointer       user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);
	const gchar            *nick = g_variant_get_string (parameter, NULL);
	guint                   i;

	for (i = 0; i < G_N_ELEMENTS (tangent_nicks); i++)
		if (g_strcmp0 (nick, tangent_nicks[i]) == 0)
			lrg_editor_timeline_panel_set_key_tangents (self, i > 0, i > 0 ? (LrgSequenceTangentMode) (i - 1)
			                                                                : LRG_SEQUENCE_TANGENT_AUTO);
}

/* Curves: channel rows become curve lanes that draw values and handles. */
static void
on_curves (LrgWidget *chip,
           gpointer   user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);

	editor_chip_set_checked (chip, !editor_chip_get_checked (chip));
	rebuild (self);
}

/*
 * key_bone:
 *
 * Keys the nine offset channels of the bone named in the bone field
 * ("bone.NAME.location.x" ...) on every selected node at the playhead,
 * with their current offsets (identity when the bone was never posed),
 * as one undo step. The timeline is created ("Timeline") when the level
 * has none.
 */
static guint
key_bone (LrgEditorTimelinePanel *self,
          const gchar            *bone)
{
	GPtrArray        *nodes = lrg_editor_selection_get_nodes (lrg_editor_get_selection (self->editor));
	LrgTimeline      *tl = current_timeline (self);
	g_autofree gchar *name = g_strdup (tl != NULL ? lrg_timeline_get_name (tl) : "Timeline");
	gchar            *channels[LRG_SEQUENCE_N_CHANNELS + 1];
	guint             i, keys = 0;

	if (self->app == NULL || bone == NULL || *bone == '\0' || nodes->len == 0)
		return 0;
	for (i = 0; i < LRG_SEQUENCE_N_CHANNELS; i++)
		channels[i] = g_strdup_printf ("bone.%s.%s", bone,
		                               lrg_sequence_transform_channel_to_string ((LrgSequenceTransformChannel) i));
	channels[LRG_SEQUENCE_N_CHANNELS] = NULL;
	if (tl == NULL)
		lrg_editor_view_state_set_timeline_name (self->view_state, name);

	lrg_editor_begin_group (self->editor, "Key Bone");
	for (i = 0; i < nodes->len; i++)
		keys += lrg_editor_timeline_key_node (self->editor, name, g_ptr_array_index (nodes, i),
		                                      lrg_editor_view_state_get_timeline_time (self->view_state),
		                                      (const gchar * const *) channels,
		                                      (LrgEasingType) self->app->timeline_easing);
	lrg_editor_end_group (self->editor);
	for (i = 0; i < LRG_SEQUENCE_N_CHANNELS; i++)
		g_free (channels[i]);
	editor_app_set_status (self->app, "Keyed bone %s (%u channel(s))", bone, keys);
	self->dirty = TRUE;
	return keys;
}

static void
on_key_bone (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);

	key_bone (self, lrg_text_input_get_text (self->bone));
}

static void
on_bone_submitted (LrgTextInput *input,
                   gpointer      user_data)
{
	on_key_bone (NULL, user_data);
}

/* Convert to Sequence: the timeline as a .rseq next to the project's
 * sequences (sequences/<name>.rseq), opened in the sequencer. */
static void
to_sequence (LrgEditorTimelinePanel *self)
{
	LrgTimeline       *tl = current_timeline (self);
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *file = NULL;
	g_autofree gchar  *rel = NULL;
	g_autofree gchar  *written = NULL;
	gchar             *p;

	if (tl == NULL || self->app == NULL)
		return;
	file = g_strdup_printf ("%s.rseq", lrg_timeline_get_name (tl));
	for (p = file; *p != '\0'; p++)
		if (*p == '/' || *p == ' ' || *p == '\\')
			*p = '_';
	rel = g_build_filename ("sequences", file, NULL);
	written = lrg_editor_sequencer_panel_convert_timeline (self->app, lrg_timeline_get_name (tl), rel, 30.0f,
	                                                       &error);
	if (written == NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Convert to sequence: %s", error->message);
	else
		editor_app_notify (self->app, EDITOR_NOTIFY_SUCCESS, "Wrote %s", written);
}

static void
on_to_sequence (LrgWidget *chip,
                gpointer   user_data)
{
	to_sequence (LRG_EDITOR_TIMELINE_PANEL (user_data));
}

static void
on_to_sequence_action (GSimpleAction *action,
                       GVariant      *parameter,
                       gpointer       user_data)
{
	to_sequence (LRG_EDITOR_TIMELINE_PANEL (user_data));
}

/* Deletes every selected key (one undo step). */
static void
delete_selected_keys (LrgEditorTimelinePanel *self)
{
	g_autoptr(GArray) ids = lrg_timeline_view_get_selected_keyframes (self->view);
	guint             i;

	for (i = 0; ids != NULL && i < ids->len; i++)
	{
		KeyRef *ref = g_hash_table_lookup (self->keys, GUINT_TO_POINTER (g_array_index (ids, guint, i)));

		if (ref != NULL)
			queue_ref (self, OP_REMOVE, ref, ref->time, ref->value);
	}
	flush_ops (self);
}

static void
on_delete_keys (LrgWidget *chip,
                gpointer   user_data)
{
	delete_selected_keys (LRG_EDITOR_TIMELINE_PANEL (user_data));
}

static void
on_easing (LrgComboBox *combo,
           gpointer     user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);
	g_autoptr(GEnumClass)   klass = g_type_class_ref (LRG_TYPE_EASING_TYPE);
	gint                    i = lrg_combo_box_get_selected (combo);

	if (self->syncing || i < 0 || (guint) i >= klass->n_values)
		return;
	lrg_editor_timeline_panel_set_key_easing (self, (LrgEasingType) klass->values[i].value);
}

/* The key context menu's "panel.easing" (string target: the nick). */
static void
on_easing_action (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (user_data);
	g_autoptr(GEnumClass)   klass = g_type_class_ref (LRG_TYPE_EASING_TYPE);
	GEnumValue             *v = g_enum_get_value_by_nick (klass, g_variant_get_string (parameter, NULL));

	if (v != NULL)
		lrg_editor_timeline_panel_set_key_easing (self, (LrgEasingType) v->value);
}

static void
on_delete_keys_action (GSimpleAction *action,
                       GVariant      *parameter,
                       gpointer       user_data)
{
	delete_selected_keys (LRG_EDITOR_TIMELINE_PANEL (user_data));
}

/* The right-click menu: delete, key the selection and per-key easing. */
static GMenuModel *
build_key_menu (void)
{
	static const gchar *families[] = { "sine", "quad", "cubic", "quart", "quint", "expo", "circ", "back", "elastic",
	                                   "bounce" };
	static const struct { const gchar *title; const gchar *prefix; } kinds[] = {
		{ "Ease In", "ease-in-" }, { "Ease Out", "ease-out-" }, { "Ease In Out", "ease-in-out-" }
	};
	GMenu *menu = g_menu_new ();
	GMenu *edit = g_menu_new ();
	GMenu *easing = g_menu_new ();
	guint  i, j;

	g_menu_append (edit, "Insert Keyframe", "editor.timeline.key");
	g_menu_append (edit, "Delete Selected Keys", "timeline-panel.delete-keys");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (edit));

	g_menu_append (easing, "Linear", "timeline-panel.easing::linear");
	for (i = 0; i < G_N_ELEMENTS (kinds); i++)
	{
		GMenu *sub = g_menu_new ();

		for (j = 0; j < G_N_ELEMENTS (families); j++)
		{
			g_autofree gchar *nick = g_strconcat (kinds[i].prefix, families[j], NULL);
			g_autofree gchar *detailed = g_strdup_printf ("timeline-panel.easing::%s", nick);
			g_autofree gchar *label = g_strdup (families[j]);

			label[0] = g_ascii_toupper (label[0]);
			g_menu_append (sub, label, detailed);
		}
		g_menu_append_submenu (easing, kinds[i].title, G_MENU_MODEL (sub));
		g_object_unref (sub);
	}
	g_menu_append_submenu (menu, "Key Easing", G_MENU_MODEL (easing));
	{
		GMenu *tangents = g_menu_new ();
		GMenu *convert = g_menu_new ();

		for (i = 0; i < G_N_ELEMENTS (tangent_nicks); i++)
		{
			g_autofree gchar *detailed = g_strdup_printf ("timeline-panel.tangent::%s", tangent_nicks[i]);

			g_menu_append (tangents, tangent_labels[i], detailed);
		}
		g_menu_append_submenu (menu, "Key Tangents", G_MENU_MODEL (tangents));
		g_menu_append (convert, "Convert to Sequence", "timeline-panel.to-sequence");
		g_menu_append_section (menu, NULL, G_MENU_MODEL (convert));
		g_object_unref (tangents);
		g_object_unref (convert);
	}
	g_object_unref (edit);
	g_object_unref (easing);
	return G_MENU_MODEL (menu);
}

/* ==========================================================================
 * Widget
 * ========================================================================== */

static gboolean
lrg_editor_timeline_panel_step (LrgWidget *widget,
                                gfloat     delta_time)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (widget);
	LrgTimeline            *tl;

	flush_ops (self);
	tl = current_timeline (self);
	if (self->dirty || g_strcmp0 (tl != NULL ? lrg_timeline_get_name (tl) : NULL, self->shown_name) != 0)
		rebuild (self);
	sync_transport (self);

	/* Zoom a newly shown timeline so its whole length fits */
	if (self->fit_pending && tl != NULL)
	{
		gfloat avail = lrg_widget_get_width (LRG_WIDGET (self->view)) - lrg_timeline_view_get_header_width (self->view)
		               - 40.0f;

		if (avail > 50.0f)
		{
			lrg_timeline_view_set_zoom (self->view, avail / MAX (lrg_timeline_get_length (tl), 0.05f));
			lrg_timeline_view_set_scroll_time (self->view, 0.0, FALSE);
			self->fit_pending = FALSE;
		}
	}
	return FALSE;
}

static void
lrg_editor_timeline_panel_layout_children (LrgContainer *container)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (container);

	editor_panel_fit_toolbar (self->row1, lrg_widget_get_width (LRG_WIDGET (self)) - 16.0f);
	editor_panel_fit_toolbar (self->row2, lrg_widget_get_width (LRG_WIDGET (self)) - 16.0f);
	editor_panel_place_column (container, self->top, LRG_WIDGET (self->view), NULL);
	editor_panel_place_column (container, self->top, self->hint, NULL);
}

static void
on_editor_changed (LrgEditor *editor,
                   gpointer   user_data)
{
	LRG_EDITOR_TIMELINE_PANEL (user_data)->dirty = TRUE;
}

/* ::level-replaced carries the new level before user_data */
static void
on_level_replaced_dirty (LrgEditor *editor,
                         LrgLevel  *level,
                         gpointer   user_data)
{
	LRG_EDITOR_TIMELINE_PANEL (user_data)->dirty = TRUE;
}

static void
lrg_editor_timeline_panel_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (object);

	switch (prop_id)
	{
	case PROP_LOOK:
		g_value_set_enum (value, self->look);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_editor_timeline_panel_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (object);

	switch (prop_id)
	{
	case PROP_LOOK:
		lrg_editor_timeline_panel_set_look (self, g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_editor_timeline_panel_dispose (GObject *object)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (object);

	if (self->editor != NULL)
		g_signal_handlers_disconnect_by_data (self->editor, self);
	g_clear_object (&self->editor);
	g_clear_object (&self->view_state);
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_timeline_panel_parent_class)->dispose (object);
}

static void
lrg_editor_timeline_panel_finalize (GObject *object)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (object);

	g_clear_pointer (&self->keys, g_hash_table_unref);
	g_clear_pointer (&self->tracks, g_hash_table_unref);
	g_clear_pointer (&self->collapsed, g_hash_table_unref);
	g_clear_pointer (&self->ops, g_ptr_array_unref);
	g_free (self->shown_name);
	g_free (self->picker_sig);
	G_OBJECT_CLASS (lrg_editor_timeline_panel_parent_class)->finalize (object);
}

static void
lrg_editor_timeline_panel_class_init (LrgEditorTimelinePanelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = lrg_editor_timeline_panel_get_property;
	object_class->set_property = lrg_editor_timeline_panel_set_property;
	object_class->dispose = lrg_editor_timeline_panel_dispose;
	object_class->finalize = lrg_editor_timeline_panel_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_timeline_panel_step;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_timeline_panel_layout_children;

	/**
	 * LrgEditorTimelinePanel:look:
	 *
	 * The timeline view's visual preset (#LrgTimelineView:look);
	 * %LRG_CANVAS_LOOK_AUTO follows the editor theme.
	 */
	properties[PROP_LOOK] =
		g_param_spec_enum ("look", "Look", "Visual preset of the timeline view", LRG_TYPE_CANVAS_LOOK,
		                   LRG_CANVAS_LOOK_AUTO,
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
toolbar_row (const gchar *name)
{
	LrgWidget *row = LRG_WIDGET (lrg_hbox_new ());

	lrg_widget_set_name (row, name);
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	return row;
}

static void
add_separator (LrgWidget *box)
{
	LrgSeparator   *sep = lrg_separator_new_with_orientation (LRG_ORIENTATION_VERTICAL);
	const LrgInsets ins = { 4.0f, 4.0f, 4.0f, 4.0f };

	lrg_widget_set_margin (LRG_WIDGET (sep), &ins);
	add_to (box, LRG_WIDGET (sep));
}

static void
lrg_editor_timeline_panel_init (LrgEditorTimelinePanel *self)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_EASING_TYPE);
	LrgWidget            *row1, *row2, *chip;
	guint                 i;

	self->keys = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, key_ref_free);
	self->tracks = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->collapsed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->ops = g_ptr_array_new_with_free_func (op_free);
	self->look = LRG_CANVAS_LOOK_AUTO;
	self->dirty = TRUE;

	self->top = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (self->top), 4.0f);

	/* Row 1: picker, name field, New / Rename / Delete | Loop, Autoplay, length */
	row1 = toolbar_row ("timeline-toolbar");
	self->picker = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->picker), "timeline-picker");
	lrg_combo_box_set_placeholder (self->picker, "(no timeline)");
	lrg_widget_set_min_size (LRG_WIDGET (self->picker), 130.0f, 0.0f);
	g_signal_connect (self->picker, "changed", G_CALLBACK (on_picked), self);
	add_to (row1, LRG_WIDGET (self->picker));
	self->name = lrg_text_input_new_with_placeholder ("Name");
	lrg_widget_set_name (LRG_WIDGET (self->name), "timeline-name");
	lrg_widget_set_min_size (LRG_WIDGET (self->name), 90.0f, 0.0f);
	g_signal_connect (self->name, "submitted", G_CALLBACK (on_name_submitted), self);
	add_to (row1, LRG_WIDGET (self->name));
	add_to (row1, editor_chip_new ("timeline-new", LRG_ICON_PLUS, "New", on_new, self));
	add_to (row1, editor_chip_new ("timeline-rename", LRG_ICON_EDIT, "Rename", on_rename, self));
	self->delete_tl = editor_chip_new ("timeline-delete", LRG_ICON_TRASH, "Delete", on_delete_timeline, self);
	editor_chip_set_tint (self->delete_tl, LRG_THEME_COLOR_DANGER);
	add_to (row1, self->delete_tl);
	add_separator (row1);
	self->loop = editor_chip_new ("timeline-loop", LRG_ICON_REFRESH, "Loop", on_loop, self);
	add_to (row1, self->loop);
	self->autoplay = editor_chip_new ("timeline-autoplay", LRG_ICON_PLAY, "Autoplay", on_autoplay, self);
	add_to (row1, self->autoplay);
	self->length = lrg_spin_box_new_with_range (0.05, 3600.0, 0.25);
	lrg_spin_box_set_digits (self->length, 2);
	lrg_spin_box_set_value (self->length, 2.0);
	lrg_widget_set_name (LRG_WIDGET (self->length), "timeline-length");
	lrg_widget_set_min_size (LRG_WIDGET (self->length), 84.0f, 0.0f);
	g_signal_connect (self->length, "value-changed", G_CALLBACK (on_length), self);
	add_to (row1, LRG_WIDGET (self->length));
	add_separator (row1);
	add_to (row1, editor_chip_new ("timeline-to-sequence", LRG_ICON_CLOCK, "To Sequence", on_to_sequence, self));
	self->row1 = row1;
	add_to (self->top, row1);

	/* Row 2: transport | Key, Delete Keys, easing | time */
	row2 = toolbar_row ("timeline-transport");
	add_to (row2, editor_chip_new ("timeline-prev", LRG_ICON_CHEVRON_LEFT, NULL, on_prev, self));
	self->play = editor_chip_new ("timeline-play", LRG_ICON_PLAY, "Play", on_play, self);
	editor_chip_set_tint (self->play, LRG_THEME_COLOR_SUCCESS);
	add_to (row2, self->play);
	add_to (row2, editor_chip_new ("timeline-stop", LRG_ICON_STOP, "Stop", on_stop, self));
	add_to (row2, editor_chip_new ("timeline-next", LRG_ICON_CHEVRON_RIGHT, NULL, on_next, self));
	add_separator (row2);
	chip = editor_chip_new ("timeline-key", LRG_ICON_PLUS, "Key (I)", on_key, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_WARNING);
	add_to (row2, chip);
	chip = editor_chip_new ("timeline-delete-keys", LRG_ICON_CLOSE, "Delete Keys", on_delete_keys, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
	add_to (row2, chip);
	self->easing = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->easing), "timeline-easing");
	for (i = 0; i < klass->n_values; i++)
		lrg_combo_box_append (self->easing, klass->values[i].value_nick);
	lrg_combo_box_set_selected (self->easing, 0);
	lrg_widget_set_min_size (LRG_WIDGET (self->easing), 130.0f, 0.0f);
	g_signal_connect (self->easing, "changed", G_CALLBACK (on_easing), self);
	add_to (row2, LRG_WIDGET (self->easing));
	self->tangent = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->tangent), "timeline-tangent");
	for (i = 0; i < G_N_ELEMENTS (tangent_labels); i++)
		lrg_combo_box_append (self->tangent, tangent_labels[i]);
	lrg_combo_box_set_selected (self->tangent, 0);
	lrg_widget_set_min_size (LRG_WIDGET (self->tangent), 96.0f, 0.0f);
	lrg_widget_set_max_size (LRG_WIDGET (self->tangent), 120.0f, 0.0f);
	g_signal_connect (self->tangent, "changed", G_CALLBACK (on_tangent), self);
	add_to (row2, LRG_WIDGET (self->tangent));
	self->curves = editor_chip_new ("timeline-curves", LRG_ICON_EYE, "Curves", on_curves, self);
	add_to (row2, self->curves);
	add_separator (row2);
	self->bone = lrg_text_input_new_with_placeholder ("Bone");
	lrg_widget_set_name (LRG_WIDGET (self->bone), "timeline-bone");
	lrg_widget_set_min_size (LRG_WIDGET (self->bone), 72.0f, 0.0f);
	lrg_widget_set_max_size (LRG_WIDGET (self->bone), 110.0f, 0.0f);
	g_signal_connect (self->bone, "submitted", G_CALLBACK (on_bone_submitted), self);
	add_to (row2, LRG_WIDGET (self->bone));
	add_to (row2, editor_chip_new ("timeline-key-bone", LRG_ICON_PLUS, "Key Bone", on_key_bone, self));
	chip = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_fill_weight (chip, 1.0f);
	add_to (row2, chip);
	self->time_label = LRG_WIDGET (lrg_label_new ("0.00 s"));
	lrg_widget_set_name (self->time_label, "timeline-time");
	lrg_widget_set_valign (self->time_label, LRG_ALIGN_CENTER);
	add_to (row2, self->time_label);
	self->row2 = row2;
	add_to (self->top, row2);
	lrg_container_add_child (LRG_CONTAINER (self), self->top);
	g_object_unref (self->top);

	/* The view: grid snapping at 100 "fps" is the 10 ms key snap */
	self->view = lrg_timeline_view_new ();
	lrg_widget_set_name (LRG_WIDGET (self->view), "timeline-view");
	lrg_timeline_view_set_show_play_button (self->view, FALSE);
	lrg_timeline_view_set_fps (self->view, 100.0);
	lrg_timeline_view_set_snap (self->view, LRG_TIMELINE_VIEW_SNAP_GRID | LRG_TIMELINE_VIEW_SNAP_PLAYHEAD |
	                                        LRG_TIMELINE_VIEW_SNAP_CLIPS);
	lrg_timeline_view_set_look (self->view, LRG_CANVAS_LOOK_AUTO);
	lrg_timeline_view_set_header_width (self->view, 170.0f);
	g_signal_connect (self->view, "keyframe-moved", G_CALLBACK (on_keyframe_moved), self);
	g_signal_connect (self->view, "keyframe-removed", G_CALLBACK (on_keyframe_removed), self);
	g_signal_connect (self->view, "keyframe-added", G_CALLBACK (on_keyframe_added), self);
	g_signal_connect (self->view, "playhead-moved", G_CALLBACK (on_playhead_moved), self);
	g_signal_connect (self->view, "selection-changed", G_CALLBACK (on_selection_changed), self);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->view));
	g_object_unref (self->view);

	self->hint = editor_panel_hint_new ("timeline-empty", "No timeline. Type a name and press New, or select nodes "
	                                                      "and press I to key their transform.");
	lrg_container_add_child (LRG_CONTAINER (self), self->hint);
	g_object_unref (self->hint);

	/* Right-click menu with per-key easing */
	{
		g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
		g_autoptr(GSimpleAction)      easing = g_simple_action_new ("easing", G_VARIANT_TYPE_STRING);
		g_autoptr(GSimpleAction)      del = g_simple_action_new ("delete-keys", NULL);
		g_autoptr(GSimpleAction)      tangent = g_simple_action_new ("tangent", G_VARIANT_TYPE_STRING);
		g_autoptr(GSimpleAction)      convert = g_simple_action_new ("to-sequence", NULL);
		g_autoptr(GMenuModel)         menu = build_key_menu ();

		g_signal_connect (easing, "activate", G_CALLBACK (on_easing_action), self);
		g_signal_connect (del, "activate", G_CALLBACK (on_delete_keys_action), self);
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (easing));
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (del));
		g_signal_connect (tangent, "activate", G_CALLBACK (on_tangent_action), self);
		g_signal_connect (convert, "activate", G_CALLBACK (on_to_sequence_action), self);
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (tangent));
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (convert));
		lrg_widget_insert_action_group (LRG_WIDGET (self), "timeline-panel", G_ACTION_GROUP (group));
		lrg_widget_set_context_menu (LRG_WIDGET (self->view), menu);
	}
}

/* ==========================================================================
 * Actions and public API
 * ========================================================================== */

static void a_timeline_rename (gpointer d) { timeline_rename (LRG_EDITOR_TIMELINE_PANEL (d)); }
static void a_timeline_delete (gpointer d) { timeline_delete (LRG_EDITOR_TIMELINE_PANEL (d)); }

static gboolean
has_timeline (gpointer data)
{
	return current_timeline (LRG_EDITOR_TIMELINE_PANEL (data)) != NULL;
}

/* Delete Keyframe with the view's whole selection (falls back to the
 * app's single selected key, which IPC and the 0.8.0 flow set). */
static void
a_timeline_delete_key (gpointer data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (data);
	g_autoptr(GArray)       ids = lrg_timeline_view_get_selected_keyframes (self->view);
	LrgEditorApp           *app = self->app;
	LrgTimeline            *tl = current_timeline (self);
	LrgNode                *node;

	if (ids != NULL && ids->len > 0)
	{
		delete_selected_keys (self);
		return;
	}
	if (app == NULL || tl == NULL || !app->timeline_key_valid ||
	    (node = lrg_editor_find_node (self->editor, app->timeline_key_guid)) == NULL)
		return;
	lrg_editor_timeline_remove_key (self->editor, lrg_timeline_get_name (tl), node, app->timeline_key_channel,
	                                app->timeline_key_time);
	app->timeline_key_valid = FALSE;
}

static gboolean
key_selected (gpointer data)
{
	LrgEditorTimelinePanel *self = LRG_EDITOR_TIMELINE_PANEL (data);

	return current_timeline (self) != NULL && self->app != NULL && self->app->timeline_key_valid;
}

/* ==========================================================================
 * IPC: timeline.tangent
 * ========================================================================== */

/*
 * m_timeline_tangent:
 *
 * {name?, node, time, channel?, tangent, slope? | in?, out?}: makes the
 * matching keys Bézier keys of @tangent (auto user broken linear
 * constant) or eased keys again ("eased"). Without slopes a key keeps the
 * slopes it is drawn with. Result: {changed}.
 */
static JsonNode *
m_timeline_tangent (LrgEditorServer  *server,
                    JsonObject       *params,
                    gpointer          user_data,
                    GError          **error)
{
	LrgEditorApp     *app = LRG_EDITOR_APP (user_data);
	LrgEditor        *editor = lrg_editor_app_get_editor (app);
	LrgTimeline      *tl;
	LrgNode          *node;
	const gchar      *nick, *channel = NULL;
	g_autofree gchar *name = NULL;
	gfloat            time;
	guint             i, changed = 0;
	gint              row = -1;
	JsonObject       *o;
	JsonNode         *out;

	if (params == NULL || !json_object_has_member (params, "node") || !json_object_has_member (params, "time") ||
	    !json_object_has_member (params, "tangent"))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "timeline.tangent needs node, time and tangent");
		return NULL;
	}
	nick = json_object_get_string_member (params, "tangent");
	for (i = 0; nick != NULL && i < G_N_ELEMENTS (tangent_nicks); i++)
		if (g_strcmp0 (nick, tangent_nicks[i]) == 0)
			row = (gint) i;
	if (row < 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "tangent must be eased, auto, user, broken, linear or constant");
		return NULL;
	}
	name = g_strdup (json_object_has_member (params, "name") ? json_object_get_string_member (params, "name")
	                 : lrg_editor_view_state_get_timeline_name (lrg_editor_app_get_view_state (app)));
	tl = editor_timeline_current (app);
	if (json_object_has_member (params, "name"))
	{
		gint idx = lrg_level_find_timeline (lrg_editor_get_level (editor), name);

		tl = idx >= 0 ? lrg_level_get_timeline (lrg_editor_get_level (editor), (guint) idx) : NULL;
	}
	node = lrg_editor_find_node (editor, json_object_get_string_member (params, "node"));
	if (tl == NULL || node == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such %s",
		             tl == NULL ? "timeline" : "node");
		return NULL;
	}
	g_free (name);
	name = g_strdup (lrg_timeline_get_name (tl));
	time = (gfloat) json_object_get_double_member (params, "time");
	if (json_object_has_member (params, "channel"))
		channel = json_object_get_string_member (params, "channel");

	if (row == 0)
		changed = lrg_editor_timeline_clear_tangents (editor, name, node, channel, time);
	else
	{
		gboolean has_slope = json_object_has_member (params, "slope") || json_object_has_member (params, "in");

		lrg_editor_begin_group (editor, "Set Tangents");
		for (i = 0; i < lrg_timeline_get_n_tracks (tl); i++)
		{
			LrgTimelineTrack            *t = lrg_timeline_get_track (tl, i);
			g_autoptr(LrgSequenceCurve)  resolved = NULL;
			g_autofree gchar            *ch = NULL;
			gfloat                       in_s = 0.0f, out_s = 0.0f;
			gint                         k;

			if (g_strcmp0 (lrg_timeline_track_get_node_guid (t), lrg_node_get_guid (node)) != 0 ||
			    (channel != NULL && g_strcmp0 (lrg_timeline_track_get_channel (t), channel) != 0) ||
			    (k = lrg_timeline_track_find_key (t, time, 0.001f)) < 0)
				continue;
			resolved = resolve_track (t);
			lrg_sequence_curve_get_slopes (resolved, (guint) k, &in_s, &out_s);
			if (has_slope)
			{
				in_s = (gfloat) json_object_get_double_member_with_default (params, "in",
				               json_object_get_double_member_with_default (params, "slope", 0.0));
				out_s = (gfloat) json_object_get_double_member_with_default (params, "out", in_s);
			}
			/* the timeline is replaced by every edit: copy the channel first */
			ch = g_strdup (lrg_timeline_track_get_channel (t));
			changed += lrg_editor_timeline_set_tangents (editor, name, node, ch, time,
			                                             (LrgSequenceTangentMode) (row - 1), in_s, out_s);
			{
				gint idx = lrg_level_find_timeline (lrg_editor_get_level (editor), name);

				tl = idx >= 0 ? lrg_level_get_timeline (lrg_editor_get_level (editor), (guint) idx) : NULL;
			}
			if (tl == NULL)
				break;
		}
		lrg_editor_end_group (editor);
	}
	o = json_object_new ();
	json_object_set_int_member (o, "changed", changed);
	out = json_node_new (JSON_NODE_OBJECT);
	json_node_take_object (out, o);
	return out;
}

static gboolean
can_convert (gpointer data)
{
	return current_timeline (LRG_EDITOR_TIMELINE_PANEL (data)) != NULL;
}

static void a_to_sequence (gpointer d) { to_sequence (LRG_EDITOR_TIMELINE_PANEL (d)); }

LrgEditorTimelinePanel *
lrg_editor_timeline_panel_new (LrgEditorApp *app)
{
	LrgEditorTimelinePanel *self;
	LrgEditorActions       *actions;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_TIMELINE_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	self->editor = g_object_ref (lrg_editor_app_get_editor (app));
	self->view_state = g_object_ref (lrg_editor_app_get_view_state (app));
	g_signal_connect (self->editor, "changed", G_CALLBACK (on_editor_changed), self);
	g_signal_connect (self->editor, "level-replaced", G_CALLBACK (on_level_replaced_dirty), self);

	actions = lrg_editor_app_get_actions (app);
	lrg_editor_actions_add (actions, "timeline.rename", "Rename Timeline", "Animation", NULL, a_timeline_rename,
	                        has_timeline, NULL, self);
	lrg_editor_actions_add (actions, "timeline.delete", "Delete Timeline", "Animation", NULL, a_timeline_delete,
	                        has_timeline, NULL, self);
	lrg_editor_actions_add (actions, "timeline.delete-key", "Delete Keyframe", "Animation", NULL,
	                        a_timeline_delete_key, key_selected, NULL, self);
	lrg_editor_actions_add (actions, "timeline.to-sequence", "Convert Timeline to Sequence", "Animation", NULL,
	                        a_to_sequence, can_convert, NULL, self);
	if (app->server != NULL)
		lrg_editor_server_register_method (app->server, "timeline.tangent", m_timeline_tangent, app, NULL);
	{
		g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_EASING_TYPE);
		guint                 i;

		for (i = 0; i < klass->n_values; i++)
			if (klass->values[i].value == app->timeline_easing)
				lrg_combo_box_set_selected (self->easing, (gint) i);
	}
	rebuild (self);
	return self;
}

LrgTimelineView *
lrg_editor_timeline_panel_get_view (LrgEditorTimelinePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TIMELINE_PANEL (self), NULL);
	return self->view;
}

LrgCanvasLook
lrg_editor_timeline_panel_get_look (LrgEditorTimelinePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TIMELINE_PANEL (self), LRG_CANVAS_LOOK_AUTO);
	return self->look;
}

void
lrg_editor_timeline_panel_set_look (LrgEditorTimelinePanel *self,
                                    LrgCanvasLook           look)
{
	g_return_if_fail (LRG_IS_EDITOR_TIMELINE_PANEL (self));

	if (self->look == look)
		return;
	self->look = look;
	lrg_timeline_view_set_look (self->view, look);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOK]);
}

void
lrg_editor_timeline_panel_refresh (LrgEditorTimelinePanel *self)
{
	g_return_if_fail (LRG_IS_EDITOR_TIMELINE_PANEL (self));
	flush_ops (self);
	rebuild (self);
	sync_transport (self);
}

LrgTimelineViewTrack *
lrg_editor_timeline_panel_find_track (LrgEditorTimelinePanel *self,
                                      const gchar            *node_guid,
                                      const gchar            *channel)
{
	g_autofree gchar *key = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_TIMELINE_PANEL (self), NULL);
	g_return_val_if_fail (node_guid != NULL, NULL);

	if (self->dirty)
		rebuild (self);
	key = track_key (node_guid, channel);
	return g_hash_table_lookup (self->tracks, key);
}

guint
lrg_editor_timeline_panel_find_keyframe (LrgEditorTimelinePanel *self,
                                         const gchar            *node_guid,
                                         const gchar            *channel,
                                         gfloat                  time)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TIMELINE_PANEL (self), LRG_EDITOR_TIMELINE_PANEL_INVALID_KEY);

	if (self->dirty)
		rebuild (self);
	return find_key_id (self, node_guid, channel, time);
}

void
lrg_editor_timeline_panel_set_key_easing (LrgEditorTimelinePanel *self,
                                          LrgEasingType           easing)
{
	g_autoptr(GArray)     ids = NULL;
	g_autoptr(GEnumClass) klass = NULL;
	g_autofree gchar     *name = NULL;
	LrgTimeline          *tl;
	guint                 i;

	g_return_if_fail (LRG_IS_EDITOR_TIMELINE_PANEL (self));

	if (self->app == NULL)
		return;
	self->app->timeline_easing = (gint) easing;

	/* The picker follows */
	klass = g_type_class_ref (LRG_TYPE_EASING_TYPE);
	self->syncing = TRUE;
	for (i = 0; i < klass->n_values; i++)
		if (klass->values[i].value == (gint) easing && lrg_combo_box_get_selected (self->easing) != (gint) i)
			lrg_combo_box_set_selected (self->easing, (gint) i);
	self->syncing = FALSE;

	/* Re-ease the selected keys as one step */
	tl = current_timeline (self);
	ids = lrg_timeline_view_get_selected_keyframes (self->view);
	if (tl == NULL || ids == NULL || ids->len == 0)
		return;
	/* A copy: every edit replaces the level's timeline object */
	name = g_strdup (lrg_timeline_get_name (tl));
	lrg_editor_begin_group (self->editor, "Set Easing");
	for (i = 0; i < ids->len; i++)
	{
		KeyRef        *ref = g_hash_table_lookup (self->keys, GUINT_TO_POINTER (g_array_index (ids, guint, i)));
		LrgNode       *node = ref != NULL ? lrg_editor_find_node (self->editor, ref->guid) : NULL;
		GHashTableIter iter;
		gpointer       v;

		if (node == NULL)
			continue;
		if (ref->channel != NULL)
		{
			lrg_editor_timeline_set_easing (self->editor, name, node, ref->channel, ref->time, easing);
			continue;
		}
		/* A summary key: every channel key of the node at its time */
		g_hash_table_iter_init (&iter, self->keys);
		while (g_hash_table_iter_next (&iter, NULL, &v))
		{
			KeyRef *member = v;

			if (member->channel != NULL && g_strcmp0 (member->guid, ref->guid) == 0 &&
			    fabsf (member->time - ref->time) < TIME_EPSILON)
				lrg_editor_timeline_set_easing (self->editor, name, node, member->channel, member->time, easing);
		}
	}
	lrg_editor_end_group (self->editor);
	rebuild (self);
}

void
lrg_editor_timeline_panel_set_key_tangents (LrgEditorTimelinePanel *self,
                                            gboolean                bezier,
                                            LrgSequenceTangentMode  mode)
{
	g_autoptr(GArray)    ids = NULL;
	g_autoptr(GPtrArray) targets = NULL;
	g_autofree gchar    *name = NULL;
	LrgTimeline         *tl;
	guint                i;

	g_return_if_fail (LRG_IS_EDITOR_TIMELINE_PANEL (self));

	tl = current_timeline (self);
	ids = lrg_timeline_view_get_selected_keyframes (self->view);
	if (tl == NULL || ids == NULL || ids->len == 0)
		return;

	/* Channel keys to change: summaries expand to their node's keys */
	targets = g_ptr_array_new ();
	for (i = 0; i < ids->len; i++)
	{
		KeyRef        *ref = g_hash_table_lookup (self->keys, GUINT_TO_POINTER (g_array_index (ids, guint, i)));
		GHashTableIter iter;
		gpointer       v;

		if (ref == NULL)
			continue;
		if (ref->channel != NULL)
		{
			g_ptr_array_add (targets, ref);
			continue;
		}
		g_hash_table_iter_init (&iter, self->keys);
		while (g_hash_table_iter_next (&iter, NULL, &v))
		{
			KeyRef *member = v;

			if (member->channel != NULL && g_strcmp0 (member->guid, ref->guid) == 0 &&
			    fabsf (member->time - ref->time) < TIME_EPSILON)
				g_ptr_array_add (targets, member);
		}
	}

	/* A copy: every edit replaces the level's timeline object */
	name = g_strdup (lrg_timeline_get_name (tl));
	lrg_editor_begin_group (self->editor, bezier ? "Set Tangents" : "Clear Tangents");
	for (i = 0; i < targets->len; i++)
	{
		KeyRef  *ref = g_ptr_array_index (targets, i);
		LrgNode *node = lrg_editor_find_node (self->editor, ref->guid);

		if (node == NULL)
			continue;
		if (!bezier)
			lrg_editor_timeline_clear_tangents (self->editor, name, node, ref->channel, ref->time);
		else
			/* keep the drawn shape: the resolved slopes become the user slopes */
			lrg_editor_timeline_set_tangents (self->editor, name, node, ref->channel, ref->time, mode,
			                                  ref->in_slope, ref->out_slope);
	}
	lrg_editor_end_group (self->editor);
	rebuild (self);
}
