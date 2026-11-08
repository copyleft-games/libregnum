/* lrg-editor-song-ops.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Song editing operations on JSON arguments (see
 * lrg-editor-song-ops-private.h and docs/editor/music.org).  Arguments
 * accept what an agent naturally writes: keys as note names ("A2") or
 * MIDI numbers, notes as [beat, key, length, velocity] arrays or
 * "beat key length velocity" strings, booleans or 0/1.
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "lrg-editor-song-ops-private.h"
#include "../../../music/lrg-synth.h"
#include "../../../music/lrg-drum-machine.h"
#include "../../../music/lrg-sampler.h"
#include "../../../music/lrg-sound-font-instrument.h"
#include "../../../music/lrg-foley-instrument.h"
#include "../../../audio/lrg-mixer-asset.h"
#include "../../../audio/lrg-audio-effect.h"
#include "../../../audio/lrg-audio-encoder.h"
#include "../../../audio/lrg-foley.h"

/* ==========================================================================
 * Argument helpers
 * ========================================================================== */

static gboolean
bad (GError      **error,
     const gchar  *format,
     ...) G_GNUC_PRINTF (2, 3);

static gboolean
bad (GError      **error,
     const gchar  *format,
     ...)
{
	va_list args;

	va_start (args, format);
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "");
	if (error != NULL && *error != NULL)
	{
		g_free ((*error)->message);
		(*error)->message = g_strdup_vprintf (format, args);
	}
	va_end (args);
	return FALSE;
}

static gboolean
missing (GError      **error,
         const gchar  *what,
         const gchar  *name)
{
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No %s '%s'", what, name != NULL ? name : "");
	return FALSE;
}

static const gchar *
str (JsonObject  *o,
     const gchar *name)
{
	JsonNode *n;

	if (o == NULL || !json_object_has_member (o, name))
		return NULL;
	n = json_object_get_member (o, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_STRING)
		return NULL;
	return json_node_get_string (n);
}

static gboolean
has (JsonObject  *o,
     const gchar *name)
{
	return o != NULL && json_object_has_member (o, name);
}

static gboolean
is_null (JsonObject  *o,
         const gchar *name)
{
	return has (o, name) && JSON_NODE_HOLDS_NULL (json_object_get_member (o, name));
}

