/* lrg-profession-def.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgProfessionTier / LrgProfessionDef - profession definitions with
 * trainer tiers (Apprentice, Journeyman, ...).
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

#define LRG_TYPE_PROFESSION_TIER (lrg_profession_tier_get_type ())

/**
 * LrgProfessionTier:
 * @name: (nullable): display name, e.g. "Apprentice"
 * @skill_cap: maximum skill once this tier is trained (1..%LRG_PROFESSION_SKILL_LIMIT)
 * @required_level: minimum character level to train this tier
 * @cost: trainer cost in the game's currency; charged by the caller
 *
 * One trainer rank of a profession. Tier 0 is granted by
 * lrg_profession_state_learn(); later tiers are bought with
 * lrg_profession_state_train().
 */
struct _LrgProfessionTier
{
    gchar *name;
    guint  skill_cap;
    guint  required_level;
    guint  cost;
};

LRG_AVAILABLE_IN_ALL
GType lrg_profession_tier_get_type (void) G_GNUC_CONST;

/**
 * lrg_profession_tier_new:
 * @name: (nullable): display name
 * @skill_cap: skill cap reached by training this tier
 * @required_level: minimum character level
 * @cost: trainer cost
 *
 * Creates a profession tier.
 *
 * Returns: (transfer full): a new #LrgProfessionTier
 */
LRG_AVAILABLE_IN_ALL
LrgProfessionTier *lrg_profession_tier_new (const gchar *name,
                                            guint        skill_cap,
                                            guint        required_level,
                                            guint        cost);

/**
 * lrg_profession_tier_copy:
 * @self: an #LrgProfessionTier
 *
 * Deep-copies a tier.
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgProfessionTier *lrg_profession_tier_copy (const LrgProfessionTier *self);

/**
 * lrg_profession_tier_free:
 * @self: (nullable): an #LrgProfessionTier
 *
 * Frees a tier. %NULL is ignored.
 */
LRG_AVAILABLE_IN_ALL
void lrg_profession_tier_free (LrgProfessionTier *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgProfessionTier, lrg_profession_tier_free)

#define LRG_TYPE_PROFESSION_DEF (lrg_profession_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgProfessionDef, lrg_profession_def, LRG, PROFESSION_DEF, GObject)

/**
 * LrgProfessionDefClass:
 * @parent_class: parent class
 *
 * Class structure for #LrgProfessionDef. Subclasses may add game data.
 */
