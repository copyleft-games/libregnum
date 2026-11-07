/* lrg-broadphase.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBroadphase - a dynamic AABB tree for 3D physics. Proxies (one per
 * body) carry a tight AABB, a fat AABB stored in the tree, an object
 * layer, a per-proxy collide mask and a caller id. Pair finding reports
 * every pair of tight AABBs that overlap, at least one proxy active,
 * that the layers allow, once each and in a deterministic order.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-physics-layers.h"

G_BEGIN_DECLS

/**
 * LRG_BROADPHASE_NULL_PROXY:
 *
 * The proxy id that names no proxy. lrg_broadphase_create_proxy()
 * never returns it.
 */
#define LRG_BROADPHASE_NULL_PROXY (G_MAXUINT)

/**
 * LRG_BROADPHASE_DEFAULT_MARGIN:
 *
 * Default fat AABB margin in world units, added on every side of the
 * tight AABB when a proxy is (re)inserted into the tree.
 */
#define LRG_BROADPHASE_DEFAULT_MARGIN (0.1f)

/**
 * LrgBroadphaseProxyFlags:
 * @LRG_BROADPHASE_PROXY_NONE: an active, movable proxy
 * @LRG_BROADPHASE_PROXY_STATIC: the proxy does not move (level geometry);
 *   two static proxies never form a pair
 * @LRG_BROADPHASE_PROXY_SLEEPING: the proxy is asleep; two proxies that
 *   are each static or sleeping never form a pair
 *
 * Flags that decide whether a proxy is active. A proxy is active when
 * it has neither flag; lrg_broadphase_find_pairs() only queries from
 * active proxies, so its cost scales with the active count.
 */
typedef enum /*< flags >*/
{
	LRG_BROADPHASE_PROXY_NONE     = 0,
	LRG_BROADPHASE_PROXY_STATIC   = 1 << 0,
	LRG_BROADPHASE_PROXY_SLEEPING = 1 << 1
} LrgBroadphaseProxyFlags;

LRG_AVAILABLE_IN_ALL
GType lrg_broadphase_proxy_flags_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BROADPHASE_PROXY_FLAGS (lrg_broadphase_proxy_flags_get_type ())

/**
 * LrgBroadphaseStats:
 * @proxy_count: live proxies
 * @active_count: proxies with neither %LRG_BROADPHASE_PROXY_STATIC nor
 *   %LRG_BROADPHASE_PROXY_SLEEPING
 * @node_count: tree nodes in use (2 * proxies - 1 when non-empty)
 * @height: tree height (a single leaf has height 0)
 * @max_balance: largest height difference between two siblings
 * @area_ratio: sum of internal node surface areas divided by the root
 *   surface area (lower is a better tree)
 * @moves: lrg_broadphase_move_proxy() calls since the last reset
 * @reinserts: moves that left the fat AABB and re-inserted the leaf
 * @rotations: balancing rotations performed since the last reset
 * @pair_count: pairs reported by the last lrg_broadphase_find_pairs()
 * @pair_tests: tight AABB tests run by the last
 *   lrg_broadphase_find_pairs()
 *
 * Counters filled by lrg_broadphase_get_stats(). The move, reinsert and
 * rotation counters accumulate until lrg_broadphase_reset_counters().
 */
typedef struct _LrgBroadphaseStats LrgBroadphaseStats;

struct _LrgBroadphaseStats
{
	guint   proxy_count;
	guint   active_count;
	guint   node_count;
	guint   height;
	guint   max_balance;
	gfloat  area_ratio;
	guint64 moves;
	guint64 reinserts;
	guint64 rotations;
	guint   pair_count;
	guint64 pair_tests;
};

LRG_AVAILABLE_IN_ALL
GType lrg_broadphase_stats_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BROADPHASE_STATS (lrg_broadphase_stats_get_type ())

/**
 * lrg_broadphase_stats_copy:
 * @stats: an #LrgBroadphaseStats
 *
 * Returns: (transfer full): a heap copy of @stats
 */
LRG_AVAILABLE_IN_ALL
LrgBroadphaseStats * lrg_broadphase_stats_copy (const LrgBroadphaseStats *stats);

