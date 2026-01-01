/* lrg-status-effect-def.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgStatusEffectDef - Status effect definition implementation.
 */

#include "lrg-status-effect-def.h"
#include "../lrg-log.h"

typedef struct
{
    gchar                  *id;
    gchar                  *name;
    gchar                  *description;
    gchar                  *icon;
    LrgStatusEffectType     effect_type;
    LrgStatusStackBehavior  stack_behavior;
    gint                    max_stacks;
    gboolean                permanent;
    gboolean                clears_at_turn_end;
    gboolean                decrements_at_turn_end;
} LrgStatusEffectDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgStatusEffectDef, lrg_status_effect_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_ICON,
    PROP_EFFECT_TYPE,
    PROP_STACK_BEHAVIOR,
    PROP_MAX_STACKS,
    PROP_PERMANENT,
    PROP_CLEARS_AT_TURN_END,
    PROP_DECREMENTS_AT_TURN_END,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static void
lrg_status_effect_def_real_on_apply (LrgStatusEffectDef *self,
                                      gpointer            owner,
                                      gint                stacks,
                                      gpointer            context)
{
    /* Default: no-op */
}

static void
lrg_status_effect_def_real_on_remove (LrgStatusEffectDef *self,
                                       gpointer            owner,
                                       gpointer            context)
{
    /* Default: no-op */
}

static void
lrg_status_effect_def_real_on_stack_change (LrgStatusEffectDef *self,
                                             gpointer            owner,
                                             gint                old_stacks,
                                             gint                new_stacks,
                                             gpointer            context)
{
    /* Default: no-op */
}

static void
lrg_status_effect_def_real_on_turn_start (LrgStatusEffectDef *self,
                                           gpointer            owner,
                                           gint                stacks,
                                           gpointer            context)
{
    /* Default: no-op */
}

static void
lrg_status_effect_def_real_on_turn_end (LrgStatusEffectDef *self,
                                         gpointer            owner,
                                         gint                stacks,
                                         gpointer            context)
{
    /* Default: no-op */
}

static void
lrg_status_effect_def_real_on_damage_dealt (LrgStatusEffectDef *self,
                                             gpointer            owner,
                                             gpointer            target,
                                             gint                damage,
                                             gint                stacks,
                                             gpointer            context)
{
    /* Default: no-op */
}

static void
lrg_status_effect_def_real_on_damage_received (LrgStatusEffectDef *self,
                                                gpointer            owner,
                                                gpointer            attacker,
                                                gint                damage,
                                                gint                stacks,
                                                gpointer            context)
{
    /* Default: no-op */
}

static void
lrg_status_effect_def_real_on_block_gained (LrgStatusEffectDef *self,
                                             gpointer            owner,
                                             gint                block,
                                             gint                stacks,
                                             gpointer            context)
{
    /* Default: no-op */
}

static gint
lrg_status_effect_def_real_modify_damage_dealt (LrgStatusEffectDef *self,
                                                 gpointer            owner,
                                                 gint                base_damage,
                                                 gint                stacks,
                                                 gpointer            context)
{
    /* Default: no modification */
    return base_damage;
}

static gint
lrg_status_effect_def_real_modify_damage_received (LrgStatusEffectDef *self,
                                                    gpointer            owner,
                                                    gint                base_damage,
                                                    gint                stacks,
                                                    gpointer            context)
{
    /* Default: no modification */
    return base_damage;
}

static gint
lrg_status_effect_def_real_modify_block_gained (LrgStatusEffectDef *self,
                                                 gpointer            owner,
                                                 gint                base_block,
                                                 gint                stacks,
                                                 gpointer            context)
{
    /* Default: no modification */
    return base_block;
}

static gboolean
lrg_status_effect_def_real_can_apply_debuff (LrgStatusEffectDef *self,
                                              gpointer            owner,
                                              LrgStatusEffectDef *debuff,
                                              gint                stacks,
                                              gpointer            context)
{
    /* Default: allow all debuffs */
    return TRUE;
}

