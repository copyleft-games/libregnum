# LrgScriptable Interface

The `LrgScriptable` interface allows GObjects to customize their script exposure beyond the default property-based access. This is **opt-in** - objects that don't implement this interface continue to work exactly as before with automatic property exposure.

## Overview

`LrgScriptable` provides three main capabilities:

| Capability | Description |
|------------|-------------|
| **Custom Methods** | Expose callable functions to scripts (e.g., `player:attack(target)`) |
| **Access Control** | Fine-grained read/write/hidden flags per property |
| **Lifecycle Hooks** | Callbacks when objects are attached/detached from script contexts |

## When to Use LrgScriptable

**Use LrgScriptable when you need to:**

- Expose methods that aren't just property getters/setters
- Hide internal properties from scripts (sandboxing)
- Make properties read-only from scripts but writable from C
- Perform initialization when objects enter script contexts

**Don't use LrgScriptable when:**

- Default property exposure is sufficient
- You just need basic get/set access to properties
- The object is purely internal and never exposed to scripts

## Quick Start

### Defining Script Methods

```c
#include <libregnum.h>

/* Forward declare the interface init */
static void my_player_scriptable_init (LrgScriptableInterface *iface);

/* Define the type with the interface */
G_DEFINE_TYPE_WITH_CODE (MyPlayer, my_player, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (LRG_TYPE_SCRIPTABLE, my_player_scriptable_init))

/*
 * Script method: attack(target)
 * Attacks a target and returns damage dealt.
 */
static gboolean
my_player_attack (LrgScriptable  *self,
                  guint           n_args,
                  const GValue   *args,
                  GValue         *return_value,
                  GError        **error)
{
    MyPlayer *player = MY_PLAYER (self);
    GObject  *target;
    gint      damage;

    if (n_args != 1)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPE,
                     "attack() expects 1 argument");
        return FALSE;
    }

    target = g_value_get_object (&args[0]);
    if (target == NULL)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPE,
                     "attack() target cannot be nil");
        return FALSE;
    }

    /* Game logic here */
    damage = my_player_calculate_damage (player, target);
    my_player_apply_damage (target, damage);

    /* Return the damage dealt */
    g_value_init (return_value, G_TYPE_INT);
    g_value_set_int (return_value, damage);

    return TRUE;
}

/*
 * Script method: heal(amount)
 * Heals the player by the specified amount.
 */
static gboolean
my_player_heal (LrgScriptable  *self,
                guint           n_args,
                const GValue   *args,
                GValue         *return_value,
                GError        **error)
{
    MyPlayer *player = MY_PLAYER (self);
    gint      amount;

    if (n_args != 1)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPE,
                     "heal() expects 1 argument");
        return FALSE;
    }

    /* Handle different numeric types from scripts */
    if (G_VALUE_HOLDS_INT64 (&args[0]))
        amount = (gint)g_value_get_int64 (&args[0]);
    else if (G_VALUE_HOLDS_INT (&args[0]))
        amount = g_value_get_int (&args[0]);
    else if (G_VALUE_HOLDS_DOUBLE (&args[0]))
        amount = (gint)g_value_get_double (&args[0]);
    else
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPE,
                     "heal() expects a numeric argument");
        return FALSE;
    }

    player->health = MIN (player->health + amount, player->max_health);

    /* Return the new health value */
    g_value_init (return_value, G_TYPE_INT);
    g_value_set_int (return_value, player->health);

    return TRUE;
}

/* Method descriptor array - must be static and persist */
static const LrgScriptMethod player_methods[] = {
    LRG_SCRIPT_METHOD ("attack", my_player_attack, "Attack a target", 1),
    LRG_SCRIPT_METHOD ("heal", my_player_heal, "Heal self by amount", 1),
    LRG_SCRIPT_METHOD_END
};

static const LrgScriptMethod *
my_player_get_script_methods (LrgScriptable *self,
                               guint         *n_methods)
{
    *n_methods = G_N_ELEMENTS (player_methods) - 1;  /* Exclude sentinel */
    return player_methods;
}
```

### Controlling Property Access

