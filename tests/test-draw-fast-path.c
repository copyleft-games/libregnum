/* test-draw-fast-path.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * graylib's fast 2D vertex path (deps/graylib/src/drawing/grl-fast2d.c)
 * as libregnum uses it: the immediate-mode GUI's raylib backend (rounded
 * and plain rectangles, outlines, circles, images, text under scissor
 * clips), sprite components in a world (tints, flips, source rects, z
 * order) and retained UI labels. Each scene is drawn into a render
 * texture with the fast path off (raylib's own functions) and on, and the
 * pixels must be identical. The GL tests open a hidden window and skip
 * without a display.
 */

#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

#define TARGET_W (320)
#define TARGET_H (240)

typedef void (*SceneFunc) (gpointer data);

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
    test_window = grl_window_new (8, 8, "lrg-draw-fast-path-test");
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

/* render:
 * Draws @scene into a fresh render texture with the fast path @fast and
 * returns the pixels. */
static Image
render (SceneFunc scene,
        gpointer  data,
        gboolean  fast)
{
    RenderTexture2D target = LoadRenderTexture (TARGET_W, TARGET_H);
    Image           image;

    grl_draw_set_fast_path_enabled (fast);
    BeginTextureMode (target);
    ClearBackground ((Color){ 10, 14, 22, 255 });
    scene (data);
    EndTextureMode ();
    grl_draw_set_fast_path_enabled (TRUE);

    image = LoadImageFromTexture (target.texture);
    UnloadRenderTexture (target);
    return image;
}

/* assert_same:
 * Renders @scene both ways (once beforehand to settle caches and install
 * graylib's batch) and asserts identical, non-blank pixels. */
static void
assert_same (SceneFunc scene,
             gpointer  data)
{
    Image         slow;
    Image         quick;
    gsize         n;
    gsize         k;
    gsize         differ;
    gboolean      ink;
    const guchar *a;
    const guchar *b;

    UnloadImage (render (scene, data, TRUE));
    slow = render (scene, data, FALSE);
    quick = render (scene, data, TRUE);

    g_assert_cmpint (slow.width, ==, quick.width);
    g_assert_cmpint (slow.height, ==, quick.height);
    g_assert_cmpint (slow.format, ==, quick.format);
    n = (gsize) GetPixelDataSize (slow.width, slow.height, slow.format);
    a = slow.data;
    b = quick.data;
    differ = 0;
    ink = FALSE;
    for (k = 0; k < n; k++)
    {
        if (a[k] != b[k])
            differ++;
        if ((k % 4) != 3 && a[k] > 40)
            ink = TRUE;
    }
    if (differ != 0)
        g_test_message ("%" G_GSIZE_FORMAT " of %" G_GSIZE_FORMAT " bytes differ",
                        differ, n);
    g_assert_cmpuint (differ, ==, 0);
    g_assert_true (ink);

    UnloadImage (slow);
    UnloadImage (quick);
}

/* ==========================================================================
 * GUI
 * ========================================================================== */

typedef struct
{
    LrgGuiContext  *ctx;
    LrgGuiRaylib   *renderer;
    LrgGuiDrawList *extra;
    GrlTexture     *image;
    gfloat          values[24];
    gboolean        flags[24];
} GuiScene;

