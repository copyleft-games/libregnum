/* lrg-world3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWorld3D, LrgRenderList, LrgRenderItem and LrgLight3DDesc.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_WORLD3D

#include <math.h>
#include <string.h>

#include "lrg-world3d.h"
#include "lrg-entity3d.h"
#include "lrg-octree.h"
#include "../core/lrg-math3d.h"
#include "../ecs/lrg-component.h"
#include "../graphics/lrg-camera.h"
#include "../graphics/lrg-camera3d.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Internal cross-file hooks (see lrg-entity3d.c)
 * ========================================================================== */

void _lrg_entity3d_set_world_recursive    (LrgEntity3D *self,
                                           LrgWorld3D  *world);
void _lrg_entity3d_emit_transform_changed (LrgEntity3D *self);

void _lrg_world3d_entity_joined   (LrgWorld3D  *world,
                                   LrgEntity3D *entity);
void _lrg_world3d_entity_leaving  (LrgWorld3D  *world,
                                   LrgEntity3D *entity);
void _lrg_world3d_guid_changed    (LrgWorld3D  *world,
                                   LrgEntity3D *entity,
                                   const gchar *old_guid);
void _lrg_world3d_queue_transform (LrgWorld3D  *world,
                                   LrgEntity3D *entity,
                                   gboolean     queue_signal);

/* ==========================================================================
 * Enum / flags GTypes
 * ========================================================================== */

