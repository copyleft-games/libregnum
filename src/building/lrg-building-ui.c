/* lrg-building-ui.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_BUILDING
#include "../lrg-log.h"

#include "lrg-building-ui.h"
#include "../ui/lrg-button.h"
#include "../ui/lrg-grid.h"

/* The instance structure for final types */
struct _LrgBuildingUI
{
    LrgContainer        parent_instance;

    LrgPlacementSystem *system;
    GHashTable         *buildings;     /* id -> LrgBuildingDef */
    LrgBuildingDef     *selected;
    gint                category_filter;
    gboolean            show_demolish;
    gint                columns;
    gfloat              button_size;
    gboolean            needs_rebuild;

    /* Internal grid widget */
    LrgGrid            *grid;
    LrgButton          *demolish_button;
};

G_DEFINE_TYPE (LrgBuildingUI, lrg_building_ui, LRG_TYPE_CONTAINER)

enum
{
    PROP_0,
    PROP_PLACEMENT_SYSTEM,
    PROP_CATEGORY_FILTER,
    PROP_SHOW_DEMOLISH,
    PROP_COLUMNS,
    PROP_BUTTON_SIZE,
    N_PROPS
};

enum
{
    SIGNAL_BUILDING_SELECTED,
    SIGNAL_DEMOLISH_SELECTED,
    SIGNAL_CATEGORY_CHANGED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* Forward declarations */
static void on_building_button_clicked (LrgWidget *widget, gpointer user_data);
static void on_demolish_button_clicked (LrgWidget *widget, gpointer user_data);

static void
lrg_building_ui_layout_children_impl (LrgContainer *container)
{
    /*
     * The layout is handled by the internal LrgGrid widget,
     * so we just call the parent implementation.
     */
    LRG_CONTAINER_CLASS (lrg_building_ui_parent_class)->layout_children (container);
}

static void
lrg_building_ui_dispose (GObject *object)
{
    LrgBuildingUI        *self = LRG_BUILDING_UI (object);
    

    g_clear_object (&self->system);
    g_clear_pointer (&self->buildings, g_hash_table_unref);
    self->selected = NULL;

    G_OBJECT_CLASS (lrg_building_ui_parent_class)->dispose (object);
}

static void
lrg_building_ui_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgBuildingUI        *self = LRG_BUILDING_UI (object);
    

