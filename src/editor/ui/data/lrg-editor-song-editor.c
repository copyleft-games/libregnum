/* lrg-editor-song-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The song editor (see lrg-editor-song-editor.h and docs/editor/music.org).
 *
 * Layout: a toolbar (transport, tracks, clips, export); on the left the
 * song settings and the selected track with its instrument parameters;
 * on the right the arrangement (bar ruler with markers, loop range and
 * playhead over one lane per track) above the pattern editor of the
 * selected clip (a step grid for drum lanes, a piano roll for notes).
 *
 * Every edit runs one of the shared song operations
 * (lrg-editor-song-ops.c) on a copy of the document's song and commits it
 * as one undo step; drags merge their steps with a gesture.  The live
 * player (LrgSongPlayer) gets every new version of the song and keeps
 * playing from the same beat.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL

#include "lrg-editor-song-editor.h"
#include "lrg-editor-song-ops-private.h"
#include "lrg-editor-music-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-scroll-box.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-menu.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-property-source.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../music/lrg-song-player.h"
#include "../../../music/lrg-midi-file.h"
#include "../../../music/lrg-instrument.h"
#include "../../../audio/lrg-audio-encoder.h"
#include "../../../audio/lrg-audio-analysis.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <errno.h>

#define INSTALL_KEY "lrg-editor-song-installed"
#define HEAD_W      (132.0f)    /* track names in the arrangement */
#define RULER_H     (32.0f)     /* bar numbers above, markers below */
#define TRACK_H     (30.0f)
#define EDGE_W      (6.0f)      /* the resize handle at a clip's end */
#define KEYS_W      (76.0f)     /* key or lane names in the pattern editor */
#define ROLL_ROW_H  (12.0f)
#define LANE_H      (20.0f)

enum
{
	PART_SONG,
	PART_TRACK,
	N_PARTS
};

typedef enum
{
	DRAG_NONE,
	DRAG_MOVE,
	DRAG_RESIZE,
	DRAG_NOTE
} DragMode;

struct _LrgEditorSongEditor
{
	LrgEditorDataEditor parent_instance;

	LrgSong            *song;
	gchar              *load_error;
	gchar              *sel_track;
	gint                sel_clip;
	guint               gesture;
	gchar              *signature;

	/* the selected track's instrument, built for its parameter list */
	LrgInstrument      *inst;
	gchar              *inst_key;

	/* playback */
	LrgSongPlayer      *player;
	gdouble             beat;

	/* widgets */
	GObject            *src[N_PARTS];
	LrgPropertyGrid    *grid[N_PARTS];
	LrgWidget          *arrange;
	LrgWidget          *pattern;
	LrgMenu            *track_menu;

	/* arrangement view */
	gfloat              ppb;        /* pixels per beat */
	gdouble             scroll;     /* first beat shown */

	/* drags */
	DragMode            drag;
	gchar              *drag_track;
	gint                drag_clip;
	gdouble             drag_beat0;
	gdouble             drag_start0;
	gdouble             drag_len0;
	gint                drag_note;
	gint                drag_key0;
};

G_DEFINE_FINAL_TYPE (LrgEditorSongEditor, lrg_editor_song_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void sync_views (LrgEditorSongEditor *self);

static LrgEditorDocument *
doc_of (LrgEditorSongEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static void
report (LrgEditorSongEditor *self,
        GError              *error)
{
	if (error != NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* ==========================================================================
 * Edits
 * ========================================================================== */

/*
 * commit_song:
 * Commits an edited copy of the song, but only when its YAML loads back:
 * a model call can accept what the loader refuses (a duck whose target
 * feeds its trigger), and the document must never become unloadable.
 */
static gboolean
commit_song (LrgEditorSongEditor  *self,
             GObject              *song,
             const gchar          *label,
             guint                 gesture,
             GError              **error)
{
	g_autofree gchar   *text = NULL;
	g_autoptr(LrgSong)  back = NULL;
	g_autoptr(GError)   err = NULL;

	text = lrg_song_to_data (LRG_SONG (song), NULL, error);
	if (text == NULL)
		return FALSE;
	back = lrg_song_new_from_data (text, -1, &err);
	if (back == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "The edit would not load back: %s", err->message);
		return FALSE;
	}
	return lrg_editor_document_commit_object (doc_of (self), song, label, gesture, error);
}

/*
 * run_op:
 * Runs the operation @name with @args on a copy of the song and commits
 * it (one undo step, merged with others of @gesture).  @result (nullable)
 * receives the operation's results.
 */
static gboolean
run_op (LrgEditorSongEditor  *self,
        const gchar          *label,
        guint                 gesture,
        const gchar          *name,
        JsonObject           *args,
        JsonObject           *result,
        GError              **error)
{
	LrgSongOp           op = lrg_editor_song_op_find (name);
	g_autoptr(GObject)  obj = NULL;
	g_autoptr(JsonObject) scratch = NULL;

	if (op == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "No song operation '%s'", name);
		return FALSE;
	}
	obj = lrg_editor_document_dup_object (doc_of (self), error);
	if (obj == NULL)
		return FALSE;
	if (result == NULL)
		result = scratch = json_object_new ();
	if (!op (LRG_SONG (obj), args, result, error))
		return FALSE;
	return commit_song (self, obj, label, gesture, error);
}

/* run_op() with arguments built from a NULL-terminated name/value list
 * of strings: the convenience of the UI (values are JSON literals) */
static gboolean
run_op_v (LrgEditorSongEditor  *self,
          const gchar          *label,
          guint                 gesture,
          const gchar          *name,
          GError              **error,
          ...) G_GNUC_NULL_TERMINATED;

static gboolean
run_op_v (LrgEditorSongEditor  *self,
          const gchar          *label,
          guint                 gesture,
          const gchar          *name,
          GError              **error,
          ...)
{
	g_autoptr(JsonObject) args = json_object_new ();
	va_list               ap;
	const gchar          *key;

	va_start (ap, error);
	while ((key = va_arg (ap, const gchar *)) != NULL)
	{
		const gchar *literal = va_arg (ap, const gchar *);
		JsonNode    *node = json_from_string (literal, NULL);

		if (node == NULL)
		{
			node = json_node_new (JSON_NODE_VALUE);
			json_node_set_string (node, literal);
		}
		json_object_set_member (args, key, node);
	}
	va_end (ap);
	return run_op (self, label, gesture, name, args, NULL, error);
}

/* a JSON string literal of @s */
static gchar *
jstr (const gchar *s)
{
	g_autoptr(JsonNode) n = json_node_new (JSON_NODE_VALUE);

	json_node_set_string (n, s);
	return json_to_string (n, FALSE);
}

static gchar *
jnum (gdouble v)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	return g_strdup (g_ascii_dtostr (buf, sizeof buf, v));
}

LrgSong *
lrg_editor_song_editor_get_song (LrgEditorSongEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SONG_EDITOR (self), NULL);
	return self->song;
}

gchar *
lrg_editor_song_editor_apply (LrgEditorSongEditor  *self,
                              const gchar          *ops,
                              const gchar          *label,
                              GError              **error)
{
	g_autoptr(JsonParser) parser = json_parser_new ();
	g_autoptr(GObject)    obj = NULL;
	g_autoptr(JsonNode)   out = NULL;
	JsonArray            *list;
	JsonArray            *results;
	JsonNode             *root;
	guint                 i;

	g_return_val_if_fail (LRG_IS_EDITOR_SONG_EDITOR (self), NULL);
	g_return_val_if_fail (ops != NULL, NULL);

	if (!json_parser_load_from_data (parser, ops, -1, error))
		return NULL;
	root = json_parser_get_root (parser);
	if (root == NULL || !JSON_NODE_HOLDS_ARRAY (root))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "The operations must be a JSON array");
		return NULL;
	}
	list = json_node_get_array (root);
	obj = lrg_editor_document_dup_object (doc_of (self), error);
	if (obj == NULL)
		return NULL;

	/* every operation on the one copy; the first failure stops it all */
	results = json_array_new ();
	out = json_node_new (JSON_NODE_ARRAY);
	json_node_take_array (out, results);
	for (i = 0; i < json_array_get_length (list); i++)
	{
		JsonNode          *n = json_array_get_element (list, i);
		JsonObject        *args = JSON_NODE_HOLDS_OBJECT (n) ? json_node_get_object (n) : NULL;
		const gchar       *name = args != NULL && json_object_has_member (args, "op")
		                          ? json_object_get_string_member_with_default (args, "op", NULL) : NULL;
		LrgSongOp          op = lrg_editor_song_op_find (name);
		JsonObject        *result;
		g_autoptr(GError)  err = NULL;

		if (op == NULL)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "ops[%u]: unknown op '%s'", i,
			             name != NULL ? name : "(missing)");
			return NULL;
		}
		result = json_object_new ();
		json_object_set_string_member (result, "op", name);
		if (!op (LRG_SONG (obj), args, result, &err))
		{
			json_object_unref (result);
			g_set_error (error, err->domain, err->code, "ops[%u] (%s): %s", i, name, err->message);
			return NULL;
		}
		json_array_add_object_element (results, result);
	}
	if (!commit_song (self, obj, label != NULL ? label : "Song: edits", 0, error))
		return NULL;
	return json_to_string (out, FALSE);
}

void
lrg_editor_song_editor_select (LrgEditorSongEditor *self,
                               const gchar         *track,
                               gint                 clip)
{
	g_return_if_fail (LRG_IS_EDITOR_SONG_EDITOR (self));
	if (track != self->sel_track)
	{
		g_free (self->sel_track);
		self->sel_track = g_strdup (track);
	}
	self->sel_clip = track != NULL ? clip : -1;
	sync_views (self);
}

const gchar *
lrg_editor_song_editor_get_selected_track (LrgEditorSongEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SONG_EDITOR (self), NULL);
	return self->sel_track;
}

gint
lrg_editor_song_editor_get_selected_clip (LrgEditorSongEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SONG_EDITOR (self), -1);
	return self->sel_clip;
}

/* ==========================================================================
 * Playback
 * ========================================================================== */

static gboolean
ensure_player (LrgEditorSongEditor  *self,
               GError              **error)
{
	if (self->player != NULL)
		return TRUE;
	if (self->song == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                     self->load_error != NULL ? self->load_error : "The song does not load");
		return FALSE;
	}
	self->player = lrg_song_player_new (self->song, error);
	if (self->player == NULL)
		return FALSE;
	lrg_song_player_seek (self->player, self->beat);
	return TRUE;
}

gboolean
lrg_editor_song_editor_play (LrgEditorSongEditor  *self,
                             gdouble               from_beat,
                             GError              **error)
{
	GrlAudioDevice *device;

	g_return_val_if_fail (LRG_IS_EDITOR_SONG_EDITOR (self), FALSE);
	if (!ensure_player (self, error))
		return FALSE;
	if (from_beat >= 0.0)
		lrg_song_player_seek (self->player, from_beat);
	if (!lrg_song_player_has_output (self->player))
	{
		/* the device opens on the first play, never before */
		device = grl_audio_device_get_default ();
		if (device == NULL || (!grl_audio_device_is_ready (device) && !grl_audio_device_init_audio (device)))
		{
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED, "No audio device available to play the song");
			return FALSE;
		}
		if (!lrg_song_player_start_output (self->player, error))
			return FALSE;
	}
	lrg_song_player_play (self->player);
	return TRUE;
}

void
lrg_editor_song_editor_stop (LrgEditorSongEditor *self)
{
	g_return_if_fail (LRG_IS_EDITOR_SONG_EDITOR (self));
	if (self->player == NULL)
		return;
	self->beat = lrg_song_player_get_beat (self->player);
	lrg_song_player_stop_output (self->player);
	lrg_song_player_pause (self->player);
}

gdouble
lrg_editor_song_editor_get_beat (LrgEditorSongEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SONG_EDITOR (self), 0.0);
	/* headless, a seek waits for the next render: the editor's playhead is
	 * the truth until a device stream runs */
	if (self->player != NULL && lrg_song_player_has_output (self->player))
		return lrg_song_player_get_beat (self->player);
	return self->beat;
}

static void
seek (LrgEditorSongEditor *self,
      gdouble              beat)
{
	self->beat = MAX (beat, 0.0);
	if (self->player != NULL)
		lrg_song_player_seek (self->player, self->beat);
}

gfloat *
lrg_editor_song_editor_preview_render (LrgEditorSongEditor  *self,
                                       guint                 frames,
                                       gsize                *n_samples,
                                       GError              **error)
{
	g_autofree gfloat *out = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SONG_EDITOR (self), NULL);
	g_return_val_if_fail (n_samples != NULL, NULL);
	*n_samples = 0;
	if (!ensure_player (self, error))
		return NULL;
	if (lrg_song_player_has_output (self->player))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_BUSY, "The song is playing on the device");
		return NULL;
	}
	if (!lrg_song_player_is_playing (self->player))
		lrg_song_player_play (self->player);
	out = g_new0 (gfloat, (gsize) frames * 2);
	if (!lrg_song_player_render (self->player, out, frames))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "The preview did not render");
		return NULL;
	}
	self->beat = lrg_song_player_get_beat (self->player);
	*n_samples = (gsize) frames * 2;
	return g_steal_pointer (&out);
}

