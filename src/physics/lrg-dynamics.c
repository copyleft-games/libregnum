/* lrg-dynamics.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Portions derived from Jolt Physics (MIT), Copyright 2021 Jorrit Rouwe.
 * (The substep pipeline follows Jolt's PhysicsSystem::Update: apply
 * gravity, broadphase pairs, narrowphase with speculative contacts,
 * islands, warm-started velocity solve, integrate, linear-cast CCD,
 * position solve, sleep. Contact persistence follows
 * ContactConstraintManager.)
 *
 * The 3D rigid-body world. Bodies live in a slot array; a body id is
 * slot | (generation << 24), so the first bodies of a world keep the
 * 0.8 ids 0, 1, 2, ... and a removed body's id never comes back.
 *
 * Per substep (fixed dt):
 *   1. remember the start pose (interpolation), integrate forces and
 *      gravity into velocities, apply damping;
 *   2. refresh the broadphase AABBs of moving bodies (inflated by the
 *      speculative distance and swept by the velocity), find pairs, add
 *      plane pairs, sort them;
 *   3. narrowphase every pair into contact manifolds (analytic routines
 *      for the 0.8 primitive pairs, lrg_collide_shapes_all() otherwise),
 *      match them against last substep's manifolds to warm start, track
 *      sensor overlaps, wake sleepers touched by awake bodies;
 *   4. build islands over awake dynamic bodies (contacts + constraints);
 *   5. set up and warm start contacts and constraints, run the velocity
 *      iterations (contacts, then constraints);
 *   6. integrate positions, then sweep CCD bodies that moved far;
 *   7. run the position iterations (NGS), post_solve the constraints;
 *   8. put quiet islands to sleep, evict bodies outside the bounds,
 *      diff contacts and sensor overlaps into queued signals.
 * Signals are emitted after all substeps of lrg_dynamics_world_step().
 */

#include "config.h"

#include <math.h>
#include <stdarg.h>
#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "physics/lrg-dynamics.h"
#include "physics/lrg-dynamics-private.h"
#include "physics/lrg-island.h"
#include "../animation/lrg-bone-pose-private.h"

#define DYN_SLOT_BITS       (24u)
#define DYN_SLOT_MASK       ((1u << DYN_SLOT_BITS) - 1u)
#define DYN_GEN_MASK        (0xffu)
#define DYN_NO_SLOT         (G_MAXUINT)
#define DYN_LEGACY_SLOP     (0.005f)
#define DYN_LEGACY_BAUMGARTE (0.2f)
#define DYN_LEGACY_POSITION (0.6f)
#define DYN_CCD_FRACTION    (0.25f)

typedef enum {
	DYN_SHAPE_SPHERE = 0,
	DYN_SHAPE_PLANE = 1,
	DYN_SHAPE_SPHYL = 2,
	DYN_SHAPE_INERTIAL = 3,
	DYN_SHAPE_GENERIC = 4
} DynShape;

typedef struct
{
	LrgSolverBody        sb;              /* centre-of-mass pose, velocities, inverse mass */
	guint                generation;
	gboolean             used;            /* the slot holds a body */
	gboolean             alive;           /* not evicted */
	gboolean             frozen;
	gboolean             sleeping;
	gboolean             allow_sleep;
	gboolean             ccd;
	gboolean             sensor;
	gboolean             material_set;
	gboolean             is_plane;
	guint                quiet;
	guint                sleep_group;
	LrgDynamicsBodyKind  kind;
	DynShape             skind;
	LrgPhysicsShape     *shape;
	gfloat               shape_rot[4];    /* body-local rotation of the shape */
	gfloat               axis[3];         /* sphyl: body-local segment axis */
	gfloat               half_length;     /* sphyl */
	gfloat               radius;          /* sphere / sphyl */
	gfloat               plane_normal[3];
	gfloat               plane_offset;
	gfloat               inner_radius;
	gfloat               mass;
	gfloat               inertia_local[9];
	gfloat               force[3];
	gfloat               torque[3];
	gfloat               prev_origin[3];
	gfloat               prev_orientation[4];
	guint                layer;
	guint32              mask;
	gfloat               friction;
	gfloat               restitution;
	gfloat               linear_damping;
	gfloat               angular_damping;
	gfloat               gravity_factor;
	guint64              user_data;
	guint                proxy;
	GArray              *debug_mesh;
} DynBody;

typedef enum {
	DYN_EVENT_CONTACT_ADDED,
	DYN_EVENT_CONTACT_REMOVED,
	DYN_EVENT_SENSOR_ENTERED,
	DYN_EVENT_SENSOR_EXITED
} DynEventKind;

typedef struct
{
	DynEventKind       kind;
	guint              a;
	guint              b;
	LrgDynamicsContact contact;
} DynEvent;

struct _LrgDynamicsWorld
{
	GObject parent_instance;

	DynBody           *bodies;
	guint              n_slots;
	guint              capacity;
	GArray            *free_slots;       /* guint, LIFO */
	guint              n_bodies;         /* used slots */

	GPtrArray         *constraints;      /* LrgPhysicsConstraint, insertion order */
	guint              next_constraint_id;

	GArray            *manifolds;        /* LrgDynManifold, sorted by key */
	GArray            *old_manifolds;
	GArray            *pairs;            /* guint64 slot keys */
	GArray            *sensor_pairs;     /* guint64 (sensor slot << 32 | other slot), sorted */
	GArray            *old_sensor_pairs;
	GArray            *touching;         /* guint manifold indices reported by get_contact */
	GArray            *scratch;          /* LrgContactManifold */
	GArray            *events;           /* DynEvent */
	GArray            *evicted;          /* guint body ids */
	GArray            *planes;           /* guint plane slots */
	GHashTable        *no_collide;       /* guint64 id pair -> 1 */
	gboolean          *island_active;
	guint              island_active_cap;

	LrgBroadphase     *broadphase;
	LrgPhysicsLayers  *layers;
	LrgContactSolver  *solver;
	LrgIslandBuilder  *islands;

	gfloat             gravity[3];
	gfloat             fixed_dt;
	guint              max_substeps;
	gfloat             accumulator;
	gfloat             dropped;
	gfloat             sleep_speed2;
	guint              sleep_after;
	gfloat             restitution;
	gfloat             friction;
	gfloat             bounds_min[3];
	gfloat             bounds_max[3];
	gboolean           has_bounds;
	guint              n_islands;
	guint              next_sleep_group;
	gboolean           in_substep;
	gboolean           reported;         /* touching list belongs to this step */
	gboolean           debug_match;      /* LRG_DYNAMICS_DEBUG=match */
	gboolean           debug_time;       /* LRG_DYNAMICS_DEBUG=time */
	gint64             phase_us[8];

	LrgSolverBody      world_body;
};

G_DEFINE_TYPE (LrgDynamicsWorld, lrg_dynamics_world, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-dynamics-error-quark, lrg_dynamics_error)

G_DEFINE_BOXED_TYPE (LrgDynamicsContact, lrg_dynamics_contact, lrg_dynamics_contact_copy, lrg_dynamics_contact_free)

enum
{
	SIGNAL_CONTACT_ADDED,
	SIGNAL_CONTACT_REMOVED,
	SIGNAL_SENSOR_ENTERED,
	SIGNAL_SENSOR_EXITED,
	SIGNAL_PRE_SUBSTEP,
	SIGNAL_POST_SUBSTEP,
	N_SIGNALS
};

static guint world_signals[N_SIGNALS];

/* ==========================================================================
 * Enum and boxed types
 * ========================================================================== */

GType
lrg_dynamics_body_kind_get_type (void)
{
	static gsize guard = 0;
	static const GEnumValue values[] = {
		{ LRG_DYNAMICS_BODY_DYNAMIC, "LRG_DYNAMICS_BODY_DYNAMIC", "dynamic" },
		{ LRG_DYNAMICS_BODY_KINEMATIC, "LRG_DYNAMICS_BODY_KINEMATIC", "kinematic" },
		{ LRG_DYNAMICS_BODY_STATIC, "LRG_DYNAMICS_BODY_STATIC", "static" },
		{ 0, NULL, NULL }
	};

	if (g_once_init_enter (&guard))
		g_once_init_leave (&guard, (gsize) g_enum_register_static ("LrgDynamicsBodyKind", values));
	return (GType) guard;
}

GType
lrg_dynamics_contact_kind_get_type (void)
{
	static gsize guard = 0;
	static const GEnumValue values[] = {
		{ LRG_DYNAMICS_CONTACT_SPHERE_SPHERE, "LRG_DYNAMICS_CONTACT_SPHERE_SPHERE", "sphere-sphere" },
		{ LRG_DYNAMICS_CONTACT_SPHERE_PLANE, "LRG_DYNAMICS_CONTACT_SPHERE_PLANE", "sphere-plane" },
		{ LRG_DYNAMICS_CONTACT_SPHYL_SPHERE, "LRG_DYNAMICS_CONTACT_SPHYL_SPHERE", "sphyl-sphere" },
		{ LRG_DYNAMICS_CONTACT_SPHYL_PLANE, "LRG_DYNAMICS_CONTACT_SPHYL_PLANE", "sphyl-plane" },
		{ LRG_DYNAMICS_CONTACT_SPHYL_SPHYL, "LRG_DYNAMICS_CONTACT_SPHYL_SPHYL", "sphyl-sphyl" },
		{ LRG_DYNAMICS_CONTACT_SHAPES, "LRG_DYNAMICS_CONTACT_SHAPES", "shapes" },
		{ 0, NULL, NULL }
	};

	if (g_once_init_enter (&guard))
		g_once_init_leave (&guard, (gsize) g_enum_register_static ("LrgDynamicsContactKind", values));
	return (GType) guard;
}

GType
lrg_dynamics_debug_flags_get_type (void)
{
	static gsize guard = 0;
	static const GFlagsValue values[] = {
		{ LRG_DYNAMICS_DEBUG_NONE, "LRG_DYNAMICS_DEBUG_NONE", "none" },
		{ LRG_DYNAMICS_DEBUG_SHAPES, "LRG_DYNAMICS_DEBUG_SHAPES", "shapes" },
		{ LRG_DYNAMICS_DEBUG_AABBS, "LRG_DYNAMICS_DEBUG_AABBS", "aabbs" },
		{ LRG_DYNAMICS_DEBUG_CONTACTS, "LRG_DYNAMICS_DEBUG_CONTACTS", "contacts" },
		{ LRG_DYNAMICS_DEBUG_CONSTRAINTS, "LRG_DYNAMICS_DEBUG_CONSTRAINTS", "constraints" },
		{ LRG_DYNAMICS_DEBUG_SLEEPING, "LRG_DYNAMICS_DEBUG_SLEEPING", "sleeping" },
		{ LRG_DYNAMICS_DEBUG_CENTER_OF_MASS, "LRG_DYNAMICS_DEBUG_CENTER_OF_MASS", "center-of-mass" },
		{ LRG_DYNAMICS_DEBUG_VELOCITY, "LRG_DYNAMICS_DEBUG_VELOCITY", "velocity" },
		{ LRG_DYNAMICS_DEBUG_BROADPHASE, "LRG_DYNAMICS_DEBUG_BROADPHASE", "broadphase" },
		{ LRG_DYNAMICS_DEBUG_ALL, "LRG_DYNAMICS_DEBUG_ALL", "all" },
		{ 0, NULL, NULL }
	};

	if (g_once_init_enter (&guard))
		g_once_init_leave (&guard, (gsize) g_flags_register_static ("LrgDynamicsDebugFlags", values));
	return (GType) guard;
}

LrgDynamicsContact *
lrg_dynamics_contact_copy (const LrgDynamicsContact *contact)
{
	g_return_val_if_fail (contact != NULL, NULL);
	return (LrgDynamicsContact *) g_memdup2 (contact, sizeof (LrgDynamicsContact));
}

void
lrg_dynamics_contact_free (LrgDynamicsContact *contact)
{
	g_free (contact);
}

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static gboolean
dyn_error (GError          **error,
           LrgDynamicsError  code,
           const gchar      *format,
           ...) G_GNUC_PRINTF (3, 4);

static gboolean
dyn_error (GError          **error,
           LrgDynamicsError  code,
           const gchar      *format,
           ...)
{
	va_list args;
	gchar *message;

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);
	g_set_error_literal (error, LRG_DYNAMICS_ERROR, (gint) code, message);
	g_free (message);
	return FALSE;
}

static gboolean
finite1 (gfloat value)
{
	return isfinite ((double) value) ? TRUE : FALSE;
}

static gboolean
finite3 (const gfloat *value)
{
	return finite1 (value[0]) && finite1 (value[1]) && finite1 (value[2]);
}

