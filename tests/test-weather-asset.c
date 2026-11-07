/* test-weather-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The .rweather format (LrgWeatherAsset): schema, byte-stable round trip
 * with unknown keys, strict errors with key paths, editing API, building
 * weathers, crossfades to layer levels, transition rules, wind layers,
 * live sync and the particle layer. Headless.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

/* the annotated example of docs/modules/weather/weather-asset.org: it is
 * written in the canonical form, so saving it gives the same bytes */
static const gchar sample[] =
	"version: 1\n"
	"name: Coastal weather\n"
	"default-preset: clear\n"
	"default-transition: 6\n"
	"editor-notes: kept as is\n"
	"presets:\n"
	"- id: clear\n"
	"  name: Clear sky\n"
	"  ambient-color: '#fff4e0'\n"
	"  ambient-brightness: 1\n"
	"  layers:\n"
	"  - kind: wind\n"
	"    intensity: 0.3\n"
	"    params:\n"
	"      speed-x: 1.5\n"
	"- id: storm\n"
	"  name: Thunderstorm\n"
	"  ambient-color: '#5a5f6e'\n"
	"  ambient-brightness: 0.55\n"
	"  layers:\n"
	"  - kind: rain\n"
	"    intensity: 0.9\n"
	"    params:\n"
	"      space: world\n"
	"      drop-count: 4000\n"
	"      drop-speed: 12\n"
	"      color: '#a0aabeaa'\n"
	"      volume-radius: 30\n"
	"      seed: 7\n"
	"  - kind: fog\n"
	"    intensity: 1\n"
	"    params:\n"
	"      fog-type: exponential\n"
	"      density: 0.03\n"
	"      color: '#5a5f69ff'\n"
	"      veil-opacity: 0\n"
	"  - kind: lightning\n"
	"    intensity: 1\n"
	"    params:\n"
	"      min-interval: 3\n"
	"      max-interval: 9\n"
	"      bolts-enabled: true\n"
	"      bolt-distance: 120\n"
	"  - kind: wind\n"
	"    id: gusts\n"
	"    intensity: 1\n"
	"    params:\n"
	"      speed-x: 6\n"
	"      gust-strength: 3\n"
	"      gust-frequency: 0.4\n"
	"    mixer-bus: weather\n"
	"  - kind: particles\n"
	"    id: spray\n"
	"    intensity: 0.5\n"
	"    enabled: false\n"
	"    params:\n"
	"      capacity: 50000\n"
	"      emission-rate: 8000\n"
	"      render-mode: stretched\n"
	"      start-color: '#c8d2e6b4'\n"
	"transitions:\n"
	"- from: clear\n"
	"  to: storm\n"
	"  duration: 8\n"
	"- from: '*'\n"
	"  to: clear\n"
	"  duration: 12\n";

static LrgWeatherAsset *
load_sample (void)
{
	g_autoptr(GError) error = NULL;
	LrgWeatherAsset  *asset = lrg_weather_asset_new_from_data (sample, -1, NULL, &error);

	g_assert_no_error (error);
	g_assert_nonnull (asset);
	return asset;
}

static gboolean
has_spec (GParamSpec  **specs,
          guint         n,
          const gchar  *name)
{
	guint i;

	for (i = 0; i < n; i++)
		if (g_strcmp0 (specs[i]->name, name) == 0)
			return TRUE;
	return FALSE;
}

static void
test_kinds (void)
{
	g_auto(GStrv)        kinds = lrg_weather_asset_list_kinds ();
	g_autofree GParamSpec **rain = NULL;
	g_autofree GParamSpec **part = NULL;
	guint                n = 0, np = 0;

	g_assert_cmpuint (g_strv_length (kinds), >=, 6);
	g_assert_true (g_strv_contains ((const gchar * const *) kinds, "rain"));
	g_assert_true (g_strv_contains ((const gchar * const *) kinds, "particles"));
	g_assert_true (lrg_weather_asset_lookup_kind ("fog") == LRG_TYPE_FOG);
	g_assert_true (lrg_weather_asset_lookup_kind ("wind") == LRG_TYPE_WEATHER_WIND);
	g_assert_true (lrg_weather_asset_lookup_kind ("nope") == G_TYPE_INVALID);
	g_assert_null (lrg_weather_asset_list_kind_params ("nope", NULL));

	rain = lrg_weather_asset_list_kind_params ("rain", &n);
	g_assert_true (has_spec (rain, n, "drop-count"));
	g_assert_true (has_spec (rain, n, "space"));
	g_assert_true (has_spec (rain, n, "color"));
	g_assert_true (has_spec (rain, n, "volume-radius"));
	g_assert_false (has_spec (rain, n, "intensity"));
	g_assert_false (has_spec (rain, n, "id"));
	g_assert_false (has_spec (rain, n, "wind-x"));

	part = lrg_weather_asset_list_kind_params ("particles", &np);
	g_assert_true (has_spec (part, np, "capacity"));
	g_assert_true (has_spec (part, np, "emission-rate"));
	g_assert_true (has_spec (part, np, "asset"));

	/* a game's own effect type */
	lrg_weather_asset_register_kind ("custom-snow", LRG_TYPE_SNOW);
	g_assert_true (lrg_weather_asset_lookup_kind ("custom-snow") == LRG_TYPE_SNOW);
}

