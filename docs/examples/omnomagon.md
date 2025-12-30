# 3D Omnomagon Example

A complete playable 3D Omnomagon game demonstrating libregnum's data-driven architecture with YAML configuration, GObject type system, and graylib 3D rendering.

## Overview

This example showcases:
- **Data-driven design** - Maze layout and game configuration loaded from YAML files
- **GObject type system** - Custom game types (Player, Ghost, Pellet, Maze, Game) with properties
- **Type registration** - Using LrgRegistry to register types for YAML deserialization
- **YAML loading** - Using LrgDataLoader to load game data
- **3D rendering** - Camera setup, primitive rendering (cubes, spheres, grid)
- **Game loop** - Update/render separation with delta time
- **Collision detection** - Grid-based wall collision and circle-circle entity collision
- **Simple AI** - Ghost chase behavior

## What This Demonstrates

### Libregnum Core Features

**LrgEngine:**
- Singleton pattern for global access
- Startup/shutdown lifecycle management
- Subsystem access (Registry, DataLoader)

**LrgRegistry:**
- Type registration with string names
- Object creation from registered types
- Integration with DataLoader for YAML-based instantiation

**LrgDataLoader:**
- Loading GObjects from YAML files
- Automatic property deserialization
- Type field lookup via registry

### Graylib 3D Features

**Camera:**
- Top-down/isometric view setup
- Dynamic camera following player
- Perspective projection

**3D Primitives:**
- Cubes for walls (`grl_draw_cube`)
- Spheres for entities (`grl_draw_sphere`)
- Grid floor (`grl_draw_grid`)
- 3D lines for direction indicator (`grl_draw_line_3d`)

**Input:**
- Keyboard input (`grl_input_is_key_down`, `grl_input_is_key_pressed`)
- WASD movement controls

**Rendering:**
- 3D mode (`grl_camera3d_begin`/`end`)
- 2D UI overlay (`grl_draw_text`)
- Color management (`GrlColor`)

### Game Architecture

**Custom GObject Types:**

All types follow the standard GObject pattern with properties, init, and finalize:

- **PacPellet** - Collectible dots with position, power status, and point value
- **PacPlayer** - Player character with position, direction, score, lives, power mode
- **PacGhost** - AI-controlled enemy with position, color, state (chase/frightened/dead)
- **PacMaze** - Level data with walls, pellets, and spawn points
- **PacGame** - Game state manager coordinating all entities

## Building and Running

### Prerequisites

Build the libregnum library first:

```bash
cd /var/home/zach/Source/Projects/libregnum
make
```

### Build Example

```bash
make examples
```

This creates the executable at `build/release/examples/game-omnomagon`

### Run Example

From the `examples/` directory:

```bash
cd examples
LD_LIBRARY_PATH=../build/release/lib:../deps/graylib/build/lib:../deps/yaml-glib/build/lib:$LD_LIBRARY_PATH \
  ../build/release/examples/game-omnomagon
```

Or from the project root:

```bash
LD_LIBRARY_PATH=./build/release/lib:./deps/graylib/build/lib:./deps/yaml-glib/build/lib:$LD_LIBRARY_PATH \
  ./build/release/examples/game-omnomagon
```

## Controls

- **W/A/S/D** - Move player (up/left/down/right in the maze)
- **R** - Reset game (restart after win/lose)
- **ESC** - Quit game

## Game Rules

### Objective

Collect all pellets in the maze to win.

### Gameplay

- **Normal pellets** (white dots) - Worth 10 points each
- **Power pellets** (large yellow dots) - Worth 50 points, activate power mode for 10 seconds
- **Ghosts** - Four colored ghosts (red, pink, cyan, orange) that chase you
  - During normal play: Touching a ghost loses a life
  - During power mode: You can eat ghosts for 200 points, they turn blue and flee
- **Lives** - Start with 3 lives, lose one per ghost collision
- **Game over** - Occurs when you run out of lives
- **Victory** - Achieved by collecting all pellets

## Code Walkthrough

### Type Registration

The game registers custom types with libregnum's registry so they can be instantiated from YAML:

