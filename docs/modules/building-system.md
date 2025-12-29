# Building/Placement System

## Overview

The building module provides a complete grid-based building placement system for city builder, tycoon, and strategy games. It handles building definitions, placement validation, terrain requirements, rotation, upgrading, and the complete placement workflow with visual ghost preview.

## Key Concepts

### Building Definition (LrgBuildingDef)

Building definitions are templates that describe a building type. They contain:

- **Identity**: Unique ID, name, description, icon
- **Dimensions**: Width and height in grid cells
- **Category**: Production, residential, commercial, infrastructure, or decoration
- **Terrain requirements**: Which terrain types the building can be placed on
- **Upgrade levels**: Maximum level and per-level upgrade costs
- **Resource costs**: Initial construction costs

### Building Instance (LrgBuildingInstance)

Building instances are actual buildings placed in the world. They have:

- **Position**: Grid coordinates (x, y)
- **Rotation**: 0°, 90°, 180°, or 270°
- **Level**: Current upgrade level (1 to max_level)
- **Health**: Current and maximum health for damage/repair
- **Active state**: Whether the building is currently functioning
- **User data**: Custom key-value storage for game-specific data

### Build Grid (LrgBuildGrid)

The build grid manages a 2D array of cells where buildings can be placed:

- **Dimensions**: Width and height in cells
- **Cell size**: Size of each cell in world units
- **Terrain**: Each cell has a terrain type (grass, dirt, water, rock, road)
- **Occupancy**: Tracks which buildings occupy which cells
- **Blocking**: Cells can be blocked independently of buildings

### Placement System (LrgPlacementSystem)

The placement system handles the complete placement workflow:

- **Idle state**: Normal gameplay, not placing anything
- **Placing state**: Showing ghost preview, validating position
- **Demolishing state**: Selecting buildings to remove

### Placement Ghost (LrgPlacementGhost)

Visual preview that shows where a building will be placed:

- Renders a semi-transparent rectangle
- Green color for valid placement
- Red color for invalid placement
- Orange color for demolition target
- Optional grid lines

## Classes

### LrgBuildingDef

Derivable GObject that defines a building type.

```c
/* Create a building definition */
LrgBuildingDef *house = lrg_building_def_new ("house");
lrg_building_def_set_name (house, "House");
lrg_building_def_set_size (house, 2, 2);
lrg_building_def_set_category (house, LRG_BUILDING_CATEGORY_RESIDENTIAL);
lrg_building_def_set_max_level (house, 3);
lrg_building_def_set_buildable_on (house, LRG_TERRAIN_GRASS | LRG_TERRAIN_DIRT);

/* Set construction cost */
lrg_building_def_set_cost (house, "gold", 100.0);

/* Set upgrade costs */
lrg_building_def_set_upgrade_cost (house, 2, "gold", 200.0);
lrg_building_def_set_upgrade_cost (house, 3, "gold", 400.0);
```

**Virtual Methods** (for subclasses):
- `can_build(x, y)`: Additional placement validation
- `on_built(x, y)`: Called after successful construction
- `on_destroyed(x, y)`: Called when building is demolished

### LrgBuildingInstance

Derivable GObject representing a placed building.

```c
/* Create an instance from a definition */
LrgBuildingInstance *building = lrg_building_instance_new (house, 5, 10);

/* Set rotation */
lrg_building_instance_set_rotation (building, LRG_ROTATION_90);
/* Or rotate incrementally */
lrg_building_instance_rotate_cw (building);

/* Get effective dimensions (accounting for rotation) */
gint width = lrg_building_instance_get_effective_width (building);
gint height = lrg_building_instance_get_effective_height (building);

/* Upgrade */
if (lrg_building_instance_can_upgrade (building))
{
    lrg_building_instance_upgrade (building);
}

/* Health management */
lrg_building_instance_set_max_health (building, 100.0);
gboolean destroyed = lrg_building_instance_damage (building, 50.0);
lrg_building_instance_repair (building, 25.0);

/* Custom data storage */
lrg_building_instance_set_data (building, "workers", GINT_TO_POINTER (5), NULL);
gint workers = GPOINTER_TO_INT (lrg_building_instance_get_data (building, "workers"));
```

