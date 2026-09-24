/* lrg-collection.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCollection - a character's owned mounts, pets, titles and toys with
 * favourites and one active selection per kind. Server-authoritative
 * runtime state persisted through lrg_collection_to_variant().
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

#define LRG_TYPE_COLLECTION (lrg_collection_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCollection, lrg_collection, LRG, COLLECTION, GObject)

/**
 * LRG_COLLECTION_MAX_ENTRIES:
 *
 * Maximum number of owned collectibles (all kinds combined). Also the
 * maximum accepted by lrg_collection_new_from_variant().
 */
#define LRG_COLLECTION_MAX_ENTRIES (4096)

/**
 * LRG_COLLECTION_MAX_ID_LENGTH:
 *
 * Maximum identifier length in bytes.
 */
#define LRG_COLLECTION_MAX_ID_LENGTH (128)

/**
 * LRG_COLLECTION_VARIANT_TYPE:
 *
 * GVariant type string of lrg_collection_to_variant():
 * `(a(usb)a(us))` = (owned entries as (kind, id, favourite) sorted by
 * kind then id; active selections as (kind, id) sorted by kind).
 */
#define LRG_COLLECTION_VARIANT_TYPE "(a(usb)a(us))"

/**
 * lrg_collection_new:
 *
 * Creates an empty collection.
 *
 * Returns: (transfer full): a new #LrgCollection
 */
LRG_AVAILABLE_IN_ALL
LrgCollection *
lrg_collection_new (void);

