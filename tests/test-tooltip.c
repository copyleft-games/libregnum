/* test-tooltip.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTooltip (model), LrgTooltipLayout (sizing, wrapping, drawing through
 * callbacks) and the placement helpers, on a fixed-width fake font.
 */

#include <libregnum.h>
#include <gio/gio.h>
#include <string.h>

/* a fake font: 6 pixels per character, 8 high */
static void
measure_fake (const gchar *text, gfloat *width, gfloat *height, gpointer user_data)
{
    *width = 6.0f * (gfloat) g_utf8_strlen (text, -1);
    *height = 8.0f;
}

/* records what a draw does */
typedef struct
{
    GString *log;
    guint    fills;
    guint    texts;
    guint    icons;
} Rec;

static void
rec_fill (gfloat x, gfloat y, gfloat w, gfloat h, guint32 color, gpointer ud)
{
    Rec *rec = ud;

    rec->fills++;
    g_string_append_printf (rec->log, "fill %.0f,%.0f %.0fx%.0f %08x\n", x, y, w, h, color);
}

static void
rec_text (const gchar *text, gfloat x, gfloat y, guint32 color, gpointer ud)
{
    Rec *rec = ud;

    rec->texts++;
    g_string_append_printf (rec->log, "text '%s' %.0f,%.0f %08x\n", text, x, y, color);
}

static void
rec_icon (const gchar *icon, gfloat x, gfloat y, gfloat size, gpointer ud)
{
    Rec *rec = ud;

    rec->icons++;
    g_string_append_printf (rec->log, "icon %s %.0f,%.0f %.0f\n", icon, x, y, size);
}

static void
test_model_lines (void)
{
    g_autoptr(LrgTooltip) tip = lrg_tooltip_new ();
    g_autoptr(LrgTooltip) copy = NULL;
    g_autoptr(LrgTooltipLine) spare = NULL;
    g_autofree gchar *text = NULL;
    LrgTooltipLine *line;

    g_assert_cmpuint (lrg_tooltip_get_n_lines (tip), ==, 0);
    line = lrg_tooltip_add_line (tip, "Sword of Testing", 0xFF8000FFu);
    lrg_tooltip_line_set_id (line, "name");
    lrg_tooltip_add_double_line (tip, "Two-Hand", "Sword", 0xFFFFFFFFu, 0xC0C0C0FFu);
    lrg_tooltip_add_separator (tip);
    lrg_tooltip_add_icon_line (tip, "coin", "12 gold", 0xFFD700FFu);
    lrg_tooltip_add_spacer (tip);
    lrg_tooltip_add_wrapped_line (tip, "Flavour", 0x808080FFu);
    g_assert_cmpuint (lrg_tooltip_get_n_lines (tip), ==, 6);

    text = lrg_tooltip_to_string (tip);
    g_assert_cmpstr (text, ==, "Sword of Testing\nTwo-Hand | Sword\n---\n[coin] 12 gold\n\nFlavour");
    g_assert_true (lrg_tooltip_line_get_wrap (lrg_tooltip_get_line (tip, 5)));
    g_assert_false (lrg_tooltip_line_get_wrap (lrg_tooltip_get_line (tip, 0)));
    g_assert_cmpuint (lrg_tooltip_line_get_left_color (lrg_tooltip_get_line (tip, 0)), ==, 0xFF8000FFu);
    g_assert_cmpuint (lrg_tooltip_line_get_right_color (lrg_tooltip_get_line (tip, 1)), ==, 0xC0C0C0FFu);
    g_assert_null (lrg_tooltip_get_line (tip, 6));

    /* a text line given a right side becomes a double line */
    line = lrg_tooltip_get_line (tip, 0);
    lrg_tooltip_line_set_kind (line, LRG_TOOLTIP_LINE_DOUBLE);
    lrg_tooltip_line_set_right (line, "R");
    g_assert_cmpint (lrg_tooltip_line_get_kind (line), ==, LRG_TOOLTIP_LINE_DOUBLE);
    lrg_tooltip_line_set_kind (line, LRG_TOOLTIP_LINE_TEXT);
    lrg_tooltip_line_set_right (line, NULL);

    /* find, replace, insert, remove */
    g_assert_cmpint (lrg_tooltip_find_line (tip, "name"), ==, 0);
    g_assert_cmpint (lrg_tooltip_find_line (tip, "nope"), ==, -1);
    g_assert_cmpint (lrg_tooltip_find_line (tip, NULL), ==, -1);
    g_assert_cmpint (lrg_tooltip_find_line (tip, ""), ==, -1);
    spare = lrg_tooltip_line_new (LRG_TOOLTIP_LINE_TEXT);
    lrg_tooltip_line_set_left (spare, "Renamed");
    lrg_tooltip_line_set_id (spare, "name");
    g_assert_true (lrg_tooltip_replace_line (tip, 0, spare));
    g_assert_cmpstr (lrg_tooltip_line_get_left (lrg_tooltip_get_line (tip, 0)), ==, "Renamed");
    g_assert_false (lrg_tooltip_replace_line (tip, 99, spare));
    g_assert_nonnull (lrg_tooltip_insert_line (tip, 1, spare));
    g_assert_nonnull (lrg_tooltip_insert_line (tip, 500, spare));
    g_assert_cmpuint (lrg_tooltip_get_n_lines (tip), ==, 8);
    g_assert_cmpuint (lrg_tooltip_remove_by_id (tip, "name"), ==, 3);
    g_assert_cmpuint (lrg_tooltip_get_n_lines (tip), ==, 5);
    g_assert_cmpuint (lrg_tooltip_remove_by_id (tip, "name"), ==, 0);
    g_assert_true (lrg_tooltip_remove_line (tip, 0));
    g_assert_false (lrg_tooltip_remove_line (tip, 40));

    /* a copy is deep */
    copy = lrg_tooltip_copy (tip);
    lrg_tooltip_line_set_left (lrg_tooltip_get_line (copy, 0), "changed");
    g_assert_cmpstr (lrg_tooltip_line_get_left (lrg_tooltip_get_line (tip, 0)), !=, "changed");
    lrg_tooltip_clear (copy);
    g_assert_cmpuint (lrg_tooltip_get_n_lines (copy), ==, 0);
    g_assert_cmpuint (lrg_tooltip_get_n_lines (tip), ==, 4);
}

