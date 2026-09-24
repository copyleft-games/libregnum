/* lrg-mount-def.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMountDef - a rideable collectible.
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

#define LRG_TYPE_MOUNT_DEF (lrg_mount_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgMountDef, lrg_mount_def, LRG, MOUNT_DEF, LrgCollectibleDef)

/**
 * LrgMountDefClass:
 * @parent_class: parent class
 *
 * Class structure for #LrgMountDef.
 */
struct _LrgMountDefClass
{
    LrgCollectibleDefClass parent_class;

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * LRG_MOUNT_DEF_MAX_SPEED_MULTIPLIER:
 *
 * Upper bound of #LrgMountDef:speed-multiplier.
 */
#define LRG_MOUNT_DEF_MAX_SPEED_MULTIPLIER (100.0)

/**
 * LRG_MOUNT_DEF_MAX_PASSENGERS:
 *
 * Upper bound of #LrgMountDef:passengers.
 */
#define LRG_MOUNT_DEF_MAX_PASSENGERS (64)

/**
 * lrg_mount_def_new:
 * @id: unique, non-empty identifier
 *
 * Creates a mount definition. Its kind is always
 * %LRG_COLLECTIBLE_KIND_MOUNT.
 *
 * Returns: (transfer full): a new #LrgMountDef
 */
LRG_AVAILABLE_IN_ALL
LrgMountDef *
lrg_mount_def_new (const gchar *id);

/**
 * lrg_mount_def_get_speed_multiplier:
 * @self: an #LrgMountDef
 *
 * Returns: the mount's own speed multiplier (>= 1.0)
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_mount_def_get_speed_multiplier (LrgMountDef *self);

/**
 * lrg_mount_def_set_speed_multiplier:
 * @self: an #LrgMountDef
 * @multiplier: speed multiplier in [1.0, %LRG_MOUNT_DEF_MAX_SPEED_MULTIPLIER]
 *
 * Sets the speed multiplier. Non-finite or out-of-range values are
 * rejected with a critical warning.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mount_def_set_speed_multiplier (LrgMountDef *self,
                                    gdouble      multiplier);

/**
 * lrg_mount_def_get_required_riding:
 * @self: an #LrgMountDef
 *
 * Returns: the minimum riding skill tier
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mount_def_get_required_riding (LrgMountDef *self);

/**
 * lrg_mount_def_set_required_riding:
 * @self: an #LrgMountDef
 * @tier: the minimum riding skill tier
 *
 * Sets the minimum riding skill tier.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mount_def_set_required_riding (LrgMountDef *self,
                                   guint        tier);

/**
 * lrg_mount_def_get_flying:
 * @self: an #LrgMountDef
 *
 * Returns: whether the mount can fly
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mount_def_get_flying (LrgMountDef *self);

/**
 * lrg_mount_def_set_flying:
 * @self: an #LrgMountDef
 * @flying: whether the mount can fly
 *
 * Sets whether the mount can fly.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mount_def_set_flying (LrgMountDef *self,
                          gboolean     flying);

/**
 * lrg_mount_def_get_passengers:
 * @self: an #LrgMountDef
 *
 * Returns: the number of extra passenger seats
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mount_def_get_passengers (LrgMountDef *self);

/**
 * lrg_mount_def_set_passengers:
 * @self: an #LrgMountDef
 * @passengers: extra passenger seats, at most %LRG_MOUNT_DEF_MAX_PASSENGERS
 *
 * Sets the number of extra passenger seats.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mount_def_set_passengers (LrgMountDef *self,
                              guint        passengers);

/**
 * lrg_mount_def_can_ride:
 * @self: an #LrgMountDef
 * @riding_tier: the character's riding skill tier
 * @level: the character's level
 *
 * Checks whether a character may ride this mount: @riding_tier must be
 * at least #LrgMountDef:required-riding and @level at least
 * #LrgCollectibleDef:required-level.
 *
 * Returns: %TRUE when the mount may be ridden
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mount_def_can_ride (LrgMountDef *self,
                        guint        riding_tier,
                        guint        level);

/**
 * lrg_mount_def_get_speed:
 * @self: an #LrgMountDef
 * @base_speed: the character's unmounted movement speed
 * @riding_multiplier: multiplier granted by the character's riding skill
 *
 * Computes the mounted speed as
 * @base_speed * @riding_multiplier * #LrgMountDef:speed-multiplier.
 * Negative or non-finite inputs yield 0.
 *
 * Returns: the mounted movement speed
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_mount_def_get_speed (LrgMountDef *self,
                         gdouble      base_speed,
                         gdouble      riding_multiplier);

G_END_DECLS
