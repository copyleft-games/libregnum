/* lrg-building-instance.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-building-instance.h"

typedef struct
{
    LrgBuildingDef *definition;
    gint            grid_x;
    gint            grid_y;
    LrgRotation     rotation;
    gint            level;
    gdouble         health;
    gdouble         max_health;
    gboolean        active;
    gboolean        destroyed;
    GHashTable     *user_data;
} LrgBuildingInstancePrivate;

enum
{
    PROP_0,
    PROP_DEFINITION,
    PROP_GRID_X,
    PROP_GRID_Y,
    PROP_ROTATION,
    PROP_LEVEL,
    PROP_HEALTH,
    PROP_MAX_HEALTH,
    PROP_ACTIVE,
    PROP_DESTROYED,
    N_PROPS
};

enum
{
    SIGNAL_PLACED,
    SIGNAL_REMOVED,
    SIGNAL_UPGRADED,
    SIGNAL_DAMAGED,
    SIGNAL_DESTROYED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_TYPE_WITH_PRIVATE (LrgBuildingInstance, lrg_building_instance, G_TYPE_OBJECT)

/* Default virtual implementations */

static void
lrg_building_instance_real_on_placed (LrgBuildingInstance *self)
{
    (void)self;
}

static void
lrg_building_instance_real_on_removed (LrgBuildingInstance *self)
{
    (void)self;
}

static void
lrg_building_instance_real_on_upgraded (LrgBuildingInstance *self,
                                        gint                 new_level)
{
    (void)self;
    (void)new_level;
}

static void
lrg_building_instance_real_on_damaged (LrgBuildingInstance *self,
                                       gdouble              damage)
{
    (void)self;
    (void)damage;
}

static void
lrg_building_instance_finalize (GObject *object)
{
    LrgBuildingInstance *self = LRG_BUILDING_INSTANCE (object);
    LrgBuildingInstancePrivate *priv = lrg_building_instance_get_instance_private (self);

    g_clear_object (&priv->definition);
    g_hash_table_unref (priv->user_data);

    G_OBJECT_CLASS (lrg_building_instance_parent_class)->finalize (object);
}

