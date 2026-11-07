/* lrg-particle-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgParticleComponent: an LrgParticleEffect on an entity.
 *
 * update() loads the asset from its path on first use, copies the
 * entity's world position and rotation to the effect and steps it.
 * collect_render() submits one %LRG_RENDER_ITEM_CUSTOM transparent item
 * per emitter that has something to draw, with the emitter's world
 * bounds (so the list sorts emitters back to front and culls them) and
 * the emitter's render layer (or the component's override).  The draw
 * callback reads the eye and up vector from rlgl's current view matrix,
 * which is the camera's inside the renderer's 3D pass, so no hook in
 * LrgWorld3DRenderer is needed.
 *
 * With world collision on, the effect's raycast function casts against
 * the entity's world's LrgDynamicsWorld.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "lrg-particle-component.h"
#include "../lrg-world3d.h"
#include "../lrg-entity3d.h"
#include "../../core/lrg-engine.h"
#include "../../physics/lrg-dynamics.h"

struct _LrgParticleComponent
{
	LrgComponent       parent_instance;

	LrgParticleAsset  *asset;
	gchar             *asset_path;
	gboolean           load_failed;   /* the path did not load; logged once */
	LrgAssetManager   *manager;
	LrgParticleEffect *effect;
	gboolean           auto_play;
	gfloat             time_scale;
	guint64            seed;          /* 0: the asset's */
	gint               layer;         /* -1: per emitter */
	gboolean           world_collision;
	LrgWorld3D        *world;         /* weak: set between world_entered/exited */
	gulong             finished_id;
};

G_DEFINE_FINAL_TYPE (LrgParticleComponent, lrg_particle_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_ASSET,
	PROP_ASSET_PATH,
	PROP_ASSET_MANAGER,
	PROP_EFFECT,
	PROP_AUTO_PLAY,
	PROP_TIME_SCALE,
	PROP_SEED,
	PROP_LAYER,
	PROP_WORLD_COLLISION,
	PROP_PLAYING,
	N_PROPS
};

enum
{
	SIGNAL_FINISHED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Effect plumbing
 * ========================================================================== */

static LrgAssetManager *
manager_of (LrgParticleComponent *self)
{
	LrgEngine *engine;

	if (self->manager != NULL)
		return self->manager;
	engine = lrg_engine_get_default ();
	return engine != NULL ? lrg_engine_get_asset_manager (engine) : NULL;
}

/* World raycast through the physics world of the entity's world */
static gboolean
physics_raycast (const gfloat *from,
                 const gfloat *to,
                 guint32       layer_mask,
                 gfloat       *out_point,
                 gfloat       *out_normal,
                 gpointer      user_data)
{
	LrgParticleComponent *self = user_data;
	LrgDynamicsWorld     *physics;
	LrgRayHit             hit;
	gfloat                dir[3];

	if (self->world == NULL || !self->world_collision)
		return FALSE;
	physics = lrg_world3d_get_physics (self->world);
	if (physics == NULL)
		return FALSE;
	dir[0] = to[0] - from[0];
	dir[1] = to[1] - from[1];
	dir[2] = to[2] - from[2];
	if (!(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2] > 1e-12f))
		return FALSE;
	if (!lrg_dynamics_world_raycast (physics, from, dir, 1.0f, layer_mask, G_MAXUINT, &hit))
		return FALSE;
	memcpy (out_point, hit.point, sizeof hit.point);
	memcpy (out_normal, hit.normal, sizeof hit.normal);
	return TRUE;
}

static void
on_effect_finished (LrgParticleEffect *effect,
                    gpointer           user_data)
{
	LrgParticleComponent *self = LRG_PARTICLE_COMPONENT (user_data);

	(void) effect;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
	g_signal_emit (self, signals[SIGNAL_FINISHED], 0);
}

