/* test-ui-theme.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgTheme (roles, metrics, accent derivation, presets,
 * YAML) and the style system (LrgPartLook, LrgStyleRule, rule
 * resolution and its cache).
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

static gboolean
color_eq (const GrlColor *a,
          const GrlColor *b)
{
	return a->r == b->r && a->g == b->g && a->b == b->b && a->a == b->a;
}

static void
on_changed_count (LrgTheme *theme,
                  gpointer  user_data)
{
	(*(gint *) user_data)++;
}

/* ==========================================================================
 * Colours
 * ========================================================================== */

static void
test_color_parse (void)
{
	GrlColor c;
	gchar   *s;

	g_assert_true (lrg_color_parse ("#7c6cff", &c));
	g_assert_cmpuint (c.r, ==, 0x7c);
	g_assert_cmpuint (c.g, ==, 0x6c);
	g_assert_cmpuint (c.b, ==, 0xff);
	g_assert_cmpuint (c.a, ==, 255);

	g_assert_true (lrg_color_parse ("f80", &c));
	g_assert_cmpuint (c.r, ==, 0xff);
	g_assert_cmpuint (c.g, ==, 0x88);
	g_assert_cmpuint (c.b, ==, 0x00);

	g_assert_true (lrg_color_parse ("  #00000080 ", &c));
	g_assert_cmpuint (c.a, ==, 0x80);

	g_assert_true (lrg_color_parse ("#fff8", &c));
	g_assert_cmpuint (c.a, ==, 0x88);

	g_assert_false (lrg_color_parse ("#12345", &c));
	g_assert_false (lrg_color_parse ("#gggggg", &c));
	g_assert_false (lrg_color_parse (NULL, &c));
	g_assert_false (lrg_color_parse ("", &c));

	c.r = 1; c.g = 2; c.b = 3; c.a = 255;
	s = lrg_color_to_string (&c);
	g_assert_cmpstr (s, ==, "#010203");
	g_free (s);
	c.a = 16;
	s = lrg_color_to_string (&c);
	g_assert_cmpstr (s, ==, "#01020310");
	g_free (s);
}

/* ==========================================================================
 * Roles, metrics, legacy aliases
 * ========================================================================== */

static void
test_roles_and_metrics (void)
{
	g_autoptr(LrgTheme) theme = lrg_theme_new ();
	GrlColor            c = { 10, 20, 30, 255 };
	gint                changed = 0;
	guint               gen;

	g_signal_connect (theme, "changed", G_CALLBACK (on_changed_count), &changed);
	gen = lrg_theme_get_generation (theme);

	lrg_theme_set_color (theme, LRG_THEME_COLOR_PANEL, &c);
	g_assert_true (color_eq (lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), &c));
	g_assert_cmpint (changed, ==, 1);
	g_assert_cmpuint (lrg_theme_get_generation (theme), >, gen);

	/* Setting the same value again is not a change */
	lrg_theme_set_color (theme, LRG_THEME_COLOR_PANEL, &c);
	g_assert_cmpint (changed, ==, 1);

	lrg_theme_set_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT, 44.0f);
	g_assert_cmpfloat (lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT), ==, 44.0f);
	g_assert_cmpint (changed, ==, 2);
}

static void
test_legacy_aliases (void)
{
	g_autoptr(LrgTheme) theme = lrg_theme_new ();
	GrlColor            c = { 1, 2, 3, 255 };
	GrlColor           *boxed = NULL;

	/* Old accessors read and write the matching roles */
	lrg_theme_set_surface_color (theme, &c);
	g_assert_true (color_eq (lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), &c));

	lrg_theme_set_color (theme, LRG_THEME_COLOR_DANGER, &c);
	g_assert_true (color_eq (lrg_theme_get_error_color (theme), &c));

	lrg_theme_set_corner_radius (theme, 13.0f);
	g_assert_cmpfloat (lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS), ==, 13.0f);

	lrg_theme_set_metric (theme, LRG_THEME_METRIC_FONT_SIZE, 19.0f);
	g_assert_cmpfloat (lrg_theme_get_font_size_normal (theme), ==, 19.0f);

	/* And through GObject properties */
	g_object_get (theme, "surface-color", &boxed, NULL);
	g_assert_true (color_eq (boxed, &c));
	g_boxed_free (GRL_TYPE_COLOR, boxed);
}

static void
test_accent_derivation (void)
{
	g_autoptr(LrgTheme) dark = lrg_theme_new_preset ("midnight");
	g_autoptr(LrgTheme) light = lrg_theme_new_preset ("daylight");
	GrlColor            accent = { 0x30, 0x80, 0xe0, 255 };
	GrlColor            yellow = { 0xff, 0xd4, 0x00, 255 };
	const GrlColor     *hover;
	const GrlColor     *pressed;

	lrg_theme_set_accent (dark, &accent);
	hover = lrg_theme_get_color (dark, LRG_THEME_COLOR_ACCENT_HOVER);
	pressed = lrg_theme_get_color (dark, LRG_THEME_COLOR_ACCENT_PRESSED);

	/* Dark themes light up on hover and darken on press */
	g_assert_cmpfloat (lrg_paint_color_luminance (hover), >, lrg_paint_color_luminance (&accent));
	g_assert_cmpfloat (lrg_paint_color_luminance (pressed), <, lrg_paint_color_luminance (&accent));
	g_assert_true (color_eq (lrg_theme_get_color (dark, LRG_THEME_COLOR_FOCUS), &accent));
	g_assert_cmpuint (lrg_theme_get_color (dark, LRG_THEME_COLOR_SELECTION)->a, <, 255);

	/* Light themes darken on hover */
	lrg_theme_set_accent (light, &accent);
	hover = lrg_theme_get_color (light, LRG_THEME_COLOR_ACCENT_HOVER);
	g_assert_cmpfloat (lrg_paint_color_luminance (hover), <, lrg_paint_color_luminance (&accent));

	/* Text on accent picks whichever of black/white reads better */
	lrg_theme_set_accent (dark, &yellow);
	g_assert_cmpuint (lrg_theme_get_color (dark, LRG_THEME_COLOR_TEXT_ON_ACCENT)->r, <, 64);
	g_assert_cmpfloat (lrg_theme_get_contrast (dark, LRG_THEME_COLOR_TEXT_ON_ACCENT,
	                                           LRG_THEME_COLOR_ACCENT), >=, 4.5f);
}