static void
test_round_trip (void)
{
	g_autoptr(LrgWeatherAsset) asset = load_sample ();
	g_autoptr(LrgWeatherAsset) again = NULL;
	g_autoptr(LrgWeatherAsset) copy = NULL;
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *text = NULL;
	g_autofree gchar          *text2 = NULL;
	g_autofree gchar          *text3 = NULL;

	text = lrg_weather_asset_to_data (asset, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, sample);

	again = lrg_weather_asset_new_from_data (text, -1, NULL, &error);
	g_assert_no_error (error);
	text2 = lrg_weather_asset_to_data (again, &error);
	g_assert_cmpstr (text2, ==, text);

	copy = lrg_weather_asset_copy (asset);
	text3 = lrg_weather_asset_to_data (copy, &error);
	g_assert_cmpstr (text3, ==, text);

	g_assert_cmpstr (lrg_weather_asset_get_name (asset), ==, "Coastal weather");
	g_assert_cmpstr (lrg_weather_asset_get_default_preset (asset), ==, "clear");
	g_assert_cmpfloat (lrg_weather_asset_get_default_transition (asset), ==, 6.0f);
	g_assert_cmpuint (lrg_weather_asset_get_preset_count (asset), ==, 2);
	g_assert_cmpint (lrg_weather_asset_find_preset (asset, "storm"), ==, 1);
	g_assert_cmpuint (lrg_weather_asset_get_layer_count (asset, 1), ==, 5);
	g_assert_cmpstr (lrg_weather_asset_get_layer_id (asset, 1, 0), ==, "rain");
	g_assert_cmpstr (lrg_weather_asset_get_layer_id (asset, 1, 3), ==, "gusts");
	g_assert_cmpstr (lrg_weather_asset_get_layer_kind (asset, 1, 3), ==, "wind");
	g_assert_false (lrg_weather_asset_get_layer_enabled (asset, 1, 4));
	g_assert_cmpfloat_with_epsilon (lrg_weather_asset_get_layer_intensity (asset, 1, 0), 0.9f, 1e-6f);
}

static void
test_file_io (void)
{
	g_autoptr(LrgWeatherAsset) asset = load_sample ();
	g_autoptr(LrgWeatherAsset) back = NULL;
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *dir = g_dir_make_tmp ("rweather-XXXXXX", &error);
	g_autofree gchar          *path = g_build_filename (dir, "coast.rweather", NULL);
	g_autofree gchar          *text = NULL;

	g_assert_true (lrg_weather_asset_save_to_file (asset, path, &error));
	g_assert_no_error (error);
	back = lrg_weather_asset_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_weather_asset_get_base_dir (back), ==, dir);
	text = lrg_weather_asset_to_data (back, NULL);
	g_assert_cmpstr (text, ==, sample);
	g_assert_null (lrg_weather_asset_new_from_file ("/nonexistent/x.rweather", &error));
	g_assert_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_IO);
	g_remove (path);
	g_rmdir (dir);
}

typedef struct
{
	const gchar *text;
	gint         code;
	const gchar *path;
} BadCase;

