/* game-taco-racing.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Time trial racing game using the taco_truck.yaml asset.
 * Features procedural track, arcade physics, and checkpoint system.
 *
 * Controls:
 *   W/S     - Accelerate / Brake (reverse)
 *   A/D     - Steer left / right
 *   Space   - Jump
 *   Enter   - Jump (alternative)
 *   Shift   - Boost
 *   R       - Reset race
 *   Escape  - Exit
 */

#include <libregnum.h>
#include <graylib.h>
#include <rlgl.h>
#include <math.h>

/* =============================================================================
 * PHYSICS CONSTANTS
 * ========================================================================== */

#define ACCEL_RATE       8.0f
#define BRAKE_RATE       12.0f
#define FRICTION         3.0f
#define MAX_SPEED        25.0f
#define BOOST_SPEED      40.0f
#define STEER_RATE       2.5f
#define JUMP_VELOCITY    8.0f
#define GRAVITY          20.0f
#define BOOST_DRAIN      0.5f
#define BOOST_RECHARGE   0.15f

/* Track dimensions */
#define TRACK_WIDTH      60.0f
#define TRACK_HEIGHT     80.0f
#define TRACK_LANE_WIDTH 12.0f
#define NUM_CHECKPOINTS  4

/* =============================================================================
 * DATA STRUCTURES
 * ========================================================================== */

typedef struct
{
	gfloat   x;
	gfloat   y;
	gfloat   z;
	gfloat   vx;
	gfloat   vy;
	gfloat   vz;
	gfloat   rotation_y;
	gfloat   speed;
	gfloat   boost_fuel;
	gboolean is_jumping;
	gboolean is_boosting;
} TacoVehicle;

typedef struct
{
	gfloat x;
	gfloat z;
	gfloat radius;
} Checkpoint;

typedef struct
{
	gfloat   elapsed_time;
	gfloat   best_lap_time;
	gint     current_lap;
	gint     next_checkpoint;
	gboolean race_started;
} RaceState;

/* Mesh model entry (copied from render-yaml-taco-truck.c) */
typedef struct
{
	GrlModel   *model;
	GrlVector3 *position;
	GrlVector3 *rotation;
	GrlVector3 *scale;
	GrlColor   *color;
} MeshModelEntry;

/* =============================================================================
 * MESH MODEL HELPERS (from render-yaml-taco-truck.c)
 * ========================================================================== */

static MeshModelEntry *
mesh_model_entry_new (GrlModel   *model,
                      GrlVector3 *position,
                      GrlVector3 *rotation,
                      GrlVector3 *scale,
                      GrlColor   *color)
{
	MeshModelEntry *entry;

	entry = g_new0 (MeshModelEntry, 1);
	entry->model = g_object_ref (model);
	entry->position = grl_vector3_copy (position);
	entry->rotation = grl_vector3_copy (rotation);
	entry->scale = grl_vector3_copy (scale);
	entry->color = grl_color_copy (color);

	return entry;
}

static void
mesh_model_entry_free (MeshModelEntry *entry)
{
	if (entry == NULL)
		return;

	g_clear_object (&entry->model);
	g_clear_pointer (&entry->position, grl_vector3_free);
	g_clear_pointer (&entry->rotation, grl_vector3_free);
	g_clear_pointer (&entry->scale, grl_vector3_free);
	g_clear_pointer (&entry->color, grl_color_free);
	g_free (entry);
}

/* =============================================================================
 * MESH TRIANGULATION (from render-yaml-taco-truck.c)
 * ========================================================================== */

static guint16 *
triangulate_faces (const gint *faces,
                   guint       n_faces,
                   guint       total_indices,
                   gboolean    reverse_winding,
                   guint      *out_n_triangles)
{
	guint    i;
	guint    pos;
	guint    n_triangles;
	guint16 *indices;
	guint    idx;

	n_triangles = 0;
	pos = 0;
	for (i = 0; i < n_faces && pos < total_indices; i++)
	{
		gint n_verts = faces[pos];
		if (n_verts >= 3)
		{
			n_triangles += (n_verts - 2);
		}
		pos += n_verts + 1;
	}

	if (n_triangles == 0)
	{
		*out_n_triangles = 0;
		return NULL;
	}

	indices = g_new (guint16, n_triangles * 3);

	pos = 0;
	idx = 0;
	for (i = 0; i < n_faces && pos < total_indices; i++)
	{
		gint n_verts;
		gint v0;
		gint j;

		n_verts = faces[pos];
		pos++;

		if (n_verts < 3)
		{
			pos += n_verts;
			continue;
		}

		v0 = faces[pos];

		for (j = 1; j < n_verts - 1; j++)
		{
			indices[idx++] = (guint16)v0;
			if (reverse_winding)
			{
				indices[idx++] = (guint16)faces[pos + j + 1];
				indices[idx++] = (guint16)faces[pos + j];
			}
			else
			{
				indices[idx++] = (guint16)faces[pos + j];
				indices[idx++] = (guint16)faces[pos + j + 1];
			}
		}

		pos += n_verts;
	}

	*out_n_triangles = n_triangles;
	return indices;
}