static void
test_presets (void)
{
	const gchar * const *names = lrg_theme_list_presets ();
	guint                i;

	g_assert_nonnull (names);
	g_assert_cmpuint (g_strv_length ((gchar **) names), ==, 5);

	for (i = 0; names[i] != NULL; i++)
	{
		g_autoptr(LrgTheme) theme = lrg_theme_new_preset (names[i]);

		g_assert_nonnull (theme);
		g_assert_cmpstr (lrg_theme_get_name (theme), ==, names[i]);

		/* Every built-in theme keeps body text readable */
		g_assert_cmpfloat (lrg_theme_get_contrast (theme, LRG_THEME_COLOR_TEXT,
		                                           LRG_THEME_COLOR_PANEL), >=, 4.5f);
		g_assert_cmpfloat (lrg_theme_get_contrast (theme, LRG_THEME_COLOR_TEXT,
		                                           LRG_THEME_COLOR_WINDOW), >=, 4.5f);
		g_assert_cmpfloat (lrg_theme_get_contrast (theme, LRG_THEME_COLOR_TEXT_ON_ACCENT,
		                                           LRG_THEME_COLOR_ACCENT), >=, 3.0f);
	}

	g_assert_null (lrg_theme_new_preset ("no-such-theme"));

	{
		g_autoptr(LrgTheme) theme = lrg_theme_new ();

		g_assert_cmpstr (lrg_theme_get_name (theme), ==, "midnight");
		g_assert_true (lrg_theme_get_dark (theme));
		g_assert_true (lrg_theme_apply_preset (theme, "catppuccin-latte"));
		g_assert_false (lrg_theme_get_dark (theme));
		g_assert_cmpint (lrg_theme_get_motion (theme), ==, LRG_MOTION_BOUNCY);
		g_assert_false (lrg_theme_apply_preset (theme, "nope"));
	}
}

static void
test_set_default (void)
{
	g_autoptr(LrgTheme) theme = lrg_theme_new_preset ("daylight");
	LrgTheme           *before = lrg_theme_get_default ();

	g_assert_nonnull (before);
	lrg_theme_set_default (theme);
	g_assert_true (lrg_theme_get_default () == theme);

	lrg_theme_set_default (NULL);
	g_assert_true (lrg_theme_get_default () != theme);
	g_assert_cmpstr (lrg_theme_get_name (lrg_theme_get_default ()), ==, "midnight");
}

static void
test_copy (void)
{
	g_autoptr(LrgTheme)     theme = lrg_theme_new_preset ("catppuccin-mocha");
	g_autoptr(LrgTheme)     copy = NULL;
	g_autoptr(LrgStyleRule) rule = lrg_style_rule_new ("LrgButton", NULL, LRG_UI_PART_BOX,
	                                                   LRG_WIDGET_STATE_NONE);

	lrg_theme_add_style_rule (theme, rule);
	copy = lrg_theme_copy (theme);

	g_assert_cmpstr (lrg_theme_get_name (copy), ==, "catppuccin-mocha");
	g_assert_true (color_eq (lrg_theme_get_color (copy, LRG_THEME_COLOR_ACCENT),
	                         lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT)));
	g_assert_cmpuint (lrg_theme_get_style_rules (copy)->len, ==, 1);
	g_assert_cmpint (lrg_theme_get_motion (copy), ==, lrg_theme_get_motion (theme));
}

/* ==========================================================================
 * YAML
 * ========================================================================== */

static const gchar *sample_theme =
	"name: ocean\n"
	"preset: midnight\n"
	"accent: \"#22d3ee\"\n"
	"motion: bouncy\n"
	"colors:\n"
	"  panel: \"#111a2c\"\n"
	"  text-muted: \"#8090a0\"\n"
	"metrics:\n"
	"  corner-radius: 12\n"
	"  control-height: 40\n"
	"styles:\n"
	"  - widget: LrgButton\n"
	"    class: primary\n"
	"    part: box\n"
	"    state: [hovered]\n"
	"    background: \"#ff0000\"\n"
	"    radius: [1, 2, 3, 4]\n"
	"    gradient: { kind: linear, start: \"#000000\", end: \"#ffffff\", angle: 90 }\n"
	"    shadow: \"#00000080\"\n"
	"    shadow-blur: 20\n";

static void
test_yaml_load (void)
{
	g_autoptr(GError)   error = NULL;
	g_autoptr(LrgTheme) theme = lrg_theme_new_from_data (sample_theme, -1, &error);
	GrlColor            expect;
	LrgStyleRule       *rule;
	LrgPartLook        *look;

	g_assert_no_error (error);
	g_assert_nonnull (theme);

	g_assert_cmpstr (lrg_theme_get_name (theme), ==, "ocean");
	g_assert_cmpint (lrg_theme_get_motion (theme), ==, LRG_MOTION_BOUNCY);
	g_assert_true (lrg_color_parse ("#22d3ee", &expect));
	g_assert_true (color_eq (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), &expect));
	g_assert_true (lrg_color_parse ("#111a2c", &expect));
	g_assert_true (color_eq (lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), &expect));
	g_assert_cmpfloat (lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS), ==, 12.0f);
	g_assert_cmpfloat (lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT), ==, 40.0f);

	g_assert_cmpuint (lrg_theme_get_style_rules (theme)->len, ==, 1);
	rule = g_ptr_array_index (lrg_theme_get_style_rules (theme), 0);
	g_assert_cmpstr (lrg_style_rule_get_widget_type (rule), ==, "LrgButton");
	g_assert_cmpstr (lrg_style_rule_get_style_class (rule), ==, "primary");
	g_assert_cmpint (lrg_style_rule_get_state (rule), ==, LRG_WIDGET_STATE_HOVERED);

	look = lrg_style_rule_get_look (rule);
	g_assert_true (look->set & LRG_PART_LOOK_BACKGROUND);
	g_assert_true (look->set & LRG_PART_LOOK_RADII);
	g_assert_cmpfloat (look->radii.bottom_left, ==, 4.0f);
	g_assert_cmpint (look->gradient.kind, ==, LRG_PAINT_GRADIENT_LINEAR);
	g_assert_cmpfloat (look->gradient.angle, ==, 90.0f);
	g_assert_cmpuint (look->shadow.a, ==, 0x80);
	g_assert_cmpfloat (look->shadow_blur, ==, 20.0f);
	g_assert_false (look->set & LRG_PART_LOOK_FOREGROUND);
}

static void
test_yaml_round_trip (void)
{
	const gchar * const *names = lrg_theme_list_presets ();
	guint                i;

	for (i = 0; names[i] != NULL; i++)
	{
		g_autoptr(GError)   error = NULL;
		g_autoptr(LrgTheme) a = NULL;
		g_autoptr(LrgTheme) b = NULL;
		g_autofree gchar   *yaml = NULL;
		guint               k;

		a = lrg_theme_new_from_data (sample_theme, -1, &error);
		g_assert_no_error (error);
		lrg_theme_apply_preset (a, names[i]);

		yaml = lrg_theme_to_data (a, NULL, &error);
		g_assert_no_error (error);
		g_assert_nonnull (yaml);

		b = lrg_theme_new_from_data (yaml, -1, &error);
		g_assert_no_error (error);

		for (k = 0; k < LRG_THEME_COLOR_COUNT; k++)
			g_assert_true (color_eq (lrg_theme_get_color (a, k), lrg_theme_get_color (b, k)));
		for (k = 0; k < LRG_THEME_METRIC_COUNT; k++)
			g_assert_cmpfloat_with_epsilon (lrg_theme_get_metric (a, k),
			                                lrg_theme_get_metric (b, k), 1e-4);
		g_assert_cmpint (lrg_theme_get_motion (a), ==, lrg_theme_get_motion (b));
		g_assert_cmpint (lrg_theme_get_dark (a), ==, lrg_theme_get_dark (b));
		g_assert_cmpstr (lrg_theme_get_name (a), ==, lrg_theme_get_name (b));
		g_assert_cmpuint (lrg_theme_get_style_rules (b)->len, ==, 1);
		g_assert_cmpfloat (lrg_style_rule_get_look (g_ptr_array_index (lrg_theme_get_style_rules (b), 0))->radii.top_right,
		                   ==, 2.0f);
	}
}

