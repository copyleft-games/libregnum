/* lrg-terrain-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTerrainComponent: draws an LrgTerrainAsset (one mesh item per chunk
 * at its distance LOD, textured with a baked splat albedo), draws its
 * foliage, and owns a static heightfield body in the world's physics.
 *
 * Caching: per chunk, one GrlMesh per LOD level (built on first use)
 * and one baked texture.  LrgTerrainAsset::region-changed marks the
 * chunks a change overlaps: heights and holes drop their meshes (heights
 * one sample further, the normals reach across), splat weights and ids
 * re-bake their texture; heights and holes also rebuild the body before
 * the next physics step.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-terrain-component.h"
#include "../lrg-entity3d.h"
#include "../lrg-world3d.h"
#include "../lrg-world3d-physics-private.h"
#include "../../graphics/lrg-terrain-asset-private.h"
#include "../../physics/lrg-physics-shape.h"

/* One chunk's GPU-side cache. */
typedef struct
{
	GrlMesh    *meshes[LRG_TERRAIN_ASSET_MAX_LOD];
	GrlTexture *bake;
	gboolean    bake_dirty;
	gboolean    bounds_dirty;
	gfloat      min_y;
	gfloat      max_y;
	gint        last_lod;
} Chunk;

struct _LrgTerrainComponent
{
	LrgComponent      parent_instance;

	LrgTerrainAsset  *asset;
	gchar            *asset_path;
	gboolean          load_failed;
	gulong            changed_id;

	gboolean          cast_shadows;
	gboolean          physics;
	gboolean          foliage;
	gint              layer;

	Chunk            *chunks;
	guint             chunks_x;
	guint             chunks_z;

	LrgWorld3D       *world;
	LrgDynamicsWorld *physics_world;
	guint             body_id;
	gboolean          body_dirty;
	guint64           synced_version;

	gfloat            time;
};

