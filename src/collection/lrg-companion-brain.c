/* lrg-companion-brain.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Companion decision logic. The only state carried between ticks is the
 * current target and whether it came from an explicit order; everything
 * else is derived from the LrgCompanionInput snapshot so the server can
 * replay or unit test any situation deterministically.
 */

#include "config.h"

#include <math.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "collection/lrg-companion-brain.h"

#define BRAIN_MAX_SHORT_RANGE (1000.0)
#define BRAIN_MAX_LONG_RANGE  (100000.0)

/* ------------------------------------------------------------------------ */
/* Boxed input / output                                                     */
/* ------------------------------------------------------------------------ */

G_DEFINE_BOXED_TYPE (LrgCompanionInput, lrg_companion_input,
                     lrg_companion_input_copy,
                     lrg_companion_input_free)

G_DEFINE_BOXED_TYPE (LrgCompanionOutput, lrg_companion_output,
                     lrg_companion_output_copy,
                     lrg_companion_output_free)

LrgCompanionInput *
lrg_companion_input_new (void)
{
    return g_new0 (LrgCompanionInput, 1);
}

LrgCompanionInput *
lrg_companion_input_copy (const LrgCompanionInput *self)
{
    LrgCompanionInput *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new (LrgCompanionInput, 1);
    *copy = *self;
    return copy;
}

void
lrg_companion_input_free (LrgCompanionInput *self)
{
    g_free (self);
}

LrgCompanionOutput *
lrg_companion_output_new (void)
{
    LrgCompanionOutput *output;

    output = g_new0 (LrgCompanionOutput, 1);
    output->action = LRG_COMPANION_ACTION_IDLE;
    return output;
}

LrgCompanionOutput *
lrg_companion_output_copy (const LrgCompanionOutput *self)
{
    LrgCompanionOutput *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new (LrgCompanionOutput, 1);
    *copy = *self;
    return copy;
}

void
lrg_companion_output_free (LrgCompanionOutput *self)
{
    g_free (self);
}

/* ------------------------------------------------------------------------ */
/* Brain                                                                    */
/* ------------------------------------------------------------------------ */

struct _LrgCompanionBrain
{
    GObject            parent_instance;

    LrgCompanionKind   kind;
    LrgCompanionStance stance;
    gdouble            follow_distance;
    gdouble            follow_angle;
    gdouble            leash_range;
    gdouble            teleport_range;
    gdouble            attack_range;

    guint              target;      /* 0 = none */
    gboolean           commanded;   /* target came from command_attack() */
};

