/* test-world3d-components.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the LrgEntity3D components of 0.9.0: mesh renderer, light,
 * camera, audio source and listener, billboard, tilemap, spawn, trigger
 * and skinned model.  Everything runs headless (properties, render items
 * with their world matrices, layers and flags, registered lights, camera
 * selection, trigger signals, the audio listener); a GPU check draws a
 * mesh renderer lit by a light component through LrgWorld3DRenderer in a
 * hidden window when a display exists.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

#define EPS (1e-4f)

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

/* Opens a hidden 8x8 window when a display exists. */
static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-world3d-components-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* A new entity named @name at (x, y, z), added to @world (the world holds
 * it; the returned pointer is borrowed). */
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
	return e;
}

/* Adds @component to @entity and drops the caller's reference. */
static gpointer
attach (LrgEntity3D *entity,
        gpointer     component)
{
	g_assert_true (lrg_entity3d_add_component (entity, LRG_COMPONENT (component)));
	g_object_unref (component);
	return component;
}

static void
assert_matrix_eq (const gfloat *a,
                  const gfloat *b)
{
	guint i;

	for (i = 0; i < 16; i++)
		g_assert_cmpfloat_with_epsilon (a[i], b[i], EPS);
}

static void
assert_vec3 (const gfloat *v,
             gfloat        x,
             gfloat        y,
             gfloat        z)
{
	g_assert_cmpfloat_with_epsilon (v[0], x, EPS);
	g_assert_cmpfloat_with_epsilon (v[1], y, EPS);
	g_assert_cmpfloat_with_epsilon (v[2], z, EPS);
}

/* Counts notify emissions. */
static void
count_notify (GObject    *object,
              GParamSpec *pspec,
              gpointer    user_data)
{
	(void) object;
	(void) pspec;
	(*(guint *) user_data)++;
}

/* ==========================================================================
 * Mesh renderer
 * ========================================================================== */

static void
test_mesh_renderer_properties (void)
{
	g_autoptr(LrgMeshRendererComponent) mr = lrg_mesh_renderer_component_new ();
	gfloat   color[4];
	gfloat   size[3];
	guint    notified = 0;
	gint     layer;
	gchar   *asset = NULL;
	gfloat   roughness;
	GObject *material;

	/* Defaults */
	g_assert_cmpint (lrg_mesh_renderer_component_get_primitive (mr), ==, LRG_PRIMITIVE_CUBE);
	g_assert_null (lrg_mesh_renderer_component_get_mesh_asset (mr));
	g_assert_true (lrg_mesh_renderer_component_get_cast_shadows (mr));
	g_assert_true (lrg_mesh_renderer_component_get_receive_shadows (mr));
	g_assert_cmpint (lrg_mesh_renderer_component_get_layer (mr), ==, -1);
	lrg_mesh_renderer_component_get_color (mr, color);
	g_assert_cmpfloat (color[3], ==, 1.0f);
	g_assert_cmpuint (lrg_mesh_renderer_component_get_flags (mr), ==,
	                  LRG_RENDER_ITEM_FLAG_CAST_SHADOW | LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW);

	/* Properties through GObject (what level instantiate and the inspector use) */
	g_signal_connect (mr, "notify", G_CALLBACK (count_notify), &notified);
	g_object_set (mr, "primitive", LRG_PRIMITIVE_UV_SPHERE, "radius", 2.0f, "layer", 7,
	              "roughness", 0.25f, "mesh-asset", "models/crate.glb", NULL);
	g_assert_cmpuint (notified, ==, 5);
	g_object_set (mr, "layer", 7, NULL);   /* unchanged: no notify (explicit notify) */
	g_assert_cmpuint (notified, ==, 5);
	g_object_get (mr, "layer", &layer, "mesh-asset", &asset, "roughness", &roughness, NULL);
	g_assert_cmpint (layer, ==, 7);
	g_assert_cmpstr (asset, ==, "models/crate.glb");
	g_assert_cmpfloat_with_epsilon (roughness, 0.25f, EPS);
	g_free (asset);

	lrg_mesh_renderer_component_set_size (mr, 2.0f, 3.0f, 4.0f);
	lrg_mesh_renderer_component_get_size (mr, size);
	assert_vec3 (size, 2.0f, 3.0f, 4.0f);

	/* The scalar material is mirrored into an LrgMaterial3D */
	lrg_mesh_renderer_component_set_color (mr, 1.0f, 0.5f, 0.25f, 1.0f);
	lrg_mesh_renderer_component_set_emission (mr, 0.0f, 1.0f, 0.0f, 3.0f);
	material = lrg_mesh_renderer_component_get_effective_material (mr);
	g_assert_true (LRG_IS_MATERIAL3D (material));
	{
		gfloat r, g, b, a;

		lrg_material3d_get_color (LRG_MATERIAL3D (material), &r, &g, &b, &a);
		g_assert_cmpfloat_with_epsilon (g, 0.5f, EPS);
		g_assert_cmpfloat_with_epsilon (lrg_material3d_get_roughness (LRG_MATERIAL3D (material)), 0.25f, EPS);
		g_assert_cmpfloat_with_epsilon (lrg_material3d_get_emission_strength (LRG_MATERIAL3D (material)),
		                                3.0f, EPS);
	}

	/* The material slot wins */
	{
		g_autoptr(LrgMaterial3D) slot = lrg_material3d_new ();

		lrg_mesh_renderer_component_set_material (mr, G_OBJECT (slot));
		g_assert_true (lrg_mesh_renderer_component_get_effective_material (mr) == G_OBJECT (slot));
		lrg_mesh_renderer_component_set_material (mr, NULL);
		g_assert_true (lrg_mesh_renderer_component_get_effective_material (mr) == material);
	}

	/* Flags follow the render-state properties */
	g_object_set (mr, "cast-shadows", FALSE, "double-sided", TRUE, "unlit", TRUE, NULL);
	g_assert_cmpuint (lrg_mesh_renderer_component_get_flags (mr), ==,
	                  LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW | LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED |
	                  LRG_RENDER_ITEM_FLAG_UNLIT);
	g_object_set (mr, "color-a", 0.5f, NULL);
	g_assert_true ((lrg_mesh_renderer_component_get_flags (mr) & LRG_RENDER_ITEM_FLAG_TRANSPARENT) != 0);
}