```c
static LrgScriptAccessFlags
my_player_get_property_access (LrgScriptable *self,
                                const gchar   *property_name)
{
    /* "name" - scripts can read and write */
    if (g_strcmp0 (property_name, "name") == 0)
        return LRG_SCRIPT_ACCESS_READWRITE;

    /* "health" - scripts can only read, not modify directly */
    if (g_strcmp0 (property_name, "health") == 0)
        return LRG_SCRIPT_ACCESS_READ;

    /* "internal-state" - completely hidden from scripts */
    if (g_strcmp0 (property_name, "internal-state") == 0)
        return LRG_SCRIPT_ACCESS_NONE;

    /* For all other properties, use default behavior (GParamSpec flags) */
    return lrg_scriptable_default_get_property_access (self, property_name);
}
```

### Lifecycle Hooks

```c
static void
my_player_on_script_attach (LrgScriptable *self,
                             LrgScripting  *scripting)
{
    MyPlayer *player = MY_PLAYER (self);

    /* Initialize script-side state or register callbacks */
    g_debug ("Player %s exposed to script context", player->name);
}

static void
my_player_on_script_detach (LrgScriptable *self,
                             LrgScripting  *scripting)
{
    MyPlayer *player = MY_PLAYER (self);

    /* Clean up any script-side resources */
    g_debug ("Player %s removed from script context", player->name);
}
```

### Interface Initialization

```c
static void
my_player_scriptable_init (LrgScriptableInterface *iface)
{
    iface->get_script_methods = my_player_get_script_methods;
    iface->get_property_access = my_player_get_property_access;
    iface->on_script_attach = my_player_on_script_attach;
    iface->on_script_detach = my_player_on_script_detach;
}
```

## Script Usage

### Lua

```lua
-- Get a player object from somewhere
local player = get_player()

-- Access properties (respects access control)
print(player.name)        -- OK: readable property
print(player.health)      -- OK: read-only property
player.name = "NewName"   -- OK: writable property
-- player.health = 999    -- ERROR: not script-writable

-- Call custom methods
local damage = player:attack(enemy)
player:heal(25)

-- Access hidden properties fails
-- print(player.internal_state)  -- Returns nil or errors
```

### Python

```python
# Get a player object from somewhere
player = get_player()

# Access properties (respects access control)
print(player.name)        # OK: readable property
print(player.health)      # OK: read-only property
player.name = "NewName"   # OK: writable property
# player.health = 999     # ERROR: not script-writable

# Call custom methods
damage = player.attack(enemy)
player.heal(25)

# Access hidden properties fails
# print(player.internal_state)  # Raises error
```

## Access Flags

The `LrgScriptAccessFlags` enum controls property visibility:

| Flag | Value | Description |
|------|-------|-------------|
| `LRG_SCRIPT_ACCESS_NONE` | 0 | Property is completely hidden |
| `LRG_SCRIPT_ACCESS_READ` | 1 | Property can be read from scripts |
| `LRG_SCRIPT_ACCESS_WRITE` | 2 | Property can be written from scripts |
| `LRG_SCRIPT_ACCESS_READWRITE` | 3 | Property can be read and written |

## Method Descriptors

### LrgScriptMethod Structure

```c
struct _LrgScriptMethod
{
    const gchar         *name;      /* Method name exposed to scripts */
    LrgScriptMethodFunc  func;      /* C function to call */
    const gchar         *doc;       /* Documentation string (optional) */
    gint                 n_params;  /* Expected param count, -1 = variadic */
};
```

### Convenience Macros

```c
/* Define a method with fixed parameter count */
LRG_SCRIPT_METHOD ("attack", my_attack_func, "Attack a target", 1)

/* Define a variadic method */
LRG_SCRIPT_METHOD ("printf", my_printf_func, "Print formatted", -1)

/* Array terminator */
LRG_SCRIPT_METHOD_END
```

### Method Function Signature

```c
typedef gboolean (*LrgScriptMethodFunc) (LrgScriptable  *self,
                                          guint           n_args,
                                          const GValue   *args,
                                          GValue         *return_value,
                                          GError        **error);
```