static void
drop_effect (LrgParticleComponent *self)
{
	if (self->effect == NULL)
		return;
	if (self->finished_id != 0)
		g_signal_handler_disconnect (self->effect, self->finished_id);
	self->finished_id = 0;
	g_clear_object (&self->effect);
}

/* Makes the effect for the current asset (loading the path if needed) */
static void
ensure_effect (LrgParticleComponent *self)
{
	if (self->effect != NULL)
		return;

	if (self->asset == NULL && self->asset_path != NULL && !self->load_failed)
	{
		LrgAssetManager  *manager = manager_of (self);
		g_autofree gchar *path = NULL;
		g_autoptr(GError) error = NULL;

		if (manager != NULL)
			path = lrg_asset_manager_resolve_reference (manager, self->asset_path);
		if (path == NULL)
			path = g_strdup (self->asset_path);
		self->asset = lrg_particle_asset_new_from_file (path, &error);
		if (self->asset == NULL)
		{
			self->load_failed = TRUE;
			g_warning ("LrgParticleComponent: cannot load '%s': %s", self->asset_path, error->message);
			return;
		}
	}
	if (self->asset == NULL)
		return;

	self->effect = lrg_particle_effect_new (self->asset);
	lrg_particle_effect_set_asset_manager (self->effect, manager_of (self));
	lrg_particle_effect_set_raycast_func (self->effect, physics_raycast, self, NULL);
	if (self->seed != 0)
		lrg_particle_effect_set_seed (self->effect, self->seed);
	self->finished_id = g_signal_connect (self->effect, "finished", G_CALLBACK (on_effect_finished), self);
	if (self->auto_play)
		lrg_particle_effect_play (self->effect);
}

/* Copies the entity's world transform onto the effect */
static void
follow_entity (LrgParticleComponent *self)
{
	LrgEntity3D *entity = lrg_component_get_entity (LRG_COMPONENT (self));
	gfloat       pos[3], rot[4];

	if (entity == NULL || self->effect == NULL)
		return;
	lrg_entity3d_get_world_position (entity, pos);
	lrg_entity3d_get_world_rotation (entity, rot);
	lrg_particle_effect_set_position (self->effect, pos[0], pos[1], pos[2]);
	lrg_particle_effect_set_rotation (self->effect, rot);
}

/* ==========================================================================
 * LrgComponent
 * ========================================================================== */

static void
lrg_particle_component_update (LrgComponent *component,
                               gfloat        delta)
{
	LrgParticleComponent *self = LRG_PARTICLE_COMPONENT (component);
	gboolean              was_playing;

	ensure_effect (self);
	if (self->effect == NULL)
		return;
	follow_entity (self);
	was_playing = lrg_particle_effect_is_playing (self->effect);
	lrg_particle_effect_update (self->effect, delta * self->time_scale);
	if (was_playing != lrg_particle_effect_is_playing (self->effect))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
}

static void
lrg_particle_component_world_entered (LrgComponent *component,
                                      LrgWorld3D   *world)
{
	LrgParticleComponent *self = LRG_PARTICLE_COMPONENT (component);

	self->world = world;
	ensure_effect (self);
	follow_entity (self);
}

static void
lrg_particle_component_world_exited (LrgComponent *component,
                                     LrgWorld3D   *world)
{
	LrgParticleComponent *self = LRG_PARTICLE_COMPONENT (component);

	(void) world;
	self->world = NULL;
}

typedef struct
{
	LrgParticleComponent *self;   /* ref */
	guint                 index;
} EmitterDraw;

static void
emitter_draw_free (gpointer data)
{
	EmitterDraw *ed = data;

	g_object_unref (ed->self);
	g_free (ed);
}

/*
 * The draw callback: the eye and up vector come from rlgl's view matrix
 * (the renderer's camera inside its 3D pass).  Raylib matrices store
 * rows as (m0, m4, m8, m12), ...: the view's rotation rows are the
 * camera's right, up and back axes, and eye = -R^T t.
 */
