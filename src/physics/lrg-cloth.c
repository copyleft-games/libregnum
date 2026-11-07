/* lrg-cloth.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Portions derived from Jolt Physics (MIT), Copyright 2021 Jorrit Rouwe
 * (Jolt/Physics/SoftBody/SoftBodySharedSettings.cpp: CreateConstraints,
 * CalculateClosestKinematic, CalculateBendConstraintConstants;
 * Jolt/Physics/SoftBody/SoftBodyMotionProperties.cpp: IntegratePositions,
 * ApplyDihedralBendConstraints, ApplySkinConstraints, ApplyEdgeConstraints,
 * ApplyLRAConstraints, ApplyCollisionConstraintsAndUpdateVelocities).
 * Jolt Physics is distributed under the MIT license:
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to
 *   the following conditions: The above copyright notice and this
 *   permission notice shall be included in all copies or substantial
 *   portions of the Software.  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT
 *   WARRANTY OF ANY KIND.
 *
 * LrgCloth: XPBD cloth.  One step is split into equal substeps; each
 * substep integrates the particles (gravity, drag, damping), moves the
 * kinematic ones to their interpolated targets, then projects the
 * constraints Gauss-Seidel style in a fixed order (dihedral bend,
 * attachments, edges, long-range attachments), resolves self-collision
 * and collider contacts and finally derives velocities from the position
 * change (the XPBD velocity update).  Everything runs on one thread in a
 * fixed order, so the simulation is deterministic.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <float.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "physics/lrg-cloth.h"
#include "animation/lrg-skeleton.h"
#include "animation/lrg-bone.h"
#include "animation/lrg-bone-pose.h"
#include "core/lrg-math3d.h"
#include "physics/lrg-dynamics.h"
#include "physics/lrg-collide.h"

#define CLOTH_MAX_GRID        (1024u)
#define CLOTH_MAX_VERTICES    (1048576u)
#define CLOTH_MESH_MAX_VERTS  (65535u)
#define CLOTH_MAX_BONES       (1024u)
#define CLOTH_MAX_DT          (0.1f)
#define CLOTH_AIR_DENSITY     (1.225f)
/* Jolt's default angle tolerance for detecting quad diagonals (8 deg). */
#define CLOTH_SHEAR_TOLERANCE (0.13962634f)
#define NO_VERTEX             (G_MAXUINT32)
/* Largest vertex move of one bend projection, as a fraction of the hinge. */
#define CLOTH_BEND_MAX_MOVE   (0.05f)

/* Attachment kinds of a vertex. */
enum
{
	ATTACH_NONE  = 0,
	ATTACH_LOCAL = 1,
	ATTACH_BONE  = 2
};

typedef struct
{
	gint   kind;
	gint   bone;
	gfloat target[3];     /* local position or bone-space offset */
	gfloat max_distance;  /* 0 = hard (kinematic) */
} Attachment;

typedef struct
{
	guint32 v[2];
	gfloat  rest;
	gfloat  compliance;
	gboolean structural;   /* a mesh edge (counted by get_max_stretch) */
} EdgeConstraint;

typedef struct
{
	guint32 v[4];          /* shared edge v0-v1, opposite v2 and v3 */
	gfloat  angle;
	gfloat  compliance;
} BendConstraint;

typedef struct
{
	guint32 v[2];          /* v[0] kinematic, v[1] free */
	gfloat  max_distance;
} LraConstraint;

typedef struct
{
	guint                id;
	LrgClothColliderKind kind;
	gfloat               a[3];        /* sphere/box centre, capsule end, plane normal */
	gfloat               b[3];        /* capsule end */
	gfloat               radius;      /* sphere/capsule radius, plane offset */
	gfloat               half[3];     /* box half extents */
	gfloat               rot[4];      /* box rotation */
	gfloat               xf[16];      /* collider transform */
	gint                 bone;
	/* world-space cache, refreshed every step */
	gfloat               wa[3];
	gfloat               wb[3];
	gfloat               wr;
	gfloat               axes[9];     /* box axes, column-major 3x3 */
} Collider;

struct _LrgCloth
{
	GObject parent_instance;

	/* topology */
	guint      n_vertices;
	guint      n_indices;
	guint32   *indices;
	gfloat    *rest;        /* local rest positions, 3 per vertex */
	gfloat    *uv;          /* 2 per vertex */
	guint      grid_columns;
	guint      grid_rows;

	/* particle state (world space) */
	gfloat    *pos;
	gfloat    *prev;        /* position at the start of the substep */
	gfloat    *vel;
	gfloat    *force;       /* aerodynamic force scratch */
	gfloat    *normals;
	gboolean   normals_dirty;
	gfloat    *area_mass;   /* mass from the surface density */
	gfloat    *inv_mass;    /* 0 for kinematic vertices */
	Attachment *attach;

	/* kinematic targets over one step */
	gfloat    *target_from;
	gfloat    *target_to;

	/* constraints, rebuilt lazily */
	gboolean   dirty;
	GArray    *edges;
	GArray    *bends;
	GArray    *lras;

	/* placement */
	gfloat     xf[16];
	gfloat     prev_xf[16];
	gfloat    *bones;
	gfloat    *prev_bones;
	guint      n_bones;

	/* colliders */
	GArray    *colliders;
	guint      next_collider_id;
	guint      contact_count;

	/* dynamics world collision: one plane per particle per step */
	LrgDynamicsWorld *world;
	guint32    world_mask;
	gfloat    *world_planes;   /* nx, ny, nz, d per particle */
	guint8    *world_has;
	LrgPhysicsShape *probe;

	/* self collision scratch */
	guint     *hash_start;
	guint     *hash_entries;
	guint      hash_size;

	/* parameters */
	gfloat     gravity[3];
	gfloat     wind[3];
	guint      substeps;
	gfloat     stretch_compliance;
	gfloat     shear_compliance;
	gfloat     bend_compliance;
	LrgClothBendMode bend_mode;
	LrgClothLraMode  lra_mode;
	gfloat     lra_multiplier;
	gfloat     damping;
	gfloat     drag;
	gfloat     friction;
	gfloat     thickness;
	gfloat     density;
	gboolean   self_collision;
	gfloat     self_collision_distance;
	gboolean   shear_edges;
	gfloat     max_speed;

	/* rendering */
	GrlMesh   *draw_mesh;
};

