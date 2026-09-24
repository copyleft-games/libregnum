/* lrg-ability-def.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Ability definitions and their effects. Every scalar field is a GObject
 * property so YAML data loading can populate definitions directly.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "progression/lrg-ability-def.h"

#include <math.h>

/* ==========================================================================
 * LrgAbilityEffect
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgAbilityEffect, lrg_ability_effect,
                     lrg_ability_effect_copy,
                     lrg_ability_effect_free)

LrgAbilityEffect *
lrg_ability_effect_new (const gchar *kind)
{
    LrgAbilityEffect *self;

    g_return_val_if_fail (kind != NULL && *kind != '\0', NULL);

    self = g_new0 (LrgAbilityEffect, 1);
    self->kind = g_strdup (kind);
    self->max_targets = 1;
    return self;
}

LrgAbilityEffect *
lrg_ability_effect_copy (const LrgAbilityEffect *self)
{
    LrgAbilityEffect *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgAbilityEffect, 1);
    *copy = *self;
    copy->kind = g_strdup (self->kind);
    copy->aura_id = g_strdup (self->aura_id);
    copy->stat = g_strdup (self->stat);
    return copy;
}

void
lrg_ability_effect_free (LrgAbilityEffect *self)
{
    if (self == NULL)
        return;

    g_free (self->kind);
    g_free (self->aura_id);
    g_free (self->stat);
    g_free (self);
}

/* ==========================================================================
 * LrgAbilityDef
 * ========================================================================== */

typedef struct
{
    gchar                 *id;
    gchar                 *name;
    gchar                 *description;
    gchar                 *icon;
    gchar                 *class_id;
    guint                  min_level;
    guint                  rank;
    gchar                 *resource;
    guint                  cost;
    gdouble                cooldown;
    gchar                 *category;
    gdouble                category_cooldown;
    gboolean               triggers_gcd;
    gdouble                cast_time;
    gboolean               channel;
    gdouble                range;
    gboolean               passive;
    LrgAbilityLearnSource  learn_source;
    guint                  trainer_cost;
    gchar                 *required_spec;
    gchar                 *required_talent;
    gchar                 *replaces;
    gchar                 *target;
    GPtrArray             *tags;      /* gchar* */
    GPtrArray             *effects;   /* LrgAbilityEffect* */
} LrgAbilityDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgAbilityDef, lrg_ability_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_ICON,
    PROP_CLASS_ID,
    PROP_MIN_LEVEL,
    PROP_RANK,
    PROP_RESOURCE,
    PROP_COST,
    PROP_COOLDOWN,
    PROP_CATEGORY,
    PROP_CATEGORY_COOLDOWN,
    PROP_TRIGGERS_GCD,
    PROP_CAST_TIME,
    PROP_CHANNEL,
    PROP_RANGE,
    PROP_PASSIVE,
    PROP_LEARN_SOURCE,
    PROP_TRAINER_COST,
    PROP_REQUIRED_SPEC,
    PROP_REQUIRED_TALENT,
    PROP_REPLACES,
    PROP_TARGET,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * Change-notifying field helpers shared by the setters and set_property.
 * -------------------------------------------------------------------------- */

