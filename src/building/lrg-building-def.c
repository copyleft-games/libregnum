/* lrg-building-def.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-building-def.h"

/* ========================================================================= */
/* LrgBuildCost - Boxed type for resource costs                             */
/* ========================================================================= */

/* Forward declarations for G_DEFINE_BOXED_TYPE */
LrgBuildCost *lrg_build_cost_copy (const LrgBuildCost *self);
void lrg_build_cost_free (LrgBuildCost *self);

G_DEFINE_BOXED_TYPE (LrgBuildCost, lrg_build_cost,
                     lrg_build_cost_copy,
                     lrg_build_cost_free)

LrgBuildCost *
lrg_build_cost_new (void)
{
    LrgBuildCost *self;

    self = g_slice_new0 (LrgBuildCost);
    self->costs = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          g_free, g_free);

    return self;
}

LrgBuildCost *
lrg_build_cost_copy (const LrgBuildCost *self)
{
    LrgBuildCost *copy;
    GHashTableIter iter;
    gpointer key, value;

    g_return_val_if_fail (self != NULL, NULL);

    copy = lrg_build_cost_new ();

    g_hash_table_iter_init (&iter, self->costs);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        gdouble *amount = g_new (gdouble, 1);
        *amount = *(gdouble *)value;
        g_hash_table_insert (copy->costs, g_strdup (key), amount);
    }

    return copy;
}

void
lrg_build_cost_free (LrgBuildCost *self)
{
    if (self == NULL)
        return;

    g_hash_table_unref (self->costs);
    g_slice_free (LrgBuildCost, self);
}

void
lrg_build_cost_set (LrgBuildCost *self,
                    const gchar  *resource_id,
                    gdouble       amount)
{
    gdouble *stored;

    g_return_if_fail (self != NULL);
    g_return_if_fail (resource_id != NULL);

    stored = g_new (gdouble, 1);
    *stored = amount;
    g_hash_table_insert (self->costs, g_strdup (resource_id), stored);
}

gdouble
lrg_build_cost_get (const LrgBuildCost *self,
                    const gchar        *resource_id)
{
    gdouble *amount;

    g_return_val_if_fail (self != NULL, 0.0);
    g_return_val_if_fail (resource_id != NULL, 0.0);

    amount = g_hash_table_lookup (self->costs, resource_id);
    if (amount == NULL)
        return 0.0;

    return *amount;
}

GPtrArray *
lrg_build_cost_get_resources (const LrgBuildCost *self)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer key;

    g_return_val_if_fail (self != NULL, NULL);

    result = g_ptr_array_new_with_free_func (g_free);

    g_hash_table_iter_init (&iter, self->costs);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        g_ptr_array_add (result, g_strdup (key));

    return result;
}

gboolean
lrg_build_cost_is_empty (const LrgBuildCost *self)
{
    g_return_val_if_fail (self != NULL, TRUE);
    return g_hash_table_size (self->costs) == 0;
}

/* ========================================================================= */
/* LrgBuildingDef - Derivable GObject for building definitions              */
/* ========================================================================= */

typedef struct
{
    gchar              *id;
    gchar              *name;
    gchar              *description;
    gchar              *icon;
    gint                width;
    gint                height;
    LrgBuildingCategory category;
    LrgTerrainType     buildable_on;
    gint                max_level;
    LrgBuildCost       *cost;
    GHashTable         *upgrade_costs;  /* level -> LrgBuildCost* */
    gdouble             refund_percent;
} LrgBuildingDefPrivate;

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_ICON,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_CATEGORY,
    PROP_BUILDABLE_ON,
    PROP_MAX_LEVEL,
    PROP_REFUND_PERCENT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE_WITH_PRIVATE (LrgBuildingDef, lrg_building_def, G_TYPE_OBJECT)

/* Default virtual implementations */

static gboolean
lrg_building_def_real_can_build (LrgBuildingDef *self,
                                 gint            grid_x,
                                 gint            grid_y,
                                 LrgTerrainType terrain)
{
    LrgBuildingDefPrivate *priv = lrg_building_def_get_instance_private (self);

    (void)grid_x;
    (void)grid_y;

    /* Check if terrain matches allowed types */
    return (terrain & priv->buildable_on) != 0;
}

static void
lrg_building_def_real_on_built (LrgBuildingDef *self,
                                gpointer        instance)
{
    (void)self;
    (void)instance;
    /* Default: do nothing */
}

static void
lrg_building_def_real_on_destroyed (LrgBuildingDef *self,
                                    gpointer        instance)
{
    (void)self;
    (void)instance;
    /* Default: do nothing */
}