static void
test_yaml_file_and_font_path (void)
{
	g_autoptr(GError)   error = NULL;
	g_autofree gchar   *dir = g_dir_make_tmp ("lrg-theme-XXXXXX", &error);
	g_autofree gchar   *path = NULL;
	g_autofree gchar   *expect = NULL;
	g_autoptr(LrgTheme) theme = NULL;

	g_assert_no_error (error);
	path = g_build_filename (dir, "game.yaml", NULL);
	g_assert_true (g_file_set_contents (path, "preset: daylight\nfont: fonts/ui.ttf\n", -1, &error));

	theme = lrg_theme_new_from_file (path, &error);
	g_assert_no_error (error);
	expect = g_build_filename (dir, "fonts", "ui.ttf", NULL);
	g_assert_cmpstr (lrg_theme_get_font_path (theme), ==, expect);
	g_assert_false (lrg_theme_get_dark (theme));

	/* Save and reload */
	g_assert_true (lrg_theme_save_to_file (theme, path, &error));
	g_assert_no_error (error);
	g_clear_object (&theme);
	theme = lrg_theme_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_theme_get_name (theme), ==, "daylight");

	g_unlink (path);
	g_rmdir (dir);
}

static void
test_yaml_errors (void)
{
	static const struct { const gchar *yaml; const gchar *needle; } cases[] = {
		{ "colours: {}\n", "unknown key 'colours'" },
		{ "colors:\n  pannel: \"#fff\"\n", "unknown role 'pannel'" },
		{ "colors:\n  panel: #fff\n", "expected a hex colour" },
		{ "metrics:\n  corner-radius: big\n", "expected a number" },
		{ "preset: neon\n", "unknown preset 'neon'" },
		{ "motion: zippy\n", "unknown value 'zippy'" },
		{ "styles:\n  - part: knobb\n", "unknown value 'knobb'" },
		{ "styles:\n  - state: [hoverd]\n", "unknown state 'hoverd'" },
		{ "styles:\n  - radius: [1, 2]\n", "four values" },
		{ "styles:\n  - colour: \"#fff\"\n", "unknown key 'colour'" },
		{ "styles: nope\n", "expected a list" },
		{ "- a\n- b\n", "must be a mapping" },
		{ "dark: maybe\n", "true or false" },
		{ "styles:\n  - material: stone\n", "unknown value 'stone'" },
		{ "styles:\n  - effect: lava\n", "unknown effect 'lava'" },
		{ "styles:\n  - glass: {frost: 0.5, blurr: 3}\n", "unknown key 'blurr'" },
		{ "styles:\n  - glass: {frost: 30}\n", "outside 0..1" },
		{ "styles:\n  - glass: 3\n", "expected a mapping" },
		{ "styles:\n  - glass: {tint: red}\n", "expected a hex colour" },
		{ "styles:\n  - effect-colors: [\"#fff\", \"#000\", \"#111\", \"#222\"]\n", "one to three" },
		{ "styles:\n  - effect-intensity: -1\n", "outside 0..4" },
		{ "[unclosed\n", "not valid YAML" }
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autoptr(GError)   error = NULL;
		g_autoptr(LrgTheme) theme = lrg_theme_new ();
		GrlColor            before = *lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL);

		g_assert_false (lrg_theme_load_from_data (theme, cases[i].yaml, -1, &error));
		g_assert_error (error, LRG_THEME_ERROR, error->code);
		if (strstr (error->message, cases[i].needle) == NULL)
			g_error ("case %u: '%s' does not mention '%s'", i, error->message, cases[i].needle);

		/* A failed load changes nothing */
		g_assert_true (color_eq (lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), &before));
		g_assert_cmpuint (lrg_theme_get_style_rules (theme)->len, ==, 0);
	}
}

static void
test_yaml_single_changed_signal (void)
{
	g_autoptr(GError)   error = NULL;
	g_autoptr(LrgTheme) theme = lrg_theme_new ();
	gint                changed = 0;

	g_signal_connect (theme, "changed", G_CALLBACK (on_changed_count), &changed);
	g_assert_true (lrg_theme_load_from_data (theme, sample_theme, -1, &error));
	g_assert_no_error (error);
	g_assert_cmpint (changed, ==, 1);

	/* An empty document is fine and still a (trivial) load */
	g_assert_true (lrg_theme_load_from_data (theme, "", -1, &error));
}

/* ==========================================================================
 * Part looks
 * ========================================================================== */

static void
test_part_look_merge (void)
{
	LrgPartLook base;
	LrgPartLook over;
	GrlColor    red = { 255, 0, 0, 255 };
	GrlColor    blue = { 0, 0, 255, 255 };

	lrg_part_look_init (&base);
	g_assert_cmpfloat (base.opacity, ==, 1.0f);
	lrg_part_look_set_background (&base, &red);
	lrg_part_look_set_radius (&base, 6.0f);

	lrg_part_look_init (&over);
	lrg_part_look_set_background (&over, &blue);
	lrg_part_look_set_size (&over, 20.0f);

	lrg_part_look_merge (&base, &over);
	g_assert_cmpuint (base.background.b, ==, 255);
	g_assert_cmpfloat (base.radii.top_left, ==, 6.0f);   /* untouched */
	g_assert_cmpfloat (base.size, ==, 20.0f);
	g_assert_true (base.set & LRG_PART_LOOK_SIZE);
	g_assert_true (base.set & LRG_PART_LOOK_RADII);
}

static void
test_part_look_lerp (void)
{
	LrgPartLook      a;
	LrgPartLook      b;
	LrgPartLook      out;
	GrlColor         black = { 0, 0, 0, 255 };
	GrlColor         white = { 255, 255, 255, 255 };
	LrgPaintGradient g;

	lrg_part_look_init (&a);
	lrg_part_look_init (&b);
	lrg_part_look_set_background (&a, &black);
	lrg_part_look_set_background (&b, &white);
	lrg_part_look_set_radius (&a, 0.0f);
	lrg_part_look_set_radius (&b, 10.0f);
	lrg_part_look_set_size (&b, 8.0f);

	lrg_part_look_lerp (&a, &b, 0.0f, &out);
	g_assert_cmpuint (out.background.r, ==, 0);
	lrg_part_look_lerp (&a, &b, 1.0f, &out);
	g_assert_cmpuint (out.background.r, ==, 255);

	lrg_part_look_lerp (&a, &b, 0.5f, &out);
	g_assert_cmpuint (out.background.r, ==, 128);
	g_assert_cmpfloat (out.radii.top_left, ==, 5.0f);
	g_assert_cmpfloat (out.size, ==, 4.0f);

	/* A gradient fades in from the solid fill */
	lrg_paint_gradient_init_linear (&g, &white, &white, 0.0f);
	lrg_part_look_set_gradient (&b, &g);
	lrg_part_look_lerp (&a, &b, 0.5f, &out);
	g_assert_cmpint (out.gradient.kind, ==, LRG_PAINT_GRADIENT_LINEAR);
	g_assert_cmpuint (out.gradient.start.r, ==, 128);
	g_assert_cmpuint (out.gradient.end.r, ==, 128);
}