/* the default export: -14 LUFS integrated, -1 dBTP */
static LrgSongRenderOptions *
default_options (void)
{
	LrgSongRenderOptions *o = lrg_song_render_options_new ();

	o->normalize = LRG_SONG_NORMALIZE_LOUDNESS;
	o->target = -14.0;
	o->true_peak_ceiling = -1.0;
	return o;
}

static LrgAudioEncodeOptions *
tagged (LrgSong *song)
{
	LrgAudioEncodeOptions *enc = lrg_audio_encode_options_new ();

	g_object_get (song, "title", &enc->title, "artist", &enc->artist, NULL);
	return enc;
}

gboolean
lrg_editor_song_editor_export (LrgEditorSongEditor         *self,
                               const gchar                 *path,
                               const LrgSongRenderOptions  *options,
                               GError                     **error)
{
	g_autoptr(LrgWaveData)     wave = NULL;
	LrgSongRenderOptions      *defaults = NULL;
	LrgAudioEncodeOptions     *enc;
	LrgAudioFileFormat         format;
	gboolean                   ok;

	g_return_val_if_fail (LRG_IS_EDITOR_SONG_EDITOR (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	if (self->song == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                     self->load_error != NULL ? self->load_error : "The song does not load");
		return FALSE;
	}
	if (!lrg_audio_file_format_from_path (path, &format))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s: unknown audio extension", path);
		return FALSE;
	}
	if (!music_ensure_parent (path, error))
		return FALSE;
	if (options == NULL)
		options = defaults = default_options ();
	wave = lrg_song_render (self->song, options, NULL, NULL, NULL, error);
	g_clear_pointer (&defaults, lrg_song_render_options_free);
	if (wave == NULL)
		return FALSE;
	enc = tagged (self->song);
	ok = lrg_audio_encode_wave (wave, format, enc, path, NULL, error);
	lrg_audio_encode_options_free (enc);
	return ok;
}

/* ==========================================================================
 * Property sources: the song settings, the selected track
 * ========================================================================== */

#define EDITOR_TYPE_SONG_SOURCE (editor_song_source_get_type ())
G_DECLARE_FINAL_TYPE (EditorSongSource, editor_song_source, EDITOR, SONG_SOURCE, GObject)

struct _EditorSongSource
{
	GObject              parent_instance;
	LrgEditorSongEditor *editor;    /* weak */
	gint                 part;
};

static void editor_song_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (EditorSongSource, editor_song_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE, editor_song_source_iface_init))

static GParamSpec *
vpspec (const gchar *name,
        GType        type)
{
	static GHashTable *specs = NULL;
	g_autofree gchar  *key = g_strdup_printf ("%s/%s", name, g_type_name (type));
	GParamSpec        *pspec;

	if (specs == NULL)
		specs = g_hash_table_new (g_str_hash, g_str_equal);
	pspec = g_hash_table_lookup (specs, key);
	if (pspec != NULL)
		return pspec;
	if (type == G_TYPE_STRING)
		pspec = g_param_spec_string (g_intern_string (name), NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	else if (type == G_TYPE_BOOLEAN)
		pspec = g_param_spec_boolean (g_intern_string (name), NULL, NULL, FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	else
		pspec = g_param_spec_double (g_intern_string (name), NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
		                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_param_spec_ref_sink (pspec);
	g_hash_table_insert (specs, (gpointer) g_intern_string (key), pspec);
	return pspec;
}

static void
add_row (GPtrArray   *infos,
         const gchar *name,
         GType        type,
         const gchar *label,
         const gchar *group,
         const gchar *hint,
         const gchar *tooltip)
{
	LrgPropertyInfo *info = lrg_property_info_new (vpspec (name, type));

	lrg_property_info_set_label (info, label);
	if (group != NULL)
		lrg_property_info_set_group (info, group);
	if (hint != NULL)
		lrg_property_info_set_hint (info, hint);
	if (tooltip != NULL)
		lrg_property_info_set_tooltip (info, tooltip);
	g_ptr_array_add (infos, info);
}

static gchar *
pretty (const gchar *name)
{
	gchar *s = g_strdelimit (g_strdup (name), "-_", ' ');

	s[0] = g_ascii_toupper (s[0]);
	return s;
}

/* the numeric range of an instrument property, or FALSE to leave it out */
static gboolean
pspec_range (GParamSpec *p,
             gdouble    *min,
             gdouble    *max)
{
	if (G_IS_PARAM_SPEC_DOUBLE (p))
	{
		*min = G_PARAM_SPEC_DOUBLE (p)->minimum;
		*max = G_PARAM_SPEC_DOUBLE (p)->maximum;
	}
	else if (G_IS_PARAM_SPEC_FLOAT (p))
	{
		*min = G_PARAM_SPEC_FLOAT (p)->minimum;
		*max = G_PARAM_SPEC_FLOAT (p)->maximum;
	}
	else if (G_IS_PARAM_SPEC_UINT (p))
	{
		*min = G_PARAM_SPEC_UINT (p)->minimum;
		*max = MIN ((gdouble) G_PARAM_SPEC_UINT (p)->maximum, 1e6);
	}
	else if (G_IS_PARAM_SPEC_INT (p))
	{
		*min = MAX ((gdouble) G_PARAM_SPEC_INT (p)->minimum, -1e6);
		*max = MIN ((gdouble) G_PARAM_SPEC_INT (p)->maximum, 1e6);
	}
	else if (G_IS_PARAM_SPEC_ENUM (p))
	{
		GEnumClass *ec = G_PARAM_SPEC_ENUM (p)->enum_class;

		*min = ec->minimum;
		*max = ec->maximum;
	}
	else
		return FALSE;
	return TRUE;
}

static gdouble
value_as_double (const GValue *v)
{
	if (G_VALUE_HOLDS_DOUBLE (v))
		return g_value_get_double (v);
	if (G_VALUE_HOLDS_FLOAT (v))
		return g_value_get_float (v);
	if (G_VALUE_HOLDS_UINT (v))
		return g_value_get_uint (v);
	if (G_VALUE_HOLDS_INT (v))
		return g_value_get_int (v);
	if (G_VALUE_HOLDS_ENUM (v))
		return g_value_get_enum (v);
	if (G_VALUE_HOLDS_BOOLEAN (v))
		return g_value_get_boolean (v) ? 1.0 : 0.0;
	return 0.0;
}

static GPtrArray *
editor_song_source_list_properties (LrgPropertySource *source)
{
	EditorSongSource    *src = EDITOR_SONG_SOURCE (source);
	LrgEditorSongEditor *ed = src->editor;
	GPtrArray           *infos = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);

	if (ed == NULL || ed->song == NULL)
		return infos;
	if (src->part == PART_SONG)
	{
		add_row (infos, "title", G_TYPE_STRING, "Title", "Song", NULL, NULL);
		add_row (infos, "artist", G_TYPE_STRING, "Artist", "Song", NULL, NULL);
		add_row (infos, "bpm", G_TYPE_DOUBLE, "Tempo", "Song", "range:20,400,0.5", "Beats per minute at beat 0");
		add_row (infos, "beats-per-bar", G_TYPE_DOUBLE, "Beats / bar", "Song", "range:1,32,1", NULL);
		add_row (infos, "length", G_TYPE_DOUBLE, "Length", "Song", "range:0,100000,1",
		         "Beats; 0: up to the end of the last clip");
		add_row (infos, "loop-start", G_TYPE_DOUBLE, "Loop start", "Loop", "range:0,100000,1", "Beat");
		add_row (infos, "loop-end", G_TYPE_DOUBLE, "Loop end", "Loop", "range:0,100000,1", "Beat; 0: no loop");
		add_row (infos, "seed", G_TYPE_DOUBLE, "Seed", "Song", "range:0,4294967295,1", "Noise of every instrument");
		return infos;
	}
	if (ed->sel_track == NULL || lrg_song_get_track_text (ed->song, ed->sel_track, "bus") == NULL)
		return infos;
	add_row (infos, "track-name", G_TYPE_STRING, "Name", "Track", NULL, NULL);
	add_row (infos, "track-volume", G_TYPE_DOUBLE, "Volume", "Track", "range:-60,12,0.1", "dB");
	add_row (infos, "track-pan", G_TYPE_DOUBLE, "Pan", "Track", "range:-1,1,0.01", NULL);
	add_row (infos, "track-mute", G_TYPE_BOOLEAN, "Mute", "Track", NULL, NULL);
	add_row (infos, "track-solo", G_TYPE_BOOLEAN, "Solo", "Track", NULL, NULL);
	add_row (infos, "track-bus", G_TYPE_STRING, "Bus", "Track", NULL, "The mixer bus it plays into");
	if (ed->inst != NULL)
	{
		guint                  n = 0, i;
		g_autofree GParamSpec **specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (ed->inst), &n);
		const gchar           *type = lrg_song_get_instrument_type (ed->song,
		                                                            lrg_song_get_track_text (ed->song, ed->sel_track,
		                                                                                     "instrument"));

		for (i = 0; i < n; i++)
		{
			gdouble           min = 0, max = 0;
			g_autofree gchar *name = NULL;
			g_autofree gchar *label = NULL;
			g_autofree gchar *hint = NULL;
			const gchar      *dash;
			const gchar      *group;

			if (!(specs[i]->flags & G_PARAM_WRITABLE) || !pspec_range (specs[i], &min, &max))
				continue;
			name = g_strconcat ("param-", specs[i]->name, NULL);
			label = pretty (specs[i]->name);
			hint = g_strdup_printf ("range:%g,%g,%g", min, max,
			                        (max - min) > 1000.0 ? 1.0 : (max - min) > 10.0 ? 0.1 : 0.001);
			/* group synth parameters by their prefix ("osc1-", "filter-") */
			dash = strchr (specs[i]->name, '-');
			if (dash != NULL)
			{
				g_autofree gchar *prefix = g_strndup (specs[i]->name, (gsize) (dash - specs[i]->name));

				group = g_intern_string (prefix);
			}
			else
				group = type != NULL ? type : "Instrument";
			add_row (infos, name, G_TYPE_DOUBLE, label, group, hint, g_param_spec_get_blurb (specs[i]));
		}
	}
	return infos;
}

static gboolean
editor_song_source_get_value (LrgPropertySource *source,
                              const gchar       *name,
                              GValue            *value)
{
	EditorSongSource    *src = EDITOR_SONG_SOURCE (source);
	LrgEditorSongEditor *ed = src->editor;

	if (ed == NULL || ed->song == NULL)
		return FALSE;
	if (src->part == PART_SONG)
	{
		GParamSpec *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (ed->song), name);
		GValue      v = G_VALUE_INIT;

		if (pspec == NULL)
			return FALSE;
		g_value_init (&v, pspec->value_type);
		g_object_get_property (G_OBJECT (ed->song), name, &v);
		if (G_VALUE_HOLDS_STRING (&v))
		{
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, g_value_get_string (&v));
		}
		else
		{
			g_value_init (value, G_TYPE_DOUBLE);
			g_value_set_double (value, value_as_double (&v));
		}
		g_value_unset (&v);
		return TRUE;
	}
	if (ed->sel_track == NULL)
		return FALSE;
	if (g_str_has_prefix (name, "track-"))
	{
		const gchar *field = name + 6;

		if (g_str_equal (field, "name") || g_str_equal (field, "bus"))
		{
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, lrg_song_get_track_text (ed->song, ed->sel_track, field));
		}
		else if (g_str_equal (field, "mute") || g_str_equal (field, "solo"))
		{
			g_value_init (value, G_TYPE_BOOLEAN);
			g_value_set_boolean (value, lrg_song_get_track_value (ed->song, ed->sel_track, field) != 0.0);
		}
		else
		{
			g_value_init (value, G_TYPE_DOUBLE);
			g_value_set_double (value, lrg_song_get_track_value (ed->song, ed->sel_track, field));
		}
		return TRUE;
	}
	if (g_str_has_prefix (name, "param-") && ed->inst != NULL)
	{
		GParamSpec *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (ed->inst), name + 6);
		GValue      v = G_VALUE_INIT;

		if (pspec == NULL)
			return FALSE;
		g_value_init (&v, pspec->value_type);
		g_object_get_property (G_OBJECT (ed->inst), name + 6, &v);
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, value_as_double (&v));
		g_value_unset (&v);
		return TRUE;
	}
	return FALSE;
}