static GrlModel *
mesh_data_to_model (LrgMeshData *mesh_data)
{
	const gfloat *vertices;
	const gint   *faces;
	guint         n_vertices;
	guint         n_faces;
	guint         total_indices;
	gboolean      reverse_winding;
	guint16      *tri_indices;
	guint         n_triangles;
	GrlMesh      *mesh;
	GrlModel     *model;

	if (mesh_data == NULL || lrg_mesh_data_is_empty (mesh_data))
		return NULL;

	vertices = lrg_mesh_data_get_vertices (mesh_data, &n_vertices);
	if (vertices == NULL || n_vertices == 0)
		return NULL;

	faces = lrg_mesh_data_get_faces (mesh_data, &n_faces, &total_indices);
	if (faces == NULL || n_faces == 0)
		return NULL;

	reverse_winding = lrg_mesh_data_get_reverse_winding (mesh_data);

	tri_indices = triangulate_faces (faces, n_faces, total_indices,
	                                  reverse_winding, &n_triangles);
	if (tri_indices == NULL)
		return NULL;

	mesh = grl_mesh_new_custom (vertices, n_vertices, NULL,
	                            tri_indices, n_triangles * 3);
	g_free (tri_indices);

	if (mesh == NULL)
		return NULL;

	model = grl_model_new_from_mesh (mesh);
	g_object_unref (mesh);

	return model;
}

/* =============================================================================
 * SCENE LOADING
 * ========================================================================== */

static void
load_scene_mesh_models (LrgScene  *scene,
                        GPtrArray *mesh_models)
{
	GList *entity_names;
	GList *iter;

	entity_names = lrg_scene_get_entity_names (scene);

	for (iter = entity_names; iter != NULL; iter = iter->next)
	{
		const gchar    *name;
		LrgSceneEntity *entity;
		GPtrArray      *objects;
		guint           i;

		name   = iter->data;
		entity = lrg_scene_get_entity (scene, name);
		objects = lrg_scene_entity_get_objects (entity);

		for (i = 0; i < objects->len; i++)
		{
			LrgSceneObject   *obj;
			LrgPrimitiveType  prim;

			obj = g_ptr_array_index (objects, i);
			prim = lrg_scene_object_get_primitive (obj);

			if (prim == LRG_PRIMITIVE_MESH)
			{
				LrgMeshData    *mesh_data;
				GrlModel       *model;
				GrlVector3     *loc;
				GrlVector3     *rot;
				GrlVector3     *scl;
				LrgMaterial3D  *mat;
				GrlColor       *color;
				MeshModelEntry *entry;

				mesh_data = lrg_scene_object_get_mesh_data (obj);
				model = mesh_data_to_model (mesh_data);

				if (model != NULL)
				{
					loc = lrg_scene_object_get_location (obj);
					rot = lrg_scene_object_get_rotation (obj);
					scl = lrg_scene_object_get_scale (obj);
					mat = lrg_scene_object_get_material (obj);
					color = lrg_material3d_get_color_grl (mat);

					entry = mesh_model_entry_new (model, loc, rot, scl, color);
					g_ptr_array_add (mesh_models, entry);
					g_object_unref (model);
				}
			}
		}
	}

	g_list_free (entity_names);
}

/* =============================================================================
 * DRAW MESH MODEL
 * ========================================================================== */