**Signals**:
- `placed`: Emitted when placed on grid
- `removed`: Emitted when removed from grid
- `upgraded`: Emitted after level increase
- `damaged`: Emitted after taking damage
- `destroyed`: Emitted when health reaches 0

### LrgBuildGrid

Final GObject managing the placement grid.

```c
/* Create a 64x64 grid with 32-pixel cells */
LrgBuildGrid *grid = lrg_build_grid_new (64, 64, 32.0);

/* Set terrain for the entire grid */
lrg_build_grid_fill_terrain (grid, LRG_TERRAIN_GRASS);

/* Create a water area */
lrg_build_grid_set_terrain_rect (grid, 20, 20, 10, 10, LRG_TERRAIN_WATER);

/* Block specific cells */
lrg_build_grid_set_blocked (grid, 5, 5, TRUE);

/* Check placement validity */
gboolean can_place = lrg_build_grid_can_place (grid, house, 10, 10, LRG_ROTATION_0);

/* Place a building */
LrgBuildingInstance *building = lrg_building_instance_new (house, 10, 10);
lrg_build_grid_place_building (grid, building);

/* Query buildings */
LrgBuildingInstance *at_cell = lrg_build_grid_get_building_at (grid, 10, 10);
GPtrArray *all = lrg_build_grid_get_all_buildings (grid);
GPtrArray *in_area = lrg_build_grid_get_buildings_in_area (grid, 0, 0, 20, 20);

/* Coordinate conversion */
gint cell_x, cell_y;
lrg_build_grid_world_to_cell (grid, 350.0, 420.0, &cell_x, &cell_y);

gdouble world_x, world_y;
lrg_build_grid_cell_to_world (grid, 10, 15, &world_x, &world_y);
```

**Signals**:
- `building-placed`: Emitted when a building is placed
- `building-removed`: Emitted when a building is removed
- `cell-changed`: Emitted when a cell's terrain or blocked state changes

### LrgPlacementSystem

Final GObject managing the placement workflow.

```c
/* Create system attached to grid */
LrgPlacementSystem *system = lrg_placement_system_new (grid);

/* Start placing a building */
lrg_placement_system_start_placement (system, house);

/* Update position based on mouse/cursor */
lrg_placement_system_update_position (system, mouse_x, mouse_y);
/* Or set directly */
lrg_placement_system_set_grid_position (system, 10, 15);

/* Rotate */
lrg_placement_system_rotate_cw (system);

/* Check validity */
if (lrg_placement_system_is_valid (system))
{
    /* Confirm placement */
    LrgBuildingInstance *placed = lrg_placement_system_confirm (system);
}
else
{
    /* Cancel */
    lrg_placement_system_cancel (system);
}

/* Demolition mode */
lrg_placement_system_start_demolition (system);
LrgBuildingInstance *demolished = lrg_placement_system_demolish_at (system, 10, 10);

/* Resource validation callback */
static gboolean
check_resources (LrgBuildingDef *def,
                 gint            level,
                 gpointer        user_data)
{
    ResourceManager *manager = user_data;
    LrgBuildCost *cost = lrg_building_def_get_cost (def);
    return resource_manager_has (manager, cost->resource_id, cost->amount);
}

lrg_placement_system_set_resource_check (system, check_resources, manager, NULL);
```

**Signals**:
- `placement-started`: Emitted when placement mode begins
- `placement-cancelled`: Emitted when placement is cancelled
- `placement-confirmed`: Emitted when a building is placed
- `building-demolished`: Emitted when a building is demolished
- `validity-changed`: Emitted when placement validity changes

### LrgPlacementGhost

Final GObject implementing LrgDrawable for visual preview.