static void
test_mesh_renderer_items (void)
{
	g_autoptr(LrgWorld3D)    world = lrg_world3d_new ();
	g_autoptr(LrgRenderList) list = lrg_render_list_new ();
	LrgEntity3D              *parent, *child;
	LrgMeshRendererComponent *mr;
	const LrgRenderItem      *item;
	gfloat                    world_m[16];
	gfloat                    scale[3] = { 2.0f, 2.0f, 2.0f };

	/* A cube on a child of a moved, scaled parent */
	parent = add_entity (world, "parent", 10.0f, 0.0f, 0.0f);
	lrg_entity3d_set_scale (parent, scale);
	child = lrg_entity3d_new_with_name ("child");
	lrg_entity3d_set_position_xyz (child, 0.0f, 1.0f, 0.0f);
	lrg_entity3d_add_child (parent, child);
	g_object_unref (child);
	lrg_entity3d_set_layer (child, 3);

	mr = attach (child, lrg_mesh_renderer_component_new ());
	lrg_mesh_renderer_component_set_size (mr, 1.0f, 2.0f, 3.0f);
	lrg_mesh_renderer_component_set_color (mr, 1.0f, 0.0f, 0.0f, 1.0f);

	lrg_world3d_collect_render (world, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_OPAQUE), ==, 1);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_SHADOW_CASTER), ==, 1);
	item = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_OPAQUE, 0);
	g_assert_cmpint (item->kind, ==, LRG_RENDER_ITEM_PRIMITIVE);
	g_assert_cmpint (item->primitive, ==, LRG_PRIMITIVE_CUBE);
	assert_vec3 (item->params, 1.0f, 2.0f, 3.0f);
	g_assert_cmpuint (item->layer, ==, 3);
	g_assert_true (item->entity == child);
	g_assert_true (item->component == LRG_COMPONENT (mr));
	g_assert_true (LRG_IS_MATERIAL3D (item->material));
	g_assert_cmpfloat (item->color[0], ==, 1.0f);
	g_assert_cmpfloat (item->color[1], ==, 0.0f);

	/* World matrix: parent T(10,0,0) S(2) times child T(0,1,0) */
	lrg_entity3d_get_world_matrix (child, world_m);
	assert_matrix_eq (item->world_matrix, world_m);
	g_assert_cmpfloat_with_epsilon (item->world_matrix[0], 2.0f, EPS);
	assert_vec3 (&item->world_matrix[12], 10.0f, 2.0f, 0.0f);

	/* World bounds: half extents (0.5, 1, 1.5) scaled by 2 around (10, 2, 0) */
	g_assert_cmpfloat_with_epsilon (item->bounds.min.x, 9.0f, EPS);
	g_assert_cmpfloat_with_epsilon (item->bounds.max.y, 4.0f, EPS);
	g_assert_cmpfloat_with_epsilon (item->bounds.max.z, 3.0f, EPS);

	/* Layer override and no shadows */
	g_object_set (mr, "layer", 9, "cast-shadows", FALSE, NULL);
	lrg_world3d_collect_render (world, NULL, list);
	item = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_OPAQUE, 0);
	g_assert_cmpuint (item->layer, ==, 9);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_SHADOW_CASTER), ==, 0);

	/* Translucent: the transparent list */
	lrg_mesh_renderer_component_set_color (mr, 1.0f, 1.0f, 1.0f, 0.5f);
	lrg_world3d_collect_render (world, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_OPAQUE), ==, 0);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_TRANSPARENT), ==, 1);

	/* Sphere params: the radius */
	lrg_mesh_renderer_component_set_color (mr, 1.0f, 1.0f, 1.0f, 1.0f);
	g_object_set (mr, "primitive", LRG_PRIMITIVE_UV_SPHERE, "radius", 0.75f, NULL);
	lrg_world3d_collect_render (world, NULL, list);
	item = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_OPAQUE, 0);
	g_assert_cmpfloat (item->params[0], ==, 0.75f);

	/* A mesh asset without an asset manager: MODEL with no model and
	 * unknown bounds, left for the renderer to resolve */
	lrg_mesh_renderer_component_set_mesh_asset (mr, "models/missing.glb");
	lrg_world3d_collect_render (world, NULL, list);
	item = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_OPAQUE, 0);
	g_assert_cmpint (item->kind, ==, LRG_RENDER_ITEM_MODEL);
	g_assert_null (item->model);
	g_assert_cmpint (item->mesh_index, ==, -1);
	g_assert_cmpfloat (item->bounds.min.x, >, item->bounds.max.x);

	/* Disabled components and invisible entities submit nothing */
	lrg_component_set_enabled (LRG_COMPONENT (mr), FALSE);
	lrg_world3d_collect_render (world, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_total_items (list), ==, 0);
	lrg_component_set_enabled (LRG_COMPONENT (mr), TRUE);
	lrg_entity3d_set_visible (parent, FALSE);
	lrg_world3d_collect_render (world, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_total_items (list), ==, 0);
}

static void
test_mesh_renderer_bounds (void)
{
	g_autoptr(LrgMeshRendererComponent) mr = lrg_mesh_renderer_component_new_primitive (LRG_PRIMITIVE_CYLINDER);
	LrgBoundingBox3D                    b;

	g_object_set (mr, "radius", 1.0f, "height", 4.0f, NULL);
	g_assert_true (lrg_mesh_renderer_component_get_local_bounds (mr, &b));
	g_assert_cmpfloat (b.min.x, ==, -1.0f);
	g_assert_cmpfloat (b.max.y, ==, 2.0f);
	g_assert_true (lrg_component_get_bounds (LRG_COMPONENT (mr), &b));

	lrg_mesh_renderer_component_set_primitive (mr, LRG_PRIMITIVE_PLANE);
	lrg_mesh_renderer_component_set_size (mr, 4.0f, 1.0f, 6.0f);
	g_assert_true (lrg_mesh_renderer_component_get_local_bounds (mr, &b));
	g_assert_cmpfloat (b.max.x, ==, 2.0f);
	g_assert_cmpfloat (b.max.y, ==, 0.0f);
	g_assert_cmpfloat (b.max.z, ==, 3.0f);

	lrg_mesh_renderer_component_set_mesh_asset (mr, "x.glb");
	g_assert_false (lrg_mesh_renderer_component_get_local_bounds (mr, &b));
}

/* ==========================================================================
 * Light
 * ========================================================================== */