static gchar *
lrg_status_effect_def_real_get_tooltip (LrgStatusEffectDef *self,
                                         gint                stacks)
{
    LrgStatusEffectDefPrivate *priv = lrg_status_effect_def_get_instance_private (self);

    /* Default: return description with stack count */
    if (priv->description != NULL)
        return g_strdup_printf ("%s (%d)", priv->description, stacks);
    else
        return g_strdup_printf ("%s: %d", priv->name, stacks);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_status_effect_def_finalize (GObject *object)
{
    LrgStatusEffectDef *self = LRG_STATUS_EFFECT_DEF (object);
    LrgStatusEffectDefPrivate *priv = lrg_status_effect_def_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->icon, g_free);

    G_OBJECT_CLASS (lrg_status_effect_def_parent_class)->finalize (object);
}

static void
lrg_status_effect_def_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
    LrgStatusEffectDef *self = LRG_STATUS_EFFECT_DEF (object);
    LrgStatusEffectDefPrivate *priv = lrg_status_effect_def_get_instance_private (self);

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
    case PROP_EFFECT_TYPE:
        g_value_set_int (value, priv->effect_type);
        break;
    case PROP_STACK_BEHAVIOR:
        g_value_set_int (value, priv->stack_behavior);
        break;
    case PROP_MAX_STACKS:
        g_value_set_int (value, priv->max_stacks);
        break;
    case PROP_PERMANENT:
        g_value_set_boolean (value, priv->permanent);
        break;
    case PROP_CLEARS_AT_TURN_END:
        g_value_set_boolean (value, priv->clears_at_turn_end);
        break;
    case PROP_DECREMENTS_AT_TURN_END:
        g_value_set_boolean (value, priv->decrements_at_turn_end);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_status_effect_def_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
    LrgStatusEffectDef *self = LRG_STATUS_EFFECT_DEF (object);
    LrgStatusEffectDefPrivate *priv = lrg_status_effect_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (priv->id);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        g_free (priv->name);
        priv->name = g_value_dup_string (value);
        break;
    case PROP_DESCRIPTION:
        g_free (priv->description);
        priv->description = g_value_dup_string (value);
        break;
    case PROP_ICON:
        g_free (priv->icon);
        priv->icon = g_value_dup_string (value);
        break;
    case PROP_EFFECT_TYPE:
        priv->effect_type = g_value_get_int (value);
        break;
    case PROP_STACK_BEHAVIOR:
        priv->stack_behavior = g_value_get_int (value);
        break;
    case PROP_MAX_STACKS:
        priv->max_stacks = g_value_get_int (value);
        break;
    case PROP_PERMANENT:
        priv->permanent = g_value_get_boolean (value);
        break;
    case PROP_CLEARS_AT_TURN_END:
        priv->clears_at_turn_end = g_value_get_boolean (value);
        break;
    case PROP_DECREMENTS_AT_TURN_END:
        priv->decrements_at_turn_end = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_status_effect_def_class_init (LrgStatusEffectDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_status_effect_def_finalize;
    object_class->get_property = lrg_status_effect_def_get_property;
    object_class->set_property = lrg_status_effect_def_set_property;

    /* Set default implementations for virtual methods */
    klass->on_apply = lrg_status_effect_def_real_on_apply;
    klass->on_remove = lrg_status_effect_def_real_on_remove;
    klass->on_stack_change = lrg_status_effect_def_real_on_stack_change;
    klass->on_turn_start = lrg_status_effect_def_real_on_turn_start;
    klass->on_turn_end = lrg_status_effect_def_real_on_turn_end;
    klass->on_damage_dealt = lrg_status_effect_def_real_on_damage_dealt;
    klass->on_damage_received = lrg_status_effect_def_real_on_damage_received;
    klass->on_block_gained = lrg_status_effect_def_real_on_block_gained;
    klass->modify_damage_dealt = lrg_status_effect_def_real_modify_damage_dealt;
    klass->modify_damage_received = lrg_status_effect_def_real_modify_damage_received;
    klass->modify_block_gained = lrg_status_effect_def_real_modify_block_gained;
    klass->can_apply_debuff = lrg_status_effect_def_real_can_apply_debuff;
    klass->get_tooltip = lrg_status_effect_def_real_get_tooltip;

    /**
     * LrgStatusEffectDef:id:
     *
     * The unique identifier for this status effect.
     *
     * Since: 1.0
     */
    properties[PROP_ID] =
        g_param_spec_string ("id", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgStatusEffectDef:name:
     *
     * The display name of this status effect.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

    /**
     * LrgStatusEffectDef:description:
     *
     * The description of this status effect.
     *
     * Since: 1.0
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgStatusEffectDef:icon:
     *
     * The icon identifier for this status effect.
     *
     * Since: 1.0
     */
    properties[PROP_ICON] =
        g_param_spec_string ("icon", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgStatusEffectDef:effect-type:
     *
     * The type of effect (buff, debuff, neutral).
     *
     * Since: 1.0
     */
    properties[PROP_EFFECT_TYPE] =
        g_param_spec_int ("effect-type", NULL, NULL,
                          LRG_STATUS_EFFECT_TYPE_BUFF,
                          LRG_STATUS_EFFECT_TYPE_NEUTRAL,
                          LRG_STATUS_EFFECT_TYPE_BUFF,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

    /**
     * LrgStatusEffectDef:stack-behavior:
     *
     * How stacks are interpreted.
     *
     * Since: 1.0
     */
    properties[PROP_STACK_BEHAVIOR] =
        g_param_spec_int ("stack-behavior", NULL, NULL,
                          LRG_STATUS_STACK_INTENSITY,
                          LRG_STATUS_STACK_COUNTER,
                          LRG_STATUS_STACK_INTENSITY,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgStatusEffectDef:max-stacks:
     *
     * Maximum stack count (0 = unlimited).
     *
     * Since: 1.0
     */
    properties[PROP_MAX_STACKS] =
        g_param_spec_int ("max-stacks", NULL, NULL,
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgStatusEffectDef:permanent:
     *
     * Whether the status survives combat end.
     *
     * Since: 1.0
     */
    properties[PROP_PERMANENT] =
        g_param_spec_boolean ("permanent", NULL, NULL,
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgStatusEffectDef:clears-at-turn-end:
     *
     * Whether the status clears at end of turn.
     *
     * Since: 1.0
     */
    properties[PROP_CLEARS_AT_TURN_END] =
        g_param_spec_boolean ("clears-at-turn-end", NULL, NULL,
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgStatusEffectDef:decrements-at-turn-end:
     *
     * Whether stacks decrement at end of turn.
     *
     * Since: 1.0
     */
    properties[PROP_DECREMENTS_AT_TURN_END] =
        g_param_spec_boolean ("decrements-at-turn-end", NULL, NULL,
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_status_effect_def_init (LrgStatusEffectDef *self)
{
    LrgStatusEffectDefPrivate *priv = lrg_status_effect_def_get_instance_private (self);

    priv->id = NULL;
    priv->name = NULL;
    priv->description = NULL;
    priv->icon = NULL;
    priv->effect_type = LRG_STATUS_EFFECT_TYPE_BUFF;
    priv->stack_behavior = LRG_STATUS_STACK_INTENSITY;
    priv->max_stacks = 0;
    priv->permanent = FALSE;
    priv->clears_at_turn_end = FALSE;
    priv->decrements_at_turn_end = FALSE;
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_status_effect_def_new:
 * @id: unique status identifier
 * @name: display name
 * @effect_type: buff, debuff, or neutral
 *
 * Creates a new status effect definition.
 *
 * Returns: (transfer full): a new #LrgStatusEffectDef
 *
 * Since: 1.0
 */
LrgStatusEffectDef *
lrg_status_effect_def_new (const gchar          *id,
                            const gchar          *name,
                            LrgStatusEffectType   effect_type)
{
    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_object_new (LRG_TYPE_STATUS_EFFECT_DEF,
                         "id", id,
                         "name", name,
                         "effect-type", effect_type,
                         NULL);
}

/* ==========================================================================
 * Property Accessors
 * ========================================================================== */

/**
 * lrg_status_effect_def_get_id:
 * @self: a #LrgStatusEffectDef
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): the status ID
 *
 * Since: 1.0
 */
const gchar *
lrg_status_effect_def_get_id (LrgStatusEffectDef *self)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), NULL);

    priv = lrg_status_effect_def_get_instance_private (self);
    return priv->id;
}

/**
 * lrg_status_effect_def_get_name:
 * @self: a #LrgStatusEffectDef
 *
 * Gets the display name.
 *
 * Returns: (transfer none): the status name
 *
 * Since: 1.0
 */
const gchar *
lrg_status_effect_def_get_name (LrgStatusEffectDef *self)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), NULL);

    priv = lrg_status_effect_def_get_instance_private (self);
    return priv->name;
}

/**
 * lrg_status_effect_def_get_description:
 * @self: a #LrgStatusEffectDef
 *
 * Gets the description.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
const gchar *
lrg_status_effect_def_get_description (LrgStatusEffectDef *self)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), NULL);

    priv = lrg_status_effect_def_get_instance_private (self);
    return priv->description;
}

/**
 * lrg_status_effect_def_set_description:
 * @self: a #LrgStatusEffectDef
 * @description: (nullable): the description
 *
 * Sets the description.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_set_description (LrgStatusEffectDef *self,
                                        const gchar        *description)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    priv = lrg_status_effect_def_get_instance_private (self);
    g_free (priv->description);
    priv->description = g_strdup (description);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESCRIPTION]);
}

/**
 * lrg_status_effect_def_get_icon:
 * @self: a #LrgStatusEffectDef
 *
 * Gets the icon identifier.
 *
 * Returns: (transfer none) (nullable): the icon ID
 *
 * Since: 1.0
 */
const gchar *
lrg_status_effect_def_get_icon (LrgStatusEffectDef *self)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), NULL);

    priv = lrg_status_effect_def_get_instance_private (self);
    return priv->icon;
}

/**
 * lrg_status_effect_def_set_icon:
 * @self: a #LrgStatusEffectDef
 * @icon: (nullable): the icon identifier
 *
 * Sets the icon identifier.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_set_icon (LrgStatusEffectDef *self,
                                 const gchar        *icon)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    priv = lrg_status_effect_def_get_instance_private (self);
    g_free (priv->icon);
    priv->icon = g_strdup (icon);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ICON]);
}

/**
 * lrg_status_effect_def_get_effect_type:
 * @self: a #LrgStatusEffectDef
 *
 * Gets the effect type (buff, debuff, neutral).
 *
 * Returns: the effect type
 *
 * Since: 1.0
 */
LrgStatusEffectType
lrg_status_effect_def_get_effect_type (LrgStatusEffectDef *self)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), LRG_STATUS_EFFECT_TYPE_BUFF);

    priv = lrg_status_effect_def_get_instance_private (self);
    return priv->effect_type;
}