```c
/* Create ghost attached to placement system */
LrgPlacementGhost *ghost = lrg_placement_ghost_new (system);

/* Customize colors */
GrlColor green = { 0, 255, 0, 128 };
GrlColor red = { 255, 0, 0, 128 };
lrg_placement_ghost_set_valid_color (ghost, &green);
lrg_placement_ghost_set_invalid_color (ghost, &red);

/* Show/hide grid lines */
lrg_placement_ghost_set_show_grid (ghost, TRUE);

/* Custom drawing */
static void
custom_draw (LrgPlacementGhost *ghost,
             LrgBuildingDef    *def,
             gdouble            x,
             gdouble            y,
             gdouble            w,
             gdouble            h,
             LrgRotation        rotation,
             gboolean           is_valid,
             gpointer           data)
{
    /* Draw custom building preview using graylib */
    GrlColor color = is_valid ? (GrlColor){ 0, 255, 0, 128 }
                              : (GrlColor){ 255, 0, 0, 128 };

    /* Draw building sprite or model here */
}

lrg_placement_ghost_set_draw_func (ghost, custom_draw, NULL, NULL);

/* Render in your draw loop */
lrg_drawable_draw (LRG_DRAWABLE (ghost), delta_time);
```

### LrgBuildingUI

Final GObject extending LrgContainer for building selection UI.

```c
/* Create UI */
LrgBuildingUI *ui = lrg_building_ui_new ();

/* Connect to placement system */
lrg_building_ui_set_placement_system (ui, system);

/* Register available buildings */
lrg_building_ui_register (ui, house);
lrg_building_ui_register (ui, factory);
lrg_building_ui_register (ui, road);

/* Configure layout */
lrg_building_ui_set_columns (ui, 4);
lrg_building_ui_set_button_size (ui, 64.0f);
lrg_building_ui_set_show_demolish (ui, TRUE);

/* Filter by category */
lrg_building_ui_set_category_filter (ui, LRG_BUILDING_CATEGORY_PRODUCTION);
/* Or show all */
lrg_building_ui_set_category_filter (ui, -1);

/* Rebuild UI after changes */
lrg_building_ui_rebuild (ui);

/* Handle signals */
g_signal_connect (ui, "building-selected",
                  G_CALLBACK (on_building_selected), NULL);
g_signal_connect (ui, "demolish-selected",
                  G_CALLBACK (on_demolish_selected), NULL);
```

## Enumerations

### LrgBuildingCategory

```c
typedef enum {
    LRG_BUILDING_CATEGORY_PRODUCTION,     /* Factories, farms */
    LRG_BUILDING_CATEGORY_RESIDENTIAL,    /* Houses, apartments */
    LRG_BUILDING_CATEGORY_COMMERCIAL,     /* Shops, markets */
    LRG_BUILDING_CATEGORY_INFRASTRUCTURE, /* Roads, utilities */
    LRG_BUILDING_CATEGORY_DECORATION      /* Parks, statues */
} LrgBuildingCategory;
```

### LrgTerrainFlags

```c
typedef enum {
    LRG_TERRAIN_GRASS = 1 << 0,  /* Buildable grassland */
    LRG_TERRAIN_DIRT  = 1 << 1,  /* Buildable dirt */
    LRG_TERRAIN_WATER = 1 << 2,  /* Water (usually not buildable) */
    LRG_TERRAIN_ROCK  = 1 << 3,  /* Rocky terrain */
    LRG_TERRAIN_ROAD  = 1 << 4   /* Road surface */
} LrgTerrainFlags;
```

### LrgRotation

```c
typedef enum {
    LRG_ROTATION_0   = 0,    /* No rotation */
    LRG_ROTATION_90  = 90,   /* 90° clockwise */
    LRG_ROTATION_180 = 180,  /* 180° */
    LRG_ROTATION_270 = 270   /* 270° clockwise (90° counter-clockwise) */
} LrgRotation;
```

### LrgPlacementState