    switch (prop_id)
    {
    case PROP_PLACEMENT_SYSTEM:
        g_value_set_object (value, self->system);
        break;
    case PROP_CATEGORY_FILTER:
        g_value_set_int (value, self->category_filter);
        break;
    case PROP_SHOW_DEMOLISH:
        g_value_set_boolean (value, self->show_demolish);
        break;
    case PROP_COLUMNS:
        g_value_set_int (value, self->columns);
        break;
    case PROP_BUTTON_SIZE:
        g_value_set_float (value, self->button_size);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_building_ui_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgBuildingUI *self = LRG_BUILDING_UI (object);

    switch (prop_id)
    {
    case PROP_PLACEMENT_SYSTEM:
        lrg_building_ui_set_placement_system (self, g_value_get_object (value));
        break;
    case PROP_CATEGORY_FILTER:
        lrg_building_ui_set_category_filter (self, g_value_get_int (value));
        break;
    case PROP_SHOW_DEMOLISH:
        lrg_building_ui_set_show_demolish (self, g_value_get_boolean (value));
        break;
    case PROP_COLUMNS:
        lrg_building_ui_set_columns (self, g_value_get_int (value));
        break;
    case PROP_BUTTON_SIZE:
        lrg_building_ui_set_button_size (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_building_ui_class_init (LrgBuildingUIClass *klass)
{
    GObjectClass      *object_class = G_OBJECT_CLASS (klass);
    LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

    object_class->dispose = lrg_building_ui_dispose;
    object_class->get_property = lrg_building_ui_get_property;
    object_class->set_property = lrg_building_ui_set_property;

    container_class->layout_children = lrg_building_ui_layout_children_impl;

    /**
     * LrgBuildingUI:placement-system:
     *
     * The placement system to integrate with.
     *
     * Since: 1.0
     */
    properties[PROP_PLACEMENT_SYSTEM] =
        g_param_spec_object ("placement-system",
                             "Placement System",
                             "The placement system",
                             LRG_TYPE_PLACEMENT_SYSTEM,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgBuildingUI:category-filter:
     *
     * Category filter (-1 for all).
     *
     * Since: 1.0
     */
    properties[PROP_CATEGORY_FILTER] =
        g_param_spec_int ("category-filter",
                          "Category Filter",
                          "Category filter (-1 for all)",
                          -1, G_MAXINT, -1,
                          G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgBuildingUI:show-demolish:
     *
     * Whether to show the demolish button.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_DEMOLISH] =
        g_param_spec_boolean ("show-demolish",
                              "Show Demolish",
                              "Whether to show demolish button",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgBuildingUI:columns:
     *
     * Number of columns in the grid.
     *
     * Since: 1.0
     */
    properties[PROP_COLUMNS] =
        g_param_spec_int ("columns",
                          "Columns",
                          "Number of columns",
                          1, G_MAXINT, 4,
                          G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgBuildingUI:button-size:
     *
     * Size of building buttons in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_BUTTON_SIZE] =
        g_param_spec_float ("button-size",
                            "Button Size",
                            "Button size in pixels",
                            16.0f, 256.0f, 64.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgBuildingUI::building-selected:
     * @ui: the building UI
     * @definition: the selected building definition
     *
     * Emitted when a building is selected.
     *
     * Since: 1.0
     */
    signals[SIGNAL_BUILDING_SELECTED] =
        g_signal_new ("building-selected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_BUILDING_DEF);

    /**
     * LrgBuildingUI::demolish-selected:
     * @ui: the building UI
     *
     * Emitted when demolish is selected.
     *
     * Since: 1.0
     */
    signals[SIGNAL_DEMOLISH_SELECTED] =
        g_signal_new ("demolish-selected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgBuildingUI::category-changed:
     * @ui: the building UI
     * @category: the new category
     *
     * Emitted when the category filter changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CATEGORY_CHANGED] =
        g_signal_new ("category-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_INT);
}

static void
lrg_building_ui_init (LrgBuildingUI *self)
{
    

    self->buildings = g_hash_table_new_full (g_str_hash, g_str_equal,
                                             g_free, g_object_unref);
    self->selected = NULL;
    self->category_filter = -1;
    self->show_demolish = TRUE;
    self->columns = 4;
    self->button_size = 64.0f;
    self->needs_rebuild = TRUE;
    self->grid = NULL;
    self->demolish_button = NULL;
}

/* Internal helpers */

static void
on_building_button_clicked (LrgWidget *widget,
                            gpointer   user_data)
{
    LrgBuildingUI        *self = LRG_BUILDING_UI (user_data);
    
    const gchar          *id;
    LrgBuildingDef       *def;

    id = g_object_get_data (G_OBJECT (widget), "building-id");
    if (id == NULL)
        return;

    def = g_hash_table_lookup (self->buildings, id);
    if (def == NULL)
        return;

    self->selected = def;

    lrg_debug (LRG_LOG_DOMAIN_BUILDING, "Selected building '%s'", id);

    /* Start placement if we have a system */
    if (self->system != NULL)
    {
        lrg_placement_system_start_placement (self->system, def);
    }

    g_signal_emit (self, signals[SIGNAL_BUILDING_SELECTED], 0, def);
}

static void
on_demolish_button_clicked (LrgWidget *widget,
                            gpointer   user_data)
{
    LrgBuildingUI        *self = LRG_BUILDING_UI (user_data);
    

    (void)widget;

    self->selected = NULL;

    lrg_debug (LRG_LOG_DOMAIN_BUILDING, "Demolish mode selected");

    /* Start demolition if we have a system */
    if (self->system != NULL)
    {
        lrg_placement_system_start_demolition (self->system);
    }

    g_signal_emit (self, signals[SIGNAL_DEMOLISH_SELECTED], 0);
}

/* Public API */

LrgBuildingUI *
lrg_building_ui_new (void)
{
    return g_object_new (LRG_TYPE_BUILDING_UI, NULL);
}

LrgPlacementSystem *
lrg_building_ui_get_placement_system (LrgBuildingUI *self)
{
    

    g_return_val_if_fail (LRG_IS_BUILDING_UI (self), NULL);

    
    return self->system;
}

void
lrg_building_ui_set_placement_system (LrgBuildingUI      *self,
                                      LrgPlacementSystem *system)
{
    

    g_return_if_fail (LRG_IS_BUILDING_UI (self));
    g_return_if_fail (system == NULL || LRG_IS_PLACEMENT_SYSTEM (system));

    

    if (g_set_object (&self->system, system))
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLACEMENT_SYSTEM]);
    }
}

void
lrg_building_ui_register (LrgBuildingUI  *self,
                          LrgBuildingDef *definition)
{
    
    const gchar          *id;

    g_return_if_fail (LRG_IS_BUILDING_UI (self));
    g_return_if_fail (LRG_IS_BUILDING_DEF (definition));

    
    id = lrg_building_def_get_id (definition);

    g_hash_table_insert (self->buildings, g_strdup (id), g_object_ref (definition));
    self->needs_rebuild = TRUE;

    lrg_debug (LRG_LOG_DOMAIN_BUILDING, "Registered building '%s'", id);
}

void
lrg_building_ui_unregister (LrgBuildingUI *self,
                            const gchar   *id)
{
    

    g_return_if_fail (LRG_IS_BUILDING_UI (self));
    g_return_if_fail (id != NULL);

    

    if (g_hash_table_remove (self->buildings, id))
    {
        self->needs_rebuild = TRUE;
        lrg_debug (LRG_LOG_DOMAIN_BUILDING, "Unregistered building '%s'", id);
    }
}

LrgBuildingDef *
lrg_building_ui_get_building (LrgBuildingUI *self,
                              const gchar   *id)
{
    

    g_return_val_if_fail (LRG_IS_BUILDING_UI (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    
    return g_hash_table_lookup (self->buildings, id);
}

GPtrArray *
lrg_building_ui_get_all_buildings (LrgBuildingUI *self)
{
    
    GPtrArray            *result;
    GHashTableIter        iter;
    gpointer              value;

    g_return_val_if_fail (LRG_IS_BUILDING_UI (self), NULL);

    
    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->buildings);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        g_ptr_array_add (result, value);
    }

    return result;
}

void
lrg_building_ui_clear_buildings (LrgBuildingUI *self)
{
    

    g_return_if_fail (LRG_IS_BUILDING_UI (self));

    

    g_hash_table_remove_all (self->buildings);
    self->selected = NULL;
    self->needs_rebuild = TRUE;

    lrg_debug (LRG_LOG_DOMAIN_BUILDING, "Cleared all buildings");
}

gint
lrg_building_ui_get_category_filter (LrgBuildingUI *self)
{
    

    g_return_val_if_fail (LRG_IS_BUILDING_UI (self), -1);

    
    return self->category_filter;
}

void
lrg_building_ui_set_category_filter (LrgBuildingUI *self,
                                     gint           category)
{
    

    g_return_if_fail (LRG_IS_BUILDING_UI (self));

    

    if (self->category_filter != category)
    {
        self->category_filter = category;
        self->needs_rebuild = TRUE;

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CATEGORY_FILTER]);
        g_signal_emit (self, signals[SIGNAL_CATEGORY_CHANGED], 0, category);
    }
}

GPtrArray *
lrg_building_ui_get_buildings_by_category (LrgBuildingUI       *self,
                                           LrgBuildingCategory  category)
{
    
    GPtrArray            *result;
    GHashTableIter        iter;
    gpointer              value;

    g_return_val_if_fail (LRG_IS_BUILDING_UI (self), NULL);

    
    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->buildings);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgBuildingDef *def = LRG_BUILDING_DEF (value);
        if (lrg_building_def_get_category (def) == category)
        {
            g_ptr_array_add (result, def);
        }
    }

    return result;
}

LrgBuildingDef *
lrg_building_ui_get_selected (LrgBuildingUI *self)
{
    

    g_return_val_if_fail (LRG_IS_BUILDING_UI (self), NULL);

    
    return self->selected;
}

void
lrg_building_ui_select (LrgBuildingUI *self,
                        const gchar   *id)
{
    

    g_return_if_fail (LRG_IS_BUILDING_UI (self));

    

    if (id == NULL)
    {
        self->selected = NULL;
    }
    else
    {
        self->selected = g_hash_table_lookup (self->buildings, id);
        if (self->selected != NULL)
        {
            g_signal_emit (self, signals[SIGNAL_BUILDING_SELECTED], 0, self->selected);
        }
    }
}

void
lrg_building_ui_deselect (LrgBuildingUI *self)
{
    lrg_building_ui_select (self, NULL);
}

gboolean
lrg_building_ui_get_show_demolish (LrgBuildingUI *self)
{
    

    g_return_val_if_fail (LRG_IS_BUILDING_UI (self), FALSE);

    
    return self->show_demolish;
}

void
lrg_building_ui_set_show_demolish (LrgBuildingUI *self,
                                   gboolean       show)
{
    

    g_return_if_fail (LRG_IS_BUILDING_UI (self));

    

    show = !!show;
    if (self->show_demolish != show)
    {
        self->show_demolish = show;
        self->needs_rebuild = TRUE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_DEMOLISH]);
    }
}

