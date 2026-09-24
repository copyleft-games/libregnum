/* lrg-ability-def.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAbilityEffect (boxed) and LrgAbilityDef (derivable definition) - data
 * describing a spell, skill or passive ability.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_ABILITY_MAX_COOLDOWN:
 *
 * Longest cooldown, category cooldown or cast time in seconds (one day).
 */
#define LRG_ABILITY_MAX_COOLDOWN (86400.0)

/**
 * LRG_ABILITY_MAX_RANGE:
 *
 * Largest accepted #LrgAbilityDef:range in world units.
 */
#define LRG_ABILITY_MAX_RANGE (1000000.0)

#define LRG_TYPE_ABILITY_EFFECT (lrg_ability_effect_get_type ())
#define LRG_TYPE_ABILITY_DEF (lrg_ability_def_get_type ())

/**
 * LrgAbilityEffect:
 * @kind: game-interpreted effect kind such as "damage", "heal", "hot", "dot",
 *   "shield", "buff", "debuff", "form", "summon", "taunt", "interrupt" or
 *   "resource"
 * @aura_id: (nullable): aura applied by the effect, or %NULL
 * @stat: (nullable): stat or effect key modified by the aura, or %NULL
 * @amount: flat amount
 * @scale: coefficient applied to a caster stat or power
 * @duration: seconds, 0 for instant effects
 * @period: seconds between periodic ticks, 0 for none
 * @radius: area radius, 0 for a single target
 * @max_targets: most targets hit (default 1)
 *
 * One effect of an ability. The engine stores and copies effects; the game
 * interprets them when an ability resolves.
 */
struct _LrgAbilityEffect
{
    gchar   *kind;
    gchar   *aura_id;
    gchar   *stat;
    gdouble  amount;
    gdouble  scale;
    gdouble  duration;
    gdouble  period;
    gdouble  radius;
    guint    max_targets;
};

LRG_AVAILABLE_IN_ALL
GType lrg_ability_effect_get_type (void) G_GNUC_CONST;

/**
 * lrg_ability_effect_new:
 * @kind: effect kind (non-empty)
 *
 * Creates an effect with all numbers zero and @max_targets 1.
 *
 * Returns: (transfer full): a new #LrgAbilityEffect
 */
LRG_AVAILABLE_IN_ALL
LrgAbilityEffect *lrg_ability_effect_new (const gchar *kind);

/**
 * lrg_ability_effect_copy:
 * @self: an #LrgAbilityEffect
 *
 * Returns: (transfer full): a deep copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgAbilityEffect *lrg_ability_effect_copy (const LrgAbilityEffect *self);

/**
 * lrg_ability_effect_free:
 * @self: (nullable): an #LrgAbilityEffect
 *
 * Frees an effect and its strings.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_effect_free (LrgAbilityEffect *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgAbilityEffect, lrg_ability_effect_free)

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgAbilityDef, lrg_ability_def, LRG, ABILITY_DEF, GObject)

/**
 * LrgAbilityDefClass:
 * @parent_class: parent class
 * @can_use: game-specific usability check (resources, target, form);
 *   the default returns %TRUE
 *
 * Class structure for #LrgAbilityDef.
 */
struct _LrgAbilityDefClass
{
    GObjectClass parent_class;

    gboolean (*can_use) (LrgAbilityDef  *self,
                         gpointer        caster,
                         GError        **error);

    gpointer _reserved[8];
};

/**
 * lrg_ability_def_new:
 * @id: unique ability identifier
 *
 * Returns: (transfer full): a new #LrgAbilityDef
 */
LRG_AVAILABLE_IN_ALL
LrgAbilityDef *lrg_ability_def_new (const gchar *id);

/**
 * lrg_ability_def_get_id:
 * @self: an #LrgAbilityDef
 *
 * Returns: (transfer none): the identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_ability_def_get_id (LrgAbilityDef *self);

/**
 * lrg_ability_def_get_name:
 * @self: an #LrgAbilityDef
 *
 * Returns: (transfer none) (nullable): display name
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_ability_def_get_name (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_name:
 * @self: an #LrgAbilityDef
 * @name: (nullable): display name
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_name (LrgAbilityDef *self,
                               const gchar   *name);

/**
 * lrg_ability_def_get_description:
 * @self: an #LrgAbilityDef
 *
 * Returns: (transfer none) (nullable): tooltip text
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_ability_def_get_description (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_description:
 * @self: an #LrgAbilityDef
 * @description: (nullable): tooltip text
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_description (LrgAbilityDef *self,
                                      const gchar   *description);

/**
 * lrg_ability_def_get_icon:
 * @self: an #LrgAbilityDef
 *
 * Returns: (transfer none) (nullable): icon key
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_ability_def_get_icon (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_icon:
 * @self: an #LrgAbilityDef
 * @icon: (nullable): icon key
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_icon (LrgAbilityDef *self,
                               const gchar   *icon);

/**
 * lrg_ability_def_get_class_id:
 * @self: an #LrgAbilityDef
 *
 * Returns: (transfer none) (nullable): owning class, %NULL for any class
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_ability_def_get_class_id (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_class_id:
 * @self: an #LrgAbilityDef
 * @class_id: (nullable): owning class, %NULL for any class
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_class_id (LrgAbilityDef *self,
                                   const gchar   *class_id);

/**
 * lrg_ability_def_get_min_level:
 * @self: an #LrgAbilityDef
 *
 * Returns: level required to learn and use the ability
 */