/**
 * lrg_broadphase_stats_free:
 * @stats: (transfer full): a heap #LrgBroadphaseStats
 *
 * Frees a copy made by lrg_broadphase_stats_copy().
 */
LRG_AVAILABLE_IN_ALL
void lrg_broadphase_stats_free (LrgBroadphaseStats *stats);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgBroadphaseStats, lrg_broadphase_stats_free)

/**
 * LrgBroadphasePairFunc:
 * @proxy_a: the lower proxy id of the pair
 * @user_a: the caller id of @proxy_a
 * @proxy_b: the higher proxy id of the pair
 * @user_b: the caller id of @proxy_b
 * @user_data: (closure): data passed to lrg_broadphase_find_pairs()
 *
 * Receives one overlapping pair. @proxy_a is always lower than
 * @proxy_b. Do not create, destroy or move proxies from inside it.
 */
typedef void (*LrgBroadphasePairFunc) (guint    proxy_a,
                                       guint    user_a,
                                       guint    proxy_b,
                                       guint    user_b,
                                       gpointer user_data);

/**
 * LrgBroadphaseQueryFunc:
 * @proxy: a proxy whose tight AABB passed the query
 * @user_id: the caller id of @proxy
 * @user_data: (closure): data passed to the query
 *
 * Receives one query hit. Do not create, destroy or move proxies from
 * inside it.
 *
 * Returns: %TRUE to keep searching, %FALSE to stop the query
 */
typedef gboolean (*LrgBroadphaseQueryFunc) (guint    proxy,
                                            guint    user_id,
                                            gpointer user_data);

/**
 * LrgBroadphaseRayFunc:
 * @proxy: a proxy whose tight AABB the ray (or swept box) enters
 * @user_id: the caller id of @proxy
 * @t_enter: the distance along the ray at which the AABB is entered
 *   (0 when the origin is inside)
 * @user_data: (closure): data passed to the cast
 *
 * Receives one candidate of a ray or box cast. Return the new maximum
 * distance: return the current maximum to keep searching unchanged, a
 * closer hit distance to clip the rest of the cast (closest-hit
 * search), or a negative value (or 0) to stop.
 *
 * Returns: the new maximum distance of the cast
 */
typedef gfloat (*LrgBroadphaseRayFunc) (guint    proxy,
                                        guint    user_id,
                                        gfloat   t_enter,
                                        gpointer user_data);

/**
 * LrgBroadphaseNodeFunc:
 * @depth: depth of the node (the root is 0)
 * @min: (array fixed-size=3): fat AABB minimum corner of the node
 * @max: (array fixed-size=3): fat AABB maximum corner of the node
 * @proxy: the proxy of a leaf, %LRG_BROADPHASE_NULL_PROXY for an
 *   internal node
 * @user_data: (closure): data passed to lrg_broadphase_foreach_node()
 *
 * Receives one tree node, for debug drawing.
 */
typedef void (*LrgBroadphaseNodeFunc) (guint         depth,
                                       const gfloat *min,
                                       const gfloat *max,
                                       guint         proxy,
                                       gpointer      user_data);

#define LRG_TYPE_BROADPHASE (lrg_broadphase_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBroadphase, lrg_broadphase, LRG, BROADPHASE, GObject)

/**
 * lrg_broadphase_new:
 *
 * Creates an empty tree with a fat margin of
 * %LRG_BROADPHASE_DEFAULT_MARGIN, a velocity factor of 2 and no layer
 * matrix (only the per-proxy masks filter pairs).
 *
 * Returns: (transfer full): a new #LrgBroadphase
 */
LRG_AVAILABLE_IN_ALL
LrgBroadphase * lrg_broadphase_new (void);

/**
 * lrg_broadphase_set_margin:
 * @self: an #LrgBroadphase
 * @margin: fat AABB margin in world units, >= 0
 *
 * Sets the margin added on every side of a tight AABB when a leaf is
 * (re)inserted. A larger margin means fewer re-inserts for jittering
 * bodies and looser query pruning. Existing leaves keep their fat AABB
 * until they next re-insert.
 */
LRG_AVAILABLE_IN_ALL
void lrg_broadphase_set_margin (LrgBroadphase *self,
                                gfloat         margin);