static gfloat
dot3 (const gfloat *a,
      const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static gfloat
len3 (const gfloat *a)
{
	return sqrtf (dot3 (a, a));
}

static void
cross3 (const gfloat *a,
        const gfloat *b,
        gfloat       *out)
{
	gfloat x;
	gfloat y;
	gfloat z;

	x = a[1] * b[2] - a[2] * b[1];
	y = a[2] * b[0] - a[0] * b[2];
	z = a[0] * b[1] - a[1] * b[0];
	out[0] = x;
	out[1] = y;
	out[2] = z;
}

static void
copy3 (gfloat       *dst,
       const gfloat *src)
{
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
}

static void
copy4 (gfloat       *dst,
       const gfloat *src)
{
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
	dst[3] = src[3];
}

static gfloat
clampf (gfloat value,
        gfloat low,
        gfloat high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

static void
unit_or_fallback (const gfloat *delta,
                  gfloat       *normal)
{
	gfloat dist;

	dist = len3 (delta);
	if (dist > 1.0e-6f)
	{
		normal[0] = delta[0] / dist;
		normal[1] = delta[1] / dist;
		normal[2] = delta[2] / dist;
		return;
	}
	normal[0] = 1.0f;
	normal[1] = 0.0f;
	normal[2] = 0.0f;
}

/* Normalise a caller quaternion; NULL is identity. */
static gboolean
read_orientation (const gfloat  *in,
                  gfloat        *out,
                  GError       **error)
{
	gfloat length;

	out[0] = 0.0f;
	out[1] = 0.0f;
	out[2] = 0.0f;
	out[3] = 1.0f;
	if (in == NULL)
		return TRUE;
	if (!finite1 (in[0]) || !finite1 (in[1]) || !finite1 (in[2]) || !finite1 (in[3]))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics orientation is not finite");
	length = sqrtf (in[0] * in[0] + in[1] * in[1] + in[2] * in[2] + in[3] * in[3]);
	if (length <= 1.0e-8f)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics orientation has zero length");
	out[0] = in[0] / length;
	out[1] = in[1] / length;
	out[2] = in[2] / length;
	out[3] = in[3] / length;
	return TRUE;
}

static guint64
pair_key (guint a,
          guint b)
{
	if (a > b)
	{
		guint t;

		t = a;
		a = b;
		b = t;
	}
	return ((guint64) a << 32) | (guint64) b;
}

static gint
compare_u64 (gconstpointer a,
             gconstpointer b)
{
	guint64 x;
	guint64 y;

	x = *(const guint64 *) a;
	y = *(const guint64 *) b;
	return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

/* ==========================================================================
 * Body bookkeeping
 * ========================================================================== */

static guint
body_id_of (const LrgDynamicsWorld *self,
            guint                   slot)
{
	return slot | ((self->bodies[slot].generation & DYN_GEN_MASK) << DYN_SLOT_BITS);
}

/* Slot of a live (used) id, or DYN_NO_SLOT. */
static guint
slot_of (const LrgDynamicsWorld *self,
         guint                   id)
{
	guint slot;

	if (id == LRG_DYNAMICS_WORLD_BODY)
		return DYN_NO_SLOT;
	slot = id & DYN_SLOT_MASK;
	if (slot >= self->n_slots || !self->bodies[slot].used)
		return DYN_NO_SLOT;
	if (((self->bodies[slot].generation & DYN_GEN_MASK) << DYN_SLOT_BITS) != (id & ~DYN_SLOT_MASK))
		return DYN_NO_SLOT;
	return slot;
}

static DynBody *
body_known (LrgDynamicsWorld  *self,
            guint              id,
            GError           **error)
{
	guint slot;

	slot = slot_of (self, id);
	if (slot == DYN_NO_SLOT)
	{
		dyn_error (error, LRG_DYNAMICS_ERROR_MISSING, "dynamics body %u was never added", id);
		return NULL;
	}
	return &self->bodies[slot];
}

/* A known body that is not a plane (the 0.8 "sphere_known"). */
static DynBody *
solid_known (LrgDynamicsWorld  *self,
             guint              id,
             GError           **error)
{
	DynBody *body;

	body = body_known (self, id, error);
	if (body == NULL)
		return NULL;
	if (body->is_plane)
	{
		dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics body %u is a plane", id);
		return NULL;
	}
	return body;
}

/* A known, not evicted, non-plane body. */
static DynBody *
alive_known (LrgDynamicsWorld  *self,
             guint              id,
             GError           **error)
{
	DynBody *body;

	body = solid_known (self, id, error);
	if (body == NULL)
		return NULL;
	if (!body->alive)
	{
		dyn_error (error, LRG_DYNAMICS_ERROR_MISSING, "dynamics body %u was evicted", id);
		return NULL;
	}
	return body;
}

static guint
slot_index (const LrgDynamicsWorld *self,
            const DynBody          *body)
{
	return (guint) (body - self->bodies);
}

static gboolean
body_dynamic (const DynBody *body)
{
	return body->kind == LRG_DYNAMICS_BODY_DYNAMIC && !body->frozen && body->alive;
}

/* Awake: participates in the solve this substep (or moves it). */
static gboolean
body_active (const DynBody *body)
{
	if (!body->used || !body->alive || body->frozen)
		return FALSE;
	if (body->kind == LRG_DYNAMICS_BODY_DYNAMIC)
		return !body->sleeping;
	if (body->kind == LRG_DYNAMICS_BODY_KINEMATIC)
		return dot3 (body->sb.linear_velocity, body->sb.linear_velocity) > 0.0f ||
		       dot3 (body->sb.angular_velocity, body->sb.angular_velocity) > 0.0f;
	return FALSE;
}

/* Origin (the caller's position) from the centre-of-mass pose. */
static void
body_origin (const DynBody *body,
             gfloat        *out)
{
	gfloat r[3];

	lrg_dyn_quat_rotate (body->sb.orientation, body->sb.com_local, r);
	out[0] = body->sb.position[0] - r[0];
	out[1] = body->sb.position[1] - r[1];
	out[2] = body->sb.position[2] - r[2];
}

static void
set_origin (DynBody      *body,
            const gfloat *origin)
{
	gfloat r[3];

	lrg_dyn_quat_rotate (body->sb.orientation, body->sb.com_local, r);
	body->sb.position[0] = origin[0] + r[0];
	body->sb.position[1] = origin[1] + r[1];
	body->sb.position[2] = origin[2] + r[2];
}

/* Column-major rigid transform from a rotation and a translation. */
static void
make_xf (const gfloat *q,
         const gfloat *t,
         gfloat       *m)
{
	gfloat r[9];

	lrg_dyn_quat_to_mat3 (q, r);
	m[0] = r[0];
	m[1] = r[1];
	m[2] = r[2];
	m[3] = 0.0f;
	m[4] = r[3];
	m[5] = r[4];
	m[6] = r[5];
	m[7] = 0.0f;
	m[8] = r[6];
	m[9] = r[7];
	m[10] = r[8];
	m[11] = 0.0f;
	m[12] = t[0];
	m[13] = t[1];
	m[14] = t[2];
	m[15] = 1.0f;
}

/* World transform of the body's shape (origin pose times shape_rot). */
static void
shape_xf (const DynBody *body,
          gfloat        *m)
{
	gfloat origin[3];
	gfloat q[4];

	body_origin (body, origin);
	lrg_dyn_quat_mul (body->sb.orientation, body->shape_rot, q);
	make_xf (q, origin, m);
}

/* Recompute the solver view's inverse mass and inertia from the state. */
static void
refresh_mass (DynBody *body)
{
	if (body->kind == LRG_DYNAMICS_BODY_DYNAMIC && !body->frozen && body->alive &&
	    body->mass > 0.0f)
	{
		body->sb.inv_mass = 1.0f / body->mass;
		if (!lrg_dyn_mat3_invert (body->inertia_local, body->sb.inv_inertia_local))
			memset (body->sb.inv_inertia_local, 0, sizeof (body->sb.inv_inertia_local));
	}
	else
	{
		body->sb.inv_mass = 0.0f;
	}
	lrg_dyn_solver_body_update_inertia (&body->sb);
}

static void
set_inertia_diagonal (DynBody *body,
                      gfloat   ixx,
                      gfloat   iyy,
                      gfloat   izz)
{
	memset (body->inertia_local, 0, sizeof (body->inertia_local));
	body->inertia_local[0] = ixx;
	body->inertia_local[4] = iyy;
	body->inertia_local[8] = izz;
	refresh_mass (body);
}

/* Tight world AABB of a body with a shape. */
static gboolean
body_bounds (const DynBody *body,
             gfloat        *mn,
             gfloat        *mx)
{
	gfloat m[16];

	if (body->shape == NULL)
		return FALSE;
	shape_xf (body, m);
	lrg_physics_shape_get_world_bounds (body->shape, m, mn, mx);
	return finite3 (mn) && finite3 (mx);
}

static LrgBroadphaseProxyFlags
proxy_flags_for (const DynBody *body)
{
	if (body->kind == LRG_DYNAMICS_BODY_STATIC)
		return LRG_BROADPHASE_PROXY_STATIC;
	if (body->kind == LRG_DYNAMICS_BODY_DYNAMIC && (body->sleeping || body->frozen))
		return LRG_BROADPHASE_PROXY_SLEEPING;
	return LRG_BROADPHASE_PROXY_NONE;
}

/* Inflated AABB the broadphase stores: speculative margin plus the
 * sweep of this substep's motion. */
static void
inflate_bounds (LrgDynamicsWorld *self,
                const DynBody    *body,
                gfloat           *mn,
                gfloat           *mx,
                gfloat            dt)
{
	const LrgContactSolverSettings *st;
	gfloat half;
	guint k;

	st = lrg_dyn_contact_solver_settings (self->solver);
	half = 0.5f * st->speculative_distance;
	for (k = 0; k < 3u; k++)
	{
		gfloat d;

		d = body->sb.linear_velocity[k] * dt;
		mn[k] -= half;
		mx[k] += half;
		if (d < 0.0f)
			mn[k] += d;
		else
			mx[k] += d;
	}
}

static void
sync_proxy (LrgDynamicsWorld *self,
            DynBody          *body,
            gfloat            dt)
{
	gfloat mn[3];
	gfloat mx[3];
	gfloat disp[3];

	if (body->is_plane || body->shape == NULL || !body->alive)
		return;
	if (!body_bounds (body, mn, mx))
		return;
	inflate_bounds (self, body, mn, mx, dt);
	if (body->proxy == LRG_BROADPHASE_NULL_PROXY)
	{
		body->proxy = lrg_broadphase_create_proxy (self->broadphase, mn, mx, body->layer, body->mask,
		                                           proxy_flags_for (body), slot_index (self, body));
		return;
	}
	disp[0] = body->sb.linear_velocity[0] * dt;
	disp[1] = body->sb.linear_velocity[1] * dt;
	disp[2] = body->sb.linear_velocity[2] * dt;
	lrg_broadphase_move_proxy (self->broadphase, body->proxy, mn, mx, disp);
}

static void
sync_flags (LrgDynamicsWorld *self,
            DynBody          *body)
{
	if (body->proxy != LRG_BROADPHASE_NULL_PROXY)
		lrg_broadphase_set_proxy_flags (self->broadphase, body->proxy, proxy_flags_for (body));
}

static void
drop_proxy (LrgDynamicsWorld *self,
            DynBody          *body)
{
	if (body->proxy != LRG_BROADPHASE_NULL_PROXY)
		lrg_broadphase_destroy_proxy (self->broadphase, body->proxy);
	body->proxy = LRG_BROADPHASE_NULL_PROXY;
}

/* Wake one body and every body that fell asleep with it. */
static void
wake_slot (LrgDynamicsWorld *self,
           guint             slot)
{
	DynBody *body;
	guint group;
	guint i;

	body = &self->bodies[slot];
	if (!body->sleeping)
	{
		body->quiet = 0;
		return;
	}
	group = body->sleep_group;
	for (i = 0; i < self->n_slots; i++)
	{
		DynBody *other;

		other = &self->bodies[i];
		if (!other->used || !other->sleeping)
			continue;
		if (other != body && (group == 0 || other->sleep_group != group))
			continue;
		other->sleeping = FALSE;
		other->quiet = 0;
		other->sleep_group = 0;
		sync_flags (self, other);
	}
}

static void
wake_body (LrgDynamicsWorld *self,
           DynBody          *body)
{
	wake_slot (self, slot_index (self, body));
}

/* Wake the bodies that share a manifold with @slot. */
static void
wake_neighbours (LrgDynamicsWorld *self,
                 guint             slot)
{
	guint i;

	for (i = 0; i < self->manifolds->len; i++)
	{
		LrgDynManifold *m;

		m = &g_array_index (self->manifolds, LrgDynManifold, i);
		if (m->a == slot && self->bodies[m->b].used)
			wake_slot (self, m->b);
		else if (m->b == slot && self->bodies[m->a].used)
			wake_slot (self, m->a);
	}
}

/* Forget every manifold and sensor pair that references @slot. */
static void
purge_slot (LrgDynamicsWorld *self,
            guint             slot,
            gboolean          queue_exits)
{
	guint i;

	guint64 last_key;

	last_key = G_MAXUINT64;
	for (i = 0; i < self->manifolds->len; i++)
	{
		LrgDynManifold *m;

		m = &g_array_index (self->manifolds, LrgDynManifold, i);
		if (m->a != slot && m->b != slot)
			continue;
		if (queue_exits && (m->flags & LRG_DYN_MANIFOLD_TOUCHING) && m->key != last_key)
		{
			DynEvent ev;

			memset (&ev, 0, sizeof (ev));
			ev.kind = DYN_EVENT_CONTACT_REMOVED;
			ev.a = body_id_of (self, (guint) (m->key >> 32));
			ev.b = body_id_of (self, (guint) (m->key & 0xffffffffu));
			g_array_append_val (self->events, ev);
			last_key = m->key;
		}
	}
	for (i = self->manifolds->len; i > 0; i--)
	{
		LrgDynManifold *m;

		m = &g_array_index (self->manifolds, LrgDynManifold, i - 1u);
		if (m->a == slot || m->b == slot)
			g_array_remove_index (self->manifolds, i - 1u);
	}
	g_array_set_size (self->touching, 0);
	for (i = self->sensor_pairs->len; i > 0; i--)
	{
		guint64 key;
		guint s;
		guint o;

		key = g_array_index (self->sensor_pairs, guint64, i - 1u);
		s = (guint) (key >> 32);
		o = (guint) (key & 0xffffffffu);
		if (s != slot && o != slot)
			continue;
		if (queue_exits)
		{
			DynEvent ev;

			memset (&ev, 0, sizeof (ev));
			ev.kind = DYN_EVENT_SENSOR_EXITED;
			ev.a = body_id_of (self, s);
			ev.b = body_id_of (self, o);
			g_array_append_val (self->events, ev);
		}
		g_array_remove_index (self->sensor_pairs, i - 1u);
	}
}

/* A new slot (reused or appended), zeroed except its generation. */
static guint
alloc_slot (LrgDynamicsWorld *self)
{
	guint slot;
	guint generation;

	if (self->free_slots->len > 0)
	{
		slot = g_array_index (self->free_slots, guint, self->free_slots->len - 1u);
		g_array_set_size (self->free_slots, self->free_slots->len - 1u);
		generation = self->bodies[slot].generation;
	}
	else
	{
		if (self->n_slots >= DYN_SLOT_MASK)
			return DYN_NO_SLOT;
		if (self->n_slots == self->capacity)
		{
			self->capacity = MAX (16u, self->capacity * 2u);
			self->bodies = g_renew (DynBody, self->bodies, self->capacity);
		}
		slot = self->n_slots++;
		generation = 0;
	}
	memset (&self->bodies[slot], 0, sizeof (DynBody));
	self->bodies[slot].generation = generation;
	return slot;
}

/* Common defaults of a new body in @slot. */
static DynBody *
init_body (LrgDynamicsWorld    *self,
           guint                slot,
           LrgDynamicsBodyKind  kind,
           DynShape             skind,
           const gfloat        *orientation)
{
	DynBody *body;

	body = &self->bodies[slot];
	body->used = TRUE;
	body->alive = TRUE;
	body->allow_sleep = TRUE;
	body->kind = kind;
	body->skind = skind;
	body->shape_rot[3] = 1.0f;
	body->sb.orientation[3] = 1.0f;
	if (orientation != NULL)
		copy4 (body->sb.orientation, orientation);
	body->sb.id = body_id_of (self, slot);
	body->mask = LRG_PHYSICS_LAYERS_ALL;
	body->gravity_factor = 1.0f;
	body->proxy = LRG_BROADPHASE_NULL_PROXY;
	self->n_bodies++;
	return body;
}

static void
finish_body (LrgDynamicsWorld *self,
             DynBody          *body)
{
	gfloat origin[3];

	refresh_mass (body);
	body_origin (body, origin);
	copy3 (body->prev_origin, origin);
	copy4 (body->prev_orientation, body->sb.orientation);
	if (body->is_plane)
	{
		guint slot;

		slot = slot_index (self, body);
		g_array_append_val (self->planes, slot);
	}
	else if (body->shape != NULL)
	{
		sync_proxy (self, body, 0.0f);
	}
}

static void
release_body (LrgDynamicsWorld *self,
              DynBody          *body)
{
	g_clear_object (&body->shape);
	if (body->debug_mesh != NULL)
		g_array_unref (body->debug_mesh);
	body->debug_mesh = NULL;
	drop_proxy (self, body);
}

/* ==========================================================================
 * 0.8 primitive narrowphase (sphere, caller-axis capsule, plane).
 * Normals are produced as "pushes A out" (B -> A) like the 0.8 world and
 * flipped into the A -> B manifold convention by emit_point().
 * ========================================================================== */

static void
sphyl_segment (const DynBody *body,
               gfloat        *end0,
               gfloat        *end1)
{
	gfloat axis[3];

	lrg_dyn_quat_rotate (body->sb.orientation, body->axis, axis);
	end0[0] = body->sb.position[0] - axis[0] * body->half_length;
	end0[1] = body->sb.position[1] - axis[1] * body->half_length;
	end0[2] = body->sb.position[2] - axis[2] * body->half_length;
	end1[0] = body->sb.position[0] + axis[0] * body->half_length;
	end1[1] = body->sb.position[1] + axis[1] * body->half_length;
	end1[2] = body->sb.position[2] + axis[2] * body->half_length;
}

static void
closest_on_segment (const gfloat *end0,
                    const gfloat *end1,
                    const gfloat *point,
                    gfloat       *closest)
{
	gfloat edge[3];
	gfloat offset[3];
	gfloat length2;
	gfloat t;

	edge[0] = end1[0] - end0[0];
	edge[1] = end1[1] - end0[1];
	edge[2] = end1[2] - end0[2];
	offset[0] = point[0] - end0[0];
	offset[1] = point[1] - end0[1];
	offset[2] = point[2] - end0[2];
	length2 = dot3 (edge, edge);
	if (length2 <= 1.0e-12f)
	{
		copy3 (closest, end0);
		return;
	}
	t = clampf (dot3 (offset, edge) / length2, 0.0f, 1.0f);
	closest[0] = end0[0] + edge[0] * t;
	closest[1] = end0[1] + edge[1] * t;
	closest[2] = end0[2] + edge[2] * t;
}

/* Closest points on two segments (Ericson, Real-Time Collision Detection 5.1.9). */
static void
closest_segments (const gfloat *p0,
                  const gfloat *p1,
                  const gfloat *q0,
                  const gfloat *q1,
                  gfloat       *c0,
                  gfloat       *c1)
{
	gfloat d0[3];
	gfloat d1[3];
	gfloat r[3];
	gfloat a;
	gfloat e;
	gfloat f;
	gfloat c;
	gfloat b;
	gfloat denom;
	gfloat s;
	gfloat t;

	d0[0] = p1[0] - p0[0];
	d0[1] = p1[1] - p0[1];
	d0[2] = p1[2] - p0[2];
	d1[0] = q1[0] - q0[0];
	d1[1] = q1[1] - q0[1];
	d1[2] = q1[2] - q0[2];
	r[0] = p0[0] - q0[0];
	r[1] = p0[1] - q0[1];
	r[2] = p0[2] - q0[2];
	a = dot3 (d0, d0);
	e = dot3 (d1, d1);
	f = dot3 (d1, r);
	b = dot3 (d0, d1);
	c = dot3 (d0, r);
	s = 0.0f;
	t = 0.0f;
	if (a <= 1.0e-12f && e <= 1.0e-12f)
	{
		copy3 (c0, p0);
		copy3 (c1, q0);
		return;
	}
	if (a <= 1.0e-12f)
	{
		s = 0.0f;
		t = (e > 1.0e-12f) ? clampf (f / e, 0.0f, 1.0f) : 0.0f;
	}
	else if (e <= 1.0e-12f)
	{
		t = 0.0f;
		s = clampf (-c / a, 0.0f, 1.0f);
	}
	else
	{
		denom = a * e - b * b;
		if (denom > 1.0e-12f)
			s = clampf ((b * f - c * e) / denom, 0.0f, 1.0f);
		else
			s = 0.0f;
		t = (b * s + f) / e;
		if (t < 0.0f)
		{
			t = 0.0f;
			s = clampf (-c / a, 0.0f, 1.0f);
		}
		else if (t > 1.0f)
		{
			t = 1.0f;
			s = clampf ((b - c) / a, 0.0f, 1.0f);
		}
	}
	c0[0] = p0[0] + d0[0] * s;
	c0[1] = p0[1] + d0[1] * s;
	c0[2] = p0[2] + d0[2] * s;
	c1[0] = q0[0] + d1[0] * t;
	c1[1] = q0[1] + d1[1] * t;
	c1[2] = q0[2] + d1[2] * t;
}

/* Start a manifold between slots a (A) and b (B). */
static void
manifold_begin (LrgDynamicsWorld *self,
                LrgDynManifold   *m,
                guint             a,
                guint             b,
                gint              kind)
{
	DynBody *ba;
	DynBody *bb;

	memset (m, 0, sizeof (*m));
	ba = &self->bodies[a];
	bb = &self->bodies[b];
	m->key = pair_key (a, b);
	m->a = a;
	m->b = b;
	m->body_a = &ba->sb;
	m->body_b = &bb->sb;
	m->kind = kind;
	m->sub_shape_a = LRG_PHYSICS_NO_SUB_SHAPE;
	m->sub_shape_b = LRG_PHYSICS_NO_SUB_SHAPE;
	{
		gfloat fa;
		gfloat fb;
		gfloat ea;
		gfloat eb;

		fa = ba->material_set ? ba->friction : self->friction;
		fb = bb->material_set ? bb->friction : self->friction;
		ea = ba->material_set ? ba->restitution : self->restitution;
		eb = bb->material_set ? bb->restitution : self->restitution;
		m->friction = sqrtf (fa * fb);
		m->restitution = MAX (ea, eb);
	}
}

/* Add one point: wa on A, wb on B (world), legacy normal pushes A out. */
static void
emit_point (LrgDynManifold *m,
            const gfloat   *push_a_out,
            const gfloat   *wa,
            const gfloat   *wb,
            gfloat          depth,
            guint32         feature)
{
	LrgDynContactPoint *p;
	gfloat rel[3];

	if (m->n_points >= LRG_DYN_MAX_POINTS)
		return;
	m->normal[0] = -push_a_out[0];
	m->normal[1] = -push_a_out[1];
	m->normal[2] = -push_a_out[2];
	p = &m->points[m->n_points++];
	copy3 (p->world_a, wa);
	copy3 (p->world_b, wb);
	p->depth = depth;
	p->feature = feature;
	rel[0] = wa[0] - m->body_a->position[0];
	rel[1] = wa[1] - m->body_a->position[1];
	rel[2] = wa[2] - m->body_a->position[2];
	lrg_dyn_quat_inv_rotate (m->body_a->orientation, rel, p->local_a);
	rel[0] = wb[0] - m->body_b->position[0];
	rel[1] = wb[1] - m->body_b->position[1];
	rel[2] = wb[2] - m->body_b->position[2];
	lrg_dyn_quat_inv_rotate (m->body_b->orientation, rel, p->local_b);
	/* Touching includes exact contact (depth 0): a body resting exactly on
	 * a surface reports the contact, as the 0.8 world did. */
	if (depth >= 0.0f)
		m->flags |= LRG_DYN_MANIFOLD_TOUCHING;
}

static gboolean
prim_sphere_sphere (LrgDynamicsWorld *self,
                    guint             ia,
                    guint             ib,
                    gfloat            spec,
                    LrgDynManifold   *m)
{
	DynBody *a;
	DynBody *b;
	gfloat delta[3];
	gfloat n[3];
	gfloat wa[3];
	gfloat wb[3];
	gfloat dist;
	gfloat reach;

	a = &self->bodies[ia];
	b = &self->bodies[ib];
	delta[0] = a->sb.position[0] - b->sb.position[0];
	delta[1] = a->sb.position[1] - b->sb.position[1];
	delta[2] = a->sb.position[2] - b->sb.position[2];
	dist = len3 (delta);
	reach = a->radius + b->radius;
	if (dist >= reach + spec)
		return FALSE;
	unit_or_fallback (delta, n);
	manifold_begin (self, m, ia, ib, LRG_DYNAMICS_CONTACT_SPHERE_SPHERE);
	wa[0] = a->sb.position[0] - n[0] * a->radius;
	wa[1] = a->sb.position[1] - n[1] * a->radius;
	wa[2] = a->sb.position[2] - n[2] * a->radius;
	wb[0] = b->sb.position[0] + n[0] * b->radius;
	wb[1] = b->sb.position[1] + n[1] * b->radius;
	wb[2] = b->sb.position[2] + n[2] * b->radius;
	emit_point (m, n, wa, wb, reach - dist, 1u);
	return TRUE;
}

static gboolean
prim_sphere_plane (LrgDynamicsWorld *self,
                   guint             is,
                   guint             ip,
                   gfloat            spec,
                   LrgDynManifold   *m)
{
	DynBody *s;
	DynBody *p;
	gfloat sd;
	gfloat wa[3];
	gfloat wb[3];

	s = &self->bodies[is];
	p = &self->bodies[ip];
	sd = dot3 (p->plane_normal, s->sb.position) - p->plane_offset;
	if (sd >= s->radius + spec)
		return FALSE;
	manifold_begin (self, m, is, ip, LRG_DYNAMICS_CONTACT_SPHERE_PLANE);
	wa[0] = s->sb.position[0] - p->plane_normal[0] * s->radius;
	wa[1] = s->sb.position[1] - p->plane_normal[1] * s->radius;
	wa[2] = s->sb.position[2] - p->plane_normal[2] * s->radius;
	wb[0] = s->sb.position[0] - p->plane_normal[0] * sd;
	wb[1] = s->sb.position[1] - p->plane_normal[1] * sd;
	wb[2] = s->sb.position[2] - p->plane_normal[2] * sd;
	emit_point (m, p->plane_normal, wa, wb, s->radius - sd, 1u);
	return TRUE;
}

/* A is the sphere, B the capsule. */
static gboolean
prim_sphyl_sphere (LrgDynamicsWorld *self,
                   guint             is,
                   guint             ic,
                   gfloat            spec,
                   LrgDynManifold   *m)
{
	DynBody *ball;
	DynBody *cap;
	gfloat e0[3];
	gfloat e1[3];
	gfloat closest[3];
	gfloat delta[3];
	gfloat n[3];
	gfloat wa[3];
	gfloat wb[3];
	gfloat dist;
	gfloat reach;

	ball = &self->bodies[is];
	cap = &self->bodies[ic];
	sphyl_segment (cap, e0, e1);
	closest_on_segment (e0, e1, ball->sb.position, closest);
	delta[0] = ball->sb.position[0] - closest[0];
	delta[1] = ball->sb.position[1] - closest[1];
	delta[2] = ball->sb.position[2] - closest[2];
	dist = len3 (delta);
	reach = ball->radius + cap->radius;
	if (dist >= reach + spec)
		return FALSE;
	unit_or_fallback (delta, n);
	manifold_begin (self, m, is, ic, LRG_DYNAMICS_CONTACT_SPHYL_SPHERE);
	wa[0] = ball->sb.position[0] - n[0] * ball->radius;
	wa[1] = ball->sb.position[1] - n[1] * ball->radius;
	wa[2] = ball->sb.position[2] - n[2] * ball->radius;
	wb[0] = closest[0] + n[0] * cap->radius;
	wb[1] = closest[1] + n[1] * cap->radius;
	wb[2] = closest[2] + n[2] * cap->radius;
	emit_point (m, n, wa, wb, reach - dist, 1u);
	return TRUE;
}

/* Both capsule ends against the plane (two points when lying flat). */
static gboolean
prim_sphyl_plane (LrgDynamicsWorld *self,
                  guint             ic,
                  guint             ip,
                  gfloat            spec,
                  LrgDynManifold   *m)
{
	DynBody *cap;
	DynBody *p;
	gfloat ends[2][3];
	guint k;
	gboolean any;

	cap = &self->bodies[ic];
	p = &self->bodies[ip];
	sphyl_segment (cap, ends[0], ends[1]);
	any = FALSE;
	for (k = 0; k < 2u; k++)
	{
		gfloat sd;
		gfloat wa[3];
		gfloat wb[3];

		/* A zero-length segment has one end. */
		if (k == 1u && cap->half_length <= 0.0f)
			break;
		sd = dot3 (p->plane_normal, ends[k]) - p->plane_offset;
		if (sd >= cap->radius + spec)
			continue;
		if (!any)
			manifold_begin (self, m, ic, ip, LRG_DYNAMICS_CONTACT_SPHYL_PLANE);
		any = TRUE;
		wa[0] = ends[k][0] - p->plane_normal[0] * cap->radius;
		wa[1] = ends[k][1] - p->plane_normal[1] * cap->radius;
		wa[2] = ends[k][2] - p->plane_normal[2] * cap->radius;
		wb[0] = ends[k][0] - p->plane_normal[0] * sd;
		wb[1] = ends[k][1] - p->plane_normal[1] * sd;
		wb[2] = ends[k][2] - p->plane_normal[2] * sd;
		emit_point (m, p->plane_normal, wa, wb, cap->radius - sd, k + 1u);
	}
	return any;
}

static gboolean
prim_sphyl_sphyl (LrgDynamicsWorld *self,
                  guint             ia,
                  guint             ib,
                  gfloat            spec,
                  LrgDynManifold   *m)
{
	DynBody *a;
	DynBody *b;
	gfloat a0[3];
	gfloat a1[3];
	gfloat b0[3];
	gfloat b1[3];
	gfloat ca[3];
	gfloat cb[3];
	gfloat delta[3];
	gfloat n[3];
	gfloat wa[3];
	gfloat wb[3];
	gfloat dist;
	gfloat reach;

	a = &self->bodies[ia];
	b = &self->bodies[ib];
	sphyl_segment (a, a0, a1);
	sphyl_segment (b, b0, b1);
	closest_segments (a0, a1, b0, b1, ca, cb);
	delta[0] = ca[0] - cb[0];
	delta[1] = ca[1] - cb[1];
	delta[2] = ca[2] - cb[2];
	dist = len3 (delta);
	reach = a->radius + b->radius;
	if (dist >= reach + spec)
		return FALSE;
	unit_or_fallback (delta, n);
	manifold_begin (self, m, ia, ib, LRG_DYNAMICS_CONTACT_SPHYL_SPHYL);
	wa[0] = ca[0] - n[0] * a->radius;
	wa[1] = ca[1] - n[1] * a->radius;
	wa[2] = ca[2] - n[2] * a->radius;
	wb[0] = cb[0] + n[0] * b->radius;
	wb[1] = cb[1] + n[1] * b->radius;
	wb[2] = cb[2] + n[2] * b->radius;
	emit_point (m, n, wa, wb, reach - dist, 1u);
	return TRUE;
}

/* The analytic routine for a primitive pair, if both are primitives.
 * Returns TRUE when the pair was handled (with or without a manifold);
 * *made says whether a manifold was produced. */
static gboolean
collide_primitives (LrgDynamicsWorld *self,
                    guint             i,
                    guint             j,
                    gfloat            spec,
                    LrgDynManifold   *m,
                    gboolean         *made)
{
	DynShape si;
	DynShape sj;

	si = self->bodies[i].skind;
	sj = self->bodies[j].skind;
	*made = FALSE;
	if (si == DYN_SHAPE_SPHERE && sj == DYN_SHAPE_SPHERE)
		*made = prim_sphere_sphere (self, i, j, spec, m);
	else if (si == DYN_SHAPE_SPHERE && sj == DYN_SHAPE_SPHYL)
		*made = prim_sphyl_sphere (self, i, j, spec, m);
	else if (si == DYN_SHAPE_SPHYL && sj == DYN_SHAPE_SPHERE)
		*made = prim_sphyl_sphere (self, j, i, spec, m);
	else if (si == DYN_SHAPE_SPHYL && sj == DYN_SHAPE_SPHYL)
		*made = prim_sphyl_sphyl (self, i, j, spec, m);
	else if (si == DYN_SHAPE_SPHERE && sj == DYN_SHAPE_PLANE)
		*made = prim_sphere_plane (self, i, j, spec, m);
	else if (si == DYN_SHAPE_PLANE && sj == DYN_SHAPE_SPHERE)
		*made = prim_sphere_plane (self, j, i, spec, m);
	else if (si == DYN_SHAPE_SPHYL && sj == DYN_SHAPE_PLANE)
		*made = prim_sphyl_plane (self, i, j, spec, m);
	else if (si == DYN_SHAPE_PLANE && sj == DYN_SHAPE_SPHYL)
		*made = prim_sphyl_plane (self, j, i, spec, m);
	else
		return FALSE;
	return TRUE;
}

/* ==========================================================================
 * Pair filtering and narrowphase
 * ========================================================================== */

static gboolean
no_collide_pair (LrgDynamicsWorld *self,
                 guint             i,
                 guint             j)
{
	guint64 key;

	if (g_hash_table_size (self->no_collide) == 0)
		return FALSE;
	key = pair_key (body_id_of (self, i), body_id_of (self, j));
	return g_hash_table_contains (self->no_collide, &key);
}

static gboolean
layers_allow (LrgDynamicsWorld *self,
              const DynBody    *a,
              const DynBody    *b)
{
	return lrg_physics_layers_should_collide (self->layers, a->layer, a->mask, b->layer, b->mask);
}

/* Static or immovable for the purpose of pairing. */
static gboolean
body_fixed (const DynBody *body)
{
	return body->kind == LRG_DYNAMICS_BODY_STATIC || body->frozen;
}

/* Whether the pair is narrowphased now. Pairs that fail only because
 * neither side is active keep their last manifold (see carry_ok()). */
static gboolean
pair_wanted (LrgDynamicsWorld *self,
             guint             i,
             guint             j)
{
	DynBody *a;
	DynBody *b;

	a = &self->bodies[i];
	b = &self->bodies[j];
	if (!a->used || !b->used || !a->alive || !b->alive)
		return FALSE;
	if (a->shape == NULL || b->shape == NULL)
		return FALSE;
	if (a->sensor && b->sensor)
		return FALSE;
	if (a->is_plane && b->is_plane)
		return FALSE;
	if (!body_active (a) && !body_active (b))
		return FALSE;
	if (a->sensor || b->sensor)
	{
		DynBody *s;
		DynBody *o;

		s = a->sensor ? a : b;
		o = a->sensor ? b : a;
		/* Static/kinematic sensors watch moving bodies; a dynamic sensor
		 * also sees static geometry. */
		if (body_fixed (o) && s->kind != LRG_DYNAMICS_BODY_DYNAMIC)
			return FALSE;
	}
	else if (!body_dynamic (a) && !body_dynamic (b))
	{
		return FALSE;
	}
	if (!layers_allow (self, a, b))
		return FALSE;
	if (no_collide_pair (self, i, j))
		return FALSE;
	return TRUE;
}

/* An old manifold or sensor pair survives a substep in which its pair was
 * not narrowphased when both bodies still exist and neither moves. */
static gboolean
carry_ok (LrgDynamicsWorld *self,
          guint             i,
          guint             j)
{
	DynBody *a;
	DynBody *b;

	if (i >= self->n_slots || j >= self->n_slots)
		return FALSE;
	a = &self->bodies[i];
	b = &self->bodies[j];
	if (!a->used || !b->used || !a->alive || !b->alive)
		return FALSE;
	if (body_active (a) || body_active (b))
		return FALSE;
	return a->sleeping || b->sleeping;
}

/* First index of @key in a key-sorted manifold array, or len. */
static guint
manifold_lower_bound (GArray  *array,
                      guint64  key)
{
	guint lo;
	guint hi;

	lo = 0;
	hi = array->len;
	while (lo < hi)
	{
		guint mid;

		mid = lo + (hi - lo) / 2u;
		if (g_array_index (array, LrgDynManifold, mid).key < key)
			lo = mid + 1u;
		else
			hi = mid;
	}
	return lo;
}

/* Warm-start data from last substep's manifolds of the same pair. */
static void
match_old (LrgDynamicsWorld *self,
           LrgDynManifold   *m)
{
	guint k;
	guint matched;

	matched = 0;
	for (k = manifold_lower_bound (self->old_manifolds, m->key); k < self->old_manifolds->len; k++)
	{
		const LrgDynManifold *old;

		old = &g_array_index (self->old_manifolds, LrgDynManifold, k);
		if (old->key != m->key)
			break;
		if (old->a != m->a)
			continue;
		matched = lrg_dyn_manifold_match (m, old);
		if (matched > 0)
			break;
	}
	if (G_UNLIKELY (self->debug_match) && matched < m->n_points)
		g_printerr ("dynamics: pair %u-%u matched %u/%u points n=(%.4f %.4f %.4f)\n",
		            m->a, m->b, matched, m->n_points, m->normal[0], m->normal[1], m->normal[2]);
}

static gfloat
closing_speed (const LrgDynManifold *m)
{
	gfloat best;
	guint i;
	guint deepest;
	gfloat mid[3];
	gfloat va[3];
	gfloat vb[3];
	const LrgDynContactPoint *p;

	best = -G_MAXFLOAT;
	deepest = 0;
	for (i = 0; i < m->n_points; i++)
	{
		if (m->points[i].depth > best)
		{
			best = m->points[i].depth;
			deepest = i;
		}
	}
	p = &m->points[deepest];
	mid[0] = 0.5f * (p->world_a[0] + p->world_b[0]);
	mid[1] = 0.5f * (p->world_a[1] + p->world_b[1]);
	mid[2] = 0.5f * (p->world_a[2] + p->world_b[2]);
	lrg_solver_body_get_point_velocity (m->body_a, mid, va);
	lrg_solver_body_get_point_velocity (m->body_b, mid, vb);
	return -((vb[0] - va[0]) * m->normal[0] + (vb[1] - va[1]) * m->normal[1] +
	         (vb[2] - va[2]) * m->normal[2]);
}

/* Wake a sleeping dynamic body that an active body touches. */
static void
wake_on_contact (LrgDynamicsWorld *self,
                 guint             i,
                 guint             j)
{
	DynBody *a;
	DynBody *b;

	a = &self->bodies[i];
	b = &self->bodies[j];
	if (a->sleeping && body_active (b))
		wake_slot (self, i);
	else if (b->sleeping && body_active (a))
		wake_slot (self, j);
}

static void
push_manifold (LrgDynamicsWorld *self,
               LrgDynManifold   *m)
{
	m->closing_speed = closing_speed (m);
	match_old (self, m);
	g_array_append_val (self->manifolds, *m);
}

/* B's centre-of-mass pose in A's frame. */
static void
relative_pose (const LrgSolverBody *a,
               const LrgSolverBody *b,
               gfloat              *pos,
               gfloat              *rot)
{
	gfloat d[3];
	gfloat conj[4];

	d[0] = b->position[0] - a->position[0];
	d[1] = b->position[1] - a->position[1];
	d[2] = b->position[2] - a->position[2];
	lrg_dyn_quat_inv_rotate (a->orientation, d, pos);
	conj[0] = -a->orientation[0];
	conj[1] = -a->orientation[1];
	conj[2] = -a->orientation[2];
	conj[3] = a->orientation[3];
	lrg_dyn_quat_mul (conj, b->orientation, rot);
}

/* Remember the relative pose a generic manifold was built at. */
static void
cache_stamp (LrgDynManifold *m)
{
	m->cacheable = TRUE;
	relative_pose (m->body_a, m->body_b, m->cache_rel_pos, m->cache_rel_rot);
	lrg_dyn_quat_inv_rotate (m->body_a->orientation, m->normal, m->normal_local_a);
}

/* Jolt's body pair cache: when B moved less than 1 mm and turned less
 * than 2 degrees relative to A since the pair's manifolds were built,
 * reuse them with points and depths refreshed from the stored local
 * points. Returns TRUE when the pair was served from the cache. */
static gboolean
reuse_cached (LrgDynamicsWorld *self,
              guint             i,
              guint             j)
{
	guint64 key;
	guint first;
	guint k;
	gfloat pos[3];
	gfloat rot[4];
	LrgSolverBody *a;
	LrgSolverBody *b;

	key = pair_key (i, j);
	first = manifold_lower_bound (self->old_manifolds, key);
	if (first >= self->old_manifolds->len ||
	    g_array_index (self->old_manifolds, LrgDynManifold, first).key != key)
		return FALSE;
	a = &self->bodies[i].sb;
	b = &self->bodies[j].sb;
	relative_pose (a, b, pos, rot);
	for (k = first; k < self->old_manifolds->len; k++)
	{
		const LrgDynManifold *old;
		gfloat dp[3];
		gfloat dq;

		old = &g_array_index (self->old_manifolds, LrgDynManifold, k);
		if (old->key != key)
			break;
		if (!old->cacheable || old->a != i)
			return FALSE;
		dp[0] = pos[0] - old->cache_rel_pos[0];
		dp[1] = pos[1] - old->cache_rel_pos[1];
		dp[2] = pos[2] - old->cache_rel_pos[2];
		if (dot3 (dp, dp) > 1.0e-6f)
			return FALSE;
		dq = fabsf (rot[0] * old->cache_rel_rot[0] + rot[1] * old->cache_rel_rot[1] +
		            rot[2] * old->cache_rel_rot[2] + rot[3] * old->cache_rel_rot[3]);
		if (dq < 0.99985f) /* cos (1 degree): a 2 degree turn */
			return FALSE;
	}
	for (k = first; k < self->old_manifolds->len; k++)
	{
		LrgDynManifold m;
		guint q;

		m = g_array_index (self->old_manifolds, LrgDynManifold, k);
		if (m.key != key)
			break;
		m.body_a = a;
		m.body_b = b;
		m.flags &= ~(LRG_DYN_MANIFOLD_TOUCHING | LRG_DYN_MANIFOLD_ACTIVE);
		lrg_dyn_quat_rotate (a->orientation, m.normal_local_a, m.normal);
		for (q = 0; q < m.n_points; q++)
		{
			LrgDynContactPoint *p;
			gfloat ra[3];
			gfloat rb[3];

			p = &m.points[q];
			lrg_dyn_quat_rotate (a->orientation, p->local_a, ra);
			lrg_dyn_quat_rotate (b->orientation, p->local_b, rb);
			p->world_a[0] = a->position[0] + ra[0];
			p->world_a[1] = a->position[1] + ra[1];
			p->world_a[2] = a->position[2] + ra[2];
			p->world_b[0] = b->position[0] + rb[0];
			p->world_b[1] = b->position[1] + rb[1];
			p->world_b[2] = b->position[2] + rb[2];
			p->depth = (p->world_a[0] - p->world_b[0]) * m.normal[0] +
			           (p->world_a[1] - p->world_b[1]) * m.normal[1] +
			           (p->world_a[2] - p->world_b[2]) * m.normal[2];
			if (p->depth >= 0.0f)
				m.flags |= LRG_DYN_MANIFOLD_TOUCHING;
		}
		m.closing_speed = closing_speed (&m);
		g_array_append_val (self->manifolds, m);
	}
	wake_on_contact (self, i, j);
	return TRUE;
}

/* Narrowphase one pair (slots i < j) into manifolds or a sensor overlap. */
static void
narrow_pair (LrgDynamicsWorld *self,
             guint             i,
             guint             j,
             gfloat            spec)
{
	DynBody *a;
	DynBody *b;
	LrgDynManifold m;
	gboolean made;
	gfloat xa[16];
	gfloat xb[16];
	guint n;
	guint k;

	a = &self->bodies[i];
	b = &self->bodies[j];

	/* Sensors only record the overlap. */
	if (a->sensor || b->sensor)
	{
		shape_xf (a, xa);
		shape_xf (b, xb);
		if (lrg_collide_overlap (a->shape, xa, b->shape, xb))
		{
			guint64 key;

			key = a->sensor ? (((guint64) i << 32) | (guint64) j)
			                : (((guint64) j << 32) | (guint64) i);
			g_array_append_val (self->sensor_pairs, key);
		}
		return;
	}

	if (collide_primitives (self, i, j, spec, &m, &made))
	{
		if (made)
		{
			push_manifold (self, &m);
			wake_on_contact (self, i, j);
		}
		return;
	}

	if (reuse_cached (self, i, j))
		return;
	shape_xf (a, xa);
	shape_xf (b, xb);
	g_array_set_size (self->scratch, 0);
	n = lrg_collide_shapes_all (a->shape, xa, b->shape, xb, spec, self->scratch);
	for (k = 0; k < n && k < self->scratch->len; k++)
	{
		const LrgContactManifold *cm;
		gfloat push[3];
		guint p;

		cm = &g_array_index (self->scratch, LrgContactManifold, k);
		if (cm->n_points == 0)
			continue;
		manifold_begin (self, &m, i, j, LRG_DYNAMICS_CONTACT_SHAPES);
		m.sub_shape_a = cm->sub_shape_a;
		m.sub_shape_b = cm->sub_shape_b;
		push[0] = -cm->normal[0];
		push[1] = -cm->normal[1];
		push[2] = -cm->normal[2];
		for (p = 0; p < cm->n_points && p < LRG_DYN_MAX_POINTS; p++)
			emit_point (&m, push, &cm->points_a[p * 3u], &cm->points_b[p * 3u],
			            cm->depths[p], cm->feature_ids[p] + 1u);
		cache_stamp (&m);
		push_manifold (self, &m);
	}
	if (n > 0)
		wake_on_contact (self, i, j);
}

/* World plane (unit normal, offset) of a plane body. */
static gboolean
plane_world (const DynBody *body,
             gfloat        *normal,
             gfloat        *offset)
{
	gfloat ln[3];
	gfloat loff;
	gfloat q[4];
	gfloat origin[3];

	if (body->skind == DYN_SHAPE_PLANE)
	{
		copy3 (normal, body->plane_normal);
		*offset = body->plane_offset;
		return TRUE;
	}
	if (body->shape == NULL || !lrg_physics_shape_get_plane (body->shape, ln, &loff))
		return FALSE;
	lrg_dyn_quat_mul (body->sb.orientation, body->shape_rot, q);
	lrg_dyn_quat_rotate (q, ln, normal);
	body_origin (body, origin);
	*offset = loff + dot3 (normal, origin);
	return TRUE;
}

typedef struct
{
	LrgDynamicsWorld *self;
} PairCtx;

static void
collect_pair (guint    proxy_a,
              guint    user_a,
              guint    proxy_b,
              guint    user_b,
              gpointer data)
{
	PairCtx *ctx;
	guint64 key;

	(void) proxy_a;
	(void) proxy_b;
	ctx = (PairCtx *) data;
	key = pair_key (user_a, user_b);
	g_array_append_val (ctx->self->pairs, key);
}

static void
find_pairs (LrgDynamicsWorld *self)
{
	PairCtx ctx;
	guint p;
	guint i;

	g_array_set_size (self->pairs, 0);
	ctx.self = self;
	lrg_broadphase_find_pairs (self->broadphase, collect_pair, &ctx);

	/* Planes stay out of the tree: pair them with every moving body whose
	 * bounds reach below the plane surface (plus the margin). */
	for (p = 0; p < self->planes->len; p++)
	{
		guint ps;
		DynBody *plane;

		ps = g_array_index (self->planes, guint, p);
		plane = &self->bodies[ps];
		for (i = 0; i < self->n_slots; i++)
		{
			DynBody *body;
			gfloat mn[3];
			gfloat mx[3];
			gfloat n[3];
			gfloat off;
			gfloat lowest;
			guint64 key;

			body = &self->bodies[i];
			if (!body->used || !body->alive || body->is_plane || body->shape == NULL)
				continue;
			if (!body_active (body))
				continue;
			if (!body_bounds (body, mn, mx))
				continue;
			if (!plane_world (plane, n, &off))
				continue;
			/* Support of the AABB along -n. */
			lowest = (n[0] >= 0.0f ? mn[0] : mx[0]) * n[0] +
			         (n[1] >= 0.0f ? mn[1] : mx[1]) * n[1] +
			         (n[2] >= 0.0f ? mn[2] : mx[2]) * n[2];
			if (lowest - off > lrg_dyn_contact_solver_settings (self->solver)->speculative_distance +
			    len3 (body->sb.linear_velocity) * self->fixed_dt)
				continue;
			key = pair_key (ps, i);
			g_array_append_val (self->pairs, key);
		}
	}
	g_array_sort (self->pairs, compare_u64);
}

/* Binary search in a sorted guint64 array. */
static gboolean
sorted_contains (GArray  *array,
                 guint64  key)
{
	guint lo;
	guint hi;

	lo = 0;
	hi = array->len;
	while (lo < hi)
	{
		guint mid;
		guint64 v;

		mid = lo + (hi - lo) / 2u;
		v = g_array_index (array, guint64, mid);
		if (v == key)
			return TRUE;
		if (v < key)
			lo = mid + 1u;
		else
			hi = mid;
	}
	return FALSE;
}

/* Build this substep's manifolds and sensor overlaps from the pairs. */
static void
narrowphase (LrgDynamicsWorld *self)
{
	GArray *tmp;
	GArray *done;
	GArray *carried;
	gfloat spec;
	guint k;

	spec = lrg_dyn_contact_solver_settings (self->solver)->speculative_distance;

	/* Last substep's manifolds become the warm-start source. */
	tmp = self->old_manifolds;
	self->old_manifolds = self->manifolds;
	self->manifolds = tmp;
	g_array_set_size (self->manifolds, 0);
	tmp = self->old_sensor_pairs;
	self->old_sensor_pairs = self->sensor_pairs;
	self->sensor_pairs = tmp;
	g_array_set_size (self->sensor_pairs, 0);

	done = g_array_new (FALSE, FALSE, sizeof (guint64));
	for (k = 0; k < self->pairs->len; k++)
	{
		guint64 key;
		guint i;
		guint j;

		key = g_array_index (self->pairs, guint64, k);
		if (k > 0 && key == g_array_index (self->pairs, guint64, k - 1u))
			continue;
		i = (guint) (key >> 32);
		j = (guint) (key & 0xffffffffu);
		if (!pair_wanted (self, i, j))
			continue;
		g_array_append_val (done, key);
		narrow_pair (self, i, j, spec);
	}

	/* Carry the manifolds of resting pairs that were not narrowphased
	 * (sleeping against static or sleeping), so contacts persist and
	 * no removed/added events fire while an island sleeps. */
	carried = g_array_new (FALSE, FALSE, sizeof (LrgDynManifold));
	for (k = 0; k < self->old_manifolds->len; k++)
	{
		LrgDynManifold *old;

		old = &g_array_index (self->old_manifolds, LrgDynManifold, k);
		if (sorted_contains (done, old->key))
			continue;
		if (!carry_ok (self, old->a, old->b))
			continue;
		old->body_a = &self->bodies[old->a].sb;
		old->body_b = &self->bodies[old->b].sb;
		old->flags &= ~LRG_DYN_MANIFOLD_ACTIVE;
		g_array_append_val (carried, *old);
	}
	if (carried->len > 0)
	{
		GArray *merged;
		guint x;
		guint y;

		merged = g_array_sized_new (FALSE, FALSE, sizeof (LrgDynManifold),
		                            self->manifolds->len + carried->len);
		x = 0;
		y = 0;
		while (x < self->manifolds->len || y < carried->len)
		{
			LrgDynManifold *mx;
			LrgDynManifold *my;

			mx = (x < self->manifolds->len) ? &g_array_index (self->manifolds, LrgDynManifold, x) : NULL;
			my = (y < carried->len) ? &g_array_index (carried, LrgDynManifold, y) : NULL;
			if (my == NULL || (mx != NULL && mx->key <= my->key))
			{
				g_array_append_val (merged, *mx);
				x++;
			}
			else
			{
				g_array_append_val (merged, *my);
				y++;
			}
		}
		g_array_unref (self->manifolds);
		self->manifolds = merged;
	}
	g_array_unref (carried);

	/* Same for sensor overlaps: a sleeping body inside a trigger stays in. */
	for (k = 0; k < self->old_sensor_pairs->len; k++)
	{
		guint64 skey;
		guint64 pkey;
		guint s;
		guint o;

		skey = g_array_index (self->old_sensor_pairs, guint64, k);
		s = (guint) (skey >> 32);
		o = (guint) (skey & 0xffffffffu);
		pkey = pair_key (s, o);
		if (sorted_contains (done, pkey))
			continue;
		if (s >= self->n_slots || o >= self->n_slots)
			continue;
		if (!self->bodies[s].used || !self->bodies[o].used ||
		    !self->bodies[s].alive || !self->bodies[o].alive ||
		    !self->bodies[s].sensor)
			continue;
		if (body_active (&self->bodies[s]) || body_active (&self->bodies[o]))
			continue;
		g_array_append_val (self->sensor_pairs, skey);
	}
	g_array_sort (self->sensor_pairs, compare_u64);
	g_array_unref (done);
}

/* ==========================================================================
 * Contact reporting and events
 * ========================================================================== */

static guint
deepest_point (const LrgDynManifold *m)
{
	guint i;
	guint best;

	best = 0;
	for (i = 1; i < m->n_points; i++)
		if (m->points[i].depth > m->points[best].depth)
			best = i;
	return best;
}

static void
manifold_to_contact (LrgDynamicsWorld     *self,
                     const LrgDynManifold *m,
                     LrgDynamicsContact   *out)
{
	const LrgDynContactPoint *p;
	guint i;

	memset (out, 0, sizeof (*out));
	p = &m->points[deepest_point (m)];
	out->body_a = body_id_of (self, m->a);
	out->body_b = body_id_of (self, m->b);
	out->kind = (LrgDynamicsContactKind) m->kind;
	out->point[0] = 0.5f * (p->world_a[0] + p->world_b[0]);
	out->point[1] = 0.5f * (p->world_a[1] + p->world_b[1]);
	out->point[2] = 0.5f * (p->world_a[2] + p->world_b[2]);
	copy3 (out->normal, m->normal);
	out->depth = p->depth;
	out->n_points = m->n_points;
	for (i = 0; i < m->n_points; i++)
		out->normal_impulse += m->points[i].normal_impulse;
	out->relative_speed = m->closing_speed;
}

/* Touching keys (unique, sorted) of a manifold array. */
static GArray *
touching_keys (GArray *manifolds)
{
	GArray *keys;
	guint i;

	keys = g_array_new (FALSE, FALSE, sizeof (guint64));
	for (i = 0; i < manifolds->len; i++)
	{
		LrgDynManifold *m;

		m = &g_array_index (manifolds, LrgDynManifold, i);
		if (!(m->flags & LRG_DYN_MANIFOLD_TOUCHING))
			continue;
		if (keys->len > 0 && g_array_index (keys, guint64, keys->len - 1u) == m->key)
			continue;
		g_array_append_val (keys, m->key);
	}
	return keys;
}

static void
queue_events (LrgDynamicsWorld *self)
{
	g_autoptr(GArray) before = NULL;
	g_autoptr(GArray) after = NULL;
	gboolean want_contacts;
	gboolean want_sensors;
	guint x;
	guint y;
	guint i;

	/* The touching list get_contact() reads. */
	g_array_set_size (self->touching, 0);
	for (i = 0; i < self->manifolds->len; i++)
		if (g_array_index (self->manifolds, LrgDynManifold, i).flags & LRG_DYN_MANIFOLD_TOUCHING)
			g_array_append_val (self->touching, i);
	self->reported = TRUE;

	want_contacts = g_signal_has_handler_pending (self, world_signals[SIGNAL_CONTACT_ADDED], 0, TRUE) ||
	                g_signal_has_handler_pending (self, world_signals[SIGNAL_CONTACT_REMOVED], 0, TRUE);
	want_sensors = g_signal_has_handler_pending (self, world_signals[SIGNAL_SENSOR_ENTERED], 0, TRUE) ||
	               g_signal_has_handler_pending (self, world_signals[SIGNAL_SENSOR_EXITED], 0, TRUE);

	if (want_contacts)
	{
		before = touching_keys (self->old_manifolds);
		after = touching_keys (self->manifolds);
		x = 0;
		y = 0;
		while (x < before->len || y < after->len)
		{
			guint64 kb;
			guint64 ka;
			DynEvent ev;

			kb = (x < before->len) ? g_array_index (before, guint64, x) : G_MAXUINT64;
			ka = (y < after->len) ? g_array_index (after, guint64, y) : G_MAXUINT64;
			memset (&ev, 0, sizeof (ev));
			if (kb == ka)
			{
				x++;
				y++;
				continue;
			}
			if (ka < kb)
			{
				guint idx;

				/* New touching pair: report its first touching manifold. */
				for (idx = manifold_lower_bound (self->manifolds, ka); idx < self->manifolds->len; idx++)
					if (g_array_index (self->manifolds, LrgDynManifold, idx).flags & LRG_DYN_MANIFOLD_TOUCHING)
						break;
				ev.kind = DYN_EVENT_CONTACT_ADDED;
				manifold_to_contact (self, &g_array_index (self->manifolds, LrgDynManifold, idx), &ev.contact);
				ev.a = ev.contact.body_a;
				ev.b = ev.contact.body_b;
				y++;
			}
			else
			{
				guint sa;
				guint sb;

				sa = (guint) (kb >> 32);
				sb = (guint) (kb & 0xffffffffu);
				ev.kind = DYN_EVENT_CONTACT_REMOVED;
				ev.a = body_id_of (self, sa);
				ev.b = body_id_of (self, sb);
				x++;
			}
			g_array_append_val (self->events, ev);
		}
	}

	if (want_sensors)
	{
		x = 0;
		y = 0;
		while (x < self->old_sensor_pairs->len || y < self->sensor_pairs->len)
		{
			guint64 kb;
			guint64 ka;
			DynEvent ev;

			kb = (x < self->old_sensor_pairs->len) ? g_array_index (self->old_sensor_pairs, guint64, x) : G_MAXUINT64;
			ka = (y < self->sensor_pairs->len) ? g_array_index (self->sensor_pairs, guint64, y) : G_MAXUINT64;
			if (kb == ka)
			{
				x++;
				y++;
				continue;
			}
			memset (&ev, 0, sizeof (ev));
			if (ka < kb)
			{
				ev.kind = DYN_EVENT_SENSOR_ENTERED;
				ev.a = body_id_of (self, (guint) (ka >> 32));
				ev.b = body_id_of (self, (guint) (ka & 0xffffffffu));
				y++;
			}
			else
			{
				guint s;
				guint o;

				s = (guint) (kb >> 32);
				o = (guint) (kb & 0xffffffffu);
				ev.kind = DYN_EVENT_SENSOR_EXITED;
				ev.a = body_id_of (self, s);
				ev.b = body_id_of (self, o);
				x++;
			}
			g_array_append_val (self->events, ev);
		}
	}
}

static void
emit_events (LrgDynamicsWorld *self)
{
	g_autoptr(GArray) events = NULL;
	guint i;

	if (self->events->len == 0)
		return;
	/* Handlers may step or edit the world: emit from a private copy. */
	events = self->events;
	self->events = g_array_new (FALSE, FALSE, sizeof (DynEvent));
	for (i = 0; i < events->len; i++)
	{
		DynEvent *ev;

		ev = &g_array_index (events, DynEvent, i);
		switch (ev->kind)
		{
		case DYN_EVENT_CONTACT_ADDED:
			g_signal_emit (self, world_signals[SIGNAL_CONTACT_ADDED], 0, &ev->contact);
			break;
		case DYN_EVENT_CONTACT_REMOVED:
			g_signal_emit (self, world_signals[SIGNAL_CONTACT_REMOVED], 0, ev->a, ev->b);
			break;
		case DYN_EVENT_SENSOR_ENTERED:
			g_signal_emit (self, world_signals[SIGNAL_SENSOR_ENTERED], 0, ev->a, ev->b);
			break;
		case DYN_EVENT_SENSOR_EXITED:
		default:
			g_signal_emit (self, world_signals[SIGNAL_SENSOR_EXITED], 0, ev->a, ev->b);
			break;
		}
	}
}

/* ==========================================================================
 * Constraints in the pipeline
 * ========================================================================== */

/* Solver view of a constraint end: a body slot, or the world frame. */
static LrgSolverBody *
constraint_end (LrgDynamicsWorld *self,
                guint             id,
                guint            *slot_out)
{
	guint slot;

	*slot_out = DYN_NO_SLOT;
	if (id == LRG_DYNAMICS_WORLD_BODY)
		return &self->world_body;
	slot = slot_of (self, id);
	if (slot == DYN_NO_SLOT || !self->bodies[slot].alive)
		return NULL;
	*slot_out = slot;
	return &self->bodies[slot].sb;
}

static gboolean
slot_awake_dynamic (LrgDynamicsWorld *self,
                    guint             slot)
{
	DynBody *body;

	if (slot == DYN_NO_SLOT)
		return FALSE;
	body = &self->bodies[slot];
	return body_dynamic (body) && !body->sleeping;
}

/* Resolve both ends; returns FALSE when the constraint is skipped. */
static gboolean
constraint_bodies (LrgDynamicsWorld      *self,
                   LrgPhysicsConstraint  *c,
                   LrgSolverBody        **a,
                   LrgSolverBody        **b,
                   guint                 *sa,
                   guint                 *sb)
{
	if (!lrg_physics_constraint_get_enabled (c))
		return FALSE;
	*a = constraint_end (self, lrg_physics_constraint_get_body_a (c), sa);
	*b = constraint_end (self, lrg_physics_constraint_get_body_b (c), sb);
	return *a != NULL && *b != NULL;
}

/* Fresh or re-enabled constraints reset their warm start and wake their
 * bodies; an awake end wakes a sleeping end. */
static void
constraints_prepare (LrgDynamicsWorld *self)
{
	guint i;

	for (i = 0; i < self->constraints->len; i++)
	{
		LrgPhysicsConstraint *c;
		LrgSolverBody *a;
		LrgSolverBody *b;
		guint sa;
		guint sb;

		c = g_ptr_array_index (self->constraints, i);
		if (lrg_dyn_constraint_take_dirty (c))
		{
			LrgPhysicsConstraintClass *klass;

			klass = LRG_PHYSICS_CONSTRAINT_GET_CLASS (c);
			if (klass->reset_warm_start != NULL)
				klass->reset_warm_start (c);
			if (constraint_bodies (self, c, &a, &b, &sa, &sb))
			{
				if (sa != DYN_NO_SLOT)
					wake_slot (self, sa);
				if (sb != DYN_NO_SLOT)
					wake_slot (self, sb);
			}
			continue;
		}
		if (!constraint_bodies (self, c, &a, &b, &sa, &sb))
			continue;
		if (slot_awake_dynamic (self, sa) && sb != DYN_NO_SLOT && self->bodies[sb].sleeping)
			wake_slot (self, sb);
		else if (slot_awake_dynamic (self, sb) && sa != DYN_NO_SLOT && self->bodies[sa].sleeping)
			wake_slot (self, sa);
	}
}

/* ==========================================================================
 * Islands and sleeping
 * ========================================================================== */

static void
build_islands (LrgDynamicsWorld *self)
{
	guint i;

	if (self->island_active_cap < self->n_slots)
	{
		self->island_active_cap = MAX (self->n_slots, self->island_active_cap * 2u);
		self->island_active = g_renew (gboolean, self->island_active, self->island_active_cap);
	}
	for (i = 0; i < self->n_slots; i++)
		self->island_active[i] = self->bodies[i].used && body_dynamic (&self->bodies[i]) &&
		                         !self->bodies[i].sleeping;
	lrg_island_builder_reset (self->islands, self->n_slots);
	for (i = 0; i < self->manifolds->len; i++)
	{
		LrgDynManifold *m;

		m = &g_array_index (self->manifolds, LrgDynManifold, i);
		if (self->island_active[m->a] && self->island_active[m->b])
			lrg_island_builder_link (self->islands, m->a, m->b);
	}
	for (i = 0; i < self->constraints->len; i++)
	{
		LrgPhysicsConstraint *c;
		LrgSolverBody *a;
		LrgSolverBody *b;
		guint sa;
		guint sb;

		c = g_ptr_array_index (self->constraints, i);
		if (!constraint_bodies (self, c, &a, &b, &sa, &sb))
			continue;
		if (sa != DYN_NO_SLOT && sb != DYN_NO_SLOT &&
		    self->island_active[sa] && self->island_active[sb])
			lrg_island_builder_link (self->islands, sa, sb);
	}
	self->n_islands = lrg_island_builder_finish (self->islands, self->island_active, self->n_slots);
}

static void
update_sleep (LrgDynamicsWorld *self)
{
	guint isl;
	guint i;

	/* Per-body quiet counters (the 0.8 rule: |v|^2 + |w|^2). */
	for (i = 0; i < self->n_slots; i++)
	{
		DynBody *body;
		gfloat speed2;

		body = &self->bodies[i];
		if (!body->used || !body_dynamic (body) || body->sleeping)
			continue;
		speed2 = dot3 (body->sb.linear_velocity, body->sb.linear_velocity) +
		         dot3 (body->sb.angular_velocity, body->sb.angular_velocity);
		if (speed2 < self->sleep_speed2)
		{
			if (body->quiet < G_MAXUINT)
				body->quiet++;
		}
		else
		{
			body->quiet = 0;
		}
	}

	/* An island sleeps when all its bodies have been quiet long enough. */
	for (isl = 0; isl < self->n_islands; isl++)
	{
		const guint *members;
		guint n;
		gboolean can;
		guint group;

		members = lrg_island_builder_get_island (self->islands, isl, &n);
		can = TRUE;
		for (i = 0; i < n && can; i++)
		{
			DynBody *body;

			body = &self->bodies[members[i]];
			if (!body->allow_sleep || body->quiet < self->sleep_after || body->sleeping)
				can = FALSE;
		}
		if (!can)
			continue;
		group = ++self->next_sleep_group;
		if (group == 0)
			group = ++self->next_sleep_group;
		for (i = 0; i < n; i++)
		{
			DynBody *body;

			body = &self->bodies[members[i]];
			body->sleeping = TRUE;
			body->sleep_group = group;
			memset (body->sb.linear_velocity, 0, sizeof (body->sb.linear_velocity));
			memset (body->sb.angular_velocity, 0, sizeof (body->sb.angular_velocity));
			sync_flags (self, body);
		}
	}
}

static void
evict_outside (LrgDynamicsWorld *self)
{
	guint i;

	if (!self->has_bounds)
		return;
	for (i = 0; i < self->n_slots; i++)
	{
		DynBody *body;
		gfloat *c;

		body = &self->bodies[i];
		if (!body->used || !body->alive || body->is_plane || body->kind != LRG_DYNAMICS_BODY_DYNAMIC)
			continue;
		c = body->sb.position;
		if (c[0] < self->bounds_min[0] || c[0] > self->bounds_max[0] ||
		    c[1] < self->bounds_min[1] || c[1] > self->bounds_max[1] ||
		    c[2] < self->bounds_min[2] || c[2] > self->bounds_max[2])
		{
			guint id;

			body->alive = FALSE;
			memset (body->sb.linear_velocity, 0, sizeof (body->sb.linear_velocity));
			memset (body->sb.angular_velocity, 0, sizeof (body->sb.angular_velocity));
			refresh_mass (body);
			drop_proxy (self, body);
			id = body_id_of (self, i);
			g_array_append_val (self->evicted, id);
		}
	}
}

/* ==========================================================================
 * Continuous collision (linear cast of fast CCD bodies)
 * ========================================================================== */

typedef struct
{
	LrgDynamicsWorld *self;
	guint             slot;
	const gfloat     *xf_from;
	const gfloat     *xf_to;
	gfloat            best;
	guint             target;
	LrgRayHit         hit;
} CcdCtx;

static gboolean
ccd_target_ok (LrgDynamicsWorld *self,
               guint             slot,
               guint             t)
{
	DynBody *body;
	DynBody *other;

	if (t == slot || t >= self->n_slots)
		return FALSE;
	body = &self->bodies[slot];
	other = &self->bodies[t];
	if (!other->used || !other->alive || other->shape == NULL || other->sensor)
		return FALSE;
	/* Two CCD bodies in flight would each stop on the other's start pose. */
	if (other->ccd && body_active (other))
		return FALSE;
	if (!layers_allow (self, body, other) || no_collide_pair (self, slot, t))
		return FALSE;
	return TRUE;
}

static void
ccd_test (CcdCtx *ctx,
          guint   t)
{
	DynBody *body;
	DynBody *other;
	gfloat xt[16];
	gfloat toi;
	LrgRayHit hit;

	body = &ctx->self->bodies[ctx->slot];
	other = &ctx->self->bodies[t];
	shape_xf (other, xt);
	toi = 1.0f;
	lrg_ray_hit_reset (&hit);
	if (!lrg_collide_cast (body->shape, ctx->xf_from, ctx->xf_to, other->shape, xt, &toi, &hit))
		return;
	/* Already touching at the start: the contact solver owns that pair. */
	if (!(toi > 0.0f) || toi >= ctx->best)
		return;
	ctx->best = toi;
	ctx->target = t;
	ctx->hit = hit;
}

static gfloat
ccd_candidate (guint    proxy,
               guint    user_id,
               gfloat   t_enter,
               gpointer data)
{
	CcdCtx *ctx;

	(void) proxy;
	ctx = (CcdCtx *) data;
	if (t_enter >= ctx->best)
		return ctx->best;
	if (ccd_target_ok (ctx->self, ctx->slot, user_id))
		ccd_test (ctx, user_id);
	return ctx->best;
}

static void
ccd_body (LrgDynamicsWorld *self,
          guint             slot)
{
	DynBody *body;
	gfloat origin[3];
	gfloat disp[3];
	gfloat q_from[4];
	gfloat xf_from[16];
	gfloat xf_to[16];
	gfloat mn[3];
	gfloat mx[3];
	CcdCtx ctx;
	guint p;

	body = &self->bodies[slot];
	body_origin (body, origin);
	disp[0] = origin[0] - body->prev_origin[0];
	disp[1] = origin[1] - body->prev_origin[1];
	disp[2] = origin[2] - body->prev_origin[2];
	if (len3 (disp) <= DYN_CCD_FRACTION * body->inner_radius)
		return;

	lrg_dyn_quat_mul (body->prev_orientation, body->shape_rot, q_from);
	make_xf (q_from, body->prev_origin, xf_from);
	shape_xf (body, xf_to);
	lrg_physics_shape_get_world_bounds (body->shape, xf_from, mn, mx);

	memset (&ctx, 0, sizeof (ctx));
	ctx.self = self;
	ctx.slot = slot;
	ctx.xf_from = xf_from;
	ctx.xf_to = xf_to;
	ctx.best = 1.0f;
	ctx.target = DYN_NO_SLOT;
	lrg_broadphase_cast_aabb (self->broadphase, mn, mx, disp, 1.0f, body->mask, ccd_candidate, &ctx);
	for (p = 0; p < self->planes->len; p++)
	{
		guint ps;

		ps = g_array_index (self->planes, guint, p);
		if (ccd_target_ok (self, slot, ps))
			ccd_test (&ctx, ps);
	}
	if (ctx.target == DYN_NO_SLOT)
		return;

	/* Stop at the time of impact (rotation by normalised lerp). */
	{
		gfloat at[3];
		gfloat q[4];
		gfloat t;
		gfloat dotq;
		guint k;

		t = ctx.best;
		at[0] = body->prev_origin[0] + disp[0] * t;
		at[1] = body->prev_origin[1] + disp[1] * t;
		at[2] = body->prev_origin[2] + disp[2] * t;
		dotq = body->prev_orientation[0] * body->sb.orientation[0] +
		       body->prev_orientation[1] * body->sb.orientation[1] +
		       body->prev_orientation[2] * body->sb.orientation[2] +
		       body->prev_orientation[3] * body->sb.orientation[3];
		for (k = 0; k < 4u; k++)
			q[k] = body->prev_orientation[k] * (1.0f - t) +
			       body->sb.orientation[k] * ((dotq < 0.0f) ? -t : t);
		lrg_dyn_quat_normalize (q);
		copy4 (body->sb.orientation, q);
		set_origin (body, at);
		lrg_dyn_solver_body_update_inertia (&body->sb);
	}

	/* Remove the approach velocity along the hit normal (target -> body),
	 * keeping the material bounce. A dynamic target shares the impulse. */
	{
		DynBody *other;
		const gfloat *n;
		gfloat vt[3];
		gfloat vn;
		gfloat e;
		gfloat inv;
		gfloat j;
		const LrgContactSolverSettings *st;

		other = &self->bodies[ctx.target];
		n = ctx.hit.normal;
		lrg_solver_body_get_point_velocity (&other->sb, ctx.hit.point, vt);
		vn = (body->sb.linear_velocity[0] - vt[0]) * n[0] +
		     (body->sb.linear_velocity[1] - vt[1]) * n[1] +
		     (body->sb.linear_velocity[2] - vt[2]) * n[2];
		if (vn >= 0.0f)
			return;
		st = lrg_dyn_contact_solver_settings (self->solver);
		e = MAX (body->material_set ? body->restitution : self->restitution,
		         other->material_set ? other->restitution : self->restitution);
		if (-vn < st->restitution_threshold)
			e = 0.0f;
		inv = body->sb.inv_mass + other->sb.inv_mass;
		if (!(inv > 0.0f))
			return;
		j = -(1.0f + e) * vn / inv;
		body->sb.linear_velocity[0] += j * n[0] * body->sb.inv_mass;
		body->sb.linear_velocity[1] += j * n[1] * body->sb.inv_mass;
		body->sb.linear_velocity[2] += j * n[2] * body->sb.inv_mass;
		if (other->sb.inv_mass > 0.0f)
		{
			other->sb.linear_velocity[0] -= j * n[0] * other->sb.inv_mass;
			other->sb.linear_velocity[1] -= j * n[1] * other->sb.inv_mass;
			other->sb.linear_velocity[2] -= j * n[2] * other->sb.inv_mass;
			wake_slot (self, ctx.target);
		}
	}
}

/* ==========================================================================
 * The substep
 * ========================================================================== */

static void
integrate_velocities (LrgDynamicsWorld *self,
                      gfloat            dt)
{
	guint i;

	for (i = 0; i < self->n_slots; i++)
	{
		DynBody *body;

		body = &self->bodies[i];
		if (!body->used)
			continue;
		if (body->alive)
		{
			body_origin (body, body->prev_origin);
			copy4 (body->prev_orientation, body->sb.orientation);
		}
		if (body_dynamic (body) && !body->sleeping)
		{
			gfloat dw[3];
			gfloat lin;
			gfloat ang;
			guint k;

			for (k = 0; k < 3u; k++)
				body->sb.linear_velocity[k] += (body->force[k] * body->sb.inv_mass +
				                                self->gravity[k] * body->gravity_factor) * dt;
			lrg_dyn_mat3_mul_vec (body->sb.inv_inertia, body->torque, dw);
			for (k = 0; k < 3u; k++)
				body->sb.angular_velocity[k] += dw[k] * dt;
			if (body->linear_damping > 0.0f)
			{
				lin = MAX (0.0f, 1.0f - body->linear_damping * dt);
				for (k = 0; k < 3u; k++)
					body->sb.linear_velocity[k] *= lin;
			}
			if (body->angular_damping > 0.0f)
			{
				ang = MAX (0.0f, 1.0f - body->angular_damping * dt);
				for (k = 0; k < 3u; k++)
					body->sb.angular_velocity[k] *= ang;
			}
		}
		memset (body->force, 0, sizeof (body->force));
		memset (body->torque, 0, sizeof (body->torque));
	}
}

static void
integrate_positions (LrgDynamicsWorld *self,
                     gfloat            dt)
{
	guint i;

	for (i = 0; i < self->n_slots; i++)
	{
		DynBody *body;
		gfloat theta[3];

		body = &self->bodies[i];
		if (!body->used || !body->alive || body->frozen)
			continue;
		if (body->kind == LRG_DYNAMICS_BODY_STATIC)
			continue;
		if (body->kind == LRG_DYNAMICS_BODY_DYNAMIC && body->sleeping)
			continue;
		body->sb.position[0] += body->sb.linear_velocity[0] * dt;
		body->sb.position[1] += body->sb.linear_velocity[1] * dt;
		body->sb.position[2] += body->sb.linear_velocity[2] * dt;
		theta[0] = body->sb.angular_velocity[0] * dt;
		theta[1] = body->sb.angular_velocity[1] * dt;
		theta[2] = body->sb.angular_velocity[2] * dt;
		if (theta[0] != 0.0f || theta[1] != 0.0f || theta[2] != 0.0f)
		{
			lrg_dyn_quat_add_rotation (body->sb.orientation, theta);
			lrg_dyn_solver_body_update_inertia (&body->sb);
		}
	}
}

/* Debug timing of the substep phases (LRG_DYNAMICS_DEBUG=time). */
#define PHASE(n) \
	G_STMT_START { \
		if (self->debug_time) \
		{ \
			gint64 t_now = g_get_monotonic_time (); \
			self->phase_us[(n)] += t_now - t_mark; \
			t_mark = t_now; \
		} \
	} G_STMT_END

static void
substep (LrgDynamicsWorld *self,
         gfloat            dt)
{
	const LrgContactSolverSettings *st;
	g_autoptr(GPtrArray) active = NULL;
	GArray *ends;
	guint i;
	guint it;
	gint64 t_mark;

	st = lrg_dyn_contact_solver_settings (self->solver);
	self->in_substep = TRUE;

	/* 1. Forces and gravity into velocities. */
	t_mark = self->debug_time ? g_get_monotonic_time () : 0;
	integrate_velocities (self, dt);
	PHASE (0);

	/* 2. Broadphase: move the proxies of moving bodies, then pairs. */
	for (i = 0; i < self->n_slots; i++)
	{
		DynBody *body;

		body = &self->bodies[i];
		if (body->used && body->alive && body->kind != LRG_DYNAMICS_BODY_STATIC && body_active (body))
			sync_proxy (self, body, dt);
	}
	find_pairs (self);
	PHASE (1);

	/* 3. Narrowphase (+ warm-start matching, sensors, wake on contact). */
	narrowphase (self);
	PHASE (2);

	/* 4. Islands over awake dynamic bodies. */
	constraints_prepare (self);
	build_islands (self);
	PHASE (3);

	/* 5. Setup and warm start. A manifold is solved when one of its bodies
	 * is an awake dynamic body; constraints likewise. */
	for (i = 0; i < self->manifolds->len; i++)
	{
		LrgDynManifold *m;

		m = &g_array_index (self->manifolds, LrgDynManifold, i);
		m->body_a = &self->bodies[m->a].sb;
		m->body_b = &self->bodies[m->b].sb;
		m->flags &= ~LRG_DYN_MANIFOLD_ACTIVE;
		if (!slot_awake_dynamic (self, m->a) && !slot_awake_dynamic (self, m->b))
			continue;
		m->flags |= LRG_DYN_MANIFOLD_ACTIVE;
		if (!st->warm_starting)
		{
			guint k;

			for (k = 0; k < m->n_points; k++)
				m->points[k].normal_impulse = 0.0f;
			m->tangent_impulse[0] = 0.0f;
			m->tangent_impulse[1] = 0.0f;
			m->twist_impulse = 0.0f;
		}
		lrg_dyn_manifold_setup (m, st, dt);
		lrg_dyn_manifold_warm_start (m, 1.0f);
	}
	active = g_ptr_array_new ();
	ends = g_array_new (FALSE, FALSE, sizeof (LrgSolverBody *));
	for (i = 0; i < self->constraints->len; i++)
	{
		LrgPhysicsConstraint *c;
		LrgPhysicsConstraintClass *klass;
		LrgSolverBody *a;
		LrgSolverBody *b;
		guint sa;
		guint sb;

		c = g_ptr_array_index (self->constraints, i);
		if (!constraint_bodies (self, c, &a, &b, &sa, &sb))
			continue;
		if (!slot_awake_dynamic (self, sa) && !slot_awake_dynamic (self, sb))
			continue;
		klass = LRG_PHYSICS_CONSTRAINT_GET_CLASS (c);
		if (!st->warm_starting && klass->reset_warm_start != NULL)
			klass->reset_warm_start (c);
		if (klass->setup != NULL)
			klass->setup (c, a, b, dt);
		if (klass->warm_start != NULL)
			klass->warm_start (c, a, b, 1.0f);
		g_ptr_array_add (active, c);
		g_array_append_val (ends, a);
		g_array_append_val (ends, b);
	}

	PHASE (4);

	/* Velocity iterations: contacts, then constraints. */
	for (it = 0; it < st->velocity_iterations; it++)
	{
		for (i = 0; i < self->manifolds->len; i++)
		{
			LrgDynManifold *m;

			m = &g_array_index (self->manifolds, LrgDynManifold, i);
			if (m->flags & LRG_DYN_MANIFOLD_ACTIVE)
				lrg_dyn_manifold_solve_velocity (m, (it & 1u) != 0u);
		}
		for (i = 0; i < active->len; i++)
		{
			LrgPhysicsConstraint *c;
			LrgPhysicsConstraintClass *klass;

			c = g_ptr_array_index (active, i);
			klass = LRG_PHYSICS_CONSTRAINT_GET_CLASS (c);
			if (klass->solve_velocity != NULL)
				klass->solve_velocity (c, g_array_index (ends, LrgSolverBody *, 2u * i),
				                       g_array_index (ends, LrgSolverBody *, 2u * i + 1u), dt);
		}
	}

	PHASE (5);

	/* 6. Integrate positions, then sweep fast CCD bodies. */
	integrate_positions (self, dt);
	for (i = 0; i < self->n_slots; i++)
	{
		DynBody *body;

		body = &self->bodies[i];
		if (body->used && body->ccd && body->shape != NULL && body_dynamic (body) && !body->sleeping)
			ccd_body (self, i);
	}

	PHASE (6);

	/* 7. Position iterations (NGS), then post_solve. */
	for (it = 0; it < st->position_iterations; it++)
	{
		for (i = 0; i < self->manifolds->len; i++)
		{
			LrgDynManifold *m;

			m = &g_array_index (self->manifolds, LrgDynManifold, i);
			if (m->flags & LRG_DYN_MANIFOLD_ACTIVE)
				lrg_dyn_manifold_solve_position (m, st);
		}
		for (i = 0; i < active->len; i++)
		{
			LrgPhysicsConstraint *c;
			LrgPhysicsConstraintClass *klass;

			c = g_ptr_array_index (active, i);
			klass = LRG_PHYSICS_CONSTRAINT_GET_CLASS (c);
			if (klass->solve_position != NULL)
				klass->solve_position (c, g_array_index (ends, LrgSolverBody *, 2u * i),
				                       g_array_index (ends, LrgSolverBody *, 2u * i + 1u),
				                       dt, st->baumgarte);
		}
	}
	for (i = 0; i < active->len; i++)
	{
		LrgPhysicsConstraint *c;
		LrgPhysicsConstraintClass *klass;

		c = g_ptr_array_index (active, i);
		klass = LRG_PHYSICS_CONSTRAINT_GET_CLASS (c);
		if (klass->post_solve != NULL)
			klass->post_solve (c, g_array_index (ends, LrgSolverBody *, 2u * i),
			                   g_array_index (ends, LrgSolverBody *, 2u * i + 1u), dt);
	}
	g_array_unref (ends);

	/* 8. Sleep, eviction, contact/sensor events. */
	update_sleep (self);
	evict_outside (self);
	queue_events (self);
	PHASE (7);
	self->in_substep = FALSE;
}

/* ==========================================================================
 * The 0.8 limit joint as a private LrgPhysicsConstraint
 *
 * Distance limits between two anchors and angle limits between two axes,
 * solved at velocity level with a Baumgarte bias and corrected once per
 * substep at position level, exactly as the 0.8 world did.
 * ========================================================================== */

#define DYN_TYPE_LIMIT_JOINT (dyn_limit_joint_get_type ())
G_DECLARE_FINAL_TYPE (DynLimitJoint, dyn_limit_joint, DYN, LIMIT_JOINT, LrgPhysicsConstraint)

struct _DynLimitJoint
{
	LrgPhysicsConstraint parent_instance;

	gfloat   anchor_a[3];
	gfloat   axis_a[3];
	gfloat   anchor_b[3];
	gfloat   axis_b[3];
	gfloat   linear_min;
	gfloat   linear_max;
	gfloat   angular_min;
	gfloat   angular_max;
	gboolean corrected;
};

G_DEFINE_FINAL_TYPE (DynLimitJoint, dyn_limit_joint, LRG_TYPE_PHYSICS_CONSTRAINT)

static void
lj_direction (const gfloat *delta,
              gfloat        distance,
              gfloat       *direction)
{
	if (distance > 1.0e-6f)
	{
		direction[0] = delta[0] / distance;
		direction[1] = delta[1] / distance;
		direction[2] = delta[2] / distance;
	}
	else
	{
		direction[0] = 1.0f;
		direction[1] = 0.0f;
		direction[2] = 0.0f;
	}
}

static void
lj_solve_linear (DynLimitJoint *j,
                 LrgSolverBody *a,
                 LrgSolverBody *b,
                 gfloat         dt)
{
	gfloat pa[3];
	gfloat pb[3];
	gfloat delta[3];
	gfloat direction[3];
	gfloat va[3];
	gfloat vb[3];
	gfloat distance;
	gfloat err;
	gfloat separation;
	gfloat inverse;
	gfloat bias;
	gfloat magnitude;
	gfloat impulse[3];
	gboolean closing;

	lrg_solver_body_local_to_world (a, j->anchor_a, pa);
	lrg_solver_body_local_to_world (b, j->anchor_b, pb);
	delta[0] = pb[0] - pa[0];
	delta[1] = pb[1] - pa[1];
	delta[2] = pb[2] - pa[2];
	distance = len3 (delta);
	if (distance > j->linear_max)
	{
		err = distance - j->linear_max;
		closing = TRUE;
	}
	else if (distance < j->linear_min)
	{
		err = j->linear_min - distance;
		closing = FALSE;
	}
	else
	{
		return;
	}
	lj_direction (delta, distance, direction);
	lrg_solver_body_get_point_velocity (a, pa, va);
	lrg_solver_body_get_point_velocity (b, pb, vb);
	vb[0] -= va[0];
	vb[1] -= va[1];
	vb[2] -= va[2];
	separation = dot3 (vb, direction);
	inverse = lrg_solver_body_get_effective_mass_inv (a, pa, direction) +
	          lrg_solver_body_get_effective_mass_inv (b, pb, direction);
	if (inverse <= 1.0e-8f)
		return;
	bias = DYN_LEGACY_BAUMGARTE * err / dt;
	if (closing)
		magnitude = (separation + bias) / inverse;
	else
		magnitude = (separation - bias) / inverse;
	impulse[0] = direction[0] * magnitude;
	impulse[1] = direction[1] * magnitude;
	impulse[2] = direction[2] * magnitude;
	lrg_solver_body_apply_impulse (a, impulse, pa);
	impulse[0] = -impulse[0];
	impulse[1] = -impulse[1];
	impulse[2] = -impulse[2];
	lrg_solver_body_apply_impulse (b, impulse, pb);
}

/* Angle error between the two world axes and the axis that reduces it. */
static gboolean
lj_axis_error (DynLimitJoint       *j,
               const LrgSolverBody *a,
               const LrgSolverBody *b,
               gfloat              *err_out,
               gfloat              *axis_out)
{
	gfloat wa[3];
	gfloat wb[3];
	gfloat cr[3];
	gfloat cosine;
	gfloat angle;
	gfloat length;
	gboolean tighten;

	lrg_solver_body_rotate_to_world (a, j->axis_a, wa);
	lrg_solver_body_rotate_to_world (b, j->axis_b, wb);
	cosine = clampf (dot3 (wa, wb), -1.0f, 1.0f);
	angle = acosf (cosine);
	if (angle > j->angular_max)
	{
		*err_out = angle - j->angular_max;
		tighten = TRUE;
	}
	else if (angle < j->angular_min)
	{
		*err_out = j->angular_min - angle;
		tighten = FALSE;
	}
	else
	{
		return FALSE;
	}
	cross3 (wa, wb, cr);
	length = len3 (cr);
	if (length < 1.0e-5f)
		return FALSE;
	axis_out[0] = cr[0] / length;
	axis_out[1] = cr[1] / length;
	axis_out[2] = cr[2] / length;
	if (!tighten)
	{
		axis_out[0] = -axis_out[0];
		axis_out[1] = -axis_out[1];
		axis_out[2] = -axis_out[2];
	}
	return TRUE;
}

static gfloat
lj_scalar_inv_inertia (const LrgSolverBody *body,
                       const gfloat        *axis)
{
	gfloat w[3];

	if (!(body->inv_mass > 0.0f))
		return 0.0f;
	lrg_solver_body_mul_inv_inertia (body, axis, w);
	return dot3 (w, axis);
}

static void
lj_solve_angular (DynLimitJoint *j,
                  LrgSolverBody *a,
                  LrgSolverBody *b,
                  gfloat         dt)
{
	gfloat axis[3];
	gfloat err;
	gfloat inverse;
	gfloat relative;
	gfloat magnitude;
	gfloat impulse[3];

	if (!lj_axis_error (j, a, b, &err, axis))
		return;
	relative = 0.0f;
	if (a->inv_mass > 0.0f)
		relative += dot3 (a->angular_velocity, axis);
	if (b->inv_mass > 0.0f)
		relative -= dot3 (b->angular_velocity, axis);
	inverse = lj_scalar_inv_inertia (a, axis) + lj_scalar_inv_inertia (b, axis);
	if (inverse <= 1.0e-8f)
		return;
	magnitude = (DYN_LEGACY_BAUMGARTE * err / dt - relative) / inverse;
	impulse[0] = axis[0] * magnitude;
	impulse[1] = axis[1] * magnitude;
	impulse[2] = axis[2] * magnitude;
	lrg_solver_body_apply_angular_impulse (a, impulse);
	impulse[0] = -impulse[0];
	impulse[1] = -impulse[1];
	impulse[2] = -impulse[2];
	lrg_solver_body_apply_angular_impulse (b, impulse);
}

static void
lj_rotate (LrgSolverBody *body,
           const gfloat  *axis,
           gfloat         angle)
{
	gfloat delta[4];
	gfloat next[4];
	gfloat half;

	if (!(body->inv_mass > 0.0f) || angle == 0.0f)
		return;
	half = sinf (angle * 0.5f);
	delta[0] = axis[0] * half;
	delta[1] = axis[1] * half;
	delta[2] = axis[2] * half;
	delta[3] = cosf (angle * 0.5f);
	lrg_dyn_quat_mul (delta, body->orientation, next);
	lrg_dyn_quat_normalize (next);
	copy4 (body->orientation, next);
	lrg_dyn_solver_body_update_inertia (body);
}

static void
dyn_limit_joint_setup (LrgPhysicsConstraint *c,
                       LrgSolverBody        *a,
                       LrgSolverBody        *b,
                       gfloat                dt)
{
	(void) a;
	(void) b;
	(void) dt;
	DYN_LIMIT_JOINT (c)->corrected = FALSE;
}

static gboolean
dyn_limit_joint_solve_velocity (LrgPhysicsConstraint *c,
                                LrgSolverBody        *a,
                                LrgSolverBody        *b,
                                gfloat                dt)
{
	DynLimitJoint *j;

	j = DYN_LIMIT_JOINT (c);
	lj_solve_linear (j, a, b, dt);
	lj_solve_angular (j, a, b, dt);
	return TRUE;
}

static gboolean
dyn_limit_joint_solve_position (LrgPhysicsConstraint *c,
                                LrgSolverBody        *a,
                                LrgSolverBody        *b,
                                gfloat                dt,
                                gfloat                baumgarte)
{
	DynLimitJoint *j;
	gfloat pa[3];
	gfloat pb[3];
	gfloat delta[3];
	gfloat direction[3];
	gfloat distance;
	gfloat err;
	gfloat ia;
	gfloat ib;
	gfloat inverse;
	gfloat axis[3];

	(void) dt;
	(void) baumgarte;
	j = DYN_LIMIT_JOINT (c);
	/* The 0.8 world corrected once per substep with its own factor. */
	if (j->corrected)
		return FALSE;
	j->corrected = TRUE;

	/* Linear: move the centres apart/together by their mass ratio. */
	lrg_solver_body_local_to_world (a, j->anchor_a, pa);
	lrg_solver_body_local_to_world (b, j->anchor_b, pb);
	delta[0] = pb[0] - pa[0];
	delta[1] = pb[1] - pa[1];
	delta[2] = pb[2] - pa[2];
	distance = len3 (delta);
	err = 0.0f;
	if (distance > j->linear_max)
		err = distance - j->linear_max;
	else if (distance < j->linear_min)
		err = distance - j->linear_min;
	if (fabsf (err) > DYN_LEGACY_SLOP)
	{
		lj_direction (delta, distance, direction);
		ia = a->inv_mass;
		ib = b->inv_mass;
		inverse = ia + ib;
		if (inverse > 1.0e-8f)
		{
			gfloat sign;
			gfloat corr;
			guint k;

			sign = (err > 0.0f) ? 1.0f : -1.0f;
			corr = (fabsf (err) - DYN_LEGACY_SLOP) * DYN_LEGACY_POSITION;
			for (k = 0; k < 3u; k++)
			{
				if (ia > 0.0f)
					a->position[k] += direction[k] * sign * corr * (ia / inverse);
				if (ib > 0.0f)
					b->position[k] -= direction[k] * sign * corr * (ib / inverse);
			}
		}
	}

	/* Angular: turn both axes toward the limit by their inertia ratio. */
	if (lj_axis_error (j, a, b, &err, axis))
	{
		ia = lj_scalar_inv_inertia (a, axis);
		ib = lj_scalar_inv_inertia (b, axis);
		inverse = ia + ib;
		if (inverse > 1.0e-8f)
		{
			lj_rotate (a, axis, err * DYN_LEGACY_POSITION * (ia / inverse));
			lj_rotate (b, axis, -err * DYN_LEGACY_POSITION * (ib / inverse));
		}
	}
	return TRUE;
}

static void
dyn_limit_joint_class_init (DynLimitJointClass *klass)
{
	LrgPhysicsConstraintClass *cclass;

	cclass = LRG_PHYSICS_CONSTRAINT_CLASS (klass);
	cclass->setup = dyn_limit_joint_setup;
	cclass->solve_velocity = dyn_limit_joint_solve_velocity;
	cclass->solve_position = dyn_limit_joint_solve_position;
}

static void
dyn_limit_joint_init (DynLimitJoint *self)
{
	(void) self;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_dynamics_world_finalize (GObject *object)
{
	LrgDynamicsWorld *self;
	guint i;

	self = LRG_DYNAMICS_WORLD (object);
	if (self->debug_time)
		g_printerr ("dynamics phases (ms): forces %.1f broadphase %.1f narrowphase %.1f islands %.1f "
		            "setup %.1f velocity %.1f integrate %.1f position+rest %.1f\n",
		            self->phase_us[0] / 1000.0, self->phase_us[1] / 1000.0, self->phase_us[2] / 1000.0,
		            self->phase_us[3] / 1000.0, self->phase_us[4] / 1000.0, self->phase_us[5] / 1000.0,
		            self->phase_us[6] / 1000.0, self->phase_us[7] / 1000.0);
	for (i = 0; i < self->constraints->len; i++)
		lrg_dyn_constraint_detach (g_ptr_array_index (self->constraints, i));
	g_ptr_array_unref (self->constraints);
	for (i = 0; i < self->n_slots; i++)
		if (self->bodies[i].used)
			release_body (self, &self->bodies[i]);
	g_free (self->bodies);
	g_free (self->island_active);
	g_array_unref (self->free_slots);
	g_array_unref (self->manifolds);
	g_array_unref (self->old_manifolds);
	g_array_unref (self->pairs);
	g_array_unref (self->sensor_pairs);
	g_array_unref (self->old_sensor_pairs);
	g_array_unref (self->touching);
	g_array_unref (self->scratch);
	g_array_unref (self->events);
	g_array_unref (self->evicted);
	g_array_unref (self->planes);
	g_hash_table_unref (self->no_collide);
	g_clear_object (&self->broadphase);
	g_clear_object (&self->layers);
	g_clear_object (&self->solver);
	g_clear_object (&self->islands);
	G_OBJECT_CLASS (lrg_dynamics_world_parent_class)->finalize (object);
}

static void
lrg_dynamics_world_class_init (LrgDynamicsWorldClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = lrg_dynamics_world_finalize;

	/**
	 * LrgDynamicsWorld::contact-added:
	 * @world: the world
	 * @contact: the new touching contact (A-to-B normal; valid during
	 *   the emission only)
	 *
	 * Two bodies started touching (a manifold got a penetrating point).
	 * Emitted after lrg_dynamics_world_step() finished all substeps, in
	 * pair order. Only computed while a handler is connected.
	 */
	world_signals[SIGNAL_CONTACT_ADDED] =
		g_signal_new ("contact-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1,
		              LRG_TYPE_DYNAMICS_CONTACT | G_SIGNAL_TYPE_STATIC_SCOPE);

	/**
	 * LrgDynamicsWorld::contact-removed:
	 * @world: the world
	 * @body_a: first body id (the lower slot)
	 * @body_b: second body id
	 *
	 * Two bodies stopped touching (or one was evicted or removed). A
	 * pair that falls asleep while touching is not removed.
	 */
	world_signals[SIGNAL_CONTACT_REMOVED] =
		g_signal_new ("contact-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	/**
	 * LrgDynamicsWorld::sensor-entered:
	 * @world: the world
	 * @sensor: the sensor body id
	 * @other: the body that entered it
	 *
	 * A body started overlapping a sensor.
	 */
	world_signals[SIGNAL_SENSOR_ENTERED] =
		g_signal_new ("sensor-entered", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	/**
	 * LrgDynamicsWorld::sensor-exited:
	 * @world: the world
	 * @sensor: the sensor body id
	 * @other: the body that left it (or was evicted or removed)
	 *
	 * A body stopped overlapping a sensor.
	 */
	world_signals[SIGNAL_SENSOR_EXITED] =
		g_signal_new ("sensor-exited", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	/**
	 * LrgDynamicsWorld::pre-substep:
	 * @world: the world
	 * @dt: the substep length
	 *
	 * Emitted before each fixed substep. Handlers may apply forces, move
	 * kinematic bodies or add and remove bodies (a character controller
	 * or a buoyancy volume hooks in here).
	 */
	world_signals[SIGNAL_PRE_SUBSTEP] =
		g_signal_new ("pre-substep", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_FLOAT);

	/**
	 * LrgDynamicsWorld::post-substep:
	 * @world: the world
	 * @dt: the substep length
	 *
	 * Emitted after each fixed substep, before the queued contact and
	 * sensor signals of the step.
	 */
	world_signals[SIGNAL_POST_SUBSTEP] =
		g_signal_new ("post-substep", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_FLOAT);
}

static void
lrg_dynamics_world_init (LrgDynamicsWorld *self)
{
	self->fixed_dt = 1.0f / 60.0f;
	self->max_substeps = 4;
	/* Jolt-like: a body under 0.03 m/s (and rad/s) for half a second of
	 * 60 Hz substeps sleeps. */
	self->sleep_speed2 = 9.0e-4f;
	self->sleep_after = 30;
	self->free_slots = g_array_new (FALSE, FALSE, sizeof (guint));
	self->constraints = g_ptr_array_new_with_free_func (g_object_unref);
	self->manifolds = g_array_new (FALSE, FALSE, sizeof (LrgDynManifold));
	self->old_manifolds = g_array_new (FALSE, FALSE, sizeof (LrgDynManifold));
	self->pairs = g_array_new (FALSE, FALSE, sizeof (guint64));
	self->sensor_pairs = g_array_new (FALSE, FALSE, sizeof (guint64));
	self->old_sensor_pairs = g_array_new (FALSE, FALSE, sizeof (guint64));
	self->touching = g_array_new (FALSE, FALSE, sizeof (guint));
	self->scratch = g_array_new (FALSE, FALSE, sizeof (LrgContactManifold));
	self->events = g_array_new (FALSE, FALSE, sizeof (DynEvent));
	self->evicted = g_array_new (FALSE, FALSE, sizeof (guint));
	self->planes = g_array_new (FALSE, FALSE, sizeof (guint));
	self->no_collide = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
	self->broadphase = lrg_broadphase_new ();
	self->solver = lrg_contact_solver_new ();
	self->islands = lrg_island_builder_new ();
	memset (&self->world_body, 0, sizeof (self->world_body));
	self->world_body.id = LRG_DYNAMICS_WORLD_BODY;
	self->world_body.orientation[3] = 1.0f;
	self->debug_match = (g_strcmp0 (g_getenv ("LRG_DYNAMICS_DEBUG"), "match") == 0);
	self->debug_time = (g_strcmp0 (g_getenv ("LRG_DYNAMICS_DEBUG"), "time") == 0);
}

/* ==========================================================================
 * World settings (0.8 API)
 * ========================================================================== */

LrgDynamicsWorld *
lrg_dynamics_world_new (void)
{
	return g_object_new (LRG_TYPE_DYNAMICS_WORLD, NULL);
}

gboolean
lrg_dynamics_world_set_gravity (LrgDynamicsWorld *self,
                                const gfloat     *gravity,
                                GError          **error)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (gravity != NULL, FALSE);
	if (!finite3 (gravity))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics gravity is not finite");
	copy3 (self->gravity, gravity);
	return TRUE;
}

gboolean
lrg_dynamics_world_set_step_limits (LrgDynamicsWorld *self,
                                    gfloat            fixed_dt,
                                    guint             max_substeps,
                                    GError          **error)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	if (!finite1 (fixed_dt) || fixed_dt <= 0.0f || fixed_dt > 0.1f)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
		                  "dynamics fixed step must be in (0, 0.1] seconds");
	if (max_substeps < 1 || max_substeps > LRG_DYNAMICS_MAX_SUBSTEPS)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
		                  "dynamics substep cap must be from 1 to %u", LRG_DYNAMICS_MAX_SUBSTEPS);
	self->fixed_dt = fixed_dt;
	self->max_substeps = max_substeps;
	return TRUE;
}

gboolean
lrg_dynamics_world_set_material (LrgDynamicsWorld *self,
                                 gfloat            restitution,
                                 gfloat            friction,
                                 GError          **error)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	if (!finite1 (restitution) || restitution < 0.0f || restitution > 1.0f)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics restitution must be in [0, 1]");
	if (!finite1 (friction) || friction < 0.0f)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics friction must be zero or positive");
	self->restitution = restitution;
	self->friction = friction;
	return TRUE;
}

void
lrg_dynamics_world_set_iterations (LrgDynamicsWorld *self,
                                   guint             iterations)
{
	g_return_if_fail (LRG_IS_DYNAMICS_WORLD (self));
	lrg_contact_solver_set_velocity_iterations (self->solver, CLAMP (iterations, 1u, 32u));
}

gboolean
lrg_dynamics_world_set_sleep (LrgDynamicsWorld *self,
                              gfloat            speed_squared,
                              guint             quiet_substeps,
                              GError          **error)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	if (!finite1 (speed_squared) || speed_squared < 0.0f)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics sleep speed must be zero or positive");
	if (quiet_substeps < 1)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics sleep needs at least one quiet substep");
	self->sleep_speed2 = speed_squared;
	self->sleep_after = quiet_substeps;
	return TRUE;
}

gboolean
lrg_dynamics_world_set_bounds (LrgDynamicsWorld *self,
                               const gfloat     *minimum,
                               const gfloat     *maximum,
                               GError          **error)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (minimum != NULL && maximum != NULL, FALSE);
	if (!finite3 (minimum) || !finite3 (maximum) ||
	    !(minimum[0] < maximum[0]) || !(minimum[1] < maximum[1]) || !(minimum[2] < maximum[2]))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
		                  "dynamics bounds need a positive finite extent on each axis");
	copy3 (self->bounds_min, minimum);
	copy3 (self->bounds_max, maximum);
	self->has_bounds = TRUE;
	return TRUE;
}