enum
{
	PROP_0,
	PROP_SUBSTEPS,
	PROP_STRETCH_COMPLIANCE,
	PROP_SHEAR_COMPLIANCE,
	PROP_BEND_COMPLIANCE,
	PROP_BEND_MODE,
	PROP_LRA_MODE,
	PROP_LRA_MULTIPLIER,
	PROP_DAMPING,
	PROP_DRAG,
	PROP_FRICTION,
	PROP_THICKNESS,
	PROP_DENSITY,
	PROP_SELF_COLLISION,
	PROP_SELF_COLLISION_DISTANCE,
	PROP_MAX_SPEED,
	PROP_SHEAR_EDGES,
	PROP_VERTEX_COUNT,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (LrgCloth, lrg_cloth, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-cloth-error-quark, lrg_cloth_error)

/* ==========================================================================
 * Enum types
 * ========================================================================== */

GType
lrg_cloth_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_CLOTH_ERROR_INVALID, "LRG_CLOTH_ERROR_INVALID", "invalid" },
			{ LRG_CLOTH_ERROR_NO_GPU, "LRG_CLOTH_ERROR_NO_GPU", "no-gpu" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgClothError"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_cloth_bend_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_CLOTH_BEND_NONE, "LRG_CLOTH_BEND_NONE", "none" },
			{ LRG_CLOTH_BEND_DISTANCE, "LRG_CLOTH_BEND_DISTANCE", "distance" },
			{ LRG_CLOTH_BEND_DIHEDRAL, "LRG_CLOTH_BEND_DIHEDRAL", "dihedral" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgClothBendMode"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_cloth_lra_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_CLOTH_LRA_NONE, "LRG_CLOTH_LRA_NONE", "none" },
			{ LRG_CLOTH_LRA_EUCLIDEAN, "LRG_CLOTH_LRA_EUCLIDEAN", "euclidean" },
			{ LRG_CLOTH_LRA_GEODESIC, "LRG_CLOTH_LRA_GEODESIC", "geodesic" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgClothLraMode"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_cloth_collider_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_CLOTH_COLLIDER_SPHERE, "LRG_CLOTH_COLLIDER_SPHERE", "sphere" },
			{ LRG_CLOTH_COLLIDER_CAPSULE, "LRG_CLOTH_COLLIDER_CAPSULE", "capsule" },
			{ LRG_CLOTH_COLLIDER_PLANE, "LRG_CLOTH_COLLIDER_PLANE", "plane" },
			{ LRG_CLOTH_COLLIDER_BOX, "LRG_CLOTH_COLLIDER_BOX", "box" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgClothColliderKind"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_cloth_constraint_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_CLOTH_CONSTRAINT_EDGE, "LRG_CLOTH_CONSTRAINT_EDGE", "edge" },
			{ LRG_CLOTH_CONSTRAINT_BEND, "LRG_CLOTH_CONSTRAINT_BEND", "bend" },
			{ LRG_CLOTH_CONSTRAINT_LRA, "LRG_CLOTH_CONSTRAINT_LRA", "lra" },
			{ LRG_CLOTH_CONSTRAINT_ATTACHMENT, "LRG_CLOTH_CONSTRAINT_ATTACHMENT", "attachment" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgClothConstraintKind"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Small vector helpers
 * ========================================================================== */

static inline void
v3_sub (const gfloat *a, const gfloat *b, gfloat *out)
{
	out[0] = a[0] - b[0];
	out[1] = a[1] - b[1];
	out[2] = a[2] - b[2];
}

static inline gfloat
v3_dot (const gfloat *a, const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void
v3_cross (const gfloat *a, const gfloat *b, gfloat *out)
{
	gfloat x = a[1] * b[2] - a[2] * b[1];
	gfloat y = a[2] * b[0] - a[0] * b[2];
	gfloat z = a[0] * b[1] - a[1] * b[0];

	out[0] = x;
	out[1] = y;
	out[2] = z;
}

static inline gfloat
v3_len (const gfloat *a)
{
	return sqrtf (v3_dot (a, a));
}

static gboolean
v3_finite (const gfloat *a)
{
	return isfinite (a[0]) && isfinite (a[1]) && isfinite (a[2]);
}

/* Rotates @v by the unit quaternion @q (x, y, z, w). */
static void
quat_to_axes (const gfloat *q,
              gfloat       *axes)
{
	gfloat m[16];
	gfloat n[4];

	lrg_math3d_quat_normalize (q, n);
	lrg_math3d_quat_to_matrix (n, m);
	axes[0] = m[0]; axes[1] = m[1]; axes[2] = m[2];
	axes[3] = m[4]; axes[4] = m[5]; axes[5] = m[6];
	axes[6] = m[8]; axes[7] = m[9]; axes[8] = m[10];
}

/* ==========================================================================
 * Storage
 * ========================================================================== */

static void
free_geometry (LrgCloth *self)
{
	g_clear_pointer (&self->indices, g_free);
	g_clear_pointer (&self->rest, g_free);
	g_clear_pointer (&self->uv, g_free);
	g_clear_pointer (&self->pos, g_free);
	g_clear_pointer (&self->prev, g_free);
	g_clear_pointer (&self->vel, g_free);
	g_clear_pointer (&self->force, g_free);
	g_clear_pointer (&self->normals, g_free);
	g_clear_pointer (&self->area_mass, g_free);
	g_clear_pointer (&self->inv_mass, g_free);
	g_clear_pointer (&self->attach, g_free);
	g_clear_pointer (&self->target_from, g_free);
	g_clear_pointer (&self->target_to, g_free);
	g_clear_pointer (&self->hash_start, g_free);
	g_clear_pointer (&self->hash_entries, g_free);
	g_clear_pointer (&self->world_planes, g_free);
	g_clear_pointer (&self->world_has, g_free);
	self->hash_size = 0;
	self->n_vertices = 0;
	self->n_indices = 0;
	self->grid_columns = 0;
	self->grid_rows = 0;
}

static void
lrg_cloth_finalize (GObject *object)
{
	LrgCloth *self = LRG_CLOTH (object);

	free_geometry (self);
	g_clear_pointer (&self->edges, g_array_unref);
	g_clear_pointer (&self->bends, g_array_unref);
	g_clear_pointer (&self->lras, g_array_unref);
	g_clear_pointer (&self->colliders, g_array_unref);
	g_clear_pointer (&self->bones, g_free);
	g_clear_pointer (&self->prev_bones, g_free);
	g_clear_object (&self->draw_mesh);
	g_clear_object (&self->world);
	g_clear_object (&self->probe);

	G_OBJECT_CLASS (lrg_cloth_parent_class)->finalize (object);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

/* Sets a float field and notifies when it changed. */
static void
set_float (LrgCloth *self,
           gfloat   *field,
           gfloat    value,
           guint     prop,
           gboolean  rebuild)
{
	if (*field == value)
		return;
	*field = value;
	if (rebuild)
		self->dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
lrg_cloth_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	LrgCloth *self = LRG_CLOTH (object);

	switch (prop_id)
	{
	case PROP_SUBSTEPS:
		lrg_cloth_set_substeps (self, g_value_get_uint (value));
		break;
	case PROP_STRETCH_COMPLIANCE:
		set_float (self, &self->stretch_compliance, g_value_get_float (value), prop_id, TRUE);
		break;
	case PROP_SHEAR_COMPLIANCE:
		set_float (self, &self->shear_compliance, g_value_get_float (value), prop_id, TRUE);
		break;
	case PROP_BEND_COMPLIANCE:
		set_float (self, &self->bend_compliance, g_value_get_float (value), prop_id, TRUE);
		break;
	case PROP_BEND_MODE:
		if (self->bend_mode != (LrgClothBendMode) g_value_get_enum (value))
		{
			self->bend_mode = (LrgClothBendMode) g_value_get_enum (value);
			self->dirty = TRUE;
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_LRA_MODE:
		if (self->lra_mode != (LrgClothLraMode) g_value_get_enum (value))
		{
			self->lra_mode = (LrgClothLraMode) g_value_get_enum (value);
			self->dirty = TRUE;
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_LRA_MULTIPLIER:
		set_float (self, &self->lra_multiplier, g_value_get_float (value), prop_id, TRUE);
		break;
	case PROP_DAMPING:
		set_float (self, &self->damping, g_value_get_float (value), prop_id, FALSE);
		break;
	case PROP_DRAG:
		set_float (self, &self->drag, g_value_get_float (value), prop_id, FALSE);
		break;
	case PROP_FRICTION:
		set_float (self, &self->friction, g_value_get_float (value), prop_id, FALSE);
		break;
	case PROP_THICKNESS:
		set_float (self, &self->thickness, g_value_get_float (value), prop_id, FALSE);
		break;
	case PROP_DENSITY:
		set_float (self, &self->density, g_value_get_float (value), prop_id, TRUE);
		break;
	case PROP_SELF_COLLISION:
		if (self->self_collision != g_value_get_boolean (value))
		{
			self->self_collision = g_value_get_boolean (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_SELF_COLLISION_DISTANCE:
		set_float (self, &self->self_collision_distance, g_value_get_float (value), prop_id, FALSE);
		break;
	case PROP_MAX_SPEED:
		set_float (self, &self->max_speed, g_value_get_float (value), prop_id, FALSE);
		break;
	case PROP_SHEAR_EDGES:
		if (self->shear_edges != g_value_get_boolean (value))
		{
			self->shear_edges = g_value_get_boolean (value);
			self->dirty = TRUE;
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_cloth_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	LrgCloth *self = LRG_CLOTH (object);

	switch (prop_id)
	{
	case PROP_SUBSTEPS:
		g_value_set_uint (value, self->substeps);
		break;
	case PROP_STRETCH_COMPLIANCE:
		g_value_set_float (value, self->stretch_compliance);
		break;
	case PROP_SHEAR_COMPLIANCE:
		g_value_set_float (value, self->shear_compliance);
		break;
	case PROP_BEND_COMPLIANCE:
		g_value_set_float (value, self->bend_compliance);
		break;
	case PROP_BEND_MODE:
		g_value_set_enum (value, (gint) self->bend_mode);
		break;
	case PROP_LRA_MODE:
		g_value_set_enum (value, (gint) self->lra_mode);
		break;
	case PROP_LRA_MULTIPLIER:
		g_value_set_float (value, self->lra_multiplier);
		break;
	case PROP_DAMPING:
		g_value_set_float (value, self->damping);
		break;
	case PROP_DRAG:
		g_value_set_float (value, self->drag);
		break;
	case PROP_FRICTION:
		g_value_set_float (value, self->friction);
		break;
	case PROP_THICKNESS:
		g_value_set_float (value, self->thickness);
		break;
	case PROP_DENSITY:
		g_value_set_float (value, self->density);
		break;
	case PROP_SELF_COLLISION:
		g_value_set_boolean (value, self->self_collision);
		break;
	case PROP_SELF_COLLISION_DISTANCE:
		g_value_set_float (value, self->self_collision_distance);
		break;
	case PROP_MAX_SPEED:
		g_value_set_float (value, self->max_speed);
		break;
	case PROP_SHEAR_EDGES:
		g_value_set_boolean (value, self->shear_edges);
		break;
	case PROP_VERTEX_COUNT:
		g_value_set_uint (value, self->n_vertices);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_cloth_class_init (LrgClothClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->finalize = lrg_cloth_finalize;
	object_class->set_property = lrg_cloth_set_property;
	object_class->get_property = lrg_cloth_get_property;

	/**
	 * LrgCloth:substeps:
	 *
	 * XPBD substeps per lrg_cloth_step() (1 .. %LRG_CLOTH_MAX_SUBSTEPS).
	 * More substeps make every constraint stiffer and collisions more
	 * robust at a linear cost.
	 */
	properties[PROP_SUBSTEPS] =
		g_param_spec_uint ("substeps", NULL, NULL, 1, LRG_CLOTH_MAX_SUBSTEPS, 8, rw);

	/**
	 * LrgCloth:stretch-compliance:
	 *
	 * Relative compliance of the structural edges: each projection
	 * removes 1 / (1 + c) of an edge's stretch, independent of the mesh
	 * resolution and the particle masses (0 = inextensible).  Unlike
	 * Jolt's absolute XPBD compliance (m/N), one value behaves alike on a
	 * coarse heavy flag and a fine light scarf.
	 */
	properties[PROP_STRETCH_COMPLIANCE] =
		g_param_spec_float ("stretch-compliance", NULL, NULL, 0.0f, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgCloth:shear-compliance:
	 *
	 * Relative compliance of the quad diagonals (the mesh's own diagonal
	 * and, with #LrgCloth:shear-edges, the other one).  0.5 by default:
	 * rigid diagonals together with rigid structural edges make the
	 * one-pass Gauss-Seidel solve oscillate on fine meshes.
	 */
	properties[PROP_SHEAR_COMPLIANCE] =
		g_param_spec_float ("shear-compliance", NULL, NULL, 0.0f, G_MAXFLOAT, 0.5f, rw);

	/**
	 * LrgCloth:bend-compliance:
	 *
	 * Relative compliance of the bend constraints; larger folds more
	 * easily (1 removes half of a bend per projection).
	 */
	properties[PROP_BEND_COMPLIANCE] =
		g_param_spec_float ("bend-compliance", NULL, NULL, 0.0f, G_MAXFLOAT, 1.0f, rw);

	/**
	 * LrgCloth:bend-mode:
	 *
	 * How bending is resisted (#LrgClothBendMode).
	 */
	properties[PROP_BEND_MODE] =
		g_param_spec_enum ("bend-mode", NULL, NULL, LRG_TYPE_CLOTH_BEND_MODE,
		                   LRG_CLOTH_BEND_DISTANCE, rw);

	/**
	 * LrgCloth:lra-mode:
	 *
	 * Long-range attachment mode (#LrgClothLraMode).
	 */
	properties[PROP_LRA_MODE] =
		g_param_spec_enum ("lra-mode", NULL, NULL, LRG_TYPE_CLOTH_LRA_MODE,
		                   LRG_CLOTH_LRA_GEODESIC, rw);

	/**
	 * LrgCloth:lra-multiplier:
	 *
	 * Slack of the long-range attachments: the allowed distance is the
	 * rest distance times this (1 = no stretch at all).
	 */
	properties[PROP_LRA_MULTIPLIER] =
		g_param_spec_float ("lra-multiplier", NULL, NULL, 1.0f, 10.0f, 1.0f, rw);

	/**
	 * LrgCloth:damping:
	 *
	 * Linear velocity damping per second (Jolt's linear damping).
	 */
	properties[PROP_DAMPING] =
		g_param_spec_float ("damping", NULL, NULL, 0.0f, 100.0f, 0.1f, rw);

	/**
	 * LrgCloth:drag:
	 *
	 * Aerodynamic drag coefficient of the triangles against the air
	 * (#lrg_cloth_set_wind); 0 turns wind and air resistance off.
	 */
	properties[PROP_DRAG] =
		g_param_spec_float ("drag", NULL, NULL, 0.0f, 100.0f, 1.0f, rw);

	/**
	 * LrgCloth:friction:
	 *
	 * Coulomb friction against colliders (0 slides freely).
	 */
	properties[PROP_FRICTION] =
		g_param_spec_float ("friction", NULL, NULL, 0.0f, 10.0f, 0.3f, rw);

	/**
	 * LrgCloth:thickness:
	 *
	 * Radius of a particle against the colliders, in metres.
	 */
	properties[PROP_THICKNESS] =
		g_param_spec_float ("thickness", NULL, NULL, 0.0f, 10.0f, 0.01f, rw);

	/**
	 * LrgCloth:density:
	 *
	 * Surface density in kg/m²; a vertex weighs a third of the area of
	 * its triangles times this.
	 */
	properties[PROP_DENSITY] =
		g_param_spec_float ("density", NULL, NULL, 0.0001f, 1000.0f, 0.25f, rw);

	/**
	 * LrgCloth:self-collision:
	 *
	 * Keeps particles that are not neighbours at rest at least
	 * #LrgCloth:self-collision-distance apart (a spatial hash pass every
	 * substep).
	 */
	properties[PROP_SELF_COLLISION] =
		g_param_spec_boolean ("self-collision", NULL, NULL, FALSE, rw);

	/**
	 * LrgCloth:self-collision-distance:
	 *
	 * Minimum distance between particles for self-collision.
	 */
	properties[PROP_SELF_COLLISION_DISTANCE] =
		g_param_spec_float ("self-collision-distance", NULL, NULL, 0.0001f, 10.0f, 0.02f, rw);

	/**
	 * LrgCloth:max-speed:
	 *
	 * Particle speed cap in m/s (keeps a bad frame from exploding).
	 */
	properties[PROP_MAX_SPEED] =
		g_param_spec_float ("max-speed", NULL, NULL, 0.1f, 10000.0f, 50.0f, rw);

	/**
	 * LrgCloth:shear-edges:
	 *
	 * Also constrain the second diagonal of every quad (two triangles
	 * forming a flat quad), with #LrgCloth:shear-compliance, as Jolt
	 * does.  Off by default: the mesh's own diagonal already resists
	 * shear, and both diagonals at once over-constrain the quad, which
	 * with near-zero compliance oscillates on fine meshes.  Turn it on
	 * with a soft shear compliance for symmetric shear on coarse cloth.
	 */
	properties[PROP_SHEAR_EDGES] =
		g_param_spec_boolean ("shear-edges", NULL, NULL, FALSE, rw);

	/**
	 * LrgCloth:vertex-count:
	 *
	 * Number of particles (read only).
	 */
	properties[PROP_VERTEX_COUNT] =
		g_param_spec_uint ("vertex-count", NULL, NULL, 0, G_MAXUINT, 0,
		                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_cloth_init (LrgCloth *self)
{
	self->edges = g_array_new (FALSE, FALSE, sizeof (EdgeConstraint));
	self->bends = g_array_new (FALSE, FALSE, sizeof (BendConstraint));
	self->lras = g_array_new (FALSE, FALSE, sizeof (LraConstraint));
	self->colliders = g_array_new (FALSE, FALSE, sizeof (Collider));
	lrg_math3d_identity (self->xf);
	lrg_math3d_identity (self->prev_xf);
	self->gravity[1] = -9.81f;
	self->substeps = 8;
	self->bend_compliance = 1.0f;
	self->shear_compliance = 0.5f;
	self->bend_mode = LRG_CLOTH_BEND_DISTANCE;
	self->lra_mode = LRG_CLOTH_LRA_GEODESIC;
	self->lra_multiplier = 1.0f;
	self->damping = 0.1f;
	self->drag = 1.0f;
	self->friction = 0.3f;
	self->thickness = 0.01f;
	self->density = 0.25f;
	self->self_collision_distance = 0.02f;
	self->max_speed = 50.0f;
	self->dirty = TRUE;
}

LrgCloth *
lrg_cloth_new (void)
{
	return g_object_new (LRG_TYPE_CLOTH, NULL);
}

LrgCloth *
lrg_cloth_new_grid (guint  columns,
                    guint  rows,
                    gfloat width,
                    gfloat height)
{
	g_autoptr(LrgCloth) self = NULL;
	gfloat origin[3] = { 0.0f, 0.0f, 0.0f };
	gfloat u[3] = { 0.0f, 0.0f, 0.0f };
	gfloat v[3] = { 0.0f, 0.0f, 0.0f };

	if (columns < 2 || rows < 2 || !(width > 0.0f) || !(height > 0.0f))
		return NULL;
	u[0] = width / (gfloat) (columns - 1);
	v[1] = -height / (gfloat) (rows - 1);
	self = lrg_cloth_new ();
	if (!lrg_cloth_set_grid (self, columns, rows, origin, u, v, NULL))
		return NULL;
	return g_steal_pointer (&self);
}

/* ==========================================================================
 * Geometry
 * ========================================================================== */

/*
 * Takes ownership of validated arrays, allocates the particle state and
 * places the particles at their rest shape under the current transform.
 */
static void
adopt_geometry (LrgCloth *self,
                guint     n_vertices,
                gfloat   *rest,
                gfloat   *uv,
                guint     n_indices,
                guint32  *indices)
{
	guint i;
	guint t;

	free_geometry (self);
	self->n_vertices = n_vertices;
	self->n_indices = n_indices;
	self->rest = rest;
	self->uv = uv;
	self->indices = indices;
	self->pos = g_new0 (gfloat, 3 * (gsize) n_vertices);
	self->prev = g_new0 (gfloat, 3 * (gsize) n_vertices);
	self->vel = g_new0 (gfloat, 3 * (gsize) n_vertices);
	self->force = g_new0 (gfloat, 3 * (gsize) n_vertices);
	self->normals = g_new0 (gfloat, 3 * (gsize) n_vertices);
	self->area_mass = g_new0 (gfloat, n_vertices);
	self->inv_mass = g_new0 (gfloat, n_vertices);
	self->attach = g_new0 (Attachment, n_vertices);
	self->target_from = g_new0 (gfloat, 3 * (gsize) n_vertices);
	self->target_to = g_new0 (gfloat, 3 * (gsize) n_vertices);
	for (i = 0; i < n_vertices; i++)
	{
		self->attach[i].kind = ATTACH_NONE;
		self->attach[i].bone = LRG_CLOTH_NO_BONE;
	}

	/* Area of every triangle split evenly between its corners (unit density, scaled later). */
	for (t = 0; t + 2 < n_indices; t += 3)
	{
		gfloat e1[3];
		gfloat e2[3];
		gfloat c[3];
		gfloat area;

		v3_sub (rest + 3 * indices[t + 1], rest + 3 * indices[t], e1);
		v3_sub (rest + 3 * indices[t + 2], rest + 3 * indices[t], e2);
		v3_cross (e1, e2, c);
		area = 0.5f * v3_len (c);
		for (i = 0; i < 3; i++)
			self->area_mass[indices[t + i]] += area / 3.0f;
	}

	self->dirty = TRUE;
	self->normals_dirty = TRUE;
	lrg_cloth_reset (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VERTEX_COUNT]);
}

gboolean
lrg_cloth_set_grid (LrgCloth      *self,
                    guint          columns,
                    guint          rows,
                    const gfloat  *origin,
                    const gfloat  *u_step,
                    const gfloat  *v_step,
                    GError       **error)
{
	gfloat  *rest;
	gfloat  *uv;
	guint32 *indices;
	guint    n;
	guint    r;
	guint    c;
	guint    k;
	gfloat   cr[3];

	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	g_return_val_if_fail (origin != NULL && u_step != NULL && v_step != NULL, FALSE);

	v3_cross (u_step, v_step, cr);
	if (columns < 2 || rows < 2 || columns > CLOTH_MAX_GRID || rows > CLOTH_MAX_GRID
	    || !v3_finite (origin) || !v3_finite (u_step) || !v3_finite (v_step)
	    || !(v3_len (cr) > 1.0e-12f))
	{
		g_set_error (error, LRG_CLOTH_ERROR, LRG_CLOTH_ERROR_INVALID,
		             "Invalid cloth grid %ux%u (2 .. %u vertices per side, independent finite steps)",
		             columns, rows, CLOTH_MAX_GRID);
		return FALSE;
	}

	n = columns * rows;
	rest = g_new (gfloat, 3 * (gsize) n);
	uv = g_new (gfloat, 2 * (gsize) n);
	indices = g_new (guint32, 6 * (gsize) (columns - 1) * (rows - 1));
	for (r = 0; r < rows; r++)
	{
		for (c = 0; c < columns; c++)
		{
			guint i = r * columns + c;

			for (k = 0; k < 3; k++)
				rest[3 * i + k] = origin[k] + (gfloat) c * u_step[k] + (gfloat) r * v_step[k];
			uv[2 * i] = (gfloat) c / (gfloat) (columns - 1);
			uv[2 * i + 1] = (gfloat) r / (gfloat) (rows - 1);
		}
	}

	/* Two triangles per quad, the diagonal alternating in a checkerboard. */
	k = 0;
	for (r = 0; r + 1 < rows; r++)
	{
		for (c = 0; c + 1 < columns; c++)
		{
			guint32 i00 = r * columns + c;
			guint32 i10 = i00 + 1;
			guint32 i01 = i00 + columns;
			guint32 i11 = i01 + 1;

			if (((r + c) & 1) == 0)
			{
				indices[k++] = i00; indices[k++] = i01; indices[k++] = i11;
				indices[k++] = i00; indices[k++] = i11; indices[k++] = i10;
			}
			else
			{
				indices[k++] = i00; indices[k++] = i01; indices[k++] = i10;
				indices[k++] = i10; indices[k++] = i01; indices[k++] = i11;
			}
		}
	}

	adopt_geometry (self, n, rest, uv, k, indices);
	self->grid_columns = columns;
	self->grid_rows = rows;
	return TRUE;
}

gboolean
lrg_cloth_set_mesh (LrgCloth       *self,
                    guint           n_vertices,
                    const gfloat   *positions,
                    const gfloat   *texcoords,
                    guint           n_indices,
                    const guint32  *indices,
                    GError        **error)
{
	guint i;

	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);

	if (n_vertices < 3 || n_vertices > CLOTH_MAX_VERTICES || positions == NULL
	    || indices == NULL || n_indices < 3 || (n_indices % 3) != 0)
	{
		g_set_error (error, LRG_CLOTH_ERROR, LRG_CLOTH_ERROR_INVALID,
		             "Invalid cloth mesh: %u vertices, %u indices", n_vertices, n_indices);
		return FALSE;
	}
	for (i = 0; i < n_vertices; i++)
	{
		if (!v3_finite (positions + 3 * i))
		{
			g_set_error (error, LRG_CLOTH_ERROR, LRG_CLOTH_ERROR_INVALID,
			             "Vertex %u of the cloth mesh is not finite", i);
			return FALSE;
		}
	}
	for (i = 0; i < n_indices; i += 3)
	{
		guint32 a = indices[i];
		guint32 b = indices[i + 1];
		guint32 c = indices[i + 2];

		if (a >= n_vertices || b >= n_vertices || c >= n_vertices)
		{
			g_set_error (error, LRG_CLOTH_ERROR, LRG_CLOTH_ERROR_INVALID,
			             "Triangle %u of the cloth mesh indexes past %u vertices", i / 3, n_vertices);
			return FALSE;
		}
		if (a == b || b == c || a == c)
		{
			g_set_error (error, LRG_CLOTH_ERROR, LRG_CLOTH_ERROR_INVALID,
			             "Triangle %u of the cloth mesh is degenerate", i / 3);
			return FALSE;
		}
	}

	adopt_geometry (self, n_vertices,
	                g_memdup2 (positions, 3 * sizeof (gfloat) * n_vertices),
	                texcoords != NULL ? g_memdup2 (texcoords, 2 * sizeof (gfloat) * n_vertices)
	                                  : g_new0 (gfloat, 2 * (gsize) n_vertices),
	                n_indices,
	                g_memdup2 (indices, sizeof (guint32) * n_indices));
	return TRUE;
}

guint
lrg_cloth_get_vertex_count (LrgCloth *self)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), 0);
	return self->n_vertices;
}

guint
lrg_cloth_get_triangle_count (LrgCloth *self)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), 0);
	return self->n_indices / 3;
}

guint
lrg_cloth_get_grid_index (LrgCloth *self,
                          guint     column,
                          guint     row)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), G_MAXUINT);
	if (self->grid_columns == 0 || column >= self->grid_columns || row >= self->grid_rows)
		return G_MAXUINT;
	return row * self->grid_columns + column;
}

gboolean
lrg_cloth_get_grid_size (LrgCloth *self,
                         guint    *columns,
                         guint    *rows)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	if (columns != NULL)
		*columns = self->grid_columns;
	if (rows != NULL)
		*rows = self->grid_rows;
	return self->grid_columns > 0;
}

/* ==========================================================================
 * Kinematic targets
 * ========================================================================== */

/*
 * World target of an attached vertex under transform @xf and bone
 * matrices @bones.  Returns FALSE for a free vertex or a missing bone.
 */
static gboolean
attachment_target (LrgCloth     *self,
                   guint         i,
                   const gfloat *xf,
                   const gfloat *bones,
                   gfloat       *out)
{
	const Attachment *a = &self->attach[i];
	gfloat            local[3];

	if (a->kind == ATTACH_LOCAL)
	{
		lrg_math3d_transform_point (xf, a->target, out);
		return TRUE;
	}
	if (a->kind == ATTACH_BONE && bones != NULL && a->bone >= 0 && (guint) a->bone < self->n_bones)
	{
		lrg_math3d_transform_point (bones + 16 * a->bone, a->target, local);
		lrg_math3d_transform_point (xf, local, out);
		return TRUE;
	}
	return FALSE;
}

/* Hard attachments make the vertex kinematic; everything else keeps its area mass. */
static void
refresh_inverse_masses (LrgCloth *self)
{
	guint i;

	for (i = 0; i < self->n_vertices; i++)
	{
		gfloat m = self->area_mass[i] * self->density;
		gboolean hard = self->attach[i].kind != ATTACH_NONE && self->attach[i].max_distance <= 0.0f;

		self->inv_mass[i] = (hard || !(m > 0.0f)) ? 0.0f : 1.0f / m;
	}
}

void
lrg_cloth_set_transform (LrgCloth     *self,
                         const gfloat *matrix)
{
	g_return_if_fail (LRG_IS_CLOTH (self));
	if (matrix == NULL)
		lrg_math3d_identity (self->xf);
	else
		memcpy (self->xf, matrix, sizeof (self->xf));
}

void
lrg_cloth_get_transform (LrgCloth *self,
                         gfloat   *out_matrix)
{
	g_return_if_fail (LRG_IS_CLOTH (self));
	g_return_if_fail (out_matrix != NULL);
	memcpy (out_matrix, self->xf, sizeof (self->xf));
}

void
lrg_cloth_reset (LrgCloth *self)
{
	guint i;

	g_return_if_fail (LRG_IS_CLOTH (self));

	/* Forget motion of the frame and bones: the next step starts here. */
	memcpy (self->prev_xf, self->xf, sizeof (self->xf));
	if (self->bones != NULL)
		memcpy (self->prev_bones, self->bones, 16 * sizeof (gfloat) * self->n_bones);

	for (i = 0; i < self->n_vertices; i++)
	{
		gfloat *p = self->pos + 3 * i;

		if (!attachment_target (self, i, self->xf, self->bones, p))
			lrg_math3d_transform_point (self->xf, self->rest + 3 * i, p);
		memcpy (self->prev + 3 * i, p, 3 * sizeof (gfloat));
		self->vel[3 * i] = self->vel[3 * i + 1] = self->vel[3 * i + 2] = 0.0f;
	}
	self->normals_dirty = TRUE;
}

/* ==========================================================================
 * Pins and bones
 * ========================================================================== */

gboolean
lrg_cloth_pin (LrgCloth *self,
               guint     index)
{
	return lrg_cloth_pin_at (self, index, NULL, 0.0f);
}

gboolean
lrg_cloth_pin_at (LrgCloth     *self,
                  guint         index,
                  const gfloat *local_position,
                  gfloat        max_distance)
{
	Attachment *a;

	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	if (index >= self->n_vertices || !isfinite (max_distance) || max_distance < 0.0f)
		return FALSE;
	if (local_position != NULL && !v3_finite (local_position))
		return FALSE;

	a = &self->attach[index];
	a->kind = ATTACH_LOCAL;
	a->bone = LRG_CLOTH_NO_BONE;
	memcpy (a->target, local_position != NULL ? local_position : self->rest + 3 * index,
	        3 * sizeof (gfloat));
	a->max_distance = max_distance;
	self->dirty = TRUE;
	return TRUE;
}

gboolean
lrg_cloth_attach_to_bone (LrgCloth     *self,
                          guint         index,
                          gint          bone,
                          const gfloat *bone_offset,
                          gfloat        max_distance)
{
	Attachment *a;
	gfloat      offset[3];

	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	if (index >= self->n_vertices || bone < 0 || bone >= (gint) CLOTH_MAX_BONES
	    || !isfinite (max_distance) || max_distance < 0.0f)
		return FALSE;

	if (bone_offset != NULL)
	{
		if (!v3_finite (bone_offset))
			return FALSE;
		memcpy (offset, bone_offset, sizeof (offset));
	}
	else
	{
		gfloat inv[16];

		/* Keep the vertex where it rests on the bone of the current pose. */
		if (self->bones == NULL || (guint) bone >= self->n_bones
		    || !lrg_math3d_invert (self->bones + 16 * bone, inv))
			return FALSE;
		lrg_math3d_transform_point (inv, self->rest + 3 * index, offset);
	}

	a = &self->attach[index];
	a->kind = ATTACH_BONE;
	a->bone = bone;
	memcpy (a->target, offset, sizeof (offset));
	a->max_distance = max_distance;
	self->dirty = TRUE;
	return TRUE;
}

gboolean
lrg_cloth_unpin (LrgCloth *self,
                 guint     index)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	if (index >= self->n_vertices || self->attach[index].kind == ATTACH_NONE)
		return FALSE;
	self->attach[index].kind = ATTACH_NONE;
	self->attach[index].bone = LRG_CLOTH_NO_BONE;
	self->dirty = TRUE;
	return TRUE;
}

void
lrg_cloth_unpin_all (LrgCloth *self)
{
	guint i;

	g_return_if_fail (LRG_IS_CLOTH (self));
	for (i = 0; i < self->n_vertices; i++)
	{
		self->attach[i].kind = ATTACH_NONE;
		self->attach[i].bone = LRG_CLOTH_NO_BONE;
	}
	self->dirty = TRUE;
}

gboolean
lrg_cloth_is_pinned (LrgCloth *self,
                     guint     index)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	return index < self->n_vertices && self->attach[index].kind != ATTACH_NONE;
}

gint
lrg_cloth_get_attachment_bone (LrgCloth *self,
                               guint     index)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), LRG_CLOTH_NO_BONE);
	if (index >= self->n_vertices || self->attach[index].kind != ATTACH_BONE)
		return LRG_CLOTH_NO_BONE;
	return self->attach[index].bone;
}

void
lrg_cloth_set_bone_matrices (LrgCloth     *self,
                             const gfloat *matrices,
                             guint         n_bones)
{
	g_return_if_fail (LRG_IS_CLOTH (self));

	if (matrices == NULL || n_bones == 0)
	{
		g_clear_pointer (&self->bones, g_free);
		g_clear_pointer (&self->prev_bones, g_free);
		self->n_bones = 0;
		return;
	}
	n_bones = MIN (n_bones, CLOTH_MAX_BONES);
	if (n_bones != self->n_bones || self->bones == NULL)
	{
		/* New skeleton: no history, the next step starts at this pose. */
		g_free (self->bones);
		g_free (self->prev_bones);
		self->bones = g_memdup2 (matrices, 16 * sizeof (gfloat) * n_bones);
		self->prev_bones = g_memdup2 (matrices, 16 * sizeof (gfloat) * n_bones);
		self->n_bones = n_bones;
		return;
	}
	memcpy (self->bones, matrices, 16 * sizeof (gfloat) * n_bones);
}

guint
lrg_cloth_set_bone_matrices_from_skeleton (LrgCloth    *self,
                                           LrgSkeleton *skeleton)
{
	g_autofree gfloat *m = NULL;
	guint              n;
	guint              i;

	g_return_val_if_fail (LRG_IS_CLOTH (self), 0);
	g_return_val_if_fail (LRG_IS_SKELETON (skeleton), 0);

	n = MIN (lrg_skeleton_get_bone_count (skeleton), CLOTH_MAX_BONES);
	if (n == 0)
		return 0;
	m = g_new (gfloat, 16 * (gsize) n);
	for (i = 0; i < n; i++)
	{
		LrgBone           *bone = lrg_skeleton_get_bone (skeleton, (gint) i);
		const LrgBonePose *pose = bone != NULL ? lrg_bone_get_world_pose (bone) : NULL;

		if (pose != NULL)
			lrg_bone_pose_to_matrix (pose, m + 16 * i);
		else
			lrg_math3d_identity (m + 16 * i);
	}
	lrg_cloth_set_bone_matrices (self, m, n);
	return n;
}

guint
lrg_cloth_get_bone_count (LrgCloth *self)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), 0);
	return self->n_bones;
}