/* ==========================================================================
 * Style rules
 * ========================================================================== */

static void
test_rule_matching (void)
{
	g_autoptr(LrgStyleRule) any = lrg_style_rule_new (NULL, NULL, LRG_UI_PART_BOX,
	                                                  LRG_WIDGET_STATE_NONE);
	g_autoptr(LrgStyleRule) widget = lrg_style_rule_new ("LrgWidget", NULL, LRG_UI_PART_BOX,
	                                                     LRG_WIDGET_STATE_NONE);
	g_autoptr(LrgStyleRule) button_hover = lrg_style_rule_new ("LrgButton", NULL,
	                                                           LRG_UI_PART_BOX,
	                                                           LRG_WIDGET_STATE_HOVERED);
	g_autoptr(LrgStyleRule) primary = lrg_style_rule_new (NULL, "primary", LRG_UI_PART_BOX,
	                                                      LRG_WIDGET_STATE_NONE);
	g_autoptr(LrgStyleRule) unknown = lrg_style_rule_new ("LrgNoSuchWidget", NULL,
	                                                      LRG_UI_PART_BOX,
	                                                      LRG_WIDGET_STATE_NONE);
	GQuark                  classes[] = { g_quark_from_string ("primary") };
	GType                   button = LRG_TYPE_BUTTON;

	g_assert_true (lrg_style_rule_matches (any, button, NULL, 0, LRG_UI_PART_BOX, 0));
	g_assert_false (lrg_style_rule_matches (any, button, NULL, 0, LRG_UI_PART_KNOB, 0));

	/* Subtypes match their ancestors' rules */
	g_assert_true (lrg_style_rule_matches (widget, button, NULL, 0, LRG_UI_PART_BOX, 0));

	/* All required states must be present (extra states are fine) */
	g_assert_false (lrg_style_rule_matches (button_hover, button, NULL, 0, LRG_UI_PART_BOX, 0));
	g_assert_true (lrg_style_rule_matches (button_hover, button, NULL, 0, LRG_UI_PART_BOX,
	                                       LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED));
	g_assert_false (lrg_style_rule_matches (button_hover, LRG_TYPE_LABEL, NULL, 0,
	                                        LRG_UI_PART_BOX, LRG_WIDGET_STATE_HOVERED));

	g_assert_false (lrg_style_rule_matches (primary, button, NULL, 0, LRG_UI_PART_BOX, 0));
	g_assert_true (lrg_style_rule_matches (primary, button, classes, 1, LRG_UI_PART_BOX, 0));

	g_assert_false (lrg_style_rule_matches (unknown, button, NULL, 0, LRG_UI_PART_BOX, 0));

	/* Specificity: type 1, class 2, each state 2 */
	g_assert_cmpuint (lrg_style_rule_get_specificity (any), ==, 0);
	g_assert_cmpuint (lrg_style_rule_get_specificity (widget), ==, 1);
	g_assert_cmpuint (lrg_style_rule_get_specificity (primary), ==, 2);
	g_assert_cmpuint (lrg_style_rule_get_specificity (button_hover), ==, 3);
}

static LrgStyleRule *
bg_rule (const gchar    *type,
         const gchar    *klass,
         LrgWidgetState  state,
         guint8          red)
{
	LrgStyleRule *rule = lrg_style_rule_new (type, klass, LRG_UI_PART_BOX, state);
	GrlColor      c = { red, 0, 0, 255 };

	lrg_part_look_set_background (lrg_style_rule_get_look (rule), &c);
	return rule;
}

static guint8
resolve_red (LrgTheme       *theme,
             const GQuark   *classes,
             guint           n,
             LrgWidgetState  state)
{
	LrgPartLook look;

	lrg_part_look_init (&look);
	lrg_theme_resolve_look (theme, LRG_TYPE_BUTTON, classes, n, LRG_UI_PART_BOX, state, &look);
	return look.background.r;
}

static void
test_rule_resolution_order (void)
{
	g_autoptr(LrgTheme) theme = lrg_theme_new ();
	GQuark              primary[] = { g_quark_from_string ("primary") };
	LrgStyleRule       *r;

	/* Added from most to least specific on purpose: order of addition
	 * only breaks ties */
	r = bg_rule (NULL, "primary", LRG_WIDGET_STATE_HOVERED, 40);  lrg_theme_add_style_rule (theme, r); lrg_style_rule_unref (r);
	r = bg_rule (NULL, "primary", LRG_WIDGET_STATE_NONE, 30);     lrg_theme_add_style_rule (theme, r); lrg_style_rule_unref (r);
	r = bg_rule ("LrgButton", NULL, LRG_WIDGET_STATE_NONE, 20);   lrg_theme_add_style_rule (theme, r); lrg_style_rule_unref (r);
	r = bg_rule (NULL, NULL, LRG_WIDGET_STATE_NONE, 10);          lrg_theme_add_style_rule (theme, r); lrg_style_rule_unref (r);

	g_assert_cmpuint (resolve_red (theme, NULL, 0, 0), ==, 20);
	g_assert_cmpuint (resolve_red (theme, primary, 1, 0), ==, 30);
	g_assert_cmpuint (resolve_red (theme, primary, 1, LRG_WIDGET_STATE_HOVERED), ==, 40);

	/* Tie: same specificity, the later rule wins */
	r = bg_rule ("LrgButton", NULL, LRG_WIDGET_STATE_NONE, 21);   lrg_theme_add_style_rule (theme, r); lrg_style_rule_unref (r);
	g_assert_cmpuint (resolve_red (theme, NULL, 0, 0), ==, 21);
}

static void
test_rule_cache_invalidation (void)
{
	g_autoptr(LrgTheme) theme = lrg_theme_new ();
	LrgStyleRule       *r;

	r = bg_rule ("LrgButton", NULL, LRG_WIDGET_STATE_NONE, 50);
	lrg_theme_add_style_rule (theme, r);
	lrg_style_rule_unref (r);
	g_assert_cmpuint (resolve_red (theme, NULL, 0, 0), ==, 50);
	g_assert_cmpuint (resolve_red (theme, NULL, 0, 0), ==, 50);   /* cached */

	/* Any theme change drops the cache */
	r = bg_rule ("LrgButton", NULL, LRG_WIDGET_STATE_NONE, 60);
	lrg_theme_add_style_rule (theme, r);
	lrg_style_rule_unref (r);
	g_assert_cmpuint (resolve_red (theme, NULL, 0, 0), ==, 60);

	lrg_theme_clear_style_rules (theme);
	g_assert_cmpuint (resolve_red (theme, NULL, 0, 0), ==, 0);
}


/* ==========================================================================
 * Materials and effects
 * ========================================================================== */

