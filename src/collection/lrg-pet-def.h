/* lrg-pet-def.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPetDef - a companion pet collectible (vanity or combat).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-collectible-def.h"

G_BEGIN_DECLS

#define LRG_TYPE_PET_DEF (lrg_pet_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgPetDef, lrg_pet_def, LRG, PET_DEF, LrgCollectibleDef)

/**
 * LrgPetDefClass:
 * @parent_class: parent class
 *
 * Class structure for #LrgPetDef.
 */
struct _LrgPetDefClass
{
    LrgCollectibleDefClass parent_class;

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * LRG_PET_DEF_MAX_ABILITIES:
 *
 * Maximum number of abilities a pet definition holds.
 */
#define LRG_PET_DEF_MAX_ABILITIES (64)

/**
 * lrg_pet_def_new:
 * @id: unique, non-empty identifier
 * @companion_kind: whether the pet is vanity-only or fights
 *
 * Creates a pet definition. Its collectible kind is always
 * %LRG_COLLECTIBLE_KIND_PET.
 *
 * Returns: (transfer full): a new #LrgPetDef
 */
LRG_AVAILABLE_IN_ALL
LrgPetDef *
lrg_pet_def_new (const gchar      *id,
                 LrgCompanionKind  companion_kind);

/**
 * lrg_pet_def_get_companion_kind:
 * @self: an #LrgPetDef
 *
 * Returns: whether the pet is vanity-only or fights
 */
LRG_AVAILABLE_IN_ALL
LrgCompanionKind
lrg_pet_def_get_companion_kind (LrgPetDef *self);

/**
 * lrg_pet_def_set_companion_kind:
 * @self: an #LrgPetDef
 * @kind: the companion kind
 *
 * Sets the companion kind.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pet_def_set_companion_kind (LrgPetDef        *self,
                                LrgCompanionKind  kind);

/**
 * lrg_pet_def_get_follow_distance:
 * @self: an #LrgPetDef
 *
 * Returns: distance from the owner the pet keeps while following
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_pet_def_get_follow_distance (LrgPetDef *self);

/**
 * lrg_pet_def_set_follow_distance:
 * @self: an #LrgPetDef
 * @distance: follow distance in [0, 1000]
 *
 * Sets the follow distance.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pet_def_set_follow_distance (LrgPetDef *self,
                                 gdouble    distance);

/**
 * lrg_pet_def_get_follow_angle:
 * @self: an #LrgPetDef
 *
 * Returns: follow angle in radians relative to the owner's facing
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_pet_def_get_follow_angle (LrgPetDef *self);

/**
 * lrg_pet_def_set_follow_angle:
 * @self: an #LrgPetDef
 * @angle: follow angle in radians, in [-2*pi, 2*pi]
 *
 * Sets the follow angle (see lrg_companion_brain_follow_point() for the
 * angle convention).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pet_def_set_follow_angle (LrgPetDef *self,
                              gdouble    angle);

/**
 * lrg_pet_def_get_health:
 * @self: an #LrgPetDef
 *
 * Returns: base health
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_pet_def_get_health (LrgPetDef *self);

/**
 * lrg_pet_def_set_health:
 * @self: an #LrgPetDef
 * @health: base health
 *
 * Sets base health.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pet_def_set_health (LrgPetDef *self,
                        guint      health);

/**
 * lrg_pet_def_get_damage:
 * @self: an #LrgPetDef
 *
 * Returns: base damage per attack
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_pet_def_get_damage (LrgPetDef *self);

/**
 * lrg_pet_def_set_damage:
 * @self: an #LrgPetDef
 * @damage: base damage per attack
 *
 * Sets base damage per attack.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pet_def_set_damage (LrgPetDef *self,
                        guint      damage);

/**
 * lrg_pet_def_get_attack_range:
 * @self: an #LrgPetDef
 *
 * Returns: melee / ranged attack reach
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_pet_def_get_attack_range (LrgPetDef *self);

/**
 * lrg_pet_def_set_attack_range:
 * @self: an #LrgPetDef
 * @range: attack reach in [0, 1000]
 *
 * Sets the attack reach.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pet_def_set_attack_range (LrgPetDef *self,
                              gdouble    range);

/**
 * lrg_pet_def_get_attack_interval:
 * @self: an #LrgPetDef
 *
 * Returns: seconds between attacks
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_pet_def_get_attack_interval (LrgPetDef *self);

/**
 * lrg_pet_def_set_attack_interval:
 * @self: an #LrgPetDef
 * @interval: seconds between attacks in [0.05, 3600]
 *
 * Sets the attack interval.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pet_def_set_attack_interval (LrgPetDef *self,
                                 gdouble    interval);

/**
 * lrg_pet_def_get_move_speed:
 * @self: an #LrgPetDef
 *
 * Returns: movement speed in world units per second
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_pet_def_get_move_speed (LrgPetDef *self);

/**
 * lrg_pet_def_set_move_speed:
 * @self: an #LrgPetDef
 * @speed: movement speed in [0, 1000]
 *
 * Sets the movement speed.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pet_def_set_move_speed (LrgPetDef *self,
                            gdouble    speed);

/**
 * lrg_pet_def_add_ability:
 * @self: an #LrgPetDef
 * @ability_id: non-empty ability identifier
 *
 * Appends an ability. Duplicates, empty ids and additions beyond
 * %LRG_PET_DEF_MAX_ABILITIES are ignored (the latter two with a critical
 * warning).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pet_def_add_ability (LrgPetDef   *self,
                         const gchar *ability_id);

/**
 * lrg_pet_def_has_ability:
 * @self: an #LrgPetDef
 * @ability_id: ability identifier
 *
 * Returns: %TRUE if @ability_id was added
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_pet_def_has_ability (LrgPetDef   *self,
                         const gchar *ability_id);

/**
 * lrg_pet_def_get_abilities:
 * @self: an #LrgPetDef
 *
 * Gets the abilities in insertion order.
 *
 * Returns: (transfer none) (element-type utf8): ability identifiers
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_pet_def_get_abilities (LrgPetDef *self);

G_END_DECLS
