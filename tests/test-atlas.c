/* test-atlas.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the atlas module.
 */

#include "../src/atlas/lrg-atlas-region.h"
#include "../src/atlas/lrg-texture-atlas.h"
#include "../src/atlas/lrg-sprite-sheet.h"
#include "../src/atlas/lrg-nine-slice.h"
#include "../src/atlas/lrg-atlas-packer.h"

#include <glib.h>
#include <math.h>

/* ========================================================================== */
/*                           Atlas Region Tests                               */
/* ========================================================================== */

static void
test_atlas_region_new (void)
{
    LrgAtlasRegion *region;

    region = lrg_atlas_region_new ("test_sprite", 10, 20, 32, 64);

    g_assert_nonnull (region);
    g_assert_cmpstr (lrg_atlas_region_get_name (region), ==, "test_sprite");
    g_assert_cmpint (lrg_atlas_region_get_x (region), ==, 10);
    g_assert_cmpint (lrg_atlas_region_get_y (region), ==, 20);
    g_assert_cmpint (lrg_atlas_region_get_width (region), ==, 32);
    g_assert_cmpint (lrg_atlas_region_get_height (region), ==, 64);

    lrg_atlas_region_free (region);
}

static void
test_atlas_region_new_with_uv (void)
{
    LrgAtlasRegion *region;
    gfloat u1, v1, u2, v2;

    region = lrg_atlas_region_new_with_uv ("sprite", 0, 0, 16, 16,
                                           0.0f, 0.0f, 0.5f, 0.5f);

    g_assert_nonnull (region);
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_u1 (region), 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_v1 (region), 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_u2 (region), 0.5f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_v2 (region), 0.5f, 0.001f);

    lrg_atlas_region_get_uv (region, &u1, &v1, &u2, &v2);
    g_assert_cmpfloat_with_epsilon (u1, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (v1, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (u2, 0.5f, 0.001f);
    g_assert_cmpfloat_with_epsilon (v2, 0.5f, 0.001f);

    lrg_atlas_region_free (region);
}

static void
test_atlas_region_copy (void)
{
    LrgAtlasRegion *original;
    LrgAtlasRegion *copy;

    original = lrg_atlas_region_new ("original", 5, 10, 15, 20);
    lrg_atlas_region_set_rotated (original, TRUE);
    lrg_atlas_region_set_pivot (original, 7.5f, 10.0f);

    copy = lrg_atlas_region_copy (original);

    g_assert_nonnull (copy);
    g_assert_cmpstr (lrg_atlas_region_get_name (copy), ==, "original");
    g_assert_cmpint (lrg_atlas_region_get_x (copy), ==, 5);
    g_assert_cmpint (lrg_atlas_region_get_y (copy), ==, 10);
    g_assert_cmpint (lrg_atlas_region_get_width (copy), ==, 15);
    g_assert_cmpint (lrg_atlas_region_get_height (copy), ==, 20);
    g_assert_true (lrg_atlas_region_is_rotated (copy));
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_pivot_x (copy), 7.5f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_pivot_y (copy), 10.0f, 0.001f);

    lrg_atlas_region_free (original);
    lrg_atlas_region_free (copy);
}

static void
test_atlas_region_calculate_uv (void)
{
    LrgAtlasRegion *region;

    region = lrg_atlas_region_new ("sprite", 64, 32, 32, 16);
    lrg_atlas_region_calculate_uv (region, 256, 128);

    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_u1 (region), 64.0f / 256.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_v1 (region), 32.0f / 128.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_u2 (region), 96.0f / 256.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_v2 (region), 48.0f / 128.0f, 0.001f);

    lrg_atlas_region_free (region);
}

static void
test_atlas_region_get_rect (void)
{
    LrgAtlasRegion *region;
    gint x, y, width, height;

    region = lrg_atlas_region_new ("rect_test", 100, 200, 50, 75);
    lrg_atlas_region_get_rect (region, &x, &y, &width, &height);

    g_assert_cmpint (x, ==, 100);
    g_assert_cmpint (y, ==, 200);
    g_assert_cmpint (width, ==, 50);
    g_assert_cmpint (height, ==, 75);

    lrg_atlas_region_free (region);
}

static void
test_atlas_region_transforms (void)
{
    LrgAtlasRegion *region;

    region = lrg_atlas_region_new ("transform_test", 0, 0, 32, 32);

    g_assert_false (lrg_atlas_region_is_rotated (region));
    g_assert_false (lrg_atlas_region_is_flipped_x (region));
    g_assert_false (lrg_atlas_region_is_flipped_y (region));

    lrg_atlas_region_set_rotated (region, TRUE);
    lrg_atlas_region_set_flipped_x (region, TRUE);
    lrg_atlas_region_set_flipped_y (region, TRUE);

    g_assert_true (lrg_atlas_region_is_rotated (region));
    g_assert_true (lrg_atlas_region_is_flipped_x (region));
    g_assert_true (lrg_atlas_region_is_flipped_y (region));

    lrg_atlas_region_free (region);
}

