# Modding Guide

## Introduction

This guide walks through creating a complete mod for Libregnum from scratch, including manifest, custom entity types, and loading in the engine.

## Step 1: Create Mod Structure

```bash
mkdir -p my_mod/data
cd my_mod
```

## Step 2: Create Manifest

Create `mod.yaml`:

```yaml
id: my_awesome_mod
name: My Awesome Mod
version: 1.0.0
author: Your Name
description: Adds cool features to the game
type: content
priority: normal
data_path: data/
dependencies:
  - id: base_game
    min_version: 1.0.0
    optional: false
```

## Step 3: Create Custom Entity Type

Create `src/my_entity.h`:

```c
#pragma once

#include <glib-object.h>

#define MY_TYPE_CUSTOM_ENEMY (my_custom_enemy_get_type())
G_DECLARE_FINAL_TYPE(MyCustomEnemy, my_custom_enemy, MY, CUSTOM_ENEMY, GObject)

MyCustomEnemy *my_custom_enemy_new(const gchar *name);
void my_custom_enemy_set_health(MyCustomEnemy *self, gint health);
gint my_custom_enemy_get_health(MyCustomEnemy *self);
```

Create `src/my_entity.c`:

```c
#include "my_entity.h"

struct _MyCustomEnemy {
    GObject parent;
    gchar *name;
    gint health;
};

G_DEFINE_TYPE(MyCustomEnemy, my_custom_enemy, G_TYPE_OBJECT)

static void
my_custom_enemy_finalize(GObject *object)
{
    MyCustomEnemy *self = MY_CUSTOM_ENEMY(object);
    g_free(self->name);
    G_OBJECT_CLASS(my_custom_enemy_parent_class)->finalize(object);
}

static void
my_custom_enemy_class_init(MyCustomEnemyClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS(class);
    object_class->finalize = my_custom_enemy_finalize;
}

static void
my_custom_enemy_init(MyCustomEnemy *self)
{
    self->name = NULL;
    self->health = 100;
}

MyCustomEnemy *
my_custom_enemy_new(const gchar *name)
{
    MyCustomEnemy *enemy = g_object_new(MY_TYPE_CUSTOM_ENEMY, NULL);
    enemy->name = g_strdup(name);
    return enemy;
}

void
my_custom_enemy_set_health(MyCustomEnemy *self, gint health)
{
    self->health = health;
}

gint
my_custom_enemy_get_health(MyCustomEnemy *self)
{
    return self->health;
}
```

## Step 4: Create Mod Class

Create `src/my_mod.h`:

```c
#pragma once

#include <libregnum.h>

#define MY_TYPE_MOD (my_mod_get_type())
G_DECLARE_FINAL_TYPE(MyMod, my_mod, MY, MOD, GObject)

MyMod *my_mod_new(void);
```

Create `src/my_mod.c`:

```c
#include "my_mod.h"
#include "my_entity.h"

struct _MyMod {
    GObject parent;
    LrgModManifest *manifest;
};

G_DEFINE_TYPE_WITH_CODE(
    MyMod, my_mod, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(LRG_TYPE_MODABLE, my_modable_interface_init)
    G_IMPLEMENT_INTERFACE(LRG_TYPE_ENTITY_PROVIDER, my_entity_provider_interface_init)
)

/* LrgModable implementation */
static gboolean
my_mod_init(LrgModable *self, LrgEngine *engine, GError **error)
{
    MyMod *mod = MY_MOD(self);
    g_print("My Awesome Mod loaded!\n");
    return TRUE;
}

static void
my_mod_shutdown(LrgModable *self)
{
    MyMod *mod = MY_MOD(self);
    g_print("My Awesome Mod unloaded\n");
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

/* LrgEntityProvider implementation */
static GList *
my_mod_get_entity_types(LrgEntityProvider *self)
{
    GList *types = NULL;
    types = g_list_append(types, GINT_TO_POINTER(MY_TYPE_CUSTOM_ENEMY));
    return types;
}

static void
my_entity_provider_interface_init(LrgEntityProviderInterface *iface)
{
    iface->get_entity_types = my_mod_get_entity_types;
}

/* GObject implementation */
static void
my_mod_finalize(GObject *object)
{
    MyMod *self = MY_MOD(object);
    g_clear_object(&self->manifest);
    G_OBJECT_CLASS(my_mod_parent_class)->finalize(object);
}

static void
my_mod_class_init(MyModClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS(class);
    object_class->finalize = my_mod_finalize;
}

static void
my_mod_init_instance(MyMod *self)
{
    /* Create manifest */
    self->manifest = lrg_mod_manifest_new("my_awesome_mod");
    lrg_mod_manifest_set_name(self->manifest, "My Awesome Mod");
    lrg_mod_manifest_set_version(self->manifest, "1.0.0");
    lrg_mod_manifest_set_author(self->manifest, "Your Name");
    lrg_mod_manifest_set_description(self->manifest, "Adds cool features");
    lrg_mod_manifest_set_mod_type(self->manifest, LRG_MOD_TYPE_CONTENT);
    lrg_mod_manifest_add_dependency(self->manifest, "base_game", "1.0.0", FALSE);
}

MyMod *
my_mod_new(void)
{
    return g_object_new(MY_TYPE_MOD, NULL);
}
```

## Step 5: Build the Mod

Create `Makefile`:

