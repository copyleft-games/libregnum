/* test-ragdoll.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhysicsAsset (.rphysasset round trip, strict errors, generation from
 * a skeleton) and LrgRagdoll (falls and settles within its joint limits,
 * powered ragdoll tracks a pose, per-bone blend, pose write-back,
 * add/remove, determinism, performance).
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define T_PI (3.14159265358979323846f)

/* ------------------------------------------------------------------ */
/* a 14-bone humanoid, Y up, arms out along X (a T pose)               */
/* ------------------------------------------------------------------ */

typedef struct
{
	const gchar *name;
	gint parent;
	gfloat x, y, z;
} BoneDef;

static const BoneDef HUMANOID[] = {
	{ "pelvis", -1, 0.0f, 1.0f, 0.0f },
	{ "spine", 0, 0.0f, 0.25f, 0.0f },
	{ "head", 1, 0.0f, 0.35f, 0.0f },
	{ "upper_arm_l", 1, 0.2f, 0.25f, 0.0f },
	{ "lower_arm_l", 3, 0.3f, 0.0f, 0.0f },
	{ "hand_l", 4, 0.25f, 0.0f, 0.0f },
	{ "upper_arm_r", 1, -0.2f, 0.25f, 0.0f },
	{ "lower_arm_r", 6, -0.3f, 0.0f, 0.0f },
	{ "hand_r", 7, -0.25f, 0.0f, 0.0f },
	{ "thigh_l", 0, 0.12f, -0.05f, 0.0f },
	{ "shin_l", 9, 0.0f, -0.45f, 0.0f },
	{ "foot_l", 10, 0.0f, -0.42f, 0.0f },
	{ "thigh_r", 0, -0.12f, -0.05f, 0.0f },
	{ "shin_r", 12, 0.0f, -0.45f, 0.0f },
};

#define N_BONES (G_N_ELEMENTS (HUMANOID))

static LrgSkeleton *
make_skeleton (void)
{
	LrgSkeleton *skel = lrg_skeleton_new ();
	guint i;

	for (i = 0; i < N_BONES; i++)
	{
		g_autoptr(LrgBone) bone = lrg_bone_new (HUMANOID[i].name, (gint) i);
		LrgBonePose pose;

		lrg_bone_pose_set_identity (&pose);
		pose.position_x = HUMANOID[i].x;
		pose.position_y = HUMANOID[i].y;
		pose.position_z = HUMANOID[i].z;
		lrg_bone_set_parent_index (bone, HUMANOID[i].parent);
		lrg_bone_set_bind_pose (bone, &pose);
		lrg_bone_set_local_pose (bone, &pose);
		lrg_skeleton_add_bone (skel, bone);
	}
	lrg_skeleton_calculate_world_poses (skel);
	return skel;
}

static LrgDynamicsWorld *
make_world (gfloat gravity, gboolean ground)
{
	LrgDynamicsWorld *world = lrg_dynamics_world_new ();
	gfloat g[3] = { 0.0f, gravity, 0.0f };
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_dynamics_world_set_gravity (world, g, &error));
	g_assert_true (lrg_dynamics_world_set_step_limits (world, 1.0f / 120.0f, 8, &error));
	g_assert_true (lrg_dynamics_world_set_material (world, 0.0f, 0.8f, &error));
	g_assert_no_error (error);
	if (ground)
	{
		gfloat n[3] = { 0.0f, 1.0f, 0.0f };
		gfloat o[3] = { 0.0f, 0.0f, 0.0f };
		g_autoptr(LrgPhysicsShape) plane = lrg_physics_shape_new_plane (n, 0.0f);
		guint id;

		g_assert_true (lrg_dynamics_world_add_body (world, plane, LRG_DYNAMICS_BODY_STATIC, 0.0f, o,
		                                            NULL, &id, &error));
		g_assert_no_error (error);
	}
	return world;
}

static void
step (LrgDynamicsWorld *world, guint n)
{
	g_autoptr(GError) error = NULL;
	guint i;

	for (i = 0; i < n; i++)
	{
		g_assert_true (lrg_dynamics_world_step (world, 1.0f / 120.0f, NULL, &error));
		g_assert_no_error (error);
	}
}

/* ------------------------------------------------------------------ */
/* the asset                                                           */
/* ------------------------------------------------------------------ */

