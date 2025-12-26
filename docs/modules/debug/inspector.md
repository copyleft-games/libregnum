# LrgInspector

Runtime inspection tool for browsing and modifying game objects, components, and their properties.

## Overview

`LrgInspector` provides introspection capabilities for examining world entities, components, and GObject properties at runtime. It supports selecting objects/components and reading/writing property values.

## Singleton Access

```c
LrgInspector *inspector = lrg_inspector_get_default ();

/* Or create new instance */
LrgInspector *inspector = lrg_inspector_new ();
```

## Visibility

```c
gboolean visible = lrg_inspector_is_visible (inspector);

lrg_inspector_set_visible (inspector, TRUE);
lrg_inspector_set_visible (inspector, FALSE);
lrg_inspector_toggle (inspector);
```

## World Management

### Setting the World

```c
LrgWorld *world = lrg_world_new ();
lrg_inspector_set_world (inspector, world);

/* Retrieve world */
LrgWorld *inspected = lrg_inspector_get_world (inspector);

/* Clear world */
lrg_inspector_set_world (inspector, NULL);
```

The inspector uses a weak reference to the world, so the world can be freed independently.

## Object Browsing

### Getting Objects

```c
/* Get all objects in world */
GList *objects = lrg_inspector_get_objects (inspector);
guint count = lrg_inspector_get_object_count (inspector);

g_print ("World has %u objects\n", count);

for (GList *l = objects; l != NULL; l = l->next)
{
    LrgGameObject *obj = (LrgGameObject *)l->data;
    g_print ("  Object: %p\n", obj);
}

g_list_free (objects);
```

### Selecting Objects

```c
/* Select by reference */
LrgGameObject *object = get_player_object ();
lrg_inspector_select_object (inspector, object);

/* Select by index */
if (lrg_inspector_select_object_at (inspector, 0))
{
    g_print ("Selected first object\n");
}
else
{
    g_print ("Index out of bounds\n");
}

/* Get selected object */
LrgGameObject *selected = lrg_inspector_get_selected_object (inspector);

/* Clear selection */
lrg_inspector_clear_selection (inspector);
```

## Component Browsing

### Getting Components

Only works after selecting an object.

```c
lrg_inspector_select_object (inspector, player);

/* Get components */
GList *components = lrg_inspector_get_components (inspector);
guint count = lrg_inspector_get_component_count (inspector);

g_print ("Object has %u components\n", count);

for (GList *l = components; l != NULL; l = l->next)
{
    LrgComponent *comp = (LrgComponent *)l->data;
    g_print ("  Component: %p\n", comp);
}

g_list_free (components);
```

### Selecting Components

```c
/* Select by reference */
LrgComponent *sprite = get_sprite_component (player);
lrg_inspector_select_component (inspector, sprite);

/* Select by index */
if (lrg_inspector_select_component_at (inspector, 0))
{
    g_print ("Selected first component\n");
}

/* Get selected component */
LrgComponent *selected = lrg_inspector_get_selected_component (inspector);
```

## Property Introspection

### Getting Properties

```c
guint n_props;
GParamSpec **props = lrg_inspector_get_properties (inspector,
                                                    G_OBJECT (selected_obj),
                                                    &n_props);

for (guint i = 0; i < n_props; i++)
{
    GParamSpec *pspec = props[i];
    g_print ("Property: %s (%s)\n", pspec->name, G_VALUE_TYPE_NAME (g_param_spec_get_default_value (pspec)));
}

g_free (props);
```

### Reading Property Values

```c
GValue value = G_VALUE_INIT;

gboolean success = lrg_inspector_get_property_value (inspector,
                                                      G_OBJECT (object),
                                                      "tag",
                                                      &value);

if (success)
{
    if (G_VALUE_HOLDS_STRING (&value))
    {
        g_print ("Tag: %s\n", g_value_get_string (&value));
    }
    g_value_unset (&value);
}
```

### Property Value as String

```c
g_autofree gchar *str = lrg_inspector_get_property_string (inspector,
                                                            G_OBJECT (object),
                                                            "tag");

g_print ("Tag (as string): %s\n", str);
```

### Writing Property Values

```c
GValue value = G_VALUE_INIT;
g_value_init (&value, G_TYPE_STRING);
g_value_set_string (&value, "new_tag");

gboolean success = lrg_inspector_set_property_value (inspector,
                                                      G_OBJECT (object),
                                                      "tag",
                                                      &value);

g_value_unset (&value);
```

