/* test-tilemap.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for Tilemap module (Tileset, TilemapLayer, Tilemap).
 */

#include <glib.h>
#include <libregnum.h>
#include <raylib.h>  /* For SetConfigFlags to create hidden window */

/* Helper macro to skip graphics-dependent tests */
#define SKIP_IF_NO_GRAPHICS() \
    do { \
        if (!graphics_available) { \
            g_test_skip ("Graphics context not available"); \
            return; \
        } \
    } while (0)

/* ==========================================================================
 * Graphics Context for Testing
 *
 * Some tests require a graphics context (for texture creation).
 * We create a hidden window to initialize OpenGL for these tests.
 * In headless CI environments, these tests are skipped.
 * ========================================================================== */

static gboolean graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
    const gchar *display = NULL;
    const gchar *wayland = NULL;

    /*
     * Check for display availability first.
     * No point trying to create a window without a display.
     */
    display = g_getenv ("DISPLAY");
    wayland = g_getenv ("WAYLAND_DISPLAY");

    if ((display == NULL || display[0] == '\0') &&
        (wayland == NULL || wayland[0] == '\0'))
    {
        return FALSE;
    }

    /*
     * Create a small hidden window to initialize the OpenGL context.
     * This is required before any texture operations can work.
     * We use raylib's SetConfigFlags directly to set the hidden flag
     * before window creation.
     */
    SetConfigFlags (FLAG_WINDOW_HIDDEN);
    test_window = grl_window_new (1, 1, "tilemap-test");

    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }

    return TRUE;
}

static void
cleanup_graphics_context (void)
{
    g_clear_object (&test_window);
}

/* ==========================================================================
 * Mock Texture for Testing
 *
 * Create textures programmatically for testing tileset functionality.
 * Uses graylib's image generation to create a checkerboard pattern.
 * ========================================================================== */

static GrlTexture *
create_mock_texture (gint width,
                     gint height)
{
    /*
     * Create a texture using graylib's test image creation.
     * This generates a checkerboard pattern in memory.
     * Width and height should be the texture dimensions we want.
     */
    g_autoptr(GrlColor) color1 = NULL;
    g_autoptr(GrlColor) color2 = NULL;
    g_autoptr(GrlImage) image = NULL;
    GrlTexture         *texture;

    if (!graphics_available)
    {
        return NULL;
    }

    color1 = grl_color_new (255, 255, 255, 255);  /* White */
    color2 = grl_color_new (128, 128, 128, 255);  /* Gray */

    image = grl_image_new_checked (width, height, 8, 8, color1, color2);
    if (image == NULL)
    {
        return NULL;
    }

    texture = grl_texture_new_from_image (image);

    return texture;
}

/* ==========================================================================
 * Test Fixtures - Tileset
 * ========================================================================== */

typedef struct
{
    GrlTexture *texture;
    LrgTileset *tileset;
} TilesetFixture;

static void
tileset_fixture_set_up (TilesetFixture *fixture,
                        gconstpointer   user_data)
{
    if (!graphics_available)
    {
        fixture->texture = NULL;
        fixture->tileset = NULL;
        return;
    }

    /* Create a 128x128 texture with 16x16 tiles = 8x8 = 64 tiles */
    fixture->texture = create_mock_texture (128, 128);
    g_assert_nonnull (fixture->texture);

    fixture->tileset = lrg_tileset_new (fixture->texture, 16, 16);
    g_assert_nonnull (fixture->tileset);
}

static void
tileset_fixture_tear_down (TilesetFixture *fixture,
                           gconstpointer   user_data)
{
    g_clear_object (&fixture->tileset);
    g_clear_object (&fixture->texture);
}

/* ==========================================================================
 * Test Fixtures - TilemapLayer
 * ========================================================================== */

typedef struct
{
    LrgTilemapLayer *layer;
} LayerFixture;

static void
layer_fixture_set_up (LayerFixture  *fixture,
                      gconstpointer  user_data)
{
    fixture->layer = lrg_tilemap_layer_new (10, 8);
    g_assert_nonnull (fixture->layer);
}

static void
layer_fixture_tear_down (LayerFixture  *fixture,
                         gconstpointer  user_data)
{
    g_clear_object (&fixture->layer);
}

/* ==========================================================================
 * Test Fixtures - Tilemap
 * ========================================================================== */

typedef struct
{
    GrlTexture      *texture;
    LrgTileset      *tileset;
    LrgTilemap      *tilemap;
    LrgTilemapLayer *layer1;
    LrgTilemapLayer *layer2;
} TilemapFixture;

static void
tilemap_fixture_set_up (TilemapFixture *fixture,
                        gconstpointer   user_data)
{
    if (!graphics_available)
    {
        fixture->texture = NULL;
        fixture->tileset = NULL;
        fixture->tilemap = NULL;
        fixture->layer1 = NULL;
        fixture->layer2 = NULL;
        return;
    }

    fixture->texture = create_mock_texture (128, 128);
    g_assert_nonnull (fixture->texture);

    fixture->tileset = lrg_tileset_new (fixture->texture, 16, 16);
    g_assert_nonnull (fixture->tileset);

    fixture->tilemap = lrg_tilemap_new (fixture->tileset);
    g_assert_nonnull (fixture->tilemap);

    fixture->layer1 = lrg_tilemap_layer_new (10, 8);
    fixture->layer2 = lrg_tilemap_layer_new (10, 8);
    g_assert_nonnull (fixture->layer1);
    g_assert_nonnull (fixture->layer2);
}

static void
tilemap_fixture_tear_down (TilemapFixture *fixture,
                           gconstpointer   user_data)
{
    g_clear_object (&fixture->layer2);
    g_clear_object (&fixture->layer1);
    g_clear_object (&fixture->tilemap);
    g_clear_object (&fixture->tileset);
    g_clear_object (&fixture->texture);
}

/* ==========================================================================
 * Test Cases - Tileset Construction
 * ========================================================================== */

