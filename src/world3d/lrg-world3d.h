/* lrg-world3d.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWorld3D: the runtime 3D world, plus the render-list contract between
 * components and renderers.
 *
 * An #LrgWorld3D owns one root #LrgEntity3D; every entity in the root's
 * subtree is "in the world".  The world keeps a GUID map, an octree of
 * entity bounds, light and camera registries and an audio listener, and
 * runs the frame phases in this order (lrg_world3d_step()):
 *
 *   1. fixed_update (0..max-substeps times, fixed-step seconds each)
 *   2. update (once, variable dt)
 *   3. late_update (once, variable dt)
 *   4. collect_render (per camera / pass) -> #LrgRenderList -> renderer
 *
 * #LrgRenderList is the hand-off to a renderer (LrgWorld3DRenderer): a
 * flat array of #LrgRenderItem records split into opaque, cutout,
 * transparent and shadow-caster categories, plus the lights visible to
 * the camera.  A list is built once per camera and can be replayed for
 * every pass (depth prepass, shadow maps, colour) with
 * lrg_render_list_foreach().
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-bounding-box3d.h"
#include "lrg-entity3d.h"
#include "../lighting/lrg-environment.h"
#include "../lighting/lrg-lightmap3d.h"
#include "../lighting/lrg-probe-grid.h"
#include "../postprocess/lrg-post-stack.h"
#include "../physics/lrg-dynamics.h"
#include "../pathfinding/lrg-nav-poly-mesh.h"
#include "../pathfinding/lrg-nav-agent.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Enumerations
 * ========================================================================== */

/**
 * LrgRenderItemKind:
 * @LRG_RENDER_ITEM_PRIMITIVE: a built-in shape (#LrgRenderItem.primitive
 *   and #LrgRenderItem.params)
 * @LRG_RENDER_ITEM_MESH: a single mesh (#LrgRenderItem.mesh, a GrlMesh)
 * @LRG_RENDER_ITEM_MODEL: a model (#LrgRenderItem.model, a GrlModel)
 * @LRG_RENDER_ITEM_BILLBOARD: a camera-facing quad (#LrgRenderItem.texture,
 *   a GrlTexture, size in params[0..1])
 * @LRG_RENDER_ITEM_CUSTOM: drawn by #LrgRenderItem.draw_func
 *
 * What a render item draws.
 */
typedef enum
{
	LRG_RENDER_ITEM_PRIMITIVE,
	LRG_RENDER_ITEM_MESH,
	LRG_RENDER_ITEM_MODEL,
	LRG_RENDER_ITEM_BILLBOARD,
	LRG_RENDER_ITEM_CUSTOM
} LrgRenderItemKind;

LRG_AVAILABLE_IN_ALL
GType lrg_render_item_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_RENDER_ITEM_KIND (lrg_render_item_kind_get_type ())

/**
 * LrgRenderItemFlags:
 * @LRG_RENDER_ITEM_FLAG_NONE: no flags (opaque, no shadows)
 * @LRG_RENDER_ITEM_FLAG_CAST_SHADOW: also listed as a shadow caster
 * @LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW: lit with shadow lookups
 * @LRG_RENDER_ITEM_FLAG_TRANSPARENT: alpha blended (sorted back to front)
 * @LRG_RENDER_ITEM_FLAG_CUTOUT: alpha tested
 * @LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED: no back-face culling
 * @LRG_RENDER_ITEM_FLAG_UNLIT: no lighting
 * @LRG_RENDER_ITEM_FLAG_NO_CULL: never frustum culled
 *
 * Render state flags of a render item.  TRANSPARENT wins over CUTOUT when
 * both are set.
 */
typedef enum /*< flags >*/
{
	LRG_RENDER_ITEM_FLAG_NONE           = 0,
	LRG_RENDER_ITEM_FLAG_CAST_SHADOW    = 1 << 0,
	LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW = 1 << 1,
	LRG_RENDER_ITEM_FLAG_TRANSPARENT    = 1 << 2,
	LRG_RENDER_ITEM_FLAG_CUTOUT         = 1 << 3,
	LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED   = 1 << 4,
	LRG_RENDER_ITEM_FLAG_UNLIT          = 1 << 5,
	LRG_RENDER_ITEM_FLAG_NO_CULL        = 1 << 6
} LrgRenderItemFlags;

LRG_AVAILABLE_IN_ALL
GType lrg_render_item_flags_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_RENDER_ITEM_FLAGS (lrg_render_item_flags_get_type ())

/**
 * LrgRenderCategory:
 * @LRG_RENDER_CATEGORY_OPAQUE: opaque items (front to back)
 * @LRG_RENDER_CATEGORY_CUTOUT: alpha-tested items (front to back)
 * @LRG_RENDER_CATEGORY_TRANSPARENT: blended items (back to front)
 * @LRG_RENDER_CATEGORY_SHADOW_CASTER: items with
 *   %LRG_RENDER_ITEM_FLAG_CAST_SHADOW, not frustum culled against the
 *   camera (they may shadow visible geometry from off screen)
 *
 * The lists of an #LrgRenderList.  An item is in exactly one of the first
 * three and additionally in SHADOW_CASTER when it casts shadows.
 */
typedef enum
{
	LRG_RENDER_CATEGORY_OPAQUE,
	LRG_RENDER_CATEGORY_CUTOUT,
	LRG_RENDER_CATEGORY_TRANSPARENT,
	LRG_RENDER_CATEGORY_SHADOW_CASTER
} LrgRenderCategory;

LRG_AVAILABLE_IN_ALL
GType lrg_render_category_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_RENDER_CATEGORY (lrg_render_category_get_type ())

/**
 * LRG_RENDER_CATEGORY_COUNT:
 *
 * The number of #LrgRenderCategory values.
 */
#define LRG_RENDER_CATEGORY_COUNT (4)

/**
 * LrgLight3DKind:
 * @LRG_LIGHT3D_DIRECTIONAL: sun-like light along #LrgLight3DDesc.direction
 * @LRG_LIGHT3D_POINT: omni light at #LrgLight3DDesc.position
 * @LRG_LIGHT3D_SPOT: cone light at position along direction
 *
 * The kind of a runtime light.
 */
typedef enum
{
	LRG_LIGHT3D_DIRECTIONAL,
	LRG_LIGHT3D_POINT,
	LRG_LIGHT3D_SPOT
} LrgLight3DKind;

