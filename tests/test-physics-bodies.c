/* test-physics-bodies.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Physics in the 3D runtime (0.9.0 M5, BODIES): LrgRigidBodyComponent,
 * LrgColliderComponent3D and LrgJointComponent on LrgEntity3D, the
 * LrgWorld3D physics hook (fixed-step stepping, transform sync both ways,
 * interpolation), the node `physics:` block (.rlevel round trip,
 * instantiation, prefabs), the character controller against a
 * LrgDynamicsWorld, and the 2D debug draw.  Headless: physics is CPU only.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define STEP (1.0f / 60.0f)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* A world with physics (gravity -9.81 Y, default layers). */
static LrgWorld3D *
physics_world (void)
{
	LrgWorld3D *world = lrg_world3d_new ();

	lrg_world3d_ensure_physics (world);
	return world;
}

/* An entity at (x, y, z) added under the world root. */
static LrgEntity3D *
add_entity (LrgWorld3D  *world,
            const gchar *name,
            gfloat       x,
            gfloat       y,
            gfloat       z)
{
	g_autoptr(LrgEntity3D) e = lrg_entity3d_new_with_name (name);

	lrg_entity3d_set_position_xyz (e, x, y, z);
	lrg_world3d_add_entity (world, e);
	return e;   /* the world holds it */
}

/* A static floor: a 20 x 1 x 20 box collider whose top is at y = 0. */
static LrgEntity3D *
add_floor (LrgWorld3D *world)
{
	LrgEntity3D                      *floor = add_entity (world, "floor", 0.0f, -0.5f, 0.0f);
	g_autoptr(LrgColliderComponent3D) box = lrg_collider_component3d_new_box (10.0f, 0.5f, 10.0f);

	lrg_entity3d_add_component (floor, LRG_COMPONENT (box));
	return floor;
}

/* A body with one collider (@collider: transfer full, nullable). */
static LrgRigidBodyComponent *
add_body (LrgEntity3D            *entity,
          LrgDynamicsBodyKind     kind,
          LrgColliderComponent3D *collider)
{
	g_autoptr(LrgRigidBodyComponent) body = lrg_rigid_body_component_new (kind);

	lrg_entity3d_add_component (entity, LRG_COMPONENT (body));
	if (collider != NULL)
	{
		lrg_entity3d_add_component (entity, LRG_COMPONENT (collider));
		g_object_unref (collider);   /* transfer full from the caller */
	}
	return body;   /* the entity holds it */
}

/* Runs @seconds of fixed updates through lrg_world3d_step(). */
static void
run (LrgWorld3D *world,
     gfloat      seconds)
{
	guint n = (guint) ceilf (seconds / STEP);
	guint i;

	for (i = 0; i < n; i++)
		lrg_world3d_step (world, STEP);
}

static gfloat
entity_y (LrgEntity3D *e)
{
	gfloat p[3];

	lrg_entity3d_get_world_position (e, p);
	return p[1];
}

/* ==========================================================================
 * Components create bodies with the right shape, mass and layer
 * ========================================================================== */

