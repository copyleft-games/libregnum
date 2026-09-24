/* lrg-gather-node-def.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGatherNodeDef - harvestable resource node definitions (ore veins,
 * herbs, skinnable beasts, fishing pools).
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
#include "lrg-recipe-def.h"

G_BEGIN_DECLS

#define LRG_TYPE_GATHER_NODE_DEF (lrg_gather_node_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgGatherNodeDef, lrg_gather_node_def, LRG, GATHER_NODE_DEF, GObject)

/**
 * LrgGatherNodeDefClass:
 * @parent_class: parent class
 *
 * Class structure for #LrgGatherNodeDef.
 */
struct _LrgGatherNodeDefClass
{
    GObjectClass parent_class;

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_gather_node_def_new:
 * @id: unique node identifier
 * @profession_id: gathering profession identifier
 *
 * Creates a gather node definition with required skill 0, respawn 0,
 * no tool, no yields and the default band.
 *
 * Returns: (transfer full): a new #LrgGatherNodeDef
 */
LRG_AVAILABLE_IN_ALL
LrgGatherNodeDef *lrg_gather_node_def_new (const gchar *id,
                                           const gchar *profession_id);

/**
 * lrg_gather_node_def_get_id:
 * @self: an #LrgGatherNodeDef
 *
 * Returns: (transfer none): the identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_gather_node_def_get_id (LrgGatherNodeDef *self);

/**
 * lrg_gather_node_def_get_name:
 * @self: an #LrgGatherNodeDef
 *
 * Returns: (transfer none) (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_gather_node_def_get_name (LrgGatherNodeDef *self);

/**
 * lrg_gather_node_def_set_name:
 * @self: an #LrgGatherNodeDef
 * @name: (nullable): display name
 *
 * Sets the display name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gather_node_def_set_name (LrgGatherNodeDef *self,
                                   const gchar      *name);

/**
 * lrg_gather_node_def_get_profession_id:
 * @self: an #LrgGatherNodeDef
 *
 * Returns: (transfer none) (nullable): the gathering profession identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_gather_node_def_get_profession_id (LrgGatherNodeDef *self);

/**
 * lrg_gather_node_def_set_profession_id:
 * @self: an #LrgGatherNodeDef
 * @profession_id: (nullable): gathering profession identifier
 *
 * Sets the gathering profession.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gather_node_def_set_profession_id (LrgGatherNodeDef *self,
                                            const gchar      *profession_id);

/**
 * lrg_gather_node_def_get_required_skill:
 * @self: an #LrgGatherNodeDef
 *
 * Returns: skill required to gather
 */
LRG_AVAILABLE_IN_ALL
guint lrg_gather_node_def_get_required_skill (LrgGatherNodeDef *self);

/**
 * lrg_gather_node_def_set_required_skill:
 * @self: an #LrgGatherNodeDef
 * @skill: required skill, 0..%LRG_PROFESSION_SKILL_LIMIT
 *
 * Sets the required skill. The default band follows this value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gather_node_def_set_required_skill (LrgGatherNodeDef *self,
                                             guint             skill);

/**
 * lrg_gather_node_def_get_respawn:
 * @self: an #LrgGatherNodeDef
 *
 * Returns: respawn delay in seconds after depletion
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_gather_node_def_get_respawn (LrgGatherNodeDef *self);

/**
 * lrg_gather_node_def_set_respawn:
 * @self: an #LrgGatherNodeDef
 * @seconds: respawn delay, 0..86400
 *
 * Sets the respawn delay. The server's world code owns node timers.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gather_node_def_set_respawn (LrgGatherNodeDef *self,
                                      gdouble           seconds);

/**
 * lrg_gather_node_def_get_required_tool:
 * @self: an #LrgGatherNodeDef
 *
 * Returns: (transfer none) (nullable): required tool item identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_gather_node_def_get_required_tool (LrgGatherNodeDef *self);

/**
 * lrg_gather_node_def_set_required_tool:
 * @self: an #LrgGatherNodeDef
 * @tool: (nullable): tool item identifier (e.g. a mining pick)
 *
 * Sets the tool needed to gather. Checked by
 * lrg_profession_state_can_gather_with() when a count function is given.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gather_node_def_set_required_tool (LrgGatherNodeDef *self,
                                            const gchar      *tool);

/**
 * lrg_gather_node_def_set_band:
 * @self: an #LrgGatherNodeDef
 * @band: (nullable): valid band to copy, or %NULL for the default
 *
 * Sets an explicit skill-up band (copied). %NULL restores the default band
 * derived from #LrgGatherNodeDef:required-skill. An invalid band is
 * rejected with a critical.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gather_node_def_set_band (LrgGatherNodeDef   *self,
                                   const LrgSkillBand *band);

/**
 * lrg_gather_node_def_get_band:
 * @self: an #LrgGatherNodeDef
 *
 * Returns: (transfer none): the effective band, owned by @self
 */
LRG_AVAILABLE_IN_ALL
const LrgSkillBand *lrg_gather_node_def_get_band (LrgGatherNodeDef *self);

/**
 * lrg_gather_node_def_add_yield:
 * @self: an #LrgGatherNodeDef
 * @item_id: item identifier
 * @count: quantity, at least 1
 * @chance: probability in (0, 1]
 *
 * Appends a possible yield. Yields keep insertion order.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gather_node_def_add_yield (LrgGatherNodeDef *self,
                                    const gchar      *item_id,
                                    guint             count,
                                    gdouble           chance);

/**
 * lrg_gather_node_def_get_yields:
 * @self: an #LrgGatherNodeDef
 *
 * Returns: (transfer none) (element-type LrgRecipeItem): yields in insertion order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_gather_node_def_get_yields (LrgGatherNodeDef *self);

G_END_DECLS