static const gchar *FIXTURE =
	"version: 1\n"
	"name: test humanoid\n"
	"parts:\n"
	"- bone: pelvis\n"
	"  shape: box\n"
	"  half-extents:\n"
	"  - 0.15\n"
	"  - 0.1\n"
	"  - 0.1\n"
	"  mass: 12\n"
	"- bone: spine\n"
	"  radius: 0.1\n"
	"  half-height: 0.05\n"
	"  offset:\n"
	"  - 0\n"
	"  - 0.12\n"
	"  - 0\n"
	"  mass: 10\n"
	"  joint:\n"
	"    rotation:\n"
	"    - 0\n"
	"    - 0\n"
	"    - 0.7071068\n"
	"    - 0.7071068\n"
	"    twist:\n"
	"    - -0.2\n"
	"    - 0.2\n"
	"    swing:\n"
	"    - 0.4\n"
	"    - 0.3\n"
	"    friction: 1.5\n"
	"- bone: lower_arm_l\n"
	"  shape: sphere\n"
	"  radius: 0.06\n"
	"  joint:\n"
	"    kind: hinge\n"
	"    twist:\n"
	"    - 0\n"
	"    - 2.5\n"
	"    motor-max-torque: 20\n"
	"    motor-frequency: 6\n"
	"    motor-damping: 0.5\n"
	"exclusions:\n"
	"- - pelvis\n"
	"  - lower_arm_l\n";

static void
test_asset_fixture_round_trip (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgPhysicsAsset) asset = NULL;
	g_autoptr(LrgPhysicsAsset) again = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *text2 = NULL;
	const LrgPhysicsAssetPart *p;
	const gchar *a;
	const gchar *b;
	gsize len;

	asset = lrg_physics_asset_new_from_data (FIXTURE, -1, &error);
	g_assert_no_error (error);
	g_assert_nonnull (asset);
	g_assert_cmpstr (lrg_physics_asset_get_name (asset), ==, "test humanoid");
	g_assert_true (lrg_physics_asset_get_exclude_adjacent (asset));
	g_assert_cmpuint (lrg_physics_asset_get_part_count (asset), ==, 3);

	p = lrg_physics_asset_get_part (asset, 0);
	g_assert_cmpint (p->shape, ==, LRG_PHYSICS_ASSET_SHAPE_BOX);
	g_assert_cmpfloat (p->half_extents[0], ==, 0.15f);
	g_assert_cmpfloat (p->mass, ==, 12.0f);
	p = lrg_physics_asset_get_part (asset, 1);
	g_assert_cmpint (p->shape, ==, LRG_PHYSICS_ASSET_SHAPE_CAPSULE);
	g_assert_cmpint (p->joint, ==, LRG_JOINT_SWING_TWIST);
	g_assert_cmpfloat (p->swing_z, ==, 0.3f);
	g_assert_cmpfloat (p->friction, ==, 1.5f);
	p = lrg_physics_asset_get_part (asset, 2);
	g_assert_cmpint (p->joint, ==, LRG_JOINT_HINGE);
	g_assert_cmpfloat (p->twist_max, ==, 2.5f);
	g_assert_cmpfloat (p->motor_frequency, ==, 6.0f);
	g_assert_cmpint (lrg_physics_asset_find_part (asset, "lower_arm_l"), ==, 2);
	g_assert_cmpint (lrg_physics_asset_find_part (asset, "nope"), ==, -1);
	g_assert_cmpuint (lrg_physics_asset_get_exclusion_count (asset), ==, 1);
	g_assert_true (lrg_physics_asset_get_exclusion (asset, 0, &a, &b));
	g_assert_cmpstr (a, ==, "pelvis");
	g_assert_cmpstr (b, ==, "lower_arm_l");
	g_assert_false (lrg_physics_asset_get_exclusion (asset, 1, NULL, NULL));

	/* the writer reproduces the fixture byte for byte */
	text = lrg_physics_asset_save_to_data (asset, &len, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, FIXTURE);
	g_assert_cmpuint (len, ==, strlen (FIXTURE));

	again = lrg_physics_asset_new_from_data (text, (gssize) len, &error);
	g_assert_no_error (error);
	text2 = lrg_physics_asset_save_to_data (again, NULL, &error);
	g_assert_cmpstr (text, ==, text2);
}