LRG_AVAILABLE_IN_ALL
GType lrg_light3d_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LIGHT3D_KIND (lrg_light3d_kind_get_type ())

/* ==========================================================================
 * LrgLight3DDesc
 * ========================================================================== */

typedef struct _LrgLight3DDesc LrgLight3DDesc;

/**
 * LrgLight3DDesc:
 * @kind: directional, point or spot
 * @color: linear RGB colour, 0..1 per channel
 * @intensity: multiplier on @color
 * @range: attenuation range in world units (point/spot)
 * @inner_angle: spot inner cone half-angle in radians (full intensity)
 * @outer_angle: spot outer cone half-angle in radians (falls to zero)
 * @cast_shadows: whether the renderer should render a shadow map
 * @enabled: whether the light contributes at all
 * @position: world-space position (point/spot)
 * @direction: unit world-space direction the light travels
 *   (directional/spot), normally the owner entity's forward (-Z)
 * @layer_mask: render layers the light illuminates
 *   (%LRG_RENDER_LAYERS_MASK_ALL by default)
 * @source: (nullable): the object that registered the light (normally a
 *   light component); not owned
 * @bake_mode: how the light takes part in baked lighting (since 0.9.0;
 *   %LRG_LIGHT_MODE_REALTIME by default)
 *
 * The renderer-facing description of one light.  Light components fill
 * it from their properties and their entity's world transform and push it
 * with lrg_world3d_register_light(); renderers read it from
 * lrg_world3d_get_lights() or lrg_render_list_get_light().
 */
struct _LrgLight3DDesc
{
	LrgLight3DKind  kind;
	gfloat          color[3];
	gfloat          intensity;
	gfloat          range;
	gfloat          inner_angle;
	gfloat          outer_angle;
	gboolean        cast_shadows;
	gboolean        enabled;
	gfloat          position[3];
	gfloat          direction[3];
	guint32         layer_mask;
	GObject        *source;
	LrgLightMode    bake_mode;

	/*< private >*/
	gpointer _reserved[3];
};

LRG_AVAILABLE_IN_ALL
GType lrg_light3d_desc_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LIGHT3D_DESC (lrg_light3d_desc_get_type ())

/**
 * lrg_light3d_desc_init:
 * @desc: (out caller-allocates): a light description
 *
 * Fills @desc with defaults: an enabled white point light, intensity 1,
 * range 10, cone 30/45 degrees, no shadows, at the origin, direction
 * (0, 0, -1), all layers, no source.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light3d_desc_init (LrgLight3DDesc *desc);

/**
 * lrg_light3d_desc_new:
 *
 * Returns: (transfer full): a heap description initialised with
 *   lrg_light3d_desc_init()
 */
LRG_AVAILABLE_IN_ALL
LrgLight3DDesc * lrg_light3d_desc_new (void);

/**
 * lrg_light3d_desc_copy:
 * @desc: a light description
 *
 * Returns: (transfer full): a copy of @desc
 */
LRG_AVAILABLE_IN_ALL
LrgLight3DDesc * lrg_light3d_desc_copy (const LrgLight3DDesc *desc);

/**
 * lrg_light3d_desc_free:
 * @desc: (nullable): a heap description
 *
 * Frees a description from lrg_light3d_desc_new() or
 * lrg_light3d_desc_copy().
 */
LRG_AVAILABLE_IN_ALL
void lrg_light3d_desc_free (LrgLight3DDesc *desc);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgLight3DDesc, lrg_light3d_desc_free)

/* ==========================================================================
 * LrgRenderItem
 * ========================================================================== */

typedef struct _LrgRenderItem LrgRenderItem;

/**
 * LrgRenderItemDrawFunc:
 * @item: the item being drawn
 * @category: the list the item is being replayed from
 * @pass_data: (nullable): renderer-defined data for the current pass
 *
 * Draw callback of %LRG_RENDER_ITEM_CUSTOM items.  Called by the renderer
 * inside its 3D mode with its shader state bound; @item->user_data holds
 * the component's data.
 */
typedef void (*LrgRenderItemDrawFunc) (const LrgRenderItem *item,
                                       LrgRenderCategory    category,
                                       gpointer             pass_data);

/**
 * LrgRenderItem:
 * @kind: what to draw
 * @flags: render state flags
 * @layer: render layer index of the item (normally the entity's)
 * @primitive: shape for %LRG_RENDER_ITEM_PRIMITIVE
 * @params: shape parameters (cube: width, height, depth; sphere: radius;
 *   cylinder/cone: radius, height; plane: width, depth; billboard: width,
 *   height); unused entries are 0
 * @mesh: (nullable): a GrlMesh for %LRG_RENDER_ITEM_MESH; borrowed
 * @model: (nullable): a GrlModel for %LRG_RENDER_ITEM_MODEL; borrowed
 * @texture: (nullable): a GrlTexture (billboard image or albedo override);
 *   borrowed
 * @material: (nullable): an LrgMaterial3D (and, from 0.9 M2, an
 *   LrgMaterial); borrowed
 * @mesh_index: model mesh to draw, or -1 for the whole model
 * @color: RGBA tint, 0..1
 * @world_matrix: world matrix (column-major) to draw with
 * @bounds: world-space AABB; min > max on any axis means "unknown, never
 *   culled"
 * @sort_key: material/state key; opaque and cutout lists sort by it first
 *   (lower first), then front to back
 * @view_depth: distance to the camera, filled by lrg_render_list_sort()
 * @entity: (nullable): the entity that produced the item; not owned
 * @component: (nullable): the component that produced the item; not owned
 * @draw_func: (nullable): draw callback for %LRG_RENDER_ITEM_CUSTOM
 * @user_data: (nullable): data for @draw_func
 * @user_data_destroy: (nullable): frees @user_data when the list is
 *   cleared or freed (the list takes ownership on lrg_render_list_add())
 *
 * One draw submitted by a component's collect_render() vfunc.  Every
 * pointer handle (mesh, model, texture, material) is borrowed: the
 * component must keep it alive until the list is cleared, which is always
 * true when the component owns it and the list lives for one frame.
 */
