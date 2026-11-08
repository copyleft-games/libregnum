/* test-mixer-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The `.rmixer` format and the 0.9.0 mixer runtime, all headless (the
 * mixer's offline render path, no audio device):
 *
 *  - format: byte-stable round trip with unknown keys kept, strict errors
 *    with key paths, editing API;
 *  - runtime: routing math through parents and sends in dB, effect chains
 *    with parameters, snapshot blends (per-frame gain ramps, per-block
 *    parameter ramps, weighted blends), side-chain ducking against the
 *    analytic one-pole envelope, solo, meters, voice gain/pan/pitch;
 *  - spatial: rolloff curves (linear, inverse, logarithmic, custom
 *    LrgKeyframeCurve) and cone gain vs analytic, doppler, and the audio
 *    source component end to end (mixer voice, bus routing, cone,
 *    doppler from movement, physics occlusion);
 *  - the project's default mixer reference.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define EPS (1e-5)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* @frames frames of a constant @value (every channel) */
static LrgWaveData *
dc_wave (guint   rate,
         guint   channels,
         guint   frames,
         gfloat  value)
{
	g_autofree gfloat *samples = g_new (gfloat, (gsize) frames * channels);
	gsize              i;

	for (i = 0; i < (gsize) frames * channels; i++)
		samples[i] = value;
	return lrg_wave_data_new_from_samples (rate, 32, channels, (const guint8 *) samples,
	                                       (gsize) frames * channels * sizeof (gfloat));
}

/* a mono ramp: frame n is n * @step */
static LrgWaveData *
ramp_wave (guint  rate,
           guint  frames,
           gfloat step)
{
	g_autofree gfloat *samples = g_new (gfloat, frames);
	guint              i;

	for (i = 0; i < frames; i++)
		samples[i] = (gfloat) i * step;
	return lrg_wave_data_new_from_samples (rate, 32, 1, (const guint8 *) samples, frames * sizeof (gfloat));
}

/* renders @frames and returns the last frame's channel @c */
static gfloat
render_last (LrgAudioMixer *mixer,
             guint          frames,
             guint          c)
{
	gsize              n = 0;
	g_autofree gfloat *pcm = lrg_audio_mixer_render (mixer, frames, &n);
	guint              ch = lrg_audio_mixer_get_channels (mixer);

	g_assert_nonnull (pcm);
	return pcm[(gsize) (frames - 1) * ch + c];
}

static gdouble
db (gdouble value)
{
	return lrg_audio_db_to_gain (value);
}

static LrgMixerAsset *
load (const gchar *text)
{
	g_autoptr(GError) error = NULL;
	LrgMixerAsset    *asset = lrg_mixer_asset_new_from_data (text, -1, &error);

	g_assert_no_error (error);
	g_assert_nonnull (asset);
	return asset;
}

/* ==========================================================================
 * Format
 * ========================================================================== */

static const gchar *full_doc =
	"version: 1\n"
	"name: Main mix\n"
	"default_snapshot: explore\n"
	"editor_layout: { strip_width: 80 }\n"
	"buses:\n"
	"  - name: master\n"
	"    volume: 0\n"
	"  - name: music\n"
	"    volume: -6\n"
	"    color: '#89b4fa'\n"
	"    sends:\n"
	"      - target: reverb\n"
	"        level: -12\n"
	"        pre_fader_note: kept\n"
	"    effects:\n"
	"      - type: low-pass\n"
	"        id: muffle\n"
	"        params:\n"
	"          cutoff: 18000\n"
	"          resonance: 0.8\n"
	"        ui_collapsed: true\n"
	"  - name: dialogue\n"
	"    volume: -1.5\n"
	"    solo: true\n"
	"  - name: reverb\n"
	"    mute: true\n"
	"    effects:\n"
	"      - type: reverb\n"
	"        enabled: false\n"
	"        params:\n"
	"          preset: hall\n"
	"          wet: 0.5\n"
	"snapshots:\n"
	"  - name: explore\n"
	"    blend: 0\n"
	"  - name: underwater\n"
	"    blend: 0.5\n"
	"    volumes:\n"
	"      music: -12\n"
	"    sends:\n"
	"      - source: dialogue\n"
	"        target: reverb\n"
	"        level: -3\n"
	"    params:\n"
	"      music/muffle/cutoff: 800\n"
	"    tint: blue\n"
	"ducking:\n"
	"  - trigger: dialogue\n"
	"    target: music\n"
	"    threshold: -30\n"
	"    depth: -10\n"
	"    attack: 0.05\n"
	"    release: 0.4\n"
	"    note: voice over music\n";

static void
test_roundtrip (void)
{
	g_autoptr(LrgMixerAsset) asset = load (full_doc);
	g_autoptr(LrgMixerAsset) again = NULL;
	g_autoptr(LrgMixerAsset) copy = NULL;
	g_autoptr(GError)        error = NULL;
	g_autofree gchar        *text1 = NULL;
	g_autofree gchar        *text2 = NULL;
	g_autofree gchar        *text3 = NULL;
	g_auto(GStrv)            buses = NULL;
	g_auto(GStrv)            ids = NULL;
	g_auto(GValue)           v = G_VALUE_INIT;
	gdouble                  d = 0.0;

	/* the model */
	g_assert_cmpstr (lrg_mixer_asset_get_name (asset), ==, "Main mix");
	buses = lrg_mixer_asset_get_bus_names (asset);
	g_assert_cmpuint (g_strv_length (buses), ==, 4);
	g_assert_cmpstr (buses[0], ==, "master");
	g_assert_cmpstr (lrg_mixer_asset_get_bus_parent (asset, "music"), ==, "master");
	g_assert_cmpfloat_with_epsilon (lrg_mixer_asset_get_bus_volume (asset, "music"), -6.0, EPS);
	g_assert_true (lrg_mixer_asset_get_bus_solo (asset, "dialogue"));
	g_assert_true (lrg_mixer_asset_get_bus_mute (asset, "reverb"));
	g_assert_true (lrg_mixer_asset_get_send (asset, "music", "reverb", &d));
	g_assert_cmpfloat_with_epsilon (d, -12.0, EPS);
	ids = lrg_mixer_asset_get_effect_ids (asset, "music");
	g_assert_cmpstrv (ids, ((const gchar *[]) { "muffle", NULL }));
	g_assert_cmpstr (lrg_mixer_asset_get_effect_type (asset, "reverb", "reverb"), ==, "reverb");
	g_assert_false (lrg_mixer_asset_get_effect_enabled (asset, "reverb", "reverb"));
	g_assert_true (lrg_mixer_asset_get_effect_param (asset, "music", "muffle", "cutoff", &v));
	g_assert_cmpfloat_with_epsilon (g_value_get_float (&v), 18000.0f, 1e-3);
	g_assert_true (lrg_mixer_asset_get_snapshot_volume (asset, "underwater", "music", &d));
	g_assert_cmpfloat_with_epsilon (d, -12.0, EPS);
	g_assert_true (lrg_mixer_asset_get_snapshot_param (asset, "underwater", "music", "muffle", "cutoff", &d));
	g_assert_cmpfloat_with_epsilon (d, 800.0, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_mixer_asset_get_snapshot_blend (asset, "underwater"), 0.5, EPS);
	g_assert_cmpstr (lrg_mixer_asset_get_default_snapshot (asset), ==, "explore");
	g_assert_cmpuint (lrg_mixer_asset_get_duck_count (asset), ==, 1);

	/* byte-stable: save, load, save again */
	text1 = lrg_mixer_asset_to_data (asset, NULL, &error);
	g_assert_no_error (error);
	again = load (text1);
	text2 = lrg_mixer_asset_to_data (again, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text1, ==, text2);

	/* unknown keys at every level survive */
	g_assert_nonnull (strstr (text1, "editor_layout"));
	g_assert_nonnull (strstr (text1, "color"));
	g_assert_nonnull (strstr (text1, "pre_fader_note"));
	g_assert_nonnull (strstr (text1, "ui_collapsed"));
	g_assert_nonnull (strstr (text1, "tint"));
	g_assert_nonnull (strstr (text1, "voice over music"));
	g_assert_nonnull (strstr (text1, "preset: hall"));
	g_assert_nonnull (strstr (text1, "music/muffle/cutoff: 800"));

	/* a deep copy writes the same bytes */
	copy = lrg_mixer_asset_copy (asset);
	text3 = lrg_mixer_asset_to_data (copy, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text1, ==, text3);
}