static void
test_asset_file_and_generated_round_trip (void)
{
	g_autoptr(LrgSkeleton) skel = make_skeleton ();
	g_autoptr(LrgPhysicsAsset) asset = lrg_physics_asset_new_from_skeleton (skel, 70.0f, 0.25f);
	g_autoptr(LrgPhysicsAsset) loaded = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *a = NULL;
	g_autofree gchar *b = NULL;
	gfloat total = 0.0f;
	guint i;

	g_assert_cmpuint (lrg_physics_asset_get_part_count (asset), ==, N_BONES);
	for (i = 0; i < lrg_physics_asset_get_part_count (asset); i++)
		total += lrg_physics_asset_get_part (asset, i)->mass;
	g_assert_cmpfloat_with_epsilon (total, 70.0f, 1e-3f);
	g_assert_true (lrg_physics_asset_validate (asset, skel, &error));
	g_assert_no_error (error);
	lrg_physics_asset_set_name (asset, "generated");
	lrg_physics_asset_add_exclusion (asset, "thigh_l", "thigh_r");
	lrg_physics_asset_add_exclusion (asset, "thigh_r", "thigh_l");
	g_assert_cmpuint (lrg_physics_asset_get_exclusion_count (asset), ==, 1);

	dir = g_dir_make_tmp ("rphysasset-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "humanoid.rphysasset", NULL);
	g_assert_true (lrg_physics_asset_save_to_file (asset, path, &error));
	g_assert_no_error (error);
	loaded = lrg_physics_asset_new_from_file (path, &error);
	g_assert_no_error (error);
	a = lrg_physics_asset_save_to_data (asset, NULL, &error);
	b = lrg_physics_asset_save_to_data (loaded, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (a, ==, b);
	g_unlink (path);
	g_rmdir (dir);
}

static void
test_asset_edit (void)
{
	g_autoptr(LrgPhysicsAsset) asset = lrg_physics_asset_new ();
	g_autoptr(LrgPhysicsAssetPart) part = lrg_physics_asset_part_new ("pelvis");
	g_autoptr(LrgPhysicsAssetPart) copy = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;

	g_assert_cmpint (lrg_physics_asset_add_part (asset, part, &error), ==, 0);
	g_assert_no_error (error);
	/* the same bone twice is refused */
	g_assert_cmpint (lrg_physics_asset_add_part (asset, part, &error), ==, -1);
	g_assert_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_SKELETON);
	g_clear_error (&error);

	copy = lrg_physics_asset_part_copy (part);
	g_free (copy->bone);
	copy->bone = g_strdup ("spine");
	copy->mass = -1.0f;
	g_assert_cmpint (lrg_physics_asset_add_part (asset, copy, &error), ==, -1);
	g_assert_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_FORMAT);
	g_clear_error (&error);
	copy->mass = 3.0f;
	copy->joint = LRG_JOINT_SLIDER;
	g_assert_cmpint (lrg_physics_asset_add_part (asset, copy, &error), ==, -1);
	g_assert_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_FORMAT);
	g_clear_error (&error);
	copy->joint = LRG_JOINT_POINT;
	g_assert_cmpint (lrg_physics_asset_add_part (asset, copy, &error), ==, 1);
	copy->mass = 4.0f;
	g_assert_true (lrg_physics_asset_set_part (asset, 1, copy, &error));
	g_assert_cmpfloat (lrg_physics_asset_get_part (asset, 1)->mass, ==, 4.0f);
	lrg_physics_asset_set_exclude_adjacent (asset, FALSE);
	lrg_physics_asset_add_exclusion (asset, "pelvis", "spine");
	lrg_physics_asset_remove_part (asset, 1);
	g_assert_cmpuint (lrg_physics_asset_get_part_count (asset), ==, 1);
	g_assert_cmpuint (lrg_physics_asset_get_exclusion_count (asset), ==, 0);
	lrg_physics_asset_add_exclusion (asset, "pelvis", "x");
	lrg_physics_asset_remove_exclusion (asset, 0);
	g_assert_cmpuint (lrg_physics_asset_get_exclusion_count (asset), ==, 0);

	text = lrg_physics_asset_save_to_data (asset, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, "version: 1\nexclude-adjacent: false\nparts:\n- bone: pelvis\n");
}

typedef struct
{
	const gchar *text;
	gint code;
	const gchar *fragment;
} BadCase;

