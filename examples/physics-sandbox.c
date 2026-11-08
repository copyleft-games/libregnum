/* physics-sandbox.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A 3D physics playground built only from LrgEntity3D components: every
 * object is an entity with an LrgMeshRendererComponent (what you see), an
 * LrgRigidBodyComponent (the body) and LrgColliderComponent3D (its
 * shapes); joints are LrgJointComponents.  The ragdoll is an
 * LrgRagdoll built from a skeleton.  LrgWorld3D owns the
 * LrgDynamicsWorld and steps it in its fixed updates, so the game loop is
 * lrg_world3d_step() + LrgWorld3DRenderer, as in any other 3D game.
 *
 * The scene:
 *   - a box pyramid and a tall tower to knock over,
 *   - two ramps with balls and a crate sliding down,
 *   - a passive ragdoll (LrgPhysicsAsset from a short skeleton),
 *   - a chain of ten links hanging from a fixed point (point joints),
 *   - a breakable wall: bricks welded by fixed joints with a break force,
 *   - a cloth flag (LrgCloth, position-based dynamics) waving in the wind.
 *
 * Controls (also in the on-screen legend):
 *   Space / left click   shoot a sphere from the camera (CCD on)
 *   right mouse + drag   orbit,  mouse wheel  zoom,  W/A/S/D  move the focus
 *   F1 shapes  F2 contacts  F3 AABBs  F4 joints   (physics debug draw)
 *   G  wind on/off   P  pause   R  rebuild the scene   Esc  quit
 *
 * Run with --help for options (--screenshot for an unattended capture,
 * --self-test builds the scene without a window).
 */

#include <libregnum.h>
#include <graylib.h>
#include <raylib.h>
#include <rlgl.h>
#include <math.h>
#include <string.h>

#define WINDOW_W        (1920)
#define WINDOW_H        (1080)
#define FIXED_DT        (1.0f / 60.0f)
#define SHOT_SPEED      (28.0f)
#define SHOT_RADIUS     (0.3f)
#define MAX_SHOTS       (40)
#define ORBIT_SPEED     (0.005f)
#define FOCUS_SPEED     (8.0f)

static const gchar *license_text =
	"physics-sandbox - libregnum 3D physics example\n"
	"Copyright (C) 2026  Zach Podbielniak\n\n"
	"This program is free software: you can redistribute it and/or modify\n"
	"it under the terms of the GNU Affero General Public License as published by\n"
	"the Free Software Foundation, either version 3 of the License, or\n"
	"(at your option) any later version.\n\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU Affero General Public License for more details.\n\n"
	"You should have received a copy of the GNU Affero General Public License\n"
	"along with this program.  If not, see <https://www.gnu.org/licenses/>.\n";

/* ==========================================================================
 * Sandbox state
 * ========================================================================== */

typedef struct
{
	LrgWorld3D   *world;
	LrgRagdoll   *ragdoll;
	LrgCamera3D  *camera;
	LrgCloth     *flag;
	GPtrArray    *shots;          /* LrgEntity3D, borrowed (world owns them) */
	guint         bricks_broken;

	/* orbit camera */
	gfloat        focus[3];
	gfloat        yaw;
	gfloat        pitch;
	gfloat        distance;

	/* toggles */
	LrgDynamicsDebugFlags debug;
	gboolean      wind;
	gboolean      paused;
	gfloat        time;
} Sandbox;

/* ==========================================================================
 * Entity helpers
 * ========================================================================== */

/*
 * add_visual:
 *
 * The render side of an object: a primitive mesh renderer sized like its
 * collider (unit cube scaled to the box, sphere / cylinder radius).
 */
static void
add_visual (LrgEntity3D      *entity,
            LrgPrimitiveType  primitive,
            const gfloat     *size,
            gfloat            r,
            gfloat            g,
            gfloat            b)
{
	g_autoptr(LrgMeshRendererComponent) mr = lrg_mesh_renderer_component_new_primitive (primitive);

	if (primitive == LRG_PRIMITIVE_CUBE)
		lrg_mesh_renderer_component_set_size (mr, size[0], size[1], size[2]);
	else
		g_object_set (mr, "radius", (gdouble) size[0], "height", (gdouble) size[1], NULL);
	lrg_mesh_renderer_component_set_color (mr, r, g, b, 1.0f);
	g_object_set (mr, "roughness", 0.6, NULL);
	lrg_entity3d_add_component (entity, LRG_COMPONENT (mr));
}

