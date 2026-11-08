/* lrg-lod-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLodComponent: selects a level of an LrgLodGroup per view and submits
 * its models as render items.
 *
 * Per view state: the selection is stateful (hysteresis, cross-fade), so
 * the component keeps one LrgLodState per LrgRenderList (renderers and
 * editor viewports each own their list). The fade clock is the world
 * time accumulated in update() once the world steps, and wall-clock time
 * before that (an editor that never steps the world still fades).
 *
 * Fade hand-off: every item carries its lodFade value packed into
 * item.user_data (the float's bits; no destroy notify, the renderer does
 * not use user_data for MODEL items). lrg_lod_component_get_item_fade()
 * unpacks it for the renderer.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-lod-component.h"
#include "../lrg-entity3d.h"
#include "../../graphics/lrg-camera3d.h"

/* Views beyond this many are assumed stale and forgotten. */
#define MAX_VIEWS (32)

/* Fade steps are clamped so a hitch does not skip a fade entirely. */
#define MAX_FADE_STEP (0.25)

typedef struct
{
	LrgLodState state;
	gdouble     last_clock;
	gint64      last_real;
} LodView;

struct _LrgLodComponent
{
	LrgComponent     parent_instance;

	/* Source */
	LrgLodGroup     *group;           /* owned ref */
	gboolean         group_explicit;
	gchar           *mesh_asset;
	LrgAssetManager *asset_manager;
	gboolean         load_failed;

	/* Overrides */
	gint             force_level;
	gfloat           bias;

	/* Render state */
	gboolean         cast_shadows;
	gboolean         receive_shadows;
	gint             layer;
	gfloat           color[4];
	GObject         *material;

	/* Per view selection */
	GHashTable      *views;           /* LrgRenderList* -> LodView* */
	gdouble          clock;
	gboolean         clock_ticked;
	gint             current_level;
	gfloat           screen_size;
};