G_DEFINE_FINAL_TYPE (LrgTerrainComponent, lrg_terrain_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_ASSET,
	PROP_ASSET_PATH,
	PROP_CAST_SHADOWS,
	PROP_PHYSICS,
	PROP_FOLIAGE,
	PROP_LAYER,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Chunk cache
 * ========================================================================== */

static void
chunk_clear (Chunk *c)
{
	guint l;

	for (l = 0; l < LRG_TERRAIN_ASSET_MAX_LOD; l++)
		g_clear_object (&c->meshes[l]);
	g_clear_object (&c->bake);
	c->bake_dirty = TRUE;
	c->bounds_dirty = TRUE;
	c->last_lod = -1;
}

static void
free_chunks (LrgTerrainComponent *self)
{
	guint i;

	for (i = 0; i < self->chunks_x * self->chunks_z; i++)
		chunk_clear (&self->chunks[i]);
	g_clear_pointer (&self->chunks, g_free);
	self->chunks_x = self->chunks_z = 0;
}

/* Matches the cache to the asset's chunk grid. */
static void
ensure_chunks (LrgTerrainComponent *self)
{
	guint cx, cz, i;

	lrg_terrain_asset_get_chunks (self->asset, &cx, &cz);
	if (self->chunks != NULL && cx == self->chunks_x && cz == self->chunks_z)
		return;
	free_chunks (self);
	self->chunks_x = cx;
	self->chunks_z = cz;
	self->chunks = g_new0 (Chunk, (gsize) cx * cz);
	for (i = 0; i < cx * cz; i++)
	{
		self->chunks[i].bake_dirty = TRUE;
		self->chunks[i].bounds_dirty = TRUE;
		self->chunks[i].last_lod = -1;
	}
}

/* LrgTerrainAsset::region-changed: drop what the change invalidates. */
static void
on_region_changed (LrgTerrainAsset     *asset,
                   LrgTerrainEditKind   kind,
                   guint                layer,
                   guint                x,
                   guint                z,
                   guint                width,
                   guint                height,
                   LrgTerrainComponent *self)
{
	guint res = lrg_terrain_asset_get_resolution (asset);
	guint cx0, cz0, cx1, cz1, cx, cz, l;

	(void) layer;
	if (kind == LRG_TERRAIN_EDIT_FOLIAGE)
		return;   /* the foliage tracks its own patches */
	ensure_chunks (self);

	if (kind == LRG_TERRAIN_EDIT_HEIGHTS || kind == LRG_TERRAIN_EDIT_SPLAT)
	{
		/* samples: a sample on a border belongs to both chunks, and a
		 * height change moves the normals of its neighbours too */
		guint lo_x = x > 0 ? x - 1 : 0, lo_z = z > 0 ? z - 1 : 0;
		guint hi_x = x + width, hi_z = z + height;

		cx0 = lo_x > 0 ? (lo_x - 1) / res : 0;
		cz0 = lo_z > 0 ? (lo_z - 1) / res : 0;
		cx1 = MIN (hi_x / res, self->chunks_x - 1);
		cz1 = MIN (hi_z / res, self->chunks_z - 1);
	}
	else
	{
		cx0 = MIN (x / res, self->chunks_x - 1);
		cz0 = MIN (z / res, self->chunks_z - 1);
		cx1 = MIN ((x + width - 1) / res, self->chunks_x - 1);
		cz1 = MIN ((z + height - 1) / res, self->chunks_z - 1);
	}
	for (cz = cz0; cz <= cz1; cz++)
		for (cx = cx0; cx <= cx1; cx++)
		{
			Chunk *c = &self->chunks[cz * self->chunks_x + cx];

			if (kind == LRG_TERRAIN_EDIT_HEIGHTS || kind == LRG_TERRAIN_EDIT_HOLES)
			{
				for (l = 0; l < LRG_TERRAIN_ASSET_MAX_LOD; l++)
					g_clear_object (&c->meshes[l]);
				c->bounds_dirty = TRUE;
			}
			if (kind == LRG_TERRAIN_EDIT_SPLAT || kind == LRG_TERRAIN_EDIT_MATERIAL_IDS)
				c->bake_dirty = TRUE;
		}
	if (kind == LRG_TERRAIN_EDIT_HEIGHTS || kind == LRG_TERRAIN_EDIT_HOLES)
		self->body_dirty = TRUE;
}

/* Loads the asset from the path once. */
static LrgTerrainAsset *
ensure_asset (LrgTerrainComponent *self)
{
	g_autoptr(GError)          error = NULL;
	g_autoptr(LrgTerrainAsset) loaded = NULL;

	if (self->asset != NULL || self->asset_path == NULL || self->load_failed)
		return self->asset;
	loaded = lrg_terrain_asset_new_from_file (self->asset_path, &error);
	if (loaded == NULL)
	{
		g_warning ("LrgTerrainComponent: cannot load '%s': %s", self->asset_path, error->message);
		self->load_failed = TRUE;
		return NULL;
	}
	lrg_terrain_component_set_asset (self, loaded);
	return self->asset;
}

/* The chunk's height range (for culling and the LOD distance). */
static void
chunk_bounds (LrgTerrainComponent *self,
              guint                cx,
              guint                cz,
              Chunk               *c)
{
	const gfloat *h;
	guint         nx, res, i, j;

	if (!c->bounds_dirty)
		return;
	h = lrg_terrain_asset_get_heights (self->asset, NULL);
	lrg_terrain_asset_get_samples (self->asset, &nx, NULL);
	res = lrg_terrain_asset_get_resolution (self->asset);
	c->min_y = G_MAXFLOAT;
	c->max_y = -G_MAXFLOAT;
	for (j = cz * res; j <= (cz + 1) * res; j++)
		for (i = cx * res; i <= (cx + 1) * res; i++)
		{
			gfloat v = h[(gsize) j * nx + i];

			c->min_y = MIN (c->min_y, v);
			c->max_y = MAX (c->max_y, v);
		}
	c->min_y -= lrg_terrain_asset_get_skirt_depth (self->asset);
	c->bounds_dirty = FALSE;
}

/* The chunk mesh at @lod, built (and uploaded with a window) on demand. */
static GrlMesh *
chunk_mesh (LrgTerrainComponent *self,
            guint                cx,
            guint                cz,
            Chunk               *c,
            guint                lod)
{
	TerrainChunkMesh cm;
	GrlMesh         *mesh;
	Mesh            *handle;

	if (c->meshes[lod] != NULL)
		return c->meshes[lod];
	if (!IsWindowReady ())
		return NULL;   /* grl_mesh_new_custom() uploads: GPU only */
	if (!_lrg_terrain_asset_build_chunk (self->asset, cx, cz, lod, &cm))
		return NULL;
	if (cm.n_indices == 0)
	{
		_lrg_terrain_chunk_mesh_clear (&cm);
		return NULL;   /* all holes */
	}
	mesh = grl_mesh_new_custom (cm.positions, cm.n_vertices, cm.normals, cm.indices, cm.n_indices);
	if (mesh == NULL)
	{
		_lrg_terrain_chunk_mesh_clear (&cm);
		return NULL;
	}

	/* texcoords for the baked splat texture: grl_mesh_new_custom() has
	 * none, so they are added to the uploaded vertex array here (raylib's
	 * allocator, so UnloadMesh() frees them and their buffer) */
	handle = (Mesh *) grl_mesh_get_handle (mesh);
	handle->texcoords = MemAlloc (sizeof (gfloat) * 2 * cm.n_vertices);
	memcpy (handle->texcoords, cm.texcoords, sizeof (gfloat) * 2 * cm.n_vertices);
	if (handle->vaoId != 0)
	{
		rlEnableVertexArray (handle->vaoId);
		handle->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD] =
			rlLoadVertexBuffer (handle->texcoords, (gint) (sizeof (gfloat) * 2 * cm.n_vertices), FALSE);
		rlSetVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD, 2, RL_FLOAT, FALSE, 0, 0);
		rlEnableVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD);
		rlDisableVertexArray ();
	}
	_lrg_terrain_chunk_mesh_clear (&cm);
	c->meshes[lod] = mesh;
	return mesh;
}

