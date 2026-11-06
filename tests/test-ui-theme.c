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

	return g_test_run ();
}