static gboolean
editor_song_source_set_value (LrgPropertySource *source,
                              const gchar       *name,
                              const GValue      *value)
{
	EditorSongSource      *src = EDITOR_SONG_SOURCE (source);
	LrgEditorSongEditor   *ed = src->editor;
	g_autoptr(GError)      error = NULL;
	g_autoptr(JsonObject)  args = json_object_new ();
	gboolean               ok = FALSE;
	JsonNode              *literal = json_node_new (JSON_NODE_VALUE);

	if (ed == NULL || ed->song == NULL)
	{
		json_node_unref (literal);
		return FALSE;
	}
	if (G_VALUE_HOLDS_STRING (value))
		json_node_set_string (literal, g_value_get_string (value) != NULL ? g_value_get_string (value) : "");
	else if (G_VALUE_HOLDS_BOOLEAN (value))
		json_node_set_boolean (literal, g_value_get_boolean (value));
	else
		json_node_set_double (literal, value_as_double (value));

	if (src->part == PART_SONG)
	{
		g_autofree gchar *key = g_strdelimit (g_strdup (name), "-", '_');

		json_object_set_member (args, key, literal);
		ok = run_op (ed, "Song: settings", ed->gesture, "set", args, NULL, &error);
	}
	else if (ed->sel_track != NULL && g_str_has_prefix (name, "track-"))
	{
		json_object_set_string_member (args, "id", ed->sel_track);
		json_object_set_member (args, name + 6, literal);
		ok = run_op (ed, "Song: track", ed->gesture, "set_track", args, NULL, &error);
	}
	else if (ed->sel_track != NULL && g_str_has_prefix (name, "param-"))
	{
		JsonObject *params = json_object_new ();

		json_object_set_member (params, name + 6, literal);
		json_object_set_string_member (args, "id", lrg_song_get_track_text (ed->song, ed->sel_track, "instrument"));
		json_object_set_object_member (args, "params", params);
		ok = run_op (ed, "Song: instrument", ed->gesture, "set_instrument", args, NULL, &error);
	}
	else
		json_node_unref (literal);
	report (ed, error);
	if (ok)
		lrg_property_source_emit_changed (source, name);
	return ok;
}

static void
editor_song_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = editor_song_source_list_properties;
	iface->get_value = editor_song_source_get_value;
	iface->set_value = editor_song_source_set_value;
}

static void
editor_song_source_finalize (GObject *object)
{
	g_clear_weak_pointer (&EDITOR_SONG_SOURCE (object)->editor);
	G_OBJECT_CLASS (editor_song_source_parent_class)->finalize (object);
}

static void
editor_song_source_class_init (EditorSongSourceClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = editor_song_source_finalize;
}

static void
editor_song_source_init (EditorSongSource *self)
{
}

static void
on_edit_begin (LrgPropertyGrid *grid,
               const gchar     *name,
               guint            gesture_id,
               gpointer         user_data)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (user_data);

	self->gesture = lrg_editor_data_editor_new_gesture (LRG_EDITOR_DATA_EDITOR (self));
}

static void
on_edit_end (LrgPropertyGrid *grid,
             const gchar     *name,
             guint            gesture_id,
             gboolean         commit_,
             gpointer         user_data)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (user_data);

	self->gesture = 0;
	sync_views (self);
}

/* ==========================================================================
 * The arrangement
 * ========================================================================== */

static guint
n_tracks (LrgEditorSongEditor *self)
{
	g_auto(GStrv) ids = NULL;

	if (self->song == NULL)
		return 0;
	ids = lrg_song_list_tracks (self->song);
	return g_strv_length (ids);
}

static guint
bpb (LrgEditorSongEditor *self)
{
	guint n = 4;

	if (self->song != NULL)
		g_object_get (self->song, "beats-per-bar", &n, NULL);
	return MAX (n, 1u);
}

/* the beats the arrangement spans: the song and two bars more */
static gdouble
view_beats (LrgEditorSongEditor *self)
{
	return (self->song != NULL ? lrg_song_get_length_beats (self->song) : 0.0) + 2.0 * bpb (self);
}

static void
arrange_measure (LrgCustomWidget     *widget,
                 gfloat               avail_width,
                 gfloat               avail_height,
                 LrgCustomWidgetSize *size,
                 gpointer             user_data)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (user_data);

	size->width = 480.0f;
	size->height = RULER_H + (gfloat) MAX (n_tracks (self), 1u) * TRACK_H + 4.0f;
}

/* the first paint (or event) fits the whole song in the view */
static void
fit_view (LrgEditorSongEditor *self,
          gfloat               width)
{
	if (self->ppb > 0.0f || self->song == NULL || width <= HEAD_W + 20.0f)
		return;
	self->ppb = CLAMP ((width - HEAD_W - 8.0f) / (gfloat) view_beats (self), 1.0f, 200.0f);
}

static gfloat
beat_x (LrgEditorSongEditor *self,
        gfloat               x0,
        gdouble              beat)
{
	return x0 + HEAD_W + (gfloat) ((beat - self->scroll) * self->ppb);
}

static gdouble
x_beat (LrgEditorSongEditor *self,
        gfloat               x0,
        gfloat               x)
{
	return self->scroll + (gdouble) ((x - x0 - HEAD_W) / self->ppb);
}

static void
arrange_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (user_data);
	LrgTheme            *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont             *font = lrg_theme_get_ui_font (theme);
	gfloat               fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor      *text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	const GrlColor      *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	const GrlColor      *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_LINK);
	const GrlColor      *sel = lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION);
	const GrlColor      *danger = lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);
	GrlColor             track_bg = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);
	GrlColor             clip_bg = *accent;
	GrlColor             loop_bg = *accent;
	g_auto(GStrv)        ids = NULL;
	gdouble              end = view_beats (self);
	gdouble              ls = 0, le = 0, b, bar_step;
	guint                beats_bar = bpb (self), i, j;
	gfloat               lanes_x = x + HEAD_W;
	gfloat               playhead;

	track_bg.a = (guint8) (track_bg.a / 2);
	clip_bg.a = 90;
	loop_bg.a = 50;
	lrg_paint_list_rect (list, x, y, width, height, 4.0f, &track_bg);
	fit_view (self, width);
	if (self->song == NULL || self->ppb <= 0.0f)
	{
		lrg_paint_list_text (list, font, self->load_error != NULL ? self->load_error : "The song does not load",
		                     x + 10.0f, y + 10.0f, fs, 0.0f, danger);
		return;
	}
	ids = lrg_song_list_tracks (self->song);
	lrg_paint_list_push_clip (list, lanes_x, y, MAX (width - HEAD_W, 0.0f), height);

	/* loop range and grid: bar lines strong, beats faint */
	g_object_get (self->song, "loop-start", &ls, "loop-end", &le, NULL);
	if (le > ls)
		lrg_paint_list_rect (list, beat_x (self, x, ls), y, (gfloat) ((le - ls) * self->ppb), RULER_H, 0.0f, &loop_bg);
	/* bars every 1, 2, 4 or 8 bars so lines stay 12 px apart or more */
	bar_step = 1.0;
	while (self->ppb * beats_bar * bar_step < 12.0f && bar_step < 64.0)
		bar_step *= 2.0;
	for (b = floor (self->scroll); b <= end; b += 1.0)
	{
		gfloat   bx = beat_x (self, x, b);
		gdouble  bar_no = b / beats_bar;
		gboolean bar = fmod (b, (gdouble) beats_bar) < 1e-9 && fmod (bar_no, bar_step) < 1e-9;

		if (bx < lanes_x - 1.0f)
			continue;
		if (bx > x + width)
			break;
		if (bar || self->ppb >= 10.0f)
			lrg_paint_list_line (list, bx, y + (bar ? 0.0f : RULER_H * 0.6f), bx, y + height, 1.0f,
			                     bar ? muted : &track_bg);
		if (bar && (self->ppb * beats_bar * bar_step >= 28.0f || fmod (bar_no, bar_step * 4.0) < 1e-9))
		{
			g_autofree gchar *num = g_strdup_printf ("%.0f", bar_no + 1.0);

			lrg_paint_list_text (list, font, num, bx + 3.0f, y + 2.0f, fs, 0.0f, muted);
		}
	}

	/* markers on the ruler */
	for (i = 0; i < lrg_song_get_n_markers (self->song); i++)
	{
		const gchar *name = NULL;
		gdouble      mb = 0;
		gfloat       mx;

		lrg_song_get_marker (self->song, i, &mb, &name);
		mx = beat_x (self, x, mb);
		lrg_paint_list_line (list, mx, y, mx, y + height, 1.0f, accent);
		lrg_paint_list_text (list, font, name, mx + 3.0f, y + RULER_H * 0.5f + 1.0f, fs, 0.0f, accent);
	}

	/* clips */
	for (i = 0; ids[i] != NULL; i++)
	{
		gfloat ty = y + RULER_H + (gfloat) i * TRACK_H;
		guint  n = lrg_song_get_n_clips (self->song, ids[i]);

		for (j = 0; j < n; j++)
		{
			const gchar *source = NULL;
			gdouble      start = 0, len = 0;
			gfloat       cx, cw;
			gboolean     is_sel = g_strcmp0 (ids[i], self->sel_track) == 0 && (gint) j == self->sel_clip;
			gboolean     muted_clip = lrg_song_get_clip_value (self->song, ids[i], j, "mute") != 0.0;

			lrg_song_get_clip (self->song, ids[i], j, &source, &start, &len);
			cx = beat_x (self, x, start);
			cw = MAX ((gfloat) (len * self->ppb), 2.0f);
			if (cx > x + width || cx + cw < lanes_x)
				continue;
			lrg_paint_list_rect (list, cx + 1.0f, ty + 2.0f, cw - 2.0f, TRACK_H - 4.0f, 3.0f,
			                     is_sel ? sel : &clip_bg);
			/* where each clip starts, so neighbours do not merge */
			lrg_paint_list_line (list, cx + 1.0f, ty + 3.0f, cx + 1.0f, ty + TRACK_H - 3.0f, 2.0f, accent);
			lrg_paint_list_push_clip (list, cx + 1.0f, ty, MAX (cw - 3.0f, 0.0f), TRACK_H);
			lrg_paint_list_text (list, font, source, cx + 5.0f, roundf (ty + (TRACK_H - fs) * 0.5f), fs, 0.0f,
			                     muted_clip ? muted : text);
			lrg_paint_list_pop_clip (list);
		}
	}

	/* the playhead */
	playhead = beat_x (self, x, lrg_editor_song_editor_get_beat (self));
	lrg_paint_list_line (list, playhead, y, playhead, y + height, 2.0f, danger);
	lrg_paint_list_pop_clip (list);

	/* track names, over everything at the left */
	lrg_paint_list_rect (list, x, y, HEAD_W, height, 0.0f, &track_bg);
	for (i = 0; ids[i] != NULL; i++)
	{
		gfloat            ty = y + RULER_H + (gfloat) i * TRACK_H;
		const gchar      *name = lrg_song_get_track_text (self->song, ids[i], "name");
		gboolean          m = lrg_song_get_track_value (self->song, ids[i], "mute") != 0.0;
		gboolean          s = lrg_song_get_track_value (self->song, ids[i], "solo") != 0.0;
		g_autofree gchar *label = g_strdup_printf ("%s%s%s", name != NULL && *name != '\0' ? name : ids[i],
		                                           m ? "  M" : "", s ? "  S" : "");

		if (g_strcmp0 (ids[i], self->sel_track) == 0)
			lrg_paint_list_rect (list, x + 2.0f, ty + 1.0f, HEAD_W - 4.0f, TRACK_H - 2.0f, 3.0f, sel);
		lrg_paint_list_push_clip (list, x + 6.0f, ty, HEAD_W - 10.0f, TRACK_H);
		lrg_paint_list_text (list, font, label, x + 8.0f, roundf (ty + (TRACK_H - fs) * 0.5f), fs, 0.0f,
		                     m ? muted : text);
		lrg_paint_list_pop_clip (list);
	}
	if (ids[0] == NULL)
		lrg_paint_list_text (list, font, "No tracks: add one from the toolbar", x + 8.0f, y + RULER_H + 8.0f, fs, 0.0f,
		                     muted);
}

/* repaints every frame while the song plays */
static gboolean
arrange_step (LrgCustomWidget *widget,
              gfloat           delta_time,
              gpointer         user_data)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (user_data);

	if (self->player == NULL || !lrg_song_player_has_output (self->player))
		return FALSE;
	lrg_song_player_update (self->player);
	return TRUE;
}

/* which track row and clip lie under (px, py); FALSE above the rows */
static gboolean
hit_clip (LrgEditorSongEditor  *self,
          gfloat                x0,
          gfloat                y0,
          gfloat                px,
          gfloat                py,
          gchar               **out_track,
          gint                 *out_clip,
          gboolean             *out_edge)
{
	g_auto(GStrv) ids = lrg_song_list_tracks (self->song);
	guint         row, n, j;
	gdouble       beat = x_beat (self, x0, px);

	*out_clip = -1;
	*out_edge = FALSE;
	if (py < y0 + RULER_H)
		return FALSE;
	row = (guint) ((py - y0 - RULER_H) / TRACK_H);
	if (row >= g_strv_length (ids))
		return FALSE;
	*out_track = g_strdup (ids[row]);
	if (px < x0 + HEAD_W)
		return TRUE;
	n = lrg_song_get_n_clips (self->song, ids[row]);
	for (j = 0; j < n; j++)
	{
		const gchar *source = NULL;
		gdouble      start = 0, len = 0;

		lrg_song_get_clip (self->song, ids[row], j, &source, &start, &len);
		if (beat >= start && beat < start + len)
		{
			*out_clip = (gint) j;
			*out_edge = px >= beat_x (self, x0, start + len) - EDGE_W;
		}
	}
	return TRUE;
}

