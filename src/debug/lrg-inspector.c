/* lrg-inspector.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Debug inspector implementation.
 */

#include "config.h"
#include "lrg-inspector.h"
#include "../ecs/lrg-world.h"
#include "../ecs/lrg-game-object.h"
#include "../ecs/lrg-component.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DEBUG
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgInspector
{
    GObject        parent_instance;

    gboolean       visible;
    LrgWorld      *world;               /* Weak reference */
    LrgGameObject *selected_object;     /* Weak reference */
    LrgComponent  *selected_component;  /* Weak reference */
};

#pragma GCC visibility push(default)
G_DEFINE_TYPE (LrgInspector, lrg_inspector, G_TYPE_OBJECT)
#pragma GCC visibility pop

static LrgInspector *default_inspector = NULL;

/* ==========================================================================
 * Weak Reference Callbacks
 * ========================================================================== */

static void
on_world_destroyed (gpointer  data,
                    GObject  *where_the_object_was)
{
    LrgInspector *self = LRG_INSPECTOR (data);

    self->world = NULL;
    self->selected_object = NULL;
    self->selected_component = NULL;

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Inspector world destroyed, selection cleared");
}

static void
on_object_destroyed (gpointer  data,
                     GObject  *where_the_object_was)
{
    LrgInspector *self = LRG_INSPECTOR (data);

    self->selected_object = NULL;
    self->selected_component = NULL;

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Inspector selected object destroyed");
}

static void
on_component_destroyed (gpointer  data,
                        GObject  *where_the_object_was)
{
    LrgInspector *self = LRG_INSPECTOR (data);

    self->selected_component = NULL;

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Inspector selected component destroyed");
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_inspector_finalize (GObject *object)
{
    LrgInspector *self = LRG_INSPECTOR (object);

    /* Remove weak references */
    if (self->world != NULL)
        g_object_weak_unref (G_OBJECT (self->world), on_world_destroyed, self);

    if (self->selected_object != NULL)
        g_object_weak_unref (G_OBJECT (self->selected_object), on_object_destroyed, self);

    if (self->selected_component != NULL)
        g_object_weak_unref (G_OBJECT (self->selected_component), on_component_destroyed, self);

    if (default_inspector == self)
        default_inspector = NULL;

    G_OBJECT_CLASS (lrg_inspector_parent_class)->finalize (object);
}

static void
lrg_inspector_class_init (LrgInspectorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_inspector_finalize;
}

static void
lrg_inspector_init (LrgInspector *self)
{
    self->visible = FALSE;
    self->world = NULL;
    self->selected_object = NULL;
    self->selected_component = NULL;

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Created inspector");
}

/* ==========================================================================
 * Construction and Singleton
 * ========================================================================== */

LrgInspector *
lrg_inspector_get_default (void)
{
    if (default_inspector == NULL)
        default_inspector = lrg_inspector_new ();

    return default_inspector;
}

LrgInspector *
lrg_inspector_new (void)
{
    return g_object_new (LRG_TYPE_INSPECTOR, NULL);
}

/* ==========================================================================
 * Visibility Control
 * ========================================================================== */

gboolean
lrg_inspector_is_visible (LrgInspector *self)
{
    g_return_val_if_fail (LRG_IS_INSPECTOR (self), FALSE);
    return self->visible;
}

void
lrg_inspector_set_visible (LrgInspector *self,
                           gboolean      visible)
{
    g_return_if_fail (LRG_IS_INSPECTOR (self));
    self->visible = visible;

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Inspector %s",
               visible ? "shown" : "hidden");
}

void
lrg_inspector_toggle (LrgInspector *self)
{
    g_return_if_fail (LRG_IS_INSPECTOR (self));
    lrg_inspector_set_visible (self, !self->visible);
}

/* ==========================================================================
 * World Management
 * ========================================================================== */

LrgWorld *
lrg_inspector_get_world (LrgInspector *self)
{
    g_return_val_if_fail (LRG_IS_INSPECTOR (self), NULL);
    return self->world;
}

void
lrg_inspector_set_world (LrgInspector *self,
                         LrgWorld     *world)
{
    g_return_if_fail (LRG_IS_INSPECTOR (self));

    /* Remove old weak reference */
    if (self->world != NULL)
    {
        g_object_weak_unref (G_OBJECT (self->world), on_world_destroyed, self);
        self->world = NULL;
    }

    /* Clear selection when world changes */
    lrg_inspector_clear_selection (self);

    /* Set new world with weak reference */
    if (world != NULL)
    {
        self->world = world;
        g_object_weak_ref (G_OBJECT (world), on_world_destroyed, self);
    }

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Inspector world %s",
               world != NULL ? "set" : "cleared");
}

