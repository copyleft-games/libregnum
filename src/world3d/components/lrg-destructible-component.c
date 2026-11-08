/* lrg-destructible-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDestructibleComponent.  Lifecycle:
 *   intact   - the entity's mesh renderer draws it, its rigid body
 *              collides; with auto-break the component listens to the
 *              physics world's contact-added signal for its body
 *   queued   - report_impact() above the threshold stores the hit; the
 *              next fixed_update breaks (never inside a physics emission)
 *   broken   - lrg_fracture_swap_in() added one body per chunk, the mesh
 *              renderer and rigid body components are disabled (the
 *              rigid body drops its body on its next sync), collect_render
 *              draws every chunk at its body's pose; after chunk-lifetime
 *              seconds (if set) the chunk bodies are removed
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>

#include "lrg-destructible-component.h"
#include "lrg-mesh-renderer-component.h"
#include "lrg-rigid-body-component.h"
#include "../lrg-entity3d.h"
#include "../lrg-world3d.h"
#include "../../core/lrg-math3d.h"

struct _LrgDestructibleComponent
{
	LrgComponent parent_instance;

	LrgFracture         *fracture;
	gboolean             fracture_explicit;
	gchar               *fracture_path;
	guint                chunk_count;
	guint                seed;
	LrgFractureSeedMode  seed_mode;
	guint                interior_material;
	gfloat               impulse_threshold;
	gfloat               density;
	guint                debris_layer;
	gboolean             auto_break;
	gfloat               chunk_lifetime;

	/* runtime */
	LrgDynamicsWorld    *physics;          /* while connected */
	gulong               contact_handler;
	gboolean             pending;
	gfloat               pending_point[3];
	gfloat               pending_impulse[3];
	gboolean             broken;
	GArray              *bodies;           /* guint per chunk */
	GPtrArray           *meshes;           /* GrlMesh per chunk, lazily */
	gfloat               elapsed;
	LrgComponent        *disabled[2];      /* components the break turned off */
};

enum
{
	PROP_0,
	PROP_FRACTURE,
	PROP_FRACTURE_PATH,
	PROP_CHUNK_COUNT,
	PROP_SEED,
	PROP_SEED_MODE,
	PROP_INTERIOR_MATERIAL,
	PROP_IMPULSE_THRESHOLD,
	PROP_DENSITY,
	PROP_DEBRIS_LAYER,
	PROP_AUTO_BREAK,
	PROP_CHUNK_LIFETIME,
	PROP_BROKEN,
	N_PROPS
};

enum
{
	SIGNAL_FRACTURED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgDestructibleComponent, lrg_destructible_component, LRG_TYPE_COMPONENT)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* The entity's world matrix split into a rigid matrix and a per-axis scale. */
static void
entity_rigid_matrix (LrgEntity3D *entity,
                     gfloat      *rigid,
                     gfloat      *scale)
{
	const gfloat *m = lrg_entity3d_peek_world_matrix (entity);
	guint         c;

	memcpy (rigid, m, 16 * sizeof (gfloat));
	for (c = 0; c < 3; c++)
	{
		gfloat len = sqrtf (m[4 * c] * m[4 * c] + m[4 * c + 1] * m[4 * c + 1] + m[4 * c + 2] * m[4 * c + 2]);

		scale[c] = len;
		if (len > 0.0f)
		{
			rigid[4 * c] /= len;
			rigid[4 * c + 1] /= len;
			rigid[4 * c + 2] /= len;
		}
	}
}

static void
unref_nullable (gpointer object)
{
	if (object != NULL)
		g_object_unref (object);
}

static void
disconnect_physics (LrgDestructibleComponent *self)
{
	if (self->physics != NULL && self->contact_handler != 0)
		g_signal_handler_disconnect (self->physics, self->contact_handler);
	self->contact_handler = 0;
	g_clear_object (&self->physics);
}