static const gchar *materials_yaml =
	"styles:\n"
	"  - widget: LrgPanel\n"
	"    part: box\n"
	"    material: glass\n"
	"    glass: {frost: 0.6, refraction: 10, saturation: 1.4, tint: \"#ffffff20\", rim: 0.8, dispersion: 0.3, blur: 18}\n"
	"  - class: hero\n"
	"    part: box\n"
	"    effect: aurora\n"
	"    effect-colors: [\"#101030\", \"#7c6cff\"]\n"
	"    effect-intensity: 0.7\n"
	"  - class: plain\n"
	"    part: box\n"
	"    material: tinted\n"
	"    effect: none\n";

static void
check_material_rules (LrgTheme *theme)
{
	GPtrArray         *rules = lrg_theme_get_style_rules (theme);
	const LrgPartLook *glass;
	const LrgPartLook *hero;
	const LrgPartLook *plain;
	GrlColor           c;

	g_assert_cmpuint (rules->len, ==, 3);
	glass = lrg_style_rule_get_look (g_ptr_array_index (rules, 0));
	hero = lrg_style_rule_get_look (g_ptr_array_index (rules, 1));
	plain = lrg_style_rule_get_look (g_ptr_array_index (rules, 2));

	g_assert_true (glass->set & LRG_PART_LOOK_MATERIAL);
	g_assert_true (glass->set & LRG_PART_LOOK_GLASS);
	g_assert_false (glass->set & LRG_PART_LOOK_EFFECT);
	g_assert_cmpint (glass->material, ==, LRG_PAINT_MATERIAL_GLASS);
	g_assert_cmpfloat_with_epsilon (glass->glass.frost, 0.6f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (glass->glass.refraction, 10.0f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (glass->glass.saturation, 1.4f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (glass->glass.rim_light, 0.8f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (glass->glass.dispersion, 0.3f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (glass->glass.blur_radius, 18.0f, 1e-5f);
	g_assert_cmpuint (glass->glass.tint.a, ==, 0x20);

	g_assert_cmpstr (lrg_part_look_get_effect_name (hero), ==, "aurora");
	g_assert_true (hero->set & LRG_PART_LOOK_EFFECT_COLORS);
	g_assert_true (hero->set & LRG_PART_LOOK_EFFECT_INTENSITY);
	g_assert_cmpfloat_with_epsilon (hero->effect_params.intensity, 0.7f, 1e-5f);
	g_assert_true (lrg_color_parse ("#7c6cff", &c));
	g_assert_true (color_eq (&hero->effect_params.color2, &c));
	/* a missing third colour repeats the second */
	g_assert_true (color_eq (&hero->effect_params.color3, &c));

	g_assert_cmpint (plain->material, ==, LRG_PAINT_MATERIAL_TINTED);
	g_assert_true (plain->set & LRG_PART_LOOK_EFFECT);
	g_assert_null (lrg_part_look_get_effect_name (plain));
}

static void
test_yaml_materials (void)
{
	g_autoptr(GError)   error = NULL;
	g_autoptr(LrgTheme) theme = lrg_theme_new ();
	g_autoptr(LrgTheme) again = lrg_theme_new ();
	g_autofree gchar   *data = NULL;

	g_assert_true (lrg_theme_load_from_data (theme, materials_yaml, -1, &error));
	g_assert_no_error (error);
	check_material_rules (theme);

	/* Round trip through lrg_theme_to_data () */
	data = lrg_theme_to_data (theme, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (data, "material"));
	g_assert_true (lrg_theme_load_from_data (again, data, -1, &error));
	g_assert_no_error (error);
	check_material_rules (again);
}

static void
test_part_look_materials (void)
{
	LrgPartLook   plain;
	LrgPartLook   glassy;
	LrgPartLook   out;
	LrgPartLook   rule;
	LrgPaintGlass g;
	GrlColor      bg = { 10, 20, 30, 200 };

	/* get_glass: defaults, adjusted per material */
	lrg_part_look_init (&glassy);
	lrg_part_look_set_background (&glassy, &bg);
	lrg_part_look_set_material (&glassy, LRG_PAINT_MATERIAL_FROSTED);
	lrg_part_look_get_glass (&glassy, &g);
	g_assert_cmpfloat (g.frost, ==, 1.0f);
	g_assert_cmpfloat (g.refraction, >, 0.0f);
	lrg_part_look_set_material (&glassy, LRG_PAINT_MATERIAL_BLUR);
	lrg_part_look_get_glass (&glassy, &g);
	g_assert_cmpfloat (g.refraction, ==, 0.0f);
	g_assert_cmpfloat (g.rim_light, ==, 0.0f);
	g_assert_cmpuint (g.tint.a, ==, 0);
	lrg_part_look_set_material (&glassy, LRG_PAINT_MATERIAL_TINTED);
	lrg_part_look_get_glass (&glassy, &g);
	g_assert_true (color_eq (&g.tint, &bg));

	/* merge copies only what is set */
	lrg_part_look_init (&rule);
	lrg_part_look_set_effect (&rule, "neon");
	lrg_part_look_set_effect_intensity (&rule, 0.5f);
	lrg_part_look_merge (&glassy, &rule);
	g_assert_cmpstr (lrg_part_look_get_effect_name (&glassy), ==, "neon");
	g_assert_cmpfloat (glassy.effect_params.intensity, ==, 0.5f);
	g_assert_cmpint (glassy.material, ==, LRG_PAINT_MATERIAL_TINTED);

	/* lerp: a plain fill fades into glass through material_opacity, and
	 * the effect fades in through its intensity */
	lrg_part_look_init (&plain);
	lrg_part_look_set_background (&plain, &bg);
	lrg_part_look_set_material (&glassy, LRG_PAINT_MATERIAL_GLASS);
	lrg_part_look_lerp (&plain, &glassy, 0.25f, &out);
	g_assert_cmpint (out.material, ==, LRG_PAINT_MATERIAL_GLASS);
	g_assert_cmpfloat_with_epsilon (out.material_opacity, 0.25f, 1e-5f);
	g_assert_cmpstr (lrg_part_look_get_effect_name (&out), ==, "neon");
	g_assert_cmpfloat_with_epsilon (out.effect_params.intensity, 0.125f, 1e-5f);

	lrg_part_look_lerp (&glassy, &plain, 0.25f, &out);
	g_assert_cmpfloat_with_epsilon (out.material_opacity, 0.75f, 1e-5f);
}

static void
test_paint_box_materials (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	g_autoptr(LrgPanel)     panel = lrg_panel_new ();
	LrgPartLook             look;
	GrlColor                bg = { 30, 30, 40, 255 };
	GrlColor                border = { 255, 255, 255, 60 };
	guint                   n;

	/* No material, no effect: one instance for fill and border */
	lrg_part_look_init (&look);
	lrg_part_look_set_background (&look, &bg);
	lrg_part_look_set_border (&look, &border, 1.0f);
	lrg_widget_paint_box (LRG_WIDGET (panel), list, &look, 0.0f, 0.0f, 100.0f, 50.0f);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 1);

	/* Glass: the slab, then the border on top */
	lrg_paint_list_clear (list);
	lrg_part_look_set_material (&look, LRG_PAINT_MATERIAL_GLASS);
	lrg_widget_paint_box (LRG_WIDGET (panel), list, &look, 0.0f, 0.0f, 100.0f, 50.0f);
	n = lrg_paint_list_get_n_instances (list);
	g_assert_cmpuint (n, ==, 2);
	g_assert_cmpint (lrg_paint_instance_get_prim (lrg_paint_list_get_instance (list, 0)), ==,
	                 LRG_PAINT_PRIM_GLASS);
	g_assert_cmpfloat (lrg_paint_list_get_instance (list, 1)->params[0], ==, 1.0f);

	/* Half-faded material: the fill cross-fades over it */
	lrg_paint_list_clear (list);
	look.material_opacity = 0.5f;
	lrg_widget_paint_box (LRG_WIDGET (panel), list, &look, 0.0f, 0.0f, 100.0f, 50.0f);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 3);
	g_assert_cmpfloat (lrg_paint_list_get_instance (list, 0)->data[2], ==, 0.5f);
	g_assert_cmpuint (lrg_paint_list_get_instance (list, 1)->color[3], ==, 128);

	/* Blur material uses the blur primitive; an effect goes on top */
	lrg_paint_list_clear (list);
	lrg_part_look_set_material (&look, LRG_PAINT_MATERIAL_BLUR);
	lrg_part_look_set_effect (&look, "shimmer");
	lrg_widget_paint_box (LRG_WIDGET (panel), list, &look, 0.0f, 0.0f, 100.0f, 50.0f);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 3);
	g_assert_cmpint (lrg_paint_instance_get_prim (lrg_paint_list_get_instance (list, 0)), ==,
	                 LRG_PAINT_PRIM_BLUR);
	g_assert_cmpint (lrg_paint_instance_get_prim (lrg_paint_list_get_instance (list, 1)), ==,
	                 LRG_PAINT_PRIM_EFFECT);
	/* default palette: the background first */
	g_assert_cmpuint (lrg_paint_list_get_instance (list, 1)->color[0], ==, 30);

	/* Effect without material replaces the fill */
	lrg_paint_list_clear (list);
	lrg_part_look_set_material (&look, LRG_PAINT_MATERIAL_NONE);
	lrg_widget_paint_box (LRG_WIDGET (panel), list, &look, 0.0f, 0.0f, 100.0f, 50.0f);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 2);
	g_assert_cmpint (lrg_paint_instance_get_prim (lrg_paint_list_get_instance (list, 0)), ==,
	                 LRG_PAINT_PRIM_EFFECT);
}

/* ==========================================================================
 * Palette generation and contrast
 * ========================================================================== */

static void
test_contrast_ratio_known_values (void)
{
	GrlColor black = { 0, 0, 0, 255 };
	GrlColor white = { 255, 255, 255, 255 };
	GrlColor grey = { 119, 119, 119, 255 };
	GrlColor clear_white = { 255, 255, 255, 0 };

	/* The WCAG extremes */
	g_assert_cmpfloat_with_epsilon (lrg_color_contrast_ratio (&black, &white), 21.0f, 0.001f);
	g_assert_cmpfloat_with_epsilon (lrg_color_contrast_ratio (&white, &black), 21.0f, 0.001f);
	g_assert_cmpfloat_with_epsilon (lrg_color_contrast_ratio (&grey, &grey), 1.0f, 0.0001f);
	g_assert_cmpfloat_with_epsilon (lrg_color_contrast_ratio (&white, &white), 1.0f, 0.0001f);

	/* #777777 on white is the classic "just fails AA" grey: 4.48 */
	g_assert_cmpfloat_with_epsilon (lrg_color_contrast_ratio (&grey, &white), 4.48f, 0.01f);

	/* Alpha is ignored */
	g_assert_cmpfloat_with_epsilon (lrg_color_contrast_ratio (&clear_white, &black), 21.0f, 0.001f);

	/* Same maths as the role-based helper */
	{
		g_autoptr(LrgTheme) theme = lrg_theme_new ();

		g_assert_cmpfloat (lrg_theme_get_contrast (theme, LRG_THEME_COLOR_TEXT, LRG_THEME_COLOR_PANEL), ==,
		                   lrg_color_contrast_ratio (lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT),
		                                             lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL)));
	}
}