```c
LrgRegistry *registry = lrg_engine_get_registry(engine);
lrg_registry_register(registry, "pac-pellet", PAC_TYPE_PELLET);
lrg_registry_register(registry, "pac-player", PAC_TYPE_PLAYER);
lrg_registry_register(registry, "pac-ghost", PAC_TYPE_GHOST);
lrg_registry_register(registry, "pac-maze", PAC_TYPE_MAZE);
lrg_registry_register(registry, "pac-game", PAC_TYPE_GAME);
```

### YAML Data Loading

The maze is loaded from `data/omnomagon-maze.yaml` using the DataLoader:

```c
LrgDataLoader *loader = lrg_engine_get_data_loader(engine);
PacMaze *maze = lrg_data_loader_load_file(loader, "data/omnomagon-maze.yaml", &error);
```

The YAML file specifies:
- Maze dimensions and tile size
- Player and ghost spawn points
- Layout as ASCII art (`#`=wall, `.`=pellet, `O`=power pellet, `G`=ghost spawn)

### Maze Layout Parsing

The `pac_maze_parse_layout()` function converts the ASCII layout string into game data:

```c
static void pac_maze_parse_layout(PacMaze *self, const gchar *layout)
{
    for (row = 0; row < num_lines; row++) {
        for (col = 0; col < line_len; col++) {
            if (c == '#') {
                /* Add wall position to array */
            } else if (c == '.' || c == 'O') {
                /* Create pellet object */
            }
        }
    }
}
```

This demonstrates data-driven design - the maze structure is defined in YAML, not hardcoded.

### Collision Detection

**Wall collision** (grid-based):

```c
static gboolean check_wall_collision(PacMaze *maze, GrlVector3 *position)
{
    gint grid_x = (gint)(position->x / maze->tile_size);
    gint grid_z = (gint)(position->z / maze->tile_size);

    /* Check if grid cell contains a wall */
    for each wall {
        if (wall grid matches position grid)
            return TRUE;
    }
    return FALSE;
}
```

**Entity collision** (circle-circle):

```c
static gboolean check_entity_collision(GrlVector3 *pos1, GrlVector3 *pos2, gfloat radius)
{
    gfloat dx = pos1->x - pos2->x;
    gfloat dz = pos1->z - pos2->z;
    gfloat dist_sq = dx*dx + dz*dz;
    return dist_sq < (radius * radius);
}
```

### Player Movement

WASD input drives player movement with wall collision:

```c
static void pac_player_update(PacPlayer *self, PacMaze *maze, gfloat delta)
{
    /* Read WASD keys */
    if (grl_input_is_key_down(GRL_KEY_W)) input_dir->z -= 1.0f;
    if (grl_input_is_key_down(GRL_KEY_S)) input_dir->z += 1.0f;
    if (grl_input_is_key_down(GRL_KEY_A)) input_dir->x -= 1.0f;
    if (grl_input_is_key_down(GRL_KEY_D)) input_dir->x += 1.0f;

    /* Normalize direction and apply speed */
    new_pos = position + direction * speed * delta;

    /* Only move if no wall collision */
    if (!check_wall_collision(maze, new_pos)) {
        update position
    }
}
```

### Ghost AI

Simple chase behavior - ghosts move toward the player:

```c
static void pac_ghost_update(PacGhost *self, PacPlayer *player, PacMaze *maze, gfloat delta)
{
    switch (self->state) {
        case GHOST_STATE_CHASE:
            /* Move toward player */
            target = player->position;
            break;

        case GHOST_STATE_FRIGHTENED:
            /* Run away from player */
            target = opposite direction from player;
            break;

        case GHOST_STATE_DEAD:
            /* Return to spawn point */
            target = self->spawn_point;
            if (reached spawn) state = CHASE;
            break;
    }

    /* Calculate direction to target and move */
    direction = normalize(target - position);
    new_pos = position + direction * speed * delta;
    if (!check_wall_collision(maze, new_pos))
        update position
}
```

### Game State Management

The `pac_game_check_collisions()` function handles all game logic:

