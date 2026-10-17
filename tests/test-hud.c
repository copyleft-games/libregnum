/* test-hud.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the HUD module (LrgHudLayout and its file,
 * LrgFloatingText, LrgRadialMenu, LrgFeed) and LrgCameraZoom.  All pure:
 * no window or graphics context.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <math.h>
#include <string.h>

/* ==========================================================================
 * LrgHudLayout
 * ========================================================================== */

static void
count_changed (LrgHudLayout *layout,
               const gchar  *piece_id,
               gpointer      user_data)
{
    (void) layout;
    (void) piece_id;
    (*(guint *) user_data)++;
}

static void
test_hud_layout_pieces (void)
{
    g_autoptr(LrgHudLayout) layout = NULL;
    g_auto(GStrv) ids = NULL;
    LrgHudAnchor anchor;
    gfloat x;
    gfloat y;
    guint changes = 0;

    layout = lrg_hud_layout_new ();
    g_signal_connect (layout, "changed", G_CALLBACK (count_changed), &changes);

    g_assert_true (lrg_hud_layout_add_piece (layout, "tracker", "Quest tracker",
                                             LRG_HUD_ANCHOR_TOP_RIGHT, -4.0f, 26.0f));
    g_assert_true (lrg_hud_layout_add_piece (layout, "feed", NULL, LRG_HUD_ANCHOR_RIGHT, -4.0f, 0.0f));
    /* duplicates and bad ids are refused */
    g_assert_false (lrg_hud_layout_add_piece (layout, "feed", NULL, LRG_HUD_ANCHOR_LEFT, 0.0f, 0.0f));
    g_assert_false (lrg_hud_layout_add_piece (layout, "", NULL, LRG_HUD_ANCHOR_LEFT, 0.0f, 0.0f));
    g_assert_false (lrg_hud_layout_add_piece (layout, "two words", NULL, LRG_HUD_ANCHOR_LEFT, 0.0f, 0.0f));
    g_assert_false (lrg_hud_layout_add_piece (layout, "nan", NULL, LRG_HUD_ANCHOR_LEFT, NAN, 0.0f));
    g_assert_cmpuint (changes, ==, 2);

    ids = lrg_hud_layout_get_pieces (layout);
    g_assert_cmpuint (g_strv_length (ids), ==, 2);
    g_assert_cmpstr (ids[0], ==, "tracker");
    g_assert_cmpstr (ids[1], ==, "feed");
    g_assert_cmpstr (lrg_hud_layout_get_label (layout, "tracker"), ==, "Quest tracker");
    g_assert_cmpstr (lrg_hud_layout_get_label (layout, "feed"), ==, "feed");
    g_assert_null (lrg_hud_layout_get_label (layout, "nothing"));

    /* the default until the player places it */
    g_assert_true (lrg_hud_layout_get_position (layout, "tracker", &anchor, &x, &y));
    g_assert_cmpint (anchor, ==, LRG_HUD_ANCHOR_TOP_RIGHT);
    g_assert_cmpfloat (x, ==, -4.0f);
    g_assert_cmpfloat (y, ==, 26.0f);
    g_assert_false (lrg_hud_layout_is_modified (layout, "tracker"));

    /* placed, moved, back to the default by placing it there */
    g_assert_true (lrg_hud_layout_set_position (layout, "tracker", LRG_HUD_ANCHOR_LEFT, 10.0f, 5.0f));
    g_assert_true (lrg_hud_layout_is_modified (layout, "tracker"));
    g_assert_true (lrg_hud_layout_move_by (layout, "tracker", 2.0f, -1.0f));
    g_assert_true (lrg_hud_layout_get_position (layout, "tracker", &anchor, &x, &y));
    g_assert_cmpint (anchor, ==, LRG_HUD_ANCHOR_LEFT);
    g_assert_cmpfloat (x, ==, 12.0f);
    g_assert_cmpfloat (y, ==, 4.0f);
    g_assert_true (lrg_hud_layout_set_position (layout, "tracker", LRG_HUD_ANCHOR_TOP_RIGHT, -4.0f, 26.0f));
    g_assert_false (lrg_hud_layout_is_modified (layout, "tracker"));
    g_assert_false (lrg_hud_layout_set_position (layout, "nothing", LRG_HUD_ANCHOR_LEFT, 0.0f, 0.0f));
    g_assert_false (lrg_hud_layout_set_position (layout, "tracker", LRG_HUD_ANCHOR_LEFT, INFINITY, 0.0f));

    /* a locked piece stays where it is */
    g_assert_true (lrg_hud_layout_set_locked (layout, "feed", TRUE));
    g_assert_true (lrg_hud_layout_get_locked (layout, "feed"));
    g_assert_false (lrg_hud_layout_move_by (layout, "feed", 1.0f, 1.0f));
    g_assert_false (lrg_hud_layout_place_rect (layout, "feed", 0.0f, 0.0f, 10.0f, 10.0f, 320.0f, 200.0f));
    g_assert_true (lrg_hud_layout_set_hidden (layout, "feed", TRUE));
    g_assert_true (lrg_hud_layout_get_hidden (layout, "feed"));

    /* reset drops the placement, the lock and the hidden flag */
    g_assert_true (lrg_hud_layout_reset (layout, "feed"));
    g_assert_false (lrg_hud_layout_get_locked (layout, "feed"));
    g_assert_false (lrg_hud_layout_get_hidden (layout, "feed"));
    g_assert_false (lrg_hud_layout_reset (layout, "nothing"));

    /* removing keeps the player's state for when it comes back */
    g_assert_true (lrg_hud_layout_set_position (layout, "feed", LRG_HUD_ANCHOR_BOTTOM, 0.0f, -30.0f));
    g_assert_true (lrg_hud_layout_remove_piece (layout, "feed"));
    g_assert_false (lrg_hud_layout_has_piece (layout, "feed"));
    g_assert_false (lrg_hud_layout_get_position (layout, "feed", NULL, NULL, NULL));
    g_assert_true (lrg_hud_layout_add_piece (layout, "feed", NULL, LRG_HUD_ANCHOR_RIGHT, -4.0f, 0.0f));
    g_assert_true (lrg_hud_layout_get_position (layout, "feed", &anchor, &x, &y));
    g_assert_cmpint (anchor, ==, LRG_HUD_ANCHOR_BOTTOM);
    g_assert_cmpfloat (y, ==, -30.0f);

    lrg_hud_layout_reset_all (layout);
    g_assert_false (lrg_hud_layout_is_modified (layout, "feed"));
}