static void
test_tileset_new (void)
{
    g_autoptr(GrlTexture) texture = NULL;
    g_autoptr(LrgTileset) tileset = NULL;

    SKIP_IF_NO_GRAPHICS ();

    texture = create_mock_texture (128, 128);
    g_assert_nonnull (texture);

    tileset = lrg_tileset_new (texture, 16, 16);

    g_assert_nonnull (tileset);
    g_assert_true (LRG_IS_TILESET (tileset));
    g_assert_true (lrg_tileset_get_texture (tileset) == texture);
    g_assert_cmpuint (lrg_tileset_get_tile_width (tileset), ==, 16);
    g_assert_cmpuint (lrg_tileset_get_tile_height (tileset), ==, 16);
    g_assert_cmpuint (lrg_tileset_get_columns (tileset), ==, 8);
    g_assert_cmpuint (lrg_tileset_get_rows (tileset), ==, 8);
    g_assert_cmpuint (lrg_tileset_get_tile_count (tileset), ==, 64);
}

static void
test_tileset_dimensions (TilesetFixture *fixture,
                         gconstpointer   user_data)
{
    SKIP_IF_NO_GRAPHICS ();

    /* Verify tile dimensions */
    g_assert_cmpuint (lrg_tileset_get_tile_width (fixture->tileset), ==, 16);
    g_assert_cmpuint (lrg_tileset_get_tile_height (fixture->tileset), ==, 16);

    /* Verify grid dimensions: 128px / 16px = 8 columns/rows */
    g_assert_cmpuint (lrg_tileset_get_columns (fixture->tileset), ==, 8);
    g_assert_cmpuint (lrg_tileset_get_rows (fixture->tileset), ==, 8);

    /* Verify total tile count: 8 * 8 = 64 */
    g_assert_cmpuint (lrg_tileset_get_tile_count (fixture->tileset), ==, 64);
}

/* ==========================================================================
 * Test Cases - Tileset Tile Rectangles
 * ========================================================================== */

