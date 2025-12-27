/* render-yaml-taco-truck.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Example demonstrating how to load and render a Blender-exported
 * YAML scene file containing custom mesh geometry (primitive_mesh)
 * using libregnum's scene module with LrgSceneSerializerBlender.
 */

#include <libregnum.h>
#include <graylib.h>
#include <math.h>

/* Camera modes */
typedef enum
{
	CAMERA_MODE_THIRDPERSON,
	CAMERA_MODE_ISOMETRIC
} CameraMode;

/* =============================================================================
 * MESH MODEL ENTRY
 *
 * Stores a GrlModel with its transform and color for mesh primitives.
 * ========================================================================== */

typedef struct
{
	GrlModel   *model;
	GrlVector3 *position;
	GrlVector3 *rotation;
	GrlVector3 *scale;
	GrlColor   *color;
} MeshModelEntry;

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
 * MESH DATA TO MODEL CONVERSION
 *
 * Converts LrgMeshData to a renderable GrlModel by triangulating faces.
 * ========================================================================== */

/**
 * triangulate_faces:
 * @faces: Face data in [n, v0, v1, ..., n, v0, v1, ...] format
 * @n_faces: Number of faces
 * @total_indices: Total length of face array
 * @reverse_winding: Whether to reverse triangle winding order
 * @out_n_triangles: (out): Number of triangles created
 *
 * Triangulates polygon faces into triangles using fan triangulation.
 * When @reverse_winding is TRUE, swaps the last two indices of each
 * triangle to correct for mirrored geometry.
 *
 * Returns: (transfer full): Array of triangle indices, or NULL if empty
 */
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

	/*
	 * First pass: count triangles
	 * Each polygon with n vertices produces (n-2) triangles
	 */
	n_triangles = 0;
	pos = 0;
	for (i = 0; i < n_faces && pos < total_indices; i++)
	{
		gint n_verts = faces[pos];
		if (n_verts >= 3)
		{
			n_triangles += (n_verts - 2);
		}
		pos += n_verts + 1;  /* skip count + vertices */
	}

	if (n_triangles == 0)
	{
		*out_n_triangles = 0;
		return NULL;
	}

	/* Allocate indices: 3 indices per triangle */
	indices = g_new (guint16, n_triangles * 3);

	/*
	 * Second pass: generate triangle indices using fan triangulation
	 * For polygon [v0, v1, v2, v3, ...] create triangles:
	 *   (v0, v1, v2), (v0, v2, v3), (v0, v3, v4), ...
	 */
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

		v0 = faces[pos];  /* fan pivot vertex */

		for (j = 1; j < n_verts - 1; j++)
		{
			indices[idx++] = (guint16)v0;
			if (reverse_winding)
			{
				/* Swap last two indices to reverse winding */
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

/**
 * mesh_data_to_model:
 * @mesh_data: The mesh data from the scene object
 *
 * Converts LrgMeshData to a GrlModel for rendering.
 *
 * Returns: (transfer full) (nullable): A new GrlModel, or NULL if invalid
 */
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

	/* Get vertex data */
	vertices = lrg_mesh_data_get_vertices (mesh_data, &n_vertices);
	if (vertices == NULL || n_vertices == 0)
		return NULL;

	/* Get face data */
	faces = lrg_mesh_data_get_faces (mesh_data, &n_faces, &total_indices);
	if (faces == NULL || n_faces == 0)
		return NULL;

	/* Get winding flag - set by serializer based on coordinate conversion */
	reverse_winding = lrg_mesh_data_get_reverse_winding (mesh_data);

	/* Triangulate faces */
	tri_indices = triangulate_faces (faces, n_faces, total_indices,
	                                  reverse_winding, &n_triangles);
	if (tri_indices == NULL)
		return NULL;

	/* Create mesh with auto-computed normals */
	mesh = grl_mesh_new_custom (vertices, n_vertices, NULL,
	                            tri_indices, n_triangles * 3);
	g_free (tri_indices);

	if (mesh == NULL)
		return NULL;

	/* Create model from mesh */
	model = grl_model_new_from_mesh (mesh);
	g_object_unref (mesh);

	return model;
}

/* =============================================================================
 * SHAPE CONVERSION (for non-mesh primitives)
 * ========================================================================== */

/**
 * scene_object_to_shape:
 * @obj: A scene object from the loaded scene
 *
 * Converts an LrgSceneObject to the appropriate LrgShape3D subclass
 * based on its primitive type and parameters.
 * Returns NULL for LRG_PRIMITIVE_MESH (handled separately).
 *
 * Returns: (transfer full) (nullable): A new shape, or NULL if unsupported
 */
static LrgShape3D *
scene_object_to_shape (LrgSceneObject *obj)
{
	LrgPrimitiveType  prim;
	LrgMaterial3D    *mat;
	GrlVector3       *loc;
	GrlVector3       *rot;
	GrlVector3       *scl;
	LrgShape3D       *shape = NULL;

	prim = lrg_scene_object_get_primitive (obj);
	mat  = lrg_scene_object_get_material (obj);
	loc  = lrg_scene_object_get_location (obj);
	rot  = lrg_scene_object_get_rotation (obj);
	scl  = lrg_scene_object_get_scale (obj);

	switch (prim)
	{
	case LRG_PRIMITIVE_CYLINDER:
		{
			gfloat radius = lrg_scene_object_get_param_float (obj, "radius", 1.0f);
			gfloat depth  = lrg_scene_object_get_param_float (obj, "depth", 2.0f);
			gint   slices = lrg_scene_object_get_param_int (obj, "vertices", 32);

			shape = LRG_SHAPE3D (lrg_cylinder3d_new_full (
				loc->x, loc->y, loc->z,
				radius, depth, slices,
				lrg_material3d_get_color_grl (mat)));
		}
		break;

	case LRG_PRIMITIVE_UV_SPHERE:
		{
			gfloat radius = lrg_scene_object_get_param_float (obj, "radius", 1.0f);

			shape = LRG_SHAPE3D (lrg_sphere3d_new_full (
				loc->x, loc->y, loc->z,
				radius,
				lrg_material3d_get_color_grl (mat)));
		}
		break;

	case LRG_PRIMITIVE_ICO_SPHERE:
		{
			gfloat radius = lrg_scene_object_get_param_float (obj, "radius", 1.0f);
			gint   subdiv = lrg_scene_object_get_param_int (obj, "subdivisions", 2);

			shape = LRG_SHAPE3D (lrg_icosphere3d_new_full (
				loc->x, loc->y, loc->z,
				radius, subdiv,
				lrg_material3d_get_color_grl (mat)));
		}
		break;

	case LRG_PRIMITIVE_CUBE:
		{
			gfloat size = lrg_scene_object_get_param_float (obj, "size", 2.0f);

			shape = LRG_SHAPE3D (lrg_cube3d_new_at (
				loc->x, loc->y, loc->z,
				size, size, size));
		}
		break;

	case LRG_PRIMITIVE_CONE:
		{
			gfloat radius1 = lrg_scene_object_get_param_float (obj, "radius1", 1.0f);
			gfloat radius2 = lrg_scene_object_get_param_float (obj, "radius2", 0.0f);
			gfloat depth   = lrg_scene_object_get_param_float (obj, "depth", 2.0f);
			gint   slices  = lrg_scene_object_get_param_int (obj, "vertices", 32);

			shape = LRG_SHAPE3D (lrg_cone3d_new_full (
				loc->x, loc->y, loc->z,
				radius1, radius2, depth, slices,
				lrg_material3d_get_color_grl (mat)));
		}
		break;

	case LRG_PRIMITIVE_PLANE:
		{
			gfloat size = lrg_scene_object_get_param_float (obj, "size", 2.0f);

			shape = LRG_SHAPE3D (lrg_plane3d_new_at (
				loc->x, loc->y, loc->z,
				size, size));
		}
		break;

	case LRG_PRIMITIVE_TORUS:
		{
			gfloat major_r = lrg_scene_object_get_param_float (obj, "major_radius", 1.0f);
			gfloat minor_r = lrg_scene_object_get_param_float (obj, "minor_radius", 0.25f);
			gint   major_s = lrg_scene_object_get_param_int (obj, "major_segments", 48);
			gint   minor_s = lrg_scene_object_get_param_int (obj, "minor_segments", 12);

			shape = LRG_SHAPE3D (lrg_torus3d_new_full (
				loc->x, loc->y, loc->z,
				major_r, minor_r, major_s, minor_s,
				lrg_material3d_get_color_grl (mat)));
		}
		break;

	case LRG_PRIMITIVE_CIRCLE:
		{
			gfloat radius   = lrg_scene_object_get_param_float (obj, "radius", 1.0f);
			gint   vertices = lrg_scene_object_get_param_int (obj, "vertices", 32);

			shape = LRG_SHAPE3D (lrg_circle3d_new_full (
				loc->x, loc->y, loc->z,
				radius, vertices,
				lrg_material3d_get_color_grl (mat)));
		}
		break;

	case LRG_PRIMITIVE_GRID:
		{
			gint   slices  = lrg_scene_object_get_param_int (obj, "x_subdivisions", 10);
			gfloat spacing = lrg_scene_object_get_param_float (obj, "size", 1.0f);

			shape = LRG_SHAPE3D (lrg_grid3d_new_sized (slices, spacing));
			lrg_shape3d_set_position (shape, loc);
		}
		break;

	case LRG_PRIMITIVE_MESH:
		/* Mesh primitives are handled separately */
		return NULL;

	default:
		g_warning ("Unknown primitive type: %d for object '%s'",
		           prim, lrg_scene_object_get_name (obj));
		return NULL;
	}

	/* Apply rotation and scale */
	if (shape != NULL)
	{
		lrg_shape3d_set_rotation (shape, rot);
		lrg_shape3d_set_scale (shape, scl);

		/* Apply color from material */
		lrg_shape_set_color (LRG_SHAPE (shape), lrg_material3d_get_color_grl (mat));
	}

	return shape;
}

/* =============================================================================
 * SCENE LOADING
 * ========================================================================== */

/**
 * load_scene_objects:
 * @scene: The loaded scene
 * @shapes: (out): Array to store shapes in
 * @mesh_models: (out): Array to store mesh model entries in
 *
 * Iterates all entities and objects in the scene, converting each
 * to either a renderable shape or a mesh model.
 */
static void
load_scene_objects (LrgScene  *scene,
                    GPtrArray *shapes,
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
				/* Handle mesh primitives */
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
				else
				{
					g_warning ("Failed to create mesh for object '%s'",
					           lrg_scene_object_get_name (obj));
				}
			}
			else
			{
				/* Handle other primitives */
				LrgShape3D *shape;

				shape = scene_object_to_shape (obj);
				if (shape != NULL)
				{
					g_ptr_array_add (shapes, shape);
				}
			}
		}
	}

	g_list_free (entity_names);
}