/* The chunk's baked splat texture (GPU only). */
static GrlTexture *
chunk_bake (LrgTerrainComponent *self,
            guint                cx,
            guint                cz,
            Chunk               *c)
{
	g_autofree guint8 *pixels = NULL;
	guint              B;
	Texture2D         *tex;

	if (!IsWindowReady ())
		return NULL;
	if (c->bake != NULL && !c->bake_dirty)
		return c->bake;
	B = lrg_terrain_asset_get_bake_resolution (self->asset);
	pixels = g_malloc ((gsize) B * B * 4);
	lrg_terrain_asset_bake_chunk (self->asset, cx, cz, pixels);
	if (c->bake != NULL && grl_texture_get_width (c->bake) == (gint) B)
	{
		tex = (Texture2D *) grl_texture_get_handle (c->bake);
		UpdateTexture (*tex, pixels);
	}
	else
	{
		g_autoptr(GrlImage) image = grl_image_new_from_pixels ((gint) B, (gint) B,
		                                                        GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, pixels);

		g_clear_object (&c->bake);
		if (image == NULL)
			return NULL;
		c->bake = grl_texture_new_from_image (image);
		if (c->bake == NULL)
			return NULL;
		tex = (Texture2D *) grl_texture_get_handle (c->bake);
	}
	GenTextureMipmaps (tex);
	SetTextureFilter (*tex, TEXTURE_FILTER_TRILINEAR);
	SetTextureWrap (*tex, TEXTURE_WRAP_CLAMP);
	c->bake_dirty = FALSE;
	return c->bake;
}

/* Squared distance from a point to a box. */
static gfloat
box_distance (const LrgBoundingBox3D *b,
              const gfloat           *p)
{
	gfloat lo[3] = { b->min.x, b->min.y, b->min.z };
	gfloat hi[3] = { b->max.x, b->max.y, b->max.z };
	gfloat d2 = 0.0f;
	guint  k;

	for (k = 0; k < 3; k++)
	{
		gfloat e = p[k] < lo[k] ? lo[k] - p[k] : p[k] > hi[k] ? p[k] - hi[k] : 0.0f;

		d2 += e * e;
	}
	return sqrtf (d2);
}

/* ==========================================================================
 * LrgComponent vfuncs
 * ========================================================================== */