G_DEFINE_TYPE (LrgCompanionBrain, lrg_companion_brain, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_KIND,
    PROP_STANCE,
    PROP_FOLLOW_DISTANCE,
    PROP_FOLLOW_ANGLE,
    PROP_LEASH_RANGE,
    PROP_TELEPORT_RANGE,
    PROP_ATTACK_RANGE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
set_range (LrgCompanionBrain *self,
           gdouble           *slot,
           gdouble            value,
           gdouble            min,
           gdouble            max,
           guint              prop)
{
    g_return_if_fail (isfinite (value));
    g_return_if_fail (value >= min && value <= max);

    if (*slot == value)
        return;

    *slot = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
clear_target (LrgCompanionBrain *self)
{
    self->target = 0;
    self->commanded = FALSE;
}

static void
lrg_companion_brain_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgCompanionBrain *self = LRG_COMPANION_BRAIN (object);

    switch (prop_id)
    {
    case PROP_KIND:
        g_value_set_enum (value, (gint)self->kind);
        break;
    case PROP_STANCE:
        g_value_set_enum (value, (gint)self->stance);
        break;
    case PROP_FOLLOW_DISTANCE:
        g_value_set_double (value, self->follow_distance);
        break;
    case PROP_FOLLOW_ANGLE:
        g_value_set_double (value, self->follow_angle);
        break;
    case PROP_LEASH_RANGE:
        g_value_set_double (value, self->leash_range);
        break;
    case PROP_TELEPORT_RANGE:
        g_value_set_double (value, self->teleport_range);
        break;
    case PROP_ATTACK_RANGE:
        g_value_set_double (value, self->attack_range);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_companion_brain_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgCompanionBrain *self = LRG_COMPANION_BRAIN (object);

    switch (prop_id)
    {
    case PROP_KIND:
        lrg_companion_brain_set_kind (self, (LrgCompanionKind)g_value_get_enum (value));
        break;
    case PROP_STANCE:
        lrg_companion_brain_set_stance (self, (LrgCompanionStance)g_value_get_enum (value));
        break;
    case PROP_FOLLOW_DISTANCE:
        lrg_companion_brain_set_follow_distance (self, g_value_get_double (value));
        break;
    case PROP_FOLLOW_ANGLE:
        lrg_companion_brain_set_follow_angle (self, g_value_get_double (value));
        break;
    case PROP_LEASH_RANGE:
        lrg_companion_brain_set_leash_range (self, g_value_get_double (value));
        break;
    case PROP_TELEPORT_RANGE:
        lrg_companion_brain_set_teleport_range (self, g_value_get_double (value));
        break;
    case PROP_ATTACK_RANGE:
        lrg_companion_brain_set_attack_range (self, g_value_get_double (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_companion_brain_class_init (LrgCompanionBrainClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_companion_brain_get_property;
    object_class->set_property = lrg_companion_brain_set_property;

    /**
     * LrgCompanionBrain:kind:
     *
     * Vanity companions never attack.
     */
    properties[PROP_KIND] =
        g_param_spec_enum ("kind", "Kind", "Vanity or combat companion",
                           LRG_TYPE_COMPANION_KIND,
                           LRG_COMPANION_KIND_VANITY,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgCompanionBrain:stance:
     *
     * Combat stance used for automatic target selection.
     */
    properties[PROP_STANCE] =
        g_param_spec_enum ("stance", "Stance", "Combat stance",
                           LRG_TYPE_COMPANION_STANCE,
                           LRG_COMPANION_STANCE_ASSIST,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgCompanionBrain:follow-distance:
     *
     * Distance from the owner of the follow point.
     */
    properties[PROP_FOLLOW_DISTANCE] =
        g_param_spec_double ("follow-distance", "Follow Distance",
                             "Distance of the follow point from the owner",
                             0.0, BRAIN_MAX_SHORT_RANGE, 2.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCompanionBrain:follow-angle:
     *
     * Angle of the follow point relative to the owner's facing, radians.
     */
    properties[PROP_FOLLOW_ANGLE] =
        g_param_spec_double ("follow-angle", "Follow Angle",
                             "Follow point angle relative to owner facing",
                             -2.0 * G_PI, 2.0 * G_PI, 2.356,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCompanionBrain:leash-range:
     *
     * Companion-to-owner distance beyond which combat is abandoned.
     */
    properties[PROP_LEASH_RANGE] =
        g_param_spec_double ("leash-range", "Leash Range",
                             "Distance from owner that breaks off combat",
                             0.0, BRAIN_MAX_LONG_RANGE, 30.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCompanionBrain:teleport-range:
     *
     * Follow-point distance beyond which the companion teleports.
     */
    properties[PROP_TELEPORT_RANGE] =
        g_param_spec_double ("teleport-range", "Teleport Range",
                             "Follow point distance that triggers a teleport",
                             0.0, BRAIN_MAX_LONG_RANGE, 60.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCompanionBrain:attack-range:
     *
     * Reach within which the companion strikes instead of chasing.
     */
    properties[PROP_ATTACK_RANGE] =
        g_param_spec_double ("attack-range", "Attack Range", "Attack reach",
                             0.0, BRAIN_MAX_SHORT_RANGE, 3.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_companion_brain_init (LrgCompanionBrain *self)
{
    self->kind = LRG_COMPANION_KIND_VANITY;
    self->stance = LRG_COMPANION_STANCE_ASSIST;
    self->follow_distance = 2.0;
    self->follow_angle = 2.356;
    self->leash_range = 30.0;
    self->teleport_range = 60.0;
    self->attack_range = 3.0;
    self->target = 0;
    self->commanded = FALSE;
}

LrgCompanionBrain *
lrg_companion_brain_new (LrgCompanionKind kind)
{
    return g_object_new (LRG_TYPE_COMPANION_BRAIN, "kind", kind, NULL);
}

LrgCompanionBrain *
lrg_companion_brain_new_for_pet (LrgPetDef *pet)
{
    g_return_val_if_fail (LRG_IS_PET_DEF (pet), NULL);

    return g_object_new (LRG_TYPE_COMPANION_BRAIN,
                         "kind", lrg_pet_def_get_companion_kind (pet),
                         "follow-distance", lrg_pet_def_get_follow_distance (pet),
                         "follow-angle", lrg_pet_def_get_follow_angle (pet),
                         "attack-range", lrg_pet_def_get_attack_range (pet),
                         NULL);
}

LrgCompanionKind
lrg_companion_brain_get_kind (LrgCompanionBrain *self)
{
    g_return_val_if_fail (LRG_IS_COMPANION_BRAIN (self), LRG_COMPANION_KIND_VANITY);
    return self->kind;
}

void
lrg_companion_brain_set_kind (LrgCompanionBrain *self,
                              LrgCompanionKind   kind)
{
    g_return_if_fail (LRG_IS_COMPANION_BRAIN (self));
    g_return_if_fail ((guint)kind <= (guint)LRG_COMPANION_KIND_COMBAT);

    if (kind == LRG_COMPANION_KIND_VANITY)
        clear_target (self);

    if (self->kind == kind)
        return;

    self->kind = kind;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KIND]);
}

LrgCompanionStance
lrg_companion_brain_get_stance (LrgCompanionBrain *self)
{
    g_return_val_if_fail (LRG_IS_COMPANION_BRAIN (self), LRG_COMPANION_STANCE_ASSIST);
    return self->stance;
}

void
lrg_companion_brain_set_stance (LrgCompanionBrain  *self,
                                LrgCompanionStance  stance)
{
    g_return_if_fail (LRG_IS_COMPANION_BRAIN (self));
    g_return_if_fail ((guint)stance <= (guint)LRG_COMPANION_STANCE_AGGRESSIVE);

    if (self->stance == stance)
        return;

    self->stance = stance;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STANCE]);
}

gdouble
lrg_companion_brain_get_follow_distance (LrgCompanionBrain *self)
{
    g_return_val_if_fail (LRG_IS_COMPANION_BRAIN (self), 0.0);
    return self->follow_distance;
}

void
lrg_companion_brain_set_follow_distance (LrgCompanionBrain *self,
                                         gdouble            distance)
{
    g_return_if_fail (LRG_IS_COMPANION_BRAIN (self));
    set_range (self, &self->follow_distance, distance, 0.0,
               BRAIN_MAX_SHORT_RANGE, PROP_FOLLOW_DISTANCE);
}

gdouble
lrg_companion_brain_get_follow_angle (LrgCompanionBrain *self)
{
    g_return_val_if_fail (LRG_IS_COMPANION_BRAIN (self), 0.0);
    return self->follow_angle;
}

void
lrg_companion_brain_set_follow_angle (LrgCompanionBrain *self,
                                      gdouble            angle)
{
    g_return_if_fail (LRG_IS_COMPANION_BRAIN (self));
    set_range (self, &self->follow_angle, angle, -2.0 * G_PI, 2.0 * G_PI,
               PROP_FOLLOW_ANGLE);
}

gdouble
lrg_companion_brain_get_leash_range (LrgCompanionBrain *self)
{
    g_return_val_if_fail (LRG_IS_COMPANION_BRAIN (self), 0.0);
    return self->leash_range;
}

void
lrg_companion_brain_set_leash_range (LrgCompanionBrain *self,
                                     gdouble            range)
{
    g_return_if_fail (LRG_IS_COMPANION_BRAIN (self));
    set_range (self, &self->leash_range, range, 0.0, BRAIN_MAX_LONG_RANGE,
               PROP_LEASH_RANGE);
}

gdouble
lrg_companion_brain_get_teleport_range (LrgCompanionBrain *self)
{
    g_return_val_if_fail (LRG_IS_COMPANION_BRAIN (self), 0.0);
    return self->teleport_range;
}

void
lrg_companion_brain_set_teleport_range (LrgCompanionBrain *self,
                                        gdouble            range)
{
    g_return_if_fail (LRG_IS_COMPANION_BRAIN (self));
    set_range (self, &self->teleport_range, range, 0.0, BRAIN_MAX_LONG_RANGE,
               PROP_TELEPORT_RANGE);
}

gdouble
lrg_companion_brain_get_attack_range (LrgCompanionBrain *self)
{
    g_return_val_if_fail (LRG_IS_COMPANION_BRAIN (self), 0.0);
    return self->attack_range;
}

void
lrg_companion_brain_set_attack_range (LrgCompanionBrain *self,
                                      gdouble            range)
{
    g_return_if_fail (LRG_IS_COMPANION_BRAIN (self));
    set_range (self, &self->attack_range, range, 0.0, BRAIN_MAX_SHORT_RANGE,
               PROP_ATTACK_RANGE);
}

void
lrg_companion_brain_command_attack (LrgCompanionBrain *self,
                                    guint              target)
{
    g_return_if_fail (LRG_IS_COMPANION_BRAIN (self));

    /* Vanity companions ignore attack orders entirely. */
    if (self->kind == LRG_COMPANION_KIND_VANITY || target == 0)
    {
        clear_target (self);
        return;
    }

    self->target = target;
    self->commanded = TRUE;
}

void
lrg_companion_brain_command_follow (LrgCompanionBrain *self)
{
    g_return_if_fail (LRG_IS_COMPANION_BRAIN (self));

    clear_target (self);
}

guint
lrg_companion_brain_get_target (LrgCompanionBrain *self)
{
    g_return_val_if_fail (LRG_IS_COMPANION_BRAIN (self), 0);
    return self->target;
}

gboolean
lrg_companion_brain_is_commanded (LrgCompanionBrain *self)
{
    g_return_val_if_fail (LRG_IS_COMPANION_BRAIN (self), FALSE);
    return self->commanded;
}

void
lrg_companion_brain_follow_point (LrgCompanionBrain *self,
                                  gdouble            owner_x,
                                  gdouble            owner_z,
                                  gdouble            owner_facing,
                                  gdouble           *out_x,
                                  gdouble           *out_z)
{
    gdouble angle;

    g_return_if_fail (LRG_IS_COMPANION_BRAIN (self));
    g_return_if_fail (out_x != NULL && out_z != NULL);

    /* Facing 0 looks down -Z; positive angles turn toward -X, so the unit
     * direction for angle a is (-sin a, -cos a). */
    angle = owner_facing + self->follow_angle;
    *out_x = owner_x - sin (angle) * self->follow_distance;
    *out_z = owner_z - cos (angle) * self->follow_distance;
}

/* ------------------------------------------------------------------------ */
/* Decision logic                                                           */
/* ------------------------------------------------------------------------ */

static gboolean
finite2 (gdouble x,
         gdouble z)
{
    return isfinite (x) && isfinite (z);
}

/* resolve_hostile:
 * Finds a position for hostile @id in the snapshot. The owner's target
 * counts only while alive; if it is reported dead the id is invalid even
 * when it also appears as an attacker. */
static gboolean
resolve_hostile (const LrgCompanionInput *input,
                 guint                    id,
                 gdouble                 *out_x,
                 gdouble                 *out_z)
{
    if (id == 0)
        return FALSE;

    if (input->owner_target == id)
    {
        if (!input->owner_target_alive)
            return FALSE;
        if (finite2 (input->owner_target_x, input->owner_target_z))
        {
            *out_x = input->owner_target_x;
            *out_z = input->owner_target_z;
            return TRUE;
        }
    }
    if (input->owner_attacker == id &&
        finite2 (input->owner_attacker_x, input->owner_attacker_z))
    {
        *out_x = input->owner_attacker_x;
        *out_z = input->owner_attacker_z;
        return TRUE;
    }
    if (input->self_attacker == id &&
        finite2 (input->self_attacker_x, input->self_attacker_z))
    {
        *out_x = input->self_attacker_x;
        *out_z = input->self_attacker_z;
        return TRUE;
    }

    return FALSE;
}

/* select_target:
 * Stance-driven automatic target choice; returns 0 for none. */
static guint
select_target (LrgCompanionBrain       *self,
               const LrgCompanionInput *input)
{
    gdouble x;
    gdouble z;

    switch (self->stance)
    {
    case LRG_COMPANION_STANCE_PASSIVE:
        return 0;
    case LRG_COMPANION_STANCE_ASSIST:
    case LRG_COMPANION_STANCE_AGGRESSIVE:
        if (resolve_hostile (input, input->owner_target, &x, &z))
            return input->owner_target;
        /* fall through: then behave defensively */
        G_GNUC_FALLTHROUGH;
    case LRG_COMPANION_STANCE_DEFENSIVE:
    default:
        if (resolve_hostile (input, input->owner_attacker, &x, &z))
            return input->owner_attacker;
        if (resolve_hostile (input, input->self_attacker, &x, &z))
            return input->self_attacker;
        return 0;
    }
}

static void
emit (LrgCompanionOutput *output,
      LrgCompanionAction  action,
      gdouble             x,
      gdouble             z,
      guint               target)
{
    output->action = action;
    output->dest_x = x;
    output->dest_z = z;
    output->target = target;
}

void
lrg_companion_brain_think (LrgCompanionBrain       *self,
                           const LrgCompanionInput *input,
                           LrgCompanionOutput      *output)
{
    gdouble follow_x;
    gdouble follow_z;
    gdouble owner_dist;
    gdouble follow_dist;
    gdouble target_x;
    gdouble target_z;

    g_return_if_fail (LRG_IS_COMPANION_BRAIN (self));
    g_return_if_fail (input != NULL);
    g_return_if_fail (output != NULL);

    /* 1. Dead or garbage geometry: stand still and forget the fight. */
    if (!input->alive ||
        !finite2 (input->x, input->z) ||
        !finite2 (input->owner_x, input->owner_z) ||
        !isfinite (input->owner_facing))
    {
        clear_target (self);
        if (finite2 (input->x, input->z))
            emit (output, LRG_COMPANION_ACTION_IDLE, input->x, input->z, 0);
        else
            emit (output, LRG_COMPANION_ACTION_IDLE, 0.0, 0.0, 0);
        return;
    }

    lrg_companion_brain_follow_point (self, input->owner_x, input->owner_z,
                                      input->owner_facing, &follow_x, &follow_z);
    owner_dist = hypot (input->x - input->owner_x, input->z - input->owner_z);
    follow_dist = hypot (input->x - follow_x, input->z - follow_z);

    if (self->kind != LRG_COMPANION_KIND_VANITY)
    {
        /* 3. Leash: too far from the owner, abandon combat and come back. */
        if (owner_dist > self->leash_range)
        {
            clear_target (self);
            if (follow_dist > self->teleport_range)
                emit (output, LRG_COMPANION_ACTION_TELEPORT, follow_x, follow_z, 0);
            else
                emit (output, LRG_COMPANION_ACTION_RETURN, follow_x, follow_z, 0);
            return;
        }

        /* 4. Target maintenance and selection. */
        if (self->stance == LRG_COMPANION_STANCE_PASSIVE && !self->commanded)
            clear_target (self);
        if (self->target != 0 &&
            !resolve_hostile (input, self->target, &target_x, &target_z))
            clear_target (self);
        if (self->target == 0)
        {
            self->target = select_target (self, input);
            self->commanded = FALSE;
        }

        if (self->target != 0 &&
            resolve_hostile (input, self->target, &target_x, &target_z))
        {
            gdouble target_dist = hypot (input->x - target_x, input->z - target_z);

            if (target_dist <= self->attack_range)
                emit (output, LRG_COMPANION_ACTION_ATTACK, input->x, input->z,
                      self->target);
            else
                emit (output, LRG_COMPANION_ACTION_ATTACK, target_x, target_z,
                      self->target);
            return;
        }
    }
    else
    {
        /* 2. Vanity companions never hold a target. */
        clear_target (self);
    }

    /* 5. Follow. */
    if (follow_dist > self->teleport_range)
        emit (output, LRG_COMPANION_ACTION_TELEPORT, follow_x, follow_z, 0);
    else if (follow_dist > LRG_COMPANION_ARRIVE_DISTANCE)
        emit (output, LRG_COMPANION_ACTION_FOLLOW, follow_x, follow_z, 0);
    else
        emit (output, LRG_COMPANION_ACTION_IDLE, input->x, input->z, 0);
}
