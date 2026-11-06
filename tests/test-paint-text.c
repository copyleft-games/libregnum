/* test-paint-text.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * SDF fonts, fallback chains and bidirectional text.
 *
 * The first half needs no window: font tables, metrics, kerning,
 * fallback selection, bidi reordering, atlas growth and the recorded
 * glyph instances. The second half opens a hidden window when a
 * display is available (run it under an isolated Xvfb) and reads
 * pixels back: crisp edges at 1x and 3x, outline and glow, a fallback
 * glyph, right-to-left order, batching and the theme binding.
 *
 * Tests that need a particular installed font skip when it is missing.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>
#include <libregnum.h>

#define TARGET_W (256)
#define TARGET_H (128)

/* UTF-8 test strings */
#define HE_ALEF  "\xd7\x90"   /* U+05D0 */
#define HE_BET   "\xd7\x91"   /* U+05D1 */
#define HE_GIMEL "\xd7\x92"   /* U+05D2 */
#define AR_SEEN  "\xd8\xb3"   /* U+0633 */
#define AR_LAM   "\xd9\x84"   /* U+0644 */
#define AR_ALEF  "\xd8\xa7"   /* U+0627 */
#define AR_MEEM  "\xd9\x85"   /* U+0645 */

static const GrlColor C_WHITE = { 255, 255, 255, 255 };
static const GrlColor C_RED = { 255, 0, 0, 255 };
static const GrlColor C_GREEN = { 0, 255, 0, 255 };
static const GrlColor C_BLUE = { 0, 0, 255, 255 };

/* ==========================================================================
 * Font helpers
 * ========================================================================== */

/* open_family: a fresh font (no fallbacks) or NULL when not installed */
static LrgPaintFont *
open_family (const gchar *family)
{
	g_autofree gchar *path = NULL;
	gint              index = 0;

	path = lrg_paint_font_find_file (family, &index);
	if (path == NULL)
		return NULL;
	return lrg_paint_font_new_from_file_index (path, index, 0, NULL);
}

#define REQUIRE_FONT(var, family) \
	do { \
		(var) = open_family (family); \
		if ((var) == NULL) \
		{ \
			g_test_skip ("font \"" family "\" is not installed"); \
			return; \
		} \
	} while (0)

/* count_prim: instances of @prim in @list */
static guint
count_prim (LrgPaintList *list,
            LrgPaintPrim  prim)
{
	guint n = lrg_paint_list_get_n_instances (list);
	guint count = 0;
	guint i;

	for (i = 0; i < n; i++)
		if (lrg_paint_instance_get_prim (lrg_paint_list_get_instance (list, i)) == prim)
			count++;
	return count;
}

/* ==========================================================================
 * Headless: fonts and metrics
 * ========================================================================== */

static void
test_style_boxed (void)
{
	LrgPaintTextStyle                  style;
	g_autoptr(LrgPaintTextStyle)       copy = NULL;

	lrg_paint_text_style_init (&style, &C_RED);
	g_assert_cmpuint (style.color.r, ==, 255);
	g_assert_cmpuint (style.color.g, ==, 0);
	g_assert_cmpfloat (style.outline_width, ==, 0.0f);
	g_assert_cmpuint (style.shadow_color.a, ==, 0);
	g_assert_cmpint (style.direction, ==, LRG_TEXT_DIRECTION_AUTO);

	style.outline_width = 2.0f;
	copy = lrg_paint_text_style_copy (&style);
	g_assert_cmpfloat (copy->outline_width, ==, 2.0f);
	g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_PAINT_TEXT_STYLE));

	lrg_paint_text_style_init (&style, NULL);
	g_assert_cmpuint (style.color.a, ==, 255);
	g_assert_cmpuint (style.color.b, ==, 255);
}