static void
test_strict_errors (void)
{
	static const BadCase cases[] = {
		{ "version: 99\npresets: []\n", LRG_WEATHER_ASSET_ERROR_PARSE, "version" },
		{ "- just a list\n", LRG_WEATHER_ASSET_ERROR_PARSE, "mapping" },
		{ "presets:\n- name: x\n", LRG_WEATHER_ASSET_ERROR_PARSE, "presets[0]" },
		{ "presets:\n- id: a\n  layers:\n  - kind: hail\n", LRG_WEATHER_ASSET_ERROR_UNKNOWN_KIND,
		  "presets[0].layers[0].kind" },
		{ "presets:\n- id: a\n  layers:\n  - kind: rain\n    params:\n      drops: 3\n",
		  LRG_WEATHER_ASSET_ERROR_UNKNOWN_PARAM, "presets[0].layers[0].params.drops" },
		{ "presets:\n- id: a\n  layers:\n  - kind: rain\n    params:\n      drop-count: lots\n",
		  LRG_WEATHER_ASSET_ERROR_INVALID_VALUE, "presets[0].layers[0].params.drop-count" },
		{ "presets:\n- id: a\n  layers:\n  - kind: fog\n    params:\n      color: grey\n",
		  LRG_WEATHER_ASSET_ERROR_INVALID_VALUE, "presets[0].layers[0].params.color" },
		{ "presets:\n- id: a\n  layers:\n  - kind: fog\n    params:\n      fog-type: thick\n",
		  LRG_WEATHER_ASSET_ERROR_INVALID_VALUE, "params.fog-type" },
		{ "presets:\n- id: a\n  layers:\n  - kind: rain\n    intensity: high\n",
		  LRG_WEATHER_ASSET_ERROR_INVALID_VALUE, "presets[0].layers[0].intensity" },
		{ "presets:\n- id: a\n- id: a\n", LRG_WEATHER_ASSET_ERROR_DUPLICATE, "presets[1].id" },
		{ "presets:\n- id: a\n  layers:\n  - kind: rain\n  - kind: rain\n",
		  LRG_WEATHER_ASSET_ERROR_DUPLICATE, "presets[0].layers[1]" },
		{ "default-preset: b\npresets:\n- id: a\n", LRG_WEATHER_ASSET_ERROR_NOT_FOUND, "default-preset" },
		{ "presets:\n- id: a\ntransitions:\n- from: a\n  to: b\n  duration: 1\n",
		  LRG_WEATHER_ASSET_ERROR_NOT_FOUND, "transitions[0]" },
		{ "presets:\n- id: a\ntransitions:\n- from: a\n  to: '*'\n", LRG_WEATHER_ASSET_ERROR_PARSE,
		  "transitions[0]" },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autoptr(GError)          error = NULL;
		g_autoptr(LrgWeatherAsset) a = lrg_weather_asset_new_from_data (cases[i].text, -1, NULL, &error);

		g_test_message ("case %u: %s", i, error != NULL ? error->message : "(no error)");
		g_assert_null (a);
		g_assert_error (error, LRG_WEATHER_ASSET_ERROR, cases[i].code);
		g_assert_nonnull (strstr (error->message, cases[i].path));
	}
}

static void
count_changed (LrgWeatherAsset *asset,
               gint             preset,
               gint             layer,
               gpointer         data)
{
	(*(guint *) data)++;
}