static void
update_string (LrgAbilityDef  *self,
               gchar         **slot,
               const gchar    *value,
               guint           prop)
{
    if (g_strcmp0 (*slot, value) == 0)
        return;
    g_free (*slot);
    *slot = g_strdup (value);
    g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
update_uint (LrgAbilityDef *self,
             guint         *slot,
             guint          value,
             guint          prop)
{
    if (*slot == value)
        return;
    *slot = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
update_bool (LrgAbilityDef *self,
             gboolean      *slot,
             gboolean       value,
             guint          prop)
{
    value = value ? TRUE : FALSE;
    if (*slot == value)
        return;
    *slot = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

/* Rejects NaN/inf and values outside [0, max] with a critical. */
static void
update_double (LrgAbilityDef *self,
               gdouble       *slot,
               gdouble        value,
               gdouble        max,
               guint          prop)
{
    g_return_if_fail (isfinite (value) && value >= 0.0 && value <= max);

    if (*slot == value)
        return;
    *slot = value;
    g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static gboolean
lrg_ability_def_real_can_use (LrgAbilityDef  *self,
                              gpointer        caster,
                              GError        **error)
{
    (void) self;
    (void) caster;
    (void) error;
    return TRUE;
}

static void
lrg_ability_def_finalize (GObject *object)
{
    LrgAbilityDef        *self = LRG_ABILITY_DEF (object);
    LrgAbilityDefPrivate *priv = lrg_ability_def_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->icon, g_free);
    g_clear_pointer (&priv->class_id, g_free);
    g_clear_pointer (&priv->resource, g_free);
    g_clear_pointer (&priv->category, g_free);
    g_clear_pointer (&priv->required_spec, g_free);
    g_clear_pointer (&priv->required_talent, g_free);
    g_clear_pointer (&priv->replaces, g_free);
    g_clear_pointer (&priv->target, g_free);
    g_clear_pointer (&priv->tags, g_ptr_array_unref);
    g_clear_pointer (&priv->effects, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_ability_def_parent_class)->finalize (object);
}

static void
lrg_ability_def_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgAbilityDef        *self = LRG_ABILITY_DEF (object);
    LrgAbilityDefPrivate *priv = lrg_ability_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:                g_value_set_string (value, priv->id); break;
    case PROP_NAME:              g_value_set_string (value, priv->name); break;
    case PROP_DESCRIPTION:       g_value_set_string (value, priv->description); break;
    case PROP_ICON:              g_value_set_string (value, priv->icon); break;
    case PROP_CLASS_ID:          g_value_set_string (value, priv->class_id); break;
    case PROP_MIN_LEVEL:         g_value_set_uint (value, priv->min_level); break;
    case PROP_RANK:              g_value_set_uint (value, priv->rank); break;
    case PROP_RESOURCE:          g_value_set_string (value, priv->resource); break;
    case PROP_COST:              g_value_set_uint (value, priv->cost); break;
    case PROP_COOLDOWN:          g_value_set_double (value, priv->cooldown); break;
    case PROP_CATEGORY:          g_value_set_string (value, priv->category); break;
    case PROP_CATEGORY_COOLDOWN: g_value_set_double (value, priv->category_cooldown); break;
    case PROP_TRIGGERS_GCD:      g_value_set_boolean (value, priv->triggers_gcd); break;
    case PROP_CAST_TIME:         g_value_set_double (value, priv->cast_time); break;
    case PROP_CHANNEL:           g_value_set_boolean (value, priv->channel); break;
    case PROP_RANGE:             g_value_set_double (value, priv->range); break;
    case PROP_PASSIVE:           g_value_set_boolean (value, priv->passive); break;
    case PROP_LEARN_SOURCE:      g_value_set_enum (value, priv->learn_source); break;
    case PROP_TRAINER_COST:      g_value_set_uint (value, priv->trainer_cost); break;
    case PROP_REQUIRED_SPEC:     g_value_set_string (value, priv->required_spec); break;
    case PROP_REQUIRED_TALENT:   g_value_set_string (value, priv->required_talent); break;
    case PROP_REPLACES:          g_value_set_string (value, priv->replaces); break;
    case PROP_TARGET:            g_value_set_string (value, priv->target); break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_ability_def_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgAbilityDef        *self = LRG_ABILITY_DEF (object);
    LrgAbilityDefPrivate *priv = lrg_ability_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        /* construct-only: no notification needed */
        g_free (priv->id);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        update_string (self, &priv->name, g_value_get_string (value), prop_id);
        break;
    case PROP_DESCRIPTION:
        update_string (self, &priv->description, g_value_get_string (value), prop_id);
        break;
    case PROP_ICON:
        update_string (self, &priv->icon, g_value_get_string (value), prop_id);
        break;
    case PROP_CLASS_ID:
        update_string (self, &priv->class_id, g_value_get_string (value), prop_id);
        break;
    case PROP_MIN_LEVEL:
        update_uint (self, &priv->min_level, g_value_get_uint (value), prop_id);
        break;
    case PROP_RANK:
        update_uint (self, &priv->rank, g_value_get_uint (value), prop_id);
        break;
    case PROP_RESOURCE:
        update_string (self, &priv->resource, g_value_get_string (value), prop_id);
        break;
    case PROP_COST:
        update_uint (self, &priv->cost, g_value_get_uint (value), prop_id);
        break;
    case PROP_COOLDOWN:
        update_double (self, &priv->cooldown, g_value_get_double (value),
                       LRG_ABILITY_MAX_COOLDOWN, prop_id);
        break;
    case PROP_CATEGORY:
        update_string (self, &priv->category, g_value_get_string (value), prop_id);
        break;
    case PROP_CATEGORY_COOLDOWN:
        update_double (self, &priv->category_cooldown, g_value_get_double (value),
                       LRG_ABILITY_MAX_COOLDOWN, prop_id);
        break;
    case PROP_TRIGGERS_GCD:
        update_bool (self, &priv->triggers_gcd, g_value_get_boolean (value), prop_id);
        break;
    case PROP_CAST_TIME:
        update_double (self, &priv->cast_time, g_value_get_double (value),
                       LRG_ABILITY_MAX_COOLDOWN, prop_id);
        break;
    case PROP_CHANNEL:
        update_bool (self, &priv->channel, g_value_get_boolean (value), prop_id);
        break;
    case PROP_RANGE:
        update_double (self, &priv->range, g_value_get_double (value),
                       LRG_ABILITY_MAX_RANGE, prop_id);
        break;
    case PROP_PASSIVE:
        update_bool (self, &priv->passive, g_value_get_boolean (value), prop_id);
        break;
    case PROP_LEARN_SOURCE:
        if (priv->learn_source != (LrgAbilityLearnSource) g_value_get_enum (value))
        {
            priv->learn_source = (LrgAbilityLearnSource) g_value_get_enum (value);
            g_object_notify_by_pspec (object, pspec);
        }
        break;
    case PROP_TRAINER_COST:
        update_uint (self, &priv->trainer_cost, g_value_get_uint (value), prop_id);
        break;
    case PROP_REQUIRED_SPEC:
        update_string (self, &priv->required_spec, g_value_get_string (value), prop_id);
        break;
    case PROP_REQUIRED_TALENT:
        update_string (self, &priv->required_talent, g_value_get_string (value), prop_id);
        break;
    case PROP_REPLACES:
        update_string (self, &priv->replaces, g_value_get_string (value), prop_id);
        break;
    case PROP_TARGET:
        update_string (self, &priv->target, g_value_get_string (value), prop_id);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

#define RW_FLAGS (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS)

static GParamSpec *
string_pspec (const gchar *name,
              const gchar *blurb)
{
    return g_param_spec_string (name, NULL, blurb, NULL, RW_FLAGS);
}

static GParamSpec *
seconds_pspec (const gchar *name,
               const gchar *blurb,
               gdouble      max)
{
    return g_param_spec_double (name, NULL, blurb, 0.0, max, 0.0, RW_FLAGS);
}

static void
lrg_ability_def_class_init (LrgAbilityDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_ability_def_finalize;
    object_class->get_property = lrg_ability_def_get_property;
    object_class->set_property = lrg_ability_def_set_property;

    klass->can_use = lrg_ability_def_real_can_use;

    /**
     * LrgAbilityDef:id:
     *
     * Unique ability identifier (construct-only).
     */
    properties[PROP_ID] =
        g_param_spec_string ("id", NULL, "Ability identifier", NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);
    /**
     * LrgAbilityDef:name:
     *
     * Display name.
     */
    properties[PROP_NAME] = string_pspec ("name", "Display name");
    /**
     * LrgAbilityDef:description:
     *
     * Tooltip text.
     */
    properties[PROP_DESCRIPTION] = string_pspec ("description", "Tooltip text");
    /**
     * LrgAbilityDef:icon:
     *
     * Icon key.
     */
    properties[PROP_ICON] = string_pspec ("icon", "Icon key");
    /**
     * LrgAbilityDef:class-id:
     *
     * Owning class, %NULL for any class.
     */
    properties[PROP_CLASS_ID] = string_pspec ("class-id", "Owning class");
    /**
     * LrgAbilityDef:min-level:
     *
     * Level required to learn the ability.
     */
    properties[PROP_MIN_LEVEL] =
        g_param_spec_uint ("min-level", NULL, "Required level",
                           0, G_MAXUINT, 1, RW_FLAGS);
    /**
     * LrgAbilityDef:rank:
     *
     * Rank of this ability.
     */
    properties[PROP_RANK] =
        g_param_spec_uint ("rank", NULL, "Ability rank", 1, G_MAXUINT, 1, RW_FLAGS);
    /**
     * LrgAbilityDef:resource:
     *
     * Resource kind spent on use, e.g. "mana".
     */
    properties[PROP_RESOURCE] = string_pspec ("resource", "Resource kind");
    /**
     * LrgAbilityDef:cost:
     *
     * Resource cost.
     */
    properties[PROP_COST] =
        g_param_spec_uint ("cost", NULL, "Resource cost", 0, G_MAXUINT, 0, RW_FLAGS);
    /**
     * LrgAbilityDef:cooldown:
     *
     * Own cooldown in seconds.
     */
    properties[PROP_COOLDOWN] =
        seconds_pspec ("cooldown", "Cooldown seconds", LRG_ABILITY_MAX_COOLDOWN);
    /**
     * LrgAbilityDef:category:
     *
     * Shared cooldown category.
     */
    properties[PROP_CATEGORY] = string_pspec ("category", "Shared cooldown category");
    /**
     * LrgAbilityDef:category-cooldown:
     *
     * Seconds the shared category locks after use.
     */
    properties[PROP_CATEGORY_COOLDOWN] =
        seconds_pspec ("category-cooldown", "Category cooldown seconds",
                       LRG_ABILITY_MAX_COOLDOWN);
    /**
     * LrgAbilityDef:triggers-gcd:
     *
     * Whether the ability starts and respects the global cooldown.
     */
    properties[PROP_TRIGGERS_GCD] =
        g_param_spec_boolean ("triggers-gcd", NULL, "Uses the global cooldown",
                              TRUE, RW_FLAGS);
    /**
     * LrgAbilityDef:cast-time:
     *
     * Cast or channel time in seconds.
     */
    properties[PROP_CAST_TIME] =
        seconds_pspec ("cast-time", "Cast time seconds", LRG_ABILITY_MAX_COOLDOWN);
    /**
     * LrgAbilityDef:channel:
     *
     * Whether the cast time is a channel.
     */
    properties[PROP_CHANNEL] =
        g_param_spec_boolean ("channel", NULL, "Channelled", FALSE, RW_FLAGS);
    /**
     * LrgAbilityDef:range:
     *
     * Maximum range in world units.
     */
    properties[PROP_RANGE] =
        seconds_pspec ("range", "Range in world units", LRG_ABILITY_MAX_RANGE);
    /**
     * LrgAbilityDef:passive:
     *
     * Whether the ability is always on.
     */
    properties[PROP_PASSIVE] =
        g_param_spec_boolean ("passive", NULL, "Passive", FALSE, RW_FLAGS);
    /**
     * LrgAbilityDef:learn-source:
     *
     * How the ability is learned.
     */
    properties[PROP_LEARN_SOURCE] =
        g_param_spec_enum ("learn-source", NULL, "How the ability is learned",
                           LRG_TYPE_ABILITY_LEARN_SOURCE,
                           LRG_ABILITY_LEARN_SOURCE_TRAINER, RW_FLAGS);
    /**
     * LrgAbilityDef:trainer-cost:
     *
     * Price charged by a trainer.
     */
    properties[PROP_TRAINER_COST] =
        g_param_spec_uint ("trainer-cost", NULL, "Trainer price", 0, G_MAXUINT, 0, RW_FLAGS);
    /**
     * LrgAbilityDef:required-spec:
     *
     * Talent tree id required as primary spec.
     */
    properties[PROP_REQUIRED_SPEC] = string_pspec ("required-spec", "Required spec tree");
    /**
     * LrgAbilityDef:required-talent:
     *
     * Talent node id that grants the ability.
     */
    properties[PROP_REQUIRED_TALENT] = string_pspec ("required-talent", "Required talent node");
    /**
     * LrgAbilityDef:replaces:
     *
     * Lower-rank ability upgraded by this one.
     */
    properties[PROP_REPLACES] = string_pspec ("replaces", "Replaced lower rank");
    /**
     * LrgAbilityDef:target:
     *
     * Targeting mode such as "enemy", "ally", "self" or "ground".
     */
    properties[PROP_TARGET] = string_pspec ("target", "Targeting mode");

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
effect_free_wrapper (gpointer data)
{
    lrg_ability_effect_free ((LrgAbilityEffect *) data);
}

static void
lrg_ability_def_init (LrgAbilityDef *self)
{
    LrgAbilityDefPrivate *priv = lrg_ability_def_get_instance_private (self);

    priv->min_level = 1;
    priv->rank = 1;
    priv->triggers_gcd = TRUE;
    priv->learn_source = LRG_ABILITY_LEARN_SOURCE_TRAINER;
    priv->tags = g_ptr_array_new_with_free_func (g_free);
    priv->effects = g_ptr_array_new_with_free_func (effect_free_wrapper);
}

LrgAbilityDef *
lrg_ability_def_new (const gchar *id)
{
    g_return_val_if_fail (id != NULL && *id != '\0', NULL);

    return g_object_new (LRG_TYPE_ABILITY_DEF, "id", id, NULL);
}

/* --------------------------------------------------------------------------
 * Accessors. Getters read the private struct; setters go through the same
 * helpers as set_property so notification happens only on change.
 * -------------------------------------------------------------------------- */

#define PRIV(self) ((LrgAbilityDefPrivate *) lrg_ability_def_get_instance_private (self))

#define DEFINE_STRING_ACCESSORS(field, prop)                                       \
const gchar *                                                                      \
lrg_ability_def_get_##field (LrgAbilityDef *self)                                  \
{                                                                                  \
    g_return_val_if_fail (LRG_IS_ABILITY_DEF (self), NULL);                        \
    return PRIV (self)->field;                                                     \
}                                                                                  \
void                                                                               \
lrg_ability_def_set_##field (LrgAbilityDef *self,                                  \
                             const gchar   *value)                                 \
{                                                                                  \
    g_return_if_fail (LRG_IS_ABILITY_DEF (self));                                  \
    update_string (self, &PRIV (self)->field, value, prop);                        \
}

#define DEFINE_UINT_ACCESSORS(field, prop)                                         \
guint                                                                              \
lrg_ability_def_get_##field (LrgAbilityDef *self)                                  \
{                                                                                  \
    g_return_val_if_fail (LRG_IS_ABILITY_DEF (self), 0);                           \
    return PRIV (self)->field;                                                     \
}                                                                                  \
void                                                                               \
lrg_ability_def_set_##field (LrgAbilityDef *self,                                  \
                             guint          value)                                 \
{                                                                                  \
    g_return_if_fail (LRG_IS_ABILITY_DEF (self));                                  \
    update_uint (self, &PRIV (self)->field, value, prop);                          \
}

#define DEFINE_BOOL_ACCESSORS(field, prop)                                         \
gboolean                                                                           \
lrg_ability_def_get_##field (LrgAbilityDef *self)                                  \
{                                                                                  \
    g_return_val_if_fail (LRG_IS_ABILITY_DEF (self), FALSE);                       \
    return PRIV (self)->field;                                                     \
}                                                                                  \
void                                                                               \
lrg_ability_def_set_##field (LrgAbilityDef *self,                                  \
                             gboolean       value)                                 \
{                                                                                  \
    g_return_if_fail (LRG_IS_ABILITY_DEF (self));                                  \
    update_bool (self, &PRIV (self)->field, value, prop);                          \
}

#define DEFINE_DOUBLE_ACCESSORS(field, prop, max)                                  \
gdouble                                                                            \
lrg_ability_def_get_##field (LrgAbilityDef *self)                                  \
{                                                                                  \
    g_return_val_if_fail (LRG_IS_ABILITY_DEF (self), 0.0);                         \
    return PRIV (self)->field;                                                     \
}                                                                                  \
void                                                                               \
lrg_ability_def_set_##field (LrgAbilityDef *self,                                  \
                             gdouble        value)                                 \
{                                                                                  \
    g_return_if_fail (LRG_IS_ABILITY_DEF (self));                                  \
    update_double (self, &PRIV (self)->field, value, max, prop);                   \
}