static void
draw_mesh_model_at (MeshModelEntry *entry)
{
	g_autoptr(GrlVector3) pos = NULL;
	g_autoptr(GrlVector3) rot_axis = NULL;
	gfloat                rot_angle;
	gfloat                rx, ry, rz;

	/*
	 * Use local position directly - the matrix stack handles
	 * the vehicle transform (translation + rotation).
	 */
	pos = grl_vector3_new (entry->position->x,
	                       entry->position->y,
	                       entry->position->z);

	/*
	 * Apply the mesh's original rotation from the YAML file.
	 * Convert Euler rotation to axis-angle for grl_model_draw_ex().
	 * Use the dominant axis for simplicity.
	 */
	rx = entry->rotation->x;
	ry = entry->rotation->y;
	rz = entry->rotation->z;

	if (fabsf (rx) > 0.001f || fabsf (ry) > 0.001f || fabsf (rz) > 0.001f)
	{
		if (fabsf (rz) >= fabsf (rx) && fabsf (rz) >= fabsf (ry))
		{
			rot_axis = grl_vector3_new (0.0f, 0.0f, 1.0f);
			rot_angle = rz * (180.0f / G_PI);
		}
		else if (fabsf (ry) >= fabsf (rx))
		{
			rot_axis = grl_vector3_new (0.0f, 1.0f, 0.0f);
			rot_angle = ry * (180.0f / G_PI);
		}
		else
		{
			rot_axis = grl_vector3_new (1.0f, 0.0f, 0.0f);
			rot_angle = rx * (180.0f / G_PI);
		}
	}
	else
	{
		rot_axis = grl_vector3_new (0.0f, 1.0f, 0.0f);
		rot_angle = 0.0f;
	}

	grl_model_draw_ex (entry->model, pos,
	                   rot_axis, rot_angle, entry->scale, entry->color);
}

/* =============================================================================
 * VEHICLE FUNCTIONS
 * ========================================================================== */

static void
vehicle_reset (TacoVehicle *v,
               gfloat       start_x,
               gfloat       start_z,
               gfloat       start_rot)
{
	v->x = start_x;
	v->y = 0.0f;
	v->z = start_z;
	v->vx = 0.0f;
	v->vy = 0.0f;
	v->vz = 0.0f;
	v->rotation_y = start_rot;
	v->speed = 0.0f;
	v->boost_fuel = 1.0f;
	v->is_jumping = FALSE;
	v->is_boosting = FALSE;
}

static void
vehicle_update (TacoVehicle     *v,
                LrgInputManager *input,
                gfloat           delta)
{
	gboolean accel;
	gboolean brake;
	gboolean steer_left;
	gboolean steer_right;
	gboolean jump;
	gboolean boost;
	gfloat   max_spd;
	gfloat   dir_x;
	gfloat   dir_z;

	/* Read input */
	accel = lrg_input_manager_is_key_down (input, GRL_KEY_W);
	brake = lrg_input_manager_is_key_down (input, GRL_KEY_S);
	steer_left = lrg_input_manager_is_key_down (input, GRL_KEY_A);
	steer_right = lrg_input_manager_is_key_down (input, GRL_KEY_D);
	jump = lrg_input_manager_is_key_pressed (input, GRL_KEY_SPACE) ||
	       lrg_input_manager_is_key_pressed (input, GRL_KEY_ENTER);
	boost = lrg_input_manager_is_key_down (input, GRL_KEY_LEFT_SHIFT) ||
	        lrg_input_manager_is_key_down (input, GRL_KEY_RIGHT_SHIFT);

	/* Handle jumping */
	if (jump && !v->is_jumping && v->y <= 0.01f)
	{
		v->is_jumping = TRUE;
		v->vy = JUMP_VELOCITY;
	}

	/* Apply gravity */
	if (v->is_jumping || v->y > 0.0f)
	{
		v->vy -= GRAVITY * delta;
		v->y += v->vy * delta;

		if (v->y <= 0.0f)
		{
			v->y = 0.0f;
			v->vy = 0.0f;
			v->is_jumping = FALSE;
		}
	}

	/* Handle boost */
	v->is_boosting = FALSE;
	if (boost && v->boost_fuel > 0.0f)
	{
		v->is_boosting = TRUE;
		v->boost_fuel -= BOOST_DRAIN * delta;
		if (v->boost_fuel < 0.0f)
			v->boost_fuel = 0.0f;
	}
	else
	{
		/* Recharge boost when not using */
		v->boost_fuel += BOOST_RECHARGE * delta;
		if (v->boost_fuel > 1.0f)
			v->boost_fuel = 1.0f;
	}

	max_spd = v->is_boosting ? BOOST_SPEED : MAX_SPEED;

	/* Steering (only when moving) */
	if (fabsf (v->speed) > 0.5f)
	{
		gfloat steer_factor;

		steer_factor = (v->speed > 0.0f) ? 1.0f : -1.0f;

		if (steer_left)
			v->rotation_y += STEER_RATE * delta * steer_factor;
		if (steer_right)
			v->rotation_y -= STEER_RATE * delta * steer_factor;
	}

	/* Acceleration / braking */
	if (accel)
	{
		v->speed += ACCEL_RATE * delta;
		if (v->speed > max_spd)
			v->speed = max_spd;
	}
	else if (brake)
	{
		v->speed -= BRAKE_RATE * delta;
		if (v->speed < -max_spd * 0.4f)
			v->speed = -max_spd * 0.4f;
	}
	else
	{
		/* Apply friction */
		if (v->speed > 0.0f)
		{
			v->speed -= FRICTION * delta;
			if (v->speed < 0.0f)
				v->speed = 0.0f;
		}
		else if (v->speed < 0.0f)
		{
			v->speed += FRICTION * delta;
			if (v->speed > 0.0f)
				v->speed = 0.0f;
		}
	}

	/* Apply velocity based on rotation (negate for correct forward direction) */
	dir_x = -sinf (v->rotation_y);
	dir_z = -cosf (v->rotation_y);
	v->vx = dir_x * v->speed;
	v->vz = dir_z * v->speed;

	/* Update position */
	v->x += v->vx * delta;
	v->z += v->vz * delta;
}

