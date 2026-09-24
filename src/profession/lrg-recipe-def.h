/* lrg-recipe-def.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRecipeItem / LrgRecipeDef - crafting recipes with reagents,
 * products and a skill-up band.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-skill-band.h"

G_BEGIN_DECLS

#define LRG_TYPE_RECIPE_ITEM (lrg_recipe_item_get_type ())

/**
 * LrgRecipeItem:
 * @item_id: item definition identifier
 * @count: number of items (>= 1)
 * @chance: probability in (0, 1] that the entry is produced; always 1 for reagents
 *
 * One reagent, product or gather yield. Reagents are consumed in full;
 * products and yields roll @chance with a caller-supplied random value.
 */
struct _LrgRecipeItem
{
    gchar   *item_id;
    guint    count;
    gdouble  chance;
};

LRG_AVAILABLE_IN_ALL
GType lrg_recipe_item_get_type (void) G_GNUC_CONST;

/**
 * lrg_recipe_item_new:
 * @item_id: item identifier
 * @count: number of items, at least 1
 * @chance: probability in (0, 1]
 *
 * Creates a recipe item. Returns %NULL (with a critical) when @count is 0 or
 * @chance is not a finite value in (0, 1].
 *
 * Returns: (transfer full) (nullable): a new #LrgRecipeItem
 */
LRG_AVAILABLE_IN_ALL
LrgRecipeItem *lrg_recipe_item_new (const gchar *item_id,
                                    guint        count,
                                    gdouble      chance);

/**
 * lrg_recipe_item_copy:
 * @self: an #LrgRecipeItem
 *
 * Returns: (transfer full): a deep copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgRecipeItem *lrg_recipe_item_copy (const LrgRecipeItem *self);

/**
 * lrg_recipe_item_free:
 * @self: (nullable): an #LrgRecipeItem
 *
 * Frees a recipe item. %NULL is ignored.
 */
LRG_AVAILABLE_IN_ALL
void lrg_recipe_item_free (LrgRecipeItem *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgRecipeItem, lrg_recipe_item_free)

#define LRG_TYPE_RECIPE_DEF (lrg_recipe_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgRecipeDef, lrg_recipe_def, LRG, RECIPE_DEF, GObject)

/**
 * LrgRecipeDefClass:
 * @parent_class: parent class
 *
 * Class structure for #LrgRecipeDef.
 */