struct _LrgRenderItem
{
	LrgRenderItemKind      kind;
	LrgRenderItemFlags     flags;
	guint                  layer;
	LrgPrimitiveType       primitive;
	gfloat                 params[4];
	gpointer               mesh;
	gpointer               model;
	gpointer               texture;
	gpointer               material;
	gint                   mesh_index;
	gfloat                 color[4];
	gfloat                 world_matrix[16];
	LrgBoundingBox3D       bounds;
	guint64                sort_key;
	gfloat                 view_depth;
	LrgEntity3D           *entity;
	LrgComponent          *component;
	LrgRenderItemDrawFunc  draw_func;
	gpointer               user_data;
	GDestroyNotify         user_data_destroy;

	/*< private >*/
	gpointer _reserved[4];
};

/**
 * lrg_render_item_init:
 * @item: (out caller-allocates): a render item
 *
 * Fills @item with defaults: an opaque white unit cube primitive that
 * receives and casts shadows, identity matrix, unknown bounds, layer 0,
 * mesh_index -1, everything else zero/%NULL.
 */
LRG_AVAILABLE_IN_ALL
void lrg_render_item_init (LrgRenderItem *item);

/**
 * lrg_render_item_init_for_entity:
 * @item: (out caller-allocates): a render item
 * @entity: the producing entity
 * @component: (nullable): the producing component
 *
 * Like lrg_render_item_init(), then copies @entity's world matrix and
 * layer and sets @entity/@component.  This is what a collect_render()
 * implementation normally starts from.
 */
LRG_AVAILABLE_IN_ALL
void lrg_render_item_init_for_entity (LrgRenderItem *item,
                                      LrgEntity3D   *entity,
                                      LrgComponent  *component);

/**
 * lrg_render_item_set_local_bounds:
 * @item: a render item whose world_matrix is set
 * @local_bounds: bounds in the item's model space
 *
 * Sets @item->bounds to the world AABB of @local_bounds transformed by
 * @item->world_matrix.
 */
LRG_AVAILABLE_IN_ALL
void lrg_render_item_set_local_bounds (LrgRenderItem          *item,
                                       const LrgBoundingBox3D *local_bounds);

/**
 * lrg_render_item_get_category:
 * @item: a render item
 *
 * Returns: the main category (OPAQUE, CUTOUT or TRANSPARENT) of @item
 */
LRG_AVAILABLE_IN_ALL
LrgRenderCategory lrg_render_item_get_category (const LrgRenderItem *item);

/* ==========================================================================
 * LrgRenderList
 * ========================================================================== */

typedef struct _LrgRenderList LrgRenderList;

/**
 * LrgRenderList:
 *
 * A reference-counted, opaque per-camera list of #LrgRenderItem records
 * and #LrgLight3DDesc lights.  Reuse one list per camera across frames
 * (lrg_world3d_collect_render() clears it) so its arrays stay allocated.
 */

LRG_AVAILABLE_IN_ALL
GType lrg_render_list_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_RENDER_LIST (lrg_render_list_get_type ())

/**
 * lrg_render_list_new:
 *
 * Returns: (transfer full): an empty render list
 */
LRG_AVAILABLE_IN_ALL
LrgRenderList * lrg_render_list_new (void);

/**
 * lrg_render_list_ref:
 * @self: a render list
 *
 * Returns: (transfer full): @self
 */
LRG_AVAILABLE_IN_ALL
LrgRenderList * lrg_render_list_ref (LrgRenderList *self);

/**
 * lrg_render_list_unref:
 * @self: (transfer full): a render list
 *
 * Drops a reference; the last one clears and frees the list.
 */
LRG_AVAILABLE_IN_ALL
void lrg_render_list_unref (LrgRenderList *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgRenderList, lrg_render_list_unref)

/**
 * lrg_render_list_clear:
 * @self: a render list
 *
 * Removes every item and light, calling each item's user_data_destroy.
 * The camera state is reset to "no camera" (eye at the origin, cull mask
 * all layers).
 */
LRG_AVAILABLE_IN_ALL
void lrg_render_list_clear (LrgRenderList *self);

/**
 * lrg_render_list_add:
 * @self: a render list
 * @item: the item to copy in
 *
 * Copies @item into the list and files it under its category (and under
 * SHADOW_CASTER if it casts shadows).  The list takes ownership of
 * @item->user_data (it calls user_data_destroy on clear).  No layer or
 * frustum filtering happens here; lrg_world3d_collect_render() filters
 * through lrg_render_list_accepts().
 *
 * Returns: the item's index in the list's flat item array
 */
LRG_AVAILABLE_IN_ALL
guint lrg_render_list_add (LrgRenderList       *self,
                           const LrgRenderItem *item);

/**
 * lrg_render_list_accepts:
 * @self: a render list
 * @item: a candidate item
 *
 * Tests @item against the list's camera state: the layer must be in the
 * cull mask, and unless the item has %LRG_RENDER_ITEM_FLAG_NO_CULL or
 * unknown bounds, it must intersect the view frustum (when frustum
 * culling is on and a frustum is set).  Shadow casters that fail only the
 * frustum test are still accepted (they go only into SHADOW_CASTER).
 *
 * Returns: %TRUE if lrg_world3d_collect_render() would keep @item
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_render_list_accepts (LrgRenderList       *self,
                                  const LrgRenderItem *item);

/**
 * lrg_render_list_get_n_items:
 * @self: a render list
 * @category: a category
 *
 * Returns: the number of items in @category
 */
LRG_AVAILABLE_IN_ALL
guint lrg_render_list_get_n_items (LrgRenderList     *self,
                                   LrgRenderCategory  category);

/**
 * lrg_render_list_get_item:
 * @self: a render list
 * @category: a category
 * @index: an index below lrg_render_list_get_n_items()
 *
 * Returns: (transfer none): the item; valid until the list changes
 */
LRG_AVAILABLE_IN_ALL
const LrgRenderItem * lrg_render_list_get_item (LrgRenderList     *self,
                                                LrgRenderCategory  category,
                                                guint              index);

/**
 * lrg_render_list_get_total_items:
 * @self: a render list
 *
 * Returns: the number of distinct items (an item in two categories
 *   counts once)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_render_list_get_total_items (LrgRenderList *self);

/**
 * LrgRenderItemFunc:
 * @item: an item
 * @category: the category being replayed
 * @user_data: user data
 *
 * Callback of lrg_render_list_foreach().
 */
typedef void (*LrgRenderItemFunc) (const LrgRenderItem *item,
                                   LrgRenderCategory    category,
                                   gpointer             user_data);