GType
lrg_render_item_kind_get_type (void)
{
	static gsize type_id = 0;
	static const GEnumValue values[] = {
		{ LRG_RENDER_ITEM_PRIMITIVE, "LRG_RENDER_ITEM_PRIMITIVE", "primitive" },
		{ LRG_RENDER_ITEM_MESH, "LRG_RENDER_ITEM_MESH", "mesh" },
		{ LRG_RENDER_ITEM_MODEL, "LRG_RENDER_ITEM_MODEL", "model" },
		{ LRG_RENDER_ITEM_BILLBOARD, "LRG_RENDER_ITEM_BILLBOARD", "billboard" },
		{ LRG_RENDER_ITEM_CUSTOM, "LRG_RENDER_ITEM_CUSTOM", "custom" },
		{ 0, NULL, NULL }
	};

	if (g_once_init_enter (&type_id))
	{
		GType id = g_enum_register_static (g_intern_static_string ("LrgRenderItemKind"), values);
		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

GType
lrg_render_item_flags_get_type (void)
{
	static gsize type_id = 0;
	static const GFlagsValue values[] = {
		{ LRG_RENDER_ITEM_FLAG_NONE, "LRG_RENDER_ITEM_FLAG_NONE", "none" },
		{ LRG_RENDER_ITEM_FLAG_CAST_SHADOW, "LRG_RENDER_ITEM_FLAG_CAST_SHADOW", "cast-shadow" },
		{ LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW, "LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW", "receive-shadow" },
		{ LRG_RENDER_ITEM_FLAG_TRANSPARENT, "LRG_RENDER_ITEM_FLAG_TRANSPARENT", "transparent" },
		{ LRG_RENDER_ITEM_FLAG_CUTOUT, "LRG_RENDER_ITEM_FLAG_CUTOUT", "cutout" },
		{ LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED, "LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED", "double-sided" },
		{ LRG_RENDER_ITEM_FLAG_UNLIT, "LRG_RENDER_ITEM_FLAG_UNLIT", "unlit" },
		{ LRG_RENDER_ITEM_FLAG_NO_CULL, "LRG_RENDER_ITEM_FLAG_NO_CULL", "no-cull" },
		{ 0, NULL, NULL }
	};

	if (g_once_init_enter (&type_id))
	{
		GType id = g_flags_register_static (g_intern_static_string ("LrgRenderItemFlags"), values);
		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

GType
lrg_render_category_get_type (void)
{
	static gsize type_id = 0;
	static const GEnumValue values[] = {
		{ LRG_RENDER_CATEGORY_OPAQUE, "LRG_RENDER_CATEGORY_OPAQUE", "opaque" },
		{ LRG_RENDER_CATEGORY_CUTOUT, "LRG_RENDER_CATEGORY_CUTOUT", "cutout" },
		{ LRG_RENDER_CATEGORY_TRANSPARENT, "LRG_RENDER_CATEGORY_TRANSPARENT", "transparent" },
		{ LRG_RENDER_CATEGORY_SHADOW_CASTER, "LRG_RENDER_CATEGORY_SHADOW_CASTER", "shadow-caster" },
		{ 0, NULL, NULL }
	};

	if (g_once_init_enter (&type_id))
	{
		GType id = g_enum_register_static (g_intern_static_string ("LrgRenderCategory"), values);
		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

GType
lrg_light3d_kind_get_type (void)
{
	static gsize type_id = 0;
	static const GEnumValue values[] = {
		{ LRG_LIGHT3D_DIRECTIONAL, "LRG_LIGHT3D_DIRECTIONAL", "directional" },
		{ LRG_LIGHT3D_POINT, "LRG_LIGHT3D_POINT", "point" },
		{ LRG_LIGHT3D_SPOT, "LRG_LIGHT3D_SPOT", "spot" },
		{ 0, NULL, NULL }
	};

	if (g_once_init_enter (&type_id))
	{
		GType id = g_enum_register_static (g_intern_static_string ("LrgLight3DKind"), values);
		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

/* ==========================================================================
 * Small math helpers
 * ========================================================================== */

/* An AABB with min > max on any axis is "unknown" (never culled). */
static gboolean
bounds_known (const LrgBoundingBox3D *b)
{
	return b->min.x <= b->max.x && b->min.y <= b->max.y && b->min.z <= b->max.z;
}

static gboolean
bounds_intersect (const LrgBoundingBox3D *a,
                  const LrgBoundingBox3D *b)
{
	return a->min.x <= b->max.x && a->max.x >= b->min.x
	    && a->min.y <= b->max.y && a->max.y >= b->min.y
	    && a->min.z <= b->max.z && a->max.z >= b->min.z;
}

static gboolean
bounds_intersect_sphere (const LrgBoundingBox3D *b,
                         const gfloat           *c,
                         gfloat                  r)
{
	gfloat dx;
	gfloat dy;
	gfloat dz;

	/* Distance from the centre to the closest point of the box. */
	dx = c[0] < b->min.x ? b->min.x - c[0] : (c[0] > b->max.x ? c[0] - b->max.x : 0.0f);
	dy = c[1] < b->min.y ? b->min.y - c[1] : (c[1] > b->max.y ? c[1] - b->max.y : 0.0f);
	dz = c[2] < b->min.z ? b->min.z - c[2] : (c[2] > b->max.z ? c[2] - b->max.z : 0.0f);
	return dx * dx + dy * dy + dz * dz <= r * r;
}

/* ==========================================================================
 * LrgLight3DDesc
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgLight3DDesc, lrg_light3d_desc,
                     lrg_light3d_desc_copy, lrg_light3d_desc_free)

void
lrg_light3d_desc_init (LrgLight3DDesc *desc)
{
	g_return_if_fail (desc != NULL);

	memset (desc, 0, sizeof (*desc));
	desc->kind = LRG_LIGHT3D_POINT;
	desc->color[0] = desc->color[1] = desc->color[2] = 1.0f;
	desc->intensity = 1.0f;
	desc->range = 10.0f;
	desc->inner_angle = (gfloat)(G_PI / 6.0);   /* 30 degrees */
	desc->outer_angle = (gfloat)(G_PI / 4.0);   /* 45 degrees */
	desc->cast_shadows = FALSE;
	desc->enabled = TRUE;
	desc->direction[2] = -1.0f;
	desc->layer_mask = LRG_RENDER_LAYERS_MASK_ALL;
	desc->source = NULL;
}

LrgLight3DDesc *
lrg_light3d_desc_new (void)
{
	LrgLight3DDesc *desc;

	desc = g_new (LrgLight3DDesc, 1);
	lrg_light3d_desc_init (desc);
	return desc;
}

LrgLight3DDesc *
lrg_light3d_desc_copy (const LrgLight3DDesc *desc)
{
	g_return_val_if_fail (desc != NULL, NULL);
	return (LrgLight3DDesc *)g_memdup2 (desc, sizeof (*desc));
}

void
lrg_light3d_desc_free (LrgLight3DDesc *desc)
{
	g_free (desc);
}

/* ==========================================================================
 * LrgRenderItem
 * ========================================================================== */

void
lrg_render_item_init (LrgRenderItem *item)
{
	g_return_if_fail (item != NULL);

	memset (item, 0, sizeof (*item));
	item->kind = LRG_RENDER_ITEM_PRIMITIVE;
	item->flags = LRG_RENDER_ITEM_FLAG_CAST_SHADOW | LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW;
	item->layer = LRG_RENDER_LAYERS_DEFAULT;
	item->primitive = LRG_PRIMITIVE_CUBE;
	item->params[0] = item->params[1] = item->params[2] = 1.0f;
	item->mesh_index = -1;
	item->color[0] = item->color[1] = item->color[2] = item->color[3] = 1.0f;
	lrg_math3d_identity (item->world_matrix);

	/* Unknown bounds: min > max. */
	item->bounds.min.x = item->bounds.min.y = item->bounds.min.z = 1.0f;
	item->bounds.max.x = item->bounds.max.y = item->bounds.max.z = -1.0f;
}

void
lrg_render_item_init_for_entity (LrgRenderItem *item,
                                 LrgEntity3D   *entity,
                                 LrgComponent  *component)
{
	g_return_if_fail (item != NULL);
	g_return_if_fail (LRG_IS_ENTITY3D (entity));

	lrg_render_item_init (item);
	lrg_entity3d_get_world_matrix (entity, item->world_matrix);
	item->layer = lrg_entity3d_get_layer (entity);
	item->entity = entity;
	item->component = component;
}

void
lrg_render_item_set_local_bounds (LrgRenderItem          *item,
                                  const LrgBoundingBox3D *local_bounds)
{
	guint corner;

	g_return_if_fail (item != NULL);
	g_return_if_fail (local_bounds != NULL);

	for (corner = 0; corner < 8; corner++)
	{
		gfloat p[3];
		gfloat w[3];

		p[0] = (corner & 1) ? local_bounds->max.x : local_bounds->min.x;
		p[1] = (corner & 2) ? local_bounds->max.y : local_bounds->min.y;
		p[2] = (corner & 4) ? local_bounds->max.z : local_bounds->min.z;
		lrg_math3d_transform_point (item->world_matrix, p, w);

		if (corner == 0)
		{
			item->bounds.min.x = item->bounds.max.x = w[0];
			item->bounds.min.y = item->bounds.max.y = w[1];
			item->bounds.min.z = item->bounds.max.z = w[2];
			continue;
		}
		item->bounds.min.x = MIN (item->bounds.min.x, w[0]);
		item->bounds.min.y = MIN (item->bounds.min.y, w[1]);
		item->bounds.min.z = MIN (item->bounds.min.z, w[2]);
		item->bounds.max.x = MAX (item->bounds.max.x, w[0]);
		item->bounds.max.y = MAX (item->bounds.max.y, w[1]);
		item->bounds.max.z = MAX (item->bounds.max.z, w[2]);
	}
}

LrgRenderCategory
lrg_render_item_get_category (const LrgRenderItem *item)
{
	g_return_val_if_fail (item != NULL, LRG_RENDER_CATEGORY_OPAQUE);

	if (item->flags & LRG_RENDER_ITEM_FLAG_TRANSPARENT)
		return LRG_RENDER_CATEGORY_TRANSPARENT;
	if (item->flags & LRG_RENDER_ITEM_FLAG_CUTOUT)
		return LRG_RENDER_CATEGORY_CUTOUT;
	return LRG_RENDER_CATEGORY_OPAQUE;
}

/* ==========================================================================
 * LrgRenderList
 *
 * Items live by value in one GArray (so pointers to them stay valid until
 * the next add/clear); each category is a GArray of guint indices into it.
 * ========================================================================== */

struct _LrgRenderList
{
	gint        ref_count;
	GArray     *items;                               /* LrgRenderItem */
	GArray     *categories[LRG_RENDER_CATEGORY_COUNT]; /* guint indices */
	GArray     *lights;                              /* LrgLight3DDesc */

	/* Camera state */
	gfloat      eye[3];
	guint32     cull_mask;
	gboolean    has_frustum;
	gfloat      view_projection[16];
	gfloat      planes[6][4];                        /* normalised */
};

G_DEFINE_BOXED_TYPE (LrgRenderList, lrg_render_list,
                     lrg_render_list_ref, lrg_render_list_unref)

LrgRenderList *
lrg_render_list_new (void)
{
	LrgRenderList *self;
	guint i;

	self = g_new0 (LrgRenderList, 1);
	self->ref_count = 1;
	self->items = g_array_new (FALSE, FALSE, sizeof (LrgRenderItem));
	for (i = 0; i < LRG_RENDER_CATEGORY_COUNT; i++)
		self->categories[i] = g_array_new (FALSE, FALSE, sizeof (guint));
	self->lights = g_array_new (FALSE, FALSE, sizeof (LrgLight3DDesc));
	self->cull_mask = LRG_RENDER_LAYERS_MASK_ALL;
	return self;
}

LrgRenderList *
lrg_render_list_ref (LrgRenderList *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	g_atomic_int_inc (&self->ref_count);
	return self;
}

void
lrg_render_list_unref (LrgRenderList *self)
{
	guint i;

	g_return_if_fail (self != NULL);

	if (!g_atomic_int_dec_and_test (&self->ref_count))
		return;

	lrg_render_list_clear (self);
	g_array_unref (self->items);
	for (i = 0; i < LRG_RENDER_CATEGORY_COUNT; i++)
		g_array_unref (self->categories[i]);
	g_array_unref (self->lights);
	g_free (self);
}

void
lrg_render_list_clear (LrgRenderList *self)
{
	guint i;

	g_return_if_fail (self != NULL);

	/* Release the items' owned user data. */
	for (i = 0; i < self->items->len; i++)
	{
		LrgRenderItem *item;

		item = &g_array_index (self->items, LrgRenderItem, i);
		if (item->user_data_destroy != NULL && item->user_data != NULL)
			item->user_data_destroy (item->user_data);
	}
	g_array_set_size (self->items, 0);
	for (i = 0; i < LRG_RENDER_CATEGORY_COUNT; i++)
		g_array_set_size (self->categories[i], 0);
	g_array_set_size (self->lights, 0);

	self->eye[0] = self->eye[1] = self->eye[2] = 0.0f;
	self->cull_mask = LRG_RENDER_LAYERS_MASK_ALL;
	self->has_frustum = FALSE;
}

/*
 * frustum_visible:
 *
 * Plane/AABB test with the "positive vertex": the box is outside if its
 * corner furthest along a plane's normal is behind that plane.
 */
static gboolean
frustum_visible (LrgRenderList          *self,
                 const LrgBoundingBox3D *b)
{
	guint i;

	if (!self->has_frustum || !bounds_known (b))
		return TRUE;

	for (i = 0; i < 6; i++)
	{
		const gfloat *p;
		gfloat x;
		gfloat y;
		gfloat z;

		p = self->planes[i];
		x = p[0] >= 0.0f ? b->max.x : b->min.x;
		y = p[1] >= 0.0f ? b->max.y : b->min.y;
		z = p[2] >= 0.0f ? b->max.z : b->min.z;
		if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0.0f)
			return FALSE;
	}
	return TRUE;
}

/*
 * item_in_view:
 *
 * Layer and frustum test without the shadow-caster exception.
 */
static gboolean
item_in_view (LrgRenderList       *self,
              const LrgRenderItem *item)
{
	if ((self->cull_mask & LRG_RENDER_LAYERS_BIT (item->layer)) == 0)
		return FALSE;
	if (item->flags & LRG_RENDER_ITEM_FLAG_NO_CULL)
		return TRUE;
	return frustum_visible (self, &item->bounds);
}

gboolean
lrg_render_list_accepts (LrgRenderList       *self,
                         const LrgRenderItem *item)
{
	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	if ((self->cull_mask & LRG_RENDER_LAYERS_BIT (item->layer)) == 0)
		return FALSE;
	if (item->flags & LRG_RENDER_ITEM_FLAG_CAST_SHADOW)
		return TRUE;
	return item_in_view (self, item);
}

guint
lrg_render_list_add (LrgRenderList       *self,
                     const LrgRenderItem *item)
{
	guint index;
	LrgRenderCategory main_category;

	g_return_val_if_fail (self != NULL, 0);
	g_return_val_if_fail (item != NULL, 0);

	index = self->items->len;
	g_array_append_val (self->items, *item);

	/* A shadow caster outside the view goes only to SHADOW_CASTER. */
	main_category = lrg_render_item_get_category (item);
	if (item_in_view (self, item))
		g_array_append_val (self->categories[main_category], index);
	if (item->flags & LRG_RENDER_ITEM_FLAG_CAST_SHADOW)
	{
		guint shadow = (guint)LRG_RENDER_CATEGORY_SHADOW_CASTER;

		g_array_append_val (self->categories[shadow], index);
	}
	return index;
}

guint
lrg_render_list_get_n_items (LrgRenderList     *self,
                             LrgRenderCategory  category)
{
	g_return_val_if_fail (self != NULL, 0);
	g_return_val_if_fail ((guint)category < LRG_RENDER_CATEGORY_COUNT, 0);
	return self->categories[category]->len;
}

const LrgRenderItem *
lrg_render_list_get_item (LrgRenderList     *self,
                          LrgRenderCategory  category,
                          guint              index)
{
	GArray *cat;

	g_return_val_if_fail (self != NULL, NULL);
	g_return_val_if_fail ((guint)category < LRG_RENDER_CATEGORY_COUNT, NULL);

	cat = self->categories[category];
	g_return_val_if_fail (index < cat->len, NULL);
	return &g_array_index (self->items, LrgRenderItem,
	                       g_array_index (cat, guint, index));
}

guint
lrg_render_list_get_total_items (LrgRenderList *self)
{
	g_return_val_if_fail (self != NULL, 0);
	return self->items->len;
}

void
lrg_render_list_foreach (LrgRenderList     *self,
                         LrgRenderCategory  category,
                         LrgRenderItemFunc  func,
                         gpointer           user_data)
{
	GArray *cat;
	guint i;

	g_return_if_fail (self != NULL);
	g_return_if_fail ((guint)category < LRG_RENDER_CATEGORY_COUNT);
	g_return_if_fail (func != NULL);

	cat = self->categories[category];
	for (i = 0; i < cat->len; i++)
		func (&g_array_index (self->items, LrgRenderItem, g_array_index (cat, guint, i)),
		      category, user_data);
}

/* Sort context: g_array_sort_with_data over index arrays. */
typedef struct
{
	GArray *items;
	gint    mode;   /* 0 = key then near first, 1 = far first, 2 = key */
} SortCtx;

static gint
compare_indices (gconstpointer a,
                 gconstpointer b,
                 gpointer      user_data)
{
	SortCtx *ctx;
	guint ia;
	guint ib;
	const LrgRenderItem *x;
	const LrgRenderItem *y;

	ctx = (SortCtx *)user_data;
	ia = *(const guint *)a;
	ib = *(const guint *)b;
	x = &g_array_index (ctx->items, LrgRenderItem, ia);
	y = &g_array_index (ctx->items, LrgRenderItem, ib);

	if (ctx->mode != 1)
	{
		if (x->sort_key < y->sort_key)
			return -1;
		if (x->sort_key > y->sort_key)
			return 1;
	}
	if (ctx->mode == 0)
	{
		if (x->view_depth < y->view_depth)
			return -1;
		if (x->view_depth > y->view_depth)
			return 1;
	}
	else if (ctx->mode == 1)
	{
		if (x->view_depth > y->view_depth)
			return -1;
		if (x->view_depth < y->view_depth)
			return 1;
	}

	/* Stable: fall back to submission order. */
	return ia < ib ? -1 : (ia > ib ? 1 : 0);
}

void
lrg_render_list_sort (LrgRenderList *self)
{
	SortCtx ctx;
	guint i;

	g_return_if_fail (self != NULL);

	/* Depth from the eye to the item's bounds centre (or its origin when
	 * the bounds are unknown). */
	for (i = 0; i < self->items->len; i++)
	{
		LrgRenderItem *item;
		gfloat c[3];
		gfloat dx;
		gfloat dy;
		gfloat dz;

		item = &g_array_index (self->items, LrgRenderItem, i);
		if (bounds_known (&item->bounds))
		{
			c[0] = 0.5f * (item->bounds.min.x + item->bounds.max.x);
			c[1] = 0.5f * (item->bounds.min.y + item->bounds.max.y);
			c[2] = 0.5f * (item->bounds.min.z + item->bounds.max.z);
		}
		else
		{
			c[0] = item->world_matrix[12];
			c[1] = item->world_matrix[13];
			c[2] = item->world_matrix[14];
		}
		dx = c[0] - self->eye[0];
		dy = c[1] - self->eye[1];
		dz = c[2] - self->eye[2];
		item->view_depth = sqrtf (dx * dx + dy * dy + dz * dz);
	}

	ctx.items = self->items;
	ctx.mode = 0;
	g_array_sort_with_data (self->categories[LRG_RENDER_CATEGORY_OPAQUE], compare_indices, &ctx);
	g_array_sort_with_data (self->categories[LRG_RENDER_CATEGORY_CUTOUT], compare_indices, &ctx);
	ctx.mode = 1;
	g_array_sort_with_data (self->categories[LRG_RENDER_CATEGORY_TRANSPARENT], compare_indices, &ctx);
	ctx.mode = 2;
	g_array_sort_with_data (self->categories[LRG_RENDER_CATEGORY_SHADOW_CASTER], compare_indices, &ctx);
}

void
lrg_render_list_set_view (LrgRenderList *self,
                          const gfloat  *eye,
                          guint32        cull_mask,
                          const gfloat  *view_projection)
{
	const gfloat *m;
	guint i;

	g_return_if_fail (self != NULL);
	g_return_if_fail (eye != NULL);

	memcpy (self->eye, eye, sizeof (self->eye));
	self->cull_mask = cull_mask;
	self->has_frustum = view_projection != NULL;
	if (view_projection == NULL)
		return;

	memcpy (self->view_projection, view_projection, sizeof (self->view_projection));

	/* Gribb/Hartmann plane extraction for a column-major matrix: row r is
	 * (m[r], m[4 + r], m[8 + r], m[12 + r]).  Planes: left, right, bottom,
	 * top, near, far = row3 +/- row0/1/2. */
	m = self->view_projection;
	for (i = 0; i < 6; i++)
	{
		guint axis;
		gfloat sign;
		gfloat *p;
		gfloat len;

		axis = i / 2;
		sign = (i % 2 == 0) ? 1.0f : -1.0f;
		p = self->planes[i];
		p[0] = m[3]  + sign * m[axis];
		p[1] = m[7]  + sign * m[4 + axis];
		p[2] = m[11] + sign * m[8 + axis];
		p[3] = m[15] + sign * m[12 + axis];

		len = sqrtf (p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
		if (len > 1e-12f)
		{
			p[0] /= len;
			p[1] /= len;
			p[2] /= len;
			p[3] /= len;
		}
	}
}

void
lrg_render_list_get_eye (LrgRenderList *self,
                         gfloat        *out)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (out != NULL);
	memcpy (out, self->eye, sizeof (self->eye));
}

guint32
lrg_render_list_get_cull_mask (LrgRenderList *self)
{
	g_return_val_if_fail (self != NULL, 0);
	return self->cull_mask;
}

gboolean
lrg_render_list_get_view_projection (LrgRenderList *self,
                                     gfloat        *out)
{
	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	if (!self->has_frustum)
		return FALSE;
	memcpy (out, self->view_projection, sizeof (self->view_projection));
	return TRUE;
}

void
lrg_render_list_add_light (LrgRenderList        *self,
                           const LrgLight3DDesc *light)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (light != NULL);
	g_array_append_val (self->lights, *light);
}

guint
lrg_render_list_get_n_lights (LrgRenderList *self)
{
	g_return_val_if_fail (self != NULL, 0);
	return self->lights->len;
}

const LrgLight3DDesc *
lrg_render_list_get_light (LrgRenderList *self,
                           guint          index)
{
	g_return_val_if_fail (self != NULL, NULL);
	g_return_val_if_fail (index < self->lights->len, NULL);
	return &g_array_index (self->lights, LrgLight3DDesc, index);
}

/* ==========================================================================
 * LrgWorld3D
 * ========================================================================== */

#define WORLD_DEFAULT_HALF_EXTENT (4096.0f)
#define WORLD_CAMERA_NEAR         (0.05f)
#define WORLD_CAMERA_FAR          (10000.0f)

typedef struct
{
	LrgCamera *camera;   /* owned ref */
	gint       priority;
} CameraEntry;

/* Per-entity spatial bookkeeping. */
typedef struct
{
	LrgBoundingBox3D bounds;
	gboolean         in_octree;
	gulong           added_handler;
	gulong           removed_handler;
} SpatialRec;

struct _LrgWorld3D
{
	GObject      parent_instance;

	LrgEntity3D *root;
	guint        entity_count;
	GHashTable  *guid_map;         /* gchar* (owned) -> LrgEntity3D* */

	/* Spatial index */
	LrgOctree   *octree;
	GHashTable  *spatial;          /* LrgEntity3D* -> SpatialRec* */
	GHashTable  *spatial_dirty;    /* LrgEntity3D* set (subtree roots) */

	/* Coalesced transform-changed queue (owned refs) */
	GPtrArray   *transform_queue;

	/* Phases */
	gfloat       fixed_step;
	guint        max_substeps;
	gfloat       accumulator;
	gdouble      time;
	gdouble      fixed_time;
	gboolean     paused;
	gboolean     frustum_culling;

	/* Registries */
	GPtrArray   *lights;           /* LrgLight3DDesc* (owned) */
	GPtrArray   *cameras;          /* CameraEntry* (owned) */
	LrgCamera   *active_camera;    /* owned ref or NULL */
	LrgEntity3D *audio_listener;   /* weak */

	/* Look (0.9.0 M2): used by renderers without their own */
	LrgEnvironment *environment;   /* owned ref or NULL */
	LrgPostStack   *post_stack;    /* owned ref or NULL */
};

G_DEFINE_FINAL_TYPE (LrgWorld3D, lrg_world3d, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_ROOT,
	PROP_FIXED_STEP,
	PROP_MAX_SUBSTEPS,
	PROP_PAUSED,
	PROP_FRUSTUM_CULLING,
	PROP_ACTIVE_CAMERA,
	PROP_AUDIO_LISTENER,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_ENTITY_ADDED,
	SIGNAL_ENTITY_REMOVED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
camera_entry_free (gpointer data)
{
	CameraEntry *entry;

	entry = (CameraEntry *)data;
	g_clear_object (&entry->camera);
	g_free (entry);
}

/* --------------------------------------------------------------------------
 * Membership hooks (called from lrg-entity3d.c)
 * -------------------------------------------------------------------------- */

static void
on_entity_components_changed (LrgEntity3D  *entity,
                              LrgComponent *component,
                              gpointer      user_data)
{
	LrgWorld3D *self;

	self = LRG_WORLD3D (user_data);
	g_hash_table_add (self->spatial_dirty, entity);
}

void
_lrg_world3d_entity_joined (LrgWorld3D  *self,
                            LrgEntity3D *entity)
{
	const gchar *guid;
	SpatialRec *rec;
	GPtrArray *components;
	guint i;

	/* GUID map */
	guid = lrg_entity3d_get_guid (entity);
	if (guid != NULL)
	{
		LrgEntity3D *existing;

		existing = (LrgEntity3D *)g_hash_table_lookup (self->guid_map, guid);
		if (existing != NULL && existing != entity)
			lrg_warning (LRG_LOG_DOMAIN_WORLD3D,
			             "Duplicate entity GUID %s; lookups return the newest", guid);
		g_hash_table_replace (self->guid_map, g_strdup (guid), entity);
	}

	if (entity == self->root)
		return;

	self->entity_count++;

	/* Spatial record; indexed lazily on the next query. */
	rec = g_new0 (SpatialRec, 1);
	rec->added_handler = g_signal_connect (entity, "component-added",
	                                       G_CALLBACK (on_entity_components_changed), self);
	rec->removed_handler = g_signal_connect (entity, "component-removed",
	                                         G_CALLBACK (on_entity_components_changed), self);
	g_hash_table_insert (self->spatial, entity, rec);
	g_hash_table_add (self->spatial_dirty, entity);

	/* Components learn about the world (parents before children, since
	 * join walks pre-order). */
	components = lrg_entity3d_get_components (entity);
	for (i = 0; i < components->len; i++)
	{
		LrgComponent *c;
		LrgComponentClass *klass;

		c = (LrgComponent *)g_ptr_array_index (components, i);
		klass = LRG_COMPONENT_GET_CLASS (c);
		if (klass->world_entered != NULL)
			klass->world_entered (c, self);
	}
	g_ptr_array_unref (components);

	g_signal_emit (self, signals[SIGNAL_ENTITY_ADDED], 0, entity);
}

void
_lrg_world3d_entity_leaving (LrgWorld3D  *self,
                             LrgEntity3D *entity)
{
	const gchar *guid;
	SpatialRec *rec;
	GPtrArray *components;
	gint i;

	if (entity != self->root)
	{
		g_signal_emit (self, signals[SIGNAL_ENTITY_REMOVED], 0, entity);

		/* Components unregister in reverse attach order. */
		components = lrg_entity3d_get_components (entity);
		for (i = (gint)components->len - 1; i >= 0; i--)
		{
			LrgComponent *c;
			LrgComponentClass *klass;

			c = (LrgComponent *)g_ptr_array_index (components, i);
			klass = LRG_COMPONENT_GET_CLASS (c);
			if (klass->world_exited != NULL)
				klass->world_exited (c, self);
		}
		g_ptr_array_unref (components);

		rec = (SpatialRec *)g_hash_table_lookup (self->spatial, entity);
		if (rec != NULL)
		{
			if (rec->in_octree)
				lrg_octree_remove (self->octree, entity);
			g_signal_handler_disconnect (entity, rec->added_handler);
			g_signal_handler_disconnect (entity, rec->removed_handler);
			g_hash_table_remove (self->spatial, entity);
		}
		g_hash_table_remove (self->spatial_dirty, entity);

		if (self->audio_listener == entity)
		{
			self->audio_listener = NULL;
			g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUDIO_LISTENER]);
		}

		self->entity_count--;
	}

	guid = lrg_entity3d_get_guid (entity);
	if (guid != NULL && g_hash_table_lookup (self->guid_map, guid) == entity)
		g_hash_table_remove (self->guid_map, guid);
}

void
_lrg_world3d_guid_changed (LrgWorld3D  *self,
                           LrgEntity3D *entity,
                           const gchar *old_guid)
{
	const gchar *guid;

	if (old_guid != NULL && g_hash_table_lookup (self->guid_map, old_guid) == entity)
		g_hash_table_remove (self->guid_map, old_guid);

	guid = lrg_entity3d_get_guid (entity);
	if (guid != NULL)
		g_hash_table_replace (self->guid_map, g_strdup (guid), entity);
}

void
_lrg_world3d_queue_transform (LrgWorld3D  *self,
                              LrgEntity3D *entity,
                              gboolean     queue_signal)
{
	if (queue_signal)
		g_ptr_array_add (self->transform_queue, g_object_ref (entity));
	g_hash_table_add (self->spatial_dirty, entity);
}

/*
 * flush_transforms:
 *
 * Emits the coalesced transform-changed signals.  Handlers may move more
 * entities; those are queued again and handled in this same flush (with
 * a generous bound so a handler ping-pong cannot hang the frame).
 */
static void
flush_transforms (LrgWorld3D *self)
{
	guint rounds;

	for (rounds = 0; rounds < 16 && self->transform_queue->len > 0; rounds++)
	{
		g_autoptr(GPtrArray) batch = NULL;
		guint i;

		batch = self->transform_queue;
		self->transform_queue = g_ptr_array_new_with_free_func (g_object_unref);

		for (i = 0; i < batch->len; i++)
			_lrg_entity3d_emit_transform_changed ((LrgEntity3D *)g_ptr_array_index (batch, i));
	}
}

/* --------------------------------------------------------------------------
 * GObject
 * -------------------------------------------------------------------------- */

static void
lrg_world3d_dispose (GObject *object)
{
	LrgWorld3D *self;

	self = LRG_WORLD3D (object);

	/* Take the whole tree out of the world first so components unregister
	 * and every signal handler is disconnected. */
	if (self->root != NULL)
	{
		_lrg_entity3d_set_world_recursive (self->root, NULL);
		g_clear_object (&self->root);
	}

	if (self->transform_queue != NULL)
		g_ptr_array_set_size (self->transform_queue, 0);
	if (self->cameras != NULL)
		g_ptr_array_set_size (self->cameras, 0);
	g_clear_object (&self->active_camera);
	g_clear_object (&self->octree);
	g_clear_object (&self->environment);
	g_clear_object (&self->post_stack);

	G_OBJECT_CLASS (lrg_world3d_parent_class)->dispose (object);
}

static void
lrg_world3d_finalize (GObject *object)
{
	LrgWorld3D *self;

	self = LRG_WORLD3D (object);

	g_clear_pointer (&self->guid_map, g_hash_table_unref);
	g_clear_pointer (&self->spatial, g_hash_table_unref);
	g_clear_pointer (&self->spatial_dirty, g_hash_table_unref);
	g_clear_pointer (&self->transform_queue, g_ptr_array_unref);
	g_clear_pointer (&self->lights, g_ptr_array_unref);
	g_clear_pointer (&self->cameras, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_world3d_parent_class)->finalize (object);
}

static void
lrg_world3d_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	LrgWorld3D *self;

	self = LRG_WORLD3D (object);

	switch (prop_id)
	{
	case PROP_ROOT:
		g_value_set_object (value, self->root);
		break;
	case PROP_FIXED_STEP:
		g_value_set_float (value, self->fixed_step);
		break;
	case PROP_MAX_SUBSTEPS:
		g_value_set_uint (value, self->max_substeps);
		break;
	case PROP_PAUSED:
		g_value_set_boolean (value, self->paused);
		break;
	case PROP_FRUSTUM_CULLING:
		g_value_set_boolean (value, self->frustum_culling);
		break;
	case PROP_ACTIVE_CAMERA:
		g_value_set_object (value, lrg_world3d_get_active_camera (self));
		break;
	case PROP_AUDIO_LISTENER:
		g_value_set_object (value, self->audio_listener);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_world3d_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	LrgWorld3D *self;

	self = LRG_WORLD3D (object);

	switch (prop_id)
	{
	case PROP_FIXED_STEP:
		lrg_world3d_set_fixed_step (self, g_value_get_float (value));
		break;
	case PROP_MAX_SUBSTEPS:
		lrg_world3d_set_max_substeps (self, g_value_get_uint (value));
		break;
	case PROP_PAUSED:
		lrg_world3d_set_paused (self, g_value_get_boolean (value));
		break;
	case PROP_FRUSTUM_CULLING:
		lrg_world3d_set_frustum_culling (self, g_value_get_boolean (value));
		break;
	case PROP_ACTIVE_CAMERA:
		lrg_world3d_set_active_camera (self, (LrgCamera *)g_value_get_object (value));
		break;
	case PROP_AUDIO_LISTENER:
		lrg_world3d_set_audio_listener (self, (LrgEntity3D *)g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_world3d_class_init (LrgWorld3DClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = lrg_world3d_dispose;
	object_class->finalize = lrg_world3d_finalize;
	object_class->get_property = lrg_world3d_get_property;
	object_class->set_property = lrg_world3d_set_property;

	/**
	 * LrgWorld3D:root:
	 *
	 * The root entity (read-only).
	 */
	properties[PROP_ROOT] =
		g_param_spec_object ("root", "Root", "Root entity", LRG_TYPE_ENTITY3D,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS |
		                     G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWorld3D:fixed-step:
	 *
	 * Seconds per fixed update.
	 */
	properties[PROP_FIXED_STEP] =
		g_param_spec_float ("fixed-step", "Fixed step", "Seconds per fixed update",
		                    1e-5f, 10.0f, 1.0f / 60.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                    G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWorld3D:max-substeps:
	 *
	 * Maximum fixed updates per frame.
	 */
	properties[PROP_MAX_SUBSTEPS] =
		g_param_spec_uint ("max-substeps", "Max substeps", "Maximum fixed updates per frame",
		                   1, 1000, 8,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                   G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWorld3D:paused:
	 *
	 * Whether lrg_world3d_step() does nothing.
	 */
	properties[PROP_PAUSED] =
		g_param_spec_boolean ("paused", "Paused", "Whether step() does nothing", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWorld3D:frustum-culling:
	 *
	 * Whether render collection frustum-culls against #LrgCamera3D cameras.
	 */
	properties[PROP_FRUSTUM_CULLING] =
		g_param_spec_boolean ("frustum-culling", "Frustum culling",
		                      "Frustum cull during render collection", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWorld3D:active-camera:
	 *
	 * The active camera (see lrg_world3d_get_active_camera()); writing it
	 * forces a camera like lrg_world3d_set_active_camera().
	 */
	properties[PROP_ACTIVE_CAMERA] =
		g_param_spec_object ("active-camera", "Active camera", "The active camera",
		                     LRG_TYPE_CAMERA,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                     G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWorld3D:audio-listener:
	 *
	 * The entity spatial audio is heard from.
	 */
	properties[PROP_AUDIO_LISTENER] =
		g_param_spec_object ("audio-listener", "Audio listener", "Listener entity",
		                     LRG_TYPE_ENTITY3D,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                     G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgWorld3D::entity-added:
	 * @self: the world
	 * @entity: an entity that joined
	 *
	 * Emitted for every entity of a subtree that joins the world (parents
	 * first), after its components' world_entered() hooks ran.
	 */
	signals[SIGNAL_ENTITY_ADDED] =
		g_signal_new ("entity-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_ENTITY3D);

	/**
	 * LrgWorld3D::entity-removed:
	 * @self: the world
	 * @entity: an entity that is leaving
	 *
	 * Emitted for every entity of a subtree that leaves the world
	 * (children first), before its components' world_exited() hooks run.
	 */
	signals[SIGNAL_ENTITY_REMOVED] =
		g_signal_new ("entity-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_ENTITY3D);
}

static void
lrg_world3d_init (LrgWorld3D *self)
{
	g_autoptr(LrgBoundingBox3D) bounds = NULL;

	self->guid_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->spatial = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	self->spatial_dirty = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->transform_queue = g_ptr_array_new_with_free_func (g_object_unref);
	self->lights = g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_light3d_desc_free);
	self->cameras = g_ptr_array_new_with_free_func (camera_entry_free);

	self->fixed_step = 1.0f / 60.0f;
	self->max_substeps = 8;
	self->frustum_culling = TRUE;

	bounds = lrg_bounding_box3d_new (-WORLD_DEFAULT_HALF_EXTENT, -WORLD_DEFAULT_HALF_EXTENT,
	                                 -WORLD_DEFAULT_HALF_EXTENT, WORLD_DEFAULT_HALF_EXTENT,
	                                 WORLD_DEFAULT_HALF_EXTENT, WORLD_DEFAULT_HALF_EXTENT);
	self->octree = lrg_octree_new (bounds);

	self->root = lrg_entity3d_new_with_name ("root");
	_lrg_entity3d_set_world_recursive (self->root, self);
}

LrgWorld3D *
lrg_world3d_new (void)
{
	return (LrgWorld3D *)g_object_new (LRG_TYPE_WORLD3D, NULL);
}

/* --------------------------------------------------------------------------
 * Entities
 * -------------------------------------------------------------------------- */

LrgEntity3D *
lrg_world3d_get_root (LrgWorld3D *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);
	return self->root;
}

void
lrg_world3d_add_entity (LrgWorld3D  *self,
                        LrgEntity3D *entity)
{
	g_return_if_fail (LRG_IS_WORLD3D (self));
	g_return_if_fail (LRG_IS_ENTITY3D (entity));

	lrg_entity3d_add_child (self->root, entity);
}

gboolean
lrg_world3d_remove_entity (LrgWorld3D  *self,
                           LrgEntity3D *entity)
{
	LrgEntity3D *parent;

	g_return_val_if_fail (LRG_IS_WORLD3D (self), FALSE);
	g_return_val_if_fail (LRG_IS_ENTITY3D (entity), FALSE);

	if (entity == self->root || lrg_entity3d_get_world (entity) != self)
		return FALSE;

	parent = lrg_entity3d_get_parent (entity);
	g_return_val_if_fail (parent != NULL, FALSE);
	return lrg_entity3d_remove_child (parent, entity);
}

void
lrg_world3d_clear (LrgWorld3D *self)
{
	g_return_if_fail (LRG_IS_WORLD3D (self));

	while (lrg_entity3d_get_n_children (self->root) > 0)
		lrg_entity3d_remove_child (self->root,
		                           lrg_entity3d_get_child (self->root,
		                                                   lrg_entity3d_get_n_children (self->root) - 1));
}

guint
lrg_world3d_get_entity_count (LrgWorld3D *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), 0);
	return self->entity_count;
}

LrgEntity3D *
lrg_world3d_find_by_guid (LrgWorld3D  *self,
                          const gchar *guid)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);
	g_return_val_if_fail (guid != NULL, NULL);
	return (LrgEntity3D *)g_hash_table_lookup (self->guid_map, guid);
}

LrgEntity3D *
lrg_world3d_find_by_name (LrgWorld3D  *self,
                          const gchar *name)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);
	return lrg_entity3d_find_child (self->root, name, TRUE);
}

LrgEntity3D *
lrg_world3d_find_by_path (LrgWorld3D  *self,
                          const gchar *path)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	return lrg_entity3d_find_by_path (self->root, path);
}

/* Collector state for the tree walks below. */
typedef struct
{
	GPtrArray   *out;
	const gchar *tag;
	GType        type;
	LrgEntity3D *root;
} Collect;

static gboolean
collect_tag (LrgEntity3D *entity,
             gpointer     user_data)
{
	Collect *c;

	c = (Collect *)user_data;
	if (entity != c->root && lrg_entity3d_has_tag (entity, c->tag))
		g_ptr_array_add (c->out, entity);
	return TRUE;
}

static gboolean
collect_with_component (LrgEntity3D *entity,
                        gpointer     user_data)
{
	Collect *c;

	c = (Collect *)user_data;
	if (entity != c->root && lrg_entity3d_has_component (entity, c->type))
		g_ptr_array_add (c->out, entity);
	return TRUE;
}

static gboolean
collect_components (LrgEntity3D *entity,
                    gpointer     user_data)
{
	Collect *c;
	GPtrArray *found;
	guint i;

	c = (Collect *)user_data;
	found = lrg_entity3d_get_components_of_type (entity, c->type);
	for (i = 0; i < found->len; i++)
		g_ptr_array_add (c->out, g_ptr_array_index (found, i));
	g_ptr_array_unref (found);
	return TRUE;
}

GPtrArray *
lrg_world3d_find_by_tag (LrgWorld3D  *self,
                         const gchar *tag)
{
	Collect c;

	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);
	g_return_val_if_fail (tag != NULL, NULL);

	c.out = g_ptr_array_new ();
	c.tag = tag;
	c.type = G_TYPE_INVALID;
	c.root = self->root;
	lrg_entity3d_foreach (self->root, collect_tag, &c);
	return c.out;
}

GPtrArray *
lrg_world3d_find_with_component (LrgWorld3D *self,
                                 GType       component_type)
{
	Collect c;

	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);

	c.out = g_ptr_array_new ();
	c.tag = NULL;
	c.type = component_type;
	c.root = self->root;
	lrg_entity3d_foreach (self->root, collect_with_component, &c);
	return c.out;
}

GPtrArray *
lrg_world3d_get_components_of_type (LrgWorld3D *self,
                                    GType       component_type)
{
	Collect c;

	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);

	c.out = g_ptr_array_new ();
	c.tag = NULL;
	c.type = component_type;
	c.root = self->root;
	lrg_entity3d_foreach (self->root, collect_components, &c);
	return c.out;
}

/* --------------------------------------------------------------------------
 * Frame phases
 * -------------------------------------------------------------------------- */

typedef enum
{
	PHASE_UPDATE,
	PHASE_FIXED,
	PHASE_LATE
} Phase;

/*
 * run_phase:
 *
 * Depth-first, pre-order walk over active entities; components in attach
 * order.  Children and components are snapshotted (with refs) per entity
 * so a component may add or remove entities/components while the phase
 * runs: additions are seen next frame, removals take effect immediately
 * for not-yet-visited siblings only through their active flag.
 */
static void
run_phase (LrgEntity3D *entity,
           Phase        phase,
           gfloat       dt)
{
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) children = NULL;
	guint i;

	if (!lrg_entity3d_get_active (entity))
		return;

	components = lrg_entity3d_get_components (entity);
	g_ptr_array_set_free_func (components, g_object_unref);
	for (i = 0; i < components->len; i++)
		g_object_ref (g_ptr_array_index (components, i));

	for (i = 0; i < components->len; i++)
	{
		LrgComponent *c;

		c = (LrgComponent *)g_ptr_array_index (components, i);
		if (lrg_component_get_entity (c) != entity)
			continue;   /* removed by an earlier component this phase */

		switch (phase)
		{
		case PHASE_UPDATE:
			lrg_component_update (c, dt);
			break;
		case PHASE_FIXED:
			lrg_component_fixed_update (c, dt);
			break;
		case PHASE_LATE:
			lrg_component_late_update (c, dt);
			break;
		default:
			break;
		}
	}

	children = lrg_entity3d_get_children (entity);
	g_ptr_array_set_free_func (children, g_object_unref);
	for (i = 0; i < children->len; i++)
		g_object_ref (g_ptr_array_index (children, i));

	for (i = 0; i < children->len; i++)
	{
		LrgEntity3D *child;

		child = (LrgEntity3D *)g_ptr_array_index (children, i);
		if (lrg_entity3d_get_parent (child) != entity)
			continue;   /* moved away during this phase */
		run_phase (child, phase, dt);
	}
}

void
lrg_world3d_update (LrgWorld3D *self,
                    gfloat      delta)
{
	g_return_if_fail (LRG_IS_WORLD3D (self));

	g_object_ref (self);
	self->time += (gdouble)delta;
	run_phase (self->root, PHASE_UPDATE, delta);
	flush_transforms (self);
	g_object_unref (self);
}

guint
lrg_world3d_fixed_update (LrgWorld3D *self,
                          gfloat      delta)
{
	guint steps;

	g_return_val_if_fail (LRG_IS_WORLD3D (self), 0);

	g_object_ref (self);
	self->accumulator += delta;
	steps = 0;

	while (self->accumulator >= self->fixed_step && steps < self->max_substeps)
	{
		run_phase (self->root, PHASE_FIXED, self->fixed_step);
		flush_transforms (self);
		self->accumulator -= self->fixed_step;
		self->fixed_time += (gdouble)self->fixed_step;
		steps++;
	}

	/* Spiral-of-death guard: drop whole steps we could not run. */
	if (self->accumulator >= self->fixed_step)
		self->accumulator = fmodf (self->accumulator, self->fixed_step);

	g_object_unref (self);
	return steps;
}

void
lrg_world3d_late_update (LrgWorld3D *self,
                         gfloat      delta)
{
	g_return_if_fail (LRG_IS_WORLD3D (self));

	g_object_ref (self);
	run_phase (self->root, PHASE_LATE, delta);
	flush_transforms (self);
	g_object_unref (self);
}

void
lrg_world3d_step (LrgWorld3D *self,
                  gfloat      delta)
{
	g_return_if_fail (LRG_IS_WORLD3D (self));

	if (self->paused)
		return;

	lrg_world3d_fixed_update (self, delta);
	lrg_world3d_update (self, delta);
	lrg_world3d_late_update (self, delta);
}

gfloat
lrg_world3d_get_fixed_step (LrgWorld3D *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), 0.0f);
	return self->fixed_step;
}

void
lrg_world3d_set_fixed_step (LrgWorld3D *self,
                            gfloat      step)
{
	g_return_if_fail (LRG_IS_WORLD3D (self));
	g_return_if_fail (step > 0.0f);

	if (self->fixed_step == step)
		return;
	self->fixed_step = step;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FIXED_STEP]);
}

