/* lrg-character-def.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCharacterDef - Base class for playable character definitions.
 *
 * Characters define the starting conditions for a run:
 * - Starting deck composition
 * - Starting relic
 * - Base stats (HP, energy, draw)
 * - Unique abilities or mechanics
 *
 * This is a derivable type - subclass it to create custom characters
 * with specialized abilities.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_CHARACTER_DEF (lrg_character_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCharacterDef, lrg_character_def, LRG, CHARACTER_DEF, GObject)

/**
 * LrgCharacterDefClass:
 * @parent_class: The parent class
 * @get_starting_deck: Returns the starting deck card IDs
 * @get_starting_relic: Returns the starting relic ID
 * @on_run_start: Called when a run starts with this character
 * @on_run_end: Called when a run ends
 * @modify_starting_hp: Modify starting HP (for ascension effects)
 * @modify_starting_gold: Modify starting gold
 * @can_unlock: Check if this character can be unlocked
 *
 * The virtual function table for #LrgCharacterDef.
 *
 * Since: 1.0
 */
struct _LrgCharacterDefClass
{
    GObjectClass parent_class;

    /* Starting conditions */
    GPtrArray *  (* get_starting_deck)   (LrgCharacterDef *self);
    const gchar * (* get_starting_relic) (LrgCharacterDef *self);

    /* Lifecycle hooks */
    void         (* on_run_start)        (LrgCharacterDef *self,
                                          gpointer         run);
    void         (* on_run_end)          (LrgCharacterDef *self,
                                          gpointer         run,
                                          gboolean         victory);

    /* Modifiers (for ascension/challenge modes) */
    gint         (* modify_starting_hp)  (LrgCharacterDef *self,
                                          gint             base_hp);
    gint         (* modify_starting_gold)(LrgCharacterDef *self,
                                          gint             base_gold);