static gboolean
lrg_terrain_component_get_bounds (LrgComponent     *component,
                                  LrgBoundingBox3D *out_local)
{
	LrgTerrainComponent *self = LRG_TERRAIN_COMPONENT (component);
	gfloat               w, d;
	guint                i, n;

	if (ensure_asset (self) == NULL)
		return FALSE;
	ensure_chunks (self);
	lrg_terrain_asset_get_size (self->asset, &w, &d);
	out_local->min.x = 0.0f;
	out_local->min.z = 0.0f;
	out_local->max.x = w;
	out_local->max.z = d;
	out_local->min.y = G_MAXFLOAT;
	out_local->max.y = -G_MAXFLOAT;
	n = self->chunks_x * self->chunks_z;
	for (i = 0; i < n; i++)
	{
		chunk_bounds (self, i % self->chunks_x, i / self->chunks_x, &self->chunks[i]);
		out_local->min.y = MIN (out_local->min.y, self->chunks[i].min_y);
		out_local->max.y = MAX (out_local->max.y, self->chunks[i].max_y);
	}
	return TRUE;
}

static void
lrg_terrain_component_collect_render (LrgComponent  *component,
                                      LrgRenderList *list)
{
	LrgTerrainComponent *self = LRG_TERRAIN_COMPONENT (component);
	LrgEntity3D         *entity = lrg_component_get_entity (component);
	gfloat               eye[3], cs;
	guint                cx, cz;

	if (entity == NULL || ensure_asset (self) == NULL)
		return;
	ensure_chunks (self);
	lrg_render_list_get_eye (list, eye);
	cs = lrg_terrain_asset_get_chunk_size (self->asset);

	for (cz = 0; cz < self->chunks_z; cz++)
		for (cx = 0; cx < self->chunks_x; cx++)
		{
			Chunk           *c = &self->chunks[cz * self->chunks_x + cx];
			LrgRenderItem    item;
			LrgBoundingBox3D local;
			guint            lod, flags;
			GrlMesh         *mesh;

			c->last_lod = -1;
			chunk_bounds (self, cx, cz, c);
			local.min.x = (gfloat) cx * cs;
			local.min.y = c->min_y;
			local.min.z = (gfloat) cz * cs;
			local.max.x = (gfloat) (cx + 1) * cs;
			local.max.y = c->max_y;
			local.max.z = (gfloat) (cz + 1) * cs;

			lrg_render_item_init_for_entity (&item, entity, component);
			item.kind = LRG_RENDER_ITEM_MESH;
			flags = LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW;
			if (self->cast_shadows)
				flags |= LRG_RENDER_ITEM_FLAG_CAST_SHADOW;
			item.flags = (LrgRenderItemFlags) flags;
			if (self->layer >= 0)
				item.layer = (guint) self->layer;
			lrg_render_item_set_local_bounds (&item, &local);
			if (!lrg_render_list_accepts (list, &item))
				continue;

			/* LOD by the distance from the eye to the chunk's box */
			lod = lrg_terrain_asset_select_lod (self->asset, box_distance (&item.bounds, eye));
			mesh = chunk_mesh (self, cx, cz, c, lod);
			if (mesh == NULL && IsWindowReady ())
				continue;   /* every cell a hole */
			item.mesh = mesh;
			item.texture = chunk_bake (self, cx, cz, c);
			item.sort_key = ((guint64) LRG_RENDER_ITEM_MESH << 56) | ((guint64) lod << 32) |
			                (guint64) (cz * self->chunks_x + cx);
			lrg_render_list_add (list, &item);
			c->last_lod = (gint) lod;
		}

	if (self->foliage)
		_lrg_foliage_collect_render (lrg_terrain_asset_get_foliage (self->asset), self->asset, list, entity,
		                             component, self->layer, self->time);
}

static void
lrg_terrain_component_update (LrgComponent *component,
                              gfloat        delta)
{
	LrgTerrainComponent *self = LRG_TERRAIN_COMPONENT (component);

	if (isfinite (delta) && delta > 0.0f)
		self->time += delta;
}

/* ==========================================================================
 * Physics client
 * ========================================================================== */

static void
drop_body (LrgTerrainComponent *self,
           LrgDynamicsWorld    *physics)
{
	if (self->body_id != LRG_PHYSICS_NO_BODY && physics != NULL && self->world != NULL)
		_lrg_physics_body_destroy (self->world, physics, self->body_id);
	self->body_id = LRG_PHYSICS_NO_BODY;
}