guint
lrg_world3d_get_max_substeps (LrgWorld3D *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), 0);
	return self->max_substeps;
}

void
lrg_world3d_set_max_substeps (LrgWorld3D *self,
                              guint       max_substeps)
{
	g_return_if_fail (LRG_IS_WORLD3D (self));
	g_return_if_fail (max_substeps >= 1);

	if (self->max_substeps == max_substeps)
		return;
	self->max_substeps = max_substeps;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_SUBSTEPS]);
}

gfloat
lrg_world3d_get_fixed_alpha (LrgWorld3D *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), 0.0f);
	return self->accumulator / self->fixed_step;
}

gdouble
lrg_world3d_get_time (LrgWorld3D *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), 0.0);
	return self->time;
}

gdouble
lrg_world3d_get_fixed_time (LrgWorld3D *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), 0.0);
	return self->fixed_time;
}

gboolean
lrg_world3d_get_paused (LrgWorld3D *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), FALSE);
	return self->paused;
}

void
lrg_world3d_set_paused (LrgWorld3D *self,
                        gboolean    paused)
{
	g_return_if_fail (LRG_IS_WORLD3D (self));

	paused = !!paused;
	if (self->paused == paused)
		return;
	self->paused = paused;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAUSED]);
}

/* --------------------------------------------------------------------------
 * Rendering
 * -------------------------------------------------------------------------- */