static void
test_generate_palette_deterministic (void)
{
	g_autoptr(LrgTheme) a = lrg_theme_new ();
	g_autoptr(LrgTheme) b = lrg_theme_new_preset ("catppuccin-latte");
	GrlColor            accent;
	gint                count = 0;
	guint               i;

	g_assert_true (lrg_color_parse ("#22d3ee", &accent));
	g_signal_connect (a, "changed", G_CALLBACK (on_changed_count), &count);

	/* Same inputs on different starting themes give identical colours */
	lrg_theme_generate_palette (a, &accent, 0.4f, 0.6f, TRUE);
	lrg_theme_generate_palette (b, &accent, 0.4f, 0.6f, TRUE);
	for (i = 0; i < LRG_THEME_COLOR_COUNT; i++)
		g_assert_true (color_eq (lrg_theme_get_color (a, i), lrg_theme_get_color (b, i)));
	g_assert_cmpint (count, ==, 1);

	/* ... and running it again changes nothing */
	lrg_theme_generate_palette (a, &accent, 0.4f, 0.6f, TRUE);
	for (i = 0; i < LRG_THEME_COLOR_COUNT; i++)
		g_assert_true (color_eq (lrg_theme_get_color (a, i), lrg_theme_get_color (b, i)));

	/* The accent family is derived as lrg_theme_set_accent() does */
	{
		g_autoptr(LrgTheme) c = lrg_theme_new ();

		lrg_theme_set_dark (c, TRUE);
		lrg_theme_set_accent (c, &accent);
		g_assert_true (color_eq (lrg_theme_get_color (a, LRG_THEME_COLOR_ACCENT), &accent));
		g_assert_true (color_eq (lrg_theme_get_color (a, LRG_THEME_COLOR_ACCENT_HOVER),
		                         lrg_theme_get_color (c, LRG_THEME_COLOR_ACCENT_HOVER)));
		g_assert_true (color_eq (lrg_theme_get_color (a, LRG_THEME_COLOR_ACCENT_PRESSED),
		                         lrg_theme_get_color (c, LRG_THEME_COLOR_ACCENT_PRESSED)));
		g_assert_true (color_eq (lrg_theme_get_color (a, LRG_THEME_COLOR_SELECTION),
		                         lrg_theme_get_color (c, LRG_THEME_COLOR_SELECTION)));
		g_assert_true (color_eq (lrg_theme_get_color (a, LRG_THEME_COLOR_FOCUS),
		                         lrg_theme_get_color (c, LRG_THEME_COLOR_FOCUS)));
		g_assert_true (color_eq (lrg_theme_get_color (a, LRG_THEME_COLOR_TEXT_ON_ACCENT),
		                         lrg_theme_get_color (c, LRG_THEME_COLOR_TEXT_ON_ACCENT)));
	}

	/* Metrics, motion and name are left alone */
	g_assert_cmpstr (lrg_theme_get_name (b), ==, "catppuccin-latte");
	g_assert_cmpfloat (lrg_theme_get_metric (b, LRG_THEME_METRIC_CORNER_RADIUS), ==, 10.0f);
	g_assert_cmpint (lrg_theme_get_motion (b), ==, LRG_MOTION_BOUNCY);
}