static void
build_body (LrgTerrainComponent *self,
            LrgEntity3D         *entity,
            LrgDynamicsWorld    *physics)
{
	g_autoptr(LrgPhysicsShape) shape = NULL;
	g_autoptr(GError)          error = NULL;
	LrgPhysicsBodyDesc         desc;
	gfloat                     scale[3];

	lrg_entity3d_get_world_scale (entity, scale);
	shape = lrg_terrain_asset_build_physics_shape (self->asset, scale[0] > 0.0f ? scale[0] : 1.0f,
	                                               scale[1] > 0.0f ? scale[1] : 1.0f,
	                                               scale[2] > 0.0f ? scale[2] : 1.0f, &error);
	if (shape == NULL)
	{
		g_warning ("LrgTerrainComponent: no heightfield: %s", error->message);
		return;
	}
	_lrg_physics_body_desc_init (&desc);
	desc.kind = LRG_DYNAMICS_BODY_STATIC;
	self->body_id = _lrg_physics_body_create (self->world, physics, entity, shape, &desc);
	self->synced_version = lrg_entity3d_get_world_version (entity);
}

static void
client_physics_changed (GObject          *client,
                        LrgDynamicsWorld *old_physics,
                        LrgDynamicsWorld *new_physics)
{
	LrgTerrainComponent *self = LRG_TERRAIN_COMPONENT (client);

	drop_body (self, old_physics);
	self->physics_world = new_physics;
}

static void
client_pre_step (GObject          *client,
                 LrgDynamicsWorld *physics,
                 gfloat            dt)
{
	LrgTerrainComponent *self = LRG_TERRAIN_COMPONENT (client);
	LrgEntity3D         *entity = lrg_component_get_entity (LRG_COMPONENT (self));

	(void) dt;
	if (entity == NULL)
		return;
	if (!self->physics || !lrg_component_get_enabled (LRG_COMPONENT (self)) ||
	    !lrg_entity3d_is_active_in_hierarchy (entity) || ensure_asset (self) == NULL)
	{
		drop_body (self, physics);
		return;
	}
	if (self->body_id != LRG_PHYSICS_NO_BODY && self->body_dirty)
		drop_body (self, physics);
	if (self->body_id == LRG_PHYSICS_NO_BODY)
	{
		self->body_dirty = FALSE;
		build_body (self, entity, physics);
		return;
	}
	if (lrg_entity3d_get_world_version (entity) != self->synced_version)
	{
		_lrg_physics_body_teleport (physics, self->body_id, entity);
		self->synced_version = lrg_entity3d_get_world_version (entity);
	}
}

static const LrgPhysicsClientClass terrain_client = {
	client_physics_changed,
	client_pre_step,
	NULL,
	NULL
};

static void
lrg_terrain_component_world_entered (LrgComponent *component,
                                     LrgWorld3D   *world)
{
	LrgTerrainComponent *self = LRG_TERRAIN_COMPONENT (component);

	self->world = world;
	self->body_dirty = TRUE;
	_lrg_world3d_physics_add_client (world, G_OBJECT (self), LRG_PHYSICS_CLIENT_COLLIDER, &terrain_client);
}