/* ==========================================================================
 * Entity Browsing
 * ========================================================================== */

GList *
lrg_inspector_get_objects (LrgInspector *self)
{
    g_return_val_if_fail (LRG_IS_INSPECTOR (self), NULL);

    if (self->world == NULL)
        return NULL;

    return lrg_world_get_objects (self->world);
}

guint
lrg_inspector_get_object_count (LrgInspector *self)
{
    g_return_val_if_fail (LRG_IS_INSPECTOR (self), 0);

    if (self->world == NULL)
        return 0;

    return lrg_world_get_object_count (self->world);
}

void
lrg_inspector_select_object (LrgInspector  *self,
                             LrgGameObject *object)
{
    g_return_if_fail (LRG_IS_INSPECTOR (self));

    /* Remove old weak references */
    if (self->selected_object != NULL)
        g_object_weak_unref (G_OBJECT (self->selected_object), on_object_destroyed, self);

    if (self->selected_component != NULL)
    {
        g_object_weak_unref (G_OBJECT (self->selected_component), on_component_destroyed, self);
        self->selected_component = NULL;
    }

    /* Set new object with weak reference */
    self->selected_object = object;
    if (object != NULL)
    {
        g_object_weak_ref (G_OBJECT (object), on_object_destroyed, self);
        lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Selected object: %s",
                   G_OBJECT_TYPE_NAME (object));
    }
}

gboolean
lrg_inspector_select_object_at (LrgInspector *self,
                                guint         index)
{
    GList *objects;
    LrgGameObject *object;

    g_return_val_if_fail (LRG_IS_INSPECTOR (self), FALSE);

    objects = lrg_inspector_get_objects (self);
    if (objects == NULL)
        return FALSE;

    object = g_list_nth_data (objects, index);
    g_list_free (objects);

    if (object == NULL)
        return FALSE;

    lrg_inspector_select_object (self, object);
    return TRUE;
}

LrgGameObject *
lrg_inspector_get_selected_object (LrgInspector *self)
{
    g_return_val_if_fail (LRG_IS_INSPECTOR (self), NULL);
    return self->selected_object;
}

void
lrg_inspector_clear_selection (LrgInspector *self)
{
    g_return_if_fail (LRG_IS_INSPECTOR (self));

    if (self->selected_component != NULL)
    {
        g_object_weak_unref (G_OBJECT (self->selected_component), on_component_destroyed, self);
        self->selected_component = NULL;
    }

    if (self->selected_object != NULL)
    {
        g_object_weak_unref (G_OBJECT (self->selected_object), on_object_destroyed, self);
        self->selected_object = NULL;
    }

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Inspector selection cleared");
}

/* ==========================================================================
 * Component Browsing
 * ========================================================================== */

GList *
lrg_inspector_get_components (LrgInspector *self)
{
    g_return_val_if_fail (LRG_IS_INSPECTOR (self), NULL);

    if (self->selected_object == NULL)
        return NULL;

    return lrg_game_object_get_components (self->selected_object);
}

guint
lrg_inspector_get_component_count (LrgInspector *self)
{
    g_return_val_if_fail (LRG_IS_INSPECTOR (self), 0);

    if (self->selected_object == NULL)
        return 0;

    return lrg_game_object_get_component_count (self->selected_object);
}

void
lrg_inspector_select_component (LrgInspector *self,
                                LrgComponent *component)
{
    g_return_if_fail (LRG_IS_INSPECTOR (self));

    /* Remove old weak reference */
    if (self->selected_component != NULL)
        g_object_weak_unref (G_OBJECT (self->selected_component), on_component_destroyed, self);

    /* Set new component with weak reference */
    self->selected_component = component;
    if (component != NULL)
    {
        g_object_weak_ref (G_OBJECT (component), on_component_destroyed, self);
        lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Selected component: %s",
                   G_OBJECT_TYPE_NAME (component));
    }
}

gboolean
lrg_inspector_select_component_at (LrgInspector *self,
                                   guint         index)
{
    GList *components;
    LrgComponent *component;

    g_return_val_if_fail (LRG_IS_INSPECTOR (self), FALSE);

    components = lrg_inspector_get_components (self);
    if (components == NULL)
        return FALSE;

    component = g_list_nth_data (components, index);
    g_list_free (components);

    if (component == NULL)
        return FALSE;

    lrg_inspector_select_component (self, component);
    return TRUE;
}