static void
draw_emitter (const LrgRenderItem *item,
              LrgRenderCategory    category,
              gpointer             pass_data)
{
	EmitterDraw *ed = item->user_data;
	Matrix       v;
	gfloat       eye[3], up[3];

	(void) pass_data;
	if (category == LRG_RENDER_CATEGORY_SHADOW_CASTER || ed->self->effect == NULL)
		return;
	v = rlGetMatrixModelview ();
	eye[0] = -(v.m0 * v.m12 + v.m1 * v.m13 + v.m2 * v.m14);
	eye[1] = -(v.m4 * v.m12 + v.m5 * v.m13 + v.m6 * v.m14);
	eye[2] = -(v.m8 * v.m12 + v.m9 * v.m13 + v.m10 * v.m14);
	up[0] = v.m1;
	up[1] = v.m5;
	up[2] = v.m9;
	lrg_particle_effect_draw_emitter (ed->self->effect, ed->index, eye, up);
}

static void
lrg_particle_component_collect_render (LrgComponent  *component,
                                       LrgRenderList *list)
{
	LrgParticleComponent *self = LRG_PARTICLE_COMPONENT (component);
	LrgEntity3D          *entity = lrg_component_get_entity (component);
	guint                 i, n;

	if (entity == NULL)
		return;
	ensure_effect (self);
	if (self->effect == NULL)
		return;

	n = lrg_particle_effect_get_n_systems (self->effect);
	for (i = 0; i < n; i++)
	{
		LrgParticleSystem *sys = lrg_particle_effect_get_system (self->effect, i);
		LrgRenderItem      item;
		EmitterDraw       *ed;
		gfloat             lo[3], hi[3];

		if (!lrg_particle_effect_get_emitter_bounds (self->effect, i, lo, hi))
			continue;
		lrg_render_item_init_for_entity (&item, entity, component);
		item.kind = LRG_RENDER_ITEM_CUSTOM;
		item.flags = (LrgRenderItemFlags) (LRG_RENDER_ITEM_FLAG_TRANSPARENT | LRG_RENDER_ITEM_FLAG_UNLIT |
		                                   LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED);
		item.layer = self->layer >= 0 ? (guint) self->layer : lrg_particle_system_get_layer (sys);
		item.bounds.min.x = lo[0];
		item.bounds.min.y = lo[1];
		item.bounds.min.z = lo[2];
		item.bounds.max.x = hi[0];
		item.bounds.max.y = hi[1];
		item.bounds.max.z = hi[2];
		/* the particles are in world space already */
		memset (item.world_matrix, 0, sizeof item.world_matrix);
		item.world_matrix[0] = item.world_matrix[5] = item.world_matrix[10] = item.world_matrix[15] = 1.0f;
		item.world_matrix[12] = (lo[0] + hi[0]) * 0.5f;
		item.world_matrix[13] = (lo[1] + hi[1]) * 0.5f;
		item.world_matrix[14] = (lo[2] + hi[2]) * 0.5f;
		item.sort_key = ((guint64) LRG_RENDER_ITEM_CUSTOM << 56) | (guint64) i;
		if (!lrg_render_list_accepts (list, &item))
			continue;

		ed = g_new0 (EmitterDraw, 1);
		ed->self = g_object_ref (self);
		ed->index = i;
		item.draw_func = draw_emitter;
		item.user_data = ed;
		item.user_data_destroy = emitter_draw_free;
		lrg_render_list_add (list, &item);
	}
}