static void
test_asset_strict_errors (void)
{
	static const BadCase cases[] = {
		{ "parts: []\n", LRG_RAGDOLL_ERROR_FORMAT, "/version: missing" },
		{ "version: 2\n", LRG_RAGDOLL_ERROR_VERSION, "newer than 1" },
		{ "version: 1\ncolour: red\n", LRG_RAGDOLL_ERROR_FORMAT, "/colour: unknown key" },
		{ "- a\n", LRG_RAGDOLL_ERROR_PARSE, "a mapping" },
		{ "version: 1\nparts:\n- shape: box\n", LRG_RAGDOLL_ERROR_FORMAT, "/parts/0/bone: missing" },
		{ "version: 1\nparts:\n- bone: a\n  shape: box\n  radius: 1\n", LRG_RAGDOLL_ERROR_FORMAT,
		  "/parts/0/radius: does not apply to a box" },
		{ "version: 1\nparts:\n- bone: a\n  shape: cube\n", LRG_RAGDOLL_ERROR_FORMAT,
		  "/parts/0/shape: expected capsule, box or sphere" },
		{ "version: 1\nparts:\n- bone: a\n  mass: -2\n", LRG_RAGDOLL_ERROR_FORMAT,
		  "/parts/0/mass: must be positive" },
		{ "version: 1\nparts:\n- bone: a\n  mass: heavy\n", LRG_RAGDOLL_ERROR_FORMAT,
		  "/parts/0/mass: expected a number" },
		{ "version: 1\nparts:\n- bone: a\n  offset: [1, 2]\n", LRG_RAGDOLL_ERROR_FORMAT,
		  "/parts/0/offset: expected a list of 3 numbers" },
		{ "version: 1\nparts:\n- bone: a\n  rotation: [0, 0, 0, 0]\n", LRG_RAGDOLL_ERROR_FORMAT,
		  "cannot be zero" },
		{ "version: 1\nparts:\n- bone: a\n  joint:\n    kind: slider\n", LRG_RAGDOLL_ERROR_FORMAT,
		  "/parts/0/joint/kind: expected fixed, point, hinge, cone or swing-twist" },
		{ "version: 1\nparts:\n- bone: a\n  joint:\n    kind: cone\n    twist: [0, 1]\n",
		  LRG_RAGDOLL_ERROR_FORMAT, "/parts/0/joint/twist: twist limits apply to hinge" },
		{ "version: 1\nparts:\n- bone: a\n  joint:\n    swing: [4, 1]\n", LRG_RAGDOLL_ERROR_FORMAT,
		  "/parts/0/joint/swing: expected [y, z] half angles" },
		{ "version: 1\nparts:\n- bone: a\n  joint:\n    twist: [1, -1]\n", LRG_RAGDOLL_ERROR_FORMAT,
		  "/parts/0/joint/twist: expected [min, max]" },
		{ "version: 1\nparts:\n- bone: a\n  joint:\n    spin: 1\n", LRG_RAGDOLL_ERROR_FORMAT,
		  "/parts/0/joint/spin: unknown key" },
		{ "version: 1\nparts:\n- bone: a\n- bone: a\n", LRG_RAGDOLL_ERROR_FORMAT,
		  "/parts/1/bone: bone 'a' already has a part" },
		{ "version: 1\nparts:\n- bone: a\nexclusions:\n- [a, b]\n", LRG_RAGDOLL_ERROR_FORMAT,
		  "/exclusions/0: bone 'b' has no part" },
		{ "version: 1\nexclude-adjacent: maybe\n", LRG_RAGDOLL_ERROR_FORMAT,
		  "/exclude-adjacent: expected true or false" },
		{ "version: 1\nparts: {}\n", LRG_RAGDOLL_ERROR_FORMAT, "/parts: expected a list" },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(LrgPhysicsAsset) asset = NULL;

		asset = lrg_physics_asset_new_from_data (cases[i].text, -1, &error);
		g_assert_null (asset);
		g_assert_nonnull (error);
		if (error->code != cases[i].code || strstr (error->message, cases[i].fragment) == NULL)
			g_error ("case %u: got '%s' (code %d), wanted '%s' (code %d)", i, error->message,
			         error->code, cases[i].fragment, cases[i].code);
		g_assert_nonnull (strstr (error->message, "<data>: "));
	}

	/* a bone the skeleton lacks */
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(LrgSkeleton) skel = make_skeleton ();
		g_autoptr(LrgPhysicsAsset) asset =
			lrg_physics_asset_new_from_data ("version: 1\nparts:\n- bone: tail\n", -1, &error);
		g_autoptr(LrgRagdoll) ragdoll = NULL;

		g_assert_no_error (error);
		ragdoll = lrg_ragdoll_new (asset, skel, &error);
		g_assert_null (ragdoll);
		g_assert_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_SKELETON);
		g_assert_nonnull (strstr (error->message, "tail"));
	}
}

/* ------------------------------------------------------------------ */
/* the ragdoll                                                         */
/* ------------------------------------------------------------------ */