/* ========================================================================== */
/*                          Texture Atlas Tests                               */
/* ========================================================================== */

typedef struct
{
    LrgTextureAtlas *atlas;
} AtlasFixture;

static void
atlas_fixture_set_up (AtlasFixture  *fixture,
                      gconstpointer  user_data)
{
    fixture->atlas = lrg_texture_atlas_new ("test_atlas");
    lrg_texture_atlas_set_size (fixture->atlas, 256, 256);
}

static void
atlas_fixture_tear_down (AtlasFixture  *fixture,
                         gconstpointer  user_data)
{
    g_object_unref (fixture->atlas);
}

static void
test_texture_atlas_new (AtlasFixture  *fixture,
                        gconstpointer  user_data)
{
    g_assert_nonnull (fixture->atlas);
    g_assert_cmpstr (lrg_texture_atlas_get_name (fixture->atlas), ==, "test_atlas");
    g_assert_cmpint (lrg_texture_atlas_get_width (fixture->atlas), ==, 256);
    g_assert_cmpint (lrg_texture_atlas_get_height (fixture->atlas), ==, 256);
    g_assert_cmpuint (lrg_texture_atlas_get_region_count (fixture->atlas), ==, 0);
}

static void
test_texture_atlas_add_region (AtlasFixture  *fixture,
                               gconstpointer  user_data)
{
    LrgAtlasRegion *region;
    LrgAtlasRegion *retrieved;

    region = lrg_atlas_region_new ("sprite1", 0, 0, 32, 32);
    lrg_texture_atlas_add_region (fixture->atlas, region);

    g_assert_cmpuint (lrg_texture_atlas_get_region_count (fixture->atlas), ==, 1);
    g_assert_true (lrg_texture_atlas_has_region (fixture->atlas, "sprite1"));

    retrieved = lrg_texture_atlas_get_region (fixture->atlas, "sprite1");
    g_assert_nonnull (retrieved);
    g_assert_cmpstr (lrg_atlas_region_get_name (retrieved), ==, "sprite1");
}

static void
test_texture_atlas_add_region_rect (AtlasFixture  *fixture,
                                    gconstpointer  user_data)
{
    LrgAtlasRegion *region;

    region = lrg_texture_atlas_add_region_rect (fixture->atlas, "player",
                                                 64, 0, 32, 64);

    g_assert_nonnull (region);
    g_assert_cmpuint (lrg_texture_atlas_get_region_count (fixture->atlas), ==, 1);

    /* UV should be calculated automatically */
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_u1 (region), 64.0f / 256.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_v1 (region), 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_u2 (region), 96.0f / 256.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_v2 (region), 64.0f / 256.0f, 0.001f);
}

static void
test_texture_atlas_remove_region (AtlasFixture  *fixture,
                                  gconstpointer  user_data)
{
    gboolean removed;

    lrg_texture_atlas_add_region_rect (fixture->atlas, "sprite1", 0, 0, 16, 16);
    lrg_texture_atlas_add_region_rect (fixture->atlas, "sprite2", 16, 0, 16, 16);

    g_assert_cmpuint (lrg_texture_atlas_get_region_count (fixture->atlas), ==, 2);

    removed = lrg_texture_atlas_remove_region (fixture->atlas, "sprite1");
    g_assert_true (removed);
    g_assert_cmpuint (lrg_texture_atlas_get_region_count (fixture->atlas), ==, 1);
    g_assert_false (lrg_texture_atlas_has_region (fixture->atlas, "sprite1"));
    g_assert_true (lrg_texture_atlas_has_region (fixture->atlas, "sprite2"));
}

static void
test_texture_atlas_clear_regions (AtlasFixture  *fixture,
                                  gconstpointer  user_data)
{
    lrg_texture_atlas_add_region_rect (fixture->atlas, "a", 0, 0, 16, 16);
    lrg_texture_atlas_add_region_rect (fixture->atlas, "b", 16, 0, 16, 16);
    lrg_texture_atlas_add_region_rect (fixture->atlas, "c", 32, 0, 16, 16);

    g_assert_cmpuint (lrg_texture_atlas_get_region_count (fixture->atlas), ==, 3);

    lrg_texture_atlas_clear_regions (fixture->atlas);

    g_assert_cmpuint (lrg_texture_atlas_get_region_count (fixture->atlas), ==, 0);
}

static void
test_texture_atlas_get_region_names (AtlasFixture  *fixture,
                                     gconstpointer  user_data)
{
    GPtrArray *names;

    lrg_texture_atlas_add_region_rect (fixture->atlas, "alpha", 0, 0, 16, 16);
    lrg_texture_atlas_add_region_rect (fixture->atlas, "beta", 16, 0, 16, 16);

    names = lrg_texture_atlas_get_region_names (fixture->atlas);

    g_assert_nonnull (names);
    g_assert_cmpuint (names->len, ==, 2);

    g_ptr_array_unref (names);
}