/* ==========================================================================
 * Adding bodies (0.8 primitives)
 * ========================================================================== */

static gboolean
mass_accepted (LrgDynamicsBodyKind   kind,
               gfloat                mass,
               GError              **error)
{
	if (kind != LRG_DYNAMICS_BODY_DYNAMIC && kind != LRG_DYNAMICS_BODY_KINEMATIC &&
	    kind != LRG_DYNAMICS_BODY_STATIC)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
		                  "dynamics body kind is not dynamic, kinematic or static");
	if (!finite1 (mass) || mass < 0.0f || (kind == LRG_DYNAMICS_BODY_DYNAMIC && !(mass > 0.0f)))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
		                  "dynamics body mass must be finite and positive when dynamic");
	return TRUE;
}

/* The rotation that turns local +Y onto the unit vector @to. */
static void
quat_from_y (const gfloat *to,
             gfloat       *q)
{
	gfloat c;
	gfloat axis[3];
	gfloat up[3];

	up[0] = 0.0f;
	up[1] = 1.0f;
	up[2] = 0.0f;
	c = dot3 (up, to);
	if (c < -0.99999f)
	{
		q[0] = 1.0f;
		q[1] = 0.0f;
		q[2] = 0.0f;
		q[3] = 0.0f;
		return;
	}
	cross3 (up, to, axis);
	q[0] = axis[0];
	q[1] = axis[1];
	q[2] = axis[2];
	q[3] = 1.0f + c;
	lrg_dyn_quat_normalize (q);
}