static void
test_light_registration (void)
{
	g_autoptr(LrgWorld3D) world = lrg_world3d_new ();
	g_autoptr(GPtrArray)  lights = NULL;
	LrgEntity3D          *e;
	LrgLightComponent    *light;
	const LrgLight3DDesc *desc;
	gfloat                euler[3] = { 0.0f, (gfloat) G_PI_2, 0.0f };

	e = add_entity (world, "lamp", 1.0f, 5.0f, 2.0f);
	lrg_entity3d_set_rotation_euler (e, euler);   /* -Z turns to -X */

	light = attach (e, lrg_light_component_new_with_kind (LRG_LIGHT3D_SPOT));
	g_object_set (light, "intensity", 2.5f, "range", 20.0f, "color-r", 1.0f, "color-g", 0.5f,
	              "color-b", 0.0f, "cast-shadows", TRUE, NULL);
	lrg_light_component_set_spot_angles (light, 20.0f, 40.0f);

	/* Registered on world entry (the entity is already in the world) */
	g_assert_cmpuint (lrg_world3d_get_n_lights (world), ==, 1);
	lights = lrg_world3d_get_lights (world);
	desc = (const LrgLight3DDesc *) g_ptr_array_index (lights, 0);
	g_assert_cmpint (desc->kind, ==, LRG_LIGHT3D_SPOT);
	g_assert_cmpfloat (desc->intensity, ==, 2.5f);
	g_assert_cmpfloat (desc->range, ==, 20.0f);
	g_assert_true (desc->cast_shadows);
	g_assert_true (desc->enabled);
	g_assert_true (desc->source == G_OBJECT (light));
	assert_vec3 (desc->color, 1.0f, 0.5f, 0.0f);
	g_assert_cmpfloat_with_epsilon (desc->inner_angle, 20.0f * (gfloat) G_PI / 180.0f, EPS);
	g_assert_cmpfloat_with_epsilon (desc->outer_angle, 40.0f * (gfloat) G_PI / 180.0f, EPS);
	assert_vec3 (desc->position, 1.0f, 5.0f, 2.0f);
	assert_vec3 (desc->direction, -1.0f, 0.0f, 0.0f);
	g_clear_pointer (&lights, g_ptr_array_unref);

	/* The entity moves: late update re-registers from the world transform */
	lrg_entity3d_set_position_xyz (e, -4.0f, 0.0f, 0.0f);
	lrg_world3d_late_update (world, 0.016f);
	lights = lrg_world3d_get_lights (world);
	desc = (const LrgLight3DDesc *) g_ptr_array_index (lights, 0);
	assert_vec3 (desc->position, -4.0f, 0.0f, 0.0f);
	g_clear_pointer (&lights, g_ptr_array_unref);

	/* Property edits apply at once */
	lrg_light_component_set_intensity (light, 7.0f);
	lights = lrg_world3d_get_lights (world);
	desc = (const LrgLight3DDesc *) g_ptr_array_index (lights, 0);
	g_assert_cmpfloat (desc->intensity, ==, 7.0f);
	g_clear_pointer (&lights, g_ptr_array_unref);

	/* Disabling turns it off without unregistering */
	lrg_component_set_enabled (LRG_COMPONENT (light), FALSE);
	lights = lrg_world3d_get_lights (world);
	desc = (const LrgLight3DDesc *) g_ptr_array_index (lights, 0);
	g_assert_false (desc->enabled);
	g_clear_pointer (&lights, g_ptr_array_unref);

	/* Leaving the world unregisters */
	lrg_world3d_remove_entity (world, e);
	g_assert_cmpuint (lrg_world3d_get_n_lights (world), ==, 0);
}

static void
test_light_in_render_list (void)
{
	g_autoptr(LrgWorld3D)    world = lrg_world3d_new ();
	g_autoptr(LrgRenderList) list = lrg_render_list_new ();
	g_autoptr(LrgEntity3D)   e = lrg_entity3d_new_with_name ("sun");
	LrgLightComponent       *light;
	const LrgLight3DDesc    *desc;

	/* Added to the entity before the entity joins the world */
	light = attach (e, lrg_light_component_new_with_kind (LRG_LIGHT3D_DIRECTIONAL));
	g_assert_cmpuint (lrg_world3d_get_n_lights (world), ==, 0);
	lrg_world3d_add_entity (world, e);
	g_assert_cmpuint (lrg_world3d_get_n_lights (world), ==, 1);

	lrg_world3d_collect_render (world, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_n_lights (list), ==, 1);
	desc = lrg_render_list_get_light (list, 0);
	g_assert_cmpint (desc->kind, ==, LRG_LIGHT3D_DIRECTIONAL);
	assert_vec3 (desc->direction, 0.0f, 0.0f, -1.0f);

	/* Removing the component unregisters too */
	lrg_entity3d_remove_component (e, LRG_COMPONENT (light));
	g_assert_cmpuint (lrg_world3d_get_n_lights (world), ==, 0);
}

/* ==========================================================================
 * Camera
 * ========================================================================== */

static void
test_camera_properties (void)
{
	g_autoptr(LrgCameraComponent) cam = lrg_camera_component_new ();
	LrgCamera3D                  *c3 = lrg_camera_component_get_camera (cam);

	g_assert_true (LRG_IS_CAMERA3D (c3));
	g_assert_true (lrg_camera_component_from_camera (LRG_CAMERA (c3)) == cam);
	g_assert_cmpfloat (lrg_camera_component_get_fov (cam), ==, 60.0f);
	g_assert_cmpfloat_with_epsilon (lrg_camera3d_get_fovy (c3), 60.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_camera_component_get_near (cam), 0.1f, EPS);
	g_assert_cmpfloat (lrg_camera_component_get_far (cam), ==, 1000.0f);
	g_assert_true (lrg_camera_component_get_active (cam));

	/* Projection properties reach the camera immediately */
	g_object_set (cam, "fov", 75.0f, "cull-mask", 0x5u, NULL);
	g_assert_cmpfloat_with_epsilon (lrg_camera3d_get_fovy (c3), 75.0f, EPS);
	g_assert_cmpuint (lrg_camera_get_cull_mask (LRG_CAMERA (c3)), ==, 0x5u);
	g_object_set (cam, "orthographic", TRUE, "ortho-size", 12.0f, NULL);
	g_assert_cmpint (lrg_camera3d_get_projection (c3), ==, LRG_PROJECTION_ORTHOGRAPHIC);
	g_assert_cmpfloat_with_epsilon (lrg_camera3d_get_fovy (c3), 12.0f, EPS);

	lrg_camera_component_set_clip (cam, 0.5f, 50.0f);
	g_assert_cmpfloat (lrg_camera_component_get_near (cam), ==, 0.5f);
	g_assert_cmpfloat (lrg_camera_component_get_far (cam), ==, 50.0f);
	lrg_camera_component_set_clip (cam, 5.0f, 1.0f);   /* far follows near */
	g_assert_cmpfloat (lrg_camera_component_get_far (cam), >, 5.0f);
}

static void
test_camera_selection (void)
{
	g_autoptr(LrgWorld3D)  world = lrg_world3d_new ();
	LrgEntity3D           *a, *b;
	LrgCameraComponent    *low, *high;
	g_autoptr(GrlVector3)  pos = NULL;
	g_autoptr(GrlVector3)  target = NULL;

	a = add_entity (world, "cam-low", 0.0f, 2.0f, 10.0f);
	b = add_entity (world, "cam-high", 5.0f, 1.0f, 0.0f);
	low = attach (a, lrg_camera_component_new ());
	high = attach (b, lrg_camera_component_new ());
	lrg_camera_component_set_priority (low, 1);
	lrg_camera_component_set_priority (high, 5);

	/* Highest priority wins */
	g_assert_true (lrg_world3d_get_active_camera (world) == LRG_CAMERA (lrg_camera_component_get_camera (high)));
	g_assert_true (lrg_camera_component_is_current (high));
	g_assert_false (lrg_camera_component_is_current (low));

	/* Priority changes re-rank at once */
	lrg_camera_component_set_priority (low, 10);
	g_assert_true (lrg_camera_component_is_current (low));

	/* Inactive and disabled cameras drop out */
	lrg_camera_component_set_active (low, FALSE);
	g_assert_true (lrg_camera_component_is_current (high));
	lrg_component_set_enabled (LRG_COMPONENT (high), FALSE);
	g_assert_null (lrg_world3d_get_active_camera (world));
	lrg_component_set_enabled (LRG_COMPONENT (high), TRUE);
	lrg_camera_component_set_active (low, TRUE);

	/* Forcing beats priority */
	lrg_camera_component_make_current (high);
	g_assert_true (lrg_camera_component_is_current (high));
	lrg_world3d_set_active_camera (world, NULL);
	g_assert_true (lrg_camera_component_is_current (low));

	/* The camera follows the entity: eye at it, looking down -Z */
	pos = lrg_camera3d_get_position (lrg_camera_component_get_camera (low));
	target = lrg_camera3d_get_target (lrg_camera_component_get_camera (low));
	g_assert_cmpfloat_with_epsilon (pos->z, 10.0f, EPS);
	g_assert_cmpfloat_with_epsilon (target->z, 9.0f, EPS);
	g_clear_pointer (&pos, grl_vector3_free);
	lrg_entity3d_set_position_xyz (a, 3.0f, 3.0f, 3.0f);
	lrg_world3d_late_update (world, 0.016f);
	pos = lrg_camera3d_get_position (lrg_camera_component_get_camera (low));
	g_assert_cmpfloat_with_epsilon (pos->x, 3.0f, EPS);

	/* Leaving the world unregisters */
	lrg_world3d_remove_entity (world, b);
	lrg_world3d_remove_entity (world, a);
	g_assert_null (lrg_world3d_get_active_camera (world));
	{
		g_autoptr(GPtrArray) cameras = lrg_world3d_get_cameras (world);

		g_assert_cmpuint (cameras->len, ==, 0);
	}
}