static void
lrg_building_def_finalize (GObject *object)
{
    LrgBuildingDef *self = LRG_BUILDING_DEF (object);
    LrgBuildingDefPrivate *priv = lrg_building_def_get_instance_private (self);

    g_free (priv->id);
    g_free (priv->name);
    g_free (priv->description);
    g_free (priv->icon);
    lrg_build_cost_free (priv->cost);
    g_hash_table_unref (priv->upgrade_costs);

    G_OBJECT_CLASS (lrg_building_def_parent_class)->finalize (object);
}

static void
lrg_building_def_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgBuildingDef *self = LRG_BUILDING_DEF (object);
    LrgBuildingDefPrivate *priv = lrg_building_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, priv->id);
        break;

    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;

    case PROP_DESCRIPTION:
        g_value_set_string (value, priv->description);
        break;

    case PROP_ICON:
        g_value_set_string (value, priv->icon);
        break;

    case PROP_WIDTH:
        g_value_set_int (value, priv->width);
        break;

    case PROP_HEIGHT:
        g_value_set_int (value, priv->height);
        break;

    case PROP_CATEGORY:
        g_value_set_int (value, priv->category);
        break;

    case PROP_BUILDABLE_ON:
        g_value_set_flags (value, priv->buildable_on);
        break;

    case PROP_MAX_LEVEL:
        g_value_set_int (value, priv->max_level);
        break;

    case PROP_REFUND_PERCENT:
        g_value_set_double (value, priv->refund_percent);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_building_def_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgBuildingDef *self = LRG_BUILDING_DEF (object);
    LrgBuildingDefPrivate *priv = lrg_building_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (priv->id);
        priv->id = g_value_dup_string (value);
        break;

    case PROP_NAME:
        lrg_building_def_set_name (self, g_value_get_string (value));
        break;

    case PROP_DESCRIPTION:
        lrg_building_def_set_description (self, g_value_get_string (value));
        break;

    case PROP_ICON:
        lrg_building_def_set_icon (self, g_value_get_string (value));
        break;

    case PROP_WIDTH:
        lrg_building_def_set_width (self, g_value_get_int (value));
        break;

    case PROP_HEIGHT:
        lrg_building_def_set_height (self, g_value_get_int (value));
        break;

    case PROP_CATEGORY:
        lrg_building_def_set_category (self, g_value_get_int (value));
        break;

    case PROP_BUILDABLE_ON:
        lrg_building_def_set_buildable_on (self, g_value_get_flags (value));
        break;

    case PROP_MAX_LEVEL:
        lrg_building_def_set_max_level (self, g_value_get_int (value));
        break;

    case PROP_REFUND_PERCENT:
        lrg_building_def_set_refund_percent (self, g_value_get_double (value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_building_def_class_init (LrgBuildingDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_building_def_finalize;
    object_class->get_property = lrg_building_def_get_property;
    object_class->set_property = lrg_building_def_set_property;

    /* Virtual methods */
    klass->can_build = lrg_building_def_real_can_build;
    klass->on_built = lrg_building_def_real_on_built;
    klass->on_destroyed = lrg_building_def_real_on_destroyed;

    properties[PROP_ID] =
        g_param_spec_string ("id", "ID", "Unique identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_NAME] =
        g_param_spec_string ("name", "Name", "Display name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description", "Description", "Description text",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_ICON] =
        g_param_spec_string ("icon", "Icon", "Icon path",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_WIDTH] =
        g_param_spec_int ("width", "Width", "Width in grid cells",
                          1, 100, 1,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_HEIGHT] =
        g_param_spec_int ("height", "Height", "Height in grid cells",
                          1, 100, 1,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_CATEGORY] =
        g_param_spec_int ("category", "Category", "Building category",
                          0, 4, LRG_BUILDING_CATEGORY_PRODUCTION,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_BUILDABLE_ON] =
        g_param_spec_flags ("buildable-on", "Buildable On", "Allowed terrain types",
                            LRG_TYPE_TERRAIN_TYPE, LRG_TERRAIN_ANY,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_LEVEL] =
        g_param_spec_int ("max-level", "Max Level", "Maximum upgrade level",
                          1, 100, 1,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_REFUND_PERCENT] =
        g_param_spec_double ("refund-percent", "Refund Percent",
                             "Refund percentage on demolition",
                             0.0, 1.0, 0.5,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
free_build_cost (gpointer data)
{
    lrg_build_cost_free (data);
}

static void
lrg_building_def_init (LrgBuildingDef *self)
{
    LrgBuildingDefPrivate *priv = lrg_building_def_get_instance_private (self);

    priv->id = NULL;
    priv->name = NULL;
    priv->description = NULL;
    priv->icon = NULL;
    priv->width = 1;
    priv->height = 1;
    priv->category = LRG_BUILDING_CATEGORY_PRODUCTION;
    priv->buildable_on = LRG_TERRAIN_ANY;
    priv->max_level = 1;
    priv->cost = lrg_build_cost_new ();
    priv->upgrade_costs = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                  NULL, free_build_cost);
    priv->refund_percent = 0.5;
}

LrgBuildingDef *
lrg_building_def_new (const gchar *id)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_BUILDING_DEF,
                         "id", id,
                         NULL);
}

const gchar *
lrg_building_def_get_id (LrgBuildingDef *self)
{
    LrgBuildingDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_DEF (self), NULL);

    priv = lrg_building_def_get_instance_private (self);
    return priv->id;
}

const gchar *
lrg_building_def_get_name (LrgBuildingDef *self)
{
    LrgBuildingDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_DEF (self), NULL);

    priv = lrg_building_def_get_instance_private (self);
    return priv->name;
}