static void
test_body_creation (void)
{
	g_autoptr(LrgWorld3D)  world = physics_world ();
	LrgDynamicsWorld      *physics = lrg_world3d_get_physics (world);
	LrgEntity3D           *crate = add_entity (world, "crate", 0.0f, 3.0f, 0.0f);
	LrgRigidBodyComponent *body;
	LrgPhysicsShape       *shape;
	gfloat                 he[3];
	guint                  id;
	guint                  layer;
	gint                   debris;
	LrgDynamicsBodyKind    kind;

	body = add_body (crate, LRG_DYNAMICS_BODY_DYNAMIC, lrg_collider_component3d_new_box (0.5f, 0.25f, 1.0f));
	lrg_rigid_body_component_set_mass (body, 2.5f);
	lrg_rigid_body_component_set_layer (body, "debris");

	/* bodies are built lazily in the first fixed update */
	g_assert_cmpuint (lrg_rigid_body_component_get_body_id (body), ==, LRG_PHYSICS_NO_BODY);
	lrg_world3d_step (world, STEP);
	id = lrg_rigid_body_component_get_body_id (body);
	g_assert_cmpuint (id, !=, LRG_PHYSICS_NO_BODY);
	g_assert_true (lrg_world3d_find_body_entity (world, id) == crate);

	g_assert_true (lrg_dynamics_world_get_body_kind (physics, id, &kind, NULL));
	g_assert_cmpint (kind, ==, LRG_DYNAMICS_BODY_DYNAMIC);
	g_assert_cmpfloat_with_epsilon (lrg_dynamics_world_get_mass (physics, id), 2.5f, 1e-5f);
	shape = lrg_dynamics_world_get_shape (physics, id);
	g_assert_nonnull (shape);
	g_assert_cmpint (lrg_physics_shape_get_kind (shape), ==, LRG_PHYSICS_SHAPE_KIND_BOX);
	lrg_physics_shape_get_half_extents (shape, he);
	g_assert_cmpfloat_with_epsilon (he[0], 0.5f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (he[1], 0.25f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (he[2], 1.0f, 1e-5f);

	debris = lrg_physics_layers_find (lrg_dynamics_world_get_layers (physics), "debris");
	g_assert_cmpint (debris, >, 0);
	g_assert_true (lrg_dynamics_world_get_body_layer (physics, id, &layer, NULL, NULL));
	g_assert_cmpuint (layer, ==, (guint) debris);

	/* two colliders with offsets make a compound; scale is baked in */
	{
		LrgEntity3D                      *dumbbell = add_entity (world, "dumbbell", 4.0f, 3.0f, 0.0f);
		g_autoptr(LrgColliderComponent3D) left = lrg_collider_component3d_new_sphere (0.5f);
		g_autoptr(LrgColliderComponent3D) right = lrg_collider_component3d_new_sphere (0.5f);
		LrgRigidBodyComponent            *b2;

		lrg_collider_component3d_set_offset (left, -1.0f, 0.0f, 0.0f);
		lrg_collider_component3d_set_offset (right, 1.0f, 0.0f, 0.0f);
		lrg_entity3d_set_scale_xyz (dumbbell, 2.0f, 2.0f, 2.0f);
		b2 = add_body (dumbbell, LRG_DYNAMICS_BODY_DYNAMIC, NULL);
		lrg_entity3d_add_component (dumbbell, LRG_COMPONENT (left));
		lrg_entity3d_add_component (dumbbell, LRG_COMPONENT (right));
		lrg_world3d_step (world, STEP);
		shape = lrg_dynamics_world_get_shape (physics, lrg_rigid_body_component_get_body_id (b2));
		g_assert_nonnull (shape);
		g_assert_cmpint (lrg_physics_shape_get_kind (shape), ==, LRG_PHYSICS_SHAPE_KIND_COMPOUND);
		g_assert_cmpuint (lrg_physics_shape_get_child_count (shape), ==, 2);
	}

	/* removing the component removes the body */
	lrg_entity3d_remove_component (crate, LRG_COMPONENT (body));
	g_assert_null (lrg_dynamics_world_get_shape (physics, id));
	g_assert_null (lrg_world3d_find_body_entity (world, id));
}

/* ==========================================================================
 * Transform sync: a falling box reaches the floor; teleports; interpolation
 * ========================================================================== */

static void
test_falling_box (void)
{
	g_autoptr(LrgWorld3D)  world = physics_world ();
	LrgEntity3D           *box;
	LrgRigidBodyComponent *body;
	gfloat                 v[3];

	add_floor (world);
	box = add_entity (world, "box", 0.0f, 5.0f, 0.0f);
	body = add_body (box, LRG_DYNAMICS_BODY_DYNAMIC, lrg_collider_component3d_new_box (0.5f, 0.5f, 0.5f));
	lrg_world3d_set_physics_interpolation (world, FALSE);

	/* free fall for 0.5 s: y = 5 - g t^2 / 2 ~ 3.77 */
	run (world, 0.5f);
	g_assert_cmpfloat (entity_y (box), <, 4.0f);
	g_assert_cmpfloat (entity_y (box), >, 3.5f);

	/* lands and rests with its centre half a unit above the floor */
	run (world, 3.0f);
	g_assert_cmpfloat_with_epsilon (entity_y (box), 0.5f, 0.05f);
	lrg_rigid_body_component_get_linear_velocity (body, v);
	g_assert_cmpfloat (fabsf (v[1]), <, 0.1f);

	/* game code moving the entity teleports the body */
	lrg_entity3d_set_position_xyz (box, 3.0f, 2.0f, 0.0f);
	lrg_world3d_step (world, STEP);
	{
		gfloat p[3];

		g_assert_true (lrg_dynamics_world_get_position (lrg_world3d_get_physics (world),
		                                                lrg_rigid_body_component_get_body_id (body), p, NULL));
		g_assert_cmpfloat_with_epsilon (p[0], 3.0f, 0.01f);
		g_assert_cmpfloat (p[1], >, 1.8f);
	}

	/* velocity given before the body exists is applied on creation */
	{
		LrgEntity3D           *ball = add_entity (world, "ball", -3.0f, 1.0f, 0.0f);
		LrgRigidBodyComponent *bb = add_body (ball, LRG_DYNAMICS_BODY_DYNAMIC,
		                                      lrg_collider_component3d_new_sphere (0.25f));
		const gfloat           up[3] = { 0.0f, 5.0f, 0.0f };

		lrg_rigid_body_component_set_linear_velocity (bb, up);
		run (world, 0.2f);
		g_assert_cmpfloat (entity_y (ball), >, 1.5f);
	}
}

static void
test_interpolation (void)
{
	g_autoptr(LrgWorld3D)  world = physics_world ();
	LrgEntity3D           *ball = add_entity (world, "ball", 0.0f, 10.0f, 0.0f);
	LrgRigidBodyComponent *body;
	gfloat                 y_full;
	gfloat                 y_half;
	gfloat                 p[3];

	body = add_body (ball, LRG_DYNAMICS_BODY_DYNAMIC, lrg_collider_component3d_new_sphere (0.5f));
	run (world, 0.5f);

	/* half a step later the entity sits between the last two states */
	lrg_world3d_step (world, STEP * 0.5f);
	y_half = entity_y (ball);
	g_assert_true (lrg_dynamics_world_get_position (lrg_world3d_get_physics (world),
	                                                lrg_rigid_body_component_get_body_id (body), p, NULL));
	y_full = p[1];
	g_assert_cmpfloat (y_half, >, y_full);   /* falling: the blend lags behind */

	/* without interpolation the entity is the physics pose */
	lrg_rigid_body_component_set_interpolate (body, FALSE);
	lrg_world3d_step (world, STEP * 0.5f);
	g_assert_true (lrg_dynamics_world_get_position (lrg_world3d_get_physics (world),
	                                                lrg_rigid_body_component_get_body_id (body), p, NULL));
	g_assert_cmpfloat_with_epsilon (entity_y (ball), p[1], 1e-4f);
}

/* ==========================================================================
 * A kinematic entity moves its body and pushes a dynamic one
 * ========================================================================== */

static void
test_kinematic (void)
{
	g_autoptr(LrgWorld3D)  world = physics_world ();
	LrgEntity3D           *pusher = add_entity (world, "pusher", -3.0f, 0.5f, 0.0f);
	LrgEntity3D           *crate = add_entity (world, "crate", 0.0f, 0.5f, 0.0f);
	LrgRigidBodyComponent *kb;
	gfloat                 p[3];
	guint                  i;

	add_floor (world);
	kb = add_body (pusher, LRG_DYNAMICS_BODY_KINEMATIC, lrg_collider_component3d_new_box (0.5f, 0.5f, 0.5f));
	add_body (crate, LRG_DYNAMICS_BODY_DYNAMIC, lrg_collider_component3d_new_box (0.5f, 0.5f, 0.5f));
	lrg_world3d_step (world, STEP);

	/* the game moves the kinematic entity 2 m/s along +X for 2 s */
	for (i = 0; i < 120; i++)
	{
		lrg_entity3d_set_position_xyz (pusher, -3.0f + 2.0f * (gfloat) (i + 1) * STEP, 0.5f, 0.0f);
		lrg_world3d_step (world, STEP);
	}
	g_assert_true (lrg_dynamics_world_get_position (lrg_world3d_get_physics (world),
	                                                lrg_rigid_body_component_get_body_id (kb), p, NULL));
	g_assert_cmpfloat_with_epsilon (p[0], 1.0f, 0.05f);   /* the body followed the entity */

	/* and the crate in its way was pushed ahead of it */
	lrg_entity3d_get_world_position (crate, p);
	g_assert_cmpfloat (p[0], >, 1.5f);
}

/* ==========================================================================
 * Trigger colliders fire component signals; contact signals
 * ========================================================================== */

typedef struct
{
	guint        entered;
	guint        exited;
	LrgEntity3D *last;
} Counts;

static void
on_entered (LrgColliderComponent3D *trigger,
            LrgEntity3D            *other,
            gpointer                data)
{
	Counts *c = data;

	c->entered++;
	c->last = other;
}

static void
on_exited (LrgColliderComponent3D *trigger,
           LrgEntity3D            *other,
           gpointer                data)
{
	((Counts *) data)->exited++;
}

static void
test_triggers (void)
{
	g_autoptr(LrgWorld3D)             world = physics_world ();
	LrgEntity3D                      *zone = add_entity (world, "zone", 0.0f, 2.0f, 0.0f);
	LrgEntity3D                      *ball = add_entity (world, "ball", 0.0f, 6.0f, 0.0f);
	g_autoptr(LrgColliderComponent3D) trigger = lrg_collider_component3d_new_box (2.0f, 0.5f, 2.0f);
	Counts                            counts = { 0, 0, NULL };
	Counts                            hits = { 0, 0, NULL };
	LrgRigidBodyComponent            *body;

	lrg_collider_component3d_set_trigger (trigger, TRUE);
	lrg_entity3d_add_component (zone, LRG_COMPONENT (trigger));
	g_signal_connect (trigger, "trigger-entered", G_CALLBACK (on_entered), &counts);
	g_signal_connect (trigger, "trigger-exited", G_CALLBACK (on_exited), &counts);

	body = add_body (ball, LRG_DYNAMICS_BODY_DYNAMIC, lrg_collider_component3d_new_sphere (0.25f));
	g_signal_connect (body, "collision-entered", G_CALLBACK (on_entered), &hits);
	add_floor (world);

	/* falls through the sensor without being stopped, then lands */
	run (world, 3.0f);
	g_assert_cmpuint (counts.entered, ==, 1);
	g_assert_cmpuint (counts.exited, ==, 1);
	g_assert_true (counts.last == ball);
	g_assert_cmpuint (lrg_collider_component3d_get_n_overlaps (trigger), ==, 0);
	g_assert_cmpfloat_with_epsilon (entity_y (ball), 0.25f, 0.05f);

	/* the ball's body reported touching the floor */
	g_assert_cmpuint (hits.entered, ==, 1);
	g_assert_cmpstr (lrg_entity3d_get_name (hits.last), ==, "floor");
}

/* ==========================================================================
 * Joints from components
 * ========================================================================== */

static void
test_joints (void)
{
	g_autoptr(LrgWorld3D)  world = physics_world ();
	LrgEntity3D           *bob = add_entity (world, "bob", 2.0f, 5.0f, 0.0f);
	LrgEntity3D           *link = add_entity (world, "link", 4.0f, 5.0f, 0.0f);
	g_autoptr(LrgJointComponent) pin = lrg_joint_component_new (LRG_JOINT_POINT);
	g_autoptr(LrgJointComponent) chain = lrg_joint_component_new (LRG_JOINT_DISTANCE);
	gfloat                 p[3];
	gfloat                 q[3];
	gfloat                 d;

	add_body (bob, LRG_DYNAMICS_BODY_DYNAMIC, lrg_collider_component3d_new_sphere (0.2f));
	add_body (link, LRG_DYNAMICS_BODY_DYNAMIC, lrg_collider_component3d_new_sphere (0.2f));

	/* bob swings on a pivot 2 m to its left (a pendulum on the world) */
	lrg_joint_component_set_anchor (pin, -2.0f, 0.0f, 0.0f);
	lrg_entity3d_add_component (bob, LRG_COMPONENT (pin));

	/* link hangs from bob by a rod, the target given by GUID */
	lrg_joint_component_set_target_guid (chain, lrg_entity3d_ensure_guid (bob));
	lrg_entity3d_add_component (link, LRG_COMPONENT (chain));

	run (world, 2.0f);
	g_assert_nonnull (lrg_joint_component_get_joint (pin));
	g_assert_true (lrg_joint_component_get_target (chain) == bob);

	/* the pendulum keeps its 2 m radius about (0, 5, 0) */
	lrg_entity3d_get_world_position (bob, p);
	d = sqrtf (p[0] * p[0] + (p[1] - 5.0f) * (p[1] - 5.0f) + p[2] * p[2]);
	g_assert_cmpfloat_with_epsilon (d, 2.0f, 0.05f);
	g_assert_cmpfloat (p[1], <, 4.9f);   /* it actually swung */

	/* and the rod keeps link 2 m from bob */
	lrg_entity3d_get_world_position (link, q);
	d = sqrtf ((q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2]));
	g_assert_cmpfloat_with_epsilon (d, 2.0f, 0.05f);
}

/* ==========================================================================
 * The node physics block: round trip, instantiation, prefabs
 * ========================================================================== */

/* A level with a static floor node, a dynamic crate and a hinged door. */
static LrgLevel *
physics_level (void)
{
	LrgLevel                       *level = lrg_level_new ("physics");
	g_autoptr(LrgNode)              floor = lrg_node_new ("floor");
	g_autoptr(LrgNode)              crate = lrg_node_new ("crate");
	g_autoptr(LrgNode)              door = lrg_node_new ("door");
	g_autoptr(LrgNodePhysics)       floor_phys = lrg_node_physics_new (LRG_DYNAMICS_BODY_STATIC);
	g_autoptr(LrgNodePhysics)       crate_phys = lrg_node_physics_new (LRG_DYNAMICS_BODY_DYNAMIC);
	g_autoptr(LrgNodePhysics)       door_phys = lrg_node_physics_new (LRG_DYNAMICS_BODY_DYNAMIC);
	g_autoptr(LrgNodePhysicsShape)  s = NULL;
	g_autoptr(LrgNodePhysicsJoint)  hinge = lrg_node_physics_joint_new (LRG_JOINT_HINGE);

	lrg_node_set_guid (floor, "floor-guid");
	lrg_node_set_location_xyz (floor, 0.0f, -0.5f, 0.0f);
	s = lrg_node_physics_shape_new (LRG_COLLIDER3D_BOX);
	lrg_node_physics_shape_set_half_extents (s, 10.0f, 0.5f, 10.0f);
	lrg_node_physics_add_shape (floor_phys, s);
	g_clear_object (&s);
	lrg_node_set_physics (floor, floor_phys);

	lrg_node_set_guid (crate, "crate-guid");
	lrg_node_set_location_xyz (crate, 0.0f, 3.0f, 0.0f);
	lrg_node_physics_set_mass (crate_phys, 4.0f);
	lrg_node_physics_set_layer (crate_phys, "debris");
	lrg_node_physics_set_restitution (crate_phys, 0.25f);
	lrg_node_physics_set_ccd (crate_phys, TRUE);
	s = lrg_node_physics_shape_new (LRG_COLLIDER3D_BOX);
	lrg_node_physics_shape_set_half_extents (s, 0.5f, 0.5f, 0.5f);
	lrg_node_physics_add_shape (crate_phys, s);
	g_clear_object (&s);
	s = lrg_node_physics_shape_new (LRG_COLLIDER3D_SPHERE);
	lrg_node_physics_shape_set_radius (s, 1.5f);
	lrg_node_physics_shape_set_trigger (s, TRUE);
	lrg_node_physics_shape_set_offset (s, 0.0f, 1.0f, 0.0f);
	lrg_node_physics_shape_set_layer (s, "trigger");
	lrg_node_physics_add_shape (crate_phys, s);
	g_clear_object (&s);
	lrg_node_set_physics (crate, crate_phys);

	lrg_node_set_guid (door, "door-guid");
	lrg_node_set_location_xyz (door, 3.0f, 1.0f, 0.0f);
	s = lrg_node_physics_shape_new (LRG_COLLIDER3D_BOX);
	lrg_node_physics_shape_set_half_extents (s, 0.5f, 1.0f, 0.05f);
	lrg_node_physics_shape_set_offset (s, 0.5f, 0.0f, 0.0f);
	lrg_node_physics_shape_set_rotation (s, 0.0f, 15.0f, 0.0f);
	lrg_node_physics_shape_set_friction (s, 0.9f);
	lrg_node_physics_add_shape (door_phys, s);
	lrg_node_physics_joint_set_target (hinge, "floor-guid");
	lrg_node_physics_joint_set_axis (hinge, 0.0f, 1.0f, 0.0f);
	lrg_node_physics_joint_set_limits (hinge, TRUE, -1.5f, 1.5f);
	lrg_node_physics_joint_set_motor (hinge, 1.0f, 20.0f);
	lrg_node_physics_joint_set_break_force (hinge, 1000.0f);
	lrg_node_physics_add_joint (door_phys, hinge);
	lrg_node_set_physics (door, door_phys);

	lrg_level_add_node (level, floor, NULL);
	lrg_level_add_node (level, crate, NULL);
	lrg_level_add_node (level, door, NULL);
	return level;
}

static void
test_level_round_trip (void)
{
	g_autoptr(LrgLevel)           level = physics_level ();
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(GError)             error = NULL;
	g_autofree gchar             *first = NULL;
	g_autofree gchar             *second = NULL;
	g_autoptr(LrgLevel)           loaded = NULL;
	LrgNode                      *crate;
	LrgNode                      *door;

	first = lrg_level_serializer_save_to_data (ser, level, NULL);
	g_assert_nonnull (first);
	g_assert_nonnull (strstr (first, "physics:"));
	g_assert_nonnull (strstr (first, "body: dynamic"));
	g_assert_nonnull (strstr (first, "kind: hinge"));

	loaded = lrg_level_serializer_load_from_data (ser, first, -1, &error);
	g_assert_no_error (error);
	second = lrg_level_serializer_save_to_data (ser, loaded, NULL);
	g_assert_cmpstr (first, ==, second);   /* byte stable */

	/* field by field equal too */
	crate = lrg_level_find_node (loaded, "crate-guid");
	door = lrg_level_find_node (loaded, "door-guid");
	g_assert_nonnull (crate);
	g_assert_true (lrg_node_physics_equal (lrg_node_get_physics (crate),
	                                       lrg_node_get_physics (lrg_level_find_node (level, "crate-guid"))));
	g_assert_true (lrg_node_physics_equal (lrg_node_get_physics (door),
	                                       lrg_node_get_physics (lrg_level_find_node (level, "door-guid"))));
	g_assert_cmpuint (lrg_node_physics_get_shapes (lrg_node_get_physics (crate))->len, ==, 2);

	/* copy_state / state_equal include the block */
	{
		g_autoptr(LrgNode) snap = lrg_node_new ("snap");

		lrg_node_copy_state (snap, crate);
		g_assert_true (lrg_node_state_equal (snap, crate));
		lrg_node_physics_set_mass (lrg_node_get_physics (snap), 9.0f);
		g_assert_false (lrg_node_state_equal (snap, crate));
	}
}

static void
test_level_instantiate (void)
{
	g_autoptr(LrgLevel)    level = physics_level ();
	g_autoptr(LrgWorld3D)  world = lrg_world3d_new ();
	g_autoptr(GError)      error = NULL;
	LrgEntity3D           *crate;
	LrgEntity3D           *door;
	LrgRigidBodyComponent *rb;
	g_autoptr(GPtrArray)   colliders = NULL;
	LrgJointComponent     *joint;

	g_assert_true (lrg_level_instantiate_3d (level, world, NULL, &error));
	g_assert_no_error (error);
	g_assert_nonnull (lrg_world3d_get_physics (world));   /* attached on demand */

	crate = lrg_world3d_find_by_guid (world, "crate-guid");
	rb = LRG_RIGID_BODY_COMPONENT (lrg_entity3d_get_component (crate, LRG_TYPE_RIGID_BODY_COMPONENT));
	g_assert_nonnull (rb);
	g_assert_cmpint (lrg_rigid_body_component_get_body_kind (rb), ==, LRG_DYNAMICS_BODY_DYNAMIC);
	g_assert_cmpfloat (lrg_rigid_body_component_get_mass (rb), ==, 4.0f);
	g_assert_cmpstr (lrg_rigid_body_component_get_layer (rb), ==, "debris");
	g_assert_true (lrg_rigid_body_component_get_ccd (rb));
	colliders = lrg_entity3d_get_components_of_type (crate, LRG_TYPE_COLLIDER_COMPONENT3D);
	g_assert_cmpuint (colliders->len, ==, 2);
	g_assert_true (lrg_collider_component3d_get_trigger (g_ptr_array_index (colliders, 1)));

	door = lrg_world3d_find_by_guid (world, "door-guid");
	joint = LRG_JOINT_COMPONENT (lrg_entity3d_get_component (door, LRG_TYPE_JOINT_COMPONENT));
	g_assert_nonnull (joint);
	g_assert_cmpint (lrg_joint_component_get_kind (joint), ==, LRG_JOINT_HINGE);
	g_assert_cmpstr (lrg_joint_component_get_target_guid (joint), ==, "floor-guid");

	/* it simulates: the crate lands on the floor, the door joint is built */
	run (world, 2.0f);
	g_assert_cmpfloat_with_epsilon (entity_y (crate), 0.5f, 0.06f);
	g_assert_nonnull (lrg_joint_component_get_joint (joint));
	g_assert_true (lrg_joint_component_get_target (joint) == lrg_world3d_find_by_guid (world, "floor-guid"));
}

static void
test_prefab_body (void)
{
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *dir = g_dir_make_tmp ("lrg-physics-prefab-XXXXXX", &error);
	g_autofree gchar          *path = NULL;
	g_autoptr(LrgNode)         barrel = lrg_node_new ("barrel");
	g_autoptr(LrgNodePhysics)  phys = lrg_node_physics_new (LRG_DYNAMICS_BODY_DYNAMIC);
	g_autoptr(LrgNodePhysicsShape) shape = lrg_node_physics_shape_new (LRG_COLLIDER3D_CYLINDER);
	g_autoptr(LrgNode)         loaded = NULL;
	g_autoptr(LrgNode)         instance = NULL;
	g_autoptr(LrgLevel)        level = lrg_level_new ("with-prefab");
	g_autoptr(LrgWorld3D)      world = lrg_world3d_new ();
	LrgEntity3D               *entity;
	LrgRigidBodyComponent     *rb;

	g_assert_no_error (error);
	path = g_build_filename (dir, "barrel.rprefab", NULL);

	lrg_node_physics_shape_set_radius (shape, 0.4f);
	lrg_node_physics_shape_set_half_height (shape, 0.6f);
	lrg_node_physics_add_shape (phys, shape);
	lrg_node_physics_set_mass (phys, 20.0f);
	lrg_node_set_physics (barrel, phys);
	g_assert_true (lrg_prefab_save (barrel, path, &error));
	g_assert_no_error (error);

	loaded = lrg_prefab_load (path, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_node_physics_equal (lrg_node_get_physics (loaded), phys));

	instance = lrg_prefab_instantiate (loaded);
	g_assert_nonnull (lrg_node_get_physics (instance));
	lrg_node_set_location_xyz (instance, 0.0f, 2.0f, 0.0f);
	lrg_level_add_node (level, instance, NULL);
	g_assert_true (lrg_level_instantiate_3d (level, world, NULL, &error));
	g_assert_no_error (error);

	entity = lrg_world3d_find_by_guid (world, lrg_node_get_guid (instance));
	rb = LRG_RIGID_BODY_COMPONENT (lrg_entity3d_get_component (entity, LRG_TYPE_RIGID_BODY_COMPONENT));
	g_assert_nonnull (rb);
	lrg_world3d_step (world, STEP);
	g_assert_cmpuint (lrg_rigid_body_component_get_body_id (rb), !=, LRG_PHYSICS_NO_BODY);
	g_assert_cmpint (lrg_physics_shape_get_kind (lrg_dynamics_world_get_shape (lrg_world3d_get_physics (world),
	                 lrg_rigid_body_component_get_body_id (rb))), ==, LRG_PHYSICS_SHAPE_KIND_CYLINDER);

	g_remove (path);
	g_rmdir (dir);
}

/* ==========================================================================
 * Character controller against static triangle meshes in the physics world
 * ========================================================================== */

/* Adds a static triangle-mesh body: a floor (y = 0, x in [-10, 10]),
 * a 0.2 high step at x in [2, 4], a 30 degree ramp along -X starting at
 * x = -2 and a 60 degree wall-ramp along +Z starting at z = 3. */
static void
add_level_mesh (LrgWorld3D *world)
{
	LrgEntity3D                      *e = add_entity (world, "level-mesh", 0.0f, 0.0f, 0.0f);
	g_autoptr(LrgColliderComponent3D) mesh = lrg_collider_component3d_new (LRG_COLLIDER3D_MESH);
	gfloat                            t30 = tanf ((gfloat) G_PI / 6.0f);
	gfloat                            t60 = tanf ((gfloat) G_PI / 3.0f);
	const gfloat                      v[] = {
		/* floor */
		-10.0f, 0.0f, -10.0f,  10.0f, 0.0f, -10.0f,  10.0f, 0.0f, 10.0f,  -10.0f, 0.0f, 10.0f,
		/* step top */
		2.0f, 0.2f, -1.0f,  4.0f, 0.2f, -1.0f,  4.0f, 0.2f, 1.0f,  2.0f, 0.2f, 1.0f,
		/* step riser at x = 2 */
		2.0f, 0.0f, -1.0f,  2.0f, 0.2f, -1.0f,  2.0f, 0.2f, 1.0f,  2.0f, 0.0f, 1.0f,
		/* 30 degree ramp rising towards -X from x = -2 to x = -6 */
		-2.0f, 0.001f, -1.0f,  -6.0f, 4.0f * t30, -1.0f,  -6.0f, 4.0f * t30, 1.0f,  -2.0f, 0.001f, 1.0f,
		/* 60 degree slope rising towards +Z from z = 3 */
		-1.0f, 0.001f, 3.0f,  1.0f, 0.001f, 3.0f,  1.0f, 2.0f * t60, 5.0f,  -1.0f, 2.0f * t60, 5.0f
	};
	const guint32 idx[] = {
		0, 2, 1, 0, 3, 2,
		4, 6, 5, 4, 7, 6,
		8, 9, 10, 8, 10, 11,
		12, 13, 14, 12, 14, 15,
		16, 18, 17, 16, 19, 18
	};

	lrg_collider_component3d_set_mesh_data (mesh, v, G_N_ELEMENTS (v), idx, G_N_ELEMENTS (idx));
	lrg_entity3d_add_component (e, LRG_COMPONENT (mesh));
	lrg_world3d_step (world, STEP);   /* builds the static body */
}

/* Walks @cc from @feet by @dir * speed for @seconds with gravity. */
static void
walk (LrgCharacterController *cc,
      gfloat                 *feet,
      gfloat                  dx,
      gfloat                  dz,
      gfloat                  seconds,
      LrgCharacterMove       *out)
{
	guint n = (guint) ceilf (seconds / STEP);
	guint i;

	for (i = 0; i < n; i++)
	{
		gfloat motion[3];

		motion[0] = dx * STEP;
		motion[1] = -9.81f * STEP * STEP * 4.0f;
		motion[2] = dz * STEP;
		g_assert_true (lrg_character_controller_move (cc, feet, motion, TRUE, out));
		memcpy (feet, out->position, sizeof (out->position));
	}
}

static void
test_character_controller (void)
{
	g_autoptr(LrgWorld3D)             world = physics_world ();
	g_autoptr(LrgCharacterController) cc = lrg_character_controller_new (0.3f, 1.8f);
	LrgCharacterMove                  mv;
	gfloat                            feet[3];

	add_level_mesh (world);
	lrg_character_controller_set_physics (cc, lrg_world3d_get_physics (world), LRG_PHYSICS_LAYERS_ALL);
	g_assert_true (lrg_character_controller_get_physics (cc) == lrg_world3d_get_physics (world));

	/* stands on the floor */
	feet[0] = 0.0f; feet[1] = 0.5f; feet[2] = 0.0f;
	walk (cc, feet, 0.0f, 0.0f, 0.5f, &mv);
	g_assert_true (mv.grounded);
	g_assert_cmpfloat_with_epsilon (feet[1], 0.0f, 0.03f);
	g_assert_cmpuint (mv.ground_body, !=, LRG_PHYSICS_NO_BODY);
	g_assert_cmpfloat (mv.ground_normal[1], >, 0.99f);

	/* walks +X onto the 0.2 step */
	walk (cc, feet, 3.0f, 0.0f, 1.0f, &mv);
	g_assert_cmpfloat (feet[0], >, 2.4f);
	g_assert_cmpfloat_with_epsilon (feet[1], 0.2f, 0.03f);
	g_assert_true (mv.grounded);

	/* back and up the 30 degree ramp (walkable with the 45 degree limit) */
	feet[0] = 0.0f; feet[1] = 0.0f; feet[2] = 0.0f;
	walk (cc, feet, -3.0f, 0.0f, 1.5f, &mv);
	g_assert_cmpfloat (feet[0], <, -3.0f);
	g_assert_cmpfloat (feet[1], >, 0.5f);
	g_assert_true (mv.grounded);

	/* the 60 degree slope stops the walk (it slides back down) */
	feet[0] = 0.0f; feet[1] = 0.0f; feet[2] = 1.0f;
	walk (cc, feet, 0.0f, 3.0f, 2.0f, &mv);
	g_assert_cmpfloat (feet[2], <, 3.6f);
	g_assert_cmpfloat (feet[1], <, 0.7f);

	/* can_stand sees the bodies too */
	feet[0] = 3.0f; feet[1] = -0.5f; feet[2] = 0.0f;
	g_assert_false (lrg_character_controller_can_stand (cc, feet, 1.8f));
	feet[1] = 0.21f;
	g_assert_true (lrg_character_controller_can_stand (cc, feet, 1.8f));
}

/* ==========================================================================
 * Debug draw smoke (2D and 3D)
 * ========================================================================== */

static void
test_debug_draw (void)
{
	g_autoptr(LrgPhysicsWorld) w2 = lrg_physics_world_new ();
	g_autoptr(LrgRigidBody)    box = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
	g_autoptr(LrgRigidBody)    ball = lrg_rigid_body_new (LRG_RIGID_BODY_STATIC);
	g_autoptr(LrgDebugDraw)    dd = lrg_debug_draw_new ();
	g_autoptr(LrgWorld3D)      world = physics_world ();
	guint                      n;

	lrg_rigid_body_set_box_shape (box, 10.0f, 4.0f);
	lrg_rigid_body_set_position (box, 50.0f, 20.0f);
	lrg_rigid_body_set_rotation (box, 0.3f);
	lrg_rigid_body_set_circle_shape (ball, 5.0f);
	lrg_physics_world_add_body (w2, box);
	lrg_physics_world_add_body (w2, ball);

	lrg_physics_world_debug_draw (w2, dd, LRG_DYNAMICS_DEBUG_NONE);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 0);
	lrg_physics_world_debug_draw (w2, dd, LRG_DYNAMICS_DEBUG_SHAPES);
	n = lrg_debug_draw_get_count (dd);
	g_assert_cmpuint (n, ==, 4 + 2);   /* box: 4 edges; circle + radius tick */
	g_assert_cmpuint (lrg_debug_draw_get_primitive_category (dd, 0), ==, LRG_DEBUG_CATEGORY_PHYSICS);
	lrg_physics_world_debug_draw (w2, dd, LRG_DYNAMICS_DEBUG_ALL);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), >, n + 6);
	lrg_debug_draw_clear (dd);

	/* 3D: forwards to the dynamics world */
	add_floor (world);
	lrg_world3d_step (world, STEP);
	lrg_world3d_debug_draw_physics (world, dd, LRG_DYNAMICS_DEBUG_SHAPES);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), >, 0);
}