/**
 * lrg_render_list_foreach:
 * @self: a render list
 * @category: the category to replay
 * @func: (scope call): called for each item in list order
 * @user_data: data for @func
 *
 * Replays one category.  Call it once per pass; the list is not consumed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_render_list_foreach (LrgRenderList     *self,
                              LrgRenderCategory  category,
                              LrgRenderItemFunc  func,
                              gpointer           user_data);

/**
 * lrg_render_list_sort:
 * @self: a render list
 *
 * Fills every item's view_depth from the list's eye position, then sorts
 * OPAQUE and CUTOUT by (sort_key, depth ascending), TRANSPARENT by depth
 * descending and SHADOW_CASTER by sort_key.  Sorting is stable.
 * lrg_world3d_collect_render() calls it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_render_list_sort (LrgRenderList *self);

/**
 * lrg_render_list_set_view:
 * @self: a render list
 * @eye: (array fixed-size=3): camera position in world space
 * @cull_mask: render layers the camera sees
 * @view_projection: (nullable) (array fixed-size=16): view-projection
 *   matrix for frustum culling, or %NULL for none
 *
 * Sets the camera state used by lrg_render_list_accepts() and
 * lrg_render_list_sort().  lrg_world3d_collect_render() calls this from
 * its camera.
 */
LRG_AVAILABLE_IN_ALL
void lrg_render_list_set_view (LrgRenderList *self,
                               const gfloat  *eye,
                               guint32        cull_mask,
                               const gfloat  *view_projection);

/**
 * lrg_render_list_get_eye:
 * @self: a render list
 * @out: (out caller-allocates) (array fixed-size=3): eye position
 *
 * Gets the eye position set by lrg_render_list_set_view().
 */
LRG_AVAILABLE_IN_ALL
void lrg_render_list_get_eye (LrgRenderList *self,
                              gfloat        *out);

/**
 * lrg_render_list_get_cull_mask:
 * @self: a render list
 *
 * Returns: the cull mask set by lrg_render_list_set_view()
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_render_list_get_cull_mask (LrgRenderList *self);

/**
 * lrg_render_list_get_view_projection:
 * @self: a render list
 * @out: (out caller-allocates) (array fixed-size=16): the matrix
 *
 * Returns: %TRUE if a view-projection matrix (frustum) is set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_render_list_get_view_projection (LrgRenderList *self,
                                              gfloat        *out);

/**
 * lrg_render_list_add_light:
 * @self: a render list
 * @light: a light to copy in
 *
 * Appends a light (lrg_world3d_collect_render() adds the world's enabled
 * lights whose layer mask intersects the camera's cull mask).
 */
LRG_AVAILABLE_IN_ALL
void lrg_render_list_add_light (LrgRenderList        *self,
                                const LrgLight3DDesc *light);

/**
 * lrg_render_list_get_n_lights:
 * @self: a render list
 *
 * Returns: the number of lights
 */
LRG_AVAILABLE_IN_ALL
guint lrg_render_list_get_n_lights (LrgRenderList *self);

/**
 * lrg_render_list_get_light:
 * @self: a render list
 * @index: a light index
 *
 * Returns: (transfer none): the light; valid until the list changes
 */
LRG_AVAILABLE_IN_ALL
const LrgLight3DDesc * lrg_render_list_get_light (LrgRenderList *self,
                                                  guint          index);

/* ==========================================================================
 * LrgWorld3D
 * ========================================================================== */

#define LRG_TYPE_WORLD3D (lrg_world3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWorld3D, lrg_world3d, LRG, WORLD3D, GObject)

/**
 * lrg_world3d_new:
 *
 * Creates an empty world with a root entity named "root", fixed step
 * 1/60 s, at most 8 substeps per frame, frustum culling on, and an
 * octree covering +/-4096 units (entities outside it are still indexed:
 * #LrgOctree keeps them in its root node, and anything it rejects goes to
 * an overflow list that queries scan linearly).
 *
 * Returns: (transfer full): a new #LrgWorld3D
 */
LRG_AVAILABLE_IN_ALL
LrgWorld3D * lrg_world3d_new (void);

/* --- Entities ------------------------------------------------------------ */

/**
 * lrg_world3d_get_root:
 * @self: an #LrgWorld3D
 *
 * Gets the root entity.  Its children are the scene's top-level entities;
 * do not remove or reparent the root itself.
 *
 * Returns: (transfer none): the root entity
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_world3d_get_root (LrgWorld3D *self);

/**
 * lrg_world3d_add_entity:
 * @self: an #LrgWorld3D
 * @entity: (transfer none): an entity without a parent
 *
 * Appends @entity under the root (same as lrg_entity3d_add_child() on the
 * root).  Its subtree joins the world.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_add_entity (LrgWorld3D  *self,
                             LrgEntity3D *entity);

/**
 * lrg_world3d_remove_entity:
 * @self: an #LrgWorld3D
 * @entity: an entity in the world (not the root)
 *
 * Detaches @entity from its parent; its subtree leaves the world.
 *
 * Returns: %TRUE if @entity was in the world
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_remove_entity (LrgWorld3D  *self,
                                    LrgEntity3D *entity);

/**
 * lrg_world3d_clear:
 * @self: an #LrgWorld3D
 *
 * Removes every entity under the root.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_clear (LrgWorld3D *self);

/**
 * lrg_world3d_get_entity_count:
 * @self: an #LrgWorld3D
 *
 * Returns: the number of entities in the world, excluding the root
 */
LRG_AVAILABLE_IN_ALL
guint lrg_world3d_get_entity_count (LrgWorld3D *self);