/* ==========================================================================
 * Colliders
 * ========================================================================== */

static Collider *
find_collider (LrgCloth *self,
               guint     id,
               guint    *out_index)
{
	guint i;

	for (i = 0; i < self->colliders->len; i++)
	{
		Collider *c = &g_array_index (self->colliders, Collider, i);

		if (c->id == id)
		{
			if (out_index != NULL)
				*out_index = i;
			return c;
		}
	}
	return NULL;
}

static guint
add_collider (LrgCloth *self,
              Collider *c)
{
	c->id = self->next_collider_id++;
	c->bone = LRG_CLOTH_NO_BONE;
	lrg_math3d_identity (c->xf);
	g_array_append_val (self->colliders, *c);
	return c->id;
}

guint
lrg_cloth_add_sphere (LrgCloth     *self,
                      const gfloat *center,
                      gfloat        radius)
{
	Collider c;

	g_return_val_if_fail (LRG_IS_CLOTH (self), G_MAXUINT);
	g_return_val_if_fail (center != NULL, G_MAXUINT);
	if (!v3_finite (center) || !(radius > 0.0f) || !isfinite (radius))
		return G_MAXUINT;
	memset (&c, 0, sizeof (c));
	c.kind = LRG_CLOTH_COLLIDER_SPHERE;
	memcpy (c.a, center, sizeof (c.a));
	c.radius = radius;
	return add_collider (self, &c);
}