static void
test_editing (void)
{
	g_autoptr(LrgWeatherAsset) asset = lrg_weather_asset_new ();
	g_autoptr(GError)          error = NULL;
	g_auto(GValue)             v = G_VALUE_INIT;
	g_auto(GValue)             out = G_VALUE_INIT;
	g_auto(GStrv)              names = NULL;
	guint                      changes = 0;
	gint                       p, l, l2;
	GrlColor                   amb = { 10, 20, 30, 99 };
	GrlColor                   got;
	gfloat                     bright;

	g_signal_connect (asset, "changed", G_CALLBACK (count_changed), &changes);
	p = lrg_weather_asset_add_preset (asset, "snowy", "Snow day", &error);
	g_assert_no_error (error);
	g_assert_cmpint (p, ==, 0);
	g_assert_cmpint (lrg_weather_asset_add_preset (asset, "snowy", NULL, &error), ==, -1);
	g_assert_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_DUPLICATE);
	g_clear_error (&error);

	l = lrg_weather_asset_add_layer (asset, 0, "snow", NULL, &error);
	l2 = lrg_weather_asset_add_layer (asset, 0, "snow", NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_weather_asset_get_layer_id (asset, 0, (guint) l), ==, "snow");
	g_assert_cmpstr (lrg_weather_asset_get_layer_id (asset, 0, (guint) l2), ==, "snow-2");
	g_assert_cmpint (lrg_weather_asset_add_layer (asset, 0, "hail", NULL, &error), ==, -1);
	g_assert_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_UNKNOWN_KIND);
	g_clear_error (&error);

	/* strings in file syntax, or typed values */
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "#ffffffc0");
	g_assert_true (lrg_weather_asset_set_layer_param (asset, 0, 0, "color", &v, &error));
	g_value_set_string (&v, "world");
	g_assert_true (lrg_weather_asset_set_layer_param (asset, 0, 0, "space", &v, &error));
	g_value_unset (&v);
	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, 1.25);
	g_assert_true (lrg_weather_asset_set_layer_param (asset, 0, 0, "flake-speed", &v, &error));
	g_value_set_double (&v, 1e12);      /* clamps to the property range */
	g_assert_true (lrg_weather_asset_set_layer_param (asset, 0, 0, "flake-count", &v, &error));
	g_assert_no_error (error);
	g_assert_false (lrg_weather_asset_set_layer_param (asset, 0, 0, "drop-count", &v, &error));
	g_assert_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_UNKNOWN_PARAM);
	g_clear_error (&error);

	g_assert_true (lrg_weather_asset_get_layer_param (asset, 0, 0, "flake-speed", &out));
	g_assert_cmpfloat (g_value_get_float (&out), ==, 1.25f);
	g_value_unset (&out);
	g_assert_false (lrg_weather_asset_get_layer_param (asset, 0, 0, "sway-amount", &out));
	names = lrg_weather_asset_get_layer_param_names (asset, 0, 0);
	g_assert_cmpuint (g_strv_length (names), ==, 4);
	g_assert_cmpstr (names[0], ==, "color");
	g_assert_true (lrg_weather_asset_unset_layer_param (asset, 0, 0, "color"));
	g_assert_false (lrg_weather_asset_unset_layer_param (asset, 0, 0, "color"));

	lrg_weather_asset_set_layer_intensity (asset, 0, 0, 2.0f);
	g_assert_cmpfloat (lrg_weather_asset_get_layer_intensity (asset, 0, 0), ==, 1.0f);
	lrg_weather_asset_set_layer_enabled (asset, 0, 1, FALSE);
	g_assert_true (lrg_weather_asset_move_layer (asset, 0, 1, 0));
	g_assert_cmpstr (lrg_weather_asset_get_layer_id (asset, 0, 0), ==, "snow-2");
	g_assert_true (lrg_weather_asset_remove_layer (asset, 0, 0));
	g_assert_false (lrg_weather_asset_remove_layer (asset, 0, 5));

	lrg_weather_asset_set_preset_ambient (asset, 0, &amb, 0.7f);
	lrg_weather_asset_get_preset_ambient (asset, 0, &got, &bright);
	g_assert_cmpuint (got.b, ==, 30);
	g_assert_cmpuint (got.a, ==, 255);
	g_assert_cmpfloat (bright, ==, 0.7f);
	lrg_weather_asset_set_preset_name (asset, 0, "Snowfall");
	g_assert_cmpstr (lrg_weather_asset_get_preset_name (asset, 0), ==, "Snowfall");
	lrg_weather_asset_set_default_preset (asset, "snowy");
	lrg_weather_asset_set_name (asset, "Edited");
	lrg_weather_asset_set_default_transition (asset, 2.0f);
	lrg_weather_asset_set_transition (asset, "*", "snowy", 3.0f);
	lrg_weather_asset_set_transition (asset, "*", "snowy", 4.0f);    /* replaces */
	g_assert_cmpuint (lrg_weather_asset_get_transition_count (asset), ==, 1);
	g_assert_cmpuint (changes, >=, 18);

	/* what the editing produced loads back identically */
	{
		g_autofree gchar          *text = lrg_weather_asset_to_data (asset, &error);
		g_autoptr(LrgWeatherAsset) back = lrg_weather_asset_new_from_data (text, -1, NULL, &error);
		g_autofree gchar          *text2 = NULL;

		g_assert_no_error (error);
		text2 = lrg_weather_asset_to_data (back, NULL);
		g_assert_cmpstr (text, ==, text2);
		g_assert_nonnull (strstr (text, "flake-count: 100000\n"));   /* clamped */
		g_assert_nonnull (strstr (text, "flake-speed: 1.25\n"));
	}

	g_assert_true (lrg_weather_asset_remove_transition (asset, "*", "snowy"));
	g_assert_true (lrg_weather_asset_remove_preset (asset, 0));
	g_assert_cmpuint (lrg_weather_asset_get_preset_count (asset), ==, 0);
}