struct _LrgRecipeDefClass
{
    GObjectClass parent_class;

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_recipe_def_new:
 * @id: unique recipe identifier
 * @profession_id: owning profession identifier
 *
 * Creates a recipe with no reagents or products, required skill 0, source
 * %LRG_RECIPE_SOURCE_TRAINER and the default band.
 *
 * Returns: (transfer full): a new #LrgRecipeDef
 */
LRG_AVAILABLE_IN_ALL
LrgRecipeDef *lrg_recipe_def_new (const gchar *id,
                                  const gchar *profession_id);

/**
 * lrg_recipe_def_get_id:
 * @self: an #LrgRecipeDef
 *
 * Returns: (transfer none): the identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_recipe_def_get_id (LrgRecipeDef *self);

/**
 * lrg_recipe_def_get_name:
 * @self: an #LrgRecipeDef
 *
 * Returns: (transfer none) (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_recipe_def_get_name (LrgRecipeDef *self);

/**
 * lrg_recipe_def_set_name:
 * @self: an #LrgRecipeDef
 * @name: (nullable): display name
 *
 * Sets the display name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_recipe_def_set_name (LrgRecipeDef *self,
                              const gchar  *name);

/**
 * lrg_recipe_def_get_description:
 * @self: an #LrgRecipeDef
 *
 * Returns: (transfer none) (nullable): the description
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_recipe_def_get_description (LrgRecipeDef *self);

/**
 * lrg_recipe_def_set_description:
 * @self: an #LrgRecipeDef
 * @description: (nullable): description
 *
 * Sets the description.
 */
LRG_AVAILABLE_IN_ALL
void lrg_recipe_def_set_description (LrgRecipeDef *self,
                                     const gchar  *description);

/**
 * lrg_recipe_def_get_profession_id:
 * @self: an #LrgRecipeDef
 *
 * Returns: (transfer none) (nullable): the owning profession identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_recipe_def_get_profession_id (LrgRecipeDef *self);

/**
 * lrg_recipe_def_set_profession_id:
 * @self: an #LrgRecipeDef
 * @profession_id: (nullable): owning profession identifier
 *
 * Sets the owning profession.
 */
LRG_AVAILABLE_IN_ALL
void lrg_recipe_def_set_profession_id (LrgRecipeDef *self,
                                       const gchar  *profession_id);

/**
 * lrg_recipe_def_get_required_skill:
 * @self: an #LrgRecipeDef
 *
 * Returns: skill required to learn and craft the recipe
 */
LRG_AVAILABLE_IN_ALL
guint lrg_recipe_def_get_required_skill (LrgRecipeDef *self);

/**
 * lrg_recipe_def_set_required_skill:
 * @self: an #LrgRecipeDef
 * @skill: required skill, 0..%LRG_PROFESSION_SKILL_LIMIT
 *
 * Sets the required skill. When no explicit band is set the default band
 * follows this value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_recipe_def_set_required_skill (LrgRecipeDef *self,
                                        guint         skill);

/**
 * lrg_recipe_def_get_craft_time:
 * @self: an #LrgRecipeDef
 *
 * Returns: craft time in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_recipe_def_get_craft_time (LrgRecipeDef *self);

/**
 * lrg_recipe_def_set_craft_time:
 * @self: an #LrgRecipeDef
 * @seconds: craft time in seconds, >= 0
 *
 * Sets the craft (cast) time.
 */
LRG_AVAILABLE_IN_ALL
void lrg_recipe_def_set_craft_time (LrgRecipeDef *self,
                                    gdouble       seconds);

/**
 * lrg_recipe_def_get_source:
 * @self: an #LrgRecipeDef
 *
 * Returns: where the recipe is learned
 */
LRG_AVAILABLE_IN_ALL
LrgRecipeSource lrg_recipe_def_get_source (LrgRecipeDef *self);

/**
 * lrg_recipe_def_set_source:
 * @self: an #LrgRecipeDef
 * @source: where the recipe is learned
 *
 * Sets the recipe source.
 */
LRG_AVAILABLE_IN_ALL
void lrg_recipe_def_set_source (LrgRecipeDef    *self,
                                LrgRecipeSource  source);

/**
 * lrg_recipe_def_get_trainer_cost:
 * @self: an #LrgRecipeDef
 *
 * Returns: trainer cost (charged by the caller)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_recipe_def_get_trainer_cost (LrgRecipeDef *self);

/**
 * lrg_recipe_def_set_trainer_cost:
 * @self: an #LrgRecipeDef
 * @cost: trainer cost
 *
 * Sets the trainer cost.
 */
LRG_AVAILABLE_IN_ALL
void lrg_recipe_def_set_trainer_cost (LrgRecipeDef *self,
                                      guint         cost);

/**
 * lrg_recipe_def_get_station:
 * @self: an #LrgRecipeDef
 *
 * Returns: (transfer none) (nullable): required crafting station key
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_recipe_def_get_station (LrgRecipeDef *self);

/**
 * lrg_recipe_def_set_station:
 * @self: an #LrgRecipeDef
 * @station: (nullable): crafting station key (forge, anvil, ...)
 *
 * Sets the station the crafter must stand at. The engine only stores it;
 * the game checks proximity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_recipe_def_set_station (LrgRecipeDef *self,
                                 const gchar  *station);

/**
 * lrg_recipe_def_get_tool:
 * @self: an #LrgRecipeDef
 *
 * Returns: (transfer none) (nullable): required tool item identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_recipe_def_get_tool (LrgRecipeDef *self);

/**
 * lrg_recipe_def_set_tool:
 * @self: an #LrgRecipeDef
 * @tool: (nullable): tool item identifier (not consumed)
 *
 * Sets the tool item the crafter must carry. lrg_profession_state_can_craft()
 * requires at least one.
 */
LRG_AVAILABLE_IN_ALL
void lrg_recipe_def_set_tool (LrgRecipeDef *self,
                              const gchar  *tool);

/**
 * lrg_recipe_def_get_required_level:
 * @self: an #LrgRecipeDef
 *
 * Returns: minimum character level (enforced by the caller)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_recipe_def_get_required_level (LrgRecipeDef *self);

/**
 * lrg_recipe_def_set_required_level:
 * @self: an #LrgRecipeDef
 * @level: minimum character level
 *
 * Sets the minimum character level.
 */
LRG_AVAILABLE_IN_ALL
void lrg_recipe_def_set_required_level (LrgRecipeDef *self,
                                        guint         level);

/**
 * lrg_recipe_def_set_band:
 * @self: an #LrgRecipeDef
 * @band: (nullable): valid skill band to copy, or %NULL for the default
 *
 * Sets an explicit skill-up band (copied). %NULL restores the default band
 * derived from #LrgRecipeDef:required-skill (see
 * lrg_skill_band_new_default()). An invalid band is rejected with a
 * critical and leaves the recipe unchanged.
 */
LRG_AVAILABLE_IN_ALL
void lrg_recipe_def_set_band (LrgRecipeDef       *self,
                              const LrgSkillBand *band);

/**
 * lrg_recipe_def_get_band:
 * @self: an #LrgRecipeDef
 *
 * Gets the explicit band, or the default band whose orange threshold is
 * the required skill when none was set.
 *
 * Returns: (transfer none): the effective band, owned by @self
 */
LRG_AVAILABLE_IN_ALL
const LrgSkillBand *lrg_recipe_def_get_band (LrgRecipeDef *self);

/**
 * lrg_recipe_def_has_explicit_band:
 * @self: an #LrgRecipeDef
 *
 * Returns: %TRUE when lrg_recipe_def_set_band() installed a band
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_recipe_def_has_explicit_band (LrgRecipeDef *self);

/**
 * lrg_recipe_def_add_reagent:
 * @self: an #LrgRecipeDef
 * @item_id: reagent item identifier
 * @count: quantity consumed per craft, at least 1
 *
 * Adds a reagent (chance 1). Adding the same item again sums the counts
 * (saturating), so each item appears once.
 */
LRG_AVAILABLE_IN_ALL
void lrg_recipe_def_add_reagent (LrgRecipeDef *self,
                                 const gchar  *item_id,
                                 guint         count);

/**
 * lrg_recipe_def_add_product:
 * @self: an #LrgRecipeDef
 * @item_id: product item identifier
 * @count: quantity produced, at least 1
 * @chance: probability in (0, 1]
 *
 * Appends a product. Products are kept in insertion order and may repeat
 * (e.g. a guaranteed item plus a rare bonus of the same item).
 */
LRG_AVAILABLE_IN_ALL
void lrg_recipe_def_add_product (LrgRecipeDef *self,
                                 const gchar  *item_id,
                                 guint         count,
                                 gdouble       chance);

/**
 * lrg_recipe_def_get_reagents:
 * @self: an #LrgRecipeDef
 *
 * Returns: (transfer none) (element-type LrgRecipeItem): reagents in insertion order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_recipe_def_get_reagents (LrgRecipeDef *self);

/**
 * lrg_recipe_def_get_products:
 * @self: an #LrgRecipeDef
 *
 * Returns: (transfer none) (element-type LrgRecipeItem): products in insertion order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_recipe_def_get_products (LrgRecipeDef *self);

G_END_DECLS