static LrgRagdoll *
make_ragdoll (LrgSkeleton *skel, LrgDynamicsWorld *world, gfloat height)
{
	g_autoptr(LrgPhysicsAsset) asset = lrg_physics_asset_new_from_skeleton (skel, 70.0f, 0.22f);
	g_autoptr(GError) error = NULL;
	LrgRagdoll *ragdoll;
	LrgBonePose root;

	ragdoll = lrg_ragdoll_new (asset, skel, &error);
	g_assert_no_error (error);
	lrg_bone_pose_set_identity (&root);
	root.position_y = height;
	lrg_ragdoll_set_root_transform (ragdoll, &root);
	if (world != NULL)
	{
		g_assert_true (lrg_ragdoll_add_to_world (ragdoll, world, &error));
		g_assert_no_error (error);
	}
	return ragdoll;
}

/* worst violation of any joint's limits (radians) and anchor separation (m) */
static void
joint_violation (LrgRagdoll *ragdoll, gfloat *angle, gfloat *separation)
{
	LrgDynamicsWorld *world = lrg_ragdoll_get_world (ragdoll);
	guint i;

	*angle = 0.0f;
	*separation = 0.0f;
	for (i = 0; i < lrg_ragdoll_get_part_count (ragdoll); i++)
	{
		LrgJoint *j = lrg_ragdoll_get_joint (ragdoll, i);
		gfloat tr[3];
		gfloat an[3];
		gfloat lo;
		gfloat hi;
		gfloat sy;
		gfloat sz;
		gfloat e;

		if (j == NULL)
			continue;
		g_assert_true (lrg_joint_measure (j, world, tr, an, NULL));
		*separation = MAX (*separation, sqrtf (tr[0] * tr[0] + tr[1] * tr[1] + tr[2] * tr[2]));
		lrg_joint_get_limits (j, LRG_JOINT_AXIS_ROTATION_X, &lo, &hi);
		*angle = MAX (*angle, MAX (lo - an[0], an[0] - hi));
		lrg_joint_get_limits (j, LRG_JOINT_AXIS_ROTATION_Y, NULL, &sy);
		lrg_joint_get_limits (j, LRG_JOINT_AXIS_ROTATION_Z, NULL, &sz);
		/* the cone is an ellipse on the swing half-angle sines; express
		 * the overshoot as an angle along the swing direction */
		{
			gfloat ty = tanf (0.5f * an[1]);
			gfloat tz = tanf (0.5f * an[2]);
			gfloat sw = 1.0f / sqrtf (1.0f + ty * ty + tz * tz);

			/* back to the swing quaternion's y, z: tan (a / 2) * w */
			e = sqrtf (powf (ty * sw / sinf (0.5f * sy), 2.0f) + powf (tz * sw / sinf (0.5f * sz), 2.0f));
		}
		if (e > 1.0f)
			*angle = MAX (*angle, (e - 1.0f) * MAX (sy, sz));
	}
}