/**
 * lrg_world3d_find_by_guid:
 * @self: an #LrgWorld3D
 * @guid: a GUID
 *
 * Looks up the GUID map (O(1)).
 *
 * Returns: (transfer none) (nullable): the entity with @guid
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_world3d_find_by_guid (LrgWorld3D  *self,
                                        const gchar *guid);

/**
 * lrg_world3d_find_by_name:
 * @self: an #LrgWorld3D
 * @name: a name
 *
 * Returns: (transfer none) (nullable): the first entity (depth first,
 *   pre-order) named @name
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_world3d_find_by_name (LrgWorld3D  *self,
                                        const gchar *name);

/**
 * lrg_world3d_find_by_path:
 * @self: an #LrgWorld3D
 * @path: a path relative to the root, as lrg_entity3d_get_path() builds
 *
 * Returns: (transfer none) (nullable): the entity at @path
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_world3d_find_by_path (LrgWorld3D  *self,
                                        const gchar *path);

/**
 * lrg_world3d_find_by_tag:
 * @self: an #LrgWorld3D
 * @tag: a tag
 *
 * Returns: (transfer container) (element-type LrgEntity3D): entities with
 *   @tag, in hierarchy order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_world3d_find_by_tag (LrgWorld3D  *self,
                                     const gchar *tag);

/**
 * lrg_world3d_find_with_component:
 * @self: an #LrgWorld3D
 * @component_type: a #GType deriving from #LrgComponent
 *
 * Finds spawn points, triggers, listeners and the like.
 *
 * Returns: (transfer container) (element-type LrgEntity3D): entities with
 *   a component of @component_type, in hierarchy order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_world3d_find_with_component (LrgWorld3D *self,
                                             GType       component_type);

/**
 * lrg_world3d_get_components_of_type:
 * @self: an #LrgWorld3D
 * @component_type: a #GType deriving from #LrgComponent
 *
 * Returns: (transfer container) (element-type LrgComponent): every
 *   component of @component_type in the world, in hierarchy order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_world3d_get_components_of_type (LrgWorld3D *self,
                                                GType       component_type);

/* --- Frame phases -------------------------------------------------------- */

/**
 * lrg_world3d_update:
 * @self: an #LrgWorld3D
 * @delta: seconds since the last frame
 *
 * Calls update() on every enabled component of every active entity,
 * depth first, pre-order, components in attach order.  Then flushes the
 * coalesced #LrgEntity3D::transform-changed signals.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_update (LrgWorld3D *self,
                         gfloat      delta);

/**
 * lrg_world3d_fixed_update:
 * @self: an #LrgWorld3D
 * @delta: seconds since the last frame
 *
 * Adds @delta to the fixed-step accumulator and runs fixed_update(step)
 * on every enabled component while the accumulator holds a whole step,
 * at most #LrgWorld3D:max-substeps times (the excess time is dropped so
 * a long hitch cannot spiral).  Transform signals are flushed after each
 * substep.
 *
 * Returns: the number of substeps run
 */
LRG_AVAILABLE_IN_ALL
guint lrg_world3d_fixed_update (LrgWorld3D *self,
                                gfloat      delta);

/**
 * lrg_world3d_late_update:
 * @self: an #LrgWorld3D
 * @delta: seconds since the last frame
 *
 * Calls late_update() on every enabled component (cameras follow, lights
 * re-register, animation poses settle here), then flushes transform
 * signals.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_late_update (LrgWorld3D *self,
                              gfloat      delta);

/**
 * lrg_world3d_step:
 * @self: an #LrgWorld3D
 * @delta: seconds since the last frame
 *
 * Runs one frame of simulation: lrg_world3d_fixed_update(),
 * lrg_world3d_update(), lrg_world3d_late_update().  Does nothing while
 * #LrgWorld3D:paused.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_step (LrgWorld3D *self,
                       gfloat      delta);

/**
 * lrg_world3d_get_fixed_step:
 * @self: an #LrgWorld3D
 *
 * Returns: the fixed timestep in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_world3d_get_fixed_step (LrgWorld3D *self);

/**
 * lrg_world3d_set_fixed_step:
 * @self: an #LrgWorld3D
 * @step: seconds per fixed update (> 0)
 *
 * Sets the fixed timestep.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_set_fixed_step (LrgWorld3D *self,
                                 gfloat      step);

/**
 * lrg_world3d_get_max_substeps:
 * @self: an #LrgWorld3D
 *
 * Returns: the maximum number of fixed updates per frame
 */
LRG_AVAILABLE_IN_ALL
guint lrg_world3d_get_max_substeps (LrgWorld3D *self);

/**
 * lrg_world3d_set_max_substeps:
 * @self: an #LrgWorld3D
 * @max_substeps: maximum fixed updates per frame (>= 1)
 *
 * Sets the substep cap.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_set_max_substeps (LrgWorld3D *self,
                                   guint       max_substeps);

/**
 * lrg_world3d_get_fixed_alpha:
 * @self: an #LrgWorld3D
 *
 * Gets accumulator / step after the last fixed update, in [0, 1): the
 * blend factor for interpolating between the last two fixed states.
 *
 * Returns: the interpolation alpha
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_world3d_get_fixed_alpha (LrgWorld3D *self);

/**
 * lrg_world3d_get_time:
 * @self: an #LrgWorld3D
 *
 * Returns: the sum of every delta passed to lrg_world3d_update()
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_world3d_get_time (LrgWorld3D *self);

/**
 * lrg_world3d_get_fixed_time:
 * @self: an #LrgWorld3D
 *
 * Returns: the simulated fixed time (substeps run * step)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_world3d_get_fixed_time (LrgWorld3D *self);

/**
 * lrg_world3d_get_paused:
 * @self: an #LrgWorld3D
 *
 * Returns: %TRUE if lrg_world3d_step() is paused
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_get_paused (LrgWorld3D *self);

/**
 * lrg_world3d_set_paused:
 * @self: an #LrgWorld3D
 * @paused: whether lrg_world3d_step() does nothing
 *
 * Pauses or resumes lrg_world3d_step() (the individual phase functions
 * still run when called directly).
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_set_paused (LrgWorld3D *self,
                             gboolean    paused);

/* --- Rendering ----------------------------------------------------------- */

/**
 * lrg_world3d_collect_render:
 * @self: an #LrgWorld3D
 * @camera: (nullable): the viewing camera; %NULL collects everything
 * @list: the list to fill
 *
 * Clears @list, sets its view from @camera (eye, cull mask, and for an
 * #LrgCamera3D a view-projection matrix for frustum culling when
 * #LrgWorld3D:frustum-culling is on), walks every active, visible entity
 * whose layer is in the cull mask, calls collect_render() on its enabled
 * components, keeps the items lrg_render_list_accepts() accepts, adds the
 * registered lights that affect the cull mask, and sorts the list.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_collect_render (LrgWorld3D    *self,
                                 LrgCamera     *camera,
                                 LrgRenderList *list);

/**
 * lrg_world3d_get_frustum_culling:
 * @self: an #LrgWorld3D
 *
 * Returns: %TRUE if collection frustum-culls against #LrgCamera3D cameras
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_get_frustum_culling (LrgWorld3D *self);

/**
 * lrg_world3d_set_frustum_culling:
 * @self: an #LrgWorld3D
 * @enabled: whether to frustum cull
 *
 * Enables or disables frustum culling in lrg_world3d_collect_render().
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_set_frustum_culling (LrgWorld3D *self,
                                      gboolean    enabled);

/* --- Spatial queries ----------------------------------------------------- */