/* ==========================================================================
 * Audio
 * ========================================================================== */

static void
test_audio_listener_follows (void)
{
	g_autoptr(LrgWorld3D)       world = lrg_world3d_new ();
	LrgAudioManager            *manager = lrg_audio_manager_get_default ();
	LrgEntity3D                *e;
	LrgAudioListenerComponent  *listener;
	gfloat                      pos[3], fwd[3], up[3];
	gfloat                      euler[3] = { 0.0f, (gfloat) G_PI, 0.0f };

	e = add_entity (world, "ears", 3.0f, 4.0f, 5.0f);
	listener = attach (e, lrg_audio_listener_component_new ());
	g_assert_null (lrg_audio_listener_component_get_audio_manager (listener));

	/* World entry: the entity is the world's listener and the manager hears there */
	g_assert_true (lrg_world3d_get_audio_listener (world) == e);
	lrg_audio_manager_get_listener (manager, pos, fwd, up);
	assert_vec3 (pos, 3.0f, 4.0f, 5.0f);
	assert_vec3 (fwd, 0.0f, 0.0f, -1.0f);
	assert_vec3 (up, 0.0f, 1.0f, 0.0f);

	/* Moving and turning: the late update follows */
	lrg_entity3d_set_position_xyz (e, -1.0f, 0.0f, 2.0f);
	lrg_entity3d_set_rotation_euler (e, euler);
	lrg_world3d_late_update (world, 0.016f);
	lrg_audio_manager_get_listener (manager, pos, fwd, NULL);
	assert_vec3 (pos, -1.0f, 0.0f, 2.0f);
	assert_vec3 (fwd, 0.0f, 0.0f, 1.0f);

	/* A disabled listener stops feeding */
	lrg_component_set_enabled (LRG_COMPONENT (listener), FALSE);
	lrg_entity3d_set_position_xyz (e, 50.0f, 0.0f, 0.0f);
	lrg_world3d_late_update (world, 0.016f);
	lrg_audio_manager_get_listener (manager, pos, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (pos[0], -1.0f, EPS);

	lrg_world3d_remove_entity (world, e);
	g_assert_null (lrg_world3d_get_audio_listener (world));
}

static void
test_audio_source_headless (void)
{
	g_autoptr(LrgWorld3D)     world = lrg_world3d_new ();
	LrgEntity3D              *e;
	LrgAudioSourceComponent  *src;
	LrgAudioAttenuation       att;
	gfloat                    volume, pitch;
	gboolean                  playing;

	e = add_entity (world, "radio", 0.0f, 0.0f, 0.0f);
	src = attach (e, lrg_audio_source_component_new_with_sound ("no-such-bank", "song"));
	g_assert_cmpstr (lrg_audio_source_component_get_bank (src), ==, "no-such-bank");
	g_assert_cmpstr (lrg_audio_source_component_get_sound (src), ==, "song");

	/* Attenuation defaults are the audio manager's */
	lrg_audio_source_component_get_attenuation (src, &att);
	g_assert_cmpfloat (att.min_distance, ==, LRG_AUDIO_DEFAULT_MIN_DISTANCE);
	g_assert_cmpfloat (att.max_distance, ==, LRG_AUDIO_DEFAULT_MAX_DISTANCE);
	g_assert_cmpfloat (att.rolloff, ==, LRG_AUDIO_DEFAULT_ROLLOFF);
	g_assert_cmpfloat (att.spatial_blend, ==, 1.0f);

	g_object_set (src, "volume", 0.5f, "pitch", 1.25f, "min-distance", 2.0f, "max-distance", 30.0f,
	              "loop", TRUE, NULL);
	g_object_get (src, "volume", &volume, "pitch", &pitch, "playing", &playing, NULL);
	g_assert_cmpfloat (volume, ==, 0.5f);
	g_assert_cmpfloat (pitch, ==, 1.25f);
	g_assert_false (playing);
	lrg_audio_source_component_get_attenuation (src, &att);
	g_assert_cmpfloat (att.min_distance, ==, 2.0f);
	g_assert_cmpfloat (att.max_distance, ==, 30.0f);

	/* Nothing to play (unknown bank, no device): fails quietly */
	g_assert_cmpuint (lrg_audio_source_component_play (src), ==, 0);
	g_assert_false (lrg_audio_source_component_is_playing (src));
	g_assert_cmpuint (lrg_audio_source_component_get_voice (src), ==, 0);
	lrg_world3d_late_update (world, 0.016f);   /* loop with no voice: no retry storm */
	g_assert_cmpuint (lrg_audio_source_component_get_voice (src), ==, 0);

	/* A missing file does not crash either */
	lrg_audio_source_component_set_sound (src, NULL, NULL);
	lrg_audio_source_component_set_sound_path (src, "/nonexistent/sound.wav");
	g_assert_cmpuint (lrg_audio_source_component_play (src), ==, 0);
	lrg_audio_source_component_stop (src);
}

/* ==========================================================================
 * Billboard
 * ========================================================================== */

static void
test_billboard_items (void)
{
	g_autoptr(LrgWorld3D)    world = lrg_world3d_new ();
	g_autoptr(LrgRenderList) list = lrg_render_list_new ();
	LrgEntity3D             *e;
	LrgBillboardComponent   *bb;
	const LrgRenderItem     *item;
	gfloat                   w, h;
	const gfloat            *m;

	e = add_entity (world, "tree", 4.0f, 0.0f, -2.0f);
	bb = attach (e, lrg_billboard_component_new_with_texture_path ("sprites/tree.png"));
	lrg_billboard_component_set_size (bb, 2.0f, 3.0f);
	g_assert_cmpstr (lrg_billboard_component_get_texture_path (bb), ==, "sprites/tree.png");
	lrg_billboard_component_get_effective_size (bb, &w, &h);
	g_assert_cmpfloat (w, ==, 2.0f);
	g_assert_cmpfloat (h, ==, 3.0f);

	/* Spherical: a BILLBOARD item, alpha tested, the mode in params[2] */
	lrg_world3d_collect_render (world, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_CUTOUT), ==, 1);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_SHADOW_CASTER), ==, 0);
	item = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_CUTOUT, 0);
	g_assert_cmpint (item->kind, ==, LRG_RENDER_ITEM_BILLBOARD);
	g_assert_cmpfloat (item->params[0], ==, 2.0f);
	g_assert_cmpfloat (item->params[1], ==, 3.0f);
	g_assert_cmpfloat (item->params[2], ==, (gfloat) LRG_BILLBOARD_MODE_SPHERICAL);
	g_assert_true ((item->flags & LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED) != 0);
	assert_vec3 (&item->world_matrix[12], 4.0f, 0.0f, -2.0f);
	g_assert_null (item->texture);   /* no asset manager: unresolved */

	g_object_set (bb, "facing", LRG_BILLBOARD_FACING_AXIS_Y, NULL);
	lrg_world3d_collect_render (world, NULL, list);
	item = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_CUTOUT, 0);
	g_assert_cmpfloat (item->params[2], ==, (gfloat) LRG_BILLBOARD_MODE_AXIS_Y);

	/* Fixed: a plane primitive turned into the local XY plane */
	lrg_billboard_component_set_facing (bb, LRG_BILLBOARD_FACING_FIXED);
	lrg_world3d_collect_render (world, NULL, list);
	item = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_CUTOUT, 0);
	g_assert_cmpint (item->kind, ==, LRG_RENDER_ITEM_PRIMITIVE);
	g_assert_cmpint (item->primitive, ==, LRG_PRIMITIVE_PLANE);
	m = item->world_matrix;
	assert_vec3 (&m[0], 1.0f, 0.0f, 0.0f);   /* X stays */
	assert_vec3 (&m[4], 0.0f, 0.0f, 1.0f);   /* plane Y (normal) -> +Z */
	assert_vec3 (&m[8], 0.0f, -1.0f, 0.0f);  /* plane Z -> -Y */
	g_assert_cmpfloat_with_epsilon (item->bounds.max.y, 1.5f, EPS);
	g_assert_cmpfloat_with_epsilon (item->bounds.max.z, -2.0f, EPS);

	/* Translucent tint: blended */
	lrg_billboard_component_set_color (bb, 1.0f, 1.0f, 1.0f, 0.5f);
	lrg_world3d_collect_render (world, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_TRANSPARENT), ==, 1);
}