static gboolean
arrange_event (LrgCustomWidget  *widget,
               const LrgUIEvent *event,
               gpointer          user_data)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (user_data);
	LrgUIEventType       type = lrg_ui_event_get_event_type (event);
	gfloat               x0 = lrg_widget_get_world_x (LRG_WIDGET (widget));
	gfloat               y0 = lrg_widget_get_world_y (LRG_WIDGET (widget));
	gfloat               px = lrg_ui_event_get_x (event);
	gfloat               py = lrg_ui_event_get_y (event);
	g_autoptr(GError)    error = NULL;

	if (self->song == NULL)
		return FALSE;
	fit_view (self, lrg_widget_get_width (LRG_WIDGET (widget)));
	if (self->ppb <= 0.0f)
		return FALSE;
	if (type == LRG_UI_EVENT_SCROLL)
	{
		/* Ctrl+wheel zooms; the wheel scrolls through the song */
		if (lrg_ui_event_get_modifiers (event) & LRG_INPUT_MODIFIER_CTRL)
			self->ppb = CLAMP (self->ppb * (lrg_ui_event_get_scroll_y (event) > 0 ? 1.25f : 0.8f), 1.0f, 200.0f);
		else
			self->scroll = CLAMP (self->scroll - lrg_ui_event_get_scroll_y (event) * 8.0 / MAX (self->ppb / 8.0f, 1.0f),
			                      0.0, view_beats (self));
		return TRUE;
	}
	if (type == LRG_UI_EVENT_MOUSE_BUTTON_DOWN)
	{
		g_autofree gchar *track = NULL;
		gint              clip = -1;
		gboolean          edge = FALSE;

		/* the ruler seeks */
		if (py < y0 + RULER_H && px >= x0 + HEAD_W)
		{
			seek (self, floor (x_beat (self, x0, px)));
			return TRUE;
		}
		if (!hit_clip (self, x0, y0, px, py, &track, &clip, &edge))
			return FALSE;
		if (lrg_ui_event_get_button (event) == 1 && clip >= 0)
		{
			/* the right button deletes */
			g_autofree gchar *jt = jstr (track);
			g_autofree gchar *ji = g_strdup_printf ("%d", clip);

			if (!run_op_v (self, "Song: remove clip", 0, "remove_clip", &error, "track", jt, "index", ji, NULL))
				report (self, error);
			lrg_editor_song_editor_select (self, track, -1);
			return TRUE;
		}
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		lrg_editor_song_editor_select (self, track, clip);
		if (clip >= 0)
		{
			const gchar *source = NULL;

			self->drag = edge ? DRAG_RESIZE : DRAG_MOVE;
			g_free (self->drag_track);
			self->drag_track = g_strdup (track);
			self->drag_clip = clip;
			self->drag_beat0 = x_beat (self, x0, px);
			lrg_song_get_clip (self->song, track, (guint) clip, &source, &self->drag_start0, &self->drag_len0);
			self->gesture = lrg_editor_data_editor_new_gesture (LRG_EDITOR_DATA_EDITOR (self));
		}
		return TRUE;
	}
	if (type == LRG_UI_EVENT_MOUSE_MOVE && (self->drag == DRAG_MOVE || self->drag == DRAG_RESIZE))
	{
		/* snapped to beats */
		gdouble           delta = round (x_beat (self, x0, px) - self->drag_beat0);
		g_autofree gchar *jt = jstr (self->drag_track);
		g_autofree gchar *ji = g_strdup_printf ("%d", self->drag_clip);
		g_autofree gchar *jv = NULL;

		if (self->drag == DRAG_MOVE)
		{
			jv = jnum (MAX (self->drag_start0 + delta, 0.0));
			if (!run_op_v (self, "Song: move clip", self->gesture, "set_clip", &error, "track", jt, "index", ji, "start",
			               jv, NULL))
				report (self, error);
			/* the clips stay sorted: follow this one */
			self->drag_clip = lrg_song_find_clip (self->song, self->drag_track, MAX (self->drag_start0 + delta, 0.0) + 1e-6);
			self->sel_clip = self->drag_clip;
		}
		else
		{
			jv = jnum (MAX (self->drag_len0 + delta, 1.0));
			if (!run_op_v (self, "Song: resize clip", self->gesture, "set_clip", &error, "track", jt, "index", ji,
			               "length", jv, NULL))
				report (self, error);
		}
		return TRUE;
	}
	if (type == LRG_UI_EVENT_MOUSE_BUTTON_UP && self->drag != DRAG_NONE && self->drag != DRAG_NOTE)
	{
		self->drag = DRAG_NONE;
		self->gesture = 0;
		return TRUE;
	}
	if (type == LRG_UI_EVENT_KEY_DOWN && lrg_ui_event_get_key (event) == GRL_KEY_DELETE && self->sel_track != NULL
	    && self->sel_clip >= 0)
	{
		g_autofree gchar *jt = jstr (self->sel_track);
		g_autofree gchar *ji = g_strdup_printf ("%d", self->sel_clip);

		if (!run_op_v (self, "Song: remove clip", 0, "remove_clip", &error, "track", jt, "index", ji, NULL))
			report (self, error);
		self->sel_clip = -1;
		return TRUE;
	}
	return FALSE;
}

/* ==========================================================================
 * The pattern editor: step grid (lanes) or piano roll (notes)
 * ========================================================================== */

/* the pattern of the selected clip, or NULL */
static const gchar *
sel_pattern (LrgEditorSongEditor *self)
{
	const gchar *source = NULL;
	gdouble      start, len;

	if (self->song == NULL || self->sel_track == NULL || self->sel_clip < 0
	    || lrg_song_get_track_kind (self->song, self->sel_track) != LRG_SONG_TRACK_INSTRUMENT
	    || !lrg_song_get_clip (self->song, self->sel_track, (guint) self->sel_clip, &source, &start, &len))
		return NULL;
	return lrg_song_get_pattern_length (self->song, source) > 0.0 ? source : NULL;
}

static gboolean
has_lanes (LrgEditorSongEditor *self,
           const gchar         *pattern)
{
	g_auto(GStrv) lanes = lrg_song_pattern_list_lanes (self->song, pattern);

	return lanes[0] != NULL;
}

/* the key range the piano roll shows: the notes and some room, two octaves at least */
static void
roll_range (LrgEditorSongEditor *self,
            const gchar         *pattern,
            gint                *lo,
            gint                *hi)
{
	guint n = lrg_song_pattern_get_n_notes (self->song, pattern), i;
	gint  a = 127, b = 0;

	for (i = 0; i < n; i++)
	{
		gdouble beat, len, vel;
		gint    key = 60;

		lrg_song_pattern_get_note (self->song, pattern, i, &beat, &key, &len, &vel);
		a = MIN (a, key);
		b = MAX (b, key);
	}
	if (n == 0)
	{
		a = 48;
		b = 72;
	}
	a = MAX (a - 3, 0);
	b = MIN (b + 3, 127);
	if (b - a < 24)
	{
		gint grow = (24 - (b - a)) / 2;

		a = MAX (a - grow, 0);
		b = MIN (a + 24, 127);
	}
	*lo = a;
	*hi = b;
}

static void
pattern_measure (LrgCustomWidget     *widget,
                 gfloat               avail_width,
                 gfloat               avail_height,
                 LrgCustomWidgetSize *size,
                 gpointer             user_data)
{
	size->width = 480.0f;
	size->height = 240.0f;
}

static void
pattern_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (user_data);
	LrgTheme            *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont             *font = lrg_theme_get_ui_font (theme);
	gfloat               fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor      *text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	const GrlColor      *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	const GrlColor      *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_LINK);
	GrlColor             bg = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);
	GrlColor             cell = *accent;
	const gchar         *pattern = sel_pattern (self);
	gdouble              len;
	guint                steps, i, j;
	gfloat               gx = x + KEYS_W;
	gfloat               gw = MAX (width - KEYS_W - 4.0f, 1.0f);

	bg.a = (guint8) (bg.a / 2);
	lrg_paint_list_rect (list, x, y, width, height, 4.0f, &bg);
	if (pattern == NULL)
	{
		lrg_paint_list_text (list, font, "Select a pattern clip to edit its notes or steps", x + 10.0f, y + 10.0f, fs,
		                     0.0f, muted);
		return;
	}
	len = lrg_song_get_pattern_length (self->song, pattern);
	steps = MAX (lrg_song_pattern_get_steps (self->song, pattern), 1u);
	if (has_lanes (self, pattern))
	{
		/* step grid: a row per lane, a cell per step, brightness = velocity */
		g_auto(GStrv) lanes = lrg_song_pattern_list_lanes (self->song, pattern);
		gfloat        cw = gw / (gfloat) steps;

		for (i = 0; lanes[i] != NULL; i++)
		{
			const gchar *s = lrg_song_pattern_get_lane (self->song, pattern, lanes[i]);
			gfloat       ry = y + 4.0f + (gfloat) i * LANE_H;
			guint        k = 0;

			lrg_paint_list_text (list, font, lanes[i], x + 6.0f, roundf (ry + (LANE_H - fs) * 0.5f), fs, 0.0f, text);
			for (j = 0; j < steps; j++)
			{
				gchar    c = '.';
				GrlColor col = bg;

				/* the j-th step character, skipping separators */
				while (s != NULL && s[k] != '\0' && (s[k] == ' ' || s[k] == '|'))
					k++;
				if (s != NULL && s[k] != '\0')
					c = s[k++];
				col.a = (guint8) MIN (255, bg.a * (j % 4 == 0 ? 3 : 2));
				if (c == 'g' || c == 'o' || c == 'x' || c == 'X')
				{
					col = cell;
					col.a = c == 'X' ? 255 : c == 'x' ? 200 : c == 'o' ? 140 : 80;
				}
				lrg_paint_list_rect (list, gx + (gfloat) j * cw + 1.0f, ry + 2.0f, MAX (cw - 2.0f, 1.0f), LANE_H - 4.0f,
				                     2.0f, &col);
			}
		}
		return;
	}
	{
		/* piano roll: a row per key, notes as bars */
		gint   lo = 0, hi = 0, key;
		guint  n = lrg_song_pattern_get_n_notes (self->song, pattern);
		gfloat rows;
		gfloat rh;
		gfloat bw = gw / (gfloat) len;

		roll_range (self, pattern, &lo, &hi);
		rows = (gfloat) (hi - lo + 1);
		rh = MAX (MIN (ROLL_ROW_H, (height - 8.0f) / rows), 4.0f);
		for (key = hi; key >= lo; key--)
		{
			gfloat           ry = y + 4.0f + (gfloat) (hi - key) * rh;
			gint             pc = key % 12;
			gboolean         black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
			GrlColor         row = bg;

			row.a = (guint8) MIN (255, bg.a * (black ? 1 : 2));
			lrg_paint_list_rect (list, gx, ry, gw, rh - 1.0f, 0.0f, &row);
			if (pc == 0 && rh >= 8.0f)
			{
				g_autofree gchar *name = lrg_music_key_to_name (key);

				lrg_paint_list_text (list, font, name, x + 6.0f, ry, MIN (fs, rh + 2.0f), 0.0f, muted);
			}
		}
		for (j = 0; j <= steps; j++)
		{
			gfloat lx = gx + gw * (gfloat) j / (gfloat) steps;

			lrg_paint_list_line (list, lx, y + 4.0f, lx, y + 4.0f + rows * rh, 1.0f,
			                     j % 4 == 0 ? muted : &bg);
		}
		for (i = 0; i < n; i++)
		{
			gdouble  beat = 0, nlen = 0, vel = 0;
			GrlColor col = cell;

			lrg_song_pattern_get_note (self->song, pattern, i, &beat, &key, &nlen, &vel);
			if (key < lo || key > hi)
				continue;
			col.a = (guint8) (120 + 135 * CLAMP (vel, 0.0, 1.0));
			lrg_paint_list_rect (list, gx + (gfloat) beat * bw, y + 4.0f + (gfloat) (hi - key) * rh,
			                     MAX ((gfloat) nlen * bw - 1.0f, 2.0f), rh - 1.0f, 2.0f, &col);
		}
	}
}

/* the notes of @pattern as a JSON array, with note @skip left out and
 * (when @extra) one note added */
static JsonArray *
notes_json (LrgSong     *song,
            const gchar *pattern,
            gint         skip,
            gboolean     extra,
            gdouble      beat,
            gint         key,
            gdouble      len,
            gdouble      vel)
{
	JsonArray *a = json_array_new ();
	guint      n = lrg_song_pattern_get_n_notes (song, pattern), i;

	for (i = 0; i <= n; i++)
	{
		gdouble    b = beat, l = len, v = vel;
		gint       k = key;
		JsonArray *note;

		if (i == n && !extra)
			break;
		if (i < n)
		{
			if ((gint) i == skip)
				continue;
			lrg_song_pattern_get_note (song, pattern, i, &b, &k, &l, &v);
		}
		note = json_array_new ();
		json_array_add_double_element (note, b);
		json_array_add_int_element (note, k);
		json_array_add_double_element (note, l);
		json_array_add_double_element (note, v);
		json_array_add_array_element (a, note);
	}
	return a;
}

static gboolean
set_notes (LrgEditorSongEditor  *self,
           const gchar          *pattern,
           JsonArray            *notes,
           const gchar          *label,
           guint                 gesture,
           GError              **error)
{
	g_autoptr(JsonObject) args = json_object_new ();

	json_object_set_string_member (args, "pattern", pattern);
	json_object_set_array_member (args, "notes", notes);
	return run_op (self, label, gesture, "set_notes", args, NULL, error);
}