static void
test_resolve_transition (void)
{
	g_autoptr(LrgWeatherAsset) asset = load_sample ();
	const gchar               *from = NULL, *to = NULL;
	gfloat                     sec = 0.0f;

	g_assert_cmpfloat (lrg_weather_asset_resolve_transition (asset, "clear", "storm"), ==, 8.0f);
	g_assert_cmpfloat (lrg_weather_asset_resolve_transition (asset, "storm", "clear"), ==, 12.0f);
	g_assert_cmpfloat (lrg_weather_asset_resolve_transition (asset, NULL, "storm"), ==, 6.0f);
	lrg_weather_asset_set_transition (asset, "none", "storm", 1.0f);
	g_assert_cmpfloat (lrg_weather_asset_resolve_transition (asset, NULL, "storm"), ==, 1.0f);
	lrg_weather_asset_set_transition (asset, "storm", "*", 0.5f);
	g_assert_cmpfloat (lrg_weather_asset_resolve_transition (asset, "storm", "clear"), ==, 0.5f);
	g_assert_true (lrg_weather_asset_get_transition (asset, 0, &from, &to, &sec));
	g_assert_cmpstr (from, ==, "clear");
	g_assert_cmpstr (to, ==, "storm");
	g_assert_false (lrg_weather_asset_get_transition (asset, 99, NULL, NULL, NULL));
}

static void
test_build_weather (void)
{
	g_autoptr(LrgWeatherAsset) asset = load_sample ();
	g_autoptr(LrgWeather)      storm = NULL;
	g_autoptr(GError)          error = NULL;
	LrgWeatherEffect          *rain, *fog, *lightning;
	guint8                     r, g, b, a;
	gfloat                     radius, height, dist, bh, bw;

	storm = lrg_weather_asset_build_weather (asset, 1, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_weather_get_id (storm), ==, "storm");
	g_assert_cmpstr (lrg_weather_get_name (storm), ==, "Thunderstorm");
	g_assert_cmpuint (lrg_weather_get_effect_count (storm), ==, 4);   /* spray disabled */
	lrg_weather_get_ambient_color (storm, &r, &g, &b);
	g_assert_cmpuint (r, ==, 0x5a);
	g_assert_cmpfloat_with_epsilon (lrg_weather_get_ambient_brightness (storm), 0.55f, 1e-6f);

	rain = lrg_weather_get_effect (storm, "rain");
	g_assert_true (LRG_IS_RAIN (rain));
	g_assert_cmpint (lrg_weather_effect_get_space (rain), ==, LRG_WEATHER_SPACE_WORLD);
	g_assert_cmpuint (lrg_rain_get_drop_count (LRG_RAIN (rain)), ==, 4000);
	/* set after the space switch, so not reset by it */
	g_assert_cmpfloat (lrg_rain_get_drop_speed (LRG_RAIN (rain)), ==, 12.0f);
	lrg_rain_get_color (LRG_RAIN (rain), &r, &g, &b, &a);
	g_assert_cmpuint (r, ==, 0xa0);
	g_assert_cmpuint (a, ==, 0xaa);
	lrg_weather_effect_get_volume (rain, &radius, &height);
	g_assert_cmpfloat (radius, ==, 30.0f);
	g_assert_cmpfloat_with_epsilon (lrg_weather_effect_get_level (rain), 0.9f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (lrg_weather_effect_get_intensity (rain), 0.9f, 1e-6f);

	fog = lrg_weather_get_effect (storm, "fog");
	g_assert_cmpint (lrg_fog_get_fog_type (LRG_FOG (fog)), ==, LRG_FOG_TYPE_EXPONENTIAL);
	g_assert_cmpfloat_with_epsilon (lrg_fog_get_density (LRG_FOG (fog)), 0.03f, 1e-6f);

	lightning = lrg_weather_get_effect (storm, "lightning");
	g_assert_true (lrg_lightning_get_bolts_enabled (LRG_LIGHTNING (lightning)));
	lrg_lightning_get_bolt_shape (LRG_LIGHTNING (lightning), &dist, &bh, &bw);
	g_assert_cmpfloat (dist, ==, 120.0f);

	g_assert_true (LRG_IS_WEATHER_WIND (lrg_weather_get_effect (storm, "gusts")));
	{
		g_autoptr(LrgWeather) none = lrg_weather_asset_build_weather (asset, 9, &error);

		g_assert_null (none);
		g_assert_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_NOT_FOUND);
	}
}

