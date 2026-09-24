/* lrg-pet-def.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <math.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "collection/lrg-pet-def.h"

#define PET_MAX_DISTANCE (1000.0)
#define PET_MIN_INTERVAL (0.05)
#define PET_MAX_INTERVAL (3600.0)

typedef struct
{
    LrgCompanionKind  companion_kind;
    gdouble           follow_distance;
    gdouble           follow_angle;
    guint             health;
    guint             damage;
    gdouble           attack_range;
    gdouble           attack_interval;
    gdouble           move_speed;
    GPtrArray        *abilities;     /* utf8, insertion order */
} LrgPetDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgPetDef, lrg_pet_def, LRG_TYPE_COLLECTIBLE_DEF)

enum
{
    PROP_0,
    PROP_COMPANION_KIND,
    PROP_FOLLOW_DISTANCE,
    PROP_FOLLOW_ANGLE,
    PROP_HEALTH,
    PROP_DAMAGE,
    PROP_ATTACK_RANGE,
    PROP_ATTACK_INTERVAL,
    PROP_MOVE_SPEED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* set_double:
 * Validates @value against [@min, @max] (and finiteness), stores it in
 * @slot and notifies @prop when it changed. */
static void
set_double (LrgPetDef *self,
            gdouble   *slot,
            gdouble    value,
            gdouble    min,
            gdouble    max,
            guint      prop)
{
    g_return_if_fail (isfinite (value));
    g_return_if_fail (value >= min && value <= max);

    if (*slot == value)
        return;

    *slot = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
set_uint (LrgPetDef *self,
          guint     *slot,
          guint      value,
          guint      prop)
{
    if (*slot == value)
        return;

    *slot = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
lrg_pet_def_finalize (GObject *object)
{
    LrgPetDef        *self = LRG_PET_DEF (object);
    LrgPetDefPrivate *priv = lrg_pet_def_get_instance_private (self);

    g_clear_pointer (&priv->abilities, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_pet_def_parent_class)->finalize (object);
}

static void
lrg_pet_def_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgPetDef        *self = LRG_PET_DEF (object);
    LrgPetDefPrivate *priv = lrg_pet_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_COMPANION_KIND:
        g_value_set_enum (value, (gint)priv->companion_kind);
        break;
    case PROP_FOLLOW_DISTANCE:
        g_value_set_double (value, priv->follow_distance);
        break;
    case PROP_FOLLOW_ANGLE:
        g_value_set_double (value, priv->follow_angle);
        break;
    case PROP_HEALTH:
        g_value_set_uint (value, priv->health);
        break;
    case PROP_DAMAGE:
        g_value_set_uint (value, priv->damage);
        break;
    case PROP_ATTACK_RANGE:
        g_value_set_double (value, priv->attack_range);
        break;
    case PROP_ATTACK_INTERVAL:
        g_value_set_double (value, priv->attack_interval);
        break;
    case PROP_MOVE_SPEED:
        g_value_set_double (value, priv->move_speed);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_pet_def_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgPetDef *self = LRG_PET_DEF (object);

    switch (prop_id)
    {
    case PROP_COMPANION_KIND:
        lrg_pet_def_set_companion_kind (self, (LrgCompanionKind)g_value_get_enum (value));
        break;
    case PROP_FOLLOW_DISTANCE:
        lrg_pet_def_set_follow_distance (self, g_value_get_double (value));
        break;
    case PROP_FOLLOW_ANGLE:
        lrg_pet_def_set_follow_angle (self, g_value_get_double (value));
        break;
    case PROP_HEALTH:
        lrg_pet_def_set_health (self, g_value_get_uint (value));
        break;
    case PROP_DAMAGE:
        lrg_pet_def_set_damage (self, g_value_get_uint (value));
        break;
    case PROP_ATTACK_RANGE:
        lrg_pet_def_set_attack_range (self, g_value_get_double (value));
        break;
    case PROP_ATTACK_INTERVAL:
        lrg_pet_def_set_attack_interval (self, g_value_get_double (value));
        break;
    case PROP_MOVE_SPEED:
        lrg_pet_def_set_move_speed (self, g_value_get_double (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_pet_def_class_init (LrgPetDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_pet_def_finalize;
    object_class->get_property = lrg_pet_def_get_property;
    object_class->set_property = lrg_pet_def_set_property;

    /**
     * LrgPetDef:companion-kind:
     *
     * Whether the pet is vanity-only or fights.
     */
    properties[PROP_COMPANION_KIND] =
        g_param_spec_enum ("companion-kind", "Companion Kind",
                           "Vanity or combat companion",
                           LRG_TYPE_COMPANION_KIND,
                           LRG_COMPANION_KIND_VANITY,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgPetDef:follow-distance:
     *
     * Distance from the owner kept while following.
     */
    properties[PROP_FOLLOW_DISTANCE] =
        g_param_spec_double ("follow-distance", "Follow Distance",
                             "Distance kept from the owner",
                             0.0, PET_MAX_DISTANCE, 2.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgPetDef:follow-angle:
     *
     * Follow angle in radians relative to the owner's facing; the default
     * 2.356 (3*pi/4) places the pet behind-left of the owner.
     */
    properties[PROP_FOLLOW_ANGLE] =
        g_param_spec_double ("follow-angle", "Follow Angle",
                             "Follow angle relative to owner facing",
                             -2.0 * G_PI, 2.0 * G_PI, 2.356,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgPetDef:health:
     *
     * Base health.
     */
    properties[PROP_HEALTH] =
        g_param_spec_uint ("health", "Health", "Base health",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgPetDef:damage:
     *
     * Base damage per attack.
     */
    properties[PROP_DAMAGE] =
        g_param_spec_uint ("damage", "Damage", "Base damage per attack",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgPetDef:attack-range:
     *
     * Attack reach.
     */
    properties[PROP_ATTACK_RANGE] =
        g_param_spec_double ("attack-range", "Attack Range", "Attack reach",
                             0.0, PET_MAX_DISTANCE, 3.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgPetDef:attack-interval:
     *
     * Seconds between attacks.
     */
    properties[PROP_ATTACK_INTERVAL] =
        g_param_spec_double ("attack-interval", "Attack Interval",
                             "Seconds between attacks",
                             PET_MIN_INTERVAL, PET_MAX_INTERVAL, 2.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgPetDef:move-speed:
     *
     * Movement speed in world units per second.
     */
    properties[PROP_MOVE_SPEED] =
        g_param_spec_double ("move-speed", "Move Speed",
                             "Movement speed in units per second",
                             0.0, PET_MAX_DISTANCE, 7.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_pet_def_init (LrgPetDef *self)
{
    LrgPetDefPrivate *priv = lrg_pet_def_get_instance_private (self);

    priv->companion_kind = LRG_COMPANION_KIND_VANITY;
    priv->follow_distance = 2.0;
    priv->follow_angle = 2.356;
    priv->health = 0;
    priv->damage = 0;
    priv->attack_range = 3.0;
    priv->attack_interval = 2.0;
    priv->move_speed = 7.0;
    priv->abilities = g_ptr_array_new_with_free_func (g_free);
}

LrgPetDef *
lrg_pet_def_new (const gchar      *id,
                 LrgCompanionKind  companion_kind)
{
    g_return_val_if_fail (id != NULL && *id != '\0', NULL);

    return g_object_new (LRG_TYPE_PET_DEF,
                         "id", id,
                         "kind", LRG_COLLECTIBLE_KIND_PET,
                         "companion-kind", companion_kind,
                         NULL);
}

LrgCompanionKind
lrg_pet_def_get_companion_kind (LrgPetDef *self)
{
    LrgPetDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PET_DEF (self), LRG_COMPANION_KIND_VANITY);
    priv = lrg_pet_def_get_instance_private (self);
    return priv->companion_kind;
}

void
lrg_pet_def_set_companion_kind (LrgPetDef        *self,
                                LrgCompanionKind  kind)
{
    LrgPetDefPrivate *priv;

    g_return_if_fail (LRG_IS_PET_DEF (self));
    g_return_if_fail ((guint)kind <= (guint)LRG_COMPANION_KIND_COMBAT);
    priv = lrg_pet_def_get_instance_private (self);

    if (priv->companion_kind == kind)
        return;

    priv->companion_kind = kind;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMPANION_KIND]);
}

gdouble
lrg_pet_def_get_follow_distance (LrgPetDef *self)
{
    LrgPetDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PET_DEF (self), 0.0);
    priv = lrg_pet_def_get_instance_private (self);
    return priv->follow_distance;
}

void
lrg_pet_def_set_follow_distance (LrgPetDef *self,
                                 gdouble    distance)
{
    LrgPetDefPrivate *priv;

    g_return_if_fail (LRG_IS_PET_DEF (self));
    priv = lrg_pet_def_get_instance_private (self);
    set_double (self, &priv->follow_distance, distance, 0.0, PET_MAX_DISTANCE,
                PROP_FOLLOW_DISTANCE);
}

gdouble
lrg_pet_def_get_follow_angle (LrgPetDef *self)
{
    LrgPetDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PET_DEF (self), 0.0);
    priv = lrg_pet_def_get_instance_private (self);
    return priv->follow_angle;
}

void
lrg_pet_def_set_follow_angle (LrgPetDef *self,
                              gdouble    angle)
{
    LrgPetDefPrivate *priv;

    g_return_if_fail (LRG_IS_PET_DEF (self));
    priv = lrg_pet_def_get_instance_private (self);
    set_double (self, &priv->follow_angle, angle, -2.0 * G_PI, 2.0 * G_PI,
                PROP_FOLLOW_ANGLE);
}

guint
lrg_pet_def_get_health (LrgPetDef *self)
{
    LrgPetDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PET_DEF (self), 0);
    priv = lrg_pet_def_get_instance_private (self);
    return priv->health;
}

void
lrg_pet_def_set_health (LrgPetDef *self,
                        guint      health)
{
    LrgPetDefPrivate *priv;

    g_return_if_fail (LRG_IS_PET_DEF (self));
    priv = lrg_pet_def_get_instance_private (self);
    set_uint (self, &priv->health, health, PROP_HEALTH);
}

guint
lrg_pet_def_get_damage (LrgPetDef *self)
{
    LrgPetDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PET_DEF (self), 0);
    priv = lrg_pet_def_get_instance_private (self);
    return priv->damage;
}

void
lrg_pet_def_set_damage (LrgPetDef *self,
                        guint      damage)
{
    LrgPetDefPrivate *priv;

    g_return_if_fail (LRG_IS_PET_DEF (self));
    priv = lrg_pet_def_get_instance_private (self);
    set_uint (self, &priv->damage, damage, PROP_DAMAGE);
}

gdouble
lrg_pet_def_get_attack_range (LrgPetDef *self)
{
    LrgPetDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PET_DEF (self), 0.0);
    priv = lrg_pet_def_get_instance_private (self);
    return priv->attack_range;
}

void
lrg_pet_def_set_attack_range (LrgPetDef *self,
                              gdouble    range)
{
    LrgPetDefPrivate *priv;

    g_return_if_fail (LRG_IS_PET_DEF (self));
    priv = lrg_pet_def_get_instance_private (self);
    set_double (self, &priv->attack_range, range, 0.0, PET_MAX_DISTANCE,
                PROP_ATTACK_RANGE);
}

gdouble
lrg_pet_def_get_attack_interval (LrgPetDef *self)
{
    LrgPetDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PET_DEF (self), 0.0);
    priv = lrg_pet_def_get_instance_private (self);
    return priv->attack_interval;
}

void
lrg_pet_def_set_attack_interval (LrgPetDef *self,
                                 gdouble    interval)
{
    LrgPetDefPrivate *priv;

    g_return_if_fail (LRG_IS_PET_DEF (self));
    priv = lrg_pet_def_get_instance_private (self);
    set_double (self, &priv->attack_interval, interval, PET_MIN_INTERVAL,
                PET_MAX_INTERVAL, PROP_ATTACK_INTERVAL);
}

gdouble
lrg_pet_def_get_move_speed (LrgPetDef *self)
{
    LrgPetDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PET_DEF (self), 0.0);
    priv = lrg_pet_def_get_instance_private (self);
    return priv->move_speed;
}

void
lrg_pet_def_set_move_speed (LrgPetDef *self,
                            gdouble    speed)
{
    LrgPetDefPrivate *priv;

    g_return_if_fail (LRG_IS_PET_DEF (self));
    priv = lrg_pet_def_get_instance_private (self);
    set_double (self, &priv->move_speed, speed, 0.0, PET_MAX_DISTANCE,
                PROP_MOVE_SPEED);
}

void
lrg_pet_def_add_ability (LrgPetDef   *self,
                         const gchar *ability_id)
{
    LrgPetDefPrivate *priv;

    g_return_if_fail (LRG_IS_PET_DEF (self));
    g_return_if_fail (ability_id != NULL && *ability_id != '\0');
    priv = lrg_pet_def_get_instance_private (self);

    /* Duplicates are silently ignored so data files can be re-applied. */
    if (lrg_pet_def_has_ability (self, ability_id))
        return;

    g_return_if_fail (priv->abilities->len < LRG_PET_DEF_MAX_ABILITIES);
    g_ptr_array_add (priv->abilities, g_strdup (ability_id));
}

gboolean
lrg_pet_def_has_ability (LrgPetDef   *self,
                         const gchar *ability_id)
{
    LrgPetDefPrivate *priv;
    guint             i;

    g_return_val_if_fail (LRG_IS_PET_DEF (self), FALSE);
    if (ability_id == NULL)
        return FALSE;
    priv = lrg_pet_def_get_instance_private (self);

    for (i = 0; i < priv->abilities->len; i++)
    {
        if (g_strcmp0 ((const gchar *)g_ptr_array_index (priv->abilities, i),
                       ability_id) == 0)
            return TRUE;
    }

    return FALSE;
}

GPtrArray *
lrg_pet_def_get_abilities (LrgPetDef *self)
{
    LrgPetDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_PET_DEF (self), NULL);
    priv = lrg_pet_def_get_instance_private (self);
    return priv->abilities;
}