const gchar *
lrg_ability_def_get_id (LrgAbilityDef *self)
{
    g_return_val_if_fail (LRG_IS_ABILITY_DEF (self), NULL);
    return PRIV (self)->id;
}

DEFINE_STRING_ACCESSORS (name, PROP_NAME)
DEFINE_STRING_ACCESSORS (description, PROP_DESCRIPTION)
DEFINE_STRING_ACCESSORS (icon, PROP_ICON)
DEFINE_STRING_ACCESSORS (class_id, PROP_CLASS_ID)
DEFINE_UINT_ACCESSORS (min_level, PROP_MIN_LEVEL)
DEFINE_STRING_ACCESSORS (resource, PROP_RESOURCE)
DEFINE_UINT_ACCESSORS (cost, PROP_COST)
DEFINE_DOUBLE_ACCESSORS (cooldown, PROP_COOLDOWN, LRG_ABILITY_MAX_COOLDOWN)
DEFINE_STRING_ACCESSORS (category, PROP_CATEGORY)
DEFINE_DOUBLE_ACCESSORS (category_cooldown, PROP_CATEGORY_COOLDOWN, LRG_ABILITY_MAX_COOLDOWN)
DEFINE_BOOL_ACCESSORS (triggers_gcd, PROP_TRIGGERS_GCD)
DEFINE_DOUBLE_ACCESSORS (cast_time, PROP_CAST_TIME, LRG_ABILITY_MAX_COOLDOWN)
DEFINE_BOOL_ACCESSORS (channel, PROP_CHANNEL)
DEFINE_DOUBLE_ACCESSORS (range, PROP_RANGE, LRG_ABILITY_MAX_RANGE)
DEFINE_BOOL_ACCESSORS (passive, PROP_PASSIVE)
DEFINE_UINT_ACCESSORS (trainer_cost, PROP_TRAINER_COST)
DEFINE_STRING_ACCESSORS (target, PROP_TARGET)
DEFINE_STRING_ACCESSORS (replaces, PROP_REPLACES)