/* a number from a JSON number, boolean or numeric string */
static gboolean
node_num (JsonNode *n,
          gdouble  *out)
{
	GType t;

	if (n == NULL || !JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	t = json_node_get_value_type (n);
	if (t == G_TYPE_DOUBLE)
		*out = json_node_get_double (n);
	else if (t == G_TYPE_INT64)
		*out = (gdouble) json_node_get_int (n);
	else if (t == G_TYPE_BOOLEAN)
		*out = json_node_get_boolean (n) ? 1.0 : 0.0;
	else if (t == G_TYPE_STRING)
	{
		gchar *end = NULL;

		*out = g_ascii_strtod (json_node_get_string (n), &end);
		if (end == json_node_get_string (n) || *end != '\0')
			return FALSE;
	}
	else
		return FALSE;
	return isfinite (*out);
}

static gboolean
num (JsonObject  *o,
     const gchar *name,
     gdouble     *out)
{
	return has (o, name) && node_num (json_object_get_member (o, name), out);
}

/* a key from a note name or a number */
static gint
node_key (JsonNode *n)
{
	gdouble v;

	if (n == NULL || !JSON_NODE_HOLDS_VALUE (n))
		return -1;
	if (json_node_get_value_type (n) == G_TYPE_STRING)
		return lrg_music_key_from_name (json_node_get_string (n));
	if (!node_num (n, &v) || v < 0 || v > 127)
		return -1;
	return (gint) round (v);
}

static JsonObject *
obj (JsonObject  *o,
     const gchar *name)
{
	JsonNode *n;

	if (!has (o, name))
		return NULL;
	n = json_object_get_member (o, name);
	return JSON_NODE_HOLDS_OBJECT (n) ? json_node_get_object (n) : NULL;
}

static JsonArray *
arr (JsonObject  *o,
     const gchar *name)
{
	JsonNode *n;

	if (!has (o, name))
		return NULL;
	n = json_object_get_member (o, name);
	return JSON_NODE_HOLDS_ARRAY (n) ? json_node_get_array (n) : NULL;
}

static gboolean
need_str (JsonObject   *o,
          const gchar  *name,
          const gchar **out,
          GError      **error)
{
	*out = str (o, name);
	if (*out == NULL || **out == '\0')
		return bad (error, "Missing string argument '%s'", name);
	return TRUE;
}

static gboolean
need_num (JsonObject  *o,
          const gchar *name,
          gdouble     *out,
          GError     **error)
{
	if (!num (o, name, out))
		return bad (error, "Missing number argument '%s'", name);
	return TRUE;
}

/* ==========================================================================
 * Settings, tempo, markers
 * ========================================================================== */

static gboolean
op_set (LrgSong     *song,
        JsonObject  *args,
        JsonObject  *result,
        GError     **error)
{
	static const struct { const gchar *key; const gchar *prop; } nums[] = {
		{ "bpm", "bpm" }, { "length", "length" }, { "loop_start", "loop-start" }, { "loop_end", "loop-end" },
	};
	static const struct { const gchar *key; const gchar *prop; guint min; guint max; } uints[] = {
		{ "beats_per_bar", "beats-per-bar", 1, 32 }, { "beat_unit", "beat-unit", 1, 16 },
		{ "sample_rate", "sample-rate", 8000, 192000 }, { "seed", "seed", 0, G_MAXUINT32 },
	};
	JsonObject *settings = obj (args, "settings") != NULL ? obj (args, "settings") : args;
	gdouble     v;
	guint       i;

	if (str (settings, "title") != NULL)
		g_object_set (song, "title", str (settings, "title"), NULL);
	if (str (settings, "artist") != NULL)
		g_object_set (song, "artist", str (settings, "artist"), NULL);
	for (i = 0; i < G_N_ELEMENTS (nums); i++)
		if (num (settings, nums[i].key, &v))
		{
			GParamSpecDouble *p = G_PARAM_SPEC_DOUBLE (g_object_class_find_property (G_OBJECT_GET_CLASS (song),
			                                                                          nums[i].prop));

			g_object_set (song, nums[i].prop, CLAMP (v, p->minimum, p->maximum), NULL);
		}
	for (i = 0; i < G_N_ELEMENTS (uints); i++)
		if (num (settings, uints[i].key, &v))
			g_object_set (song, uints[i].prop, (guint) CLAMP (round (v), (gdouble) uints[i].min, (gdouble) uints[i].max),
			              NULL);
	if (str (settings, "time_signature") != NULL)
	{
		guint n = 0, d = 0;

		if (sscanf (str (settings, "time_signature"), "%u/%u", &n, &d) != 2 || n < 1 || n > 32 || d < 1 || d > 16)
			return bad (error, "time_signature must look like 4/4");
		g_object_set (song, "beats-per-bar", n, "beat-unit", d, NULL);
	}
	(void) result;
	return TRUE;
}

/* "changes": [{beat, bpm, ramp}] replaces the tempo map after beat 0 */
static gboolean
op_set_tempo (LrgSong     *song,
              JsonObject  *args,
              JsonObject  *result,
              GError     **error)
{
	JsonArray *changes = arr (args, "changes");
	gdouble    bpm;
	guint      i;

	if (num (args, "bpm", &bpm))
		g_object_set (song, "bpm", CLAMP (bpm, 20.0, 400.0), NULL);
	if (changes == NULL)
		return TRUE;
	lrg_song_clear_tempo_changes (song);
	for (i = 0; i < json_array_get_length (changes); i++)
	{
		JsonNode   *n = json_array_get_element (changes, i);
		JsonObject *c = JSON_NODE_HOLDS_OBJECT (n) ? json_node_get_object (n) : NULL;
		gdouble     beat = 0, b = 0, ramp = 0;

		if (c == NULL || !num (c, "beat", &beat) || !num (c, "bpm", &b))
			return bad (error, "changes[%u] must be {beat, bpm, ramp}", i);
		num (c, "ramp", &ramp);
		if (!lrg_song_add_tempo_change (song, beat, b, ramp != 0.0))
			return bad (error, "changes[%u]: beat above 0 and bpm 20 .. 400", i);
	}
	(void) result;
	return TRUE;
}

static gboolean
op_set_marker (LrgSong     *song,
               JsonObject  *args,
               JsonObject  *result,
               GError     **error)
{
	const gchar *name;
	gdouble      beat;

	if (!need_str (args, "name", &name, error) || !need_num (args, "beat", &beat, error))
		return FALSE;
	if (!lrg_song_add_marker (song, beat, name))
		return bad (error, "A marker needs a beat of 0 or more");
	(void) result;
	return TRUE;
}

static gboolean
op_remove_marker (LrgSong     *song,
                  JsonObject  *args,
                  JsonObject  *result,
                  GError     **error)
{
	const gchar *name;

	if (!need_str (args, "name", &name, error))
		return FALSE;
	if (!lrg_song_remove_marker (song, name))
		return missing (error, "marker", name);
	(void) result;
	return TRUE;
}

/* ==========================================================================
 * Instruments
 * ========================================================================== */

/* "params": {name: value or null} overrides (null unsets) */
static gboolean
apply_params (LrgSong     *song,
              const gchar *id,
              JsonObject  *params,
              GError     **error)
{
	GList   *keys;
	GList   *l;
	gboolean ok = TRUE;

	if (params == NULL)
		return TRUE;
	keys = json_object_get_members (params);
	for (l = keys; l != NULL && ok; l = l->next)
	{
		const gchar *name = l->data;
		gdouble      v;

		if (is_null (params, name))
			lrg_song_unset_instrument_param (song, id, name);
		else if (!num (params, name, &v) || !lrg_song_set_instrument_param (song, id, name, v))
			ok = bad (error, "Instrument '%s' (%s) has no number parameter '%s'", id,
			          lrg_song_get_instrument_type (song, id), name);
	}
	g_list_free (keys);
	return ok;
}

static gboolean
op_set_instrument (LrgSong     *song,
                   JsonObject  *args,
                   JsonObject  *result,
                   GError     **error)
{
	const gchar *id;
	const gchar *type = str (args, "type");

	if (!need_str (args, "id", &id, error))
		return FALSE;
	if (type == NULL)
	{
		/* an existing instrument: parameters only */
		if (lrg_song_get_instrument_type (song, id) == NULL)
			return bad (error, "A new instrument needs a 'type' (synth, drums, sampler, soundfont, foley)");
	}
	else if (!lrg_song_set_instrument (song, id, type, str (args, "source")))
		return bad (error, "Instrument id '%s' or type '%s' is not valid (synth, drums, sampler, soundfont, foley)", id,
		            type);
	if (!apply_params (song, id, obj (args, "params"), error))
		return FALSE;
	(void) result;
	return TRUE;
}

static gboolean
op_remove_instrument (LrgSong     *song,
                      JsonObject  *args,
                      JsonObject  *result,
                      GError     **error)
{
	const gchar *id;

	if (!need_str (args, "id", &id, error))
		return FALSE;
	if (!lrg_song_remove_instrument (song, id))
		return missing (error, "instrument", id);
	(void) result;
	return TRUE;
}

/* ==========================================================================
 * Patterns
 * ========================================================================== */

/* one note from [beat, key, length, velocity?] or "beat key length vel" */
static gboolean
parse_note (JsonNode *n,
            gdouble  *beat,
            gint     *key,
            gdouble  *length,
            gdouble  *velocity)
{
	*velocity = 0.8;
	if (JSON_NODE_HOLDS_ARRAY (n))
	{
		JsonArray *a = json_node_get_array (n);
		guint      len = json_array_get_length (a);

		if (len < 3 || !node_num (json_array_get_element (a, 0), beat)
		    || (*key = node_key (json_array_get_element (a, 1))) < 0
		    || !node_num (json_array_get_element (a, 2), length))
			return FALSE;
		if (len >= 4 && !node_num (json_array_get_element (a, 3), velocity))
			return FALSE;
		return TRUE;
	}
	if (JSON_NODE_HOLDS_OBJECT (n))
	{
		JsonObject *o = json_node_get_object (n);

		if (!num (o, "beat", beat) || (*key = node_key (has (o, "key") ? json_object_get_member (o, "key") : NULL)) < 0
		    || !num (o, "length", length))
			return FALSE;
		num (o, "velocity", velocity);
		return TRUE;
	}
	if (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
	{
		g_auto(GStrv) f = g_strsplit_set (json_node_get_string (n), " \t", -1);
		const gchar  *p[4] = { NULL, NULL, NULL, NULL };
		guint         i, m = 0;
		gchar        *end = NULL;

		for (i = 0; f[i] != NULL; i++)
			if (*f[i] != '\0' && m < 4)
				p[m++] = f[i];
		if (m < 3)
			return FALSE;
		*beat = g_ascii_strtod (p[0], &end);
		if (end == p[0])
			return FALSE;
		*key = lrg_music_key_from_name (p[1]);
		*length = g_ascii_strtod (p[2], &end);
		if (end == p[2] || *key < 0)
			return FALSE;
		if (p[3] != NULL)
			*velocity = g_ascii_strtod (p[3], NULL);
		return TRUE;
	}
	return FALSE;
}

static gboolean
add_notes (LrgSong     *song,
           const gchar *pattern,
           JsonArray   *notes,
           GError     **error)
{
	guint i;

	for (i = 0; notes != NULL && i < json_array_get_length (notes); i++)
	{
		gdouble beat = 0, length = 0, velocity = 0.8;
		gint    key = -1;

		if (!parse_note (json_array_get_element (notes, i), &beat, &key, &length, &velocity)
		    || lrg_song_pattern_add_note (song, pattern, beat, key, length, velocity) < 0)
			return bad (error, "notes[%u] must be [beat, key, length, velocity] (key a note name like \"A2\" or 0..127, "
			            "length above 0, velocity 0..1)", i);
	}
	return TRUE;
}

static gboolean
set_lanes (LrgSong     *song,
           const gchar *pattern,
           JsonObject  *lanes,
           GError     **error)
{
	GList   *keys;
	GList   *l;
	gboolean ok = TRUE;

	if (lanes == NULL)
		return TRUE;
	keys = json_object_get_members (lanes);
	for (l = keys; l != NULL && ok; l = l->next)
	{
		const gchar *steps = is_null (lanes, l->data) ? NULL : str (lanes, l->data);

		if (!is_null (lanes, l->data) && steps == NULL)
			ok = bad (error, "lanes.%s must be a string of steps", (const gchar *) l->data);
		else if (!lrg_song_pattern_set_lane (song, pattern, l->data, steps) && steps != NULL)
			ok = bad (error, "lanes.%s: not a drum piece or note, or steps other than . - g o x X",
			          (const gchar *) l->data);
	}
	g_list_free (keys);
	return ok;
}

static gboolean
op_add_pattern (LrgSong     *song,
                JsonObject  *args,
                JsonObject  *result,
                GError     **error)
{
	const gchar *id;
	gdouble      length = 4, steps = 16;

	if (!need_str (args, "id", &id, error))
		return FALSE;
	num (args, "length", &length);
	if (!lrg_song_add_pattern (song, id, length))
		return bad (error, "Pattern id '%s' or length %g (0.25 .. 1024) is not valid", id, length);
	if (num (args, "steps", &steps) && !lrg_song_pattern_set_steps (song, id, (guint) CLAMP (round (steps), 1, 256)))
		return bad (error, "steps must be 1 .. 256");
	if (!add_notes (song, id, arr (args, "notes"), error) || !set_lanes (song, id, obj (args, "lanes"), error))
		return FALSE;
	(void) result;
	return TRUE;
}

/* "notes" replace the pattern's notes (or are added with "replace": false) */
static gboolean
op_set_notes (LrgSong     *song,
              JsonObject  *args,
              JsonObject  *result,
              GError     **error)
{
	const gchar *id;
	gdouble      replace = 1, length;

	if (!need_str (args, "pattern", &id, error))
		return FALSE;
	if (lrg_song_get_pattern_length (song, id) <= 0.0)
		return missing (error, "pattern", id);
	num (args, "replace", &replace);
	if (replace != 0.0)
		lrg_song_pattern_clear_notes (song, id);
	if (num (args, "length", &length))
		lrg_song_set_pattern_length (song, id, length);
	if (!add_notes (song, id, arr (args, "notes"), error))
		return FALSE;
	json_object_set_int_member (result, "notes", lrg_song_pattern_get_n_notes (song, id));
	return TRUE;
}

static gboolean
op_set_lanes (LrgSong     *song,
              JsonObject  *args,
              JsonObject  *result,
              GError     **error)
{
	const gchar *id;
	gdouble      steps;

	if (!need_str (args, "pattern", &id, error))
		return FALSE;
	if (lrg_song_get_pattern_length (song, id) <= 0.0)
		return missing (error, "pattern", id);
	if (num (args, "steps", &steps))
		lrg_song_pattern_set_steps (song, id, (guint) CLAMP (round (steps), 1, 256));
	(void) result;
	return set_lanes (song, id, obj (args, "lanes"), error);
}

static gboolean
op_remove_pattern (LrgSong     *song,
                   JsonObject  *args,
                   JsonObject  *result,
                   GError     **error)
{
	const gchar *id;

	if (!need_str (args, "id", &id, error))
		return FALSE;
	if (!lrg_song_remove_pattern (song, id))
		return missing (error, "pattern", id);
	(void) result;
	return TRUE;
}

/* ==========================================================================
 * Tracks and clips
 * ========================================================================== */

static gboolean
track_fields (LrgSong     *song,
              const gchar *id,
              JsonObject  *args,
              GError     **error)
{
	static const gchar *texts[] = { "name", "instrument", "bus" };
	static const gchar *nums[] = { "volume", "pan", "mute", "solo" };
	guint               i;
	gdouble             v;

	for (i = 0; i < G_N_ELEMENTS (texts); i++)
		if (str (args, texts[i]) != NULL && !lrg_song_set_track (song, id, texts[i], str (args, texts[i]), 0))
			return bad (error, "Track '%s': bad %s", id, texts[i]);
	for (i = 0; i < G_N_ELEMENTS (nums); i++)
		if (num (args, nums[i], &v))
			lrg_song_set_track (song, id, nums[i], NULL, v);
	return TRUE;
}

static gboolean
op_add_track (LrgSong     *song,
              JsonObject  *args,
              JsonObject  *result,
              GError     **error)
{
	const gchar     *id;
	const gchar     *kind = str (args, "kind");
	LrgSongTrackKind k = LRG_SONG_TRACK_INSTRUMENT;

	if (!need_str (args, "id", &id, error))
		return FALSE;
	if (g_strcmp0 (kind, "audio") == 0)
		k = LRG_SONG_TRACK_AUDIO;
	else if (kind != NULL && g_strcmp0 (kind, "instrument") != 0)
		return bad (error, "kind must be instrument or audio");
	if (!lrg_song_add_track (song, id, k, str (args, "instrument")))
		return bad (error, "Track id '%s' is not valid or already used", id);
	(void) result;
	return track_fields (song, id, args, error);
}

static gboolean
op_set_track (LrgSong     *song,
              JsonObject  *args,
              JsonObject  *result,
              GError     **error)
{
	const gchar *id;

	if (!need_str (args, "id", &id, error))
		return FALSE;
	if (lrg_song_get_track_text (song, id, "bus") == NULL)
		return missing (error, "track", id);
	(void) result;
	return track_fields (song, id, args, error);
}

static gboolean
op_remove_track (LrgSong     *song,
                 JsonObject  *args,
                 JsonObject  *result,
                 GError     **error)
{
	const gchar *id;

	if (!need_str (args, "id", &id, error))
		return FALSE;
	if (!lrg_song_remove_track (song, id))
		return missing (error, "track", id);
	(void) result;
	return TRUE;
}

static gboolean
op_move_track (LrgSong     *song,
               JsonObject  *args,
               JsonObject  *result,
               GError     **error)
{
	const gchar *id;
	gdouble      index;

	if (!need_str (args, "id", &id, error) || !need_num (args, "index", &index, error))
		return FALSE;
	if (index < 0 || !lrg_song_move_track (song, id, (guint) index))
		return bad (error, "No track '%s' or index %g out of range", id, index);
	(void) result;
	return TRUE;
}

static gboolean
clip_fields (LrgSong     *song,
             const gchar *track,
             guint        index,
             JsonObject  *args,
             GError     **error)
{
	static const gchar *fields[] = { "length", "offset", "transpose", "gain", "fade_in", "fade_out", "mute", "start" };
	guint               i;
	gdouble             v;

	if (str (args, "source") != NULL && !lrg_song_set_clip_source (song, track, index, str (args, "source")))
		return bad (error, "Bad clip source");
	for (i = 0; i < G_N_ELEMENTS (fields); i++)
		if (num (args, fields[i], &v) && !lrg_song_set_clip (song, track, index, fields[i], v))
			return bad (error, "Bad clip %s", fields[i]);
	return TRUE;
}

static gboolean
op_add_clip (LrgSong     *song,
             JsonObject  *args,
             JsonObject  *result,
             GError     **error)
{
	const gchar *track;
	const gchar *source = str (args, "source") != NULL ? str (args, "source")
	                      : str (args, "pattern") != NULL ? str (args, "pattern") : str (args, "file");
	gdouble      start, length = 0, end;
	gint         index;

	if (!need_str (args, "track", &track, error) || !need_num (args, "start", &start, error))
		return FALSE;
	if (source == NULL)
		return bad (error, "Missing 'source' (a pattern id, or a file on an audio track)");
	if (lrg_song_get_track_text (song, track, "bus") == NULL)
		return missing (error, "track", track);
	if (!num (args, "length", &length) && num (args, "end", &end))
		length = MAX (end - start, 0.0);
	index = lrg_song_add_clip (song, track, source, start, length);
	if (index < 0)
		return bad (error, "Bad clip on track '%s'", track);
	{
		/* the other fields, without moving it again */
		JsonObject *rest = json_object_new ();
		GList      *keys = json_object_get_members (args);
		GList      *l;
		gboolean    ok;

		for (l = keys; l != NULL; l = l->next)
			if (!g_str_equal (l->data, "start") && !g_str_equal (l->data, "length") && !g_str_equal (l->data, "source"))
				json_object_set_member (rest, l->data, json_node_copy (json_object_get_member (args, l->data)));
		g_list_free (keys);
		ok = clip_fields (song, track, (guint) index, rest, error);
		json_object_unref (rest);
		if (!ok)
			return FALSE;
	}
	json_object_set_int_member (result, "index", lrg_song_find_clip (song, track, start + 1e-9) >= 0
	                                             ? lrg_song_find_clip (song, track, start + 1e-9) : index);
	return TRUE;
}

static gboolean
op_set_clip (LrgSong     *song,
             JsonObject  *args,
             JsonObject  *result,
             GError     **error)
{
	const gchar *track;
	gdouble      index;

	if (!need_str (args, "track", &track, error) || !need_num (args, "index", &index, error))
		return FALSE;
	if (index < 0 || index >= lrg_song_get_n_clips (song, track))
		return bad (error, "No clip %g on track '%s'", index, track);
	(void) result;
	return clip_fields (song, track, (guint) index, args, error);
}

static gboolean
op_remove_clip (LrgSong     *song,
                JsonObject  *args,
                JsonObject  *result,
                GError     **error)
{
	const gchar *track;
	gdouble      index;

	if (!need_str (args, "track", &track, error) || !need_num (args, "index", &index, error))
		return FALSE;
	if (index < 0 || !lrg_song_remove_clip (song, track, (guint) index))
		return bad (error, "No clip %g on track '%s'", index, track);
	(void) result;
	return TRUE;
}

/* ==========================================================================
 * Automation
 * ========================================================================== */

static gboolean
op_set_automation (LrgSong     *song,
                   JsonObject  *args,
                   JsonObject  *result,
                   GError     **error)
{
	const gchar       *target;
	JsonArray         *points = arr (args, "points");
	g_autoptr(GArray)  values = g_array_new (FALSE, FALSE, sizeof (gdouble));
	gdouble            step = 0;
	guint              i;

	if (!need_str (args, "target", &target, error))
		return FALSE;
	num (args, "step", &step);
	for (i = 0; points != NULL && i < json_array_get_length (points); i++)
	{
		JsonNode *n = json_array_get_element (points, i);
		gdouble   b, v;

		if (JSON_NODE_HOLDS_ARRAY (n) && json_array_get_length (json_node_get_array (n)) == 2
		    && node_num (json_array_get_element (json_node_get_array (n), 0), &b)
		    && node_num (json_array_get_element (json_node_get_array (n), 1), &v))
		{
			g_array_append_val (values, b);
			g_array_append_val (values, v);
		}
		else
			return bad (error, "points[%u] must be [beat, value]", i);
	}
	if (!lrg_song_set_automation (song, str (args, "track"), target, (const gdouble *) values->data, values->len,
	                              step != 0.0))
		return bad (error, "Bad automation: track '%s' and target '%s' (track: volume, pan, param.<name>; song: "
		            "bus.<bus>.volume, effect.<bus>.<id>.<property>)", str (args, "track") != NULL ? str (args, "track")
		                                                                                            : "(song)", target);
	(void) result;
	return TRUE;
}

/* ==========================================================================
 * Mixer
 * ========================================================================== */

static gboolean
op_set_bus (LrgSong     *song,
            JsonObject  *args,
            JsonObject  *result,
            GError     **error)
{
	LrgMixerAsset *m = lrg_song_get_mixer (song);
	const gchar   *name;
	const gchar   *parent = str (args, "parent");
	gdouble        v;

	if (!need_str (args, "name", &name, error))
		return FALSE;
	if (!lrg_mixer_asset_has_bus (m, name) && !lrg_mixer_asset_add_bus (m, name, parent))
		return bad (error, "Cannot add bus '%s' (bad name or unknown parent)", name);
	if (parent != NULL && !lrg_mixer_asset_set_bus_parent (m, name, parent))
		return bad (error, "Cannot route bus '%s' into '%s'", name, parent);
	if (num (args, "volume", &v))
		lrg_mixer_asset_set_bus_volume (m, name, v);
	if (num (args, "mute", &v))
		lrg_mixer_asset_set_bus_mute (m, name, v != 0.0);
	if (num (args, "solo", &v))
		lrg_mixer_asset_set_bus_solo (m, name, v != 0.0);
	(void) result;
	return TRUE;
}

static gboolean
op_remove_bus (LrgSong     *song,
               JsonObject  *args,
               JsonObject  *result,
               GError     **error)
{
	const gchar *name;

	if (!need_str (args, "name", &name, error))
		return FALSE;
	if (!lrg_mixer_asset_remove_bus (lrg_song_get_mixer (song), name))
		return missing (error, "bus", name);
	(void) result;
	return TRUE;
}

static gboolean
op_set_effect (LrgSong     *song,
               JsonObject  *args,
               JsonObject  *result,
               GError     **error)
{
	LrgMixerAsset   *m = lrg_song_get_mixer (song);
	const gchar     *bus;
	const gchar     *id;
	const gchar     *type = str (args, "type");
	JsonObject      *params = obj (args, "params");
	g_auto(GStrv)    ids = NULL;
	gdouble          enabled;
	GList           *keys;
	GList           *l;
	gboolean         ok = TRUE;

	if (!need_str (args, "bus", &bus, error))
		return FALSE;
	id = str (args, "id") != NULL ? str (args, "id") : type;
	if (id == NULL)
		return bad (error, "Missing 'id' (or 'type')");
	if (!lrg_mixer_asset_has_bus (m, bus))
		return missing (error, "bus", bus);
	ids = lrg_mixer_asset_get_effect_ids (m, bus);
	if (ids == NULL || !g_strv_contains ((const gchar * const *) ids, id))
	{
		if (type == NULL)
			return bad (error, "A new effect needs a 'type' (song.instruments lists them)");
		if (!lrg_mixer_asset_add_effect (m, bus, type, id, error))
			return FALSE;
	}
	if (num (args, "enabled", &enabled))
		lrg_mixer_asset_set_effect_enabled (m, bus, id, enabled != 0.0);
	if (params == NULL)
		return TRUE;
	keys = json_object_get_members (params);
	for (l = keys; l != NULL && ok; l = l->next)
	{
		JsonNode *n = json_object_get_member (params, l->data);
		GValue    v = G_VALUE_INIT;
		gdouble   d;

		if (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
		{
			g_value_init (&v, G_TYPE_STRING);
			g_value_set_string (&v, json_node_get_string (n));
		}
		else if (node_num (n, &d))
		{
			g_value_init (&v, G_TYPE_DOUBLE);
			g_value_set_double (&v, d);
		}
		else
		{
			ok = bad (error, "params.%s must be a number or text", (const gchar *) l->data);
			break;
		}
		ok = lrg_mixer_asset_set_effect_param (m, bus, id, l->data, &v, error);
		g_value_unset (&v);
	}
	g_list_free (keys);
	(void) result;
	return ok;
}

static gboolean
op_remove_effect (LrgSong     *song,
                  JsonObject  *args,
                  JsonObject  *result,
                  GError     **error)
{
	const gchar *bus;
	const gchar *id;

	if (!need_str (args, "bus", &bus, error) || !need_str (args, "id", &id, error))
		return FALSE;
	if (!lrg_mixer_asset_remove_effect (lrg_song_get_mixer (song), bus, id))
		return missing (error, "effect", id);
	(void) result;
	return TRUE;
}

static gboolean
op_set_send (LrgSong     *song,
             JsonObject  *args,
             JsonObject  *result,
             GError     **error)
{
	LrgMixerAsset *m = lrg_song_get_mixer (song);
	const gchar   *source;
	const gchar   *target;
	gdouble        level;

	if (!need_str (args, "source", &source, error) || !need_str (args, "target", &target, error))
		return FALSE;
	if (is_null (args, "level"))
	{
		lrg_mixer_asset_remove_send (m, source, target);
		return TRUE;
	}
	if (!need_num (args, "level", &level, error))
		return FALSE;
	if (!lrg_mixer_asset_set_send (m, source, target, level))
		return bad (error, "Cannot send '%s' to '%s'", source, target);
	(void) result;
	return TRUE;
}

static gboolean
op_set_duck (LrgSong     *song,
             JsonObject  *args,
             JsonObject  *result,
             GError     **error)
{
	LrgMixerAsset *m = lrg_song_get_mixer (song);
	const gchar   *trigger;
	const gchar   *target;
	gdouble        threshold = -30, depth = -6, attack = 0.005, release = 0.15;

	if (!need_str (args, "trigger", &trigger, error) || !need_str (args, "target", &target, error))
		return FALSE;
	if (is_null (args, "depth"))
	{
		lrg_mixer_asset_remove_duck (m, trigger, target);
		return TRUE;
	}
	num (args, "threshold", &threshold);
	num (args, "depth", &depth);
	num (args, "attack", &attack);
	num (args, "release", &release);
	if (!lrg_mixer_asset_set_duck (m, trigger, target, threshold, depth, attack, release))
		return bad (error, "Cannot duck '%s' from '%s' (unknown buses, a loop, or values out of range)", target,
		            trigger);
	(void) result;
	return TRUE;
}

/* ==========================================================================
 * The table
 * ========================================================================== */

static const struct
{
	const gchar *name;
	LrgSongOp    op;
} ops[] = {
	{ "set", op_set },
	{ "set_tempo", op_set_tempo },
	{ "set_marker", op_set_marker },
	{ "remove_marker", op_remove_marker },
	{ "set_instrument", op_set_instrument },
	{ "remove_instrument", op_remove_instrument },
	{ "add_pattern", op_add_pattern },
	{ "set_notes", op_set_notes },
	{ "set_lanes", op_set_lanes },
	{ "remove_pattern", op_remove_pattern },
	{ "add_track", op_add_track },
	{ "set_track", op_set_track },
	{ "remove_track", op_remove_track },
	{ "move_track", op_move_track },
	{ "add_clip", op_add_clip },
	{ "set_clip", op_set_clip },
	{ "remove_clip", op_remove_clip },
	{ "set_automation", op_set_automation },
	{ "set_bus", op_set_bus },
	{ "remove_bus", op_remove_bus },
	{ "set_effect", op_set_effect },
	{ "remove_effect", op_remove_effect },
	{ "set_send", op_set_send },
	{ "set_duck", op_set_duck },
};

LrgSongOp
lrg_editor_song_op_find (const gchar *name)
{
	guint i;

	for (i = 0; name != NULL && i < G_N_ELEMENTS (ops); i++)
		if (g_str_equal (ops[i].name, name))
			return ops[i].op;
	return NULL;
}

const gchar * const *
lrg_editor_song_op_names (void)
{
	static const gchar *names[G_N_ELEMENTS (ops) + 1];
	guint               i;

	if (names[0] == NULL)
		for (i = 0; i < G_N_ELEMENTS (ops); i++)
			names[i] = ops[i].name;
	return names;
}

/* ==========================================================================
 * The song as JSON
 * ========================================================================== */

static JsonArray *
points_json (LrgSong     *song,
             const gchar *track,
             const gchar *target)
{
	JsonArray        *a = json_array_new ();
	gsize             n = 0, i;
	g_autofree gdouble *p = lrg_song_get_automation_points (song, track, target, &n);

	for (i = 0; p != NULL && i + 1 < n; i += 2)
	{
		JsonArray *pt = json_array_new ();

		json_array_add_double_element (pt, p[i]);
		json_array_add_double_element (pt, p[i + 1]);
		json_array_add_array_element (a, pt);
	}
	return a;
}

static JsonArray *
automation_json (LrgSong     *song,
                 const gchar *track)
{
	JsonArray    *a = json_array_new ();
	g_auto(GStrv) targets = lrg_song_list_automation (song, track);
	guint         i;

	for (i = 0; targets[i] != NULL; i++)
	{
		JsonObject *o = json_object_new ();

		json_object_set_string_member (o, "target", targets[i]);
		json_object_set_array_member (o, "points", points_json (song, track, targets[i]));
		json_array_add_object_element (a, o);
	}
	return a;
}

JsonObject *
lrg_editor_song_to_json (LrgSong     *song,
                         const gchar *section)
{
	JsonObject *o = json_object_new ();
	guint       i, j;

#define WANT(s) (section == NULL || g_str_equal (section, s))
	if (WANT ("settings"))
	{
		gchar      *title = NULL, *artist = NULL;
		gdouble     bpm = 0, length = 0, ls = 0, le = 0;
		guint       bpb = 4, unit = 4, rate = 0, seed = 0;
		JsonArray  *tempo = json_array_new ();
		JsonArray  *markers = json_array_new ();
		g_autofree gchar *ts = NULL;

		g_object_get (song, "title", &title, "artist", &artist, "bpm", &bpm, "length", &length, "loop-start", &ls,
		              "loop-end", &le, "beats-per-bar", &bpb, "beat-unit", &unit, "sample-rate", &rate, "seed", &seed,
		              NULL);
		ts = g_strdup_printf ("%u/%u", bpb, unit);
		json_object_set_string_member (o, "title", title);
		json_object_set_string_member (o, "artist", artist);
		json_object_set_double_member (o, "bpm", bpm);
		json_object_set_string_member (o, "time_signature", ts);
		json_object_set_int_member (o, "sample_rate", rate);
		json_object_set_int_member (o, "seed", seed);
		json_object_set_double_member (o, "length", length);
		json_object_set_double_member (o, "length_beats", lrg_song_get_length_beats (song));
		json_object_set_double_member (o, "duration", lrg_song_get_duration (song));
		json_object_set_double_member (o, "loop_start", ls);
		json_object_set_double_member (o, "loop_end", le);
		for (i = 0; i < lrg_song_get_n_tempo_changes (song); i++)
		{
			JsonObject *t = json_object_new ();
			gdouble     b = 0, tb = 0;
			gboolean    ramp = FALSE;

			lrg_song_get_tempo_change (song, i, &b, &tb, &ramp);
			json_object_set_double_member (t, "beat", b);
			json_object_set_double_member (t, "bpm", tb);
			json_object_set_boolean_member (t, "ramp", ramp);
			json_array_add_object_element (tempo, t);
		}
		json_object_set_array_member (o, "tempo", tempo);
		for (i = 0; i < lrg_song_get_n_markers (song); i++)
		{
			JsonObject  *mk = json_object_new ();
			gdouble      b = 0;
			const gchar *name = NULL;

			lrg_song_get_marker (song, i, &b, &name);
			json_object_set_double_member (mk, "beat", b);
			json_object_set_string_member (mk, "name", name);
			json_array_add_object_element (markers, mk);
		}
		json_object_set_array_member (o, "markers", markers);
		g_free (title);
		g_free (artist);
	}
	if (WANT ("instruments"))
	{
		JsonObject   *ins = json_object_new ();
		g_auto(GStrv) ids = lrg_song_list_instruments (song);

		for (i = 0; ids[i] != NULL; i++)
		{
			JsonObject   *in = json_object_new ();
			JsonObject   *params = json_object_new ();
			g_auto(GStrv) names = lrg_song_list_instrument_params (song, ids[i]);

			json_object_set_string_member (in, "type", lrg_song_get_instrument_type (song, ids[i]));
			json_object_set_string_member (in, "source", lrg_song_get_instrument_source (song, ids[i]));
			for (j = 0; names[j] != NULL; j++)
				json_object_set_double_member (params, names[j], lrg_song_get_instrument_param (song, ids[i], names[j]));
			json_object_set_object_member (in, "params", params);
			json_object_set_object_member (ins, ids[i], in);
		}
		json_object_set_object_member (o, "instruments", ins);
	}
	if (WANT ("patterns"))
	{
		JsonObject   *pats = json_object_new ();
		g_auto(GStrv) ids = lrg_song_list_patterns (song);

		for (i = 0; ids[i] != NULL; i++)
		{
			JsonObject   *p = json_object_new ();
			JsonArray    *notes = json_array_new ();
			JsonObject   *lanes = json_object_new ();
			g_auto(GStrv) lane_names = lrg_song_pattern_list_lanes (song, ids[i]);
			guint         n = lrg_song_pattern_get_n_notes (song, ids[i]);

			json_object_set_double_member (p, "length", lrg_song_get_pattern_length (song, ids[i]));
			json_object_set_int_member (p, "steps", lrg_song_pattern_get_steps (song, ids[i]));
			for (j = 0; j < n; j++)
			{
				JsonArray        *note = json_array_new ();
				gdouble           beat = 0, len = 0, vel = 0;
				gint              key = 0;
				g_autofree gchar *name = NULL;

				lrg_song_pattern_get_note (song, ids[i], j, &beat, &key, &len, &vel);
				name = lrg_music_key_to_name (key);
				json_array_add_double_element (note, beat);
				json_array_add_string_element (note, name);
				json_array_add_double_element (note, len);
				json_array_add_double_element (note, vel);
				json_array_add_array_element (notes, note);
			}
			for (j = 0; lane_names[j] != NULL; j++)
				json_object_set_string_member (lanes, lane_names[j], lrg_song_pattern_get_lane (song, ids[i], lane_names[j]));
			json_object_set_array_member (p, "notes", notes);
			json_object_set_object_member (p, "lanes", lanes);
			json_object_set_object_member (pats, ids[i], p);
		}
		json_object_set_object_member (o, "patterns", pats);
	}
	if (WANT ("tracks"))
	{
		JsonArray    *tracks = json_array_new ();
		g_auto(GStrv) ids = lrg_song_list_tracks (song);

		for (i = 0; ids[i] != NULL; i++)
		{
			JsonObject *t = json_object_new ();
			JsonArray  *clips = json_array_new ();
			guint       n = lrg_song_get_n_clips (song, ids[i]);

			json_object_set_string_member (t, "id", ids[i]);
			json_object_set_string_member (t, "kind", lrg_song_get_track_kind (song, ids[i]) == LRG_SONG_TRACK_AUDIO
			                                          ? "audio" : "instrument");
			json_object_set_string_member (t, "name", lrg_song_get_track_text (song, ids[i], "name"));
			json_object_set_string_member (t, "instrument", lrg_song_get_track_text (song, ids[i], "instrument"));
			json_object_set_string_member (t, "bus", lrg_song_get_track_text (song, ids[i], "bus"));
			json_object_set_double_member (t, "volume", lrg_song_get_track_value (song, ids[i], "volume"));
			json_object_set_double_member (t, "pan", lrg_song_get_track_value (song, ids[i], "pan"));
			json_object_set_boolean_member (t, "mute", lrg_song_get_track_value (song, ids[i], "mute") != 0.0);
			json_object_set_boolean_member (t, "solo", lrg_song_get_track_value (song, ids[i], "solo") != 0.0);
			for (j = 0; j < n; j++)
			{
				JsonObject  *c = json_object_new ();
				const gchar *source = NULL;
				gdouble      start = 0, length = 0;

				lrg_song_get_clip (song, ids[i], j, &source, &start, &length);
				json_object_set_int_member (c, "index", j);
				json_object_set_string_member (c, "source", source);
				json_object_set_double_member (c, "start", start);
				json_object_set_double_member (c, "length", length);
				json_object_set_double_member (c, "offset", lrg_song_get_clip_value (song, ids[i], j, "offset"));
				json_object_set_double_member (c, "transpose", lrg_song_get_clip_value (song, ids[i], j, "transpose"));
				json_object_set_double_member (c, "gain", lrg_song_get_clip_value (song, ids[i], j, "gain"));
				json_object_set_double_member (c, "fade_in", lrg_song_get_clip_value (song, ids[i], j, "fade-in"));
				json_object_set_double_member (c, "fade_out", lrg_song_get_clip_value (song, ids[i], j, "fade-out"));
				json_object_set_boolean_member (c, "mute", lrg_song_get_clip_value (song, ids[i], j, "mute") != 0.0);
				json_array_add_object_element (clips, c);
			}
			json_object_set_array_member (t, "clips", clips);
			json_object_set_array_member (t, "automation", automation_json (song, ids[i]));
			json_array_add_object_element (tracks, t);
		}
		json_object_set_array_member (o, "tracks", tracks);
	}
	if (WANT ("automation"))
		json_object_set_array_member (o, "automation", automation_json (song, NULL));
	if (WANT ("mixer"))
	{
		LrgMixerAsset *m = lrg_song_get_mixer (song);
		JsonObject    *mixer = json_object_new ();
		JsonArray     *buses = json_array_new ();
		JsonArray     *ducks = json_array_new ();
		g_auto(GStrv)  names = lrg_mixer_asset_get_bus_names (m);

		for (i = 0; names[i] != NULL; i++)
		{
			JsonObject   *b = json_object_new ();
			JsonArray    *effects = json_array_new ();
			JsonArray    *sends = json_array_new ();
			g_auto(GStrv) ids = lrg_mixer_asset_get_effect_ids (m, names[i]);
			g_auto(GStrv) targets = lrg_mixer_asset_get_send_targets (m, names[i]);

			json_object_set_string_member (b, "name", names[i]);
			json_object_set_string_member (b, "parent", lrg_mixer_asset_get_bus_parent (m, names[i]));
			json_object_set_double_member (b, "volume", lrg_mixer_asset_get_bus_volume (m, names[i]));
			json_object_set_boolean_member (b, "mute", lrg_mixer_asset_get_bus_mute (m, names[i]));
			json_object_set_boolean_member (b, "solo", lrg_mixer_asset_get_bus_solo (m, names[i]));
			for (j = 0; ids != NULL && ids[j] != NULL; j++)
			{
				JsonObject *e = json_object_new ();

				json_object_set_string_member (e, "id", ids[j]);
				json_object_set_string_member (e, "type", lrg_mixer_asset_get_effect_type (m, names[i], ids[j]));
				json_object_set_boolean_member (e, "enabled", lrg_mixer_asset_get_effect_enabled (m, names[i], ids[j]));
				json_array_add_object_element (effects, e);
			}
			for (j = 0; targets != NULL && targets[j] != NULL; j++)
			{
				JsonObject *s = json_object_new ();
				gdouble     level = 0;

				lrg_mixer_asset_get_send (m, names[i], targets[j], &level);
				json_object_set_string_member (s, "target", targets[j]);
				json_object_set_double_member (s, "level", level);
				json_array_add_object_element (sends, s);
			}
			json_object_set_array_member (b, "effects", effects);
			json_object_set_array_member (b, "sends", sends);
			json_array_add_object_element (buses, b);
		}
		for (i = 0; i < lrg_mixer_asset_get_duck_count (m); i++)
		{
			JsonObject  *d = json_object_new ();
			const gchar *trigger = NULL, *target = NULL;
			gdouble      thr = 0, depth = 0, att = 0, rel = 0;

			lrg_mixer_asset_get_duck (m, i, &trigger, &target, &thr, &depth, &att, &rel);
			json_object_set_string_member (d, "trigger", trigger);
			json_object_set_string_member (d, "target", target);
			json_object_set_double_member (d, "threshold", thr);
			json_object_set_double_member (d, "depth", depth);
			json_object_set_double_member (d, "attack", att);
			json_object_set_double_member (d, "release", rel);
			json_array_add_object_element (ducks, d);
		}
		json_object_set_array_member (mixer, "buses", buses);
		json_object_set_array_member (mixer, "ducking", ducks);
		json_object_set_object_member (o, "mixer", mixer);
	}
#undef WANT
	return o;
}

/* ==========================================================================
 * Discovery
 * ========================================================================== */

/* every writable numeric/enum/boolean property of @type with its range */
static JsonArray *
params_of (GType type)
{
	g_autoptr(GTypeClass) klass = g_type_class_ref (type);
	guint                 n = 0, i;
	g_autofree GParamSpec **specs = g_object_class_list_properties (G_OBJECT_CLASS (klass), &n);
	JsonArray            *a = json_array_new ();

	for (i = 0; i < n; i++)
	{
		GParamSpec *p = specs[i];
		JsonObject *o;

		if (!(p->flags & G_PARAM_WRITABLE) || g_str_equal (p->name, "enabled"))
			continue;
		o = json_object_new ();
		json_object_set_string_member (o, "name", p->name);
		if (G_IS_PARAM_SPEC_DOUBLE (p))
		{
			json_object_set_double_member (o, "min", G_PARAM_SPEC_DOUBLE (p)->minimum);
			json_object_set_double_member (o, "max", G_PARAM_SPEC_DOUBLE (p)->maximum);
			json_object_set_double_member (o, "default", G_PARAM_SPEC_DOUBLE (p)->default_value);
		}
		else if (G_IS_PARAM_SPEC_FLOAT (p))
		{
			json_object_set_double_member (o, "min", G_PARAM_SPEC_FLOAT (p)->minimum);
			json_object_set_double_member (o, "max", G_PARAM_SPEC_FLOAT (p)->maximum);
			json_object_set_double_member (o, "default", G_PARAM_SPEC_FLOAT (p)->default_value);
		}
		else if (G_IS_PARAM_SPEC_UINT (p))
		{
			json_object_set_double_member (o, "min", G_PARAM_SPEC_UINT (p)->minimum);
			json_object_set_double_member (o, "max", G_PARAM_SPEC_UINT (p)->maximum);
			json_object_set_double_member (o, "default", G_PARAM_SPEC_UINT (p)->default_value);
		}
		else if (G_IS_PARAM_SPEC_INT (p))
		{
			json_object_set_double_member (o, "min", G_PARAM_SPEC_INT (p)->minimum);
			json_object_set_double_member (o, "max", G_PARAM_SPEC_INT (p)->maximum);
			json_object_set_double_member (o, "default", G_PARAM_SPEC_INT (p)->default_value);
		}
		else if (G_IS_PARAM_SPEC_BOOLEAN (p))
			json_object_set_boolean_member (o, "default", G_PARAM_SPEC_BOOLEAN (p)->default_value);
		else if (G_IS_PARAM_SPEC_ENUM (p))
		{
			GEnumClass *ec = G_PARAM_SPEC_ENUM (p)->enum_class;
			JsonArray  *values = json_array_new ();
			guint       k;

			for (k = 0; k < ec->n_values; k++)
				json_array_add_string_element (values, ec->values[k].value_nick);
			json_object_set_array_member (o, "values", values);
		}
		else
		{
			json_object_unref (o);
			continue;
		}
		if (g_param_spec_get_blurb (p) != NULL)
			json_object_set_string_member (o, "description", g_param_spec_get_blurb (p));
		json_array_add_object_element (a, o);
	}
	return a;
}

JsonObject *
lrg_editor_song_catalogue (void)
{
	JsonObject   *o = json_object_new ();
	JsonObject   *types = json_object_new ();
	JsonObject   *synth = json_object_new ();
	JsonObject   *presets = json_object_new ();
	JsonObject   *drums = json_object_new ();
	JsonArray    *pieces = json_array_new ();
	JsonObject   *effects = json_object_new ();
	JsonArray    *kinds = json_array_new ();
	g_auto(GStrv) names = lrg_synth_list_presets ();
	g_auto(GStrv) piece_names = lrg_drum_machine_list_pieces ();
	g_auto(GStrv) fx = lrg_mixer_asset_list_effect_types ();
	g_autoptr(GEnumClass) kind_class = g_type_class_ref (LRG_TYPE_FOLEY_KIND);
	guint         i;

	for (i = 0; names[i] != NULL; i++)
		json_object_set_string_member (presets, names[i], lrg_synth_get_preset_description (names[i]));
	json_object_set_string_member (synth, "source", "a preset name");
	json_object_set_object_member (synth, "presets", presets);
	json_object_set_array_member (synth, "params", params_of (LRG_TYPE_SYNTH));
	json_object_set_object_member (types, "synth", synth);

	for (i = 0; piece_names[i] != NULL; i++)
	{
		JsonObject *p = json_object_new ();

		json_object_set_string_member (p, "name", piece_names[i]);
		json_object_set_int_member (p, "key", lrg_drum_machine_piece_to_key (piece_names[i]));
		json_array_add_object_element (pieces, p);
	}
	json_object_set_string_member (drums, "source", "a kit: 808, 909 or linn");
	json_object_set_array_member (drums, "pieces", pieces);
	json_object_set_array_member (drums, "params", params_of (LRG_TYPE_DRUM_MACHINE));
	json_object_set_object_member (types, "drums", drums);

	{
		JsonObject *sampler = json_object_new ();
		JsonObject *sf = json_object_new ();
		JsonObject *foley = json_object_new ();

		json_object_set_string_member (sampler, "source", "an audio file (root-key, loop-start, loop-end params)");
		json_object_set_array_member (sampler, "params", params_of (LRG_TYPE_SAMPLER));
		json_object_set_object_member (types, "sampler", sampler);
		json_object_set_string_member (sf, "source", "a .sf2 SoundFont file");
		json_object_set_array_member (sf, "params", params_of (LRG_TYPE_SOUND_FONT_INSTRUMENT));
		json_object_set_object_member (types, "soundfont", sf);
		json_object_set_string_member (foley, "source", "a .rfoley patch file");
		json_object_set_array_member (foley, "params", params_of (LRG_TYPE_FOLEY_INSTRUMENT));
		json_object_set_object_member (types, "foley", foley);
	}

	for (i = 0; fx != NULL && fx[i] != NULL; i++)
	{
		GType t = lrg_mixer_asset_lookup_effect_type (fx[i]);

		if (t != G_TYPE_INVALID)
			json_object_set_array_member (effects, fx[i], params_of (t));
	}
	for (i = 0; i < kind_class->n_values; i++)
		json_array_add_string_element (kinds, kind_class->values[i].value_nick);

	json_object_set_object_member (o, "instrument_types", types);
	json_object_set_object_member (o, "effects", effects);
	json_object_set_array_member (o, "foley_kinds", kinds);
	json_object_set_string_member (o, "steps", ". or - rest, g 0.3, o 0.5, x 0.8, X 1.0; spaces and | ignored");
	json_object_set_string_member (o, "notes", "[beat, key, length, velocity]: key a note name (C4 = 60) or MIDI number");
	json_object_set_array_member (o, "automation_targets", NULL);
	{
		JsonArray *t = json_array_new ();

		json_array_add_string_element (t, "track: volume (dB)");
		json_array_add_string_element (t, "track: pan (-1 .. 1)");
		json_array_add_string_element (t, "track: param.<instrument parameter>");
		json_array_add_string_element (t, "song: bus.<bus>.volume (dB)");
		json_array_add_string_element (t, "song: effect.<bus>.<effect id>.<property>");
		json_object_set_array_member (o, "automation_targets", t);
	}
	return o;
}

JsonObject *
lrg_editor_song_formats (void)
{
	g_autoptr(GEnumClass) fmt = g_type_class_ref (LRG_TYPE_AUDIO_FILE_FORMAT);
	g_autoptr(GEnumClass) bk = g_type_class_ref (LRG_TYPE_AUDIO_ENCODER_BACKEND);
	JsonObject           *o = json_object_new ();
	JsonArray            *list = json_array_new ();
	guint                 i;

	for (i = 0; i < fmt->n_values; i++)
	{
		LrgAudioFileFormat     f = (LrgAudioFileFormat) fmt->values[i].value;
		LrgAudioEncoderBackend b = lrg_audio_encoder_get_backend (f);
		JsonObject            *e = json_object_new ();

		json_object_set_string_member (e, "format", fmt->values[i].value_nick);
		json_object_set_string_member (e, "extension", lrg_audio_file_format_get_extension (f));
		json_object_set_boolean_member (e, "lossless", lrg_audio_file_format_is_lossless (f));
		json_object_set_string_member (e, "backend", g_enum_get_value (bk, (gint) b)->value_nick);
		json_object_set_boolean_member (e, "available", b != LRG_AUDIO_ENCODER_BACKEND_NONE);
		json_array_add_object_element (list, e);
	}
	json_object_set_array_member (o, "formats", list);
	return o;
}