/* contact-added: a hit on our rigid body at or above the threshold queues the break. */
static void
on_contact_added (LrgDynamicsWorld         *world,
                  LrgDynamicsContact       *contact,
                  LrgDestructibleComponent *self)
{
	LrgEntity3D           *entity = lrg_component_get_entity (LRG_COMPONENT (self));
	LrgRigidBodyComponent *body;
	guint                  id;
	gfloat                 sign;
	gfloat                 impulse[3];

	if (entity == NULL || self->broken || !self->auto_break)
		return;
	body = (LrgRigidBodyComponent *) lrg_entity3d_get_component (entity, LRG_TYPE_RIGID_BODY_COMPONENT);
	if (body == NULL)
		return;
	id = lrg_rigid_body_component_get_body_id (body);
	if (contact->body_a == id)
		sign = -1.0f;      /* the normal points from A (us) to B: we are pushed back */
	else if (contact->body_b == id)
		sign = 1.0f;
	else
		return;
	impulse[0] = sign * contact->normal[0] * contact->normal_impulse;
	impulse[1] = sign * contact->normal[1] * contact->normal_impulse;
	impulse[2] = sign * contact->normal[2] * contact->normal_impulse;
	lrg_destructible_component_report_impact (self, contact->point, impulse);
}

/* Listens to the world's physics while auto-break is on. */
static void
connect_physics (LrgDestructibleComponent *self)
{
	LrgWorld3D       *world = lrg_component_get_world3d (LRG_COMPONENT (self));
	LrgDynamicsWorld *physics = world != NULL ? lrg_world3d_get_physics (world) : NULL;

	if (!self->auto_break || self->broken)
		physics = NULL;
	if (physics == self->physics)
		return;
	disconnect_physics (self);
	if (physics == NULL)
		return;
	self->physics = g_object_ref (physics);
	self->contact_handler = g_signal_connect (physics, "contact-added", G_CALLBACK (on_contact_added), self);
}

static void
remove_chunk_bodies (LrgDestructibleComponent *self)
{
	LrgWorld3D       *world = lrg_component_get_world3d (LRG_COMPONENT (self));
	LrgDynamicsWorld *physics = world != NULL ? lrg_world3d_get_physics (world) : NULL;
	guint             i;

	if (physics != NULL)
		for (i = 0; i < self->bodies->len; i++)
			lrg_dynamics_world_remove_body (physics, g_array_index (self->bodies, guint, i), NULL);
	g_array_set_size (self->bodies, 0);
}

/* ==========================================================================
 * Component hooks
 * ========================================================================== */

static void
lrg_destructible_component_fixed_update (LrgComponent *component,
                                         gfloat        step)
{
	LrgDestructibleComponent *self = LRG_DESTRUCTIBLE_COMPONENT (component);

	if (!lrg_component_get_enabled (component))
		return;
	connect_physics (self);
	if (self->pending && !self->broken)
	{
		g_autoptr(GError) error = NULL;

		self->pending = FALSE;
		if (!lrg_destructible_component_fracture (self, self->pending_point, self->pending_impulse, &error))
			g_debug ("Destructible could not break: %s", error->message);
	}
	if (self->broken && self->bodies->len > 0 && self->chunk_lifetime > 0.0f)
	{
		self->elapsed += step;
		if (self->elapsed >= self->chunk_lifetime)
			remove_chunk_bodies (self);
	}
}