gint
lrg_building_ui_get_columns (LrgBuildingUI *self)
{
    

    g_return_val_if_fail (LRG_IS_BUILDING_UI (self), 4);

    
    return self->columns;
}

void
lrg_building_ui_set_columns (LrgBuildingUI *self,
                             gint           columns)
{
    

    g_return_if_fail (LRG_IS_BUILDING_UI (self));
    g_return_if_fail (columns > 0);

    

    if (self->columns != columns)
    {
        self->columns = columns;
        self->needs_rebuild = TRUE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLUMNS]);
    }
}

gfloat
lrg_building_ui_get_button_size (LrgBuildingUI *self)
{
    

    g_return_val_if_fail (LRG_IS_BUILDING_UI (self), 64.0f);

    
    return self->button_size;
}

void
lrg_building_ui_set_button_size (LrgBuildingUI *self,
                                 gfloat         size)
{
    

    g_return_if_fail (LRG_IS_BUILDING_UI (self));
    g_return_if_fail (size > 0.0f);

    

    if (self->button_size != size)
    {
        self->button_size = size;
        self->needs_rebuild = TRUE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUTTON_SIZE]);
    }
}

void
lrg_building_ui_rebuild (LrgBuildingUI *self)
{
    
    GHashTableIter        iter;
    gpointer              key;
    gpointer              value;
    LrgButton            *button;

    g_return_if_fail (LRG_IS_BUILDING_UI (self));

    

    /* Clear existing children */
    lrg_container_remove_all (LRG_CONTAINER (self));

    /* Create internal grid if needed */
    self->grid = lrg_grid_new ((guint)self->columns);
    lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->grid));

    /* Add building buttons */
    g_hash_table_iter_init (&iter, self->buildings);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        LrgBuildingDef *def = LRG_BUILDING_DEF (value);
        const gchar    *id = (const gchar *)key;

        /* Apply category filter */
        if (self->category_filter >= 0)
        {
            if ((gint)lrg_building_def_get_category (def) != self->category_filter)
                continue;
        }

        /* Create button */
        button = lrg_button_new (lrg_building_def_get_name (def));
        lrg_widget_set_width (LRG_WIDGET (button), self->button_size);
        lrg_widget_set_height (LRG_WIDGET (button), self->button_size);

        /* Store building ID for click handling */
        g_object_set_data_full (G_OBJECT (button), "building-id",
                                g_strdup (id), g_free);

        /* Connect click signal */
        g_signal_connect (button, "clicked",
                          G_CALLBACK (on_building_button_clicked), self);

        lrg_container_add_child (LRG_CONTAINER (self->grid), LRG_WIDGET (button));
    }

    /* Add demolish button if enabled */
    if (self->show_demolish)
    {
        self->demolish_button = lrg_button_new ("Demolish");
        lrg_widget_set_width (LRG_WIDGET (self->demolish_button), self->button_size);
        lrg_widget_set_height (LRG_WIDGET (self->demolish_button), self->button_size);

        g_signal_connect (self->demolish_button, "clicked",
                          G_CALLBACK (on_demolish_button_clicked), self);

        lrg_container_add_child (LRG_CONTAINER (self->grid),
                                 LRG_WIDGET (self->demolish_button));
    }

    self->needs_rebuild = FALSE;

    /* Trigger layout */
    lrg_container_layout_children (LRG_CONTAINER (self));

    lrg_debug (LRG_LOG_DOMAIN_BUILDING, "Rebuilt building UI");
}