/* ==========================================================================
 * Determinism: two identical worlds give bit-identical results
 * ========================================================================== */

static void
pile (LrgWorld3D *world,
      gfloat     *out)
{
	guint i;

	add_floor (world);
	for (i = 0; i < 8; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("box%u", i);
		LrgEntity3D      *e = add_entity (world, name, 0.1f * (gfloat) i, 1.0f + 1.05f * (gfloat) i, 0.0f);

		add_body (e, LRG_DYNAMICS_BODY_DYNAMIC, lrg_collider_component3d_new_box (0.5f, 0.5f, 0.5f));
	}
	run (world, 2.0f);
	lrg_entity3d_get_world_position (lrg_world3d_find_by_name (world, "box7"), out);
}

static void
test_determinism (void)
{
	g_autoptr(LrgWorld3D) a = physics_world ();
	g_autoptr(LrgWorld3D) b = physics_world ();
	gfloat                pa[3];
	gfloat                pb[3];

	pile (a, pa);
	pile (b, pb);
	g_assert_true (memcmp (pa, pb, sizeof (pa)) == 0);
}



/* ==========================================================================
 * Prefabs: joint targets follow new GUIDs; the physics block is an override
 * ========================================================================== */

/* A two-node prefab: a "frame" root and a "door" child hinged to it. */
static LrgNode *
hinged_prefab (void)
{
	LrgNode                        *frame = lrg_node_new ("frame");
	g_autoptr(LrgNode)              door = lrg_node_new ("door");
	g_autoptr(LrgNodePhysics)       phys = lrg_node_physics_new (LRG_DYNAMICS_BODY_DYNAMIC);
	g_autoptr(LrgNodePhysicsShape)  shape = lrg_node_physics_shape_new (LRG_COLLIDER3D_BOX);
	g_autoptr(LrgNodePhysicsJoint)  hinge = lrg_node_physics_joint_new (LRG_JOINT_HINGE);

	lrg_node_physics_add_shape (phys, shape);
	lrg_node_physics_joint_set_target (hinge, lrg_node_get_guid (frame));
	lrg_node_physics_add_joint (phys, hinge);
	lrg_node_set_physics (door, phys);
	lrg_node_add_child (frame, door);
	return frame;
}