**Parameters:**

- `self`: The scriptable object instance
- `n_args`: Number of arguments passed from the script
- `args`: Array of GValue arguments (may be NULL if n_args is 0)
- `return_value`: Uninitialized GValue for return (call g_value_init if returning)
- `error`: Location to store error on failure

**Return:**

- `TRUE` on success
- `FALSE` on error (set `error` with details)

## Default Behavior

Objects that don't implement `LrgScriptable` get automatic property exposure:

| GParamSpec Flag | Script Access |
|-----------------|---------------|
| `G_PARAM_READABLE` | Can read property |
| `G_PARAM_WRITABLE` | Can write property |
| Both | Full read/write access |

Objects implementing `LrgScriptable` can call `lrg_scriptable_default_get_property_access()` as a fallback to retain this behavior for unhandled properties.

## API Reference

### Interface Methods

#### lrg_scriptable_get_script_methods

```c
const LrgScriptMethod *
lrg_scriptable_get_script_methods (LrgScriptable *self,
                                    guint         *n_methods);
```

Returns the array of custom script methods. The array is owned by the object and must remain valid for the object's lifetime.

#### lrg_scriptable_get_property_access

```c
LrgScriptAccessFlags
lrg_scriptable_get_property_access (LrgScriptable *self,
                                     const gchar   *property_name);
```

Returns the script access flags for a property.

#### lrg_scriptable_on_script_attach

```c
void
lrg_scriptable_on_script_attach (LrgScriptable *self,
                                  LrgScripting  *scripting);
```

Called when the object is first exposed to a script context.

#### lrg_scriptable_on_script_detach

```c
void
lrg_scriptable_on_script_detach (LrgScriptable *self,
                                  LrgScripting  *scripting);
```

Called when the object is removed from a script context.

### Utility Functions

#### lrg_scriptable_find_method

```c
const LrgScriptMethod *
lrg_scriptable_find_method (LrgScriptable *self,
                             const gchar   *method_name);
```

Finds a script method by name. Returns NULL if not found.

#### lrg_scriptable_invoke_method

```c
gboolean
lrg_scriptable_invoke_method (LrgScriptable  *self,
                               const gchar    *method_name,
                               guint           n_args,
                               const GValue   *args,
                               GValue         *return_value,
                               GError        **error);
```

Invokes a script method by name from C code. Useful for C-to-C calls through the script method interface.

#### lrg_scriptable_default_get_property_access

```c
LrgScriptAccessFlags
lrg_scriptable_default_get_property_access (LrgScriptable *self,
                                             const gchar   *property_name);
```

Default implementation that returns access flags based on GParamSpec. Use this as a fallback in custom implementations.

## Complete Example

```c
/* my-player.h */
#pragma once

#include <libregnum.h>

G_BEGIN_DECLS

#define MY_TYPE_PLAYER (my_player_get_type ())
G_DECLARE_FINAL_TYPE (MyPlayer, my_player, MY, PLAYER, GObject)

MyPlayer * my_player_new (const gchar *name);

G_END_DECLS
```