/*
 * camera_view_projection:
 *
 * Builds the view-projection matrix of an #LrgCamera3D from its position,
 * target, up, fovy and aspect (near/far fixed at WORLD_CAMERA_NEAR/FAR,
 * since LrgCamera3D does not expose clip planes).
 */
static void
camera_view_projection (LrgCamera3D *camera,
                        gfloat      *out)
{
	g_autoptr(GrlVector3) pos = NULL;
	g_autoptr(GrlVector3) target = NULL;
	g_autoptr(GrlVector3) up = NULL;
	gfloat eye[3];
	gfloat tgt[3];
	gfloat upv[3];
	gfloat view[16];
	gfloat proj[16];
	gfloat aspect;
	gfloat fovy;

	pos = lrg_camera3d_get_position (camera);
	target = lrg_camera3d_get_target (camera);
	up = lrg_camera3d_get_up (camera);
	eye[0] = pos->x;
	eye[1] = pos->y;
	eye[2] = pos->z;
	tgt[0] = target->x;
	tgt[1] = target->y;
	tgt[2] = target->z;
	upv[0] = up->x;
	upv[1] = up->y;
	upv[2] = up->z;

	aspect = lrg_camera3d_get_aspect (camera);
	if (aspect <= 0.0f)
		aspect = 16.0f / 9.0f;
	fovy = lrg_camera3d_get_fovy (camera);

	lrg_math3d_look_at (eye, tgt, upv, view);
	if (lrg_camera3d_get_projection (camera) == LRG_PROJECTION_ORTHOGRAPHIC)
	{
		gfloat half_h;
		gfloat half_w;

		half_h = 0.5f * fovy;
		half_w = half_h * aspect;
		lrg_math3d_ortho (-half_w, half_w, -half_h, half_h,
		                  WORLD_CAMERA_NEAR, WORLD_CAMERA_FAR, proj);
	}
	else
	{
		lrg_math3d_perspective (fovy * (gfloat)(G_PI / 180.0), aspect,
		                        WORLD_CAMERA_NEAR, WORLD_CAMERA_FAR, proj);
	}
	lrg_math3d_multiply (proj, view, out);
}

