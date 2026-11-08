/* lrg-level.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Top-level editable level document.
 *
 * LrgLevel is the serializable authoring document at the heart of the editor.
 * It owns a root #LrgNode whose descendants form the editable scene tree, plus
 * level-wide settings and metadata. A level is host-agnostic data: it is baked
 * into an #LrgScene for rendering (lrg_level_to_scene()) and instantiated into
 * a runtime #LrgWorld for play-in-editor (lrg_level_instantiate()), and it
 * round-trips to disk as a `.rlevel` file via #LrgLevelSerializer.
 *
 * The level emits #LrgLevel::node-added, #LrgLevel::node-removed and
 * #LrgLevel::node-changed so a host IDE can keep its panels in sync.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lighting/lrg-environment.h"

G_BEGIN_DECLS

#define LRG_TYPE_LEVEL (lrg_level_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLevel, lrg_level, LRG, LEVEL, GObject)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_level_new:
 * @name: (nullable): the level name
 *
 * Creates a new, empty #LrgLevel with a single empty root node.
 *
 * Returns: (transfer full): a new #LrgLevel
 */
LRG_AVAILABLE_IN_ALL
LrgLevel * lrg_level_new (const gchar *name);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_level_get_name:
 * @self: an #LrgLevel
 *
 * Returns: (transfer none) (nullable): the level name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_level_get_name (LrgLevel *self);

/**
 * lrg_level_set_name:
 * @self: an #LrgLevel
 * @name: (nullable): the new name
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_set_name (LrgLevel    *self,
                         const gchar *name);

/**
 * lrg_level_get_default_2d:
 * @self: an #LrgLevel
 *
 * Gets whether the level defaults to a 2D editing view.
 *
 * Returns: %TRUE if the default view is 2D
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_get_default_2d (LrgLevel *self);

/**
 * lrg_level_set_default_2d:
 * @self: an #LrgLevel
 * @default_2d: whether the default view is 2D
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_set_default_2d (LrgLevel *self,
                               gboolean  default_2d);

/**
 * lrg_level_get_environment:
 * @self: an #LrgLevel
 *
 * The level's `environment:` block (sky, image-based lighting, ambient,
 * fog, volumetrics, post stack), %NULL for levels without one (every
 * level written before 0.9.0).
 *
 * Returns: (transfer none) (nullable): the environment
 */
LRG_AVAILABLE_IN_ALL
LrgEnvironment *lrg_level_get_environment (LrgLevel *self);

/**
 * lrg_level_set_environment:
 * @self: an #LrgLevel
 * @environment: (nullable) (transfer none): the new environment (a
 *   reference is kept), %NULL to drop the block
 *
 * Sets #LrgLevel:environment.  Changes made to the environment object
 * afterwards are saved with the level as well.
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_set_environment (LrgLevel       *self,
                                LrgEnvironment *environment);

/**
 * lrg_level_ensure_environment:
 * @self: an #LrgLevel
 *
 * Returns the level's environment, creating a default one
 * (lrg_environment_new()) first when there is none.
 *
 * Returns: (transfer none): the environment
 */
LRG_AVAILABLE_IN_ALL
LrgEnvironment *lrg_level_ensure_environment (LrgLevel *self);

/**
 * lrg_level_get_root:
 * @self: an #LrgLevel
 *
 * Gets the root node. All level content hangs off the root; the root itself is
 * an empty group node that is not serialized as content.
 *
 * Returns: (transfer none): the root #LrgNode
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_level_get_root (LrgLevel *self);

/* ==========================================================================
 * Node Management
 * ========================================================================== */

/**
 * lrg_level_add_node:
 * @self: an #LrgLevel
 * @node: (transfer none): the node to add
 * @parent: (nullable): the parent node, or %NULL to add under the root
 *
 * Adds @node under @parent (or the root). A reference is taken on @node.
 * Emits #LrgLevel::node-added.
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_add_node (LrgLevel *self,
                         LrgNode  *node,
                         LrgNode  *parent);

/**
 * lrg_level_insert_node:
 * @self: an #LrgLevel
 * @node: (transfer none): the node to insert
 * @parent: (nullable): the parent node, or %NULL for the root
 * @index: position among the parent's children, or -1 to append
 *
 * Inserts @node under @parent at @index and emits #LrgLevel::node-added.
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_insert_node (LrgLevel *self,
                            LrgNode  *node,
                            LrgNode  *parent,
                            gint      index);

/**
 * lrg_level_count_nodes:
 * @self: an #LrgLevel
 *
 * Returns: the number of nodes in the level, excluding the implicit root
 */