static gboolean
pattern_event (LrgCustomWidget  *widget,
               const LrgUIEvent *event,
               gpointer          user_data)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (user_data);
	LrgUIEventType       type = lrg_ui_event_get_event_type (event);
	gfloat               x = lrg_widget_get_world_x (LRG_WIDGET (widget));
	gfloat               y = lrg_widget_get_world_y (LRG_WIDGET (widget));
	gfloat               w = lrg_widget_get_width (LRG_WIDGET (widget));
	gfloat               h = lrg_widget_get_height (LRG_WIDGET (widget));
	gfloat               px = lrg_ui_event_get_x (event);
	gfloat               py = lrg_ui_event_get_y (event);
	gfloat               gx = x + KEYS_W;
	gfloat               gw = MAX (w - KEYS_W - 4.0f, 1.0f);
	const gchar         *pattern = sel_pattern (self);
	g_autoptr(GError)    error = NULL;
	gdouble              len;
	guint                steps;
	gint                 button;

	if (pattern == NULL)
		return FALSE;
	len = lrg_song_get_pattern_length (self->song, pattern);
	steps = MAX (lrg_song_pattern_get_steps (self->song, pattern), 1u);
	button = lrg_ui_event_get_button (event);

	if (has_lanes (self, pattern))
	{
		g_auto(GStrv)          lanes = NULL;
		g_autoptr(JsonObject)  args = NULL;
		JsonObject            *map;
		GString               *s;
		const gchar           *old;
		guint                  row, col, k, j;
		gchar                  c = '.';

		if (type != LRG_UI_EVENT_MOUSE_BUTTON_DOWN || px < gx || py < y + 4.0f)
			return FALSE;
		lanes = lrg_song_pattern_list_lanes (self->song, pattern);
		row = (guint) ((py - y - 4.0f) / LANE_H);
		col = (guint) ((px - gx) / (gw / (gfloat) steps));
		if (row >= g_strv_length (lanes) || col >= steps)
			return TRUE;

		/* the lane as plain steps, the clicked one cycled: . x X o g . */
		old = lrg_song_pattern_get_lane (self->song, pattern, lanes[row]);
		s = g_string_new (NULL);
		for (k = 0; old != NULL && old[k] != '\0'; k++)
			if (old[k] != ' ' && old[k] != '|')
				g_string_append_c (s, old[k] == '-' ? '.' : old[k]);
		while (s->len < steps)
			g_string_append_c (s, '.');
		c = s->str[col];
		if (button == 1)
			c = '.';
		else
			c = c == '.' ? 'x' : c == 'x' ? 'X' : c == 'X' ? 'o' : c == 'o' ? 'g' : '.';
		s->str[col] = c;
		/* written in groups of four for the file */
		{
			GString *grouped = g_string_new (NULL);

			for (j = 0; j < s->len; j++)
			{
				if (j > 0 && j % 4 == 0)
					g_string_append_c (grouped, ' ');
				g_string_append_c (grouped, s->str[j]);
			}
			g_string_free (s, TRUE);
			s = grouped;
		}
		args = json_object_new ();
		map = json_object_new ();
		json_object_set_string_member (map, lanes[row], s->str);
		json_object_set_string_member (args, "pattern", pattern);
		json_object_set_object_member (args, "lanes", map);
		g_string_free (s, TRUE);
		if (!run_op (self, "Song: step", 0, "set_lanes", args, NULL, &error))
			report (self, error);
		return TRUE;
	}

	/* piano roll */
	{
		gint    lo = 0, hi = 0, key, hit = -1;
		gfloat  rh;
		gdouble beat, step_len = len / (gdouble) steps;
		guint   n = lrg_song_pattern_get_n_notes (self->song, pattern), i;

		roll_range (self, pattern, &lo, &hi);
		rh = MAX (MIN (ROLL_ROW_H, (h - 8.0f) / (gfloat) (hi - lo + 1)), 4.0f);
		key = hi - (gint) floorf ((py - y - 4.0f) / rh);
		beat = (gdouble) ((px - gx) / gw) * len;

		if (type == LRG_UI_EVENT_MOUSE_BUTTON_DOWN)
		{
			if (px < gx || key < lo || key > hi || beat < 0.0 || beat >= len)
				return FALSE;
			for (i = 0; i < n; i++)
			{
				gdouble nb = 0, nl = 0, nv = 0;
				gint    nk = 0;

				lrg_song_pattern_get_note (self->song, pattern, i, &nb, &nk, &nl, &nv);
				if (nk == key && beat >= nb && beat < nb + nl)
					hit = (gint) i;
			}
			if (hit >= 0 && button == 1)
			{
				/* the right button deletes the note */
				if (!set_notes (self, pattern, notes_json (self->song, pattern, hit, FALSE, 0, 0, 0, 0), "Song: delete note",
				                0, &error))
					report (self, error);
				return TRUE;
			}
			if (button != 0)
				return FALSE;
			if (hit < 0)
			{
				/* a click on nothing adds a step-long note there */
				gdouble at = floor (beat / step_len) * step_len;

				if (!set_notes (self, pattern, notes_json (self->song, pattern, -1, TRUE, at, key, step_len, 0.8),
				                "Song: add note", 0, &error))
					report (self, error);
				if (self->player != NULL && lrg_song_player_has_output (self->player))
				{
					lrg_song_player_note_on (self->player, self->sel_track, key, 0.8);
					lrg_song_player_note_off (self->player, self->sel_track, key);
				}
				return TRUE;
			}
			/* a click on a note drags it */
			self->drag = DRAG_NOTE;
			self->drag_note = hit;
			self->drag_beat0 = beat;
			self->drag_key0 = key;
			self->gesture = lrg_editor_data_editor_new_gesture (LRG_EDITOR_DATA_EDITOR (self));
			return TRUE;
		}
		if (type == LRG_UI_EVENT_MOUSE_MOVE && self->drag == DRAG_NOTE)
		{
			gdouble nb = 0, nl = 0, nv = 0;
			gint    nk = 0, dk = key - self->drag_key0;
			gdouble db = round ((beat - self->drag_beat0) / step_len) * step_len;

			if ((dk == 0 && fabs (db) < 1e-9) || self->drag_note < 0 || (guint) self->drag_note >= n)
				return TRUE;
			lrg_song_pattern_get_note (self->song, pattern, (guint) self->drag_note, &nb, &nk, &nl, &nv);
			nb = CLAMP (nb + db, 0.0, len - step_len);
			nk = CLAMP (nk + dk, 0, 127);
			if (!set_notes (self, pattern, notes_json (self->song, pattern, self->drag_note, TRUE, nb, nk, nl, nv),
			                "Song: move note", self->gesture, &error))
			{
				report (self, error);
				return TRUE;
			}
			/* the moved note is now the last one */
			self->drag_note = (gint) lrg_song_pattern_get_n_notes (self->song, pattern) - 1;
			self->drag_beat0 += db;
			self->drag_key0 += dk;
			return TRUE;
		}
		if (type == LRG_UI_EVENT_MOUSE_BUTTON_UP && self->drag == DRAG_NOTE)
		{
			self->drag = DRAG_NONE;
			self->gesture = 0;
			return TRUE;
		}
	}
	return FALSE;
}

/* ==========================================================================
 * Toolbar
 * ========================================================================== */

static void
on_play_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (user_data);
	g_autoptr(GError)    error = NULL;

	if (self->player != NULL && lrg_song_player_has_output (self->player) && lrg_song_player_is_playing (self->player))
		lrg_editor_song_editor_stop (self);
	else if (!lrg_editor_song_editor_play (self, -1.0, &error))
		report (self, error);
}

static void
on_stop_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (user_data);

	lrg_editor_song_editor_stop (self);
	seek (self, 0.0);
}

/* a new id: @base, or @base-2, @base-3 ... whichever is free */
static gchar *
free_id (gchar      **taken,
         const gchar *base)
{
	guint n;

	if (!g_strv_contains ((const gchar * const *) taken, base))
		return g_strdup (base);
	for (n = 2;; n++)
	{
		g_autofree gchar *id = g_strdup_printf ("%s-%u", base, n);

		if (!g_strv_contains ((const gchar * const *) taken, id))
			return g_steal_pointer (&id);
	}
}

static void
on_track_action (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
	LrgEditorSongEditor   *self = LRG_EDITOR_SONG_EDITOR (user_data);
	const gchar           *kind = g_variant_get_string (parameter, NULL);
	g_autoptr(GError)      error = NULL;
	g_autoptr(GObject)     obj = NULL;
	g_auto(GStrv)          tracks = NULL;
	g_auto(GStrv)          instruments = NULL;
	g_autofree gchar      *id = NULL;
	g_autoptr(JsonObject)  ins = json_object_new ();
	g_autoptr(JsonObject)  trk = json_object_new ();
	g_autoptr(JsonObject)  result = json_object_new ();
	gboolean               audio = g_str_equal (kind, "audio");

	if (self->song == NULL)
		return;
	tracks = lrg_song_list_tracks (self->song);
	instruments = lrg_song_list_instruments (self->song);
	id = free_id (tracks, audio ? "audio" : g_str_equal (kind, "drums") ? "drums" : "synth");
	obj = lrg_editor_document_dup_object (doc_of (self), &error);
	if (obj == NULL)
	{
		report (self, error);
		return;
	}
	json_object_set_string_member (trk, "id", id);
	if (!audio)
	{
		g_autofree gchar *iid = free_id (instruments, id);

		json_object_set_string_member (ins, "id", iid);
		json_object_set_string_member (ins, "type", g_str_equal (kind, "drums") ? "drums" : "synth");
		json_object_set_string_member (ins, "source", g_str_equal (kind, "drums") ? "808" : kind);
		if (!lrg_editor_song_op_find ("set_instrument") (LRG_SONG (obj), ins, result, &error))
		{
			report (self, error);
			return;
		}
		json_object_set_string_member (trk, "instrument", iid);
	}
	else
		json_object_set_string_member (trk, "kind", "audio");
	if (!lrg_editor_song_op_find ("add_track") (LRG_SONG (obj), trk, result, &error)
	    || !commit_song (self, obj, "Song: add track", 0, &error))
	{
		report (self, error);
		return;
	}
	lrg_editor_song_editor_select (self, id, -1);
}

static void
on_track_chip (LrgWidget *chip,
               gpointer   user_data)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (user_data);

	lrg_menu_popup_at_widget (self->track_menu, chip, LRG_POPUP_PLACEMENT_BELOW);
}

/* a new one-bar pattern at the playhead on the selected track (drum
 * tracks get kick, snare and hat lanes) */
static void
on_clip_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorSongEditor   *self = LRG_EDITOR_SONG_EDITOR (user_data);
	g_autoptr(GError)      error = NULL;
	g_autoptr(GObject)     obj = NULL;
	g_auto(GStrv)          patterns = NULL;
	g_autofree gchar      *id = NULL;
	g_autoptr(JsonObject)  pat = json_object_new ();
	g_autoptr(JsonObject)  clip = json_object_new ();
	g_autoptr(JsonObject)  result = json_object_new ();
	const gchar           *inst;
	const gchar           *type;
	gdouble                at;

	if (self->song == NULL || self->sel_track == NULL
	    || lrg_song_get_track_kind (self->song, self->sel_track) != LRG_SONG_TRACK_INSTRUMENT)
	{
		g_autoptr(GError) e = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                                           "Select an instrument track first");

		report (self, e);
		return;
	}
	inst = lrg_song_get_track_text (self->song, self->sel_track, "instrument");
	type = lrg_song_get_instrument_type (self->song, inst);
	patterns = lrg_song_list_patterns (self->song);
	id = free_id (patterns, self->sel_track);
	at = floor (lrg_editor_song_editor_get_beat (self) / bpb (self)) * bpb (self);
	obj = lrg_editor_document_dup_object (doc_of (self), &error);
	if (obj == NULL)
	{
		report (self, error);
		return;
	}
	json_object_set_string_member (pat, "id", id);
	json_object_set_double_member (pat, "length", bpb (self));
	if (g_strcmp0 (type, "drums") == 0)
	{
		JsonObject *lanes = json_object_new ();

		json_object_set_string_member (lanes, "kick", "x... x... x... x...");
		json_object_set_string_member (lanes, "snare", ".... x... .... x...");
		json_object_set_string_member (lanes, "closed-hat", "..x. ..x. ..x. ..x.");
		json_object_set_object_member (pat, "lanes", lanes);
	}
	json_object_set_string_member (clip, "track", self->sel_track);
	json_object_set_string_member (clip, "source", id);
	json_object_set_double_member (clip, "start", at);
	if (!lrg_editor_song_op_find ("add_pattern") (LRG_SONG (obj), pat, result, &error)
	    || !lrg_editor_song_op_find ("add_clip") (LRG_SONG (obj), clip, result, &error)
	    || !commit_song (self, obj, "Song: new clip", 0, &error))
	{
		report (self, error);
		return;
	}
	lrg_editor_song_editor_select (self, self->sel_track, lrg_song_find_clip (self->song, self->sel_track, at + 1e-6));
}