static void
test_file_io (void)
{
	g_autoptr(LrgMixerAsset) asset = load (full_doc);
	g_autoptr(LrgMixerAsset) back = NULL;
	g_autoptr(GError)        error = NULL;
	g_autofree gchar        *dir = g_dir_make_tmp ("lrg-mixer-XXXXXX", &error);
	g_autofree gchar        *path = NULL;
	g_autofree gchar        *a = NULL;
	g_autofree gchar        *b = NULL;

	g_assert_no_error (error);
	path = g_build_filename (dir, "main.rmixer", NULL);
	g_assert_true (lrg_mixer_asset_save_to_file (asset, path, &error));
	g_assert_no_error (error);
	back = lrg_mixer_asset_new_from_file (path, &error);
	g_assert_no_error (error);
	a = lrg_mixer_asset_to_data (asset, NULL, NULL);
	b = lrg_mixer_asset_to_data (back, NULL, NULL);
	g_assert_cmpstr (a, ==, b);

	g_assert_null (lrg_mixer_asset_new_from_file ("/nonexistent/x.rmixer", &error));
	g_assert_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_IO);

	g_unlink (path);
	g_rmdir (dir);
}

typedef struct
{
	const gchar *doc;
	gint         code;
	const gchar *path;
} BadDoc;

static void
test_errors (void)
{
	static const BadDoc cases[] = {
		{ "- a\n- b\n", LRG_MIXER_ASSET_ERROR_PARSE, "/" },
		{ "version: 1\nname: x\n", LRG_MIXER_ASSET_ERROR_PARSE, "/" },
		{ "version: 2\nbuses: []\n", LRG_MIXER_ASSET_ERROR_PARSE, "/version" },
		{ "buses:\n  - name: a\n    parent: nope\n", LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS, "/buses/0/parent" },
		{ "buses:\n  - name: a\n    parent: b\n  - name: b\n    parent: a\n", LRG_MIXER_ASSET_ERROR_ROUTING,
		  "/parent" },
		{ "buses:\n  - name: master\n    parent: x\n", LRG_MIXER_ASSET_ERROR_ROUTING, "/buses/0/parent" },
		{ "buses:\n  - name: a\n    sends: [{target: master, level: 0}]\n", LRG_MIXER_ASSET_ERROR_ROUTING,
		  "/buses/0/sends/0/target" },
		{ "buses:\n  - name: a\n    sends: [{target: b, level: 0}]\n  - name: b\n    sends: [{target: a, level: 0}]\n",
		  LRG_MIXER_ASSET_ERROR_ROUTING, "/buses/1/sends/0/target" },
		{ "buses:\n  - name: a\n    effects: [{type: flanger}]\n", LRG_MIXER_ASSET_ERROR_UNKNOWN_EFFECT,
		  "/buses/0/effects/0/type" },
		{ "buses:\n  - name: a\n    effects: [{type: echo, params: {nope: 1}}]\n",
		  LRG_MIXER_ASSET_ERROR_UNKNOWN_PARAM, "/buses/0/effects/0/params/nope" },
		{ "buses:\n  - name: a\n    effects: [{type: low-pass, params: {cutoff: 5}}]\n",
		  LRG_MIXER_ASSET_ERROR_INVALID_VALUE, "/buses/0/effects/0/params/cutoff" },
		{ "buses:\n  - name: a\n    effects: [{type: echo, params: {delay: fast}}]\n",
		  LRG_MIXER_ASSET_ERROR_INVALID_VALUE, "/buses/0/effects/0/params/delay" },
		{ "buses:\n  - name: a\n    effects: [{type: echo}, {type: echo}]\n", LRG_MIXER_ASSET_ERROR_INVALID_VALUE,
		  "/buses/0/effects/1" },
		{ "buses:\n  - name: a\n    volume: 20\n", LRG_MIXER_ASSET_ERROR_INVALID_VALUE, "/buses/0/volume" },
		{ "buses:\n  - name: a\n    mute: maybe\n", LRG_MIXER_ASSET_ERROR_INVALID_VALUE, "/buses/0/mute" },
		{ "buses:\n  - name: a\n  - name: a\n", LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS, "/buses/1" },
		{ "buses:\n  - name: a\nsnapshots:\n  - name: s\n    volumes: {zz: -3}\n", LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS,
		  "/snapshots/0/volumes/zz" },
		{ "buses:\n  - name: a\nsnapshots:\n  - name: s\n    params: {a/fx/cutoff: 3}\n",
		  LRG_MIXER_ASSET_ERROR_UNKNOWN_EFFECT, "/snapshots/0/params/a/fx/cutoff" },
		{ "buses:\n  - name: a\nsnapshots:\n  - name: s\n    params: {cutoff: 3}\n", LRG_MIXER_ASSET_ERROR_PARSE,
		  "/snapshots/0/params/cutoff" },
		{ "buses:\n  - name: a\n    effects: [{type: reverb}]\nsnapshots:\n  - name: s\n    params: {a/reverb/preset: 1}\n",
		  LRG_MIXER_ASSET_ERROR_INVALID_VALUE, "/snapshots/0/params/a/reverb/preset" },
		{ "buses:\n  - name: a\n  - name: b\n    sends: [{target: a, level: 0}]\nducking:\n  - {trigger: a, target: b}\n",
		  LRG_MIXER_ASSET_ERROR_ROUTING, "/ducking/0" },
		{ "buses:\n  - name: a\nducking:\n  - {trigger: master, target: a}\n", LRG_MIXER_ASSET_ERROR_ROUTING,
		  "/ducking/0" },
		{ "buses:\n  - name: a\nducking:\n  - {trigger: a, target: a}\n", LRG_MIXER_ASSET_ERROR_ROUTING, "/ducking/0" },
		{ "buses:\n  - name: a\nducking:\n  - {trigger: a, target: master, depth: 3}\n",
		  LRG_MIXER_ASSET_ERROR_INVALID_VALUE, "/ducking/0/depth" },
		{ "buses:\n  - name: a\ndefault_snapshot: nope\n", LRG_MIXER_ASSET_ERROR_UNKNOWN_SNAPSHOT,
		  "/default_snapshot" },
		{ "buses:\n  - name: a\nsnapshots:\n  - name: s\n  - name: s\n", LRG_MIXER_ASSET_ERROR_INVALID_VALUE,
		  "/snapshots/1" },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autoptr(GError)        error = NULL;
		g_autoptr(LrgMixerAsset) asset = lrg_mixer_asset_new_from_data (cases[i].doc, -1, &error);

		if (asset != NULL || error == NULL || error->code != cases[i].code
		    || strstr (error->message, cases[i].path) == NULL)
			g_error ("case %u: expected code %d at %s, got %s", i, cases[i].code, cases[i].path,
			         error != NULL ? error->message : "success");
		g_assert_true (g_str_has_prefix (error->message, "<data>: "));
	}
}