guint
lrg_cloth_add_capsule (LrgCloth     *self,
                       const gfloat *a,
                       const gfloat *b,
                       gfloat        radius)
{
	Collider c;

	g_return_val_if_fail (LRG_IS_CLOTH (self), G_MAXUINT);
	g_return_val_if_fail (a != NULL && b != NULL, G_MAXUINT);
	if (!v3_finite (a) || !v3_finite (b) || !(radius > 0.0f) || !isfinite (radius))
		return G_MAXUINT;
	memset (&c, 0, sizeof (c));
	c.kind = LRG_CLOTH_COLLIDER_CAPSULE;
	memcpy (c.a, a, sizeof (c.a));
	memcpy (c.b, b, sizeof (c.b));
	c.radius = radius;
	return add_collider (self, &c);
}

guint
lrg_cloth_add_plane (LrgCloth     *self,
                     const gfloat *normal,
                     gfloat        offset)
{
	Collider c;
	gfloat   len;

	g_return_val_if_fail (LRG_IS_CLOTH (self), G_MAXUINT);
	g_return_val_if_fail (normal != NULL, G_MAXUINT);
	len = v3_finite (normal) ? v3_len (normal) : 0.0f;
	if (!(len > 1.0e-12f) || !isfinite (offset))
		return G_MAXUINT;
	memset (&c, 0, sizeof (c));
	c.kind = LRG_CLOTH_COLLIDER_PLANE;
	c.a[0] = normal[0] / len;
	c.a[1] = normal[1] / len;
	c.a[2] = normal[2] / len;
	c.radius = offset;
	return add_collider (self, &c);
}

guint
lrg_cloth_add_box (LrgCloth     *self,
                   const gfloat *center,
                   const gfloat *half_extents,
                   const gfloat *rotation)
{
	Collider c;

	g_return_val_if_fail (LRG_IS_CLOTH (self), G_MAXUINT);
	g_return_val_if_fail (center != NULL && half_extents != NULL, G_MAXUINT);
	if (!v3_finite (center) || !v3_finite (half_extents) || !(half_extents[0] > 0.0f)
	    || !(half_extents[1] > 0.0f) || !(half_extents[2] > 0.0f))
		return G_MAXUINT;
	memset (&c, 0, sizeof (c));
	c.kind = LRG_CLOTH_COLLIDER_BOX;
	memcpy (c.a, center, sizeof (c.a));
	memcpy (c.half, half_extents, sizeof (c.half));
	if (rotation != NULL)
	{
		if (!(isfinite (rotation[0]) && isfinite (rotation[1]) && isfinite (rotation[2])
		      && isfinite (rotation[3]))
		    || !(rotation[0] * rotation[0] + rotation[1] * rotation[1]
		         + rotation[2] * rotation[2] + rotation[3] * rotation[3] > 1.0e-12f))
			return G_MAXUINT;
		memcpy (c.rot, rotation, sizeof (c.rot));
	}
	else
	{
		lrg_math3d_quat_identity (c.rot);
	}
	return add_collider (self, &c);
}

gboolean
lrg_cloth_set_collider_transform (LrgCloth     *self,
                                  guint         id,
                                  const gfloat *matrix)
{
	Collider *c;

	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	c = find_collider (self, id, NULL);
	if (c == NULL)
		return FALSE;
	if (matrix == NULL)
		lrg_math3d_identity (c->xf);
	else
		memcpy (c->xf, matrix, sizeof (c->xf));
	return TRUE;
}

gboolean
lrg_cloth_set_collider_bone (LrgCloth *self,
                             guint     id,
                             gint      bone)
{
	Collider *c;

	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	c = find_collider (self, id, NULL);
	if (c == NULL)
		return FALSE;
	c->bone = bone < 0 ? LRG_CLOTH_NO_BONE : bone;
	return TRUE;
}

gboolean
lrg_cloth_remove_collider (LrgCloth *self,
                           guint     id)
{
	guint index;

	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	if (find_collider (self, id, &index) == NULL)
		return FALSE;
	g_array_remove_index (self->colliders, index);
	return TRUE;
}

void
lrg_cloth_clear_colliders (LrgCloth *self)
{
	g_return_if_fail (LRG_IS_CLOTH (self));
	g_array_set_size (self->colliders, 0);
}

guint
lrg_cloth_get_collider_count (LrgCloth *self)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), 0);
	return self->colliders->len;
}

gboolean
lrg_cloth_get_collider_kind (LrgCloth             *self,
                             guint                 id,
                             LrgClothColliderKind *kind)
{
	Collider *c;

	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	c = find_collider (self, id, NULL);
	if (c == NULL)
		return FALSE;
	if (kind != NULL)
		*kind = c->kind;
	return TRUE;
}

/* World matrix of a collider: cloth transform * bone, or identity, times its own transform. */
static void
collider_world_matrix (LrgCloth       *self,
                       const Collider *c,
                       gfloat         *out)
{
	if (c->bone >= 0 && self->bones != NULL && (guint) c->bone < self->n_bones)
	{
		gfloat tmp[16];

		lrg_math3d_multiply (self->xf, self->bones + 16 * c->bone, tmp);
		lrg_math3d_multiply (tmp, c->xf, out);
	}
	else
	{
		memcpy (out, c->xf, 16 * sizeof (gfloat));
	}
}

/* Refreshes every collider's world-space cache (once per step). */
static void
update_collider_cache (LrgCloth *self)
{
	guint i;

	for (i = 0; i < self->colliders->len; i++)
	{
		Collider *c = &g_array_index (self->colliders, Collider, i);
		gfloat    m[16];
		gfloat    p[3];
		gfloat    local_axes[9];
		guint     k;

		collider_world_matrix (self, c, m);
		switch (c->kind)
		{
		case LRG_CLOTH_COLLIDER_SPHERE:
			lrg_math3d_transform_point (m, c->a, c->wa);
			c->wr = c->radius;
			break;
		case LRG_CLOTH_COLLIDER_CAPSULE:
			lrg_math3d_transform_point (m, c->a, c->wa);
			lrg_math3d_transform_point (m, c->b, c->wb);
			c->wr = c->radius;
			break;
		case LRG_CLOTH_COLLIDER_PLANE:
			lrg_math3d_transform_direction (m, c->a, c->wa);
			{
				gfloat len = v3_len (c->wa);

				if (len > 0.0f)
				{
					c->wa[0] /= len;
					c->wa[1] /= len;
					c->wa[2] /= len;
				}
			}
			p[0] = c->a[0] * c->radius;
			p[1] = c->a[1] * c->radius;
			p[2] = c->a[2] * c->radius;
			lrg_math3d_transform_point (m, p, c->wb);
			c->wr = v3_dot (c->wa, c->wb);
			break;
		case LRG_CLOTH_COLLIDER_BOX:
		default:
			lrg_math3d_transform_point (m, c->a, c->wa);
			quat_to_axes (c->rot, local_axes);
			for (k = 0; k < 3; k++)
			{
				gfloat len;

				lrg_math3d_transform_direction (m, local_axes + 3 * k, c->axes + 3 * k);
				len = v3_len (c->axes + 3 * k);
				if (len > 0.0f)
				{
					c->axes[3 * k] /= len;
					c->axes[3 * k + 1] /= len;
					c->axes[3 * k + 2] /= len;
				}
			}
			break;
		}
	}
}

/*
 * Signed distance from @p to the collider's surface and the outward
 * normal at the closest point.  Negative inside.
 */
static gfloat
collider_distance (const Collider *c,
                   const gfloat   *p,
                   gfloat         *n)
{
	gfloat d[3];
	gfloat len;

	switch (c->kind)
	{
	case LRG_CLOTH_COLLIDER_SPHERE:
		v3_sub (p, c->wa, d);
		len = v3_len (d);
		if (len < 1.0e-9f)
		{
			n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f;
			return -c->wr;
		}
		n[0] = d[0] / len; n[1] = d[1] / len; n[2] = d[2] / len;
		return len - c->wr;

	case LRG_CLOTH_COLLIDER_CAPSULE:
		{
			gfloat ab[3];
			gfloat t;
			gfloat q[3];
			gfloat ab2;

			v3_sub (c->wb, c->wa, ab);
			v3_sub (p, c->wa, d);
			ab2 = v3_dot (ab, ab);
			t = ab2 > 0.0f ? v3_dot (d, ab) / ab2 : 0.0f;
			t = CLAMP (t, 0.0f, 1.0f);
			q[0] = c->wa[0] + t * ab[0];
			q[1] = c->wa[1] + t * ab[1];
			q[2] = c->wa[2] + t * ab[2];
			v3_sub (p, q, d);
			len = v3_len (d);
			if (len < 1.0e-9f)
			{
				gfloat any[3] = { 0.0f, 1.0f, 0.0f };

				/* On the axis: push out perpendicular to it. */
				v3_cross (ab, any, n);
				if (v3_len (n) < 1.0e-9f)
				{
					any[0] = 1.0f; any[1] = 0.0f;
					v3_cross (ab, any, n);
				}
				len = v3_len (n);
				if (len > 0.0f)
				{
					n[0] /= len; n[1] /= len; n[2] /= len;
				}
				return -c->wr;
			}
			n[0] = d[0] / len; n[1] = d[1] / len; n[2] = d[2] / len;
			return len - c->wr;
		}

	case LRG_CLOTH_COLLIDER_PLANE:
		n[0] = c->wa[0]; n[1] = c->wa[1]; n[2] = c->wa[2];
		return v3_dot (c->wa, p) - c->wr;

	case LRG_CLOTH_COLLIDER_BOX:
	default:
		{
			gfloat q[3];
			gfloat outside[3];
			gboolean is_outside = FALSE;
			guint  k;

			v3_sub (p, c->wa, d);
			for (k = 0; k < 3; k++)
			{
				q[k] = v3_dot (d, c->axes + 3 * k);
				outside[k] = 0.0f;
				if (q[k] > c->half[k])
				{
					outside[k] = q[k] - c->half[k];
					is_outside = TRUE;
				}
				else if (q[k] < -c->half[k])
				{
					outside[k] = q[k] + c->half[k];
					is_outside = TRUE;
				}
			}
			if (is_outside)
			{
				len = v3_len (outside);
				for (k = 0; k < 3; k++)
					n[k] = (outside[0] * c->axes[k] + outside[1] * c->axes[3 + k]
					        + outside[2] * c->axes[6 + k]) / len;
				return len;
			}
			else
			{
				/* Inside: leave through the closest face. */
				guint  best = 0;
				gfloat best_depth = c->half[0] - fabsf (q[0]);
				gfloat sign;

				for (k = 1; k < 3; k++)
				{
					gfloat depth = c->half[k] - fabsf (q[k]);

					if (depth < best_depth)
					{
						best_depth = depth;
						best = k;
					}
				}
				sign = q[best] >= 0.0f ? 1.0f : -1.0f;
				for (k = 0; k < 3; k++)
					n[k] = sign * c->axes[3 * best + k];
				return -best_depth;
			}
		}
	}
}

/* ==========================================================================
 * Constraint construction (after Jolt's SoftBodySharedSettings)
 * ========================================================================== */

typedef struct
{
	guint32 v[2];
	guint32 edge_index;    /* face * 3 + corner */
} EdgeHelper;

static gint
edge_helper_compare (gconstpointer a,
                     gconstpointer b)
{
	const EdgeHelper *x = a;
	const EdgeHelper *y = b;

	if (x->v[0] != y->v[0])
		return x->v[0] < y->v[0] ? -1 : 1;
	if (x->v[1] != y->v[1])
		return x->v[1] < y->v[1] ? -1 : 1;
	/* Ties broken by face order so qsort's result never depends on its implementation. */
	if (x->edge_index != y->edge_index)
		return x->edge_index < y->edge_index ? -1 : 1;
	return 0;
}

static void
add_edge (LrgCloth *self,
          guint32   a,
          guint32   b,
          gfloat    compliance,
          gboolean  structural)
{
	EdgeConstraint e;
	gfloat         d[3];

	/* Only constrain edges with a movable end. */
	if (self->inv_mass[a] <= 0.0f && self->inv_mass[b] <= 0.0f)
		return;
	v3_sub (self->rest + 3 * b, self->rest + 3 * a, d);
	e.v[0] = a;
	e.v[1] = b;
	e.rest = v3_len (d);
	e.compliance = compliance;
	e.structural = structural;
	if (e.rest > 0.0f)
		g_array_append_val (self->edges, e);
}

/* Initial dihedral angle of a bend constraint (Jolt CalculateBendConstraintConstants). */
static gfloat
bend_rest_angle (const gfloat *x0,
                 const gfloat *x1,
                 const gfloat *x2,
                 const gfloat *x3)
{
	gfloat e0[3];
	gfloat e1[3];
	gfloat e2[3];
	gfloat n1[3];
	gfloat n2[3];
	gfloat c[3];
	gfloat denom;
	gfloat cosine;

	v3_sub (x1, x0, e0);
	v3_sub (x2, x0, e1);
	v3_sub (x3, x0, e2);
	v3_cross (e0, e1, n1);
	v3_cross (e2, e0, n2);
	denom = sqrtf (v3_dot (n1, n1) * v3_dot (n2, n2));
	if (denom < 1.0e-12f)
		return 0.0f;
	v3_cross (n2, n1, c);
	cosine = CLAMP (v3_dot (n1, n2) / denom, -1.0f, 1.0f);
	return (v3_dot (c, e0) >= 0.0f ? 1.0f : -1.0f) * acosf (cosine);
}

/* Binary min-heap entry for the Dijkstra search. */
typedef struct
{
	gfloat  distance;
	guint32 vertex;
} OpenEntry;

static gboolean
open_less (const OpenEntry *a,
           const OpenEntry *b)
{
	if (a->distance != b->distance)
		return a->distance < b->distance;
	return a->vertex < b->vertex;
}