static void
test_tileset_tile_rect (TilesetFixture *fixture,
                        gconstpointer   user_data)
{
    g_autoptr(GrlRectangle) rect = NULL;

    SKIP_IF_NO_GRAPHICS ();

    /* Test tile 0 (top-left corner) */
    rect = lrg_tileset_get_tile_rect (fixture->tileset, 0);
    g_assert_nonnull (rect);
    g_assert_cmpfloat_with_epsilon (rect->x, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (rect->y, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (rect->width, 16.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (rect->height, 16.0f, 0.0001f);

    /* Test tile 1 (second in first row) */
    g_clear_pointer (&rect, grl_rectangle_free);
    rect = lrg_tileset_get_tile_rect (fixture->tileset, 1);
    g_assert_nonnull (rect);
    g_assert_cmpfloat_with_epsilon (rect->x, 16.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (rect->y, 0.0f, 0.0001f);

    /* Test tile 8 (first in second row, since columns=8) */
    g_clear_pointer (&rect, grl_rectangle_free);
    rect = lrg_tileset_get_tile_rect (fixture->tileset, 8);
    g_assert_nonnull (rect);
    g_assert_cmpfloat_with_epsilon (rect->x, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (rect->y, 16.0f, 0.0001f);

    /* Test tile 9 (second in second row) */
    g_clear_pointer (&rect, grl_rectangle_free);
    rect = lrg_tileset_get_tile_rect (fixture->tileset, 9);
    g_assert_nonnull (rect);
    g_assert_cmpfloat_with_epsilon (rect->x, 16.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (rect->y, 16.0f, 0.0001f);
}

static void
test_tileset_tile_rect_to (TilesetFixture *fixture,
                           gconstpointer   user_data)
{
    GrlRectangle rect;
    gboolean     success;

    SKIP_IF_NO_GRAPHICS ();

    /* Test valid tile */
    success = lrg_tileset_get_tile_rect_to (fixture->tileset, 5, &rect);
    g_assert_true (success);
    g_assert_cmpfloat_with_epsilon (rect.x, 80.0f, 0.0001f);  /* 5 % 8 = 5, 5 * 16 = 80 */
    g_assert_cmpfloat_with_epsilon (rect.y, 0.0f, 0.0001f);   /* 5 / 8 = 0, 0 * 16 = 0 */

    /* Test out of bounds */
    success = lrg_tileset_get_tile_rect_to (fixture->tileset, 100, &rect);
    g_assert_false (success);
}

static void
test_tileset_tile_rect_out_of_bounds (TilesetFixture *fixture,
                                      gconstpointer   user_data)
{
    GrlRectangle *rect;

    SKIP_IF_NO_GRAPHICS ();

    /* tile_count is 64, so tile 64+ should be out of bounds */
    rect = lrg_tileset_get_tile_rect (fixture->tileset, 64);
    g_assert_null (rect);

    rect = lrg_tileset_get_tile_rect (fixture->tileset, 100);
    g_assert_null (rect);
}

/* ==========================================================================
 * Test Cases - Tileset Properties
 * ========================================================================== */

static void
test_tileset_properties_default (TilesetFixture *fixture,
                                 gconstpointer   user_data)
{
    LrgTileProperty props;

    SKIP_IF_NO_GRAPHICS ();

    /* All tiles should start with no properties */
    props = lrg_tileset_get_tile_properties (fixture->tileset, 0);
    g_assert_cmpuint (props, ==, LRG_TILE_PROPERTY_NONE);

    props = lrg_tileset_get_tile_properties (fixture->tileset, 10);
    g_assert_cmpuint (props, ==, LRG_TILE_PROPERTY_NONE);
}

static void
test_tileset_properties_set (TilesetFixture *fixture,
                             gconstpointer   user_data)
{
    LrgTileProperty props;

    SKIP_IF_NO_GRAPHICS ();

    /* Set solid property on tile 5 */
    lrg_tileset_set_tile_properties (fixture->tileset, 5, LRG_TILE_PROPERTY_SOLID);
    props = lrg_tileset_get_tile_properties (fixture->tileset, 5);
    g_assert_cmpuint (props, ==, LRG_TILE_PROPERTY_SOLID);

    /* Other tiles should be unaffected */
    props = lrg_tileset_get_tile_properties (fixture->tileset, 6);
    g_assert_cmpuint (props, ==, LRG_TILE_PROPERTY_NONE);

    /* Set multiple properties on tile 10 */
    lrg_tileset_set_tile_properties (fixture->tileset, 10,
                                     LRG_TILE_PROPERTY_SOLID |
                                     LRG_TILE_PROPERTY_HAZARD);
    props = lrg_tileset_get_tile_properties (fixture->tileset, 10);
    g_assert_cmpuint (props, ==, (LRG_TILE_PROPERTY_SOLID | LRG_TILE_PROPERTY_HAZARD));
}

static void
test_tileset_tile_has_property (TilesetFixture *fixture,
                                gconstpointer   user_data)
{
    SKIP_IF_NO_GRAPHICS ();

    /* Set multiple flags */
    lrg_tileset_set_tile_properties (fixture->tileset, 3,
                                     LRG_TILE_PROPERTY_SOLID |
                                     LRG_TILE_PROPERTY_ANIMATED);

    /* Check individual flags */
    g_assert_true (lrg_tileset_tile_has_property (fixture->tileset, 3,
                                                  LRG_TILE_PROPERTY_SOLID));
    g_assert_true (lrg_tileset_tile_has_property (fixture->tileset, 3,
                                                  LRG_TILE_PROPERTY_ANIMATED));
    g_assert_false (lrg_tileset_tile_has_property (fixture->tileset, 3,
                                                   LRG_TILE_PROPERTY_HAZARD));

    /* Check combination */
    g_assert_true (lrg_tileset_tile_has_property (fixture->tileset, 3,
                                                  LRG_TILE_PROPERTY_SOLID |
                                                  LRG_TILE_PROPERTY_ANIMATED));

    /* Tile without properties */
    g_assert_false (lrg_tileset_tile_has_property (fixture->tileset, 0,
                                                   LRG_TILE_PROPERTY_SOLID));
}

/* ==========================================================================
 * Test Cases - TilemapLayer Construction
 * ========================================================================== */

static void
test_layer_new (void)
{
    g_autoptr(LrgTilemapLayer) layer = NULL;

    layer = lrg_tilemap_layer_new (20, 15);

    g_assert_nonnull (layer);
    g_assert_true (LRG_IS_TILEMAP_LAYER (layer));
    g_assert_cmpuint (lrg_tilemap_layer_get_width (layer), ==, 20);
    g_assert_cmpuint (lrg_tilemap_layer_get_height (layer), ==, 15);
}

static void
test_layer_dimensions (LayerFixture  *fixture,
                       gconstpointer  user_data)
{
    g_assert_cmpuint (lrg_tilemap_layer_get_width (fixture->layer), ==, 10);
    g_assert_cmpuint (lrg_tilemap_layer_get_height (fixture->layer), ==, 8);
}

/* ==========================================================================
 * Test Cases - TilemapLayer Tile Access
 * ========================================================================== */

static void
test_layer_get_set (LayerFixture  *fixture,
                    gconstpointer  user_data)
{
    guint tile;

    /* All tiles start as empty */
    tile = lrg_tilemap_layer_get_tile (fixture->layer, 0, 0);
    g_assert_cmpuint (tile, ==, LRG_TILEMAP_EMPTY_TILE);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 5, 3);
    g_assert_cmpuint (tile, ==, LRG_TILEMAP_EMPTY_TILE);

    /* Set some tiles */
    lrg_tilemap_layer_set_tile (fixture->layer, 0, 0, 1);
    lrg_tilemap_layer_set_tile (fixture->layer, 5, 3, 42);
    lrg_tilemap_layer_set_tile (fixture->layer, 9, 7, 99);

    /* Verify tiles */
    tile = lrg_tilemap_layer_get_tile (fixture->layer, 0, 0);
    g_assert_cmpuint (tile, ==, 1);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 5, 3);
    g_assert_cmpuint (tile, ==, 42);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 9, 7);
    g_assert_cmpuint (tile, ==, 99);

    /* Other tiles remain empty */
    tile = lrg_tilemap_layer_get_tile (fixture->layer, 1, 1);
    g_assert_cmpuint (tile, ==, LRG_TILEMAP_EMPTY_TILE);
}

static void
test_layer_get_set_out_of_bounds (LayerFixture  *fixture,
                                  gconstpointer  user_data)
{
    guint tile;

    /* Out of bounds get should return EMPTY_TILE */
    tile = lrg_tilemap_layer_get_tile (fixture->layer, 20, 0);
    g_assert_cmpuint (tile, ==, LRG_TILEMAP_EMPTY_TILE);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 0, 20);
    g_assert_cmpuint (tile, ==, LRG_TILEMAP_EMPTY_TILE);

    /* Out of bounds set should be ignored (no crash) */
    lrg_tilemap_layer_set_tile (fixture->layer, 100, 100, 5);

    /* In-bounds tile should be unaffected */
    tile = lrg_tilemap_layer_get_tile (fixture->layer, 0, 0);
    g_assert_cmpuint (tile, ==, LRG_TILEMAP_EMPTY_TILE);
}

static void
test_layer_fill (LayerFixture  *fixture,
                 gconstpointer  user_data)
{
    guint tile;

    /* Fill entire layer with tile 7 */
    lrg_tilemap_layer_fill (fixture->layer, 7);

    /* Check various positions */
    tile = lrg_tilemap_layer_get_tile (fixture->layer, 0, 0);
    g_assert_cmpuint (tile, ==, 7);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 5, 4);
    g_assert_cmpuint (tile, ==, 7);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 9, 7);
    g_assert_cmpuint (tile, ==, 7);
}