guint
lrg_ability_def_get_rank (LrgAbilityDef *self)
{
    g_return_val_if_fail (LRG_IS_ABILITY_DEF (self), 1);
    return PRIV (self)->rank;
}

void
lrg_ability_def_set_rank (LrgAbilityDef *self,
                          guint          rank)
{
    g_return_if_fail (LRG_IS_ABILITY_DEF (self));
    g_return_if_fail (rank >= 1);
    update_uint (self, &PRIV (self)->rank, rank, PROP_RANK);
}

const gchar *
lrg_ability_def_get_required_spec (LrgAbilityDef *self)
{
    g_return_val_if_fail (LRG_IS_ABILITY_DEF (self), NULL);
    return PRIV (self)->required_spec;
}

void
lrg_ability_def_set_required_spec (LrgAbilityDef *self,
                                   const gchar   *spec_id)
{
    g_return_if_fail (LRG_IS_ABILITY_DEF (self));
    update_string (self, &PRIV (self)->required_spec, spec_id, PROP_REQUIRED_SPEC);
}

const gchar *
lrg_ability_def_get_required_talent (LrgAbilityDef *self)
{
    g_return_val_if_fail (LRG_IS_ABILITY_DEF (self), NULL);
    return PRIV (self)->required_talent;
}

void
lrg_ability_def_set_required_talent (LrgAbilityDef *self,
                                     const gchar   *talent_id)
{
    g_return_if_fail (LRG_IS_ABILITY_DEF (self));
    update_string (self, &PRIV (self)->required_talent, talent_id, PROP_REQUIRED_TALENT);
}