/*
 * spawn:
 *
 * One physics object: an entity at @pos with a body of @kind (mass @mass)
 * and @collider (transfer full), plus a visual.  Returns the entity
 * (owned by the world).
 */
static LrgEntity3D *
spawn (Sandbox                *sb,
       const gchar            *name,
       const gfloat           *pos,
       LrgDynamicsBodyKind     kind,
       gfloat                  mass,
       LrgColliderComponent3D *collider)
{
	g_autoptr(LrgEntity3D)           e = lrg_entity3d_new_with_name (name);
	g_autoptr(LrgRigidBodyComponent) body = lrg_rigid_body_component_new (kind);

	lrg_entity3d_set_position (e, pos);
	lrg_rigid_body_component_set_mass (body, mass);
	lrg_entity3d_add_component (e, LRG_COMPONENT (body));
	lrg_entity3d_add_component (e, LRG_COMPONENT (collider));
	g_object_unref (collider);
	lrg_world3d_add_entity (sb->world, e);
	return e;
}

/* A box object of half extents @he. */
static LrgEntity3D *
spawn_box (Sandbox             *sb,
           const gchar         *name,
           gfloat               x,
           gfloat               y,
           gfloat               z,
           const gfloat        *he,
           LrgDynamicsBodyKind  kind,
           gfloat               mass,
           const gfloat        *rgb)
{
	const gfloat  pos[3] = { x, y, z };
	const gfloat  size[3] = { he[0] * 2.0f, he[1] * 2.0f, he[2] * 2.0f };
	LrgEntity3D  *e;

	e = spawn (sb, name, pos, kind, mass, lrg_collider_component3d_new_box (he[0], he[1], he[2]));
	add_visual (e, LRG_PRIMITIVE_CUBE, size, rgb[0], rgb[1], rgb[2]);
	return e;
}

/* A sphere object. */
static LrgEntity3D *
spawn_sphere (Sandbox      *sb,
              const gchar  *name,
              const gfloat *pos,
              gfloat        radius,
              gfloat        mass,
              const gfloat *rgb)
{
	const gfloat  size[3] = { radius, 0.0f, 0.0f };
	LrgEntity3D  *e;

	e = spawn (sb, name, pos, LRG_DYNAMICS_BODY_DYNAMIC, mass, lrg_collider_component3d_new_sphere (radius));
	add_visual (e, LRG_PRIMITIVE_UV_SPHERE, size, rgb[0], rgb[1], rgb[2]);
	return e;
}

/* Adds a joint component (transfer full) to @entity. */
static LrgJointComponent *
add_joint (LrgEntity3D       *entity,
           LrgJointComponent *joint)
{
	lrg_entity3d_add_component (entity, LRG_COMPONENT (joint));
	g_object_unref (joint);
	return joint;
}

/* ==========================================================================
 * Scene
 * ========================================================================== */

static const gfloat COL_GREY[3]   = { 0.55f, 0.56f, 0.58f };
static const gfloat COL_SAND[3]   = { 0.86f, 0.72f, 0.48f };
static const gfloat COL_TEAL[3]   = { 0.20f, 0.62f, 0.66f };
static const gfloat COL_BRICK[3]  = { 0.72f, 0.30f, 0.22f };
static const gfloat COL_CLOTHC[3] = { 0.25f, 0.35f, 0.75f };
static const gfloat COL_STEEL[3]  = { 0.70f, 0.72f, 0.78f };
static const gfloat COL_GOLD[3]   = { 0.95f, 0.75f, 0.25f };