static void
test_hud_layout_geometry (void)
{
    g_autoptr(LrgHudLayout) layout = NULL;
    LrgHudAnchor anchor;
    gfloat left;
    gfloat top;
    gfloat x;
    gfloat y;

    /* a 40 x 20 piece at the top right, 4 in from the edge */
    lrg_hud_layout_resolve (LRG_HUD_ANCHOR_TOP_RIGHT, -4.0f, 26.0f, 40.0f, 20.0f, 320.0f, 200.0f,
                            &left, &top);
    g_assert_cmpfloat (left, ==, 276.0f);
    g_assert_cmpfloat (top, ==, 26.0f);
    /* centred */
    lrg_hud_layout_resolve (LRG_HUD_ANCHOR_CENTER, 0.0f, 10.0f, 40.0f, 20.0f, 320.0f, 200.0f,
                            &left, &top);
    g_assert_cmpfloat (left, ==, 140.0f);
    g_assert_cmpfloat (top, ==, 100.0f);
    /* the bottom row */
    lrg_hud_layout_resolve (LRG_HUD_ANCHOR_BOTTOM_LEFT, 2.0f, -2.0f, 40.0f, 20.0f, 320.0f, 200.0f,
                            &left, &top);
    g_assert_cmpfloat (left, ==, 2.0f);
    g_assert_cmpfloat (top, ==, 178.0f);

    /* a dragged rectangle picks the anchor of the third its centre is in */
    layout = lrg_hud_layout_new ();
    g_assert_true (lrg_hud_layout_add_piece (layout, "frame", NULL, LRG_HUD_ANCHOR_CENTER, 0.0f, 0.0f));
    g_assert_true (lrg_hud_layout_place_rect (layout, "frame", 250.0f, 150.0f, 40.0f, 20.0f, 320.0f, 200.0f));
    g_assert_true (lrg_hud_layout_get_position (layout, "frame", &anchor, &x, &y));
    g_assert_cmpint (anchor, ==, LRG_HUD_ANCHOR_BOTTOM_RIGHT);
    lrg_hud_layout_resolve (anchor, x, y, 40.0f, 20.0f, 320.0f, 200.0f, &left, &top);
    g_assert_cmpfloat_with_epsilon (left, 250.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (top, 150.0f, 1e-4);
    /* the same spot on a wider canvas stays at the right edge */
    lrg_hud_layout_resolve (anchor, x, y, 40.0f, 20.0f, 480.0f, 200.0f, &left, &top);
    g_assert_cmpfloat_with_epsilon (left, 410.0f, 1e-4);

    /* dragged off the canvas: kept inside */
    g_assert_true (lrg_hud_layout_place_rect (layout, "frame", -50.0f, 500.0f, 40.0f, 20.0f, 320.0f, 200.0f));
    g_assert_true (lrg_hud_layout_get_position (layout, "frame", &anchor, &x, &y));
    g_assert_cmpint (anchor, ==, LRG_HUD_ANCHOR_BOTTOM_LEFT);
    lrg_hud_layout_resolve (anchor, x, y, 40.0f, 20.0f, 320.0f, 200.0f, &left, &top);
    g_assert_cmpfloat_with_epsilon (left, 0.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (top, 180.0f, 1e-4);
    g_assert_false (lrg_hud_layout_place_rect (layout, "frame", 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 200.0f));
}

static void
test_hud_layout_file (void)
{
    g_autoptr(LrgHudLayout) layout = NULL;
    g_autoptr(LrgHudLayout) other = NULL;
    g_autoptr(GVariant) state = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *bad = NULL;
    g_autofree gchar *text = NULL;
    g_autofree gchar *sub = NULL;
    LrgHudAnchor anchor;
    gfloat x;
    gfloat y;

    dir = g_dir_make_tmp ("lrg-hud-XXXXXX", &error);
    g_assert_no_error (error);
    path = g_build_filename (dir, "sub", "hud-layout.yaml", NULL);
    bad = g_build_filename (dir, "bad.yaml", NULL);

    layout = lrg_hud_layout_new ();
    g_assert_true (lrg_hud_layout_add_piece (layout, "tracker", NULL, LRG_HUD_ANCHOR_TOP_RIGHT, -4.0f, 26.0f));
    g_assert_true (lrg_hud_layout_add_piece (layout, "feed", NULL, LRG_HUD_ANCHOR_RIGHT, -4.0f, 0.0f));
    g_assert_true (lrg_hud_layout_set_position (layout, "tracker", LRG_HUD_ANCHOR_TOP_LEFT, 8.5f, 30.0f));
    g_assert_true (lrg_hud_layout_set_locked (layout, "tracker", TRUE));
    g_assert_true (lrg_hud_layout_set_hidden (layout, "feed", TRUE));
    g_assert_true (lrg_hud_layout_save_to_file (layout, path, &error));
    g_assert_no_error (error);
    g_assert_true (g_file_get_contents (path, &text, NULL, NULL));
    g_assert_nonnull (strstr (text, "tracker"));
    g_assert_nonnull (strstr (text, "top-left"));

    /* a new layout loads it before and after its pieces are added */
    other = lrg_hud_layout_new ();
    g_assert_true (lrg_hud_layout_add_piece (other, "feed", NULL, LRG_HUD_ANCHOR_RIGHT, -4.0f, 0.0f));
    g_assert_true (lrg_hud_layout_load_from_file (other, path, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_hud_layout_get_hidden (other, "feed"));
    g_assert_false (lrg_hud_layout_has_piece (other, "tracker"));
    g_assert_true (lrg_hud_layout_add_piece (other, "tracker", NULL, LRG_HUD_ANCHOR_TOP_RIGHT, -4.0f, 26.0f));
    g_assert_true (lrg_hud_layout_get_position (other, "tracker", &anchor, &x, &y));
    g_assert_cmpint (anchor, ==, LRG_HUD_ANCHOR_TOP_LEFT);
    g_assert_cmpfloat (x, ==, 8.5f);
    g_assert_cmpfloat (y, ==, 30.0f);
    g_assert_true (lrg_hud_layout_get_locked (other, "tracker"));

    /* the variant round trip */
    state = g_variant_ref_sink (lrg_hud_layout_to_variant (other));
    g_assert_cmpstr (g_variant_get_type_string (state), ==, "a{s(bsddbb)}");
    lrg_hud_layout_reset_all (other);
    g_assert_false (lrg_hud_layout_is_modified (other, "tracker"));
    g_assert_true (lrg_hud_layout_load_variant (other, state, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_hud_layout_is_modified (other, "tracker"));

    /* bad files fail and change nothing */
    g_assert_true (g_file_set_contents (bad, "version: 1\npieces:\n  tracker:\n    anchor: sideways\n", -1, NULL));
    g_assert_false (lrg_hud_layout_load_from_file (other, bad, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_assert_true (lrg_hud_layout_is_modified (other, "tracker"));
    g_assert_true (g_file_set_contents (bad, "version: 1\npieces:\n  tracker:\n    anchor: top\n    x: wide\n", -1, NULL));
    g_assert_false (lrg_hud_layout_load_from_file (other, bad, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_assert_true (g_file_set_contents (bad, "version: 1\npieces:\n  tracker:\n    colour: red\n", -1, NULL));
    g_assert_false (lrg_hud_layout_load_from_file (other, bad, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_assert_true (g_file_set_contents (bad, "version: 9\n", -1, NULL));
    g_assert_false (lrg_hud_layout_load_from_file (other, bad, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
    g_clear_error (&error);
    g_assert_true (g_file_set_contents (bad, "[ not, a, mapping ]\n", -1, NULL));
    g_assert_false (lrg_hud_layout_load_from_file (other, bad, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_assert_true (lrg_hud_layout_get_locked (other, "tracker"));

    /* a bad variant too */
    g_assert_false (lrg_hud_layout_load_variant (other, g_variant_new_string ("x"), &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);

    /* a missing file is the default layout */
    g_assert_cmpint (g_unlink (path), ==, 0);
    g_assert_true (lrg_hud_layout_load_from_file (other, path, &error));
    g_assert_no_error (error);
    g_assert_false (lrg_hud_layout_is_modified (other, "tracker"));
    g_assert_false (lrg_hud_layout_get_hidden (other, "feed"));

    g_unlink (bad);
    sub = g_path_get_dirname (path);
    g_rmdir (sub);
    g_rmdir (dir);
}

/* ==========================================================================
 * LrgFloatingText
 * ========================================================================== */

static void
test_floating_text (void)
{
    g_autoptr(LrgFloatingText) texts = NULL;
    const LrgFloatingTextEntry *entry;
    guint8 red[4] = { 255, 40, 40, 255 };
    gfloat position[3];
    gfloat lift_early;
    guint first;
    guint second;

    texts = lrg_floating_text_new (3);
    g_assert_cmpuint (lrg_floating_text_add (texts, "", 0.0f, 0.0f, 0.0f, NULL, 0, 0.0f), ==, 0);
    g_assert_cmpuint (lrg_floating_text_add (texts, "1", NAN, 0.0f, 0.0f, NULL, 0, 0.0f), ==, 0);

    first = lrg_floating_text_add (texts, "12", 1.0f, 2.0f, 3.0f, red, 7, 1.0f);
    g_assert_cmpuint (first, >, 0);
    entry = lrg_floating_text_peek (texts, 0);
    g_assert_cmpstr (entry->text, ==, "12");
    g_assert_cmpint (entry->kind, ==, 7);
    g_assert_cmpuint (entry->color[1], ==, 40);
    g_assert_cmpfloat (lrg_floating_text_entry_get_alpha (entry), ==, 1.0f);
    g_assert_cmpfloat (lrg_floating_text_entry_get_scale (entry), >, 1.0f);
    g_assert_cmpuint (entry->stack, ==, 0);

    /* a second hit at the same point stacks above */
    second = lrg_floating_text_add (texts, "5", 1.1f, 2.0f, 3.0f, NULL, 0, 0.0f);
    g_assert_cmpuint (second, >, first);
    g_assert_cmpuint (lrg_floating_text_peek (texts, 1)->stack, ==, 1);
    g_assert_cmpfloat (lrg_floating_text_peek (texts, 1)->lifetime, ==, LRG_FLOATING_TEXT_DEFAULT_LIFETIME);

    /* rising, slowing, fading */
    g_assert_cmpuint (lrg_floating_text_update (texts, 0.25f), ==, 0);
    entry = lrg_floating_text_peek (texts, 0);
    lift_early = lrg_floating_text_entry_get_lift (entry);
    g_assert_cmpfloat (lift_early, >, LRG_FLOATING_TEXT_DEFAULT_RISE * 0.25f);
    g_assert_cmpfloat (lrg_floating_text_entry_get_scale (entry), ==, 1.0f);
    g_assert_true (lrg_floating_text_get_world_position (texts, 0, position));
    g_assert_cmpfloat_with_epsilon (position[1], 2.0f + lift_early, 1e-5);
    g_assert_true (lrg_floating_text_get_world_position (texts, 1, position));
    g_assert_cmpfloat (position[1], >, 2.0f + 0.25f);
    g_assert_false (lrg_floating_text_get_world_position (texts, 5, position));
    lrg_floating_text_update (texts, 0.6f);
    entry = lrg_floating_text_peek (texts, 0);
    g_assert_cmpfloat (lrg_floating_text_entry_get_alpha (entry), <, 1.0f);
    g_assert_cmpfloat (lrg_floating_text_entry_get_alpha (entry), >, 0.0f);

    /* the first expires, the second lives on */
    g_assert_cmpuint (lrg_floating_text_update (texts, 0.2f), ==, 1);
    g_assert_cmpuint (lrg_floating_text_get_count (texts), ==, 1);
    g_assert_cmpstr (lrg_floating_text_peek (texts, 0)->text, ==, "5");

    /* a full queue lets the oldest go */
    lrg_floating_text_add (texts, "a", 10.0f, 0.0f, 0.0f, NULL, 0, 0.0f);
    lrg_floating_text_add (texts, "b", 20.0f, 0.0f, 0.0f, NULL, 0, 0.0f);
    lrg_floating_text_add (texts, "c", 30.0f, 0.0f, 0.0f, NULL, 0, 0.0f);
    g_assert_cmpuint (lrg_floating_text_get_count (texts), ==, 3);
    g_assert_cmpstr (lrg_floating_text_peek (texts, 0)->text, ==, "a");
    g_assert_cmpuint (lrg_floating_text_peek (texts, 2)->stack, ==, 0);
    g_assert_cmpuint (lrg_floating_text_update (texts, -1.0f), ==, 0);
    lrg_floating_text_clear (texts);
    g_assert_cmpuint (lrg_floating_text_get_count (texts), ==, 0);
}

/* ==========================================================================
 * LrgRadialMenu
 * ========================================================================== */

static void
on_activated (LrgRadialMenu *menu,
              gint           index,
              const gchar   *item_id,
              gpointer       user_data)
{
    (void) menu;
    (void) index;
    g_free (*(gchar **) user_data);
    *(gchar **) user_data = g_strdup (item_id);
}

static void
test_radial_menu (void)
{
    g_autoptr(LrgRadialMenu) menu = NULL;
    g_autofree gchar *chosen = NULL;
    gfloat x;
    gfloat y;

    menu = lrg_radial_menu_new ();
    g_assert_cmpint (lrg_radial_menu_index_at (menu, 0.0f, -50.0f), ==, -1);
    g_assert_cmpint (lrg_radial_menu_add_item (menu, "steal", "Steal"), ==, 0);
    g_assert_cmpint (lrg_radial_menu_add_item (menu, "grab", NULL), ==, 1);
    g_assert_cmpint (lrg_radial_menu_add_item (menu, "info", "Info"), ==, 2);
    g_assert_cmpint (lrg_radial_menu_add_item (menu, "talk", "Talk"), ==, 3);
    g_assert_cmpint (lrg_radial_menu_add_item (menu, "talk", "Again"), ==, -1);
    g_assert_cmpint (lrg_radial_menu_add_item (menu, "", NULL), ==, -1);
    g_assert_cmpuint (lrg_radial_menu_get_count (menu), ==, 4);
    g_assert_cmpstr (lrg_radial_menu_get_item_label (menu, 1), ==, "grab");
    g_assert_cmpint (lrg_radial_menu_find (menu, "info"), ==, 2);

    /* four sectors: up, right, down, left */
    g_assert_cmpint (lrg_radial_menu_index_at (menu, 0.0f, -50.0f), ==, 0);
    g_assert_cmpint (lrg_radial_menu_index_at (menu, 50.0f, 0.0f), ==, 1);
    g_assert_cmpint (lrg_radial_menu_index_at (menu, 0.0f, 50.0f), ==, 2);
    g_assert_cmpint (lrg_radial_menu_index_at (menu, -50.0f, 0.0f), ==, 3);
    g_assert_cmpint (lrg_radial_menu_index_at (menu, -30.0f, -31.0f), ==, 0);
    g_assert_cmpint (lrg_radial_menu_index_at (menu, -31.0f, -30.0f), ==, 3);
    /* the dead zone */
    g_assert_cmpint (lrg_radial_menu_index_at (menu, 2.0f, 2.0f), ==, -1);
    lrg_radial_menu_set_dead_zone (menu, -5.0f);
    g_assert_cmpfloat (lrg_radial_menu_get_dead_zone (menu), ==, 0.0f);
    lrg_radial_menu_set_dead_zone (menu, 8.0f);

    /* turning the wheel moves the sectors */
    lrg_radial_menu_set_start_angle (menu, -90.0f);
    g_assert_cmpfloat (lrg_radial_menu_get_start_angle (menu), ==, 270.0f);
    g_assert_cmpint (lrg_radial_menu_index_at (menu, -50.0f, 0.0f), ==, 0);
    lrg_radial_menu_set_start_angle (menu, 0.0f);

    /* item positions: the first straight up */
    g_assert_true (lrg_radial_menu_get_item_position (menu, 0, 40.0f, &x, &y));
    g_assert_cmpfloat_with_epsilon (x, 0.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (y, -40.0f, 1e-4);
    g_assert_true (lrg_radial_menu_get_item_position (menu, 1, 40.0f, &x, &y));
    g_assert_cmpfloat_with_epsilon (x, 40.0f, 1e-4);
    g_assert_false (lrg_radial_menu_get_item_position (menu, 9, 40.0f, &x, &y));

    /* closed: pointing does nothing */
    g_assert_cmpint (lrg_radial_menu_point (menu, 50.0f, 0.0f), ==, -1);
    g_assert_cmpint (lrg_radial_menu_close (menu, TRUE), ==, -1);

    /* open on the current choice, point, keep it in the dead zone, choose */
    g_signal_connect (menu, "activated", G_CALLBACK (on_activated), &chosen);
    lrg_radial_menu_open (menu, 1);
    g_assert_true (lrg_radial_menu_is_open (menu));
    g_assert_cmpint (lrg_radial_menu_get_selected (menu), ==, 1);
    g_assert_cmpint (lrg_radial_menu_point (menu, 0.0f, 60.0f), ==, 2);
    g_assert_cmpint (lrg_radial_menu_point (menu, 1.0f, 1.0f), ==, 2);
    g_assert_cmpint (lrg_radial_menu_step (menu, 1), ==, 3);
    g_assert_cmpint (lrg_radial_menu_step (menu, 1), ==, 0);
    g_assert_cmpint (lrg_radial_menu_step (menu, -1), ==, 3);
    g_assert_cmpint (lrg_radial_menu_close (menu, TRUE), ==, 3);
    g_assert_cmpstr (chosen, ==, "talk");
    g_assert_false (lrg_radial_menu_is_open (menu));

    /* cancelled */
    g_clear_pointer (&chosen, g_free);
    lrg_radial_menu_open (menu, -1);
    g_assert_cmpint (lrg_radial_menu_get_selected (menu), ==, -1);
    g_assert_cmpint (lrg_radial_menu_step (menu, -1), ==, 3);
    g_assert_cmpint (lrg_radial_menu_close (menu, FALSE), ==, -1);
    g_assert_null (chosen);
    lrg_radial_menu_open (menu, 42);
    g_assert_cmpint (lrg_radial_menu_get_selected (menu), ==, -1);
    g_assert_cmpint (lrg_radial_menu_close (menu, TRUE), ==, -1);
}

/* ==========================================================================
 * LrgFeed
 * ========================================================================== */

static void
test_feed (void)
{
    g_autoptr(LrgFeed) feed = NULL;

    feed = lrg_feed_new (3, 2.0f);
    g_assert_cmpint (lrg_feed_push (feed, "x", "", 1), ==, -1);
    g_assert_cmpint (lrg_feed_push (feed, "gold", "Gold", 35), ==, 0);
    g_assert_cmpint (lrg_feed_push (feed, "item:1", "Longsword", 1), ==, 1);
    /* the same key merges and moves to the end */
    g_assert_cmpint (lrg_feed_push (feed, "gold", "Gold", 15), ==, 1);
    g_assert_cmpuint (lrg_feed_get_count (feed), ==, 2);
    g_assert_cmpstr (lrg_feed_get_label (feed, 1), ==, "Gold");
    g_assert_cmpint (lrg_feed_get_row_count (feed, 1), ==, 50);
    g_assert_cmpstr (lrg_feed_get_key (feed, 0), ==, "item:1");
    /* rows without a key never merge */
    lrg_feed_push (feed, NULL, "Note", 1);
    lrg_feed_push (feed, NULL, "Note", 1);
    g_assert_cmpuint (lrg_feed_get_count (feed), ==, 3);
    g_assert_cmpstr (lrg_feed_get_label (feed, 0), ==, "Gold");

    /* fading over the last second, gone after its time */
    g_assert_cmpuint (lrg_feed_update (feed, 1.5f), ==, 0);
    g_assert_cmpfloat (lrg_feed_get_alpha (feed, 0), <, 1.0f);
    g_assert_cmpfloat (lrg_feed_get_alpha (feed, 0), >, 0.0f);
    g_assert_cmpuint (lrg_feed_update (feed, 0.6f), ==, 3);
    g_assert_cmpuint (lrg_feed_get_count (feed), ==, 0);
    g_assert_null (lrg_feed_get_label (feed, 0));
    g_assert_cmpfloat (lrg_feed_get_alpha (feed, 0), ==, 0.0f);
    lrg_feed_push (feed, "a", "A", 1);
    lrg_feed_clear (feed);
    g_assert_cmpuint (lrg_feed_get_count (feed), ==, 0);
}

/* ==========================================================================
 * LrgCameraZoom
 * ========================================================================== */

static void
count_crossed (LrgCameraZoom *zoom,
               gboolean       first_person,
               gpointer       user_data)
{
    (void) zoom;
    ((guint *) user_data)[first_person ? 1 : 0]++;
}

/* updates until the arm settles (or a crossing happened) */
static void
settle (LrgCameraZoom *zoom)
{
    guint i;

    for (i = 0; i < 240; i++)
    {
        if (lrg_camera_zoom_update (zoom, 1.0f / 60.0f))
            return;
    }
}

static void
test_camera_zoom (void)
{
    g_autoptr(LrgCameraZoom) zoom = NULL;
    guint crossed[2] = { 0, 0 };
    gfloat before;

    zoom = lrg_camera_zoom_new (1.5f, 8.0f);
    g_signal_connect (zoom, "crossed", G_CALLBACK (count_crossed), crossed);
    g_assert_false (lrg_camera_zoom_get_first_person (zoom));
    g_assert_cmpfloat (lrg_camera_zoom_get_distance (zoom), ==, 8.0f);
    lrg_camera_zoom_set_step (zoom, 1.0f);
    lrg_camera_zoom_set_step (zoom, -2.0f);
    g_assert_cmpfloat (lrg_camera_zoom_get_step (zoom), ==, 1.0f);

    /* a notch in moves the target; the arm eases there smoothly */
    g_assert_true (lrg_camera_zoom_scroll (zoom, 1.0f));
    g_assert_cmpfloat (lrg_camera_zoom_get_target (zoom), ==, 7.0f);
    g_assert_false (lrg_camera_zoom_update (zoom, 1.0f / 60.0f));
    before = lrg_camera_zoom_get_distance (zoom);
    g_assert_cmpfloat (before, <, 8.0f);
    g_assert_cmpfloat (before, >, 7.0f);
    settle (zoom);
    g_assert_cmpfloat (lrg_camera_zoom_get_distance (zoom), ==, 7.0f);

    /* never past the far end */
    lrg_camera_zoom_scroll (zoom, -5.0f);
    g_assert_cmpfloat (lrg_camera_zoom_get_target (zoom), ==, 8.0f);

    /* spinning in to the near end stops there first */
    lrg_camera_zoom_set_distance (zoom, 2.0f);
    g_assert_cmpfloat (lrg_camera_zoom_get_distance (zoom), ==, 2.0f);
    g_assert_true (lrg_camera_zoom_scroll (zoom, 0.25f));
    g_assert_false (lrg_camera_zoom_is_crossing (zoom));
    g_assert_cmpfloat (lrg_camera_zoom_get_target (zoom), ==, 1.75f);
    lrg_camera_zoom_scroll (zoom, 0.3f);
    g_assert_cmpfloat (lrg_camera_zoom_get_target (zoom), ==, 1.5f);
    g_assert_false (lrg_camera_zoom_is_crossing (zoom));
    settle (zoom);
    g_assert_false (lrg_camera_zoom_get_first_person (zoom));
    g_assert_cmpuint (crossed[1], ==, 0);

    /* one more notch at the near end: the eye */
    g_assert_true (lrg_camera_zoom_scroll (zoom, 1.0f));
    g_assert_true (lrg_camera_zoom_get_first_person (zoom));
    g_assert_cmpfloat (lrg_camera_zoom_get_distance (zoom), ==, 0.0f);
    g_assert_cmpuint (crossed[1], ==, 1);
    /* in first person zooming in does nothing */
    g_assert_false (lrg_camera_zoom_scroll (zoom, 1.0f));

    /* zooming out starts at the near end */
    g_assert_true (lrg_camera_zoom_scroll (zoom, -1.0f));
    g_assert_false (lrg_camera_zoom_get_first_person (zoom));
    g_assert_cmpfloat (lrg_camera_zoom_get_distance (zoom), ==, 1.5f);
    g_assert_cmpfloat (lrg_camera_zoom_get_target (zoom), ==, 1.5f);
    g_assert_cmpuint (crossed[0], ==, 1);
    lrg_camera_zoom_scroll (zoom, -3.0f);
    g_assert_cmpfloat (lrg_camera_zoom_get_target (zoom), ==, 4.5f);
    settle (zoom);

    /* a fast spin from afar eases all the way, then crosses */
    lrg_camera_zoom_scroll (zoom, 6.0f);
    g_assert_true (lrg_camera_zoom_is_crossing (zoom));
    g_assert_false (lrg_camera_zoom_get_first_person (zoom));
    g_assert_false (lrg_camera_zoom_update (zoom, 1.0f / 60.0f));
    g_assert_cmpfloat (lrg_camera_zoom_get_distance (zoom), <, 4.5f);
    g_assert_cmpfloat (lrg_camera_zoom_get_distance (zoom), >, 1.5f);
    settle (zoom);
    g_assert_true (lrg_camera_zoom_get_first_person (zoom));
    g_assert_cmpuint (crossed[1], ==, 2);

    /* zooming out cancels a pending crossing */
    lrg_camera_zoom_set_distance (zoom, 6.0f);
    lrg_camera_zoom_scroll (zoom, 9.0f);
    g_assert_true (lrg_camera_zoom_is_crossing (zoom));
    lrg_camera_zoom_scroll (zoom, -1.0f);
    g_assert_false (lrg_camera_zoom_is_crossing (zoom));
    g_assert_cmpfloat (lrg_camera_zoom_get_target (zoom), ==, 2.5f);

    /* a toggle elsewhere remembers the distance and comes back to it */
    lrg_camera_zoom_set_distance (zoom, 5.0f);
    lrg_camera_zoom_set_first_person (zoom, TRUE);
    g_assert_true (lrg_camera_zoom_get_first_person (zoom));
    lrg_camera_zoom_set_first_person (zoom, FALSE);
    g_assert_cmpfloat (lrg_camera_zoom_get_distance (zoom), ==, 5.0f);
    lrg_camera_zoom_set_resume_distance (zoom, 99.0f);
    g_assert_cmpfloat (lrg_camera_zoom_get_resume_distance (zoom), ==, 8.0f);

    /* set_distance(0) enters the eye; the limits clamp */
    lrg_camera_zoom_set_distance (zoom, 0.0f);
    g_assert_true (lrg_camera_zoom_get_first_person (zoom));
    lrg_camera_zoom_set_distance (zoom, 0.2f);
    g_assert_false (lrg_camera_zoom_get_first_person (zoom));
    g_assert_cmpfloat (lrg_camera_zoom_get_distance (zoom), ==, 1.5f);
    lrg_camera_zoom_set_limits (zoom, 2.0f, 1.0f);
    g_assert_cmpfloat (lrg_camera_zoom_get_max_distance (zoom), ==, 2.0f);
    g_assert_cmpfloat (lrg_camera_zoom_get_distance (zoom), ==, 2.0f);

    /* speed 0 jumps */
    lrg_camera_zoom_set_limits (zoom, 1.0f, 10.0f);
    lrg_camera_zoom_set_speed (zoom, 0.0f);
    lrg_camera_zoom_scroll (zoom, -3.0f);
    lrg_camera_zoom_update (zoom, 0.0f);
    g_assert_cmpfloat (lrg_camera_zoom_get_distance (zoom), ==, 5.0f);
    g_assert_false (lrg_camera_zoom_scroll (zoom, NAN));
}

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/hud/layout/pieces", test_hud_layout_pieces);
    g_test_add_func ("/hud/layout/geometry", test_hud_layout_geometry);
    g_test_add_func ("/hud/layout/file", test_hud_layout_file);
    g_test_add_func ("/hud/floating-text", test_floating_text);
    g_test_add_func ("/hud/radial-menu", test_radial_menu);
    g_test_add_func ("/hud/feed", test_feed);
    g_test_add_func ("/hud/camera-zoom", test_camera_zoom);

    return g_test_run ();
}