/* the selected clip again, right after itself */
static void
on_dup_chip (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (user_data);
	g_autoptr(GError)    error = NULL;
	const gchar         *source = NULL;
	gdouble              start = 0, len = 0;
	g_autofree gchar    *jt = NULL;
	g_autofree gchar    *js = NULL;
	g_autofree gchar    *jb = NULL;
	g_autofree gchar    *jl = NULL;

	if (self->song == NULL || self->sel_track == NULL || self->sel_clip < 0
	    || !lrg_song_get_clip (self->song, self->sel_track, (guint) self->sel_clip, &source, &start, &len))
		return;
	jt = jstr (self->sel_track);
	js = jstr (source);
	jb = jnum (start + len);
	jl = jnum (len);
	if (!run_op_v (self, "Song: duplicate clip", 0, "add_clip", &error, "track", jt, "source", js, "start", jb, "length",
	               jl, NULL))
	{
		report (self, error);
		return;
	}
	lrg_editor_song_editor_select (self, self->sel_track,
	                               lrg_song_find_clip (self->song, self->sel_track, start + len + 1e-6));
}

static void
on_delete_chip (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (user_data);
	g_autoptr(GError)    error = NULL;
	g_autofree gchar    *jt = NULL;
	g_autofree gchar    *ji = NULL;

	if (self->song == NULL || self->sel_track == NULL)
		return;
	jt = jstr (self->sel_track);
	if (self->sel_clip >= 0)
	{
		ji = g_strdup_printf ("%d", self->sel_clip);
		if (!run_op_v (self, "Song: remove clip", 0, "remove_clip", &error, "track", jt, "index", ji, NULL))
			report (self, error);
		self->sel_clip = -1;
		sync_views (self);
		return;
	}
	if (!run_op_v (self, "Song: remove track", 0, "remove_track", &error, "id", jt, NULL))
		report (self, error);
	lrg_editor_song_editor_select (self, NULL, -1);
}

/* exports next to the .rsong: the same name with .wav (song.export does
 * every other format) */
static void
on_export_chip (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (user_data);
	const gchar         *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	g_autoptr(GError)    error = NULL;
	g_autofree gchar    *stem = NULL;
	g_autofree gchar    *out = NULL;

	if (path == NULL)
		return;
	stem = g_str_has_suffix (path, ".rsong") ? g_strndup (path, strlen (path) - 6) : g_strdup (path);
	out = g_strconcat (stem, ".wav", NULL);
	if (!lrg_editor_song_editor_export (self, out, NULL, &error))
		report (self, error);
	else
		g_message ("Song: exported %s", out);
}

/* ==========================================================================
 * Sync
 * ========================================================================== */

static void
reparse (LrgEditorSongEditor *self)
{
	g_autoptr(GError) error = NULL;
	GObject          *obj = lrg_editor_document_dup_object (doc_of (self), &error);

	g_clear_object (&self->song);
	g_clear_pointer (&self->load_error, g_free);
	if (obj == NULL)
	{
		self->load_error = g_strdup (error != NULL ? error->message : "The document does not load");
		return;
	}
	self->song = LRG_SONG (obj);

	/* the live player follows every edit from the beat it is at */
	if (self->player != NULL && !lrg_song_player_set_song (self->player, self->song, &error))
		g_debug ("Song: the preview keeps the last good version: %s", error->message);
}

/* builds the selected track's instrument for its parameter list */
static void
sync_instrument (LrgEditorSongEditor *self)
{
	const gchar      *inst = NULL;
	g_autofree gchar *key = NULL;
	g_auto(GStrv)     names = NULL;
	GString          *k;
	guint             i;

	if (self->song != NULL && self->sel_track != NULL)
		inst = lrg_song_get_track_text (self->song, self->sel_track, "instrument");
	if (inst == NULL || lrg_song_get_instrument_type (self->song, inst) == NULL)
	{
		g_clear_object (&self->inst);
		g_clear_pointer (&self->inst_key, g_free);
		return;
	}
	/* the instrument changes with its type, source and overrides */
	k = g_string_new (NULL);
	g_string_append_printf (k, "%s|%s|%s", inst, lrg_song_get_instrument_type (self->song, inst),
	                        lrg_song_get_instrument_source (self->song, inst));
	names = lrg_song_list_instrument_params (self->song, inst);
	for (i = 0; names[i] != NULL; i++)
		g_string_append_printf (k, "|%s=%g", names[i], lrg_song_get_instrument_param (self->song, inst, names[i]));
	key = g_string_free (k, FALSE);
	if (g_strcmp0 (key, self->inst_key) == 0)
		return;
	g_clear_object (&self->inst);
	self->inst = lrg_song_create_instrument (self->song, inst, NULL);
	g_free (self->inst_key);
	self->inst_key = g_steal_pointer (&key);
}

static void
sync_views (LrgEditorSongEditor *self)
{
	g_autofree gchar *sig = NULL;
	guint             i;

	if (self->grid[PART_SONG] == NULL)
		return;
	if (self->song != NULL && self->sel_track != NULL
	    && lrg_song_get_track_text (self->song, self->sel_track, "bus") == NULL)
	{
		g_clear_pointer (&self->sel_track, g_free);
		self->sel_clip = -1;
	}
	if (self->song != NULL && self->sel_track != NULL
	    && self->sel_clip >= (gint) lrg_song_get_n_clips (self->song, self->sel_track))
		self->sel_clip = -1;
	sync_instrument (self);

	/* the grids rebuild when what they list changes, else refresh */
	sig = g_strdup_printf ("%s/%s/%p", self->sel_track != NULL ? self->sel_track : "",
	                       self->inst_key != NULL ? self->inst_key : "", (gpointer) self->inst);
	if (g_strcmp0 (sig, self->signature) != 0)
	{
		g_free (self->signature);
		self->signature = g_steal_pointer (&sig);
		for (i = 0; i < N_PARTS; i++)
		{
			lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self->src[i]), NULL);
			lrg_property_grid_rebuild (self->grid[i]);
		}
	}
	else if (self->gesture == 0)
		for (i = 0; i < N_PARTS; i++)
			lrg_property_grid_refresh (self->grid[i]);
	lrg_widget_set_visible (LRG_WIDGET (self->grid[PART_TRACK]), self->sel_track != NULL);
	lrg_widget_queue_layout (self->arrange);
	lrg_widget_queue_layout (self->pattern);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
song_refresh (LrgEditorDataEditor *editor,
              const gchar         *path)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (editor);

	reparse (self);
	sync_views (self);
}

static gchar *
song_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (editor);

	if (self->song == NULL)
		return g_strdup ("not loaded");
	return g_strdup_printf ("%u tracks - %.1f s - track %s clip %d - beat %.2f", n_tracks (self),
	                        lrg_song_get_duration (self->song), self->sel_track != NULL ? self->sel_track : "-",
	                        self->sel_clip, lrg_editor_song_editor_get_beat (self));
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static gpointer
add_to (gpointer box,
        gpointer child)
{
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (child));
	g_object_unref (child);
	return child;
}

static LrgPropertyGrid *
make_grid (LrgEditorSongEditor *self,
           const gchar         *name,
           gint                 part)
{
	LrgPropertyGrid  *grid = lrg_property_grid_new ();
	EditorSongSource *src = g_object_new (EDITOR_TYPE_SONG_SOURCE, NULL);

	g_set_weak_pointer (&src->editor, self);
	src->part = part;
	self->src[part] = G_OBJECT (src);
	lrg_widget_set_name (LRG_WIDGET (grid), name);
	lrg_property_grid_set_label_width (grid, 110.0f);
	g_signal_connect (grid, "edit-begin", G_CALLBACK (on_edit_begin), self);
	g_signal_connect (grid, "edit-end", G_CALLBACK (on_edit_end), self);
	lrg_property_grid_set_source (grid, LRG_PROPERTY_SOURCE (src));
	self->grid[part] = grid;
	return grid;
}

static void
tool (LrgEditorSongEditor *self,
      const gchar         *name,
      LrgIcon              icon,
      const gchar         *label,
      EditorChipFunc       func)
{
	LrgWidget *chip = editor_chip_new (name, icon, label, func, self);

	lrg_editor_data_editor_add_tool (LRG_EDITOR_DATA_EDITOR (self), chip);
	g_object_unref (chip);
}

static void
lrg_editor_song_editor_constructed (GObject *object)
{
	static const LrgCustomWidgetFuncs arrange_funcs = { arrange_measure, arrange_paint, arrange_step, arrange_event,
	                                                    { NULL } };
	static const LrgCustomWidgetFuncs pattern_funcs = { pattern_measure, pattern_paint, NULL, pattern_event, { NULL } };
	static const GActionEntry         entries[] = {
		{ "add-track", on_track_action, "s", NULL, NULL, { 0 } },
	};
	LrgEditorSongEditor           *self = LRG_EDITOR_SONG_EDITOR (object);
	const LrgInsets                pad = { 8.0f, 6.0f, 8.0f, 6.0f };
	g_autoptr(GSimpleActionGroup)  group = g_simple_action_group_new ();
	g_autoptr(GMenu)               menu = g_menu_new ();
	LrgWidget                     *left, *right, *scroll;
	LrgSplitter                   *split;
	LrgEditorApp                  *app;

	G_OBJECT_CLASS (lrg_editor_song_editor_parent_class)->constructed (object);

	/* the toolbar and the add-track menu (a few synth presets, drums, audio) */
	g_action_map_add_action_entries (G_ACTION_MAP (group), entries, G_N_ELEMENTS (entries), self);
	lrg_widget_insert_action_group (LRG_WIDGET (self), "song", G_ACTION_GROUP (group));
	g_menu_append (menu, "Drum machine", "song.add-track::drums");
	g_menu_append (menu, "Bass synth", "song.add-track::synthwave-bass");
	g_menu_append (menu, "Pad synth", "song.add-track::synthwave-pad");
	g_menu_append (menu, "Lead synth", "song.add-track::synthwave-lead");
	g_menu_append (menu, "Pluck synth", "song.add-track::arp-pluck");
	g_menu_append (menu, "Init synth", "song.add-track::init");
	g_menu_append (menu, "Audio track", "song.add-track::audio");
	self->track_menu = lrg_menu_new_from_model (G_MENU_MODEL (menu));
	tool (self, "song-play", LRG_ICON_PLAY, "Play / pause", on_play_chip);
	tool (self, "song-stop", LRG_ICON_STOP, "Stop", on_stop_chip);
	tool (self, "song-add-track", LRG_ICON_PLUS, "Track", on_track_chip);
	tool (self, "song-add-clip", LRG_ICON_FILE, "New clip", on_clip_chip);
	tool (self, "song-dup-clip", LRG_ICON_REFRESH, "Duplicate", on_dup_chip);
	tool (self, "song-delete", LRG_ICON_TRASH, "Delete", on_delete_chip);
	tool (self, "song-export", LRG_ICON_DOWNLOAD, "Export WAV", on_export_chip);

	/* left: song settings, the selected track */
	left = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (left, "song-form");
	lrg_container_set_spacing (LRG_CONTAINER (left), 6.0f);
	lrg_widget_set_margin (left, &pad);
	add_to (left, make_grid (self, "song-settings-grid", PART_SONG));
	add_to (left, make_grid (self, "song-track-grid", PART_TRACK));
	scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (scroll, "song-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (scroll), left);
	g_object_unref (left);

	/* right: the arrangement over the pattern editor */
	right = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (right, "song-views");
	lrg_container_set_spacing (LRG_CONTAINER (right), 6.0f);
	lrg_widget_set_margin (right, &pad);
	self->arrange = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->arrange, "song-arrangement");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->arrange), &arrange_funcs, self, NULL);
	lrg_widget_set_fill_weight (self->arrange, 1.0f);
	add_to (right, self->arrange);
	self->pattern = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->pattern, "song-pattern");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->pattern), &pattern_funcs, self, NULL);
	lrg_widget_set_fill_weight (self->pattern, 1.0f);
	add_to (right, self->pattern);

	split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_splitter_set_start_child (split, scroll);
	g_object_unref (scroll);
	lrg_splitter_set_end_child (split, right);
	g_object_unref (right);
	lrg_splitter_set_default_fraction (split, 0.28f);
	lrg_splitter_set_fraction (split, 0.28f, FALSE);
	lrg_editor_data_editor_set_content (LRG_EDITOR_DATA_EDITOR (self), LRG_WIDGET (split));

	reparse (self);
	if (self->song != NULL)
	{
		g_auto(GStrv) ids = lrg_song_list_tracks (self->song);

		if (ids[0] != NULL)
			self->sel_track = g_strdup (ids[0]);
	}
	sync_views (self);
	app = lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
	if (app != NULL)
		lrg_editor_song_editor_install (app);
}

static void
lrg_editor_song_editor_dispose (GObject *object)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (object);
	guint                i;

	if (self->track_menu != NULL)
		lrg_menu_close (self->track_menu);
	g_clear_object (&self->track_menu);
	if (self->player != NULL)
		lrg_song_player_stop_output (self->player);
	g_clear_object (&self->player);
	g_clear_object (&self->inst);
	g_clear_object (&self->song);
	for (i = 0; i < N_PARTS; i++)
		g_clear_object (&self->src[i]);
	G_OBJECT_CLASS (lrg_editor_song_editor_parent_class)->dispose (object);
}