typedef struct
{
	LrgRenderList *list;
	guint32        mask;
} CollectRender;

static gboolean
collect_render_visit (LrgEntity3D *entity,
                      gpointer     user_data)
{
	CollectRender *cr;
	GPtrArray *components;
	guint i;

	cr = (CollectRender *)user_data;

	/* Inactive or hidden subtrees are skipped entirely. */
	if (!lrg_entity3d_get_active (entity) || !lrg_entity3d_get_visible (entity))
		return FALSE;

	/* Layer filter per entity; children may sit on other layers. */
	if ((cr->mask & lrg_entity3d_get_layer_mask (entity)) == 0)
		return TRUE;

	components = lrg_entity3d_get_components (entity);
	for (i = 0; i < components->len; i++)
		lrg_component_collect_render ((LrgComponent *)g_ptr_array_index (components, i),
		                              cr->list);
	g_ptr_array_unref (components);
	return TRUE;
}

void
lrg_world3d_collect_render (LrgWorld3D    *self,
                            LrgCamera     *camera,
                            LrgRenderList *list)
{
	CollectRender cr;
	gfloat eye[3];
	gfloat vp[16];
	gboolean has_vp;
	guint32 mask;
	guint i;

	g_return_if_fail (LRG_IS_WORLD3D (self));
	g_return_if_fail (camera == NULL || LRG_IS_CAMERA (camera));
	g_return_if_fail (list != NULL);

	/* Settle pending transform signals before reading matrices. */
	flush_transforms (self);

	lrg_render_list_clear (list);

	/* View state from the camera. */
	eye[0] = eye[1] = eye[2] = 0.0f;
	mask = LRG_RENDER_LAYERS_MASK_ALL;
	has_vp = FALSE;
	if (camera != NULL)
	{
		mask = lrg_camera_get_cull_mask (camera);
		if (LRG_IS_CAMERA3D (camera))
		{
			g_autoptr(GrlVector3) pos = NULL;

			pos = lrg_camera3d_get_position (LRG_CAMERA3D (camera));
			eye[0] = pos->x;
			eye[1] = pos->y;
			eye[2] = pos->z;
			if (self->frustum_culling)
			{
				camera_view_projection (LRG_CAMERA3D (camera), vp);
				has_vp = TRUE;
			}
		}
	}
	lrg_render_list_set_view (list, eye, mask, has_vp ? vp : NULL);

	/* Walk the tree. */
	cr.list = list;
	cr.mask = mask;
	lrg_entity3d_foreach (self->root, collect_render_visit, &cr);

	/* Lights affecting what this camera sees. */
	for (i = 0; i < self->lights->len; i++)
	{
		const LrgLight3DDesc *light;

		light = (const LrgLight3DDesc *)g_ptr_array_index (self->lights, i);
		if (light->enabled && (light->layer_mask & mask) != 0)
			lrg_render_list_add_light (list, light);
	}

	lrg_render_list_sort (list);
}

