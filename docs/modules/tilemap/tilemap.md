# LrgTilemap - Multi-Layer Tilemap

A multi-layer tilemap that combines multiple `LrgTilemapLayer` objects and a `LrgTileset` for complete tile-based world rendering. Supports rendering with cameras, parallax scrolling, and collision detection.

## Type

- **GObject Class** - Derivable type allowing subclassing
- **Type ID** - `LRG_TYPE_TILEMAP`
- **GIR Name** - `Libregnum.Tilemap`

## Signals

### tile-changed
```c
void (*tile_changed) (LrgTilemap    *self,
                      guint          layer_index,
                      guint          x,
                      guint          y,
                      guint          old_tile,
                      guint          new_tile);
```

Emitted when a tile value changes in any layer. Used to track dynamic tilemap updates.

## Construction

### lrg_tilemap_new()
```c
LrgTilemap * lrg_tilemap_new (LrgTileset *tileset);
```

Creates a new tilemap with the specified tileset. The tilemap starts with no layers.

**Parameters:**
- `tileset` - The tileset to use for rendering (transfer none)

**Returns:** A new `LrgTilemap` (transfer full)

## Tileset Management

### lrg_tilemap_get_tileset()
```c
LrgTileset * lrg_tilemap_get_tileset (LrgTilemap *self);
```

Gets the tileset used for rendering.

**Parameters:**
- `self` - An `LrgTilemap`

**Returns:** The tileset (transfer none)

### lrg_tilemap_set_tileset()
```c
void lrg_tilemap_set_tileset (LrgTilemap  *self,
                              LrgTileset  *tileset);
```

Sets the tileset used for rendering. This affects all layers.

**Parameters:**
- `self` - An `LrgTilemap`
- `tileset` - The new tileset (transfer none)

## Layer Management

### lrg_tilemap_add_layer()
```c
void lrg_tilemap_add_layer (LrgTilemap      *self,
                            LrgTilemapLayer *layer);
```

Adds a layer to the tilemap. Layers are rendered in the order they are added (first added = rendered first = behind).

**Parameters:**
- `self` - An `LrgTilemap`
- `layer` - The layer to add (transfer none)

### lrg_tilemap_insert_layer()
```c
void lrg_tilemap_insert_layer (LrgTilemap      *self,
                               LrgTilemapLayer *layer,
                               guint            index);
```

Inserts a layer at a specific position. Layers with lower indices are rendered first (behind layers with higher indices).

**Parameters:**
- `self` - An `LrgTilemap`
- `layer` - The layer to insert (transfer none)
- `index` - The position to insert at

### lrg_tilemap_remove_layer()
```c
void lrg_tilemap_remove_layer (LrgTilemap      *self,
                               LrgTilemapLayer *layer);
```

Removes a layer from the tilemap.

**Parameters:**
- `self` - An `LrgTilemap`
- `layer` - The layer to remove

### lrg_tilemap_remove_layer_at()
```c
void lrg_tilemap_remove_layer_at (LrgTilemap *self,
                                  guint       index);
```

Removes the layer at the specified index.

**Parameters:**
- `self` - An `LrgTilemap`
- `index` - The layer index to remove

### lrg_tilemap_get_layer()
```c
LrgTilemapLayer * lrg_tilemap_get_layer (LrgTilemap *self,
                                         guint       index);
```

Gets a layer by index.

**Parameters:**
- `self` - An `LrgTilemap`
- `index` - The layer index

**Returns:** The layer (transfer none, nullable) - `NULL` if out of bounds

### lrg_tilemap_get_layer_by_name()
```c
LrgTilemapLayer * lrg_tilemap_get_layer_by_name (LrgTilemap  *self,
                                                 const gchar *name);
```

Finds a layer by name.

**Parameters:**
- `self` - An `LrgTilemap`
- `name` - The layer name

**Returns:** The layer (transfer none, nullable) - `NULL` if not found

### lrg_tilemap_get_layer_count()
```c
guint lrg_tilemap_get_layer_count (LrgTilemap *self);
```

Gets the number of layers in the tilemap.

**Parameters:**
- `self` - An `LrgTilemap`

**Returns:** The layer count

### lrg_tilemap_get_layers()
```c
GList * lrg_tilemap_get_layers (LrgTilemap *self);
```

Gets all layers in the tilemap.

**Parameters:**
- `self` - An `LrgTilemap`

**Returns:** List of `LrgTilemapLayer` (element-type, transfer none)

## Rendering

### lrg_tilemap_draw()
```c
void lrg_tilemap_draw (LrgTilemap *self);
```

Draws all visible layers of the tilemap at position (0, 0).

**Parameters:**
- `self` - An `LrgTilemap`

### lrg_tilemap_draw_at()
```c
void lrg_tilemap_draw_at (LrgTilemap *self,
                          gfloat      x,
                          gfloat      y);
```

Draws all visible layers at the specified position.

**Parameters:**
- `self` - An `LrgTilemap`
- `x` - The X position to draw at
- `y` - The Y position to draw at

### lrg_tilemap_draw_with_camera()
```c
void lrg_tilemap_draw_with_camera (LrgTilemap  *self,
                                   GrlCamera2D *camera);
```

Draws all visible layers using a camera for view transformation. Parallax scrolling is applied based on each layer's parallax settings.

**Parameters:**
- `self` - An `LrgTilemap`
- `camera` - The camera for view transformation and parallax

### lrg_tilemap_draw_layer()
```c
void lrg_tilemap_draw_layer (LrgTilemap *self,
                             guint       layer_index,
                             gfloat      x,
                             gfloat      y);
```

