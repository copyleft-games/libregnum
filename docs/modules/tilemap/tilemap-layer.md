# LrgTilemapLayer - Single Tilemap Layer

A single layer of tile data for a tilemap. Represents a 2D grid of tile IDs with rendering and collision properties.

## Type

- **Final GObject Class** - Cannot be subclassed
- **Type ID** - `LRG_TYPE_TILEMAP_LAYER`
- **GIR Name** - `Libregnum.TilemapLayer`

## Constants

### LRG_TILEMAP_EMPTY_TILE
```c
#define LRG_TILEMAP_EMPTY_TILE (0)
```

Special tile ID value indicating an empty/transparent tile. This value (0) is reserved and should not be used for actual tiles.

## Construction

### lrg_tilemap_layer_new()
```c
LrgTilemapLayer * lrg_tilemap_layer_new (guint width,
                                         guint height);
```

Creates a new tilemap layer with all tiles initialized to `LRG_TILEMAP_EMPTY_TILE` (0).

**Parameters:**
- `width` - The layer width in tiles
- `height` - The layer height in tiles

**Returns:** A new `LrgTilemapLayer` (transfer full)

## Dimensions

### lrg_tilemap_layer_get_width()
```c
guint lrg_tilemap_layer_get_width (LrgTilemapLayer *self);
```

Gets the layer width in tiles.

**Parameters:**
- `self` - An `LrgTilemapLayer`

**Returns:** The width

### lrg_tilemap_layer_get_height()
```c
guint lrg_tilemap_layer_get_height (LrgTilemapLayer *self);
```

Gets the layer height in tiles.

**Parameters:**
- `self` - An `LrgTilemapLayer`

**Returns:** The height

## Tile Access

### lrg_tilemap_layer_get_tile()
```c
guint lrg_tilemap_layer_get_tile (LrgTilemapLayer *self,
                                  guint            x,
                                  guint            y);
```

Gets the tile ID at the specified position.

**Parameters:**
- `self` - An `LrgTilemapLayer`
- `x` - The tile X coordinate
- `y` - The tile Y coordinate

**Returns:** The tile ID, or `LRG_TILEMAP_EMPTY_TILE` if out of bounds

### lrg_tilemap_layer_set_tile()
```c
void lrg_tilemap_layer_set_tile (LrgTilemapLayer *self,
                                 guint            x,
                                 guint            y,
                                 guint            tile_id);
```

Sets the tile ID at the specified position. Does nothing if coordinates are out of bounds.

**Parameters:**
- `self` - An `LrgTilemapLayer`
- `x` - The tile X coordinate
- `y` - The tile Y coordinate
- `tile_id` - The tile ID to set

### lrg_tilemap_layer_fill()
```c
void lrg_tilemap_layer_fill (LrgTilemapLayer *self,
                             guint            tile_id);
```

Fills the entire layer with the specified tile ID.

**Parameters:**
- `self` - An `LrgTilemapLayer`
- `tile_id` - The tile ID to fill with

### lrg_tilemap_layer_fill_rect()
```c
void lrg_tilemap_layer_fill_rect (LrgTilemapLayer *self,
                                  guint            x,
                                  guint            y,
                                  guint            width,
                                  guint            height,
                                  guint            tile_id);
```

Fills a rectangular region with the specified tile ID. Coordinates are clamped to layer bounds.

**Parameters:**
- `self` - An `LrgTilemapLayer`
- `x` - The starting X coordinate
- `y` - The starting Y coordinate
- `width` - The rectangle width in tiles
- `height` - The rectangle height in tiles
- `tile_id` - The tile ID to fill with

### lrg_tilemap_layer_clear()
```c
void lrg_tilemap_layer_clear (LrgTilemapLayer *self);
```

Clears the layer by setting all tiles to `LRG_TILEMAP_EMPTY_TILE`.

**Parameters:**
- `self` - An `LrgTilemapLayer`

## Layer Properties

### lrg_tilemap_layer_get_visible()
```c
gboolean lrg_tilemap_layer_get_visible (LrgTilemapLayer *self);
```

Gets whether the layer is visible.

**Parameters:**
- `self` - An `LrgTilemapLayer`

**Returns:** `TRUE` if visible

### lrg_tilemap_layer_set_visible()
```c
void lrg_tilemap_layer_set_visible (LrgTilemapLayer *self,
                                    gboolean         visible);
```

Sets whether the layer is visible.

**Parameters:**
- `self` - An `LrgTilemapLayer`
- `visible` - Whether the layer is visible

### lrg_tilemap_layer_get_collision_enabled()
```c
gboolean lrg_tilemap_layer_get_collision_enabled (LrgTilemapLayer *self);
```

Gets whether collision detection is enabled for this layer.

**Parameters:**
- `self` - An `LrgTilemapLayer`

**Returns:** `TRUE` if collision is enabled

### lrg_tilemap_layer_set_collision_enabled()
```c
void lrg_tilemap_layer_set_collision_enabled (LrgTilemapLayer *self,
                                              gboolean         enabled);
```