static void
lrg_editor_song_editor_finalize (GObject *object)
{
	LrgEditorSongEditor *self = LRG_EDITOR_SONG_EDITOR (object);

	g_free (self->signature);
	g_free (self->load_error);
	g_free (self->sel_track);
	g_free (self->inst_key);
	g_free (self->drag_track);
	G_OBJECT_CLASS (lrg_editor_song_editor_parent_class)->finalize (object);
}

static void
lrg_editor_song_editor_class_init (LrgEditorSongEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_song_editor_constructed;
	object_class->dispose = lrg_editor_song_editor_dispose;
	object_class->finalize = lrg_editor_song_editor_finalize;
	data_class->refresh = song_refresh;
	data_class->get_state = song_get_state;
}

static void
lrg_editor_song_editor_init (LrgEditorSongEditor *self)
{
	self->sel_clip = -1;
	self->ppb = 0.0f;    /* fitted on the first paint */
	self->drag_note = -1;
}

LrgEditorSongEditor *
lrg_editor_song_editor_new (LrgEditorApp      *app,
                            LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_SONG_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorSongEditor *
lrg_editor_song_editor_open (LrgEditorApp  *app,
                             const gchar   *path,
                             GError       **error)
{
	LrgEditorDataEditor *editor;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	editor = lrg_editor_data_editor_open (app, path, error);
	if (editor == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_SONG_EDITOR (editor))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "%s is not a .rsong song", path);
		return NULL;
	}
	return LRG_EDITOR_SONG_EDITOR (editor);
}

gboolean
lrg_editor_song_editor_create (LrgEditorApp  *app,
                               const gchar   *path,
                               gboolean       empty,
                               GError       **error)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autofree gchar            *abs = NULL;
	g_autofree gchar            *dir = NULL;

	g_return_val_if_fail (path != NULL, FALSE);
	abs = music_abs_path (app, path);
	if (g_file_test (abs, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s already exists", abs);
		return FALSE;
	}
	dir = g_path_get_dirname (abs);
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Could not create %s", dir);
		return FALSE;
	}
	doc = lrg_editor_document_new (LRG_EDITOR_DOCUMENT_FORMAT_SONG);
	if (empty)
	{
		g_autoptr(LrgSong) song = lrg_song_new ();

		if (!lrg_editor_document_commit_object (doc, G_OBJECT (song), "Empty", 0, error))
			return FALSE;
	}
	return lrg_editor_document_save_as (doc, abs, error);
}

/* ==========================================================================
 * IPC: song.*
 *
 * Every method takes an optional "path" (a .rsong file, opened when
 * needed); without it the focused song editor is used.  Every edit
 * method is one of the shared operations (lrg-editor-song-ops.c), one
 * undo step; song.apply runs a list of them as one.
 * ========================================================================== */

static LrgEditorSongEditor *
need_editor (LrgEditorApp  *app,
             JsonObject    *params,
             GError       **error)
{
	const gchar         *path = music_param_string (params, "path");
	LrgEditorDataEditor *editor;
	g_autoptr(GError)    err = NULL;

	if (path == NULL)
	{
		editor = lrg_editor_data_editor_find (app, NULL);
		if (!LRG_IS_EDITOR_SONG_EDITOR (editor))
			return music_missing (error, "No song is open");
		return LRG_EDITOR_SONG_EDITOR (editor);
	}
	editor = LRG_EDITOR_DATA_EDITOR (lrg_editor_song_editor_open (app, path, &err));
	if (editor == NULL)
		return music_fail (error, err);
	return LRG_EDITOR_SONG_EDITOR (editor);
}

static LrgEditorSongEditor *
need_song (LrgEditorApp  *app,
           JsonObject    *params,
           GError       **error)
{
	LrgEditorSongEditor *self = need_editor (app, params, error);

	if (self != NULL && self->song == NULL)
		return music_bad (error, self->load_error != NULL ? self->load_error : "The song does not load");
	return self;
}

/* the state an agent reads after a change: where things are and what plays */
static void
add_state (LrgEditorSongEditor *self,
           JsonObject          *o)
{
	json_object_set_double_member (o, "beat", lrg_editor_song_editor_get_beat (self));
	json_object_set_boolean_member (o, "playing", self->player != NULL && lrg_song_player_is_playing (self->player)
	                                              && lrg_song_player_has_output (self->player));
	json_object_set_string_member (o, "selected_track", self->sel_track);
	json_object_set_int_member (o, "selected_clip", self->sel_clip);
	if (self->song != NULL)
	{
		json_object_set_double_member (o, "length_beats", lrg_song_get_length_beats (self->song));
		json_object_set_double_member (o, "duration", lrg_song_get_duration (self->song));
	}
}

/* one method per operation: song.<op> */
static JsonNode *
m_op (LrgEditorServer  *server,
      JsonObject       *params,
      gpointer          user_data,
      GError          **error,
      const gchar      *name)
{
	LrgEditorSongEditor *self = need_song (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)    err = NULL;
	g_autofree gchar    *label = NULL;
	JsonObject          *result;

	if (self == NULL)
		return NULL;
	result = json_object_new ();
	label = g_strdup_printf ("Song: %s", name);
	if (!run_op (self, label, 0, name, params, result, &err))
	{
		json_object_unref (result);
		return music_fail (error, err);
	}
	add_state (self, result);
	return music_object_node (result);
}

