/* lrg-prefab.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Prefabs: reusable saved node subtrees, and linked prefab instances.
 *
 * A prefab is a single #LrgNode subtree saved to a `.rprefab` file (reusing the
 * `.rlevel` serializer). lrg_prefab_clone() deep-copies a subtree with fresh
 * guids; lrg_prefab_instantiate() produces an independent placeable copy; and
 * lrg_prefab_save()/lrg_prefab_load() round-trip a prefab to disk.
 *
 * Since 0.8.1 an instance can stay *linked* to its source: the instance root
 * carries an #LrgPrefabLink (source path and asset guid, a map from every
 * instance node guid to the source node guid it came from, and a set of
 * overridden property paths). lrg_prefab_sync_instance() re-applies the
 * source everywhere except at overridden paths, and the apply / revert /
 * relink / unlink functions below manage the overrides.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgPrefabLink
 * ========================================================================== */

#define LRG_TYPE_PREFAB_LINK (lrg_prefab_link_get_type ())

/**
 * LrgPrefabLink:
 *
 * The link from a prefab instance root back to its source prefab.
 *
 * It records the source asset (a path, plus the asset guid when one is
 * known), a map from each instance node guid to the guid of the source node
 * it mirrors, and the set of override keys. An override key is a property
 * path (see `docs/editor/prefabs.org`) relative to one node of the instance:
 * a bare path such as `transform/scale` addresses the instance root, and
 * `<instance-node-guid>:<path>` addresses any other node of the instance.
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPrefabLink, lrg_prefab_link, LRG, PREFAB_LINK, GObject)

/**
 * lrg_prefab_link_new:
 * @source_path: path of the source `.rprefab` file
 * @source_guid: (nullable): asset guid of the source, if known
 *
 * Creates an empty link (no map entries, no overrides).
 *
 * Returns: (transfer full): a new #LrgPrefabLink
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
LrgPrefabLink * lrg_prefab_link_new (const gchar *source_path,
                                     const gchar *source_guid);

/**
 * lrg_prefab_link_copy:
 * @self: an #LrgPrefabLink
 *
 * Deep-copies the link (source, map and overrides).
 *
 * Returns: (transfer full): a new #LrgPrefabLink
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
LrgPrefabLink * lrg_prefab_link_copy (LrgPrefabLink *self);

/**
 * lrg_prefab_link_equal:
 * @a: (nullable): a link
 * @b: (nullable): another link
 *
 * Compares source, asset guid, map and overrides.
 *
 * Returns: %TRUE when both are %NULL or hold the same data
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_link_equal (LrgPrefabLink *a,
                                LrgPrefabLink *b);

/**
 * lrg_prefab_link_get_source_path:
 * @self: an #LrgPrefabLink
 *
 * Returns: (transfer none): the source `.rprefab` path
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_prefab_link_get_source_path (LrgPrefabLink *self);

/**
 * lrg_prefab_link_set_source_path:
 * @self: an #LrgPrefabLink
 * @path: the source `.rprefab` path
 *
 * Sets the source path.
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
void lrg_prefab_link_set_source_path (LrgPrefabLink *self,
                                      const gchar   *path);

/**
 * lrg_prefab_link_get_source_guid:
 * @self: an #LrgPrefabLink
 *
 * Gets the asset guid of the source prefab (from its `.meta` sidecar).
 * Resolution falls back to the path when it is %NULL.
 *
 * Returns: (transfer none) (nullable): the asset guid
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_prefab_link_get_source_guid (LrgPrefabLink *self);

/**
 * lrg_prefab_link_set_source_guid:
 * @self: an #LrgPrefabLink
 * @guid: (nullable): the asset guid
 *
 * Sets the asset guid of the source prefab.
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
void lrg_prefab_link_set_source_guid (LrgPrefabLink *self,
                                      const gchar   *guid);

/**
 * lrg_prefab_link_map_set:
 * @self: an #LrgPrefabLink
 * @instance_guid: guid of a node of the instance
 * @source_guid: guid of the source node it mirrors
 *
 * Records that @instance_guid mirrors @source_guid, replacing any earlier
 * entry for either guid.
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
void lrg_prefab_link_map_set (LrgPrefabLink *self,
                              const gchar   *instance_guid,
                              const gchar   *source_guid);

/**
 * lrg_prefab_link_map_remove:
 * @self: an #LrgPrefabLink
 * @instance_guid: guid of a node of the instance
 *
 * Forgets the mapping of @instance_guid.
 *
 * Returns: %TRUE if an entry was removed
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_link_map_remove (LrgPrefabLink *self,
                                     const gchar   *instance_guid);

/**
 * lrg_prefab_link_map_lookup:
 * @self: an #LrgPrefabLink
 * @instance_guid: guid of a node of the instance
 *
 * Returns: (transfer none) (nullable): the source node guid mirrored by
 *   @instance_guid, or %NULL when the node was added in the instance
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_prefab_link_map_lookup (LrgPrefabLink *self,
                                          const gchar   *instance_guid);

/**
 * lrg_prefab_link_map_lookup_instance:
 * @self: an #LrgPrefabLink
 * @source_guid: guid of a source node
 *
 * Reverse lookup of the map.
 *
 * Returns: (transfer none) (nullable): the instance node guid mirroring
 *   @source_guid
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_prefab_link_map_lookup_instance (LrgPrefabLink *self,
                                                   const gchar   *source_guid);

/**
 * lrg_prefab_link_get_map_size:
 * @self: an #LrgPrefabLink
 *
 * Returns: the number of mapped instance nodes
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
guint lrg_prefab_link_get_map_size (LrgPrefabLink *self);

/**
 * lrg_prefab_link_get_mapped_guids:
 * @self: an #LrgPrefabLink
 *
 * Returns: (transfer full) (array zero-terminated=1): the mapped instance
 *   node guids, sorted
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_prefab_link_get_mapped_guids (LrgPrefabLink *self);

/**
 * lrg_prefab_link_clear_map:
 * @self: an #LrgPrefabLink
 *
 * Removes every map entry.
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
void lrg_prefab_link_clear_map (LrgPrefabLink *self);

/**
 * lrg_prefab_link_add_override:
 * @self: an #LrgPrefabLink
 * @key: an override key (`path` or `<instance-guid>:path`)
 *
 * Adds @key to the override set.
 *
 * Returns: %TRUE if @key was not present before
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_link_add_override (LrgPrefabLink *self,
                                       const gchar   *key);

/**
 * lrg_prefab_link_remove_override:
 * @self: an #LrgPrefabLink
 * @key: an override key
 *
 * Removes exactly @key from the override set.
 *
 * Returns: %TRUE if @key was present
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_link_remove_override (LrgPrefabLink *self,
                                          const gchar   *key);

/**
 * lrg_prefab_link_has_override:
 * @self: an #LrgPrefabLink
 * @key: an override key
 *
 * Checks whether @key is overridden, either exactly or by a key naming one
 * of its parent paths on the same node (`visual` covers `visual/asset`).
 *
 * Returns: %TRUE if @key is covered by the override set
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_link_has_override (LrgPrefabLink *self,
                                       const gchar   *key);

/**
 * lrg_prefab_link_contains_override:
 * @self: an #LrgPrefabLink
 * @key: an override key
 *
 * Exact membership test (no parent-path coverage, unlike
 * lrg_prefab_link_has_override()).
 *
 * Returns: %TRUE if @key itself is in the override set
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_link_contains_override (LrgPrefabLink *self,
                                            const gchar   *key);

/**
 * lrg_prefab_link_get_overrides:
 * @self: an #LrgPrefabLink
 *
 * Returns: (transfer full) (array zero-terminated=1): the override keys,
 *   sorted
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_prefab_link_get_overrides (LrgPrefabLink *self);

/**
 * lrg_prefab_link_get_n_overrides:
 * @self: an #LrgPrefabLink
 *
 * Returns: the number of override keys
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
guint lrg_prefab_link_get_n_overrides (LrgPrefabLink *self);

/**
 * lrg_prefab_link_clear_overrides:
 * @self: an #LrgPrefabLink
 *
 * Removes every override key.
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
void lrg_prefab_link_clear_overrides (LrgPrefabLink *self);

/* ==========================================================================
 * Cloning, instancing and persistence
 * ========================================================================== */