Sets whether collision detection is enabled for this layer.

**Parameters:**
- `self` - An `LrgTilemapLayer`
- `enabled` - Whether collision is enabled

## Parallax and Visual Effects

### lrg_tilemap_layer_get_parallax_x()
```c
gfloat lrg_tilemap_layer_get_parallax_x (LrgTilemapLayer *self);
```

Gets the horizontal parallax factor. 1.0 = normal scrolling, 0.5 = half speed, 0.0 = stationary.

**Parameters:**
- `self` - An `LrgTilemapLayer`

**Returns:** The parallax factor

### lrg_tilemap_layer_set_parallax_x()
```c
void lrg_tilemap_layer_set_parallax_x (LrgTilemapLayer *self,
                                       gfloat           parallax);
```

Sets the horizontal parallax factor.

**Parameters:**
- `self` - An `LrgTilemapLayer`
- `parallax` - The parallax factor

### lrg_tilemap_layer_get_parallax_y()
```c
gfloat lrg_tilemap_layer_get_parallax_y (LrgTilemapLayer *self);
```

Gets the vertical parallax factor.

**Parameters:**
- `self` - An `LrgTilemapLayer`

**Returns:** The parallax factor

### lrg_tilemap_layer_set_parallax_y()
```c
void lrg_tilemap_layer_set_parallax_y (LrgTilemapLayer *self,
                                       gfloat           parallax);
```

Sets the vertical parallax factor.

**Parameters:**
- `self` - An `LrgTilemapLayer`
- `parallax` - The parallax factor

### lrg_tilemap_layer_get_opacity()
```c
gfloat lrg_tilemap_layer_get_opacity (LrgTilemapLayer *self);
```

Gets the layer opacity (0.0 = transparent, 1.0 = opaque).

**Parameters:**
- `self` - An `LrgTilemapLayer`

**Returns:** The opacity value

### lrg_tilemap_layer_set_opacity()
```c
void lrg_tilemap_layer_set_opacity (LrgTilemapLayer *self,
                                    gfloat           opacity);
```

Sets the layer opacity.

**Parameters:**
- `self` - An `LrgTilemapLayer`
- `opacity` - The opacity value (0.0 to 1.0)

### lrg_tilemap_layer_get_name()
```c
const gchar * lrg_tilemap_layer_get_name (LrgTilemapLayer *self);
```

Gets the layer name.

**Parameters:**
- `self` - An `LrgTilemapLayer`

**Returns:** The layer name (transfer none, nullable)

### lrg_tilemap_layer_set_name()
```c
void lrg_tilemap_layer_set_name (LrgTilemapLayer *self,
                                 const gchar     *name);
```

Sets the layer name.

**Parameters:**
- `self` - An `LrgTilemapLayer`
- `name` - The layer name (nullable)

## Tile Data Access

### lrg_tilemap_layer_get_tiles()
```c
const guint * lrg_tilemap_layer_get_tiles (LrgTilemapLayer *self,
                                           gsize           *out_len);
```

Gets direct access to the tile data array. The array is stored in row-major order (y * width + x).

**Parameters:**
- `self` - An `LrgTilemapLayer`
- `out_len` - (out optional) Return location for tile count

**Returns:** The tile data (array, transfer none)

### lrg_tilemap_layer_set_tiles()
```c
gboolean lrg_tilemap_layer_set_tiles (LrgTilemapLayer *self,
                                      const guint     *tiles,
                                      gsize            len);
```

Sets all tile data from an array. The array must contain exactly width * height elements in row-major order.

**Parameters:**
- `self` - An `LrgTilemapLayer`
- `tiles` - The tile data to copy (array)
- `len` - The number of tiles (must match width * height)

**Returns:** `TRUE` if successful, `FALSE` if @len is incorrect

## Example

```c
/* Create a layer */
g_autoptr(LrgTilemapLayer) layer = lrg_tilemap_layer_new(100, 100);
lrg_tilemap_layer_set_name(layer, "ground");

/* Fill with default tile */
lrg_tilemap_layer_fill(layer, 1);

/* Set specific tiles */
lrg_tilemap_layer_set_tile(layer, 10, 10, 5);
lrg_tilemap_layer_set_tile(layer, 11, 10, 6);

/* Enable collision detection */
lrg_tilemap_layer_set_collision_enabled(layer, TRUE);

/* Set parallax for depth effect */
lrg_tilemap_layer_set_parallax_x(layer, 0.8);
lrg_tilemap_layer_set_parallax_y(layer, 0.8);

/* Set opacity for transparency effect */
lrg_tilemap_layer_set_opacity(layer, 0.9);

/* Fill a rectangular region */
lrg_tilemap_layer_fill_rect(layer, 20, 20, 10, 10, 2);

/* Get tile count */
gsize count = 0;
const guint *tiles = lrg_tilemap_layer_get_tiles(layer, &count);
g_print("Layer has %zu tiles\n", count);
```