static void
test_ragdoll_falls_and_settles (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (-9.81f, TRUE);
	g_autoptr(LrgSkeleton) skel = make_skeleton ();
	g_autoptr(LrgRagdoll) ragdoll = make_ragdoll (skel, world, 0.6f);
	gfloat worst_angle = 0.0f;
	gfloat worst_sep = 0.0f;
	gfloat rest_angle = 0.0f;
	gfloat rest_sep = 0.0f;
	gfloat lowest = 10.0f;
	gfloat speed = 0.0f;
	guint i;
	guint k;
	gfloat push[3] = { 1.5f, 0.0f, 0.5f };

	g_assert_cmpuint (lrg_ragdoll_get_part_count (ragdoll), ==, N_BONES);
	g_assert_cmpint (lrg_ragdoll_get_part_parent (ragdoll, 0), ==, -1);
	g_assert_cmpint (lrg_ragdoll_get_part_parent (ragdoll, 2), ==, 1);
	g_assert_null (lrg_ragdoll_get_joint (ragdoll, 0));
	g_assert_nonnull (lrg_ragdoll_get_joint (ragdoll, 4));
	g_assert_true (lrg_ragdoll_get_world (ragdoll) == world);
	lrg_ragdoll_set_linear_velocity (ragdoll, push);

	for (i = 0; i < 120 * 5; i++)
	{
		gfloat a;
		gfloat s;

		step (world, 1);
		joint_violation (ragdoll, &a, &s);
		worst_angle = MAX (worst_angle, a);
		worst_sep = MAX (worst_sep, s);
		if (i >= 120 * 4)
		{
			rest_angle = MAX (rest_angle, a);
			rest_sep = MAX (rest_sep, s);
		}
	}
	for (k = 0; k < lrg_ragdoll_get_part_count (ragdoll); k++)
	{
		gfloat p[3];
		gfloat v[3];

		g_assert_true (lrg_dynamics_world_get_position (world, lrg_ragdoll_get_body (ragdoll, k), p, NULL));
		g_assert_true (lrg_dynamics_world_get_linear_velocity (world, lrg_ragdoll_get_body (ragdoll, k),
		                                                       v, NULL));
		lowest = MIN (lowest, p[1]);
		speed = MAX (speed, sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
	}
	g_test_message ("ragdoll fall: during the impact limit overshoot %.4f rad and joint gap %.4f m; "
	                "at rest (last second) %.4f rad and %.4f m; lowest centre %.3f m, fastest part "
	                "after 5 s %.4f m/s",
	                (gdouble) worst_angle, (gdouble) worst_sep, (gdouble) rest_angle,
	                (gdouble) rest_sep, (gdouble) lowest, (gdouble) speed);
	/* the impact may push a joint briefly past its limit (one row per
	 * limit, 8 velocity iterations); at rest every joint is inside */
	g_assert_cmpfloat (worst_angle, <, 0.35f);
	g_assert_cmpfloat (worst_sep, <, 0.05f);
	g_assert_cmpfloat (rest_angle, <, 0.02f);
	g_assert_cmpfloat (rest_sep, <, 0.01f);
	g_assert_cmpfloat (lowest, >, 0.0f);
	g_assert_cmpfloat (lowest, <, 0.3f);
	g_assert_cmpfloat (speed, <, 0.1f);

	/* the skeleton follows the bodies */
	lrg_ragdoll_write_pose (ragdoll);
	{
		LrgBone *pelvis = lrg_skeleton_get_bone (skel, 0);
		const LrgBonePose *w = lrg_bone_get_world_pose (pelvis);
		gfloat p[3];

		g_assert_true (lrg_dynamics_world_get_position (world, lrg_ragdoll_get_body (ragdoll, 0), p, NULL));
		/* model space = world - root (0, 0.6, 0); the pelvis body is offset from the bone */
		g_assert_cmpfloat (fabsf (w->position_y + 0.6f - p[1]), <, 0.3f);
		g_assert_cmpfloat (w->position_y, <, 0.0f);
	}
}

/* rotation angle between two unit quaternions */
static gfloat
quat_angle (const LrgBonePose *a, const LrgBonePose *b)
{
	gfloat d;

	d = a->rotation_x * b->rotation_x + a->rotation_y * b->rotation_y +
	    a->rotation_z * b->rotation_z + a->rotation_w * b->rotation_w;
	return 2.0f * acosf (CLAMP (fabsf (d), 0.0f, 1.0f));
}

/*
 * A powered ragdoll in zero gravity with its pelvis pinned tracks a pose
 * that bends the elbows, knees and neck by 0.35 rad: after 2 s the
 * written-back local rotations match the target pose.
 */
static void
test_ragdoll_powered_tracks_pose (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (0.0f, FALSE);
	g_autoptr(LrgSkeleton) skel = make_skeleton ();
	g_autoptr(LrgSkeleton) target = make_skeleton ();
	g_autoptr(LrgRagdoll) ragdoll = make_ragdoll (skel, world, 0.0f);
	g_autoptr(LrgJoint) pin = NULL;
	g_autoptr(GError) error = NULL;
	static const guint bent[] = { 2, 4, 7, 10, 13 };
	gfloat worst = 0.0f;
	guint i;

	/* the target pose: rotate selected bones about Z */
	for (i = 0; i < G_N_ELEMENTS (bent); i++)
	{
		LrgBone *bone = lrg_skeleton_get_bone (target, (gint) bent[i]);
		LrgBonePose pose = *lrg_bone_get_local_pose (bone);

		pose.rotation_z = sinf (0.175f);
		pose.rotation_w = cosf (0.175f);
		lrg_skeleton_set_pose (target, (gint) bent[i], &pose);
	}
	lrg_skeleton_calculate_world_poses (target);

	pin = lrg_joint_new_fixed (world, lrg_ragdoll_get_body (ragdoll, 0), LRG_DYNAMICS_WORLD_BODY, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_dynamics_world_add_constraint (world, LRG_PHYSICS_CONSTRAINT (pin), NULL, &error));

	lrg_ragdoll_set_mode (ragdoll, LRG_RAGDOLL_POWERED);
	g_assert_cmpint (lrg_ragdoll_get_mode (ragdoll), ==, LRG_RAGDOLL_POWERED);
	for (i = 0; i < 240; i++)
	{
		lrg_ragdoll_drive_to_pose (ragdoll, target);
		step (world, 1);
	}
	lrg_ragdoll_write_pose (ragdoll);
	for (i = 1; i < N_BONES; i++)
	{
		gfloat a = quat_angle (lrg_bone_get_local_pose (lrg_skeleton_get_bone (skel, (gint) i)),
		                       lrg_bone_get_local_pose (lrg_skeleton_get_bone (target, (gint) i)));

		worst = MAX (worst, a);
	}
	g_test_message ("powered ragdoll: worst local rotation error %.4f rad after 2 s", (gdouble) worst);
	g_assert_cmpfloat (worst, <, 0.05f);

	/* passive again: the motors are off */
	lrg_ragdoll_set_mode (ragdoll, LRG_RAGDOLL_PASSIVE);
	g_assert_cmpint (lrg_joint_get_motor_mode (lrg_ragdoll_get_joint (ragdoll, 4), LRG_JOINT_AXIS_ROTATION_Y),
	                 ==, LRG_JOINT_MOTOR_OFF);
}

/* blend 0 keeps the animation, per-bone weights mix */
static void
test_ragdoll_blend (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (-9.81f, TRUE);
	g_autoptr(LrgSkeleton) skel = make_skeleton ();
	g_autoptr(LrgRagdoll) ragdoll = make_ragdoll (skel, world, 0.6f);
	LrgBonePose before;
	LrgBonePose spine_before;
	const LrgBonePose *after;

	step (world, 60);
	before = *lrg_bone_get_local_pose (lrg_skeleton_get_bone (skel, 4));
	spine_before = *lrg_bone_get_local_pose (lrg_skeleton_get_bone (skel, 0));
	lrg_ragdoll_set_blend (ragdoll, 0.0f);
	g_assert_cmpfloat (lrg_ragdoll_get_bone_blend (ragdoll, 3), ==, 0.0f);
	lrg_ragdoll_write_pose (ragdoll);
	after = lrg_bone_get_local_pose (lrg_skeleton_get_bone (skel, 4));
	g_assert_true (lrg_bone_pose_equal (&before, after));

	/* the root bone follows physics fully, the forearm half */
	lrg_ragdoll_set_bone_blend (ragdoll, 0, 1.0f);
	lrg_ragdoll_set_bone_blend (ragdoll, 4, 0.5f);
	g_assert_cmpfloat (lrg_ragdoll_get_bone_blend (ragdoll, 4), ==, 0.5f);
	lrg_ragdoll_write_pose (ragdoll);
	after = lrg_bone_get_local_pose (lrg_skeleton_get_bone (skel, 0));
	g_assert_false (lrg_bone_pose_equal (&spine_before, after));
}

static void
test_ragdoll_add_remove (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (-9.81f, TRUE);
	g_autoptr(LrgSkeleton) skel = make_skeleton ();
	g_autoptr(LrgRagdoll) ragdoll = make_ragdoll (skel, NULL, 1.0f);
	g_autoptr(GError) error = NULL;
	guint bodies;
	guint constraints;
	LrgBonePose root;

	bodies = lrg_dynamics_world_get_body_count (world);
	constraints = lrg_dynamics_world_get_constraint_count (world);
	g_assert_null (lrg_ragdoll_get_world (ragdoll));
	g_assert_cmpuint (lrg_ragdoll_get_body (ragdoll, 0), ==, G_MAXUINT);
	g_assert_true (lrg_ragdoll_get_asset (ragdoll) != NULL);
	g_assert_true (lrg_ragdoll_get_skeleton (ragdoll) == skel);
	g_assert_cmpint (lrg_ragdoll_get_part_bone (ragdoll, 3), ==, 3);
	lrg_ragdoll_get_root_transform (ragdoll, &root);
	g_assert_cmpfloat (root.position_y, ==, 1.0f);

	g_assert_true (lrg_ragdoll_add_to_world (ragdoll, world, &error));
	g_assert_cmpuint (lrg_dynamics_world_get_body_count (world), ==, bodies + N_BONES);
	g_assert_cmpuint (lrg_dynamics_world_get_constraint_count (world), ==, constraints + N_BONES - 1);
	{
		guint layer;

		g_assert_true (lrg_dynamics_world_get_body_layer (world, lrg_ragdoll_get_body (ragdoll, 1),
		                                                  &layer, NULL, NULL));
		g_assert_cmpuint (layer, ==, LRG_PHYSICS_LAYER_RAGDOLL);
	}
	g_assert_false (lrg_ragdoll_add_to_world (ragdoll, world, &error));
	g_assert_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_STATE);
	g_clear_error (&error);
	step (world, 10);
	lrg_ragdoll_remove_from_world (ragdoll);
	g_assert_cmpuint (lrg_dynamics_world_get_body_count (world), ==, bodies);
	g_assert_cmpuint (lrg_dynamics_world_get_constraint_count (world), ==, constraints);

	/* and back in again, placed from the skeleton */
	g_assert_true (lrg_ragdoll_add_to_world (ragdoll, world, &error));
	g_assert_no_error (error);
	step (world, 10);
}