/* the manager crossfades to each layer's level, not to 1 */
static void
test_apply_and_transition (void)
{
	g_autoptr(LrgWeatherAsset)   asset = load_sample ();
	g_autoptr(LrgWeatherManager) mgr = lrg_weather_manager_new ();
	g_autoptr(GError)            error = NULL;
	LrgWeather                  *storm;
	LrgWeatherEffect            *rain;
	guint                        i;

	g_assert_true (lrg_weather_asset_apply (asset, mgr, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_weather_manager_get_active_weather_id (mgr), ==, "clear");
	/* the instant switch sets the level: clear's wind layer is at 0.3 */
	g_assert_cmpfloat_with_epsilon (
		lrg_weather_effect_get_intensity (lrg_weather_get_effect (lrg_weather_manager_get_weather (mgr, "clear"), "wind")),
		0.3f, 1e-6f);

	g_assert_true (lrg_weather_asset_transition (asset, mgr, "storm", &error));
	g_assert_true (lrg_weather_manager_is_transitioning (mgr));
	storm = lrg_weather_manager_get_weather (mgr, "storm");
	rain = lrg_weather_get_effect (storm, "rain");
	for (i = 0; i < 40; i++)
		lrg_weather_manager_update (mgr, 0.1f);      /* 4 s of 8 */
	g_assert_cmpfloat_with_epsilon (lrg_weather_effect_get_intensity (rain), 0.45f, 0.02f);
	for (i = 0; i < 41; i++)
		lrg_weather_manager_update (mgr, 0.1f);
	g_assert_false (lrg_weather_manager_is_transitioning (mgr));
	g_assert_cmpfloat_with_epsilon (lrg_weather_effect_get_intensity (rain), 0.9f, 1e-5f);

	g_assert_false (lrg_weather_asset_transition (asset, mgr, "fogbank", &error));
	g_assert_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_NOT_FOUND);
	g_clear_error (&error);
	g_assert_true (lrg_weather_asset_transition (asset, mgr, NULL, &error));

	/* applying again syncs in place: same weather object */
	g_assert_true (lrg_weather_asset_apply (asset, mgr, &error));
	g_assert_true (lrg_weather_manager_get_weather (mgr, "storm") == storm);
}

/* effects without a level keep the old 0 -> 1 crossfade */
static void
test_legacy_crossfade (void)
{
	g_autoptr(LrgWeatherManager) mgr = lrg_weather_manager_new ();
	g_autoptr(LrgWeather)        w = lrg_weather_new ("w", "W");
	g_autoptr(LrgFog)            fog = lrg_fog_new ();
	guint                        i;

	lrg_weather_add_effect (w, LRG_WEATHER_EFFECT (fog));
	lrg_weather_manager_register_weather (mgr, w);
	lrg_weather_manager_set_weather (mgr, "w", 1.0f);
	for (i = 0; i < 12; i++)
		lrg_weather_manager_update (mgr, 0.1f);
	g_assert_cmpfloat (lrg_weather_effect_get_level (LRG_WEATHER_EFFECT (fog)), <, 0.0f);
	g_assert_cmpfloat_with_epsilon (lrg_weather_effect_get_intensity (LRG_WEATHER_EFFECT (fog)), 1.0f, 1e-6f);
}