/**
 * lrg_status_effect_def_get_stack_behavior:
 * @self: a #LrgStatusEffectDef
 *
 * Gets the stack behavior.
 *
 * Returns: the stack behavior
 *
 * Since: 1.0
 */
LrgStatusStackBehavior
lrg_status_effect_def_get_stack_behavior (LrgStatusEffectDef *self)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), LRG_STATUS_STACK_INTENSITY);

    priv = lrg_status_effect_def_get_instance_private (self);
    return priv->stack_behavior;
}

/**
 * lrg_status_effect_def_set_stack_behavior:
 * @self: a #LrgStatusEffectDef
 * @behavior: the stack behavior
 *
 * Sets the stack behavior.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_set_stack_behavior (LrgStatusEffectDef    *self,
                                           LrgStatusStackBehavior behavior)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    priv = lrg_status_effect_def_get_instance_private (self);
    priv->stack_behavior = behavior;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STACK_BEHAVIOR]);
}

/**
 * lrg_status_effect_def_get_max_stacks:
 * @self: a #LrgStatusEffectDef
 *
 * Gets the maximum stack count (0 = unlimited).
 *
 * Returns: the max stacks
 *
 * Since: 1.0
 */
gint
lrg_status_effect_def_get_max_stacks (LrgStatusEffectDef *self)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), 0);

    priv = lrg_status_effect_def_get_instance_private (self);
    return priv->max_stacks;
}