/* The floor and two ramps: static bodies (rigid body kind static). */
static void
build_ground (Sandbox *sb)
{
	const gfloat  floor_he[3] = { 30.0f, 0.5f, 30.0f };
	const gfloat  ramp_he[3] = { 3.0f, 0.2f, 1.5f };
	LrgEntity3D  *ramp;
	gfloat        euler[3] = { 0.0f, 0.0f, 0.0f };
	guint         i;

	spawn_box (sb, "floor", 0.0f, -0.5f, 0.0f, floor_he, LRG_DYNAMICS_BODY_STATIC, 0.0f, COL_GREY);

	/* ramp 1 tilted 25 degrees about Z, ramp 2 tilted the other way below it */
	ramp = spawn_box (sb, "ramp-a", 9.0f, 3.2f, -6.0f, ramp_he, LRG_DYNAMICS_BODY_STATIC, 0.0f, COL_SAND);
	euler[2] = 25.0f * (gfloat) G_PI / 180.0f;
	lrg_entity3d_set_rotation_euler (ramp, euler);
	ramp = spawn_box (sb, "ramp-b", 13.5f, 1.2f, -6.0f, ramp_he, LRG_DYNAMICS_BODY_STATIC, 0.0f, COL_SAND);
	euler[2] = -15.0f * (gfloat) G_PI / 180.0f;
	lrg_entity3d_set_rotation_euler (ramp, euler);

	/* things to slide and roll down the ramps */
	for (i = 0; i < 3; i++)
	{
		const gfloat     pos[3] = { 7.0f + 0.2f * (gfloat) i, 6.5f + 1.2f * (gfloat) i, -6.6f + 0.6f * (gfloat) i };
		g_autofree gchar *name = g_strdup_printf ("ball-%u", i);

		spawn_sphere (sb, name, pos, 0.35f, 2.0f, COL_GOLD);
	}
	{
		const gfloat crate_he[3] = { 0.4f, 0.4f, 0.4f };

		spawn_box (sb, "crate", 7.5f, 5.5f, -5.4f, crate_he, LRG_DYNAMICS_BODY_DYNAMIC, 4.0f, COL_SAND);
	}
}

/* A 5-row box pyramid and a 6-box tower. */
static void
build_stacks (Sandbox *sb)
{
	const gfloat he[3] = { 0.45f, 0.45f, 0.45f };
	guint        row;
	guint        i;

	for (row = 0; row < 5; row++)
		for (i = 0; i < 5 - row; i++)
		{
			g_autofree gchar *name = g_strdup_printf ("pyramid-%u-%u", row, i);
			gfloat            x = -2.0f + (gfloat) i * 0.92f + (gfloat) row * 0.46f;

			spawn_box (sb, name, x, 0.45f + (gfloat) row * 0.905f, 2.0f, he, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f,
			           (row % 2) == 0 ? COL_TEAL : COL_SAND);
		}

	for (i = 0; i < 6; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("tower-%u", i);
		const gfloat      the[3] = { 0.6f, 0.3f, 0.6f };

		spawn_box (sb, name, 4.0f, 0.3f + (gfloat) i * 0.6f, 4.0f, the, LRG_DYNAMICS_BODY_DYNAMIC, 1.0f,
		           (i % 2) == 0 ? COL_STEEL : COL_TEAL);
	}
}

/*
 * build_wall:
 *
 * A 6 x 5 brick wall in running bond.  Each brick is welded to its left
 * and lower neighbours with a fixed joint that breaks above 400 N: a
 * shot knocks a hole and the rest stays standing until hit.
 */
static void
on_brick_broken (LrgJointComponent *joint,
                 gpointer           data)
{
	((Sandbox *) data)->bricks_broken++;
}

static void
build_wall (Sandbox *sb)
{
	const gfloat  he[3] = { 0.5f, 0.25f, 0.25f };
	LrgEntity3D  *grid[5][6];
	guint         row;
	guint         col;

	for (row = 0; row < 5; row++)
		for (col = 0; col < 6; col++)
		{
			g_autofree gchar *name = g_strdup_printf ("brick-%u-%u", row, col);
			gfloat            x = -1.0f - 3.0f + (gfloat) col * 1.0f + ((row % 2) ? 0.5f : 0.0f);
			gfloat            y = 0.25f + (gfloat) row * 0.5f;

			grid[row][col] = spawn_box (sb, name, x, y, -8.0f, he, LRG_DYNAMICS_BODY_DYNAMIC, 2.0f, COL_BRICK);
		}

	for (row = 0; row < 5; row++)
		for (col = 0; col < 6; col++)
		{
			LrgEntity3D *brick = grid[row][col];

			if (col > 0)
			{
				LrgJointComponent *weld = add_joint (brick, lrg_joint_component_new (LRG_JOINT_FIXED));

				lrg_joint_component_set_target (weld, grid[row][col - 1]);
				lrg_joint_component_set_break_force (weld, 400.0f);
				g_signal_connect (weld, "broken", G_CALLBACK (on_brick_broken), sb);
			}
			if (row > 0)
			{
				LrgJointComponent *weld = add_joint (brick, lrg_joint_component_new (LRG_JOINT_FIXED));

				lrg_joint_component_set_target (weld, grid[row - 1][col]);
				lrg_joint_component_set_break_force (weld, 400.0f);
				g_signal_connect (weld, "broken", G_CALLBACK (on_brick_broken), sb);
			}
		}
}