static void
gui_scene (gpointer data)
{
    GuiScene   *st = data;
    LrgGuiInput input;
    LrgGuiRect  rect = { 0.0f, 0.0f, TARGET_W, TARGET_H };
    guint       i;

    /* Widgets: tree nodes, sliders, checkboxes, text, buttons */
    lrg_gui_input_init (&input);
    lrg_gui_context_begin_frame (st->ctx, &input, TARGET_W, TARGET_H);
    lrg_gui_begin_region (st->ctx, "panel", &rect, 0);
    for (i = 0; i < 24; i++)
    {
        gchar label[32];

        lrg_gui_push_id_int (st->ctx, (gint) i);
        g_snprintf (label, sizeof label, "Property %u", i);
        if (lrg_gui_tree_node (st->ctx, label, 0))
            lrg_gui_tree_pop (st->ctx);
        lrg_gui_slider_float (st->ctx, "value", &st->values[i], 0.0f, 1.0f, "%.3f");
        lrg_gui_checkbox (st->ctx, "enabled", &st->flags[i]);
        lrg_gui_textf (st->ctx, "item %u", i);
        lrg_gui_button (st->ctx, "Apply");
        lrg_gui_pop_id (st->ctx);
    }
    lrg_gui_end_region (st->ctx);
    lrg_gui_context_end_frame (st->ctx);
    lrg_gui_raylib_render (st->renderer, lrg_gui_context_get_draw_list (st->ctx));

    /* Every command kind the backend draws, rounded and not, clipped */
    lrg_gui_raylib_render (st->renderer, st->extra);
}

static void
gui_build_extra (GuiScene *st)
{
    guint i;

    st->extra = lrg_gui_draw_list_new ();
    for (i = 0; i < 30; i++)
    {
        LrgGuiRect  r = { 4.0f + (gfloat) (i % 6) * 52.0f + 0.5f,
                          4.0f + (gfloat) (i / 6) * 46.0f + 0.25f,
                          44.0f + (gfloat) (i % 3), 30.0f + (gfloat) (i % 4) };
        LrgGuiRect  clip = { 0.0f, 0.0f, 300.0f, 200.0f };
        LrgGuiColor c = LRG_GUI_RGBA (40 + i * 7, 200 - i * 5, 90 + i * 3, 160 + i * 3);

        if (i == 10)
            lrg_gui_draw_list_push_clip (st->extra, &clip);
        lrg_gui_draw_list_rect_filled (st->extra, &r, c, (gfloat) (i % 5) * 2.0f);
        lrg_gui_draw_list_rect_outline (st->extra, &r, LRG_GUI_HEX (0xd0d0f0), 1.0f + (gfloat) (i % 3),
                                        (i % 2) ? 0.0f : 4.0f);
        lrg_gui_draw_list_circle (st->extra, r.x + 10.0f, r.y + 10.0f, 4.0f + (gfloat) (i % 4),
                                  c, (i % 3) != 0);
        lrg_gui_draw_list_text (st->extra, r.x + 2.0f, r.y + 14.0f, "Label", -1, 10.0f,
                                LRG_GUI_HEX (0xffffff), (i % 2) == 0);
        lrg_gui_draw_list_line (st->extra, r.x, r.y, r.x + r.w, r.y + r.h, c, 1.5f);
        if (i % 4 == 0)
            lrg_gui_draw_list_image (st->extra, &r, G_OBJECT (st->image), LRG_GUI_HEX (0xffffff));
        if (i == 20)
            lrg_gui_draw_list_pop_clip (st->extra);
    }
}

static void
test_draw_fast_path_gui (void)
{
    GuiScene             st;
    g_autoptr(GrlColor)  white = NULL;
    g_autoptr(GrlImage)  img = NULL;

    SKIP_WITHOUT_GL ();

    memset (&st, 0, sizeof st);
    white = grl_color_new (230, 180, 60, 255);
    img = grl_image_new_color (16, 16, white);
    st.image = grl_texture_new_from_image (img);
    st.ctx = lrg_gui_context_new ();
    st.renderer = lrg_gui_raylib_new ();
    lrg_gui_raylib_attach (st.renderer, st.ctx);
    gui_build_extra (&st);

    assert_same (gui_scene, &st);

    g_clear_object (&st.extra);
    g_clear_object (&st.renderer);
    g_clear_object (&st.ctx);
    g_clear_object (&st.image);
}

/* ==========================================================================
 * Sprites
 * ========================================================================== */

static void
world_scene (gpointer data)
{
    lrg_world_draw (LRG_WORLD (data));
}