static void
heap_push (GArray    *heap,
           OpenEntry  e)
{
	guint i;

	g_array_append_val (heap, e);
	i = heap->len - 1;
	while (i > 0)
	{
		guint      parent = (i - 1) / 2;
		OpenEntry *p = &g_array_index (heap, OpenEntry, parent);
		OpenEntry *c = &g_array_index (heap, OpenEntry, i);
		OpenEntry  tmp;

		if (!open_less (c, p))
			break;
		tmp = *p;
		*p = *c;
		*c = tmp;
		i = parent;
	}
}

static OpenEntry
heap_pop (GArray *heap)
{
	OpenEntry top = g_array_index (heap, OpenEntry, 0);
	guint     i = 0;

	g_array_index (heap, OpenEntry, 0) = g_array_index (heap, OpenEntry, heap->len - 1);
	g_array_set_size (heap, heap->len - 1);
	for (;;)
	{
		guint l = 2 * i + 1;
		guint r = l + 1;
		guint m = i;
		OpenEntry tmp;

		if (l < heap->len && open_less (&g_array_index (heap, OpenEntry, l), &g_array_index (heap, OpenEntry, m)))
			m = l;
		if (r < heap->len && open_less (&g_array_index (heap, OpenEntry, r), &g_array_index (heap, OpenEntry, m)))
			m = r;
		if (m == i)
			break;
		tmp = g_array_index (heap, OpenEntry, i);
		g_array_index (heap, OpenEntry, i) = g_array_index (heap, OpenEntry, m);
		g_array_index (heap, OpenEntry, m) = tmp;
		i = m;
	}
	return top;
}

/* Smallest kinematic distance of an edge's ends (Jolt Optimize's sort key). */
static gfloat
edge_key (const gfloat         *dist,
          const EdgeConstraint *e)
{
	return MIN (dist[e->v[0]], dist[e->v[1]]);
}

static gint
edge_order (gconstpointer a,
            gconstpointer b,
            gpointer      data)
{
	const gfloat         *dist = data;
	const EdgeConstraint *x = a;
	const EdgeConstraint *y = b;
	gfloat                kx = edge_key (dist, x);
	gfloat                ky = edge_key (dist, y);
	guint32               mx = MIN (x->v[0], x->v[1]);
	guint32               my = MIN (y->v[0], y->v[1]);

	if (kx != ky)
		return kx < ky ? -1 : 1;
	if (mx != my)
		return mx < my ? -1 : 1;
	mx = MAX (x->v[0], x->v[1]);
	my = MAX (y->v[0], y->v[1]);
	if (mx != my)
		return mx < my ? -1 : 1;
	return 0;
}

static gint
bend_order (gconstpointer a,
            gconstpointer b,
            gpointer      data)
{
	const gfloat         *dist = data;
	const BendConstraint *x = a;
	const BendConstraint *y = b;
	gfloat kx = MIN (MIN (dist[x->v[0]], dist[x->v[1]]), MIN (dist[x->v[2]], dist[x->v[3]]));
	gfloat ky = MIN (MIN (dist[y->v[0]], dist[y->v[1]]), MIN (dist[y->v[2]], dist[y->v[3]]));
	guint  k;

	if (kx != ky)
		return kx < ky ? -1 : 1;
	for (k = 0; k < 4; k++)
		if (x->v[k] != y->v[k])
			return x->v[k] < y->v[k] ? -1 : 1;
	return 0;
}

static gint
lra_order (gconstpointer a,
           gconstpointer b)
{
	const LraConstraint *x = a;
	const LraConstraint *y = b;

	/* Longest first, so the shortest has the last word (Jolt). */
	if (x->max_distance != y->max_distance)
		return x->max_distance > y->max_distance ? -1 : 1;
	if (x->v[1] != y->v[1])
		return x->v[1] < y->v[1] ? -1 : 1;
	return 0;
}

/*
 * Dijkstra over the edge graph from every kinematic vertex (Jolt
 * CalculateClosestKinematic).  The distances order the edges and bends
 * so corrections flow outward from the pins in one sweep (Jolt
 * Optimize), and give one long-range attachment per free vertex to its
 * closest kinematic vertex.
 */
static void
order_and_build_lras (LrgCloth *self)
{
	g_autofree guint   *adj_start = NULL;
	g_autofree guint32 *adj = NULL;
	g_autofree guint   *fill = NULL;
	g_autofree gfloat  *dist = NULL;
	g_autofree guint32 *closest = NULL;
	g_autoptr(GArray)   heap = NULL;
	guint               n = self->n_vertices;
	guint               i;
	gboolean            any_kinematic = FALSE;

	for (i = 0; i < n; i++)
	{
		if (self->inv_mass[i] <= 0.0f && self->attach[i].kind != ATTACH_NONE)
		{
			any_kinematic = TRUE;
			break;
		}
	}
	if (!any_kinematic || self->edges->len == 0)
		return;

	/* Compressed adjacency from the edge constraints. */
	adj_start = g_new0 (guint, n + 1);
	for (i = 0; i < self->edges->len; i++)
	{
		const EdgeConstraint *e = &g_array_index (self->edges, EdgeConstraint, i);

		adj_start[e->v[0] + 1]++;
		adj_start[e->v[1] + 1]++;
	}
	for (i = 0; i < n; i++)
		adj_start[i + 1] += adj_start[i];
	adj = g_new (guint32, adj_start[n]);
	fill = g_memdup2 (adj_start, sizeof (guint) * n);
	for (i = 0; i < self->edges->len; i++)
	{
		const EdgeConstraint *e = &g_array_index (self->edges, EdgeConstraint, i);

		adj[fill[e->v[0]]++] = e->v[1];
		adj[fill[e->v[1]]++] = e->v[0];
	}

	dist = g_new (gfloat, n);
	closest = g_new (guint32, n);
	heap = g_array_new (FALSE, FALSE, sizeof (OpenEntry));
	for (i = 0; i < n; i++)
	{
		dist[i] = FLT_MAX;
		closest[i] = NO_VERTEX;
		if (self->inv_mass[i] <= 0.0f && self->attach[i].kind != ATTACH_NONE)
		{
			OpenEntry e;

			dist[i] = 0.0f;
			closest[i] = i;
			e.distance = 0.0f;
			e.vertex = i;
			heap_push (heap, e);
		}
	}
	while (heap->len > 0)
	{
		OpenEntry cur = heap_pop (heap);
		guint     k;

		if (cur.distance > dist[cur.vertex])
			continue;
		for (k = adj_start[cur.vertex]; k < adj_start[cur.vertex + 1]; k++)
		{
			guint32 v = adj[k];
			gfloat  d[3];
			gfloat  nd;

			v3_sub (self->rest + 3 * v, self->rest + 3 * cur.vertex, d);
			nd = cur.distance + v3_len (d);
			if (nd < dist[v])
			{
				OpenEntry e;

				dist[v] = nd;
				closest[v] = closest[cur.vertex];
				e.distance = nd;
				e.vertex = v;
				heap_push (heap, e);
			}
		}
	}

	g_array_sort_with_data (self->edges, edge_order, dist);
	g_array_sort_with_data (self->bends, bend_order, dist);
	if (self->lra_mode == LRG_CLOTH_LRA_NONE)
		return;

	for (i = 0; i < n; i++)
	{
		LraConstraint l;
		gfloat        d[3];

		if (self->inv_mass[i] <= 0.0f || closest[i] == NO_VERTEX)
			continue;
		l.v[0] = closest[i];
		l.v[1] = i;
		if (self->lra_mode == LRG_CLOTH_LRA_EUCLIDEAN)
		{
			v3_sub (self->rest + 3 * i, self->rest + 3 * closest[i], d);
			l.max_distance = self->lra_multiplier * v3_len (d);
		}
		else
		{
			l.max_distance = self->lra_multiplier * dist[i];
		}
		if (l.max_distance > 0.0f)
			g_array_append_val (self->lras, l);
	}
	g_array_sort (self->lras, lra_order);
}

/*
 * Rebuilds edges, bends and LRAs from the faces (Jolt CreateConstraints):
 * every mesh edge becomes a distance constraint; two triangles sharing an
 * edge that form a flat quad make the opposite-vertex diagonal a shear
 * edge; each shared edge also gets a bend constraint.
 */
static void
build_constraints (LrgCloth *self)
{
	g_autoptr(GArray) helpers = NULL;
	gfloat            sq_sin_tol = sinf (CLOTH_SHEAR_TOLERANCE) * sinf (CLOTH_SHEAR_TOLERANCE);
	gfloat            sq_cos_tol = cosf (CLOTH_SHEAR_TOLERANCE) * cosf (CLOTH_SHEAR_TOLERANCE);
	guint             i;
	guint             f;

	g_array_set_size (self->edges, 0);
	g_array_set_size (self->bends, 0);
	g_array_set_size (self->lras, 0);
	refresh_inverse_masses (self);
	self->dirty = FALSE;
	if (self->n_vertices == 0)
		return;

	helpers = g_array_sized_new (FALSE, FALSE, sizeof (EdgeHelper), self->n_indices);
	for (f = 0; f < self->n_indices / 3; f++)
	{
		for (i = 0; i < 3; i++)
		{
			EdgeHelper h;
			guint32    a = self->indices[3 * f + i];
			guint32    b = self->indices[3 * f + (i + 1) % 3];

			h.v[0] = MIN (a, b);
			h.v[1] = MAX (a, b);
			h.edge_index = 3 * f + i;
			g_array_append_val (helpers, h);
		}
	}
	g_array_sort (helpers, edge_helper_compare);

	for (i = 0; i < helpers->len; i++)
	{
		const EdgeHelper *e0 = &g_array_index (helpers, EdgeHelper, i);
		gboolean          is_shear = FALSE;
		guint             j;

		for (j = i + 1; j < helpers->len; j++)
		{
			const EdgeHelper *e1 = &g_array_index (helpers, EdgeHelper, j);
			const guint32    *f0;
			const guint32    *f1;
			guint32           opp0;
			guint32           opp1;
			gfloat            a[3];
			gfloat            b[3];
			gfloat            n0[3];
			gfloat            n1[3];
			gfloat            n0n1;

			if (e0->v[0] != e1->v[0] || e0->v[1] != e1->v[1])
			{
				i = j - 1;
				break;
			}
			f0 = self->indices + 3 * (e0->edge_index / 3);
			f1 = self->indices + 3 * (e1->edge_index / 3);
			opp0 = f0[(e0->edge_index + 2) % 3];
			opp1 = f1[(e1->edge_index + 2) % 3];
			if (opp0 == opp1)
				continue;

			/* Flat enough and quad shaped: the opposite diagonal is a shear edge. */
			v3_sub (self->rest + 3 * f0[2], self->rest + 3 * f0[0], a);
			v3_sub (self->rest + 3 * f0[1], self->rest + 3 * f0[0], b);
			v3_cross (a, b, n0);
			v3_sub (self->rest + 3 * f1[2], self->rest + 3 * f1[0], a);
			v3_sub (self->rest + 3 * f1[1], self->rest + 3 * f1[0], b);
			v3_cross (a, b, n1);
			n0n1 = v3_dot (n0, n1);
			if (n0n1 > 0.0f && n0n1 * n0n1 > sq_cos_tol * v3_dot (n0, n0) * v3_dot (n1, n1))
			{
				gfloat d0[3];
				gfloat d1[3];
				gfloat dd;

				v3_sub (self->rest + 3 * opp0, self->rest + 3 * e0->v[0], d0);
				v3_sub (self->rest + 3 * opp1, self->rest + 3 * e0->v[0], d1);
				dd = v3_dot (d0, d1);
				v3_sub (self->rest + 3 * opp1, self->rest + 3 * opp0, a);
				v3_sub (self->rest + 3 * e0->v[1], self->rest + 3 * e0->v[0], b);
				/*
				 * Jolt's test plus equal diagonals: with alternating grid
				 * diagonals two triangles across a structural edge can also
				 * have perpendicular opposite directions without forming a
				 * quad; a real quad's (rectangle's) diagonals match in length.
				 */
				if (dd * dd < sq_sin_tol * v3_dot (d0, d0) * v3_dot (d1, d1)
				    && fabsf (v3_dot (a, a) - v3_dot (b, b)) < 0.25f * v3_dot (b, b))
				{
					/*
					 * The other diagonal of a quad.  Off by default: with the
					 * mesh's own diagonal it over-constrains the quad and stiff
					 * over-constrained edges oscillate on fine meshes.
					 */
					if (self->shear_edges)
						add_edge (self, opp0, opp1, self->shear_compliance, FALSE);
					is_shear = TRUE;
				}
			}

			switch (self->bend_mode)
			{
			case LRG_CLOTH_BEND_DISTANCE:
				if (!is_shear)
					add_edge (self, opp0, opp1, self->bend_compliance, FALSE);
				break;
			case LRG_CLOTH_BEND_DIHEDRAL:
				if (self->inv_mass[opp0] > 0.0f || self->inv_mass[opp1] > 0.0f
				    || self->inv_mass[e0->v[0]] > 0.0f || self->inv_mass[e0->v[1]] > 0.0f)
				{
					BendConstraint bc;

					bc.v[0] = e0->v[0];
					bc.v[1] = e0->v[1];
					bc.v[2] = opp0;
					bc.v[3] = opp1;
					bc.compliance = self->bend_compliance;
					bc.angle = bend_rest_angle (self->rest + 3 * bc.v[0], self->rest + 3 * bc.v[1],
					                            self->rest + 3 * bc.v[2], self->rest + 3 * bc.v[3]);
					g_array_append_val (self->bends, bc);
				}
				break;
			case LRG_CLOTH_BEND_NONE:
			default:
				break;
			}
		}
		add_edge (self, e0->v[0], e0->v[1],
		          is_shear ? self->shear_compliance : self->stretch_compliance, TRUE);
		if (j >= helpers->len)
			break;
	}

	order_and_build_lras (self);
}

guint
lrg_cloth_get_constraint_count (LrgCloth               *self,
                                LrgClothConstraintKind  kind)
{
	guint i;
	guint n = 0;

	g_return_val_if_fail (LRG_IS_CLOTH (self), 0);
	if (self->dirty)
		build_constraints (self);
	switch (kind)
	{
	case LRG_CLOTH_CONSTRAINT_EDGE:
		return self->edges->len;
	case LRG_CLOTH_CONSTRAINT_BEND:
		return self->bends->len;
	case LRG_CLOTH_CONSTRAINT_LRA:
		return self->lras->len;
	case LRG_CLOTH_CONSTRAINT_ATTACHMENT:
	default:
		for (i = 0; i < self->n_vertices; i++)
			if (self->attach[i].kind != ATTACH_NONE)
				n++;
		return n;
	}
}