/**
 * lrg_world3d_query_box:
 * @self: an #LrgWorld3D
 * @box: a world-space box
 *
 * Finds active entities whose world bounds intersect @box.  Bounds come
 * from the components' get_bounds() (an entity without bounds is a point
 * at its world position).  Moved entities are re-indexed lazily before
 * the query.
 *
 * Returns: (transfer container) (element-type LrgEntity3D): the entities
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_world3d_query_box (LrgWorld3D             *self,
                                   const LrgBoundingBox3D *box);

/**
 * lrg_world3d_query_sphere:
 * @self: an #LrgWorld3D
 * @center: (array fixed-size=3): sphere centre
 * @radius: sphere radius
 *
 * Like lrg_world3d_query_box() for a sphere (tested against the bounds'
 * closest point).
 *
 * Returns: (transfer container) (element-type LrgEntity3D): the entities
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_world3d_query_sphere (LrgWorld3D   *self,
                                      const gfloat *center,
                                      gfloat        radius);

/**
 * lrg_world3d_invalidate_bounds:
 * @self: an #LrgWorld3D
 * @entity: an entity in the world
 *
 * Tells the world that @entity's component bounds changed without a
 * transform or component-list change (for example a mesh renderer got a
 * new mesh), so the next query re-indexes its subtree.  Transform moves
 * and component add/remove are tracked automatically.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_invalidate_bounds (LrgWorld3D  *self,
                                    LrgEntity3D *entity);

/**
 * lrg_world3d_get_octree:
 * @self: an #LrgWorld3D
 *
 * Gets the octree after re-indexing moved entities.  Its objects are
 * #LrgEntity3D pointers (the root entity is never in it).
 *
 * Returns: (transfer none): the octree
 */
LRG_AVAILABLE_IN_ALL
LrgOctree * lrg_world3d_get_octree (LrgWorld3D *self);

/* --- Lights, cameras, listener ------------------------------------------- */

/**
 * lrg_world3d_register_light:
 * @self: an #LrgWorld3D
 * @source: the owner of the light (normally a light component)
 * @desc: the light; copied
 *
 * Adds or replaces the light registered for @source.  Light components
 * call it from world_entered() and again whenever the light or its entity
 * moves (late_update() is a good place).  The registry holds no reference
 * on @source: unregister before it is finalized.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_register_light (LrgWorld3D           *self,
                                 GObject              *source,
                                 const LrgLight3DDesc *desc);

/**
 * lrg_world3d_unregister_light:
 * @self: an #LrgWorld3D
 * @source: the owner passed to lrg_world3d_register_light()
 *
 * Returns: %TRUE if a light was registered for @source
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_unregister_light (LrgWorld3D *self,
                                       GObject    *source);

/**
 * lrg_world3d_get_lights:
 * @self: an #LrgWorld3D
 *
 * Returns: (transfer container) (element-type LrgLight3DDesc): the
 *   registered lights in registration order; the descriptions are owned by
 *   the world and valid until the registry changes
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_world3d_get_lights (LrgWorld3D *self);

/**
 * lrg_world3d_get_n_lights:
 * @self: an #LrgWorld3D
 *
 * Returns: the number of registered lights
 */
LRG_AVAILABLE_IN_ALL
guint lrg_world3d_get_n_lights (LrgWorld3D *self);

/**
 * lrg_world3d_register_camera:
 * @self: an #LrgWorld3D
 * @camera: a camera (the world takes a reference)
 * @priority: higher wins when no camera is explicitly active
 *
 * Adds @camera to the registry, or updates its priority.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_register_camera (LrgWorld3D *self,
                                  LrgCamera  *camera,
                                  gint        priority);

/**
 * lrg_world3d_unregister_camera:
 * @self: an #LrgWorld3D
 * @camera: a registered camera
 *
 * Removes @camera (and clears it as the explicit active camera).
 *
 * Returns: %TRUE if @camera was registered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_unregister_camera (LrgWorld3D *self,
                                        LrgCamera  *camera);

/**
 * lrg_world3d_get_cameras:
 * @self: an #LrgWorld3D
 *
 * Returns: (transfer container) (element-type LrgCamera): the registered
 *   cameras in registration order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_world3d_get_cameras (LrgWorld3D *self);

/**
 * lrg_world3d_set_active_camera:
 * @self: an #LrgWorld3D
 * @camera: (nullable): a camera to force, or %NULL for priority order
 *
 * Forces the active camera (the world takes a reference).
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_set_active_camera (LrgWorld3D *self,
                                    LrgCamera  *camera);

/**
 * lrg_world3d_get_active_camera:
 * @self: an #LrgWorld3D
 *
 * Returns: (transfer none) (nullable): the forced camera, else the
 *   registered camera with the highest priority (earliest registered on
 *   ties), else %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgCamera * lrg_world3d_get_active_camera (LrgWorld3D *self);

/**
 * lrg_world3d_set_audio_listener:
 * @self: an #LrgWorld3D
 * @listener: (nullable): the entity that hears, or %NULL
 *
 * Sets the entity whose world transform spatial audio uses (normally set
 * by the audio-listener component).  Not referenced; cleared
 * automatically when the entity leaves the world.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_set_audio_listener (LrgWorld3D  *self,
                                     LrgEntity3D *listener);

/**
 * lrg_world3d_get_audio_listener:
 * @self: an #LrgWorld3D
 *
 * Returns: (transfer none) (nullable): the listener entity
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_world3d_get_audio_listener (LrgWorld3D *self);

/**
 * lrg_world3d_set_environment:
 * @self: a #LrgWorld3D
 * @environment: (nullable): the world's environment (sky, IBL, fog), or %NULL
 *
 * Sets the environment renderers use for this world when they have no
 * environment of their own.  lrg_level_instantiate_3d() sets the level's
 * `environment:` block here.  The world holds a reference.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_set_environment (LrgWorld3D     *self,
                                  LrgEnvironment *environment);

/**
 * lrg_world3d_get_environment:
 * @self: a #LrgWorld3D
 *
 * Returns: (transfer none) (nullable): the world's environment
 */
