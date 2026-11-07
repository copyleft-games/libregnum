/* lrg-tileset.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Texture atlas for tilemap rendering.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TILEMAP

#include "lrg-tileset.h"
#include "lrg-tilemap-layer.h"
#include "../lrg-log.h"
#include "../core/lrg-data-loader.h"

#include <gio/gio.h>
#include <math.h>
#include <string.h>
#include <raylib.h>

/* ==========================================================================
 * Per-tile data
 * ========================================================================== */

typedef struct
{
    guint tile;          /* 0-based tile index */
    guint duration_ms;
} TileFrame;

/*
 * TileData:
 * Everything a tile carries besides its flags.  Allocated on first use;
 * tiles without data have a NULL slot.
 */
typedef struct
{
    GPtrArray   *polygons;    /* GArray of gfloat (x, y pairs) */
    GPtrArray   *data_keys;   /* gchar*, insertion order */
    GHashTable  *data;        /* key -> value */
    GArray      *animation;   /* TileFrame */
    YamlMapping *extra;       /* unknown keys of the loaded tile entry */
} TileData;

static TileData *
tile_data_new (void)
{
    TileData *td = g_new0 (TileData, 1);

    td->polygons = g_ptr_array_new_with_free_func ((GDestroyNotify) g_array_unref);
    td->data_keys = g_ptr_array_new_with_free_func (g_free);
    td->data = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    td->animation = g_array_new (FALSE, FALSE, sizeof (TileFrame));
    return td;
}

static void
tile_data_free (gpointer data)
{
    TileData *td = data;

    if (td == NULL)
        return;
    g_ptr_array_unref (td->polygons);
    g_ptr_array_unref (td->data_keys);
    g_hash_table_unref (td->data);
    g_array_unref (td->animation);
    if (td->extra != NULL)
        yaml_mapping_unref (td->extra);
    g_free (td);
}

static gboolean
tile_data_is_empty (const TileData *td)
{
    return td == NULL ||
           (td->polygons->len == 0 && td->data_keys->len == 0 &&
            td->animation->len == 0 && td->extra == NULL);
}

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgTileset
{
    GObject     parent_instance;

    GrlTexture *texture;
    guint       tile_width;
    guint       tile_height;
    guint       columns;
    guint       rows;
    guint       tile_count;

    /* Per-tile properties, indexed by tile_id */
    GArray     *tile_properties;

    /* 0.9.0: .rtileset description */
    gchar       *name;
    gchar       *image_path;
    gchar       *base_dir;       /* directory the image path is relative to */
    guint        margin;
    guint        spacing;
    gint         offset_x;
    gint         offset_y;
    GPtrArray   *tile_data;      /* TileData* or NULL, tile_count slots */
    GPtrArray   *rule_tiles;     /* LrgRuleTile* */
    YamlMapping *extra;          /* unknown top-level keys */
};

G_DEFINE_TYPE (LrgTileset, lrg_tileset, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_TEXTURE,
    PROP_TILE_WIDTH,
    PROP_TILE_HEIGHT,
    PROP_COLUMNS,
    PROP_ROWS,
    PROP_TILE_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void rule_tile_changed_cb (LrgRuleTile *rule_tile, LrgTileset *self);

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_tileset_finalize (GObject *object)
{
    LrgTileset *self = LRG_TILESET (object);

    g_clear_object (&self->texture);
    g_clear_pointer (&self->tile_properties, g_array_unref);
    g_free (self->name);
    g_free (self->image_path);
    g_free (self->base_dir);
    g_clear_pointer (&self->tile_data, g_ptr_array_unref);
    if (self->rule_tiles != NULL)
    {
        guint i;

        for (i = 0; i < self->rule_tiles->len; i++)
            g_signal_handlers_disconnect_by_func (g_ptr_array_index (self->rule_tiles, i),
                                                  rule_tile_changed_cb, self);
        g_clear_pointer (&self->rule_tiles, g_ptr_array_unref);
    }
    if (self->extra != NULL)
        yaml_mapping_unref (self->extra);

    G_OBJECT_CLASS (lrg_tileset_parent_class)->finalize (object);
}

static void
lrg_tileset_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgTileset *self = LRG_TILESET (object);

    switch (prop_id)
    {
    case PROP_TEXTURE:
        g_value_set_object (value, self->texture);
        break;
    case PROP_TILE_WIDTH:
        g_value_set_uint (value, self->tile_width);
        break;
    case PROP_TILE_HEIGHT:
        g_value_set_uint (value, self->tile_height);
        break;
    case PROP_COLUMNS:
        g_value_set_uint (value, self->columns);
        break;
    case PROP_ROWS:
        g_value_set_uint (value, self->rows);
        break;
    case PROP_TILE_COUNT:
        g_value_set_uint (value, self->tile_count);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tileset_class_init (LrgTilesetClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_tileset_finalize;
    object_class->get_property = lrg_tileset_get_property;

    /**
     * LrgTileset:texture:
     *
     * The source texture atlas.
     */
    properties[PROP_TEXTURE] =
        g_param_spec_object ("texture",
                             "Texture",
                             "The source texture atlas",
                             GRL_TYPE_TEXTURE,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTileset:tile-width:
     *
     * The width of each tile in pixels.
     */
    properties[PROP_TILE_WIDTH] =
        g_param_spec_uint ("tile-width",
                           "Tile Width",
                           "Width of each tile in pixels",
                           1, G_MAXUINT, 16,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTileset:tile-height:
     *
     * The height of each tile in pixels.
     */
    properties[PROP_TILE_HEIGHT] =
        g_param_spec_uint ("tile-height",
                           "Tile Height",
                           "Height of each tile in pixels",
                           1, G_MAXUINT, 16,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTileset:columns:
     *
     * The number of tile columns in the tileset.
     */
    properties[PROP_COLUMNS] =
        g_param_spec_uint ("columns",
                           "Columns",
                           "Number of tile columns",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTileset:rows:
     *
     * The number of tile rows in the tileset.
     */
    properties[PROP_ROWS] =
        g_param_spec_uint ("rows",
                           "Rows",
                           "Number of tile rows",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTileset:tile-count:
     *
     * The total number of tiles in the tileset.
     */
    properties[PROP_TILE_COUNT] =
        g_param_spec_uint ("tile-count",
                           "Tile Count",
                           "Total number of tiles",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgTileset::changed:
     * @self: the tileset
     *
     * Emitted when per-tile data (flags, collision, custom data,
     * animation), the layout or a rule tile changes, so editors and
     * views can refresh.
     */
    signals[SIGNAL_CHANGED] =
        g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_tileset_init (LrgTileset *self)
{
    self->texture = NULL;
    self->tile_width = 16;
    self->tile_height = 16;
    self->columns = 0;
    self->rows = 0;
    self->tile_count = 0;
    self->tile_properties = NULL;
    self->tile_data = g_ptr_array_new_with_free_func (tile_data_free);
    self->rule_tiles = g_ptr_array_new_with_free_func (g_object_unref);
}

/* sizes the per-tile arrays for self->tile_count tiles */
static void
resize_tile_arrays (LrgTileset *self)
{
    if (self->tile_properties == NULL)
        self->tile_properties = g_array_sized_new (FALSE, TRUE, sizeof (LrgTileProperty),
                                                   self->tile_count);
    g_array_set_size (self->tile_properties, self->tile_count);
    g_ptr_array_set_size (self->tile_data, (gint) self->tile_count);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/*
 * lrg_tileset_new:
 * @texture: (transfer none): the source texture atlas
 * @tile_width: width of each tile in pixels
 * @tile_height: height of each tile in pixels
 *
 * Creates a new tileset from a texture atlas. The texture is divided
 * into a grid of tiles based on the specified tile dimensions.
 *
 * The number of columns is calculated from the texture width divided
 * by @tile_width. Tiles are numbered left-to-right, top-to-bottom,
 * starting from 0.
 *
 * Returns: (transfer full): A new #LrgTileset
 */
LrgTileset *
lrg_tileset_new (GrlTexture *texture,
                 guint       tile_width,
                 guint       tile_height)
{
    LrgTileset *self;
    gint        tex_width;
    gint        tex_height;

    g_return_val_if_fail (GRL_IS_TEXTURE (texture), NULL);
    g_return_val_if_fail (tile_width > 0, NULL);
    g_return_val_if_fail (tile_height > 0, NULL);

    self = g_object_new (LRG_TYPE_TILESET, NULL);

    self->texture = g_object_ref (texture);
    self->tile_width = tile_width;
    self->tile_height = tile_height;

    /* Calculate grid dimensions */
    tex_width = grl_texture_get_width (texture);
    tex_height = grl_texture_get_height (texture);

    self->columns = (guint)(tex_width / (gint)tile_width);
    self->rows = (guint)(tex_height / (gint)tile_height);
    self->tile_count = self->columns * self->rows;

    /* Initialize the per-tile arrays */
    resize_tile_arrays (self);

    lrg_log_debug ("Created tileset: %ux%u tiles (%ux%u each) from %dx%d texture",
               self->columns, self->rows,
               tile_width, tile_height,
               tex_width, tex_height);

    return self;
}

/*
 * lrg_tileset_new_from_file:
 * @path: path to the image file
 * @tile_width: width of each tile in pixels
 * @tile_height: height of each tile in pixels
 * @error: (nullable): return location for error, or %NULL
 *
 * Creates a new tileset by loading a texture from a file.
 *
 * Returns: (transfer full) (nullable): A new #LrgTileset, or %NULL on error
 */
LrgTileset *
lrg_tileset_new_from_file (const gchar  *path,
                           guint         tile_width,
                           guint         tile_height,
                           GError      **error)
{
    g_autoptr(GrlTexture) texture = NULL;
    LrgTileset           *tileset;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (tile_width > 0, NULL);
    g_return_val_if_fail (tile_height > 0, NULL);

    texture = grl_texture_new_from_file (path);
    if (texture == NULL || !grl_texture_is_valid (texture))
    {
        g_set_error (error,
                     G_IO_ERROR,
                     G_IO_ERROR_FAILED,
                     "Failed to load texture from '%s'",
                     path);
        return NULL;
    }

    tileset = lrg_tileset_new (texture, tile_width, tile_height);

    return tileset;
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

/*
 * lrg_tileset_get_texture:
 * @self: an #LrgTileset
 *
 * Gets the underlying texture atlas.
 *
 * Returns: (transfer none): The texture
 */
GrlTexture *
lrg_tileset_get_texture (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), NULL);

    return self->texture;
}

/*
 * lrg_tileset_get_tile_width:
 * @self: an #LrgTileset
 *
 * Gets the width of each tile in pixels.
 *
 * Returns: The tile width
 */
guint
lrg_tileset_get_tile_width (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), 0);

    return self->tile_width;
}

/*
 * lrg_tileset_get_tile_height:
 * @self: an #LrgTileset
 *
 * Gets the height of each tile in pixels.
 *
 * Returns: The tile height
 */
guint
lrg_tileset_get_tile_height (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), 0);

    return self->tile_height;
}

/*
 * lrg_tileset_get_columns:
 * @self: an #LrgTileset
 *
 * Gets the number of tile columns in the tileset.
 *
 * Returns: The column count
 */
guint
lrg_tileset_get_columns (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), 0);

    return self->columns;
}

/*
 * lrg_tileset_get_rows:
 * @self: an #LrgTileset
 *
 * Gets the number of tile rows in the tileset.
 *
 * Returns: The row count
 */
guint
lrg_tileset_get_rows (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), 0);

    return self->rows;
}

/*
 * lrg_tileset_get_tile_count:
 * @self: an #LrgTileset
 *
 * Gets the total number of tiles in the tileset.
 *
 * Returns: The tile count
 */
guint
lrg_tileset_get_tile_count (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), 0);

    return self->tile_count;
}