static void
lrg_terrain_component_world_exited (LrgComponent *component,
                                    LrgWorld3D   *world)
{
	LrgTerrainComponent *self = LRG_TERRAIN_COMPONENT (component);

	drop_body (self, lrg_world3d_get_physics (world));
	_lrg_world3d_physics_remove_client (world, G_OBJECT (self));
	self->world = NULL;
	self->physics_world = NULL;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_terrain_component_dispose (GObject *object)
{
	LrgTerrainComponent *self = LRG_TERRAIN_COMPONENT (object);

	if (self->asset != NULL && self->changed_id != 0)
		g_signal_handler_disconnect (self->asset, self->changed_id);
	self->changed_id = 0;
	free_chunks (self);
	g_clear_object (&self->asset);
	G_OBJECT_CLASS (lrg_terrain_component_parent_class)->dispose (object);
}

static void
lrg_terrain_component_finalize (GObject *object)
{
	LrgTerrainComponent *self = LRG_TERRAIN_COMPONENT (object);

	g_free (self->asset_path);
	G_OBJECT_CLASS (lrg_terrain_component_parent_class)->finalize (object);
}

static void
lrg_terrain_component_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	LrgTerrainComponent *self = LRG_TERRAIN_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_ASSET:
		g_value_set_object (value, self->asset);
		break;
	case PROP_ASSET_PATH:
		g_value_set_string (value, self->asset_path);
		break;
	case PROP_CAST_SHADOWS:
		g_value_set_boolean (value, self->cast_shadows);
		break;
	case PROP_PHYSICS:
		g_value_set_boolean (value, self->physics);
		break;
	case PROP_FOLIAGE:
		g_value_set_boolean (value, self->foliage);
		break;
	case PROP_LAYER:
		g_value_set_int (value, self->layer);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_terrain_component_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	LrgTerrainComponent *self = LRG_TERRAIN_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_ASSET:
		lrg_terrain_component_set_asset (self, g_value_get_object (value));
		break;
	case PROP_ASSET_PATH:
		lrg_terrain_component_set_asset_path (self, g_value_get_string (value));
		break;
	case PROP_CAST_SHADOWS:
		lrg_terrain_component_set_cast_shadows (self, g_value_get_boolean (value));
		break;
	case PROP_PHYSICS:
		lrg_terrain_component_set_physics (self, g_value_get_boolean (value));
		break;
	case PROP_FOLIAGE:
		lrg_terrain_component_set_foliage (self, g_value_get_boolean (value));
		break;
	case PROP_LAYER:
		if (self->layer != g_value_get_int (value))
		{
			self->layer = g_value_get_int (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_terrain_component_class_init (LrgTerrainComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_terrain_component_dispose;
	object_class->finalize = lrg_terrain_component_finalize;
	object_class->get_property = lrg_terrain_component_get_property;
	object_class->set_property = lrg_terrain_component_set_property;

	component_class->collect_render = lrg_terrain_component_collect_render;
	component_class->get_bounds = lrg_terrain_component_get_bounds;
	component_class->update = lrg_terrain_component_update;
	component_class->world_entered = lrg_terrain_component_world_entered;
	component_class->world_exited = lrg_terrain_component_world_exited;

	/**
	 * LrgTerrainComponent:asset:
	 *
	 * The terrain drawn (explicit, or loaded from the asset path).
	 */
	properties[PROP_ASSET] = g_param_spec_object ("asset", NULL, "Terrain asset", LRG_TYPE_TERRAIN_ASSET, rw);

	/**
	 * LrgTerrainComponent:asset-path:
	 *
	 * The `.rterrain` file loaded on first use when no asset is set (the
	 * level node's asset).
	 */
	properties[PROP_ASSET_PATH] = g_param_spec_string ("asset-path", NULL, "Terrain file", NULL, rw);

	/**
	 * LrgTerrainComponent:cast-shadows:
	 *
	 * Whether the chunks cast shadows.
	 */
	properties[PROP_CAST_SHADOWS] = g_param_spec_boolean ("cast-shadows", NULL, "Cast shadows", TRUE, rw);

	/**
	 * LrgTerrainComponent:physics:
	 *
	 * Whether a static heightfield body is built in the world's physics.
	 */
	properties[PROP_PHYSICS] = g_param_spec_boolean ("physics", NULL, "Heightfield body", TRUE, rw);

	/**
	 * LrgTerrainComponent:foliage:
	 *
	 * Whether the asset's foliage is drawn.
	 */
	properties[PROP_FOLIAGE] = g_param_spec_boolean ("foliage", NULL, "Draw foliage", TRUE, rw);

	/**
	 * LrgTerrainComponent:layer:
	 *
	 * Render layer override 0 .. 31, or -1 for the entity's layer.
	 */
	properties[PROP_LAYER] = g_param_spec_int ("layer", NULL, "Render layer (-1 entity)", -1, 31, -1, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_terrain_component_init (LrgTerrainComponent *self)
{
	self->cast_shadows = TRUE;
	self->physics = TRUE;
	self->foliage = TRUE;
	self->layer = -1;
	self->body_id = LRG_PHYSICS_NO_BODY;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgTerrainComponent *
lrg_terrain_component_new (void)
{
	return (LrgTerrainComponent *) g_object_new (LRG_TYPE_TERRAIN_COMPONENT, NULL);
}

LrgTerrainComponent *
lrg_terrain_component_new_with_asset (LrgTerrainAsset *asset)
{
	return (LrgTerrainComponent *) g_object_new (LRG_TYPE_TERRAIN_COMPONENT, "asset", asset, NULL);
}

LrgTerrainAsset *
lrg_terrain_component_get_asset (LrgTerrainComponent *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_COMPONENT (self), NULL);
	return ensure_asset (self);
}

void
lrg_terrain_component_set_asset (LrgTerrainComponent *self,
                                 LrgTerrainAsset     *asset)
{
	g_return_if_fail (LRG_IS_TERRAIN_COMPONENT (self));
	g_return_if_fail (asset == NULL || LRG_IS_TERRAIN_ASSET (asset));
	if (self->asset == asset)
		return;
	if (self->asset != NULL && self->changed_id != 0)
		g_signal_handler_disconnect (self->asset, self->changed_id);
	self->changed_id = 0;
	free_chunks (self);
	g_set_object (&self->asset, asset);
	if (asset != NULL)
		self->changed_id = g_signal_connect (asset, "region-changed", G_CALLBACK (on_region_changed), self);
	self->body_dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET]);
}

const gchar *
lrg_terrain_component_get_asset_path (LrgTerrainComponent *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_COMPONENT (self), NULL);
	return self->asset_path;
}

void
lrg_terrain_component_set_asset_path (LrgTerrainComponent *self,
                                      const gchar         *path)
{
	g_return_if_fail (LRG_IS_TERRAIN_COMPONENT (self));
	if (g_strcmp0 (self->asset_path, path) == 0)
		return;
	g_free (self->asset_path);
	self->asset_path = g_strdup (path);
	self->load_failed = FALSE;
	lrg_terrain_component_set_asset (self, NULL);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET_PATH]);
}