gboolean
lrg_world3d_get_frustum_culling (LrgWorld3D *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), FALSE);
	return self->frustum_culling;
}

void
lrg_world3d_set_frustum_culling (LrgWorld3D *self,
                                 gboolean    enabled)
{
	g_return_if_fail (LRG_IS_WORLD3D (self));

	enabled = !!enabled;
	if (self->frustum_culling == enabled)
		return;
	self->frustum_culling = enabled;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRUSTUM_CULLING]);
}

/* --------------------------------------------------------------------------
 * Spatial index
 * -------------------------------------------------------------------------- */

static gboolean
reindex_visit (LrgEntity3D *entity,
               gpointer     user_data)
{
	LrgWorld3D *self;
	SpatialRec *rec;

	self = LRG_WORLD3D (user_data);
	rec = (SpatialRec *)g_hash_table_lookup (self->spatial, entity);
	if (rec == NULL)
		return TRUE;   /* the root */

	lrg_entity3d_get_world_bounds (entity, &rec->bounds);
	if (rec->in_octree)
		lrg_octree_remove (self->octree, entity);
	rec->in_octree = lrg_octree_insert (self->octree, entity, &rec->bounds);
	return TRUE;
}

/*
 * refresh_spatial:
 *
 * Re-indexes the subtree of every dirty entity (moved, joined, component
 * list changed, or explicitly invalidated).  Entities whose bounds lie
 * outside the octree stay with in_octree = FALSE and are scanned linearly
 * by the queries.
 */