static void
test_load_errors (void)
{
	g_autoptr(GError)       error = NULL;
	g_autoptr(LrgPaintFont) font = NULL;
	g_autoptr(GBytes)       junk = NULL;
	g_autofree gchar       *path = NULL;
	gint                    fd;

	/* Missing file */
	font = lrg_paint_font_new_from_file ("/nonexistent/font.ttf", &error);
	g_assert_null (font);
	g_assert_nonnull (error);
	g_clear_error (&error);

	/* Not a font */
	junk = g_bytes_new_static ("this is certainly not a font file at all", 40);
	font = lrg_paint_font_new_from_bytes (junk, &error);
	g_assert_null (font);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_clear_error (&error);

	/* Empty file */
	fd = g_file_open_tmp ("lrg-font-XXXXXX.ttf", &path, NULL);
	g_assert_cmpint (fd, >=, 0);
	close (fd);
	font = lrg_paint_font_new_from_file (path, &error);
	g_assert_null (font);
	g_assert_nonnull (error);
	g_unlink (path);

	/* Unknown family */
	g_clear_error (&error);
	font = lrg_paint_font_new_for_family ("No Such Family Zz9", &error);
	g_assert_null (font);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

static void
test_load_and_metrics (void)
{
	g_autoptr(LrgPaintFont) font = NULL;
	g_autoptr(GBytes)       bytes = NULL;
	g_autoptr(LrgPaintFont) from_bytes = NULL;
	gchar                  *contents = NULL;
	gsize                   len = 0;
	gfloat                  asc;
	gfloat                  desc;

	REQUIRE_FONT (font, "sans-serif");

	g_assert_nonnull (lrg_paint_font_get_path (font));
	g_assert_cmpint (lrg_paint_font_get_base_size (font), ==, LRG_PAINT_FONT_DEFAULT_BASE_SIZE);
	g_assert_cmpuint (lrg_paint_font_get_units_per_em (font), >, 0);

	g_assert_true (lrg_paint_font_has_glyph (font, 'A'));
	g_assert_true (lrg_paint_font_has_glyph (font, 'z'));
	g_assert_false (lrg_paint_font_has_glyph (font, 0x10FFFD));

	/* size is ascent + descent, as for raylib fonts */
	asc = lrg_paint_font_get_ascent (font, 20.0f);
	desc = lrg_paint_font_get_descent (font, 20.0f);
	g_assert_cmpfloat (asc, >, 0.0f);
	g_assert_cmpfloat (desc, >, 0.0f);
	g_assert_cmpfloat_with_epsilon (asc + desc, 20.0f, 1e-3);
	g_assert_cmpfloat (lrg_paint_font_get_line_height (font, 20.0f), >=, 20.0f);

	/* Advances scale linearly and are unrounded */
	g_assert_cmpfloat (lrg_paint_font_get_advance (font, 'M', 10.0f), >, 0.0f);
	g_assert_cmpfloat_with_epsilon (lrg_paint_font_get_advance (font, 'M', 30.0f),
	                                3.0f * lrg_paint_font_get_advance (font, 'M', 10.0f), 1e-3);
	g_assert_cmpfloat (lrg_paint_font_get_advance (font, 'W', 16.0f), >,
	                   lrg_paint_font_get_advance (font, 'i', 16.0f));

	/* The effect reach is the spread at the drawn size */
	g_assert_cmpfloat_with_epsilon (lrg_paint_font_get_max_effect (font, 48.0f),
	                                (gfloat) LRG_PAINT_FONT_SDF_SPREAD, 1e-3);

	/* The same face from memory measures the same */
	g_assert_true (g_file_get_contents (lrg_paint_font_get_path (font), &contents, &len, NULL));
	bytes = g_bytes_new_take (contents, len);
	from_bytes = lrg_paint_font_new_from_bytes (bytes, NULL);
	if (from_bytes != NULL)
		g_assert_cmpfloat_with_epsilon (lrg_paint_font_get_advance (from_bytes, 'Q', 17.0f),
		                                lrg_paint_font_get_advance (font, 'Q', 17.0f), 1e-4);
}

static void
test_measure (void)
{
	g_autoptr(LrgPaintFont) font = NULL;
	gfloat                  w;
	gfloat                  h;
	gfloat                  w2;
	gfloat                  expect;

	REQUIRE_FONT (font, "sans-serif");

	/* Empty text: no width, one line tall */
	lrg_paint_font_measure_text (font, "", -1, 20.0f, 0.0f, LRG_TEXT_DIRECTION_AUTO, &w, &h);
	g_assert_cmpfloat (w, ==, 0.0f);
	g_assert_cmpfloat (h, ==, 20.0f);

	/* "Hi" is the sum of advances plus kerning */
	expect = lrg_paint_font_get_advance (font, 'H', 20.0f) +
	         lrg_paint_font_get_kerning (font, 'H', 'i', 20.0f) +
	         lrg_paint_font_get_advance (font, 'i', 20.0f);
	lrg_paint_font_measure_text (font, "Hi", -1, 20.0f, 0.0f, LRG_TEXT_DIRECTION_AUTO, &w, &h);
	g_assert_cmpfloat_with_epsilon (w, expect, 1e-3);

	/* Spacing goes between glyphs, not after the last */
	lrg_paint_font_measure_text (font, "Hi", -1, 20.0f, 3.0f, LRG_TEXT_DIRECTION_AUTO, &w2, NULL);
	g_assert_cmpfloat_with_epsilon (w2, w + 3.0f, 1e-3);

	/* Lines: widest wins, height uses the shared 1.2 factor */
	lrg_paint_font_measure_text (font, "Hi\nHello", -1, 20.0f, 0.0f,
	                             LRG_TEXT_DIRECTION_AUTO, &w2, &h);
	lrg_paint_font_measure_text (font, "Hello", -1, 20.0f, 0.0f,
	                             LRG_TEXT_DIRECTION_AUTO, &w, NULL);
	g_assert_cmpfloat_with_epsilon (w2, w, 1e-3);
	g_assert_cmpfloat_with_epsilon (h, 20.0f + 24.0f, 1e-3);

	/* A byte length limits the text */
	lrg_paint_font_measure_text (font, "Hello", 2, 20.0f, 0.0f, LRG_TEXT_DIRECTION_AUTO, &w2, NULL);
	lrg_paint_font_measure_text (font, "He", -1, 20.0f, 0.0f, LRG_TEXT_DIRECTION_AUTO, &w, NULL);
	g_assert_cmpfloat_with_epsilon (w2, w, 1e-3);

	/* Invisible controls take no room */
	lrg_paint_font_measure_text (font, "H\xe2\x80\x8bi", -1, 20.0f, 0.0f,
	                             LRG_TEXT_DIRECTION_AUTO, &w2, NULL);
	g_assert_cmpfloat_with_epsilon (w2, expect, 1e-3);
}

static void
test_measure_matches_paint_list (void)
{
	g_autoptr(LrgPaintFont) font = NULL;
	gfloat                  w1;
	gfloat                  h1;
	gfloat                  w2;
	gfloat                  h2;

	REQUIRE_FONT (font, "sans-serif");

	/* With a default SDF font, NULL-font measuring goes through it */
	lrg_paint_font_set_default (font);
	g_assert_true (lrg_paint_font_get_default () == font);
	g_assert_true (lrg_paint_font_lookup (NULL) == font);
	lrg_paint_measure_text (NULL, "Kerning AV To\nline two", 18.0f, 1.0f, &w1, &h1);
	lrg_paint_font_measure_text (font, "Kerning AV To\nline two", -1, 18.0f, 1.0f,
	                             LRG_TEXT_DIRECTION_AUTO, &w2, &h2);
	lrg_paint_font_set_default (NULL);

	g_assert_cmpfloat (w1, ==, w2);
	g_assert_cmpfloat (h1, ==, h2);
	g_assert_null (lrg_paint_font_get_default ());
}

static void
test_kerning (void)
{
	g_autoptr(LrgPaintFont) font = NULL;
	const gchar * const     pairs[] = { "AV", "To", "VA", "Ty", "LT", "Yo", NULL };
	gfloat                  k = 0.0f;
	guint                   i;
	gfloat                  w;

	REQUIRE_FONT (font, "sans-serif");

	for (i = 0; pairs[i] != NULL && k == 0.0f; i++)
	{
		k = lrg_paint_font_get_kerning (font, (gunichar) pairs[i][0], (gunichar) pairs[i][1], 40.0f);
		if (k != 0.0f)
			break;
	}
	if (k == 0.0f)
	{
		g_test_skip ("the system font has no kerning for common pairs");
		return;
	}

	/* Tightening pairs are negative and in the measured width */
	g_assert_cmpfloat (k, <, 0.0f);
	lrg_paint_font_measure_text (font, pairs[i], -1, 40.0f, 0.0f, LRG_TEXT_DIRECTION_LTR, &w, NULL);
	g_assert_cmpfloat_with_epsilon (w,
	                                lrg_paint_font_get_advance (font, (gunichar) pairs[i][0], 40.0f) +
	                                lrg_paint_font_get_advance (font, (gunichar) pairs[i][1], 40.0f) + k,
	                                1e-3);

	/* Unkerned pairs and pairs across faces are 0 */
	g_assert_cmpfloat (lrg_paint_font_get_kerning (font, 'l', 'l', 40.0f), ==, 0.0f);
}

/* ==========================================================================
 * Headless: fallback chains
 * ========================================================================== */

static void
test_fallback_selection (void)
{
	g_autoptr(LrgPaintFont) primary = NULL;
	g_autoptr(LrgPaintFont) emoji = NULL;
	g_autoptr(LrgPaintFont) symbols = NULL;
	gunichar                smile = 0x1F600;
	gfloat                  w;

	REQUIRE_FONT (primary, "sans-serif");
	REQUIRE_FONT (emoji, "Noto Emoji");

	if (lrg_paint_font_has_glyph (primary, smile))
	{
		g_test_skip ("the system font already has emoji");
		return;
	}
	g_assert_true (lrg_paint_font_has_glyph (emoji, smile));

	/* Without fallbacks: nothing draws the emoji, it takes a box's room */
	g_assert_null (lrg_paint_font_resolve (primary, smile));
	g_assert_true (lrg_paint_font_resolve (primary, 'A') == primary);
	lrg_paint_font_measure_text (primary, "\xf0\x9f\x98\x80", -1, 20.0f, 0.0f,
	                             LRG_TEXT_DIRECTION_AUTO, &w, NULL);
	g_assert_cmpfloat (w, >, 0.0f);

	/* With the emoji font: it wins for the emoji only */
	lrg_paint_font_add_fallback (primary, emoji);
	lrg_paint_font_add_fallback (primary, emoji);   /* duplicate ignored */
	lrg_paint_font_add_fallback (primary, primary); /* self ignored */
	g_assert_cmpuint (lrg_paint_font_get_fallbacks (primary)->len, ==, 1);
	g_assert_true (lrg_paint_font_resolve (primary, smile) == emoji);
	g_assert_true (lrg_paint_font_resolve (primary, 'A') == primary);
	g_assert_null (lrg_paint_font_resolve (primary, 0x10FFFD));
	/* the fallback's own advance, scaled to the primary's em */
	g_assert_cmpfloat (lrg_paint_font_get_advance (primary, smile, 20.0f), >, 0.0f);
	g_assert_cmpfloat (lrg_paint_font_get_advance (primary, smile, 20.0f), !=, w);

	/* Order matters: the first font having the glyph is used */
	symbols = open_family ("DejaVu Sans");
	if (symbols != NULL && lrg_paint_font_has_glyph (symbols, 0x2605) &&
	    lrg_paint_font_has_glyph (emoji, 0x2605) && !lrg_paint_font_has_glyph (primary, 0x2605))
	{
		lrg_paint_font_clear_fallbacks (primary);
		lrg_paint_font_add_fallback (primary, symbols);
		lrg_paint_font_add_fallback (primary, emoji);
		g_assert_true (lrg_paint_font_resolve (primary, 0x2605) == symbols);
	}

	lrg_paint_font_clear_fallbacks (primary);
	g_assert_cmpuint (lrg_paint_font_get_fallbacks (primary)->len, ==, 0);
	g_assert_null (lrg_paint_font_resolve (primary, smile));
}

static void
test_system_fallbacks (void)
{
	LrgPaintFont           *sys;
	g_autoptr(LrgPaintFont) font = NULL;
	guint                   added;
	g_autofree gchar       *colour = NULL;

	sys = lrg_paint_font_get_system_default ();
	if (sys == NULL)
	{
		g_test_skip ("no system font");
		return;
	}
	g_assert_true (sys == lrg_paint_font_get_system_default ());

	REQUIRE_FONT (font, "sans-serif");
	added = lrg_paint_font_add_system_fallbacks (font);
	g_assert_cmpuint (added, ==, lrg_paint_font_get_fallbacks (font)->len);
	/* adding again finds the same shared faces: nothing new */
	g_assert_cmpuint (lrg_paint_font_add_system_fallbacks (font), ==, 0);

	/* Colour emoji fonts are never picked */
	colour = lrg_paint_font_find_file ("Noto Color Emoji", NULL);
	if (colour != NULL)
		g_assert_null (strstr (colour, "Color"));
}

/* ==========================================================================
 * Headless: bidi
 * ========================================================================== */

static void
test_bidi_ltr_untouched (void)
{
	g_autofree gchar *out = NULL;
	LrgTextDirection  dir = LRG_TEXT_DIRECTION_RTL;

	/* Pure left-to-right text is returned as is, with or without fribidi */
	out = lrg_paint_text_reorder ("Hello, world (1 + 2)", -1, LRG_TEXT_DIRECTION_AUTO, &dir);
	g_assert_cmpstr (out, ==, "Hello, world (1 + 2)");
	g_assert_cmpint (dir, ==, LRG_TEXT_DIRECTION_LTR);

	g_clear_pointer (&out, g_free);
	out = lrg_paint_text_reorder ("abc", 2, LRG_TEXT_DIRECTION_LTR, NULL);
	g_assert_cmpstr (out, ==, "ab");
}

static void
test_bidi_without_fribidi (void)
{
	g_autofree gchar *out = NULL;
	LrgTextDirection  dir = LRG_TEXT_DIRECTION_LTR;

	if (lrg_paint_text_has_bidi ())
	{
		g_test_skip ("built with fribidi");
		return;
	}
	/* Logical order is kept; the direction is still resolved */
	out = lrg_paint_text_reorder (HE_ALEF HE_BET " abc", -1, LRG_TEXT_DIRECTION_AUTO, &dir);
	g_assert_cmpstr (out, ==, HE_ALEF HE_BET " abc");
	g_assert_cmpint (dir, ==, LRG_TEXT_DIRECTION_RTL);
}

static void
test_bidi_reorder (void)
{
	g_autofree gchar *out = NULL;
	LrgTextDirection  dir = LRG_TEXT_DIRECTION_AUTO;

	if (!lrg_paint_text_has_bidi ())
	{
		g_test_skip ("built without fribidi");
		return;
	}

	/* LTR paragraph with a Hebrew run: only the run is reversed */
	out = lrg_paint_text_reorder ("abc " HE_ALEF HE_BET HE_GIMEL, -1, LRG_TEXT_DIRECTION_AUTO, &dir);
	g_assert_cmpstr (out, ==, "abc " HE_GIMEL HE_BET HE_ALEF);
	g_assert_cmpint (dir, ==, LRG_TEXT_DIRECTION_LTR);
	g_clear_pointer (&out, g_free);

	/* RTL paragraph (first strong is Hebrew): runs swap sides */
	out = lrg_paint_text_reorder (HE_ALEF HE_BET HE_GIMEL " abc", -1, LRG_TEXT_DIRECTION_AUTO, &dir);
	g_assert_cmpstr (out, ==, "abc " HE_GIMEL HE_BET HE_ALEF);
	g_assert_cmpint (dir, ==, LRG_TEXT_DIRECTION_RTL);
	g_clear_pointer (&out, g_free);

	/* Forced RTL moves trailing neutrals of Latin text to the left */
	out = lrg_paint_text_reorder ("abc!", -1, LRG_TEXT_DIRECTION_RTL, &dir);
	g_assert_cmpstr (out, ==, "!abc");
	g_assert_cmpint (dir, ==, LRG_TEXT_DIRECTION_RTL);
	g_clear_pointer (&out, g_free);

	/* Mirroring: a parenthesis inside RTL text flips */
	out = lrg_paint_text_reorder (HE_ALEF "(" HE_BET ")", -1, LRG_TEXT_DIRECTION_RTL, NULL);
	g_assert_cmpstr (out, ==, "(" HE_BET ")" HE_ALEF);
}

static void
test_bidi_arabic_shaping (void)
{
	g_autofree gchar    *out = NULL;
	g_autofree gunichar *cps = NULL;
	glong                n = 0;
	glong                i;

	if (!lrg_paint_text_has_bidi ())
	{
		g_test_skip ("built without fribidi");
		return;
	}

	/* "salam": joined into presentation forms, lam-alef ligated, and
	 * reversed, so the meem comes first */
	out = lrg_paint_text_reorder (AR_SEEN AR_LAM AR_ALEF AR_MEEM, -1, LRG_TEXT_DIRECTION_AUTO, NULL);
	cps = g_utf8_to_ucs4_fast (out, -1, &n);
	g_assert_cmpint (n, ==, 4);
	g_assert_cmphex (cps[0], ==, 0xFEE1);   /* meem, isolated: alef does not join forward */
	for (i = 0; i < n; i++)
		g_assert_true (cps[i] >= 0xFE70 && cps[i] <= 0xFEFF);
}

/* ==========================================================================
 * Headless: recording and the atlas
 * ========================================================================== */

static void
test_record_headless (void)
{
	g_autoptr(LrgPaintFont) font = NULL;
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	LrgPaintTextStyle       style;
	const LrgPaintInstance *inst;
	guint                   before;
	guint                   n;
	guint                   mark;
	gfloat                  x0;
	gfloat                  scale;

	REQUIRE_FONT (font, "sans-serif");

	/* One glyph instance per inked glyph; the space draws nothing */
	before = lrg_paint_font_get_n_glyphs (font);
	lrg_paint_text_style_init (&style, &C_WHITE);
	lrg_paint_font_draw_text (font, list, "Hi there", -1, 10.0f, 20.0f, 24.0f, &style);
	n = lrg_paint_list_get_n_instances (list);
	g_assert_cmpuint (n, ==, 7);
	g_assert_cmpuint (count_prim (list, LRG_PAINT_PRIM_GLYPH), ==, 7);
	g_assert_cmpuint (lrg_paint_font_get_n_glyphs (font), >=, before + 5);
	g_assert_cmpuint (lrg_paint_list_get_n_commands (list), ==, 1);

	/* Headless instances carry no texture (with a window, the atlas
	 * page); glyphs advance left to right around the baseline */
	inst = lrg_paint_list_get_instance (list, 0);
	if (IsWindowReady ())
		g_assert_cmpuint (inst->texture, !=, 0);
	else
		g_assert_cmpuint (inst->texture, ==, 0);
	g_assert_cmpfloat (inst->rect[0], <, 10.0f);
	g_assert_cmpfloat (inst->rect[1], >, 20.0f);
	g_assert_cmpfloat (inst->rect[1], <, 20.0f + lrg_paint_font_get_ascent (font, 24.0f));
	g_assert_cmpfloat (inst->rect[3], >, 20.0f + lrg_paint_font_get_ascent (font, 24.0f));
	g_assert_cmpfloat (inst->data[0], >, 0.0f);
	g_assert_cmpfloat (inst->extra[2], >, inst->extra[0]);
	x0 = inst->rect[0];
	inst = lrg_paint_list_get_instance (list, 1);
	g_assert_cmpfloat (inst->rect[0], >, x0);
	g_assert_cmpuint (inst->color[0], ==, 255);

	/* Outline, glow and shadow land in the instance stream */
	lrg_paint_list_clear (list);
	style.outline_width = 2.0f;
	style.outline_color = C_RED;
	style.glow_radius = 3.0f;
	style.glow_color = C_GREEN;
	style.shadow_color = C_BLUE;
	style.shadow_offset_x = 2.0f;
	style.shadow_offset_y = 3.0f;
	style.shadow_softness = 1.5f;
	lrg_paint_font_draw_text (font, list, "Ab", -1, 0.0f, 0.0f, 24.0f, &style);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 4);

	/* Shadows first, offset, soft, dilated by the outline */
	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpuint (inst->color[2], ==, 255);
	g_assert_cmpfloat (inst->data[1], ==, 1.5f);
	g_assert_cmpfloat (inst->data[2], ==, 2.0f);
	g_assert_cmpfloat_with_epsilon (inst->rect[0] - lrg_paint_list_get_instance (list, 2)->rect[0],
	                                2.0f, 1e-4);
	/* Then the glyphs with outline and glow */
	inst = lrg_paint_list_get_instance (list, 2);
	g_assert_cmpfloat (inst->params[0], ==, 2.0f);
	g_assert_cmpfloat (inst->params[1], ==, 3.0f);
	g_assert_cmpuint (inst->border[0], ==, 255);
	g_assert_cmpuint (inst->color2[1], ==, 255);

	/* Group transforms scale the distance range with the glyph */
	lrg_paint_list_clear (list);
	lrg_paint_text_style_init (&style, &C_WHITE);
	style.outline_width = 1.0f;
	style.outline_color = C_RED;
	mark = lrg_paint_list_begin_group (list);
	lrg_paint_font_draw_text (font, list, "W", -1, 0.0f, 0.0f, 24.0f, &style);
	inst = lrg_paint_list_get_instance (list, 0);
	scale = inst->data[0];
	lrg_paint_list_transform_group (list, mark, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f);
	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpfloat_with_epsilon (inst->data[0], scale * 3.0f, 1e-4);
	g_assert_cmpfloat_with_epsilon (inst->params[0], 3.0f, 1e-4);
	g_assert_cmpuint (inst->color[3], ==, 128);

	/* Clipped away text records nothing; transparent text neither */
	lrg_paint_list_clear (list);
	lrg_paint_list_push_clip (list, 500.0f, 500.0f, 10.0f, 10.0f);
	lrg_paint_font_draw_text (font, list, "Hidden", -1, 0.0f, 0.0f, 24.0f, NULL);
	lrg_paint_list_pop_clip (list);
	lrg_paint_text_style_init (&style, NULL);
	style.color.a = 0;
	lrg_paint_font_draw_text (font, list, "Clear", -1, 0.0f, 0.0f, 24.0f, &style);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 0);
}