/**
 * draw_mesh_model:
 * @entry: The mesh model entry to draw
 *
 * Draws a mesh model with its transform and color.
 */
static void
draw_mesh_model (MeshModelEntry *entry)
{
	g_autoptr(GrlVector3) rot_axis = NULL;
	gfloat                rot_angle;
	gfloat                rx, ry, rz;

	/*
	 * Convert Euler rotation to axis-angle for grl_model_draw_ex()
	 * For simplicity, we use Z-axis rotation as primary since most
	 * Blender exports orient objects this way.
	 *
	 * Note: This is a simplified conversion. For full Euler support,
	 * you'd need to compose quaternions or use matrix transforms.
	 */
	rx = entry->rotation->x;
	ry = entry->rotation->y;
	rz = entry->rotation->z;

	if (fabsf (rx) > 0.001f || fabsf (ry) > 0.001f || fabsf (rz) > 0.001f)
	{
		/*
		 * Simple approach: use the dominant axis
		 * A proper implementation would compose rotations
		 */
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

	grl_model_draw_ex (entry->model, entry->position,
	                   rot_axis, rot_angle, entry->scale, entry->color);
}

/* =============================================================================
 * MAIN
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GError)                     error = NULL;
	g_autoptr(LrgSceneSerializerBlender)  serializer = NULL;
	g_autoptr(LrgScene)                   scene = NULL;
	g_autoptr(GPtrArray)                  shapes = NULL;
	g_autoptr(GPtrArray)                  mesh_models = NULL;
	LrgEngine                            *engine;
	LrgRenderer                          *renderer;
	g_autoptr(LrgGrlWindow)               window = NULL;
	g_autoptr(LrgCameraThirdPerson)       camera_tp = NULL;
	g_autoptr(LrgCameraIsometric)         camera_iso = NULL;
	g_autoptr(GrlColor)                   bg_color = NULL;
	gfloat                                camera_angle = 0.0f;
	CameraMode                            camera_mode = CAMERA_MODE_THIRDPERSON;
	guint                                 i;

	/* Create window first */
	window = lrg_grl_window_new (1280, 720, "Taco Truck - YAML Mesh Renderer");
	lrg_window_set_target_fps (LRG_WINDOW (window), 60);

	/* Initialize engine with window */
	engine = lrg_engine_get_default ();
	lrg_engine_set_window (engine, LRG_WINDOW (window));

	if (!lrg_engine_startup (engine, &error))
	{
		g_error ("Failed to start engine: %s", error->message);
		return 1;
	}

	/* Get renderer */
	renderer = lrg_engine_get_renderer (engine);

	/* Load YAML scene using Blender serializer (handles Z-up to Y-up conversion) */
	serializer = lrg_scene_serializer_blender_new ();
	scene = lrg_scene_serializer_load_from_file (
		LRG_SCENE_SERIALIZER (serializer),
		"data/taco_truck.yaml",
		&error);

	if (scene == NULL)
	{
		g_error ("Failed to load scene: %s", error->message);
		return 1;
	}

	g_print ("Loaded scene: %s\n", lrg_scene_get_name (scene));
	g_print ("Exported from: %s\n", lrg_scene_get_exported_from (scene));
	g_print ("Entity count: %u\n", lrg_scene_get_entity_count (scene));

	/* Convert scene objects to shapes and mesh models */
	shapes = g_ptr_array_new_with_free_func (g_object_unref);
	mesh_models = g_ptr_array_new_with_free_func ((GDestroyNotify)mesh_model_entry_free);
	load_scene_objects (scene, shapes, mesh_models);

	g_print ("Created %u shapes\n", shapes->len);
	g_print ("Created %u mesh models\n", mesh_models->len);

	/* Create third-person camera for viewing */
	camera_tp = lrg_camera_thirdperson_new ();
	lrg_camera_thirdperson_set_distance (camera_tp, 8.0f);
	lrg_camera_thirdperson_set_pitch (camera_tp, 25.0f);
	lrg_camera_thirdperson_set_height_offset (camera_tp, 1.5f);
	lrg_camera_thirdperson_snap_to_target (camera_tp, 0.0f, 1.0f, 0.0f);

	/* Create isometric camera */
	camera_iso = lrg_camera_isometric_new ();
	lrg_camera_isometric_set_zoom (camera_iso, 0.15f);
	lrg_camera_isometric_focus_on (camera_iso, 0.0f, 1.0f, 0.0f);

	/* Start with third-person camera */
	lrg_renderer_set_camera (renderer, LRG_CAMERA (camera_tp));
	bg_color = grl_color_new (45, 50, 60, 255);

	/* Main render loop */
	while (!lrg_window_should_close (LRG_WINDOW (window)))
	{
		gfloat delta;

		delta = lrg_window_get_frame_time (LRG_WINDOW (window));

		/* Handle camera switching with 'C' key */
		if (grl_input_is_key_pressed (GRL_KEY_C))
		{
			if (camera_mode == CAMERA_MODE_THIRDPERSON)
			{
				camera_mode = CAMERA_MODE_ISOMETRIC;
				lrg_renderer_set_camera (renderer, LRG_CAMERA (camera_iso));
			}
			else
			{
				camera_mode = CAMERA_MODE_THIRDPERSON;
				lrg_renderer_set_camera (renderer, LRG_CAMERA (camera_tp));
			}
		}

		/* Update camera based on mode */
		if (camera_mode == CAMERA_MODE_THIRDPERSON)
		{
			/* Auto-rotate third-person camera around the scene */
			camera_angle += delta * 0.3f;
			lrg_camera_thirdperson_set_yaw (camera_tp, camera_angle * (180.0f / G_PI));
			lrg_camera_thirdperson_follow (camera_tp, 0.0f, 1.0f, 0.0f, delta);
		}

		/* Render */
		lrg_renderer_begin_frame (renderer);
		lrg_renderer_clear (renderer, bg_color);

		/* Render world layer (with camera transform) */
		lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_WORLD);

		/* Draw standard primitive shapes */
		for (i = 0; i < shapes->len; i++)
		{
			LrgShape3D *shape;

			shape = g_ptr_array_index (shapes, i);
			lrg_drawable_draw (LRG_DRAWABLE (shape), delta);
		}

		/* Draw mesh models */
		for (i = 0; i < mesh_models->len; i++)
		{
			MeshModelEntry *entry;

			entry = g_ptr_array_index (mesh_models, i);
			draw_mesh_model (entry);
		}

		lrg_renderer_end_layer (renderer);

		/* Render UI layer */
		lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_UI);
		{
			g_autoptr(GrlColor)  white = NULL;
			g_autoptr(GrlColor)  gray = NULL;
			g_autoptr(LrgText2D) title = NULL;
			g_autoptr(LrgText2D) info = NULL;
			g_autoptr(LrgText2D) mesh_info = NULL;
			g_autoptr(LrgText2D) camera_info = NULL;
			g_autoptr(LrgText2D) controls = NULL;
			g_autofree gchar    *info_text = NULL;
			g_autofree gchar    *mesh_text = NULL;
			const gchar         *camera_name = NULL;

			white = grl_color_new (255, 255, 255, 255);
			gray = grl_color_new (180, 180, 180, 255);

			title = lrg_text2d_new_full (10.0f, 10.0f,
			                             "Taco Truck (YAML Mesh Renderer)",
			                             24.0f, white);
			lrg_drawable_draw (LRG_DRAWABLE (title), delta);

			info_text = g_strdup_printf ("Entities: %u | Shapes: %u",
			                             lrg_scene_get_entity_count (scene),
			                             shapes->len);
			info = lrg_text2d_new_full (10.0f, 40.0f, info_text, 18.0f, white);
			lrg_drawable_draw (LRG_DRAWABLE (info), delta);

			mesh_text = g_strdup_printf ("Mesh Models: %u", mesh_models->len);
			mesh_info = lrg_text2d_new_full (10.0f, 65.0f, mesh_text, 18.0f, white);
			lrg_drawable_draw (LRG_DRAWABLE (mesh_info), delta);

			/* Show current camera mode */
			camera_name = (camera_mode == CAMERA_MODE_THIRDPERSON)
			              ? "Third-Person (rotating)"
			              : "Isometric";
			camera_info = lrg_text2d_new_full (10.0f, 90.0f, camera_name, 18.0f, white);
			lrg_drawable_draw (LRG_DRAWABLE (camera_info), delta);

			/* Show controls */
			controls = lrg_text2d_new_full (10.0f, 115.0f,
			                                "Press 'C' to switch camera",
			                                16.0f, gray);
			lrg_drawable_draw (LRG_DRAWABLE (controls), delta);
		}
		lrg_renderer_end_layer (renderer);

		lrg_renderer_end_frame (renderer);
	}

	/* Cleanup */
	lrg_engine_shutdown (engine);

	return 0;
}