LRG_AVAILABLE_IN_ALL
guint lrg_level_count_nodes (LrgLevel *self);

/**
 * lrg_level_remove_node:
 * @self: an #LrgLevel
 * @node: the node to remove
 *
 * Removes @node from its parent. Emits #LrgLevel::node-removed.
 *
 * Returns: %TRUE if the node was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_remove_node (LrgLevel *self,
                                LrgNode  *node);

/**
 * lrg_level_find_node:
 * @self: an #LrgLevel
 * @guid: the node guid to find
 *
 * Searches the whole tree for a node with @guid.
 *
 * Returns: (transfer none) (nullable): the matching node, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_level_find_node (LrgLevel    *self,
                               const gchar *guid);

/**
 * lrg_level_notify_node_changed:
 * @self: an #LrgLevel
 * @node: the node that changed
 *
 * Emits #LrgLevel::node-changed for @node. Editor operations call this after
 * mutating a node so the host IDE can refresh its panels.
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_notify_node_changed (LrgLevel *self,
                                    LrgNode  *node);

/* ==========================================================================
 * Timelines (keyframed animation clips, see #LrgTimeline)
 * ========================================================================== */

/**
 * lrg_level_get_n_timelines:
 * @self: an #LrgLevel
 *
 * Returns: the number of timelines in the level
 */
LRG_AVAILABLE_IN_ALL
guint lrg_level_get_n_timelines (LrgLevel *self);

/**
 * lrg_level_get_timeline:
 * @self: an #LrgLevel
 * @index: a timeline index
 *
 * Returns: (transfer none) (nullable): the timeline, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgTimeline * lrg_level_get_timeline (LrgLevel *self,
                                      guint     index);

/**
 * lrg_level_find_timeline:
 * @self: an #LrgLevel
 * @name: a timeline name
 *
 * Returns: the index of the timeline called @name, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_level_find_timeline (LrgLevel    *self,
                              const gchar *name);

/**
 * lrg_level_insert_timeline:
 * @self: an #LrgLevel
 * @timeline: (transfer none): the timeline (a reference is taken)
 * @index: position, or -1 to append
 *
 * Adds a timeline. Names should be unique; lookups return the first match.
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_insert_timeline (LrgLevel    *self,
                                LrgTimeline *timeline,
                                gint         index);

/**
 * lrg_level_remove_timeline:
 * @self: an #LrgLevel
 * @index: a timeline index
 *
 * Returns: whether a timeline was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_remove_timeline (LrgLevel *self,
                                    guint     index);

/**
 * lrg_level_get_lightmap_ref:
 * @self: an #LrgLevel
 *
 * The `lighting: lightmap:` reference: a `.rlightmap` path (relative to
 * the level file) or a `guid:` asset reference (since 0.9.0).
 *
 * Returns: (transfer none) (nullable): the reference
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_level_get_lightmap_ref (LrgLevel *self);

/**
 * lrg_level_set_lightmap_ref:
 * @self: an #LrgLevel
 * @ref: (nullable): a `.rlightmap` path or `guid:` reference, %NULL or
 *   empty to drop it
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_set_lightmap_ref (LrgLevel    *self,
                                 const gchar *ref);

/**
 * lrg_level_add_probe_ref:
 * @self: an #LrgLevel
 * @ref: a `.rprobes` path or `guid:` reference
 *
 * Appends to `lighting: probes:` (a reference already listed is not
 * added twice).
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_add_probe_ref (LrgLevel    *self,
                              const gchar *ref);

/**
 * lrg_level_remove_probe_ref:
 * @self: an #LrgLevel
 * @ref: a listed reference
 *
 * Returns: %TRUE when @ref was listed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_remove_probe_ref (LrgLevel    *self,
                                     const gchar *ref);

/**
 * lrg_level_get_probe_refs:
 * @self: an #LrgLevel
 *
 * Returns: (transfer none) (element-type utf8): the `lighting: probes:`
 *   references, in file order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_level_get_probe_refs (LrgLevel *self);

/**
 * lrg_level_set_bake_setting:
 * @self: an #LrgLevel
 * @key: a bake setting (texels_per_unit, samples, bounces,
 *   max_lightmap_size, denoise, probe_spacing, ...)
 * @value: its value (booleans as 0 / 1)
 *
 * Sets one entry of `lighting: settings:`, the settings the editor's
 * bake panel hands the baker.
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_set_bake_setting (LrgLevel    *self,
                                 const gchar *key,
                                 gdouble      value);

/**
 * lrg_level_get_bake_setting:
 * @self: an #LrgLevel
 * @key: a bake setting
 * @out_value: (out) (optional): its value
 *
 * Returns: %TRUE when @key is set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_get_bake_setting (LrgLevel    *self,
                                     const gchar *key,
                                     gdouble     *out_value);

/**
 * lrg_level_get_bake_setting_keys:
 * @self: an #LrgLevel
 *
 * Returns: (transfer full) (element-type utf8): the set bake settings,
 *   sorted
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_level_get_bake_setting_keys (LrgLevel *self);

/**
 * lrg_level_has_lighting:
 * @self: an #LrgLevel
 *
 * Returns: %TRUE when the level has a lightmap, probe grids or bake
 *   settings (and therefore a `lighting:` block)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_has_lighting (LrgLevel *self);

/**
 * lrg_level_clear_lighting:
 * @self: an #LrgLevel
 *
 * Drops the whole `lighting:` block.
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_clear_lighting (LrgLevel *self);

/* ==========================================================================
 * Navigation (the `navigation:` block, 0.9.0 M6)
 * ========================================================================== */