static void
test_texture_atlas_recalculate_uvs (AtlasFixture  *fixture,
                                    gconstpointer  user_data)
{
    LrgAtlasRegion *region;

    /* Add region without UVs */
    region = lrg_atlas_region_new ("sprite", 128, 128, 64, 64);
    lrg_texture_atlas_add_region (fixture->atlas, region);

    /* UVs should be zero initially */
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_u1 (region), 0.0f, 0.001f);

    lrg_texture_atlas_recalculate_uvs (fixture->atlas);

    /* Now UVs should be correct */
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_u1 (region), 0.5f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_v1 (region), 0.5f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_u2 (region), 0.75f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_atlas_region_get_v2 (region), 0.75f, 0.001f);
}

/* ========================================================================== */
/*                          Sprite Sheet Tests                                */
/* ========================================================================== */

typedef struct
{
    LrgSpriteSheet *sheet;
} SpriteSheetFixture;

static void
sprite_sheet_fixture_set_up (SpriteSheetFixture *fixture,
                             gconstpointer       user_data)
{
    fixture->sheet = lrg_sprite_sheet_new ("test_sheet");
    lrg_sprite_sheet_set_texture_size (fixture->sheet, 256, 256);
}

static void
sprite_sheet_fixture_tear_down (SpriteSheetFixture *fixture,
                                gconstpointer       user_data)
{
    g_object_unref (fixture->sheet);
}

static void
test_sprite_sheet_new (SpriteSheetFixture *fixture,
                       gconstpointer       user_data)
{
    g_assert_nonnull (fixture->sheet);
    g_assert_cmpstr (lrg_sprite_sheet_get_name (fixture->sheet), ==, "test_sheet");
    g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (fixture->sheet), ==, 0);
}

static void
test_sprite_sheet_new_from_grid (void)
{
    LrgSpriteSheet *sheet;

    sheet = lrg_sprite_sheet_new_from_grid ("player_walk", "player.png",
                                            32, 32, 8, 4);

    g_assert_nonnull (sheet);
    g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (sheet), ==, 8);
    g_assert_cmpint (lrg_sprite_sheet_get_format (sheet), ==, LRG_SPRITE_SHEET_FORMAT_GRID);

    g_object_unref (sheet);
}

static void
test_sprite_sheet_add_frame (SpriteSheetFixture *fixture,
                             gconstpointer       user_data)
{
    guint index;
    LrgAtlasRegion *frame;

    index = lrg_sprite_sheet_add_frame_rect (fixture->sheet, "idle_0", 0, 0, 32, 32);
    g_assert_cmpuint (index, ==, 0);

    index = lrg_sprite_sheet_add_frame_rect (fixture->sheet, "idle_1", 32, 0, 32, 32);
    g_assert_cmpuint (index, ==, 1);

    g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (fixture->sheet), ==, 2);

    frame = lrg_sprite_sheet_get_frame (fixture->sheet, 0);
    g_assert_nonnull (frame);
    g_assert_cmpstr (lrg_atlas_region_get_name (frame), ==, "idle_0");

    frame = lrg_sprite_sheet_get_frame_by_name (fixture->sheet, "idle_1");
    g_assert_nonnull (frame);
}

static void
test_sprite_sheet_remove_frame (SpriteSheetFixture *fixture,
                                gconstpointer       user_data)
{
    gboolean removed;

    lrg_sprite_sheet_add_frame_rect (fixture->sheet, "frame0", 0, 0, 32, 32);
    lrg_sprite_sheet_add_frame_rect (fixture->sheet, "frame1", 32, 0, 32, 32);

    g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (fixture->sheet), ==, 2);

    removed = lrg_sprite_sheet_remove_frame (fixture->sheet, 0);
    g_assert_true (removed);
    g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (fixture->sheet), ==, 1);

    /* frame1 is now at index 0 */
    g_assert_nonnull (lrg_sprite_sheet_get_frame_by_name (fixture->sheet, "frame1"));
}

static void
test_sprite_sheet_generate_grid (SpriteSheetFixture *fixture,
                                 gconstpointer       user_data)
{
    guint count;

    count = lrg_sprite_sheet_generate_grid (fixture->sheet,
                                            32, 32,  /* frame size */
                                            8, 8,    /* columns, rows */
                                            0,       /* padding */
                                            0, 0);   /* offset */

    g_assert_cmpuint (count, ==, 64);
    g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (fixture->sheet), ==, 64);
}

static void
test_sprite_sheet_define_animation (SpriteSheetFixture *fixture,
                                    gconstpointer       user_data)
{
    gboolean defined;

    /* Generate frames first */
    lrg_sprite_sheet_generate_grid (fixture->sheet, 32, 32, 4, 4, 0, 0, 0);

    defined = lrg_sprite_sheet_define_animation (fixture->sheet, "walk",
                                                 0, 3, 0.1f, TRUE);
    g_assert_true (defined);
    g_assert_true (lrg_sprite_sheet_has_animation (fixture->sheet, "walk"));
    g_assert_cmpuint (lrg_sprite_sheet_get_animation_frame_count (fixture->sheet, "walk"), ==, 4);
    g_assert_cmpfloat_with_epsilon (lrg_sprite_sheet_get_animation_duration (fixture->sheet, "walk"),
                                    0.4f, 0.001f);
}