static void
test_record_tofu (void)
{
	g_autoptr(LrgPaintFont) font = NULL;
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();

	REQUIRE_FONT (font, "sans-serif");

	/* A codepoint no font has becomes an empty box (a border rect) */
	lrg_paint_font_draw_text (font, list, "A\xf4\x8f\xbf\xbd", -1, 0.0f, 0.0f, 20.0f, NULL);
	g_assert_cmpuint (count_prim (list, LRG_PAINT_PRIM_GLYPH), ==, 1);
	g_assert_cmpuint (count_prim (list, LRG_PAINT_PRIM_RECT), ==, 1);
}

static void
test_atlas_growth (void)
{
	g_autoptr(LrgPaintFont) font = NULL;
	GString                *text;
	gunichar                cp;
	guint                   made;
	gint                    w = 0;
	gint                    h = 0;
	guint                   pages;
	guint                   total;

	REQUIRE_FONT (font, "sans-serif");

	/* A large base size makes cells big, so the first page fills fast */
	g_clear_object (&font);
	{
		g_autofree gchar *path = lrg_paint_font_find_file ("sans-serif", NULL);

		font = lrg_paint_font_new_from_file_index (path, 0, 96, NULL);
	}
	g_assert_nonnull (font);
	g_assert_cmpint (lrg_paint_font_get_base_size (font), ==, 96);

	/* Glyphs are added only when asked for */
	total = lrg_paint_font_atlas_get_n_glyphs ();
	g_assert_cmpuint (lrg_paint_font_get_n_glyphs (font), ==, 0);
	g_assert_cmpuint (lrg_paint_font_prepare (font, "abc"), ==, 3);
	g_assert_cmpuint (lrg_paint_font_prepare (font, "abc"), ==, 0);
	g_assert_cmpuint (lrg_paint_font_atlas_get_n_glyphs (), ==, total + 3);
	g_assert_cmpuint (lrg_paint_font_atlas_get_n_pages (), >=, 1);

	/* ~130 cells of ~120 px cannot fit 512x512: the page grows */
	text = g_string_new (NULL);
	for (cp = 0x21; cp < 0x7F; cp++)
		g_string_append_unichar (text, cp);
	for (cp = 0xC0; cp < 0x100; cp++)
		g_string_append_unichar (text, cp);
	made = lrg_paint_font_prepare (font, text->str);
	g_string_free (text, TRUE);
	g_assert_cmpuint (made, >, 100);

	pages = lrg_paint_font_atlas_get_n_pages ();
	g_assert_true (lrg_paint_font_atlas_get_page_size (pages - 1, &w, &h));
	g_assert_true (pages > 1 || w > 512);
	g_assert_cmpint (w, <=, 2048);
	g_assert_false (lrg_paint_font_atlas_get_page_size (pages, NULL, NULL));
}