static void
test_layer_fill_rect (LayerFixture  *fixture,
                      gconstpointer  user_data)
{
    guint tile;

    /* Fill a 3x2 rectangle starting at (2,3) with tile 15 */
    lrg_tilemap_layer_fill_rect (fixture->layer, 2, 3, 3, 2, 15);

    /* Check filled area */
    tile = lrg_tilemap_layer_get_tile (fixture->layer, 2, 3);
    g_assert_cmpuint (tile, ==, 15);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 3, 3);
    g_assert_cmpuint (tile, ==, 15);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 4, 3);
    g_assert_cmpuint (tile, ==, 15);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 2, 4);
    g_assert_cmpuint (tile, ==, 15);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 4, 4);
    g_assert_cmpuint (tile, ==, 15);

    /* Check outside filled area */
    tile = lrg_tilemap_layer_get_tile (fixture->layer, 1, 3);
    g_assert_cmpuint (tile, ==, LRG_TILEMAP_EMPTY_TILE);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 5, 3);
    g_assert_cmpuint (tile, ==, LRG_TILEMAP_EMPTY_TILE);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 2, 5);
    g_assert_cmpuint (tile, ==, LRG_TILEMAP_EMPTY_TILE);
}

static void
test_layer_clear (LayerFixture  *fixture,
                  gconstpointer  user_data)
{
    guint tile;

    /* Set some tiles */
    lrg_tilemap_layer_fill (fixture->layer, 10);

    /* Clear */
    lrg_tilemap_layer_clear (fixture->layer);

    /* All should be empty */
    tile = lrg_tilemap_layer_get_tile (fixture->layer, 0, 0);
    g_assert_cmpuint (tile, ==, LRG_TILEMAP_EMPTY_TILE);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 5, 5);
    g_assert_cmpuint (tile, ==, LRG_TILEMAP_EMPTY_TILE);
}

/* ==========================================================================
 * Test Cases - TilemapLayer Properties
 * ========================================================================== */

static void
test_layer_visibility (LayerFixture  *fixture,
                       gconstpointer  user_data)
{
    /* Default is visible */
    g_assert_true (lrg_tilemap_layer_get_visible (fixture->layer));

    /* Hide */
    lrg_tilemap_layer_set_visible (fixture->layer, FALSE);
    g_assert_false (lrg_tilemap_layer_get_visible (fixture->layer));

    /* Show */
    lrg_tilemap_layer_set_visible (fixture->layer, TRUE);
    g_assert_true (lrg_tilemap_layer_get_visible (fixture->layer));
}

static void
test_layer_collision (LayerFixture  *fixture,
                      gconstpointer  user_data)
{
    /* Default is enabled */
    g_assert_true (lrg_tilemap_layer_get_collision_enabled (fixture->layer));

    /* Disable */
    lrg_tilemap_layer_set_collision_enabled (fixture->layer, FALSE);
    g_assert_false (lrg_tilemap_layer_get_collision_enabled (fixture->layer));

    /* Enable */
    lrg_tilemap_layer_set_collision_enabled (fixture->layer, TRUE);
    g_assert_true (lrg_tilemap_layer_get_collision_enabled (fixture->layer));
}

static void
test_layer_parallax (LayerFixture  *fixture,
                     gconstpointer  user_data)
{
    /* Default parallax is 1.0 */
    g_assert_cmpfloat_with_epsilon (lrg_tilemap_layer_get_parallax_x (fixture->layer),
                                    1.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_tilemap_layer_get_parallax_y (fixture->layer),
                                    1.0f, 0.0001f);

    /* Set parallax */
    lrg_tilemap_layer_set_parallax_x (fixture->layer, 0.5f);
    lrg_tilemap_layer_set_parallax_y (fixture->layer, 0.25f);

    g_assert_cmpfloat_with_epsilon (lrg_tilemap_layer_get_parallax_x (fixture->layer),
                                    0.5f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_tilemap_layer_get_parallax_y (fixture->layer),
                                    0.25f, 0.0001f);
}

static void
test_layer_opacity (LayerFixture  *fixture,
                    gconstpointer  user_data)
{
    /* Default opacity is 1.0 */
    g_assert_cmpfloat_with_epsilon (lrg_tilemap_layer_get_opacity (fixture->layer),
                                    1.0f, 0.0001f);

    /* Set opacity */
    lrg_tilemap_layer_set_opacity (fixture->layer, 0.75f);
    g_assert_cmpfloat_with_epsilon (lrg_tilemap_layer_get_opacity (fixture->layer),
                                    0.75f, 0.0001f);

    /* Clamped to valid range */
    lrg_tilemap_layer_set_opacity (fixture->layer, 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_tilemap_layer_get_opacity (fixture->layer),
                                    1.0f, 0.0001f);

    lrg_tilemap_layer_set_opacity (fixture->layer, -0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_tilemap_layer_get_opacity (fixture->layer),
                                    0.0f, 0.0001f);
}

static void
test_layer_name (LayerFixture  *fixture,
                 gconstpointer  user_data)
{
    const gchar *name;

    /* Default name is NULL */
    name = lrg_tilemap_layer_get_name (fixture->layer);
    g_assert_null (name);

    /* Set name */
    lrg_tilemap_layer_set_name (fixture->layer, "background");
    name = lrg_tilemap_layer_get_name (fixture->layer);
    g_assert_cmpstr (name, ==, "background");

    /* Change name */
    lrg_tilemap_layer_set_name (fixture->layer, "foreground");
    name = lrg_tilemap_layer_get_name (fixture->layer);
    g_assert_cmpstr (name, ==, "foreground");

    /* Clear name */
    lrg_tilemap_layer_set_name (fixture->layer, NULL);
    name = lrg_tilemap_layer_get_name (fixture->layer);
    g_assert_null (name);
}