G_DEFINE_FINAL_TYPE (LrgLodComponent, lrg_lod_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_GROUP,
	PROP_MESH_ASSET,
	PROP_ASSET_MANAGER,
	PROP_FORCE_LEVEL,
	PROP_BIAS,
	PROP_CAST_SHADOWS,
	PROP_RECEIVE_SHADOWS,
	PROP_LAYER,
	PROP_COLOR_R,
	PROP_COLOR_G,
	PROP_COLOR_B,
	PROP_COLOR_A,
	PROP_MATERIAL,
	PROP_CURRENT_LEVEL,
	PROP_SCREEN_SIZE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Shared generated groups
 * ========================================================================== */

/* "model pointer|settings hash" -> GWeakRef to the generated group. A
 * group holds its source model, so while the weak ref is alive the model
 * pointer cannot have been reused. Main thread only (like rendering). */
static GHashTable *shared_groups = NULL;

static void
weak_ref_free (gpointer data)
{
	g_weak_ref_clear ((GWeakRef *) data);
	g_free (data);
}

static LrgLodGroup *
shared_group_lookup (const gchar *key)
{
	GWeakRef *ref;

	if (shared_groups == NULL)
		return NULL;
	ref = (GWeakRef *) g_hash_table_lookup (shared_groups, key);
	if (ref == NULL)
		return NULL;
	return (LrgLodGroup *) g_weak_ref_get (ref);
}

static void
shared_group_store (const gchar *key,
                    LrgLodGroup *group)
{
	GWeakRef *ref;

	if (shared_groups == NULL)
		shared_groups = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, weak_ref_free);
	ref = g_new0 (GWeakRef, 1);
	g_weak_ref_init (ref, group);
	g_hash_table_replace (shared_groups, g_strdup (key), ref);
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void
invalidate_bounds (LrgLodComponent *self)
{
	LrgEntity3D *entity = lrg_component_get_entity (LRG_COMPONENT (self));
	LrgWorld3D  *world = (entity != NULL) ? lrg_entity3d_get_world (entity) : NULL;

	if (world != NULL)
		lrg_world3d_invalidate_bounds (world, entity);
}

/*
 * resolve_group:
 *
 * Loads the mesh asset and builds (or reuses) its LOD chain from the
 * `.meta` keys, once. Failures are remembered until the asset or the
 * manager changes; a chain that cannot be generated falls back to the
 * model alone.
 */
static void
resolve_group (LrgLodComponent *self)
{
	g_autoptr(GError)       error = NULL;
	g_autoptr(LrgAssetMeta) meta = NULL;
	g_autofree gchar       *hash = NULL;
	g_autofree gchar       *key = NULL;
	GrlModel               *model;
	LrgLodGroup            *group;

	if (self->group != NULL || self->mesh_asset == NULL || self->asset_manager == NULL ||
	    self->load_failed)
		return;

	model = lrg_asset_manager_load_model (self->asset_manager, self->mesh_asset, &error);
	if (model == NULL)
	{
		g_debug ("LrgLodComponent: cannot load '%s': %s", self->mesh_asset,
		         error != NULL ? error->message : "unknown error");
		self->load_failed = TRUE;
		return;
	}

	if (lrg_asset_manager_get_apply_import_settings (self->asset_manager))
		meta = lrg_asset_manager_get_asset_meta (self->asset_manager, self->mesh_asset);
	hash = (meta != NULL) ? lrg_asset_meta_get_settings_hash (meta) : g_strdup ("-");
	key = g_strdup_printf ("%p|%s", (gpointer) model, hash);

	group = shared_group_lookup (key);
	if (group == NULL)
	{
		group = lrg_lod_group_new_from_model_with_meta (model, meta, &error);
		if (group == NULL)
		{
			g_warning ("LrgLodComponent: no LOD chain for '%s': %s", self->mesh_asset,
			           error != NULL ? error->message : "unknown error");
			group = lrg_lod_group_new ();
			lrg_lod_group_add_level (group, model, 0.0f);
		}
		shared_group_store (key, group);
	}
	self->group = group;
	g_hash_table_remove_all (self->views);
	invalidate_bounds (self);
}

/* The view state of @list, created (uninitialised) on first use. */
static LodView *
view_for_list (LrgLodComponent *self,
               LrgRenderList   *list)
{
	LodView *view = (LodView *) g_hash_table_lookup (self->views, list);

	if (view != NULL)
		return view;
	if (g_hash_table_size (self->views) >= MAX_VIEWS)
		g_hash_table_remove_all (self->views);
	view = g_new0 (LodView, 1);
	lrg_lod_state_init (&view->state);
	view->last_clock = self->clock;
	view->last_real = g_get_monotonic_time ();
	g_hash_table_insert (self->views, list, view);
	return view;
}

/* Seconds since @view was last drawn: world time once update() runs,
 * wall-clock time before. */
static gfloat
view_delta (LrgLodComponent *self,
            LodView         *view)
{
	gint64  now = g_get_monotonic_time ();
	gdouble dt;

	if (self->clock_ticked)
		dt = self->clock - view->last_clock;
	else
		dt = (gdouble) (now - view->last_real) / (gdouble) G_USEC_PER_SEC;
	view->last_clock = self->clock;
	view->last_real = now;
	return (gfloat) CLAMP (dt, 0.0, MAX_FADE_STEP);
}

/*
 * measure_screen_size:
 *
 * The group's bounding sphere moved to world space by the entity matrix
 * (radius scaled by the largest axis scale), measured against the list's
 * view-projection; without one (frustum culling off) by distance to the
 * eye with the world's active camera fovy (45 degrees if none).
 */
static gfloat
measure_screen_size (LrgLodComponent *self,
                     LrgEntity3D     *entity,
                     LrgRenderList   *list)
{
	gfloat center[3] = { 0.0f, 0.0f, 0.0f };
	gfloat radius = 1.0f;
	gfloat m[16];
	gfloat wc[3];
	gfloat vp[16];
	gfloat scale = 0.0f;
	guint  c;

	lrg_lod_group_get_bounding_sphere (self->group, center, &radius);
	lrg_entity3d_get_world_matrix (entity, m);
	for (c = 0; c < 3; c++)
	{
		gfloat len = sqrtf (m[c * 4] * m[c * 4] + m[c * 4 + 1] * m[c * 4 + 1] + m[c * 4 + 2] * m[c * 4 + 2]);

		scale = MAX (scale, len);
	}
	for (c = 0; c < 3; c++)
		wc[c] = m[c] * center[0] + m[4 + c] * center[1] + m[8 + c] * center[2] + m[12 + c];
	radius *= scale;

	if (lrg_render_list_get_view_projection (list, vp))
		return lrg_lod_screen_size_from_view_projection (vp, wc, radius);

	{
		gfloat      eye[3];
		gfloat      d[3];
		gfloat      fovy = 45.0f;
		LrgWorld3D *world = lrg_entity3d_get_world (entity);
		LrgCamera  *camera = (world != NULL) ? lrg_world3d_get_active_camera (world) : NULL;

		lrg_render_list_get_eye (list, eye);
		for (c = 0; c < 3; c++)
			d[c] = wc[c] - eye[c];
		if (camera != NULL && LRG_IS_CAMERA3D (camera))
			fovy = lrg_camera3d_get_fovy (LRG_CAMERA3D (camera));
		return lrg_lod_compute_screen_size (radius, sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), fovy);
	}
}