static void
test_generate_palette_dark_light (void)
{
	g_autoptr(LrgTheme) theme = lrg_theme_new ();
	GrlColor            accent;
	gfloat              window_l;
	gfloat              panel_l;
	gfloat              text_l;

	g_assert_true (lrg_color_parse ("#7c6cff", &accent));

	/* Dark: dark layers stepping up, light text */
	lrg_theme_generate_palette (theme, &accent, 0.3f, 0.5f, TRUE);
	g_assert_true (lrg_theme_get_dark (theme));
	window_l = lrg_paint_color_luminance (lrg_theme_get_color (theme, LRG_THEME_COLOR_WINDOW));
	panel_l = lrg_paint_color_luminance (lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
	text_l = lrg_paint_color_luminance (lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
	g_assert_cmpfloat (window_l, <, 0.05f);
	g_assert_cmpfloat (panel_l, >, window_l);
	g_assert_cmpfloat (lrg_paint_color_luminance (lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE)), >, panel_l);
	g_assert_cmpfloat (text_l, >, 0.7f);

	/* Light: light surfaces, dark text */
	lrg_theme_generate_palette (theme, &accent, 0.3f, 0.5f, FALSE);
	g_assert_false (lrg_theme_get_dark (theme));
	window_l = lrg_paint_color_luminance (lrg_theme_get_color (theme, LRG_THEME_COLOR_WINDOW));
	panel_l = lrg_paint_color_luminance (lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
	text_l = lrg_paint_color_luminance (lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
	g_assert_cmpfloat (window_l, >, 0.6f);
	g_assert_cmpfloat (panel_l, >, window_l);
	g_assert_cmpfloat (text_l, <, 0.05f);

	/* Depth spreads the dark layers further apart */
	{
		gfloat flat;
		gfloat deep;

		lrg_theme_generate_palette (theme, &accent, 0.3f, 0.0f, TRUE);
		flat = lrg_theme_get_contrast (theme, LRG_THEME_COLOR_SURFACE, LRG_THEME_COLOR_WINDOW);
		lrg_theme_generate_palette (theme, &accent, 0.3f, 1.0f, TRUE);
		deep = lrg_theme_get_contrast (theme, LRG_THEME_COLOR_SURFACE, LRG_THEME_COLOR_WINDOW);
		g_assert_cmpfloat (deep, >, flat);
	}

	/* Tint 0 gives pure greys, tint 1 carries the accent's hue */
	{
		const GrlColor *panel;

		lrg_theme_generate_palette (theme, &accent, 0.0f, 0.5f, TRUE);
		panel = lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL);
		g_assert_cmpuint (panel->r, ==, panel->g);
		g_assert_cmpuint (panel->g, ==, panel->b);

		lrg_theme_generate_palette (theme, &accent, 1.0f, 0.5f, TRUE);
		panel = lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL);
		/* #7c6cff is blue-violet: blue leads */
		g_assert_cmpuint (panel->b, >, panel->g);
	}

	/* Generated palettes already read well */
	{
		static const gchar *hexes[] = { "#7c6cff", "#22d3ee", "#34d399", "#fbbf24", "#ff3d7f", "#808080" };
		guint               i;

		for (i = 0; i < G_N_ELEMENTS (hexes); i++)
		{
			guint d;

			g_assert_true (lrg_color_parse (hexes[i], &accent));
			for (d = 0; d < 2; d++)
			{
				guint p;

				lrg_theme_generate_palette (theme, &accent, 0.5f, 0.5f, d == 0);
				g_assert_cmpuint (lrg_theme_count_contrast_failures (theme, 3.0f), ==, 0);

				/* Everything but text on the accent shades is AA */
				for (p = 0; p < lrg_theme_get_n_contrast_pairs (); p++)
				{
					LrgThemeColor fg;
					LrgThemeColor bg;

					lrg_theme_get_contrast_pair (p, &fg, &bg);
					if (fg != LRG_THEME_COLOR_TEXT_ON_ACCENT)
						g_assert_cmpfloat (lrg_theme_get_contrast (theme, fg, bg), >=, 4.5f);
				}
			}
		}
	}
}

static void
test_contrast_pairs (void)
{
	LrgThemeColor fg;
	LrgThemeColor bg;
	guint         n = lrg_theme_get_n_contrast_pairs ();
	guint         i;

	g_assert_cmpuint (n, >=, 8);
	for (i = 0; i < n; i++)
	{
		g_assert_true (lrg_theme_get_contrast_pair (i, &fg, &bg));
		g_assert_cmpint (fg, !=, bg);
		g_assert_cmpint ((gint) fg, <, LRG_THEME_COLOR_COUNT);
		g_assert_cmpint ((gint) bg, <, LRG_THEME_COLOR_COUNT);
	}
	g_assert_true (lrg_theme_get_contrast_pair (0, &fg, &bg));
	g_assert_cmpint (fg, ==, LRG_THEME_COLOR_TEXT);
	g_assert_cmpint (bg, ==, LRG_THEME_COLOR_WINDOW);
	g_assert_true (lrg_theme_get_contrast_pair (0, NULL, NULL));
	g_assert_false (lrg_theme_get_contrast_pair (n, &fg, &bg));
}

/* assert_all_pairs_pass: every listed pair reaches @min */
static void
assert_all_pairs_pass (LrgTheme *theme,
                       gfloat    min)
{
	guint i;

	for (i = 0; i < lrg_theme_get_n_contrast_pairs (); i++)
	{
		LrgThemeColor fg;
		LrgThemeColor bg;

		lrg_theme_get_contrast_pair (i, &fg, &bg);
		if (lrg_theme_get_contrast (theme, fg, bg) < min)
			g_error ("%s: pair %u (%d on %d) is %.2f, wanted %.2f",
			         lrg_theme_get_name (theme), i, fg, bg,
			         (gdouble) lrg_theme_get_contrast (theme, fg, bg), (gdouble) min);
	}
	g_assert_cmpuint (lrg_theme_count_contrast_failures (theme, min), ==, 0);
}

static void
test_fix_contrast_presets (void)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	guint                i;

	for (i = 0; presets[i] != NULL; i++)
	{
		g_autoptr(LrgTheme) theme = lrg_theme_new_preset (presets[i]);
		g_autoptr(LrgTheme) before = lrg_theme_copy (theme);
		guint               c;

		lrg_theme_fix_contrast (theme, 4.5f);
		assert_all_pairs_pass (theme, 4.5f);

		/* Fixing again is a no-op */
		g_assert_cmpuint (lrg_theme_fix_contrast (theme, 4.5f), ==, 0);

		/* Roles outside the pairs are never touched */
		for (c = 0; c < LRG_THEME_COLOR_COUNT; c++)
		{
			guint    p;
			gboolean in_pair = FALSE;

			for (p = 0; p < lrg_theme_get_n_contrast_pairs (); p++)
			{
				LrgThemeColor fg;
				LrgThemeColor bg;

				lrg_theme_get_contrast_pair (p, &fg, &bg);
				in_pair = in_pair || fg == (LrgThemeColor) c || bg == (LrgThemeColor) c;
			}
			if (!in_pair)
				g_assert_true (color_eq (lrg_theme_get_color (theme, c), lrg_theme_get_color (before, c)));
		}

		/* AAA works too */
		lrg_theme_fix_contrast (theme, 7.0f);
		assert_all_pairs_pass (theme, 7.0f);
	}
}