/* A chain of ten links hanging from a fixed point at (-6, 8, 3). */
static void
build_chain (Sandbox *sb)
{
	LrgEntity3D *prev = NULL;
	guint        i;

	for (i = 0; i < 10; i++)
	{
		g_autofree gchar  *name = g_strdup_printf ("link-%u", i);
		const gfloat       pos[3] = { -6.0f + 0.5f * (gfloat) (i + 1), 8.0f, 3.0f };
		LrgEntity3D       *link = spawn_sphere (sb, name, pos, 0.18f, 0.5f, COL_STEEL);
		LrgJointComponent *j = add_joint (link, lrg_joint_component_new (LRG_JOINT_POINT));

		/* the pivot is half a link towards the previous one (or the hook) */
		lrg_joint_component_set_anchor (j, -0.25f, 0.0f, 0.0f);
		if (prev != NULL)
			lrg_joint_component_set_target (j, prev);
		prev = link;
	}
	/* a weight at the end */
	{
		const gfloat pos[3] = { -6.0f + 0.5f * 11.0f, 8.0f, 3.0f };
		LrgEntity3D *weight = spawn_sphere (sb, "chain-weight", pos, 0.4f, 6.0f, COL_GOLD);
		LrgJointComponent *j = add_joint (weight, lrg_joint_component_new (LRG_JOINT_POINT));

		lrg_joint_component_set_anchor (j, -0.5f, 0.0f, 0.0f);
		lrg_joint_component_set_target (j, prev);
	}
}

/* Pelvis at the origin; the root transform stands the doll in the scene. */
static const struct
{
	const gchar *name;
	gint         parent;
	gfloat       x;
	gfloat       y;
	gfloat       z;
} ragdoll_bones[] = {
	{ "pelvis", -1, 0.0f, 0.0f, 0.0f },
	{ "spine", 0, 0.0f, 0.40f, 0.0f },
	{ "head", 1, 0.0f, 0.35f, 0.0f },
	{ "thigh_l", 0, 0.12f, -0.05f, 0.0f },
	{ "shin_l", 3, 0.0f, -0.45f, 0.0f },
};

/*
 * build_ragdoll:
 *
 * A passive LrgRagdoll: one capsule per bone from
 * lrg_physics_asset_new_from_skeleton(), posed where the old box doll
 * stood, in the world's dynamics world.
 */
static void
build_ragdoll (Sandbox *sb)
{
	g_autoptr(LrgSkeleton)     skel = lrg_skeleton_new ();
	g_autoptr(LrgPhysicsAsset) asset = NULL;
	g_autoptr(GError)          error = NULL;
	LrgBonePose                root;
	LrgDynamicsWorld          *physics;
	guint                      i;

	for (i = 0; i < G_N_ELEMENTS (ragdoll_bones); i++)
	{
		g_autoptr(LrgBone) bone = lrg_bone_new (ragdoll_bones[i].name, (gint) i);
		LrgBonePose        pose;

		lrg_bone_pose_set_identity (&pose);
		pose.position_x = ragdoll_bones[i].x;
		pose.position_y = ragdoll_bones[i].y;
		pose.position_z = ragdoll_bones[i].z;
		lrg_bone_set_parent_index (bone, ragdoll_bones[i].parent);
		lrg_bone_set_bind_pose (bone, &pose);
		lrg_bone_set_local_pose (bone, &pose);
		lrg_skeleton_add_bone (skel, bone);
	}
	lrg_skeleton_calculate_world_poses (skel);
	asset = lrg_physics_asset_new_from_skeleton (skel, 70.0f, 0.22f);
	g_clear_object (&sb->ragdoll);
	sb->ragdoll = lrg_ragdoll_new (asset, skel, &error);
	if (sb->ragdoll == NULL)
	{
		g_printerr ("physics-sandbox: ragdoll: %s\n", error->message);
		return;
	}
	lrg_bone_pose_set_identity (&root);
	root.position_x = 9.5f;
	root.position_y = 9.0f;
	root.position_z = -6.0f;
	lrg_ragdoll_set_root_transform (sb->ragdoll, &root);
	lrg_ragdoll_set_mode (sb->ragdoll, LRG_RAGDOLL_PASSIVE);
	physics = lrg_world3d_get_physics (sb->world);
	if (!lrg_ragdoll_add_to_world (sb->ragdoll, physics, &error))
	{
		g_printerr ("physics-sandbox: ragdoll: %s\n", error->message);
		g_clear_object (&sb->ragdoll);
	}
}