/**
 * lrg_broadphase_get_margin:
 * @self: an #LrgBroadphase
 *
 * Returns: the fat AABB margin
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_broadphase_get_margin (LrgBroadphase *self);

/**
 * lrg_broadphase_set_velocity_factor:
 * @self: an #LrgBroadphase
 * @factor: multiplier on the displacement passed to
 *   lrg_broadphase_move_proxy(), >= 0
 *
 * Sets how far ahead a moving leaf's fat AABB is stretched along its
 * displacement when it re-inserts, so a body moving steadily does not
 * re-insert every step.
 */
LRG_AVAILABLE_IN_ALL
void lrg_broadphase_set_velocity_factor (LrgBroadphase *self,
                                         gfloat         factor);

/**
 * lrg_broadphase_get_velocity_factor:
 * @self: an #LrgBroadphase
 *
 * Returns: the displacement multiplier for fat AABBs
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_broadphase_get_velocity_factor (LrgBroadphase *self);

/**
 * lrg_broadphase_set_layers:
 * @self: an #LrgBroadphase
 * @layers: (nullable): the layer matrix, or %NULL for none
 *
 * Sets the collide matrix consulted for every pair and query (the tree
 * keeps a reference and reads it live, so later matrix edits apply on
 * the next lrg_broadphase_find_pairs()). With %NULL every layer pair
 * collides and only the per-proxy masks filter.
 */
LRG_AVAILABLE_IN_ALL
void lrg_broadphase_set_layers (LrgBroadphase    *self,
                                LrgPhysicsLayers *layers);

/**
 * lrg_broadphase_get_layers:
 * @self: an #LrgBroadphase
 *
 * Returns: (transfer none) (nullable): the layer matrix, if set
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsLayers * lrg_broadphase_get_layers (LrgBroadphase *self);

/**
 * lrg_broadphase_create_proxy:
 * @self: an #LrgBroadphase
 * @min: (array fixed-size=3): tight AABB minimum corner
 * @max: (array fixed-size=3): tight AABB maximum corner
 * @layer: object layer index, below %LRG_PHYSICS_LAYERS_MAX
 * @mask: layers this proxy may pair with (one bit per layer;
 *   %LRG_PHYSICS_LAYERS_ALL for every layer)
 * @flags: initial #LrgBroadphaseProxyFlags
 * @user_id: caller id handed back in callbacks (e.g. a body id)
 *
 * Inserts a proxy. Ids are the lowest free slot, so the same sequence of
 * creates and destroys gives the same ids and the same tree.
 *
 * Returns: the new proxy id, or %LRG_BROADPHASE_NULL_PROXY when the
 *   AABB is not finite, inverted, or @layer is out of range
 */
LRG_AVAILABLE_IN_ALL
guint lrg_broadphase_create_proxy (LrgBroadphase           *self,
                                   const gfloat            *min,
                                   const gfloat            *max,
                                   guint                    layer,
                                   guint32                  mask,
                                   LrgBroadphaseProxyFlags  flags,
                                   guint                    user_id);

/**
 * lrg_broadphase_destroy_proxy:
 * @self: an #LrgBroadphase
 * @proxy: a proxy id
 *
 * Removes a proxy from the tree and frees its id for reuse.
 *
 * Returns: %TRUE if @proxy was live
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_broadphase_destroy_proxy (LrgBroadphase *self,
                                       guint          proxy);

/**
 * lrg_broadphase_move_proxy:
 * @self: an #LrgBroadphase
 * @proxy: a proxy id
 * @min: (array fixed-size=3): new tight AABB minimum corner
 * @max: (array fixed-size=3): new tight AABB maximum corner
 * @displacement: (array fixed-size=3) (nullable): this step's motion,
 *   used to stretch the fat AABB ahead of the body, or %NULL
 *
 * Updates the tight AABB. While it stays inside the stored fat AABB this
 * is O(1); otherwise the leaf is removed and re-inserted with a new fat
 * AABB (margin plus velocity-factor * displacement).
 *
 * Returns: %TRUE if the leaf was re-inserted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_broadphase_move_proxy (LrgBroadphase *self,
                                    guint          proxy,
                                    const gfloat  *min,
                                    const gfloat  *max,
                                    const gfloat  *displacement);

/**
 * lrg_broadphase_is_proxy_valid:
 * @self: an #LrgBroadphase
 * @proxy: a proxy id
 *
 * Returns: %TRUE if @proxy names a live proxy
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_broadphase_is_proxy_valid (LrgBroadphase *self,
                                        guint          proxy);

/**
 * lrg_broadphase_get_proxy_count:
 * @self: an #LrgBroadphase
 *
 * Returns: the number of live proxies
 */