static void
test_editing (void)
{
	g_autoptr(LrgMixerAsset) asset = lrg_mixer_asset_new ();
	g_autoptr(GError)        error = NULL;
	g_auto(GValue)           v = G_VALUE_INIT;
	g_auto(GStrv)            ids = NULL;
	g_auto(GStrv)            types = NULL;
	guint                    changes = 0;
	gdouble                  d;

	g_signal_connect_swapped (asset, "changed", G_CALLBACK (g_atomic_int_inc), &changes);

	g_assert_true (lrg_mixer_asset_add_bus (asset, "sfx", NULL));
	g_assert_true (lrg_mixer_asset_add_bus (asset, "steps", "sfx"));
	g_assert_false (lrg_mixer_asset_add_bus (asset, "sfx", NULL));
	g_assert_false (lrg_mixer_asset_add_bus (asset, "x", "nope"));
	g_assert_true (lrg_mixer_asset_add_bus (asset, "verb", NULL));
	g_assert_false (lrg_mixer_asset_set_bus_parent (asset, "sfx", "steps"));   /* below itself */
	g_assert_true (lrg_mixer_asset_set_bus_volume (asset, "sfx", 99.0));       /* clamped */
	g_assert_cmpfloat_with_epsilon (lrg_mixer_asset_get_bus_volume (asset, "sfx"), LRG_AUDIO_MIXER_MAX_GAIN_DB,
	                                1e-9);
	g_assert_true (lrg_mixer_asset_set_send (asset, "steps", "verb", -9.0));
	g_assert_false (lrg_mixer_asset_set_send (asset, "master", "verb", 0.0));

	/* effects: registry, params by name (kebab or snake), order */
	types = lrg_mixer_asset_list_effect_types ();
	g_assert_true (g_strv_contains ((const gchar * const *) types, "reverb"));
	g_assert_true (g_strv_contains ((const gchar * const *) types, "pitch-shift"));
	g_assert_cmpuint (lrg_mixer_asset_lookup_effect_type ("LrgEcho"), ==, LRG_TYPE_ECHO);
	g_assert_true (lrg_mixer_asset_add_effect (asset, "verb", "reverb", NULL, &error));
	g_assert_true (lrg_mixer_asset_add_effect (asset, "verb", "low-pass", "dark", &error));
	g_assert_false (lrg_mixer_asset_add_effect (asset, "verb", "reverb", NULL, &error));
	g_assert_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_INVALID_VALUE);
	g_clear_error (&error);
	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, 0.75);
	g_assert_true (lrg_mixer_asset_set_effect_param (asset, "verb", "reverb", "room_size", &v, &error));
	g_value_set_double (&v, 3.0);
	g_assert_false (lrg_mixer_asset_set_effect_param (asset, "verb", "reverb", "room-size", &v, &error));
	g_assert_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_INVALID_VALUE);
	g_clear_error (&error);
	g_value_unset (&v);
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "cave");
	g_assert_true (lrg_mixer_asset_set_effect_param (asset, "verb", "reverb", "preset", &v, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_mixer_asset_move_effect (asset, "verb", "dark", 0));
	ids = lrg_mixer_asset_get_effect_ids (asset, "verb");
	g_assert_cmpstrv (ids, ((const gchar *[]) { "dark", "reverb", NULL }));

	/* snapshots and ducking */
	g_assert_true (lrg_mixer_asset_add_snapshot (asset, "night", 2.0));
	g_assert_true (lrg_mixer_asset_set_snapshot_volume (asset, "night", "sfx", -20.0));
	g_assert_true (lrg_mixer_asset_set_snapshot_param (asset, "night", "verb", "dark", "cutoff", 900.0, &error));
	g_assert_false (lrg_mixer_asset_set_snapshot_param (asset, "night", "verb", "reverb", "preset", 1.0, &error));
	g_assert_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_UNKNOWN_PARAM);
	g_clear_error (&error);
	g_assert_true (lrg_mixer_asset_set_snapshot_volume (asset, "night", "sfx", NAN));
	g_assert_false (lrg_mixer_asset_get_snapshot_volume (asset, "night", "sfx", &d));
	g_assert_true (lrg_mixer_asset_set_duck (asset, "steps", "verb", -40.0, -6.0, 0.01, 0.2));
	g_assert_false (lrg_mixer_asset_set_duck (asset, "steps", "steps", -40.0, -6.0, 0.01, 0.2));
	g_assert_true (lrg_mixer_asset_validate (asset, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_mixer_asset_set_default_snapshot (asset, "night"));

	/* removing an effect drops its snapshot entries; a bus with children
	 * cannot go, a leaf takes its references along */
	g_assert_true (lrg_mixer_asset_remove_effect (asset, "verb", "dark"));
	g_assert_false (lrg_mixer_asset_get_snapshot_param (asset, "night", "verb", "dark", "cutoff", NULL));
	g_assert_false (lrg_mixer_asset_remove_bus (asset, "sfx"));
	g_assert_true (lrg_mixer_asset_remove_bus (asset, "steps"));
	g_assert_cmpuint (lrg_mixer_asset_get_duck_count (asset), ==, 0);
	g_assert_true (lrg_mixer_asset_remove_snapshot (asset, "night"));
	g_assert_null (lrg_mixer_asset_get_default_snapshot (asset));
	g_assert_cmpuint (changes, >, 15);
}

/* ==========================================================================
 * Runtime: routing, effects, meters
 * ========================================================================== */

static const gchar *routing_doc =
	"version: 1\n"
	"buses:\n"
	"  - name: sfx\n"
	"    volume: -6\n"
	"  - name: weapons\n"
	"    parent: sfx\n"
	"    volume: -3\n"
	"    sends:\n"
	"      - target: fx\n"
	"        level: -12\n"
	"  - name: fx\n"
	"    volume: 0\n";

static void
test_routing_math (void)
{
	g_autoptr(LrgMixerAsset) asset = load (routing_doc);
	g_autoptr(LrgAudioMixer) mixer = NULL;
	g_autoptr(LrgWaveData)   wave = dc_wave (8000, 1, 64, 0.25f);
	g_autoptr(GError)        error = NULL;
	gdouble                  expected, peak, rms;
	gfloat                   out;

	mixer = lrg_mixer_asset_create_mixer (asset, 8000, 1, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_audio_mixer_get_bus_count (mixer), ==, 4);
	g_assert_cmpstr (lrg_audio_mixer_get_bus_parent (mixer, "weapons"), ==, "sfx");
	g_assert_cmpfloat_with_epsilon (lrg_audio_mixer_get_send (mixer, "weapons", "fx"), db (-12.0), EPS);
	g_assert_cmpuint (lrg_audio_mixer_play (mixer, wave, "weapons", 1.0, TRUE), >, 0);

	/* weapons (-3) -> sfx (-6) -> master, plus weapons (-3) -> send (-12) -> fx (0) -> master */
	out = render_last (mixer, 32, 0);
	expected = 0.25 * db (-3) * db (-6) + 0.25 * db (-3) * db (-12);
	g_assert_cmpfloat_with_epsilon (out, expected, EPS);

	/* meters: a DC block has peak == RMS == level */
	g_assert_true (lrg_audio_mixer_get_bus_meter (mixer, "weapons", -1, &peak, &rms));
	g_assert_cmpfloat_with_epsilon (peak, 0.25 * db (-3), EPS);
	g_assert_cmpfloat_with_epsilon (rms, 0.25 * db (-3), EPS);
	g_assert_true (lrg_audio_mixer_get_bus_meter (mixer, "master", 0, &peak, NULL));
	g_assert_cmpfloat_with_epsilon (peak, expected, EPS);
	g_assert_false (lrg_audio_mixer_get_bus_meter (mixer, "master", 1, &peak, NULL));   /* mono */

	/* re-applying a changed layout updates the live mixer */
	lrg_mixer_asset_set_bus_volume (asset, "sfx", 0.0);
	lrg_mixer_asset_remove_send (asset, "weapons", "fx");
	g_assert_true (lrg_mixer_asset_apply (asset, mixer, &error));
	g_assert_no_error (error);
	out = render_last (mixer, 32, 0);
	g_assert_cmpfloat_with_epsilon (out, 0.25 * db (-3), EPS);
	g_assert_cmpfloat (lrg_audio_mixer_get_send (mixer, "weapons", "fx"), ==, 0.0);

	/* a mixer bus under another parent is an error */
	{
		g_autoptr(LrgAudioMixer) other = lrg_audio_mixer_new (8000, 1);

		lrg_audio_mixer_add_bus (other, "fx", "master");
		lrg_audio_mixer_add_bus (other, "weapons", "fx");
		g_assert_false (lrg_mixer_asset_apply (asset, other, &error));
		g_assert_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_ROUTING);
	}
}

static void
test_effect_chain (void)
{
	g_autoptr(LrgMixerAsset) asset = load (full_doc);
	g_autoptr(LrgAudioMixer) mixer = NULL;
	g_autoptr(GError)        error = NULL;
	LrgAudioEffectChain     *chain;
	LrgAudioEffect          *effect;
	gfloat                   f = 0.0f;
	gint                     preset = 0;

	mixer = lrg_mixer_asset_create_mixer (asset, 48000, 2, &error);
	g_assert_no_error (error);

	chain = lrg_audio_mixer_get_bus_chain (mixer, "music");
	g_assert_cmpuint (lrg_audio_effect_chain_get_count (chain), ==, 1);
	effect = lrg_audio_effect_chain_get_effect (chain, 0);
	g_assert_true (LRG_IS_LOW_PASS (effect));
	g_object_get (effect, "cutoff", &f, NULL);
	g_assert_cmpfloat_with_epsilon (f, 18000.0f, 1e-3);
	g_object_get (effect, "resonance", &f, NULL);
	g_assert_cmpfloat_with_epsilon (f, 0.8f, 1e-6);

	/* the preset first, then wet over it (file order) */
	chain = lrg_audio_mixer_get_bus_chain (mixer, "reverb");
	effect = lrg_audio_effect_chain_get_effect (chain, 0);
	g_assert_true (LRG_IS_REVERB (effect));
	g_assert_false (lrg_audio_effect_get_enabled (effect));
	g_object_get (effect, "preset", &preset, "wet", &f, NULL);
	g_assert_cmpint (preset, ==, LRG_REVERB_PRESET_HALL);
	g_assert_cmpfloat_with_epsilon (f, 0.5f, 1e-6);
	g_object_get (effect, "room-size", &f, NULL);
	g_assert_cmpfloat_with_epsilon (f, 0.82f, 1e-6);   /* the hall's */

	/* solo and mute land on the mixer */
	g_assert_true (lrg_audio_mixer_get_bus_solo (mixer, "dialogue"));
	g_assert_true (lrg_audio_mixer_get_bus_muted (mixer, "reverb"));
	g_assert_cmpuint (lrg_audio_mixer_get_duck_count (mixer), ==, 1);

	/* re-applying replaces the chains rather than appending */
	g_assert_true (lrg_mixer_asset_apply (asset, mixer, &error));
	g_assert_cmpuint (lrg_audio_effect_chain_get_count (lrg_audio_mixer_get_bus_chain (mixer, "music")), ==, 1);
}