/* ==========================================================================
 * Tilemap
 * ========================================================================== */

static void
test_tilemap_items (void)
{
	g_autoptr(LrgWorld3D)     world = lrg_world3d_new ();
	g_autoptr(LrgRenderList)  list = lrg_render_list_new ();
	g_autoptr(LrgNodeTilemap) grid = lrg_node_tilemap_new (4, 3);
	LrgEntity3D              *e;
	LrgTilemapComponent      *tm;
	const LrgRenderItem      *item;
	const guint              *cells;
	LrgBoundingBox3D          b;
	guint                     w, h;
	gboolean                  visible;
	gfloat                    m[16];

	/* Two layers; a few painted cells */
	lrg_node_tilemap_add_layer (grid, "top");
	g_assert_cmpuint (lrg_node_tilemap_get_n_layers (grid), ==, 2);
	lrg_node_tilemap_set_tile (grid, 0, 1, 1, 3);
	lrg_node_tilemap_set_tile (grid, 1, 2, 0, 5);

	e = add_entity (world, "map", 0.0f, 0.0f, 0.0f);
	tm = attach (e, lrg_tilemap_component_new_from_node_tilemap (grid, 2.0f));
	g_assert_cmpfloat (lrg_tilemap_component_get_tile_size (tm), ==, 2.0f);
	lrg_tilemap_component_get_grid_size (tm, &w, &h);
	g_assert_cmpuint (w, ==, 4);
	g_assert_cmpuint (h, ==, 3);
	g_assert_cmpuint (lrg_tilemap_component_get_n_layers (tm), ==, 2);
	cells = lrg_tilemap_component_get_layer_cells (tm, 0, &visible);
	g_assert_nonnull (cells);
	g_assert_true (visible);
	g_assert_cmpuint (cells[1 * 4 + 1], ==, 3);
	g_assert_null (lrg_tilemap_component_get_layer_cells (tm, 2, NULL));
	g_assert_null (lrg_tilemap_component_get_tilemap (tm));   /* no tileset yet */

	/* Grid bounds: x 0..8, y -6..0 */
	g_assert_true (lrg_tilemap_component_get_local_bounds (tm, &b));
	g_assert_cmpfloat (b.max.x, ==, 8.0f);
	g_assert_cmpfloat (b.min.y, ==, -6.0f);

	/* One custom item per visible layer, stacked along local +Z */
	lrg_world3d_collect_render (world, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_CUTOUT), ==, 2);
	item = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_CUTOUT, 0);
	g_assert_cmpint (item->kind, ==, LRG_RENDER_ITEM_CUSTOM);
	g_assert_nonnull (item->draw_func);
	g_assert_nonnull (item->user_data);
	g_assert_true ((item->flags & LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED) != 0);
	lrg_entity3d_get_world_matrix (e, m);
	{
		guint i, n_z_offset = 0;

		for (i = 0; i < 2; i++)
		{
			item = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_CUTOUT, i);
			g_assert_cmpfloat (item->world_matrix[0], ==, m[0]);
			if (fabsf (item->world_matrix[14] - LRG_TILEMAP_COMPONENT_LAYER_GAP) < EPS)
				n_z_offset++;
		}
		g_assert_cmpuint (n_z_offset, ==, 1);
	}

	/* A hidden layer submits nothing */
	lrg_node_tilemap_set_layer_visible (lrg_tilemap_component_get_node_tilemap (tm), 1, FALSE);
	lrg_world3d_collect_render (world, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_CUTOUT), ==, 1);

	/* The copy is independent of the level data */
	lrg_node_tilemap_set_tile (grid, 0, 0, 0, 9);
	cells = lrg_tilemap_component_get_layer_cells (tm, 0, NULL);
	g_assert_cmpuint (cells[0], ==, 0);

	/* Clearing the list releases the callback data (no leak under ASan) */
	lrg_render_list_clear (list);
}

/* ==========================================================================
 * Spawn
 * ========================================================================== */