/* ==========================================================================
 * Test Cases - TilemapLayer Data Access
 * ========================================================================== */

static void
test_layer_get_tiles (LayerFixture  *fixture,
                      gconstpointer  user_data)
{
    const guint *tiles;
    gsize        len;

    /* Set some tiles */
    lrg_tilemap_layer_set_tile (fixture->layer, 0, 0, 1);
    lrg_tilemap_layer_set_tile (fixture->layer, 1, 0, 2);
    lrg_tilemap_layer_set_tile (fixture->layer, 0, 1, 10);

    /* Get tile array */
    tiles = lrg_tilemap_layer_get_tiles (fixture->layer, &len);

    g_assert_nonnull (tiles);
    g_assert_cmpuint (len, ==, 10 * 8);  /* width * height */

    /* Check values (row-major order: y * width + x) */
    g_assert_cmpuint (tiles[0], ==, 1);   /* (0,0) */
    g_assert_cmpuint (tiles[1], ==, 2);   /* (1,0) */
    g_assert_cmpuint (tiles[10], ==, 10); /* (0,1) -> 1*10 + 0 = 10 */
}

static void
test_layer_set_tiles (LayerFixture  *fixture,
                      gconstpointer  user_data)
{
    guint   tile_data[80];  /* 10 * 8 */
    gsize   i;
    gboolean success;
    guint   tile;

    /* Initialize tile data */
    for (i = 0; i < 80; i++)
    {
        tile_data[i] = (guint)i;
    }

    /* Set all tiles */
    success = lrg_tilemap_layer_set_tiles (fixture->layer, tile_data, 80);
    g_assert_true (success);

    /* Verify */
    tile = lrg_tilemap_layer_get_tile (fixture->layer, 0, 0);
    g_assert_cmpuint (tile, ==, 0);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 5, 3);  /* 3*10 + 5 = 35 */
    g_assert_cmpuint (tile, ==, 35);

    tile = lrg_tilemap_layer_get_tile (fixture->layer, 9, 7);  /* 7*10 + 9 = 79 */
    g_assert_cmpuint (tile, ==, 79);
}

static void
test_layer_set_tiles_wrong_size (LayerFixture  *fixture,
                                 gconstpointer  user_data)
{
    guint    tile_data[50] = {0};  /* Wrong size, but initialized */
    gboolean success;

    /*
     * Expect a warning message when wrong size is provided.
     * This prevents GLib's fatal warning handler from aborting the test.
     */
    g_test_expect_message ("Libregnum-Tilemap",
                           G_LOG_LEVEL_WARNING,
                           "*Tile data length*does not match layer size*");

    /* Should fail with wrong size */
    success = lrg_tilemap_layer_set_tiles (fixture->layer, tile_data, 50);
    g_assert_false (success);

    g_test_assert_expected_messages ();
}

/* ==========================================================================
 * Test Cases - Tilemap Construction
 * ========================================================================== */

static void
test_tilemap_new (void)
{
    g_autoptr(GrlTexture) texture = NULL;
    g_autoptr(LrgTileset) tileset = NULL;
    g_autoptr(LrgTilemap) tilemap = NULL;

    SKIP_IF_NO_GRAPHICS ();

    texture = create_mock_texture (128, 128);
    tileset = lrg_tileset_new (texture, 16, 16);
    tilemap = lrg_tilemap_new (tileset);

    g_assert_nonnull (tilemap);
    g_assert_true (LRG_IS_TILEMAP (tilemap));
    g_assert_true (lrg_tilemap_get_tileset (tilemap) == tileset);
    g_assert_cmpuint (lrg_tilemap_get_layer_count (tilemap), ==, 0);
}

/* ==========================================================================
 * Test Cases - Tilemap Layer Management
 * ========================================================================== */

static void
test_tilemap_add_layer (TilemapFixture *fixture,
                        gconstpointer   user_data)
{
    SKIP_IF_NO_GRAPHICS ();

    g_assert_cmpuint (lrg_tilemap_get_layer_count (fixture->tilemap), ==, 0);

    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer1);
    g_assert_cmpuint (lrg_tilemap_get_layer_count (fixture->tilemap), ==, 1);

    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer2);
    g_assert_cmpuint (lrg_tilemap_get_layer_count (fixture->tilemap), ==, 2);
}

static void
test_tilemap_get_layer (TilemapFixture *fixture,
                        gconstpointer   user_data)
{
    LrgTilemapLayer *found;

    SKIP_IF_NO_GRAPHICS ();

    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer1);
    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer2);

    found = lrg_tilemap_get_layer (fixture->tilemap, 0);
    g_assert_true (found == fixture->layer1);

    found = lrg_tilemap_get_layer (fixture->tilemap, 1);
    g_assert_true (found == fixture->layer2);

    /* Out of bounds */
    found = lrg_tilemap_get_layer (fixture->tilemap, 10);
    g_assert_null (found);
}

static void
test_tilemap_get_layer_by_name (TilemapFixture *fixture,
                                gconstpointer   user_data)
{
    LrgTilemapLayer *found;

    SKIP_IF_NO_GRAPHICS ();

    lrg_tilemap_layer_set_name (fixture->layer1, "ground");
    lrg_tilemap_layer_set_name (fixture->layer2, "objects");

    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer1);
    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer2);

    found = lrg_tilemap_get_layer_by_name (fixture->tilemap, "ground");
    g_assert_true (found == fixture->layer1);

    found = lrg_tilemap_get_layer_by_name (fixture->tilemap, "objects");
    g_assert_true (found == fixture->layer2);

    /* Not found */
    found = lrg_tilemap_get_layer_by_name (fixture->tilemap, "nonexistent");
    g_assert_null (found);
}