/* An effect chain on a mixer bus really processes: a disabled-by-default
 * passthrough check and an echo whose first repeat lands where it should. */
static void
test_chain_processing (void)
{
	g_autoptr(LrgAudioMixer) mixer = lrg_audio_mixer_new (8000, 1);
	g_autoptr(LrgEcho)       echo = lrg_echo_new (0.01f, 0.0f);   /* 80 frames */
	g_autoptr(LrgWaveData)   click = NULL;
	g_autofree gfloat       *pcm = NULL;
	gfloat                   one = 1.0f;
	gsize                    n = 0;

	click = lrg_wave_data_new_from_samples (8000, 32, 1, (const guint8 *) &one, sizeof one);
	lrg_audio_mixer_add_bus (mixer, "fx", "master");
	lrg_audio_effect_chain_add (lrg_audio_mixer_get_bus_chain (mixer, "fx"), LRG_AUDIO_EFFECT (echo));
	lrg_echo_set_dry (echo, 1.0f);
	lrg_echo_set_wet (echo, 0.5f);
	lrg_audio_mixer_play (mixer, click, "fx", 1.0, FALSE);
	pcm = lrg_audio_mixer_render (mixer, 600, &n);   /* crosses block boundaries */
	g_assert_cmpfloat_with_epsilon (pcm[0], 1.0f, 1e-6);
	g_assert_cmpfloat_with_epsilon (pcm[80], 0.5f, 1e-6);
	g_assert_cmpfloat_with_epsilon (pcm[79], 0.0f, 1e-6);
}

/* ==========================================================================
 * Snapshots
 * ========================================================================== */

static const gchar *snapshot_doc =
	"version: 1\n"
	"buses:\n"
	"  - name: music\n"
	"    effects:\n"
	"      - type: low-pass\n"
	"        id: muffle\n"
	"        params: { cutoff: 18000, glide: 0 }\n"
	"  - name: verb\n"
	"snapshots:\n"
	"  - name: quiet\n"
	"    blend: 1\n"
	"    volumes: { music: -20 }\n"
	"    params: { music/muffle/cutoff: 500 }\n"
	"    sends:\n"
	"      - { source: music, target: verb, level: 0 }\n"
	"  - name: loud\n"
	"    volumes: { music: -10 }\n";

static void
test_snapshot_blend (void)
{
	g_autoptr(LrgMixerAsset) asset = load (snapshot_doc);
	g_autoptr(LrgAudioMixer) mixer = NULL;
	g_autoptr(GError)        error = NULL;
	LrgAudioEffect          *muffle;
	gfloat                   cutoff = 0.0f;
	gdouble                  expected;
	const gchar             *names[2] = { "quiet", "loud" };
	gdouble                  weights[2] = { 0.25, 0.75 };

	mixer = lrg_mixer_asset_create_mixer (asset, 8000, 1, &error);
	g_assert_no_error (error);
	muffle = lrg_audio_effect_chain_get_effect (lrg_audio_mixer_get_bus_chain (mixer, "music"), 0);
	g_assert_cmpfloat (lrg_audio_mixer_get_send (mixer, "music", "verb"), ==, 0.0);

	/* a 1 s blend (8000 frames): halfway at 4000 frames */
	g_assert_true (lrg_mixer_asset_apply_snapshot (asset, mixer, "quiet", -1.0, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_audio_mixer_is_ramping (mixer));
	g_free (lrg_audio_mixer_render (mixer, 4000, &(gsize) { 0 }));
	g_assert_cmpfloat_with_epsilon (lrg_audio_mixer_get_bus_gain (mixer, "music"), 1.0 + (db (-20) - 1.0) * 0.5,
	                                1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_audio_mixer_get_send (mixer, "music", "verb"), 0.5, 1e-9);

	/* the parameter steps per 256-frame block: the last block of this
	 * render began at frame 3840 */
	g_object_get (muffle, "cutoff", &cutoff, NULL);
	expected = 18000.0 + (500.0 - 18000.0) * 3840.0 / 8000.0;
	g_assert_cmpfloat_with_epsilon (cutoff, expected, 0.01);

	/* the end values land exactly */
	g_free (lrg_audio_mixer_render (mixer, 4000, &(gsize) { 0 }));
	g_assert_false (lrg_audio_mixer_is_ramping (mixer));
	g_assert_cmpfloat_with_epsilon (lrg_audio_mixer_get_bus_gain (mixer, "music"), db (-20), 1e-12);
	g_assert_cmpfloat_with_epsilon (lrg_audio_mixer_get_send (mixer, "music", "verb"), 1.0, 1e-12);
	g_object_get (muffle, "cutoff", &cutoff, NULL);
	g_assert_cmpfloat_with_epsilon (cutoff, 500.0f, 1e-3);

	/* leaving the snapshot returns what it changed (to the base layout) */
	g_assert_true (lrg_mixer_asset_apply_snapshot (asset, mixer, NULL, 0.0, &error));
	g_assert_cmpfloat_with_epsilon (lrg_audio_mixer_get_bus_gain (mixer, "music"), 1.0, 1e-12);
	g_assert_cmpfloat (lrg_audio_mixer_get_send (mixer, "music", "verb"), ==, 0.0);
	g_object_get (muffle, "cutoff", &cutoff, NULL);
	g_assert_cmpfloat_with_epsilon (cutoff, 18000.0f, 1e-3);

	/* "loud" does not name the cutoff: it stays at the base value */
	g_assert_true (lrg_mixer_asset_apply_snapshot (asset, mixer, "loud", 0.0, &error));
	g_assert_cmpfloat_with_epsilon (lrg_audio_mixer_get_bus_gain (mixer, "music"), db (-10), 1e-12);

	/* weighted: volumes average in dB */
	g_assert_true (lrg_mixer_asset_blend_snapshots (asset, mixer, names, weights, 2, 0.0, &error));
	g_assert_cmpfloat_with_epsilon (lrg_audio_mixer_get_bus_gain (mixer, "music"), db (0.25 * -20 + 0.75 * -10),
	                                1e-12);

	/* errors */
	g_assert_false (lrg_mixer_asset_apply_snapshot (asset, mixer, "nope", 0.0, &error));
	g_assert_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_UNKNOWN_SNAPSHOT);
}

/* the blended gain really scales samples, frame by frame */
static void
test_ramp_samples (void)
{
	g_autoptr(LrgAudioMixer) mixer = lrg_audio_mixer_new (8000, 1);
	g_autoptr(LrgWaveData)   wave = dc_wave (8000, 1, 16, 1.0f);
	g_autofree gfloat       *pcm = NULL;
	gsize                    n = 0;
	guint                    i;

	lrg_audio_mixer_add_bus (mixer, "a", "master");
	lrg_audio_mixer_play (mixer, wave, "a", 0.5, TRUE);
	g_assert_true (lrg_audio_mixer_ramp_bus_gain (mixer, "a", 0.0, 100.0 / 8000.0));
	pcm = lrg_audio_mixer_render (mixer, 120, &n);
	for (i = 0; i < 100; i++)
		g_assert_cmpfloat_with_epsilon (pcm[i], 0.5 * (1.0 - i / 100.0), 1e-6);
	for (i = 100; i < 120; i++)
		g_assert_cmpfloat (pcm[i], ==, 0.0f);
}

/* ==========================================================================
 * Ducking, solo
 * ========================================================================== */

