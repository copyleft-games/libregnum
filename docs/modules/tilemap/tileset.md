# LrgTileset - Texture Atlas for Tilemap

A texture atlas that defines a grid of tiles extracted from a single image. The tileset provides tile rectangles for rendering and per-tile property management (collision, water, etc.).

## Type

- **Final GObject Class** - Cannot be subclassed
- **Type ID** - `LRG_TYPE_TILESET`
- **GIR Name** - `Libregnum.Tileset`

## Construction

### lrg_tileset_new()
```c
LrgTileset * lrg_tileset_new (GrlTexture *texture,
                              guint       tile_width,
                              guint       tile_height);
```

Creates a new tileset from a texture atlas. The texture is divided into a grid of tiles based on the specified tile dimensions. The number of columns is calculated from the texture width divided by @tile_width. Tiles are numbered left-to-right, top-to-bottom, starting from 0.

**Parameters:**
- `texture` - The source texture atlas (transfer none)
- `tile_width` - Width of each tile in pixels
- `tile_height` - Height of each tile in pixels

**Returns:** A new `LrgTileset` (transfer full)

### lrg_tileset_new_from_file()
```c
LrgTileset * lrg_tileset_new_from_file (const gchar  *path,
                                        guint         tile_width,
                                        guint         tile_height,
                                        GError      **error);
```

Creates a new tileset by loading a texture from a file.

**Parameters:**
- `path` - Path to the image file
- `tile_width` - Width of each tile in pixels
- `tile_height` - Height of each tile in pixels
- `error` - (optional) Return location for error

**Returns:** A new `LrgTileset` (transfer full, nullable), or `NULL` on error

## Accessors

### lrg_tileset_get_texture()
```c
GrlTexture * lrg_tileset_get_texture (LrgTileset *self);
```

Gets the underlying texture atlas.

**Parameters:**
- `self` - An `LrgTileset`

**Returns:** The texture (transfer none)

### lrg_tileset_get_tile_width()
```c
guint lrg_tileset_get_tile_width (LrgTileset *self);
```

Gets the width of each tile in pixels.

**Parameters:**
- `self` - An `LrgTileset`

**Returns:** The tile width

### lrg_tileset_get_tile_height()
```c
guint lrg_tileset_get_tile_height (LrgTileset *self);
```

Gets the height of each tile in pixels.

**Parameters:**
- `self` - An `LrgTileset`

**Returns:** The tile height

### lrg_tileset_get_columns()
```c
guint lrg_tileset_get_columns (LrgTileset *self);
```

Gets the number of tile columns in the tileset.

**Parameters:**
- `self` - An `LrgTileset`

**Returns:** The column count

### lrg_tileset_get_rows()
```c
guint lrg_tileset_get_rows (LrgTileset *self);
```

Gets the number of tile rows in the tileset.

**Parameters:**
- `self` - An `LrgTileset`

**Returns:** The row count

### lrg_tileset_get_tile_count()
```c
guint lrg_tileset_get_tile_count (LrgTileset *self);
```

Gets the total number of tiles in the tileset.

**Parameters:**
- `self` - An `LrgTileset`

**Returns:** The tile count

## Tile Rectangles

### lrg_tileset_get_tile_rect()
```c
GrlRectangle * lrg_tileset_get_tile_rect (LrgTileset *self,
                                          guint       tile_id);
```

Gets the source rectangle for a specific tile within the texture. This rectangle can be used with `grl_draw_texture_rec()` to render the tile.

**Parameters:**
- `self` - An `LrgTileset`
- `tile_id` - The tile index (0-based)

**Returns:** The tile source rectangle (transfer full, nullable), or `NULL` if @tile_id is out of bounds

### lrg_tileset_get_tile_rect_to()
```c
gboolean lrg_tileset_get_tile_rect_to (LrgTileset   *self,
                                       guint         tile_id,
                                       GrlRectangle *out_rect);
```

Gets the source rectangle for a specific tile, storing it in a caller-provided rectangle. This avoids allocation.

**Parameters:**
- `self` - An `LrgTileset`
- `tile_id` - The tile index (0-based)
- `out_rect` - (out caller-allocates) Location to store the rectangle

**Returns:** `TRUE` if successful, `FALSE` if @tile_id is out of bounds

## Tile Properties

Tile properties define behavior and collision characteristics.

### lrg_tileset_get_tile_properties()
```c
LrgTileProperty lrg_tileset_get_tile_properties (LrgTileset *self,
                                                 guint       tile_id);
```

Gets the property flags for a specific tile.

**Parameters:**
- `self` - An `LrgTileset`
- `tile_id` - The tile index (0-based)

**Returns:** The tile property flags

**Possible Property Values:**
- `LRG_TILE_PROPERTY_SOLID` - Tile blocks movement
- `LRG_TILE_PROPERTY_SPIKE` - Tile causes damage
- `LRG_TILE_PROPERTY_WATER` - Tile is water
- `LRG_TILE_PROPERTY_ICE` - Tile is slippery
- Other custom properties as defined by the game

### lrg_tileset_set_tile_properties()
```c
void lrg_tileset_set_tile_properties (LrgTileset      *self,
                                      guint            tile_id,
                                      LrgTileProperty  properties);
```

Sets the property flags for a specific tile.

**Parameters:**
- `self` - An `LrgTileset`
- `tile_id` - The tile index (0-based)
- `properties` - The property flags to set

### lrg_tileset_tile_has_property()
```c
gboolean lrg_tileset_tile_has_property (LrgTileset      *self,
                                        guint            tile_id,
                                        LrgTileProperty  property);
```

Checks if a tile has a specific property flag set.

**Parameters:**
- `self` - An `LrgTileset`
- `tile_id` - The tile index (0-based)
- `property` - The property to check

**Returns:** `TRUE` if the tile has the property

## Example

```c
/* Create a tileset from a file */
g_autoptr(LrgTileset) tileset = lrg_tileset_new_from_file(
    "assets/tileset.png",
    16,  /* tile width */
    16,  /* tile height */
    NULL
);

if (tileset == NULL) {
    g_warning("Failed to load tileset");
    return;
}

/* Get tileset info */
g_print("Tileset: %u x %u tiles, %u total\n",
        lrg_tileset_get_columns(tileset),
        lrg_tileset_get_rows(tileset),
        lrg_tileset_get_tile_count(tileset));

/* Set tile properties */
/* Tile 1 is solid */
lrg_tileset_set_tile_properties(tileset, 1, LRG_TILE_PROPERTY_SOLID);

/* Tile 2 is water */
lrg_tileset_set_tile_properties(tileset, 2, LRG_TILE_PROPERTY_WATER);

/* Tile 3 is spikes */
lrg_tileset_set_tile_properties(tileset, 3, LRG_TILE_PROPERTY_SPIKE);

/* Check tile properties */
if (lrg_tileset_tile_has_property(tileset, tile_id, LRG_TILE_PROPERTY_SOLID)) {
    g_print("Tile %u is solid\n", tile_id);
}

/* Get tile rectangle for rendering */
g_autoptr(GrlRectangle) rect = lrg_tileset_get_tile_rect(tileset, tile_id);
if (rect != NULL) {
    /* Use rect for drawing */
    grl_draw_texture_rec(texture, *rect, dest_x, dest_y);
}

/* Avoid allocation - use stack-allocated rectangle */
GrlRectangle rect_stack;
if (lrg_tileset_get_tile_rect_to(tileset, 5, &rect_stack)) {
    grl_draw_texture_rec(texture, rect_stack, 100, 100);
}
```