#define OP_METHOD(op) \
	static JsonNode * \
	m_##op (LrgEditorServer  *server, \
	        JsonObject       *params, \
	        gpointer          user_data, \
	        GError          **error) \
	{ \
		return m_op (server, params, user_data, error, #op); \
	}

OP_METHOD (set)
OP_METHOD (set_tempo)
OP_METHOD (set_marker)
OP_METHOD (remove_marker)
OP_METHOD (set_instrument)
OP_METHOD (remove_instrument)
OP_METHOD (add_pattern)
OP_METHOD (set_notes)
OP_METHOD (set_lanes)
OP_METHOD (remove_pattern)
OP_METHOD (add_track)
OP_METHOD (set_track)
OP_METHOD (remove_track)
OP_METHOD (move_track)
OP_METHOD (add_clip)
OP_METHOD (set_clip)
OP_METHOD (remove_clip)
OP_METHOD (set_automation)
OP_METHOD (set_bus)
OP_METHOD (remove_bus)
OP_METHOD (set_effect)
OP_METHOD (remove_effect)
OP_METHOD (set_send)
OP_METHOD (set_duck)

static JsonNode *
m_new (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	const gchar       *path = music_param_string (params, "path");
	g_autoptr(GError)  err = NULL;
	gboolean           empty = FALSE;
	JsonObject        *o;

	if (path == NULL)
		return music_bad (error, "Missing string parameter 'path'");
	music_param_bool (params, "empty", &empty);
	if (!lrg_editor_song_editor_create (app, path, empty, &err))
		return music_fail (error, err);
	if (lrg_editor_song_editor_open (app, path, &err) == NULL)
		return music_fail (error, err);
	o = json_object_new ();
	json_object_set_string_member (o, "path", path);
	return music_object_node (o);
}

static JsonNode *
m_get (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorSongEditor *self = need_song (LRG_EDITOR_APP (user_data), params, error);
	JsonObject          *o;

	if (self == NULL)
		return NULL;
	o = lrg_editor_song_to_json (self->song, music_param_string (params, "section"));
	add_state (self, o);
	return music_object_node (o);
}

static JsonNode *
m_apply (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	LrgEditorSongEditor *self = need_song (LRG_EDITOR_APP (user_data), params, error);
	JsonArray           *ops = music_param_array (params, "ops");
	g_autoptr(GError)    err = NULL;
	g_autoptr(JsonNode)  wrap = NULL;
	g_autofree gchar    *text = NULL;
	g_autofree gchar    *results = NULL;
	JsonNode            *parsed;
	JsonObject          *o;

	if (self == NULL)
		return NULL;
	if (ops == NULL)
		return music_bad (error, "Missing array parameter 'ops' ([{\"op\": \"add_clip\", ...}, ...])");
	wrap = json_node_new (JSON_NODE_ARRAY);
	json_node_set_array (wrap, ops);
	text = json_to_string (wrap, FALSE);
	results = lrg_editor_song_editor_apply (self, text, music_param_string (params, "label"), &err);
	if (results == NULL)
		return music_fail (error, err);
	parsed = json_from_string (results, NULL);
	o = json_object_new ();
	if (parsed != NULL)
		json_object_set_member (o, "results", parsed);
	add_state (self, o);
	return music_object_node (o);
}

static JsonNode *
m_validate (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorSongEditor *self = need_song (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GPtrArray) issues = NULL;
	JsonArray           *list;
	JsonObject          *o;
	gboolean             ok;
	guint                i;

	if (self == NULL)
		return NULL;
	ok = lrg_song_validate (self->song, &issues);
	list = json_array_new ();
	for (i = 0; issues != NULL && i < issues->len; i++)
		json_array_add_string_element (list, g_ptr_array_index (issues, i));
	o = json_object_new ();
	json_object_set_boolean_member (o, "valid", ok);
	json_object_set_array_member (o, "issues", list);
	return music_object_node (o);
}

/* render options from IPC parameters: start/end beats, normalize
 * (none, peak, loudness), target, ceiling, sample_rate, mute, solo */
static LrgSongRenderOptions *
options_from (JsonObject  *params,
              gboolean     export_defaults,
              GError     **error)
{
	LrgSongRenderOptions *o = export_defaults ? default_options () : lrg_song_render_options_new ();
	const gchar          *norm = music_param_string (params, "normalize");
	JsonArray            *mute = music_param_array (params, "mute");
	JsonArray            *solo = music_param_array (params, "solo");
	gdouble               v;
	guint                 i;

	if (music_param_double (params, "start", &v))
		o->start_beat = MAX (v, 0.0);
	if (music_param_double (params, "end", &v))
		o->end_beat = v;
	if (music_param_double (params, "tail", &v))
		o->tail = v;
	if (music_param_double (params, "target", &v))
		o->target = v;
	if (music_param_double (params, "ceiling", &v))
		o->true_peak_ceiling = v;
	if (music_param_double (params, "sample_rate", &v))
		o->sample_rate = (guint) CLAMP (v, 0.0, 192000.0);
	if (norm != NULL)
	{
		if (g_str_equal (norm, "none"))
			o->normalize = LRG_SONG_NORMALIZE_NONE;
		else if (g_str_equal (norm, "peak"))
			o->normalize = LRG_SONG_NORMALIZE_PEAK;
		else if (g_str_equal (norm, "loudness"))
			o->normalize = LRG_SONG_NORMALIZE_LOUDNESS;
		else
		{
			lrg_song_render_options_free (o);
			return music_bad (error, "normalize must be none, peak or loudness");
		}
		if (o->normalize == LRG_SONG_NORMALIZE_PEAK && !music_param_double (params, "target", &v))
			o->target = -1.0;
	}
	if (mute != NULL)
	{
		o->mute = g_new0 (gchar *, json_array_get_length (mute) + 1);
		for (i = 0; i < json_array_get_length (mute); i++)
			o->mute[i] = g_strdup (json_array_get_string_element (mute, i));
	}
	if (solo != NULL)
	{
		o->solo = g_new0 (gchar *, json_array_get_length (solo) + 1);
		for (i = 0; i < json_array_get_length (solo); i++)
			o->solo[i] = g_strdup (json_array_get_string_element (solo, i));
	}
	return o;
}

/* analysis of the mix and of every track alone: mixing "without ears" */
static JsonNode *
m_render (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorSongEditor        *self = need_song (LRG_EDITOR_APP (user_data), params, error);
	LrgSongRenderOptions       *opts;
	g_autoptr(GError)           err = NULL;
	g_autoptr(LrgWaveData)      wave = NULL;
	g_autoptr(LrgAudioAnalysis) a = NULL;
	g_autofree gfloat          *pcm = NULL;
	gsize                       n = 0;
	gdouble                     bins = 64;
	gboolean                    tracks = FALSE;
	JsonObject                 *o;

	if (self == NULL)
		return NULL;
	opts = options_from (params, FALSE, error);
	if (opts == NULL)
		return NULL;
	wave = lrg_song_render (self->song, opts, NULL, NULL, NULL, &err);
	if (wave == NULL)
	{
		lrg_song_render_options_free (opts);
		return music_fail (error, err);
	}
	pcm = lrg_wave_data_get_samples (wave, &n);
	a = lrg_audio_analyze (pcm, n, 2, lrg_wave_data_get_sample_rate (wave));
	if (a == NULL)
	{
		lrg_song_render_options_free (opts);
		return music_bad (error, "The render could not be measured");
	}
	o = music_analysis_json (a);
	json_object_set_int_member (o, "frames", (gint64) (n / 2));
	json_object_set_int_member (o, "sample_rate", lrg_wave_data_get_sample_rate (wave));
	music_param_double (params, "bins", &bins);
	json_object_set_array_member (o, "envelope", music_envelope (pcm, n / 2, 2, (guint) CLAMP (bins, 1.0, 1024.0)));

	/* per track: loudness and peak of each stem */
	music_param_bool (params, "tracks", &tracks);
	if (tracks)
	{
		g_auto(GStrv)        names = NULL;
		g_autoptr(GPtrArray) stems = lrg_song_render_stems (self->song, opts, NULL, NULL, NULL, &names, &err);
		JsonObject          *per = json_object_new ();
		guint                i;

		for (i = 0; stems != NULL && i < stems->len; i++)
		{
			gsize                       m = 0;
			g_autofree gfloat          *s = lrg_wave_data_get_samples (g_ptr_array_index (stems, i), &m);
			g_autoptr(LrgAudioAnalysis) sa = lrg_audio_analyze (s, m, 2, lrg_wave_data_get_sample_rate (wave));

			if (sa != NULL)
				json_object_set_object_member (per, names[i], music_analysis_json (sa));
		}
		json_object_set_object_member (o, "tracks", per);
	}
	lrg_song_render_options_free (opts);
	return music_object_node (o);
}

/* the mixdown (and optionally stems) in any format: "file" or "formats" */
static JsonNode *
m_export (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp               *app = LRG_EDITOR_APP (user_data);
	LrgEditorSongEditor        *self = need_song (app, params, error);
	const gchar                *file = music_param_string (params, "file");
	LrgSongRenderOptions       *opts;
	LrgAudioEncodeOptions      *enc;
	g_autoptr(GError)           err = NULL;
	g_autoptr(LrgWaveData)      wave = NULL;
	g_autoptr(GPtrArray)        files = g_ptr_array_new_with_free_func (g_free);
	g_autofree gchar           *abs = NULL;
	g_autofree gchar           *stem = NULL;
	JsonArray                  *formats;
	JsonArray                  *written;
	JsonObject                 *o;
	gboolean                    stems = FALSE;
	gdouble                     v;
	guint                       i;

	if (self == NULL)
		return NULL;
	if (file == NULL)
		return music_bad (error, "Missing string parameter 'file' (its extension picks the format; with 'formats', "
		                  "the extension is replaced)");
	abs = music_abs_path (app, file);
	if (!music_ensure_parent (abs, &err))
		return music_fail (error, err);
	opts = options_from (params, TRUE, error);
	if (opts == NULL)
		return NULL;
	{
		const gchar *dot = strrchr (abs, '.');
		const gchar *slash = strrchr (abs, '/');

		stem = dot != NULL && (slash == NULL || dot > slash) ? g_strndup (abs, (gsize) (dot - abs)) : g_strdup (abs);
	}
	formats = music_param_array (params, "formats");
	if (formats == NULL)
		g_ptr_array_add (files, g_strdup (abs));
	else
		for (i = 0; i < json_array_get_length (formats); i++)
		{
			LrgAudioFileFormat f;
			const gchar       *name = json_array_get_string_element (formats, i);

			if (name == NULL || !lrg_audio_file_format_from_name (name, &f))
			{
				lrg_song_render_options_free (opts);
				return music_bad (error, "formats: unknown format (audio.formats lists them)");
			}
			g_ptr_array_add (files, g_strconcat (stem, ".", lrg_audio_file_format_get_extension (f), NULL));
		}

	wave = lrg_song_render (self->song, opts, NULL, NULL, NULL, &err);
	if (wave == NULL)
	{
		lrg_song_render_options_free (opts);
		return music_fail (error, err);
	}
	enc = tagged (self->song);
	if (music_param_double (params, "bitrate", &v))
		enc->bitrate_kbps = (guint) CLAMP (v, 8.0, 512.0);
	if (music_param_double (params, "bit_depth", &v))
		enc->bit_depth = (guint) v;
	written = json_array_new ();
	o = json_object_new ();
	for (i = 0; i < files->len; i++)
	{
		const gchar       *path = g_ptr_array_index (files, i);
		LrgAudioFileFormat f = LRG_AUDIO_FILE_FORMAT_WAV;
		JsonObject        *w = json_object_new ();
		GStatBuf           st;

		json_object_set_string_member (w, "file", path);
		if (!lrg_audio_file_format_from_path (path, &f))
			json_object_set_string_member (w, "error", "unknown extension");
		else if (!lrg_audio_encode_wave (wave, f, enc, path, NULL, &err))
		{
			json_object_set_string_member (w, "error", err->message);
			g_clear_error (&err);
		}
		else if (g_stat (path, &st) == 0)
			json_object_set_int_member (w, "bytes", st.st_size);
		json_array_add_object_element (written, w);
	}

	/* stems: <stem>.<track>.wav (lossless, for a DAW) */
	music_param_bool (params, "stems", &stems);
	if (stems)
	{
		g_auto(GStrv)        names = NULL;
		g_autoptr(GPtrArray) waves = lrg_song_render_stems (self->song, opts, NULL, NULL, NULL, &names, &err);

		if (waves == NULL)
		{
			json_object_set_string_member (o, "stems_error", err->message);
			g_clear_error (&err);
		}
		for (i = 0; waves != NULL && i < waves->len; i++)
		{
			g_autofree gchar *path = g_strdup_printf ("%s.%s.wav", stem, names[i]);
			JsonObject       *w = json_object_new ();

			json_object_set_string_member (w, "file", path);
			json_object_set_string_member (w, "stem", names[i]);
			if (!lrg_audio_encode_wave (g_ptr_array_index (waves, i), LRG_AUDIO_FILE_FORMAT_WAV, enc, path, NULL, &err))
			{
				json_object_set_string_member (w, "error", err->message);
				g_clear_error (&err);
			}
			json_array_add_object_element (written, w);
		}
	}
	lrg_audio_encode_options_free (enc);
	lrg_song_render_options_free (opts);
	json_object_set_array_member (o, "files", written);
	json_object_set_double_member (o, "duration", (gdouble) lrg_wave_data_get_frame_count (wave)
	                                              / lrg_wave_data_get_sample_rate (wave));
	return music_object_node (o);
}

static JsonNode *
m_export_midi (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorApp        *app = LRG_EDITOR_APP (user_data);
	LrgEditorSongEditor *self = need_song (app, params, error);
	const gchar         *file = music_param_string (params, "file");
	g_autofree gchar    *abs = NULL;
	g_autoptr(GError)    err = NULL;
	JsonObject          *o;

	if (self == NULL)
		return NULL;
	if (file == NULL)
		return music_bad (error, "Missing string parameter 'file'");
	abs = music_abs_path (app, file);
	if (!music_ensure_parent (abs, &err))
		return music_fail (error, err);
	if (!lrg_song_save_midi (self->song, abs, &err))
		return music_fail (error, err);
	o = json_object_new ();
	json_object_set_string_member (o, "file", abs);
	return music_object_node (o);
}

/* a .mid file as a new .rsong ("file" in, "path" out) */
static JsonNode *
m_import_midi (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorApp       *app = LRG_EDITOR_APP (user_data);
	const gchar        *file = music_param_string (params, "file");
	const gchar        *path = music_param_string (params, "path");
	g_autofree gchar   *in = NULL;
	g_autofree gchar   *out = NULL;
	g_autoptr(GError)   err = NULL;
	g_autoptr(LrgSong)  song = NULL;
	JsonObject         *o;

	if (file == NULL || path == NULL)
		return music_bad (error, "Missing string parameters 'file' (a .mid) and 'path' (the new .rsong)");
	in = music_abs_path (app, file);
	out = music_abs_path (app, path);
	if (g_file_test (out, G_FILE_TEST_EXISTS))
		return music_bad (error, "The .rsong already exists");
	song = lrg_song_new_from_midi_file (in, &err);
	if (song == NULL || !lrg_song_save_to_file (song, out, &err))
		return music_fail (error, err);
	if (lrg_editor_song_editor_open (app, out, &err) == NULL)
		return music_fail (error, err);
	o = json_object_new ();
	json_object_set_string_member (o, "path", out);
	return music_object_node (o);
}

static JsonNode *
m_play (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSongEditor *self = need_song (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)    err = NULL;
	gdouble              from = -1.0;
	const gchar         *marker = music_param_string (params, "marker");
	JsonObject          *o;

	if (self == NULL)
		return NULL;
	music_param_double (params, "beat", &from);
	if (marker != NULL)
	{
		from = lrg_song_find_marker (self->song, marker);
		if (from < 0.0)
			return music_bad (error, "No such marker");
	}
	if (!lrg_editor_song_editor_play (self, from, &err))
		return music_fail (error, err);
	o = json_object_new ();
	add_state (self, o);
	return music_object_node (o);
}

static JsonNode *
m_stop (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSongEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject          *o;

	if (self == NULL)
		return NULL;
	lrg_editor_song_editor_stop (self);
	o = json_object_new ();
	add_state (self, o);
	return music_object_node (o);
}

static JsonNode *
m_seek (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorSongEditor *self = need_song (LRG_EDITOR_APP (user_data), params, error);
	const gchar         *marker = music_param_string (params, "marker");
	gdouble              beat = -1.0;
	JsonObject          *o;

	if (self == NULL)
		return NULL;
	if (marker != NULL)
		beat = lrg_song_find_marker (self->song, marker);
	else
		music_param_double (params, "beat", &beat);
	if (beat < 0.0)
		return music_bad (error, "Give 'beat' (0 or more) or a known 'marker'");
	seek (self, beat);
	o = json_object_new ();
	add_state (self, o);
	return music_object_node (o);
}

static JsonNode *
m_select (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorSongEditor *self = need_song (LRG_EDITOR_APP (user_data), params, error);
	const gchar         *track = music_param_string (params, "track");
	gdouble              clip = -1;
	JsonObject          *o;

	if (self == NULL)
		return NULL;
	if (track != NULL && lrg_song_get_track_text (self->song, track, "bus") == NULL)
		return music_bad (error, "No such track");
	music_param_double (params, "clip", &clip);
	lrg_editor_song_editor_select (self, track, (gint) clip);
	o = json_object_new ();
	add_state (self, o);
	return music_object_node (o);
}

static JsonNode *
m_state (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	LrgEditorSongEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject          *o;
	gdouble              peak = 0, rms = 0;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	add_state (self, o);
	if (self->player != NULL && lrg_song_player_get_meter (self->player, "master", &peak, &rms))
	{
		json_object_set_double_member (o, "master_peak", peak);
		json_object_set_double_member (o, "master_rms", rms);
		json_object_set_int_member (o, "underruns", lrg_song_player_get_underruns (self->player));
	}
	return music_object_node (o);
}

static JsonNode *
m_instruments (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	JsonObject *o = lrg_editor_song_catalogue ();
	JsonArray  *ops = json_array_new ();
	const gchar * const *names = lrg_editor_song_op_names ();
	guint       i;

	for (i = 0; names[i] != NULL; i++)
		json_array_add_string_element (ops, names[i]);
	json_object_set_array_member (o, "ops", ops);
	return music_object_node (o);
}

static JsonNode *
m_formats (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	return music_object_node (lrg_editor_song_formats ());
}

void
lrg_editor_song_editor_install (LrgEditorApp *app)
{
	static const struct
	{
		const gchar           *name;
		LrgEditorServerMethod  func;
	} methods[] = {
		{ "song.new", m_new },
		{ "song.get", m_get },
		{ "song.apply", m_apply },
		{ "song.validate", m_validate },
		{ "song.render", m_render },
		{ "song.export", m_export },
		{ "song.export_midi", m_export_midi },
		{ "song.import_midi", m_import_midi },
		{ "song.play", m_play },
		{ "song.stop", m_stop },
		{ "song.seek", m_seek },
		{ "song.select", m_select },
		{ "song.state", m_state },
		{ "song.instruments", m_instruments },
		{ "audio.formats", m_formats },
		{ "song.set", m_set },
		{ "song.set_tempo", m_set_tempo },
		{ "song.set_marker", m_set_marker },
		{ "song.remove_marker", m_remove_marker },
		{ "song.set_instrument", m_set_instrument },
		{ "song.remove_instrument", m_remove_instrument },
		{ "song.add_pattern", m_add_pattern },
		{ "song.set_notes", m_set_notes },
		{ "song.set_lanes", m_set_lanes },
		{ "song.remove_pattern", m_remove_pattern },
		{ "song.add_track", m_add_track },
		{ "song.set_track", m_set_track },
		{ "song.remove_track", m_remove_track },
		{ "song.move_track", m_move_track },
		{ "song.add_clip", m_add_clip },
		{ "song.set_clip", m_set_clip },
		{ "song.remove_clip", m_remove_clip },
		{ "song.set_automation", m_set_automation },
		{ "song.set_bus", m_set_bus },
		{ "song.remove_bus", m_remove_bus },
		{ "song.set_effect", m_set_effect },
		{ "song.remove_effect", m_remove_effect },
		{ "song.set_send", m_set_send },
		{ "song.set_duck", m_set_duck },
	};
	LrgEditorServer *server;
	guint            i;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));
	if (g_object_get_data (G_OBJECT (app), INSTALL_KEY) != NULL)
		return;
	server = lrg_editor_app_get_server (app);
	if (server == NULL)
		return;
	g_object_set_data (G_OBJECT (app), INSTALL_KEY, GINT_TO_POINTER (1));
	lrg_editor_data_editor_install (app);
	for (i = 0; i < G_N_ELEMENTS (methods); i++)
		lrg_editor_server_register_method (server, methods[i].name, methods[i].func, app, NULL);
}