static void
test_tilemap_insert_layer (TilemapFixture *fixture,
                           gconstpointer   user_data)
{
    g_autoptr(LrgTilemapLayer) layer3 = NULL;
    LrgTilemapLayer           *found;

    SKIP_IF_NO_GRAPHICS ();

    layer3 = lrg_tilemap_layer_new (10, 8);
    lrg_tilemap_layer_set_name (layer3, "middle");
    lrg_tilemap_layer_set_name (fixture->layer1, "first");
    lrg_tilemap_layer_set_name (fixture->layer2, "last");

    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer1);
    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer2);

    /* Insert in the middle */
    lrg_tilemap_insert_layer (fixture->tilemap, layer3, 1);

    g_assert_cmpuint (lrg_tilemap_get_layer_count (fixture->tilemap), ==, 3);

    found = lrg_tilemap_get_layer (fixture->tilemap, 0);
    g_assert_true (found == fixture->layer1);

    found = lrg_tilemap_get_layer (fixture->tilemap, 1);
    g_assert_true (found == layer3);

    found = lrg_tilemap_get_layer (fixture->tilemap, 2);
    g_assert_true (found == fixture->layer2);
}

static void
test_tilemap_remove_layer (TilemapFixture *fixture,
                           gconstpointer   user_data)
{
    SKIP_IF_NO_GRAPHICS ();

    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer1);
    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer2);

    g_assert_cmpuint (lrg_tilemap_get_layer_count (fixture->tilemap), ==, 2);

    lrg_tilemap_remove_layer (fixture->tilemap, fixture->layer1);

    g_assert_cmpuint (lrg_tilemap_get_layer_count (fixture->tilemap), ==, 1);

    /* layer2 should now be at index 0 */
    g_assert_true (lrg_tilemap_get_layer (fixture->tilemap, 0) == fixture->layer2);
}

static void
test_tilemap_remove_layer_at (TilemapFixture *fixture,
                              gconstpointer   user_data)
{
    SKIP_IF_NO_GRAPHICS ();

    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer1);
    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer2);

    lrg_tilemap_remove_layer_at (fixture->tilemap, 0);

    g_assert_cmpuint (lrg_tilemap_get_layer_count (fixture->tilemap), ==, 1);
    g_assert_true (lrg_tilemap_get_layer (fixture->tilemap, 0) == fixture->layer2);
}

/* ==========================================================================
 * Test Cases - Tilemap Dimensions
 * ========================================================================== */

static void
test_tilemap_dimensions (TilemapFixture *fixture,
                         gconstpointer   user_data)
{
    SKIP_IF_NO_GRAPHICS ();

    /* No layers = 0 dimensions */
    g_assert_cmpuint (lrg_tilemap_get_width (fixture->tilemap), ==, 0);
    g_assert_cmpuint (lrg_tilemap_get_height (fixture->tilemap), ==, 0);

    /* Add layer */
    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer1);

    g_assert_cmpuint (lrg_tilemap_get_width (fixture->tilemap), ==, 10);
    g_assert_cmpuint (lrg_tilemap_get_height (fixture->tilemap), ==, 8);

    /* Pixel dimensions (10*16, 8*16) */
    g_assert_cmpuint (lrg_tilemap_get_pixel_width (fixture->tilemap), ==, 160);
    g_assert_cmpuint (lrg_tilemap_get_pixel_height (fixture->tilemap), ==, 128);
}

static void
test_tilemap_world_bounds (TilemapFixture *fixture,
                           gconstpointer   user_data)
{
    g_autoptr(GrlRectangle) bounds = NULL;

    SKIP_IF_NO_GRAPHICS ();

    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer1);

    bounds = lrg_tilemap_get_world_bounds (fixture->tilemap);
    g_assert_nonnull (bounds);
    g_assert_cmpfloat_with_epsilon (bounds->x, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (bounds->y, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (bounds->width, 160.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (bounds->height, 128.0f, 0.0001f);
}

/* ==========================================================================
 * Test Cases - Tilemap Collision
 * ========================================================================== */

static void
test_tilemap_is_solid (TilemapFixture *fixture,
                       gconstpointer   user_data)
{
    SKIP_IF_NO_GRAPHICS ();

    /*
     * Set tileset tile 0 as solid.
     * Note: tile_id 0 in the layer means empty (LRG_TILEMAP_EMPTY_TILE),
     * so tile_id 1 in the layer corresponds to tileset index 0.
     */
    lrg_tileset_set_tile_properties (fixture->tileset, 0, LRG_TILE_PROPERTY_SOLID);

    /* Place tile 1 at (5,3) - this references tileset tile 0 */
    lrg_tilemap_layer_set_tile (fixture->layer1, 5, 3, 1);
    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer1);

    /* Check collision */
    g_assert_true (lrg_tilemap_is_solid (fixture->tilemap, 5, 3));
    g_assert_false (lrg_tilemap_is_solid (fixture->tilemap, 0, 0));
    g_assert_false (lrg_tilemap_is_solid (fixture->tilemap, 4, 3));
}

static void
test_tilemap_is_solid_at (TilemapFixture *fixture,
                          gconstpointer   user_data)
{
    SKIP_IF_NO_GRAPHICS ();

    /*
     * Set tileset tile 0 as solid.
     * Note: tile_id 0 in the layer means empty (LRG_TILEMAP_EMPTY_TILE),
     * so tile_id 1 in the layer corresponds to tileset index 0.
     */
    lrg_tileset_set_tile_properties (fixture->tileset, 0, LRG_TILE_PROPERTY_SOLID);

    /* Place tile 1 at (2,1) = pixel position (32,16) to (48,32) */
    lrg_tilemap_layer_set_tile (fixture->layer1, 2, 1, 1);
    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer1);

    /* Check world position collision */
    g_assert_true (lrg_tilemap_is_solid_at (fixture->tilemap, 35.0f, 20.0f));
    g_assert_true (lrg_tilemap_is_solid_at (fixture->tilemap, 32.0f, 16.0f));
    g_assert_false (lrg_tilemap_is_solid_at (fixture->tilemap, 0.0f, 0.0f));
    g_assert_false (lrg_tilemap_is_solid_at (fixture->tilemap, 50.0f, 20.0f));
}