LrgAbilityLearnSource
lrg_ability_def_get_learn_source (LrgAbilityDef *self)
{
    g_return_val_if_fail (LRG_IS_ABILITY_DEF (self), LRG_ABILITY_LEARN_SOURCE_TRAINER);
    return PRIV (self)->learn_source;
}

void
lrg_ability_def_set_learn_source (LrgAbilityDef         *self,
                                  LrgAbilityLearnSource  source)
{
    g_return_if_fail (LRG_IS_ABILITY_DEF (self));
    g_return_if_fail (source >= LRG_ABILITY_LEARN_SOURCE_AUTO &&
                      source <= LRG_ABILITY_LEARN_SOURCE_ITEM);

    if (PRIV (self)->learn_source == source)
        return;
    PRIV (self)->learn_source = source;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LEARN_SOURCE]);
}

/* --------------------------------------------------------------------------
 * Tags, effects and checks
 * -------------------------------------------------------------------------- */

void
lrg_ability_def_add_tag (LrgAbilityDef *self,
                         const gchar   *tag)
{
    g_return_if_fail (LRG_IS_ABILITY_DEF (self));
    g_return_if_fail (tag != NULL && *tag != '\0');

    if (lrg_ability_def_has_tag (self, tag))
        return;
    g_ptr_array_add (PRIV (self)->tags, g_strdup (tag));
}

