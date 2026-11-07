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
 *
 * Since 0.9.0:
 *
 *  - a prefab may be a *variant*: its root is itself a linked instance of a
 *    base prefab (it carries an #LrgPrefabLink to the base). Loading a
 *    variant with lrg_prefab_load_resolved() walks the base chain (cycles
 *    and chains deeper than 8 are errors) and syncs the variant against
 *    its resolved base, so base edits reach the variant and, through it,
 *    every instance of the variant;
 *  - a level may store a linked instance as a *reference* (the link, the
 *    overridden values and the instance-added children) instead of the
 *    expanded subtree (#LrgPrefabStorage). lrg_prefab_make_reference()
 *    builds that sparse form and lrg_prefab_instantiate_reference()
 *    expands it again; the level serializer does both, so
 *    lrg_level_instantiate() and games always see the full subtree.
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
 * LrgPrefabStorage
 * ========================================================================== */

/**
 * LrgPrefabStorage:
 * @LRG_PREFAB_STORAGE_DEFAULT: no preference here; the next level of
 *   configuration decides (serializer, then level, then project, then
 *   %LRG_PREFAB_STORAGE_EXPANDED)
 * @LRG_PREFAB_STORAGE_EXPANDED: a linked instance is saved as its whole
 *   subtree plus the `prefab:` link block (the 0.8.x format; a game that
 *   ignores links still sees every node)
 * @LRG_PREFAB_STORAGE_REFERENCE: a linked instance is saved as a reference:
 *   its root, the link, the overridden values and the instance-added
 *   children only. The rest is rebuilt from the `.rprefab` when the level
 *   loads, so prefab edits reach the level without re-saving it
 *
 * How a level stores linked prefab instances (the `store_prefab_instances`
 * setting of a level or project; see `docs/modules/level/prefabs-runtime.org`).
 * Loading always understands both forms.
 *
 * Since: 0.9.0
 */
typedef enum
{
	LRG_PREFAB_STORAGE_DEFAULT,
	LRG_PREFAB_STORAGE_EXPANDED,
	LRG_PREFAB_STORAGE_REFERENCE
} LrgPrefabStorage;

LRG_AVAILABLE_IN_ALL
GType lrg_prefab_storage_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PREFAB_STORAGE (lrg_prefab_storage_get_type ())

/**
 * lrg_prefab_storage_from_string:
 * @text: (nullable): `expanded`, `reference` or `default` (case
 *   insensitive)
 *
 * Parses the value of a `store_prefab_instances` setting.
 *
 * Returns: the storage, %LRG_PREFAB_STORAGE_DEFAULT for %NULL or unknown
 *   text
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgPrefabStorage lrg_prefab_storage_from_string (const gchar *text);

/**
 * lrg_prefab_storage_to_string:
 * @storage: a storage mode
 *
 * Returns: (transfer none): `default`, `expanded` or `reference`
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_prefab_storage_to_string (LrgPrefabStorage storage);

/**
 * lrg_prefab_level_get_storage:
 * @level: an #LrgLevel
 *
 * Gets the level's own `store_prefab_instances` setting (read from and
 * written to the `.rlevel` file by the serializer).
 *
 * Returns: the level setting, %LRG_PREFAB_STORAGE_DEFAULT when unset
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgPrefabStorage lrg_prefab_level_get_storage (LrgLevel *level);

/**
 * lrg_prefab_level_set_storage:
 * @level: an #LrgLevel
 * @storage: the level setting; %LRG_PREFAB_STORAGE_DEFAULT defers to the
 *   project
 *
 * Sets how this level stores linked prefab instances when it is saved.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_prefab_level_set_storage (LrgLevel         *level,
                                   LrgPrefabStorage  storage);

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
 * stay valid across saves. Links on descendants (nested instances) are
 * written. Since 0.9.0 a link on @node itself is written too: @node is
 * then a prefab variant of the link's source. Save a copy made with
 * lrg_prefab_copy() (@keep_root_link %FALSE) to save an instance as an
 * independent prefab.
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
 * sources. Since 0.9.0 the root keeps its #LrgPrefabLink when the file is
 * a prefab variant (its base is not read; see lrg_prefab_load_resolved()).
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
 * source files. Since 0.9.0, when the prefab is a variant its base chain
 * is loaded and resolved first and the variant is synced against it
 * (variant overrides kept), so a base edit shows in the variant without
 * re-saving it. Nesting and the variant chain share one depth limit (8)
 * and one cycle check. @dependencies then lists the bases too. A relative
 * source path is tried as given (current directory) and then relative to
 * the directory of the prefab that names it.
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
 * modified in memory only. When @prefab is a variant, descendants that
 * mirror its base (mapped by the root link) are left to the base sync and
 * not resolved again.
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

/* ==========================================================================
 * Guids, copies and variants (since 0.9.0)
 * ========================================================================== */

/**
 * lrg_prefab_derive_guid:
 * @instance_root_guid: guid of a linked instance root
 * @source_guid: guid of a node of its source prefab
 *
 * Computes the guid an instance node mirroring @source_guid gets below
 * the instance root @instance_root_guid. It is a name-based
 * (UUIDv5-shaped) guid of the two, the same one
 * lrg_prefab_instantiate_linked() and lrg_prefab_sync_instance() assign.
 * Reference storage relies on it: a level does not record the map
 * entries a loader can re-derive.
 *
 * Returns: (transfer full): the derived guid
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_prefab_derive_guid (const gchar *instance_root_guid,
                                const gchar *source_guid);

/**
 * lrg_prefab_copy:
 * @node: a node subtree
 * @keep_root_link: whether the copy keeps @node's own #LrgPrefabLink
 *
 * An exact deep copy: same guids, same nested links (unlike
 * lrg_prefab_clone(), which assigns fresh guids). With @keep_root_link
 * %FALSE the copy of a linked instance root is a plain subtree, which
 * lrg_prefab_save() writes as an independent prefab.
 *
 * Returns: (transfer full): the copy
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_prefab_copy (LrgNode  *node,
                           gboolean  keep_root_link);

/**
 * lrg_prefab_is_variant:
 * @prefab: a prefab root node
 *
 * Returns: %TRUE when @prefab is a variant (its root carries a link to a
 *   base prefab)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_is_variant (LrgNode *prefab);

/**
 * lrg_prefab_get_variant_chain:
 * @path: a `.rprefab` path
 * @error: (nullable): return location for an error
 *
 * Follows the base links of a variant: @path first, then its base, the
 * base's base, and so on. A prefab that is not a variant yields just
 * @path. Only the root links are read; nothing is resolved.
 *
 * Returns: (transfer full) (array zero-terminated=1) (nullable): the chain
 *   from @path to the outermost base, or %NULL with @error set when a file
 *   cannot be read, the chain loops (%G_IO_ERROR_INVALID_DATA) or is deeper
 *   than 8
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_prefab_get_variant_chain (const gchar  *path,
                                    GError      **error);

/**
 * lrg_prefab_create_variant:
 * @base: the base prefab root (resolved)
 * @base_path: the base's `.rprefab` path
 * @base_guid: (nullable): the base's asset guid, if known
 *
 * Creates a new variant of @base: a linked instance of it (see
 * lrg_prefab_instantiate_linked()) with no overrides whose root location
 * and rotation are zeroed. Save it with lrg_prefab_save(); edits to it are
 * overrides relative to @base.
 *
 * Returns: (transfer full): the variant root
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_prefab_create_variant (LrgNode     *base,
                                     const gchar *base_path,
                                     const gchar *base_guid);

/**
 * lrg_prefab_derive_variant:
 * @instance: a linked instance root (its overrides become the variant's)
 * @variant_path: where the variant will be saved
 * @variant_guid: (nullable): the variant's asset guid, if known
 *
 * "Create Variant" from an instance: returns a variant of the instance's
 * source holding the instance's overrides (a copy with fresh guids whose
 * root link is the instance link, rewritten), and relinks @instance to
 * that variant with no overrides of its own, so it looks exactly as
 * before. The variant root's location and rotation are zeroed. The caller
 * saves the variant to @variant_path (lrg_prefab_save()).
 *
 * Returns: (transfer full) (nullable): the variant root, or %NULL when
 *   @instance is not a linked instance root
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_prefab_derive_variant (LrgNode     *instance,
                                     const gchar *variant_path,
                                     const gchar *variant_guid);

/* ==========================================================================
 * Reference storage (since 0.9.0)
 * ========================================================================== */

/**
 * lrg_prefab_make_reference:
 * @instance: a linked instance root
 *
 * Builds the sparse *reference* form of @instance that reference storage
 * writes: the root with its own state and a copy of the link; below it
 * only the nodes that matter, namely nodes with overrides, nodes moved on
 * purpose (`children/+<guid>`), instance-added subtrees (copied whole,
 * guids and nested links kept) and their ancestors. When a node is kept,
 * all its children are kept too (other mapped children as bare
 * placeholders with name and guid), so the child order survives. Nested
 * links of mapped nodes are dropped: they come back from the source.
 *
 * Returns: (transfer full) (nullable): the sparse root, or %NULL when
 *   @instance carries no link
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_prefab_make_reference (LrgNode *instance);

/**
 * lrg_prefab_instantiate_reference:
 * @reference: a sparse instance root (from lrg_prefab_make_reference() or
 *   read from a level in reference storage)
 * @prefab: its source prefab root, resolved (lrg_prefab_load_resolved())
 * @error: (nullable): return location for an error
 *
 * Expands a reference into the full linked instance, without any editor
 * object: every source node is created with the guid recorded in the
 * link map or else lrg_prefab_derive_guid() of the root guid (the guid the
 * editor gave it), source children removed by `children/-` overrides are
 * skipped, nested links are translated from the source, the values at
 * the overridden paths are copied from @reference, instance-added
 * subtrees are inserted where they were, nodes moved on purpose are moved
 * and the recorded child order is restored. The root keeps its own name,
 * location, rotation and locked flag. The result carries the complete
 * link map, exactly as an expanded instance would.
 *
 * Returns: (transfer full) (nullable): the expanded instance root, or
 *   %NULL with @error set when @reference carries no link
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_prefab_instantiate_reference (LrgNode  *reference,
                                            LrgNode  *prefab,
                                            GError  **error);

G_END_DECLS