/**
 * lrg_prefab_clone:
 * @node: the node subtree to copy
 *
 * Deep-copies @node and all descendants, assigning fresh guids to every copied
 * node. The copy is parentless. Prefab links inside the subtree (including
 * one on @node itself) are copied with their maps and override keys
 * rewritten to the new guids, so a duplicated instance stays linked.
 *
 * Returns: (transfer full): a new #LrgNode tree
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_prefab_clone (LrgNode *node);

/**
 * lrg_prefab_instantiate:
 * @prefab: a prefab root node
 *
 * Produces an independent placeable instance of @prefab (a fresh-guid deep
 * copy without a link to the source). See lrg_prefab_instantiate_linked().
 *
 * Returns: (transfer full): a new #LrgNode tree
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_prefab_instantiate (LrgNode *prefab);

/**
 * lrg_prefab_instantiate_linked:
 * @prefab: a prefab root node (as returned by lrg_prefab_load())
 * @source_path: the `.rprefab` path @prefab was loaded from
 * @source_guid: (nullable): the prefab's asset guid, if known
 *
 * Produces an instance of @prefab that stays linked to it. The root gets a
 * random guid; every other node gets a guid derived from the root guid and
 * its source node guid (stable across syncs). The root carries an
 * #LrgPrefabLink mapping every instance node to its source node, with no
 * overrides, and remembers its source like 0.8.0 instances did (a
 * %LRG_NODE_VISUAL_PREFAB_INSTANCE visual when the source root has no
 * visual, otherwise a `prefab:source` visual param). Nested instances
 * inside @prefab keep their own links, rewritten to the new guids.
 *
 * Returns: (transfer full): a new #LrgNode tree
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_prefab_instantiate_linked (LrgNode     *prefab,
                                         const gchar *source_path,
                                         const gchar *source_guid);

/**
 * lrg_prefab_save:
 * @node: the prefab root to save
 * @path: destination `.rprefab` path
 * @error: (nullable): return location for an error
 *
 * Saves @node (and its subtree) as a prefab. Node guids are written as they
 * are (since 0.8.1), so the source node guids recorded by linked instances
 * stay valid across saves. A prefab link on @node itself is not written
 * (prefab variants are not supported); links on descendants (nested
 * instances) are.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_save (LrgNode      *node,
                          const gchar  *path,
                          GError      **error);

/**
 * lrg_prefab_load:
 * @path: a `.rprefab` path
 * @error: (nullable): return location for an error
 *
 * Loads a prefab root node from disk. Nested instances are returned as
 * stored; see lrg_prefab_load_resolved() to refresh them from their own
 * sources.
 *
 * Returns: (transfer full) (nullable): the prefab root node, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_prefab_load (const gchar  *path,
                           GError      **error);

/**
 * lrg_prefab_load_resolved:
 * @path: a `.rprefab` path
 * @dependencies: (out) (optional) (transfer full) (array zero-terminated=1):
 *   return location for the canonical paths of every nested prefab that was
 *   read while resolving (transitively), or %NULL
 * @error: (nullable): return location for an error
 *
 * Loads a prefab like lrg_prefab_load(), then runs
 * lrg_prefab_resolve_nested() on it so nested instances reflect their own
 * source files.
 *
 * Returns: (transfer full) (nullable): the resolved prefab root, or %NULL on
 *   error
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_prefab_load_resolved (const gchar   *path,
                                    GStrv         *dependencies,
                                    GError       **error);

/**
 * lrg_prefab_resolve_nested:
 * @prefab: a prefab root node
 * @error: (nullable): return location for an error
 *
 * Syncs every outermost nested instance inside @prefab (a descendant that
 * carries an #LrgPrefabLink) against its own source file, loaded and
 * resolved recursively (depth limit 8, cycles are an error). The nested
 * instance's overrides, which belong to @prefab, are kept. @prefab itself is
 * modified in memory only.
 *
 * Returns: %TRUE on success; on failure the nested instances processed so
 *   far stay updated
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_resolve_nested (LrgNode  *prefab,
                                    GError  **error);

/**
 * lrg_prefab_regenerate_guids:
 * @root: a node subtree
 *
 * Gives every node of @root a fresh random guid and rewrites every prefab
 * link inside the subtree to the new guids (used when pasting).
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
void lrg_prefab_regenerate_guids (LrgNode *root);

/* ==========================================================================
 * Linked instances
 * ========================================================================== */