static void
lrg_destructible_component_collect_render (LrgComponent  *component,
                                           LrgRenderList *list)
{
	LrgDestructibleComponent *self = LRG_DESTRUCTIBLE_COMPONENT (component);
	LrgEntity3D              *entity = lrg_component_get_entity (component);
	LrgWorld3D               *world = lrg_component_get_world3d (component);
	LrgDynamicsWorld         *physics = world != NULL ? lrg_world3d_get_physics (world) : NULL;
	LrgMeshRendererComponent *source = NULL;
	gboolean                  interpolate = world != NULL && lrg_world3d_get_physics_interpolation (world);
	guint                     i;

	if (!self->broken || entity == NULL || physics == NULL || self->fracture == NULL)
		return;
	source = (LrgMeshRendererComponent *) lrg_entity3d_get_component (entity, LRG_TYPE_MESH_RENDERER_COMPONENT);

	for (i = 0; i < self->bodies->len; i++)
	{
		const LrgFractureChunk *chunk = lrg_fracture_get_chunk (self->fracture, i);
		guint                   id = g_array_index (self->bodies, guint, i);
		LrgRenderItem           item;
		LrgBoundingBox3D        box;
		gfloat                  lo[3];
		gfloat                  hi[3];
		gfloat                  com[3];
		gfloat                  pos[3];
		gfloat                  rot[4];
		gfloat                  one[3] = { 1.0f, 1.0f, 1.0f };
		guint                   k;

		if (chunk == NULL)
			continue;
		if (interpolate)
		{
			if (!lrg_dynamics_world_get_interpolated_pose (physics, id, pos, rot, NULL))
				continue;
		}
		else
		{
			gfloat m[16];

			if (!lrg_dynamics_world_get_transform (physics, id, m, NULL))
				continue;
			pos[0] = m[12]; pos[1] = m[13]; pos[2] = m[14];
			lrg_math3d_matrix_to_quat (m, rot);
		}

		/* The chunk mesh is centred on its centre of mass, the body origin. */
		while (self->meshes->len <= i)
			g_ptr_array_add (self->meshes, NULL);
		if (g_ptr_array_index (self->meshes, i) == NULL && IsWindowReady ())
			g_ptr_array_index (self->meshes, i) = lrg_fracture_chunk_create_mesh (chunk, TRUE, NULL);

		lrg_render_item_init_for_entity (&item, entity, component);
		item.kind = LRG_RENDER_ITEM_MESH;
		item.mesh = g_ptr_array_index (self->meshes, i);
		lrg_math3d_compose_quat (pos, rot, one, item.world_matrix);
		item.color[0] = item.color[1] = item.color[2] = item.color[3] = 1.0f;
		if (source != NULL)
		{
			lrg_mesh_renderer_component_get_color (source, item.color);
			item.material = lrg_mesh_renderer_component_get_effective_material (source);
			item.flags = (LrgRenderItemFlags) lrg_mesh_renderer_component_get_flags (source);
		}
		else
		{
			item.flags = (LrgRenderItemFlags) (LRG_RENDER_ITEM_FLAG_CAST_SHADOW | LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW);
		}
		item.sort_key = ((guint64) item.kind << 56) | (guint64) i;

		lrg_fracture_chunk_get_bounds (chunk, lo, hi);
		lrg_fracture_chunk_get_center_of_mass (chunk, com);
		for (k = 0; k < 3; k++)
		{
			lo[k] -= com[k];
			hi[k] -= com[k];
		}
		box.min.x = lo[0]; box.min.y = lo[1]; box.min.z = lo[2];
		box.max.x = hi[0]; box.max.y = hi[1]; box.max.z = hi[2];
		lrg_render_item_set_local_bounds (&item, &box);
		if (lrg_render_list_accepts (list, &item))
			lrg_render_list_add (list, &item);
	}
}

static void
lrg_destructible_component_world_entered (LrgComponent *component,
                                          LrgWorld3D   *world)
{
	connect_physics (LRG_DESTRUCTIBLE_COMPONENT (component));
}