static void
lrg_building_instance_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LrgBuildingInstance *self = LRG_BUILDING_INSTANCE (object);
    LrgBuildingInstancePrivate *priv = lrg_building_instance_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_DEFINITION:
        g_value_set_object (value, priv->definition);
        break;

    case PROP_GRID_X:
        g_value_set_int (value, priv->grid_x);
        break;

    case PROP_GRID_Y:
        g_value_set_int (value, priv->grid_y);
        break;

    case PROP_ROTATION:
        g_value_set_int (value, priv->rotation);
        break;

    case PROP_LEVEL:
        g_value_set_int (value, priv->level);
        break;

    case PROP_HEALTH:
        g_value_set_double (value, priv->health);
        break;

    case PROP_MAX_HEALTH:
        g_value_set_double (value, priv->max_health);
        break;

    case PROP_ACTIVE:
        g_value_set_boolean (value, priv->active);
        break;

    case PROP_DESTROYED:
        g_value_set_boolean (value, priv->destroyed);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_building_instance_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    LrgBuildingInstance *self = LRG_BUILDING_INSTANCE (object);
    LrgBuildingInstancePrivate *priv = lrg_building_instance_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_DEFINITION:
        g_clear_object (&priv->definition);
        priv->definition = g_value_dup_object (value);
        break;

    case PROP_GRID_X:
        priv->grid_x = g_value_get_int (value);
        break;

    case PROP_GRID_Y:
        priv->grid_y = g_value_get_int (value);
        break;

    case PROP_ROTATION:
        lrg_building_instance_set_rotation (self, g_value_get_int (value));
        break;

    case PROP_ACTIVE:
        lrg_building_instance_set_active (self, g_value_get_boolean (value));
        break;

    case PROP_MAX_HEALTH:
        lrg_building_instance_set_max_health (self, g_value_get_double (value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_building_instance_class_init (LrgBuildingInstanceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_building_instance_finalize;
    object_class->get_property = lrg_building_instance_get_property;
    object_class->set_property = lrg_building_instance_set_property;

    /* Virtual methods */
    klass->on_placed = lrg_building_instance_real_on_placed;
    klass->on_removed = lrg_building_instance_real_on_removed;
    klass->on_upgraded = lrg_building_instance_real_on_upgraded;
    klass->on_damaged = lrg_building_instance_real_on_damaged;

    properties[PROP_DEFINITION] =
        g_param_spec_object ("definition", "Definition", "Building definition",
                             LRG_TYPE_BUILDING_DEF,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_GRID_X] =
        g_param_spec_int ("grid-x", "Grid X", "Grid X position",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_GRID_Y] =
        g_param_spec_int ("grid-y", "Grid Y", "Grid Y position",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_ROTATION] =
        g_param_spec_int ("rotation", "Rotation", "Rotation in degrees",
                          0, 270, 0,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_LEVEL] =
        g_param_spec_int ("level", "Level", "Current upgrade level",
                          1, 100, 1,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_HEALTH] =
        g_param_spec_double ("health", "Health", "Current health",
                             0.0, G_MAXDOUBLE, 100.0,
                             G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_HEALTH] =
        g_param_spec_double ("max-health", "Max Health", "Maximum health",
                             1.0, G_MAXDOUBLE, 100.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_ACTIVE] =
        g_param_spec_boolean ("active", "Active", "Whether building is active",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    properties[PROP_DESTROYED] =
        g_param_spec_boolean ("destroyed", "Destroyed", "Whether building is destroyed",
                              FALSE,
                              G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgBuildingInstance::placed:
     * @self: The building instance
     *
     * Emitted when building is placed on grid.
     *
     * Since: 1.0
     */
    signals[SIGNAL_PLACED] =
        g_signal_new ("placed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgBuildingInstanceClass, on_placed),
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgBuildingInstance::removed:
     * @self: The building instance
     *
     * Emitted when building is removed from grid.
     *
     * Since: 1.0
     */
    signals[SIGNAL_REMOVED] =
        g_signal_new ("removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgBuildingInstanceClass, on_removed),
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgBuildingInstance::upgraded:
     * @self: The building instance
     * @new_level: New level
     *
     * Emitted when building is upgraded.
     *
     * Since: 1.0
     */
    signals[SIGNAL_UPGRADED] =
        g_signal_new ("upgraded",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgBuildingInstanceClass, on_upgraded),
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    /**
     * LrgBuildingInstance::damaged:
     * @self: The building instance
     * @damage: Damage amount
     *
     * Emitted when building takes damage.
     *
     * Since: 1.0
     */
    signals[SIGNAL_DAMAGED] =
        g_signal_new ("damaged",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgBuildingInstanceClass, on_damaged),
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_DOUBLE);

    /**
     * LrgBuildingInstance::destroyed:
     * @self: The building instance
     *
     * Emitted when building is destroyed.
     *
     * Since: 1.0
     */
    signals[SIGNAL_DESTROYED] =
        g_signal_new ("destroyed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_building_instance_init (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv = lrg_building_instance_get_instance_private (self);

    priv->definition = NULL;
    priv->grid_x = 0;
    priv->grid_y = 0;
    priv->rotation = LRG_ROTATION_0;
    priv->level = 1;
    priv->health = 100.0;
    priv->max_health = 100.0;
    priv->active = TRUE;
    priv->destroyed = FALSE;
    priv->user_data = g_hash_table_new_full (g_str_hash, g_str_equal,
                                              g_free, NULL);
}

LrgBuildingInstance *
lrg_building_instance_new (LrgBuildingDef *definition,
                           gint            grid_x,
                           gint            grid_y)
{
    g_return_val_if_fail (LRG_IS_BUILDING_DEF (definition), NULL);

    return g_object_new (LRG_TYPE_BUILDING_INSTANCE,
                         "definition", definition,
                         "grid-x", grid_x,
                         "grid-y", grid_y,
                         NULL);
}

LrgBuildingDef *
lrg_building_instance_get_definition (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), NULL);

    priv = lrg_building_instance_get_instance_private (self);
    return priv->definition;
}

gint
lrg_building_instance_get_grid_x (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), 0);

    priv = lrg_building_instance_get_instance_private (self);
    return priv->grid_x;
}

gint
lrg_building_instance_get_grid_y (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), 0);

    priv = lrg_building_instance_get_instance_private (self);
    return priv->grid_y;
}