LRG_AVAILABLE_IN_ALL
guint lrg_broadphase_get_proxy_count (LrgBroadphase *self);

/**
 * lrg_broadphase_set_proxy_layer:
 * @self: an #LrgBroadphase
 * @proxy: a proxy id
 * @layer: object layer index, below %LRG_PHYSICS_LAYERS_MAX
 * @mask: layers this proxy may pair with
 *
 * Changes the filter data of a proxy. Takes effect on the next pair
 * search or query.
 *
 * Returns: %TRUE on success, %FALSE for a dead proxy or bad layer
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_broadphase_set_proxy_layer (LrgBroadphase *self,
                                         guint          proxy,
                                         guint          layer,
                                         guint32        mask);

/**
 * lrg_broadphase_get_proxy_layer:
 * @self: an #LrgBroadphase
 * @proxy: a proxy id
 *
 * Returns: the object layer of @proxy (0 for a dead proxy)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_broadphase_get_proxy_layer (LrgBroadphase *self,
                                      guint          proxy);

/**
 * lrg_broadphase_get_proxy_mask:
 * @self: an #LrgBroadphase
 * @proxy: a proxy id
 *
 * Returns: the collide mask of @proxy (0 for a dead proxy)
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_broadphase_get_proxy_mask (LrgBroadphase *self,
                                       guint          proxy);

/**
 * lrg_broadphase_set_proxy_flags:
 * @self: an #LrgBroadphase
 * @proxy: a proxy id
 * @flags: new #LrgBroadphaseProxyFlags
 *
 * Marks a proxy static, sleeping or active (e.g. when its body falls
 * asleep or wakes up).
 *
 * Returns: %TRUE on success, %FALSE for a dead proxy
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_broadphase_set_proxy_flags (LrgBroadphase           *self,
                                         guint                    proxy,
                                         LrgBroadphaseProxyFlags  flags);

/**
 * lrg_broadphase_get_proxy_flags:
 * @self: an #LrgBroadphase
 * @proxy: a proxy id
 *
 * Returns: the flags of @proxy (%LRG_BROADPHASE_PROXY_NONE for a dead
 *   proxy)
 */
LRG_AVAILABLE_IN_ALL
LrgBroadphaseProxyFlags lrg_broadphase_get_proxy_flags (LrgBroadphase *self,
                                                        guint          proxy);

/**
 * lrg_broadphase_get_proxy_user_id:
 * @self: an #LrgBroadphase
 * @proxy: a proxy id
 *
 * Returns: the caller id of @proxy (G_MAXUINT for a dead proxy)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_broadphase_get_proxy_user_id (LrgBroadphase *self,
                                        guint          proxy);

/**
 * lrg_broadphase_set_proxy_user_id:
 * @self: an #LrgBroadphase
 * @proxy: a proxy id
 * @user_id: the new caller id
 *
 * Returns: %TRUE on success, %FALSE for a dead proxy
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_broadphase_set_proxy_user_id (LrgBroadphase *self,
                                           guint          proxy,
                                           guint          user_id);

/**
 * lrg_broadphase_get_proxy_aabb:
 * @self: an #LrgBroadphase
 * @proxy: a proxy id
 * @out_min: (out caller-allocates) (array fixed-size=3): tight minimum
 * @out_max: (out caller-allocates) (array fixed-size=3): tight maximum
 *
 * Returns: %TRUE if @proxy is live and the corners were written
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_broadphase_get_proxy_aabb (LrgBroadphase *self,
                                        guint          proxy,
                                        gfloat        *out_min,
                                        gfloat        *out_max);

/**
 * lrg_broadphase_get_fat_aabb:
 * @self: an #LrgBroadphase
 * @proxy: a proxy id
 * @out_min: (out caller-allocates) (array fixed-size=3): fat minimum
 * @out_max: (out caller-allocates) (array fixed-size=3): fat maximum
 *
 * Returns: %TRUE if @proxy is live and the corners were written
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_broadphase_get_fat_aabb (LrgBroadphase *self,
                                      guint          proxy,
                                      gfloat        *out_min,
                                      gfloat        *out_max);

/**
 * lrg_broadphase_layers_allow:
 * @self: an #LrgBroadphase
 * @proxy_a: a proxy id
 * @proxy_b: another proxy id
 *
 * Applies the pair filter without the AABB test: both masks accept the
 * other layer and the layer matrix (if set) lets the two layers
 * collide. The active-flag rule is not part of it.
 *
 * Returns: %TRUE if the two proxies may pair
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_broadphase_layers_allow (LrgBroadphase *self,
                                      guint          proxy_a,
                                      guint          proxy_b);

/**
 * lrg_broadphase_find_pairs:
 * @self: an #LrgBroadphase
 * @func: (scope call) (closure user_data): receives each pair
 * @user_data: data for @func
 *
 * Reports every pair of live proxies whose tight AABBs overlap
 * (touching counts), where at least one proxy is active and the pair
 * filter (lrg_broadphase_layers_allow()) passes. Each pair is reported
 * once, sorted by (lower id, higher id), so the order depends only on
 * the ids and the AABBs.
 *
 * Returns: the number of pairs reported
 */