/**
 * lrg_collection_add:
 * @self: an #LrgCollection
 * @kind: the collectible kind
 * @id: collectible identifier (1..128 bytes of valid UTF-8)
 * @error: (nullable): return location for an error
 *
 * Adds an owned collectible. Identifiers are unique across all kinds.
 *
 * Errors: %LRG_PROGRESSION_ERROR_INVALID for an unknown @kind or a NULL,
 * empty, overlong or non-UTF-8 @id; %LRG_PROGRESSION_ERROR_DUPLICATE when
 * @id is already owned (of any kind); %LRG_PROGRESSION_ERROR_LIMIT when
 * the collection already holds %LRG_COLLECTION_MAX_ENTRIES entries. On
 * failure the collection is unchanged.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collection_add (LrgCollection       *self,
                    LrgCollectibleKind   kind,
                    const gchar         *id,
                    GError             **error);

/**
 * lrg_collection_remove:
 * @self: an #LrgCollection
 * @id: collectible identifier
 *
 * Removes an owned collectible, dropping its favourite flag and clearing
 * the active selection of its kind if it was active.
 *
 * Returns: %TRUE if @id was owned
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collection_remove (LrgCollection *self,
                       const gchar   *id);

/**
 * lrg_collection_has:
 * @self: an #LrgCollection
 * @id: (nullable): collectible identifier
 *
 * Returns: %TRUE if @id is owned
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collection_has (LrgCollection *self,
                    const gchar   *id);

/**
 * lrg_collection_get_kind:
 * @self: an #LrgCollection
 * @id: (nullable): collectible identifier
 * @out_kind: (out) (optional): return location for the kind
 *
 * Looks up the kind @id was added with.
 *
 * Returns: %TRUE if @id is owned
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collection_get_kind (LrgCollection      *self,
                         const gchar        *id,
                         LrgCollectibleKind *out_kind);

/**
 * lrg_collection_get_count:
 * @self: an #LrgCollection
 * @kind: the collectible kind
 *
 * Returns: number of owned collectibles of @kind (0 for an unknown kind)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_collection_get_count (LrgCollection      *self,
                          LrgCollectibleKind  kind);

/**
 * lrg_collection_get_total_count:
 * @self: an #LrgCollection
 *
 * Returns: number of owned collectibles of every kind
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_collection_get_total_count (LrgCollection *self);

/**
 * lrg_collection_get_ids:
 * @self: an #LrgCollection
 * @kind: the collectible kind
 *
 * Gets the owned identifiers of @kind sorted with g_strcmp0(). The
 * strings are owned by the collection and remain valid until the entry
 * is removed or the collection is finalized.
 *
 * Returns: (transfer container) (element-type utf8): sorted identifiers
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_collection_get_ids (LrgCollection      *self,
                        LrgCollectibleKind  kind);

/**
 * lrg_collection_get_favorites:
 * @self: an #LrgCollection
 * @kind: the collectible kind
 *
 * Gets the favourite identifiers of @kind sorted with g_strcmp0(), with
 * the same string ownership as lrg_collection_get_ids().
 *
 * Returns: (transfer container) (element-type utf8): sorted identifiers
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_collection_get_favorites (LrgCollection      *self,
                              LrgCollectibleKind  kind);

/**
 * lrg_collection_set_favorite:
 * @self: an #LrgCollection
 * @id: owned collectible identifier
 * @favorite: new favourite flag
 * @error: (nullable): return location for an error
 *
 * Marks or unmarks a favourite.
 *
 * Errors: %LRG_PROGRESSION_ERROR_NOT_FOUND when @id is not owned.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collection_set_favorite (LrgCollection  *self,
                             const gchar    *id,
                             gboolean        favorite,
                             GError        **error);

/**
 * lrg_collection_is_favorite:
 * @self: an #LrgCollection
 * @id: (nullable): collectible identifier
 *
 * Returns: %TRUE if @id is owned and marked favourite
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collection_is_favorite (LrgCollection *self,
                            const gchar   *id);

/**
 * lrg_collection_set_active:
 * @self: an #LrgCollection
 * @kind: the collectible kind whose selection changes
 * @id: (nullable): owned identifier of that kind, or %NULL to clear
 * @error: (nullable): return location for an error
 *
 * Selects the active collectible of @kind (summoned pet, current mount,
 * displayed title, ...).
 *
 * Errors: %LRG_PROGRESSION_ERROR_INVALID for an unknown @kind or when @id
 * is owned with a different kind; %LRG_PROGRESSION_ERROR_NOT_FOUND when
 * @id is not owned. On failure the selection is unchanged.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collection_set_active (LrgCollection       *self,
                           LrgCollectibleKind   kind,
                           const gchar         *id,
                           GError             **error);

/**
 * lrg_collection_get_active:
 * @self: an #LrgCollection
 * @kind: the collectible kind
 *
 * Returns: (transfer none) (nullable): the active identifier of @kind
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_collection_get_active (LrgCollection      *self,
                           LrgCollectibleKind  kind);

/**
 * lrg_collection_to_variant:
 * @self: an #LrgCollection
 *
 * Serialises the collection as %LRG_COLLECTION_VARIANT_TYPE
 * (`(a(usb)a(us))`). Output is deterministic: entries sorted by
 * (kind, id), active selections sorted by kind.
 *
 * Returns: (transfer full): a new non-floating #GVariant
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_collection_to_variant (LrgCollection *self);

/**
 * lrg_collection_new_from_variant:
 * @variant: a variant of type %LRG_COLLECTION_VARIANT_TYPE; a floating
 *   reference is consumed
 * @error: (nullable): return location for an error
 *
 * Restores a collection. The whole snapshot is rejected with
 * %LRG_PROGRESSION_ERROR_INVALID when the type string differs, the data
 * is not in normal form, there are more than %LRG_COLLECTION_MAX_ENTRIES
 * entries or more active rows than kinds, a kind value is unknown, an id
 * is empty / longer than %LRG_COLLECTION_MAX_ID_LENGTH / not UTF-8, an id
 * or active kind is repeated, or an active id is not owned or owned with
 * another kind.
 *
 * Returns: (transfer full) (nullable): the restored collection
 */
LRG_AVAILABLE_IN_ALL
LrgCollection *
lrg_collection_new_from_variant (GVariant  *variant,
                                 GError   **error);

G_END_DECLS