LRG_AVAILABLE_IN_ALL
guint lrg_ability_def_get_min_level (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_min_level:
 * @self: an #LrgAbilityDef
 * @min_level: required level
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_min_level (LrgAbilityDef *self,
                                    guint          min_level);

/**
 * lrg_ability_def_get_rank:
 * @self: an #LrgAbilityDef
 *
 * Returns: rank of this ability, starting at 1
 */
LRG_AVAILABLE_IN_ALL
guint lrg_ability_def_get_rank (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_rank:
 * @self: an #LrgAbilityDef
 * @rank: rank, at least 1
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_rank (LrgAbilityDef *self,
                               guint          rank);

/**
 * lrg_ability_def_get_resource:
 * @self: an #LrgAbilityDef
 *
 * Returns: (transfer none) (nullable): resource kind spent, e.g. "mana"
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_ability_def_get_resource (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_resource:
 * @self: an #LrgAbilityDef
 * @resource: (nullable): resource kind, e.g. "mana", "rage" or "energy"
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_resource (LrgAbilityDef *self,
                                   const gchar   *resource);

/**
 * lrg_ability_def_get_cost:
 * @self: an #LrgAbilityDef
 *
 * Returns: resource cost
 */
LRG_AVAILABLE_IN_ALL
guint lrg_ability_def_get_cost (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_cost:
 * @self: an #LrgAbilityDef
 * @cost: resource cost
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_cost (LrgAbilityDef *self,
                               guint          cost);

/**
 * lrg_ability_def_get_cooldown:
 * @self: an #LrgAbilityDef
 *
 * Returns: own cooldown in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_ability_def_get_cooldown (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_cooldown:
 * @self: an #LrgAbilityDef
 * @cooldown: finite seconds, 0 to %LRG_ABILITY_MAX_COOLDOWN
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_cooldown (LrgAbilityDef *self,
                                   gdouble        cooldown);

/**
 * lrg_ability_def_get_category:
 * @self: an #LrgAbilityDef
 *
 * Returns: (transfer none) (nullable): shared cooldown category
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_ability_def_get_category (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_category:
 * @self: an #LrgAbilityDef
 * @category: (nullable): shared cooldown category
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_category (LrgAbilityDef *self,
                                   const gchar   *category);

/**
 * lrg_ability_def_get_category_cooldown:
 * @self: an #LrgAbilityDef
 *
 * Returns: seconds the shared category locks after use
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_ability_def_get_category_cooldown (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_category_cooldown:
 * @self: an #LrgAbilityDef
 * @seconds: finite seconds, 0 to %LRG_ABILITY_MAX_COOLDOWN
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_category_cooldown (LrgAbilityDef *self,
                                            gdouble        seconds);

/**
 * lrg_ability_def_get_triggers_gcd:
 * @self: an #LrgAbilityDef
 *
 * Returns: whether using the ability starts and respects the global cooldown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ability_def_get_triggers_gcd (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_triggers_gcd:
 * @self: an #LrgAbilityDef
 * @triggers_gcd: whether the ability is on the global cooldown
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_triggers_gcd (LrgAbilityDef *self,
                                       gboolean       triggers_gcd);

/**
 * lrg_ability_def_get_cast_time:
 * @self: an #LrgAbilityDef
 *
 * Returns: cast or channel time in seconds, 0 for instant
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_ability_def_get_cast_time (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_cast_time:
 * @self: an #LrgAbilityDef
 * @cast_time: finite seconds, 0 to %LRG_ABILITY_MAX_COOLDOWN
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_cast_time (LrgAbilityDef *self,
                                    gdouble        cast_time);

/**
 * lrg_ability_def_get_channel:
 * @self: an #LrgAbilityDef
 *
 * Returns: whether the cast time is a channel
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ability_def_get_channel (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_channel:
 * @self: an #LrgAbilityDef
 * @channel: whether the cast time is a channel
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_channel (LrgAbilityDef *self,
                                  gboolean       channel);

/**
 * lrg_ability_def_get_range:
 * @self: an #LrgAbilityDef
 *
 * Returns: maximum range in world units, 0 for self/melee
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_ability_def_get_range (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_range:
 * @self: an #LrgAbilityDef
 * @range: finite units, 0 to %LRG_ABILITY_MAX_RANGE
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_range (LrgAbilityDef *self,
                                gdouble        range);

/**
 * lrg_ability_def_get_passive:
 * @self: an #LrgAbilityDef
 *
 * Returns: whether the ability is always on rather than cast
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ability_def_get_passive (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_passive:
 * @self: an #LrgAbilityDef
 * @passive: whether the ability is passive
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_passive (LrgAbilityDef *self,
                                  gboolean       passive);

/**
 * lrg_ability_def_get_learn_source:
 * @self: an #LrgAbilityDef
 *
 * Returns: how the ability is learned
 */
LRG_AVAILABLE_IN_ALL
LrgAbilityLearnSource lrg_ability_def_get_learn_source (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_learn_source:
 * @self: an #LrgAbilityDef
 * @source: how the ability is learned
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_learn_source (LrgAbilityDef         *self,
                                       LrgAbilityLearnSource  source);

/**
 * lrg_ability_def_get_trainer_cost:
 * @self: an #LrgAbilityDef
 *
 * Returns: price charged by a trainer
 */
LRG_AVAILABLE_IN_ALL
guint lrg_ability_def_get_trainer_cost (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_trainer_cost:
 * @self: an #LrgAbilityDef
 * @cost: price charged by a trainer
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_trainer_cost (LrgAbilityDef *self,
                                       guint          cost);

/**
 * lrg_ability_def_get_required_spec:
 * @self: an #LrgAbilityDef
 *
 * Returns: (transfer none) (nullable): talent tree id required as primary spec
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_ability_def_get_required_spec (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_required_spec:
 * @self: an #LrgAbilityDef
 * @spec_id: (nullable): talent tree id, %NULL for none
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_required_spec (LrgAbilityDef *self,
                                        const gchar   *spec_id);

/**
 * lrg_ability_def_get_required_talent:
 * @self: an #LrgAbilityDef
 *
 * Returns: (transfer none) (nullable): talent node id that grants the ability
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_ability_def_get_required_talent (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_required_talent:
 * @self: an #LrgAbilityDef
 * @talent_id: (nullable): talent node id, %NULL for none
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_required_talent (LrgAbilityDef *self,
                                          const gchar   *talent_id);

/**
 * lrg_ability_def_get_replaces:
 * @self: an #LrgAbilityDef
 *
 * Returns: (transfer none) (nullable): lower-rank ability upgraded by this one
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_ability_def_get_replaces (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_replaces:
 * @self: an #LrgAbilityDef
 * @ability_id: (nullable): lower-rank ability id, %NULL for none
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_replaces (LrgAbilityDef *self,
                                   const gchar   *ability_id);

/**
 * lrg_ability_def_get_target:
 * @self: an #LrgAbilityDef
 *
 * Returns: (transfer none) (nullable): targeting mode such as "enemy",
 *   "ally", "self" or "ground"
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_ability_def_get_target (LrgAbilityDef *self);

/**
 * lrg_ability_def_set_target:
 * @self: an #LrgAbilityDef
 * @target: (nullable): targeting mode
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_set_target (LrgAbilityDef *self,
                                 const gchar   *target);

/**
 * lrg_ability_def_add_tag:
 * @self: an #LrgAbilityDef
 * @tag: tag such as "fire" or "school:holy"
 *
 * Adds a tag; adding an existing tag does nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_add_tag (LrgAbilityDef *self,
                              const gchar   *tag);

/**
 * lrg_ability_def_has_tag:
 * @self: an #LrgAbilityDef
 * @tag: tag to look up
 *
 * Returns: %TRUE when @tag was added
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ability_def_has_tag (LrgAbilityDef *self,
                                  const gchar   *tag);

/**
 * lrg_ability_def_get_tags:
 * @self: an #LrgAbilityDef
 *
 * Returns: (transfer none) (element-type utf8): tags in insertion order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_ability_def_get_tags (LrgAbilityDef *self);

/**
 * lrg_ability_def_add_effect:
 * @self: an #LrgAbilityDef
 * @effect: (transfer full): effect to append
 *
 * Appends an effect; the definition takes ownership.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ability_def_add_effect (LrgAbilityDef    *self,
                                 LrgAbilityEffect *effect);

/**
 * lrg_ability_def_get_effects:
 * @self: an #LrgAbilityDef
 *
 * Returns: (transfer none) (element-type LrgAbilityEffect): effects in order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_ability_def_get_effects (LrgAbilityDef *self);

/**
 * lrg_ability_def_can_use:
 * @self: an #LrgAbilityDef
 * @caster: (nullable): game-defined caster context
 * @error: (nullable): return location for a #GError
 *
 * Calls the #LrgAbilityDefClass.can_use virtual method.
 *
 * Returns: %TRUE if the caster may use the ability now
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ability_def_can_use (LrgAbilityDef  *self,
                                  gpointer        caster,
                                  GError        **error);

/**
 * lrg_ability_def_is_eligible:
 * @self: an #LrgAbilityDef
 * @class_id: (nullable): character class
 * @level: character level
 * @spec_id: (nullable): character's primary talent tree
 *
 * Pure eligibility check (no virtual method): the definition's class is
 * %NULL or equals @class_id, @level is at least min-level, and
 * required-spec is %NULL or equals @spec_id.
 *
 * Returns: %TRUE when a character with these attributes may learn the ability
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ability_def_is_eligible (LrgAbilityDef *self,
                                      const gchar   *class_id,
                                      guint          level,
                                      const gchar   *spec_id);

G_END_DECLS