static void
test_spawn_points (void)
{
	g_autoptr(LrgWorld3D)      world = lrg_world3d_new ();
	g_autoptr(GPtrArray)       found = NULL;
	g_autoptr(LrgSpawnPoint3D) point = NULL;
	LrgEntity3D               *a, *b, *c;
	LrgSpawnComponent         *sa, *sb, *sc;
	const gchar               *tags[] = { "start", "red", "start", NULL };
	gchar                    **got = NULL;
	const GValue              *team;
	g_autoptr(GrlVector3)      pos = NULL;

	a = add_entity (world, "red-start", 1.0f, 0.0f, 1.0f);
	b = add_entity (world, "blue-start", -1.0f, 0.0f, 1.0f);
	c = add_entity (world, "ammo", 0.0f, 0.0f, 9.0f);
	sa = attach (a, lrg_spawn_component_new_with_type (LRG_SPAWN_TYPE_PLAYER, 1));
	sb = attach (b, lrg_spawn_component_new_with_type (LRG_SPAWN_TYPE_PLAYER, 2));
	sc = attach (c, lrg_spawn_component_new_with_type (LRG_SPAWN_TYPE_ITEM, 0));

	/* Tags: duplicates dropped; the strv property round-trips */
	lrg_spawn_component_set_tags (sa, tags);
	g_object_get (sa, "tags", &got, NULL);
	g_assert_cmpuint (g_strv_length (got), ==, 2);
	g_strfreev (got);
	lrg_spawn_component_add_tag (sb, "start");
	lrg_spawn_component_set_entity_type (sc, "prefabs/ammo.rprefab");

	found = lrg_spawn_component_find (world, "start", LRG_SPAWN_COMPONENT_ANY_TEAM);
	g_assert_cmpuint (found->len, ==, 2);
	g_clear_pointer (&found, g_ptr_array_unref);
	found = lrg_spawn_component_find (world, "start", 2);
	g_assert_cmpuint (found->len, ==, 1);
	g_assert_true (g_ptr_array_index (found, 0) == sb);
	g_clear_pointer (&found, g_ptr_array_unref);
	found = lrg_spawn_component_find (world, NULL, LRG_SPAWN_COMPONENT_ANY_TEAM);
	g_assert_cmpuint (found->len, ==, 3);
	g_clear_pointer (&found, g_ptr_array_unref);

	/* Disabled components and inactive entities are skipped */
	lrg_component_set_enabled (LRG_COMPONENT (sa), FALSE);
	lrg_entity3d_set_active (c, FALSE);
	found = lrg_spawn_component_find (world, NULL, LRG_SPAWN_COMPONENT_ANY_TEAM);
	g_assert_cmpuint (found->len, ==, 1);
	g_clear_pointer (&found, g_ptr_array_unref);

	/* As a world3d spawn point */
	point = lrg_spawn_component_to_spawn_point (sc);
	g_assert_cmpstr (lrg_spawn_point3d_get_id (point), ==, "ammo");
	g_assert_cmpint (lrg_spawn_point3d_get_spawn_type (point), ==, LRG_SPAWN_TYPE_ITEM);
	g_assert_cmpstr (lrg_spawn_point3d_get_entity_type (point), ==, "prefabs/ammo.rprefab");
	pos = lrg_spawn_point3d_get_position (point);
	g_assert_cmpfloat_with_epsilon (pos->z, 9.0f, EPS);
	team = lrg_spawn_point3d_get_property (point, "team");
	g_assert_nonnull (team);
	g_assert_cmpint (g_value_get_int (team), ==, 0);
}

/* ==========================================================================
 * Trigger
 * ========================================================================== */

typedef struct
{
	guint        entered;
	guint        exited;
	LrgEntity3D *last;
} TriggerLog;

static void
on_entered (LrgTriggerComponent *trigger,
            LrgEntity3D         *entity,
            gpointer             user_data)
{
	TriggerLog *log = (TriggerLog *) user_data;

	(void) trigger;
	log->entered++;
	log->last = entity;
}

static void
on_exited (LrgTriggerComponent *trigger,
           LrgEntity3D         *entity,
           gpointer             user_data)
{
	TriggerLog *log = (TriggerLog *) user_data;

	(void) trigger;
	log->exited++;
	log->last = entity;
}

static void
test_trigger_enter_exit (void)
{
	g_autoptr(LrgWorld3D) world = lrg_world3d_new ();
	LrgEntity3D          *zone, *player, *crate;
	LrgTriggerComponent  *trig;
	TriggerLog            log = { 0, 0, NULL };
	gfloat                step;

	step = lrg_world3d_get_fixed_step (world);
	zone = add_entity (world, "zone", 0.0f, 0.0f, 0.0f);
	trig = attach (zone, lrg_trigger_component_new_box (4.0f, 2.0f, 4.0f));
	lrg_trigger_component_set_filter_tag (trig, "player");
	g_signal_connect (trig, "entered", G_CALLBACK (on_entered), &log);
	g_signal_connect (trig, "exited", G_CALLBACK (on_exited), &log);

	player = add_entity (world, "player", 10.0f, 0.0f, 0.0f);
	lrg_entity3d_add_tag (player, "player");
	crate = add_entity (world, "crate", 0.5f, 0.0f, 0.5f);   /* inside, but untagged */

	lrg_world3d_fixed_update (world, step);
	g_assert_cmpuint (log.entered, ==, 0);
	g_assert_false (lrg_trigger_component_is_inside (trig, crate));

	/* The player walks in: one enter, no repeat while inside */
	lrg_entity3d_set_position_xyz (player, 1.5f, 0.5f, -1.5f);
	lrg_world3d_fixed_update (world, step);
	g_assert_cmpuint (log.entered, ==, 1);
	g_assert_true (log.last == player);
	g_assert_true (lrg_trigger_component_is_inside (trig, player));
	g_assert_cmpuint (lrg_trigger_component_get_n_inside (trig), ==, 1);
	lrg_world3d_fixed_update (world, step);
	g_assert_cmpuint (log.entered, ==, 1);

	/* The box turns and scales with its entity: rotate 45 degrees about Y,
	 * the corner point (1.5, 0, -1.5) is now outside the 2 x 2 half extents */
	{
		gfloat euler[3] = { 0.0f, (gfloat) G_PI / 4.0f, 0.0f };
		gfloat p[3] = { 1.9f, 0.0f, -1.9f };

		g_assert_true (lrg_trigger_component_contains_point (trig, p));
		lrg_entity3d_set_rotation_euler (zone, euler);
		g_assert_false (lrg_trigger_component_contains_point (trig, p));
		euler[1] = 0.0f;
		lrg_entity3d_set_rotation_euler (zone, euler);
	}

	/* Out again: one exit */
	lrg_entity3d_set_position_xyz (player, 0.0f, 5.0f, 0.0f);
	lrg_world3d_fixed_update (world, step);
	g_assert_cmpuint (log.exited, ==, 1);
	g_assert_cmpuint (lrg_trigger_component_get_n_inside (trig), ==, 0);

	/* A sphere filtered by component type */
	lrg_trigger_component_set_filter_tag (trig, NULL);
	lrg_trigger_component_set_filter_component (trig, "LrgAudioListenerComponent");
	lrg_trigger_component_set_shape (trig, LRG_TRIGGER_SHAPE_SPHERE);
	lrg_trigger_component_set_radius (trig, 1.0f);
	attach (crate, lrg_audio_listener_component_new ());
	lrg_world3d_fixed_update (world, step);
	g_assert_cmpuint (log.entered, ==, 2);
	g_assert_true (log.last == crate);

	/* Removing the entity from the world counts as an exit */
	lrg_world3d_remove_entity (world, crate);
	lrg_world3d_fixed_update (world, step);
	g_assert_cmpuint (log.exited, ==, 2);

	/* An unknown type name matches nothing */
	lrg_trigger_component_set_filter_component (trig, "NoSuchType");
	lrg_world3d_fixed_update (world, step);
	g_assert_cmpuint (lrg_trigger_component_get_n_inside (trig), ==, 0);
}