/* float -> pointer-sized bits and back (the fade travels in user_data). */
static gpointer
pack_fade (gfloat fade)
{
	union { gfloat f; guint32 u; } v;

	v.f = fade;
	return GUINT_TO_POINTER (v.u);
}

static gfloat
unpack_fade (gconstpointer data)
{
	union { gfloat f; guint32 u; } v;

	v.u = (guint32) GPOINTER_TO_UINT (data);
	return v.f;
}

/* Submits every model of @level with @fade. */
static void
submit_level (LrgLodComponent *self,
              LrgEntity3D     *entity,
              LrgRenderList   *list,
              guint            level,
              gfloat           fade,
              gboolean         casts)
{
	GPtrArray        *models = lrg_lod_group_get_level_models (self->group, level);
	LrgBoundingBox3D  local;
	gboolean          has_local;
	gfloat            mn[3];
	gfloat            mx[3];
	guint             i;

	if (models == NULL)
		return;
	has_local = lrg_lod_group_get_bounds (self->group, mn, mx);
	if (has_local)
	{
		local.min.x = mn[0];
		local.min.y = mn[1];
		local.min.z = mn[2];
		local.max.x = mx[0];
		local.max.y = mx[1];
		local.max.z = mx[2];
	}

	for (i = 0; i < models->len; i++)
	{
		LrgRenderItem item;
		GrlModel     *model = g_ptr_array_index (models, i);

		lrg_render_item_init_for_entity (&item, entity, LRG_COMPONENT (self));
		item.kind = LRG_RENDER_ITEM_MODEL;
		item.model = model;
		item.mesh_index = -1;
		item.material = self->material;
		memcpy (item.color, self->color, sizeof item.color);
		item.flags = LRG_RENDER_ITEM_FLAG_NONE;
		if (casts)
			item.flags |= LRG_RENDER_ITEM_FLAG_CAST_SHADOW;
		if (self->receive_shadows)
			item.flags |= LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW;
		if (self->layer >= 0)
			item.layer = (guint) self->layer;
		/* Group by model like the mesh renderer (kind in the top byte). */
		item.sort_key = ((guint64) LRG_RENDER_ITEM_MODEL << 56) |
		                ((guint64) (guintptr) model & G_GUINT64_CONSTANT (0x00ffffffffffffff));
		item.user_data = pack_fade (fade);
		item.user_data_destroy = NULL;
		if (has_local)
			lrg_render_item_set_local_bounds (&item, &local);
		if (lrg_render_list_accepts (list, &item))
			lrg_render_list_add (list, &item);
	}
}

/* ==========================================================================
 * LrgComponent vfuncs
 * ========================================================================== */

static void
lrg_lod_component_update (LrgComponent *component,
                          gfloat        delta)
{
	LrgLodComponent *self = LRG_LOD_COMPONENT (component);

	self->clock += (gdouble) MAX (delta, 0.0f);
	self->clock_ticked = TRUE;
}