/* ==========================================================================
 * Solver (after Jolt's SoftBodyMotionProperties)
 * ========================================================================== */

/* Aerodynamic drag: force along each triangle's normal, ½ ρ C A (v_rel·n)|v_rel·n|. */
static void
accumulate_drag (LrgCloth *self)
{
	guint t;

	memset (self->force, 0, 3 * sizeof (gfloat) * self->n_vertices);
	for (t = 0; t < self->n_indices; t += 3)
	{
		guint32 a = self->indices[t];
		guint32 b = self->indices[t + 1];
		guint32 c = self->indices[t + 2];
		gfloat  e1[3];
		gfloat  e2[3];
		gfloat  n[3];
		gfloat  vrel[3];
		gfloat  len;
		gfloat  area;
		gfloat  vn;
		gfloat  f;
		guint   k;

		v3_sub (self->pos + 3 * b, self->pos + 3 * a, e1);
		v3_sub (self->pos + 3 * c, self->pos + 3 * a, e2);
		v3_cross (e1, e2, n);
		len = v3_len (n);
		if (len < 1.0e-12f)
			continue;
		area = 0.5f * len;
		for (k = 0; k < 3; k++)
		{
			n[k] /= len;
			vrel[k] = self->wind[k]
			          - (self->vel[3 * a + k] + self->vel[3 * b + k] + self->vel[3 * c + k]) / 3.0f;
		}
		vn = v3_dot (vrel, n);
		f = 0.5f * CLOTH_AIR_DENSITY * self->drag * area * vn * fabsf (vn) / 3.0f;
		for (k = 0; k < 3; k++)
		{
			self->force[3 * a + k] += f * n[k];
			self->force[3 * b + k] += f * n[k];
			self->force[3 * c + k] += f * n[k];
		}
	}
}

/* Jolt IntegratePositions plus the aerodynamic force and a speed cap. */
static void
integrate (LrgCloth     *self,
           gfloat        h,
           gfloat        factor)
{
	gfloat damp = MAX (0.0f, 1.0f - self->damping * h);
	gfloat max2 = self->max_speed * self->max_speed;
	guint  i;
	guint  k;

	for (i = 0; i < self->n_vertices; i++)
	{
		gfloat *p = self->pos + 3 * i;
		gfloat *v = self->vel + 3 * i;
		gfloat  w = self->inv_mass[i];

		memcpy (self->prev + 3 * i, p, 3 * sizeof (gfloat));
		if (w > 0.0f)
		{
			gfloat s2;

			for (k = 0; k < 3; k++)
				v[k] = (v[k] + (self->gravity[k] + self->force[3 * i + k] * w) * h) * damp;
			s2 = v3_dot (v, v);
			if (s2 > max2)
			{
				gfloat s = sqrtf (max2 / s2);

				v[0] *= s; v[1] *= s; v[2] *= s;
			}
			for (k = 0; k < 3; k++)
				p[k] += v[k] * h;
		}
		else if (self->attach[i].kind != ATTACH_NONE)
		{
			/* Kinematic: move to the target interpolated across the step. */
			for (k = 0; k < 3; k++)
				p[k] = self->target_from[3 * i + k]
				       + factor * (self->target_to[3 * i + k] - self->target_from[3 * i + k]);
		}
	}
}

/* Jolt ApplyDihedralBendConstraints (exact acos instead of the approximation). */
static void
solve_bends (LrgCloth *self)
{
	guint i;

	for (i = 0; i < self->bends->len; i++)
	{
		const BendConstraint *b = &g_array_index (self->bends, BendConstraint, i);
		gfloat *x0 = self->pos + 3 * b->v[0];
		gfloat *x1 = self->pos + 3 * b->v[1];
		gfloat *x2 = self->pos + 3 * b->v[2];
		gfloat *x3 = self->pos + 3 * b->v[3];
		gfloat  w0 = self->inv_mass[b->v[0]];
		gfloat  w1 = self->inv_mass[b->v[1]];
		gfloat  w2 = self->inv_mass[b->v[2]];
		gfloat  w3 = self->inv_mass[b->v[3]];
		gfloat  e[3];
		gfloat  x1x2[3];
		gfloat  x1x3[3];
		gfloat  t[3];
		gfloat  n1[3];
		gfloat  n2[3];
		gfloat  cr[3];
		gfloat  d0c[3];
		gfloat  d1c[3];
		gfloat  d2c[3];
		gfloat  d3c[3];
		gfloat  e_len;
		gfloat  n1_sq;
		gfloat  n2_sq;
		gfloat  d;
		gfloat  c;
		gfloat  denom;
		gfloat  lambda;
		guint   k;

		v3_sub (x1, x0, e);
		e_len = v3_len (e);
		if (e_len < 1.0e-6f)
			continue;
		v3_sub (x2, x1, x1x2);
		v3_sub (x3, x1, x1x3);
		v3_sub (x2, x0, t);
		v3_cross (t, x1x2, n1);
		v3_sub (x3, x0, t);
		v3_cross (x1x3, t, n2);
		n1_sq = v3_dot (n1, n1);
		n2_sq = v3_dot (n2, n2);
		if (n1_sq * n2_sq < 1.0e-24f)
			continue;

		v3_cross (n2, n1, cr);
		d = CLAMP (v3_dot (n1, n2) / sqrtf (n1_sq * n2_sq), -1.0f, 1.0f);
		c = (v3_dot (cr, e) >= 0.0f ? 1.0f : -1.0f) * acosf (d) - b->angle;
		if (c > G_PI)
			c -= 2.0f * (gfloat) G_PI;
		else if (c < -G_PI)
			c += 2.0f * (gfloat) G_PI;

		/* Gradients from "Strain Based Dynamics" (Müller et al.), Appendix A. */
		for (k = 0; k < 3; k++)
		{
			n1[k] /= n1_sq;
			n2[k] /= n2_sq;
		}
		for (k = 0; k < 3; k++)
		{
			d0c[k] = (v3_dot (x1x2, e) * n1[k] + v3_dot (x1x3, e) * n2[k]) / e_len;
			d2c[k] = e_len * n1[k];
			d3c[k] = e_len * n2[k];
			d1c[k] = -d0c[k] - d2c[k] - d3c[k];
		}

		/*
		 * Relative compliance (not Jolt's absolute XPBD alpha / h^2): the
		 * projection removes 1 / (1 + c) of the violation, whatever the
		 * mesh resolution and particle masses.
		 */
		denom = (w0 * v3_dot (d0c, d0c) + w1 * v3_dot (d1c, d1c) + w2 * v3_dot (d2c, d2c)
		         + w3 * v3_dot (d3c, d3c)) * (1.0f + b->compliance);
		if (denom < 1.0e-12f)
			continue;
		lambda = c / denom;

		/*
		 * Not in Jolt: cap the largest vertex move of one projection to a
		 * fraction of the hinge length.  Near-rigid bending on fine meshes
		 * otherwise feeds the XPBD velocity update large single-substep
		 * corrections that grow into an in-plane oscillation; the cap only
		 * engages far from rest and is harmless to convergence.
		 */
		{
			gfloat move = MAX (MAX (w0 * v3_len (d0c), w1 * v3_len (d1c)),
			                   MAX (w2 * v3_len (d2c), w3 * v3_len (d3c))) * fabsf (lambda);
			gfloat limit = CLOTH_BEND_MAX_MOVE * e_len;

			if (move > limit)
				lambda *= limit / move;
		}
		for (k = 0; k < 3; k++)
		{
			x0[k] -= lambda * w0 * d0c[k];
			x1[k] -= lambda * w1 * d1c[k];
			x2[k] -= lambda * w2 * d2c[k];
			x3[k] -= lambda * w3 * d3c[k];
		}
	}
}

/* Jolt ApplySkinConstraints: soft tethers clamp to a sphere round the target. */
static void
solve_attachments (LrgCloth *self,
                   gfloat    factor)
{
	guint i;
	guint k;

	for (i = 0; i < self->n_vertices; i++)
	{
		const Attachment *a = &self->attach[i];
		gfloat           *p = self->pos + 3 * i;
		gfloat            target[3];
		gfloat            d[3];
		gfloat            len2;

		if (a->kind == ATTACH_NONE)
			continue;
		for (k = 0; k < 3; k++)
			target[k] = self->target_from[3 * i + k]
			            + factor * (self->target_to[3 * i + k] - self->target_from[3 * i + k]);
		if (self->inv_mass[i] <= 0.0f)
		{
			memcpy (p, target, sizeof (target));
			continue;
		}
		v3_sub (p, target, d);
		len2 = v3_dot (d, d);
		if (len2 > a->max_distance * a->max_distance)
		{
			gfloat s = a->max_distance / sqrtf (len2);

			for (k = 0; k < 3; k++)
				p[k] = target[k] + d[k] * s;
		}
	}
}

/* Jolt ApplyEdgeConstraints. */
static void
solve_edges (LrgCloth *self)
{
	guint i;

	for (i = 0; i < self->edges->len; i++)
	{
		const EdgeConstraint *e = &g_array_index (self->edges, EdgeConstraint, i);
		gfloat *x0 = self->pos + 3 * e->v[0];
		gfloat *x1 = self->pos + 3 * e->v[1];
		gfloat  w0 = self->inv_mass[e->v[0]];
		gfloat  w1 = self->inv_mass[e->v[1]];
		gfloat  d[3];
		gfloat  len;
		gfloat  denom;
		gfloat  s;
		guint   k;

		v3_sub (x1, x0, d);
		len = v3_len (d);
		/* Relative compliance: 1 / (1 + c) of the violation per projection. */
		denom = len * (w0 + w1) * (1.0f + e->compliance);
		if (denom < 1.0e-12f)
			continue;
		s = (len - e->rest) / denom;
		for (k = 0; k < 3; k++)
		{
			x0[k] += w0 * d[k] * s;
			x1[k] -= w1 * d[k] * s;
		}
	}
}

/* Jolt ApplyLRAConstraints. */
static void
solve_lras (LrgCloth *self)
{
	guint i;

	for (i = 0; i < self->lras->len; i++)
	{
		const LraConstraint *l = &g_array_index (self->lras, LraConstraint, i);
		const gfloat        *x0 = self->pos + 3 * l->v[0];
		gfloat              *x1 = self->pos + 3 * l->v[1];
		gfloat               d[3];
		gfloat               len2;

		v3_sub (x1, x0, d);
		len2 = v3_dot (d, d);
		if (len2 > l->max_distance * l->max_distance)
		{
			gfloat s = l->max_distance / sqrtf (len2);

			x1[0] = x0[0] + d[0] * s;
			x1[1] = x0[1] + d[1] * s;
			x1[2] = x0[2] + d[2] * s;
		}
	}
}

/* Spatial hash of a cell coordinate (Müller's "Ten Minute Physics" hash). */
static guint
cell_hash (gint     x,
           gint     y,
           gint     z,
           guint    size)
{
	guint h = ((guint) x * 92837111u) ^ ((guint) y * 689287499u) ^ ((guint) z * 283923481u);

	return h % size;
}

/*
 * Self-collision: particles closer than the distance that are not close
 * at rest are pushed apart symmetrically (weighted by inverse mass).
 */
static void
solve_self_collision (LrgCloth *self)
{
	gfloat dist = self->self_collision_distance;
	gfloat inv_cell = 1.0f / dist;
	guint  n = self->n_vertices;
	guint  i;

	if (self->hash_size != 2 * n)
	{
		g_free (self->hash_start);
		g_free (self->hash_entries);
		self->hash_size = 2 * n;
		self->hash_start = g_new (guint, self->hash_size + 1);
		self->hash_entries = g_new (guint, n);
	}

	/* Counting sort of the particles into hash cells. */
	memset (self->hash_start, 0, sizeof (guint) * (self->hash_size + 1));
	for (i = 0; i < n; i++)
	{
		const gfloat *p = self->pos + 3 * i;
		guint h = cell_hash ((gint) floorf (p[0] * inv_cell), (gint) floorf (p[1] * inv_cell),
		                     (gint) floorf (p[2] * inv_cell), self->hash_size);

		self->hash_start[h]++;
	}
	for (i = 1; i <= self->hash_size; i++)
		self->hash_start[i] += self->hash_start[i - 1];
	for (i = 0; i < n; i++)
	{
		const gfloat *p = self->pos + 3 * i;
		guint h = cell_hash ((gint) floorf (p[0] * inv_cell), (gint) floorf (p[1] * inv_cell),
		                     (gint) floorf (p[2] * inv_cell), self->hash_size);

		self->hash_entries[--self->hash_start[h]] = i;
	}

	for (i = 0; i < n; i++)
	{
		gfloat *pi = self->pos + 3 * i;
		gint    cx = (gint) floorf (pi[0] * inv_cell);
		gint    cy = (gint) floorf (pi[1] * inv_cell);
		gint    cz = (gint) floorf (pi[2] * inv_cell);
		gint    dx;
		gint    dy;
		gint    dz;

		for (dx = -1; dx <= 1; dx++)
		for (dy = -1; dy <= 1; dy++)
		for (dz = -1; dz <= 1; dz++)
		{
			guint h = cell_hash (cx + dx, cy + dy, cz + dz, self->hash_size);
			guint k;

			for (k = self->hash_start[h]; k < self->hash_start[h + 1]; k++)
			{
				guint   j = self->hash_entries[k];
				gfloat *pj;
				gfloat  d[3];
				gfloat  r[3];
				gfloat  len;
				gfloat  rest_len;
				gfloat  wsum;
				gfloat  corr;
				guint   c;

				if (j <= i)
					continue;
				pj = self->pos + 3 * j;
				v3_sub (pj, pi, d);
				len = v3_len (d);
				if (len >= dist || len < 1.0e-9f)
					continue;
				v3_sub (self->rest + 3 * j, self->rest + 3 * i, r);
				rest_len = v3_len (r);
				if (rest_len < dist)
					continue;
				wsum = self->inv_mass[i] + self->inv_mass[j];
				if (wsum <= 0.0f)
					continue;
				corr = (dist - len) / (len * wsum);
				for (c = 0; c < 3; c++)
				{
					pi[c] -= self->inv_mass[i] * corr * d[c];
					pj[c] += self->inv_mass[j] * corr * d[c];
				}
			}
		}
	}
}

/*
 * Jolt ApplyCollisionConstraintsAndUpdateVelocities, static-collider
 * branch: the XPBD velocity update, then every collider the particle
 * penetrates pushes it out along the normal and applies Coulomb friction
 * to the tangential velocity and removes the approaching normal velocity.
 */
