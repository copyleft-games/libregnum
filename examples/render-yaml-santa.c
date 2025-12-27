/* render-yaml-santa.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Example demonstrating how to load and render a Blender-exported
 * YAML scene file using libregnum's scene module.
 */

#include <libregnum.h>
#include <graylib.h>
#include <math.h>

/* =============================================================================
 * SHAPE CONVERSION
 * ========================================================================== */

/**
 * scene_object_to_shape:
 * @obj: A scene object from the loaded scene
 *
 * Converts an LrgSceneObject to the appropriate LrgShape3D subclass
 * based on its primitive type and parameters.
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
 * load_scene_shapes:
 * @scene: The loaded scene
 * @shapes: (out): Array to store shapes in
 *
 * Iterates all entities and objects in the scene, converting each
 * to a renderable shape.
 */
static void
load_scene_shapes (LrgScene  *scene,
                   GPtrArray *shapes)
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
			LrgSceneObject *obj;
			LrgShape3D     *shape;

			obj = g_ptr_array_index (objects, i);
			shape = scene_object_to_shape (obj);

			if (shape != NULL)
			{
				g_ptr_array_add (shapes, shape);
			}
		}
	}

	g_list_free (entity_names);
}

/* =============================================================================
 * MAIN
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GError)                   error = NULL;
	g_autoptr(LrgSceneSerializerYaml)   serializer = NULL;
	g_autoptr(LrgScene)                 scene = NULL;
	g_autoptr(GPtrArray)                shapes = NULL;
	LrgEngine                          *engine;
	LrgRenderer                        *renderer;
	g_autoptr(LrgGrlWindow)             window = NULL;
	g_autoptr(LrgCameraThirdPerson)     camera = NULL;
	g_autoptr(GrlColor)                 bg_color = NULL;
	gfloat                              camera_angle = 0.0f;
	guint                               i;

	/* Create window first */
	window = lrg_grl_window_new (1024, 768, "Santa Sleigh Scene - YAML Renderer");
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

	/* Load YAML scene */
	serializer = lrg_scene_serializer_yaml_new ();
	scene = lrg_scene_serializer_load_from_file (
		LRG_SCENE_SERIALIZER (serializer),
		"data/santa_sleigh_scene.yaml",
		&error);

	if (scene == NULL)
	{
		g_error ("Failed to load scene: %s", error->message);
		return 1;
	}

	g_print ("Loaded scene: %s\n", lrg_scene_get_name (scene));
	g_print ("Exported from: %s\n", lrg_scene_get_exported_from (scene));
	g_print ("Entity count: %u\n", lrg_scene_get_entity_count (scene));

	/* Convert scene objects to shapes */
	shapes = g_ptr_array_new_with_free_func (g_object_unref);
	load_scene_shapes (scene, shapes);
	g_print ("Created %u shapes\n", shapes->len);

	/* Create third-person camera for viewing */
	camera = lrg_camera_thirdperson_new ();
	lrg_camera_thirdperson_set_distance (camera, 30.0f);
	lrg_camera_thirdperson_set_pitch (camera, 40.0f);
	lrg_camera_thirdperson_set_height_offset (camera, 3.0f);
	lrg_camera_thirdperson_snap_to_target (camera, 0.0f, 1.0f, 0.0f);

	lrg_renderer_set_camera (renderer, LRG_CAMERA (camera));
	bg_color = grl_color_new (40, 44, 52, 255);

	/* Main render loop */
	while (!lrg_window_should_close (LRG_WINDOW (window)))
	{
		gfloat delta;

		delta = lrg_window_get_frame_time (LRG_WINDOW (window));

		/* Auto-rotate camera around the scene */
		camera_angle += delta * 0.3f;
		lrg_camera_thirdperson_set_yaw (camera, camera_angle * (180.0f / G_PI));
		lrg_camera_thirdperson_follow (camera, 0.0f, 1.0f, 0.0f, delta);

		/* Render */
		lrg_renderer_begin_frame (renderer);
		lrg_renderer_clear (renderer, bg_color);

		/* Render world layer (with camera transform) */
		lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_WORLD);

		for (i = 0; i < shapes->len; i++)
		{
			LrgShape3D *shape;

			shape = g_ptr_array_index (shapes, i);
			lrg_drawable_draw (LRG_DRAWABLE (shape), delta);
		}

		lrg_renderer_end_layer (renderer);

		/* Render UI layer */
		lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_UI);
		{
			g_autoptr(GrlColor)  white = NULL;
			g_autoptr(LrgText2D) title = NULL;
			g_autoptr(LrgText2D) info = NULL;
			g_autofree gchar    *info_text = NULL;

			white = grl_color_new (255, 255, 255, 255);

			title = lrg_text2d_new_full (10.0f, 10.0f,
			                             "Santa Sleigh Scene (YAML Renderer)",
			                             24.0f, white);
			lrg_drawable_draw (LRG_DRAWABLE (title), delta);

			info_text = g_strdup_printf ("Entities: %u | Shapes: %u",
			                             lrg_scene_get_entity_count (scene),
			                             shapes->len);
			info = lrg_text2d_new_full (10.0f, 40.0f, info_text, 18.0f, white);
			lrg_drawable_draw (LRG_DRAWABLE (info), delta);
		}
		lrg_renderer_end_layer (renderer);

		lrg_renderer_end_frame (renderer);
	}

	/* Cleanup */
	lrg_engine_shutdown (engine);

	return 0;
}