/**
 * lrg_status_effect_def_set_max_stacks:
 * @self: a #LrgStatusEffectDef
 * @max_stacks: maximum stacks (0 = unlimited)
 *
 * Sets the maximum stack count.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_set_max_stacks (LrgStatusEffectDef *self,
                                       gint                max_stacks)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    priv = lrg_status_effect_def_get_instance_private (self);
    priv->max_stacks = max_stacks;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_STACKS]);
}

/**
 * lrg_status_effect_def_is_permanent:
 * @self: a #LrgStatusEffectDef
 *
 * Checks if the status is permanent (survives combat end).
 *
 * Returns: %TRUE if permanent
 *
 * Since: 1.0
 */
gboolean
lrg_status_effect_def_is_permanent (LrgStatusEffectDef *self)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), FALSE);

    priv = lrg_status_effect_def_get_instance_private (self);
    return priv->permanent;
}

/**
 * lrg_status_effect_def_set_permanent:
 * @self: a #LrgStatusEffectDef
 * @permanent: whether the status is permanent
 *
 * Sets whether the status is permanent.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_set_permanent (LrgStatusEffectDef *self,
                                      gboolean            permanent)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    priv = lrg_status_effect_def_get_instance_private (self);
    priv->permanent = permanent;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PERMANENT]);
}

/**
 * lrg_status_effect_def_clears_at_turn_end:
 * @self: a #LrgStatusEffectDef
 *
 * Checks if the status clears at end of turn.
 *
 * Returns: %TRUE if clears at turn end
 *
 * Since: 1.0
 */