void
lrg_building_instance_set_position (LrgBuildingInstance *self,
                                    gint                 grid_x,
                                    gint                 grid_y)
{
    LrgBuildingInstancePrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_INSTANCE (self));

    priv = lrg_building_instance_get_instance_private (self);
    priv->grid_x = grid_x;
    priv->grid_y = grid_y;
}

LrgRotation
lrg_building_instance_get_rotation (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), LRG_ROTATION_0);

    priv = lrg_building_instance_get_instance_private (self);
    return priv->rotation;
}

void
lrg_building_instance_set_rotation (LrgBuildingInstance *self,
                                    LrgRotation          rotation)
{
    LrgBuildingInstancePrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_INSTANCE (self));

    priv = lrg_building_instance_get_instance_private (self);

    if (priv->rotation != rotation)
    {
        priv->rotation = rotation;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROTATION]);
    }
}

void
lrg_building_instance_rotate_cw (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;
    LrgRotation new_rot;

    g_return_if_fail (LRG_IS_BUILDING_INSTANCE (self));

    priv = lrg_building_instance_get_instance_private (self);

    switch (priv->rotation)
    {
    case LRG_ROTATION_0:   new_rot = LRG_ROTATION_90;  break;
    case LRG_ROTATION_90:  new_rot = LRG_ROTATION_180; break;
    case LRG_ROTATION_180: new_rot = LRG_ROTATION_270; break;
    case LRG_ROTATION_270: new_rot = LRG_ROTATION_0;   break;
    default:               new_rot = LRG_ROTATION_0;   break;
    }

    lrg_building_instance_set_rotation (self, new_rot);
}

void
lrg_building_instance_rotate_ccw (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;
    LrgRotation new_rot;

    g_return_if_fail (LRG_IS_BUILDING_INSTANCE (self));

    priv = lrg_building_instance_get_instance_private (self);

    switch (priv->rotation)
    {
    case LRG_ROTATION_0:   new_rot = LRG_ROTATION_270; break;
    case LRG_ROTATION_90:  new_rot = LRG_ROTATION_0;   break;
    case LRG_ROTATION_180: new_rot = LRG_ROTATION_90;  break;
    case LRG_ROTATION_270: new_rot = LRG_ROTATION_180; break;
    default:               new_rot = LRG_ROTATION_0;   break;
    }

    lrg_building_instance_set_rotation (self, new_rot);
}

gint
lrg_building_instance_get_effective_width (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), 1);

    priv = lrg_building_instance_get_instance_private (self);

    /* Width and height swap at 90/270 rotation */
    if (priv->rotation == LRG_ROTATION_90 || priv->rotation == LRG_ROTATION_270)
        return lrg_building_def_get_height (priv->definition);
    else
        return lrg_building_def_get_width (priv->definition);
}

gint
lrg_building_instance_get_effective_height (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), 1);

    priv = lrg_building_instance_get_instance_private (self);

    if (priv->rotation == LRG_ROTATION_90 || priv->rotation == LRG_ROTATION_270)
        return lrg_building_def_get_width (priv->definition);
    else
        return lrg_building_def_get_height (priv->definition);
}

gint
lrg_building_instance_get_level (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), 1);

    priv = lrg_building_instance_get_instance_private (self);
    return priv->level;
}

gboolean
lrg_building_instance_can_upgrade (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;
    gint max_level;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), FALSE);

    priv = lrg_building_instance_get_instance_private (self);
    max_level = lrg_building_def_get_max_level (priv->definition);

    return priv->level < max_level && !priv->destroyed;
}

gboolean
lrg_building_instance_upgrade (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), FALSE);

    if (!lrg_building_instance_can_upgrade (self))
        return FALSE;

    priv = lrg_building_instance_get_instance_private (self);
    priv->level++;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LEVEL]);
    g_signal_emit (self, signals[SIGNAL_UPGRADED], 0, priv->level);

    return TRUE;
}

gdouble
lrg_building_instance_get_health (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), 0.0);

    priv = lrg_building_instance_get_instance_private (self);
    return priv->health;
}