```c
static void pac_game_check_collisions(PacGame *self)
{
    /* Check pellet collection */
    for each pellet {
        if (player touching pellet) {
            mark collected
            add points to score
            if (power pellet) {
                activate power mode
                frighten all ghosts
            }
        }
    }

    /* Check ghost collisions */
    for each ghost {
        if (player touching ghost) {
            if (power mode && ghost frightened) {
                eat ghost (200 points)
                ghost state = DEAD
            } else if (ghost not dead) {
                lose life
                if (no lives left) game over
                else reset positions
            }
        }
    }

    /* Check win condition */
    if (collected all pellets)
        state = WIN
}
```

### Camera Setup

Top-down/isometric view following the player:

```c
static void setup_camera(GrlCamera3D *camera, PacPlayer *player)
{
    grl_camera3d_set_position_xyz(camera,
        player->position->x,    /* X: follow player */
        35.0f,                  /* Y: height above maze */
        player->position->z + 30.0f);  /* Z: behind player */

    grl_camera3d_set_target(camera, player->position);
    grl_camera3d_set_fovy(camera, 45.0f);
}
```

### Main Game Loop

```c
while (!grl_window_should_close(window)) {
    gfloat delta = grl_window_get_frame_time(window);

    /* Update */
    if (game->state == GAME_STATE_PLAYING)
        pac_game_update(game, delta);

    /* Reset on R key */
    if (grl_input_is_key_pressed(GRL_KEY_R))
        pac_game_reset(game);

    /* Render */
    grl_window_begin_drawing(window);
    grl_window_clear_background(window, black);

    /* 3D rendering */
    setup_camera(camera, player);
    grl_camera3d_begin(camera);
    pac_game_render(game);
    grl_camera3d_end(camera);

    /* 2D UI overlay */
    render_ui(game, window);

    grl_window_end_drawing(window);
}
```

## Extending the Example

Ideas for improvements and modifications:

### Game Features

- **Multiple levels** - Load different omnomagon-maze.yaml files for each level
- **Ghost personalities** - Different AI behaviors (Blinky aggressive, Pinky ambush, Inky erratic, Clyde coward)
- **Scatter mode** - Ghosts periodically retreat to corners
- **Fruit bonuses** - Spawn collectible fruit for extra points
- **High score persistence** - Use LrgSaveManager to save/load high scores
- **Sound effects** - Use LrgAudioManager for pellet collection, ghost eating, game over sounds
- **Music** - Background music that changes during power mode

### Visual Improvements

- **Better models** - Replace spheres with actual 3D models (use `GrlModel`)
- **Animations** - Animate player mouth opening/closing, ghost movement
- **Particle effects** - Sparkles when collecting pellets, explosion when eating ghosts
- **Lighting** - Add directional lights for better 3D appearance
- **Textures** - Apply textures to walls and floor

### Technical Improvements

- **Smooth movement** - Implement grid-snapping and smooth transitions between tiles
- **Better pathfinding** - Use A* algorithm for ghost navigation
- **State machines** - Formalize ghost AI with proper state machine
- **Configuration** - Make more settings configurable via omnomagon-config.yaml (ghost speeds, scoring, timer durations)
- **Pause menu** - Implement proper pause screen with options

### Educational Extensions

- **Custom types with signals** - Add signals to entities (e.g., "pellet-collected", "ghost-eaten")
- **Property bindings** - Bind UI elements to object properties
- **Save/load** - Implement game save/load using yaml-glib serialization
- **Network multiplayer** - Add second player or network ghost control
- **Mod support** - Allow loading custom mazes and game rules from external files

## File Structure

```
examples/
├── Makefile              # Build configuration
├── game-omnomagon.c       # Complete game implementation (1600 lines)
└── data/
    ├── omnomagon-maze.yaml         # Maze layout and spawn points
    └── omnomagon-config.yaml       # Game configuration (currently for reference)

docs/
└── examples/
    └── omnomagon.md       # This documentation
```

## Key Takeaways

This example demonstrates the power of libregnum's data-driven approach:

1. **Separation of data and code** - Maze layout is in YAML, not hardcoded
2. **Type registration enables flexibility** - New entity types can be added and loaded from YAML
3. **GObject properties enable serialization** - Objects can be saved/loaded automatically
4. **Clean architecture** - Clear separation between types, game logic, and rendering
5. **Extensibility** - Easy to add new features by defining new types and properties

The combination of libregnum's data-driven architecture and graylib's 3D rendering capabilities makes it straightforward to create complete 3D games with clean, maintainable code.