static void
test_theme_settings (void)
{
	g_autoptr(LrgTheme) theme = lrg_theme_new ();
	g_autoptr(LrgTheme) copy = NULL;
	g_autoptr(LrgTheme) loaded = lrg_theme_new ();
	g_autoptr(GError)   error = NULL;
	g_autofree gchar   *data = NULL;
	g_autofree gchar   *dejavu = NULL;
	const gchar * const fallbacks[] = { "Noto Emoji", "/opt/fonts/x.ttf", NULL };
	const gchar        *yaml =
		"font-family: DejaVu Sans\n"
		"font-fallbacks: [ Noto Emoji, extra/Mine.ttf ]\n"
		"font-size: 17\n"
		"sdf-text: false\n";

	/* Defaults: SDF on, no family or fallbacks */
	g_assert_true (lrg_theme_get_sdf_text (theme));
	g_assert_null (lrg_theme_get_font_family (theme));
	g_assert_null (lrg_theme_get_font_fallbacks (theme));

	lrg_theme_set_font_family (theme, "Noto Sans");
	lrg_theme_set_font_fallbacks (theme, fallbacks);
	g_assert_cmpstr (lrg_theme_get_font_family (theme), ==, "Noto Sans");
	g_assert_cmpstr (lrg_theme_get_font_fallbacks (theme)[1], ==, "/opt/fonts/x.ttf");

	/* Copies keep the settings; YAML round-trips them */
	copy = lrg_theme_copy (theme);
	g_assert_cmpstr (lrg_theme_get_font_family (copy), ==, "Noto Sans");
	data = lrg_theme_to_data (theme, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (data, "font-family"));
	g_assert_nonnull (strstr (data, "font-fallbacks"));

	/* Loading: relative fallback files resolve, font-size feeds the
	 * metric, sdf-text switches the SDF font off */
	g_assert_true (lrg_theme_load_from_data (loaded, yaml, -1, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_theme_get_font_family (loaded), ==, "DejaVu Sans");
	g_assert_cmpstr (lrg_theme_get_font_fallbacks (loaded)[0], ==, "Noto Emoji");
	g_assert_cmpfloat (lrg_theme_get_metric (loaded, LRG_THEME_METRIC_FONT_SIZE), ==, 17.0f);
	g_assert_false (lrg_theme_get_sdf_text (loaded));
	g_assert_null (lrg_theme_get_paint_font (loaded));

	/* Bad values are errors */
	g_assert_false (lrg_theme_load_from_data (loaded, "font-fallbacks: 3\n", -1, &error));
	g_assert_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID);
	g_clear_error (&error);
	g_assert_false (lrg_theme_load_from_data (loaded, "sdf-text: maybe\n", -1, &error));
	g_assert_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID);
	g_clear_error (&error);

	/* SDF font built from the settings (headless is fine) */
	lrg_theme_set_sdf_text (loaded, TRUE);
	dejavu = lrg_paint_font_find_file ("DejaVu Sans", NULL);
	if (dejavu != NULL)
	{
		LrgPaintFont *pf = lrg_theme_get_paint_font (loaded);

		g_assert_nonnull (pf);
		g_assert_nonnull (strstr (lrg_paint_font_get_path (pf), "DejaVu"));
		g_assert_true (lrg_theme_get_paint_font (loaded) == pf);
	}
	lrg_theme_set_paint_font (loaded, NULL);
}

