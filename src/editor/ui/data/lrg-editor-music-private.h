/* lrg-editor-music-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Helpers shared by the Foley designer and the song editor: JSON
 * parameters of IPC methods, error mapping, project paths, analysis as
 * JSON, preview playback on the audio device.  Static inline: each editor
 * gets its own copy.  Not installed.
 */

#pragma once

#include <glib.h>
#include <json-glib/json-glib.h>
#include <math.h>
#include <errno.h>
#include <glib/gstdio.h>
#include <graylib.h>

#include "../lrg-editor-app-private.h"
#include "../../lrg-editor-server.h"
#include "../../../audio/lrg-audio-analysis.h"
#include "../../../audio/lrg-wave-data.h"

G_BEGIN_DECLS

static inline gchar *
music_abs_path (LrgEditorApp *app,
                const gchar  *path)
{
	if (path == NULL)
		return NULL;
	if (g_path_is_absolute (path) || app == NULL)
		return g_strdup (path);
	return editor_app_project_path (app, path);
}

static inline JsonNode *
music_object_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static inline const gchar *
music_param_string (JsonObject  *params,
                    const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_STRING)
		return NULL;
	return json_node_get_string (n);
}

/* a number (int or double) member; FALSE when absent or not a number */
static inline gboolean
music_param_double (JsonObject  *params,
                    const gchar *name,
                    gdouble     *out)
{
	JsonNode *n;
	GType     t;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	t = json_node_get_value_type (n);
	if (t == G_TYPE_DOUBLE)
		*out = json_node_get_double (n);
	else if (t == G_TYPE_INT64)
		*out = (gdouble) json_node_get_int (n);
	else if (t == G_TYPE_BOOLEAN)
		*out = json_node_get_boolean (n) ? 1.0 : 0.0;
	else
		return FALSE;
	return isfinite (*out);
}

static inline gboolean
music_param_bool (JsonObject  *params,
                  const gchar *name,
                  gboolean    *out)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_BOOLEAN)
		return FALSE;
	*out = json_node_get_boolean (n);
	return TRUE;
}

static inline JsonObject *
music_param_object (JsonObject  *params,
                    const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	return JSON_NODE_HOLDS_OBJECT (n) ? json_node_get_object (n) : NULL;
}

static inline JsonArray *
music_param_array (JsonObject  *params,
                   const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	return JSON_NODE_HOLDS_ARRAY (n) ? json_node_get_array (n) : NULL;
}

/* @error as an IPC error: not-found and invalid-params mapped, else failed */
static inline gpointer
music_fail (GError       **error,
            const GError  *cause)
{
	gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

	if (cause != NULL && (cause->code == G_IO_ERROR_NOT_FOUND || g_error_matches (cause, G_FILE_ERROR, G_FILE_ERROR_NOENT)
	                      || g_error_matches (cause, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND)))
		code = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
	else if (cause != NULL && (g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)
	                           || g_error_matches (cause, LRG_EDITOR_SERVER_ERROR,
	                                               LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS)))
		code = LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS;
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, cause != NULL ? cause->message : "failed");
	return NULL;
}

static inline gpointer
music_bad (GError      **error,
           const gchar  *message)
{
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, message);
	return NULL;
}

static inline gpointer
music_missing (GError      **error,
               const gchar  *message)
{
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, message);
	return NULL;
}

/* the measurements an agent mixes by, as JSON */
static inline JsonObject *
music_analysis_json (const LrgAudioAnalysis *a)
{
	JsonObject *o = json_object_new ();

	json_object_set_double_member (o, "duration", a->duration);
	json_object_set_double_member (o, "peak_db", a->peak_db);
	json_object_set_double_member (o, "true_peak_db", a->true_peak_db);
	json_object_set_double_member (o, "rms_db", a->rms_db);
	json_object_set_double_member (o, "integrated_lufs", a->integrated_lufs);
	json_object_set_double_member (o, "short_term_max_lufs", a->short_term_max_lufs);
	json_object_set_double_member (o, "momentary_max_lufs", a->momentary_max_lufs);
	json_object_set_double_member (o, "loudness_range", a->loudness_range);
	json_object_set_double_member (o, "dc_offset", a->dc_offset);
	json_object_set_int_member (o, "clipped_samples", (gint64) a->clipped_samples);
	return o;
}

/*
 * music_ensure_parent:
 * Creates the folder @path will be written in.
 */
static inline gboolean
music_ensure_parent (const gchar  *path,
                     GError      **error)
{
	g_autofree gchar *dir = g_path_get_dirname (path);

	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Could not create %s: %s", dir,
		             g_strerror (errno));
		return FALSE;
	}
	return TRUE;
}

/*
 * music_envelope:
 * @n_bins peak levels (0 .. 1) of interleaved @samples, for an outline of
 * the sound (a coarse waveform an agent can read).
 */
static inline JsonArray *
music_envelope (const gfloat *samples,
                gsize         n_frames,
                guint         channels,
                guint         n_bins)
{
	JsonArray *a = json_array_new ();
	guint      b;

	for (b = 0; b < n_bins; b++)
	{
		gsize   from = n_frames * b / n_bins;
		gsize   to = n_frames * (b + 1) / n_bins;
		gdouble peak = 0.0;
		gsize   i;

		for (i = from * channels; i < to * channels; i++)
			peak = MAX (peak, fabs ((gdouble) samples[i]));
		json_array_add_double_element (a, round (peak * 1000.0) / 1000.0);
	}
	return a;
}

/*
 * music_play_wave:
 * Plays @wave on the audio device (opened on first use), replacing the
 * sound in *@slot.
 */
static inline gboolean
music_play_wave (GrlSound    **slot,
                 LrgWaveData  *wave,
                 GError      **error)
{
	GrlAudioDevice *device = grl_audio_device_get_default ();

	if (device == NULL || (!grl_audio_device_is_ready (device) && !grl_audio_device_init_audio (device)))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED, "No audio device available for the preview");
		return FALSE;
	}
	if (*slot != NULL)
		grl_sound_stop (*slot);
	g_clear_object (slot);
	*slot = lrg_wave_data_to_sound (wave);
	if (*slot == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "The preview could not be played");
		return FALSE;
	}
	grl_sound_play (*slot);
	return TRUE;
}

G_END_DECLS