static void
refresh_spatial (LrgWorld3D *self)
{
	g_autoptr(GPtrArray) dirty = NULL;
	GHashTableIter iter;
	gpointer key;
	guint i;

	if (g_hash_table_size (self->spatial_dirty) == 0)
		return;

	dirty = g_ptr_array_new ();
	g_hash_table_iter_init (&iter, self->spatial_dirty);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_ptr_array_add (dirty, key);
	g_hash_table_remove_all (self->spatial_dirty);

	for (i = 0; i < dirty->len; i++)
	{
		LrgEntity3D *entity;

		entity = (LrgEntity3D *)g_ptr_array_index (dirty, i);
		if (lrg_entity3d_get_world (entity) == self)
			lrg_entity3d_foreach (entity, reindex_visit, self);
	}
}

void
lrg_world3d_invalidate_bounds (LrgWorld3D  *self,
                               LrgEntity3D *entity)
{
	g_return_if_fail (LRG_IS_WORLD3D (self));
	g_return_if_fail (LRG_IS_ENTITY3D (entity));
	g_return_if_fail (lrg_entity3d_get_world (entity) == self);

	g_hash_table_add (self->spatial_dirty, entity);
}

LrgOctree *
lrg_world3d_get_octree (LrgWorld3D *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);

	refresh_spatial (self);
	return self->octree;
}

/*
 * query_common:
 *
 * Octree candidates plus a linear pass over overflow entities; keeps
 * entities active in the hierarchy whose bounds pass the exact test.
 */