/* ==========================================================================
 * Tile Rectangles
 * ========================================================================== */

/*
 * lrg_tileset_get_tile_rect:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 *
 * Gets the source rectangle for a specific tile within the texture.
 * This rectangle can be used with grl_draw_texture_rec() to render
 * the tile.
 *
 * Returns: (transfer full) (nullable): The tile source rectangle,
 *          or %NULL if @tile_id is out of bounds
 */
GrlRectangle *
lrg_tileset_get_tile_rect (LrgTileset *self,
                           guint       tile_id)
{
    GrlRectangle *rect;
    guint         col;
    guint         row;

    g_return_val_if_fail (LRG_IS_TILESET (self), NULL);

    if (tile_id >= self->tile_count)
    {
        return NULL;
    }

    col = tile_id % self->columns;
    row = tile_id / self->columns;

    rect = grl_rectangle_new ((gfloat)(self->margin + col * (self->tile_width + self->spacing)),
                              (gfloat)(self->margin + row * (self->tile_height + self->spacing)),
                              (gfloat)self->tile_width,
                              (gfloat)self->tile_height);

    return rect;
}

/*
 * lrg_tileset_get_tile_rect_to:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 * @out_rect: (out caller-allocates): location to store the rectangle
 *
 * Gets the source rectangle for a specific tile, storing it in
 * a caller-provided rectangle. This avoids allocation.
 *
 * Returns: %TRUE if successful, %FALSE if @tile_id is out of bounds
 */
gboolean
lrg_tileset_get_tile_rect_to (LrgTileset   *self,
                              guint         tile_id,
                              GrlRectangle *out_rect)
{
    guint col;
    guint row;

    g_return_val_if_fail (LRG_IS_TILESET (self), FALSE);
    g_return_val_if_fail (out_rect != NULL, FALSE);

    if (tile_id >= self->tile_count)
    {
        return FALSE;
    }

    col = tile_id % self->columns;
    row = tile_id / self->columns;

    out_rect->x = (gfloat)(self->margin + col * (self->tile_width + self->spacing));
    out_rect->y = (gfloat)(self->margin + row * (self->tile_height + self->spacing));
    out_rect->width = (gfloat)self->tile_width;
    out_rect->height = (gfloat)self->tile_height;

    return TRUE;
}

/* ==========================================================================
 * Tile Properties
 * ========================================================================== */

/*
 * lrg_tileset_get_tile_properties:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 *
 * Gets the property flags for a specific tile.
 *
 * Returns: The tile property flags
 */
LrgTileProperty
lrg_tileset_get_tile_properties (LrgTileset *self,
                                 guint       tile_id)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), LRG_TILE_PROPERTY_NONE);

    if (tile_id >= self->tile_count)
    {
        return LRG_TILE_PROPERTY_NONE;
    }

    return g_array_index (self->tile_properties, LrgTileProperty, tile_id);
}

/*
 * lrg_tileset_set_tile_properties:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 * @properties: the property flags to set
 *
 * Sets the property flags for a specific tile.
 */