```makefile
CFLAGS = -Wall -Wextra -Werror -fPIC $(shell pkg-config --cflags glib-2.0 gobject-2.0 libregnum)
LDFLAGS = $(shell pkg-config --libs glib-2.0 gobject-2.0 libregnum)

SOURCES = src/my_entity.c src/my_mod.c
OBJECTS = $(SOURCES:.c=.o)
LIB = libmy_mod.so

all: $(LIB)

$(LIB): $(OBJECTS)
	gcc -shared -o $@ $^ $(LDFLAGS)

%.o: %.c
	gcc $(CFLAGS) -c -o $@ $<

install: $(LIB)
	install -d $(DESTDIR)/usr/lib/libregnum/mods/my_awesome_mod
	install -m 644 $(LIB) $(DESTDIR)/usr/lib/libregnum/mods/my_awesome_mod/
	install -m 644 mod.yaml $(DESTDIR)/usr/lib/libregnum/mods/my_awesome_mod/

clean:
	rm -f $(OBJECTS) $(LIB)

.PHONY: all install clean
```

Build:
```bash
make
```

## Step 6: Load the Mod in Game

```c
#include <libregnum.h>

int
main(int argc, char *argv[])
{
    g_autoptr(LrgModManager) mod_mgr = lrg_mod_manager_get_default();

    /* Add search path */
    lrg_mod_manager_add_search_path(mod_mgr, ".");

    /* Discover mods */
    g_autoptr(GError) error = NULL;
    guint discovered = lrg_mod_manager_discover(mod_mgr, &error);
    if (discovered == 0) {
        g_print("No mods found\n");
        return 1;
    }
    g_print("Discovered %u mods\n", discovered);

    /* Load all mods */
    if (!lrg_mod_manager_load_all(mod_mgr, &error)) {
        g_print("Mod loading failed: %s\n", error->message);
        return 1;
    }
    g_print("Mods loaded successfully\n");

    /* Collect entity types from mods */
    g_autoptr(GList) entity_types = lrg_mod_manager_collect_entity_types(mod_mgr);
    for (GList *l = entity_types; l; l = l->next) {
        GType entity_type = GPOINTER_TO_INT(l->data);
        const gchar *name = g_type_name(entity_type);
        g_print("Registered entity: %s\n", name);

        /* Create instance to test */
        if (entity_type == MY_TYPE_CUSTOM_ENEMY) {
            g_autoptr(MyCustomEnemy) enemy = my_custom_enemy_new("Test Enemy");
            my_custom_enemy_set_health(enemy, 50);
            g_print("  Health: %d\n", my_custom_enemy_get_health(enemy));
        }
    }

    return 0;
}
```

## Step 7: Test

```bash
./game_executable
```

Expected output:
```
Discovered 1 mods
My Awesome Mod loaded!
Mods loaded successfully
Registered entity: MyCustomEnemy
  Health: 50
```

## Common Patterns

### Accessing Game Data from Mod

```c
static gboolean
my_mod_init(LrgModable *self, LrgEngine *engine, GError **error)
{
    /* Get registry to access types */
    LrgRegistry *registry = lrg_engine_get_registry(engine);

    /* Get data loader for resources */
    LrgDataLoader *loader = lrg_engine_get_data_loader(engine);

    /* Register custom types */
    lrg_registry_register(registry, "my_enemy", MY_TYPE_CUSTOM_ENEMY);

    return TRUE;
}
```

### Adding Item Definitions

```c
static GList *
my_mod_get_item_defs(LrgItemProvider *self)
{
    GList *items = NULL;

    /* Create item definitions */
    LrgItemDef *sword = lrg_item_def_new("golden_sword", "Golden Sword");
    lrg_item_def_set_description(sword, "A powerful golden sword");
    lrg_item_def_set_value(sword, 1000);
    items = g_list_append(items, sword);

    LrgItemDef *potion = lrg_item_def_new("health_potion", "Health Potion");
    lrg_item_def_set_description(potion, "Restores 50 health");
    lrg_item_def_set_stackable(potion, TRUE);
    items = g_list_append(items, potion);

    return items;
}
```

### Loading Mod Resources

```c
static gboolean
my_mod_init(LrgModable *self, LrgEngine *engine, GError **error)
{
    MyMod *mod = MY_MOD(self);
    LrgMod *lrg_mod = /* get from somewhere */;

    /* List files in mod */
    g_autoptr(GPtrArray) files = lrg_mod_list_files(lrg_mod, "data", "*.yaml");
    for (guint i = 0; i < files->len; i++) {
        const gchar *file = g_ptr_array_index(files, i);
        g_print("Loading data file: %s\n", file);
        /* Parse and load YAML file */
    }

    return TRUE;
}
```

## Troubleshooting

### Mod not discovered
- Check manifest filename is `mod.yaml`
- Verify YAML syntax
- Check mod is in search path

### Type not registered
- Ensure `G_IMPLEMENT_INTERFACE()` is called
- Check `lrg_entity_provider_get_entity_types()` returns the type
- Verify mod is loaded before querying

### Manifest errors
- Use a YAML validator
- Check indentation (YAML is sensitive)
- Verify all required fields present

## See Also

- [LrgModManager](../modules/mod/mod-manager.md) - Mod management
- [Provider Interfaces](../modules/mod/providers.md) - Content providers
- [LrgModable](../modules/mod/modable.md) - Mod interface