LRG_AVAILABLE_IN_ALL
LrgEnvironment * lrg_world3d_get_environment (LrgWorld3D *self);

/**
 * lrg_world3d_set_post_stack:
 * @self: a #LrgWorld3D
 * @stack: (nullable): the world's post stack, or %NULL
 *
 * Sets the post stack renderers build onto their post processor for this
 * world (the level environment's `post_stack:`).  The world holds a
 * reference.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_set_post_stack (LrgWorld3D   *self,
                                 LrgPostStack *stack);

/**
 * lrg_world3d_get_post_stack:
 * @self: a #LrgWorld3D
 *
 * Returns: (transfer none) (nullable): the world's post stack
 */
LRG_AVAILABLE_IN_ALL
LrgPostStack * lrg_world3d_get_post_stack (LrgWorld3D *self);

/**
 * lrg_world3d_set_lightmap:
 * @self: a #LrgWorld3D
 * @lightmap: (nullable): the world's baked lightmap, or %NULL
 *
 * Sets the lightmap renderers read per item: an item whose entity GUID
 * has an entry draws its indirect light (and the direct light of
 * %LRG_LIGHT_MODE_BAKED lights) from it.  lrg_level_instantiate_3d()
 * sets the level's `lighting: lightmap:` here.  The world holds a
 * reference.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_set_lightmap (LrgWorld3D    *self,
                               LrgLightmap3D *lightmap);

/**
 * lrg_world3d_get_lightmap:
 * @self: a #LrgWorld3D
 *
 * Returns: (transfer none) (nullable): the world's lightmap
 */
LRG_AVAILABLE_IN_ALL
LrgLightmap3D * lrg_world3d_get_lightmap (LrgWorld3D *self);

/**
 * lrg_world3d_add_probe_grid:
 * @self: a #LrgWorld3D
 * @grid: a probe grid
 *
 * Adds a grid lighting items without a lightmap entry (several grids may
 * overlap: see lrg_world3d_find_probe_grid()).  Adding a grid twice does
 * nothing.  The world holds a reference.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_add_probe_grid (LrgWorld3D   *self,
                                 LrgProbeGrid *grid);

/**
 * lrg_world3d_remove_probe_grid:
 * @self: a #LrgWorld3D
 * @grid: a probe grid
 *
 * Returns: %TRUE when @grid was in the world
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_remove_probe_grid (LrgWorld3D   *self,
                                        LrgProbeGrid *grid);

/**
 * lrg_world3d_clear_probe_grids:
 * @self: a #LrgWorld3D
 *
 * Removes every probe grid.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_clear_probe_grids (LrgWorld3D *self);

/**
 * lrg_world3d_get_probe_grids:
 * @self: a #LrgWorld3D
 *
 * Returns: (transfer none) (element-type LrgProbeGrid): the grids, in
 *   the order they were added
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_world3d_get_probe_grids (LrgWorld3D *self);

/**
 * lrg_world3d_find_probe_grid:
 * @self: a #LrgWorld3D
 * @position: (array fixed-size=3): a world position
 *
 * The grid lighting @position: among the grids that contain it and have
 * a valid probe, the one with the smallest box (the most detailed);
 * ties go to the grid added first.
 *
 * Returns: (transfer none) (nullable): the grid, %NULL when none
 *   contains @position
 */
LRG_AVAILABLE_IN_ALL
LrgProbeGrid * lrg_world3d_find_probe_grid (LrgWorld3D   *self,
                                            const gfloat *position);

/* ==========================================================================
 * Physics (0.9.0 M5)
 * ========================================================================== */

/**
 * lrg_world3d_set_physics:
 * @self: a #LrgWorld3D
 * @physics: (nullable): the physics world, %NULL to detach
 *
 * Attaches the #LrgDynamicsWorld the world's #LrgRigidBodyComponent,
 * #LrgColliderComponent3D and #LrgJointComponent components create their
 * bodies and joints in (the world holds a reference).  Replacing or
 * detaching it removes every body the components made from the old one; the
 * components rebuild in the new one on the next fixed update.
 *
 * While attached, every fixed update of lrg_world3d_fixed_update() runs:
 * the components' fixed_update() (game logic), entity-to-body sync
 * (static / kinematic bodies and teleports), one physics step of
 * #LrgWorld3D:fixed-step, body-to-entity sync (dynamic bodies, triggers,
 * contact signals), then the transform-changed flush.  After the substeps
 * the rendered transforms of interpolated bodies are blended by
 * lrg_world3d_get_fixed_alpha().
 *
 * The physics world's step limits are set to one substep of
 * #LrgWorld3D:fixed-step (and kept in step when the fixed step changes), so
 * do not step it yourself while it is attached.
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_set_physics (LrgWorld3D       *self,
                              LrgDynamicsWorld *physics);

/**
 * lrg_world3d_get_physics:
 * @self: a #LrgWorld3D
 *
 * Returns: (transfer none) (nullable): the attached physics world
 */
LRG_AVAILABLE_IN_ALL
LrgDynamicsWorld * lrg_world3d_get_physics (LrgWorld3D *self);

/**
 * lrg_world3d_ensure_physics:
 * @self: a #LrgWorld3D
 *
 * Returns the attached physics world, attaching a new one first when there
 * is none. The new world is configured from the project physics settings
 * of lrg_world3d_set_physics_settings() (lrg_level_instantiate_3d() finds
 * them in the level's project.ryaml); without any it uses the defaults
 * (lrg_physics_settings_init_default(): gravity (0, -9.81, 0)) and the
 * default layer table (lrg_physics_layers_new_default()).
 *
 * Returns: (transfer none): the physics world
 */
LRG_AVAILABLE_IN_ALL
LrgDynamicsWorld * lrg_world3d_ensure_physics (LrgWorld3D *self);

