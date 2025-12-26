# Tilemap Module

The Tilemap module provides a complete 2D tile-based rendering and collision system for building game worlds. It supports multi-layered maps with parallax scrolling, tile-based collision detection, and efficient rendering.

## Overview

The Tilemap module consists of three core classes:

- **LrgTileset** - A texture atlas containing tile definitions with properties and collision information
- **LrgTilemapLayer** - A single layer of tile data with rendering and collision options
- **LrgTilemap** - A multi-layer tilemap that combines layers and tilesets for complete world rendering

## Key Features

- **Multi-layer support** - Stack multiple layers with depth ordering
- **Parallax scrolling** - Each layer has independent parallax factors for depth effects
- **Collision detection** - Per-layer collision queries and world bounds checking
- **Tile properties** - Define solid, spike, water, and custom tile properties
- **Camera integration** - Built-in support for camera transformations
- **Coordinate conversion** - Convert between world and tile coordinates
- **Layer visibility and opacity** - Control rendering per layer
- **Dynamic tile editing** - Change tile values at runtime

## Quick Start

```c
/* Create a tileset from a texture atlas */
g_autoptr(LrgTileset) tileset = lrg_tileset_new_from_file(
    "assets/tiles/tileset.png",
    16, 16,  /* tile dimensions */
    NULL
);

/* Create a tilemap */
g_autoptr(LrgTilemap) tilemap = lrg_tilemap_new(tileset);

/* Create and add layers */
g_autoptr(LrgTilemapLayer) ground = lrg_tilemap_layer_new(50, 30);
lrg_tilemap_layer_set_name(ground, "ground");
lrg_tilemap_add_layer(tilemap, ground);

/* Set some tiles */
lrg_tilemap_layer_set_tile(ground, 5, 5, 1);  /* tile ID 1 */

/* Draw the tilemap */
lrg_tilemap_draw(tilemap);
```

## Collision Detection

The tilemap supports layer-based collision detection. Tiles with the SOLID property will be detected as collisions:

```c
/* Check if a world position has a solid tile */
if (lrg_tilemap_is_solid_at(tilemap, player_x, player_y)) {
    /* Collision detected */
}

/* Convert world coordinates to tile coordinates */
guint tile_x, tile_y;
lrg_tilemap_world_to_tile(tilemap, world_x, world_y, &tile_x, &tile_y);
```

## Parallax Scrolling

Create depth effects by setting parallax factors on layers:

```c
/* Background layer moves at half speed */
lrg_tilemap_layer_set_parallax_x(background, 0.5);
lrg_tilemap_layer_set_parallax_x(midground, 0.75);
lrg_tilemap_layer_set_parallax_x(foreground, 1.0);  /* Normal speed */

/* Draw with camera - parallax is applied automatically */
lrg_tilemap_draw_with_camera(tilemap, camera);
```

## Tile Properties

Define collision and special properties per tile:

```c
/* Set tile as solid */
lrg_tileset_set_tile_properties(tileset, TILE_WALL, LRG_TILE_PROPERTY_SOLID);

/* Check if a tile has a property */
if (lrg_tileset_tile_has_property(tileset, tile_id, LRG_TILE_PROPERTY_SOLID)) {
    /* Tile is solid */
}
```

## API Reference

See the individual class documentation:

- [LrgTileset](tileset.md) - Texture atlas for tile definitions
- [LrgTilemapLayer](tilemap-layer.md) - Single layer of tile data
- [LrgTilemap](tilemap.md) - Multi-layer tilemap renderer