static void
test_tilemap_collision_layer_disabled (TilemapFixture *fixture,
                                       gconstpointer   user_data)
{
    SKIP_IF_NO_GRAPHICS ();

    /*
     * Set tileset tile 0 as solid.
     * Note: tile_id 0 in the layer means empty (LRG_TILEMAP_EMPTY_TILE),
     * so tile_id 1 in the layer corresponds to tileset index 0.
     */
    lrg_tileset_set_tile_properties (fixture->tileset, 0, LRG_TILE_PROPERTY_SOLID);

    /* Place solid tile (tile_id 1 -> tileset index 0) */
    lrg_tilemap_layer_set_tile (fixture->layer1, 3, 3, 1);
    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer1);

    /* Should be solid with collision enabled */
    g_assert_true (lrg_tilemap_is_solid (fixture->tilemap, 3, 3));

    /* Disable collision on layer */
    lrg_tilemap_layer_set_collision_enabled (fixture->layer1, FALSE);

    /* Should not be solid anymore */
    g_assert_false (lrg_tilemap_is_solid (fixture->tilemap, 3, 3));
}

/* ==========================================================================
 * Test Cases - Tilemap Coordinate Conversion
 * ========================================================================== */

static void
test_tilemap_world_to_tile (TilemapFixture *fixture,
                            gconstpointer   user_data)
{
    guint tile_x, tile_y;

    SKIP_IF_NO_GRAPHICS ();

    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer1);

    /* (0,0) -> (0,0) */
    lrg_tilemap_world_to_tile (fixture->tilemap, 0.0f, 0.0f, &tile_x, &tile_y);
    g_assert_cmpuint (tile_x, ==, 0);
    g_assert_cmpuint (tile_y, ==, 0);

    /* (15,15) -> (0,0) */
    lrg_tilemap_world_to_tile (fixture->tilemap, 15.0f, 15.0f, &tile_x, &tile_y);
    g_assert_cmpuint (tile_x, ==, 0);
    g_assert_cmpuint (tile_y, ==, 0);

    /* (16,0) -> (1,0) */
    lrg_tilemap_world_to_tile (fixture->tilemap, 16.0f, 0.0f, &tile_x, &tile_y);
    g_assert_cmpuint (tile_x, ==, 1);
    g_assert_cmpuint (tile_y, ==, 0);

    /* (35,50) -> (2,3) */
    lrg_tilemap_world_to_tile (fixture->tilemap, 35.0f, 50.0f, &tile_x, &tile_y);
    g_assert_cmpuint (tile_x, ==, 2);
    g_assert_cmpuint (tile_y, ==, 3);
}