static void
test_sprite_sheet_define_animation_frames (SpriteSheetFixture *fixture,
                                           gconstpointer       user_data)
{
    gboolean defined;
    guint frames[] = { 0, 2, 4, 6, 4, 2 };  /* Ping-pong style */

    /* Generate frames first */
    lrg_sprite_sheet_generate_grid (fixture->sheet, 32, 32, 4, 2, 0, 0, 0);

    defined = lrg_sprite_sheet_define_animation_frames (fixture->sheet, "bounce",
                                                        frames, 6, 0.1f, TRUE);
    g_assert_true (defined);
    g_assert_cmpuint (lrg_sprite_sheet_get_animation_frame_count (fixture->sheet, "bounce"), ==, 6);
}

static void
test_sprite_sheet_get_animation_frame (SpriteSheetFixture *fixture,
                                       gconstpointer       user_data)
{
    LrgAtlasRegion *frame;

    /* Generate 4 frames */
    lrg_sprite_sheet_generate_grid (fixture->sheet, 32, 32, 4, 1, 0, 0, 0);
    lrg_sprite_sheet_define_animation (fixture->sheet, "anim", 0, 3, 0.25f, TRUE);

    /* At time 0, should be frame 0 */
    frame = lrg_sprite_sheet_get_animation_frame (fixture->sheet, "anim", 0.0f);
    g_assert_nonnull (frame);
    g_assert_cmpint (lrg_atlas_region_get_x (frame), ==, 0);

    /* At time 0.25, should be frame 1 */
    frame = lrg_sprite_sheet_get_animation_frame (fixture->sheet, "anim", 0.25f);
    g_assert_nonnull (frame);
    g_assert_cmpint (lrg_atlas_region_get_x (frame), ==, 32);

    /* At time 0.5, should be frame 2 */
    frame = lrg_sprite_sheet_get_animation_frame (fixture->sheet, "anim", 0.5f);
    g_assert_nonnull (frame);
    g_assert_cmpint (lrg_atlas_region_get_x (frame), ==, 64);

    /* At time 1.0 (looping), should be back to frame 0 */
    frame = lrg_sprite_sheet_get_animation_frame (fixture->sheet, "anim", 1.0f);
    g_assert_nonnull (frame);
    g_assert_cmpint (lrg_atlas_region_get_x (frame), ==, 0);
}

/* ========================================================================== */
/*                           Nine-Slice Tests                                 */
/* ========================================================================== */

typedef struct
{
    LrgNineSlice *nine_slice;
    LrgAtlasRegion *source;
} NineSliceFixture;

static void
nine_slice_fixture_set_up (NineSliceFixture *fixture,
                           gconstpointer     user_data)
{
    fixture->source = lrg_atlas_region_new_with_uv ("panel", 0, 0, 48, 48,
                                                    0.0f, 0.0f, 0.375f, 0.375f);
    fixture->nine_slice = lrg_nine_slice_new_from_region ("test_panel",
                                                          fixture->source,
                                                          16, 16, 16, 16);
}

static void
nine_slice_fixture_tear_down (NineSliceFixture *fixture,
                              gconstpointer     user_data)
{
    lrg_atlas_region_free (fixture->source);
    g_object_unref (fixture->nine_slice);
}

static void
test_nine_slice_new (void)
{
    LrgNineSlice *ns;

    ns = lrg_nine_slice_new ("empty");

    g_assert_nonnull (ns);
    g_assert_cmpstr (lrg_nine_slice_get_name (ns), ==, "empty");
    g_assert_null (lrg_nine_slice_get_source_region (ns));
    g_assert_cmpint (lrg_nine_slice_get_border_left (ns), ==, 0);
    g_assert_cmpint (lrg_nine_slice_get_mode (ns), ==, LRG_NINE_SLICE_MODE_STRETCH);

    g_object_unref (ns);
}

static void
test_nine_slice_from_region (NineSliceFixture *fixture,
                             gconstpointer     user_data)
{
    g_assert_nonnull (fixture->nine_slice);
    g_assert_nonnull (lrg_nine_slice_get_source_region (fixture->nine_slice));

    g_assert_cmpint (lrg_nine_slice_get_border_left (fixture->nine_slice), ==, 16);
    g_assert_cmpint (lrg_nine_slice_get_border_right (fixture->nine_slice), ==, 16);
    g_assert_cmpint (lrg_nine_slice_get_border_top (fixture->nine_slice), ==, 16);
    g_assert_cmpint (lrg_nine_slice_get_border_bottom (fixture->nine_slice), ==, 16);
}