gboolean
lrg_ability_def_has_tag (LrgAbilityDef *self,
                         const gchar   *tag)
{
    g_return_val_if_fail (LRG_IS_ABILITY_DEF (self), FALSE);

    if (tag == NULL)
        return FALSE;
    return g_ptr_array_find_with_equal_func (PRIV (self)->tags, tag, g_str_equal, NULL);
}

GPtrArray *
lrg_ability_def_get_tags (LrgAbilityDef *self)
{
    g_return_val_if_fail (LRG_IS_ABILITY_DEF (self), NULL);
    return PRIV (self)->tags;
}

void
lrg_ability_def_add_effect (LrgAbilityDef    *self,
                            LrgAbilityEffect *effect)
{
    g_return_if_fail (LRG_IS_ABILITY_DEF (self));
    g_return_if_fail (effect != NULL);

    g_ptr_array_add (PRIV (self)->effects, effect);
}

GPtrArray *
lrg_ability_def_get_effects (LrgAbilityDef *self)
{
    g_return_val_if_fail (LRG_IS_ABILITY_DEF (self), NULL);
    return PRIV (self)->effects;
}

gboolean
lrg_ability_def_can_use (LrgAbilityDef  *self,
                         gpointer        caster,
                         GError        **error)
{
    LrgAbilityDefClass *klass;

    g_return_val_if_fail (LRG_IS_ABILITY_DEF (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    klass = LRG_ABILITY_DEF_GET_CLASS (self);
    if (klass->can_use == NULL)
        return TRUE;
    return klass->can_use (self, caster, error);
}

gboolean
lrg_ability_def_is_eligible (LrgAbilityDef *self,
                             const gchar   *class_id,
                             guint          level,
                             const gchar   *spec_id)
{
    LrgAbilityDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_ABILITY_DEF (self), FALSE);

    priv = PRIV (self);
    if (priv->class_id != NULL && g_strcmp0 (priv->class_id, class_id) != 0)
        return FALSE;
    if (level < priv->min_level)
        return FALSE;
    if (priv->required_spec != NULL && g_strcmp0 (priv->required_spec, spec_id) != 0)
        return FALSE;
    return TRUE;
}