gboolean
lrg_status_effect_def_clears_at_turn_end (LrgStatusEffectDef *self)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), FALSE);

    priv = lrg_status_effect_def_get_instance_private (self);
    return priv->clears_at_turn_end;
}

/**
 * lrg_status_effect_def_set_clears_at_turn_end:
 * @self: a #LrgStatusEffectDef
 * @clears: whether to clear at turn end
 *
 * Sets whether the status clears at end of turn.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_set_clears_at_turn_end (LrgStatusEffectDef *self,
                                               gboolean            clears)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    priv = lrg_status_effect_def_get_instance_private (self);
    priv->clears_at_turn_end = clears;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CLEARS_AT_TURN_END]);
}

/**
 * lrg_status_effect_def_decrements_at_turn_end:
 * @self: a #LrgStatusEffectDef
 *
 * Checks if the status decrements stacks at end of turn.
 *
 * Returns: %TRUE if decrements at turn end
 *
 * Since: 1.0
 */
gboolean
lrg_status_effect_def_decrements_at_turn_end (LrgStatusEffectDef *self)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), FALSE);

    priv = lrg_status_effect_def_get_instance_private (self);
    return priv->decrements_at_turn_end;
}

/**
 * lrg_status_effect_def_set_decrements_at_turn_end:
 * @self: a #LrgStatusEffectDef
 * @decrements: whether to decrement at turn end
 *
 * Sets whether the status decrements stacks at end of turn.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_set_decrements_at_turn_end (LrgStatusEffectDef *self,
                                                   gboolean            decrements)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    priv = lrg_status_effect_def_get_instance_private (self);
    priv->decrements_at_turn_end = decrements;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DECREMENTS_AT_TURN_END]);
}

/**
 * lrg_status_effect_def_is_buff:
 * @self: a #LrgStatusEffectDef
 *
 * Checks if the status is a buff.
 *
 * Returns: %TRUE if buff
 *
 * Since: 1.0
 */
gboolean
lrg_status_effect_def_is_buff (LrgStatusEffectDef *self)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), FALSE);

    priv = lrg_status_effect_def_get_instance_private (self);
    return priv->effect_type == LRG_STATUS_EFFECT_TYPE_BUFF;
}

/**
 * lrg_status_effect_def_is_debuff:
 * @self: a #LrgStatusEffectDef
 *
 * Checks if the status is a debuff.
 *
 * Returns: %TRUE if debuff
 *
 * Since: 1.0
 */
gboolean
lrg_status_effect_def_is_debuff (LrgStatusEffectDef *self)
{
    LrgStatusEffectDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), FALSE);

    priv = lrg_status_effect_def_get_instance_private (self);
    return priv->effect_type == LRG_STATUS_EFFECT_TYPE_DEBUFF;
}

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_status_effect_def_on_apply:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant receiving the status
 * @stacks: initial stack count
 * @context: (nullable): combat context
 *
 * Called when the status is first applied.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_on_apply (LrgStatusEffectDef *self,
                                 gpointer            owner,
                                 gint                stacks,
                                 gpointer            context)
{
    LrgStatusEffectDefClass *klass;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    klass = LRG_STATUS_EFFECT_DEF_GET_CLASS (self);
    if (klass->on_apply != NULL)
        klass->on_apply (self, owner, stacks, context);
}