static void
test_nine_slice_borders (NineSliceFixture *fixture,
                         gconstpointer     user_data)
{
    gint left, right, top, bottom;

    lrg_nine_slice_set_borders (fixture->nine_slice, 8, 12, 10, 14);
    lrg_nine_slice_get_borders (fixture->nine_slice, &left, &right, &top, &bottom);

    g_assert_cmpint (left, ==, 8);
    g_assert_cmpint (right, ==, 12);
    g_assert_cmpint (top, ==, 10);
    g_assert_cmpint (bottom, ==, 14);
}

static void
test_nine_slice_uniform_border (NineSliceFixture *fixture,
                                gconstpointer     user_data)
{
    lrg_nine_slice_set_uniform_border (fixture->nine_slice, 5);

    g_assert_cmpint (lrg_nine_slice_get_border_left (fixture->nine_slice), ==, 5);
    g_assert_cmpint (lrg_nine_slice_get_border_right (fixture->nine_slice), ==, 5);
    g_assert_cmpint (lrg_nine_slice_get_border_top (fixture->nine_slice), ==, 5);
    g_assert_cmpint (lrg_nine_slice_get_border_bottom (fixture->nine_slice), ==, 5);
}

static void
test_nine_slice_min_size (NineSliceFixture *fixture,
                          gconstpointer     user_data)
{
    /* With 16px borders on each side */
    g_assert_cmpint (lrg_nine_slice_get_min_width (fixture->nine_slice), ==, 32);  /* 16 + 16 */
    g_assert_cmpint (lrg_nine_slice_get_min_height (fixture->nine_slice), ==, 32); /* 16 + 16 */
}

static void
test_nine_slice_center_size (NineSliceFixture *fixture,
                             gconstpointer     user_data)
{
    /* Source is 48x48, borders are 16 each side, center is 16x16 */
    g_assert_cmpint (lrg_nine_slice_get_center_width (fixture->nine_slice), ==, 16);
    g_assert_cmpint (lrg_nine_slice_get_center_height (fixture->nine_slice), ==, 16);
}

static void
test_nine_slice_get_patch_rect (NineSliceFixture *fixture,
                                gconstpointer     user_data)
{
    gint x, y, w, h;
    gboolean success;

    /* Test top-left patch */
    success = lrg_nine_slice_get_patch_rect (fixture->nine_slice,
                                             LRG_NINE_SLICE_PATCH_TOP_LEFT,
                                             &x, &y, &w, &h);
    g_assert_true (success);
    g_assert_cmpint (x, ==, 0);
    g_assert_cmpint (y, ==, 0);
    g_assert_cmpint (w, ==, 16);
    g_assert_cmpint (h, ==, 16);

    /* Test center patch */
    success = lrg_nine_slice_get_patch_rect (fixture->nine_slice,
                                             LRG_NINE_SLICE_PATCH_CENTER,
                                             &x, &y, &w, &h);
    g_assert_true (success);
    g_assert_cmpint (x, ==, 16);
    g_assert_cmpint (y, ==, 16);
    g_assert_cmpint (w, ==, 16);
    g_assert_cmpint (h, ==, 16);

    /* Test bottom-right patch */
    success = lrg_nine_slice_get_patch_rect (fixture->nine_slice,
                                             LRG_NINE_SLICE_PATCH_BOTTOM_RIGHT,
                                             &x, &y, &w, &h);
    g_assert_true (success);
    g_assert_cmpint (x, ==, 32);
    g_assert_cmpint (y, ==, 32);
    g_assert_cmpint (w, ==, 16);
    g_assert_cmpint (h, ==, 16);
}

static void
test_nine_slice_calculate_dest_rects (NineSliceFixture *fixture,
                                      gconstpointer     user_data)
{
    gfloat rects[36];

    lrg_nine_slice_calculate_dest_rects (fixture->nine_slice,
                                         0.0f, 0.0f, 100.0f, 80.0f,
                                         rects);

    /* TOP_LEFT should be at 0,0 with 16x16 */
    g_assert_cmpfloat_with_epsilon (rects[0], 0.0f, 0.001f);    /* x */
    g_assert_cmpfloat_with_epsilon (rects[1], 0.0f, 0.001f);    /* y */
    g_assert_cmpfloat_with_epsilon (rects[2], 16.0f, 0.001f);   /* w */
    g_assert_cmpfloat_with_epsilon (rects[3], 16.0f, 0.001f);   /* h */

    /* TOP should stretch horizontally */
    g_assert_cmpfloat_with_epsilon (rects[4], 16.0f, 0.001f);   /* x */
    g_assert_cmpfloat_with_epsilon (rects[5], 0.0f, 0.001f);    /* y */
    g_assert_cmpfloat_with_epsilon (rects[6], 68.0f, 0.001f);   /* w = 100 - 16 - 16 */
    g_assert_cmpfloat_with_epsilon (rects[7], 16.0f, 0.001f);   /* h */

    /* CENTER should fill the middle */
    g_assert_cmpfloat_with_epsilon (rects[16], 16.0f, 0.001f);  /* x */
    g_assert_cmpfloat_with_epsilon (rects[17], 16.0f, 0.001f);  /* y */
    g_assert_cmpfloat_with_epsilon (rects[18], 68.0f, 0.001f);  /* w */
    g_assert_cmpfloat_with_epsilon (rects[19], 48.0f, 0.001f);  /* h = 80 - 16 - 16 */
}