LRG_AVAILABLE_IN_ALL
guint lrg_broadphase_find_pairs (LrgBroadphase         *self,
                                 LrgBroadphasePairFunc  func,
                                 gpointer               user_data);

/**
 * lrg_broadphase_query_aabb:
 * @self: an #LrgBroadphase
 * @min: (array fixed-size=3): box minimum corner
 * @max: (array fixed-size=3): box maximum corner
 * @layer_mask: layers to report (one bit per layer)
 * @func: (scope call) (closure user_data): receives each proxy whose tight AABB overlaps
 * @user_data: data for @func
 *
 * Box query. Static and sleeping proxies are reported too.
 *
 * Returns: the number of proxies reported
 */
LRG_AVAILABLE_IN_ALL
guint lrg_broadphase_query_aabb (LrgBroadphase          *self,
                                 const gfloat           *min,
                                 const gfloat           *max,
                                 guint32                 layer_mask,
                                 LrgBroadphaseQueryFunc  func,
                                 gpointer                user_data);

/**
 * lrg_broadphase_query_sphere:
 * @self: an #LrgBroadphase
 * @center: (array fixed-size=3): sphere center
 * @radius: sphere radius, >= 0
 * @layer_mask: layers to report
 * @func: (scope call) (closure user_data): receives each proxy whose tight AABB touches the
 *   sphere
 * @user_data: data for @func
 *
 * Sphere query (exact sphere against AABB distance test).
 *
 * Returns: the number of proxies reported
 */
LRG_AVAILABLE_IN_ALL
guint lrg_broadphase_query_sphere (LrgBroadphase          *self,
                                   const gfloat           *center,
                                   gfloat                  radius,
                                   guint32                 layer_mask,
                                   LrgBroadphaseQueryFunc  func,
                                   gpointer                user_data);

/**
 * lrg_broadphase_query_frustum:
 * @self: an #LrgBroadphase
 * @planes: (array fixed-size=24): six planes (a, b, c, d) whose normals
 *   point into the frustum: a point p is inside when
 *   a*p.x + b*p.y + c*p.z + d >= 0 for all six
 * @layer_mask: layers to report
 * @func: (scope call) (closure user_data): receives each proxy whose tight AABB is not
 *   fully outside a plane
 * @user_data: data for @func
 *
 * Frustum (or any convex six-plane volume) query. Conservative like
 * every plane-AABB culling test: a box near a frustum corner can be
 * reported though it is outside.
 *
 * Returns: the number of proxies reported
 */
LRG_AVAILABLE_IN_ALL
guint lrg_broadphase_query_frustum (LrgBroadphase          *self,
                                    const gfloat           *planes,
                                    guint32                 layer_mask,
                                    LrgBroadphaseQueryFunc  func,
                                    gpointer                user_data);