static gboolean
check_capacity (LrgDynamicsWorld  *self,
                GError           **error)
{
	if (self->free_slots->len == 0 && self->n_slots >= DYN_SLOT_MASK)
		return dyn_error (error, LRG_DYNAMICS_ERROR_CAPACITY,
		                  "dynamics world already has %u bodies", DYN_SLOT_MASK);
	return TRUE;
}

static void
store_isotropic (DynBody *body,
                 gfloat   mass,
                 gfloat   radius)
{
	gfloat inertia;

	body->mass = mass;
	if (body->kind != LRG_DYNAMICS_BODY_DYNAMIC)
		return;
	inertia = 0.4f * mass * radius * radius;
	memset (body->inertia_local, 0, sizeof (body->inertia_local));
	body->inertia_local[0] = inertia;
	body->inertia_local[4] = inertia;
	body->inertia_local[8] = inertia;
}

gboolean
lrg_dynamics_world_add_sphere (LrgDynamicsWorld     *self,
                               LrgDynamicsBodyKind   kind,
                               gfloat                mass,
                               gfloat                radius,
                               const gfloat         *position,
                               const gfloat         *orientation,
                               guint                *body_id,
                               GError              **error)
{
	gfloat q[4];
	guint slot;
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (position != NULL, FALSE);
	g_return_val_if_fail (body_id != NULL, FALSE);
	if (!mass_accepted (kind, mass, error))
		return FALSE;
	if (!finite1 (radius) || !(radius > 0.0f) || !finite3 (position))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
		                  "dynamics sphere radius and position must be finite and the radius positive");
	if (!read_orientation (orientation, q, error) || !check_capacity (self, error))
		return FALSE;
	slot = alloc_slot (self);
	body = init_body (self, slot, kind, DYN_SHAPE_SPHERE, q);
	body->radius = radius;
	body->inner_radius = radius;
	body->shape = lrg_physics_shape_new_sphere (radius);
	store_isotropic (body, mass, radius);
	copy3 (body->sb.position, position);
	finish_body (self, body);
	*body_id = body_id_of (self, slot);
	return TRUE;
}

