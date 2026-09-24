/* lrg-spellbook.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSpellbook - Known abilities and an action bar.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-ability-def.h"

G_BEGIN_DECLS

/**
 * LRG_SPELLBOOK_MAX_KNOWN:
 *
 * Most abilities a spellbook can know.
 */
#define LRG_SPELLBOOK_MAX_KNOWN (512)

/**
 * LRG_SPELLBOOK_DEFAULT_BAR_SIZE:
 *
 * Action bar size of a new spellbook.
 */
#define LRG_SPELLBOOK_DEFAULT_BAR_SIZE (12)

/**
 * LRG_SPELLBOOK_MAX_BAR_SIZE:
 *
 * Largest action bar.
 */
#define LRG_SPELLBOOK_MAX_BAR_SIZE (48)

/**
 * LRG_SPELLBOOK_VARIANT_TYPE:
 *
 * GVariant type string of lrg_spellbook_to_variant(): known ability ids
 * (sorted), bar size, and one entry per bar slot ("" for an empty slot).
 */
#define LRG_SPELLBOOK_VARIANT_TYPE "(asuas)"

#define LRG_TYPE_SPELLBOOK (lrg_spellbook_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSpellbook, lrg_spellbook, LRG, SPELLBOOK, GObject)

/**
 * lrg_spellbook_new:
 *
 * Creates an empty spellbook with a %LRG_SPELLBOOK_DEFAULT_BAR_SIZE bar.
 *
 * Returns: (transfer full): a new #LrgSpellbook
 */
LRG_AVAILABLE_IN_ALL
LrgSpellbook *lrg_spellbook_new (void);

/**
 * lrg_spellbook_learn:
 * @self: an #LrgSpellbook
 * @ability_id: ability to learn
 * @error: (nullable): return location for a #GError
 *
 * Adds an ability. Fails with %LRG_PROGRESSION_ERROR_INVALID for an empty,
 * overlong (over 128 bytes) or non-UTF-8 id, %LRG_PROGRESSION_ERROR_DUPLICATE
 * when already known and %LRG_PROGRESSION_ERROR_LIMIT at
 * %LRG_SPELLBOOK_MAX_KNOWN.
 *
 * Returns: %TRUE if learned
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_spellbook_learn (LrgSpellbook  *self,
                              const gchar   *ability_id,
                              GError       **error);

/**
 * lrg_spellbook_forget:
 * @self: an #LrgSpellbook
 * @ability_id: ability to forget
 *
 * Removes an ability and clears every bar slot bound to it.
 *
 * Returns: %TRUE if the ability was known
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_spellbook_forget (LrgSpellbook *self,
                               const gchar  *ability_id);

/**
 * lrg_spellbook_knows:
 * @self: an #LrgSpellbook
 * @ability_id: (nullable): ability to look up
 *
 * Returns: %TRUE if the ability is known
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_spellbook_knows (LrgSpellbook *self,
                              const gchar  *ability_id);

/**
 * lrg_spellbook_get_known:
 * @self: an #LrgSpellbook
 *
 * Returns: (transfer container) (element-type utf8): known ids sorted with
 *   g_strcmp0(); the strings belong to the spellbook
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_spellbook_get_known (LrgSpellbook *self);

/**
 * lrg_spellbook_get_known_count:
 * @self: an #LrgSpellbook
 *
 * Returns: number of known abilities
 */
LRG_AVAILABLE_IN_ALL
guint lrg_spellbook_get_known_count (LrgSpellbook *self);