/**
 * lrg_prefab_find_owner:
 * @node: a node
 *
 * Finds the instance root whose link governs @node: the outermost linked
 * ancestor (or @node itself) that maps @node or is @node. Overrides of
 * @node are recorded on that root.
 *
 * Returns: (transfer none) (nullable): the owning instance root, or %NULL
 *   when @node is not part of a linked instance
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_prefab_find_owner (LrgNode *node);

/**
 * lrg_prefab_make_override_key:
 * @owner: the instance root
 * @node: a node of the instance (may be @owner)
 * @path: a property path relative to @node
 *
 * Builds an override key: @path for the root, `<guid>:path` otherwise.
 *
 * Returns: (transfer full): the key
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_prefab_make_override_key (LrgNode     *owner,
                                      LrgNode     *node,
                                      const gchar *path);

/**
 * lrg_prefab_is_overridden:
 * @node: a node
 * @path: a property path relative to @node
 *
 * Returns: %TRUE when @node belongs to a linked instance whose override set
 *   covers @path on @node
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_is_overridden (LrgNode     *node,
                                   const gchar *path);

/**
 * lrg_prefab_mark_override:
 * @node: a node of a linked instance
 * @path: a property path relative to @node
 *
 * Marks @path of @node overridden on the owning instance root. Does nothing
 * when @node is not part of a linked instance or the path is already
 * covered.
 *
 * Returns: %TRUE if a key was added
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_mark_override (LrgNode     *node,
                                   const gchar *path);

/**
 * lrg_prefab_diff_node_state:
 * @instance: a node
 * @source: the node to compare against
 * @is_instance_root: %TRUE when @instance is an instance root; its name,
 *   location and rotation are then instance-owned and never reported, and
 *   its prefab marker visual is ignored
 *
 * Lists the property paths of the node's own state (not children) whose
 * values differ between @instance and @source, e.g. `visible`,
 * `transform/scale`, `visual/params/radius`, `components/Health/max`,
 * `components/+Ai`, `scripts/ai.lua/enabled`.
 *
 * Returns: (transfer full) (array zero-terminated=1): the differing paths
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_prefab_diff_node_state (LrgNode  *instance,
                                  LrgNode  *source,
                                  gboolean  is_instance_root);

/**
 * lrg_prefab_compute_overrides:
 * @instance: a linked instance root
 * @prefab: its source prefab root
 *
 * Diffs the whole instance against @prefab and returns the override keys
 * that describe every difference: property paths per mapped node, plus
 * `children/+<guid>` for nodes added in the instance and
 * `children/-<source-guid>` for source children missing from it. Implicit
 * root overrides (name, location, rotation) are not listed. The link is not
 * modified.
 *
 * Returns: (transfer full) (array zero-terminated=1): the keys, sorted
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_prefab_compute_overrides (LrgNode *instance,
                                    LrgNode *prefab);

/**
 * lrg_prefab_sync_instance:
 * @instance: a linked instance root
 * @prefab: its (resolved) source prefab root
 *
 * Re-applies @prefab to @instance everywhere except at overridden paths:
 * property values are copied path by path; source children missing from
 * the instance are created (unless `children/-<source-guid>` is
 * overridden); instance children whose source node was deleted are
 * removed, unless they carry overrides, in which case they are kept as
 * instance-added children. Guids of mapped nodes are preserved, existing
 * children are not reordered, and nested links are refreshed from the
 * source. The root's name, location, rotation and `locked` flag are never
 * touched.
 *
 * Returns: %TRUE if anything in @instance changed
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_sync_instance (LrgNode *instance,
                                   LrgNode *prefab);

/**
 * lrg_prefab_apply_overrides_to_source:
 * @instance: a linked instance root
 * @prefab: its source prefab root (modified)
 * @keys: (nullable) (array zero-terminated=1): override keys to apply, or
 *   %NULL for all of them
 *
 * Writes the instance values at @keys back into @prefab and removes those
 * keys from the instance. Added children are cloned into the source (and
 * mapped), removed children are deleted from it. Edits that land inside a
 * nested instance of @prefab are marked as overrides of that nested
 * instance. The caller then saves @prefab and syncs the other instances.
 *
 * Returns: the number of keys applied
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
guint lrg_prefab_apply_overrides_to_source (LrgNode            *instance,
                                            LrgNode            *prefab,
                                            const gchar *const *keys);

/**
 * lrg_prefab_revert_override:
 * @instance: a linked instance root
 * @prefab: its source prefab root
 * @key: the override key to revert
 *
 * Restores the source value at @key (re-creating a removed child, deleting
 * an added one) and removes @key, plus every key below it, from the set.
 *
 * Returns: %TRUE if @key was overridden and has been reverted
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_revert_override (LrgNode     *instance,
                                     LrgNode     *prefab,
                                     const gchar *key);

/**
 * lrg_prefab_revert_all:
 * @instance: a linked instance root
 * @prefab: its source prefab root
 *
 * Reverts every override, then syncs so unmarked drift is reverted too.
 *
 * Returns: the number of override keys that were reverted
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
guint lrg_prefab_revert_all (LrgNode *instance,
                             LrgNode *prefab);

/**
 * lrg_prefab_unlink:
 * @instance: a linked instance root
 *
 * Removes the link from @instance, turning it into a plain copy. Nested
 * instances below it keep their own links. The 0.8.0 source marker (visual
 * kind or `prefab:source` param) is left in place so the instance can be
 * relinked.
 *
 * Returns: %TRUE if @instance was linked
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_unlink (LrgNode *instance);

/**
 * lrg_prefab_relink:
 * @instance: an (unlinked) instance root, e.g. from a 0.8.0 level
 * @prefab: the source prefab root
 * @source_path: (nullable): the source path; %NULL reads it from the 0.8.0
 *   marker (`prefab:source` param or prefab-instance visual asset)
 * @source_guid: (nullable): the asset guid, if known
 *
 * Links @instance to @prefab by structure: roots match each other; below,
 * children match source children of the same name (first unused), and the
 * leftovers match by order when both sides have the same number left.
 * Unmatched instance children become `children/+` overrides, unmatched
 * source children `children/-` overrides, and the overrides are then
 * computed with lrg_prefab_compute_overrides(). Guids are not changed.
 *
 * Returns: %TRUE on success, %FALSE when no source path is known
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_relink (LrgNode     *instance,
                            LrgNode     *prefab,
                            const gchar *source_path,
                            const gchar *source_guid);

/**
 * lrg_prefab_get_marker_source:
 * @node: a node
 *
 * Reads the 0.8.0 source marker of a prefab instance: the `prefab:source`
 * visual param, or the asset of a %LRG_NODE_VISUAL_PREFAB_INSTANCE visual.
 *
 * Returns: (transfer none) (nullable): the source path recorded on @node
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_prefab_get_marker_source (LrgNode *node);

G_END_DECLS