static void
collide_and_update_velocities (LrgCloth *self,
                               gfloat    h)
{
	gfloat inv_h = 1.0f / h;
	guint  i;
	guint  c;
	guint  k;

	for (i = 0; i < self->n_vertices; i++)
	{
		gfloat  *p = self->pos + 3 * i;
		gfloat  *v = self->vel + 3 * i;
		gboolean touched = FALSE;

		if (self->inv_mass[i] <= 0.0f)
		{
			for (k = 0; k < 3; k++)
				v[k] = (p[k] - self->prev[3 * i + k]) * inv_h;
			continue;
		}
		for (k = 0; k < 3; k++)
			v[k] = (p[k] - self->prev[3 * i + k]) * inv_h;

		for (c = 0; c < self->colliders->len; c++)
		{
			const Collider *col = &g_array_index (self->colliders, Collider, c);
			gfloat          n[3];
			gfloat          depth;
			gfloat          vn;
			gfloat          vt[3];
			gfloat          vt_len;

			depth = self->thickness - collider_distance (col, p, n);
			if (depth <= 0.0f)
				continue;
			touched = TRUE;
			for (k = 0; k < 3; k++)
				p[k] += n[k] * depth;

			vn = v3_dot (v, n);
			for (k = 0; k < 3; k++)
				vt[k] = v[k] - vn * n[k];
			vt_len = v3_len (vt);
			if (vt_len > 0.0f)
			{
				gfloat s = MIN (self->friction * depth / (vt_len * h), 1.0f);

				for (k = 0; k < 3; k++)
					v[k] -= vt[k] * s;
			}
			if (vn < 0.0f)
			{
				for (k = 0; k < 3; k++)
					v[k] -= vn * n[k];
			}
		}
		if (self->world_has != NULL && self->world_has[i])
		{
			const gfloat *pl = self->world_planes + 4 * i;
			gfloat        depth = pl[3] - v3_dot (pl, p);

			if (depth > 0.0f)
			{
				gfloat vn;
				gfloat vt[3];
				gfloat vt_len;

				touched = TRUE;
				for (k = 0; k < 3; k++)
					p[k] += pl[k] * depth;
				vn = v3_dot (v, pl);
				for (k = 0; k < 3; k++)
					vt[k] = v[k] - vn * pl[k];
				vt_len = v3_len (vt);
				if (vt_len > 0.0f)
				{
					gfloat s = MIN (self->friction * depth / (vt_len * h), 1.0f);

					for (k = 0; k < 3; k++)
						v[k] -= vt[k] * s;
				}
				if (vn < 0.0f)
					for (k = 0; k < 3; k++)
						v[k] -= vn * pl[k];
			}
		}
		if (touched)
			self->contact_count++;
	}
}

/*
 * Jolt's DetermineCollisionPlanes against an LrgDynamicsWorld: once per
 * step, every particle inside the AABB of a nearby body collides a
 * sphere of the cloth's thickness with the body's shape (speculatively,
 * by how far it can move this step) and keeps its deepest contact as a
 * plane n . x >= d.  The substeps enforce the planes.
 */
static void
find_world_planes (LrgCloth *self,
                   gfloat    dt)
{
	g_autoptr(GArray) bodies = NULL;
	g_autoptr(GArray) manifolds = NULL;
	g_autofree gfloat *best = NULL;
	gfloat            lo[3];
	gfloat            hi[3];
	gfloat            margin;
	gfloat            vmax2 = 0.0f;
	gfloat            radius = MAX (self->thickness, 0.001f);
	guint             i;
	guint             b;
	guint             k;

	if (self->world_planes == NULL)
	{
		self->world_planes = g_new0 (gfloat, 4 * (gsize) self->n_vertices);
		self->world_has = g_new0 (guint8, self->n_vertices);
	}
	memset (self->world_has, 0, self->n_vertices);
	if (self->probe == NULL || lrg_physics_shape_get_radius (self->probe) != radius)
	{
		g_clear_object (&self->probe);
		self->probe = lrg_physics_shape_new_sphere (radius);
	}

	/* Swept bounds of the particles. */
	for (i = 0; i < self->n_vertices; i++)
		vmax2 = MAX (vmax2, v3_dot (self->vel + 3 * i, self->vel + 3 * i));
	margin = sqrtf (vmax2) * dt + 9.81f * dt * dt + radius;
	if (!lrg_cloth_get_bounds (self, lo, hi))
		return;
	for (k = 0; k < 3; k++)
	{
		lo[k] -= margin;
		hi[k] += margin;
	}
	bodies = lrg_dynamics_world_overlap_aabb (self->world, lo, hi, self->world_mask);
	if (bodies == NULL || bodies->len == 0)
		return;

	manifolds = g_array_new (FALSE, FALSE, sizeof (LrgContactManifold));
	best = g_new (gfloat, self->n_vertices);
	for (i = 0; i < self->n_vertices; i++)
		best[i] = -G_MAXFLOAT;

	for (b = 0; b < bodies->len; b++)
	{
		guint            id = g_array_index (bodies, guint, b);
		LrgPhysicsShape *shape = lrg_dynamics_world_get_shape (self->world, id);
		gfloat           xf[16];
		gfloat           bmin[3];
		gfloat           bmax[3];

		if (shape == NULL
		    || lrg_dynamics_world_get_sensor (self->world, id)
		    || !lrg_dynamics_world_get_transform (self->world, id, xf, NULL)
		    || !lrg_dynamics_world_get_body_aabb (self->world, id, bmin, bmax, NULL))
			continue;

		for (i = 0; i < self->n_vertices; i++)
		{
			const gfloat *p = self->pos + 3 * i;
			gfloat        pxf[16];
			guint         m;

			if (self->inv_mass[i] <= 0.0f
			    || p[0] < bmin[0] - margin || p[0] > bmax[0] + margin
			    || p[1] < bmin[1] - margin || p[1] > bmax[1] + margin
			    || p[2] < bmin[2] - margin || p[2] > bmax[2] + margin)
				continue;
			lrg_math3d_identity (pxf);
			pxf[12] = p[0];
			pxf[13] = p[1];
			pxf[14] = p[2];
			g_array_set_size (manifolds, 0);
			if (lrg_collide_shapes_all (shape, xf, self->probe, pxf, margin, manifolds) == 0)
				continue;
			for (m = 0; m < manifolds->len; m++)
			{
				const LrgContactManifold *cm = &g_array_index (manifolds, LrgContactManifold, m);
				gfloat                    depth;
				guint                     q;

				if (cm->n_points == 0)
					continue;
				depth = cm->depths[0];
				for (q = 1; q < cm->n_points; q++)
					depth = MAX (depth, cm->depths[q]);
				if (depth <= best[i])
					continue;
				/* Moving the probe (B) along +normal separates it: n . x >= n . p + depth. */
				best[i] = depth;
				self->world_planes[4 * i] = cm->normal[0];
				self->world_planes[4 * i + 1] = cm->normal[1];
				self->world_planes[4 * i + 2] = cm->normal[2];
				self->world_planes[4 * i + 3] = v3_dot (cm->normal, p) + depth;
				self->world_has[i] = 1;
			}
		}
	}
}

void
lrg_cloth_set_dynamics_world (LrgCloth         *self,
                              LrgDynamicsWorld *world,
                              guint32           layer_mask)
{
	g_return_if_fail (LRG_IS_CLOTH (self));
	g_return_if_fail (world == NULL || LRG_IS_DYNAMICS_WORLD (world));
	g_set_object (&self->world, world);
	self->world_mask = layer_mask;
	if (world == NULL && self->world_has != NULL)
		memset (self->world_has, 0, self->n_vertices);
}

LrgDynamicsWorld *
lrg_cloth_get_dynamics_world (LrgCloth *self)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), NULL);
	return self->world;
}

void
lrg_cloth_step (LrgCloth *self,
                gfloat    dt)
{
	gfloat h;
	guint  s;
	guint  i;

	g_return_if_fail (LRG_IS_CLOTH (self));
	if (!(dt > 0.0f) || self->n_vertices == 0)
		return;
	dt = MIN (dt, CLOTH_MAX_DT);
	if (self->dirty)
		build_constraints (self);

	h = dt / (gfloat) self->substeps;
	self->contact_count = 0;
	update_collider_cache (self);
	if (self->world != NULL)
		find_world_planes (self, dt);

	/* Kinematic targets at the start and the end of the step. */
	for (i = 0; i < self->n_vertices; i++)
	{
		if (self->attach[i].kind == ATTACH_NONE)
			continue;
		if (!attachment_target (self, i, self->prev_xf, self->prev_bones, self->target_from + 3 * i))
			memcpy (self->target_from + 3 * i, self->pos + 3 * i, 3 * sizeof (gfloat));
		if (!attachment_target (self, i, self->xf, self->bones, self->target_to + 3 * i))
			memcpy (self->target_to + 3 * i, self->target_from + 3 * i, 3 * sizeof (gfloat));
	}

	for (s = 0; s < self->substeps; s++)
	{
		gfloat factor = (gfloat) (s + 1) / (gfloat) self->substeps;

		if (self->drag > 0.0f)
			accumulate_drag (self);
		else
			memset (self->force, 0, 3 * sizeof (gfloat) * self->n_vertices);
		integrate (self, h, factor);
		solve_bends (self);
		solve_attachments (self, factor);
		solve_edges (self);
		solve_lras (self);
		if (self->self_collision)
			solve_self_collision (self);
		collide_and_update_velocities (self, h);
	}

	/* The step consumed the motion of the frame and the bones. */
	memcpy (self->prev_xf, self->xf, sizeof (self->xf));
	if (self->bones != NULL)
		memcpy (self->prev_bones, self->bones, 16 * sizeof (gfloat) * self->n_bones);
	self->normals_dirty = TRUE;
}

/* ==========================================================================
 * Parameters
 * ========================================================================== */

void
lrg_cloth_set_gravity (LrgCloth     *self,
                       const gfloat *gravity)
{
	g_return_if_fail (LRG_IS_CLOTH (self));
	g_return_if_fail (gravity != NULL);
	if (v3_finite (gravity))
		memcpy (self->gravity, gravity, sizeof (self->gravity));
}

void
lrg_cloth_get_gravity (LrgCloth *self,
                       gfloat   *out)
{
	g_return_if_fail (LRG_IS_CLOTH (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->gravity, sizeof (self->gravity));
}

void
lrg_cloth_set_wind (LrgCloth     *self,
                    const gfloat *velocity)
{
	g_return_if_fail (LRG_IS_CLOTH (self));
	g_return_if_fail (velocity != NULL);
	if (v3_finite (velocity))
		memcpy (self->wind, velocity, sizeof (self->wind));
}

void
lrg_cloth_get_wind (LrgCloth *self,
                    gfloat   *out)
{
	g_return_if_fail (LRG_IS_CLOTH (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->wind, sizeof (self->wind));
}

void
lrg_cloth_set_substeps (LrgCloth *self,
                        guint     substeps)
{
	g_return_if_fail (LRG_IS_CLOTH (self));
	substeps = CLAMP (substeps, 1u, LRG_CLOTH_MAX_SUBSTEPS);
	if (self->substeps == substeps)
		return;
	self->substeps = substeps;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBSTEPS]);
}

guint
lrg_cloth_get_substeps (LrgCloth *self)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), 0);
	return self->substeps;
}

void
lrg_cloth_set_compliance (LrgCloth *self,
                          gfloat    stretch,
                          gfloat    shear,
                          gfloat    bend)
{
	g_return_if_fail (LRG_IS_CLOTH (self));
	set_float (self, &self->stretch_compliance, isfinite (stretch) ? MAX (stretch, 0.0f) : 0.0f,
	           PROP_STRETCH_COMPLIANCE, TRUE);
	set_float (self, &self->shear_compliance, isfinite (shear) ? MAX (shear, 0.0f) : 0.0f,
	           PROP_SHEAR_COMPLIANCE, TRUE);
	set_float (self, &self->bend_compliance, isfinite (bend) ? MAX (bend, 0.0f) : 0.0f,
	           PROP_BEND_COMPLIANCE, TRUE);
}

gfloat
lrg_cloth_get_max_stretch (LrgCloth *self)
{
	gfloat best = 1.0f;
	guint  i;

	g_return_val_if_fail (LRG_IS_CLOTH (self), 1.0f);
	if (self->dirty)
		build_constraints (self);
	for (i = 0; i < self->edges->len; i++)
	{
		const EdgeConstraint *e = &g_array_index (self->edges, EdgeConstraint, i);
		gfloat                d[3];
		gfloat                ratio;

		if (!e->structural)
			continue;
		v3_sub (self->pos + 3 * e->v[1], self->pos + 3 * e->v[0], d);
		ratio = v3_len (d) / e->rest;
		if (ratio > best)
			best = ratio;
	}
	return best;
}

/* ==========================================================================
 * State readback
 * ========================================================================== */

static void
compute_normals (LrgCloth *self)
{
	guint t;
	guint i;

	if (!self->normals_dirty || self->n_vertices == 0)
		return;
	memset (self->normals, 0, 3 * sizeof (gfloat) * self->n_vertices);
	for (t = 0; t < self->n_indices; t += 3)
	{
		guint32 a = self->indices[t];
		guint32 b = self->indices[t + 1];
		guint32 c = self->indices[t + 2];
		gfloat  e1[3];
		gfloat  e2[3];
		gfloat  n[3];
		guint   k;

		v3_sub (self->pos + 3 * b, self->pos + 3 * a, e1);
		v3_sub (self->pos + 3 * c, self->pos + 3 * a, e2);
		v3_cross (e1, e2, n);
		for (k = 0; k < 3; k++)
		{
			self->normals[3 * a + k] += n[k];
			self->normals[3 * b + k] += n[k];
			self->normals[3 * c + k] += n[k];
		}
	}
	for (i = 0; i < self->n_vertices; i++)
	{
		gfloat *n = self->normals + 3 * i;
		gfloat  len = v3_len (n);

		if (len > 1.0e-20f)
		{
			n[0] /= len; n[1] /= len; n[2] /= len;
		}
		else
		{
			n[0] = 0.0f; n[1] = 0.0f; n[2] = 1.0f;
		}
	}
	self->normals_dirty = FALSE;
}

const gfloat *
lrg_cloth_get_positions (LrgCloth *self,
                         guint    *n_floats)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), NULL);
	if (n_floats != NULL)
		*n_floats = 3 * self->n_vertices;
	return self->pos;
}