static const gchar *
door_target (LrgNode *root)
{
	LrgNode *door = g_ptr_array_index (lrg_node_get_children (root), 0);

	return lrg_node_physics_joint_get_target (
		g_ptr_array_index (lrg_node_physics_get_joints (lrg_node_get_physics (door)), 0));
}

static void
test_prefab_joint_targets (void)
{
	g_autoptr(LrgNode) prefab = hinged_prefab ();
	g_autoptr(LrgNode) plain = NULL;
	g_autoptr(LrgNode) linked = NULL;
	g_autoptr(LrgNode) regen = NULL;

	/* unlinked instance (clone): the target is the instance's frame */
	plain = lrg_prefab_instantiate (prefab);
	g_assert_cmpstr (lrg_node_get_guid (plain), !=, lrg_node_get_guid (prefab));
	g_assert_cmpstr (door_target (plain), ==, lrg_node_get_guid (plain));
	g_assert_cmpstr (door_target (prefab), ==, lrg_node_get_guid (prefab));   /* source untouched */

	/* linked instance, and a sync that changes nothing keeps it */
	linked = lrg_prefab_instantiate_linked (prefab, "prefabs/door.rprefab", NULL);
	g_assert_cmpstr (door_target (linked), ==, lrg_node_get_guid (linked));
	lrg_prefab_sync_instance (linked, prefab);
	g_assert_cmpstr (door_target (linked), ==, lrg_node_get_guid (linked));
	{
		g_auto(GStrv) keys = lrg_prefab_compute_overrides (linked, prefab);

		g_assert_true (keys == NULL || keys[0] == NULL);   /* remapped targets are not overrides */
	}

	/* regenerated GUIDs */
	regen = lrg_prefab_clone (prefab);
	lrg_prefab_regenerate_guids (regen);
	g_assert_cmpstr (door_target (regen), ==, lrg_node_get_guid (regen));

	/* reference storage round trip expands with instance targets */
	{
		g_autoptr(LrgNode) sparse = lrg_prefab_make_reference (linked);
		g_autoptr(GError)  error = NULL;
		g_autoptr(LrgNode) full = lrg_prefab_instantiate_reference (sparse, prefab, &error);

		g_assert_no_error (error);
		g_assert_cmpstr (door_target (full), ==, lrg_node_get_guid (full));
	}
}