static void
test_ducking (void)
{
	g_autoptr(LrgAudioMixer) mixer = lrg_audio_mixer_new (8000, 1);
	g_autoptr(LrgWaveData)   wave = dc_wave (8000, 1, 32, 0.5f);
	g_autofree gfloat       *pcm = NULL;
	gsize                    n = 0;
	guint64                  voice;
	gdouble                  depth = db (-12.0);
	gdouble                  a = 1.0 - exp (-1.0 / (0.01 * 8000.0));
	gdouble                  r = 1.0 - exp (-1.0 / (0.05 * 8000.0));
	gdouble                  g_end;
	guint                    checks[] = { 0, 10, 79, 400, 1999 };
	guint                    i;

	lrg_audio_mixer_add_bus (mixer, "dialogue", "master");
	lrg_audio_mixer_add_bus (mixer, "music", "master");
	g_assert_true (lrg_audio_mixer_set_duck (mixer, "dialogue", "music", -20.0, -12.0, 0.01, 0.05));
	g_assert_false (lrg_audio_mixer_set_duck (mixer, "music", "music", -20.0, -12.0, 0.01, 0.05));
	g_assert_false (lrg_audio_mixer_set_duck (mixer, "master", "music", -20.0, -12.0, 0.01, 0.05));
	g_assert_false (lrg_audio_mixer_set_duck (mixer, "dialogue", "music", -20.0, 3.0, 0.01, 0.05));
	lrg_audio_mixer_play (mixer, wave, "music", 1.0, TRUE);
	voice = lrg_audio_mixer_play (mixer, wave, "dialogue", 1.0, TRUE);

	/* attack: g(n) = depth + (1 - depth) (1 - a)^(n + 1) */
	pcm = lrg_audio_mixer_render (mixer, 2000, &n);
	for (i = 0; i < G_N_ELEMENTS (checks); i++)
	{
		gdouble g = depth + (1.0 - depth) * pow (1.0 - a, checks[i] + 1);

		g_assert_cmpfloat_with_epsilon (pcm[checks[i]], 0.5 + 0.5 * g, 1e-5);
	}
	g_end = depth + (1.0 - depth) * pow (1.0 - a, 2000);
	g_assert_cmpfloat_with_epsilon (lrg_audio_mixer_get_bus_duck_gain (mixer, "music"), g_end, 1e-6);

	/* release once the dialogue stops: g(m) = 1 + (g_end - 1) (1 - r)^(m + 1) */
	lrg_audio_mixer_stop_voice (mixer, voice);
	g_free (pcm);
	pcm = lrg_audio_mixer_render (mixer, 1000, &n);
	g_assert_cmpfloat_with_epsilon (pcm[0], 0.5 * (1.0 + (g_end - 1.0) * (1.0 - r)), 1e-5);
	g_assert_cmpfloat_with_epsilon (pcm[999], 0.5 * (1.0 + (g_end - 1.0) * pow (1.0 - r, 1000)), 1e-5);

	/* a rule can be removed; the target is at unity again */
	g_assert_true (lrg_audio_mixer_remove_duck (mixer, "dialogue", "music"));
	g_assert_cmpuint (lrg_audio_mixer_get_duck_count (mixer), ==, 0);
}

/* The same ducking rule loaded from a .rmixer: below the threshold
 * nothing happens. */
static void
test_ducking_asset_threshold (void)
{
	g_autoptr(LrgMixerAsset) asset = load ("buses:\n  - name: vo\n  - name: bgm\n"
	                                       "ducking:\n  - {trigger: vo, target: bgm, threshold: -6, depth: -20,"
	                                       " attack: 0, release: 0}\n");
	g_autoptr(LrgAudioMixer) mixer = lrg_mixer_asset_create_mixer (asset, 8000, 1, NULL);
	g_autoptr(LrgWaveData)   quiet = dc_wave (8000, 1, 8, 0.25f);   /* -12 dB: below */
	g_autoptr(LrgWaveData)   loud = dc_wave (8000, 1, 8, 0.75f);    /* -2.5 dB: above */
	g_autoptr(LrgWaveData)   bed = dc_wave (8000, 1, 8, 0.1f);
	guint64                  v;

	lrg_audio_mixer_play (mixer, bed, "bgm", 1.0, TRUE);
	v = lrg_audio_mixer_play (mixer, quiet, "vo", 1.0, TRUE);
	g_assert_cmpfloat_with_epsilon (render_last (mixer, 16, 0), 0.25 + 0.1, 1e-6);
	lrg_audio_mixer_stop_voice (mixer, v);
	lrg_audio_mixer_play (mixer, loud, "vo", 1.0, TRUE);
	/* attack 0: the duck is instant */
	g_assert_cmpfloat_with_epsilon (render_last (mixer, 16, 0), 0.75 + 0.1 * db (-20), 1e-6);
}

static void
test_solo (void)
{
	g_autoptr(LrgAudioMixer) mixer = lrg_audio_mixer_new (8000, 1);
	g_autoptr(LrgWaveData)   wa = dc_wave (8000, 1, 8, 0.1f);
	g_autoptr(LrgWaveData)   wb = dc_wave (8000, 1, 8, 0.2f);
	g_autoptr(LrgWaveData)   wc = dc_wave (8000, 1, 8, 0.4f);
	gdouble                  peak = 0.0;

	lrg_audio_mixer_add_bus (mixer, "a", "master");
	lrg_audio_mixer_add_bus (mixer, "b", "master");
	lrg_audio_mixer_add_bus (mixer, "c", "a");
	lrg_audio_mixer_play (mixer, wa, "a", 1.0, TRUE);
	lrg_audio_mixer_play (mixer, wb, "b", 1.0, TRUE);
	lrg_audio_mixer_play (mixer, wc, "c", 1.0, TRUE);
	g_assert_cmpfloat_with_epsilon (render_last (mixer, 8, 0), 0.7, 1e-6);

	/* soloing a: a, its child c and master are heard; b is not */
	lrg_audio_mixer_set_bus_solo (mixer, "a", TRUE);
	g_assert_cmpfloat_with_epsilon (render_last (mixer, 8, 0), 0.5, 1e-6);
	g_assert_false (lrg_audio_mixer_is_bus_audible (mixer, "b"));
	g_assert_true (lrg_audio_mixer_is_bus_audible (mixer, "c"));
	lrg_audio_mixer_get_bus_meter (mixer, "b", -1, &peak, NULL);
	g_assert_cmpfloat (peak, ==, 0.0);

	/* soloing b instead */
	lrg_audio_mixer_set_bus_solo (mixer, "a", FALSE);
	lrg_audio_mixer_set_bus_solo (mixer, "b", TRUE);
	g_assert_cmpfloat_with_epsilon (render_last (mixer, 8, 0), 0.2, 1e-6);

	/* mute wins over solo */
	lrg_audio_mixer_set_bus (mixer, "b", 1.0, TRUE);
	g_assert_cmpfloat_with_epsilon (render_last (mixer, 8, 0), 0.0, 1e-6);
}

/* ==========================================================================
 * Voice controls
 * ========================================================================== */

static void
test_voice_controls (void)
{
	g_autoptr(LrgAudioMixer) mono = lrg_audio_mixer_new (8000, 1);
	g_autoptr(LrgAudioMixer) stereo = lrg_audio_mixer_new (8000, 2);
	g_autoptr(LrgWaveData)   ramp = ramp_wave (8000, 200, 0.001f);
	g_autoptr(LrgWaveData)   dc = dc_wave (8000, 2, 64, 0.5f);
	g_autofree gfloat       *pcm = NULL;
	gsize                    n = 0;
	guint64                  v;
	guint                    i;

	/* pitch 2 reads every other frame, then ends early */
	v = lrg_audio_mixer_play (mono, ramp, "master", 1.0, FALSE);
	g_assert_true (lrg_audio_mixer_set_voice_pitch (mono, v, 2.0));
	g_assert_false (lrg_audio_mixer_set_voice_pitch (mono, v, 20.0));
	pcm = lrg_audio_mixer_render (mono, 120, &n);
	for (i = 0; i < 100; i++)
		g_assert_cmpfloat_with_epsilon (pcm[i], 2 * i * 0.001, 1e-6);
	g_assert_cmpfloat (pcm[100], ==, 0.0f);
	g_assert_false (lrg_audio_mixer_has_voice (mono, v));

	/* pitch 0.5 interpolates between frames */
	v = lrg_audio_mixer_play (mono, ramp, "master", 1.0, FALSE);
	lrg_audio_mixer_set_voice_pitch (mono, v, 0.5);
	g_free (pcm);
	pcm = lrg_audio_mixer_render (mono, 100, &n);
	for (i = 0; i < 100; i++)
		g_assert_cmpfloat_with_epsilon (pcm[i], i * 0.5 * 0.001, 1e-6);

	/* gain ramps linearly over the given time */
	lrg_audio_mixer_stop_voice (mono, v);
	v = lrg_audio_mixer_play (mono, ramp, "master", 1.0, TRUE);
	lrg_audio_mixer_set_voice_gain (mono, v, 0.0, 50.0 / 8000.0);
	g_free (pcm);
	pcm = lrg_audio_mixer_render (mono, 60, &n);
	for (i = 0; i < 50; i++)
		g_assert_cmpfloat_with_epsilon (pcm[i], i * 0.001 * (1.0 - i / 50.0), 1e-6);
	g_assert_cmpfloat (pcm[55], ==, 0.0f);
	g_assert_cmpstr (lrg_audio_mixer_get_voice_bus (mono, v), ==, "master");

	/* balance: the far channel scales by 1 - |pan| */
	v = lrg_audio_mixer_play (stereo, dc, "master", 1.0, TRUE);
	g_assert_true (lrg_audio_mixer_set_voice_pan (stereo, v, 0.5, 0.0));
	g_assert_cmpfloat_with_epsilon (render_last (stereo, 8, 0), 0.25, 1e-6);
	g_assert_cmpfloat_with_epsilon (render_last (stereo, 8, 1), 0.5, 1e-6);
	lrg_audio_mixer_set_voice_pan (stereo, v, -1.0, 0.0);
	g_assert_cmpfloat_with_epsilon (render_last (stereo, 8, 0), 0.5, 1e-6);
	g_assert_cmpfloat_with_epsilon (render_last (stereo, 8, 1), 0.0, 1e-6);
	g_assert_false (lrg_audio_mixer_set_voice_pan (stereo, v, 2.0, 0.0));

	/* the voice low-pass converges on DC and lags a step */
	lrg_audio_mixer_set_voice_pan (stereo, v, 0.0, 0.0);
	g_assert_true (lrg_audio_mixer_set_voice_lowpass (stereo, v, 100.0));
	g_assert_false (lrg_audio_mixer_set_voice_lowpass (stereo, v, 5000.0));   /* >= Nyquist */
	{
		gdouble alpha = 1.0 - exp (-2.0 * G_PI * 100.0 / 8000.0);

		g_assert_cmpfloat_with_epsilon (render_last (stereo, 1, 0), 0.5 * alpha, 1e-6);
	}
}