static gboolean
lrg_particle_component_get_bounds (LrgComponent     *component,
                                   LrgBoundingBox3D *out_local)
{
	LrgParticleComponent *self = LRG_PARTICLE_COMPONENT (component);
	LrgEntity3D          *entity = lrg_component_get_entity (component);
	gfloat                lo[3], hi[3], pos[3] = { 0.0f, 0.0f, 0.0f };

	/* local bounds: the world box around the particles, about the entity
	 * (rotation ignored: a box that holds them in any orientation) */
	if (self->effect == NULL || !lrg_particle_effect_get_bounds (self->effect, lo, hi))
		return FALSE;
	if (entity != NULL)
		lrg_entity3d_get_world_position (entity, pos);
	{
		gfloat r = 0.0f;
		gint   k;

		for (k = 0; k < 3; k++)
			r = MAX (r, MAX (fabsf (lo[k] - pos[k]), fabsf (hi[k] - pos[k])));
		out_local->min.x = out_local->min.y = out_local->min.z = -r;
		out_local->max.x = out_local->max.y = out_local->max.z = r;
	}
	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_particle_component_dispose (GObject *object)
{
	LrgParticleComponent *self = LRG_PARTICLE_COMPONENT (object);

	drop_effect (self);
	g_clear_object (&self->asset);
	g_clear_object (&self->manager);
	G_OBJECT_CLASS (lrg_particle_component_parent_class)->dispose (object);
}

static void
lrg_particle_component_finalize (GObject *object)
{
	LrgParticleComponent *self = LRG_PARTICLE_COMPONENT (object);

	g_clear_pointer (&self->asset_path, g_free);
	G_OBJECT_CLASS (lrg_particle_component_parent_class)->finalize (object);
}

static void
lrg_particle_component_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
	LrgParticleComponent *self = LRG_PARTICLE_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_ASSET:           g_value_set_object (value, self->asset); break;
	case PROP_ASSET_PATH:      g_value_set_string (value, self->asset_path); break;
	case PROP_ASSET_MANAGER:   g_value_set_object (value, self->manager); break;
	case PROP_EFFECT:          g_value_set_object (value, self->effect); break;
	case PROP_AUTO_PLAY:       g_value_set_boolean (value, self->auto_play); break;
	case PROP_TIME_SCALE:      g_value_set_float (value, self->time_scale); break;
	case PROP_SEED:            g_value_set_uint64 (value, self->seed); break;
	case PROP_LAYER:           g_value_set_int (value, self->layer); break;
	case PROP_WORLD_COLLISION: g_value_set_boolean (value, self->world_collision); break;
	case PROP_PLAYING:
		g_value_set_boolean (value, self->effect != NULL && lrg_particle_effect_is_playing (self->effect));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_particle_component_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
	LrgParticleComponent *self = LRG_PARTICLE_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_ASSET:
		lrg_particle_component_set_asset (self, g_value_get_object (value));
		break;
	case PROP_ASSET_PATH:
		lrg_particle_component_set_asset_path (self, g_value_get_string (value));
		break;
	case PROP_ASSET_MANAGER:
		lrg_particle_component_set_asset_manager (self, g_value_get_object (value));
		break;
	case PROP_AUTO_PLAY:
		lrg_particle_component_set_auto_play (self, g_value_get_boolean (value));
		break;
	case PROP_TIME_SCALE:
		lrg_particle_component_set_time_scale (self, g_value_get_float (value));
		break;
	case PROP_SEED:
		if (self->seed != g_value_get_uint64 (value))
		{
			self->seed = g_value_get_uint64 (value);
			if (self->effect != NULL && self->seed != 0)
				lrg_particle_effect_set_seed (self->effect, self->seed);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_LAYER:
		if (self->layer != g_value_get_int (value))
		{
			self->layer = g_value_get_int (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_WORLD_COLLISION:
		if (self->world_collision != g_value_get_boolean (value))
		{
			self->world_collision = g_value_get_boolean (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_particle_component_class_init (LrgParticleComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	GParamFlags        rw = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS;

	object_class->dispose = lrg_particle_component_dispose;
	object_class->finalize = lrg_particle_component_finalize;
	object_class->get_property = lrg_particle_component_get_property;
	object_class->set_property = lrg_particle_component_set_property;
	component_class->update = lrg_particle_component_update;
	component_class->collect_render = lrg_particle_component_collect_render;
	component_class->get_bounds = lrg_particle_component_get_bounds;
	component_class->world_entered = lrg_particle_component_world_entered;
	component_class->world_exited = lrg_particle_component_world_exited;

	/**
	 * LrgParticleComponent:asset:
	 *
	 * The effect asset played.
	 */
	properties[PROP_ASSET] =
		g_param_spec_object ("asset", "Asset", "Effect asset", LRG_TYPE_PARTICLE_ASSET, rw);

	/**
	 * LrgParticleComponent:asset-path:
	 *
	 * The `.rparticles` reference loaded when no asset is set.
	 */
	properties[PROP_ASSET_PATH] =
		g_param_spec_string ("asset-path", "Asset Path", "[hint:asset:particles] Effect file", NULL, rw);

	/**
	 * LrgParticleComponent:asset-manager:
	 *
	 * Resolves the path, textures and meshes (the engine's when unset).
	 */
	properties[PROP_ASSET_MANAGER] =
		g_param_spec_object ("asset-manager", "Asset Manager", "Resolves references", LRG_TYPE_ASSET_MANAGER, rw);

	/**
	 * LrgParticleComponent:effect:
	 *
	 * The playing effect (read-only).
	 */
	properties[PROP_EFFECT] =
		g_param_spec_object ("effect", "Effect", "The playing effect", LRG_TYPE_PARTICLE_EFFECT,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgParticleComponent:auto-play:
	 *
	 * Whether the effect plays once loaded.
	 */
	properties[PROP_AUTO_PLAY] =
		g_param_spec_boolean ("auto-play", "Auto Play", "Play once loaded", TRUE, rw);

	/**
	 * LrgParticleComponent:time-scale:
	 *
	 * Simulation speed factor.
	 */
	properties[PROP_TIME_SCALE] =
		g_param_spec_float ("time-scale", "Time Scale", "Simulation speed", 0.0f, 100.0f, 1.0f, rw);

	/**
	 * LrgParticleComponent:seed:
	 *
	 * A seed overriding the asset's (0 keeps the asset's), so copies of
	 * one effect in a level differ.
	 */
	properties[PROP_SEED] =
		g_param_spec_uint64 ("seed", "Seed", "Seed override (0: the asset's)", 0, G_MAXUINT64, 0, rw);

	/**
	 * LrgParticleComponent:layer:
	 *
	 * Render layer of every emitter, or -1 for each emitter's own.
	 */
	properties[PROP_LAYER] =
		g_param_spec_int ("layer", "Layer", "Render layer override (-1: per emitter)", -1, 31, -1, rw);

	/**
	 * LrgParticleComponent:world-collision:
	 *
	 * Whether collision modules in world mode cast against the world's
	 * physics.
	 */
	properties[PROP_WORLD_COLLISION] =
		g_param_spec_boolean ("world-collision", "World Collision", "Collide with the physics world", TRUE, rw);

	/**
	 * LrgParticleComponent:playing:
	 *
	 * Whether the effect is playing (read-only).
	 */
	properties[PROP_PLAYING] =
		g_param_spec_boolean ("playing", "Playing", "Whether the effect plays", FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgParticleComponent::finished:
	 * @self: the component
	 *
	 * Forwarded from #LrgParticleEffect::finished (a one-shot effect is
	 * over: a spawner may destroy the entity).
	 */
	signals[SIGNAL_FINISHED] =
		g_signal_new ("finished", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_particle_component_init (LrgParticleComponent *self)
{
	self->auto_play = TRUE;
	self->time_scale = 1.0f;
	self->layer = -1;
	self->world_collision = TRUE;
}

LrgParticleComponent *
lrg_particle_component_new (void)
{
	return g_object_new (LRG_TYPE_PARTICLE_COMPONENT, NULL);
}

LrgParticleComponent *
lrg_particle_component_new_with_asset (LrgParticleAsset *asset)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (asset), NULL);
	return g_object_new (LRG_TYPE_PARTICLE_COMPONENT, "asset", asset, NULL);
}

LrgParticleAsset *
lrg_particle_component_get_asset (LrgParticleComponent *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_COMPONENT (self), NULL);
	ensure_effect (self);
	return self->asset;
}

void
lrg_particle_component_set_asset (LrgParticleComponent *self,
                                  LrgParticleAsset     *asset)
{
	g_return_if_fail (LRG_IS_PARTICLE_COMPONENT (self));

	if (!g_set_object (&self->asset, asset))
		return;
	drop_effect (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EFFECT]);
}

const gchar *
lrg_particle_component_get_asset_path (LrgParticleComponent *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_COMPONENT (self), NULL);
	return self->asset_path;
}

void
lrg_particle_component_set_asset_path (LrgParticleComponent *self,
                                       const gchar          *path)
{
	g_return_if_fail (LRG_IS_PARTICLE_COMPONENT (self));

	if (!g_set_str (&self->asset_path, path))
		return;
	/* a new path replaces an asset loaded from the old one */
	drop_effect (self);
	g_clear_object (&self->asset);
	self->load_failed = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET_PATH]);
}

void
lrg_particle_component_set_asset_manager (LrgParticleComponent *self,
                                          LrgAssetManager      *manager)
{
	g_return_if_fail (LRG_IS_PARTICLE_COMPONENT (self));

	if (!g_set_object (&self->manager, manager))
		return;
	self->load_failed = FALSE;
	if (self->effect != NULL)
		lrg_particle_effect_set_asset_manager (self->effect, manager_of (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET_MANAGER]);
}

LrgParticleEffect *
lrg_particle_component_get_effect (LrgParticleComponent *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_COMPONENT (self), NULL);
	ensure_effect (self);
	return self->effect;
}

void
lrg_particle_component_play (LrgParticleComponent *self)
{
	g_return_if_fail (LRG_IS_PARTICLE_COMPONENT (self));
	ensure_effect (self);
	if (self->effect == NULL)
		return;
	follow_entity (self);
	lrg_particle_effect_play (self->effect);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
}

void
lrg_particle_component_pause (LrgParticleComponent *self)
{
	g_return_if_fail (LRG_IS_PARTICLE_COMPONENT (self));
	if (self->effect == NULL)
		return;
	lrg_particle_effect_pause (self->effect);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
}

void
lrg_particle_component_stop (LrgParticleComponent *self)
{
	g_return_if_fail (LRG_IS_PARTICLE_COMPONENT (self));
	if (self->effect == NULL)
		return;
	lrg_particle_effect_stop (self->effect);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
}

void
lrg_particle_component_restart (LrgParticleComponent *self)
{
	g_return_if_fail (LRG_IS_PARTICLE_COMPONENT (self));
	ensure_effect (self);
	if (self->effect == NULL)
		return;
	follow_entity (self);
	lrg_particle_effect_restart (self->effect);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
}

gboolean
lrg_particle_component_get_auto_play (LrgParticleComponent *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_COMPONENT (self), FALSE);
	return self->auto_play;
}

void
lrg_particle_component_set_auto_play (LrgParticleComponent *self,
                                      gboolean              auto_play)
{
	g_return_if_fail (LRG_IS_PARTICLE_COMPONENT (self));

	auto_play = !!auto_play;
	if (self->auto_play == auto_play)
		return;
	self->auto_play = auto_play;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTO_PLAY]);
}

gfloat
lrg_particle_component_get_time_scale (LrgParticleComponent *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_COMPONENT (self), 1.0f);
	return self->time_scale;
}

void
lrg_particle_component_set_time_scale (LrgParticleComponent *self,
                                       gfloat                scale)
{
	g_return_if_fail (LRG_IS_PARTICLE_COMPONENT (self));

	scale = MAX (scale, 0.0f);
	if (self->time_scale == scale)
		return;
	self->time_scale = scale;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME_SCALE]);
}
