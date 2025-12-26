# LrgModable Interface

## Overview

`LrgModable` is the base interface that mods should implement to integrate with the engine lifecycle. It provides hooks for initialization, shutdown, and metadata access.

## Type Information

- **Type Name**: `LrgModable`
- **Type ID**: `LRG_TYPE_MODABLE`
- **Type Category**: Interface (glib interface)
- **Base Type**: `GObject`

## Interface Definition

```c
struct _LrgModableInterface
{
    GTypeInterface g_iface;

    gboolean       (*mod_init)      (LrgModable  *self,
                                     LrgEngine   *engine,
                                     GError     **error);
    void           (*mod_shutdown)  (LrgModable *self);
    LrgModManifest *(*mod_get_info) (LrgModable *self);
};
```

## Methods

### lrg_modable_init

```c
gboolean lrg_modable_init(LrgModable *self, LrgEngine *engine, GError **error);
```

Initializes the mod with the engine.

Called when the mod is being loaded. This is where mods should:
- Register content providers
- Create custom types
- Initialize subsystems
- Connect to engine signals

**Parameters:**
- `engine` - The engine instance (for accessing registries, etc.)
- `error` - (nullable) Return location for error

**Returns:** `TRUE` on success

**Implementation Example:**
```c
static gboolean
my_mod_init(LrgModable *self, LrgEngine *engine, GError **error)
{
    MyMod *mod = MY_MOD(self);

    /* Register custom entity types */
    LrgRegistry *registry = lrg_engine_get_registry(engine);
    lrg_registry_register(registry, "my_entity", MY_TYPE_ENTITY);

    /* Initialize subsystems */
    mod->item_system = my_item_system_new();

    g_print("MyMod initialized\n");
    return TRUE;
}
```

### lrg_modable_shutdown

```c
void lrg_modable_shutdown(LrgModable *self);
```

Shuts down the mod.

Called when the mod is being unloaded. This is where mods should:
- Clean up resources
- Disconnect from signals
- Save state if needed

**Implementation Example:**
```c
static void
my_mod_shutdown(LrgModable *self)
{
    MyMod *mod = MY_MOD(self);

    /* Clean up */
    g_clear_object(&mod->item_system);

    g_print("MyMod shut down\n");
}
```

### lrg_modable_get_info

```c
LrgModManifest *lrg_modable_get_info(LrgModable *self);
```

Gets the mod's manifest.

**Returns:** (transfer none) (nullable) The mod manifest

**Implementation Example:**
```c
static LrgModManifest *
my_mod_get_info(LrgModable *self)
{
    MyMod *mod = MY_MOD(self);
    return mod->manifest;  /* Should be non-NULL */
}
```

## Implementing a Modable

### 1. Define the Type

```c
#define MY_TYPE_MOD (my_mod_get_type())
G_DECLARE_FINAL_TYPE(MyMod, my_mod, MY, MOD, GObject)

struct _MyMod {
    GObject parent;
    LrgModManifest *manifest;
    MyItemSystem *item_system;
};
```

### 2. Implement the Interface

```c
static void
my_modable_interface_init(LrgModableInterface *iface)
{
    iface->mod_init = my_mod_init;
    iface->mod_shutdown = my_mod_shutdown;
    iface->mod_get_info = my_mod_get_info;
}

G_DEFINE_TYPE_WITH_CODE(
    MyMod, my_mod, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(LRG_TYPE_MODABLE, my_modable_interface_init)
)
```

### 3. Implement Virtual Functions

```c
static gboolean
my_mod_init(LrgModable *self, LrgEngine *engine, GError **error)
{
    MyMod *mod = MY_MOD(self);
    /* Initialize mod */
    return TRUE;
}

static void
my_mod_shutdown(LrgModable *self)
{
    MyMod *mod = MY_MOD(self);
    /* Shutdown mod */
}

static LrgModManifest *
my_mod_get_info(LrgModable *self)
{
    MyMod *mod = MY_MOD(self);
    return mod->manifest;
}
```

### 4. Implement glib-class-init

```c
static void
my_mod_class_init(MyModClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS(class);
    object_class->finalize = my_mod_finalize;
}

static void
my_mod_init(MyMod *self)
{
    self->manifest = NULL;
    self->item_system = NULL;
}
```

## Provider Interfaces

Most mods will also implement one or more provider interfaces:

```c
static void
my_mod_entity_provider_init(LrgEntityProviderInterface *iface)
{
    iface->get_entity_types = my_mod_get_entity_types;
}

G_DEFINE_TYPE_WITH_CODE(
    MyMod, my_mod, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(LRG_TYPE_MODABLE, my_modable_interface_init)
    G_IMPLEMENT_INTERFACE(LRG_TYPE_ENTITY_PROVIDER, my_mod_entity_provider_init)
)
```

## Complete Example

```c
#include <libregnum.h>

#define MY_TYPE_MOD (my_mod_get_type())
G_DECLARE_FINAL_TYPE(MyMod, my_mod, MY, MOD, GObject)

struct _MyMod {
    GObject parent;
    LrgModManifest *manifest;
};

G_DEFINE_TYPE_WITH_CODE(
    MyMod, my_mod, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(LRG_TYPE_MODABLE, my_modable_interface_init)
)

static gboolean
my_mod_init(LrgModable *self, LrgEngine *engine, GError **error)
{
    MyMod *mod = MY_MOD(self);
    g_print("MyMod init\n");
    return TRUE;
}

static void
my_mod_shutdown(LrgModable *self)
{
    MyMod *mod = MY_MOD(self);
    g_print("MyMod shutdown\n");
}

static LrgModManifest *
my_mod_get_info(LrgModable *self)
{
    MyMod *mod = MY_MOD(self);
    return mod->manifest;
}

static void
my_modable_interface_init(LrgModableInterface *iface)
{
    iface->mod_init = my_mod_init;
    iface->mod_shutdown = my_mod_shutdown;
    iface->mod_get_info = my_mod_get_info;
}

static void
my_mod_class_init(MyModClass *class)
{
}

static void
my_mod_init_instance(MyMod *self)
{
    self->manifest = NULL;
}

MyMod *
my_mod_new(LrgModManifest *manifest)
{
    MyMod *mod = g_object_new(MY_TYPE_MOD, NULL);
    mod->manifest = g_object_ref(manifest);
    return mod;
}
```

## Lifecycle

The typical mod lifecycle is:

1. **Discovery** - Mod found via manifest
2. **Construction** - Mod instance created
3. **Initialization** - `lrg_modable_init()` called
4. **Active** - Mod provides content via providers
5. **Shutdown** - `lrg_modable_shutdown()` called
6. **Destruction** - Mod object freed

## See Also

- [LrgModManager](mod-manager.md) - Manages modable instances
- [Providers](providers.md) - Content provider interfaces