static void
test_wind_layer (void)
{
	g_autoptr(LrgWeatherAsset) asset = load_sample ();
	g_autoptr(LrgWeather)      storm = lrg_weather_asset_build_weather (asset, 1, NULL);
	LrgWeatherEffect          *gusts = lrg_weather_get_effect (storm, "gusts");
	LrgWeatherEffect          *rain = lrg_weather_get_effect (storm, "rain");
	gfloat                     wx = 0.0f, wy = 0.0f, rx = 0.0f, lo = 1e9f, hi = -1e9f;
	guint                      i;

	lrg_weather_set_wind (storm, 1.0f, 0.0f);
	lrg_weather_activate (storm);
	for (i = 0; i < 200; i++)
	{
		lrg_weather_update (storm, 0.05f);
		lrg_weather_get_effective_wind (storm, &wx, &wy);
		lo = MIN (lo, wx);
		hi = MAX (hi, wx);
	}
	/* 1 (base) + 6 +- 3 gusts, all along +X */
	g_assert_cmpfloat (lo, >=, 1.0f + 6.0f - 3.0f - 1e-3f);
	g_assert_cmpfloat (hi, <=, 1.0f + 6.0f + 3.0f + 1e-3f);
	g_assert_cmpfloat (hi - lo, >, 2.0f);
	g_assert_cmpfloat_with_epsilon (wy, 0.0f, 1e-5f);
	g_object_get (rain, "wind-x", &rx, NULL);
	g_assert_cmpfloat_with_epsilon (rx, wx, 1e-5f);

	/* the wind scales with the layer intensity, and stops when inactive */
	lrg_weather_effect_set_intensity (gusts, 0.0f);
	lrg_weather_update (storm, 0.05f);
	lrg_weather_get_effective_wind (storm, &wx, NULL);
	g_assert_cmpfloat_with_epsilon (wx, 1.0f, 1e-5f);
	lrg_weather_deactivate (storm);
	lrg_weather_wind_get_current (LRG_WEATHER_WIND (gusts), &wx, &wy);
	g_assert_cmpfloat (wx, ==, 0.0f);
}