/* ========================================================================== */
/*                          Atlas Packer Tests                                */
/* ========================================================================== */

typedef struct
{
    LrgAtlasPacker *packer;
} PackerFixture;

static void
packer_fixture_set_up (PackerFixture *fixture,
                       gconstpointer  user_data)
{
    fixture->packer = lrg_atlas_packer_new ();
    lrg_atlas_packer_set_max_size (fixture->packer, 512, 512);
    lrg_atlas_packer_set_padding (fixture->packer, 1);
}

static void
packer_fixture_tear_down (PackerFixture *fixture,
                          gconstpointer  user_data)
{
    g_object_unref (fixture->packer);
}

static void
test_atlas_packer_new (void)
{
    LrgAtlasPacker *packer;

    packer = lrg_atlas_packer_new ();

    g_assert_nonnull (packer);
    g_assert_cmpint (lrg_atlas_packer_get_max_width (packer), ==, 4096);
    g_assert_cmpint (lrg_atlas_packer_get_max_height (packer), ==, 4096);
    g_assert_cmpint (lrg_atlas_packer_get_padding (packer), ==, 1);
    g_assert_true (lrg_atlas_packer_get_power_of_two (packer));
    g_assert_false (lrg_atlas_packer_get_allow_rotation (packer));
    g_assert_cmpuint (lrg_atlas_packer_get_image_count (packer), ==, 0);

    g_object_unref (packer);
}

static void
test_atlas_packer_add_image (PackerFixture *fixture,
                             gconstpointer  user_data)
{
    gboolean added;

    added = lrg_atlas_packer_add_image (fixture->packer, "sprite1", 32, 32, NULL);
    g_assert_true (added);
    g_assert_cmpuint (lrg_atlas_packer_get_image_count (fixture->packer), ==, 1);

    added = lrg_atlas_packer_add_image (fixture->packer, "sprite2", 64, 64, NULL);
    g_assert_true (added);
    g_assert_cmpuint (lrg_atlas_packer_get_image_count (fixture->packer), ==, 2);

    /* Duplicate name should fail (expect warning from add_image) */
    g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "Image 'sprite1' already exists in packer");
    added = lrg_atlas_packer_add_image (fixture->packer, "sprite1", 16, 16, NULL);
    g_test_assert_expected_messages ();
    g_assert_false (added);
    g_assert_cmpuint (lrg_atlas_packer_get_image_count (fixture->packer), ==, 2);
}

static void
test_atlas_packer_remove_image (PackerFixture *fixture,
                                gconstpointer  user_data)
{
    gboolean removed;

    lrg_atlas_packer_add_image (fixture->packer, "a", 10, 10, NULL);
    lrg_atlas_packer_add_image (fixture->packer, "b", 20, 20, NULL);

    removed = lrg_atlas_packer_remove_image (fixture->packer, "a");
    g_assert_true (removed);
    g_assert_cmpuint (lrg_atlas_packer_get_image_count (fixture->packer), ==, 1);

    removed = lrg_atlas_packer_remove_image (fixture->packer, "nonexistent");
    g_assert_false (removed);
}

static void
test_atlas_packer_pack_empty (PackerFixture *fixture,
                              gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean success;

    success = lrg_atlas_packer_pack (fixture->packer, &error);
    g_assert_false (success);
    g_assert_nonnull (error);
}

static void
test_atlas_packer_pack_single (PackerFixture *fixture,
                               gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean success;
    gint x, y;
    gboolean rotated;

    lrg_atlas_packer_add_image (fixture->packer, "single", 100, 50, NULL);

    success = lrg_atlas_packer_pack (fixture->packer, &error);
    g_assert_true (success);
    g_assert_null (error);

    /* Result should be power of two */
    g_assert_cmpint (lrg_atlas_packer_get_packed_width (fixture->packer), ==, 128);
    g_assert_cmpint (lrg_atlas_packer_get_packed_height (fixture->packer), ==, 64);

    success = lrg_atlas_packer_get_image_position (fixture->packer, "single", &x, &y, &rotated);
    g_assert_true (success);
    g_assert_cmpint (x, ==, 0);
    g_assert_cmpint (y, ==, 0);
    g_assert_false (rotated);
}

static void
test_atlas_packer_pack_multiple (PackerFixture *fixture,
                                 gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean success;

    lrg_atlas_packer_add_image (fixture->packer, "img1", 64, 64, NULL);
    lrg_atlas_packer_add_image (fixture->packer, "img2", 64, 64, NULL);
    lrg_atlas_packer_add_image (fixture->packer, "img3", 64, 64, NULL);
    lrg_atlas_packer_add_image (fixture->packer, "img4", 64, 64, NULL);

    success = lrg_atlas_packer_pack (fixture->packer, &error);
    g_assert_true (success);
    g_assert_null (error);

    /* All images should fit */
    g_assert_cmpint (lrg_atlas_packer_get_packed_width (fixture->packer), >=, 64);
    g_assert_cmpint (lrg_atlas_packer_get_packed_height (fixture->packer), >=, 64);

    /* Efficiency should be reasonable (> 20%, depends on packing algorithm) */
    g_assert_cmpfloat (lrg_atlas_packer_get_efficiency (fixture->packer), >, 0.2f);
}