```c
/* my-player.c */
#include "my-player.h"

struct _MyPlayer
{
    GObject parent_instance;

    gchar *name;
    gint   health;
    gint   max_health;
    gint   gold;           /* Internal, hidden from scripts */
};

enum
{
    PROP_0,
    PROP_NAME,
    PROP_HEALTH,
    PROP_GOLD,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void my_player_scriptable_init (LrgScriptableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MyPlayer, my_player, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (LRG_TYPE_SCRIPTABLE, my_player_scriptable_init))

/* Script methods */
static gboolean
player_take_damage (LrgScriptable  *self,
                    guint           n_args,
                    const GValue   *args,
                    GValue         *return_value,
                    GError        **error)
{
    MyPlayer *player = MY_PLAYER (self);
    gint      damage;

    if (n_args != 1)
    {
        g_set_error_literal (error, LRG_SCRIPTING_ERROR,
                             LRG_SCRIPTING_ERROR_TYPE,
                             "take_damage() expects 1 argument");
        return FALSE;
    }

    damage = (gint)g_value_get_int64 (&args[0]);
    player->health = MAX (0, player->health - damage);

    g_value_init (return_value, G_TYPE_BOOLEAN);
    g_value_set_boolean (return_value, player->health <= 0);

    return TRUE;
}

static gboolean
player_is_alive (LrgScriptable  *self,
                 guint           n_args,
                 const GValue   *args,
                 GValue         *return_value,
                 GError        **error)
{
    MyPlayer *player = MY_PLAYER (self);

    g_value_init (return_value, G_TYPE_BOOLEAN);
    g_value_set_boolean (return_value, player->health > 0);

    return TRUE;
}

static const LrgScriptMethod player_methods[] = {
    LRG_SCRIPT_METHOD ("take_damage", player_take_damage,
                       "Apply damage, returns true if dead", 1),
    LRG_SCRIPT_METHOD ("is_alive", player_is_alive,
                       "Check if player is alive", 0),
    LRG_SCRIPT_METHOD_END
};

static const LrgScriptMethod *
player_get_script_methods (LrgScriptable *self,
                            guint         *n_methods)
{
    *n_methods = G_N_ELEMENTS (player_methods) - 1;
    return player_methods;
}

static LrgScriptAccessFlags
player_get_property_access (LrgScriptable *self,
                             const gchar   *property_name)
{
    if (g_strcmp0 (property_name, "health") == 0)
        return LRG_SCRIPT_ACCESS_READ;  /* Read-only */

    if (g_strcmp0 (property_name, "gold") == 0)
        return LRG_SCRIPT_ACCESS_NONE;  /* Hidden */

    return lrg_scriptable_default_get_property_access (self, property_name);
}

static void
my_player_scriptable_init (LrgScriptableInterface *iface)
{
    iface->get_script_methods = player_get_script_methods;
    iface->get_property_access = player_get_property_access;
}

/* Standard GObject implementation follows... */
static void
my_player_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
    MyPlayer *self = MY_PLAYER (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_HEALTH:
        g_value_set_int (value, self->health);
        break;
    case PROP_GOLD:
        g_value_set_int (value, self->gold);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
my_player_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
    MyPlayer *self = MY_PLAYER (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_free (self->name);
        self->name = g_value_dup_string (value);
        break;
    case PROP_HEALTH:
        self->health = g_value_get_int (value);
        break;
    case PROP_GOLD:
        self->gold = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
my_player_finalize (GObject *object)
{
    MyPlayer *self = MY_PLAYER (object);

    g_free (self->name);

    G_OBJECT_CLASS (my_player_parent_class)->finalize (object);
}

static void
my_player_class_init (MyPlayerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = my_player_get_property;
    object_class->set_property = my_player_set_property;
    object_class->finalize = my_player_finalize;

    properties[PROP_NAME] =
        g_param_spec_string ("name", NULL, NULL, NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HEALTH] =
        g_param_spec_int ("health", NULL, NULL, 0, G_MAXINT, 100,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_GOLD] =
        g_param_spec_int ("gold", NULL, NULL, 0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
my_player_init (MyPlayer *self)
{
    self->name = NULL;
    self->health = 100;
    self->max_health = 100;
    self->gold = 0;
}

MyPlayer *
my_player_new (const gchar *name)
{
    return g_object_new (MY_TYPE_PLAYER, "name", name, NULL);
}
```

**Usage from Lua:**

```lua
local player = my_player_new("Hero")

-- Properties with access control
print(player.name)        -- "Hero"
print(player.health)      -- 100 (read-only)
-- player.health = 50     -- ERROR!
-- print(player.gold)     -- nil (hidden)

-- Custom methods
local is_dead = player:take_damage(30)
print(player.health)      -- 70
print(player:is_alive())  -- true
```

## See Also

- [Scripting Module Overview](index.md)
- [LrgScripting](scripting.md) - Base scripting interface
- [LrgScriptingLua](scripting-lua.md) - Lua implementation
- [LrgScriptingPython](scripting-python.md) - Python implementation