static void
test_trigger_one_shot_unfiltered (void)
{
	g_autoptr(LrgWorld3D) world = lrg_world3d_new ();
	LrgEntity3D          *zone, *ball;
	LrgTriggerComponent  *trig;
	TriggerLog            log = { 0, 0, NULL };
	gfloat                step = lrg_world3d_get_fixed_step (world);

	zone = add_entity (world, "zone", 0.0f, 0.0f, 0.0f);
	trig = attach (zone, lrg_trigger_component_new_sphere (2.0f));
	g_object_set (trig, "one-shot", TRUE, NULL);
	g_signal_connect (trig, "entered", G_CALLBACK (on_entered), &log);
	g_signal_connect (trig, "exited", G_CALLBACK (on_exited), &log);

	/* Unfiltered: any entity but the trigger's own */
	ball = add_entity (world, "ball", 1.0f, 0.0f, 0.0f);
	lrg_world3d_fixed_update (world, step);
	g_assert_cmpuint (log.entered, ==, 1);
	g_assert_true (log.last == ball);

	/* Fired: no more tests until reset */
	lrg_entity3d_set_position_xyz (ball, 50.0f, 0.0f, 0.0f);
	lrg_world3d_fixed_update (world, step);
	g_assert_cmpuint (log.exited, ==, 0);
	lrg_trigger_component_reset (trig);
	lrg_entity3d_set_position_xyz (ball, 0.5f, 0.0f, 0.0f);
	lrg_world3d_fixed_update (world, step);
	g_assert_cmpuint (log.entered, ==, 2);
}

/* ==========================================================================
 * Skinned model
 * ========================================================================== */

static void
test_skinned_model_playback (void)
{
	g_autoptr(LrgWorld3D)       world = lrg_world3d_new ();
	g_autoptr(LrgRenderList)    list = lrg_render_list_new ();
	g_autoptr(LrgModelAnimator) animator = NULL;
	const gchar                *names[] = { "Armature|Idle", "Armature|Walk" };
	const gint                  frames[] = { 60, 120 };
	LrgEntity3D                *e;
	LrgSkinnedModelComponent   *sk;
	const LrgModelAnimState    *state;

	animator = lrg_model_animator_new_headless (names, frames, 2);
	e = add_entity (world, "hero", 0.0f, 0.0f, 0.0f);
	sk = attach (e, lrg_skinned_model_component_new_with_model ("models/hero.glb", "walk"));
	g_assert_cmpstr (lrg_skinned_model_component_get_model_path (sk), ==, "models/hero.glb");
	g_assert_cmpstr (lrg_skinned_model_component_get_clip (sk), ==, "walk");

	/* No animator yet (no asset manager): nothing plays, nothing draws */
	lrg_world3d_update (world, 0.1f);
	g_assert_null (lrg_skinned_model_component_get_current_clip_name (sk));
	lrg_world3d_collect_render (world, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_total_items (list), ==, 0);

	/* A headless animator: the clip resolves by suffix and time advances */
	lrg_skinned_model_component_set_animator (sk, animator);
	lrg_world3d_update (world, 0.5f);
	g_assert_cmpstr (lrg_skinned_model_component_get_current_clip_name (sk), ==, "Armature|Walk");
	state = lrg_skinned_model_component_get_state (sk);
	g_assert_cmpfloat_with_epsilon (state->time, 0.5, 1e-6);

	/* Speed scales time; pausing holds it */
	g_object_set (sk, "speed", 2.0f, NULL);
	lrg_world3d_update (world, 0.25f);
	g_assert_cmpfloat_with_epsilon (state->time, 1.0, 1e-6);
	g_object_set (sk, "playing", FALSE, NULL);
	lrg_world3d_update (world, 0.25f);
	g_assert_cmpfloat_with_epsilon (state->time, 1.0, 1e-6);

	/* play(): switch clips at once, non-looping clips finish */
	g_assert_true (lrg_skinned_model_component_play (sk, "idle", FALSE));
	g_assert_cmpstr (lrg_skinned_model_component_get_current_clip_name (sk), ==, "Armature|Idle");
	g_assert_false (state->loop);
	lrg_world3d_update (world, 5.0f);
	g_assert_true (lrg_skinned_model_component_is_finished (sk));
	g_assert_false (lrg_skinned_model_component_play (sk, "swim", TRUE));

	/* Headless: no model, so still no render item; posing is tracked */
	g_assert_null (lrg_skinned_model_component_get_model (sk));
	lrg_world3d_collect_render (world, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_total_items (list), ==, 0);
}

/* ==========================================================================
 * Component plumbing
 * ========================================================================== */

static void
test_component_types (void)
{
	GType types[10];
	guint i;

	types[0] = LRG_TYPE_MESH_RENDERER_COMPONENT;
	types[1] = LRG_TYPE_LIGHT_COMPONENT;
	types[2] = LRG_TYPE_CAMERA_COMPONENT;
	types[3] = LRG_TYPE_AUDIO_SOURCE_COMPONENT;
	types[4] = LRG_TYPE_AUDIO_LISTENER_COMPONENT;
	types[5] = LRG_TYPE_BILLBOARD_COMPONENT;
	types[6] = LRG_TYPE_TILEMAP_COMPONENT;
	types[7] = LRG_TYPE_SPAWN_COMPONENT;
	types[8] = LRG_TYPE_TRIGGER_COMPONENT;
	types[9] = LRG_TYPE_SKINNED_MODEL_COMPONENT;

	/* Every component is an LrgComponent constructible by type name (what
	 * level instantiation does), and every property is explicit-notify */
	for (i = 0; i < G_N_ELEMENTS (types); i++)
	{
		g_autoptr(GObject) obj = NULL;
		g_autofree GParamSpec **specs = NULL;
		guint                   n = 0, k;

		g_assert_true (g_type_is_a (types[i], LRG_TYPE_COMPONENT));
		g_assert_true (g_type_from_name (g_type_name (types[i])) == types[i]);
		obj = g_object_new (types[i], NULL);
		specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (obj), &n);
		for (k = 0; k < n; k++)
		{
			if (specs[k]->owner_type != types[i] || (specs[k]->flags & G_PARAM_WRITABLE) == 0)
				continue;
			g_assert_true ((specs[k]->flags & G_PARAM_EXPLICIT_NOTIFY) != 0);
		}
	}

	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_BILLBOARD_FACING));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_TRIGGER_SHAPE));
}

/* ==========================================================================
 * GPU: mesh renderer + light through the world renderer
 * ========================================================================== */

