/* lrg-collectible-def.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCollectibleDef - immutable-ish data definition of something a
 * character can own in their collection (mount, pet, title, toy).
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

#define LRG_TYPE_COLLECTIBLE_DEF (lrg_collectible_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCollectibleDef, lrg_collectible_def, LRG, COLLECTIBLE_DEF, GObject)

/**
 * LrgCollectibleDefClass:
 * @parent_class: parent class
 * @can_obtain: checks whether a character of @level may obtain the
 *   collectible. The default implementation compares @level against the
 *   #LrgCollectibleDef:required-level property.
 *
 * Class structure for #LrgCollectibleDef. Subclass it to attach
 * game-specific acquisition rules.
 */
struct _LrgCollectibleDefClass
{
    GObjectClass parent_class;

    gboolean (*can_obtain) (LrgCollectibleDef  *self,
                            guint               level,
                            GError            **error);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_collectible_def_new:
 * @id: unique, non-empty identifier
 * @kind: the collectible kind
 *
 * Creates a plain collectible definition (typically a title or toy; use
 * lrg_mount_def_new() and lrg_pet_def_new() for mounts and pets).
 *
 * Returns: (transfer full): a new #LrgCollectibleDef
 */
LRG_AVAILABLE_IN_ALL
LrgCollectibleDef *
lrg_collectible_def_new (const gchar        *id,
                         LrgCollectibleKind  kind);

/**
 * lrg_collectible_def_get_id:
 * @self: an #LrgCollectibleDef
 *
 * Returns: (transfer none) (nullable): the identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_collectible_def_get_id (LrgCollectibleDef *self);

/**
 * lrg_collectible_def_get_kind:
 * @self: an #LrgCollectibleDef
 *
 * Gets the kind. #LrgMountDef instances always report
 * %LRG_COLLECTIBLE_KIND_MOUNT and #LrgPetDef instances always report
 * %LRG_COLLECTIBLE_KIND_PET regardless of the construct value.
 *
 * Returns: the collectible kind
 */
LRG_AVAILABLE_IN_ALL
LrgCollectibleKind
lrg_collectible_def_get_kind (LrgCollectibleDef *self);

/**
 * lrg_collectible_def_get_name:
 * @self: an #LrgCollectibleDef
 *
 * Returns: (transfer none) (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_collectible_def_get_name (LrgCollectibleDef *self);

/**
 * lrg_collectible_def_set_name:
 * @self: an #LrgCollectibleDef
 * @name: (nullable): the display name
 *
 * Sets the display name.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_collectible_def_set_name (LrgCollectibleDef *self,
                              const gchar       *name);

/**
 * lrg_collectible_def_get_description:
 * @self: an #LrgCollectibleDef
 *
 * Returns: (transfer none) (nullable): the description
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_collectible_def_get_description (LrgCollectibleDef *self);

/**
 * lrg_collectible_def_set_description:
 * @self: an #LrgCollectibleDef
 * @description: (nullable): the description
 *
 * Sets the description.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_collectible_def_set_description (LrgCollectibleDef *self,
                                     const gchar       *description);

/**
 * lrg_collectible_def_get_icon:
 * @self: an #LrgCollectibleDef
 *
 * Returns: (transfer none) (nullable): the icon key
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_collectible_def_get_icon (LrgCollectibleDef *self);

/**
 * lrg_collectible_def_set_icon:
 * @self: an #LrgCollectibleDef
 * @icon: (nullable): the icon key
 *
 * Sets the icon key.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_collectible_def_set_icon (LrgCollectibleDef *self,
                              const gchar       *icon);

/**
 * lrg_collectible_def_get_rarity:
 * @self: an #LrgCollectibleDef
 *
 * Returns: the rarity tier
 */
LRG_AVAILABLE_IN_ALL
LrgCollectibleRarity
lrg_collectible_def_get_rarity (LrgCollectibleDef *self);

/**
 * lrg_collectible_def_set_rarity:
 * @self: an #LrgCollectibleDef
 * @rarity: the rarity tier
 *
 * Sets the rarity tier.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_collectible_def_set_rarity (LrgCollectibleDef    *self,
                                LrgCollectibleRarity  rarity);

/**
 * lrg_collectible_def_get_source:
 * @self: an #LrgCollectibleDef
 *
 * Returns: (transfer none) (nullable): human readable acquisition source
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_collectible_def_get_source (LrgCollectibleDef *self);

/**
 * lrg_collectible_def_set_source:
 * @self: an #LrgCollectibleDef
 * @source: (nullable): human readable acquisition source, e.g. "Vendor: Stablemaster"
 *
 * Sets the acquisition source text.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_collectible_def_set_source (LrgCollectibleDef *self,
                                const gchar       *source);

/**
 * lrg_collectible_def_get_model:
 * @self: an #LrgCollectibleDef
 *
 * Returns: (transfer none) (nullable): the model / visual key
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_collectible_def_get_model (LrgCollectibleDef *self);

/**
 * lrg_collectible_def_set_model:
 * @self: an #LrgCollectibleDef
 * @model: (nullable): the model / visual key
 *
 * Sets the model / visual key.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_collectible_def_set_model (LrgCollectibleDef *self,
                               const gchar       *model);

/**
 * lrg_collectible_def_get_required_level:
 * @self: an #LrgCollectibleDef
 *
 * Returns: the minimum character level required to obtain or use it
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_collectible_def_get_required_level (LrgCollectibleDef *self);

/**
 * lrg_collectible_def_set_required_level:
 * @self: an #LrgCollectibleDef
 * @level: the minimum character level
 *
 * Sets the minimum character level.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_collectible_def_set_required_level (LrgCollectibleDef *self,
                                        guint              level);

/**
 * lrg_collectible_def_can_obtain:
 * @self: an #LrgCollectibleDef
 * @level: the character level
 * @error: (nullable): return location for an error
 *
 * Calls the #LrgCollectibleDefClass.can_obtain virtual method. The default
 * implementation fails with %LRG_PROGRESSION_ERROR_REQUIREMENT when @level
 * is below #LrgCollectibleDef:required-level.
 *
 * Returns: %TRUE when a character of @level may obtain the collectible
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collectible_def_can_obtain (LrgCollectibleDef  *self,
                                guint               level,
                                GError            **error);

G_END_DECLS