gboolean
lrg_terrain_component_get_cast_shadows (LrgTerrainComponent *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_COMPONENT (self), FALSE);
	return self->cast_shadows;
}

void
lrg_terrain_component_set_cast_shadows (LrgTerrainComponent *self,
                                        gboolean             cast)
{
	g_return_if_fail (LRG_IS_TERRAIN_COMPONENT (self));
	cast = !!cast;
	if (self->cast_shadows == cast)
		return;
	self->cast_shadows = cast;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAST_SHADOWS]);
}

gboolean
lrg_terrain_component_get_physics (LrgTerrainComponent *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_COMPONENT (self), FALSE);
	return self->physics;
}

void
lrg_terrain_component_set_physics (LrgTerrainComponent *self,
                                   gboolean             physics)
{
	g_return_if_fail (LRG_IS_TERRAIN_COMPONENT (self));
	physics = !!physics;
	if (self->physics == physics)
		return;
	self->physics = physics;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PHYSICS]);
}

gboolean
lrg_terrain_component_get_foliage (LrgTerrainComponent *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_COMPONENT (self), FALSE);
	return self->foliage;
}

void
lrg_terrain_component_set_foliage (LrgTerrainComponent *self,
                                   gboolean             foliage)
{
	g_return_if_fail (LRG_IS_TERRAIN_COMPONENT (self));
	foliage = !!foliage;
	if (self->foliage == foliage)
		return;
	self->foliage = foliage;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOLIAGE]);
}

guint
lrg_terrain_component_get_body_id (LrgTerrainComponent *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_COMPONENT (self), LRG_PHYSICS_NO_BODY);
	return self->body_id;
}

gint
lrg_terrain_component_get_chunk_lod (LrgTerrainComponent *self,
                                     guint                cx,
                                     guint                cz)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_COMPONENT (self), -1);
	if (self->chunks == NULL || cx >= self->chunks_x || cz >= self->chunks_z)
		return -1;
	return self->chunks[cz * self->chunks_x + cx].last_lod;
}