static void
test_db_helpers (void)
{
	g_assert_cmpfloat_with_epsilon (lrg_audio_db_to_gain (0.0), 1.0, 1e-12);
	g_assert_cmpfloat_with_epsilon (lrg_audio_db_to_gain (-20.0), 0.1, 1e-12);
	g_assert_cmpfloat (lrg_audio_db_to_gain (-INFINITY), ==, 0.0);
	g_assert_cmpfloat (lrg_audio_db_to_gain (LRG_AUDIO_SILENCE_DB), ==, 0.0);
	g_assert_cmpfloat_with_epsilon (lrg_audio_gain_to_db (0.5), -6.0206, 1e-4);
	g_assert_cmpfloat (lrg_audio_gain_to_db (0.0), ==, LRG_AUDIO_SILENCE_DB);
	g_assert_cmpfloat_with_epsilon (lrg_audio_db_to_gain (LRG_AUDIO_MIXER_MAX_GAIN_DB), 4.0, 1e-4);
}

/* ==========================================================================
 * Spatial helpers
 * ========================================================================== */

static void
test_rolloff_curves (void)
{
	g_autoptr(LrgKeyframeCurve) curve = lrg_keyframe_curve_new ();
	gfloat                      d;

	/* linear: (max - d) / (max - min) */
	g_assert_cmpfloat_with_epsilon (lrg_audio_rolloff_evaluate (LRG_AUDIO_ROLLOFF_LINEAR, 1, 21, 1, NULL, 6),
	                                0.75, 1e-6);
	/* inverse, rescaled to reach 0 at max */
	{
		gdouble inv_d = 1.0 / (1.0 + 1.0 * 4.0);
		gdouble inv_max = 1.0 / (1.0 + 1.0 * 49.0);

		g_assert_cmpfloat_with_epsilon (lrg_audio_rolloff_evaluate (LRG_AUDIO_ROLLOFF_INVERSE, 1, 50, 1, NULL, 5),
		                                (inv_d - inv_max) / (1.0 - inv_max), 1e-6);
	}
	/* logarithmic: 1 - ln (d / min) / ln (max / min) */
	g_assert_cmpfloat_with_epsilon (lrg_audio_rolloff_evaluate (LRG_AUDIO_ROLLOFF_LOGARITHMIC, 2, 32, 1, NULL, 8),
	                                1.0 - log (4.0) / log (16.0), 1e-6);
	/* every built-in curve: 1 inside min, 0 beyond max */
	for (d = 0.0f; d <= 1.0f; d += 0.5f)
		g_assert_cmpfloat (lrg_audio_rolloff_evaluate (LRG_AUDIO_ROLLOFF_LOGARITHMIC, 1, 9, 1, NULL, d), ==, 1.0f);
	g_assert_cmpfloat (lrg_audio_rolloff_evaluate (LRG_AUDIO_ROLLOFF_INVERSE, 1, 9, 1, NULL, 9.5f), ==, 0.0f);

	/* custom: a keyframe curve over normalised distance */
	lrg_keyframe_curve_add_key (curve, 0.0f, 1.0f, LRG_EASING_LINEAR);
	lrg_keyframe_curve_add_key (curve, 0.5f, 0.2f, LRG_EASING_LINEAR);
	lrg_keyframe_curve_add_key (curve, 1.0f, 0.0f, LRG_EASING_LINEAR);
	g_assert_cmpfloat_with_epsilon (lrg_audio_rolloff_evaluate (LRG_AUDIO_ROLLOFF_CUSTOM, 1, 21, 1, curve, 6),
	                                0.6, 1e-6);
	g_assert_cmpfloat_with_epsilon (lrg_audio_rolloff_evaluate (LRG_AUDIO_ROLLOFF_CUSTOM, 1, 21, 1, curve, 16),
	                                0.1, 1e-6);
	g_assert_cmpfloat (lrg_audio_rolloff_evaluate (LRG_AUDIO_ROLLOFF_CUSTOM, 1, 21, 1, curve, 0.2f), ==, 1.0f);
	g_assert_cmpfloat (lrg_audio_rolloff_evaluate (LRG_AUDIO_ROLLOFF_CUSTOM, 1, 21, 1, curve, 99.0f), ==, 0.0f);
	/* custom without a curve falls back to linear */
	g_assert_cmpfloat_with_epsilon (lrg_audio_rolloff_evaluate (LRG_AUDIO_ROLLOFF_CUSTOM, 1, 21, 1, NULL, 6),
	                                0.75, 1e-6);
}

static void
test_cone_and_doppler (void)
{
	gfloat listener[3] = { 0.0f, 0.0f, 0.0f };
	gfloat source[3] = { 10.0f, 0.0f, 0.0f };
	gfloat toward[3] = { -34.3f, 0.0f, 0.0f };
	gfloat lvel[3] = { 34.3f, 0.0f, 0.0f };

	/* cone: inner 90 (45 half), outer 180 (90 half), outer gain 0.2 */
	g_assert_cmpfloat (lrg_audio_cone_gain (90, 180, 0.2f, 30), ==, 1.0f);
	g_assert_cmpfloat (lrg_audio_cone_gain (90, 180, 0.2f, 45), ==, 1.0f);
	g_assert_cmpfloat_with_epsilon (lrg_audio_cone_gain (90, 180, 0.2f, 67.5f), 0.6, 1e-6);
	g_assert_cmpfloat_with_epsilon (lrg_audio_cone_gain (90, 180, 0.2f, 120), 0.2, 1e-6);
	g_assert_cmpfloat (lrg_audio_cone_gain (360, 360, 0.0f, 180), ==, 1.0f);

	/* doppler: c / (c - vs) for a source approaching at c / 10 */
	g_assert_cmpfloat_with_epsilon (lrg_audio_doppler_pitch (listener, NULL, source, toward, 343.0f, 1.0f),
	                                343.0 / (343.0 - 34.3), 1e-5);
	/* receding: lower */
	toward[0] = 34.3f;
	g_assert_cmpfloat_with_epsilon (lrg_audio_doppler_pitch (listener, NULL, source, toward, 343.0f, 1.0f),
	                                343.0 / (343.0 + 34.3), 1e-5);
	/* a listener moving toward the source: (c + v) / c */
	g_assert_cmpfloat_with_epsilon (lrg_audio_doppler_pitch (listener, lvel, source, NULL, 343.0f, 1.0f),
	                                (343.0 + 34.3) / 343.0, 1e-5);
	/* factor 0 turns it off; coincident positions give 1 */
	g_assert_cmpfloat (lrg_audio_doppler_pitch (listener, lvel, source, toward, 343.0f, 0.0f), ==, 1.0f);
	g_assert_cmpfloat (lrg_audio_doppler_pitch (listener, lvel, listener, toward, 343.0f, 1.0f), ==, 1.0f);
}

/* ==========================================================================
 * The audio source component (headless, through a mixer)
 * ========================================================================== */

typedef struct
{
	LrgWorld3D              *world;
	LrgAudioManager         *manager;
	LrgAudioMixer           *mixer;
	LrgEntity3D             *entity;
	LrgAudioSourceComponent *src;
} SourceFixture;