/* =============================================================================
 * TRACK GENERATION
 * ========================================================================== */

static void
create_track_markers (GPtrArray *markers,
                      GrlColor  *cone_color,
                      GrlColor  *line_color)
{
	gint   i;
	gfloat hw;
	gfloat hh;
	gfloat inner_hw;
	gfloat inner_hh;

	hw = TRACK_WIDTH / 2.0f;
	hh = TRACK_HEIGHT / 2.0f;
	inner_hw = hw - TRACK_LANE_WIDTH;
	inner_hh = hh - TRACK_LANE_WIDTH;

	/* Outer track boundary - cones along the edges */
	/* Top edge */
	for (i = 0; i < 12; i++)
	{
		gfloat x;
		LrgCone3D *cone;

		x = -hw + (i * (TRACK_WIDTH / 11.0f));
		cone = lrg_cone3d_new_full (x, 0.0f, hh, 0.5f, 0.0f, 1.0f, 8, cone_color);
		g_ptr_array_add (markers, cone);
	}

	/* Bottom edge */
	for (i = 0; i < 12; i++)
	{
		gfloat x;
		LrgCone3D *cone;

		x = -hw + (i * (TRACK_WIDTH / 11.0f));
		cone = lrg_cone3d_new_full (x, 0.0f, -hh, 0.5f, 0.0f, 1.0f, 8, cone_color);
		g_ptr_array_add (markers, cone);
	}

	/* Left edge */
	for (i = 1; i < 15; i++)
	{
		gfloat z;
		LrgCone3D *cone;

		z = -hh + (i * (TRACK_HEIGHT / 15.0f));
		cone = lrg_cone3d_new_full (-hw, 0.0f, z, 0.5f, 0.0f, 1.0f, 8, cone_color);
		g_ptr_array_add (markers, cone);
	}

	/* Right edge */
	for (i = 1; i < 15; i++)
	{
		gfloat z;
		LrgCone3D *cone;

		z = -hh + (i * (TRACK_HEIGHT / 15.0f));
		cone = lrg_cone3d_new_full (hw, 0.0f, z, 0.5f, 0.0f, 1.0f, 8, cone_color);
		g_ptr_array_add (markers, cone);
	}

	/* Inner boundary - smaller cones */
	/* Top inner edge */
	for (i = 0; i < 8; i++)
	{
		gfloat x;
		LrgCone3D *cone;

		x = -inner_hw + (i * (inner_hw * 2.0f / 7.0f));
		cone = lrg_cone3d_new_full (x, 0.0f, inner_hh, 0.4f, 0.0f, 0.8f, 8, cone_color);
		g_ptr_array_add (markers, cone);
	}

	/* Bottom inner edge */
	for (i = 0; i < 8; i++)
	{
		gfloat x;
		LrgCone3D *cone;

		x = -inner_hw + (i * (inner_hw * 2.0f / 7.0f));
		cone = lrg_cone3d_new_full (x, 0.0f, -inner_hh, 0.4f, 0.0f, 0.8f, 8, cone_color);
		g_ptr_array_add (markers, cone);
	}

	/* Left inner edge */
	for (i = 1; i < 11; i++)
	{
		gfloat z;
		LrgCone3D *cone;

		z = -inner_hh + (i * (inner_hh * 2.0f / 11.0f));
		cone = lrg_cone3d_new_full (-inner_hw, 0.0f, z, 0.4f, 0.0f, 0.8f, 8, cone_color);
		g_ptr_array_add (markers, cone);
	}

	/* Right inner edge */
	for (i = 1; i < 11; i++)
	{
		gfloat z;
		LrgCone3D *cone;

		z = -inner_hh + (i * (inner_hh * 2.0f / 11.0f));
		cone = lrg_cone3d_new_full (inner_hw, 0.0f, z, 0.4f, 0.0f, 0.8f, 8, cone_color);
		g_ptr_array_add (markers, cone);
	}

	/* Start/finish line markers */
	for (i = 0; i < 6; i++)
	{
		gfloat x;
		LrgCube3D *cube;

		x = inner_hw + (i * (TRACK_LANE_WIDTH / 6.0f));
		cube = lrg_cube3d_new_at (x, 0.05f, -hh + 2.0f, 0.8f, 0.1f, 0.3f);
		if (i % 2 == 0)
			lrg_shape_set_color (LRG_SHAPE (cube), line_color);
		g_ptr_array_add (markers, cube);
	}
}