/**
 * lrg_spellbook_get_learnable:
 * @self: an #LrgSpellbook
 * @defs: (element-type LrgAbilityDef): candidate definitions
 * @class_id: (nullable): character class
 * @level: character level
 * @spec_id: (nullable): character's primary talent tree
 * @source: learn source to filter on
 *
 * Lists definitions that are eligible (lrg_ability_def_is_eligible()), not
 * yet known and learned from @source. Passive abilities are included.
 *
 * Returns: (transfer container) (element-type LrgAbilityDef): matching
 *   definitions sorted by (min-level, id); the definitions are not referenced
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_spellbook_get_learnable (LrgSpellbook          *self,
                                        GPtrArray             *defs,
                                        const gchar           *class_id,
                                        guint                  level,
                                        const gchar           *spec_id,
                                        LrgAbilityLearnSource  source);

/**
 * lrg_spellbook_learn_automatic:
 * @self: an #LrgSpellbook
 * @defs: (element-type LrgAbilityDef): candidate definitions
 * @class_id: (nullable): character class
 * @level: character level
 * @spec_id: (nullable): character's primary talent tree
 *
 * Learns every eligible, unknown %LRG_ABILITY_LEARN_SOURCE_AUTO definition in
 * (min-level, id) order. Lower ranks are kept; nothing is re-bound.
 *
 * Returns: number of abilities learned
 */
LRG_AVAILABLE_IN_ALL
guint lrg_spellbook_learn_automatic (LrgSpellbook *self,
                                     GPtrArray    *defs,
                                     const gchar  *class_id,
                                     guint         level,
                                     const gchar  *spec_id);

/**
 * lrg_spellbook_get_bar_size:
 * @self: an #LrgSpellbook
 *
 * Returns: number of action bar slots
 */
LRG_AVAILABLE_IN_ALL
guint lrg_spellbook_get_bar_size (LrgSpellbook *self);

/**
 * lrg_spellbook_set_bar_size:
 * @self: an #LrgSpellbook
 * @size: 1 to %LRG_SPELLBOOK_MAX_BAR_SIZE
 * @error: (nullable): return location for a #GError
 *
 * Resizes the bar. Growing adds empty slots; shrinking drops the trailing
 * slots and their bindings. %LRG_PROGRESSION_ERROR_INVALID when out of range.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_spellbook_set_bar_size (LrgSpellbook  *self,
                                     guint          size,
                                     GError       **error);

/**
 * lrg_spellbook_bind:
 * @self: an #LrgSpellbook
 * @slot: zero-based slot
 * @ability_id: (nullable): known ability, or %NULL to clear the slot
 * @error: (nullable): return location for a #GError
 *
 * Binds a slot. %LRG_PROGRESSION_ERROR_INVALID when @slot is past the bar
 * and %LRG_PROGRESSION_ERROR_NOT_FOUND when @ability_id is not known. The
 * same ability may be bound to several slots.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_spellbook_bind (LrgSpellbook  *self,
                             guint          slot,
                             const gchar   *ability_id,
                             GError       **error);

/**
 * lrg_spellbook_get_binding:
 * @self: an #LrgSpellbook
 * @slot: zero-based slot
 *
 * Returns: (transfer none) (nullable): bound ability, or %NULL for an empty
 *   or out-of-range slot
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_spellbook_get_binding (LrgSpellbook *self,
                                        guint         slot);

/**
 * lrg_spellbook_to_variant:
 * @self: an #LrgSpellbook
 *
 * Serialises the spellbook as %LRG_SPELLBOOK_VARIANT_TYPE.
 *
 * Returns: (transfer full): a non-floating #GVariant
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_spellbook_to_variant (LrgSpellbook *self);

/**
 * lrg_spellbook_new_from_variant:
 * @variant: a %LRG_SPELLBOOK_VARIANT_TYPE variant
 * @error: (nullable): return location for a #GError
 *
 * Restores a spellbook. The variant must be in normal form, know at most
 * %LRG_SPELLBOOK_MAX_KNOWN valid, unique ids, have a bar size in
 * 1..%LRG_SPELLBOOK_MAX_BAR_SIZE with exactly that many slots, and every
 * bound slot must name a known id. Anything else fails with
 * %LRG_PROGRESSION_ERROR_INVALID.
 *
 * Returns: (transfer full) (nullable): the spellbook, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgSpellbook *lrg_spellbook_new_from_variant (GVariant  *variant,
                                              GError   **error);

G_END_DECLS