static void
source_setup (SourceFixture *f,
              gconstpointer  data)
{
	g_autoptr(LrgWaveData) wave = dc_wave (8000, 2, 64, 0.5f);
	gfloat                 lpos[3] = { 0.0f, 0.0f, 0.0f };
	gfloat                 lfwd[3] = { 0.0f, 0.0f, -1.0f };

	(void) data;
	f->world = lrg_world3d_new ();
	f->manager = g_object_new (LRG_TYPE_AUDIO_MANAGER, NULL);
	lrg_audio_manager_set_listener (f->manager, lpos, lfwd, NULL);
	f->mixer = lrg_audio_mixer_new (8000, 2);
	lrg_audio_mixer_add_bus (f->mixer, "sfx", "master");
	lrg_audio_mixer_set_bus (f->mixer, "sfx", db (-6.0), FALSE);

	f->entity = lrg_entity3d_new ();
	lrg_entity3d_set_position_xyz (f->entity, 0.0f, 0.0f, -6.0f);
	lrg_world3d_add_entity (f->world, f->entity);
	f->src = lrg_audio_source_component_new ();
	g_object_set (f->src, "audio-manager", f->manager, "min-distance", 1.0f, "max-distance", 21.0f,
	              "rolloff-mode", LRG_AUDIO_ROLLOFF_LINEAR, "loop", TRUE, "wave", wave, "doppler-factor", 0.0f,
	              NULL);
	lrg_entity3d_add_component (f->entity, LRG_COMPONENT (f->src));
}

static void
source_teardown (SourceFixture *f,
                 gconstpointer  data)
{
	(void) data;
	g_clear_object (&f->src);
	g_clear_object (&f->entity);
	g_clear_object (&f->world);
	g_clear_object (&f->mixer);
	g_clear_object (&f->manager);
}

static void
test_source_mixer_voice (SourceFixture *f,
                         gconstpointer  data)
{
	gfloat gain = 0.0f, pan = 9.0f, pitch = 0.0f;
	gfloat lpos[3] = { 0.0f, 0.0f, 0.0f };
	gfloat lfwd[3] = { 0.0f, 0.0f, -1.0f };

	(void) data;

	/* straight ahead at 6 of 1 .. 21: linear gain 0.75, centred */
	lrg_audio_source_component_evaluate (f->src, lpos, lfwd, NULL, NULL, &gain, &pan, &pitch);
	g_assert_cmpfloat_with_epsilon (gain, 0.75, 1e-6);
	g_assert_cmpfloat_with_epsilon (pan, 0.0, 1e-6);
	g_assert_cmpfloat (pitch, ==, 1.0f);
	g_assert_cmpfloat_with_epsilon (lrg_audio_source_component_get_distance_gain (f->src, 11.0f), 0.5, 1e-6);

	/* an explicit mixer: a mixer voice on master */
	lrg_audio_source_component_set_mixer (f->src, f->mixer);
	g_assert_cmpuint (lrg_audio_source_component_play (f->src), >, 0);
	g_assert_cmpuint (lrg_audio_source_component_get_voice (f->src), ==, 0);
	g_assert_true (lrg_audio_source_component_is_playing (f->src));
	g_assert_cmpstr (lrg_audio_mixer_get_voice_bus (f->mixer, lrg_audio_source_component_get_mixer_voice (f->src)),
	                 ==, "master");
	g_assert_cmpfloat_with_epsilon (render_last (f->mixer, 16, 0), 0.5 * 0.75, 1e-6);
	g_assert_cmpfloat_with_epsilon (render_last (f->mixer, 16, 1), 0.5 * 0.75, 1e-6);

	/* to the listener's right (+X when facing -Z): fully right */
	lrg_entity3d_set_position_xyz (f->entity, 6.0f, 0.0f, 0.0f);
	lrg_audio_source_component_update_spatial (f->src, 0.0f);
	g_assert_cmpfloat_with_epsilon (render_last (f->mixer, 16, 0), 0.0, 1e-6);
	g_assert_cmpfloat_with_epsilon (render_last (f->mixer, 16, 1), 0.5 * 0.75, 1e-6);

	/* a custom curve from level-param text */
	lrg_entity3d_set_position_xyz (f->entity, 0.0f, 0.0f, -6.0f);
	g_object_set (f->src, "rolloff-curve-keys", "0:1, 0.5:0.2, 1:0", NULL);
	{
		gint              mode = 0;
		g_autofree gchar *keys = NULL;

		g_object_get (f->src, "rolloff-mode", &mode, "rolloff-curve-keys", &keys, NULL);
		g_assert_cmpint (mode, ==, LRG_AUDIO_ROLLOFF_CUSTOM);
		g_assert_cmpstr (keys, ==, "0:1, 0.5:0.2, 1:0");
	}
	lrg_audio_source_component_update_spatial (f->src, 0.0f);
	g_assert_cmpfloat_with_epsilon (render_last (f->mixer, 16, 0), 0.5 * 0.6, 1e-6);

	/* stop */
	lrg_audio_source_component_stop (f->src);
	g_assert_cmpuint (lrg_audio_mixer_get_voice_count (f->mixer), ==, 0);
}

static void
test_source_bus_routing (SourceFixture *f,
                         gconstpointer  data)
{
	(void) data;

	/* a bus without an explicit mixer uses the manager's */
	lrg_audio_manager_set_mixer (f->manager, f->mixer);
	g_assert_true (lrg_audio_manager_get_mixer (f->manager) == f->mixer);
	lrg_audio_source_component_set_bus (f->src, "sfx");
	g_assert_cmpuint (lrg_audio_source_component_play (f->src), >, 0);
	g_assert_cmpstr (lrg_audio_mixer_get_voice_bus (f->mixer, lrg_audio_source_component_get_mixer_voice (f->src)),
	                 ==, "sfx");
	g_assert_cmpfloat_with_epsilon (render_last (f->mixer, 16, 0), 0.5 * 0.75 * db (-6.0), 1e-6);

	/* an unknown bus: nothing plays */
	lrg_audio_source_component_set_bus (f->src, "nope");
	g_assert_cmpuint (lrg_audio_source_component_play (f->src), ==, 0);
	lrg_audio_manager_set_mixer (f->manager, NULL);
}