LrgComponent *
lrg_inspector_get_selected_component (LrgInspector *self)
{
    g_return_val_if_fail (LRG_IS_INSPECTOR (self), NULL);
    return self->selected_component;
}

/* ==========================================================================
 * Property Introspection
 * ========================================================================== */

GParamSpec **
lrg_inspector_get_properties (LrgInspector *self,
                              GObject      *object,
                              guint        *n_properties)
{
    GObjectClass *klass;

    g_return_val_if_fail (LRG_IS_INSPECTOR (self), NULL);
    g_return_val_if_fail (G_IS_OBJECT (object), NULL);
    g_return_val_if_fail (n_properties != NULL, NULL);

    klass = G_OBJECT_GET_CLASS (object);
    return g_object_class_list_properties (klass, n_properties);
}

gboolean
lrg_inspector_get_property_value (LrgInspector *self,
                                  GObject      *object,
                                  const gchar  *property_name,
                                  GValue       *value)
{
    GParamSpec *pspec;

    g_return_val_if_fail (LRG_IS_INSPECTOR (self), FALSE);
    g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
    g_return_val_if_fail (property_name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), property_name);
    if (pspec == NULL)
        return FALSE;

    g_value_init (value, pspec->value_type);
    g_object_get_property (object, property_name, value);

    return TRUE;
}

gboolean
lrg_inspector_set_property_value (LrgInspector *self,
                                  GObject      *object,
                                  const gchar  *property_name,
                                  const GValue *value)
{
    GParamSpec *pspec;

    g_return_val_if_fail (LRG_IS_INSPECTOR (self), FALSE);
    g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
    g_return_val_if_fail (property_name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), property_name);
    if (pspec == NULL)
        return FALSE;

    /* Check if property is writable */
    if ((pspec->flags & G_PARAM_WRITABLE) == 0)
        return FALSE;

    g_object_set_property (object, property_name, value);

    return TRUE;
}

gchar *
lrg_inspector_get_property_string (LrgInspector *self,
                                   GObject      *object,
                                   const gchar  *property_name)
{
    GValue value = G_VALUE_INIT;
    gchar *result;

    g_return_val_if_fail (LRG_IS_INSPECTOR (self), NULL);
    g_return_val_if_fail (G_IS_OBJECT (object), NULL);
    g_return_val_if_fail (property_name != NULL, NULL);

    if (!lrg_inspector_get_property_value (self, object, property_name, &value))
        return NULL;

    result = g_strdup_value_contents (&value);
    g_value_unset (&value);

    return result;
}

/* ==========================================================================
 * Text Output (for console/overlay integration)
 * ========================================================================== */

gchar *
lrg_inspector_get_world_info (LrgInspector *self)
{
    GString *info;

    g_return_val_if_fail (LRG_IS_INSPECTOR (self), NULL);

    if (self->world == NULL)
        return g_strdup ("No world set");

    info = g_string_new ("World Info:\n");
    g_string_append_printf (info, "  Objects: %u\n",
                            lrg_world_get_object_count (self->world));
    g_string_append_printf (info, "  Active: %s\n",
                            lrg_world_get_active (self->world) ? "yes" : "no");
    g_string_append_printf (info, "  Paused: %s\n",
                            lrg_world_get_paused (self->world) ? "yes" : "no");

    return g_string_free (info, FALSE);
}

gchar *
lrg_inspector_get_object_info (LrgInspector *self)
{
    GString *info;
    gfloat x, y;
    g_autofree gchar *tag = NULL;

    g_return_val_if_fail (LRG_IS_INSPECTOR (self), NULL);

    if (self->selected_object == NULL)
        return g_strdup ("No object selected");

    info = g_string_new ("Object Info:\n");
    g_string_append_printf (info, "  Type: %s\n",
                            G_OBJECT_TYPE_NAME (self->selected_object));

    /* Get tag and position from GrlEntity */
    g_object_get (self->selected_object,
                  "tag", &tag,
                  "x", &x,
                  "y", &y,
                  NULL);
    if (tag != NULL && tag[0] != '\0')
        g_string_append_printf (info, "  Tag: %s\n", tag);
    g_string_append_printf (info, "  Position: (%.2f, %.2f)\n", x, y);
    g_string_append_printf (info, "  Components: %u\n",
                            lrg_game_object_get_component_count (self->selected_object));

    return g_string_free (info, FALSE);
}