static gboolean
lrg_lod_component_get_bounds (LrgComponent     *component,
                              LrgBoundingBox3D *out_local)
{
	LrgLodComponent *self = LRG_LOD_COMPONENT (component);
	gfloat           mn[3];
	gfloat           mx[3];

	/* No resolve here: the octree may ask before a window exists, and a
	 * failed load is not retried. resolve_group() invalidates the bounds
	 * once the chain exists. */
	if (self->group == NULL || !lrg_lod_group_get_bounds (self->group, mn, mx))
		return FALSE;
	out_local->min.x = mn[0];
	out_local->min.y = mn[1];
	out_local->min.z = mn[2];
	out_local->max.x = mx[0];
	out_local->max.y = mx[1];
	out_local->max.z = mx[2];
	return TRUE;
}

static void
lrg_lod_component_collect_render (LrgComponent  *component,
                                  LrgRenderList *list)
{
	LrgLodComponent *self = LRG_LOD_COMPONENT (component);
	LrgEntity3D     *entity = lrg_component_get_entity (component);
	LodView         *view;
	gfloat           size;
	gfloat           dt;
	gfloat           fade;
	guint            n;

	if (entity == NULL)
		return;
	resolve_group (self);
	if (self->group == NULL || (n = lrg_lod_group_get_n_levels (self->group)) == 0)
		return;

	/* 1. Measure and select for this view. */
	view = view_for_list (self, list);
	dt = view_delta (self, view);
	size = measure_screen_size (self, entity, list) * self->bias;
	if (self->force_level >= 0)
	{
		view->state.level = MIN (self->force_level, (gint) n - 1);
		view->state.previous_level = LRG_LOD_LEVEL_CULLED;
		view->state.fade = 1.0f;
		view->state.screen_size = size;
		view->state.initialized = TRUE;
	}
	else
		lrg_lod_group_update_state (self->group, &view->state, size, dt);
	self->current_level = view->state.level;
	self->screen_size = view->state.screen_size;

	/* 2. The current level (casts shadows) and the one fading out. */
	if (view->state.level >= 0 && lrg_lod_state_get_level_fade (&view->state, view->state.level, &fade))
		submit_level (self, entity, list, (guint) view->state.level, fade, self->cast_shadows);
	if (lrg_lod_state_is_fading (&view->state) && view->state.previous_level >= 0 &&
	    view->state.previous_level < (gint) n &&
	    lrg_lod_state_get_level_fade (&view->state, view->state.previous_level, &fade))
		submit_level (self, entity, list, (guint) view->state.previous_level, fade, FALSE);
}

static void
lrg_lod_component_world_exited (LrgComponent *component,
                                LrgWorld3D   *world)
{
	(void) world;
	lrg_lod_component_reset_states (LRG_LOD_COMPONENT (component));
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgLodComponent *
lrg_lod_component_new (void)
{
	return g_object_new (LRG_TYPE_LOD_COMPONENT, NULL);
}

LrgLodComponent *
lrg_lod_component_new_with_group (LrgLodGroup *group)
{
	g_return_val_if_fail (LRG_IS_LOD_GROUP (group), NULL);
	return g_object_new (LRG_TYPE_LOD_COMPONENT, "group", group, NULL);
}

LrgLodGroup *
lrg_lod_component_get_group (LrgLodComponent *self)
{
	g_return_val_if_fail (LRG_IS_LOD_COMPONENT (self), NULL);
	return self->group;
}

void
lrg_lod_component_set_group (LrgLodComponent *self,
                             LrgLodGroup     *group)
{
	g_return_if_fail (LRG_IS_LOD_COMPONENT (self));
	g_return_if_fail (group == NULL || LRG_IS_LOD_GROUP (group));

	if (self->group == group && (group != NULL) == self->group_explicit)
		return;
	g_set_object (&self->group, group);
	self->group_explicit = (group != NULL);
	self->load_failed = FALSE;
	g_hash_table_remove_all (self->views);
	invalidate_bounds (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GROUP]);
}