static void
test_atlas_packer_create_atlas (PackerFixture *fixture,
                                gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    LrgTextureAtlas *atlas;

    lrg_atlas_packer_add_image (fixture->packer, "sprite_a", 32, 32, NULL);
    lrg_atlas_packer_add_image (fixture->packer, "sprite_b", 48, 48, NULL);
    lrg_atlas_packer_add_image (fixture->packer, "sprite_c", 16, 64, NULL);

    lrg_atlas_packer_pack (fixture->packer, &error);
    g_assert_null (error);

    atlas = lrg_atlas_packer_create_atlas (fixture->packer, "packed_atlas");
    g_assert_nonnull (atlas);

    g_assert_cmpstr (lrg_texture_atlas_get_name (atlas), ==, "packed_atlas");
    g_assert_cmpuint (lrg_texture_atlas_get_region_count (atlas), ==, 3);
    g_assert_true (lrg_texture_atlas_has_region (atlas, "sprite_a"));
    g_assert_true (lrg_texture_atlas_has_region (atlas, "sprite_b"));
    g_assert_true (lrg_texture_atlas_has_region (atlas, "sprite_c"));

    g_object_unref (atlas);
}

static void
test_atlas_packer_user_data (PackerFixture *fixture,
                             gconstpointer  user_data)
{
    gint my_data = 42;
    gpointer retrieved;

    lrg_atlas_packer_add_image (fixture->packer, "with_data", 32, 32, &my_data);

    retrieved = lrg_atlas_packer_get_image_user_data (fixture->packer, "with_data");
    g_assert_nonnull (retrieved);
    g_assert_cmpint (*(gint*)retrieved, ==, 42);

    retrieved = lrg_atlas_packer_get_image_user_data (fixture->packer, "nonexistent");
    g_assert_null (retrieved);
}

static void
test_atlas_packer_no_power_of_two (PackerFixture *fixture,
                                   gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;

    lrg_atlas_packer_set_power_of_two (fixture->packer, FALSE);
    lrg_atlas_packer_set_padding (fixture->packer, 0);
    lrg_atlas_packer_add_image (fixture->packer, "exact", 100, 50, NULL);

    lrg_atlas_packer_pack (fixture->packer, &error);

    /* Should be exactly 100x50 without power-of-two rounding */
    g_assert_cmpint (lrg_atlas_packer_get_packed_width (fixture->packer), ==, 100);
    g_assert_cmpint (lrg_atlas_packer_get_packed_height (fixture->packer), ==, 50);
}