gboolean
lrg_dynamics_world_add_plane (LrgDynamicsWorld *self,
                              const gfloat     *normal,
                              gfloat            offset,
                              guint            *body_id,
                              GError          **error)
{
	guint slot;
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (normal != NULL, FALSE);
	g_return_val_if_fail (body_id != NULL, FALSE);
	if (!finite3 (normal) || !finite1 (offset))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics plane normal and offset must be finite");
	if (fabsf (len3 (normal) - 1.0f) > 1.0e-4f)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics plane normal must already have length 1");
	if (!check_capacity (self, error))
		return FALSE;
	slot = alloc_slot (self);
	body = init_body (self, slot, LRG_DYNAMICS_BODY_STATIC, DYN_SHAPE_PLANE, NULL);
	body->is_plane = TRUE;
	copy3 (body->plane_normal, normal);
	body->plane_offset = offset;
	body->shape = lrg_physics_shape_new_plane (normal, offset);
	finish_body (self, body);
	*body_id = body_id_of (self, slot);
	return TRUE;
}

gboolean
lrg_dynamics_world_add_sphyl (LrgDynamicsWorld     *self,
                              LrgDynamicsBodyKind   kind,
                              gfloat                mass,
                              gfloat                radius,
                              gfloat                half_length,
                              const gfloat         *axis,
                              const gfloat         *position,
                              const gfloat         *orientation,
                              guint                *body_id,
                              GError              **error)
{
	gfloat q[4];
	guint slot;
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (axis != NULL && position != NULL && body_id != NULL, FALSE);
	if (!mass_accepted (kind, mass, error))
		return FALSE;
	if (!finite1 (radius) || !(radius > 0.0f) || !finite1 (half_length) || half_length < 0.0f ||
	    !finite3 (position) || !finite3 (axis))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
		                  "dynamics sphyl radius, half-length, axis and position must be finite");
	if (fabsf (len3 (axis) - 1.0f) > 1.0e-4f)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics sphyl axis must already have length 1");
	if (!read_orientation (orientation, q, error) || !check_capacity (self, error))
		return FALSE;
	slot = alloc_slot (self);
	body = init_body (self, slot, kind, DYN_SHAPE_SPHYL, q);
	body->radius = radius;
	body->inner_radius = radius;
	body->half_length = half_length;
	copy3 (body->axis, axis);
	/* The shape library's capsule runs along local +Y; turn it onto the
	 * caller's axis. A zero half-length is a sphere. */
	if (half_length > 0.0f)
		body->shape = lrg_physics_shape_new_capsule (radius, half_length);
	else
		body->shape = lrg_physics_shape_new_sphere (radius);
	quat_from_y (axis, body->shape_rot);
	store_isotropic (body, mass, radius);
	copy3 (body->sb.position, position);
	finish_body (self, body);
	*body_id = body_id_of (self, slot);
	return TRUE;
}

gboolean
lrg_dynamics_world_add_inertial (LrgDynamicsWorld *self,
                                 gfloat            mass,
                                 const gfloat     *inertia_diagonal,
                                 const gfloat     *position,
                                 const gfloat     *orientation,
                                 guint            *body_id,
                                 GError          **error)
{
	gfloat q[4];
	guint slot;
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (inertia_diagonal != NULL && position != NULL && body_id != NULL, FALSE);
	if (!finite1 (mass) || !(mass > 0.0f) || !finite3 (position))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
		                  "dynamics contactless mass and position must be finite and the mass positive");
	if (!finite3 (inertia_diagonal) || !(inertia_diagonal[0] > 0.0f) ||
	    !(inertia_diagonal[1] > 0.0f) || !(inertia_diagonal[2] > 0.0f))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
		                  "dynamics contactless inertia diagonal must be finite and positive");
	if (!read_orientation (orientation, q, error) || !check_capacity (self, error))
		return FALSE;
	slot = alloc_slot (self);
	body = init_body (self, slot, LRG_DYNAMICS_BODY_DYNAMIC, DYN_SHAPE_INERTIAL, q);
	body->mass = mass;
	memset (body->inertia_local, 0, sizeof (body->inertia_local));
	body->inertia_local[0] = inertia_diagonal[0];
	body->inertia_local[4] = inertia_diagonal[1];
	body->inertia_local[8] = inertia_diagonal[2];
	copy3 (body->sb.position, position);
	finish_body (self, body);
	*body_id = body_id_of (self, slot);
	return TRUE;
}