static void
test_ragdoll_determinism (void)
{
	g_autoptr(LrgDynamicsWorld) wa = make_world (-9.81f, TRUE);
	g_autoptr(LrgDynamicsWorld) wb = make_world (-9.81f, TRUE);
	g_autoptr(LrgSkeleton) sa = make_skeleton ();
	g_autoptr(LrgSkeleton) sb = make_skeleton ();
	g_autoptr(LrgRagdoll) ra = make_ragdoll (sa, wa, 0.8f);
	g_autoptr(LrgRagdoll) rb = make_ragdoll (sb, wb, 0.8f);
	gfloat push[3] = { 2.0f, 1.0f, -1.0f };
	guint k;

	lrg_ragdoll_set_linear_velocity (ra, push);
	lrg_ragdoll_set_linear_velocity (rb, push);
	step (wa, 240);
	step (wb, 240);
	for (k = 0; k < N_BONES; k++)
	{
		gfloat pa[3];
		gfloat pb[3];

		g_assert_true (lrg_dynamics_world_get_position (wa, lrg_ragdoll_get_body (ra, k), pa, NULL));
		g_assert_true (lrg_dynamics_world_get_position (wb, lrg_ragdoll_get_body (rb, k), pb, NULL));
		g_assert_cmpint (memcmp (pa, pb, sizeof (pa)), ==, 0);
	}
}