/* Inverts an affine column-major 4x4 (3x3 by cofactors). */
static gboolean
invert_affine (const gfloat *m,
               gfloat       *out)
{
	gfloat a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
	gfloat A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
	gfloat det = a * A + b * B + c * C, inv;
	guint  k;

	if (fabsf (det) < 1e-20f || !isfinite (det))
		return FALSE;
	inv = 1.0f / det;
	out[0] = A * inv;
	out[1] = B * inv;
	out[2] = C * inv;
	out[4] = -(b * i - c * h) * inv;
	out[5] = (a * i - c * g) * inv;
	out[6] = -(a * h - b * g) * inv;
	out[8] = (b * f - c * e) * inv;
	out[9] = -(a * f - c * d) * inv;
	out[10] = (a * e - b * d) * inv;
	out[3] = out[7] = out[11] = 0.0f;
	out[15] = 1.0f;
	for (k = 0; k < 3; k++)
		out[12 + k] = -(out[k] * m[12] + out[4 + k] * m[13] + out[8 + k] * m[14]);
	return TRUE;
}

static void
xform (const gfloat *m,
       const gfloat *p,
       gfloat        w,
       gfloat       *out)
{
	guint k;

	for (k = 0; k < 3; k++)
		out[k] = m[k] * p[0] + m[4 + k] * p[1] + m[8 + k] * p[2] + m[12 + k] * w;
}

gboolean
lrg_terrain_component_height_at (LrgTerrainComponent *self,
                                 gfloat               x,
                                 gfloat               z,
                                 gfloat              *out_height)
{
	gfloat origin[3], dir[3] = { 0.0f, -1.0f, 0.0f }, hit[3];

	g_return_val_if_fail (LRG_IS_TERRAIN_COMPONENT (self), FALSE);
	if (ensure_asset (self) == NULL)
		return FALSE;

	/* a vertical ray from far above, through the entity transform */
	origin[0] = x;
	origin[1] = 1.0e6f;
	origin[2] = z;
	if (!lrg_terrain_component_raycast (self, origin, dir, 2.0e6f, hit, NULL))
		return FALSE;
	if (out_height != NULL)
		*out_height = hit[1];
	return TRUE;
}

gboolean
lrg_terrain_component_raycast (LrgTerrainComponent *self,
                               const gfloat        *origin,
                               const gfloat        *direction,
                               gfloat               max_distance,
                               gfloat              *out_world,
                               gfloat              *out_local)
{
	LrgEntity3D  *entity;
	const gfloat *world;
	gfloat        inv[16], lo[3], ld[3], hit[3], len;
	gfloat        identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

	g_return_val_if_fail (LRG_IS_TERRAIN_COMPONENT (self), FALSE);
	g_return_val_if_fail (origin != NULL && direction != NULL, FALSE);
	if (ensure_asset (self) == NULL)
		return FALSE;
	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	world = entity != NULL ? lrg_entity3d_peek_world_matrix (entity) : identity;
	if (!invert_affine (world, inv))
		return FALSE;
	len = sqrtf (direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
	if (!(len > 0.0f))
		return FALSE;

	/* local ray: the parameter keeps world units because the march
	 * scales by the direction's length */
	xform (inv, origin, 1.0f, lo);
	xform (inv, direction, 0.0f, ld);
	ld[0] /= len;
	ld[1] /= len;
	ld[2] /= len;
	if (!lrg_terrain_asset_raycast (self->asset, lo, ld, max_distance, hit))
		return FALSE;
	if (out_local != NULL)
		memcpy (out_local, hit, sizeof hit);
	if (out_world != NULL)
		xform (world, hit, 1.0f, out_world);
	return TRUE;
}

void
lrg_terrain_component_release_gpu (LrgTerrainComponent *self)
{
	guint i, l;

	g_return_if_fail (LRG_IS_TERRAIN_COMPONENT (self));
	for (i = 0; i < self->chunks_x * self->chunks_z; i++)
	{
		for (l = 0; l < LRG_TERRAIN_ASSET_MAX_LOD; l++)
			g_clear_object (&self->chunks[i].meshes[l]);
		g_clear_object (&self->chunks[i].bake);
		self->chunks[i].bake_dirty = TRUE;
	}
	if (self->asset != NULL)
		lrg_foliage_release_gpu (lrg_terrain_asset_get_foliage (self->asset));
}
