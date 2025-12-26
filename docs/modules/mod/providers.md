# Provider Interfaces

## Overview

Provider interfaces allow mods to contribute specific types of content to the engine. Multiple providers can be implemented by a single mod.

## LrgEntityProvider

Provides custom entity types (characters, enemies, objects).

### Interface

```c
struct _LrgEntityProviderInterface
{
    GTypeInterface g_iface;
    GList * (*get_entity_types) (LrgEntityProvider *self);
};
```

### lrg_entity_provider_get_entity_types

```c
GList *lrg_entity_provider_get_entity_types(LrgEntityProvider *self);
```

Gets entity GTypes provided by this mod.

**Returns:** (transfer container) (element-type GType) List of entity GTypes

**Implementation:**
```c
static GList *
my_mod_get_entity_types(LrgEntityProvider *self)
{
    GList *types = NULL;
    types = g_list_append(types, GINT_TO_POINTER(MY_TYPE_PLAYER));
    types = g_list_append(types, GINT_TO_POINTER(MY_TYPE_MONSTER));
    return types;
}
```

## LrgItemProvider

Provides item definitions (equipment, consumables, etc.).

### Interface

```c
struct _LrgItemProviderInterface
{
    GTypeInterface g_iface;
    GList * (*get_item_defs) (LrgItemProvider *self);
};
```

### lrg_item_provider_get_item_defs

```c
GList *lrg_item_provider_get_item_defs(LrgItemProvider *self);
```

Gets item definitions.

**Returns:** (transfer container) (element-type LrgItemDef) Item definitions

## LrgSceneProvider

Provides game scenes (graylib GrlScene objects).

### Interface

```c
struct _LrgSceneProviderInterface
{
    GTypeInterface g_iface;
    GList * (*get_scenes) (LrgSceneProvider *self);
};
```

### lrg_scene_provider_get_scenes

```c
GList *lrg_scene_provider_get_scenes(LrgSceneProvider *self);
```

Gets scenes provided by this mod.

**Returns:** (transfer container) (element-type GObject) List of GrlScene

## LrgDialogProvider

Provides dialog trees for NPC conversations.

### Interface

```c
struct _LrgDialogProviderInterface
{
    GTypeInterface g_iface;
    GList * (*get_dialog_trees) (LrgDialogProvider *self);
};
```

### lrg_dialog_provider_get_dialog_trees

```c
GList *lrg_dialog_provider_get_dialog_trees(LrgDialogProvider *self);
```

Gets dialog trees.

**Returns:** (transfer container) (element-type LrgDialogTree) Dialog trees

## LrgQuestProvider

Provides quest definitions and objectives.

### Interface

```c
struct _LrgQuestProviderInterface
{
    GTypeInterface g_iface;
    GList * (*get_quest_defs) (LrgQuestProvider *self);
};
```

### lrg_quest_provider_get_quest_defs

```c
GList *lrg_quest_provider_get_quest_defs(LrgQuestProvider *self);
```

Gets quest definitions.

**Returns:** (transfer container) (element-type LrgQuestDef) Quest definitions

## LrgAIProvider

Provides custom behavior tree node types for AI.

### Interface

```c
struct _LrgAIProviderInterface
{
    GTypeInterface g_iface;
    GList * (*get_bt_node_types) (LrgAIProvider *self);
};
```

### lrg_ai_provider_get_bt_node_types

```c
GList *lrg_ai_provider_get_bt_node_types(LrgAIProvider *self);
```

Gets behavior tree node GTypes.

**Returns:** (transfer container) (element-type GType) Node GTypes

## LrgCommandProvider

Provides debug console commands.

### Interface

```c
struct _LrgCommandProviderInterface
{
    GTypeInterface g_iface;
    GList * (*get_commands) (LrgCommandProvider *self);
};
```

### lrg_command_provider_get_commands

```c
GList *lrg_command_provider_get_commands(LrgCommandProvider *self);
```

Gets console commands.

**Returns:** (transfer container) (element-type LrgConsoleCommand) Commands

### LrgConsoleCommand (Boxed Type)

```c
LrgConsoleCommand *lrg_console_command_new(const gchar *name, const gchar *description,
                                           LrgConsoleCommandFunc callback,
                                           gpointer user_data, GDestroyNotify destroy);
```

Creates a console command.

**Example:**
```c
static gchar *
my_command_handler(LrgDebugConsole *console, guint argc, const gchar **argv, gpointer user_data)
{
    return g_strdup("Command executed\n");
}

LrgConsoleCommand *cmd = lrg_console_command_new(
    "mycommand",
    "Does something amazing",
    my_command_handler,
    NULL,
    NULL
);
```

## LrgLocaleProvider

Provides localization data (translations).

### Interface

```c
struct _LrgLocaleProviderInterface
{
    GTypeInterface g_iface;
    GList * (*get_locales) (LrgLocaleProvider *self);
};
```

### lrg_locale_provider_get_locales

```c
GList *lrg_locale_provider_get_locales(LrgLocaleProvider *self);
```

Gets locales.

**Returns:** (transfer container) (element-type LrgLocale) Locales

## Multi-Provider Example

A mod implementing multiple providers:

```c
#define MY_TYPE_MOD (my_mod_get_type())
G_DECLARE_FINAL_TYPE(MyMod, my_mod, MY, MOD, GObject)

static GList *
my_mod_get_entity_types(LrgEntityProvider *self)
{
    GList *types = NULL;
    types = g_list_append(types, GINT_TO_POINTER(MY_TYPE_CUSTOM_PLAYER));
    return types;
}

static GList *
my_mod_get_item_defs(LrgItemProvider *self)
{
    GList *items = NULL;
    items = g_list_append(items, my_item_def_new("sword", "Golden Sword"));
    return items;
}

static void
my_entity_provider_init(LrgEntityProviderInterface *iface)
{
    iface->get_entity_types = my_mod_get_entity_types;
}

static void
my_item_provider_init(LrgItemProviderInterface *iface)
{
    iface->get_item_defs = my_mod_get_item_defs;
}

G_DEFINE_TYPE_WITH_CODE(
    MyMod, my_mod, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(LRG_TYPE_MODABLE, my_modable_interface_init)
    G_IMPLEMENT_INTERFACE(LRG_TYPE_ENTITY_PROVIDER, my_entity_provider_init)
    G_IMPLEMENT_INTERFACE(LRG_TYPE_ITEM_PROVIDER, my_item_provider_init)
)
```

## Loading Provider Content

```c
/* In game initialization */
g_autoptr(GList) entity_types = lrg_mod_manager_collect_entity_types(mod_mgr);
for (GList *l = entity_types; l; l = l->next) {
    GType entity_type = GPOINTER_TO_INT(l->data);
    register_entity_type(entity_type);
}

g_autoptr(GList) items = lrg_mod_manager_collect_item_defs(mod_mgr);
for (GList *l = items; l; l = l->next) {
    LrgItemDef *item = l->data;
    add_to_item_database(item);
}
```

## See Also

- [LrgModable](modable.md) - Base interface
- [LrgModManager](mod-manager.md) - Collects provider content