static GPtrArray *
query_common (LrgWorld3D             *self,
              const LrgBoundingBox3D *box,
              const gfloat           *center,
              gfloat                  radius)
{
	GPtrArray *out;
	GPtrArray *candidates;
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	guint i;

	refresh_spatial (self);
	out = g_ptr_array_new ();

	/* Octree candidates. */
	if (box != NULL)
	{
		candidates = lrg_octree_query_box (self->octree, box);
	}
	else
	{
		GrlVector3 c;

		c.x = center[0];
		c.y = center[1];
		c.z = center[2];
		candidates = lrg_octree_query_sphere (self->octree, &c, radius > 0.0f ? radius : 1e-6f);
	}

	for (i = 0; i < candidates->len; i++)
	{
		LrgEntity3D *e;
		SpatialRec *rec;
		gboolean hit;

		e = (LrgEntity3D *)g_ptr_array_index (candidates, i);
		rec = (SpatialRec *)g_hash_table_lookup (self->spatial, e);
		if (rec == NULL || !lrg_entity3d_is_active_in_hierarchy (e))
			continue;
		hit = box != NULL ? bounds_intersect (&rec->bounds, box)
		                  : bounds_intersect_sphere (&rec->bounds, center, radius);
		if (hit)
			g_ptr_array_add (out, e);
	}
	g_ptr_array_unref (candidates);

	/* Overflow entities (outside the octree). */
	g_hash_table_iter_init (&iter, self->spatial);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		SpatialRec *rec;
		gboolean hit;

		rec = (SpatialRec *)value;
		if (rec->in_octree || !lrg_entity3d_is_active_in_hierarchy ((LrgEntity3D *)key))
			continue;
		hit = box != NULL ? bounds_intersect (&rec->bounds, box)
		                  : bounds_intersect_sphere (&rec->bounds, center, radius);
		if (hit)
			g_ptr_array_add (out, key);
	}
	return out;
}

GPtrArray *
lrg_world3d_query_box (LrgWorld3D             *self,
                       const LrgBoundingBox3D *box)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);
	g_return_val_if_fail (box != NULL, NULL);
	return query_common (self, box, NULL, 0.0f);
}

GPtrArray *
lrg_world3d_query_sphere (LrgWorld3D   *self,
                          const gfloat *center,
                          gfloat        radius)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);
	g_return_val_if_fail (center != NULL, NULL);
	g_return_val_if_fail (radius >= 0.0f, NULL);
	return query_common (self, NULL, center, radius);
}

/* --------------------------------------------------------------------------
 * Lights, cameras, listener
 * -------------------------------------------------------------------------- */

static gint
find_light (LrgWorld3D *self,
            GObject    *source)
{
	guint i;

	for (i = 0; i < self->lights->len; i++)
	{
		if (((LrgLight3DDesc *)g_ptr_array_index (self->lights, i))->source == source)
			return (gint)i;
	}
	return -1;
}

void
lrg_world3d_register_light (LrgWorld3D           *self,
                            GObject              *source,
                            const LrgLight3DDesc *desc)
{
	LrgLight3DDesc *copy;
	gint index;

	g_return_if_fail (LRG_IS_WORLD3D (self));
	g_return_if_fail (G_IS_OBJECT (source));
	g_return_if_fail (desc != NULL);

	index = find_light (self, source);
	if (index >= 0)
	{
		/* Update in place: keeps registration order stable. */
		copy = (LrgLight3DDesc *)g_ptr_array_index (self->lights, index);
		*copy = *desc;
		copy->source = source;
		return;
	}

	copy = lrg_light3d_desc_copy (desc);
	copy->source = source;
	g_ptr_array_add (self->lights, copy);
}

gboolean
lrg_world3d_unregister_light (LrgWorld3D *self,
                              GObject    *source)
{
	gint index;

	g_return_val_if_fail (LRG_IS_WORLD3D (self), FALSE);

	index = find_light (self, source);
	if (index < 0)
		return FALSE;
	g_ptr_array_remove_index (self->lights, (guint)index);
	return TRUE;
}

GPtrArray *
lrg_world3d_get_lights (LrgWorld3D *self)
{
	GPtrArray *out;
	guint i;

	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);

	out = g_ptr_array_sized_new (self->lights->len);
	for (i = 0; i < self->lights->len; i++)
		g_ptr_array_add (out, g_ptr_array_index (self->lights, i));
	return out;
}

guint
lrg_world3d_get_n_lights (LrgWorld3D *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), 0);
	return self->lights->len;
}

static gint
find_camera (LrgWorld3D *self,
             LrgCamera  *camera)
{
	guint i;

	for (i = 0; i < self->cameras->len; i++)
	{
		if (((CameraEntry *)g_ptr_array_index (self->cameras, i))->camera == camera)
			return (gint)i;
	}
	return -1;
}

void
lrg_world3d_register_camera (LrgWorld3D *self,
                             LrgCamera  *camera,
                             gint        priority)
{
	CameraEntry *entry;
	gint index;

	g_return_if_fail (LRG_IS_WORLD3D (self));
	g_return_if_fail (LRG_IS_CAMERA (camera));

	index = find_camera (self, camera);
	if (index >= 0)
	{
		((CameraEntry *)g_ptr_array_index (self->cameras, index))->priority = priority;
	}
	else
	{
		entry = g_new0 (CameraEntry, 1);
		entry->camera = (LrgCamera *)g_object_ref (camera);
		entry->priority = priority;
		g_ptr_array_add (self->cameras, entry);
	}
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_CAMERA]);
}

gboolean
lrg_world3d_unregister_camera (LrgWorld3D *self,
                               LrgCamera  *camera)
{
	gint index;

	g_return_val_if_fail (LRG_IS_WORLD3D (self), FALSE);

	index = find_camera (self, camera);
	if (index < 0)
		return FALSE;

	if (self->active_camera == camera)
		g_clear_object (&self->active_camera);
	g_ptr_array_remove_index (self->cameras, (guint)index);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_CAMERA]);
	return TRUE;
}

GPtrArray *
lrg_world3d_get_cameras (LrgWorld3D *self)
{
	GPtrArray *out;
	guint i;

	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);

	out = g_ptr_array_sized_new (self->cameras->len);
	for (i = 0; i < self->cameras->len; i++)
		g_ptr_array_add (out, ((CameraEntry *)g_ptr_array_index (self->cameras, i))->camera);
	return out;
}

void
lrg_world3d_set_active_camera (LrgWorld3D *self,
                               LrgCamera  *camera)
{
	g_return_if_fail (LRG_IS_WORLD3D (self));
	g_return_if_fail (camera == NULL || LRG_IS_CAMERA (camera));

	if (g_set_object (&self->active_camera, camera))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_CAMERA]);
}

LrgCamera *
lrg_world3d_get_active_camera (LrgWorld3D *self)
{
	CameraEntry *best;
	guint i;

	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);

	if (self->active_camera != NULL)
		return self->active_camera;

	/* Highest priority; earliest registered wins ties (strict >). */
	best = NULL;
	for (i = 0; i < self->cameras->len; i++)
	{
		CameraEntry *entry;

		entry = (CameraEntry *)g_ptr_array_index (self->cameras, i);
		if (best == NULL || entry->priority > best->priority)
			best = entry;
	}
	return best != NULL ? best->camera : NULL;
}

void
lrg_world3d_set_audio_listener (LrgWorld3D  *self,
                                LrgEntity3D *listener)
{
	g_return_if_fail (LRG_IS_WORLD3D (self));
	g_return_if_fail (listener == NULL || LRG_IS_ENTITY3D (listener));

	if (self->audio_listener == listener)
		return;
	self->audio_listener = listener;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUDIO_LISTENER]);
}

LrgEntity3D *
lrg_world3d_get_audio_listener (LrgWorld3D *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);
	return self->audio_listener;
}

/* ------------------------------------------------------------------ look */

void
lrg_world3d_set_environment (LrgWorld3D     *self,
                             LrgEnvironment *environment)
{
	g_return_if_fail (LRG_IS_WORLD3D (self));
	g_return_if_fail (environment == NULL || LRG_IS_ENVIRONMENT (environment));

	g_set_object (&self->environment, environment);
}

LrgEnvironment *
lrg_world3d_get_environment (LrgWorld3D *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);

	return self->environment;
}

void
lrg_world3d_set_post_stack (LrgWorld3D   *self,
                            LrgPostStack *stack)
{
	g_return_if_fail (LRG_IS_WORLD3D (self));
	g_return_if_fail (stack == NULL || LRG_IS_POST_STACK (stack));

	g_set_object (&self->post_stack, stack);
}

LrgPostStack *
lrg_world3d_get_post_stack (LrgWorld3D *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D (self), NULL);

	return self->post_stack;
}