struct _LrgProfessionDefClass
{
    GObjectClass parent_class;

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_profession_def_new:
 * @id: unique profession identifier
 * @kind: primary or secondary
 * @category: gathering, crafting or service
 *
 * Creates a profession definition with no tiers.
 *
 * Returns: (transfer full): a new #LrgProfessionDef
 */
LRG_AVAILABLE_IN_ALL
LrgProfessionDef *lrg_profession_def_new (const gchar           *id,
                                          LrgProfessionKind      kind,
                                          LrgProfessionCategory  category);

/**
 * lrg_profession_def_get_id:
 * @self: an #LrgProfessionDef
 *
 * Returns: (transfer none): the identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_profession_def_get_id (LrgProfessionDef *self);

/**
 * lrg_profession_def_get_name:
 * @self: an #LrgProfessionDef
 *
 * Returns: (transfer none) (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_profession_def_get_name (LrgProfessionDef *self);

/**
 * lrg_profession_def_set_name:
 * @self: an #LrgProfessionDef
 * @name: (nullable): display name
 *
 * Sets the display name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_profession_def_set_name (LrgProfessionDef *self,
                                  const gchar      *name);

/**
 * lrg_profession_def_get_description:
 * @self: an #LrgProfessionDef
 *
 * Returns: (transfer none) (nullable): the description
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_profession_def_get_description (LrgProfessionDef *self);

/**
 * lrg_profession_def_set_description:
 * @self: an #LrgProfessionDef
 * @description: (nullable): description
 *
 * Sets the description.
 */
LRG_AVAILABLE_IN_ALL
void lrg_profession_def_set_description (LrgProfessionDef *self,
                                         const gchar      *description);

/**
 * lrg_profession_def_get_icon:
 * @self: an #LrgProfessionDef
 *
 * Returns: (transfer none) (nullable): the icon key
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_profession_def_get_icon (LrgProfessionDef *self);

/**
 * lrg_profession_def_set_icon:
 * @self: an #LrgProfessionDef
 * @icon: (nullable): icon key
 *
 * Sets the icon key.
 */
LRG_AVAILABLE_IN_ALL
void lrg_profession_def_set_icon (LrgProfessionDef *self,
                                  const gchar      *icon);

/**
 * lrg_profession_def_get_kind:
 * @self: an #LrgProfessionDef
 *
 * Returns: whether the profession is primary or secondary
 */
LRG_AVAILABLE_IN_ALL
LrgProfessionKind lrg_profession_def_get_kind (LrgProfessionDef *self);

/**
 * lrg_profession_def_set_kind:
 * @self: an #LrgProfessionDef
 * @kind: primary or secondary
 *
 * Sets the profession kind.
 */
LRG_AVAILABLE_IN_ALL
void lrg_profession_def_set_kind (LrgProfessionDef  *self,
                                  LrgProfessionKind  kind);

/**
 * lrg_profession_def_get_category:
 * @self: an #LrgProfessionDef
 *
 * Returns: the profession category
 */
LRG_AVAILABLE_IN_ALL
LrgProfessionCategory lrg_profession_def_get_category (LrgProfessionDef *self);

/**
 * lrg_profession_def_set_category:
 * @self: an #LrgProfessionDef
 * @category: the profession category
 *
 * Sets the profession category.
 */
LRG_AVAILABLE_IN_ALL
void lrg_profession_def_set_category (LrgProfessionDef      *self,
                                      LrgProfessionCategory  category);

/**
 * lrg_profession_def_add_tier:
 * @self: an #LrgProfessionDef
 * @tier: (transfer full): tier to append
 * @error: (nullable): return location for a #GError
 *
 * Appends a trainer tier. Tier skill caps must be strictly increasing and
 * lie in 1..%LRG_PROFESSION_SKILL_LIMIT; at most 64 tiers are allowed.
 * Ownership of @tier is always taken: on failure it is freed.
 *
 * Errors: %LRG_PROGRESSION_ERROR_INVALID for a zero, oversized or
 * non-increasing cap; %LRG_PROGRESSION_ERROR_LIMIT beyond 64 tiers.
 *
 * Returns: %TRUE if the tier was appended
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_profession_def_add_tier (LrgProfessionDef   *self,
                                      LrgProfessionTier  *tier,
                                      GError            **error);

/**
 * lrg_profession_def_get_tier_count:
 * @self: an #LrgProfessionDef
 *
 * Returns: number of tiers
 */
LRG_AVAILABLE_IN_ALL
guint lrg_profession_def_get_tier_count (LrgProfessionDef *self);

/**
 * lrg_profession_def_get_tier:
 * @self: an #LrgProfessionDef
 * @index: tier index
 *
 * Returns: (transfer none) (nullable): the tier, or %NULL if out of range
 */
LRG_AVAILABLE_IN_ALL
const LrgProfessionTier *lrg_profession_def_get_tier (LrgProfessionDef *self,
                                                      guint             index);

/**
 * lrg_profession_def_get_max_skill:
 * @self: an #LrgProfessionDef
 *
 * Returns: the last tier's skill cap, or 0 without tiers
 */
LRG_AVAILABLE_IN_ALL
guint lrg_profession_def_get_max_skill (LrgProfessionDef *self);

/**
 * lrg_profession_def_get_tier_for_cap:
 * @self: an #LrgProfessionDef
 * @skill_cap: skill cap to look up
 *
 * Returns: index of the tier whose cap equals @skill_cap, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_profession_def_get_tier_for_cap (LrgProfessionDef *self,
                                          guint             skill_cap);

/**
 * lrg_profession_def_get_next_tier:
 * @self: an #LrgProfessionDef
 * @current_cap: the character's current skill cap
 *
 * Finds the first tier whose cap is strictly greater than @current_cap,
 * i.e. the tier a trainer would teach next.
 *
 * Returns: the tier index, or -1 when @current_cap is at or beyond the top
 */
LRG_AVAILABLE_IN_ALL
gint lrg_profession_def_get_next_tier (LrgProfessionDef *self,
                                       guint             current_cap);

G_END_DECLS