## Text Output

### Formatted Information

```c
/* World information */
g_autofree gchar *world_info = lrg_inspector_get_world_info (inspector);
g_print ("%s\n", world_info);
/* Output: "World with 5 objects" */

/* Selected object information */
g_autofree gchar *obj_info = lrg_inspector_get_object_info (inspector);
g_print ("%s\n", obj_info);
/* Output: "GameObject 'player': 3 components" */

/* Selected component information */
g_autofree gchar *comp_info = lrg_inspector_get_component_info (inspector);
g_print ("%s\n", comp_info);
/* Output: "SpriteComponent: texture=player.png" */

/* List of objects */
g_autofree gchar *obj_list = lrg_inspector_get_object_list (inspector);
g_print ("%s\n", obj_list);

/* List of components in selected object */
g_autofree gchar *comp_list = lrg_inspector_get_component_list (inspector);
g_print ("%s\n", comp_list);

/* Property list for object or component */
g_autofree gchar *prop_list = lrg_inspector_get_property_list (inspector,
                                                                G_OBJECT (selected_obj));
g_print ("%s\n", prop_list);
```

## Complete Example

```c
#include <libregnum.h>

void inspect_game_state (LrgWorld *world)
{
    LrgInspector *inspector = lrg_inspector_get_default ();

    lrg_inspector_set_world (inspector, world);
    lrg_inspector_set_visible (inspector, TRUE);

    /* Show world info */
    g_autofree gchar *world_info = lrg_inspector_get_world_info (inspector);
    g_print ("== World ==\n%s\n\n", world_info);

    /* Iterate objects */
    GList *objects = lrg_inspector_get_objects (inspector);
    for (GList *l = objects; l != NULL; l = l->next)
    {
        LrgGameObject *obj = (LrgGameObject *)l->data;
        lrg_inspector_select_object (inspector, obj);

        g_autofree gchar *obj_info = lrg_inspector_get_object_info (inspector);
        g_print ("== Object ==\n%s\n", obj_info);

        /* Show components */
        GList *components = lrg_inspector_get_components (inspector);
        for (GList *cl = components; cl != NULL; cl = cl->next)
        {
            LrgComponent *comp = (LrgComponent *)cl->data;
            lrg_inspector_select_component (inspector, comp);

            g_autofree gchar *comp_info = lrg_inspector_get_component_info (inspector);
            g_print ("  == Component ==\n  %s\n", comp_info);

            /* Show properties */
            guint n_props;
            GParamSpec **props = lrg_inspector_get_properties (inspector,
                                                                G_OBJECT (comp),
                                                                &n_props);

            for (guint i = 0; i < n_props; i++)
            {
                GParamSpec *pspec = props[i];
                g_autofree gchar *value_str = lrg_inspector_get_property_string (
                    inspector, G_OBJECT (comp), pspec->name);

                g_print ("    %s = %s\n", pspec->name, value_str);
            }

            g_free (props);
        }

        g_list_free (components);
        g_print ("\n");
    }

    g_list_free (objects);
}

int main (void)
{
    g_autoptr(LrgWorld) world = lrg_world_new ();
    g_autoptr(LrgGameObject) player = g_object_new (LRG_TYPE_GAME_OBJECT,
                                                     "tag", "player",
                                                     NULL);

    LrgComponent *sprite = LRG_COMPONENT (lrg_sprite_component_new ());
    lrg_game_object_add_component (player, sprite);
    lrg_world_add_object (world, player);

    inspect_game_state (world);

    return 0;
}
```

## Integration with Console

Use inspector with debug console to provide inspection commands:

```c
static gchar *
cmd_inspect (LrgDebugConsole  *console,
             guint             argc,
             const gchar     **argv,
             gpointer          user_data)
{
    LrgInspector *inspector = (LrgInspector *)user_data;

    if (argc < 2)
    {
        return g_strdup_printf ("Objects: %u",
                                lrg_inspector_get_object_count (inspector));
    }

    const gchar *cmd = argv[1];
    if (g_strcmp0 (cmd, "world") == 0)
    {
        return lrg_inspector_get_world_info (inspector);
    }

    return g_strdup ("Unknown subcommand");
}

/* Register with console */
lrg_debug_console_register_command (console, "inspect",
    "Inspect world/objects", cmd_inspect, inspector, NULL);
```