/**
 * lrg_status_effect_def_on_remove:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant losing the status
 * @context: (nullable): combat context
 *
 * Called when the status is removed.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_on_remove (LrgStatusEffectDef *self,
                                  gpointer            owner,
                                  gpointer            context)
{
    LrgStatusEffectDefClass *klass;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    klass = LRG_STATUS_EFFECT_DEF_GET_CLASS (self);
    if (klass->on_remove != NULL)
        klass->on_remove (self, owner, context);
}

/**
 * lrg_status_effect_def_on_stack_change:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @old_stacks: previous stack count
 * @new_stacks: new stack count
 * @context: (nullable): combat context
 *
 * Called when the stack count changes.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_on_stack_change (LrgStatusEffectDef *self,
                                        gpointer            owner,
                                        gint                old_stacks,
                                        gint                new_stacks,
                                        gpointer            context)
{
    LrgStatusEffectDefClass *klass;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    klass = LRG_STATUS_EFFECT_DEF_GET_CLASS (self);
    if (klass->on_stack_change != NULL)
        klass->on_stack_change (self, owner, old_stacks, new_stacks, context);
}

/**
 * lrg_status_effect_def_on_turn_start:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Called at the start of the owner's turn.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_on_turn_start (LrgStatusEffectDef *self,
                                      gpointer            owner,
                                      gint                stacks,
                                      gpointer            context)
{
    LrgStatusEffectDefClass *klass;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    klass = LRG_STATUS_EFFECT_DEF_GET_CLASS (self);
    if (klass->on_turn_start != NULL)
        klass->on_turn_start (self, owner, stacks, context);
}

/**
 * lrg_status_effect_def_on_turn_end:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Called at the end of the owner's turn.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_on_turn_end (LrgStatusEffectDef *self,
                                    gpointer            owner,
                                    gint                stacks,
                                    gpointer            context)
{
    LrgStatusEffectDefClass *klass;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    klass = LRG_STATUS_EFFECT_DEF_GET_CLASS (self);
    if (klass->on_turn_end != NULL)
        klass->on_turn_end (self, owner, stacks, context);
}

/**
 * lrg_status_effect_def_on_damage_dealt:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @target: the damage target
 * @damage: damage amount dealt
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Called after the owner deals damage.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_on_damage_dealt (LrgStatusEffectDef *self,
                                        gpointer            owner,
                                        gpointer            target,
                                        gint                damage,
                                        gint                stacks,
                                        gpointer            context)
{
    LrgStatusEffectDefClass *klass;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    klass = LRG_STATUS_EFFECT_DEF_GET_CLASS (self);
    if (klass->on_damage_dealt != NULL)
        klass->on_damage_dealt (self, owner, target, damage, stacks, context);
}

/**
 * lrg_status_effect_def_on_damage_received:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @attacker: (nullable): the damage source
 * @damage: damage amount received
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Called after the owner receives damage.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_on_damage_received (LrgStatusEffectDef *self,
                                           gpointer            owner,
                                           gpointer            attacker,
                                           gint                damage,
                                           gint                stacks,
                                           gpointer            context)
{
    LrgStatusEffectDefClass *klass;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    klass = LRG_STATUS_EFFECT_DEF_GET_CLASS (self);
    if (klass->on_damage_received != NULL)
        klass->on_damage_received (self, owner, attacker, damage, stacks, context);
}

/**
 * lrg_status_effect_def_on_block_gained:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @block: block amount gained
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Called after the owner gains block.
 *
 * Since: 1.0
 */