/* The flag pole (a static body) and its cloth. */
static void
build_flag (Sandbox *sb)
{
	const gfloat pole_pos[3] = { -9.0f, 3.0f, -3.0f };
	const gfloat pole_size[3] = { 0.08f, 6.0f, 0.0f };
	LrgEntity3D *pole;
	gfloat       xf[16];
	const gfloat top[3] = { -9.0f, 5.9f, -3.0f };
	const gfloat q[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	const gfloat one[3] = { 1.0f, 1.0f, 1.0f };
	guint        cols;
	guint        rows;
	guint        r;

	pole = spawn (sb, "flag-pole", pole_pos, LRG_DYNAMICS_BODY_STATIC, 0.0f,
	              lrg_collider_component3d_new_capsule (0.08f, 2.95f));
	add_visual (pole, LRG_PRIMITIVE_CYLINDER, pole_size, COL_STEEL[0], COL_STEEL[1], COL_STEEL[2]);

	g_clear_object (&sb->flag);
	sb->flag = lrg_cloth_new_grid (24, 16, 3.0f, 2.0f);
	lrg_math3d_compose_quat (top, q, one, xf);
	lrg_cloth_set_transform (sb->flag, xf);
	lrg_cloth_reset (sb->flag);
	lrg_cloth_get_grid_size (sb->flag, &cols, &rows);
	for (r = 0; r < rows; r++)
		lrg_cloth_pin (sb->flag, lrg_cloth_get_grid_index (sb->flag, 0, r));
	{
		const gfloat g[3] = { 0.0f, -9.81f, 0.0f };

		lrg_cloth_set_gravity (sb->flag, g);
	}
}

/* (Re)builds the whole scene in a fresh world. */
static void
build_scene (Sandbox *sb)
{
	/* The ragdoll's dispose removes its bodies, so it goes before the world. */
	g_clear_object (&sb->ragdoll);
	g_clear_object (&sb->world);
	sb->world = lrg_world3d_new ();
	lrg_world3d_set_fixed_step (sb->world, FIXED_DT);
	lrg_world3d_ensure_physics (sb->world);   /* gravity -9.81 Y, default layers */
	g_ptr_array_set_size (sb->shots, 0);
	sb->bricks_broken = 0;

	build_ground (sb);
	build_stacks (sb);
	build_wall (sb);
	build_chain (sb);
	build_ragdoll (sb);
	build_flag (sb);
}

/* ==========================================================================
 * Shooting, camera, input
 * ========================================================================== */

/* Camera eye from the orbit parameters. */
static void
orbit_eye (Sandbox *sb,
           gfloat  *eye)
{
	eye[0] = sb->focus[0] + sb->distance * cosf (sb->pitch) * sinf (sb->yaw);
	eye[1] = sb->focus[1] + sb->distance * sinf (sb->pitch);
	eye[2] = sb->focus[2] + sb->distance * cosf (sb->pitch) * cosf (sb->yaw);
}

static void
update_camera (Sandbox *sb)
{
	gfloat eye[3];

	orbit_eye (sb, eye);
	lrg_camera3d_set_position_xyz (sb->camera, eye[0], eye[1], eye[2]);
	lrg_camera3d_set_target_xyz (sb->camera, sb->focus[0], sb->focus[1], sb->focus[2]);
}

/*
 * shoot:
 *
 * A heavy CCD sphere from the eye towards @target (the focus when NULL),
 * with an initial velocity given before its body exists (the component
 * applies it when the body is built).  The oldest shot goes when there
 * are too many.
 */
static void
shoot (Sandbox      *sb,
       const gfloat *target)
{
	gfloat                 eye[3];
	gfloat                 dir[3];
	gfloat                 len;
	gfloat                 v[3];
	LrgEntity3D           *ball;
	LrgRigidBodyComponent *body;
	g_autofree gchar      *name = NULL;
	guint                  k;

	orbit_eye (sb, eye);
	for (k = 0; k < 3; k++)
		dir[k] = (target != NULL ? target[k] : sb->focus[k]) - eye[k];
	len = sqrtf (dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
	if (len < 1e-4f)
		return;
	for (k = 0; k < 3; k++)
	{
		v[k] = dir[k] / len * SHOT_SPEED;
		eye[k] += dir[k] / len * 1.0f;   /* start in front of the camera */
	}

	name = g_strdup_printf ("shot-%u", sb->shots->len);
	ball = spawn_sphere (sb, name, eye, SHOT_RADIUS, 40.0f, COL_GOLD);
	body = LRG_RIGID_BODY_COMPONENT (lrg_entity3d_get_component (ball, LRG_TYPE_RIGID_BODY_COMPONENT));
	lrg_rigid_body_component_set_ccd (body, TRUE);
	lrg_rigid_body_component_set_linear_velocity (body, v);
	g_ptr_array_add (sb->shots, ball);

	if (sb->shots->len > MAX_SHOTS)
	{
		lrg_world3d_remove_entity (sb->world, g_ptr_array_index (sb->shots, 0));
		g_ptr_array_remove_index (sb->shots, 0);
	}
}

static void
handle_input (Sandbox *sb,
              gfloat   delta)
{
	gfloat wheel;
	gfloat fwd[2];
	gfloat move = FOCUS_SPEED * delta;

	if (IsMouseButtonDown (MOUSE_BUTTON_RIGHT))
	{
		Vector2 d = GetMouseDelta ();

		sb->yaw -= d.x * ORBIT_SPEED;
		sb->pitch = CLAMP (sb->pitch + d.y * ORBIT_SPEED, 0.05f, 1.45f);
	}
	wheel = GetMouseWheelMove ();
	if (wheel != 0.0f)
		sb->distance = CLAMP (sb->distance * (1.0f - wheel * 0.1f), 4.0f, 80.0f);

	/* W/S/A/D move the focus on the ground plane, relative to the view */
	fwd[0] = -sinf (sb->yaw);
	fwd[1] = -cosf (sb->yaw);
	if (IsKeyDown (KEY_W)) { sb->focus[0] += fwd[0] * move; sb->focus[2] += fwd[1] * move; }
	if (IsKeyDown (KEY_S)) { sb->focus[0] -= fwd[0] * move; sb->focus[2] -= fwd[1] * move; }
	if (IsKeyDown (KEY_A)) { sb->focus[0] += fwd[1] * move; sb->focus[2] -= fwd[0] * move; }
	if (IsKeyDown (KEY_D)) { sb->focus[0] -= fwd[1] * move; sb->focus[2] += fwd[0] * move; }

	if (IsKeyPressed (KEY_SPACE) || IsMouseButtonPressed (MOUSE_BUTTON_LEFT))
		shoot (sb, NULL);
	if (IsKeyPressed (KEY_F1)) sb->debug ^= LRG_DYNAMICS_DEBUG_SHAPES;
	if (IsKeyPressed (KEY_F2)) sb->debug ^= LRG_DYNAMICS_DEBUG_CONTACTS;
	if (IsKeyPressed (KEY_F3)) sb->debug ^= LRG_DYNAMICS_DEBUG_AABBS;
	if (IsKeyPressed (KEY_F4)) sb->debug ^= LRG_DYNAMICS_DEBUG_CONSTRAINTS;
	if (IsKeyPressed (KEY_G)) sb->wind = !sb->wind;
	if (IsKeyPressed (KEY_P)) sb->paused = !sb->paused;
	if (IsKeyPressed (KEY_R)) build_scene (sb);
}

/* Gusty wind along +X with a little Z, or still air. */
static void
update_flag (Sandbox *sb,
             gfloat   delta)
{
	gfloat wind[3] = { 0.0f, 0.0f, 0.0f };

	if (sb->wind)
	{
		gfloat gust = 1.0f + 0.5f * sinf (sb->time * 1.7f) + 0.25f * sinf (sb->time * 4.3f);

		wind[0] = 7.0f * gust;
		wind[2] = 2.0f * sinf (sb->time * 0.6f);
	}
	lrg_cloth_set_wind (sb->flag, wind);
	lrg_cloth_step (sb->flag, delta);
}

/* ==========================================================================
 * HUD
 * ========================================================================== */

static void
draw_hud (Sandbox *sb)
{
	LrgDynamicsWorld   *physics = lrg_world3d_get_physics (sb->world);
	g_autofree gchar   *stats = NULL;
	static const gchar *title = "libregnum physics sandbox";
	static const gchar *legend[] = {
		"Space / LMB   shoot a sphere",
		"RMB drag      orbit     wheel  zoom     W/A/S/D  move focus",
		"F1 shapes   F2 contacts   F3 AABBs   F4 joints",
		"G wind   P pause   R reset   Esc quit",
		NULL
	};
	const gint pad = 12;
	const gint x = 8 + pad;
	gint       width;
	gint       height;
	gint       y;
	guint      i;

	stats = g_strdup_printf ("bodies %u   awake %u   contacts %u   joints %u   welds broken %u   wind %s%s",
	                         lrg_dynamics_world_get_body_count (physics),
	                         lrg_dynamics_world_get_active_body_count (physics),
	                         lrg_dynamics_world_get_contact_count (physics),
	                         lrg_dynamics_world_get_constraint_count (physics),
	                         sb->bricks_broken, sb->wind ? "on" : "off",
	                         sb->paused ? "   PAUSED" : "");

	/* the panel fits the widest measured line plus padding */
	width = MeasureText (title, 26);
	for (i = 0; legend[i] != NULL; i++)
		width = MAX (width, MeasureText (legend[i], 20));
	width = MAX (width, MeasureText (stats, 20));
	height = 26 + 8 + (gint) i * 24 + 4 + 20;
	DrawRectangle (8, 8, width + 2 * pad, height + 2 * pad, (Color) { 10, 14, 22, 190 });

	y = 8 + pad;
	DrawText (title, x, y, 26, RAYWHITE);
	y += 34;
	for (i = 0; legend[i] != NULL; i++, y += 24)
		DrawText (legend[i], x, y, 20, (Color) { 200, 210, 225, 255 });
	DrawText (stats, x, y + 4, 20, (Color) { 255, 214, 120, 255 });
	DrawFPS (GetScreenWidth () - 100, 12);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GOptionContext)     ctx = NULL;
	g_autoptr(GError)             error = NULL;
	g_autoptr(GrlWindow)          window = NULL;
	g_autoptr(LrgWorld3DRenderer) renderer = NULL;
	g_autoptr(LrgDebugDraw)       dd = NULL;
	g_autofree gchar             *opt_screenshot = NULL;
	gint                          opt_frames = 150;
	gboolean                      opt_license = FALSE;
	gboolean                      opt_self_test = FALSE;
	gint                          frame = 0;
	Sandbox                       sb;

	GOptionEntry entries[] = {
		{ "screenshot", 's', 0, G_OPTION_ARG_FILENAME, &opt_screenshot,
		  "Save a PNG after --frames frames and exit (shoots the wall on its own)", "PATH" },
		{ "frames", 'f', 0, G_OPTION_ARG_INT, &opt_frames,
		  "Frames simulated before --screenshot (default 150)", "N" },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Show the license and exit", NULL },
		{ "self-test", 0, 0, G_OPTION_ARG_NONE, &opt_self_test,
		  "Build the scene without a window and exit", NULL },
		{ NULL }
	};

	ctx = g_option_context_new ("- a 3D physics playground");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Controls: Space / left click shoot, right mouse drag orbit, wheel zoom,\n"
		"W/A/S/D move the focus, F1-F4 physics debug draw, G wind, P pause,\n"
		"R reset, Esc quit.\n\n"
		"Examples (from the examples/ directory):\n"
		"  physics-sandbox\n"
		"  physics-sandbox --self-test\n"
		"  physics-sandbox --screenshot sandbox.png --frames 150\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("physics-sandbox: %s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("%s", license_text);
		return 0;
	}
	if (opt_frames < 1)
		opt_frames = 1;

	memset (&sb, 0, sizeof (sb));
	sb.shots = g_ptr_array_new ();
	sb.focus[0] = 2.0f;
	sb.focus[1] = 1.5f;
	sb.focus[2] = -2.0f;
	sb.yaw = 0.35f;
	sb.pitch = 0.42f;
	sb.distance = 21.0f;
	sb.wind = TRUE;

	if (opt_self_test)
	{
		build_scene (&sb);
		if (sb.ragdoll == NULL ||
		    lrg_ragdoll_get_part_count (sb.ragdoll) < 2 ||
		    lrg_ragdoll_get_world (sb.ragdoll) != lrg_world3d_get_physics (sb.world))
		{
			g_printerr ("physics-sandbox: self-test failed\n");
			return 1;
		}
		g_clear_object (&sb.ragdoll);
		g_clear_object (&sb.world);
		g_ptr_array_unref (sb.shots);
		return 0;
	}

	SetConfigFlags (FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
	window = grl_window_new (WINDOW_W, WINDOW_H, "libregnum - physics-sandbox");
	grl_window_set_target_fps (window, 60);

	sb.camera = lrg_camera3d_new ();
	lrg_camera3d_set_fovy (sb.camera, 50.0f);
	build_scene (&sb);

	renderer = lrg_world3d_renderer_new ();
	lrg_world3d_renderer_set_fallback_sun (renderer, TRUE);
	dd = lrg_debug_draw_new ();
	lrg_world3d_renderer_set_debug_draw (renderer, dd);
	lrg_world3d_renderer_set_flags (renderer, lrg_world3d_renderer_get_flags (renderer) |
	                                LRG_WORLD3D_RENDER_FLAG_DEBUG_DRAW);
	if (opt_screenshot != NULL)
		sb.debug = LRG_DYNAMICS_DEBUG_CONTACTS;

	while (!grl_window_should_close (window))
	{
		/* a fixed frame time when capturing keeps screenshots reproducible */
		gfloat delta = (opt_screenshot != NULL) ? FIXED_DT : GetFrameTime ();

		if (opt_screenshot == NULL)
			handle_input (&sb, delta);
		else if (frame == 30 || frame == 42 || frame == 54)
		{
			/* unattended: three shots into the wall */
			const gfloat wall[3] = { -1.5f + 0.9f * (gfloat) (frame - 30) / 12.0f, 1.0f, -8.0f };

			shoot (&sb, wall);
		}

		if (!sb.paused)
		{
			sb.time += delta;
			lrg_world3d_step (sb.world, delta);
			update_flag (&sb, delta);
		}
		update_camera (&sb);

		/* physics debug draw for this frame (drawn by the renderer) */
		lrg_world3d_debug_draw_physics (sb.world, dd, sb.debug);

		grl_window_begin_drawing (window);
		lrg_world3d_renderer_render (renderer, sb.world, sb.camera, NULL, 0, 0);

		/* the cloth is not an entity: draw it in the same camera */
		lrg_camera_begin (LRG_CAMERA (sb.camera));
		{
			GrlColor c = { (guint8) (COL_CLOTHC[0] * 255.0f), (guint8) (COL_CLOTHC[1] * 255.0f),
			               (guint8) (COL_CLOTHC[2] * 255.0f), 255 };

			lrg_cloth_draw (sb.flag, &c, NULL);
		}
		lrg_camera_end (LRG_CAMERA (sb.camera));
		lrg_debug_draw_end_frame (dd);

		draw_hud (&sb);

		if (opt_screenshot != NULL && frame + 1 >= opt_frames)
		{
			Image shot;

			rlDrawRenderBatchActive ();
			shot = LoadImageFromScreen ();
			if (!ExportImage (shot, opt_screenshot))
				g_printerr ("physics-sandbox: could not write %s\n", opt_screenshot);
			UnloadImage (shot);
			grl_window_end_drawing (window);
			break;
		}
		grl_window_end_drawing (window);
		frame++;
	}

	/* GPU resources go before the window closes */
	lrg_cloth_release_mesh (sb.flag);
	lrg_world3d_renderer_release (renderer);
	g_clear_object (&sb.flag);
	g_clear_object (&sb.world);
	g_clear_object (&sb.camera);
	g_ptr_array_unref (sb.shots);
	return 0;
}
