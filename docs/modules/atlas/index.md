# Texture Atlas Module

The Texture Atlas module provides tools for packing multiple textures into atlases, parsing sprite sheets, and rendering 9-slice/9-patch scalable UI elements.

## Overview

Texture atlases improve rendering performance by reducing texture switches:

- **LrgAtlasRegion** - Defines a rectangular region within an atlas (boxed)
- **LrgTextureAtlas** - Packed texture containing multiple regions
- **LrgSpriteSheet** - Parses grid-based or metadata sprite sheets
- **LrgNineSlice** - Scalable UI sprites with fixed corners
- **LrgAtlasPacker** - Build-time tool for creating atlases

## Quick Start

```c
/* Load a texture atlas */
LrgTextureAtlas *atlas = lrg_asset_manager_load_atlas (assets, "ui-atlas", NULL);

/* Get a region by name */
LrgAtlasRegion *button_region = lrg_texture_atlas_get_region (atlas, "button-normal");

/* Draw the region */
lrg_texture_atlas_draw_region (atlas, button_region, x, y);

/* Load a sprite sheet for animation */
LrgSpriteSheet *sheet = lrg_asset_manager_load_sprite_sheet (assets, "player", NULL);
LrgAtlasRegion *frame = lrg_sprite_sheet_get_frame (sheet, "walk", 3);
```

## Texture Atlas

```c
/* Create atlas manually */
LrgTextureAtlas *atlas = lrg_texture_atlas_new ();
lrg_texture_atlas_load (atlas, "textures/ui-atlas.png", NULL);
lrg_texture_atlas_load_metadata (atlas, "textures/ui-atlas.yaml", NULL);

/* Query regions */
LrgAtlasRegion *icon = lrg_texture_atlas_get_region (atlas, "health-icon");
GPtrArray *all_buttons = lrg_texture_atlas_get_regions_matching (atlas, "button-*");

/* Draw at position with optional transforms */
lrg_texture_atlas_draw_region (atlas, icon, 100, 50);
lrg_texture_atlas_draw_region_ex (atlas, icon, 100, 50, 0.0f, 2.0f, 2.0f);
```

## Atlas Region

```c
/* Region structure */
struct _LrgAtlasRegion {
    gchar  *name;
    gint    x, y;
    gint    width, height;
    gint    original_width, original_height;  /* Pre-trim size */
    gint    offset_x, offset_y;               /* Trim offset */
    gboolean rotated;                         /* 90-degree rotation */
};

/* Create programmatically */
LrgAtlasRegion *region = lrg_atlas_region_new ("icon", 0, 0, 32, 32);
```

## Sprite Sheet

Parse sprite sheets from various formats:

```c
/* Grid-based sheet (uniform frames) */
LrgSpriteSheet *sheet = lrg_sprite_sheet_new_grid (
    "player.png",
    64, 64,    /* Frame size */
    8, 4       /* Columns, rows */
);

/* Load with metadata (Aseprite, TexturePacker, etc.) */
LrgSpriteSheet *sheet = lrg_sprite_sheet_new ();
lrg_sprite_sheet_load (sheet, "player.png", NULL);
lrg_sprite_sheet_load_metadata (sheet, "player.json", LRG_SPRITE_SHEET_FORMAT_ASEPRITE, NULL);

/* Get animation frames */
LrgAtlasRegion *frame = lrg_sprite_sheet_get_frame (sheet, "walk", 0);
guint frame_count = lrg_sprite_sheet_get_frame_count (sheet, "walk");
gfloat duration = lrg_sprite_sheet_get_animation_duration (sheet, "walk");
```

## Nine-Slice (9-Patch)

Scalable UI elements with fixed corners:

```c
/* Create a nine-slice from an atlas region */
LrgNineSlice *panel = lrg_nine_slice_new (atlas, "panel");
lrg_nine_slice_set_borders (panel, 16, 16, 16, 16);  /* left, top, right, bottom */

/* Draw at any size - corners stay fixed */
lrg_nine_slice_draw (panel, x, y, 200, 150);
lrg_nine_slice_draw_tiled (panel, x, y, 200, 150);  /* Tile center instead of stretch */
```

## Atlas Packer (Build Tool)

Pack loose textures into an atlas:

```c
/* Create packer */
LrgAtlasPacker *packer = lrg_atlas_packer_new ();
lrg_atlas_packer_set_max_size (packer, 2048, 2048);
lrg_atlas_packer_set_padding (packer, 2);
lrg_atlas_packer_set_power_of_two (packer, TRUE);

/* Add textures */
lrg_atlas_packer_add_file (packer, "textures/button.png", "button");
lrg_atlas_packer_add_file (packer, "textures/icon.png", "icon");
lrg_atlas_packer_add_directory (packer, "textures/items/", "item-");

/* Pack and save */
lrg_atlas_packer_pack (packer, NULL);
lrg_atlas_packer_save (packer, "output/atlas.png", "output/atlas.yaml", NULL);
```

## YAML Metadata Format

```yaml
# atlas.yaml
texture: atlas.png
size: { width: 1024, height: 1024 }
regions:
  - name: button-normal
    x: 0
    y: 0
    width: 128
    height: 48
  - name: button-hover
    x: 128
    y: 0
    width: 128
    height: 48
  - name: icon-health
    x: 0
    y: 48
    width: 32
    height: 32
    nine-slice: { left: 8, top: 8, right: 8, bottom: 8 }
```

## Sprite Sheet Formats

| Format | Description |
|--------|-------------|
| `GRID` | Uniform grid, no metadata needed |
| `ASEPRITE` | Aseprite JSON export |
| `TEXTUREPACKER` | TexturePacker JSON/XML |
| `LIBREGNUM` | Native YAML format |

## Key Classes

| Class | Description |
|-------|-------------|
| `LrgAtlasRegion` | Region definition (boxed) |
| `LrgTextureAtlas` | Packed texture atlas |
| `LrgSpriteSheet` | Animation sprite sheet |
| `LrgNineSlice` | Scalable UI sprite |
| `LrgAtlasPacker` | Build-time packer |

## Files

| File | Description |
|------|-------------|
| `src/atlas/lrg-atlas-region.h` | Region boxed type |
| `src/atlas/lrg-texture-atlas.h` | Atlas class |
| `src/atlas/lrg-sprite-sheet.h` | Sprite sheet parser |
| `src/atlas/lrg-nine-slice.h` | 9-slice renderer |
| `src/atlas/lrg-atlas-packer.h` | Build tool |