/**
 * lrg_world3d_set_physics_settings:
 * @self: a #LrgWorld3D
 * @settings: (nullable): the project's physics world settings, or %NULL
 *   to forget them
 * @layers: (nullable): the project's layer table (the world keeps a
 *   reference; the physics world reads it live), or %NULL for the
 *   default table
 * @error: return location for a #GError
 *
 * Gives the world a project's `physics:` block (see #LrgPhysicsSettings).
 * The physics world attached now, or later by lrg_world3d_ensure_physics(),
 * gets them: gravity, solver iterations, sleep and @layers through
 * lrg_dynamics_world_apply_settings(), while fixed_dt and max_substeps
 * become #LrgWorld3D:fixed-step and #LrgWorld3D:max-substeps (the world
 * runs one physics substep per fixed update). A world given settings this
 * way is not reconfigured by lrg_level_instantiate_3d().
 *
 * %NULL @settings with %NULL @layers forgets the settings; a physics world
 * that is already attached keeps its values. Invalid @settings (see
 * lrg_physics_settings_validate()) fail and change nothing.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_set_physics_settings (LrgWorld3D                *self,
                                           const LrgPhysicsSettings  *settings,
                                           LrgPhysicsLayers          *layers,
                                           GError                   **error);

/**
 * lrg_world3d_get_physics_settings:
 * @self: a #LrgWorld3D
 *
 * Returns: (transfer none) (nullable): the settings given with
 *   lrg_world3d_set_physics_settings() or
 *   lrg_world3d_load_physics_project(), or %NULL when the world uses the
 *   defaults
 */
LRG_AVAILABLE_IN_ALL
const LrgPhysicsSettings * lrg_world3d_get_physics_settings (LrgWorld3D *self);

/**
 * lrg_world3d_get_physics_layers:
 * @self: a #LrgWorld3D
 *
 * Returns: (transfer none) (nullable): the layer table given with
 *   lrg_world3d_set_physics_settings() or
 *   lrg_world3d_load_physics_project(), or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsLayers * lrg_world3d_get_physics_layers (LrgWorld3D *self);

/**
 * lrg_world3d_load_physics_project:
 * @self: a #LrgWorld3D
 * @path: (type filename): a project.ryaml (see
 *   lrg_physics_settings_find_project() to find one above a level)
 * @error: return location for a #GError
 *
 * Reads the `physics:` block of @path (the layer table with
 * lrg_physics_layers_load_from_file(), the world settings with
 * lrg_physics_settings_load_from_file()) and calls
 * lrg_world3d_set_physics_settings(). On error nothing changes.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_load_physics_project (LrgWorld3D   *self,
                                           const gchar  *path,
                                           GError      **error);

/**
 * lrg_world3d_get_physics_interpolation:
 * @self: a #LrgWorld3D
 *
 * Returns: %TRUE when rendered transforms of dynamic bodies are interpolated
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world3d_get_physics_interpolation (LrgWorld3D *self);

/**
 * lrg_world3d_set_physics_interpolation:
 * @self: a #LrgWorld3D
 * @interpolate: blend the entities of dynamic bodies between the last two
 *   physics states (default %TRUE); bodies opt out with
 *   #LrgRigidBodyComponent:interpolate
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_set_physics_interpolation (LrgWorld3D *self,
                                            gboolean    interpolate);

/**
 * lrg_world3d_find_body_entity:
 * @self: a #LrgWorld3D
 * @body_id: a body id of the attached physics world
 *
 * Maps a body (a raycast or contact result) back to its entity.
 *
 * Returns: (transfer none) (nullable): the entity whose component owns the
 *   body, %NULL for bodies made outside the components
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_world3d_find_body_entity (LrgWorld3D *self,
                                            guint       body_id);

/**
 * lrg_world3d_debug_draw_physics:
 * @self: a #LrgWorld3D
 * @draw: the debug draw batch to record into
 * @flags: what to draw (#LrgDynamicsDebugFlags)
 *
 * Records the attached physics world (lrg_dynamics_world_debug_draw()) for
 * one frame; does nothing without one.  Toggle categories with the flags
 * (shapes, AABBs, contacts, constraints, sleeping, centres of mass,
 * velocities, broadphase) and the whole layer with
 * lrg_debug_draw_set_category_enabled (draw, LRG_DEBUG_CATEGORY_PHYSICS, on).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_debug_draw_physics (LrgWorld3D            *self,
                                     LrgDebugDraw          *draw,
                                     LrgDynamicsDebugFlags  flags);

/* ==========================================================================
 * Navigation (0.9.0 M6)
 * ========================================================================== */

/**
 * lrg_world3d_set_navigation:
 * @self: a #LrgWorld3D
 * @mesh: (nullable): the world's navigation mesh, %NULL to drop it
 *
 * Gives the world its #LrgNavPolyMesh (lrg_level_instantiate_3d() does
 * this for a level's `navigation:` block).  The world's crowd
 * (lrg_world3d_get_nav_crowd()) and every #LrgNavAgentComponent switch
 * to it; their agents plan their paths again.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_set_navigation (LrgWorld3D     *self,
                                 LrgNavPolyMesh *mesh);

/**
 * lrg_world3d_get_navigation:
 * @self: a #LrgWorld3D
 *
 * Returns: (transfer none) (nullable): the navigation mesh
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavPolyMesh * lrg_world3d_get_navigation (LrgWorld3D *self);

/**
 * lrg_world3d_get_nav_crowd:
 * @self: a #LrgWorld3D
 *
 * The crowd of the world's navigation agents, made on first use and
 * kept on lrg_world3d_get_navigation().  While it exists every fixed
 * update of lrg_world3d_fixed_update() steps it with
 * #LrgWorld3D:fixed-step after the components' fixed_update() and
 * before physics (so kinematic bodies follow the agents in the same
 * step); #LrgNavAgentComponent writes the agents back to their entities
 * from #LrgNavCrowd::stepped.
 *
 * Returns: (transfer none): the crowd
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavCrowd * lrg_world3d_get_nav_crowd (LrgWorld3D *self);

/**
 * lrg_world3d_debug_draw_navigation:
 * @self: a #LrgWorld3D
 * @draw: the debug draw batch to record into
 * @flags: what to draw (#LrgNavDebugFlags)
 *
 * Records the navigation mesh and the crowd's agents (paths,
 * velocities, ORCA half-planes as @flags ask) for one frame into
 * %LRG_DEBUG_CATEGORY_NAVIGATION; does nothing without a mesh or crowd.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world3d_debug_draw_navigation (LrgWorld3D       *self,
                                        LrgDebugDraw     *draw,
                                        LrgNavDebugFlags  flags);

G_END_DECLS