void
lrg_building_def_set_name (LrgBuildingDef *self,
                           const gchar    *name)
{
    LrgBuildingDefPrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_DEF (self));

    priv = lrg_building_def_get_instance_private (self);

    if (g_strcmp0 (priv->name, name) != 0)
    {
        g_free (priv->name);
        priv->name = g_strdup (name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
    }
}

const gchar *
lrg_building_def_get_description (LrgBuildingDef *self)
{
    LrgBuildingDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_DEF (self), NULL);

    priv = lrg_building_def_get_instance_private (self);
    return priv->description;
}

void
lrg_building_def_set_description (LrgBuildingDef *self,
                                  const gchar    *description)
{
    LrgBuildingDefPrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_DEF (self));

    priv = lrg_building_def_get_instance_private (self);

    if (g_strcmp0 (priv->description, description) != 0)
    {
        g_free (priv->description);
        priv->description = g_strdup (description);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESCRIPTION]);
    }
}

const gchar *
lrg_building_def_get_icon (LrgBuildingDef *self)
{
    LrgBuildingDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_DEF (self), NULL);

    priv = lrg_building_def_get_instance_private (self);
    return priv->icon;
}

void
lrg_building_def_set_icon (LrgBuildingDef *self,
                           const gchar    *icon)
{
    LrgBuildingDefPrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_DEF (self));

    priv = lrg_building_def_get_instance_private (self);

    if (g_strcmp0 (priv->icon, icon) != 0)
    {
        g_free (priv->icon);
        priv->icon = g_strdup (icon);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ICON]);
    }
}

gint
lrg_building_def_get_width (LrgBuildingDef *self)
{
    LrgBuildingDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_DEF (self), 1);

    priv = lrg_building_def_get_instance_private (self);
    return priv->width;
}

void
lrg_building_def_set_width (LrgBuildingDef *self,
                            gint            width)
{
    LrgBuildingDefPrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_DEF (self));
    g_return_if_fail (width >= 1);

    priv = lrg_building_def_get_instance_private (self);

    if (priv->width != width)
    {
        priv->width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
    }
}

gint
lrg_building_def_get_height (LrgBuildingDef *self)
{
    LrgBuildingDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_DEF (self), 1);

    priv = lrg_building_def_get_instance_private (self);
    return priv->height;
}

void
lrg_building_def_set_height (LrgBuildingDef *self,
                             gint            height)
{
    LrgBuildingDefPrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_DEF (self));
    g_return_if_fail (height >= 1);

    priv = lrg_building_def_get_instance_private (self);

    if (priv->height != height)
    {
        priv->height = height;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
    }
}

void
lrg_building_def_set_size (LrgBuildingDef *self,
                           gint            width,
                           gint            height)
{
    lrg_building_def_set_width (self, width);
    lrg_building_def_set_height (self, height);
}

LrgBuildingCategory
lrg_building_def_get_category (LrgBuildingDef *self)
{
    LrgBuildingDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_DEF (self), LRG_BUILDING_CATEGORY_PRODUCTION);

    priv = lrg_building_def_get_instance_private (self);
    return priv->category;
}

void
lrg_building_def_set_category (LrgBuildingDef     *self,
                               LrgBuildingCategory category)
{
    LrgBuildingDefPrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_DEF (self));

    priv = lrg_building_def_get_instance_private (self);

    if (priv->category != category)
    {
        priv->category = category;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CATEGORY]);
    }
}

LrgTerrainType
lrg_building_def_get_buildable_on (LrgBuildingDef *self)
{
    LrgBuildingDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_DEF (self), LRG_TERRAIN_ANY);

    priv = lrg_building_def_get_instance_private (self);
    return priv->buildable_on;
}