Draws a specific layer at the given position.

**Parameters:**
- `self` - An `LrgTilemap`
- `layer_index` - The layer to draw
- `x` - The X position
- `y` - The Y position

## Collision Queries

### lrg_tilemap_is_solid()
```c
gboolean lrg_tilemap_is_solid (LrgTilemap *self,
                               guint       tile_x,
                               guint       tile_y);
```

Checks if any collision-enabled layer has a solid tile at the position.

**Parameters:**
- `self` - An `LrgTilemap`
- `tile_x` - The tile X coordinate
- `tile_y` - The tile Y coordinate

**Returns:** `TRUE` if there is a solid tile

### lrg_tilemap_is_solid_at()
```c
gboolean lrg_tilemap_is_solid_at (LrgTilemap *self,
                                  gfloat      world_x,
                                  gfloat      world_y);
```

Checks if there is a solid tile at the world position.

**Parameters:**
- `self` - An `LrgTilemap`
- `world_x` - The world X coordinate in pixels
- `world_y` - The world Y coordinate in pixels

**Returns:** `TRUE` if there is a solid tile

### lrg_tilemap_get_tile_at()
```c
guint lrg_tilemap_get_tile_at (LrgTilemap *self,
                               guint       layer_index,
                               gfloat      world_x,
                               gfloat      world_y);
```

Gets the tile ID at a world position for a specific layer.

**Parameters:**
- `self` - An `LrgTilemap`
- `layer_index` - The layer to query
- `world_x` - The world X coordinate in pixels
- `world_y` - The world Y coordinate in pixels

**Returns:** The tile ID, or `LRG_TILEMAP_EMPTY_TILE` if out of bounds

## World Bounds

### lrg_tilemap_get_world_bounds()
```c
GrlRectangle * lrg_tilemap_get_world_bounds (LrgTilemap *self);
```

Gets the world bounds of the tilemap in pixels. Uses the dimensions of the first layer or returns empty if no layers.

**Parameters:**
- `self` - An `LrgTilemap`

**Returns:** The bounds rectangle (transfer full)

### lrg_tilemap_get_width()
```c
guint lrg_tilemap_get_width (LrgTilemap *self);
```

Gets the tilemap width in tiles (from first layer).

**Parameters:**
- `self` - An `LrgTilemap`

**Returns:** The width in tiles

### lrg_tilemap_get_height()
```c
guint lrg_tilemap_get_height (LrgTilemap *self);
```

Gets the tilemap height in tiles (from first layer).

**Parameters:**
- `self` - An `LrgTilemap`

**Returns:** The height in tiles

### lrg_tilemap_get_pixel_width()
```c
guint lrg_tilemap_get_pixel_width (LrgTilemap *self);
```

Gets the tilemap width in pixels.

**Parameters:**
- `self` - An `LrgTilemap`

**Returns:** The width in pixels

### lrg_tilemap_get_pixel_height()
```c
guint lrg_tilemap_get_pixel_height (LrgTilemap *self);
```

Gets the tilemap height in pixels.

**Parameters:**
- `self` - An `LrgTilemap`

**Returns:** The height in pixels

## Coordinate Conversion

### lrg_tilemap_world_to_tile()
```c
void lrg_tilemap_world_to_tile (LrgTilemap *self,
                                gfloat      world_x,
                                gfloat      world_y,
                                guint      *out_tile_x,
                                guint      *out_tile_y);
```

Converts world coordinates to tile coordinates.

**Parameters:**
- `self` - An `LrgTilemap`
- `world_x` - The world X coordinate
- `world_y` - The world Y coordinate
- `out_tile_x` - (out) Return location for tile X
- `out_tile_y` - (out) Return location for tile Y

### lrg_tilemap_tile_to_world()
```c
void lrg_tilemap_tile_to_world (LrgTilemap *self,
                                guint       tile_x,
                                guint       tile_y,
                                gfloat     *out_world_x,
                                gfloat     *out_world_y);
```

Converts tile coordinates to world coordinates (top-left corner of tile).

**Parameters:**
- `self` - An `LrgTilemap`
- `tile_x` - The tile X coordinate
- `tile_y` - The tile Y coordinate
- `out_world_x` - (out) Return location for world X
- `out_world_y` - (out) Return location for world Y

## Example

```c
/* Create a simple tilemap */
g_autoptr(LrgTileset) tileset = lrg_tileset_new_from_file(
    "assets/tiles.png", 16, 16, NULL
);
g_autoptr(LrgTilemap) tilemap = lrg_tilemap_new(tileset);

/* Add ground and collision layers */
g_autoptr(LrgTilemapLayer) ground = lrg_tilemap_layer_new(50, 30);
lrg_tilemap_layer_set_name(ground, "ground");
lrg_tilemap_layer_fill(ground, 1);  /* Fill with tile 1 */
lrg_tilemap_add_layer(tilemap, ground);

g_autoptr(LrgTilemapLayer) collision = lrg_tilemap_layer_new(50, 30);
lrg_tilemap_layer_set_name(collision, "collision");
lrg_tilemap_layer_set_collision_enabled(collision, TRUE);
lrg_tilemap_add_layer(tilemap, collision);

/* Set some collision tiles */
lrg_tilemap_layer_set_tile(collision, 5, 5, 2);  /* Solid tile */

/* Check collision in game loop */
if (lrg_tilemap_is_solid_at(tilemap, player_x, player_y)) {
    /* Handle collision */
}

/* Render */
lrg_tilemap_draw_with_camera(tilemap, camera);
```