/**
 * lrg_level_get_nav_settings_ref:
 * @self: an #LrgLevel
 *
 * The `navigation: settings:` reference: the level's `.rnav` bake
 * settings (#LrgNavBake), a path relative to the level file or a
 * `guid:` asset reference.
 *
 * Returns: (transfer none) (nullable): the reference
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_level_get_nav_settings_ref (LrgLevel *self);

/**
 * lrg_level_set_nav_settings_ref:
 * @self: an #LrgLevel
 * @ref: (nullable): a `.rnav` path or `guid:` reference, %NULL or empty
 *   to drop it
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_set_nav_settings_ref (LrgLevel    *self,
                                     const gchar *ref);

/**
 * lrg_level_get_nav_data_ref:
 * @self: an #LrgLevel
 *
 * The `navigation: data:` reference: the baked mesh
 * (lrg_nav_poly_mesh_save() bytes, conventionally `.rnavmesh`) that
 * lrg_level_instantiate_3d() loads onto the world.
 *
 * Returns: (transfer none) (nullable): the reference
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_level_get_nav_data_ref (LrgLevel *self);

/**
 * lrg_level_set_nav_data_ref:
 * @self: an #LrgLevel
 * @ref: (nullable): a baked mesh path or `guid:` reference, %NULL or
 *   empty to drop it
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_set_nav_data_ref (LrgLevel    *self,
                                 const gchar *ref);

/**
 * lrg_level_get_nav_bake_on_load:
 * @self: an #LrgLevel
 *
 * Returns: the `navigation: bake_on_load:` flag
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_get_nav_bake_on_load (LrgLevel *self);

/**
 * lrg_level_set_nav_bake_on_load:
 * @self: an #LrgLevel
 * @bake_on_load: when the baked data is missing (or does not load),
 *   bake the mesh from the instantiated world with the settings
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_set_nav_bake_on_load (LrgLevel *self,
                                     gboolean  bake_on_load);

/**
 * lrg_level_has_navigation:
 * @self: an #LrgLevel
 *
 * Returns: %TRUE when the level has a `navigation:` block (settings,
 *   data or bake on load)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_has_navigation (LrgLevel *self);

/**
 * lrg_level_clear_navigation:
 * @self: an #LrgLevel
 *
 * Drops the whole `navigation:` block.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_clear_navigation (LrgLevel *self);

/**
 * lrg_level_get_audio_mixer:
 * @self: an #LrgLevel
 *
 * Returns: (nullable): the `audio: mixer:` path, or %NULL when the
 *   level names no mixer
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_level_get_audio_mixer (LrgLevel *self);

/**
 * lrg_level_set_audio_mixer:
 * @self: an #LrgLevel
 * @path: (nullable): a `.rmixer` path or `guid:` reference, or %NULL
 *   to drop the block
 *
 * The mixer is loaded when the level is instantiated. A level with no
 * mixer leaves the audio manager's current mixer in place.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_set_audio_mixer (LrgLevel    *self,
                                const gchar *path);

G_END_DECLS