static void
create_checkpoints (Checkpoint *cps)
{
	/* CP0: Left side (middle of lane) */
	cps[0].x = -(TRACK_WIDTH / 2.0f) + (TRACK_LANE_WIDTH / 2.0f);
	cps[0].z = 0.0f;
	cps[0].radius = TRACK_LANE_WIDTH;

	/* CP1: Top side */
	cps[1].x = 0.0f;
	cps[1].z = (TRACK_HEIGHT / 2.0f) - (TRACK_LANE_WIDTH / 2.0f);
	cps[1].radius = TRACK_LANE_WIDTH;

	/* CP2: Right side */
	cps[2].x = (TRACK_WIDTH / 2.0f) - (TRACK_LANE_WIDTH / 2.0f);
	cps[2].z = 0.0f;
	cps[2].radius = TRACK_LANE_WIDTH;

	/* CP3: Bottom side (start/finish) */
	cps[3].x = 0.0f;
	cps[3].z = -(TRACK_HEIGHT / 2.0f) + (TRACK_LANE_WIDTH / 2.0f);
	cps[3].radius = TRACK_LANE_WIDTH;
}

static void
create_checkpoint_gates (GPtrArray  *gates,
                         Checkpoint *cps,
                         GrlColor   *gate_color)
{
	gint i;

	for (i = 0; i < NUM_CHECKPOINTS; i++)
	{
		LrgCube3D *left_post;
		LrgCube3D *right_post;
		LrgCube3D *top_bar;
		gfloat     post_offset;

		post_offset = 4.0f;

		/* Determine orientation based on checkpoint position */
		if (i == 0 || i == 2)
		{
			/* Left/right sides - gate spans X axis (perpendicular to Z travel) */
			left_post = lrg_cube3d_new_at (cps[i].x - post_offset, 2.0f, cps[i].z, 0.3f, 4.0f, 0.3f);
			right_post = lrg_cube3d_new_at (cps[i].x + post_offset, 2.0f, cps[i].z, 0.3f, 4.0f, 0.3f);
			top_bar = lrg_cube3d_new_at (cps[i].x, 4.0f, cps[i].z, post_offset * 2.0f, 0.3f, 0.3f);
		}
		else
		{
			/* Top/bottom sides - gate spans Z axis (perpendicular to X travel) */
			left_post = lrg_cube3d_new_at (cps[i].x, 2.0f, cps[i].z - post_offset, 0.3f, 4.0f, 0.3f);
			right_post = lrg_cube3d_new_at (cps[i].x, 2.0f, cps[i].z + post_offset, 0.3f, 4.0f, 0.3f);
			top_bar = lrg_cube3d_new_at (cps[i].x, 4.0f, cps[i].z, 0.3f, 0.3f, post_offset * 2.0f);
		}

		lrg_shape_set_color (LRG_SHAPE (left_post), gate_color);
		lrg_shape_set_color (LRG_SHAPE (right_post), gate_color);
		lrg_shape_set_color (LRG_SHAPE (top_bar), gate_color);

		g_ptr_array_add (gates, left_post);
		g_ptr_array_add (gates, right_post);
		g_ptr_array_add (gates, top_bar);
	}
}

/* =============================================================================
 * CHECKPOINT LOGIC
 * ========================================================================== */

static gboolean
check_checkpoint (TacoVehicle *v,
                  Checkpoint  *cp)
{
	gfloat dx;
	gfloat dz;
	gfloat dist_sq;

	dx = v->x - cp->x;
	dz = v->z - cp->z;
	dist_sq = dx * dx + dz * dz;

	return dist_sq < (cp->radius * cp->radius);
}