gboolean
lrg_dynamics_world_set_box_inertia (LrgDynamicsWorld *self,
                                    guint             body_id,
                                    gfloat            half_x,
                                    gfloat            half_y,
                                    gfloat            half_z,
                                    GError          **error)
{
	DynBody *body;
	gfloat m;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = alive_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (body->skind != DYN_SHAPE_SPHERE || body->kind != LRG_DYNAMICS_BODY_DYNAMIC || !(body->mass > 0.0f))
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics box inertia requires a dynamic sphere");
	if (!finite1 (half_x) || !finite1 (half_y) || !finite1 (half_z) ||
	    !(half_x > 0.0f) || !(half_y > 0.0f) || !(half_z > 0.0f))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics box half extents must be finite and positive");
	m = body->mass;
	set_inertia_diagonal (body,
	                      m / 12.0f * (half_y * half_y + half_z * half_z),
	                      m / 12.0f * (half_x * half_x + half_z * half_z),
	                      m / 12.0f * (half_x * half_x + half_y * half_y));
	return TRUE;
}

gboolean
lrg_dynamics_world_set_inertia_diagonal (LrgDynamicsWorld *self,
                                         guint             body_id,
                                         gfloat            ixx,
                                         gfloat            iyy,
                                         gfloat            izz,
                                         GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = alive_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (body->kind != LRG_DYNAMICS_BODY_DYNAMIC || !(body->mass > 0.0f))
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics inertia diagonal requires a dynamic body");
	if (!finite1 (ixx) || !finite1 (iyy) || !finite1 (izz) || !(ixx > 0.0f) || !(iyy > 0.0f) || !(izz > 0.0f))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics inertia diagonal must be finite and positive");
	set_inertia_diagonal (body, ixx, iyy, izz);
	return TRUE;
}

gboolean
lrg_dynamics_world_get_inertia (LrgDynamicsWorld *self,
                                guint             body_id,
                                gfloat           *inertia,
                                GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (inertia != NULL, FALSE);
	body = solid_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	inertia[0] = body->inertia_local[0];
	inertia[1] = body->inertia_local[4];
	inertia[2] = body->inertia_local[8];
	return TRUE;
}

/* ==========================================================================
 * Pose and velocity (0.8 API)
 * ========================================================================== */

/* Teleport: the new pose also becomes the interpolation start. */
static void
teleport (LrgDynamicsWorld *self,
          DynBody          *body,
          const gfloat     *origin,
          const gfloat     *orientation)
{
	copy4 (body->sb.orientation, orientation);
	set_origin (body, origin);
	lrg_dyn_solver_body_update_inertia (&body->sb);
	copy3 (body->prev_origin, origin);
	copy4 (body->prev_orientation, orientation);
	if (body->shape != NULL && !body->is_plane && body->alive)
		sync_proxy (self, body, 0.0f);
}

gboolean
lrg_dynamics_world_set_pose (LrgDynamicsWorld *self,
                             guint             body_id,
                             const gfloat     *position,
                             const gfloat     *orientation,
                             GError          **error)
{
	DynBody *body;
	gfloat q[4];

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (position != NULL && orientation != NULL, FALSE);
	body = alive_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (!finite3 (position))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics pose is not finite");
	if (!read_orientation (orientation, q, error))
		return FALSE;
	teleport (self, body, position, q);
	if (!body->frozen)
		wake_body (self, body);
	if (body->kind != LRG_DYNAMICS_BODY_DYNAMIC)
		wake_neighbours (self, slot_index (self, body));
	return TRUE;
}

gboolean
lrg_dynamics_world_get_position (LrgDynamicsWorld *self,
                                 guint             body_id,
                                 gfloat           *position,
                                 GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (position != NULL, FALSE);
	body = solid_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	body_origin (body, position);
	return TRUE;
}

gboolean
lrg_dynamics_world_get_orientation (LrgDynamicsWorld *self,
                                    guint             body_id,
                                    gfloat           *orientation,
                                    GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (orientation != NULL, FALSE);
	body = solid_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	copy4 (orientation, body->sb.orientation);
	return TRUE;
}

gboolean
lrg_dynamics_world_get_linear_velocity (LrgDynamicsWorld *self,
                                        guint             body_id,
                                        gfloat           *velocity,
                                        GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (velocity != NULL, FALSE);
	body = solid_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	copy3 (velocity, body->sb.linear_velocity);
	return TRUE;
}

gboolean
lrg_dynamics_world_get_angular_velocity (LrgDynamicsWorld *self,
                                         guint             body_id,
                                         gfloat           *velocity,
                                         GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (velocity != NULL, FALSE);
	body = solid_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	copy3 (velocity, body->sb.angular_velocity);
	return TRUE;
}

/* A body whose velocity the caller may write. */
static DynBody *
movable_known (LrgDynamicsWorld  *self,
               guint              body_id,
               const gfloat      *vector,
               const gchar       *what,
               GError           **error)
{
	DynBody *body;

	body = alive_known (self, body_id, error);
	if (body == NULL)
		return NULL;
	if (body->frozen || body->kind == LRG_DYNAMICS_BODY_STATIC)
	{
		dyn_error (error, LRG_DYNAMICS_ERROR_STATE,
		           "dynamics %s requires an unfrozen dynamic or kinematic body", what);
		return NULL;
	}
	if (!finite3 (vector))
	{
		dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics %s is not finite", what);
		return NULL;
	}
	return body;
}

/* A dynamic, unfrozen body forces and impulses may act on. */
static DynBody *
pushable_known (LrgDynamicsWorld  *self,
                guint              body_id,
                const gfloat      *vector,
                const gchar       *what,
                GError           **error)
{
	DynBody *body;

	body = alive_known (self, body_id, error);
	if (body == NULL)
		return NULL;
	if (body->frozen || body->kind != LRG_DYNAMICS_BODY_DYNAMIC)
	{
		dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics %s requires an unfrozen dynamic body", what);
		return NULL;
	}
	if (!finite3 (vector))
	{
		dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics %s is not finite", what);
		return NULL;
	}
	return body;
}

gboolean
lrg_dynamics_world_set_linear_velocity (LrgDynamicsWorld *self,
                                        guint             body_id,
                                        const gfloat     *velocity,
                                        GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (velocity != NULL, FALSE);
	body = movable_known (self, body_id, velocity, "velocity", error);
	if (body == NULL)
		return FALSE;
	copy3 (body->sb.linear_velocity, velocity);
	wake_body (self, body);
	return TRUE;
}

gboolean
lrg_dynamics_world_set_angular_velocity (LrgDynamicsWorld *self,
                                         guint             body_id,
                                         const gfloat     *velocity,
                                         GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (velocity != NULL, FALSE);
	body = movable_known (self, body_id, velocity, "angular velocity", error);
	if (body == NULL)
		return FALSE;
	copy3 (body->sb.angular_velocity, velocity);
	wake_body (self, body);
	return TRUE;
}

gboolean
lrg_dynamics_world_apply_force (LrgDynamicsWorld *self,
                                guint             body_id,
                                const gfloat     *force,
                                GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (force != NULL, FALSE);
	body = pushable_known (self, body_id, force, "force", error);
	if (body == NULL)
		return FALSE;
	wake_body (self, body);
	body->force[0] += force[0];
	body->force[1] += force[1];
	body->force[2] += force[2];
	return TRUE;
}

gboolean
lrg_dynamics_world_apply_torque (LrgDynamicsWorld *self,
                                 guint             body_id,
                                 const gfloat     *torque,
                                 GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (torque != NULL, FALSE);
	body = pushable_known (self, body_id, torque, "torque", error);
	if (body == NULL)
		return FALSE;
	wake_body (self, body);
	body->torque[0] += torque[0];
	body->torque[1] += torque[1];
	body->torque[2] += torque[2];
	return TRUE;
}

gboolean
lrg_dynamics_world_apply_force_at_point (LrgDynamicsWorld *self,
                                         guint             body_id,
                                         const gfloat     *force,
                                         const gfloat     *point,
                                         GError          **error)
{
	DynBody *body;
	gfloat r[3];
	gfloat t[3];

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (force != NULL && point != NULL, FALSE);
	body = pushable_known (self, body_id, force, "force", error);
	if (body == NULL)
		return FALSE;
	if (!finite3 (point))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics force point is not finite");
	wake_body (self, body);
	r[0] = point[0] - body->sb.position[0];
	r[1] = point[1] - body->sb.position[1];
	r[2] = point[2] - body->sb.position[2];
	cross3 (r, force, t);
	body->force[0] += force[0];
	body->force[1] += force[1];
	body->force[2] += force[2];
	body->torque[0] += t[0];
	body->torque[1] += t[1];
	body->torque[2] += t[2];
	return TRUE;
}

gboolean
lrg_dynamics_world_apply_impulse (LrgDynamicsWorld *self,
                                  guint             body_id,
                                  const gfloat     *impulse,
                                  const gfloat     *point,
                                  GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (impulse != NULL, FALSE);
	body = pushable_known (self, body_id, impulse, "impulse", error);
	if (body == NULL)
		return FALSE;
	if (point != NULL && !finite3 (point))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics impulse point is not finite");
	wake_body (self, body);
	lrg_solver_body_apply_impulse (&body->sb, impulse, point);
	return TRUE;
}

gboolean
lrg_dynamics_world_apply_angular_impulse (LrgDynamicsWorld *self,
                                          guint             body_id,
                                          const gfloat     *impulse,
                                          GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (impulse != NULL, FALSE);
	body = pushable_known (self, body_id, impulse, "angular impulse", error);
	if (body == NULL)
		return FALSE;
	wake_body (self, body);
	lrg_solver_body_apply_angular_impulse (&body->sb, impulse);
	return TRUE;
}

gboolean
lrg_dynamics_world_set_frozen (LrgDynamicsWorld *self,
                               guint             body_id,
                               gboolean          frozen,
                               GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = solid_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	body->frozen = frozen ? TRUE : FALSE;
	refresh_mass (body);
	sync_flags (self, body);
	return TRUE;
}

gboolean
lrg_dynamics_world_get_sleeping (LrgDynamicsWorld *self,
                                 guint             body_id,
                                 gboolean         *sleeping,
                                 GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (sleeping != NULL, FALSE);
	body = solid_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	*sleeping = body->sleeping;
	return TRUE;
}

gboolean
lrg_dynamics_world_body_is_active (LrgDynamicsWorld *self,
                                   guint             body_id,
                                   gboolean         *active,
                                   GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (active != NULL, FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	*active = body->alive;
	return TRUE;
}

/* ==========================================================================
 * Joints and constraints
 * ========================================================================== */

static gboolean
normalize_axis (const gfloat  *in,
                gfloat        *out,
                GError       **error,
                const gchar   *which)
{
	gfloat length;

	if (!finite3 (in))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics %s is not finite", which);
	length = len3 (in);
	if (length <= 1.0e-8f)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics %s has zero length", which);
	out[0] = in[0] / length;
	out[1] = in[1] / length;
	out[2] = in[2] / length;
	return TRUE;
}

gboolean
lrg_dynamics_world_add_joint (LrgDynamicsWorld *self,
                              guint             body_a,
                              guint             body_b,
                              const gfloat     *anchor_a,
                              const gfloat     *axis_a,
                              const gfloat     *anchor_b,
                              const gfloat     *axis_b,
                              gfloat            linear_min,
                              gfloat            linear_max,
                              gfloat            angular_min,
                              gfloat            angular_max,
                              guint            *joint_id,
                              GError          **error)
{
	g_autoptr(DynLimitJoint) joint = NULL;
	gfloat na[3];
	gfloat nb[3];

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (anchor_a != NULL && axis_a != NULL, FALSE);
	g_return_val_if_fail (anchor_b != NULL && axis_b != NULL, FALSE);
	g_return_val_if_fail (joint_id != NULL, FALSE);
	if (body_a == body_b)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics joint needs two different endpoints");
	if (alive_known (self, body_a, error) == NULL)
		return FALSE;
	if (body_b != LRG_DYNAMICS_WORLD_BODY && alive_known (self, body_b, error) == NULL)
		return FALSE;
	if (!finite3 (anchor_a) || !finite3 (anchor_b))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics joint anchors must be finite");
	if (!finite1 (linear_min) || !finite1 (linear_max) || linear_min < 0.0f || linear_max < linear_min)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
		                  "dynamics linear limits must be finite, non-negative and ordered");
	if (!finite1 (angular_min) || !finite1 (angular_max) || angular_min < 0.0f ||
	    angular_max > (gfloat) G_PI || angular_max < angular_min)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
		                  "dynamics angular limits must lie in [0, pi] and be ordered");
	if (!normalize_axis (axis_a, na, error, "joint axis A") ||
	    !normalize_axis (axis_b, nb, error, "joint axis B"))
		return FALSE;
	joint = g_object_new (DYN_TYPE_LIMIT_JOINT, "body-a", body_a, "body-b", body_b, NULL);
	copy3 (joint->anchor_a, anchor_a);
	copy3 (joint->anchor_b, anchor_b);
	copy3 (joint->axis_a, na);
	copy3 (joint->axis_b, nb);
	joint->linear_min = linear_min;
	joint->linear_max = linear_max;
	joint->angular_min = angular_min;
	joint->angular_max = angular_max;
	return lrg_dynamics_world_add_constraint (self, LRG_PHYSICS_CONSTRAINT (joint), joint_id, error);
}

gboolean
lrg_dynamics_world_add_constraint (LrgDynamicsWorld      *self,
                                   LrgPhysicsConstraint  *constraint,
                                   guint                 *constraint_id,
                                   GError               **error)
{
	guint a;
	guint b;
	guint id;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (LRG_IS_PHYSICS_CONSTRAINT (constraint), FALSE);
	if (lrg_physics_constraint_get_in_world (constraint))
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics constraint is already in a world");
	if (self->in_substep)
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics constraints cannot be added during a substep");
	a = lrg_physics_constraint_get_body_a (constraint);
	b = lrg_physics_constraint_get_body_b (constraint);
	if (a == b)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics constraint needs two different bodies");
	if (a != LRG_DYNAMICS_WORLD_BODY && slot_of (self, a) == DYN_NO_SLOT)
		return dyn_error (error, LRG_DYNAMICS_ERROR_MISSING, "dynamics body %u was never added", a);
	if (b != LRG_DYNAMICS_WORLD_BODY && slot_of (self, b) == DYN_NO_SLOT)
		return dyn_error (error, LRG_DYNAMICS_ERROR_MISSING, "dynamics body %u was never added", b);
	id = self->next_constraint_id++;
	g_ptr_array_add (self->constraints, g_object_ref (constraint));
	lrg_dyn_constraint_attach (constraint, id);
	if (constraint_id != NULL)
		*constraint_id = id;
	return TRUE;
}

gboolean
lrg_dynamics_world_remove_constraint (LrgDynamicsWorld     *self,
                                      LrgPhysicsConstraint *constraint)
{
	guint index;
	guint slot;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (LRG_IS_PHYSICS_CONSTRAINT (constraint), FALSE);
	if (self->in_substep)
	{
		g_warning ("lrg_dynamics_world_remove_constraint: called during a substep; disable the constraint instead");
		return FALSE;
	}
	if (!g_ptr_array_find (self->constraints, constraint, &index))
		return FALSE;
	slot = slot_of (self, lrg_physics_constraint_get_body_a (constraint));
	if (slot != DYN_NO_SLOT)
		wake_slot (self, slot);
	slot = slot_of (self, lrg_physics_constraint_get_body_b (constraint));
	if (slot != DYN_NO_SLOT)
		wake_slot (self, slot);
	lrg_dyn_constraint_detach (constraint);
	g_ptr_array_remove_index (self->constraints, index);
	return TRUE;
}

guint
lrg_dynamics_world_get_constraint_count (LrgDynamicsWorld *self)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), 0);
	return self->constraints->len;
}

LrgPhysicsConstraint *
lrg_dynamics_world_get_constraint (LrgDynamicsWorld *self,
                                   guint             index)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), NULL);
	if (index >= self->constraints->len)
		return NULL;
	return g_ptr_array_index (self->constraints, index);
}

gboolean
lrg_dynamics_world_get_solver_body (LrgDynamicsWorld *self,
                                    guint             body_id,
                                    LrgSolverBody    *out,
                                    GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);
	if (body_id == LRG_DYNAMICS_WORLD_BODY)
	{
		*out = self->world_body;
		return TRUE;
	}
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	*out = body->sb;
	return TRUE;
}

/* ==========================================================================
 * Stepping
 * ========================================================================== */

gboolean
lrg_dynamics_world_step (LrgDynamicsWorld *self,
                         gfloat            dt,
                         guint            *substeps,
                         GError          **error)
{
	guint taken;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	if (!finite1 (dt) || dt < 0.0f)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics step time must be finite and non-negative");
	if (self->in_substep)
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics step called from inside a substep");
	g_array_set_size (self->touching, 0);
	g_array_set_size (self->evicted, 0);
	self->reported = FALSE;
	self->dropped = 0.0f;
	self->accumulator += dt;
	taken = 0;
	while (taken < self->max_substeps && self->accumulator + 1.0e-6f >= self->fixed_dt)
	{
		g_signal_emit (self, world_signals[SIGNAL_PRE_SUBSTEP], 0, self->fixed_dt);
		substep (self, self->fixed_dt);
		g_signal_emit (self, world_signals[SIGNAL_POST_SUBSTEP], 0, self->fixed_dt);
		self->accumulator -= self->fixed_dt;
		taken++;
	}
	if (self->accumulator >= self->fixed_dt)
	{
		self->dropped = self->accumulator;
		self->accumulator = 0.0f;
	}
	if (self->accumulator < 0.0f)
		self->accumulator = 0.0f;
	if (substeps != NULL)
		*substeps = taken;
	emit_events (self);
	return TRUE;
}

gfloat
lrg_dynamics_world_get_dropped_time (LrgDynamicsWorld *self)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), 0.0f);
	return self->dropped;
}

guint
lrg_dynamics_world_get_contact_count (LrgDynamicsWorld *self)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), 0);
	return self->touching->len;
}

static LrgDynManifold *
touching_manifold (LrgDynamicsWorld  *self,
                   guint              index,
                   GError           **error)
{
	guint mi;

	if (index >= self->touching->len)
	{
		dyn_error (error, LRG_DYNAMICS_ERROR_MISSING, "dynamics contact %u is outside the last substep", index);
		return NULL;
	}
	mi = g_array_index (self->touching, guint, index);
	return &g_array_index (self->manifolds, LrgDynManifold, mi);
}

gboolean
lrg_dynamics_world_get_contact (LrgDynamicsWorld        *self,
                                guint                    index,
                                LrgDynamicsContactKind  *kind,
                                guint                   *body_a,
                                guint                   *body_b,
                                gfloat                  *point,
                                gfloat                  *normal,
                                gfloat                  *depth,
                                GError                 **error)
{
	LrgDynManifold *m;
	LrgDynContactPoint *p;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (kind != NULL && body_a != NULL && body_b != NULL, FALSE);
	g_return_val_if_fail (point != NULL && normal != NULL && depth != NULL, FALSE);
	m = touching_manifold (self, index, error);
	if (m == NULL)
		return FALSE;
	p = &m->points[deepest_point (m)];
	*kind = (LrgDynamicsContactKind) m->kind;
	*body_a = body_id_of (self, m->a);
	*body_b = body_id_of (self, m->b);
	/* The 0.8 point: on the plane for plane pairs, on A otherwise; the
	 * midpoint for generic shape pairs. */
	if (m->kind == LRG_DYNAMICS_CONTACT_SPHERE_PLANE || m->kind == LRG_DYNAMICS_CONTACT_SPHYL_PLANE)
		copy3 (point, p->world_b);
	else if (m->kind == LRG_DYNAMICS_CONTACT_SHAPES)
	{
		point[0] = 0.5f * (p->world_a[0] + p->world_b[0]);
		point[1] = 0.5f * (p->world_a[1] + p->world_b[1]);
		point[2] = 0.5f * (p->world_a[2] + p->world_b[2]);
	}
	else
		copy3 (point, p->world_a);
	normal[0] = -m->normal[0];
	normal[1] = -m->normal[1];
	normal[2] = -m->normal[2];
	*depth = p->depth;
	return TRUE;
}

gboolean
lrg_dynamics_world_get_contact_info (LrgDynamicsWorld    *self,
                                     guint                index,
                                     LrgDynamicsContact  *out,
                                     GError             **error)
{
	LrgDynManifold *m;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);
	m = touching_manifold (self, index, error);
	if (m == NULL)
		return FALSE;
	manifold_to_contact (self, m, out);
	return TRUE;
}

gboolean
lrg_dynamics_world_get_contact_point (LrgDynamicsWorld *self,
                                      guint             index,
                                      guint             point_index,
                                      gfloat           *point_a,
                                      gfloat           *point_b,
                                      gfloat           *depth,
                                      gfloat           *impulse,
                                      GError          **error)
{
	LrgDynManifold *m;
	LrgDynContactPoint *p;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (point_a != NULL && point_b != NULL, FALSE);
	m = touching_manifold (self, index, error);
	if (m == NULL)
		return FALSE;
	if (point_index >= m->n_points)
		return dyn_error (error, LRG_DYNAMICS_ERROR_MISSING,
		                  "dynamics contact %u has %u points", index, m->n_points);
	p = &m->points[point_index];
	copy3 (point_a, p->world_a);
	copy3 (point_b, p->world_b);
	if (depth != NULL)
		*depth = p->depth;
	if (impulse != NULL)
		*impulse = p->normal_impulse;
	return TRUE;
}

guint
lrg_dynamics_world_get_manifold_count (LrgDynamicsWorld *self)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), 0);
	return self->manifolds->len;
}

guint
lrg_dynamics_world_get_active_body_count (LrgDynamicsWorld *self)
{
	guint i;
	guint n;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), 0);
	n = 0;
	for (i = 0; i < self->n_slots; i++)
		if (self->bodies[i].used && body_dynamic (&self->bodies[i]) && !self->bodies[i].sleeping)
			n++;
	return n;
}

guint
lrg_dynamics_world_get_island_count (LrgDynamicsWorld *self)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), 0);
	return self->n_islands;
}

guint
lrg_dynamics_world_get_evicted (LrgDynamicsWorld *self,
                                guint            *ids,
                                guint             max_ids)
{
	guint i;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), 0);
	g_return_val_if_fail (ids != NULL || max_ids == 0, 0);
	if (ids != NULL)
		for (i = 0; i < self->evicted->len && i < max_ids; i++)
			ids[i] = g_array_index (self->evicted, guint, i);
	return self->evicted->len;
}

gboolean
lrg_dynamics_world_write_bone_pose (LrgDynamicsWorld  *self,
                                    guint              body_id,
                                    const LrgBonePose *parent_world,
                                    LrgBonePose       *local,
                                    LrgBonePose       *world_pose,
                                    GError           **error)
{
	DynBody *body;
	LrgBonePose parent;
	LrgBonePose local_out;
	LrgBonePose world_out;
	gfloat parent_q[4];
	gfloat local_q[4];
	gfloat conj[4];
	gfloat length;
	gfloat origin[3];
	gfloat delta[3];
	gfloat local_pos[3];
	gfloat scale_x;
	gfloat scale_y;
	gfloat scale_z;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (local != NULL && world_pose != NULL, FALSE);
	body = alive_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	scale_x = local->scale_x;
	scale_y = local->scale_y;
	scale_z = local->scale_z;
	if (!finite1 (scale_x) || !finite1 (scale_y) || !finite1 (scale_z))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics bone scale is not finite");
	memset (&parent, 0, sizeof (parent));
	parent.rotation_w = 1.0f;
	parent.scale_x = 1.0f;
	parent.scale_y = 1.0f;
	parent.scale_z = 1.0f;
	if (parent_world != NULL)
		parent = *parent_world;
	if (!finite1 (parent.position_x) || !finite1 (parent.position_y) || !finite1 (parent.position_z) ||
	    !finite1 (parent.rotation_x) || !finite1 (parent.rotation_y) || !finite1 (parent.rotation_z) ||
	    !finite1 (parent.rotation_w) || !finite1 (parent.scale_x) || !finite1 (parent.scale_y) ||
	    !finite1 (parent.scale_z))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics parent bone pose is not finite");
	if (fabsf (parent.scale_x) < 1.0e-8f || fabsf (parent.scale_y) < 1.0e-8f || fabsf (parent.scale_z) < 1.0e-8f)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics parent bone scale is zero");
	parent_q[0] = parent.rotation_x;
	parent_q[1] = parent.rotation_y;
	parent_q[2] = parent.rotation_z;
	parent_q[3] = parent.rotation_w;
	length = sqrtf (parent_q[0] * parent_q[0] + parent_q[1] * parent_q[1] +
	                parent_q[2] * parent_q[2] + parent_q[3] * parent_q[3]);
	if (length <= 1.0e-8f)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics parent bone rotation has zero length");
	parent_q[0] /= length;
	parent_q[1] /= length;
	parent_q[2] /= length;
	parent_q[3] /= length;

	/* The body origin expressed in the parent frame. */
	body_origin (body, origin);
	delta[0] = origin[0] - parent.position_x;
	delta[1] = origin[1] - parent.position_y;
	delta[2] = origin[2] - parent.position_z;
	lrg_dyn_quat_inv_rotate (parent_q, delta, local_pos);
	local_pos[0] /= parent.scale_x;
	local_pos[1] /= parent.scale_y;
	local_pos[2] /= parent.scale_z;
	conj[0] = -parent_q[0];
	conj[1] = -parent_q[1];
	conj[2] = -parent_q[2];
	conj[3] = parent_q[3];
	lrg_dyn_quat_mul (conj, body->sb.orientation, local_q);
	lrg_dyn_quat_normalize (local_q);
	memset (&local_out, 0, sizeof (local_out));
	local_out.position_x = local_pos[0];
	local_out.position_y = local_pos[1];
	local_out.position_z = local_pos[2];
	local_out.rotation_x = local_q[0];
	local_out.rotation_y = local_q[1];
	local_out.rotation_z = local_q[2];
	local_out.rotation_w = local_q[3];
	local_out.scale_x = scale_x;
	local_out.scale_y = scale_y;
	local_out.scale_z = scale_z;
	/* Always the shipped compose, including a root against identity. */
	lrg_bone_pose_compose_into (&parent, &local_out, &world_out);
	*local = local_out;
	*world_pose = world_out;
	return TRUE;
}