void
lrg_tileset_set_tile_properties (LrgTileset      *self,
                                 guint            tile_id,
                                 LrgTileProperty  properties)
{
    g_return_if_fail (LRG_IS_TILESET (self));

    if (tile_id >= self->tile_count)
    {
        g_warning ("Tile ID %u out of bounds (max %u)", tile_id, self->tile_count);
        return;
    }

    if (g_array_index (self->tile_properties, LrgTileProperty, tile_id) == properties)
        return;
    g_array_index (self->tile_properties, LrgTileProperty, tile_id) = properties;
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/*
 * lrg_tileset_tile_has_property:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 * @property: the property to check
 *
 * Checks if a tile has a specific property flag set.
 *
 * Returns: %TRUE if the tile has the property
 */
gboolean
lrg_tileset_tile_has_property (LrgTileset      *self,
                               guint            tile_id,
                               LrgTileProperty  property)
{
    LrgTileProperty props;

    g_return_val_if_fail (LRG_IS_TILESET (self), FALSE);

    if (tile_id >= self->tile_count)
    {
        return FALSE;
    }

    props = g_array_index (self->tile_properties, LrgTileProperty, tile_id);

    return (props & property) == property;
}

/* ==========================================================================
 * 0.9.0: errors, construction without a texture, layout
 * ========================================================================== */

G_DEFINE_QUARK (lrg-tileset-error-quark, lrg_tileset_error)

GType
lrg_tileset_error_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        static const GEnumValue values[] = {
            { LRG_TILESET_ERROR_IO, "LRG_TILESET_ERROR_IO", "io" },
            { LRG_TILESET_ERROR_PARSE, "LRG_TILESET_ERROR_PARSE", "parse" },
            { LRG_TILESET_ERROR_INVALID, "LRG_TILESET_ERROR_INVALID", "invalid" },
            { 0, NULL, NULL }
        };
        GType id = g_enum_register_static (g_intern_static_string ("LrgTilesetError"), values);

        g_once_init_leave (&type_id, id);
    }
    return (GType) type_id;
}

LrgTileset *
lrg_tileset_new_empty (guint tile_width,
                       guint tile_height,
                       guint columns,
                       guint tile_count)
{
    LrgTileset *self;

    g_return_val_if_fail (tile_width > 0, NULL);
    g_return_val_if_fail (tile_height > 0, NULL);
    g_return_val_if_fail (columns > 0, NULL);

    self = g_object_new (LRG_TYPE_TILESET, NULL);
    self->tile_width = tile_width;
    self->tile_height = tile_height;
    self->columns = columns;
    self->tile_count = tile_count;
    self->rows = (tile_count + columns - 1) / columns;
    resize_tile_arrays (self);
    return self;
}

void
lrg_tileset_set_texture (LrgTileset *self,
                         GrlTexture *texture)
{
    g_return_if_fail (LRG_IS_TILESET (self));
    g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));

    if (g_set_object (&self->texture, texture))
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXTURE]);
}

gboolean
lrg_tileset_load_texture (LrgTileset  *self,
                          GError     **error)
{
    g_autofree gchar *path = NULL;
    g_autoptr(GrlTexture) texture = NULL;

    g_return_val_if_fail (LRG_IS_TILESET (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (self->image_path == NULL)
    {
        g_set_error (error, LRG_TILESET_ERROR, LRG_TILESET_ERROR_IO, "the tileset has no image path");
        return FALSE;
    }
    if (!IsWindowReady ())
    {
        g_set_error (error, LRG_TILESET_ERROR, LRG_TILESET_ERROR_IO,
                     "cannot load '%s': no GL context (open a window first)", self->image_path);
        return FALSE;
    }

    path = (self->base_dir != NULL && !g_path_is_absolute (self->image_path))
        ? g_build_filename (self->base_dir, self->image_path, NULL)
        : g_strdup (self->image_path);
    texture = grl_texture_new_from_file (path);
    if (texture == NULL || !grl_texture_is_valid (texture))
    {
        g_set_error (error, LRG_TILESET_ERROR, LRG_TILESET_ERROR_IO, "failed to load image '%s'", path);
        return FALSE;
    }
    lrg_tileset_set_texture (self, texture);
    return TRUE;
}

const gchar *
lrg_tileset_get_name (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), NULL);
    return self->name;
}

void
lrg_tileset_set_name (LrgTileset  *self,
                      const gchar *name)
{
    g_return_if_fail (LRG_IS_TILESET (self));
    g_free (self->name);
    self->name = g_strdup (name);
}

const gchar *
lrg_tileset_get_image_path (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), NULL);
    return self->image_path;
}

void
lrg_tileset_set_image_path (LrgTileset  *self,
                            const gchar *path)
{
    g_return_if_fail (LRG_IS_TILESET (self));
    g_free (self->image_path);
    self->image_path = g_strdup (path);
}

guint
lrg_tileset_get_margin (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), 0);
    return self->margin;
}

guint
lrg_tileset_get_spacing (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), 0);
    return self->spacing;
}