static void
update_race (RaceState   *race,
             TacoVehicle *v,
             Checkpoint  *cps,
             gfloat       delta)
{
	if (!race->race_started)
	{
		/* Start race when player moves */
		if (fabsf (v->speed) > 0.5f)
		{
			race->race_started = TRUE;
		}
		return;
	}

	race->elapsed_time += delta;

	/* Check if we hit the next checkpoint */
	if (check_checkpoint (v, &cps[race->next_checkpoint]))
	{
		race->next_checkpoint++;

		if (race->next_checkpoint >= NUM_CHECKPOINTS)
		{
			/* Completed a lap */
			race->next_checkpoint = 0;
			race->current_lap++;

			if (race->best_lap_time < 0.0f || race->elapsed_time < race->best_lap_time)
			{
				race->best_lap_time = race->elapsed_time;
			}

			race->elapsed_time = 0.0f;
		}
	}
}

static void
race_reset (RaceState *race)
{
	race->elapsed_time = 0.0f;
	race->current_lap = 0;
	race->next_checkpoint = 0;
	race->race_started = FALSE;
	/* Keep best lap time */
}

/* =============================================================================
 * HUD RENDERING
 * ========================================================================== */

static void
render_hud (TacoVehicle *v,
            RaceState   *race,
            gfloat       delta)
{
	g_autoptr(GrlColor) white = NULL;
	g_autoptr(GrlColor) yellow = NULL;
	g_autoptr(GrlColor) green = NULL;
	g_autoptr(GrlColor) red = NULL;
	g_autoptr(GrlColor) gray = NULL;
	g_autoptr(GrlColor) bg_color = NULL;
	g_autoptr(GrlColor) boost_color = NULL;

	g_autoptr(LrgText2D) speed_label = NULL;
	g_autoptr(LrgText2D) time_label = NULL;
	g_autoptr(LrgText2D) lap_label = NULL;
	g_autoptr(LrgText2D) best_label = NULL;
	g_autoptr(LrgText2D) boost_label = NULL;
	g_autoptr(LrgText2D) checkpoint_label = NULL;
	g_autoptr(LrgText2D) controls_label = NULL;

	g_autofree gchar *speed_str = NULL;
	g_autofree gchar *time_str = NULL;
	g_autofree gchar *lap_str = NULL;
	g_autofree gchar *best_str = NULL;
	g_autofree gchar *cp_str = NULL;

	gint   time_min;
	gint   time_sec;
	gint   time_ms;
	gint   best_min;
	gint   best_sec;
	gint   best_ms;
	gfloat boost_width;

	white = grl_color_new (255, 255, 255, 255);
	yellow = grl_color_new (255, 255, 0, 255);
	green = grl_color_new (0, 255, 0, 255);
	red = grl_color_new (255, 80, 80, 255);
	gray = grl_color_new (150, 150, 150, 255);
	bg_color = grl_color_new (0, 0, 0, 180);

	/* Background panel */
	grl_draw_rectangle (5, 5, 220, 180, bg_color);

	/* Speed */
	speed_str = g_strdup_printf ("Speed: %.1f", fabsf (v->speed));
	speed_label = lrg_text2d_new_full (15.0f, 15.0f, speed_str, 20.0f,
	                                    v->is_boosting ? yellow : white);
	lrg_drawable_draw (LRG_DRAWABLE (speed_label), delta);

	/* Time */
	time_min = (gint)(race->elapsed_time / 60.0f);
	time_sec = (gint)fmodf (race->elapsed_time, 60.0f);
	time_ms = (gint)(fmodf (race->elapsed_time, 1.0f) * 100.0f);
	time_str = g_strdup_printf ("Time: %02d:%02d.%02d", time_min, time_sec, time_ms);
	time_label = lrg_text2d_new_full (15.0f, 40.0f, time_str, 20.0f, white);
	lrg_drawable_draw (LRG_DRAWABLE (time_label), delta);

	/* Lap */
	lap_str = g_strdup_printf ("Lap: %d", race->current_lap + 1);
	lap_label = lrg_text2d_new_full (15.0f, 65.0f, lap_str, 20.0f, white);
	lrg_drawable_draw (LRG_DRAWABLE (lap_label), delta);

	/* Best time */
	if (race->best_lap_time >= 0.0f)
	{
		best_min = (gint)(race->best_lap_time / 60.0f);
		best_sec = (gint)fmodf (race->best_lap_time, 60.0f);
		best_ms = (gint)(fmodf (race->best_lap_time, 1.0f) * 100.0f);
		best_str = g_strdup_printf ("Best: %02d:%02d.%02d", best_min, best_sec, best_ms);
	}
	else
	{
		best_str = g_strdup ("Best: --:--.--");
	}
	best_label = lrg_text2d_new_full (15.0f, 90.0f, best_str, 20.0f, green);
	lrg_drawable_draw (LRG_DRAWABLE (best_label), delta);

	/* Checkpoint indicator */
	cp_str = g_strdup_printf ("Checkpoint: %d/%d", race->next_checkpoint, NUM_CHECKPOINTS);
	checkpoint_label = lrg_text2d_new_full (15.0f, 115.0f, cp_str, 16.0f, gray);
	lrg_drawable_draw (LRG_DRAWABLE (checkpoint_label), delta);

	/* Boost bar */
	boost_label = lrg_text2d_new_full (15.0f, 140.0f, "Boost:", 16.0f, white);
	lrg_drawable_draw (LRG_DRAWABLE (boost_label), delta);

	/* Boost bar background */
	grl_draw_rectangle (75, 140, 100, 16, gray);

	/* Boost bar fill */
	boost_width = v->boost_fuel * 100.0f;
	if (v->boost_fuel > 0.5f)
		boost_color = grl_color_new (0, 200, 255, 255);
	else if (v->boost_fuel > 0.2f)
		boost_color = grl_color_new (255, 200, 0, 255);
	else
		boost_color = grl_color_new (255, 50, 50, 255);

	grl_draw_rectangle (75, 140, (gint)boost_width, 16, boost_color);

	/* Controls hint */
	controls_label = lrg_text2d_new_full (15.0f, 165.0f,
	                                       "WASD:Drive Space:Jump Shift:Boost R:Reset",
	                                       12.0f, gray);
	lrg_drawable_draw (LRG_DRAWABLE (controls_label), delta);
}