static void
test_gpu_mesh_and_light (void)
{
	g_autoptr(LrgWorld3D)         world = NULL;
	g_autoptr(LrgWorld3DRenderer) renderer = NULL;
	LrgEntity3D                  *cube, *lamp, *eye;
	LrgMeshRendererComponent     *mr;
	LrgLightComponent            *light;
	LrgCameraComponent           *cam;
	LrgWorld3DView                view;
	RenderTexture2D               rt;
	Image                         shot;
	Color                         centre, corner;
	gfloat                        euler[3] = { 0.0f, 0.0f, 0.0f };

	if (!graphics_available)
	{
		g_test_skip ("No display available (headless environment)");
		return;
	}

	/* A red cube at the origin, a white point light in front, a camera at +Z */
	world = lrg_world3d_new ();
	cube = add_entity (world, "cube", 0.0f, 0.0f, 0.0f);
	mr = attach (cube, lrg_mesh_renderer_component_new ());
	lrg_mesh_renderer_component_set_size (mr, 2.0f, 2.0f, 2.0f);
	lrg_mesh_renderer_component_set_color (mr, 1.0f, 0.1f, 0.1f, 1.0f);
	lamp = add_entity (world, "lamp", 0.0f, 0.0f, 3.0f);
	light = attach (lamp, lrg_light_component_new ());
	lrg_light_component_set_intensity (light, 2.0f);
	eye = add_entity (world, "eye", 0.0f, 0.0f, 6.0f);
	lrg_entity3d_set_rotation_euler (eye, euler);
	cam = attach (eye, lrg_camera_component_new ());
	lrg_world3d_step (world, 0.016f);
	g_assert_true (lrg_camera_component_is_current (cam));

	renderer = lrg_world3d_renderer_new ();
	lrg_world3d_view_init_from_camera (&view, lrg_camera_component_get_camera (cam), 32.0f, 32.0f);

	rt = LoadRenderTexture (32, 32);
	BeginTextureMode (rt);
	g_assert_true (lrg_world3d_renderer_render_view (renderer, world, &view));
	EndTextureMode ();
	shot = LoadImageFromTexture (rt.texture);
	centre = GetImageColor (shot, 16, 16);
	corner = GetImageColor (shot, 0, 0);
	UnloadImage (shot);
	UnloadRenderTexture (rt);

	/* The cube is drawn, red, and the light was collected */
	g_assert_cmpuint (lrg_world3d_renderer_get_drawn (renderer), >=, 1);
	g_assert_cmpuint (centre.r, >, centre.g + 40);
	g_assert_cmpuint (centre.r, >, centre.b + 40);
	g_assert_false (corner.r == centre.r && corner.g == centre.g && corner.b == centre.b);
	g_assert_cmpuint (lrg_render_list_get_n_lights (lrg_world3d_renderer_get_render_list (renderer)), ==, 1);

	lrg_world3d_renderer_release (renderer);
}

/* A tilemap's custom items draw their swatch quads through the renderer. */
static void
test_gpu_tilemap (void)
{
	g_autoptr(LrgWorld3D)         world = NULL;
	g_autoptr(LrgWorld3DRenderer) renderer = NULL;
	g_autoptr(LrgNodeTilemap)     grid = NULL;
	LrgEntity3D                  *map, *eye;
	LrgCameraComponent           *cam;
	LrgWorld3DView                view;
	RenderTexture2D               rt;
	Image                         shot;
	Color                         centre, bg;
	guint                         x, y;

	if (!graphics_available)
	{
		g_test_skip ("No display available (headless environment)");
		return;
	}

	/* A 2 x 2 grid of tile 1 (the first swatch) centred in front of the
	 * camera: the grid spans x 0..2, y -2..0, so shift it by (-1, 1); the
	 * view is about 3.5 units tall there, so the corners stay background */
	grid = lrg_node_tilemap_new (2, 2);
	for (y = 0; y < 2; y++)
		for (x = 0; x < 2; x++)
			lrg_node_tilemap_set_tile (grid, 0, x, y, 1);
	world = lrg_world3d_new ();
	map = add_entity (world, "map", -1.0f, 1.0f, 0.0f);
	attach (map, lrg_tilemap_component_new_from_node_tilemap (grid, 1.0f));
	eye = add_entity (world, "eye", 0.0f, 0.0f, 3.0f);
	cam = attach (eye, lrg_camera_component_new ());
	lrg_world3d_step (world, 0.016f);

	renderer = lrg_world3d_renderer_new ();
	lrg_world3d_renderer_set_render_mode (renderer, LRG_WORLD3D_RENDER_MODE_UNLIT);
	lrg_world3d_view_init_from_camera (&view, lrg_camera_component_get_camera (cam), 32.0f, 32.0f);

	rt = LoadRenderTexture (32, 32);
	BeginTextureMode (rt);
	lrg_world3d_renderer_render_view (renderer, world, &view);
	EndTextureMode ();
	shot = LoadImageFromTexture (rt.texture);
	centre = GetImageColor (shot, 16, 16);
	bg = GetImageColor (shot, 0, 0);
	UnloadImage (shot);
	UnloadRenderTexture (rt);

	/* The swatch is pink (0xf38ba8): red dominates; the corner is not it */
	g_assert_cmpuint (centre.r, >, 150);
	g_assert_cmpuint (centre.r, >, centre.g);
	g_assert_false (bg.r == centre.r && bg.g == centre.g && bg.b == centre.b);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (renderer, LRG_WORLD3D_RENDER_PASS_CUTOUT), ==, 1);

	lrg_world3d_renderer_release (renderer);
}

int
main (int   argc,
      char *argv[])
{
	int result;

	g_test_init (&argc, &argv, NULL);

	graphics_available = init_graphics_context ();

	g_test_add_func ("/world3d-components/types", test_component_types);
	g_test_add_func ("/world3d-components/mesh-renderer/properties", test_mesh_renderer_properties);
	g_test_add_func ("/world3d-components/mesh-renderer/items", test_mesh_renderer_items);
	g_test_add_func ("/world3d-components/mesh-renderer/bounds", test_mesh_renderer_bounds);
	g_test_add_func ("/world3d-components/light/registration", test_light_registration);
	g_test_add_func ("/world3d-components/light/render-list", test_light_in_render_list);
	g_test_add_func ("/world3d-components/camera/properties", test_camera_properties);
	g_test_add_func ("/world3d-components/camera/selection", test_camera_selection);
	g_test_add_func ("/world3d-components/audio/listener", test_audio_listener_follows);
	g_test_add_func ("/world3d-components/audio/source-headless", test_audio_source_headless);
	g_test_add_func ("/world3d-components/billboard/items", test_billboard_items);
	g_test_add_func ("/world3d-components/tilemap/items", test_tilemap_items);
	g_test_add_func ("/world3d-components/spawn/find", test_spawn_points);
	g_test_add_func ("/world3d-components/trigger/enter-exit", test_trigger_enter_exit);
	g_test_add_func ("/world3d-components/trigger/one-shot", test_trigger_one_shot_unfiltered);
	g_test_add_func ("/world3d-components/skinned-model/playback", test_skinned_model_playback);
	g_test_add_func ("/world3d-components/gpu/mesh-and-light", test_gpu_mesh_and_light);
	g_test_add_func ("/world3d-components/gpu/tilemap", test_gpu_tilemap);

	result = g_test_run ();

	g_clear_object (&test_window);
	return result;
}