static void
test_sync_weather (void)
{
	g_autoptr(LrgWeatherAsset) asset = load_sample ();
	g_autoptr(LrgWeather)      storm = lrg_weather_asset_build_weather (asset, 1, NULL);
	g_autoptr(GError)          error = NULL;
	g_auto(GValue)             v = G_VALUE_INIT;
	LrgWeatherEffect          *rain = lrg_weather_get_effect (storm, "rain");
	LrgWeatherEffect          *rain2;
	g_autoptr(GList)           effects = NULL;

	lrg_weather_activate (storm);

	/* a value edit keeps the live effect */
	g_value_init (&v, G_TYPE_UINT);
	g_value_set_uint (&v, 1234);
	g_assert_true (lrg_weather_asset_set_layer_param (asset, 1, 0, "drop-count", &v, &error));
	lrg_weather_asset_set_layer_intensity (asset, 1, 0, 0.6f);
	g_assert_true (lrg_weather_asset_sync_weather (asset, 1, storm, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_weather_get_effect (storm, "rain") == rain);
	g_assert_cmpuint (lrg_rain_get_drop_count (LRG_RAIN (rain)), ==, 1234);
	g_assert_cmpfloat_with_epsilon (lrg_weather_effect_get_intensity (rain), 0.6f, 1e-6f);

	/* an unset parameter rebuilds the effect with the default back */
	g_assert_true (lrg_weather_asset_unset_layer_param (asset, 1, 0, "drop-count"));
	g_assert_true (lrg_weather_asset_sync_weather (asset, 1, storm, &error));
	rain2 = lrg_weather_get_effect (storm, "rain");
	g_assert_true (rain2 != rain);
	g_assert_true (lrg_weather_effect_is_active (rain2));

	/* enabling the spray adds an effect; removing fog drops one */
	lrg_weather_asset_set_layer_enabled (asset, 1, 4, TRUE);
	lrg_weather_asset_remove_layer (asset, 1, 1);
	g_assert_true (lrg_weather_asset_sync_weather (asset, 1, storm, &error));
	g_assert_null (lrg_weather_get_effect (storm, "fog"));
	g_assert_true (LRG_IS_WEATHER_PARTICLES (lrg_weather_get_effect (storm, "spray")));

	/* reordering follows the layers */
	g_assert_true (lrg_weather_asset_move_layer (asset, 1, 3, 0));
	g_assert_true (lrg_weather_asset_sync_weather (asset, 1, storm, &error));
	effects = lrg_weather_get_effects (storm);
	g_assert_cmpstr (lrg_weather_effect_get_id (effects->data), ==, "spray");
}

static void
test_particles_layer (void)
{
	g_autoptr(LrgWeatherParticles) layer = lrg_weather_particles_new ();
	LrgWeatherEffect              *e = LRG_WEATHER_EFFECT (layer);
	LrgGpuParticles               *em = lrg_weather_particles_get_emitter (layer);
	GrlVector3                     cam = { 10.0f, 2.0f, -5.0f };
	gfloat                         x, y, z, wx = 0.0f;
	guint                          i;

	g_assert_cmpint (lrg_weather_effect_get_space (e), ==, LRG_WEATHER_SPACE_WORLD);
	lrg_gpu_particles_set_prefer_gpu (em, FALSE);
	g_object_set (layer, "gpu", FALSE, NULL);
	g_object_set (em, "emission-rate", 1000.0f, NULL);
	lrg_weather_effect_set_intensity (e, 0.5f);
	lrg_weather_effect_set_camera_position (e, &cam);
	lrg_weather_effect_activate (e);
	for (i = 0; i < 10; i++)
		lrg_weather_effect_update (e, 0.1f);
	/* 1000/s x 0.5 intensity x 1 s */
	g_assert_cmpuint (lrg_gpu_particles_get_emitted_total (em), ==, 500);
	lrg_gpu_particles_get_position (em, &x, &y, &z);
	g_assert_cmpfloat (x, ==, 10.0f);
	g_assert_cmpfloat (y, ==, 12.0f);   /* default offset-y 10 */
	g_assert_cmpfloat (z, ==, -5.0f);

	lrg_weather_effect_set_wind (e, 3.0f, -2.0f);
	g_object_get (em, "wind-x", &wx, NULL);
	g_assert_cmpfloat (wx, ==, 3.0f);

	/* a new capacity keeps the parameters */
	g_object_set (layer, "capacity", 777u, NULL);
	em = lrg_weather_particles_get_emitter (layer);
	g_assert_cmpuint (lrg_gpu_particles_get_capacity (em), ==, 777);
	g_object_get (em, "wind-x", &wx, NULL);
	g_assert_cmpfloat (wx, ==, 3.0f);
}

/* an .rparticles the GPU cannot run falls back to a CPU effect */
static void
test_particles_layer_asset (void)
{
	g_autoptr(LrgWeatherParticles)     layer = lrg_weather_particles_new ();
	g_autoptr(LrgParticleAsset)        ok = lrg_particle_asset_new ();
	g_autoptr(LrgParticleAsset)        bad = lrg_particle_asset_new ();
	g_autoptr(LrgParticleAssetEmitter) e1 = lrg_particle_asset_emitter_new ("a");
	g_autoptr(LrgParticleAssetEmitter) e2 = lrg_particle_asset_emitter_new ("b");
	g_autoptr(LrgParticleAssetEmitter) e3 = lrg_particle_asset_emitter_new ("c");
	g_autoptr(GError)                  error = NULL;
	gfloat                             rate = 0.0f;

	g_object_set (lrg_particle_asset_emitter_add_module (e1, "spawn-rate"), "rate", 321.0f, NULL);
	lrg_particle_asset_add_emitter (ok, e1);
	g_assert_true (lrg_weather_particles_set_asset (layer, ok, &error));
	g_assert_no_error (error);
	g_assert_null (lrg_weather_particles_get_effect (layer));
	g_object_get (lrg_weather_particles_get_emitter (layer), "emission-rate", &rate, NULL);
	g_assert_cmpfloat (rate, ==, 321.0f);

	lrg_particle_asset_add_emitter (bad, e2);
	lrg_particle_asset_add_emitter (bad, e3);
	g_assert_false (lrg_weather_particles_set_asset (layer, bad, &error));
	g_assert_error (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_UNSUPPORTED);
	g_assert_nonnull (lrg_weather_particles_get_effect (layer));
	g_assert_true (lrg_weather_particles_set_asset (layer, NULL, NULL));
	g_assert_null (lrg_weather_particles_get_effect (layer));
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/weather-asset/kinds", test_kinds);
	g_test_add_func ("/weather-asset/round-trip", test_round_trip);
	g_test_add_func ("/weather-asset/file-io", test_file_io);
	g_test_add_func ("/weather-asset/strict-errors", test_strict_errors);
	g_test_add_func ("/weather-asset/editing", test_editing);
	g_test_add_func ("/weather-asset/resolve-transition", test_resolve_transition);
	g_test_add_func ("/weather-asset/build-weather", test_build_weather);
	g_test_add_func ("/weather-asset/apply-and-transition", test_apply_and_transition);
	g_test_add_func ("/weather-asset/legacy-crossfade", test_legacy_crossfade);
	g_test_add_func ("/weather-asset/wind-layer", test_wind_layer);
	g_test_add_func ("/weather-asset/sync-weather", test_sync_weather);
	g_test_add_func ("/weather-asset/particles-layer", test_particles_layer);
	g_test_add_func ("/weather-asset/particles-layer-asset", test_particles_layer_asset);

	return g_test_run ();
}