/* ==========================================================================
 * GPU rendering (hidden window)
 * ========================================================================== */

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') &&
	    (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-paint-text-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

#define SKIP_WITHOUT_GL() \
	do { \
		if (!graphics_available) \
		{ \
			g_test_skip ("Graphics context not available"); \
			return; \
		} \
	} while (0)

/* render_scaled: draw @list over black, scaled by @scale with the
 * model-view matrix (as a zoomed canvas would), and read it back */
static Image
render_scaled (LrgPaintList *list,
               gfloat        scale)
{
	RenderTexture2D target = LoadRenderTexture (TARGET_W, TARGET_H);
	Image           image;

	BeginTextureMode (target);
	ClearBackground ((Color){ 0, 0, 0, 255 });
	rlPushMatrix ();
	rlScalef (scale, scale, 1.0f);
	g_assert_true (lrg_paint_renderer_render (lrg_paint_renderer_get_default (), list));
	rlPopMatrix ();
	EndTextureMode ();

	image = LoadImageFromTexture (target.texture);
	ImageFlipVertical (&image);
	UnloadRenderTexture (target);
	return image;
}

static Color
pixel (Image *image,
       gint   x,
       gint   y)
{
	return GetImageColor (*image, CLAMP (x, 0, image->width - 1), CLAMP (y, 0, image->height - 1));
}

/* ink_bounds: box of pixels with red above @threshold; FALSE if none */
static gboolean
ink_bounds (Image *img,
            guint8 threshold,
            gint  *x0,
            gint  *y0,
            gint  *x1,
            gint  *y1)
{
	gint x;
	gint y;

	*x0 = *y0 = G_MAXINT;
	*x1 = *y1 = -1;
	for (y = 0; y < img->height; y++)
		for (x = 0; x < img->width; x++)
			if (pixel (img, x, y).r > threshold)
			{
				*x0 = MIN (*x0, x);
				*y0 = MIN (*y0, y);
				*x1 = MAX (*x1, x);
				*y1 = MAX (*y1, y);
			}
	return *x1 >= 0;
}

/*
 * edge_ramp:
 *
 * Along row @y, counts the partially covered pixels (between 10% and
 * 90%) on the left edge of the first stem: a crisp edge has one or two.
 */
static gint
edge_ramp (Image *img,
           gint   y)
{
	gint x;
	gint ramp = 0;

	for (x = 0; x < img->width; x++)
	{
		guint8 r = pixel (img, x, y).r;

		if (r >= 230)
			break;
		if (r > 25)
			ramp++;
	}
	return ramp;
}

static void
test_gpu_crisp_at_scales (void)
{
	g_autoptr(LrgPaintFont) font = NULL;
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintTextStyle       style;
	Image                   img1;
	Image                   img3;
	gint                    x0;
	gint                    y0;
	gint                    x1;
	gint                    y1;
	gint                    ramp1;
	gint                    ramp3;

	SKIP_WITHOUT_GL ();
	REQUIRE_FONT (font, "sans-serif");

	/* An "l": one vertical stem whose left edge is a clean test */
	list = lrg_paint_list_new ();
	lrg_paint_text_style_init (&style, &C_WHITE);
	lrg_paint_font_draw_text (font, list, "l", -1, 8.0f, 4.0f, 32.0f, &style);
	g_assert_cmpuint (lrg_paint_list_get_instance (list, 0)->texture, !=, 0);

	img1 = render_scaled (list, 1.0f);
	img3 = render_scaled (list, 3.0f);

	/* 1x: the stem is solid in the middle, edges 1-2 px wide */
	g_assert_true (ink_bounds (&img1, 128, &x0, &y0, &x1, &y1));
	g_assert_cmpint (y1 - y0, >, 14);
	ramp1 = edge_ramp (&img1, (y0 + y1) / 2);
	g_assert_cmpint (ramp1, <=, 2);
	g_assert_cmpuint (pixel (&img1, (x0 + x1) / 2, (y0 + y1) / 2).r, >=, 230);

	/* 3x: three times taller, the edge ramp stays as narrow: no blur
	 * from magnifying the atlas */
	g_assert_true (ink_bounds (&img3, 128, &x0, &y0, &x1, &y1));
	g_assert_cmpint (y1 - y0, >, 42);
	ramp3 = edge_ramp (&img3, (y0 + y1) / 2);
	g_assert_cmpint (ramp3, <=, 2);
	g_assert_cmpint (ABS (ramp3 - ramp1), <=, 1);

	UnloadImage (img1);
	UnloadImage (img3);
}

static void
test_gpu_outline_and_glow (void)
{
	g_autoptr(LrgPaintFont) font = NULL;
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintTextStyle       style;
	Image                   img;
	gint                    y;
	gint                    x;
	gint                    first_white = -1;
	gboolean                red_before = FALSE;
	gboolean                green_before = FALSE;
	gfloat                  green_near = 0.0f;
	gfloat                  green_far = 0.0f;

	SKIP_WITHOUT_GL ();
	REQUIRE_FONT (font, "sans-serif");

	/* White "l", red 2 px outline, green 4 px glow, drawn large */
	list = lrg_paint_list_new ();
	lrg_paint_text_style_init (&style, &C_WHITE);
	style.outline_width = 2.0f;
	style.outline_color = C_RED;
	style.glow_radius = 4.0f;
	style.glow_color = C_GREEN;
	lrg_paint_font_draw_text (font, list, "l", -1, 30.0f, 10.0f, 64.0f, &style);
	img = render_scaled (list, 1.0f);

	/* Scan the middle row from the left: green glow, then red rim, then
	 * the white core */
	y = 10 + 30;
	for (x = 0; x < TARGET_W; x++)
	{
		Color c = pixel (&img, x, y);

		if (c.r > 230 && c.g > 230 && c.b > 230)
		{
			first_white = x;
			break;
		}
		if (c.r > 200 && c.g < 60)
			red_before = TRUE;
		if (c.g > 30 && c.r < 60)
			green_before = TRUE;
	}
	g_assert_cmpint (first_white, >, 0);
	g_assert_true (red_before);
	g_assert_true (green_before);

	/* The glow fades with distance */
	for (x = first_white - 1; x > 0; x--)
		if (pixel (&img, x, y).r < 60 && pixel (&img, x, y).g > 0)
			break;
	green_near = (gfloat) pixel (&img, x, y).g;
	green_far = (gfloat) pixel (&img, x - 3, y).g;
	g_assert_cmpfloat (green_near, >, green_far);
	g_assert_cmpuint (pixel (&img, first_white - 20, y).g, ==, 0);

	UnloadImage (img);
}

static void
test_gpu_fallback_glyph (void)
{
	g_autoptr(LrgPaintFont) primary = NULL;
	g_autoptr(LrgPaintFont) emoji = NULL;
	g_autoptr(LrgPaintList) list = NULL;
	Image                   img;
	gint                    x0;
	gint                    y0;
	gint                    x1;
	gint                    y1;

	SKIP_WITHOUT_GL ();
	REQUIRE_FONT (primary, "sans-serif");
	REQUIRE_FONT (emoji, "Noto Emoji");
	if (lrg_paint_font_has_glyph (primary, 0x1F600))
	{
		g_test_skip ("the system font already has emoji");
		return;
	}

	/* The emoji comes from the fallback, batched with the Latin text */
	lrg_paint_font_add_fallback (primary, emoji);
	list = lrg_paint_list_new ();
	lrg_paint_font_draw_text (primary, list, "\xf0\x9f\x98\x80", -1, 100.0f, 30.0f, 48.0f, NULL);
	g_assert_cmpuint (count_prim (list, LRG_PAINT_PRIM_GLYPH), ==, 1);
	g_assert_cmpuint (count_prim (list, LRG_PAINT_PRIM_RECT), ==, 0);
	g_assert_cmpuint (lrg_paint_font_get_n_glyphs (emoji), >=, 1);

	img = render_scaled (list, 1.0f);
	g_assert_true (ink_bounds (&img, 128, &x0, &y0, &x1, &y1));
	g_assert_cmpint (x1 - x0, >, 20);
	g_assert_cmpint (y1 - y0, >, 20);
	g_assert_cmpint (x0, >=, 98);
	UnloadImage (img);
}

/* image_diff: summed absolute red difference */
static gdouble
image_diff (Image *a,
            Image *b)
{
	gdouble sum = 0.0;
	gint    x;
	gint    y;

	for (y = 0; y < a->height; y++)
		for (x = 0; x < a->width; x++)
			sum += ABS ((gint) pixel (a, x, y).r - (gint) pixel (b, x, y).r);
	return sum;
}

static void
test_gpu_rtl_order (void)
{
	g_autoptr(LrgPaintFont) font = NULL;
	g_autoptr(LrgPaintList) pair = NULL;
	g_autoptr(LrgPaintList) right = NULL;
	g_autoptr(LrgPaintList) wrong = NULL;
	Image                   img_pair;
	Image                   img_right;
	Image                   img_wrong;
	gfloat                  a_alef;
	gfloat                  a_bet;
	gdouble                 d_right;
	gdouble                 d_wrong;

	SKIP_WITHOUT_GL ();
	if (!lrg_paint_text_has_bidi ())
	{
		g_test_skip ("built without fribidi");
		return;
	}
	REQUIRE_FONT (font, "DejaVu Sans");

	/* Logical alef-bet draws as bet then alef from the left */
	pair = lrg_paint_list_new ();
	lrg_paint_font_draw_text (font, pair, HE_ALEF HE_BET, -1, 40.0f, 20.0f, 64.0f, NULL);

	a_alef = lrg_paint_font_get_advance (font, 0x05D0, 64.0f);
	a_bet = lrg_paint_font_get_advance (font, 0x05D1, 64.0f);
	right = lrg_paint_list_new ();
	lrg_paint_font_draw_text (font, right, HE_BET, -1, 40.0f, 20.0f, 64.0f, NULL);
	lrg_paint_font_draw_text (font, right, HE_ALEF, -1, 40.0f + a_bet, 20.0f, 64.0f, NULL);
	wrong = lrg_paint_list_new ();
	lrg_paint_font_draw_text (font, wrong, HE_ALEF, -1, 40.0f, 20.0f, 64.0f, NULL);
	lrg_paint_font_draw_text (font, wrong, HE_BET, -1, 40.0f + a_alef, 20.0f, 64.0f, NULL);

	img_pair = render_scaled (pair, 1.0f);
	img_right = render_scaled (right, 1.0f);
	img_wrong = render_scaled (wrong, 1.0f);
	d_right = image_diff (&img_pair, &img_right);
	d_wrong = image_diff (&img_pair, &img_wrong);
	g_assert_cmpfloat (d_right * 4.0, <, d_wrong);

	UnloadImage (img_pair);
	UnloadImage (img_right);
	UnloadImage (img_wrong);
}

static void
test_gpu_batches_with_shapes (void)
{
	g_autoptr(LrgPaintFont) font = NULL;
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintRenderer       *renderer;
	Image                   img;

	SKIP_WITHOUT_GL ();
	REQUIRE_FONT (font, "sans-serif");

	/* Shapes around SDF text: one batch, one draw call */
	list = lrg_paint_list_new ();
	renderer = lrg_paint_renderer_get_default ();
	lrg_paint_list_rect (list, 0.0f, 0.0f, 256.0f, 20.0f, 4.0f, &C_BLUE);
	lrg_paint_font_draw_text (font, list, "Batch me", -1, 4.0f, 30.0f, 24.0f, NULL);
	lrg_paint_list_rect (list, 0.0f, 100.0f, 256.0f, 28.0f, 0.0f, &C_RED);
	g_assert_cmpuint (lrg_paint_list_get_n_commands (list), ==, 1);

	img = render_scaled (list, 1.0f);
	g_assert_cmpuint (lrg_paint_renderer_get_draw_calls (renderer), ==, 1);
	UnloadImage (img);
}

static void
test_gpu_bind_and_theme (void)
{
	g_autoptr(LrgPaintFont) font = NULL;
	g_autoptr(LrgPaintList) list = NULL;
	g_autoptr(GrlFont)      grl = NULL;
	g_autoptr(LrgTheme)     theme = NULL;
	GrlFont                *ui;
	gfloat                  w1;
	gfloat                  w2;

	SKIP_WITHOUT_GL ();
	REQUIRE_FONT (font, "sans-serif");

	/* A bound GrlFont draws and measures through the SDF font */
	grl = grl_font_new_default ();
	g_assert_nonnull (grl);
	g_assert_null (lrg_paint_font_lookup (grl));
	lrg_paint_font_bind (font, grl);
	g_assert_true (lrg_paint_font_lookup (grl) == font);

	list = lrg_paint_list_new ();
	lrg_paint_list_text (list, grl, "Bound", 0.0f, 0.0f, 20.0f, 1.0f, &C_WHITE);
	g_assert_cmpuint (count_prim (list, LRG_PAINT_PRIM_GLYPH), ==, 5);
	lrg_paint_measure_text (grl, "Bound", 20.0f, 1.0f, &w1, NULL);
	lrg_paint_font_measure_text (font, "Bound", -1, 20.0f, 1.0f, LRG_TEXT_DIRECTION_AUTO, &w2, NULL);
	g_assert_cmpfloat (w1, ==, w2);

	lrg_paint_font_bind (NULL, grl);
	g_assert_null (lrg_paint_font_lookup (grl));

	/* The theme binds its SDF font to its UI font */
	theme = lrg_theme_new ();
	ui = lrg_theme_get_ui_font (theme);
	if (ui == NULL || lrg_theme_get_paint_font (theme) == NULL)
	{
		g_test_skip ("no UI font installed");
		return;
	}
	g_assert_true (lrg_paint_font_lookup (ui) == lrg_theme_get_paint_font (theme));

	/* Turning SDF text off gives the plain bitmap font again */
	lrg_theme_set_sdf_text (theme, FALSE);
	ui = lrg_theme_get_ui_font (theme);
	g_assert_null (lrg_theme_get_paint_font (theme));
	if (ui != NULL)
		g_assert_null (lrg_paint_font_lookup (ui));
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/paint/text/style-boxed", test_style_boxed);
	g_test_add_func ("/paint/text/font/load-errors", test_load_errors);
	g_test_add_func ("/paint/text/font/load-and-metrics", test_load_and_metrics);
	g_test_add_func ("/paint/text/font/measure", test_measure);
	g_test_add_func ("/paint/text/font/measure-matches-paint-list", test_measure_matches_paint_list);
	g_test_add_func ("/paint/text/font/kerning", test_kerning);
	g_test_add_func ("/paint/text/fallback/selection", test_fallback_selection);
	g_test_add_func ("/paint/text/fallback/system", test_system_fallbacks);
	g_test_add_func ("/paint/text/bidi/ltr-untouched", test_bidi_ltr_untouched);
	g_test_add_func ("/paint/text/bidi/without-fribidi", test_bidi_without_fribidi);
	g_test_add_func ("/paint/text/bidi/reorder", test_bidi_reorder);
	g_test_add_func ("/paint/text/bidi/arabic-shaping", test_bidi_arabic_shaping);
	g_test_add_func ("/paint/text/record/headless", test_record_headless);
	g_test_add_func ("/paint/text/record/tofu", test_record_tofu);
	g_test_add_func ("/paint/text/atlas/growth", test_atlas_growth);
	g_test_add_func ("/paint/text/theme/settings", test_theme_settings);

	if (!g_test_subprocess ())
		graphics_available = init_graphics_context ();

	g_test_add_func ("/paint/text/gpu/crisp-at-scales", test_gpu_crisp_at_scales);
	g_test_add_func ("/paint/text/gpu/outline-and-glow", test_gpu_outline_and_glow);
	g_test_add_func ("/paint/text/gpu/fallback-glyph", test_gpu_fallback_glyph);
	g_test_add_func ("/paint/text/gpu/rtl-order", test_gpu_rtl_order);
	g_test_add_func ("/paint/text/gpu/batches-with-shapes", test_gpu_batches_with_shapes);
	g_test_add_func ("/paint/text/gpu/bind-and-theme", test_gpu_bind_and_theme);

	result = g_test_run ();

	g_clear_object (&test_window);
	return result;
}