gchar *
lrg_inspector_get_component_info (LrgInspector *self)
{
    GString *info;

    g_return_val_if_fail (LRG_IS_INSPECTOR (self), NULL);

    if (self->selected_component == NULL)
        return g_strdup ("No component selected");

    info = g_string_new ("Component Info:\n");
    g_string_append_printf (info, "  Type: %s\n",
                            G_OBJECT_TYPE_NAME (self->selected_component));
    g_string_append_printf (info, "  Enabled: %s\n",
                            lrg_component_get_enabled (self->selected_component) ? "yes" : "no");

    return g_string_free (info, FALSE);
}

gchar *
lrg_inspector_get_object_list (LrgInspector *self)
{
    GString *list;
    GList *objects;
    GList *l;
    guint index;

    g_return_val_if_fail (LRG_IS_INSPECTOR (self), NULL);

    if (self->world == NULL)
        return g_strdup ("No world set");

    objects = lrg_inspector_get_objects (self);
    if (objects == NULL)
        return g_strdup ("World is empty");

    list = g_string_new ("Objects:\n");

    index = 0;
    for (l = objects; l != NULL; l = l->next)
    {
        GObject *obj = l->data;
        gfloat x, y;
        g_autofree gchar *tag = NULL;

        g_object_get (obj, "tag", &tag, "x", &x, "y", &y, NULL);
        if (tag != NULL && tag[0] != '\0')
        {
            g_string_append_printf (list, "  [%u] %s (%s) @ (%.1f, %.1f)\n",
                                    index++,
                                    G_OBJECT_TYPE_NAME (obj),
                                    tag,
                                    x, y);
        }
        else
        {
            g_string_append_printf (list, "  [%u] %s @ (%.1f, %.1f)\n",
                                    index++,
                                    G_OBJECT_TYPE_NAME (obj),
                                    x, y);
        }
    }

    g_list_free (objects);

    return g_string_free (list, FALSE);
}

gchar *
lrg_inspector_get_component_list (LrgInspector *self)
{
    GString *list;
    GList *components;
    GList *l;
    guint index;

    g_return_val_if_fail (LRG_IS_INSPECTOR (self), NULL);

    if (self->selected_object == NULL)
        return g_strdup ("No object selected");

    components = lrg_inspector_get_components (self);
    if (components == NULL)
        return g_strdup ("Object has no components");

    list = g_string_new ("Components:\n");

    index = 0;
    for (l = components; l != NULL; l = l->next)
    {
        LrgComponent *comp = l->data;
        g_string_append_printf (list, "  [%u] %s (%s)\n",
                                index++,
                                G_OBJECT_TYPE_NAME (comp),
                                lrg_component_get_enabled (comp) ? "enabled" : "disabled");
    }

    g_list_free (components);

    return g_string_free (list, FALSE);
}

gchar *
lrg_inspector_get_property_list (LrgInspector *self,
                                 GObject      *object)
{
    GString *list;
    GParamSpec **props;
    guint n_props;
    guint i;

    g_return_val_if_fail (LRG_IS_INSPECTOR (self), NULL);

    /* Use selected object/component if none provided */
    if (object == NULL)
    {
        if (self->selected_component != NULL)
            object = G_OBJECT (self->selected_component);
        else if (self->selected_object != NULL)
            object = G_OBJECT (self->selected_object);
        else
            return g_strdup ("No object to inspect");
    }

    props = lrg_inspector_get_properties (self, object, &n_props);
    if (props == NULL || n_props == 0)
    {
        g_free (props);
        return g_strdup ("No properties");
    }

    list = g_string_new (NULL);
    g_string_append_printf (list, "Properties (%s):\n", G_OBJECT_TYPE_NAME (object));

    for (i = 0; i < n_props; i++)
    {
        GParamSpec *pspec = props[i];
        g_autofree gchar *value_str = NULL;

        value_str = lrg_inspector_get_property_string (self, object, pspec->name);

        g_string_append_printf (list, "  %s: %s",
                                pspec->name,
                                value_str != NULL ? value_str : "(null)");

        /* Add flags info */
        if ((pspec->flags & G_PARAM_READABLE) == 0)
            g_string_append (list, " [write-only]");
        else if ((pspec->flags & G_PARAM_WRITABLE) == 0)
            g_string_append (list, " [read-only]");

        g_string_append_c (list, '\n');
    }

    g_free (props);

    return g_string_free (list, FALSE);
}