/* =============================================================================
 * MAIN
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GError)                    error = NULL;
	g_autoptr(LrgSceneSerializerBlender) serializer = NULL;
	g_autoptr(LrgScene)                  scene = NULL;
	g_autoptr(GPtrArray)                 mesh_models = NULL;
	g_autoptr(GPtrArray)                 track_markers = NULL;
	g_autoptr(GPtrArray)                 checkpoint_gates = NULL;
	LrgEngine                           *engine;
	LrgRenderer                         *renderer;
	LrgInputManager                     *input_manager;
	g_autoptr(LrgGrlWindow)              window = NULL;
	g_autoptr(LrgCameraThirdPerson)      camera = NULL;
	g_autoptr(LrgPlane3D)                ground = NULL;
	g_autoptr(GrlColor)                  bg_color = NULL;
	g_autoptr(GrlColor)                  ground_color = NULL;
	g_autoptr(GrlColor)                  cone_color = NULL;
	g_autoptr(GrlColor)                  line_color = NULL;
	g_autoptr(GrlColor)                  gate_color = NULL;

	TacoVehicle vehicle;
	RaceState   race;
	Checkpoint  checkpoints[NUM_CHECKPOINTS];
	gfloat      start_x;
	gfloat      start_z;
	gfloat      start_rot;
	guint       i;

	/* Create window */
	window = lrg_grl_window_new (1280, 720, "Taco Truck Racing - Time Trial");
	lrg_window_set_target_fps (LRG_WINDOW (window), 60);

	/* Initialize engine */
	engine = lrg_engine_get_default ();
	lrg_engine_set_window (engine, LRG_WINDOW (window));

	if (!lrg_engine_startup (engine, &error))
	{
		g_error ("Failed to start engine: %s", error->message);
		return 1;
	}

	renderer = lrg_engine_get_renderer (engine);
	input_manager = lrg_input_manager_get_default ();

	/* Load taco truck scene */
	serializer = lrg_scene_serializer_blender_new ();
	scene = lrg_scene_serializer_load_from_file (
		LRG_SCENE_SERIALIZER (serializer),
		"data/taco_truck.yaml",
		&error);

	if (scene == NULL)
	{
		g_error ("Failed to load taco truck: %s", error->message);
		return 1;
	}

	g_print ("Loaded: %s\n", lrg_scene_get_name (scene));

	/* Load mesh models */
	mesh_models = g_ptr_array_new_with_free_func ((GDestroyNotify)mesh_model_entry_free);
	load_scene_mesh_models (scene, mesh_models);
	g_print ("Loaded %u mesh models\n", mesh_models->len);

	/* Create track elements */
	cone_color = grl_color_new (255, 140, 0, 255);
	line_color = grl_color_new (255, 255, 255, 255);
	gate_color = grl_color_new (100, 200, 255, 255);

	track_markers = g_ptr_array_new_with_free_func (g_object_unref);
	create_track_markers (track_markers, cone_color, line_color);

	create_checkpoints (checkpoints);

	checkpoint_gates = g_ptr_array_new_with_free_func (g_object_unref);
	create_checkpoint_gates (checkpoint_gates, checkpoints, gate_color);

	/* Create ground plane */
	ground_color = grl_color_new (60, 100, 60, 255);
	ground = lrg_plane3d_new_at (0.0f, -0.1f, 0.0f, 200.0f, 200.0f);
	lrg_shape_set_color (LRG_SHAPE (ground), ground_color);

	/* Initialize vehicle at start/finish line */
	start_x = (TRACK_WIDTH / 2.0f) - (TRACK_LANE_WIDTH / 2.0f);
	start_z = -(TRACK_HEIGHT / 2.0f) + (TRACK_LANE_WIDTH / 2.0f);
	start_rot = G_PI / 2.0f;  /* Face left to start going around track */
	vehicle_reset (&vehicle, start_x, start_z, start_rot);

	/* Initialize race state */
	race.elapsed_time = 0.0f;
	race.best_lap_time = -1.0f;
	race.current_lap = 0;
	race.next_checkpoint = 0;
	race.race_started = FALSE;

	/* Create camera */
	camera = lrg_camera_thirdperson_new ();
	lrg_camera_thirdperson_set_distance (camera, 12.0f);
	lrg_camera_thirdperson_set_pitch (camera, 25.0f);
	lrg_camera_thirdperson_set_height_offset (camera, 3.0f);
	lrg_camera_thirdperson_snap_to_target (camera, vehicle.x, vehicle.y + 1.0f, vehicle.z);

	lrg_renderer_set_camera (renderer, LRG_CAMERA (camera));

	bg_color = grl_color_new (135, 180, 220, 255);

	/* Main loop */
	while (!lrg_window_should_close (LRG_WINDOW (window)))
	{
		gfloat delta;

		delta = lrg_window_get_frame_time (LRG_WINDOW (window));

		/* Poll input */
		lrg_input_manager_poll (input_manager);

		/* Reset */
		if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_R))
		{
			vehicle_reset (&vehicle, start_x, start_z, start_rot);
			race_reset (&race);
		}

		/* Update vehicle */
		vehicle_update (&vehicle, input_manager, delta);

		/* Update race */
		update_race (&race, &vehicle, checkpoints, delta);

		/* Update camera to follow vehicle */
		lrg_camera_thirdperson_set_yaw (camera, vehicle.rotation_y * (180.0f / G_PI));
		lrg_camera_thirdperson_follow (camera, vehicle.x, vehicle.y + 1.0f, vehicle.z, delta);

		/* Render */
		lrg_renderer_begin_frame (renderer);
		lrg_renderer_clear (renderer, bg_color);

		/* World layer */
		lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_WORLD);
		{
			/* Ground */
			lrg_drawable_draw (LRG_DRAWABLE (ground), delta);

			/* Track markers */
			for (i = 0; i < track_markers->len; i++)
			{
				LrgShape3D *marker;

				marker = g_ptr_array_index (track_markers, i);
				lrg_drawable_draw (LRG_DRAWABLE (marker), delta);
			}

			/* Checkpoint gates */
			for (i = 0; i < checkpoint_gates->len; i++)
			{
				LrgShape3D *gate;

				gate = g_ptr_array_index (checkpoint_gates, i);
				lrg_drawable_draw (LRG_DRAWABLE (gate), delta);
			}

			/* Draw taco truck at vehicle position using matrix stack */
			rlPushMatrix ();
			rlTranslatef (vehicle.x, vehicle.y, vehicle.z);
			rlRotatef (vehicle.rotation_y * (180.0f / G_PI), 0.0f, 1.0f, 0.0f);

			for (i = 0; i < mesh_models->len; i++)
			{
				MeshModelEntry *entry;

				entry = g_ptr_array_index (mesh_models, i);
				draw_mesh_model_at (entry);
			}

			rlPopMatrix ();
		}
		lrg_renderer_end_layer (renderer);

		/* UI layer */
		lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_UI);
		{
			render_hud (&vehicle, &race, delta);
		}
		lrg_renderer_end_layer (renderer);

		lrg_renderer_end_frame (renderer);
	}

	/* Cleanup */
	lrg_engine_shutdown (engine);

	return 0;
}