const gfloat *
lrg_cloth_get_normals (LrgCloth *self,
                       guint    *n_floats)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), NULL);
	compute_normals (self);
	if (n_floats != NULL)
		*n_floats = 3 * self->n_vertices;
	return self->normals;
}

const gfloat *
lrg_cloth_get_velocities (LrgCloth *self,
                          guint    *n_floats)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), NULL);
	if (n_floats != NULL)
		*n_floats = 3 * self->n_vertices;
	return self->vel;
}

const gfloat *
lrg_cloth_get_texcoords (LrgCloth *self,
                         guint    *n_floats)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), NULL);
	if (n_floats != NULL)
		*n_floats = 2 * self->n_vertices;
	return self->uv;
}

const guint32 *
lrg_cloth_get_indices (LrgCloth *self,
                       guint    *n_indices)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), NULL);
	if (n_indices != NULL)
		*n_indices = self->n_indices;
	return self->indices;
}

const gfloat *
lrg_cloth_get_rest_positions (LrgCloth *self,
                              guint    *n_floats)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), NULL);
	if (n_floats != NULL)
		*n_floats = 3 * self->n_vertices;
	return self->rest;
}

gboolean
lrg_cloth_get_position (LrgCloth *self,
                        guint     index,
                        gfloat   *out)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);
	if (index >= self->n_vertices)
		return FALSE;
	memcpy (out, self->pos + 3 * index, 3 * sizeof (gfloat));
	return TRUE;
}

gboolean
lrg_cloth_set_position (LrgCloth     *self,
                        guint         index,
                        const gfloat *position,
                        gboolean      keep_velocity)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	g_return_val_if_fail (position != NULL, FALSE);
	if (index >= self->n_vertices || !v3_finite (position))
		return FALSE;
	memcpy (self->pos + 3 * index, position, 3 * sizeof (gfloat));
	memcpy (self->prev + 3 * index, position, 3 * sizeof (gfloat));
	if (!keep_velocity)
		memset (self->vel + 3 * index, 0, 3 * sizeof (gfloat));
	self->normals_dirty = TRUE;
	return TRUE;
}

gboolean
lrg_cloth_apply_impulse (LrgCloth     *self,
                         guint         index,
                         const gfloat *impulse)
{
	gfloat w;

	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	g_return_val_if_fail (impulse != NULL, FALSE);
	if (index >= self->n_vertices || !v3_finite (impulse))
		return FALSE;
	if (self->dirty)
		build_constraints (self);
	w = self->inv_mass[index];
	self->vel[3 * index] += impulse[0] * w;
	self->vel[3 * index + 1] += impulse[1] * w;
	self->vel[3 * index + 2] += impulse[2] * w;
	return TRUE;
}

gboolean
lrg_cloth_get_bounds (LrgCloth *self,
                      gfloat   *out_min,
                      gfloat   *out_max)
{
	guint i;
	guint k;

	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	g_return_val_if_fail (out_min != NULL && out_max != NULL, FALSE);
	if (self->n_vertices == 0)
		return FALSE;
	memcpy (out_min, self->pos, 3 * sizeof (gfloat));
	memcpy (out_max, self->pos, 3 * sizeof (gfloat));
	for (i = 1; i < self->n_vertices; i++)
	{
		for (k = 0; k < 3; k++)
		{
			out_min[k] = MIN (out_min[k], self->pos[3 * i + k]);
			out_max[k] = MAX (out_max[k], self->pos[3 * i + k]);
		}
	}
	return TRUE;
}

gdouble
lrg_cloth_get_kinetic_energy (LrgCloth *self)
{
	gdouble e = 0.0;
	guint   i;

	g_return_val_if_fail (LRG_IS_CLOTH (self), 0.0);
	if (self->dirty)
		build_constraints (self);
	for (i = 0; i < self->n_vertices; i++)
	{
		if (self->inv_mass[i] > 0.0f)
			e += 0.5 * (gdouble) (1.0f / self->inv_mass[i]) * (gdouble) v3_dot (self->vel + 3 * i, self->vel + 3 * i);
	}
	return e;
}

gfloat
lrg_cloth_get_total_mass (LrgCloth *self)
{
	gfloat m = 0.0f;
	guint  i;

	g_return_val_if_fail (LRG_IS_CLOTH (self), 0.0f);
	if (self->dirty)
		build_constraints (self);
	for (i = 0; i < self->n_vertices; i++)
		if (self->inv_mass[i] > 0.0f)
			m += 1.0f / self->inv_mass[i];
	return m;
}

guint
lrg_cloth_get_contact_count (LrgCloth *self)
{
	g_return_val_if_fail (LRG_IS_CLOTH (self), 0);
	return self->contact_count;
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

GrlMesh *
lrg_cloth_create_mesh (LrgCloth  *self,
                       GError   **error)
{
	static const gfloat   tri_vertices[9] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
	static const guint16  tri_indices[3] = { 0, 1, 2 };
	GrlMesh              *wrapper;
	Mesh                 *handle;
	Mesh                  m;
	guint                 i;

	g_return_val_if_fail (LRG_IS_CLOTH (self), NULL);

	if (!IsWindowReady ())
	{
		g_set_error_literal (error, LRG_CLOTH_ERROR, LRG_CLOTH_ERROR_NO_GPU,
		                     "A graphics context is required to create a cloth mesh");
		return NULL;
	}
	if (self->n_vertices == 0 || self->n_vertices > CLOTH_MESH_MAX_VERTS)
	{
		g_set_error (error, LRG_CLOTH_ERROR, LRG_CLOTH_ERROR_INVALID,
		             "A cloth mesh needs 1 .. %u vertices (has %u)", CLOTH_MESH_MAX_VERTS,
		             self->n_vertices);
		return NULL;
	}

	compute_normals (self);
	memset (&m, 0, sizeof (m));
	m.vertexCount = (int) self->n_vertices;
	m.triangleCount = (int) (self->n_indices / 3);
	m.vertices = RL_MALLOC (3 * sizeof (float) * self->n_vertices);
	m.normals = RL_MALLOC (3 * sizeof (float) * self->n_vertices);
	m.texcoords = RL_MALLOC (2 * sizeof (float) * self->n_vertices);
	m.colors = RL_MALLOC (4 * sizeof (unsigned char) * self->n_vertices);
	m.indices = RL_MALLOC (sizeof (unsigned short) * self->n_indices);
	memcpy (m.vertices, self->pos, 3 * sizeof (float) * self->n_vertices);
	memcpy (m.normals, self->normals, 3 * sizeof (float) * self->n_vertices);
	memcpy (m.texcoords, self->uv, 2 * sizeof (float) * self->n_vertices);
	memset (m.colors, 255, 4 * sizeof (unsigned char) * self->n_vertices);
	for (i = 0; i < self->n_indices; i++)
		m.indices[i] = (unsigned short) self->indices[i];

	/*
	 * graylib has no constructor adopting a raylib Mesh, so build a
	 * one-triangle GrlMesh and swap its contents (the GrlMesh unloads
	 * whatever its handle holds when finalized).
	 */
	wrapper = grl_mesh_new_custom (tri_vertices, 3, NULL, tri_indices, 3);
	if (wrapper == NULL)
	{
		UnloadMesh (m);
		g_set_error_literal (error, LRG_CLOTH_ERROR, LRG_CLOTH_ERROR_NO_GPU,
		                     "Could not create a mesh");
		return NULL;
	}
	handle = (Mesh *) grl_mesh_get_handle (wrapper);
	UnloadMesh (*handle);
	*handle = m;
	UploadMesh (handle, true);
	return wrapper;
}

gboolean
lrg_cloth_update_mesh (LrgCloth *self,
                       GrlMesh  *mesh)
{
	Mesh *m;
	gint  size;

	g_return_val_if_fail (LRG_IS_CLOTH (self), FALSE);
	g_return_val_if_fail (GRL_IS_MESH (mesh), FALSE);

	m = (Mesh *) grl_mesh_get_handle (mesh);
	if (m == NULL || m->vertexCount != (int) self->n_vertices || self->n_vertices == 0)
		return FALSE;
	compute_normals (self);
	size = (gint) (3 * sizeof (float) * self->n_vertices);
	if (m->vertices != NULL)
		memcpy (m->vertices, self->pos, (gsize) size);
	if (m->normals != NULL)
		memcpy (m->normals, self->normals, (gsize) size);
	if (m->vboId != NULL && IsWindowReady ())
	{
		UpdateMeshBuffer (*m, RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION, self->pos, size, 0);
		if (m->normals != NULL)
			UpdateMeshBuffer (*m, RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL, self->normals, size, 0);
	}
	return TRUE;
}

void
lrg_cloth_draw (LrgCloth       *self,
                const GrlColor *color,
                GrlTexture     *texture)
{
	static const gfloat light[3] = { 0.37139067f, 0.74278134f, 0.55708601f };
	Mesh               *m;
	Material            mat;
	Matrix              identity;
	guint               i;
	guint8              r = 255;
	guint8              g = 255;
	guint8              b = 255;
	guint8              a = 255;

	g_return_if_fail (LRG_IS_CLOTH (self));
	if (!IsWindowReady () || self->n_vertices == 0)
		return;
	if (self->draw_mesh == NULL)
	{
		self->draw_mesh = lrg_cloth_create_mesh (self, NULL);
		if (self->draw_mesh == NULL)
			return;
	}
	if (!lrg_cloth_update_mesh (self, self->draw_mesh))
	{
		/* The geometry changed: start over with a fresh mesh. */
		g_clear_object (&self->draw_mesh);
		return;
	}
	if (color != NULL)
	{
		r = color->r;
		g = color->g;
		b = color->b;
		a = color->a;
	}

	/* A fixed two-sided key light baked into the vertex colours. */
	m = (Mesh *) grl_mesh_get_handle (self->draw_mesh);
	for (i = 0; i < self->n_vertices; i++)
	{
		gfloat shade = 0.35f + 0.65f * fabsf (v3_dot (self->normals + 3 * i, light));

		m->colors[4 * i] = (unsigned char) (shade * (gfloat) r);
		m->colors[4 * i + 1] = (unsigned char) (shade * (gfloat) g);
		m->colors[4 * i + 2] = (unsigned char) (shade * (gfloat) b);
		m->colors[4 * i + 3] = a;
	}
	UpdateMeshBuffer (*m, RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR, m->colors,
	                  (int) (4 * self->n_vertices), 0);

	memset (&identity, 0, sizeof (identity));
	identity.m0 = 1.0f;
	identity.m5 = 1.0f;
	identity.m10 = 1.0f;
	identity.m15 = 1.0f;
	mat = LoadMaterialDefault ();
	if (texture != NULL && grl_texture_is_valid (texture))
		mat.maps[MATERIAL_MAP_DIFFUSE].texture = *(Texture2D *) grl_texture_get_handle (texture);
	rlDrawRenderBatchActive ();
	rlDisableBackfaceCulling ();
	DrawMesh (*m, mat, identity);
	rlDrawRenderBatchActive ();
	rlEnableBackfaceCulling ();
	RL_FREE (mat.maps);
}

void
lrg_cloth_release_mesh (LrgCloth *self)
{
	g_return_if_fail (LRG_IS_CLOTH (self));
	g_clear_object (&self->draw_mesh);
}

void
lrg_cloth_debug_draw (LrgCloth     *self,
                      LrgDebugDraw *draw,
                      gboolean      draw_colliders)
{
	g_autoptr(GrlColor) green = grl_color_new (60, 220, 90, 255);
	g_autoptr(GrlColor) red = grl_color_new (230, 50, 40, 255);
	g_autoptr(GrlColor) yellow = grl_color_new (250, 220, 40, 255);
	g_autoptr(GrlColor) cyan = grl_color_new (60, 200, 230, 255);
	guint               i;

	g_return_if_fail (LRG_IS_CLOTH (self));
	g_return_if_fail (LRG_IS_DEBUG_DRAW (draw));

	if (self->dirty)
		build_constraints (self);
	for (i = 0; i < self->edges->len; i++)
	{
		const EdgeConstraint *e = &g_array_index (self->edges, EdgeConstraint, i);
		const gfloat         *a = self->pos + 3 * e->v[0];
		const gfloat         *b = self->pos + 3 * e->v[1];
		GrlVector3            va = { a[0], a[1], a[2] };
		GrlVector3            vb = { b[0], b[1], b[2] };
		gfloat                d[3];

		if (!e->structural)
			continue;
		v3_sub (b, a, d);
		lrg_debug_draw_line (draw, &va, &vb, v3_len (d) > 1.1f * e->rest ? red : green, 0.0f);
	}
	for (i = 0; i < self->n_vertices; i++)
	{
		if (self->attach[i].kind != ATTACH_NONE)
		{
			GrlVector3 p = { self->pos[3 * i], self->pos[3 * i + 1], self->pos[3 * i + 2] };

			lrg_debug_draw_point (draw, &p, 0.03f, yellow, 0.0f);
		}
	}
	if (!draw_colliders)
		return;
	update_collider_cache (self);
	for (i = 0; i < self->colliders->len; i++)
	{
		const Collider *c = &g_array_index (self->colliders, Collider, i);
		GrlVector3      a = { c->wa[0], c->wa[1], c->wa[2] };
		GrlVector3      b = { c->wb[0], c->wb[1], c->wb[2] };

		switch (c->kind)
		{
		case LRG_CLOTH_COLLIDER_SPHERE:
			lrg_debug_draw_sphere (draw, &a, c->wr, cyan, 0.0f);
			break;
		case LRG_CLOTH_COLLIDER_CAPSULE:
			lrg_debug_draw_capsule (draw, &a, &b, c->wr, cyan, 0.0f);
			break;
		case LRG_CLOTH_COLLIDER_PLANE:
			{
				GrlVector3 tip = { c->wb[0] + c->wa[0], c->wb[1] + c->wa[1], c->wb[2] + c->wa[2] };

				lrg_debug_draw_arrow (draw, &b, &tip, 0.0f, cyan, 0.0f);
			}
			break;
		case LRG_CLOTH_COLLIDER_BOX:
		default:
			{
				gfloat     m[16];
				GrlVector3 half = { c->half[0], c->half[1], c->half[2] };
				guint      k;

				lrg_math3d_identity (m);
				for (k = 0; k < 3; k++)
				{
					m[k] = c->axes[k];
					m[4 + k] = c->axes[3 + k];
					m[8 + k] = c->axes[6 + k];
					m[12 + k] = c->wa[k];
				}
				lrg_debug_draw_oriented_box (draw, m, &half, cyan, 0.0f);
			}
			break;
		}
	}
}