const gchar *
lrg_lod_component_get_mesh_asset (LrgLodComponent *self)
{
	g_return_val_if_fail (LRG_IS_LOD_COMPONENT (self), NULL);
	return self->mesh_asset;
}

void
lrg_lod_component_set_mesh_asset (LrgLodComponent *self,
                                  const gchar     *asset)
{
	g_return_if_fail (LRG_IS_LOD_COMPONENT (self));

	if (g_strcmp0 (self->mesh_asset, asset) == 0)
		return;
	g_free (self->mesh_asset);
	self->mesh_asset = g_strdup (asset);
	if (!self->group_explicit)
		g_clear_object (&self->group);
	self->load_failed = FALSE;
	g_hash_table_remove_all (self->views);
	invalidate_bounds (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MESH_ASSET]);
}

LrgAssetManager *
lrg_lod_component_get_asset_manager (LrgLodComponent *self)
{
	g_return_val_if_fail (LRG_IS_LOD_COMPONENT (self), NULL);
	return self->asset_manager;
}

void
lrg_lod_component_set_asset_manager (LrgLodComponent *self,
                                     LrgAssetManager *manager)
{
	g_return_if_fail (LRG_IS_LOD_COMPONENT (self));
	g_return_if_fail (manager == NULL || LRG_IS_ASSET_MANAGER (manager));

	if (!g_set_object (&self->asset_manager, manager))
		return;
	if (!self->group_explicit)
		g_clear_object (&self->group);
	self->load_failed = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET_MANAGER]);
}

gint
lrg_lod_component_get_force_level (LrgLodComponent *self)
{
	g_return_val_if_fail (LRG_IS_LOD_COMPONENT (self), -1);
	return self->force_level;
}

void
lrg_lod_component_set_force_level (LrgLodComponent *self,
                                   gint             level)
{
	g_return_if_fail (LRG_IS_LOD_COMPONENT (self));

	level = CLAMP (level, -1, LRG_LOD_MAX_LEVELS - 1);
	if (self->force_level == level)
		return;
	self->force_level = level;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FORCE_LEVEL]);
}

gfloat
lrg_lod_component_get_bias (LrgLodComponent *self)
{
	g_return_val_if_fail (LRG_IS_LOD_COMPONENT (self), 1.0f);
	return self->bias;
}

void
lrg_lod_component_set_bias (LrgLodComponent *self,
                            gfloat           bias)
{
	g_return_if_fail (LRG_IS_LOD_COMPONENT (self));
	g_return_if_fail (bias > 0.0f);

	if (self->bias == bias)
		return;
	self->bias = bias;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BIAS]);
}

gboolean
lrg_lod_component_get_cast_shadows (LrgLodComponent *self)
{
	g_return_val_if_fail (LRG_IS_LOD_COMPONENT (self), FALSE);
	return self->cast_shadows;
}

void
lrg_lod_component_set_cast_shadows (LrgLodComponent *self,
                                    gboolean         cast)
{
	g_return_if_fail (LRG_IS_LOD_COMPONENT (self));

	cast = !!cast;
	if (self->cast_shadows == cast)
		return;
	self->cast_shadows = cast;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAST_SHADOWS]);
}

gboolean
lrg_lod_component_get_receive_shadows (LrgLodComponent *self)
{
	g_return_val_if_fail (LRG_IS_LOD_COMPONENT (self), FALSE);
	return self->receive_shadows;
}

void
lrg_lod_component_set_receive_shadows (LrgLodComponent *self,
                                       gboolean         receive)
{
	g_return_if_fail (LRG_IS_LOD_COMPONENT (self));

	receive = !!receive;
	if (self->receive_shadows == receive)
		return;
	self->receive_shadows = receive;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RECEIVE_SHADOWS]);
}

gint
lrg_lod_component_get_layer (LrgLodComponent *self)
{
	g_return_val_if_fail (LRG_IS_LOD_COMPONENT (self), -1);
	return self->layer;
}

void
lrg_lod_component_set_layer (LrgLodComponent *self,
                             gint             layer)
{
	g_return_if_fail (LRG_IS_LOD_COMPONENT (self));

	layer = CLAMP (layer, -1, 31);
	if (self->layer == layer)
		return;
	self->layer = layer;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYER]);
}