static void
test_source_cone (SourceFixture *f,
                  gconstpointer  data)
{
	gfloat lpos[3] = { 0.0f, 0.0f, 0.0f };
	gfloat lfwd[3] = { 0.0f, 0.0f, -1.0f };
	gfloat yaw[3] = { 0.0f, (gfloat) G_PI, 0.0f };
	gfloat gain = 0.0f;

	(void) data;
	g_object_set (f->src, "cone-inner-angle", 90.0f, "cone-outer-angle", 180.0f, "cone-outer-gain", 0.2f, NULL);

	/* the entity faces -Z, away from the listener at the origin: behind */
	lrg_audio_source_component_evaluate (f->src, lpos, lfwd, NULL, NULL, &gain, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (gain, 0.75 * 0.2, 1e-6);

	/* turned around it faces the listener */
	lrg_entity3d_set_rotation_euler (f->entity, yaw);
	lrg_audio_source_component_evaluate (f->src, lpos, lfwd, NULL, NULL, &gain, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (gain, 0.75, 1e-5);

	/* spatial blend 0: flat */
	g_object_set (f->src, "spatial-blend", 0.0f, NULL);
	lrg_audio_source_component_evaluate (f->src, lpos, lfwd, NULL, NULL, &gain, NULL, NULL);
	g_assert_cmpfloat (gain, ==, 1.0f);
}

static void
test_source_doppler (SourceFixture *f,
                     gconstpointer  data)
{
	gfloat lpos[3] = { 0.0f, 0.0f, 0.0f };
	gfloat lfwd[3] = { 0.0f, 0.0f, -1.0f };
	gfloat vel[3];
	gfloat pitch = 0.0f;

	(void) data;
	g_object_set (f->src, "doppler-factor", 1.0f, "pitch", 1.0f, NULL);
	lrg_audio_source_component_set_mixer (f->src, f->mixer);
	lrg_audio_source_component_play (f->src);

	/* moving 1 unit toward the listener in 0.1 s: 10 units/s */
	lrg_entity3d_set_position_xyz (f->entity, 0.0f, 0.0f, -5.0f);
	lrg_world3d_late_update (f->world, 0.1f);
	lrg_audio_source_component_get_velocity (f->src, vel);
	g_assert_cmpfloat_with_epsilon (vel[2], 10.0, 1e-4);
	lrg_audio_source_component_evaluate (f->src, lpos, lfwd, NULL, NULL, NULL, NULL, &pitch);
	g_assert_cmpfloat_with_epsilon (pitch, 343.0 / (343.0 - 10.0), 1e-5);
}

static void
test_source_occlusion (SourceFixture *f,
                       gconstpointer  data)
{
	g_autoptr(LrgPhysicsShape) box = lrg_physics_shape_new_box (1.0f, 1.0f, 1.0f);
	g_autoptr(GError)          error = NULL;
	LrgDynamicsWorld          *physics;
	gfloat                     at[3] = { 0.0f, 0.0f, -3.0f };
	gfloat                     lpos[3] = { 0.0f, 0.0f, 0.0f };
	gfloat                     lfwd[3] = { 0.0f, 0.0f, -1.0f };
	gfloat                     gain = 0.0f;
	guint                      id = 0;

	(void) data;
	physics = lrg_world3d_ensure_physics (f->world);
	g_assert_true (lrg_dynamics_world_add_body (physics, box, LRG_DYNAMICS_BODY_STATIC, 0.0f, at, NULL, &id, &error));
	g_assert_no_error (error);
	g_object_set (f->src, "occlusion", TRUE, "occlusion-fade", 0.0f, "occlusion-gain", 0.4f, NULL);
	lrg_audio_source_component_set_mixer (f->src, f->mixer);
	lrg_audio_source_component_play (f->src);

	/* the wall between ears and source: one occluder */
	g_assert_cmpuint (lrg_audio_source_component_get_occluders (f->src), ==, 1);
	g_assert_cmpfloat_with_epsilon (lrg_audio_source_component_get_occlusion (f->src), 0.4, 1e-6);
	lrg_audio_source_component_evaluate (f->src, lpos, lfwd, NULL, NULL, &gain, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (gain, 0.75 * 0.4, 1e-6);

	/* beside the wall: clear */
	lrg_entity3d_set_position_xyz (f->entity, 6.0f, 0.0f, 0.0f);
	lrg_world3d_late_update (f->world, 0.016f);
	g_assert_cmpuint (lrg_audio_source_component_get_occluders (f->src), ==, 0);
	g_assert_cmpfloat (lrg_audio_source_component_get_occlusion (f->src), ==, 1.0f);
}

/* ==========================================================================
 * The project's default mixer
 * ========================================================================== */

static void
test_project_mixer (void)
{
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *root = g_dir_make_tmp ("lrg-mixer-project-XXXXXX", &error);
	g_autofree gchar          *audio = NULL;
	g_autofree gchar          *levels = NULL;
	g_autofree gchar          *manifest = NULL;
	g_autofree gchar          *rmixer = NULL;
	g_autofree gchar          *found = NULL;
	g_autofree gchar          *expected = NULL;
	g_autoptr(LrgAudioManager) manager = g_object_new (LRG_TYPE_AUDIO_MANAGER, NULL);
	g_autoptr(LrgMixerAsset)   asset = NULL;

	g_assert_no_error (error);
	audio = g_build_filename (root, "audio", NULL);
	levels = g_build_filename (root, "levels", NULL);
	g_mkdir (audio, 0755);
	g_mkdir (levels, 0755);
	manifest = g_build_filename (root, "project.ryaml", NULL);
	rmixer = g_build_filename (audio, "main.rmixer", NULL);

	/* no project: nothing, and loading is not an error */
	g_assert_null (lrg_mixer_asset_find_project_mixer (levels));
	g_assert_true (lrg_audio_manager_load_project_mixer (manager, levels, 0, &error));
	g_assert_null (lrg_audio_manager_get_mixer (manager));

	g_assert_true (g_file_set_contents (manifest, "project:\n  name: demo\naudio:\n  mixer: audio/main.rmixer\n", -1,
	                                    &error));
	g_assert_true (g_file_set_contents (rmixer, routing_doc, -1, &error));

	found = lrg_mixer_asset_find_project_mixer (levels);
	expected = g_canonicalize_filename (rmixer, NULL);
	g_assert_cmpstr (found, ==, expected);
	asset = lrg_mixer_asset_new_for_project (levels, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_mixer_asset_has_bus (asset, "weapons"));

	/* the manager builds the game mixer from it (no device: no stream) */
	g_assert_true (lrg_audio_manager_load_project_mixer (manager, levels, 22050, &error));
	g_assert_no_error (error);
	g_assert_nonnull (lrg_audio_manager_get_mixer (manager));
	g_assert_cmpuint (lrg_audio_mixer_get_sample_rate (lrg_audio_manager_get_mixer (manager)), ==, 22050);
	g_assert_true (lrg_audio_mixer_has_bus (lrg_audio_manager_get_mixer (manager), "fx"));
	lrg_audio_manager_update (manager);

	/* a broken mixer file is an error */
	g_assert_true (g_file_set_contents (rmixer, "buses: 3\n", -1, &error));
	g_assert_false (lrg_audio_manager_load_project_mixer (manager, levels, 0, &error));
	g_assert_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_PARSE);

	g_unlink (rmixer);
	g_unlink (manifest);
	g_rmdir (audio);
	g_rmdir (levels);
	g_rmdir (root);
}

/* ==========================================================================
 * Performance (informational; reported in docs/modules/audio/mixer-asset.org)
 * ========================================================================== */

static void
test_perf (void)
{
	g_autoptr(LrgMixerAsset) asset = load (full_doc);
	g_autoptr(LrgAudioMixer) mixer = lrg_mixer_asset_create_mixer (asset, 48000, 2, NULL);
	g_autoptr(LrgWaveData)   wave = dc_wave (48000, 2, 4800, 0.1f);
	gint64                   start;
	gdouble                  ms;
	guint                    i;

	if (!g_test_perf ())
	{
		g_test_skip ("run with -m perf");
		return;
	}
	/* everything on: the reverb (disabled and muted in the document), the
	 * music send into it, the ducking rule */
	lrg_audio_mixer_set_bus_solo (mixer, "dialogue", FALSE);
	lrg_audio_mixer_set_bus (mixer, "reverb", 1.0, FALSE);
	lrg_audio_effect_set_enabled (lrg_audio_effect_chain_get_effect (lrg_audio_mixer_get_bus_chain (mixer, "reverb"), 0),
	                              TRUE);
	for (i = 0; i < 32; i++)
		lrg_audio_mixer_play (mixer, wave, i % 2 ? "music" : "dialogue", 0.5, TRUE);
	start = g_get_monotonic_time ();
	for (i = 0; i < 100; i++)
		g_free (lrg_audio_mixer_render (mixer, 2048, &(gsize) { 0 }));
	ms = (g_get_monotonic_time () - start) / 1000.0 / 100.0;
	g_test_minimized_result (ms, "render 2048 frames at 48 kHz stereo, 4 buses, 32 voices, low-pass, reverb, send, duck: %.3f ms", ms);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/mixer-asset/format/roundtrip", test_roundtrip);
	g_test_add_func ("/mixer-asset/format/file-io", test_file_io);
	g_test_add_func ("/mixer-asset/format/errors", test_errors);
	g_test_add_func ("/mixer-asset/format/editing", test_editing);
	g_test_add_func ("/mixer-asset/runtime/routing-math", test_routing_math);
	g_test_add_func ("/mixer-asset/runtime/effect-chain", test_effect_chain);
	g_test_add_func ("/mixer-asset/runtime/chain-processing", test_chain_processing);
	g_test_add_func ("/mixer-asset/runtime/snapshot-blend", test_snapshot_blend);
	g_test_add_func ("/mixer-asset/runtime/ramp-samples", test_ramp_samples);
	g_test_add_func ("/mixer-asset/runtime/ducking", test_ducking);
	g_test_add_func ("/mixer-asset/runtime/ducking-threshold", test_ducking_asset_threshold);
	g_test_add_func ("/mixer-asset/runtime/solo", test_solo);
	g_test_add_func ("/mixer-asset/runtime/voice-controls", test_voice_controls);
	g_test_add_func ("/mixer-asset/runtime/db-helpers", test_db_helpers);
	g_test_add_func ("/mixer-asset/spatial/rolloff-curves", test_rolloff_curves);
	g_test_add_func ("/mixer-asset/spatial/cone-doppler", test_cone_and_doppler);
	g_test_add ("/mixer-asset/source/mixer-voice", SourceFixture, NULL, source_setup, test_source_mixer_voice,
	            source_teardown);
	g_test_add ("/mixer-asset/source/bus-routing", SourceFixture, NULL, source_setup, test_source_bus_routing,
	            source_teardown);
	g_test_add ("/mixer-asset/source/cone", SourceFixture, NULL, source_setup, test_source_cone, source_teardown);
	g_test_add ("/mixer-asset/source/doppler", SourceFixture, NULL, source_setup, test_source_doppler,
	            source_teardown);
	g_test_add ("/mixer-asset/source/occlusion", SourceFixture, NULL, source_setup, test_source_occlusion,
	            source_teardown);
	g_test_add_func ("/mixer-asset/project/default-mixer", test_project_mixer);
	g_test_add_func ("/mixer-asset/perf/render", test_perf);
	return g_test_run ();
}