static void
test_ragdoll_performance (void)
{
	g_autoptr(LrgDynamicsWorld) world = make_world (-9.81f, TRUE);
	g_autoptr(GPtrArray) skels = g_ptr_array_new_with_free_func (g_object_unref);
	g_autoptr(GPtrArray) dolls = g_ptr_array_new_with_free_func (g_object_unref);
	gint64 t0;
	gint64 t1;
	guint i;

	for (i = 0; i < 10; i++)
	{
		LrgSkeleton *skel = make_skeleton ();
		LrgRagdoll *doll;
		LrgBonePose root;
		g_autoptr(GError) error = NULL;

		doll = lrg_ragdoll_new (lrg_physics_asset_new_from_skeleton (skel, 70.0f, 0.22f), skel, &error);
		g_object_unref (lrg_ragdoll_get_asset (doll));
		lrg_bone_pose_set_identity (&root);
		root.position_x = (gfloat) (i % 5) * 1.5f;
		root.position_z = (gfloat) (i / 5) * 1.5f;
		root.position_y = 0.5f + 0.3f * (gfloat) i;
		lrg_ragdoll_set_root_transform (doll, &root);
		g_assert_true (lrg_ragdoll_add_to_world (doll, world, &error));
		g_ptr_array_add (skels, skel);
		g_ptr_array_add (dolls, doll);
	}
	t0 = g_get_monotonic_time ();
	for (i = 0; i < 240; i++)
	{
		guint k;

		step (world, 1);
		for (k = 0; k < dolls->len; k++)
			lrg_ragdoll_write_pose (g_ptr_array_index (dolls, k));
	}
	t1 = g_get_monotonic_time ();
	g_test_message ("perf: 10 ragdolls (140 bodies, 130 joints) falling, %.3f ms per 1/120 s step "
	                "including pose write-back", (gdouble) (t1 - t0) / 1000.0 / 240.0);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/ragdoll/asset/fixture-round-trip", test_asset_fixture_round_trip);
	g_test_add_func ("/ragdoll/asset/file-and-generated", test_asset_file_and_generated_round_trip);
	g_test_add_func ("/ragdoll/asset/edit", test_asset_edit);
	g_test_add_func ("/ragdoll/asset/strict-errors", test_asset_strict_errors);
	g_test_add_func ("/ragdoll/falls-and-settles", test_ragdoll_falls_and_settles);
	g_test_add_func ("/ragdoll/powered-tracks-pose", test_ragdoll_powered_tracks_pose);
	g_test_add_func ("/ragdoll/blend", test_ragdoll_blend);
	g_test_add_func ("/ragdoll/add-remove", test_ragdoll_add_remove);
	g_test_add_func ("/ragdoll/determinism", test_ragdoll_determinism);
	g_test_add_func ("/ragdoll/performance", test_ragdoll_performance);

	return g_test_run ();
}