```c
typedef enum {
    LRG_PLACEMENT_STATE_IDLE,        /* Not placing anything */
    LRG_PLACEMENT_STATE_PLACING,     /* Placing a building */
    LRG_PLACEMENT_STATE_DEMOLISHING  /* Demolition mode */
} LrgPlacementState;
```

## Boxed Types

### LrgBuildCost

```c
typedef struct {
    gchar   *resource_id;  /* Resource identifier */
    gdouble  amount;       /* Amount required */
} LrgBuildCost;

/* Copy and free */
LrgBuildCost *copy = lrg_build_cost_copy (cost);
lrg_build_cost_free (cost);
```

### LrgBuildCell

```c
typedef struct {
    gint                 x;         /* Grid X coordinate */
    gint                 y;         /* Grid Y coordinate */
    LrgTerrainFlags      terrain;   /* Terrain type */
    LrgBuildingInstance *building;  /* Occupying building (or NULL) */
    gboolean             blocked;   /* Manually blocked */
} LrgBuildCell;

/* Create, copy, free */
LrgBuildCell *cell = lrg_build_cell_new (5, 10);
LrgBuildCell *copy = lrg_build_cell_copy (cell);
gboolean free_cell = lrg_build_cell_is_free (cell);
lrg_build_cell_free (cell);
```

## Integration Example

Complete example showing all components working together:

```c
/* Initialize */
LrgBuildGrid *grid = lrg_build_grid_new (64, 64, 32.0);
lrg_build_grid_fill_terrain (grid, LRG_TERRAIN_GRASS);

/* Create building definitions */
LrgBuildingDef *house = lrg_building_def_new ("house");
lrg_building_def_set_name (house, "House");
lrg_building_def_set_size (house, 2, 2);
lrg_building_def_set_category (house, LRG_BUILDING_CATEGORY_RESIDENTIAL);
lrg_building_def_set_buildable_on (house, LRG_TERRAIN_GRASS);
lrg_building_def_set_cost (house, "gold", 100.0);

/* Create placement system */
LrgPlacementSystem *system = lrg_placement_system_new (grid);

/* Create visual ghost */
LrgPlacementGhost *ghost = lrg_placement_ghost_new (system);

/* Create UI */
LrgBuildingUI *ui = lrg_building_ui_new ();
lrg_building_ui_set_placement_system (ui, system);
lrg_building_ui_register (ui, house);
lrg_building_ui_rebuild (ui);

/* Game loop */
while (running)
{
    /* Update placement position from mouse */
    if (lrg_placement_system_is_placing (system))
    {
        gdouble mouse_x = get_mouse_world_x ();
        gdouble mouse_y = get_mouse_world_y ();
        lrg_placement_system_update_position (system, mouse_x, mouse_y);
    }

    /* Handle input */
    if (key_pressed ('R'))
    {
        lrg_placement_system_rotate_cw (system);
    }

    if (mouse_clicked (MOUSE_LEFT))
    {
        if (lrg_placement_system_is_valid (system))
        {
            LrgBuildingInstance *placed = lrg_placement_system_confirm (system);
            if (placed)
            {
                /* Deduct resources here */
                deduct_gold (100);
            }
        }
    }

    if (key_pressed (KEY_ESCAPE))
    {
        lrg_placement_system_cancel (system);
    }

    /* Render */
    /* ... render world ... */

    /* Draw placement ghost */
    lrg_drawable_draw (LRG_DRAWABLE (ghost), delta_time);

    /* Draw UI */
    lrg_drawable_draw (LRG_DRAWABLE (ui), delta_time);
}
```

## File Reference

| File | Description |
|------|-------------|
| `lrg-building-def.h/.c` | Building definition (template) |
| `lrg-building-instance.h/.c` | Placed building instance |
| `lrg-build-grid.h/.c` | Grid cell management |
| `lrg-placement-system.h/.c` | Placement workflow |
| `lrg-placement-ghost.h/.c` | Visual preview (LrgDrawable) |
| `lrg-building-ui.h/.c` | Building selection UI |