void
lrg_building_def_set_buildable_on (LrgBuildingDef  *self,
                                   LrgTerrainType  terrain)
{
    LrgBuildingDefPrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_DEF (self));

    priv = lrg_building_def_get_instance_private (self);

    if (priv->buildable_on != terrain)
    {
        priv->buildable_on = terrain;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUILDABLE_ON]);
    }
}

gint
lrg_building_def_get_max_level (LrgBuildingDef *self)
{
    LrgBuildingDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_DEF (self), 1);

    priv = lrg_building_def_get_instance_private (self);
    return priv->max_level;
}

void
lrg_building_def_set_max_level (LrgBuildingDef *self,
                                gint            max_level)
{
    LrgBuildingDefPrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_DEF (self));
    g_return_if_fail (max_level >= 1);

    priv = lrg_building_def_get_instance_private (self);

    if (priv->max_level != max_level)
    {
        priv->max_level = max_level;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_LEVEL]);
    }
}

const LrgBuildCost *
lrg_building_def_get_cost (LrgBuildingDef *self)
{
    LrgBuildingDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_DEF (self), NULL);

    priv = lrg_building_def_get_instance_private (self);
    return priv->cost;
}

void
lrg_building_def_set_cost (LrgBuildingDef     *self,
                           const LrgBuildCost *cost)
{
    LrgBuildingDefPrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_DEF (self));
    g_return_if_fail (cost != NULL);

    priv = lrg_building_def_get_instance_private (self);

    lrg_build_cost_free (priv->cost);
    priv->cost = lrg_build_cost_copy (cost);
}

void
lrg_building_def_set_cost_simple (LrgBuildingDef *self,
                                  const gchar    *resource_id,
                                  gdouble         amount)
{
    LrgBuildingDefPrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_DEF (self));
    g_return_if_fail (resource_id != NULL);

    priv = lrg_building_def_get_instance_private (self);

    lrg_build_cost_set (priv->cost, resource_id, amount);
}

const LrgBuildCost *
lrg_building_def_get_upgrade_cost (LrgBuildingDef *self,
                                   gint            level)
{
    LrgBuildingDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_DEF (self), NULL);

    priv = lrg_building_def_get_instance_private (self);
    return g_hash_table_lookup (priv->upgrade_costs, GINT_TO_POINTER (level));
}

void
lrg_building_def_set_upgrade_cost (LrgBuildingDef     *self,
                                   gint                level,
                                   const LrgBuildCost *cost)
{
    LrgBuildingDefPrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_DEF (self));
    g_return_if_fail (cost != NULL);
    g_return_if_fail (level >= 2);

    priv = lrg_building_def_get_instance_private (self);

    g_hash_table_insert (priv->upgrade_costs,
                         GINT_TO_POINTER (level),
                         lrg_build_cost_copy (cost));
}

gdouble
lrg_building_def_get_refund_percent (LrgBuildingDef *self)
{
    LrgBuildingDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_DEF (self), 0.0);

    priv = lrg_building_def_get_instance_private (self);
    return priv->refund_percent;
}

void
lrg_building_def_set_refund_percent (LrgBuildingDef *self,
                                     gdouble         percent)
{
    LrgBuildingDefPrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_DEF (self));

    priv = lrg_building_def_get_instance_private (self);
    percent = CLAMP (percent, 0.0, 1.0);

    if (priv->refund_percent != percent)
    {
        priv->refund_percent = percent;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_REFUND_PERCENT]);
    }
}

gboolean
lrg_building_def_can_build (LrgBuildingDef *self,
                            gint            grid_x,
                            gint            grid_y,
                            LrgTerrainType terrain)
{
    LrgBuildingDefClass *klass;

    g_return_val_if_fail (LRG_IS_BUILDING_DEF (self), FALSE);

    klass = LRG_BUILDING_DEF_GET_CLASS (self);
    return klass->can_build (self, grid_x, grid_y, terrain);
}

void
lrg_building_def_on_built (LrgBuildingDef *self,
                           gpointer        instance)
{
    LrgBuildingDefClass *klass;

    g_return_if_fail (LRG_IS_BUILDING_DEF (self));

    klass = LRG_BUILDING_DEF_GET_CLASS (self);
    klass->on_built (self, instance);
}

void
lrg_building_def_on_destroyed (LrgBuildingDef *self,
                               gpointer        instance)
{
    LrgBuildingDefClass *klass;

    g_return_if_fail (LRG_IS_BUILDING_DEF (self));

    klass = LRG_BUILDING_DEF_GET_CLASS (self);
    klass->on_destroyed (self, instance);
}