void
lrg_lod_component_get_color (LrgLodComponent *self,
                             gfloat          *out_rgba)
{
	g_return_if_fail (LRG_IS_LOD_COMPONENT (self));
	g_return_if_fail (out_rgba != NULL);
	memcpy (out_rgba, self->color, sizeof self->color);
}

/* Stores channel @i and notifies its property on change. */
static void
set_channel (LrgLodComponent *self,
             guint            i,
             gfloat           value)
{
	value = CLAMP (value, 0.0f, 1.0f);
	if (self->color[i] == value)
		return;
	self->color[i] = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR_R + i]);
}

void
lrg_lod_component_set_color (LrgLodComponent *self,
                             gfloat           r,
                             gfloat           g,
                             gfloat           b,
                             gfloat           a)
{
	g_return_if_fail (LRG_IS_LOD_COMPONENT (self));

	g_object_freeze_notify (G_OBJECT (self));
	set_channel (self, 0, r);
	set_channel (self, 1, g);
	set_channel (self, 2, b);
	set_channel (self, 3, a);
	g_object_thaw_notify (G_OBJECT (self));
}

GObject *
lrg_lod_component_get_material (LrgLodComponent *self)
{
	g_return_val_if_fail (LRG_IS_LOD_COMPONENT (self), NULL);
	return self->material;
}

void
lrg_lod_component_set_material (LrgLodComponent *self,
                                GObject         *material)
{
	g_return_if_fail (LRG_IS_LOD_COMPONENT (self));
	g_return_if_fail (material == NULL || G_IS_OBJECT (material));

	if (g_set_object (&self->material, material))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MATERIAL]);
}

gint
lrg_lod_component_get_current_level (LrgLodComponent *self)
{
	g_return_val_if_fail (LRG_IS_LOD_COMPONENT (self), LRG_LOD_LEVEL_CULLED);
	return self->current_level;
}

gfloat
lrg_lod_component_get_screen_size (LrgLodComponent *self)
{
	g_return_val_if_fail (LRG_IS_LOD_COMPONENT (self), 0.0f);
	return self->screen_size;
}

const LrgLodState *
lrg_lod_component_get_state_for_list (LrgLodComponent *self,
                                      LrgRenderList   *list)
{
	LodView *view;

	g_return_val_if_fail (LRG_IS_LOD_COMPONENT (self), NULL);

	view = (LodView *) g_hash_table_lookup (self->views, list);
	return (view != NULL) ? &view->state : NULL;
}

void
lrg_lod_component_reset_states (LrgLodComponent *self)
{
	g_return_if_fail (LRG_IS_LOD_COMPONENT (self));
	g_hash_table_remove_all (self->views);
}