void
lrg_tileset_set_margin_spacing (LrgTileset *self,
                                guint       margin,
                                guint       spacing)
{
    g_return_if_fail (LRG_IS_TILESET (self));

    if (self->margin == margin && self->spacing == spacing)
        return;
    self->margin = margin;
    self->spacing = spacing;
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

void
lrg_tileset_get_draw_offset (LrgTileset *self,
                             gint       *x,
                             gint       *y)
{
    g_return_if_fail (LRG_IS_TILESET (self));
    if (x != NULL)
        *x = self->offset_x;
    if (y != NULL)
        *y = self->offset_y;
}

void
lrg_tileset_set_draw_offset (LrgTileset *self,
                             gint        x,
                             gint        y)
{
    g_return_if_fail (LRG_IS_TILESET (self));

    if (self->offset_x == x && self->offset_y == y)
        return;
    self->offset_x = x;
    self->offset_y = y;
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

void
lrg_tileset_changed (LrgTileset *self)
{
    g_return_if_fail (LRG_IS_TILESET (self));
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* ==========================================================================
 * Per-tile data access
 * ========================================================================== */

/* the tile's data, or NULL (never creates it) */
static TileData *
peek_tile_data (LrgTileset *self,
                guint       tile_id)
{
    if (tile_id >= self->tile_data->len)
        return NULL;
    return g_ptr_array_index (self->tile_data, tile_id);
}

/* the tile's data, created on demand; NULL when out of range */
static TileData *
ensure_tile_data (LrgTileset *self,
                  guint       tile_id)
{
    TileData *td;

    if (tile_id >= self->tile_data->len)
    {
        g_warning ("Tile ID %u out of bounds (max %u)", tile_id, self->tile_count);
        return NULL;
    }
    td = g_ptr_array_index (self->tile_data, tile_id);
    if (td == NULL)
    {
        td = tile_data_new ();
        g_ptr_array_index (self->tile_data, tile_id) = td;
    }
    return td;
}

void
lrg_tileset_add_tile_collision_polygon (LrgTileset   *self,
                                        guint         tile_id,
                                        const gfloat *coords,
                                        guint         n_coords)
{
    TileData *td;
    GArray *poly;

    g_return_if_fail (LRG_IS_TILESET (self));
    g_return_if_fail (coords != NULL);
    g_return_if_fail (n_coords >= 6 && n_coords % 2 == 0);

    td = ensure_tile_data (self, tile_id);
    if (td == NULL)
        return;
    poly = g_array_sized_new (FALSE, FALSE, sizeof (gfloat), n_coords);
    g_array_append_vals (poly, coords, n_coords);
    g_ptr_array_add (td->polygons, poly);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

guint
lrg_tileset_get_tile_collision_polygon_count (LrgTileset *self,
                                              guint       tile_id)
{
    TileData *td;

    g_return_val_if_fail (LRG_IS_TILESET (self), 0);
    td = peek_tile_data (self, tile_id);
    return td != NULL ? td->polygons->len : 0;
}

const gfloat *
lrg_tileset_get_tile_collision_polygon (LrgTileset *self,
                                        guint       tile_id,
                                        guint       index,
                                        guint      *n_coords)
{
    TileData *td;
    GArray *poly;

    g_return_val_if_fail (LRG_IS_TILESET (self), NULL);
    g_return_val_if_fail (n_coords != NULL, NULL);

    *n_coords = 0;
    td = peek_tile_data (self, tile_id);
    if (td == NULL || index >= td->polygons->len)
        return NULL;
    poly = g_ptr_array_index (td->polygons, index);
    *n_coords = poly->len;
    return (const gfloat *) poly->data;
}

void
lrg_tileset_clear_tile_collision (LrgTileset *self,
                                  guint       tile_id)
{
    TileData *td;

    g_return_if_fail (LRG_IS_TILESET (self));
    td = peek_tile_data (self, tile_id);
    if (td == NULL || td->polygons->len == 0)
        return;
    g_ptr_array_set_size (td->polygons, 0);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

gboolean
lrg_tileset_tile_has_collision (LrgTileset *self,
                                guint       tile_id)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), FALSE);

    return lrg_tileset_tile_has_property (self, tile_id, LRG_TILE_PROPERTY_SOLID) ||
           lrg_tileset_get_tile_collision_polygon_count (self, tile_id) > 0;
}

void
lrg_tileset_set_tile_data (LrgTileset  *self,
                           guint        tile_id,
                           const gchar *key,
                           const gchar *value)
{
    TileData *td;

    g_return_if_fail (LRG_IS_TILESET (self));
    g_return_if_fail (key != NULL);

    if (value == NULL)
    {
        guint i;

        td = peek_tile_data (self, tile_id);
        if (td == NULL || !g_hash_table_remove (td->data, key))
            return;
        for (i = 0; i < td->data_keys->len; i++)
        {
            if (g_strcmp0 (g_ptr_array_index (td->data_keys, i), key) == 0)
            {
                g_ptr_array_remove_index (td->data_keys, i);
                break;
            }
        }
        g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
        return;
    }

    td = ensure_tile_data (self, tile_id);
    if (td == NULL)
        return;
    if (g_strcmp0 (g_hash_table_lookup (td->data, key), value) == 0)
        return;
    if (!g_hash_table_contains (td->data, key))
        g_ptr_array_add (td->data_keys, g_strdup (key));
    g_hash_table_insert (td->data, g_strdup (key), g_strdup (value));
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

const gchar *
lrg_tileset_get_tile_data (LrgTileset  *self,
                           guint        tile_id,
                           const gchar *key)
{
    TileData *td;

    g_return_val_if_fail (LRG_IS_TILESET (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    td = peek_tile_data (self, tile_id);
    return td != NULL ? g_hash_table_lookup (td->data, key) : NULL;
}

gdouble
lrg_tileset_get_tile_data_double (LrgTileset  *self,
                                  guint        tile_id,
                                  const gchar *key,
                                  gdouble      default_value)
{
    const gchar *text = lrg_tileset_get_tile_data (self, tile_id, key);
    gchar *end = NULL;
    gdouble value;

    if (text == NULL || text[0] == '\0')
        return default_value;
    value = g_ascii_strtod (text, &end);
    if (end == NULL || *end != '\0')
        return default_value;
    return value;
}

gboolean
lrg_tileset_get_tile_data_boolean (LrgTileset  *self,
                                   guint        tile_id,
                                   const gchar *key,
                                   gboolean     default_value)
{
    const gchar *text = lrg_tileset_get_tile_data (self, tile_id, key);

    if (g_strcmp0 (text, "true") == 0)
        return TRUE;
    if (g_strcmp0 (text, "false") == 0)
        return FALSE;
    return default_value;
}

gchar **
lrg_tileset_get_tile_data_keys (LrgTileset *self,
                                guint       tile_id)
{
    TileData *td;
    GStrvBuilder *builder;
    guint i;

    g_return_val_if_fail (LRG_IS_TILESET (self), NULL);

    builder = g_strv_builder_new ();
    td = peek_tile_data (self, tile_id);
    if (td != NULL)
        for (i = 0; i < td->data_keys->len; i++)
            g_strv_builder_add (builder, g_ptr_array_index (td->data_keys, i));
    {
        gchar **keys = g_strv_builder_end (builder);

        g_strv_builder_unref (builder);
        return keys;
    }
}

guint *
lrg_tileset_find_tiles_with_data (LrgTileset  *self,
                                  const gchar *key,
                                  const gchar *value,
                                  guint       *n_tiles)
{
    GArray *found;
    guint i;

    g_return_val_if_fail (LRG_IS_TILESET (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);
    g_return_val_if_fail (n_tiles != NULL, NULL);

    found = g_array_new (FALSE, FALSE, sizeof (guint));
    for (i = 0; i < self->tile_data->len; i++)
    {
        TileData *td = g_ptr_array_index (self->tile_data, i);
        const gchar *v;

        if (td == NULL)
            continue;
        v = g_hash_table_lookup (td->data, key);
        if (v != NULL && (value == NULL || g_strcmp0 (v, value) == 0))
            g_array_append_val (found, i);
    }
    *n_tiles = found->len;
    return (guint *) g_array_free (found, FALSE);
}

void
lrg_tileset_set_tile_animation (LrgTileset  *self,
                                guint        tile_id,
                                const guint *frames,
                                const guint *durations_ms,
                                guint        n_frames)
{
    TileData *td;
    LrgTileProperty props;
    guint i;

    g_return_if_fail (LRG_IS_TILESET (self));
    g_return_if_fail (n_frames == 0 || (frames != NULL && durations_ms != NULL));

    if (tile_id >= self->tile_count)
    {
        g_warning ("Tile ID %u out of bounds (max %u)", tile_id, self->tile_count);
        return;
    }

    td = n_frames > 0 ? ensure_tile_data (self, tile_id) : peek_tile_data (self, tile_id);
    if (td != NULL)
    {
        g_array_set_size (td->animation, 0);
        for (i = 0; i < n_frames; i++)
        {
            TileFrame frame;

            frame.tile = frames[i];
            frame.duration_ms = MAX (durations_ms[i], 1u);
            g_array_append_val (td->animation, frame);
        }
    }

    /* keep the ANIMATED flag in step with the frames */
    props = g_array_index (self->tile_properties, LrgTileProperty, tile_id);
    if (n_frames > 0)
        props |= LRG_TILE_PROPERTY_ANIMATED;
    else
        props &= ~LRG_TILE_PROPERTY_ANIMATED;
    g_array_index (self->tile_properties, LrgTileProperty, tile_id) = props;
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

guint
lrg_tileset_get_tile_animation_length (LrgTileset *self,
                                       guint       tile_id)
{
    TileData *td;

    g_return_val_if_fail (LRG_IS_TILESET (self), 0);
    td = peek_tile_data (self, tile_id);
    return td != NULL ? td->animation->len : 0;
}

gboolean
lrg_tileset_get_tile_animation_frame (LrgTileset *self,
                                      guint       tile_id,
                                      guint       index,
                                      guint      *out_tile,
                                      guint      *out_duration_ms)
{
    TileData *td;
    TileFrame *frame;

    g_return_val_if_fail (LRG_IS_TILESET (self), FALSE);

    td = peek_tile_data (self, tile_id);
    if (td == NULL || index >= td->animation->len)
        return FALSE;
    frame = &g_array_index (td->animation, TileFrame, index);
    if (out_tile != NULL)
        *out_tile = frame->tile;
    if (out_duration_ms != NULL)
        *out_duration_ms = frame->duration_ms;
    return TRUE;
}

guint
lrg_tileset_get_animated_tile (LrgTileset *self,
                               guint       tile_id,
                               guint64     time_ms)
{
    TileData *td;
    guint64 total = 0;
    guint64 t;
    guint i;

    g_return_val_if_fail (LRG_IS_TILESET (self), tile_id);

    td = peek_tile_data (self, tile_id);
    if (td == NULL || td->animation->len == 0)
        return tile_id;

    for (i = 0; i < td->animation->len; i++)
        total += g_array_index (td->animation, TileFrame, i).duration_ms;
    t = time_ms % total;
    for (i = 0; i < td->animation->len; i++)
    {
        TileFrame *frame = &g_array_index (td->animation, TileFrame, i);

        if (t < frame->duration_ms)
            return frame->tile;
        t -= frame->duration_ms;
    }
    return g_array_index (td->animation, TileFrame, td->animation->len - 1).tile;
}

/* ==========================================================================
 * Rule tiles
 * ========================================================================== */

static void
rule_tile_changed_cb (LrgRuleTile *rule_tile,
                      LrgTileset  *self)
{
    (void) rule_tile;
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

void
lrg_tileset_add_rule_tile (LrgTileset  *self,
                           LrgRuleTile *rule_tile)
{
    g_return_if_fail (LRG_IS_TILESET (self));
    g_return_if_fail (LRG_IS_RULE_TILE (rule_tile));

    if (g_ptr_array_find (self->rule_tiles, rule_tile, NULL))
        return;
    g_ptr_array_add (self->rule_tiles, g_object_ref (rule_tile));
    g_signal_connect (rule_tile, "changed", G_CALLBACK (rule_tile_changed_cb), self);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

gboolean
lrg_tileset_remove_rule_tile (LrgTileset  *self,
                              LrgRuleTile *rule_tile)
{
    guint index;

    g_return_val_if_fail (LRG_IS_TILESET (self), FALSE);
    g_return_val_if_fail (LRG_IS_RULE_TILE (rule_tile), FALSE);

    if (!g_ptr_array_find (self->rule_tiles, rule_tile, &index))
        return FALSE;
    g_signal_handlers_disconnect_by_func (rule_tile, rule_tile_changed_cb, self);
    g_ptr_array_remove_index (self->rule_tiles, index);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
    return TRUE;
}

guint
lrg_tileset_get_n_rule_tiles (LrgTileset *self)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), 0);
    return self->rule_tiles->len;
}

LrgRuleTile *
lrg_tileset_get_rule_tile (LrgTileset *self,
                           guint       index)
{
    g_return_val_if_fail (LRG_IS_TILESET (self), NULL);

    if (index >= self->rule_tiles->len)
        return NULL;
    return g_ptr_array_index (self->rule_tiles, index);
}

LrgRuleTile *
lrg_tileset_get_rule_tile_by_name (LrgTileset  *self,
                                   const gchar *name)
{
    guint i;

    g_return_val_if_fail (LRG_IS_TILESET (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    for (i = 0; i < self->rule_tiles->len; i++)
    {
        LrgRuleTile *rt = g_ptr_array_index (self->rule_tiles, i);

        if (g_strcmp0 (lrg_rule_tile_get_name (rt), name) == 0)
            return rt;
    }
    return NULL;
}

LrgRuleTile *
lrg_tileset_find_rule_tile_for (LrgTileset *self,
                                guint       tile)
{
    guint i;

    g_return_val_if_fail (LRG_IS_TILESET (self), NULL);

    for (i = 0; i < self->rule_tiles->len; i++)
    {
        LrgRuleTile *rt = g_ptr_array_index (self->rule_tiles, i);

        if (lrg_rule_tile_owns_tile (rt, tile))
            return rt;
    }
    return NULL;
}

guint
lrg_tileset_apply_rule_tiles (LrgTileset            *self,
                              guint                 *tiles,
                              gsize                  n_tiles,
                              guint                  width,
                              LrgTileNeighborLayout  layout,
                              gint                   x,
                              gint                   y,
                              guint                  rect_width,
                              guint                  rect_height)
{
    guint changed = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_TILESET (self), 0);

    /*
     * Each rule tile repaints only the cells it owns; since a cell is owned
     * by one rule tile (keep their tile ranges apart), the order of the
     * passes does not matter.
     */
    for (i = 0; i < self->rule_tiles->len; i++)
        changed += lrg_rule_tile_apply (g_ptr_array_index (self->rule_tiles, i), tiles, n_tiles,
                                        width, layout, x, y, rect_width, rect_height);
    return changed;
}

/* ==========================================================================
 * .rtileset reader
 *
 * version: 1
 * name: dungeon
 * image: dungeon.png
 * tile_size: [16, 16]
 * columns: 8
 * tile_count: 64
 * margin: 0
 * spacing: 0
 * offset: [0, -8]
 * tiles:
 *   - id: 5
 *     flags: [solid]
 *     collision:
 *       - [0, 8, 16, 8, 16, 16, 0, 16]
 *     data: {kind: water}
 *     animation:
 *       - {tile: 5, duration: 200}
 * rule_tiles: [...]           (lrg-rule-tile.c)
 * ========================================================================== */

typedef struct
{
    const gchar  *source;
    GError      **error;
} Reader;

static gboolean
rd_fail (Reader      *rd,
         const gchar *path,
         const gchar *message)
{
    g_set_error (rd->error, LRG_TILESET_ERROR, LRG_TILESET_ERROR_INVALID,
                 "%s: %s: %s", rd->source, path, message);
    return FALSE;
}

static const gchar *
rd_scalar (YamlNode *node)
{
    if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
        return NULL;
    return yaml_node_get_scalar (node);
}

static gboolean
rd_uint (Reader      *rd,
         YamlNode    *node,
         const gchar *path,
         guint64      min,
         guint64      max,
         guint       *out)
{
    const gchar *text = rd_scalar (node);
    gchar *end = NULL;
    guint64 value;

    if (text == NULL || text[0] == '\0' || text[0] == '-' || text[0] == '+')
        return rd_fail (rd, path, "expected a whole number");
    value = g_ascii_strtoull (text, &end, 10);
    if (end == NULL || *end != '\0')
        return rd_fail (rd, path, "expected a whole number");
    if (value < min || value > max)
    {
        g_autofree gchar *msg = g_strdup_printf ("expected a whole number from %" G_GUINT64_FORMAT
                                                 " to %" G_GUINT64_FORMAT, min, max);

        return rd_fail (rd, path, msg);
    }
    *out = (guint) value;
    return TRUE;
}

static gboolean
rd_int (Reader      *rd,
        YamlNode    *node,
        const gchar *path,
        gint        *out)
{
    const gchar *text = rd_scalar (node);
    gchar *end = NULL;
    gint64 value;

    if (text == NULL || text[0] == '\0')
        return rd_fail (rd, path, "expected a whole number");
    value = g_ascii_strtoll (text, &end, 10);
    if (end == NULL || *end != '\0' || value < G_MININT || value > G_MAXINT)
        return rd_fail (rd, path, "expected a whole number");
    *out = (gint) value;
    return TRUE;
}

static gboolean
rd_float (Reader      *rd,
          YamlNode    *node,
          const gchar *path,
          gfloat      *out)
{
    const gchar *text = rd_scalar (node);
    gchar *end = NULL;
    gdouble value;

    if (text == NULL || text[0] == '\0')
        return rd_fail (rd, path, "expected a number");
    value = g_ascii_strtod (text, &end);
    if (end == NULL || *end != '\0' || !isfinite (value))
        return rd_fail (rd, path, "expected a number");
    *out = (gfloat) value;
    return TRUE;
}

/* a sequence of exactly @n elements */
static YamlSequence *
rd_sequence (Reader      *rd,
             YamlNode    *node,
             const gchar *path,
             guint        n,
             const gchar *what)
{
    YamlSequence *seq;

    if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
    {
        rd_fail (rd, path, what);
        return NULL;
    }
    seq = yaml_node_get_sequence (node);
    if (n > 0 && yaml_sequence_get_length (seq) != n)
    {
        rd_fail (rd, path, what);
        return NULL;
    }
    return seq;
}

/* tile flag names, in bit order */
static const struct
{
    LrgTileProperty  flag;
    const gchar     *name;
} flag_names[] = {
    { LRG_TILE_PROPERTY_SOLID, "solid" },
    { LRG_TILE_PROPERTY_ANIMATED, "animated" },
    { LRG_TILE_PROPERTY_HAZARD, "hazard" },
};

static gboolean
read_tile_entry (Reader      *rd,
                 LrgTileset  *self,
                 YamlNode    *node,
                 const gchar *path)
{
    YamlMapping *map;
    TileData *td;
    guint tile_id = 0;
    guint i;

    if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
        return rd_fail (rd, path, "expected a mapping");
    map = yaml_node_get_mapping (node);
    if (!yaml_mapping_has_member (map, "id"))
        return rd_fail (rd, path, "missing key \"id\"");
    {
        g_autofree gchar *p = g_strdup_printf ("%s/id", path);

        if (self->tile_count == 0)
            return rd_fail (rd, p, "the tileset has no tiles (tile_count is 0)");
        if (!rd_uint (rd, yaml_mapping_get_member (map, "id"), p, 0, self->tile_count - 1, &tile_id))
            return FALSE;
    }
    if (peek_tile_data (self, tile_id) != NULL ||
        g_array_index (self->tile_properties, LrgTileProperty, tile_id) != 0)
    {
        g_autofree gchar *msg = g_strdup_printf ("tile %u is listed twice", tile_id);

        return rd_fail (rd, path, msg);
    }
    td = ensure_tile_data (self, tile_id);

    for (i = 0; i < yaml_mapping_get_size (map); i++)
    {
        const gchar *key = yaml_mapping_get_key (map, i);
        YamlNode *value = yaml_mapping_get_value (map, i);
        g_autofree gchar *p = g_strdup_printf ("%s/%s", path, key);

        if (g_strcmp0 (key, "id") == 0)
            continue;
        else if (g_strcmp0 (key, "flags") == 0)
        {
            YamlSequence *seq = rd_sequence (rd, value, p, 0, "expected a list of flags");
            LrgTileProperty props = 0;
            guint k;

            if (seq == NULL)
                return FALSE;
            for (k = 0; k < yaml_sequence_get_length (seq); k++)
            {
                const gchar *text = rd_scalar (yaml_sequence_get_element (seq, k));
                gboolean found = FALSE;
                guint f;

                for (f = 0; f < G_N_ELEMENTS (flag_names); f++)
                {
                    if (g_strcmp0 (text, flag_names[f].name) == 0)
                    {
                        props |= flag_names[f].flag;
                        found = TRUE;
                    }
                }
                if (!found)
                {
                    g_autofree gchar *fp = g_strdup_printf ("%s/%u", p, k);

                    return rd_fail (rd, fp, "expected solid, animated or hazard");
                }
            }
            g_array_index (self->tile_properties, LrgTileProperty, tile_id) |= props;
        }
        else if (g_strcmp0 (key, "collision") == 0)
        {
            YamlSequence *seq = rd_sequence (rd, value, p, 0, "expected a list of polygons");
            guint k;

            if (seq == NULL)
                return FALSE;
            for (k = 0; k < yaml_sequence_get_length (seq); k++)
            {
                g_autofree gchar *pp = g_strdup_printf ("%s/%u", p, k);
                YamlSequence *coords = rd_sequence (rd, yaml_sequence_get_element (seq, k), pp, 0,
                                                    "expected a list of x, y coordinates");
                GArray *poly;
                guint c;

                if (coords == NULL)
                    return FALSE;
                if (yaml_sequence_get_length (coords) < 6 || yaml_sequence_get_length (coords) % 2 != 0)
                    return rd_fail (rd, pp, "a polygon needs at least three x, y pairs");
                poly = g_array_new (FALSE, FALSE, sizeof (gfloat));
                g_ptr_array_add (td->polygons, poly);
                for (c = 0; c < yaml_sequence_get_length (coords); c++)
                {
                    g_autofree gchar *cp = g_strdup_printf ("%s/%u", pp, c);
                    gfloat v = 0.0f;

                    if (!rd_float (rd, yaml_sequence_get_element (coords, c), cp, &v))
                        return FALSE;
                    g_array_append_val (poly, v);
                }
            }
        }
        else if (g_strcmp0 (key, "data") == 0)
        {
            YamlMapping *data;
            guint k;

            if (value == NULL || yaml_node_get_node_type (value) != YAML_NODE_MAPPING)
                return rd_fail (rd, p, "expected a mapping of key: value");
            data = yaml_node_get_mapping (value);
            for (k = 0; k < yaml_mapping_get_size (data); k++)
            {
                const gchar *dkey = yaml_mapping_get_key (data, k);
                const gchar *text = rd_scalar (yaml_mapping_get_value (data, k));

                if (text == NULL)
                {
                    g_autofree gchar *dp = g_strdup_printf ("%s/%s", p, dkey);

                    return rd_fail (rd, dp, "expected a scalar value");
                }
                g_ptr_array_add (td->data_keys, g_strdup (dkey));
                g_hash_table_insert (td->data, g_strdup (dkey), g_strdup (text));
            }
        }
        else if (g_strcmp0 (key, "animation") == 0)
        {
            YamlSequence *seq = rd_sequence (rd, value, p, 0, "expected a list of frames");
            guint k;

            if (seq == NULL)
                return FALSE;
            for (k = 0; k < yaml_sequence_get_length (seq); k++)
            {
                g_autofree gchar *fp = g_strdup_printf ("%s/%u", p, k);
                YamlNode *fnode = yaml_sequence_get_element (seq, k);
                YamlMapping *fmap;
                TileFrame frame = { 0, 0 };
                guint m;
                gboolean have_tile = FALSE;
                gboolean have_duration = FALSE;

                if (fnode == NULL || yaml_node_get_node_type (fnode) != YAML_NODE_MAPPING)
                    return rd_fail (rd, fp, "expected {tile, duration}");
                fmap = yaml_node_get_mapping (fnode);
                for (m = 0; m < yaml_mapping_get_size (fmap); m++)
                {
                    const gchar *fkey = yaml_mapping_get_key (fmap, m);
                    g_autofree gchar *kp = g_strdup_printf ("%s/%s", fp, fkey);

                    if (g_strcmp0 (fkey, "tile") == 0)
                    {
                        if (!rd_uint (rd, yaml_mapping_get_value (fmap, m), kp, 0, self->tile_count - 1,
                                      &frame.tile))
                            return FALSE;
                        have_tile = TRUE;
                    }
                    else if (g_strcmp0 (fkey, "duration") == 0)
                    {
                        if (!rd_uint (rd, yaml_mapping_get_value (fmap, m), kp, 1, G_MAXUINT,
                                      &frame.duration_ms))
                            return FALSE;
                        have_duration = TRUE;
                    }
                    else
                    {
                        g_autofree gchar *msg = g_strdup_printf ("unknown key \"%s\" (expected tile, duration)",
                                                                 fkey);

                        return rd_fail (rd, fp, msg);
                    }
                }
                if (!have_tile || !have_duration)
                    return rd_fail (rd, fp, "a frame needs tile and duration (milliseconds)");
                g_array_append_val (td->animation, frame);
            }
            if (td->animation->len > 0)
                g_array_index (self->tile_properties, LrgTileProperty, tile_id) |= LRG_TILE_PROPERTY_ANIMATED;
        }
        else
        {
            /* unknown keys are kept and written back in place */
            if (td->extra == NULL)
                td->extra = yaml_mapping_new ();
            yaml_mapping_set_member (td->extra, key, value);
        }
    }
    return TRUE;
}

static LrgTileset *
tileset_from_parser (YamlParser   *parser,
                     const gchar  *base_dir,
                     const gchar  *source,
                     gboolean      load_texture,
                     GError      **error)
{
    g_autoptr(LrgTileset) self = NULL;
    Reader rd = { source, error };
    YamlNode *root = yaml_parser_get_root (parser);
    YamlMapping *map;
    guint tile_w = 0;
    guint tile_h = 0;
    guint columns = 0;
    guint tile_count = 0;
    guint i;

    if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
    {
        rd_fail (&rd, "/", "an .rtileset document is a mapping");
        return NULL;
    }
    map = yaml_node_get_mapping (root);

    /* the grid comes first: everything else is checked against it */
    if (!yaml_mapping_has_member (map, "tile_size"))
    {
        rd_fail (&rd, "/", "missing key \"tile_size\" ([width, height])");
        return NULL;
    }
    {
        YamlSequence *seq = rd_sequence (&rd, yaml_mapping_get_member (map, "tile_size"), "/tile_size", 2,
                                         "expected [width, height]");

        if (seq == NULL ||
            !rd_uint (&rd, yaml_sequence_get_element (seq, 0), "/tile_size/0", 1, 65536, &tile_w) ||
            !rd_uint (&rd, yaml_sequence_get_element (seq, 1), "/tile_size/1", 1, 65536, &tile_h))
            return NULL;
    }
    if (!yaml_mapping_has_member (map, "columns") ||
        !rd_uint (&rd, yaml_mapping_get_member (map, "columns"), "/columns", 1, 65536, &columns))
    {
        if (error != NULL && *error == NULL)
            rd_fail (&rd, "/", "missing key \"columns\"");
        return NULL;
    }
    if (!yaml_mapping_has_member (map, "tile_count") ||
        !rd_uint (&rd, yaml_mapping_get_member (map, "tile_count"), "/tile_count", 0, 1u << 24, &tile_count))
    {
        if (error != NULL && *error == NULL)
            rd_fail (&rd, "/", "missing key \"tile_count\"");
        return NULL;
    }

    self = lrg_tileset_new_empty (tile_w, tile_h, columns, tile_count);
    self->base_dir = g_strdup (base_dir);

    for (i = 0; i < yaml_mapping_get_size (map); i++)
    {
        const gchar *key = yaml_mapping_get_key (map, i);
        YamlNode *value = yaml_mapping_get_value (map, i);
        g_autofree gchar *p = g_strdup_printf ("/%s", key);

        if (g_strcmp0 (key, "tile_size") == 0 || g_strcmp0 (key, "columns") == 0 ||
            g_strcmp0 (key, "tile_count") == 0)
            continue;
        else if (g_strcmp0 (key, "version") == 0)
        {
            guint version = 0;

            if (!rd_uint (&rd, value, p, 1, 1, &version))
                return NULL;
        }
        else if (g_strcmp0 (key, "name") == 0 || g_strcmp0 (key, "image") == 0)
        {
            const gchar *text = rd_scalar (value);

            if (text == NULL)
            {
                rd_fail (&rd, p, "expected a string");
                return NULL;
            }
            if (key[0] == 'n')
                lrg_tileset_set_name (self, text);
            else
                lrg_tileset_set_image_path (self, text);
        }
        else if (g_strcmp0 (key, "margin") == 0)
        {
            if (!rd_uint (&rd, value, p, 0, 65536, &self->margin))
                return NULL;
        }
        else if (g_strcmp0 (key, "spacing") == 0)
        {
            if (!rd_uint (&rd, value, p, 0, 65536, &self->spacing))
                return NULL;
        }
        else if (g_strcmp0 (key, "offset") == 0)
        {
            YamlSequence *seq = rd_sequence (&rd, value, p, 2, "expected [x, y]");
            g_autofree gchar *px = g_strdup_printf ("%s/0", p);
            g_autofree gchar *py = g_strdup_printf ("%s/1", p);

            if (seq == NULL ||
                !rd_int (&rd, yaml_sequence_get_element (seq, 0), px, &self->offset_x) ||
                !rd_int (&rd, yaml_sequence_get_element (seq, 1), py, &self->offset_y))
                return NULL;
        }
        else if (g_strcmp0 (key, "tiles") == 0)
        {
            YamlSequence *seq = rd_sequence (&rd, value, p, 0, "expected a list of tile entries");
            guint k;

            if (seq == NULL)
                return NULL;
            for (k = 0; k < yaml_sequence_get_length (seq); k++)
            {
                g_autofree gchar *tp = g_strdup_printf ("%s/%u", p, k);

                if (!read_tile_entry (&rd, self, yaml_sequence_get_element (seq, k), tp))
                    return NULL;
            }
        }
        else if (g_strcmp0 (key, "rule_tiles") == 0)
        {
            YamlSequence *seq = rd_sequence (&rd, value, p, 0, "expected a list of rule tiles");
            guint k;

            if (seq == NULL)
                return NULL;
            for (k = 0; k < yaml_sequence_get_length (seq); k++)
            {
                g_autofree gchar *rp = g_strdup_printf ("%s/%u", p, k);
                g_autoptr(LrgRuleTile) rt = lrg_rule_tile_from_yaml (yaml_sequence_get_element (seq, k),
                                                                     source, rp, tile_count, error);

                if (rt == NULL)
                    return NULL;
                if (lrg_tileset_get_rule_tile_by_name (self, lrg_rule_tile_get_name (rt)) != NULL)
                {
                    g_autofree gchar *msg = g_strdup_printf ("rule tile \"%s\" is defined twice",
                                                             lrg_rule_tile_get_name (rt));

                    rd_fail (&rd, rp, msg);
                    return NULL;
                }
                lrg_tileset_add_rule_tile (self, rt);
            }
        }
        else
        {
            if (self->extra == NULL)
                self->extra = yaml_mapping_new ();
            yaml_mapping_set_member (self->extra, key, value);
        }
    }

    if (load_texture && !lrg_tileset_load_texture (self, error))
        return NULL;

    return g_steal_pointer (&self);
}

/* maps a yaml-glib load error onto the tileset domain */
static void
propagate_load_error (GError      **error,
                      GError       *local,
                      const gchar  *source)
{
    gint code = (local->domain == G_FILE_ERROR || local->domain == G_IO_ERROR)
        ? LRG_TILESET_ERROR_IO : LRG_TILESET_ERROR_PARSE;

    g_set_error (error, LRG_TILESET_ERROR, code, "%s: %s", source, local->message);
}

LrgTileset *
lrg_tileset_new_from_asset_file (const gchar  *path,
                                 gboolean      load_texture,
                                 GError      **error)
{
    g_autoptr(YamlParser) parser = NULL;
    g_autoptr(GError) local = NULL;
    g_autofree gchar *base_dir = NULL;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_file (parser, path, &local))
    {
        propagate_load_error (error, local, path);
        return NULL;
    }
    base_dir = g_path_get_dirname (path);
    return tileset_from_parser (parser, base_dir, path, load_texture, error);
}

LrgTileset *
lrg_tileset_new_from_asset_data (const gchar  *data,
                                 gssize        length,
                                 const gchar  *base_dir,
                                 const gchar  *source,
                                 gboolean      load_texture,
                                 GError      **error)
{
    g_autoptr(YamlParser) parser = NULL;
    g_autoptr(GError) local = NULL;

    g_return_val_if_fail (data != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (source == NULL)
        source = "<data>";
    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_data (parser, data, length, &local))
    {
        propagate_load_error (error, local, source);
        return NULL;
    }
    return tileset_from_parser (parser, base_dir, source, load_texture, error);
}

/* ==========================================================================
 * .rtileset writer
 * ========================================================================== */

static void
set_member (YamlMapping *map,
            const gchar *key,
            YamlNode    *node)
{
    yaml_mapping_set_member (map, key, node);
    yaml_node_unref (node);
}

static YamlNode *
int_pair (gint64 a,
          gint64 b)
{
    g_autoptr(YamlSequence) seq = yaml_sequence_new ();

    {
        g_autoptr(YamlNode) n = lrg_data_yaml_int (a);

        yaml_sequence_add_element (seq, n);
    }
    {
        g_autoptr(YamlNode) n = lrg_data_yaml_int (b);

        yaml_sequence_add_element (seq, n);
    }
    return yaml_node_new_sequence (seq);
}

static YamlNode *
tile_entry_to_yaml (LrgTileset *self,
                    guint       tile_id,
                    TileData   *td)
{
    g_autoptr(YamlMapping) map = yaml_mapping_new ();
    LrgTileProperty props = g_array_index (self->tile_properties, LrgTileProperty, tile_id);
    guint i;

    set_member (map, "id", lrg_data_yaml_int (tile_id));

    /* ANIMATED follows the frames, so only write it for frame-less tiles */
    if (td != NULL && td->animation->len > 0)
        props &= ~LRG_TILE_PROPERTY_ANIMATED;
    if (props != 0)
    {
        g_autoptr(YamlSequence) seq = yaml_sequence_new ();
        guint f;

        for (f = 0; f < G_N_ELEMENTS (flag_names); f++)
        {
            if ((props & flag_names[f].flag) != 0)
            {
                g_autoptr(YamlNode) n = lrg_data_yaml_string (flag_names[f].name);

                yaml_sequence_add_element (seq, n);
            }
        }
        set_member (map, "flags", yaml_node_new_sequence (seq));
    }

    if (td == NULL)
        return yaml_node_new_mapping (map);

    if (td->polygons->len > 0)
    {
        g_autoptr(YamlSequence) polys = yaml_sequence_new ();

        for (i = 0; i < td->polygons->len; i++)
        {
            GArray *poly = g_ptr_array_index (td->polygons, i);
            g_autoptr(YamlSequence) coords = yaml_sequence_new ();
            g_autoptr(YamlNode) node = NULL;
            guint c;

            for (c = 0; c < poly->len; c++)
            {
                g_autoptr(YamlNode) n = lrg_data_yaml_float (g_array_index (poly, gfloat, c));

                yaml_sequence_add_element (coords, n);
            }
            node = yaml_node_new_sequence (coords);
            yaml_sequence_add_element (polys, node);
        }
        set_member (map, "collision", yaml_node_new_sequence (polys));
    }

    if (td->data_keys->len > 0)
    {
        g_autoptr(YamlMapping) data = yaml_mapping_new ();

        for (i = 0; i < td->data_keys->len; i++)
        {
            const gchar *key = g_ptr_array_index (td->data_keys, i);

            set_member (data, key, lrg_data_yaml_string (g_hash_table_lookup (td->data, key)));
        }
        set_member (map, "data", yaml_node_new_mapping (data));
    }

    if (td->animation->len > 0)
    {
        g_autoptr(YamlSequence) frames = yaml_sequence_new ();

        for (i = 0; i < td->animation->len; i++)
        {
            TileFrame *frame = &g_array_index (td->animation, TileFrame, i);
            g_autoptr(YamlMapping) fmap = yaml_mapping_new ();
            g_autoptr(YamlNode) node = NULL;

            set_member (fmap, "tile", lrg_data_yaml_int (frame->tile));
            set_member (fmap, "duration", lrg_data_yaml_int (frame->duration_ms));
            node = yaml_node_new_mapping (fmap);
            yaml_sequence_add_element (frames, node);
        }
        set_member (map, "animation", yaml_node_new_sequence (frames));
    }

    if (td->extra != NULL)
        for (i = 0; i < yaml_mapping_get_size (td->extra); i++)
            yaml_mapping_set_member (map, yaml_mapping_get_key (td->extra, i),
                                     yaml_mapping_get_value (td->extra, i));

    return yaml_node_new_mapping (map);
}

static YamlNode *
tileset_to_yaml (LrgTileset *self)
{
    g_autoptr(YamlMapping) root = yaml_mapping_new ();
    g_autoptr(YamlSequence) tiles = yaml_sequence_new ();
    guint i;

    set_member (root, "version", lrg_data_yaml_int (1));
    if (self->name != NULL)
        set_member (root, "name", lrg_data_yaml_string (self->name));
    if (self->image_path != NULL)
        set_member (root, "image", lrg_data_yaml_string (self->image_path));
    set_member (root, "tile_size", int_pair (self->tile_width, self->tile_height));
    set_member (root, "columns", lrg_data_yaml_int (self->columns));
    set_member (root, "tile_count", lrg_data_yaml_int (self->tile_count));
    if (self->margin != 0)
        set_member (root, "margin", lrg_data_yaml_int (self->margin));
    if (self->spacing != 0)
        set_member (root, "spacing", lrg_data_yaml_int (self->spacing));
    if (self->offset_x != 0 || self->offset_y != 0)
        set_member (root, "offset", int_pair (self->offset_x, self->offset_y));

    /* tiles with anything to say, ascending */
    for (i = 0; i < self->tile_count; i++)
    {
        TileData *td = peek_tile_data (self, i);
        LrgTileProperty props = g_array_index (self->tile_properties, LrgTileProperty, i);
        g_autoptr(YamlNode) node = NULL;

        if (props == 0 && tile_data_is_empty (td))
            continue;
        node = tile_entry_to_yaml (self, i, td);
        yaml_sequence_add_element (tiles, node);
    }
    if (yaml_sequence_get_length (tiles) > 0)
    {
        g_autoptr(YamlNode) node = yaml_node_new_sequence (tiles);

        yaml_mapping_set_member (root, "tiles", node);
    }

    if (self->rule_tiles->len > 0)
    {
        g_autoptr(YamlSequence) seq = yaml_sequence_new ();

        for (i = 0; i < self->rule_tiles->len; i++)
        {
            g_autoptr(YamlNode) node = lrg_rule_tile_to_yaml (g_ptr_array_index (self->rule_tiles, i));

            yaml_sequence_add_element (seq, node);
        }
        set_member (root, "rule_tiles", yaml_node_new_sequence (seq));
    }

    if (self->extra != NULL)
        for (i = 0; i < yaml_mapping_get_size (self->extra); i++)
            yaml_mapping_set_member (root, yaml_mapping_get_key (self->extra, i),
                                     yaml_mapping_get_value (self->extra, i));

    return yaml_node_new_mapping (root);
}

gchar *
lrg_tileset_save_to_data (LrgTileset  *self,
                          gsize       *length,
                          GError     **error)
{
    g_autoptr(YamlNode) root = NULL;

    g_return_val_if_fail (LRG_IS_TILESET (self), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    root = tileset_to_yaml (self);
    return lrg_data_yaml_to_data (root, length, error);
}

gboolean
lrg_tileset_save_to_file (LrgTileset   *self,
                          const gchar  *path,
                          GError      **error)
{
    g_autoptr(YamlNode) root = NULL;
    g_autoptr(GError) local = NULL;

    g_return_val_if_fail (LRG_IS_TILESET (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    root = tileset_to_yaml (self);
    if (!lrg_data_yaml_to_file (root, path, &local))
    {
        g_set_error (error, LRG_TILESET_ERROR, LRG_TILESET_ERROR_IO, "%s: %s", path, local->message);
        return FALSE;
    }
    return TRUE;
}