/* ==========================================================================
 * Shape bodies (0.9 API)
 * ========================================================================== */

/* Mass, centre of mass and inertia from a shape, keeping the origin. */
static gboolean
apply_shape_mass (DynBody          *body,
                  LrgPhysicsShape  *shape,
                  gfloat            mass,
                  GError          **error)
{
	gfloat origin[3];
	gfloat unit_mass;
	gfloat com[3];
	gfloat inertia[9];
	gfloat rotated[9];
	gfloat rot[9];
	gfloat rt[9];
	gfloat tmp[9];
	guint k;

	body_origin (body, origin);
	memset (com, 0, sizeof (com));
	if (body->kind == LRG_DYNAMICS_BODY_DYNAMIC)
	{
		if (!lrg_physics_shape_get_mass_properties (shape, 1.0f, &unit_mass, com, inertia) ||
		    !(unit_mass > 0.0f))
			return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
			                  "dynamics shape has no volume; a dynamic body needs a sphere, capsule, "
			                  "cylinder, box, hull or compound");
		/* The shape may be turned inside the body (shape_rot): bring the
		 * centre of mass and the tensor into the body frame. */
		lrg_dyn_quat_rotate (body->shape_rot, com, com);
		lrg_dyn_quat_to_mat3 (body->shape_rot, rot);
		lrg_dyn_mat3_transpose (rot, rt);
		lrg_dyn_mat3_mul (rot, inertia, tmp);
		lrg_dyn_mat3_mul (tmp, rt, rotated);
		for (k = 0; k < 9u; k++)
			body->inertia_local[k] = rotated[k] * (mass / unit_mass);
		body->mass = mass;
	}
	copy3 (body->sb.com_local, com);
	set_origin (body, origin);
	body->inner_radius = lrg_physics_shape_get_inner_radius (shape);
	refresh_mass (body);
	return TRUE;
}

static gboolean
kind_shape_ok (LrgDynamicsBodyKind   kind,
               LrgPhysicsShape      *shape,
               GError              **error)
{
	LrgPhysicsShapeKind sk;

	sk = lrg_physics_shape_get_kind (shape);
	if (sk == LRG_PHYSICS_SHAPE_KIND_PLANE && kind != LRG_DYNAMICS_BODY_STATIC)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics plane bodies must be static");
	if ((sk == LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH || sk == LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD) &&
	    kind == LRG_DYNAMICS_BODY_DYNAMIC)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
		                  "dynamics mesh and heightfield bodies must be static or kinematic");
	return TRUE;
}

gboolean
lrg_dynamics_world_add_body (LrgDynamicsWorld     *self,
                             LrgPhysicsShape      *shape,
                             LrgDynamicsBodyKind   kind,
                             gfloat                mass,
                             const gfloat         *position,
                             const gfloat         *orientation,
                             guint                *body_id,
                             GError              **error)
{
	gfloat q[4];
	guint slot;
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (shape), FALSE);
	g_return_val_if_fail (position != NULL && body_id != NULL, FALSE);
	if (!mass_accepted (kind, kind == LRG_DYNAMICS_BODY_DYNAMIC ? mass : 0.0f, error))
		return FALSE;
	if (!finite3 (position))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics body position is not finite");
	if (!kind_shape_ok (kind, shape, error))
		return FALSE;
	if (!read_orientation (orientation, q, error) || !check_capacity (self, error))
		return FALSE;
	slot = alloc_slot (self);
	body = init_body (self, slot, kind, DYN_SHAPE_GENERIC, q);
	body->shape = g_object_ref (shape);
	body->is_plane = (lrg_physics_shape_get_kind (shape) == LRG_PHYSICS_SHAPE_KIND_PLANE);
	/* Shape bodies start with Jolt's defaults: a little damping and a
	 * material of their own (friction 0.5, no bounce); the 0.8 primitive
	 * calls keep zero damping and the world material. */
	body->linear_damping = 0.05f;
	body->angular_damping = 0.05f;
	body->friction = 0.5f;
	body->restitution = 0.0f;
	body->material_set = TRUE;
	copy3 (body->sb.position, position);
	if (!apply_shape_mass (body, shape, mass, error))
	{
		/* Undo the slot. */
		g_clear_object (&body->shape);
		body->used = FALSE;
		body->generation++;
		g_array_append_val (self->free_slots, slot);
		self->n_bodies--;
		return FALSE;
	}
	finish_body (self, body);
	*body_id = body_id_of (self, slot);
	return TRUE;
}

gboolean
lrg_dynamics_world_remove_body (LrgDynamicsWorld *self,
                                guint             body_id,
                                GError          **error)
{
	DynBody *body;
	guint slot;
	guint i;
	GHashTableIter iter;
	gpointer key;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	if (self->in_substep)
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics bodies cannot be removed during a substep");
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	slot = slot_index (self, body);

	/* Wake what rested on it, then forget its contacts (queueing the
	 * removed/exited signals for the next step). */
	wake_neighbours (self, slot);
	purge_slot (self, slot, TRUE);

	/* Constraints attached to it go with it. */
	for (i = self->constraints->len; i > 0; i--)
	{
		LrgPhysicsConstraint *c;

		c = g_ptr_array_index (self->constraints, i - 1u);
		if (lrg_physics_constraint_get_body_a (c) == body_id ||
		    lrg_physics_constraint_get_body_b (c) == body_id)
		{
			lrg_dyn_constraint_detach (c);
			g_ptr_array_remove_index (self->constraints, i - 1u);
		}
	}

	/* Per-pair filters naming it. */
	g_hash_table_iter_init (&iter, self->no_collide);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		guint64 k;

		k = *(guint64 *) key;
		if ((guint) (k >> 32) == body_id || (guint) (k & 0xffffffffu) == body_id)
			g_hash_table_iter_remove (&iter);
	}

	for (i = 0; i < self->planes->len; i++)
	{
		if (g_array_index (self->planes, guint, i) == slot)
		{
			g_array_remove_index (self->planes, i);
			break;
		}
	}
	release_body (self, body);
	body->used = FALSE;
	body->alive = FALSE;
	body->generation++;
	g_array_append_val (self->free_slots, slot);
	self->n_bodies--;
	return TRUE;
}

guint
lrg_dynamics_world_get_body_count (LrgDynamicsWorld *self)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), 0);
	return self->n_bodies;
}

GArray *
lrg_dynamics_world_get_body_ids (LrgDynamicsWorld *self)
{
	GArray *ids;
	guint i;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), NULL);
	ids = g_array_sized_new (FALSE, FALSE, sizeof (guint), self->n_bodies);
	for (i = 0; i < self->n_slots; i++)
	{
		guint id;

		if (!self->bodies[i].used)
			continue;
		id = body_id_of (self, i);
		g_array_append_val (ids, id);
	}
	return ids;
}

gboolean
lrg_dynamics_world_get_body_kind (LrgDynamicsWorld     *self,
                                  guint                 body_id,
                                  LrgDynamicsBodyKind  *kind,
                                  GError              **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (kind != NULL, FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	*kind = body->kind;
	return TRUE;
}

gboolean
lrg_dynamics_world_set_body_kind (LrgDynamicsWorld     *self,
                                  guint                 body_id,
                                  LrgDynamicsBodyKind   kind,
                                  gfloat                mass,
                                  GError              **error)
{
	DynBody *body;
	LrgDynamicsBodyKind before;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = alive_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (body->skind == DYN_SHAPE_INERTIAL)
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics contactless bodies stay dynamic");
	if (kind == LRG_DYNAMICS_BODY_DYNAMIC && mass == 0.0f)
		mass = body->mass;
	if (!mass_accepted (kind, kind == LRG_DYNAMICS_BODY_DYNAMIC ? mass : 0.0f, error))
		return FALSE;
	if (!kind_shape_ok (kind, body->shape, error))
		return FALSE;
	before = body->kind;
	body->kind = kind;
	if (body->skind == DYN_SHAPE_GENERIC)
	{
		if (!apply_shape_mass (body, body->shape, mass, error))
		{
			body->kind = before;
			return FALSE;
		}
	}
	else
	{
		store_isotropic (body, mass, body->radius);
		refresh_mass (body);
	}
	if (kind == LRG_DYNAMICS_BODY_STATIC)
	{
		memset (body->sb.linear_velocity, 0, sizeof (body->sb.linear_velocity));
		memset (body->sb.angular_velocity, 0, sizeof (body->sb.angular_velocity));
	}
	body->sleeping = FALSE;
	body->quiet = 0;
	sync_flags (self, body);
	wake_neighbours (self, slot_index (self, body));
	return TRUE;
}

LrgPhysicsShape *
lrg_dynamics_world_get_shape (LrgDynamicsWorld *self,
                              guint             body_id)
{
	guint slot;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), NULL);
	slot = slot_of (self, body_id);
	if (slot == DYN_NO_SLOT)
		return NULL;
	return self->bodies[slot].shape;
}

gboolean
lrg_dynamics_world_set_shape (LrgDynamicsWorld *self,
                              guint             body_id,
                              LrgPhysicsShape  *shape,
                              GError          **error)
{
	DynBody *body;
	LrgPhysicsShape *old;
	gfloat old_rot[4];
	gfloat old_com[3];
	DynShape old_kind;
	guint slot;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (shape), FALSE);
	if (self->in_substep)
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics shapes cannot change during a substep");
	body = alive_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (body->skind == DYN_SHAPE_INERTIAL)
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics contactless bodies have no shape");
	if (lrg_physics_shape_get_kind (shape) == LRG_PHYSICS_SHAPE_KIND_PLANE)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics shapes cannot change into a plane");
	if (!kind_shape_ok (body->kind, shape, error))
		return FALSE;
	slot = slot_index (self, body);
	old = body->shape;
	old_kind = body->skind;
	copy4 (old_rot, body->shape_rot);
	copy3 (old_com, body->sb.com_local);
	body->shape = g_object_ref (shape);
	body->skind = DYN_SHAPE_GENERIC;
	body->shape_rot[0] = 0.0f;
	body->shape_rot[1] = 0.0f;
	body->shape_rot[2] = 0.0f;
	body->shape_rot[3] = 1.0f;
	if (!apply_shape_mass (body, shape, body->mass, error))
	{
		g_object_unref (body->shape);
		body->shape = old;
		body->skind = old_kind;
		copy4 (body->shape_rot, old_rot);
		copy3 (body->sb.com_local, old_com);
		return FALSE;
	}
	g_clear_object (&old);
	if (body->debug_mesh != NULL)
		g_array_unref (body->debug_mesh);
	body->debug_mesh = NULL;
	purge_slot (self, slot, TRUE);
	drop_proxy (self, body);
	sync_proxy (self, body, 0.0f);
	wake_body (self, body);
	return TRUE;
}

gfloat
lrg_dynamics_world_get_mass (LrgDynamicsWorld *self,
                             guint             body_id)
{
	guint slot;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), 0.0f);
	slot = slot_of (self, body_id);
	if (slot == DYN_NO_SLOT || self->bodies[slot].kind != LRG_DYNAMICS_BODY_DYNAMIC)
		return 0.0f;
	return self->bodies[slot].mass;
}

gboolean
lrg_dynamics_world_set_mass (LrgDynamicsWorld *self,
                             guint             body_id,
                             gfloat            mass,
                             GError          **error)
{
	DynBody *body;
	gfloat scale;
	guint k;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = alive_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (body->kind != LRG_DYNAMICS_BODY_DYNAMIC || !(body->mass > 0.0f))
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics mass requires a dynamic body");
	if (!finite1 (mass) || !(mass > 0.0f))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics mass must be finite and positive");
	scale = mass / body->mass;
	for (k = 0; k < 9u; k++)
		body->inertia_local[k] *= scale;
	body->mass = mass;
	refresh_mass (body);
	return TRUE;
}

gboolean
lrg_dynamics_world_get_center_of_mass (LrgDynamicsWorld *self,
                                       guint             body_id,
                                       gfloat           *center,
                                       GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (center != NULL, FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	copy3 (center, body->sb.position);
	return TRUE;
}

gboolean
lrg_dynamics_world_set_body_layer (LrgDynamicsWorld *self,
                                   guint             body_id,
                                   guint             layer,
                                   guint32           mask,
                                   GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (layer >= LRG_PHYSICS_LAYERS_MAX)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics layer %u is not below %u",
		                  layer, LRG_PHYSICS_LAYERS_MAX);
	body->layer = layer;
	body->mask = mask;
	if (body->proxy != LRG_BROADPHASE_NULL_PROXY)
		lrg_broadphase_set_proxy_layer (self->broadphase, body->proxy, layer, mask);
	wake_body (self, body);
	return TRUE;
}

gboolean
lrg_dynamics_world_get_body_layer (LrgDynamicsWorld *self,
                                   guint             body_id,
                                   guint            *layer,
                                   guint32          *mask,
                                   GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (layer != NULL)
		*layer = body->layer;
	if (mask != NULL)
		*mask = body->mask;
	return TRUE;
}

void
lrg_dynamics_world_set_layers (LrgDynamicsWorld *self,
                               LrgPhysicsLayers *layers)
{
	g_return_if_fail (LRG_IS_DYNAMICS_WORLD (self));
	g_return_if_fail (layers == NULL || LRG_IS_PHYSICS_LAYERS (layers));
	g_set_object (&self->layers, layers);
	lrg_broadphase_set_layers (self->broadphase, layers);
}

LrgPhysicsLayers *
lrg_dynamics_world_get_layers (LrgDynamicsWorld *self)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), NULL);
	return self->layers;
}

/* the settings block and the world share one substep cap */
G_STATIC_ASSERT (LRG_PHYSICS_SETTINGS_MAX_SUBSTEPS == LRG_DYNAMICS_MAX_SUBSTEPS);

gboolean
lrg_dynamics_world_apply_settings (LrgDynamicsWorld          *self,
                                   const LrgPhysicsSettings  *settings,
                                   LrgPhysicsLayers          *layers,
                                   GError                   **error)
{
	LrgPhysicsSettings defaults;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (layers == NULL || LRG_IS_PHYSICS_LAYERS (layers), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (settings == NULL)
	{
		lrg_physics_settings_init_default (&defaults);
		settings = &defaults;
	}

	/* validate everything up front so a bad block changes nothing; the
	 * setters below then cannot fail */
	if (!lrg_physics_settings_validate (settings, error))
		return FALSE;
	copy3 (self->gravity, settings->gravity);
	self->fixed_dt = settings->fixed_dt;
	self->max_substeps = settings->max_substeps;
	lrg_contact_solver_set_velocity_iterations (self->solver, settings->iterations);
	self->sleep_speed2 = settings->sleep_threshold;
	self->sleep_after = settings->sleep_substeps;
	if (layers != NULL)
		lrg_dynamics_world_set_layers (self, layers);
	return TRUE;
}

void
lrg_dynamics_world_get_settings (LrgDynamicsWorld   *self,
                                 LrgPhysicsSettings *out_settings)
{
	g_return_if_fail (LRG_IS_DYNAMICS_WORLD (self));
	g_return_if_fail (out_settings != NULL);

	memset (out_settings, 0, sizeof (LrgPhysicsSettings));
	copy3 (out_settings->gravity, self->gravity);
	out_settings->fixed_dt = self->fixed_dt;
	out_settings->max_substeps = self->max_substeps;
	out_settings->iterations = lrg_contact_solver_get_velocity_iterations (self->solver);
	out_settings->sleep_threshold = self->sleep_speed2;
	out_settings->sleep_substeps = self->sleep_after;
}

gboolean
lrg_dynamics_world_set_collision_between (LrgDynamicsWorld *self,
                                          guint             body_a,
                                          guint             body_b,
                                          gboolean          collide,
                                          GError          **error)
{
	guint64 key;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	if (body_known (self, body_a, error) == NULL || body_known (self, body_b, error) == NULL)
		return FALSE;
	if (body_a == body_b)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics pair filter needs two different bodies");
	key = pair_key (body_a, body_b);
	if (collide)
		g_hash_table_remove (self->no_collide, &key);
	else
		g_hash_table_add (self->no_collide, g_memdup2 (&key, sizeof (key)));
	return TRUE;
}

gboolean
lrg_dynamics_world_set_body_material (LrgDynamicsWorld *self,
                                      guint             body_id,
                                      gfloat            friction,
                                      gfloat            restitution,
                                      GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (!finite1 (friction) || friction < 0.0f)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics friction must be zero or positive");
	if (!finite1 (restitution) || restitution < 0.0f || restitution > 1.0f)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics restitution must be in [0, 1]");
	body->friction = friction;
	body->restitution = restitution;
	body->material_set = TRUE;
	return TRUE;
}

gboolean
lrg_dynamics_world_get_body_material (LrgDynamicsWorld *self,
                                      guint             body_id,
                                      gfloat           *friction,
                                      gfloat           *restitution,
                                      GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (friction != NULL)
		*friction = body->material_set ? body->friction : self->friction;
	if (restitution != NULL)
		*restitution = body->material_set ? body->restitution : self->restitution;
	return TRUE;
}

gboolean
lrg_dynamics_world_set_damping (LrgDynamicsWorld *self,
                                guint             body_id,
                                gfloat            linear,
                                gfloat            angular,
                                GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (!finite1 (linear) || !finite1 (angular) || linear < 0.0f || angular < 0.0f)
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics damping must be finite and non-negative");
	body->linear_damping = linear;
	body->angular_damping = angular;
	return TRUE;
}

gboolean
lrg_dynamics_world_set_gravity_factor (LrgDynamicsWorld *self,
                                       guint             body_id,
                                       gfloat            factor,
                                       GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (!finite1 (factor))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics gravity factor is not finite");
	body->gravity_factor = factor;
	wake_body (self, body);
	return TRUE;
}

gboolean
lrg_dynamics_world_set_ccd (LrgDynamicsWorld *self,
                            guint             body_id,
                            gboolean          enabled,
                            GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (enabled && body->shape == NULL)
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics CCD needs a body with a shape");
	body->ccd = enabled ? TRUE : FALSE;
	return TRUE;
}

gboolean
lrg_dynamics_world_get_ccd (LrgDynamicsWorld *self,
                            guint             body_id)
{
	guint slot;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	slot = slot_of (self, body_id);
	return slot != DYN_NO_SLOT && self->bodies[slot].ccd;
}

gboolean
lrg_dynamics_world_set_sensor (LrgDynamicsWorld *self,
                               guint             body_id,
                               gboolean          sensor,
                               GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (body->shape == NULL || body->is_plane)
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics sensors need a non-plane shape");
	sensor = sensor ? TRUE : FALSE;
	if (body->sensor == sensor)
		return TRUE;
	if (self->in_substep)
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics sensor flag cannot change during a substep");
	purge_slot (self, slot_index (self, body), TRUE);
	body->sensor = sensor;
	wake_body (self, body);
	return TRUE;
}

gboolean
lrg_dynamics_world_get_sensor (LrgDynamicsWorld *self,
                               guint             body_id)
{
	guint slot;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	slot = slot_of (self, body_id);
	return slot != DYN_NO_SLOT && self->bodies[slot].sensor;
}

GArray *
lrg_dynamics_world_get_sensor_overlaps (LrgDynamicsWorld *self,
                                        guint             body_id)
{
	GArray *out;
	guint slot;
	guint i;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), NULL);
	out = g_array_new (FALSE, FALSE, sizeof (guint));
	slot = slot_of (self, body_id);
	if (slot == DYN_NO_SLOT)
		return out;
	for (i = 0; i < self->sensor_pairs->len; i++)
	{
		guint64 key;
		guint other;

		key = g_array_index (self->sensor_pairs, guint64, i);
		if ((guint) (key >> 32) != slot)
			continue;
		other = body_id_of (self, (guint) (key & 0xffffffffu));
		g_array_append_val (out, other);
	}
	return out;
}

void
lrg_dynamics_world_set_user_data (LrgDynamicsWorld *self,
                                  guint             body_id,
                                  guint64           user_data)
{
	guint slot;

	g_return_if_fail (LRG_IS_DYNAMICS_WORLD (self));
	slot = slot_of (self, body_id);
	if (slot != DYN_NO_SLOT)
		self->bodies[slot].user_data = user_data;
}

guint64
lrg_dynamics_world_get_user_data (LrgDynamicsWorld *self,
                                  guint             body_id)
{
	guint slot;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), 0);
	slot = slot_of (self, body_id);
	return (slot != DYN_NO_SLOT) ? self->bodies[slot].user_data : 0;
}

gboolean
lrg_dynamics_world_set_allow_sleep (LrgDynamicsWorld *self,
                                    guint             body_id,
                                    gboolean          allow,
                                    GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	body->allow_sleep = allow ? TRUE : FALSE;
	if (!allow)
		wake_body (self, body);
	return TRUE;
}

gboolean
lrg_dynamics_world_wake_body (LrgDynamicsWorld *self,
                              guint             body_id,
                              GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	wake_body (self, body);
	return TRUE;
}

gboolean
lrg_dynamics_world_move_kinematic (LrgDynamicsWorld *self,
                                   guint             body_id,
                                   const gfloat     *position,
                                   const gfloat     *orientation,
                                   gfloat            dt,
                                   GError          **error)
{
	DynBody *body;
	gfloat q[4];
	gfloat conj[4];
	gfloat dq[4];
	gfloat target_com[3];
	gfloat r[3];
	gfloat s;
	gfloat angle;
	guint k;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (position != NULL, FALSE);
	body = alive_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (body->kind != LRG_DYNAMICS_BODY_KINEMATIC || body->frozen)
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics move_kinematic requires a kinematic body");
	if (!finite3 (position) || !finite1 (dt) || !(dt > 0.0f))
		return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID, "dynamics kinematic target and dt must be finite, dt positive");
	if (orientation != NULL)
	{
		if (!read_orientation (orientation, q, error))
			return FALSE;
	}
	else
	{
		copy4 (q, body->sb.orientation);
	}

	/* Linear: the centre of mass at the target pose. */
	lrg_dyn_quat_rotate (q, body->sb.com_local, r);
	target_com[0] = position[0] + r[0];
	target_com[1] = position[1] + r[1];
	target_com[2] = position[2] + r[2];
	for (k = 0; k < 3u; k++)
		body->sb.linear_velocity[k] = (target_com[k] - body->sb.position[k]) / dt;

	/* Angular: the shortest rotation from the current to the target. */
	conj[0] = -body->sb.orientation[0];
	conj[1] = -body->sb.orientation[1];
	conj[2] = -body->sb.orientation[2];
	conj[3] = body->sb.orientation[3];
	lrg_dyn_quat_mul (q, conj, dq);
	if (dq[3] < 0.0f)
		for (k = 0; k < 4u; k++)
			dq[k] = -dq[k];
	s = sqrtf (dq[0] * dq[0] + dq[1] * dq[1] + dq[2] * dq[2]);
	if (s < 1.0e-7f)
	{
		memset (body->sb.angular_velocity, 0, sizeof (body->sb.angular_velocity));
	}
	else
	{
		angle = 2.0f * atan2f (s, dq[3]);
		for (k = 0; k < 3u; k++)
			body->sb.angular_velocity[k] = dq[k] / s * angle / dt;
	}
	return TRUE;
}

gboolean
lrg_dynamics_world_get_body_aabb (LrgDynamicsWorld *self,
                                  guint             body_id,
                                  gfloat           *minimum,
                                  gfloat           *maximum,
                                  GError          **error)
{
	DynBody *body;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (minimum != NULL && maximum != NULL, FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	if (!body_bounds (body, minimum, maximum))
		return dyn_error (error, LRG_DYNAMICS_ERROR_STATE, "dynamics body %u has no shape", body_id);
	return TRUE;
}

gboolean
lrg_dynamics_world_get_transform (LrgDynamicsWorld *self,
                                  guint             body_id,
                                  gfloat           *matrix,
                                  GError          **error)
{
	DynBody *body;
	gfloat origin[3];

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (matrix != NULL, FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	body_origin (body, origin);
	make_xf (body->sb.orientation, origin, matrix);
	return TRUE;
}

gfloat
lrg_dynamics_world_get_interpolation_alpha (LrgDynamicsWorld *self)
{
	gfloat alpha;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), 0.0f);
	alpha = self->accumulator / self->fixed_dt;
	return clampf (alpha, 0.0f, 0.9999f);
}