static void
test_draw_fast_path_sprites (void)
{
    g_autoptr(LrgWorld)   world = NULL;
    g_autoptr(GrlTexture) texture = NULL;
    g_autoptr(GrlTexture) other = NULL;
    g_autoptr(GrlImage)   img = NULL;
    g_autoptr(GrlColor)   base = NULL;
    guint                 i;

    SKIP_WITHOUT_GL ();

    base = grl_color_new (255, 255, 255, 255);
    img = grl_image_new_color (8, 8, base);
    for (i = 0; i < 8; i++)
    {
        g_autoptr(GrlColor) px = grl_color_new ((guint8) (i * 30), 255 - (guint8) (i * 25), 128, 255);

        grl_image_draw_pixel (img, (gint) i, (gint) (7 - i), px);
        grl_image_draw_pixel (img, (gint) i, (gint) (i / 2), px);
    }
    texture = grl_texture_new_from_image (img);
    other = grl_texture_new_from_image (img);

    world = lrg_world_new ();
    for (i = 0; i < 600; i++)
    {
        g_autoptr(LrgGameObject)      obj = NULL;
        g_autoptr(LrgSpriteComponent) sprite = NULL;
        g_autoptr(GrlColor)           tint = NULL;

        obj = lrg_game_object_new_at ((gfloat) (i % 40) * 7.5f + 0.25f,
                                      (gfloat) (i / 40) * 15.0f + 0.5f);
        sprite = lrg_sprite_component_new_with_texture ((i % 9) == 0 ? other : texture);
        tint = grl_color_new ((guint8) (i * 13), (guint8) (255 - i), (guint8) (i * 7), (guint8) (128 + i % 128));
        lrg_sprite_component_set_tint (sprite, tint);
        if (i % 3 == 0)
            lrg_sprite_component_set_source (sprite, 1, 2, 5, 4);
        lrg_sprite_component_set_flip_h (sprite, (i % 4) == 1);
        lrg_sprite_component_set_flip_v (sprite, (i % 5) == 2);
        lrg_game_object_add_component (obj, LRG_COMPONENT (sprite));
        grl_entity_set_z_index (GRL_ENTITY (obj), (gint) (i % 7));
        lrg_world_add_object (world, obj);
    }

    assert_same (world_scene, world);
}

/* ==========================================================================
 * Labels
 * ========================================================================== */

static void
labels_scene (gpointer data)
{
    GPtrArray *labels = data;
    guint      i;

    for (i = 0; i < labels->len; i++)
        lrg_widget_draw (LRG_WIDGET (g_ptr_array_index (labels, i)));
}

static void
test_draw_fast_path_labels (void)
{
    g_autoptr(GPtrArray) labels = NULL;
    guint                i;

    SKIP_WITHOUT_GL ();

    labels = g_ptr_array_new_with_free_func (g_object_unref);
    for (i = 0; i < 40; i++)
    {
        g_autofree gchar *text = g_strdup_printf ("Label %u: the quick brown fox", i);
        LrgLabel         *label = lrg_label_new (text);

        lrg_widget_set_position (LRG_WIDGET (label), (gfloat) (i % 2) * 160.0f + 0.5f,
                                 (gfloat) (i / 2) * 12.0f);
        lrg_label_set_font_size (label, 10.0f + (gfloat) (i % 3));
        g_ptr_array_add (labels, label);
    }

    assert_same (labels_scene, labels);
}

int
main (int   argc,
      char *argv[])
{
    gint result;

    g_test_init (&argc, &argv, NULL);

    graphics_available = init_graphics_context ();

    g_test_add_func ("/draw-fast-path/gui", test_draw_fast_path_gui);
    g_test_add_func ("/draw-fast-path/sprites", test_draw_fast_path_sprites);
    g_test_add_func ("/draw-fast-path/labels", test_draw_fast_path_labels);

    result = g_test_run ();
    g_clear_object (&test_window);
    return result;
}