/**
 * lrg_broadphase_cast_ray:
 * @self: an #LrgBroadphase
 * @origin: (array fixed-size=3): ray origin
 * @direction: (array fixed-size=3): ray direction (need not be unit;
 *   distances are in multiples of its length)
 * @max_distance: largest distance to search, > 0
 * @layer_mask: layers to report
 * @func: (scope call) (closure user_data): receives candidates; returns the new maximum
 * @user_data: data for @func
 *
 * Ray query against tight AABBs, pruned with the current maximum, so
 * a closest-hit search that returns each hit's exact distance visits
 * few leaves.
 *
 * Returns: the final maximum distance (equal to @max_distance when
 *   nothing clipped it; <= 0 when @func stopped the cast)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_broadphase_cast_ray (LrgBroadphase        *self,
                                const gfloat         *origin,
                                const gfloat         *direction,
                                gfloat                max_distance,
                                guint32               layer_mask,
                                LrgBroadphaseRayFunc  func,
                                gpointer              user_data);

/**
 * lrg_broadphase_cast_aabb:
 * @self: an #LrgBroadphase
 * @min: (array fixed-size=3): start box minimum corner
 * @max: (array fixed-size=3): start box maximum corner
 * @direction: (array fixed-size=3): sweep direction
 * @max_distance: largest distance to sweep, > 0
 * @layer_mask: layers to report
 * @func: (scope call) (closure user_data): receives candidates; returns the new maximum
 * @user_data: data for @func
 *
 * Swept box query (for shape casts and CCD): reports every proxy whose
 * tight AABB the moving box touches, with the distance of first touch.
 *
 * Returns: the final maximum distance, as lrg_broadphase_cast_ray()
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_broadphase_cast_aabb (LrgBroadphase        *self,
                                 const gfloat         *min,
                                 const gfloat         *max,
                                 const gfloat         *direction,
                                 gfloat                max_distance,
                                 guint32               layer_mask,
                                 LrgBroadphaseRayFunc  func,
                                 gpointer              user_data);

/**
 * lrg_broadphase_foreach_node:
 * @self: an #LrgBroadphase
 * @func: (scope call) (closure user_data): receives each node, depth first
 * @user_data: data for @func
 *
 * Walks the tree for debug drawing. The order is deterministic.
 */
LRG_AVAILABLE_IN_ALL
void lrg_broadphase_foreach_node (LrgBroadphase         *self,
                                  LrgBroadphaseNodeFunc  func,
                                  gpointer               user_data);

/**
 * lrg_broadphase_get_height:
 * @self: an #LrgBroadphase
 *
 * Returns: the tree height (0 for one leaf or an empty tree)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_broadphase_get_height (LrgBroadphase *self);

/**
 * lrg_broadphase_get_stats:
 * @self: an #LrgBroadphase
 * @out_stats: (out caller-allocates): receives the counters
 *
 * Fills @out_stats. Walks the tree (O(n)) for the shape fields.
 */
LRG_AVAILABLE_IN_ALL
void lrg_broadphase_get_stats (LrgBroadphase      *self,
                               LrgBroadphaseStats *out_stats);

/**
 * lrg_broadphase_reset_counters:
 * @self: an #LrgBroadphase
 *
 * Zeroes the move, reinsert and rotation counters.
 */
LRG_AVAILABLE_IN_ALL
void lrg_broadphase_reset_counters (LrgBroadphase *self);

/**
 * lrg_broadphase_validate:
 * @self: an #LrgBroadphase
 *
 * Checks every invariant (parent links, heights, enclosing boxes, leaf
 * count, free list). For tests and debugging; O(n).
 *
 * Returns: %TRUE if the tree is consistent
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_broadphase_validate (LrgBroadphase *self);

/**
 * lrg_broadphase_rebuild:
 * @self: an #LrgBroadphase
 *
 * Rebuilds the whole tree top-down (median split on the longest axis of
 * the leaf centers), giving a well balanced tree after a bulk load of
 * level geometry. Proxy ids, AABBs and flags are unchanged.
 */
LRG_AVAILABLE_IN_ALL
void lrg_broadphase_rebuild (LrgBroadphase *self);

/**
 * lrg_broadphase_clear:
 * @self: an #LrgBroadphase
 *
 * Removes every proxy (ids restart at 0) and zeroes the counters.
 */
LRG_AVAILABLE_IN_ALL
void lrg_broadphase_clear (LrgBroadphase *self);

G_END_DECLS
