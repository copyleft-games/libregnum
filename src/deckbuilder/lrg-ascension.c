/* lrg-ascension.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-ascension.h"
#include "../lrg-log.h"

/**
 * LrgAscension:
 *
 * Challenge mode configuration with difficulty modifiers.
 *
 * Since: 1.0
 */

struct _LrgAscension
{
    GObject parent_instance;

    gint                  level;
    gchar                *name;
    gchar                *description;
    LrgAscensionModifier  modifiers;

    /* Numeric modifiers */
    gint hp_reduction;
    gint gold_reduction;
    gint heal_reduction_percent;
    gint enemy_hp_increase_percent;
    gint enemy_damage_increase_percent;
};

G_DEFINE_TYPE (LrgAscension, lrg_ascension, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_LEVEL,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_MODIFIERS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_ascension_finalize (GObject *object)
{
    LrgAscension *self;

    self = LRG_ASCENSION (object);

    g_clear_pointer (&self->name, g_free);
    g_clear_pointer (&self->description, g_free);

    G_OBJECT_CLASS (lrg_ascension_parent_class)->finalize (object);
}

static void
lrg_ascension_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgAscension *self;

    self = LRG_ASCENSION (object);

    switch (prop_id)
    {
        case PROP_LEVEL:
            g_value_set_int (value, self->level);
            break;

        case PROP_NAME:
            g_value_set_string (value, self->name);
            break;

        case PROP_DESCRIPTION:
            g_value_set_string (value, self->description);
            break;

        case PROP_MODIFIERS:
            g_value_set_flags (value, self->modifiers);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_ascension_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgAscension *self;

    self = LRG_ASCENSION (object);

    switch (prop_id)
    {
        case PROP_LEVEL:
            self->level = CLAMP (g_value_get_int (value), 0, LRG_ASCENSION_MAX_LEVEL);
            /* Update name */
            g_clear_pointer (&self->name, g_free);
            if (self->level == 0)
                self->name = g_strdup ("Normal");
            else
                self->name = g_strdup_printf ("Ascension %d", self->level);
            break;

        case PROP_DESCRIPTION:
            g_clear_pointer (&self->description, g_free);
            self->description = g_value_dup_string (value);
            break;

        case PROP_MODIFIERS:
            self->modifiers = g_value_get_flags (value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_ascension_class_init (LrgAscensionClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_ascension_finalize;
    object_class->get_property = lrg_ascension_get_property;
    object_class->set_property = lrg_ascension_set_property;

    /**
     * LrgAscension:level:
     *
     * The ascension level (0 = normal, 1-20 = ascension).
     *
     * Since: 1.0
     */
    properties[PROP_LEVEL] =
        g_param_spec_int ("level",
                          "Level",
                          "Ascension level",
                          0, LRG_ASCENSION_MAX_LEVEL, 0,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

    /**
     * LrgAscension:name:
     *
     * Display name (e.g., "Ascension 5").
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name",
                             "Normal",
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgAscension:description:
     *
     * Description of this level's modifiers.
     *
     * Since: 1.0
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Modifier description",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgAscension:modifiers:
     *
     * Active modifier flags.
     *
     * Since: 1.0
     */
    properties[PROP_MODIFIERS] =
        g_param_spec_flags ("modifiers",
                            "Modifiers",
                            "Active modifier flags",
                            LRG_TYPE_ASCENSION_MODIFIER,
                            LRG_ASCENSION_MODIFIER_NONE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_ascension_init (LrgAscension *self)
{
    self->name = g_strdup ("Normal");
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_ascension_new:
 * @level: ascension level (0 = normal, 1-20 = ascension)
 *
 * Creates a new ascension configuration for a level.
 *
 * Returns: (transfer full): a new #LrgAscension
 *
 * Since: 1.0
 */
LrgAscension *
lrg_ascension_new (gint level)
{
    return g_object_new (LRG_TYPE_ASCENSION,
                         "level", level,
                         NULL);
}

/**
 * lrg_ascension_new_default:
 * @level: ascension level
 *
 * Creates a new ascension with default modifiers for that level.
 * Uses standard StS-style ascension progression.
 *
 * Returns: (transfer full): a new #LrgAscension with defaults
 *
 * Since: 1.0
 */
LrgAscension *
lrg_ascension_new_default (gint level)
{
    LrgAscension *self;
    GString      *desc;

    self = lrg_ascension_new (level);
    desc = g_string_new (NULL);

    /* StS-style cumulative modifiers */
    if (level >= 1)
    {
        /* A1: Elite drops worse rewards (handled elsewhere) */
        g_string_append (desc, "Elites drop worse rewards. ");
    }

    if (level >= 2)
    {
        /* A2: -1 max HP */
        self->hp_reduction = 1;
        g_string_append (desc, "-1 Max HP. ");
    }

    if (level >= 4)
    {
        /* A4: -2 more max HP (total -3) */
        self->hp_reduction = 3;
        /* Update description */
        g_string_truncate (desc, 0);
        g_string_append (desc, "Elites drop worse rewards. -3 Max HP. ");
    }

    if (level >= 5)
    {
        /* A5: Heal 25% less at rest sites */
        self->heal_reduction_percent = 25;
        self->modifiers |= LRG_ASCENSION_MODIFIER_LESS_HEALING;
        g_string_append (desc, "Heal 25% less at rest. ");
    }

    if (level >= 6)
    {
        /* A6: -5 starting gold */
        self->gold_reduction = 5;
        self->modifiers |= LRG_ASCENSION_MODIFIER_LESS_GOLD;
        g_string_append (desc, "-5 starting gold. ");
    }

    if (level >= 7)
    {
        /* A7: Boss drops 1 fewer relic (handled elsewhere) */
        g_string_append (desc, "Boss drops fewer relics. ");
    }

    if (level >= 8)
    {
        /* A8: -10 more gold (total -15) */
        self->gold_reduction = 15;
        /* Update description */
    }

    if (level >= 10)
    {
        /* A10: Enemies have more HP */
        self->enemy_hp_increase_percent = 10;
        self->modifiers |= LRG_ASCENSION_MODIFIER_ENEMY_HP;
        g_string_append (desc, "+10% enemy HP. ");
    }

    if (level >= 11)
    {
        /* A11: Start with 1 curse */
        self->modifiers |= LRG_ASCENSION_MODIFIER_CURSES;
        g_string_append (desc, "Start with a curse. ");
    }

    if (level >= 15)
    {
        /* A15: Elites are harder */
        self->modifiers |= LRG_ASCENSION_MODIFIER_HARDER_ELITES;
        g_string_append (desc, "Elites are stronger. ");
    }

    if (level >= 17)
    {
        /* A17: Enemies deal more damage */
        self->enemy_damage_increase_percent = 10;
        self->modifiers |= LRG_ASCENSION_MODIFIER_ENEMY_DAMAGE;
        g_string_append (desc, "+10% enemy damage. ");
    }

    if (level >= 19)
    {
        /* A19: Bosses are harder */
        self->modifiers |= LRG_ASCENSION_MODIFIER_HARDER_BOSSES;
        g_string_append (desc, "Bosses are stronger. ");
    }

    if (level >= 20)
    {
        /* A20: Heart fight starts with debuffs */
        g_string_append (desc, "Final boss starts at full power. ");
    }

    if (desc->len > 0)
        self->description = g_string_free (desc, FALSE);
    else
        g_string_free (desc, TRUE);

    return self;
}

/**
 * lrg_ascension_get_level:
 * @self: a #LrgAscension
 *
 * Gets the ascension level.
 *
 * Returns: the level (0-20)
 *
 * Since: 1.0
 */
gint
lrg_ascension_get_level (LrgAscension *self)
{
    g_return_val_if_fail (LRG_IS_ASCENSION (self), 0);

    return self->level;
}

/**
 * lrg_ascension_get_name:
 * @self: a #LrgAscension
 *
 * Gets the display name (e.g., "Ascension 5").
 *
 * Returns: (transfer none): the name
 *
 * Since: 1.0
 */
const gchar *
lrg_ascension_get_name (LrgAscension *self)
{
    g_return_val_if_fail (LRG_IS_ASCENSION (self), NULL);

    return self->name;
}

/**
 * lrg_ascension_get_description:
 * @self: a #LrgAscension
 *
 * Gets the description of this level's modifiers.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
const gchar *
lrg_ascension_get_description (LrgAscension *self)
{
    g_return_val_if_fail (LRG_IS_ASCENSION (self), NULL);

    return self->description;
}

/**
 * lrg_ascension_set_description:
 * @self: a #LrgAscension
 * @description: (nullable): the description
 *
 * Sets the description.
 *
 * Since: 1.0
 */
void
lrg_ascension_set_description (LrgAscension *self,
                                const gchar  *description)
{
    g_return_if_fail (LRG_IS_ASCENSION (self));

    g_object_set (self, "description", description, NULL);
}

/**
 * lrg_ascension_get_modifiers:
 * @self: a #LrgAscension
 *
 * Gets the active modifier flags.
 *
 * Returns: the #LrgAscensionModifier flags
 *
 * Since: 1.0
 */
LrgAscensionModifier
lrg_ascension_get_modifiers (LrgAscension *self)
{
    g_return_val_if_fail (LRG_IS_ASCENSION (self), LRG_ASCENSION_MODIFIER_NONE);

    return self->modifiers;
}

/**
 * lrg_ascension_has_modifier:
 * @self: a #LrgAscension
 * @modifier: modifier to check
 *
 * Checks if a specific modifier is active.
 *
 * Returns: %TRUE if modifier is active
 *
 * Since: 1.0
 */
gboolean
lrg_ascension_has_modifier (LrgAscension         *self,
                             LrgAscensionModifier  modifier)
{
    g_return_val_if_fail (LRG_IS_ASCENSION (self), FALSE);

    return (self->modifiers & modifier) != 0;
}

/**
 * lrg_ascension_add_modifier:
 * @self: a #LrgAscension
 * @modifier: modifier to add
 *
 * Adds a modifier.
 *
 * Since: 1.0
 */
void
lrg_ascension_add_modifier (LrgAscension         *self,
                             LrgAscensionModifier  modifier)
{
    g_return_if_fail (LRG_IS_ASCENSION (self));

    self->modifiers |= modifier;
}

/**
 * lrg_ascension_get_hp_reduction:
 * @self: a #LrgAscension
 *
 * Gets the starting HP reduction.
 *
 * Returns: HP to subtract from starting max HP
 *
 * Since: 1.0
 */
gint
lrg_ascension_get_hp_reduction (LrgAscension *self)
{
    g_return_val_if_fail (LRG_IS_ASCENSION (self), 0);

    return self->hp_reduction;
}

/**
 * lrg_ascension_set_hp_reduction:
 * @self: a #LrgAscension
 * @reduction: HP reduction amount
 *
 * Sets the starting HP reduction.
 *
 * Since: 1.0
 */
void
lrg_ascension_set_hp_reduction (LrgAscension *self,
                                 gint          reduction)
{
    g_return_if_fail (LRG_IS_ASCENSION (self));
    g_return_if_fail (reduction >= 0);

    self->hp_reduction = reduction;
}

/**
 * lrg_ascension_get_gold_reduction:
 * @self: a #LrgAscension
 *
 * Gets the starting gold reduction.
 *
 * Returns: gold to subtract from starting gold
 *
 * Since: 1.0
 */
gint
lrg_ascension_get_gold_reduction (LrgAscension *self)
{
    g_return_val_if_fail (LRG_IS_ASCENSION (self), 0);

    return self->gold_reduction;
}

/**
 * lrg_ascension_set_gold_reduction:
 * @self: a #LrgAscension
 * @reduction: gold reduction amount
 *
 * Sets the starting gold reduction.
 *
 * Since: 1.0
 */
void
lrg_ascension_set_gold_reduction (LrgAscension *self,
                                   gint          reduction)
{
    g_return_if_fail (LRG_IS_ASCENSION (self));
    g_return_if_fail (reduction >= 0);

    self->gold_reduction = reduction;
}

/**
 * lrg_ascension_get_heal_reduction_percent:
 * @self: a #LrgAscension
 *
 * Gets the healing reduction percentage.
 *
 * Returns: percentage of healing lost (0-100)
 *
 * Since: 1.0
 */
gint
lrg_ascension_get_heal_reduction_percent (LrgAscension *self)
{
    g_return_val_if_fail (LRG_IS_ASCENSION (self), 0);

    return self->heal_reduction_percent;
}

/**
 * lrg_ascension_set_heal_reduction_percent:
 * @self: a #LrgAscension
 * @percent: healing reduction percentage
 *
 * Sets the healing reduction percentage.
 *
 * Since: 1.0
 */
void
lrg_ascension_set_heal_reduction_percent (LrgAscension *self,
                                           gint          percent)
{
    g_return_if_fail (LRG_IS_ASCENSION (self));
    g_return_if_fail (percent >= 0 && percent <= 100);

    self->heal_reduction_percent = percent;
}

/**
 * lrg_ascension_get_enemy_hp_increase_percent:
 * @self: a #LrgAscension
 *
 * Gets the enemy HP increase percentage.
 *
 * Returns: percentage increase in enemy HP
 *
 * Since: 1.0
 */
gint
lrg_ascension_get_enemy_hp_increase_percent (LrgAscension *self)
{
    g_return_val_if_fail (LRG_IS_ASCENSION (self), 0);

    return self->enemy_hp_increase_percent;
}

/**
 * lrg_ascension_set_enemy_hp_increase_percent:
 * @self: a #LrgAscension
 * @percent: enemy HP increase percentage
 *
 * Sets the enemy HP increase percentage.
 *
 * Since: 1.0
 */
void
lrg_ascension_set_enemy_hp_increase_percent (LrgAscension *self,
                                              gint          percent)
{
    g_return_if_fail (LRG_IS_ASCENSION (self));
    g_return_if_fail (percent >= 0);

    self->enemy_hp_increase_percent = percent;
}

/**
 * lrg_ascension_get_enemy_damage_increase_percent:
 * @self: a #LrgAscension
 *
 * Gets the enemy damage increase percentage.
 *
 * Returns: percentage increase in enemy damage
 *
 * Since: 1.0
 */
gint
lrg_ascension_get_enemy_damage_increase_percent (LrgAscension *self)
{
    g_return_val_if_fail (LRG_IS_ASCENSION (self), 0);

    return self->enemy_damage_increase_percent;
}

/**
 * lrg_ascension_set_enemy_damage_increase_percent:
 * @self: a #LrgAscension
 * @percent: enemy damage increase percentage
 *
 * Sets the enemy damage increase percentage.
 *
 * Since: 1.0
 */
void
lrg_ascension_set_enemy_damage_increase_percent (LrgAscension *self,
                                                  gint          percent)
{
    g_return_if_fail (LRG_IS_ASCENSION (self));
    g_return_if_fail (percent >= 0);

    self->enemy_damage_increase_percent = percent;
}

/**
 * lrg_ascension_apply_hp:
 * @self: a #LrgAscension
 * @base_hp: base HP before modifiers
 *
 * Applies HP modifiers.
 *
 * Returns: modified HP value
 *
 * Since: 1.0
 */
gint
lrg_ascension_apply_hp (LrgAscension *self,
                         gint          base_hp)
{
    gint result;

    g_return_val_if_fail (LRG_IS_ASCENSION (self), base_hp);

    result = base_hp - self->hp_reduction;
    if (result < 1)
        result = 1;

    return result;
}

/**
 * lrg_ascension_apply_gold:
 * @self: a #LrgAscension
 * @base_gold: base gold before modifiers
 *
 * Applies gold modifiers.
 *
 * Returns: modified gold value
 *
 * Since: 1.0
 */
gint
lrg_ascension_apply_gold (LrgAscension *self,
                           gint          base_gold)
{
    gint result;

    g_return_val_if_fail (LRG_IS_ASCENSION (self), base_gold);

    result = base_gold - self->gold_reduction;
    if (result < 0)
        result = 0;

    return result;
}

/**
 * lrg_ascension_apply_heal:
 * @self: a #LrgAscension
 * @base_heal: base heal amount before modifiers
 *
 * Applies healing modifiers.
 *
 * Returns: modified heal amount
 *
 * Since: 1.0
 */
gint
lrg_ascension_apply_heal (LrgAscension *self,
                           gint          base_heal)
{
    gint result;

    g_return_val_if_fail (LRG_IS_ASCENSION (self), base_heal);

    if (self->heal_reduction_percent == 0)
        return base_heal;

    result = base_heal * (100 - self->heal_reduction_percent) / 100;
    if (result < 0)
        result = 0;

    return result;
}

/**
 * lrg_ascension_apply_enemy_hp:
 * @self: a #LrgAscension
 * @base_hp: base enemy HP
 *
 * Applies enemy HP modifiers.
 *
 * Returns: modified enemy HP
 *
 * Since: 1.0
 */
gint
lrg_ascension_apply_enemy_hp (LrgAscension *self,
                               gint          base_hp)
{
    gint result;

    g_return_val_if_fail (LRG_IS_ASCENSION (self), base_hp);

    if (self->enemy_hp_increase_percent == 0)
        return base_hp;

    result = base_hp * (100 + self->enemy_hp_increase_percent) / 100;
    return result;
}

/**
 * lrg_ascension_apply_enemy_damage:
 * @self: a #LrgAscension
 * @base_damage: base enemy damage
 *
 * Applies enemy damage modifiers.
 *
 * Returns: modified enemy damage
 *
 * Since: 1.0
 */
gint
lrg_ascension_apply_enemy_damage (LrgAscension *self,
                                   gint          base_damage)
{
    gint result;

    g_return_val_if_fail (LRG_IS_ASCENSION (self), base_damage);

    if (self->enemy_damage_increase_percent == 0)
        return base_damage;

    result = base_damage * (100 + self->enemy_damage_increase_percent) / 100;
    return result;
}