gfloat
lrg_lod_component_get_item_fade (const LrgRenderItem *item,
                                 gint                *out_level)
{
	LrgLodComponent *self;

	g_return_val_if_fail (item != NULL, 0.0f);

	if (out_level != NULL)
		*out_level = -1;
	if (item->component == NULL || !LRG_IS_LOD_COMPONENT (item->component))
		return 0.0f;
	self = LRG_LOD_COMPONENT (item->component);
	if (out_level != NULL && self->group != NULL && item->model != NULL && GRL_IS_MODEL (item->model))
		*out_level = lrg_lod_group_find_level_of_model (self->group, GRL_MODEL (item->model));
	return unpack_fade (item->user_data);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_lod_component_dispose (GObject *object)
{
	LrgLodComponent *self = LRG_LOD_COMPONENT (object);

	g_clear_object (&self->group);
	g_clear_object (&self->asset_manager);
	g_clear_object (&self->material);

	G_OBJECT_CLASS (lrg_lod_component_parent_class)->dispose (object);
}

static void
lrg_lod_component_finalize (GObject *object)
{
	LrgLodComponent *self = LRG_LOD_COMPONENT (object);

	g_free (self->mesh_asset);
	g_hash_table_unref (self->views);

	G_OBJECT_CLASS (lrg_lod_component_parent_class)->finalize (object);
}

static void
lrg_lod_component_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgLodComponent *self = LRG_LOD_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_GROUP:           g_value_set_object (value, self->group); break;
	case PROP_MESH_ASSET:      g_value_set_string (value, self->mesh_asset); break;
	case PROP_ASSET_MANAGER:   g_value_set_object (value, self->asset_manager); break;
	case PROP_FORCE_LEVEL:     g_value_set_int (value, self->force_level); break;
	case PROP_BIAS:            g_value_set_float (value, self->bias); break;
	case PROP_CAST_SHADOWS:    g_value_set_boolean (value, self->cast_shadows); break;
	case PROP_RECEIVE_SHADOWS: g_value_set_boolean (value, self->receive_shadows); break;
	case PROP_LAYER:           g_value_set_int (value, self->layer); break;
	case PROP_COLOR_R:         g_value_set_float (value, self->color[0]); break;
	case PROP_COLOR_G:         g_value_set_float (value, self->color[1]); break;
	case PROP_COLOR_B:         g_value_set_float (value, self->color[2]); break;
	case PROP_COLOR_A:         g_value_set_float (value, self->color[3]); break;
	case PROP_MATERIAL:        g_value_set_object (value, self->material); break;
	case PROP_CURRENT_LEVEL:   g_value_set_int (value, self->current_level); break;
	case PROP_SCREEN_SIZE:     g_value_set_float (value, self->screen_size); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_lod_component_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgLodComponent *self = LRG_LOD_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_GROUP:           lrg_lod_component_set_group (self, g_value_get_object (value)); break;
	case PROP_MESH_ASSET:      lrg_lod_component_set_mesh_asset (self, g_value_get_string (value)); break;
	case PROP_ASSET_MANAGER:   lrg_lod_component_set_asset_manager (self, g_value_get_object (value)); break;
	case PROP_FORCE_LEVEL:     lrg_lod_component_set_force_level (self, g_value_get_int (value)); break;
	case PROP_BIAS:            lrg_lod_component_set_bias (self, g_value_get_float (value)); break;
	case PROP_CAST_SHADOWS:    lrg_lod_component_set_cast_shadows (self, g_value_get_boolean (value)); break;
	case PROP_RECEIVE_SHADOWS: lrg_lod_component_set_receive_shadows (self, g_value_get_boolean (value)); break;
	case PROP_LAYER:           lrg_lod_component_set_layer (self, g_value_get_int (value)); break;
	case PROP_COLOR_R:         set_channel (self, 0, g_value_get_float (value)); break;
	case PROP_COLOR_G:         set_channel (self, 1, g_value_get_float (value)); break;
	case PROP_COLOR_B:         set_channel (self, 2, g_value_get_float (value)); break;
	case PROP_COLOR_A:         set_channel (self, 3, g_value_get_float (value)); break;
	case PROP_MATERIAL:        lrg_lod_component_set_material (self, g_value_get_object (value)); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_lod_component_class_init (LrgLodComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
	const GParamFlags  ro = G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_lod_component_dispose;
	object_class->finalize = lrg_lod_component_finalize;
	object_class->get_property = lrg_lod_component_get_property;
	object_class->set_property = lrg_lod_component_set_property;

	component_class->update = lrg_lod_component_update;
	component_class->collect_render = lrg_lod_component_collect_render;
	component_class->get_bounds = lrg_lod_component_get_bounds;
	component_class->world_exited = lrg_lod_component_world_exited;

	/**
	 * LrgLodComponent:group:
	 *
	 * The LOD chain drawn (set explicitly, or generated from
	 * #LrgLodComponent:mesh-asset on the first draw).
	 */
	properties[PROP_GROUP] =
		g_param_spec_object ("group", "Group", "LOD chain", LRG_TYPE_LOD_GROUP, rw);

	/**
	 * LrgLodComponent:mesh-asset:
	 *
	 * Model asset (path or `guid:`) whose LOD chain is generated from its
	 * `.meta` LOD keys through #LrgLodComponent:asset-manager.
	 */
	properties[PROP_MESH_ASSET] =
		g_param_spec_string ("mesh-asset", "Mesh asset", "Model asset for a generated chain", NULL, rw);

	/**
	 * LrgLodComponent:asset-manager:
	 *
	 * Loads #LrgLodComponent:mesh-asset and its sidecar.
	 */
	properties[PROP_ASSET_MANAGER] =
		g_param_spec_object ("asset-manager", "Asset manager", "Resolves the mesh asset",
		                     LRG_TYPE_ASSET_MANAGER, rw);

	/**
	 * LrgLodComponent:force-level:
	 *
	 * Level this instance always draws, or -1.
	 */
	properties[PROP_FORCE_LEVEL] =
		g_param_spec_int ("force-level", "Force level", "Level to always draw, or -1",
		                  -1, LRG_LOD_MAX_LEVELS - 1, -1, rw);

	/**
	 * LrgLodComponent:bias:
	 *
	 * Per-instance screen-size multiplier.
	 */
	properties[PROP_BIAS] =
		g_param_spec_float ("bias", "Bias", "Screen-size multiplier", 0.0001f, 10000.0f, 1.0f, rw);

	/**
	 * LrgLodComponent:cast-shadows:
	 *
	 * Whether the current level is a shadow caster.
	 */
	properties[PROP_CAST_SHADOWS] =
		g_param_spec_boolean ("cast-shadows", "Cast shadows", "Current level casts shadows", TRUE, rw);

	/**
	 * LrgLodComponent:receive-shadows:
	 *
	 * Whether the items receive shadows.
	 */
	properties[PROP_RECEIVE_SHADOWS] =
		g_param_spec_boolean ("receive-shadows", "Receive shadows", "Lit with shadow lookups", TRUE, rw);

	/**
	 * LrgLodComponent:layer:
	 *
	 * Render layer override (0 .. 31), or -1 for the entity's layer.
	 */
	properties[PROP_LAYER] =
		g_param_spec_int ("layer", "Layer", "Render layer, or -1", -1, 31, -1, rw);

	/**
	 * LrgLodComponent:color-r:
	 *
	 * Red tint, 0..1.
	 */
	properties[PROP_COLOR_R] =
		g_param_spec_float ("color-r", "Red", "Red tint", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgLodComponent:color-g:
	 *
	 * Green tint, 0..1.
	 */
	properties[PROP_COLOR_G] =
		g_param_spec_float ("color-g", "Green", "Green tint", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgLodComponent:color-b:
	 *
	 * Blue tint, 0..1.
	 */
	properties[PROP_COLOR_B] =
		g_param_spec_float ("color-b", "Blue", "Blue tint", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgLodComponent:color-a:
	 *
	 * Alpha tint, 0..1.
	 */
	properties[PROP_COLOR_A] =
		g_param_spec_float ("color-a", "Alpha", "Alpha tint", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgLodComponent:material:
	 *
	 * Material override passed on every item, or %NULL.
	 */
	properties[PROP_MATERIAL] =
		g_param_spec_object ("material", "Material", "Material override", G_TYPE_OBJECT, rw);

	/**
	 * LrgLodComponent:current-level:
	 *
	 * Level chosen by the last collect_render, or -1.
	 */
	properties[PROP_CURRENT_LEVEL] =
		g_param_spec_int ("current-level", "Current level", "Last selected level",
		                  -1, LRG_LOD_MAX_LEVELS - 1, -1, ro);

	/**
	 * LrgLodComponent:screen-size:
	 *
	 * Biased screen size measured by the last collect_render.
	 */
	properties[PROP_SCREEN_SIZE] =
		g_param_spec_float ("screen-size", "Screen size", "Last measured screen size",
		                    0.0f, G_MAXFLOAT, 0.0f, ro);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_lod_component_init (LrgLodComponent *self)
{
	self->force_level = -1;
	self->bias = 1.0f;
	self->cast_shadows = TRUE;
	self->receive_shadows = TRUE;
	self->layer = -1;
	self->color[0] = self->color[1] = self->color[2] = self->color[3] = 1.0f;
	self->views = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	self->current_level = LRG_LOD_LEVEL_CULLED;
}