void
lrg_status_effect_def_on_block_gained (LrgStatusEffectDef *self,
                                        gpointer            owner,
                                        gint                block,
                                        gint                stacks,
                                        gpointer            context)
{
    LrgStatusEffectDefClass *klass;

    g_return_if_fail (LRG_IS_STATUS_EFFECT_DEF (self));

    klass = LRG_STATUS_EFFECT_DEF_GET_CLASS (self);
    if (klass->on_block_gained != NULL)
        klass->on_block_gained (self, owner, block, stacks, context);
}

/**
 * lrg_status_effect_def_modify_damage_dealt:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @base_damage: base damage amount
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Modifies outgoing damage.
 *
 * Returns: the modified damage amount
 *
 * Since: 1.0
 */
gint
lrg_status_effect_def_modify_damage_dealt (LrgStatusEffectDef *self,
                                            gpointer            owner,
                                            gint                base_damage,
                                            gint                stacks,
                                            gpointer            context)
{
    LrgStatusEffectDefClass *klass;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), base_damage);

    klass = LRG_STATUS_EFFECT_DEF_GET_CLASS (self);
    if (klass->modify_damage_dealt != NULL)
        return klass->modify_damage_dealt (self, owner, base_damage, stacks, context);

    return base_damage;
}

/**
 * lrg_status_effect_def_modify_damage_received:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @base_damage: base damage amount
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Modifies incoming damage.
 *
 * Returns: the modified damage amount
 *
 * Since: 1.0
 */
gint
lrg_status_effect_def_modify_damage_received (LrgStatusEffectDef *self,
                                               gpointer            owner,
                                               gint                base_damage,
                                               gint                stacks,
                                               gpointer            context)
{
    LrgStatusEffectDefClass *klass;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), base_damage);

    klass = LRG_STATUS_EFFECT_DEF_GET_CLASS (self);
    if (klass->modify_damage_received != NULL)
        return klass->modify_damage_received (self, owner, base_damage, stacks, context);

    return base_damage;
}

/**
 * lrg_status_effect_def_modify_block_gained:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @base_block: base block amount
 * @stacks: current stack count
 * @context: (nullable): combat context
 *
 * Modifies block gained.
 *
 * Returns: the modified block amount
 *
 * Since: 1.0
 */
gint
lrg_status_effect_def_modify_block_gained (LrgStatusEffectDef *self,
                                            gpointer            owner,
                                            gint                base_block,
                                            gint                stacks,
                                            gpointer            context)
{
    LrgStatusEffectDefClass *klass;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), base_block);

    klass = LRG_STATUS_EFFECT_DEF_GET_CLASS (self);
    if (klass->modify_block_gained != NULL)
        return klass->modify_block_gained (self, owner, base_block, stacks, context);

    return base_block;
}

/**
 * lrg_status_effect_def_can_apply_debuff:
 * @self: a #LrgStatusEffectDef
 * @owner: the combatant with the status
 * @debuff: the debuff being applied
 * @stacks: current stack count of this status
 * @context: (nullable): combat context
 *
 * Checks if a debuff can be applied. Used by Artifact.
 *
 * Returns: %TRUE if the debuff can be applied
 *
 * Since: 1.0
 */
gboolean
lrg_status_effect_def_can_apply_debuff (LrgStatusEffectDef *self,
                                         gpointer            owner,
                                         LrgStatusEffectDef *debuff,
                                         gint                stacks,
                                         gpointer            context)
{
    LrgStatusEffectDefClass *klass;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), TRUE);

    klass = LRG_STATUS_EFFECT_DEF_GET_CLASS (self);
    if (klass->can_apply_debuff != NULL)
        return klass->can_apply_debuff (self, owner, debuff, stacks, context);

    return TRUE;
}

/**
 * lrg_status_effect_def_get_tooltip:
 * @self: a #LrgStatusEffectDef
 * @stacks: current stack count
 *
 * Gets dynamic tooltip text based on current stacks.
 *
 * Returns: (transfer full): the tooltip text
 *
 * Since: 1.0
 */
gchar *
lrg_status_effect_def_get_tooltip (LrgStatusEffectDef *self,
                                    gint                stacks)
{
    LrgStatusEffectDefClass *klass;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (self), NULL);

    klass = LRG_STATUS_EFFECT_DEF_GET_CLASS (self);
    if (klass->get_tooltip != NULL)
        return klass->get_tooltip (self, stacks);

    return g_strdup ("");
}