static void
test_fix_contrast_bad_theme (void)
{
	g_autoptr(LrgTheme) theme = lrg_theme_new ();
	GrlColor            c;
	gint                count = 0;
	guint               changed;
	guint               i;

	/* Everything mid-grey, text the same as its backgrounds, text on
	 * accent identical to the accent: every pair fails */
	lrg_theme_set_name (theme, "bad");
	lrg_color_parse ("#777777", &c);
	for (i = 0; i < LRG_THEME_COLOR_COUNT; i++)
		lrg_theme_set_color (theme, i, &c);
	lrg_color_parse ("#8a8a8a", &c);
	lrg_theme_set_color (theme, LRG_THEME_COLOR_PANEL, &c);
	lrg_color_parse ("#6f6f6f", &c);
	lrg_theme_set_color (theme, LRG_THEME_COLOR_SURFACE, &c);
	g_assert_cmpuint (lrg_theme_count_contrast_failures (theme, 4.5f), ==, lrg_theme_get_n_contrast_pairs ());

	g_signal_connect (theme, "changed", G_CALLBACK (on_changed_count), &count);
	changed = lrg_theme_fix_contrast (theme, 4.5f);
	g_assert_cmpuint (changed, >, 0);
	g_assert_cmpint (count, ==, 1);
	assert_all_pairs_pass (theme, 4.5f);

	/* Light text on a dark-but-not-enough background: only the text
	 * moves, and only as far as needed */
	{
		g_autoptr(LrgTheme) t2 = lrg_theme_new ();
		GrlColor            panel;
		gfloat              ratio;

		lrg_color_parse ("#5a5a66", &c);
		lrg_theme_set_color (t2, LRG_THEME_COLOR_TEXT_MUTED, &c);
		panel = *lrg_theme_get_color (t2, LRG_THEME_COLOR_PANEL);
		lrg_theme_fix_contrast (t2, 4.5f);
		g_assert_true (color_eq (lrg_theme_get_color (t2, LRG_THEME_COLOR_PANEL), &panel));
		ratio = lrg_theme_get_contrast (t2, LRG_THEME_COLOR_TEXT_MUTED, LRG_THEME_COLOR_PANEL);
		g_assert_cmpfloat (ratio, >=, 4.5f);
		/* the surface pair (lighter) decides how far it moves */
		ratio = lrg_theme_get_contrast (t2, LRG_THEME_COLOR_TEXT_MUTED, LRG_THEME_COLOR_SURFACE);
		g_assert_cmpfloat (ratio, >=, 4.5f);
		g_assert_cmpfloat (ratio, <, 4.8f);
	}
}

static void
test_generated_yaml_round_trip (void)
{
	static const struct { const gchar *hex; gfloat tint; gfloat depth; gboolean dark; } cases[] = {
		{ "#7c6cff", 0.3f, 0.5f, TRUE },
		{ "#22d3ee", 0.8f, 0.9f, TRUE },
		{ "#ff7a45", 0.2f, 0.1f, FALSE },
		{ "#16a34a", 1.0f, 1.0f, FALSE }
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autoptr(LrgTheme) theme = lrg_theme_new ();
		g_autoptr(LrgTheme) back = NULL;
		g_autoptr(GError)   error = NULL;
		g_autofree gchar   *yaml = NULL;
		GrlColor            accent;
		guint               c;

		g_assert_true (lrg_color_parse (cases[i].hex, &accent));
		lrg_theme_generate_palette (theme, &accent, cases[i].tint, cases[i].depth, cases[i].dark);
		lrg_theme_fix_contrast (theme, 4.5f);
		lrg_theme_set_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS, 13.0f);
		lrg_theme_set_name (theme, "generated");

		yaml = lrg_theme_to_data (theme, NULL, &error);
		g_assert_no_error (error);
		back = lrg_theme_new_from_data (yaml, -1, &error);
		g_assert_no_error (error);
		g_assert_nonnull (back);

		for (c = 0; c < LRG_THEME_COLOR_COUNT; c++)
			g_assert_true (color_eq (lrg_theme_get_color (theme, c), lrg_theme_get_color (back, c)));
		g_assert_cmpint (lrg_theme_get_dark (back), ==, cases[i].dark);
		g_assert_cmpfloat (lrg_theme_get_metric (back, LRG_THEME_METRIC_CORNER_RADIUS), ==, 13.0f);
		g_assert_cmpstr (lrg_theme_get_name (back), ==, "generated");
		g_assert_cmpuint (lrg_theme_count_contrast_failures (back, 4.5f), ==, 0);
	}
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/ui/theme/color-parse", test_color_parse);
	g_test_add_func ("/ui/theme/roles-and-metrics", test_roles_and_metrics);
	g_test_add_func ("/ui/theme/legacy-aliases", test_legacy_aliases);
	g_test_add_func ("/ui/theme/accent-derivation", test_accent_derivation);
	g_test_add_func ("/ui/theme/presets", test_presets);
	g_test_add_func ("/ui/theme/set-default", test_set_default);
	g_test_add_func ("/ui/theme/copy", test_copy);
	g_test_add_func ("/ui/theme/yaml/load", test_yaml_load);
	g_test_add_func ("/ui/theme/yaml/round-trip", test_yaml_round_trip);
	g_test_add_func ("/ui/theme/yaml/file-and-font-path", test_yaml_file_and_font_path);
	g_test_add_func ("/ui/theme/yaml/errors", test_yaml_errors);
	g_test_add_func ("/ui/theme/yaml/single-changed", test_yaml_single_changed_signal);

	g_test_add_func ("/ui/style/look-merge", test_part_look_merge);
	g_test_add_func ("/ui/style/look-lerp", test_part_look_lerp);
	g_test_add_func ("/ui/style/rule-matching", test_rule_matching);
	g_test_add_func ("/ui/style/resolution-order", test_rule_resolution_order);
	g_test_add_func ("/ui/style/cache-invalidation", test_rule_cache_invalidation);
	g_test_add_func ("/ui/theme/yaml/materials", test_yaml_materials);
	g_test_add_func ("/ui/style/look-materials", test_part_look_materials);
	g_test_add_func ("/ui/style/paint-box-materials", test_paint_box_materials);

	g_test_add_func ("/ui/theme/contrast/ratio", test_contrast_ratio_known_values);
	g_test_add_func ("/ui/theme/contrast/pairs", test_contrast_pairs);
	g_test_add_func ("/ui/theme/contrast/fix-presets", test_fix_contrast_presets);
	g_test_add_func ("/ui/theme/contrast/fix-bad-theme", test_fix_contrast_bad_theme);
	g_test_add_func ("/ui/theme/palette/deterministic", test_generate_palette_deterministic);
	g_test_add_func ("/ui/theme/palette/dark-light", test_generate_palette_dark_light);
	g_test_add_func ("/ui/theme/palette/yaml-round-trip", test_generated_yaml_round_trip);

	return g_test_run ();
}
