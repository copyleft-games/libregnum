# Best Practices for Libregnum Games

This document outlines design patterns and best practices discovered while building example games with libregnum. These patterns apply broadly to any game built with the engine.

## Table of Contents

- [Data-Driven Design](#data-driven-design)
- [GObject Integration](#gobject-integration)
- [Code Organization](#code-organization)
- [Common Patterns](#common-patterns)
- [Pitfalls to Avoid](#pitfalls-to-avoid)

---

## Data-Driven Design

### ASCII Art for Spatial Layouts

**Pattern**: Use ASCII art with single-character symbols to define spatial layouts (maps, levels, grids).

**Why**:
- Visual representation matches the actual layout
- Easy to edit and understand at a glance
- Simpler than arrays of coordinates
- Version control friendly (text diff shows changes clearly)

**Implementation**:

1. **Define symbols** in YAML comments:
   ```yaml
   # Layout symbols:
   #   '#' = wall/obstacle
   #   '.' = collectible item
   #   'S' = spawn point
   #   ' ' = empty space
   ```

2. **Store as multi-line string** using YAML pipe syntax:
   ```yaml
   layout: |
     #########
     #.......#
     #.#.#.#.#
     #...S...#
     #########
   ```

3. **Parse in property setter**:
   ```c
   static void
   my_map_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
   {
       MyMap *self = MY_MAP (object);

       switch (prop_id)
       {
       case PROP_LAYOUT:
           g_free (self->layout);
           self->layout = g_value_dup_string (value);

           /* Automatically parse when layout is set */
           if (self->layout != NULL)
               my_map_parse_layout (self, self->layout);
           break;
       /* ... */
       }
   }
   ```

**Example**: See `examples/game-omnomagon.c` - `pac_maze_parse_layout()`

### Property-Based Auto-Parsing

**Pattern**: Trigger parsing automatically when properties are set, rather than manually after YAML load.

**Why**:
- Ensures parsing always happens when data changes
- Cleaner main code (no manual parse calls)
- Works with YAML deserialization automatically

**Implementation**:

```c
/* In set_property */
case PROP_LAYOUT:
    g_free (self->layout);
    self->layout = g_value_dup_string (value);

    /* Parse happens here, not in main() */
    if (self->layout != NULL)
        parse_layout_function (self, self->layout);
    break;
```

**Don't**:
```c
/* Manual parsing after load - NOT recommended */
map = load_from_yaml ("map.yaml", &error);
my_map_parse_layout (map, map->layout);  /* Fragile! */
```

### YAML Data Organization

**Prefer Simple Types**

Use simple, flat structures rather than complex nested types:

```yaml
# Good - simple types that yaml-glib handles well
map-config:
  width: 20
  height: 15
  tile-size: 1.0

  # Spatial data as ASCII layout
  layout: |
    ##########
    #........#
```

Avoid complex nested structures:

```yaml
# Problematic - arrays of custom boxed types
# May not deserialize correctly with yaml-glib
spawn-points:
  - { x: 1.5, y: 0.5, z: 2.5, type: enemy }
  - { x: 3.5, y: 0.5, z: 4.5, type: item }
```

**Property Naming Conventions**

Use kebab-case for YAML properties to match GObject conventions:

```yaml
# Correct - matches GObject property naming
tile-size: 1.0
player-spawn: { x: 5.0, y: 0.5, z: 10.0 }
```

```c
/* Corresponding GObject property */
g_param_spec_float ("tile-size", ...);
g_param_spec_boxed ("player-spawn", ...);
```

---

## GObject Integration

### Property Registration

**Critical**: Every YAML field must have a corresponding registered GObject property, or deserialization will silently fail.

**Pattern**:

1. **Add to property enum**:
   ```c
   enum {
       PROP_0,
       PROP_WIDTH,
       PROP_HEIGHT,
       PROP_LAYOUT,  /* Must match YAML field */
       N_PROPS
   };
   ```

2. **Register in class_init**:
   ```c
   properties[PROP_LAYOUT] =
       g_param_spec_string ("layout",          /* matches YAML */
                            "Layout",
                            "Map layout as ASCII art",
                            NULL,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

   g_object_class_install_properties (object_class, N_PROPS, properties);
   ```

3. **Handle in get/set_property**:
   ```c
   static void
   my_type_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
   {
       MyType *self = MY_TYPE (object);

       switch (prop_id)
       {
       case PROP_LAYOUT:
           g_free (self->layout);
           self->layout = g_value_dup_string (value);
           break;
       default:
           G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
       }
   }
   ```

### Memory Management

**Use GLib's automatic memory management helpers**:

```c
/* Automatic cleanup on scope exit */
g_autoptr(GError) error = NULL;
g_autoptr(MyType) obj = my_type_new ();
g_autofree gchar *str = g_strdup ("text");

/* No need to manually free - happens automatically */
```

**Conditional cleanup for optional pointers**:

```c
/* Finalize function */
static void
my_type_finalize (GObject *object)
{
    MyType *self = MY_TYPE (object);

    /* Safe cleanup of GObject pointers */
    g_clear_object (&self->child_object);

    /* Safe cleanup of other pointers */
    g_clear_pointer (&self->vector, grl_vector3_free);

    /* Simple pointers */
    g_free (self->string_data);

    G_OBJECT_CLASS (my_type_parent_class)->finalize (object);
}
```

**Transfer ownership with steal**:

```c
/* Transfer ownership to another container */
g_autoptr(MyObject) obj = my_object_new ();
g_ptr_array_add (array, g_steal_pointer (&obj));
/* obj is now NULL, array owns the object */
```

### Forward Declarations

**When needed**: If a function is called before its definition, add a forward declaration.

```c
G_DEFINE_TYPE (MyMap, my_map, G_TYPE_OBJECT)

/* Forward declaration - function used before definition */
static void my_map_parse_layout (MyMap *self, const gchar *layout);

static void
my_map_set_property (GObject      *object,
                     guint         prop_id,
                     const GValue *value,
                     GParamSpec   *pspec)
{
    MyMap *self = MY_MAP (object);

    switch (prop_id)
    {
    case PROP_LAYOUT:
        /* Uses function defined later - needs forward declaration */
        my_map_parse_layout (self, g_value_get_string (value));
        break;
    }
}

/* Actual definition comes after */
static void
my_map_parse_layout (MyMap     *self,
                     const gchar *layout)
{
    /* ... */
}
```

### Type Registration for YAML

**Pattern**: Register all custom GObject types that need YAML deserialization with LrgRegistry.

```c
/* In main or setup function */
LrgRegistry *registry = lrg_engine_get_registry (engine);

/* Register each custom type with its YAML type name */
lrg_registry_register (registry, "my-map", MY_TYPE_MAP);
lrg_registry_register (registry, "my-entity", MY_TYPE_ENTITY);
lrg_registry_register (registry, "my-config", MY_TYPE_CONFIG);
```

**YAML files must use registered type names**:

```yaml
# Type name must match registry
type: my-map
width: 20
height: 15
```

---

## Code Organization

### Modular Type Design

**Pattern**: One GObject type per logical game entity or system.

**Good separation**:
- **Map/Level** - Spatial data, collision, layout
- **Player** - Input handling, movement, state
- **Enemy** - AI behavior, pathfinding
- **Item** - Collectibles, pickups, effects
- **Game** - Overall state, coordination, win/lose conditions

Each type is self-contained with:
- Own data structure
- Own update logic
- Own render logic
- Clear interface for interaction

### Consistent Type Structure

**Follow this organization** for all GObject types:

```c
/* 1. Type declaration and forward refs */
typedef struct _MyEntity MyEntity;
G_DECLARE_FINAL_TYPE (MyEntity, my_entity, MY, ENTITY, GObject)

/* 2. Structure definition */
struct _MyEntity
{
    GObject parent_instance;

    /* Instance variables */
    gfloat     speed;
    GrlVector3 *position;
    GrlColor   *color;
};

/* 3. Property enum */
enum {
    PROP_0,
    PROP_SPEED,
    PROP_POSITION,
    PROP_COLOR,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* 4. G_DEFINE_TYPE */
G_DEFINE_TYPE (MyEntity, my_entity, G_TYPE_OBJECT)

/* 5. Lifecycle functions */
static void my_entity_finalize (GObject *object) { /* ... */ }
static void my_entity_get_property (/*...*/) { /* ... */ }
static void my_entity_set_property (/*...*/) { /* ... */ }
static void my_entity_class_init (MyEntityClass *klass) { /* ... */ }
static void my_entity_init (MyEntity *self) { /* ... */ }

/* 6. Public constructor */
MyEntity *my_entity_new (void) { /* ... */ }

/* 7. Helper functions (static) */
static void helper_function (MyEntity *self) { /* ... */ }

/* 8. Public API functions */
void my_entity_update (MyEntity *self, gfloat delta) { /* ... */ }
void my_entity_render (MyEntity *self) { /* ... */ }
```

---

## Common Patterns

### Layout Parsing

**Generic parsing loop** for character-based layouts:

```c
static void
parse_layout (MyMap       *self,
              const gchar *layout)
{
    gchar **lines;
    gint    row;
    gint    col;
    gint    num_lines;

    lines = g_strsplit (layout, "\n", -1);
    num_lines = g_strv_length (lines);

    for (row = 0; row < num_lines && row < self->height; row++)
    {
        gint line_len = strlen (lines[row]);

        for (col = 0; col < line_len && col < self->width; col++)
        {
            gchar  c;
            gfloat x, y, z;

            c = lines[row][col];

            /* Calculate world position from grid coordinates */
            x = col * self->tile_size + self->tile_size * 0.5f;
            z = row * self->tile_size + self->tile_size * 0.5f;
            y = 0.5f;  /* Ground level */

            /* Handle each symbol type */
            switch (c)
            {
            case '#':
                /* Wall/obstacle */
                add_wall (self, x, y, z);
                break;
            case '.':
                /* Collectible */
                add_item (self, x, y, z);
                break;
            case 'S':
                /* Spawn point */
                add_spawn (self, x, y, z);
                break;
            case ' ':
                /* Empty space - do nothing */
                break;
            }
        }
    }

    g_strfreev (lines);
}
```

### Spawn Point Management

**Store spawn points** extracted from layout:

```c
struct _MyMap
{
    /* ... */
    GArray *enemy_spawns;  /* Array of GrlVector3 */
    GArray *item_spawns;   /* Array of GrlVector3 */
};

/* In init */
static void
my_map_init (MyMap *self)
{
    self->enemy_spawns = g_array_new (FALSE, FALSE, sizeof (GrlVector3));
    self->item_spawns = g_array_new (FALSE, FALSE, sizeof (GrlVector3));
}

/* In layout parsing */
case 'E':  /* Enemy spawn marker */
{
    GrlVector3 pos = { x, y, z };
    g_array_append_val (self->enemy_spawns, pos);
}
break;
```

**Use spawn points** to create entities:

```c
/* In game setup */
for (guint i = 0; i < map->enemy_spawns->len; i++)
{
    GrlVector3 *spawn;
    MyEnemy    *enemy;

    spawn = &g_array_index (map->enemy_spawns, GrlVector3, i);
    enemy = my_enemy_new (spawn);
    g_ptr_array_add (game->enemies, enemy);
}
```

### Game State Management

**Simple state machine** pattern:

```c
typedef enum
{
    GAME_STATE_MENU,
    GAME_STATE_PLAYING,
    GAME_STATE_PAUSED,
    GAME_STATE_GAME_OVER,
    GAME_STATE_WIN
} GameState;

struct _MyGame
{
    /* ... */
    GameState state;
};

/* In update function */
void
my_game_update (MyGame *self,
                gfloat  delta)
{
    switch (self->state)
    {
    case GAME_STATE_PLAYING:
        /* Update game logic */
        update_player (self->player, delta);
        update_enemies (self->enemies, delta);
        check_win_condition (self);
        check_lose_condition (self);
        break;

    case GAME_STATE_PAUSED:
        /* Don't update game logic */
        break;

    case GAME_STATE_GAME_OVER:
    case GAME_STATE_WIN:
        /* Wait for restart input */
        if (check_restart_input ())
            reset_game (self);
        break;
    }
}
```

---

## Pitfalls to Avoid

### Silent YAML Deserialization Failures

**Problem**: YAML fields that don't match registered GObject properties are silently ignored.

**Symptoms**:
- YAML file has data, but it's not loaded
- Properties remain at default values
- No error messages

**Solution**:
1. Ensure property is registered: `g_param_spec_*("property-name", ...)`
2. Property name matches YAML field exactly (kebab-case)
3. Add debug logging to verify properties are set

```c
case PROP_LAYOUT:
    g_free (self->layout);
    self->layout = g_value_dup_string (value);
    g_debug ("Layout property set: %zu chars",
             self->layout ? strlen (self->layout) : 0);
    break;
```

### Missing Property Registration

**Problem**: Forgot to add property to enum, register it, or handle it in get/set.

**Checklist** for each YAML-loaded property:
- [ ] Added to property enum
- [ ] Registered in class_init with `g_param_spec_*`
- [ ] Handled in `set_property` function
- [ ] Handled in `get_property` function (if needed)
- [ ] Initialized in `init` function
- [ ] Cleaned up in `finalize` function (if allocated)

### Complex YAML Structures

**Problem**: Trying to deserialize complex nested structures or arrays of custom types.

**yaml-glib limitations**:
- Limited support for arrays of boxed types
- Complex nested structures may not deserialize correctly
- Custom deserialization often needed for advanced structures

**Solution**: Keep YAML simple, use ASCII layouts for spatial data.

**Instead of**:
```yaml
# Complex, may not work
walls:
  - position: { x: 1.0, y: 0.5, z: 2.0 }
    size: { width: 1.0, height: 2.0, depth: 1.0 }
  - position: { x: 3.0, y: 0.5, z: 4.0 }
    size: { width: 1.0, height: 2.0, depth: 1.0 }
```

**Use**:
```yaml
# Simple ASCII layout
layout: |
  ########
  #  #  #
  #  #  #
  ########
```

### Forgetting Forward Declarations

**Problem**: Calling a function before it's defined causes compilation errors.

**Solution**: Add forward declaration at top of type implementation.

```c
G_DEFINE_TYPE (MyType, my_type, G_TYPE_OBJECT)

/* Forward declarations for functions used before definition */
static void my_helper_function (MyType *self);
static gboolean my_check_function (MyType *self);

/* Now can use them in earlier functions */
static void
my_type_set_property (/* ... */)
{
    my_helper_function (self);  /* OK - forward declared */
}

/* Definitions come later */
static void
my_helper_function (MyType *self)
{
    /* ... */
}
```

---

## Summary

**Key Takeaways**:

1. **Use ASCII layouts** for spatial data - simpler and more visual than coordinate arrays
2. **Parse automatically** in property setters - cleaner than manual parsing
3. **Register all properties** - YAML deserialization fails silently without them
4. **Keep YAML simple** - stick to basic types, avoid complex nested structures
5. **One type per entity** - modular design with clear responsibilities
6. **Use GLib memory helpers** - `g_autoptr`, `g_clear_pointer`, `g_steal_pointer`
7. **Follow consistent structure** - makes code easier to understand and maintain

**Reference Implementation**: See `examples/game-omnomagon.c` for a complete working example demonstrating all these patterns.