gboolean
lrg_dynamics_world_get_interpolated_pose (LrgDynamicsWorld *self,
                                          guint             body_id,
                                          gfloat           *position,
                                          gfloat           *orientation,
                                          GError          **error)
{
	DynBody *body;
	gfloat alpha;
	gfloat origin[3];
	gfloat d;
	guint k;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (position != NULL && orientation != NULL, FALSE);
	body = body_known (self, body_id, error);
	if (body == NULL)
		return FALSE;
	alpha = lrg_dynamics_world_get_interpolation_alpha (self);
	body_origin (body, origin);
	for (k = 0; k < 3u; k++)
		position[k] = body->prev_origin[k] + (origin[k] - body->prev_origin[k]) * alpha;
	d = body->prev_orientation[0] * body->sb.orientation[0] + body->prev_orientation[1] * body->sb.orientation[1] +
	    body->prev_orientation[2] * body->sb.orientation[2] + body->prev_orientation[3] * body->sb.orientation[3];
	for (k = 0; k < 4u; k++)
		orientation[k] = body->prev_orientation[k] * (1.0f - alpha) +
		                 body->sb.orientation[k] * ((d < 0.0f) ? -alpha : alpha);
	lrg_dyn_quat_normalize (orientation);
	return TRUE;
}

LrgContactSolver *
lrg_dynamics_world_get_contact_solver (LrgDynamicsWorld *self)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), NULL);
	return self->solver;
}

LrgBroadphase *
lrg_dynamics_world_get_broadphase (LrgDynamicsWorld *self)
{
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), NULL);
	return self->broadphase;
}

/* ==========================================================================
 * Queries
 * ========================================================================== */

static gboolean
query_body_ok (LrgDynamicsWorld *self,
               guint             slot,
               guint32           layer_mask,
               guint             ignore_slot,
               gboolean          sensors)
{
	DynBody *body;

	if (slot >= self->n_slots || slot == ignore_slot)
		return FALSE;
	body = &self->bodies[slot];
	if (!body->used || !body->alive || body->shape == NULL)
		return FALSE;
	if (body->sensor && !sensors)
		return FALSE;
	return (layer_mask & LRG_PHYSICS_LAYER_BIT (body->layer)) != 0u;
}

static guint
ignore_slot_of (LrgDynamicsWorld *self,
                guint             ignore_body)
{
	return (ignore_body == G_MAXUINT) ? DYN_NO_SLOT : slot_of (self, ignore_body);
}

typedef struct
{
	LrgDynamicsWorld *self;
	const gfloat     *origin;
	const gfloat     *direction;
	guint32           mask;
	guint             ignore;
	gboolean          all;
	gboolean          found;
	gfloat            best;
	LrgRayHit         hit;
	GArray           *hits;
} RayCtx;

static gfloat
ray_test (RayCtx *ctx,
          guint   slot,
          gfloat  max_fraction)
{
	DynBody *body;
	gfloat xf[16];
	LrgRayHit hit;

	if (!query_body_ok (ctx->self, slot, ctx->mask, ctx->ignore, FALSE))
		return max_fraction;
	body = &ctx->self->bodies[slot];
	shape_xf (body, xf);
	lrg_ray_hit_reset (&hit);
	if (!lrg_collide_ray (body->shape, xf, ctx->origin, ctx->direction, max_fraction, &hit))
		return max_fraction;
	hit.body = body_id_of (ctx->self, slot);
	if (ctx->all)
	{
		g_array_append_val (ctx->hits, hit);
		return max_fraction;
	}
	if (!ctx->found || hit.fraction < ctx->best)
	{
		ctx->found = TRUE;
		ctx->best = hit.fraction;
		ctx->hit = hit;
	}
	return ctx->best;
}

static gfloat
ray_candidate (guint    proxy,
               guint    user_id,
               gfloat   t_enter,
               gpointer data)
{
	RayCtx *ctx;

	(void) proxy;
	(void) t_enter;
	ctx = (RayCtx *) data;
	return ray_test (ctx, user_id, ctx->best);
}

static void
ray_planes (RayCtx *ctx)
{
	guint p;

	for (p = 0; p < ctx->self->planes->len; p++)
		ray_test (ctx, g_array_index (ctx->self->planes, guint, p), ctx->best);
}

gboolean
lrg_dynamics_world_raycast (LrgDynamicsWorld *self,
                            const gfloat     *origin,
                            const gfloat     *direction,
                            gfloat            max_fraction,
                            guint32           layer_mask,
                            guint             ignore_body,
                            LrgRayHit        *hit)
{
	RayCtx ctx;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (origin != NULL && direction != NULL, FALSE);
	if (!finite3 (origin) || !finite3 (direction) || !(max_fraction > 0.0f) || len3 (direction) <= 0.0f)
		return FALSE;
	memset (&ctx, 0, sizeof (ctx));
	ctx.self = self;
	ctx.origin = origin;
	ctx.direction = direction;
	ctx.mask = layer_mask;
	ctx.ignore = ignore_slot_of (self, ignore_body);
	ctx.best = max_fraction;
	lrg_broadphase_cast_ray (self->broadphase, origin, direction, max_fraction, layer_mask, ray_candidate, &ctx);
	ray_planes (&ctx);
	if (ctx.found && hit != NULL)
		*hit = ctx.hit;
	return ctx.found;
}

static gint
compare_hits (gconstpointer a,
              gconstpointer b)
{
	const LrgRayHit *x;
	const LrgRayHit *y;

	x = (const LrgRayHit *) a;
	y = (const LrgRayHit *) b;
	if (x->fraction < y->fraction)
		return -1;
	if (x->fraction > y->fraction)
		return 1;
	return (x->body < y->body) ? -1 : ((x->body > y->body) ? 1 : 0);
}

GArray *
lrg_dynamics_world_raycast_all (LrgDynamicsWorld *self,
                                const gfloat     *origin,
                                const gfloat     *direction,
                                gfloat            max_fraction,
                                guint32           layer_mask,
                                guint             ignore_body)
{
	RayCtx ctx;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), NULL);
	g_return_val_if_fail (origin != NULL && direction != NULL, NULL);
	memset (&ctx, 0, sizeof (ctx));
	ctx.hits = g_array_new (FALSE, FALSE, sizeof (LrgRayHit));
	if (!finite3 (origin) || !finite3 (direction) || !(max_fraction > 0.0f) || len3 (direction) <= 0.0f)
		return ctx.hits;
	ctx.self = self;
	ctx.origin = origin;
	ctx.direction = direction;
	ctx.mask = layer_mask;
	ctx.ignore = ignore_slot_of (self, ignore_body);
	ctx.best = max_fraction;
	ctx.all = TRUE;
	lrg_broadphase_cast_ray (self->broadphase, origin, direction, max_fraction, layer_mask, ray_candidate, &ctx);
	ray_planes (&ctx);
	g_array_sort (ctx.hits, compare_hits);
	return ctx.hits;
}

typedef struct
{
	LrgDynamicsWorld *self;
	LrgPhysicsShape  *shape;
	const gfloat     *xf_from;
	const gfloat     *xf_to;
	gfloat            scale;     /* max_fraction: toi * scale = t */
	guint32           mask;
	guint             ignore;
	gboolean          found;
	gfloat            best;      /* in t units */
	LrgRayHit         hit;
} CastCtx;

static void
cast_test (CastCtx *ctx,
           guint    slot)
{
	DynBody *body;
	gfloat xt[16];
	gfloat toi;
	LrgRayHit hit;

	if (!query_body_ok (ctx->self, slot, ctx->mask, ctx->ignore, FALSE))
		return;
	body = &ctx->self->bodies[slot];
	shape_xf (body, xt);
	toi = 1.0f;
	lrg_ray_hit_reset (&hit);
	if (!lrg_collide_cast (ctx->shape, ctx->xf_from, ctx->xf_to, body->shape, xt, &toi, &hit))
		return;
	if (ctx->found && toi * ctx->scale >= ctx->best)
		return;
	ctx->found = TRUE;
	ctx->best = toi * ctx->scale;
	hit.fraction = ctx->best;
	hit.body = body_id_of (ctx->self, slot);
	ctx->hit = hit;
}

static gfloat
cast_candidate (guint    proxy,
                guint    user_id,
                gfloat   t_enter,
                gpointer data)
{
	CastCtx *ctx;

	(void) proxy;
	ctx = (CastCtx *) data;
	if (ctx->found && t_enter > ctx->best)
		return ctx->best;
	cast_test (ctx, user_id);
	return ctx->found ? ctx->best : ctx->scale;
}

gboolean
lrg_dynamics_world_shape_cast (LrgDynamicsWorld *self,
                               LrgPhysicsShape  *shape,
                               const gfloat     *position,
                               const gfloat     *orientation,
                               const gfloat     *direction,
                               gfloat            max_fraction,
                               guint32           layer_mask,
                               guint             ignore_body,
                               LrgRayHit        *hit)
{
	CastCtx ctx;
	gfloat q[4];
	gfloat to[3];
	gfloat xf_from[16];
	gfloat xf_to[16];
	gfloat mn[3];
	gfloat mx[3];
	guint p;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (shape), FALSE);
	g_return_val_if_fail (position != NULL && direction != NULL, FALSE);
	if (!finite3 (position) || !finite3 (direction) || !(max_fraction > 0.0f))
		return FALSE;
	if (!read_orientation (orientation, q, NULL))
		return FALSE;
	to[0] = position[0] + direction[0] * max_fraction;
	to[1] = position[1] + direction[1] * max_fraction;
	to[2] = position[2] + direction[2] * max_fraction;
	make_xf (q, position, xf_from);
	make_xf (q, to, xf_to);
	lrg_physics_shape_get_world_bounds (shape, xf_from, mn, mx);
	memset (&ctx, 0, sizeof (ctx));
	ctx.self = self;
	ctx.shape = shape;
	ctx.xf_from = xf_from;
	ctx.xf_to = xf_to;
	ctx.scale = max_fraction;
	ctx.mask = layer_mask;
	ctx.ignore = ignore_slot_of (self, ignore_body);
	ctx.best = max_fraction;
	lrg_broadphase_cast_aabb (self->broadphase, mn, mx, direction, max_fraction, layer_mask, cast_candidate, &ctx);
	for (p = 0; p < self->planes->len; p++)
		cast_test (&ctx, g_array_index (self->planes, guint, p));
	if (ctx.found && hit != NULL)
		*hit = ctx.hit;
	return ctx.found;
}

typedef struct
{
	LrgDynamicsWorld *self;
	GArray           *slots;
} CollectCtx;

static gboolean
collect_slot (guint    proxy,
              guint    user_id,
              gpointer data)
{
	CollectCtx *ctx;

	(void) proxy;
	ctx = (CollectCtx *) data;
	g_array_append_val (ctx->slots, user_id);
	return TRUE;
}

static gint
compare_uint (gconstpointer a,
              gconstpointer b)
{
	guint x;
	guint y;

	x = *(const guint *) a;
	y = *(const guint *) b;
	return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

GArray *
lrg_dynamics_world_overlap_shape (LrgDynamicsWorld *self,
                                  LrgPhysicsShape  *shape,
                                  const gfloat     *position,
                                  const gfloat     *orientation,
                                  guint32           layer_mask,
                                  guint             ignore_body)
{
	CollectCtx ctx;
	GArray *out;
	gfloat q[4];
	gfloat xf[16];
	gfloat mn[3];
	gfloat mx[3];
	guint ignore;
	guint i;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), NULL);
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (shape), NULL);
	g_return_val_if_fail (position != NULL, NULL);
	out = g_array_new (FALSE, FALSE, sizeof (guint));
	if (!finite3 (position) || !read_orientation (orientation, q, NULL))
		return out;
	make_xf (q, position, xf);
	lrg_physics_shape_get_world_bounds (shape, xf, mn, mx);
	ctx.self = self;
	ctx.slots = g_array_new (FALSE, FALSE, sizeof (guint));
	lrg_broadphase_query_aabb (self->broadphase, mn, mx, layer_mask, collect_slot, &ctx);
	for (i = 0; i < self->planes->len; i++)
		g_array_append_val (ctx.slots, g_array_index (self->planes, guint, i));
	g_array_sort (ctx.slots, compare_uint);
	ignore = ignore_slot_of (self, ignore_body);
	for (i = 0; i < ctx.slots->len; i++)
	{
		guint slot;
		gfloat xb[16];
		guint id;

		slot = g_array_index (ctx.slots, guint, i);
		if (i > 0 && slot == g_array_index (ctx.slots, guint, i - 1u))
			continue;
		if (!query_body_ok (self, slot, layer_mask, ignore, FALSE))
			continue;
		shape_xf (&self->bodies[slot], xb);
		if (!lrg_collide_overlap (shape, xf, self->bodies[slot].shape, xb))
			continue;
		id = body_id_of (self, slot);
		g_array_append_val (out, id);
	}
	g_array_unref (ctx.slots);
	return out;
}

GArray *
lrg_dynamics_world_overlap_aabb (LrgDynamicsWorld *self,
                                 const gfloat     *minimum,
                                 const gfloat     *maximum,
                                 guint32           layer_mask)
{
	CollectCtx ctx;
	GArray *out;
	guint i;

	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), NULL);
	g_return_val_if_fail (minimum != NULL && maximum != NULL, NULL);
	out = g_array_new (FALSE, FALSE, sizeof (guint));
	ctx.self = self;
	ctx.slots = g_array_new (FALSE, FALSE, sizeof (guint));
	lrg_broadphase_query_aabb (self->broadphase, minimum, maximum, layer_mask, collect_slot, &ctx);
	g_array_sort (ctx.slots, compare_uint);
	for (i = 0; i < ctx.slots->len; i++)
	{
		guint slot;
		guint id;

		slot = g_array_index (ctx.slots, guint, i);
		if (!query_body_ok (self, slot, layer_mask, DYN_NO_SLOT, TRUE))
			continue;
		id = body_id_of (self, slot);
		g_array_append_val (out, id);
	}
	g_array_unref (ctx.slots);
	return out;
}

/* ==========================================================================
 * Debug drawing
 * ========================================================================== */

static GrlColor
rgba (guint8 r,
      guint8 g,
      guint8 b,
      guint8 a)
{
	GrlColor c;

	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	return c;
}

static GrlVector3
v3 (const gfloat *p)
{
	GrlVector3 v;

	v.x = p[0];
	v.y = p[1];
	v.z = p[2];
	return v;
}

static void
xf_point (const gfloat *m,
          const gfloat *p,
          gfloat       *out)
{
	out[0] = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
	out[1] = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
	out[2] = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
}

static GrlColor
body_color (const DynBody *body,
            gboolean       dim_sleep)
{
	GrlColor c;

	if (body->sensor)
		c = rgba (240, 220, 60, 255);
	else if (body->kind == LRG_DYNAMICS_BODY_STATIC)
		c = rgba (150, 150, 150, 255);
	else if (body->kind == LRG_DYNAMICS_BODY_KINEMATIC)
		c = rgba (80, 140, 255, 255);
	else if (body->frozen)
		c = rgba (160, 120, 220, 255);
	else
		c = rgba (80, 220, 110, 255);
	if (dim_sleep && body->sleeping)
	{
		c.r = (guint8) (c.r / 3u);
		c.g = (guint8) (c.g / 3u);
		c.b = (guint8) (c.b / 3u);
	}
	return c;
}

static void
draw_shape (DynBody      *body,
            LrgDebugDraw *draw,
            GrlColor      color)
{
	gfloat m[16];
	gfloat a[3];
	gfloat b[3];
	gfloat local[3];
	GrlVector3 va;
	GrlVector3 vb;
	LrgPhysicsShapeKind kind;
	gfloat h;

	shape_xf (body, m);
	kind = lrg_physics_shape_get_kind (body->shape);
	switch (kind)
	{
	case LRG_PHYSICS_SHAPE_KIND_SPHERE:
		va = v3 (&m[12]);
		lrg_debug_draw_sphere (draw, &va, lrg_physics_shape_get_radius (body->shape), &color, 0.0f);
		return;
	case LRG_PHYSICS_SHAPE_KIND_CAPSULE:
	case LRG_PHYSICS_SHAPE_KIND_CYLINDER:
		h = lrg_physics_shape_get_half_height (body->shape);
		local[0] = 0.0f;
		local[1] = -h;
		local[2] = 0.0f;
		xf_point (m, local, a);
		local[1] = h;
		xf_point (m, local, b);
		va = v3 (a);
		vb = v3 (b);
		if (kind == LRG_PHYSICS_SHAPE_KIND_CAPSULE)
			lrg_debug_draw_capsule (draw, &va, &vb, lrg_physics_shape_get_radius (body->shape), &color, 0.0f);
		else
			lrg_debug_draw_cylinder (draw, &va, &vb, lrg_physics_shape_get_radius (body->shape), &color, 0.0f);
		return;
	case LRG_PHYSICS_SHAPE_KIND_BOX:
		{
			gfloat he[3];
			GrlVector3 vh;

			lrg_physics_shape_get_half_extents (body->shape, he);
			vh = v3 (he);
			lrg_debug_draw_oriented_box (draw, m, &vh, &color, 0.0f);
		}
		return;
	case LRG_PHYSICS_SHAPE_KIND_PLANE:
		{
			gfloat n[3];
			gfloat off;
			gfloat c[3];
			GrlVector3 vn;

			if (!plane_world (body, n, &off))
				return;
			c[0] = n[0] * off;
			c[1] = n[1] * off;
			c[2] = n[2] * off;
			va = v3 (c);
			vn = v3 (n);
			lrg_debug_draw_circle (draw, &va, &vn, 10.0f, &color, 0.0f);
			b[0] = c[0] + n[0];
			b[1] = c[1] + n[1];
			b[2] = c[2] + n[2];
			vb = v3 (b);
			lrg_debug_draw_arrow (draw, &va, &vb, 0.0f, &color, 0.0f);
		}
		return;
	default:
		break;
	}

	/* Hulls, meshes, heightfields, compounds: the cached triangle soup. */
	if (body->debug_mesh == NULL)
		body->debug_mesh = lrg_physics_shape_build_debug_mesh (body->shape, 12);
	if (body->debug_mesh != NULL)
	{
		guint t;
		guint n_tris;
		const gfloat *tri;

		n_tris = body->debug_mesh->len / 9u;
		tri = (const gfloat *) body->debug_mesh->data;
		for (t = 0; t < n_tris; t++)
		{
			GrlVector3 pts[3];
			guint k;

			for (k = 0; k < 3u; k++)
			{
				xf_point (m, &tri[t * 9u + k * 3u], a);
				pts[k] = v3 (a);
			}
			lrg_debug_draw_polyline (draw, pts, 3, TRUE, &color, 0.0f);
		}
	}
}

typedef struct
{
	LrgDebugDraw *draw;
} NodeCtx;

static void
draw_node (guint         depth,
           const gfloat *min,
           const gfloat *max,
           guint         proxy,
           gpointer      data)
{
	NodeCtx *ctx;
	GrlVector3 a;
	GrlVector3 b;
	GrlColor c;

	ctx = (NodeCtx *) data;
	a = v3 (min);
	b = v3 (max);
	if (proxy != LRG_BROADPHASE_NULL_PROXY)
		c = rgba (120, 200, 255, 160);
	else
		c = rgba ((guint8) MIN (255u, 80u + depth * 20u), 80, 200, 90);
	lrg_debug_draw_box (ctx->draw, &a, &b, &c, 0.0f);
}

void
lrg_dynamics_world_debug_draw (LrgDynamicsWorld      *self,
                               LrgDebugDraw          *draw,
                               LrgDynamicsDebugFlags  flags)
{
	guint i;

	g_return_if_fail (LRG_IS_DYNAMICS_WORLD (self));
	g_return_if_fail (draw != NULL);
	if (flags == LRG_DYNAMICS_DEBUG_NONE)
		return;
	lrg_debug_draw_push (draw);
	lrg_debug_draw_set_category (draw, LRG_DEBUG_CATEGORY_PHYSICS);

	for (i = 0; i < self->n_slots; i++)
	{
		DynBody *body;
		GrlColor color;

		body = &self->bodies[i];
		if (!body->used || !body->alive)
			continue;
		color = body_color (body, (flags & LRG_DYNAMICS_DEBUG_SLEEPING) != 0);
		if ((flags & LRG_DYNAMICS_DEBUG_SHAPES) && body->shape != NULL)
			draw_shape (body, draw, color);
		if ((flags & LRG_DYNAMICS_DEBUG_SLEEPING) && !(flags & LRG_DYNAMICS_DEBUG_SHAPES) && body->sleeping)
		{
			GrlVector3 c;
			GrlColor grey;

			c = v3 (body->sb.position);
			grey = rgba (120, 120, 120, 255);
			lrg_debug_draw_point (draw, &c, 0.1f, &grey, 0.0f);
		}
		if ((flags & LRG_DYNAMICS_DEBUG_AABBS) && body->shape != NULL && !body->is_plane)
		{
			gfloat mn[3];
			gfloat mx[3];

			if (body_bounds (body, mn, mx))
			{
				GrlVector3 a;
				GrlVector3 b;
				GrlColor c;

				a = v3 (mn);
				b = v3 (mx);
				c = rgba (255, 255, 255, 120);
				lrg_debug_draw_box (draw, &a, &b, &c, 0.0f);
			}
		}
		if (flags & LRG_DYNAMICS_DEBUG_CENTER_OF_MASS)
		{
			gfloat m[16];

			make_xf (body->sb.orientation, body->sb.position, m);
			lrg_debug_draw_axes (draw, m, 0.25f, 0.0f);
		}
		if ((flags & LRG_DYNAMICS_DEBUG_VELOCITY) && body->kind != LRG_DYNAMICS_BODY_STATIC &&
		    dot3 (body->sb.linear_velocity, body->sb.linear_velocity) > 1.0e-6f)
		{
			gfloat tip[3];
			GrlVector3 a;
			GrlVector3 b;
			GrlColor c;

			tip[0] = body->sb.position[0] + body->sb.linear_velocity[0] * 0.25f;
			tip[1] = body->sb.position[1] + body->sb.linear_velocity[1] * 0.25f;
			tip[2] = body->sb.position[2] + body->sb.linear_velocity[2] * 0.25f;
			a = v3 (body->sb.position);
			b = v3 (tip);
			c = rgba (255, 255, 0, 255);
			lrg_debug_draw_arrow (draw, &a, &b, 0.0f, &c, 0.0f);
		}
	}

	if (flags & LRG_DYNAMICS_DEBUG_CONTACTS)
	{
		for (i = 0; i < self->manifolds->len; i++)
		{
			LrgDynManifold *m;
			guint k;

			m = &g_array_index (self->manifolds, LrgDynManifold, i);
			for (k = 0; k < m->n_points; k++)
			{
				LrgDynContactPoint *p;
				gfloat mid[3];
				gfloat tip[3];
				GrlVector3 a;
				GrlVector3 b;
				GrlColor c;

				p = &m->points[k];
				mid[0] = 0.5f * (p->world_a[0] + p->world_b[0]);
				mid[1] = 0.5f * (p->world_a[1] + p->world_b[1]);
				mid[2] = 0.5f * (p->world_a[2] + p->world_b[2]);
				tip[0] = mid[0] - m->normal[0] * 0.2f;
				tip[1] = mid[1] - m->normal[1] * 0.2f;
				tip[2] = mid[2] - m->normal[2] * 0.2f;
				c = (p->depth > 0.0f) ? rgba (255, 60, 60, 255) : rgba (255, 160, 40, 255);
				a = v3 (mid);
				b = v3 (tip);
				lrg_debug_draw_point (draw, &a, 0.05f, &c, 0.0f);
				lrg_debug_draw_arrow (draw, &a, &b, 0.05f, &c, 0.0f);
			}
		}
	}

	if (flags & LRG_DYNAMICS_DEBUG_CONSTRAINTS)
	{
		for (i = 0; i < self->constraints->len; i++)
		{
			LrgPhysicsConstraint *c;
			LrgPhysicsConstraintClass *klass;
			LrgSolverBody *a;
			LrgSolverBody *b;
			guint sa;
			guint sb;

			c = g_ptr_array_index (self->constraints, i);
			klass = LRG_PHYSICS_CONSTRAINT_GET_CLASS (c);
			if (klass->debug_draw == NULL || !constraint_bodies (self, c, &a, &b, &sa, &sb))
				continue;
			klass->debug_draw (c, a, b, draw);
		}
	}

	if (flags & LRG_DYNAMICS_DEBUG_BROADPHASE)
	{
		NodeCtx ctx;

		ctx.draw = draw;
		lrg_broadphase_foreach_node (self->broadphase, draw_node, &ctx);
	}
	lrg_debug_draw_pop (draw);
}
