/* lrg-nav-poly-mesh-cache.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavPolyMeshCache - an in-memory, least-recently-used cache of baked
 * navigation meshes keyed by the digest of their bake input
 * (lrg_nav_poly_mesh_get_input_digest()).  lrg_nav_poly_mesh_bake_cached()
 * loads a cached bake (lrg_nav_poly_mesh_load()) instead of baking the
 * same input again: a level whose moving parts return to an arrangement
 * baked before (a save loaded back, a lever pulled twice) costs a load,
 * not a bake.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_NAV_POLY_MESH_CACHE (lrg_nav_poly_mesh_cache_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNavPolyMeshCache, lrg_nav_poly_mesh_cache, LRG, NAV_POLY_MESH_CACHE, GObject)

/**
 * lrg_nav_poly_mesh_cache_new:
 * @max_entries: how many bakes the cache keeps (at least 1); the least
 *   recently used one is dropped first
 *
 * Creates an empty cache.  Each entry holds one mesh's
 * lrg_nav_poly_mesh_save() bytes.  Single-threaded.
 *
 * Returns: (transfer full): a new #LrgNavPolyMeshCache
 */
LRG_AVAILABLE_IN_ALL
LrgNavPolyMeshCache *
lrg_nav_poly_mesh_cache_new (guint max_entries);

/**
 * lrg_nav_poly_mesh_cache_lookup:
 * @self: an #LrgNavPolyMeshCache
 * @digest: an input digest (lrg_nav_poly_mesh_get_input_digest())
 *
 * Finds a cached bake and marks it as the most recently used.  Counts a
 * hit or a miss.
 *
 * Returns: (transfer full) (nullable): the bake's bytes, or %NULL
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_nav_poly_mesh_cache_lookup (LrgNavPolyMeshCache *self,
                                const gchar         *digest);

/**
 * lrg_nav_poly_mesh_cache_insert:
 * @self: an #LrgNavPolyMeshCache
 * @digest: the input digest of the bake
 * @bytes: the bake (lrg_nav_poly_mesh_save())
 *
 * Stores (or replaces) the bake for @digest as the most recently used
 * entry, dropping the least recently used one when the cache is full.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_nav_poly_mesh_cache_insert (LrgNavPolyMeshCache *self,
                                const gchar         *digest,
                                GBytes              *bytes);

/**
 * lrg_nav_poly_mesh_cache_clear:
 * @self: an #LrgNavPolyMeshCache
 *
 * Drops every entry; the hit and miss counts stay.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_nav_poly_mesh_cache_clear (LrgNavPolyMeshCache *self);

/**
 * lrg_nav_poly_mesh_cache_get_max_entries:
 * @self: an #LrgNavPolyMeshCache
 *
 * Returns: how many bakes the cache keeps at most
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_poly_mesh_cache_get_max_entries (LrgNavPolyMeshCache *self);

/**
 * lrg_nav_poly_mesh_cache_get_size:
 * @self: an #LrgNavPolyMeshCache
 *
 * Returns: how many bakes the cache holds now
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_poly_mesh_cache_get_size (LrgNavPolyMeshCache *self);

/**
 * lrg_nav_poly_mesh_cache_get_bytes:
 * @self: an #LrgNavPolyMeshCache
 *
 * Returns: the total size of the cached bakes in bytes
 */
LRG_AVAILABLE_IN_ALL
gsize
lrg_nav_poly_mesh_cache_get_bytes (LrgNavPolyMeshCache *self);

/**
 * lrg_nav_poly_mesh_cache_get_hits:
 * @self: an #LrgNavPolyMeshCache
 *
 * Returns: how many lookups found a bake
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_poly_mesh_cache_get_hits (LrgNavPolyMeshCache *self);

/**
 * lrg_nav_poly_mesh_cache_get_misses:
 * @self: an #LrgNavPolyMeshCache
 *
 * Returns: how many lookups found nothing
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_poly_mesh_cache_get_misses (LrgNavPolyMeshCache *self);

G_END_DECLS