static void
test_color_parse (void)
{
    guint32 c = 1234;

    g_assert_true (lrg_tooltip_color_parse ("#FF8000", &c));
    g_assert_cmpuint (c, ==, 0xFF8000FFu);
    g_assert_true (lrg_tooltip_color_parse ("#ff800080", &c));
    g_assert_cmpuint (c, ==, 0xFF800080u);
    g_assert_true (lrg_tooltip_color_parse ("00ff00", &c));
    g_assert_cmpuint (c, ==, 0x00FF00FFu);

    /* bad specs leave the output alone */
    c = 77;
    g_assert_false (lrg_tooltip_color_parse ("#GG0000", &c));
    g_assert_false (lrg_tooltip_color_parse ("#FFF", &c));
    g_assert_false (lrg_tooltip_color_parse ("", &c));
    g_assert_false (lrg_tooltip_color_parse ("#FF80001", &c));
    g_assert_false (lrg_tooltip_color_parse (NULL, &c));
    g_assert_false (lrg_tooltip_color_parse ("#FF8000", NULL));
    g_assert_cmpuint (c, ==, 77);
}

static void
test_layout_sizes_and_pairs (void)
{
    g_autoptr(LrgTooltip) tip = lrg_tooltip_new ();
    LrgTooltipStyle style;
    LrgTooltipLayout *layout;
    LrgTooltipRow row;
    g_autoptr(GError) error = NULL;

    lrg_tooltip_style_init_default (&style);
    style.min_width = 10.0f;
    lrg_tooltip_add_line (tip, "Hello", 0xFFFFFFFFu);                   /* 30 */
    lrg_tooltip_add_double_line (tip, "Two-Hand", "Sword", 0xFFFFFFFFu, 0x808080FFu); /* 48 + 12 + 30 = 90 */
    lrg_tooltip_add_separator (tip);

    layout = lrg_tooltip_layout_new (tip, &style, measure_fake, NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (layout);
    /* inset 5, widest row 90 */
    g_assert_cmpfloat (lrg_tooltip_layout_get_width (layout), ==, 100.0f);
    g_assert_cmpuint (lrg_tooltip_layout_get_n_rows (layout), ==, 3);
    /* 5 + 8 + 1 + 8 + 1 + 5 (separator) + 5 */
    g_assert_cmpfloat (lrg_tooltip_layout_get_height (layout), ==, 33.0f);

    g_assert_true (lrg_tooltip_layout_get_row (layout, 1, &row));
    g_assert_cmpint (row.kind, ==, LRG_TOOLTIP_LINE_DOUBLE);
    g_assert_cmpstr (row.left, ==, "Two-Hand");
    g_assert_cmpstr (row.right, ==, "Sword");
    g_assert_cmpfloat (row.left_x, ==, 5.0f);
    /* right text flush with the right inset: 100 - 5 - 30 */
    g_assert_cmpfloat (row.right_x, ==, 65.0f);
    g_assert_cmpfloat (row.y, ==, 14.0f);
    g_assert_false (lrg_tooltip_layout_get_row (layout, 3, &row));
    lrg_tooltip_layout_unref (layout);
}

static void
test_layout_wrap (void)
{
    g_autoptr(LrgTooltip) tip = lrg_tooltip_new ();
    LrgTooltipStyle style;
    LrgTooltipLayout *layout;
    LrgTooltipRow row;
    guint i;

    lrg_tooltip_style_init_default (&style);
    style.min_width = 10.0f;
    lrg_tooltip_add_line (tip, "Name here", 0xFFFFFFFFu);   /* 54 -> inner 54 -> width 64 */
    lrg_tooltip_add_wrapped_line (tip, "one two three four five six seven", 0x808080FFu);

    layout = lrg_tooltip_layout_new (tip, &style, measure_fake, NULL, NULL);
    g_assert_nonnull (layout);
    /* the flavour text never widens the tooltip */
    g_assert_cmpfloat (lrg_tooltip_layout_get_width (layout), ==, 64.0f);
    g_assert_cmpuint (lrg_tooltip_layout_get_n_rows (layout), >, 2);
    for (i = 1; i < lrg_tooltip_layout_get_n_rows (layout); i++)
    {
        g_assert_true (lrg_tooltip_layout_get_row (layout, i, &row));
        g_assert_cmpint (row.kind, ==, LRG_TOOLTIP_LINE_TEXT);
        g_assert_cmpuint (row.line_index, ==, 1);
        /* every piece fits the inner width of 54 */
        g_assert_cmpfloat (6.0f * (gfloat) strlen (row.left), <=, 54.0f);
    }
    lrg_tooltip_layout_unref (layout);

    /* a single word wider than the box is cut between characters, nothing lost */
    {
        g_autoptr(LrgTooltip) long_tip = lrg_tooltip_new ();
        GString *joined = g_string_new (NULL);

        lrg_tooltip_add_wrapped_line (long_tip, "Supercalifragilisticexpialidocious", 0xFFFFFFFFu);
        layout = lrg_tooltip_layout_new (long_tip, NULL, measure_fake, NULL, NULL);
        g_assert_nonnull (layout);
        g_assert_cmpuint (lrg_tooltip_layout_get_n_rows (layout), >, 1);
        for (i = 0; i < lrg_tooltip_layout_get_n_rows (layout); i++)
        {
            lrg_tooltip_layout_get_row (layout, i, &row);
            g_assert_cmpfloat (6.0f * (gfloat) strlen (row.left), <=, lrg_tooltip_layout_get_width (layout) - 10.0f);
            g_string_append (joined, row.left);
        }
        g_assert_cmpstr (joined->str, ==, "Supercalifragilisticexpialidocious");
        g_string_free (joined, TRUE);
        lrg_tooltip_layout_unref (layout);
    }

    /* explicit newlines break rows even when the text is short */
    {
        g_autoptr(LrgTooltip) nl = lrg_tooltip_new ();

        lrg_tooltip_add_line (nl, "a\nb\nc", 0xFFFFFFFFu);
        layout = lrg_tooltip_layout_new (nl, &style, measure_fake, NULL, NULL);
        g_assert_cmpuint (lrg_tooltip_layout_get_n_rows (layout), ==, 3);
        lrg_tooltip_layout_unref (layout);
    }
}

static void
test_layout_max_width_and_draw (void)
{
    g_autoptr(LrgTooltip) tip = lrg_tooltip_new ();
    LrgTooltipLayout *layout;
    LrgTooltipDrawFuncs funcs;
    Rec rec;

    /* a plain line far wider than max_width wraps instead of widening */
    lrg_tooltip_add_line (tip, "this long line of plain text must wrap at the maximum width of the tooltip", 0xFFFFFFFFu);
    lrg_tooltip_add_icon_line (tip, "coin", "9", 0xFFD700FFu);
    lrg_tooltip_add_separator (tip);
    lrg_tooltip_add_double_line (tip, "L", "R", 0x11223344u, 0x55667788u);
    layout = lrg_tooltip_layout_new (tip, NULL, measure_fake, NULL, NULL);
    g_assert_nonnull (layout);
    g_assert_cmpfloat (lrg_tooltip_layout_get_width (layout), ==, 220.0f);

    memset (&funcs, 0, sizeof funcs);
    funcs.fill_rect = rec_fill;
    funcs.draw_text = rec_text;
    funcs.draw_icon = rec_icon;
    rec.log = g_string_new (NULL);
    rec.fills = rec.texts = rec.icons = 0;
    lrg_tooltip_layout_draw (layout, 100.0f, 50.0f, &funcs, &rec);
    /* border + background + the separator rule */
    g_assert_cmpuint (rec.fills, ==, 3);
    g_assert_cmpuint (rec.icons, ==, 1);
    g_assert_nonnull (strstr (rec.log->str, "text 'L' 105,"));
    g_assert_nonnull (strstr (rec.log->str, "text 'R' 309,"));
    g_assert_nonnull (strstr (rec.log->str, "11223344"));
    g_assert_nonnull (strstr (rec.log->str, "55667788"));
    g_assert_nonnull (strstr (rec.log->str, "icon coin"));
    g_assert_cmpuint (rec.texts, >=, 4);

    /* missing callbacks are skipped */
    memset (&funcs, 0, sizeof funcs);
    g_string_truncate (rec.log, 0);
    lrg_tooltip_layout_draw (layout, 0.0f, 0.0f, &funcs, &rec);
    g_assert_cmpuint (rec.log->len, ==, 0);

    g_string_free (rec.log, TRUE);
    lrg_tooltip_layout_unref (layout);
}

static void
test_layout_failures (void)
{
    g_autoptr(LrgTooltip) tip = lrg_tooltip_new ();
    LrgTooltipStyle style;
    GError *error = NULL;

    /* empty tooltip */
    g_assert_null (lrg_tooltip_layout_new (tip, NULL, measure_fake, NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    lrg_tooltip_add_line (tip, "x", 0xFFFFFFFFu);
    /* no measure function */
    g_assert_null (lrg_tooltip_layout_new (tip, NULL, NULL, NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    /* a style that cannot hold any text */
    lrg_tooltip_style_init_default (&style);
    style.max_width = 8.0f;
    g_assert_null (lrg_tooltip_layout_new (tip, &style, measure_fake, NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    lrg_tooltip_style_init_default (&style);
    style.padding = -1.0f;
    g_assert_null (lrg_tooltip_layout_new (tip, &style, measure_fake, NULL, &error));
    g_clear_error (&error);
    lrg_tooltip_style_init_default (&style);
    style.min_width = 500.0f;
    g_assert_null (lrg_tooltip_layout_new (tip, &style, measure_fake, NULL, &error));
    g_clear_error (&error);
}

static void
test_placement (void)
{
    gfloat x;
    gfloat y;
    gfloat ox;
    gfloat oy;

    /* room: right and below the pointer */
    lrg_tooltip_place (50.0f, 30.0f, 100.0f, 100.0f, 4.0f, 320.0f, 200.0f, &x, &y);
    g_assert_cmpfloat (x, ==, 104.0f);
    g_assert_cmpfloat (y, ==, 104.0f);
    /* the right edge: flips left of the pointer */
    lrg_tooltip_place (50.0f, 30.0f, 300.0f, 100.0f, 4.0f, 320.0f, 200.0f, &x, &y);
    g_assert_cmpfloat (x, ==, 246.0f);
    /* the bottom: flips above */
    lrg_tooltip_place (50.0f, 30.0f, 100.0f, 190.0f, 4.0f, 320.0f, 200.0f, &x, &y);
    g_assert_cmpfloat (y, ==, 156.0f);
    /* a corner: both flip */
    lrg_tooltip_place (50.0f, 30.0f, 319.0f, 199.0f, 4.0f, 320.0f, 200.0f, &x, &y);
    g_assert_cmpfloat (x + 50.0f, <=, 320.0f);
    g_assert_cmpfloat (y + 30.0f, <=, 200.0f);
    /* bigger than the screen: the top left wins */
    lrg_tooltip_place (400.0f, 300.0f, 50.0f, 50.0f, 4.0f, 320.0f, 200.0f, &x, &y);
    g_assert_cmpfloat (x, ==, 0.0f);
    g_assert_cmpfloat (y, ==, 0.0f);
    /* a pointer outside the screen still lands the tooltip on it */
    lrg_tooltip_place (50.0f, 30.0f, -80.0f, -80.0f, 4.0f, 320.0f, 200.0f, &x, &y);
    g_assert_cmpfloat (x, >=, 0.0f);
    g_assert_cmpfloat (y, >=, 0.0f);
    /* outputs may be NULL */
    lrg_tooltip_place (50.0f, 30.0f, 10.0f, 10.0f, 4.0f, 320.0f, 200.0f, NULL, NULL);

    /* the pair: side by side, main first, tops level */
    g_assert_true (lrg_tooltip_place_pair (80.0f, 60.0f, 90.0f, 70.0f, 20.0f, 20.0f, 4.0f,
                                           320.0f, 200.0f, &x, &y, &ox, &oy));
    g_assert_cmpfloat (x, ==, 24.0f);
    g_assert_cmpfloat (ox, ==, 24.0f + 80.0f + 4.0f);
    g_assert_cmpfloat (y, ==, oy);
    /* no room on the right: the pair moves left of the pointer, order kept */
    g_assert_true (lrg_tooltip_place_pair (80.0f, 60.0f, 90.0f, 70.0f, 300.0f, 20.0f, 4.0f,
                                           320.0f, 200.0f, &x, &y, &ox, &oy));
    g_assert_cmpfloat (ox + 90.0f, <=, 300.0f);
    g_assert_cmpfloat (x + 80.0f + 4.0f, ==, ox);
    /* wider than the screen: stacked */
    g_assert_false (lrg_tooltip_place_pair (200.0f, 60.0f, 200.0f, 70.0f, 20.0f, 20.0f, 4.0f,
                                            320.0f, 200.0f, &x, &y, &ox, &oy));
    g_assert_cmpfloat (ox, ==, x);
    g_assert_cmpfloat (oy, ==, y + 60.0f + 4.0f);
    g_assert_cmpfloat (oy + 70.0f, <=, 200.0f);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/tooltip/model-lines", test_model_lines);
    g_test_add_func ("/tooltip/color-parse", test_color_parse);
    g_test_add_func ("/tooltip/layout/sizes-and-pairs", test_layout_sizes_and_pairs);
    g_test_add_func ("/tooltip/layout/wrap", test_layout_wrap);
    g_test_add_func ("/tooltip/layout/max-width-and-draw", test_layout_max_width_and_draw);
    g_test_add_func ("/tooltip/layout/failures", test_layout_failures);
    g_test_add_func ("/tooltip/placement", test_placement);
    return g_test_run ();
}