/* ========================================================================== */
/*                              Main Entry                                    */
/* ========================================================================== */

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Atlas Region tests */
    g_test_add_func ("/atlas/region/new", test_atlas_region_new);
    g_test_add_func ("/atlas/region/new_with_uv", test_atlas_region_new_with_uv);
    g_test_add_func ("/atlas/region/copy", test_atlas_region_copy);
    g_test_add_func ("/atlas/region/calculate_uv", test_atlas_region_calculate_uv);
    g_test_add_func ("/atlas/region/get_rect", test_atlas_region_get_rect);
    g_test_add_func ("/atlas/region/transforms", test_atlas_region_transforms);

    /* Texture Atlas tests */
    g_test_add ("/atlas/texture/new", AtlasFixture, NULL,
                atlas_fixture_set_up, test_texture_atlas_new, atlas_fixture_tear_down);
    g_test_add ("/atlas/texture/add_region", AtlasFixture, NULL,
                atlas_fixture_set_up, test_texture_atlas_add_region, atlas_fixture_tear_down);
    g_test_add ("/atlas/texture/add_region_rect", AtlasFixture, NULL,
                atlas_fixture_set_up, test_texture_atlas_add_region_rect, atlas_fixture_tear_down);
    g_test_add ("/atlas/texture/remove_region", AtlasFixture, NULL,
                atlas_fixture_set_up, test_texture_atlas_remove_region, atlas_fixture_tear_down);
    g_test_add ("/atlas/texture/clear_regions", AtlasFixture, NULL,
                atlas_fixture_set_up, test_texture_atlas_clear_regions, atlas_fixture_tear_down);
    g_test_add ("/atlas/texture/get_region_names", AtlasFixture, NULL,
                atlas_fixture_set_up, test_texture_atlas_get_region_names, atlas_fixture_tear_down);
    g_test_add ("/atlas/texture/recalculate_uvs", AtlasFixture, NULL,
                atlas_fixture_set_up, test_texture_atlas_recalculate_uvs, atlas_fixture_tear_down);

    /* Sprite Sheet tests */
    g_test_add ("/atlas/sprite_sheet/new", SpriteSheetFixture, NULL,
                sprite_sheet_fixture_set_up, test_sprite_sheet_new, sprite_sheet_fixture_tear_down);
    g_test_add_func ("/atlas/sprite_sheet/new_from_grid", test_sprite_sheet_new_from_grid);
    g_test_add ("/atlas/sprite_sheet/add_frame", SpriteSheetFixture, NULL,
                sprite_sheet_fixture_set_up, test_sprite_sheet_add_frame, sprite_sheet_fixture_tear_down);
    g_test_add ("/atlas/sprite_sheet/remove_frame", SpriteSheetFixture, NULL,
                sprite_sheet_fixture_set_up, test_sprite_sheet_remove_frame, sprite_sheet_fixture_tear_down);
    g_test_add ("/atlas/sprite_sheet/generate_grid", SpriteSheetFixture, NULL,
                sprite_sheet_fixture_set_up, test_sprite_sheet_generate_grid, sprite_sheet_fixture_tear_down);
    g_test_add ("/atlas/sprite_sheet/define_animation", SpriteSheetFixture, NULL,
                sprite_sheet_fixture_set_up, test_sprite_sheet_define_animation, sprite_sheet_fixture_tear_down);
    g_test_add ("/atlas/sprite_sheet/define_animation_frames", SpriteSheetFixture, NULL,
                sprite_sheet_fixture_set_up, test_sprite_sheet_define_animation_frames, sprite_sheet_fixture_tear_down);
    g_test_add ("/atlas/sprite_sheet/get_animation_frame", SpriteSheetFixture, NULL,
                sprite_sheet_fixture_set_up, test_sprite_sheet_get_animation_frame, sprite_sheet_fixture_tear_down);

    /* Nine-Slice tests */
    g_test_add_func ("/atlas/nine_slice/new", test_nine_slice_new);
    g_test_add ("/atlas/nine_slice/from_region", NineSliceFixture, NULL,
                nine_slice_fixture_set_up, test_nine_slice_from_region, nine_slice_fixture_tear_down);
    g_test_add ("/atlas/nine_slice/borders", NineSliceFixture, NULL,
                nine_slice_fixture_set_up, test_nine_slice_borders, nine_slice_fixture_tear_down);
    g_test_add ("/atlas/nine_slice/uniform_border", NineSliceFixture, NULL,
                nine_slice_fixture_set_up, test_nine_slice_uniform_border, nine_slice_fixture_tear_down);
    g_test_add ("/atlas/nine_slice/min_size", NineSliceFixture, NULL,
                nine_slice_fixture_set_up, test_nine_slice_min_size, nine_slice_fixture_tear_down);
    g_test_add ("/atlas/nine_slice/center_size", NineSliceFixture, NULL,
                nine_slice_fixture_set_up, test_nine_slice_center_size, nine_slice_fixture_tear_down);
    g_test_add ("/atlas/nine_slice/get_patch_rect", NineSliceFixture, NULL,
                nine_slice_fixture_set_up, test_nine_slice_get_patch_rect, nine_slice_fixture_tear_down);
    g_test_add ("/atlas/nine_slice/calculate_dest_rects", NineSliceFixture, NULL,
                nine_slice_fixture_set_up, test_nine_slice_calculate_dest_rects, nine_slice_fixture_tear_down);

    /* Atlas Packer tests */
    g_test_add_func ("/atlas/packer/new", test_atlas_packer_new);
    g_test_add ("/atlas/packer/add_image", PackerFixture, NULL,
                packer_fixture_set_up, test_atlas_packer_add_image, packer_fixture_tear_down);
    g_test_add ("/atlas/packer/remove_image", PackerFixture, NULL,
                packer_fixture_set_up, test_atlas_packer_remove_image, packer_fixture_tear_down);
    g_test_add ("/atlas/packer/pack_empty", PackerFixture, NULL,
                packer_fixture_set_up, test_atlas_packer_pack_empty, packer_fixture_tear_down);
    g_test_add ("/atlas/packer/pack_single", PackerFixture, NULL,
                packer_fixture_set_up, test_atlas_packer_pack_single, packer_fixture_tear_down);
    g_test_add ("/atlas/packer/pack_multiple", PackerFixture, NULL,
                packer_fixture_set_up, test_atlas_packer_pack_multiple, packer_fixture_tear_down);
    g_test_add ("/atlas/packer/create_atlas", PackerFixture, NULL,
                packer_fixture_set_up, test_atlas_packer_create_atlas, packer_fixture_tear_down);
    g_test_add ("/atlas/packer/user_data", PackerFixture, NULL,
                packer_fixture_set_up, test_atlas_packer_user_data, packer_fixture_tear_down);
    g_test_add ("/atlas/packer/no_power_of_two", PackerFixture, NULL,
                packer_fixture_set_up, test_atlas_packer_no_power_of_two, packer_fixture_tear_down);

    return g_test_run ();
}