gdouble
lrg_building_instance_get_max_health (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), 100.0);

    priv = lrg_building_instance_get_instance_private (self);
    return priv->max_health;
}

void
lrg_building_instance_set_max_health (LrgBuildingInstance *self,
                                      gdouble              max_health)
{
    LrgBuildingInstancePrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_INSTANCE (self));
    g_return_if_fail (max_health > 0.0);

    priv = lrg_building_instance_get_instance_private (self);

    if (priv->max_health != max_health)
    {
        priv->max_health = max_health;
        if (priv->health > max_health)
        {
            priv->health = max_health;
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEALTH]);
        }
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_HEALTH]);
    }
}

gboolean
lrg_building_instance_damage (LrgBuildingInstance *self,
                              gdouble              amount)
{
    LrgBuildingInstancePrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), FALSE);

    priv = lrg_building_instance_get_instance_private (self);

    if (priv->destroyed)
        return TRUE;

    priv->health -= amount;
    if (priv->health < 0.0)
        priv->health = 0.0;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEALTH]);
    g_signal_emit (self, signals[SIGNAL_DAMAGED], 0, amount);

    if (priv->health <= 0.0)
    {
        priv->destroyed = TRUE;
        priv->active = FALSE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESTROYED]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
        g_signal_emit (self, signals[SIGNAL_DESTROYED], 0);
        return TRUE;
    }

    return FALSE;
}

void
lrg_building_instance_repair (LrgBuildingInstance *self,
                              gdouble              amount)
{
    LrgBuildingInstancePrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_INSTANCE (self));

    priv = lrg_building_instance_get_instance_private (self);

    if (priv->destroyed)
        return;

    priv->health += amount;
    if (priv->health > priv->max_health)
        priv->health = priv->max_health;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEALTH]);
}

gboolean
lrg_building_instance_is_destroyed (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), TRUE);

    priv = lrg_building_instance_get_instance_private (self);
    return priv->destroyed;
}

gboolean
lrg_building_instance_is_active (LrgBuildingInstance *self)
{
    LrgBuildingInstancePrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), FALSE);

    priv = lrg_building_instance_get_instance_private (self);
    return priv->active && !priv->destroyed;
}

void
lrg_building_instance_set_active (LrgBuildingInstance *self,
                                  gboolean             active)
{
    LrgBuildingInstancePrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_INSTANCE (self));

    priv = lrg_building_instance_get_instance_private (self);

    if (priv->active != active)
    {
        priv->active = active;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
    }
}

gpointer
lrg_building_instance_get_data (LrgBuildingInstance *self,
                                const gchar         *key)
{
    LrgBuildingInstancePrivate *priv;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    priv = lrg_building_instance_get_instance_private (self);
    return g_hash_table_lookup (priv->user_data, key);
}

void
lrg_building_instance_set_data (LrgBuildingInstance *self,
                                const gchar         *key,
                                gpointer             data,
                                GDestroyNotify       destroy)
{
    LrgBuildingInstancePrivate *priv;

    g_return_if_fail (LRG_IS_BUILDING_INSTANCE (self));
    g_return_if_fail (key != NULL);

    priv = lrg_building_instance_get_instance_private (self);

    /* Note: This simple implementation doesn't handle destroy notify properly.
     * A more complete implementation would use a custom struct or
     * g_object_set_data_full style approach. */
    (void)destroy;

    if (data == NULL)
        g_hash_table_remove (priv->user_data, key);
    else
        g_hash_table_insert (priv->user_data, g_strdup (key), data);
}

gboolean
lrg_building_instance_contains_cell (LrgBuildingInstance *self,
                                     gint                 cell_x,
                                     gint                 cell_y)
{
    LrgBuildingInstancePrivate *priv;
    gint width, height;

    g_return_val_if_fail (LRG_IS_BUILDING_INSTANCE (self), FALSE);

    priv = lrg_building_instance_get_instance_private (self);
    width = lrg_building_instance_get_effective_width (self);
    height = lrg_building_instance_get_effective_height (self);

    return cell_x >= priv->grid_x && cell_x < priv->grid_x + width &&
           cell_y >= priv->grid_y && cell_y < priv->grid_y + height;
}