    /* Unlock condition */
    gboolean     (* can_unlock)          (LrgCharacterDef *self,
                                          LrgPlayerProfile *profile);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_character_def_new:
 * @id: unique identifier
 * @name: display name
 *
 * Creates a new character definition.
 *
 * Returns: (transfer full): a new #LrgCharacterDef
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCharacterDef * lrg_character_def_new (const gchar *id,
                                          const gchar *name);

/* ==========================================================================
 * Properties - Identification
 * ========================================================================== */

/**
 * lrg_character_def_get_id:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's unique identifier.
 *
 * Returns: (transfer none): the ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_character_def_get_id (LrgCharacterDef *self);

/**
 * lrg_character_def_get_name:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's display name.
 *
 * Returns: (transfer none): the name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_character_def_get_name (LrgCharacterDef *self);

/**
 * lrg_character_def_set_name:
 * @self: a #LrgCharacterDef
 * @name: the new name
 *
 * Sets the character's display name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_def_set_name (LrgCharacterDef *self,
                                  const gchar     *name);

/**
 * lrg_character_def_get_description:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's description.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_character_def_get_description (LrgCharacterDef *self);

/**
 * lrg_character_def_set_description:
 * @self: a #LrgCharacterDef
 * @description: (nullable): the description
 *
 * Sets the character's description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_def_set_description (LrgCharacterDef *self,
                                         const gchar     *description);

/**
 * lrg_character_def_get_icon:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's icon/portrait path.
 *
 * Returns: (transfer none) (nullable): the icon path
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_character_def_get_icon (LrgCharacterDef *self);

/**
 * lrg_character_def_set_icon:
 * @self: a #LrgCharacterDef
 * @icon: (nullable): the icon path
 *
 * Sets the character's icon/portrait path.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_def_set_icon (LrgCharacterDef *self,
                                  const gchar     *icon);

/* ==========================================================================
 * Properties - Stats
 * ========================================================================== */

/**
 * lrg_character_def_get_base_hp:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's base maximum HP.
 *
 * Returns: base HP
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_character_def_get_base_hp (LrgCharacterDef *self);

/**
 * lrg_character_def_set_base_hp:
 * @self: a #LrgCharacterDef
 * @base_hp: base HP
 *
 * Sets the character's base maximum HP.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_def_set_base_hp (LrgCharacterDef *self,
                                     gint             base_hp);

/**
 * lrg_character_def_get_base_energy:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's base energy per turn.
 *
 * Returns: base energy
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_character_def_get_base_energy (LrgCharacterDef *self);

/**
 * lrg_character_def_set_base_energy:
 * @self: a #LrgCharacterDef
 * @base_energy: base energy per turn
 *
 * Sets the character's base energy per turn.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_def_set_base_energy (LrgCharacterDef *self,
                                         gint             base_energy);

/**
 * lrg_character_def_get_base_draw:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's base cards drawn per turn.
 *
 * Returns: base draw
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_character_def_get_base_draw (LrgCharacterDef *self);

/**
 * lrg_character_def_set_base_draw:
 * @self: a #LrgCharacterDef
 * @base_draw: base cards drawn per turn
 *
 * Sets the character's base cards drawn per turn.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_def_set_base_draw (LrgCharacterDef *self,
                                       gint             base_draw);

/**
 * lrg_character_def_get_starting_gold:
 * @self: a #LrgCharacterDef
 *
 * Gets the character's starting gold.
 *
 * Returns: starting gold
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_character_def_get_starting_gold (LrgCharacterDef *self);

/**
 * lrg_character_def_set_starting_gold:
 * @self: a #LrgCharacterDef
 * @starting_gold: starting gold
 *
 * Sets the character's starting gold.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_def_set_starting_gold (LrgCharacterDef *self,
                                           gint             starting_gold);

/* ==========================================================================
 * Properties - Starting Deck
 * ========================================================================== */

/**
 * lrg_character_def_add_starting_card:
 * @self: a #LrgCharacterDef
 * @card_id: the card ID to add
 * @count: number of copies to add
 *
 * Adds cards to the starting deck.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_def_add_starting_card (LrgCharacterDef *self,
                                           const gchar     *card_id,
                                           gint             count);

/**
 * lrg_character_def_get_starting_deck:
 * @self: a #LrgCharacterDef
 *
 * Gets the starting deck card IDs.
 *
 * Returns: (transfer full) (element-type utf8): array of card IDs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_character_def_get_starting_deck (LrgCharacterDef *self);

/**
 * lrg_character_def_set_starting_relic:
 * @self: a #LrgCharacterDef
 * @relic_id: (nullable): the starting relic ID
 *
 * Sets the character's starting relic.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_def_set_starting_relic (LrgCharacterDef *self,
                                            const gchar     *relic_id);

/**
 * lrg_character_def_get_starting_relic:
 * @self: a #LrgCharacterDef
 *
 * Gets the starting relic ID.
 *
 * Returns: (transfer none) (nullable): the relic ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_character_def_get_starting_relic (LrgCharacterDef *self);

/* ==========================================================================
 * Properties - Unlock
 * ========================================================================== */

/**
 * lrg_character_def_get_unlocked_by_default:
 * @self: a #LrgCharacterDef
 *
 * Gets whether this character is unlocked by default.
 *
 * Returns: %TRUE if unlocked by default
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_character_def_get_unlocked_by_default (LrgCharacterDef *self);

/**
 * lrg_character_def_set_unlocked_by_default:
 * @self: a #LrgCharacterDef
 * @unlocked: whether unlocked by default
 *
 * Sets whether this character is unlocked by default.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_def_set_unlocked_by_default (LrgCharacterDef *self,
                                                 gboolean         unlocked);

/**
 * lrg_character_def_get_unlock_requirement:
 * @self: a #LrgCharacterDef
 *
 * Gets the unlock requirement description.
 *
 * Returns: (transfer none) (nullable): requirement text
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_character_def_get_unlock_requirement (LrgCharacterDef *self);

/**
 * lrg_character_def_set_unlock_requirement:
 * @self: a #LrgCharacterDef
 * @requirement: (nullable): requirement description
 *
 * Sets the unlock requirement description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_def_set_unlock_requirement (LrgCharacterDef *self,
                                                const gchar     *requirement);

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_character_def_on_run_start:
 * @self: a #LrgCharacterDef
 * @run: (nullable): the run context
 *
 * Called when a run starts with this character.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_def_on_run_start (LrgCharacterDef *self,
                                      gpointer         run);

/**
 * lrg_character_def_on_run_end:
 * @self: a #LrgCharacterDef
 * @run: (nullable): the run context
 * @victory: whether the run was won
 *
 * Called when a run ends.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_def_on_run_end (LrgCharacterDef *self,
                                    gpointer         run,
                                    gboolean         victory);

/**
 * lrg_character_def_modify_starting_hp:
 * @self: a #LrgCharacterDef
 * @base_hp: base HP value
 *
 * Modifies starting HP (for ascension effects).
 *
 * Returns: modified HP
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_character_def_modify_starting_hp (LrgCharacterDef *self,
                                            gint             base_hp);

/**
 * lrg_character_def_modify_starting_gold:
 * @self: a #LrgCharacterDef
 * @base_gold: base gold value
 *
 * Modifies starting gold.
 *
 * Returns: modified gold
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_character_def_modify_starting_gold (LrgCharacterDef *self,
                                              gint             base_gold);

/**
 * lrg_character_def_can_unlock:
 * @self: a #LrgCharacterDef
 * @profile: (nullable): player profile to check against
 *
 * Checks if this character can be unlocked.
 *
 * Returns: %TRUE if unlock conditions are met
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_character_def_can_unlock (LrgCharacterDef  *self,
                                        LrgPlayerProfile *profile);

G_END_DECLS