static void
test_tilemap_tile_to_world (TilemapFixture *fixture,
                            gconstpointer   user_data)
{
    gfloat world_x, world_y;

    SKIP_IF_NO_GRAPHICS ();

    lrg_tilemap_add_layer (fixture->tilemap, fixture->layer1);

    /* (0,0) -> (0,0) */
    lrg_tilemap_tile_to_world (fixture->tilemap, 0, 0, &world_x, &world_y);
    g_assert_cmpfloat_with_epsilon (world_x, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (world_y, 0.0f, 0.0001f);

    /* (1,0) -> (16,0) */
    lrg_tilemap_tile_to_world (fixture->tilemap, 1, 0, &world_x, &world_y);
    g_assert_cmpfloat_with_epsilon (world_x, 16.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (world_y, 0.0f, 0.0001f);

    /* (5,3) -> (80,48) */
    lrg_tilemap_tile_to_world (fixture->tilemap, 5, 3, &world_x, &world_y);
    g_assert_cmpfloat_with_epsilon (world_x, 80.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (world_y, 48.0f, 0.0001f);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    int result;

    g_test_init (&argc, &argv, NULL);

    /* Initialize graphics context (for texture creation) */
    graphics_available = init_graphics_context ();
    if (!graphics_available)
    {
        g_message ("Graphics context not available - some tests will be skipped");
    }

    /* Tileset Construction Tests */
    g_test_add_func ("/tilemap/tileset/new", test_tileset_new);

    g_test_add ("/tilemap/tileset/dimensions",
                TilesetFixture, NULL,
                tileset_fixture_set_up,
                test_tileset_dimensions,
                tileset_fixture_tear_down);

    /* Tileset Tile Rectangle Tests */
    g_test_add ("/tilemap/tileset/tile-rect",
                TilesetFixture, NULL,
                tileset_fixture_set_up,
                test_tileset_tile_rect,
                tileset_fixture_tear_down);

    g_test_add ("/tilemap/tileset/tile-rect-to",
                TilesetFixture, NULL,
                tileset_fixture_set_up,
                test_tileset_tile_rect_to,
                tileset_fixture_tear_down);

    g_test_add ("/tilemap/tileset/tile-rect-out-of-bounds",
                TilesetFixture, NULL,
                tileset_fixture_set_up,
                test_tileset_tile_rect_out_of_bounds,
                tileset_fixture_tear_down);

    /* Tileset Properties Tests */
    g_test_add ("/tilemap/tileset/properties-default",
                TilesetFixture, NULL,
                tileset_fixture_set_up,
                test_tileset_properties_default,
                tileset_fixture_tear_down);

    g_test_add ("/tilemap/tileset/properties-set",
                TilesetFixture, NULL,
                tileset_fixture_set_up,
                test_tileset_properties_set,
                tileset_fixture_tear_down);

    g_test_add ("/tilemap/tileset/tile-has-property",
                TilesetFixture, NULL,
                tileset_fixture_set_up,
                test_tileset_tile_has_property,
                tileset_fixture_tear_down);

    /* Layer Construction Tests */
    g_test_add_func ("/tilemap/layer/new", test_layer_new);

    g_test_add ("/tilemap/layer/dimensions",
                LayerFixture, NULL,
                layer_fixture_set_up,
                test_layer_dimensions,
                layer_fixture_tear_down);

    /* Layer Tile Access Tests */
    g_test_add ("/tilemap/layer/get-set",
                LayerFixture, NULL,
                layer_fixture_set_up,
                test_layer_get_set,
                layer_fixture_tear_down);

    g_test_add ("/tilemap/layer/get-set-out-of-bounds",
                LayerFixture, NULL,
                layer_fixture_set_up,
                test_layer_get_set_out_of_bounds,
                layer_fixture_tear_down);

    g_test_add ("/tilemap/layer/fill",
                LayerFixture, NULL,
                layer_fixture_set_up,
                test_layer_fill,
                layer_fixture_tear_down);

    g_test_add ("/tilemap/layer/fill-rect",
                LayerFixture, NULL,
                layer_fixture_set_up,
                test_layer_fill_rect,
                layer_fixture_tear_down);

    g_test_add ("/tilemap/layer/clear",
                LayerFixture, NULL,
                layer_fixture_set_up,
                test_layer_clear,
                layer_fixture_tear_down);

    /* Layer Property Tests */
    g_test_add ("/tilemap/layer/visibility",
                LayerFixture, NULL,
                layer_fixture_set_up,
                test_layer_visibility,
                layer_fixture_tear_down);

    g_test_add ("/tilemap/layer/collision",
                LayerFixture, NULL,
                layer_fixture_set_up,
                test_layer_collision,
                layer_fixture_tear_down);

    g_test_add ("/tilemap/layer/parallax",
                LayerFixture, NULL,
                layer_fixture_set_up,
                test_layer_parallax,
                layer_fixture_tear_down);

    g_test_add ("/tilemap/layer/opacity",
                LayerFixture, NULL,
                layer_fixture_set_up,
                test_layer_opacity,
                layer_fixture_tear_down);

    g_test_add ("/tilemap/layer/name",
                LayerFixture, NULL,
                layer_fixture_set_up,
                test_layer_name,
                layer_fixture_tear_down);

    /* Layer Data Access Tests */
    g_test_add ("/tilemap/layer/get-tiles",
                LayerFixture, NULL,
                layer_fixture_set_up,
                test_layer_get_tiles,
                layer_fixture_tear_down);

    g_test_add ("/tilemap/layer/set-tiles",
                LayerFixture, NULL,
                layer_fixture_set_up,
                test_layer_set_tiles,
                layer_fixture_tear_down);

    g_test_add ("/tilemap/layer/set-tiles-wrong-size",
                LayerFixture, NULL,
                layer_fixture_set_up,
                test_layer_set_tiles_wrong_size,
                layer_fixture_tear_down);

    /* Tilemap Construction Tests */
    g_test_add_func ("/tilemap/tilemap/new", test_tilemap_new);

    /* Tilemap Layer Management Tests */
    g_test_add ("/tilemap/tilemap/add-layer",
                TilemapFixture, NULL,
                tilemap_fixture_set_up,
                test_tilemap_add_layer,
                tilemap_fixture_tear_down);

    g_test_add ("/tilemap/tilemap/get-layer",
                TilemapFixture, NULL,
                tilemap_fixture_set_up,
                test_tilemap_get_layer,
                tilemap_fixture_tear_down);

    g_test_add ("/tilemap/tilemap/get-layer-by-name",
                TilemapFixture, NULL,
                tilemap_fixture_set_up,
                test_tilemap_get_layer_by_name,
                tilemap_fixture_tear_down);

    g_test_add ("/tilemap/tilemap/insert-layer",
                TilemapFixture, NULL,
                tilemap_fixture_set_up,
                test_tilemap_insert_layer,
                tilemap_fixture_tear_down);

    g_test_add ("/tilemap/tilemap/remove-layer",
                TilemapFixture, NULL,
                tilemap_fixture_set_up,
                test_tilemap_remove_layer,
                tilemap_fixture_tear_down);

    g_test_add ("/tilemap/tilemap/remove-layer-at",
                TilemapFixture, NULL,
                tilemap_fixture_set_up,
                test_tilemap_remove_layer_at,
                tilemap_fixture_tear_down);

    /* Tilemap Dimensions Tests */
    g_test_add ("/tilemap/tilemap/dimensions",
                TilemapFixture, NULL,
                tilemap_fixture_set_up,
                test_tilemap_dimensions,
                tilemap_fixture_tear_down);

    g_test_add ("/tilemap/tilemap/world-bounds",
                TilemapFixture, NULL,
                tilemap_fixture_set_up,
                test_tilemap_world_bounds,
                tilemap_fixture_tear_down);

    /* Tilemap Collision Tests */
    g_test_add ("/tilemap/tilemap/is-solid",
                TilemapFixture, NULL,
                tilemap_fixture_set_up,
                test_tilemap_is_solid,
                tilemap_fixture_tear_down);

    g_test_add ("/tilemap/tilemap/is-solid-at",
                TilemapFixture, NULL,
                tilemap_fixture_set_up,
                test_tilemap_is_solid_at,
                tilemap_fixture_tear_down);

    g_test_add ("/tilemap/tilemap/collision-layer-disabled",
                TilemapFixture, NULL,
                tilemap_fixture_set_up,
                test_tilemap_collision_layer_disabled,
                tilemap_fixture_tear_down);

    /* Tilemap Coordinate Conversion Tests */
    g_test_add ("/tilemap/tilemap/world-to-tile",
                TilemapFixture, NULL,
                tilemap_fixture_set_up,
                test_tilemap_world_to_tile,
                tilemap_fixture_tear_down);

    g_test_add ("/tilemap/tilemap/tile-to-world",
                TilemapFixture, NULL,
                tilemap_fixture_set_up,
                test_tilemap_tile_to_world,
                tilemap_fixture_tear_down);

    result = g_test_run ();

    /* Cleanup graphics context */
    cleanup_graphics_context ();

    return result;
}