static void
test_prefab_physics_override (void)
{
	g_autoptr(LrgNode) prefab = hinged_prefab ();
	g_autoptr(LrgNode) inst = lrg_prefab_instantiate_linked (prefab, "prefabs/door.rprefab", NULL);
	LrgNode           *door = g_ptr_array_index (lrg_node_get_children (inst), 0);
	LrgNode           *src_door = g_ptr_array_index (lrg_node_get_children (prefab), 0);
	g_auto(GStrv)      keys = NULL;
	gboolean           found = FALSE;
	guint              i;

	/* edit the instance's block: recorded as the "physics" override */
	lrg_node_physics_set_mass (lrg_node_get_physics (door), 42.0f);
	keys = lrg_prefab_compute_overrides (inst, prefab);
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		if (g_str_has_suffix (keys[i], "physics"))
			found = TRUE;
		lrg_prefab_link_add_override (lrg_node_get_prefab_link (inst), keys[i]);
	}
	g_assert_true (found);

	/* the source changes; a sync keeps the overridden block */
	lrg_node_physics_set_mass (lrg_node_get_physics (src_door), 7.0f);
	lrg_prefab_sync_instance (inst, prefab);
	door = g_ptr_array_index (lrg_node_get_children (inst), 0);
	g_assert_cmpfloat (lrg_node_physics_get_mass (lrg_node_get_physics (door)), ==, 42.0f);
	g_assert_cmpstr (door_target (inst), ==, lrg_node_get_guid (inst));

	/* without the override a sync brings the source block (targets localized) */
	{
		g_autoptr(LrgNode) fresh = lrg_prefab_instantiate_linked (prefab, "prefabs/door.rprefab", NULL);
		LrgNode           *fd = g_ptr_array_index (lrg_node_get_children (fresh), 0);

		lrg_node_physics_set_mass (lrg_node_get_physics (src_door), 9.0f);
		lrg_prefab_sync_instance (fresh, prefab);
		g_assert_cmpfloat (lrg_node_physics_get_mass (lrg_node_get_physics (fd)), ==, 9.0f);
		g_assert_cmpstr (door_target (fresh), ==, lrg_node_get_guid (fresh));
	}

	/* applying the override to the source writes source GUIDs */
	{
		g_assert_cmpuint (lrg_prefab_apply_overrides_to_source (inst, prefab, NULL), >, 0);
		g_assert_cmpfloat (lrg_node_physics_get_mass (lrg_node_get_physics (src_door)), ==, 42.0f);
		g_assert_cmpstr (door_target (prefab), ==, lrg_node_get_guid (prefab));
	}
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/physics-bodies/body-creation", test_body_creation);
	g_test_add_func ("/physics-bodies/falling-box", test_falling_box);
	g_test_add_func ("/physics-bodies/interpolation", test_interpolation);
	g_test_add_func ("/physics-bodies/kinematic", test_kinematic);
	g_test_add_func ("/physics-bodies/triggers", test_triggers);
	g_test_add_func ("/physics-bodies/joints", test_joints);
	g_test_add_func ("/physics-bodies/level-round-trip", test_level_round_trip);
	g_test_add_func ("/physics-bodies/level-instantiate", test_level_instantiate);
	g_test_add_func ("/physics-bodies/prefab-body", test_prefab_body);
	g_test_add_func ("/physics-bodies/prefab-joint-targets", test_prefab_joint_targets);
	g_test_add_func ("/physics-bodies/prefab-physics-override", test_prefab_physics_override);
	g_test_add_func ("/physics-bodies/character-controller", test_character_controller);
	g_test_add_func ("/physics-bodies/debug-draw", test_debug_draw);
	g_test_add_func ("/physics-bodies/determinism", test_determinism);

	return g_test_run ();
}