static void
lrg_destructible_component_world_exited (LrgComponent *component,
                                         LrgWorld3D   *world)
{
	LrgDestructibleComponent *self = LRG_DESTRUCTIBLE_COMPONENT (component);
	LrgDynamicsWorld         *physics = lrg_world3d_get_physics (world);
	guint                     i;

	disconnect_physics (self);
	if (physics != NULL)
		for (i = 0; i < self->bodies->len; i++)
			lrg_dynamics_world_remove_body (physics, g_array_index (self->bodies, guint, i), NULL);
	g_array_set_size (self->bodies, 0);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_destructible_component_dispose (GObject *object)
{
	LrgDestructibleComponent *self = LRG_DESTRUCTIBLE_COMPONENT (object);

	disconnect_physics (self);
	g_clear_object (&self->fracture);
	g_clear_pointer (&self->meshes, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_destructible_component_parent_class)->dispose (object);
}

static void
lrg_destructible_component_finalize (GObject *object)
{
	LrgDestructibleComponent *self = LRG_DESTRUCTIBLE_COMPONENT (object);

	g_free (self->fracture_path);
	g_array_unref (self->bodies);

	G_OBJECT_CLASS (lrg_destructible_component_parent_class)->finalize (object);
}

/* A change to a build parameter drops a derived fracture (rebuilt on use). */
static void
invalidate (LrgDestructibleComponent *self)
{
	if (!self->fracture_explicit)
		g_clear_object (&self->fracture);
}

static void
lrg_destructible_component_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
	LrgDestructibleComponent *self = LRG_DESTRUCTIBLE_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_FRACTURE:
		lrg_destructible_component_set_fracture (self, g_value_get_object (value));
		break;
	case PROP_FRACTURE_PATH:
		if (g_strcmp0 (self->fracture_path, g_value_get_string (value)) != 0)
		{
			g_free (self->fracture_path);
			self->fracture_path = g_value_dup_string (value);
			invalidate (self);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_CHUNK_COUNT:
		if (self->chunk_count != g_value_get_uint (value))
		{
			self->chunk_count = g_value_get_uint (value);
			invalidate (self);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_SEED:
		if (self->seed != g_value_get_uint (value))
		{
			self->seed = g_value_get_uint (value);
			invalidate (self);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_SEED_MODE:
		if (self->seed_mode != (LrgFractureSeedMode) g_value_get_enum (value))
		{
			self->seed_mode = (LrgFractureSeedMode) g_value_get_enum (value);
			invalidate (self);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_INTERIOR_MATERIAL:
		if (self->interior_material != g_value_get_uint (value))
		{
			self->interior_material = g_value_get_uint (value);
			invalidate (self);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_IMPULSE_THRESHOLD:
		if (self->impulse_threshold != g_value_get_float (value))
		{
			self->impulse_threshold = g_value_get_float (value);
			if (self->fracture != NULL && !self->fracture_explicit)
				g_object_set (self->fracture, "impulse-threshold", self->impulse_threshold, NULL);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_DENSITY:
		if (self->density != g_value_get_float (value))
		{
			self->density = g_value_get_float (value);
			if (self->fracture != NULL && !self->fracture_explicit)
				g_object_set (self->fracture, "density", self->density, NULL);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_DEBRIS_LAYER:
		if (self->debris_layer != g_value_get_uint (value))
		{
			self->debris_layer = g_value_get_uint (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_AUTO_BREAK:
		if (self->auto_break != g_value_get_boolean (value))
		{
			self->auto_break = g_value_get_boolean (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_CHUNK_LIFETIME:
		if (self->chunk_lifetime != g_value_get_float (value))
		{
			self->chunk_lifetime = g_value_get_float (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_destructible_component_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
	LrgDestructibleComponent *self = LRG_DESTRUCTIBLE_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_FRACTURE:
		g_value_set_object (value, self->fracture);
		break;
	case PROP_FRACTURE_PATH:
		g_value_set_string (value, self->fracture_path);
		break;
	case PROP_CHUNK_COUNT:
		g_value_set_uint (value, self->chunk_count);
		break;
	case PROP_SEED:
		g_value_set_uint (value, self->seed);
		break;
	case PROP_SEED_MODE:
		g_value_set_enum (value, (gint) self->seed_mode);
		break;
	case PROP_INTERIOR_MATERIAL:
		g_value_set_uint (value, self->interior_material);
		break;
	case PROP_IMPULSE_THRESHOLD:
		g_value_set_float (value, self->impulse_threshold);
		break;
	case PROP_DENSITY:
		g_value_set_float (value, self->density);
		break;
	case PROP_DEBRIS_LAYER:
		g_value_set_uint (value, self->debris_layer);
		break;
	case PROP_AUTO_BREAK:
		g_value_set_boolean (value, self->auto_break);
		break;
	case PROP_CHUNK_LIFETIME:
		g_value_set_float (value, self->chunk_lifetime);
		break;
	case PROP_BROKEN:
		g_value_set_boolean (value, self->broken);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_destructible_component_class_init (LrgDestructibleComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	GParamFlags        rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_destructible_component_dispose;
	object_class->finalize = lrg_destructible_component_finalize;
	object_class->set_property = lrg_destructible_component_set_property;
	object_class->get_property = lrg_destructible_component_get_property;
	component_class->fixed_update = lrg_destructible_component_fixed_update;
	component_class->collect_render = lrg_destructible_component_collect_render;
	component_class->world_entered = lrg_destructible_component_world_entered;
	component_class->world_exited = lrg_destructible_component_world_exited;

	/**
	 * LrgDestructibleComponent:fracture:
	 *
	 * The #LrgFracture in use (see lrg_destructible_component_set_fracture()).
	 */
	properties[PROP_FRACTURE] =
		g_param_spec_object ("fracture", NULL, "Fracture", LRG_TYPE_FRACTURE, rw);

	/**
	 * LrgDestructibleComponent:fracture-path:
	 *
	 * `.rfracture` cache file of the derived fracture (loaded when its
	 * hash matches, written after a build); %NULL builds in memory.
	 */
	properties[PROP_FRACTURE_PATH] =
		g_param_spec_string ("fracture-path", NULL, "Fracture cache file", NULL, rw);

	/**
	 * LrgDestructibleComponent:chunk-count:
	 *
	 * #LrgFracture:chunk-count of the derived fracture.
	 */
	properties[PROP_CHUNK_COUNT] =
		g_param_spec_uint ("chunk-count", NULL, "Chunks", 1, LRG_FRACTURE_MAX_CHUNKS, 12, rw);

	/**
	 * LrgDestructibleComponent:seed:
	 *
	 * #LrgFracture:seed of the derived fracture.
	 */
	properties[PROP_SEED] =
		g_param_spec_uint ("seed", NULL, "Random seed", 0, G_MAXUINT, 1, rw);

	/**
	 * LrgDestructibleComponent:seed-mode:
	 *
	 * #LrgFracture:seed-mode of the derived fracture.
	 */
	properties[PROP_SEED_MODE] =
		g_param_spec_enum ("seed-mode", NULL, "Seed placement", LRG_TYPE_FRACTURE_SEED_MODE,
		                   LRG_FRACTURE_SEED_RANDOM, rw);

	/**
	 * LrgDestructibleComponent:interior-material:
	 *
	 * #LrgFracture:interior-material of the derived fracture.
	 */
	properties[PROP_INTERIOR_MATERIAL] =
		g_param_spec_uint ("interior-material", NULL, "Cut face material id", 0, G_MAXUINT32, 1, rw);

	/**
	 * LrgDestructibleComponent:impulse-threshold:
	 *
	 * Contact impulse (N·s) that breaks the object.
	 */
	properties[PROP_IMPULSE_THRESHOLD] =
		g_param_spec_float ("impulse-threshold", NULL, "Break impulse (N s)", 0.0f, G_MAXFLOAT, 50.0f, rw);

	/**
	 * LrgDestructibleComponent:density:
	 *
	 * Chunk density in kg/m³.
	 */
	properties[PROP_DENSITY] =
		g_param_spec_float ("density", NULL, "Density (kg/m3)", 0.001f, G_MAXFLOAT, 1000.0f, rw);

	/**
	 * LrgDestructibleComponent:debris-layer:
	 *
	 * Object layer of the chunk bodies (default %LRG_PHYSICS_LAYER_DEBRIS).
	 */
	properties[PROP_DEBRIS_LAYER] =
		g_param_spec_uint ("debris-layer", NULL, "Chunk object layer", 0, LRG_PHYSICS_LAYERS_MAX - 1,
		                   LRG_PHYSICS_LAYER_DEBRIS, rw);

	/**
	 * LrgDestructibleComponent:auto-break:
	 *
	 * Watch the world's physics contacts on the entity's rigid body and
	 * break on a hit at or above the threshold.
	 */
	properties[PROP_AUTO_BREAK] =
		g_param_spec_boolean ("auto-break", NULL, "Break on impact", TRUE, rw);

	/**
	 * LrgDestructibleComponent:chunk-lifetime:
	 *
	 * Seconds after the break before the chunk bodies are removed (0
	 * keeps them).
	 */
	properties[PROP_CHUNK_LIFETIME] =
		g_param_spec_float ("chunk-lifetime", NULL, "Debris lifetime (s)", 0.0f, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgDestructibleComponent:broken:
	 *
	 * Whether the object has been broken (read only).
	 */
	properties[PROP_BROKEN] =
		g_param_spec_boolean ("broken", NULL, "Broken", FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgDestructibleComponent::fractured:
	 * @self: the component
	 * @n_chunks: number of chunk bodies swapped in
	 *
	 * The object broke (after the chunk bodies were added).
	 */
	signals[SIGNAL_FRACTURED] =
		g_signal_new ("fractured", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
		              NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
lrg_destructible_component_init (LrgDestructibleComponent *self)
{
	self->chunk_count = 12;
	self->seed = 1;
	self->seed_mode = LRG_FRACTURE_SEED_RANDOM;
	self->interior_material = 1;
	self->impulse_threshold = 50.0f;
	self->density = 1000.0f;
	self->debris_layer = LRG_PHYSICS_LAYER_DEBRIS;
	self->auto_break = TRUE;
	self->bodies = g_array_new (FALSE, FALSE, sizeof (guint));
	/* A NULL slot (no GL yet) is allowed in the array. */
	self->meshes = g_ptr_array_new_with_free_func (unref_nullable);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgDestructibleComponent *
lrg_destructible_component_new (void)
{
	return g_object_new (LRG_TYPE_DESTRUCTIBLE_COMPONENT, NULL);
}

LrgFracture *
lrg_destructible_component_get_fracture (LrgDestructibleComponent *self)
{
	g_return_val_if_fail (LRG_IS_DESTRUCTIBLE_COMPONENT (self), NULL);
	return self->fracture;
}

void
lrg_destructible_component_set_fracture (LrgDestructibleComponent *self,
                                         LrgFracture              *fracture)
{
	g_return_if_fail (LRG_IS_DESTRUCTIBLE_COMPONENT (self));
	g_return_if_fail (fracture == NULL || LRG_IS_FRACTURE (fracture));
	self->fracture_explicit = fracture != NULL;
	if (g_set_object (&self->fracture, fracture))
	{
		g_ptr_array_set_size (self->meshes, 0);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRACTURE]);
	}
}

gboolean
lrg_destructible_component_prepare (LrgDestructibleComponent  *self,
                                    GError                   **error)
{
	LrgEntity3D              *entity;
	LrgMeshRendererComponent *mesh;
	g_autoptr(LrgFracture)    fracture = NULL;
	gfloat                    rigid[16];
	gfloat                    scale[3];
	gfloat                    size[3];
	gfloat                    half[3];
	guint                     k;

	g_return_val_if_fail (LRG_IS_DESTRUCTIBLE_COMPONENT (self), FALSE);
	if (self->fracture != NULL && lrg_fracture_is_built (self->fracture))
		return TRUE;
	if (self->fracture != NULL && self->fracture_explicit)
	{
		if (lrg_fracture_has_source (self->fracture))
			return lrg_fracture_build (self->fracture, error);
		g_set_error_literal (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NO_SOURCE,
		                     "The destructible's fracture has neither chunks nor a source");
		return FALSE;
	}

	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	mesh = entity != NULL ? (LrgMeshRendererComponent *) lrg_entity3d_get_component (entity, LRG_TYPE_MESH_RENDERER_COMPONENT) : NULL;
	if (mesh == NULL || lrg_mesh_renderer_component_get_mesh_asset (mesh) != NULL
	    || lrg_mesh_renderer_component_get_primitive (mesh) != LRG_PRIMITIVE_CUBE)
	{
		g_set_error_literal (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NO_SOURCE,
		                     "A derived fracture needs a cube LrgMeshRendererComponent on the entity "
		                     "(set an LrgFracture for other shapes)");
		return FALSE;
	}

	/* The cube's size times the entity scale: chunks live in the rigid frame. */
	entity_rigid_matrix (entity, rigid, scale);
	lrg_mesh_renderer_component_get_size (mesh, size);
	for (k = 0; k < 3; k++)
		half[k] = 0.5f * size[k] * scale[k];

	fracture = lrg_fracture_new ();
	g_object_set (fracture, "chunk-count", self->chunk_count, "seed", self->seed,
	              "seed-mode", self->seed_mode == LRG_FRACTURE_SEED_CUSTOM ? LRG_FRACTURE_SEED_RANDOM : self->seed_mode,
	              "interior-material", self->interior_material,
	              "impulse-threshold", self->impulse_threshold, "density", self->density, NULL);
	if (!lrg_fracture_set_box (fracture, half, error)
	    || !lrg_fracture_load_or_build (fracture, self->fracture_path, error))
		return FALSE;
	g_set_object (&self->fracture, fracture);
	g_ptr_array_set_size (self->meshes, 0);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRACTURE]);
	return TRUE;
}

gboolean
lrg_destructible_component_fracture (LrgDestructibleComponent  *self,
                                     const gfloat              *impact_point,
                                     const gfloat              *impulse,
                                     GError                   **error)
{
	LrgEntity3D       *entity;
	LrgWorld3D        *world;
	LrgDynamicsWorld  *physics;
	LrgComponent      *c;
	g_autoptr(GArray)  ids = NULL;
	gfloat             rigid[16];
	gfloat             scale[3];

	g_return_val_if_fail (LRG_IS_DESTRUCTIBLE_COMPONENT (self), FALSE);
	if (self->broken)
		return TRUE;
	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	world = lrg_component_get_world3d (LRG_COMPONENT (self));
	physics = world != NULL ? lrg_world3d_get_physics (world) : NULL;
	if (entity == NULL || physics == NULL)
	{
		g_set_error_literal (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_INVALID,
		                     "The destructible is not in a world with physics");
		return FALSE;
	}
	if (!lrg_destructible_component_prepare (self, error))
		return FALSE;

	entity_rigid_matrix (entity, rigid, scale);
	ids = lrg_fracture_swap_in (self->fracture, physics, rigid, impact_point, impulse,
	                            self->debris_layer, error);
	if (ids == NULL)
		return FALSE;
	g_array_set_size (self->bodies, 0);
	g_array_append_vals (self->bodies, ids->data, ids->len);

	/* Hide the intact object; the rigid body drops its body on its next sync. */
	c = lrg_entity3d_get_component (entity, LRG_TYPE_MESH_RENDERER_COMPONENT);
	if (c != NULL && lrg_component_get_enabled (c))
	{
		lrg_component_set_enabled (c, FALSE);
		self->disabled[0] = c;
	}
	c = lrg_entity3d_get_component (entity, LRG_TYPE_RIGID_BODY_COMPONENT);
	if (c != NULL && lrg_component_get_enabled (c))
	{
		lrg_component_set_enabled (c, FALSE);
		self->disabled[1] = c;
	}

	self->broken = TRUE;
	self->pending = FALSE;
	self->elapsed = 0.0f;
	disconnect_physics (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BROKEN]);
	g_signal_emit (self, signals[SIGNAL_FRACTURED], 0, self->bodies->len);
	return TRUE;
}

gboolean
lrg_destructible_component_report_impact (LrgDestructibleComponent *self,
                                          const gfloat             *point,
                                          const gfloat             *impulse)
{
	gfloat magnitude;

	g_return_val_if_fail (LRG_IS_DESTRUCTIBLE_COMPONENT (self), FALSE);
	g_return_val_if_fail (point != NULL && impulse != NULL, FALSE);
	if (self->broken)
		return FALSE;
	magnitude = sqrtf (impulse[0] * impulse[0] + impulse[1] * impulse[1] + impulse[2] * impulse[2]);
	if (!isfinite (magnitude) || magnitude < self->impulse_threshold)
		return FALSE;
	/* Keep the strongest hit of the step. */
	if (!self->pending
	    || magnitude > sqrtf (self->pending_impulse[0] * self->pending_impulse[0]
	                          + self->pending_impulse[1] * self->pending_impulse[1]
	                          + self->pending_impulse[2] * self->pending_impulse[2]))
	{
		memcpy (self->pending_point, point, sizeof (self->pending_point));
		memcpy (self->pending_impulse, impulse, sizeof (self->pending_impulse));
	}
	self->pending = TRUE;
	return TRUE;
}

void
lrg_destructible_component_restore (LrgDestructibleComponent *self)
{
	guint i;

	g_return_if_fail (LRG_IS_DESTRUCTIBLE_COMPONENT (self));
	remove_chunk_bodies (self);
	for (i = 0; i < G_N_ELEMENTS (self->disabled); i++)
	{
		if (self->disabled[i] != NULL)
			lrg_component_set_enabled (self->disabled[i], TRUE);
		self->disabled[i] = NULL;
	}
	self->pending = FALSE;
	if (self->broken)
	{
		self->broken = FALSE;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BROKEN]);
	}
	connect_physics (self);
}

gboolean
lrg_destructible_component_is_broken (LrgDestructibleComponent *self)
{
	g_return_val_if_fail (LRG_IS_DESTRUCTIBLE_COMPONENT (self), FALSE);
	return self->broken;
}

guint
lrg_destructible_component_get_chunk_count (LrgDestructibleComponent *self)
{
	g_return_val_if_fail (LRG_IS_DESTRUCTIBLE_COMPONENT (self), 0);
	return self->bodies->len;
}

guint
lrg_destructible_component_get_chunk_body (LrgDestructibleComponent *self,
                                           guint                     index)
{
	g_return_val_if_fail (LRG_IS_DESTRUCTIBLE_COMPONENT (self), G_MAXUINT);
	if (index >= self->bodies->len)
		return G_MAXUINT;
	return g_array_index (self->bodies, guint, index);
}
