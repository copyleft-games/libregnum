/* test-world3d-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgWorld3DRenderer, the shared 3D renderer.
 *
 * Headless (every case runs with the "headless" property, so they also
 * pass on a machine with a display): views and matrices, light sync,
 * pass ordering (opaque front to back, cutout, transparent back to
 * front), shadow-caster replay per shadow pass and the caster mask, the
 * view's cull mask, render modes and flags, a world rendered through its
 * camera, static batching and the asset resolution.
 *
 * GPU (only under the isolated X display, tests/xrun.sh style: DISPLAY
 * set and no WAYLAND_DISPLAY): a world with a ground plane, a cube and a
 * directional light renders lit pixels, the shadow darkens the ground
 * beside the cube, and the sky colours the top of the image.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <raylib.h>
#include <math.h>
#include <string.h>

#define GPU_W (160)
#define GPU_H (120)

static gboolean gpu_ready = FALSE;

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* A headless renderer (records instead of drawing). */
static LrgWorld3DRenderer *
new_headless (void)
{
	LrgWorld3DRenderer *r = lrg_world3d_renderer_new ();

	lrg_world3d_renderer_set_headless (r, TRUE);
	return r;
}

/* A render item for @entity at @x,@y,@z on @layer with @flags. */
static void
add_item (LrgRenderList      *list,
          LrgEntity3D        *entity,
          gfloat              x,
          gfloat              y,
          gfloat              z,
          guint               layer,
          LrgRenderItemFlags  flags)
{
	LrgRenderItem item;

	lrg_render_item_init (&item);
	item.kind = LRG_RENDER_ITEM_PRIMITIVE;
	item.primitive = LRG_PRIMITIVE_CUBE;
	item.flags = flags;
	item.layer = layer;
	item.world_matrix[12] = x;
	item.world_matrix[13] = y;
	item.world_matrix[14] = z;
	item.entity = entity;
	lrg_render_list_add (list, &item);
}

/* A directional light shining down and slightly sideways. */
static void
sun_desc (LrgLight3DDesc *out)
{
	lrg_light3d_desc_init (out);
	out->kind = LRG_LIGHT3D_DIRECTIONAL;
	out->direction[0] = -0.4f;
	out->direction[1] = -1.0f;
	out->direction[2] = -0.25f;
	out->cast_shadows = TRUE;
}

/* ==========================================================================
 * Views
 * ========================================================================== */

static void
test_view (void)
{
	LrgWorld3DView           view;
	g_autoptr(LrgWorld3DView) copy = NULL;
	g_autoptr(LrgCamera3D)   cam = lrg_camera3d_new ();
	gfloat                   m[16], expect[16];
	gint                     i;

	lrg_world3d_view_init (&view, 320.0f, 200.0f);
	g_assert_cmpfloat (view.position[0], ==, 8.0f);
	g_assert_cmpfloat (view.fovy, ==, 45.0f);
	g_assert_cmpfloat (view.width, ==, 320.0f);
	g_assert_cmpuint (view.cull_mask, ==, LRG_RENDER_LAYERS_MASK_ALL);

	/* The projection is the core helper's, for the view's aspect */
	lrg_world3d_view_get_projection_matrix (&view, m);
	lrg_math3d_perspective (45.0f * (gfloat) G_PI / 180.0f, 320.0f / 200.0f, 0.05f, 2000.0f, expect);
	for (i = 0; i < 16; i++)
		g_assert_cmpfloat_with_epsilon (m[i], expect[i], 1e-6);

	copy = lrg_world3d_view_copy (&view);
	g_assert_cmpfloat (copy->height, ==, 200.0f);

	/* From a camera: pose, fov and cull mask */
	lrg_camera3d_set_position_xyz (cam, 1.0f, 2.0f, 3.0f);
	lrg_camera3d_set_target_xyz (cam, 0.0f, 0.5f, 0.0f);
	lrg_camera3d_set_fovy (cam, 60.0f);
	lrg_camera_set_cull_mask (LRG_CAMERA (cam), 0x5u);
	lrg_world3d_view_init_from_camera (&view, cam, 100.0f, 50.0f);
	g_assert_cmpfloat (view.position[1], ==, 2.0f);
	g_assert_cmpfloat (view.target[1], ==, 0.5f);
	g_assert_cmpfloat (view.fovy, ==, 60.0f);
	g_assert_false (view.orthographic);
	g_assert_cmpuint (view.cull_mask, ==, 0x5u);
	g_assert_cmpfloat (view.near_plane, >, 0.0f);
	g_assert_cmpfloat (view.far_plane, >, view.near_plane);
}

/* ==========================================================================
 * Lights
 * ========================================================================== */

static void
test_lights (void)
{
	g_autoptr(LrgWorld3DRenderer) r = new_headless ();
	LrgLight3DDesc                lights[5];
	gfloat                        dir[3];

	/* No lights: the fallback sun only */
	g_assert_cmpuint (lrg_world3d_renderer_sync_lights (r, NULL, 0), ==, 0);
	g_assert_false (lrg_world3d_renderer_has_scene_sun (r));
	g_assert_cmpuint (lrg_light_set_get_point_light_count (lrg_world3d_renderer_get_light_set (r)), ==, 0);

	/* A sun, a second sun (ignored), a point, a spot (as a point) and a
	 * disabled point */
	sun_desc (&lights[0]);
	sun_desc (&lights[1]);
	lights[1].direction[0] = 1.0f;
	lrg_light3d_desc_init (&lights[2]);
	lights[2].position[1] = 2.0f;
	lrg_light3d_desc_init (&lights[3]);
	lights[3].kind = LRG_LIGHT3D_SPOT;
	lrg_light3d_desc_init (&lights[4]);
	lights[4].enabled = FALSE;
	g_assert_cmpuint (lrg_world3d_renderer_sync_lights (r, lights, 5), ==, 3);
	g_assert_true (lrg_world3d_renderer_has_scene_sun (r));
	g_assert_cmpuint (lrg_light_set_get_point_light_count (lrg_world3d_renderer_get_light_set (r)), ==, 2);

	/* The sun direction is the first sun's, normalised */
	lrg_world3d_renderer_get_sun_direction (r, dir);
	g_assert_cmpfloat_with_epsilon (sqrtf (dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]), 1.0f, 1e-5);
	g_assert_cmpfloat (dir[0], <, 0.0f);
	g_assert_cmpfloat (dir[1], <, 0.0f);

	/* Fallback sun off: still no scene sun, nothing added */
	lrg_world3d_renderer_set_fallback_sun (r, FALSE);
	g_assert_false (lrg_world3d_renderer_get_fallback_sun (r));
	g_assert_cmpuint (lrg_world3d_renderer_sync_lights (r, &lights[2], 1), ==, 1);
	g_assert_false (lrg_world3d_renderer_has_scene_sun (r));
}

/* ==========================================================================
 * Pass ordering
 * ========================================================================== */

static void
test_pass_order (void)
{
	g_autoptr(LrgWorld3DRenderer) r = new_headless ();
	g_autoptr(LrgRenderList)      list = lrg_render_list_new ();
	g_autoptr(LrgEntity3D)        near_opaque = lrg_entity3d_new_with_name ("near-opaque");
	g_autoptr(LrgEntity3D)        far_opaque = lrg_entity3d_new_with_name ("far-opaque");
	g_autoptr(LrgEntity3D)        cutout = lrg_entity3d_new_with_name ("cutout");
	g_autoptr(LrgEntity3D)        near_glass = lrg_entity3d_new_with_name ("near-glass");
	g_autoptr(LrgEntity3D)        far_glass = lrg_entity3d_new_with_name ("far-glass");
	LrgWorld3DView                view;
	const gfloat                  eye[3] = { 0.0f, 0.0f, 0.0f };

	/* Added in a scrambled order: the sort decides */
	add_item (list, far_glass, 0.0f, 0.0f, -20.0f, 0, LRG_RENDER_ITEM_FLAG_TRANSPARENT);
	add_item (list, far_opaque, 0.0f, 0.0f, -15.0f, 0, LRG_RENDER_ITEM_FLAG_NONE);
	add_item (list, cutout, 0.0f, 0.0f, -8.0f, 0, LRG_RENDER_ITEM_FLAG_CUTOUT);
	add_item (list, near_glass, 0.0f, 0.0f, -3.0f, 0, LRG_RENDER_ITEM_FLAG_TRANSPARENT);
	add_item (list, near_opaque, 0.0f, 0.0f, -5.0f, 0, LRG_RENDER_ITEM_FLAG_NONE);
	lrg_render_list_set_view (list, eye, LRG_RENDER_LAYERS_MASK_ALL, NULL);
	lrg_render_list_sort (list);

	lrg_world3d_view_init (&view, 64.0f, 64.0f);
	g_assert_false (lrg_world3d_renderer_render_list (r, list, &view));

	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), ==, 2);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_CUTOUT), ==, 1);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_TRANSPARENT), ==, 2);

	/* Opaque front to back, transparent back to front */
	g_assert_true (lrg_world3d_renderer_get_recorded_entity (r, LRG_WORLD3D_RENDER_PASS_OPAQUE, 0) == near_opaque);
	g_assert_true (lrg_world3d_renderer_get_recorded_entity (r, LRG_WORLD3D_RENDER_PASS_OPAQUE, 1) == far_opaque);
	g_assert_true (lrg_world3d_renderer_get_recorded_entity (r, LRG_WORLD3D_RENDER_PASS_CUTOUT, 0) == cutout);
	g_assert_true (lrg_world3d_renderer_get_recorded_entity (r, LRG_WORLD3D_RENDER_PASS_TRANSPARENT, 0) == far_glass);
	g_assert_true (lrg_world3d_renderer_get_recorded_entity (r, LRG_WORLD3D_RENDER_PASS_TRANSPARENT, 1) ==
	               near_glass);
	g_assert_null (lrg_world3d_renderer_get_recorded_entity (r, LRG_WORLD3D_RENDER_PASS_TRANSPARENT, 2));

	/* The sky ran (perspective, default flags); no post without a processor */
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SKY), ==, 1);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_POST), ==, 0);
	g_assert_cmpuint (lrg_world3d_renderer_get_drawn (r), >=, 5);
}

/* ==========================================================================
 * Shadows
 * ========================================================================== */

static void
test_shadow_replay (void)
{
	g_autoptr(LrgWorld3DRenderer) r = new_headless ();
	g_autoptr(LrgRenderList)      list = lrg_render_list_new ();
	g_autoptr(LrgEntity3D)        a = lrg_entity3d_new ();
	g_autoptr(LrgEntity3D)        b = lrg_entity3d_new ();
	g_autoptr(LrgEntity3D)        c = lrg_entity3d_new ();
	LrgShadowRenderer            *shadows;
	LrgWorld3DView                view;
	LrgLight3DDesc                sun;
	guint                         cascades;
	const gfloat                  eye[3] = { 8.0f, 6.0f, 8.0f };

	/* Two casters (layers 0 and 3) and one item that casts nothing */
	add_item (list, a, 0.0f, 0.5f, 0.0f, 0, LRG_RENDER_ITEM_FLAG_CAST_SHADOW | LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW);
	add_item (list, b, 2.0f, 0.5f, 0.0f, 3, LRG_RENDER_ITEM_FLAG_CAST_SHADOW);
	add_item (list, c, -2.0f, 0.5f, 0.0f, 0, LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW);
	sun_desc (&sun);
	lrg_render_list_add_light (list, &sun);
	lrg_render_list_set_view (list, eye, LRG_RENDER_LAYERS_MASK_ALL, NULL);
	lrg_render_list_sort (list);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_SHADOW_CASTER), ==, 2);

	lrg_world3d_view_init (&view, 128.0f, 96.0f);
	lrg_world3d_renderer_render_list (r, list, &view);
	shadows = lrg_world3d_renderer_get_shadow_renderer (r);
	cascades = lrg_shadow_renderer_get_cascade_count (shadows);
	g_assert_cmpuint (cascades, >, 0);

	/* Every caster once per shadow pass */
	g_assert_cmpuint (lrg_world3d_renderer_get_shadow_pass_count (r), ==, lrg_shadow_renderer_get_pass_count (shadows));
	g_assert_cmpuint (lrg_world3d_renderer_get_shadow_pass_count (r), >=, cascades);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SHADOW), ==,
	                  2 * lrg_world3d_renderer_get_shadow_pass_count (r));
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), ==, 3);

	/* The caster mask leaves layer 3 out of the replay */
	lrg_shadow_renderer_set_caster_mask (shadows, LRG_RENDER_LAYERS_MASK_ALL & ~LRG_RENDER_LAYERS_BIT (3));
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SHADOW), ==,
	                  lrg_world3d_renderer_get_shadow_pass_count (r));
	g_assert_true (lrg_world3d_renderer_get_recorded_entity (r, LRG_WORLD3D_RENDER_PASS_SHADOW, 0) == a);
	lrg_shadow_renderer_set_caster_mask (shadows, LRG_RENDER_LAYERS_MASK_ALL);

	/* No shadows: flag off, unlit modes, orthographic views */
	lrg_world3d_renderer_set_flags (r, LRG_WORLD3D_RENDER_FLAG_CLEAR);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_shadow_pass_count (r), ==, 0);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SKY), ==, 0);

	lrg_world3d_renderer_set_flags (r, LRG_WORLD3D_RENDER_FLAGS_DEFAULT);
	lrg_world3d_renderer_set_render_mode (r, LRG_WORLD3D_RENDER_MODE_UNLIT);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_shadow_pass_count (r), ==, 0);

	lrg_world3d_renderer_set_render_mode (r, LRG_WORLD3D_RENDER_MODE_LIGHTING_ONLY);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_shadow_pass_count (r), >, 0);

	view.orthographic = TRUE;
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_shadow_pass_count (r), ==, 0);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SKY), ==, 0);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), ==, 3);
}

/* ==========================================================================
 * Cull mask and render settings
 * ========================================================================== */

static void
test_cull_mask (void)
{
	g_autoptr(LrgWorld3DRenderer) r = new_headless ();
	g_autoptr(LrgRenderList)      list = lrg_render_list_new ();
	g_autoptr(LrgEntity3D)        visible = lrg_entity3d_new ();
	g_autoptr(LrgEntity3D)        editor_only = lrg_entity3d_new ();
	LrgWorld3DView                view;
	const gfloat                  eye[3] = { 0.0f, 0.0f, 5.0f };

	add_item (list, visible, 0.0f, 0.0f, 0.0f, LRG_RENDER_LAYERS_DEFAULT, LRG_RENDER_ITEM_FLAG_CAST_SHADOW);
	add_item (list, editor_only, 1.0f, 0.0f, 0.0f, LRG_RENDER_LAYERS_EDITOR_ONLY, LRG_RENDER_ITEM_FLAG_CAST_SHADOW);
	lrg_render_list_set_view (list, eye, LRG_RENDER_LAYERS_MASK_ALL, NULL);
	lrg_render_list_sort (list);

	/* Every layer: both */
	lrg_world3d_view_init (&view, 64.0f, 64.0f);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), ==, 2);

	/* A game camera leaves the editor-only layer out, shadows included */
	view.cull_mask = LRG_RENDER_LAYERS_MASK_ALL & ~LRG_RENDER_LAYERS_BIT (LRG_RENDER_LAYERS_EDITOR_ONLY);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), ==, 1);
	g_assert_true (lrg_world3d_renderer_get_recorded_entity (r, LRG_WORLD3D_RENDER_PASS_OPAQUE, 0) == visible);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SHADOW), ==,
	                  lrg_world3d_renderer_get_shadow_pass_count (r));

	/* No layer at all: nothing */
	view.cull_mask = LRG_RENDER_LAYERS_MASK_NONE;
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), ==, 0);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SHADOW), ==, 0);
}

static void
test_settings (void)
{
	g_autoptr(LrgWorld3DRenderer) r = lrg_world3d_renderer_new ();
	g_autoptr(LrgPostProcessor)   post = lrg_world3d_renderer_new_preview_post_processor ();
	g_autoptr(LrgDebugDraw)       dd = lrg_debug_draw_new ();
	GrlColor                      bg = { 10, 20, 30, 255 };
	GrlColor                      out;
	LrgWorld3DRenderMode          mode;
	guint                         flags;

	/* Defaults */
	g_assert_cmpint (lrg_world3d_renderer_get_render_mode (r), ==, LRG_WORLD3D_RENDER_MODE_LIT);
	g_assert_cmpuint (lrg_world3d_renderer_get_flags (r), ==, LRG_WORLD3D_RENDER_FLAGS_DEFAULT);
	g_assert_false (lrg_world3d_renderer_get_headless (r));
	g_assert_true (lrg_world3d_renderer_get_fallback_sun (r));
	g_assert_null (lrg_world3d_renderer_get_post_processor (r));
	g_assert_nonnull (lrg_world3d_renderer_get_render_list (r));
	g_assert_nonnull (lrg_world3d_renderer_get_sky_dome (r));

	/* Properties and setters agree */
	g_object_set (r, "render-mode", LRG_WORLD3D_RENDER_MODE_NORMALS, "flags",
	              LRG_WORLD3D_RENDER_FLAG_SKY | LRG_WORLD3D_RENDER_FLAG_POST, NULL);
	g_object_get (r, "render-mode", &mode, "flags", &flags, NULL);
	g_assert_cmpint (mode, ==, LRG_WORLD3D_RENDER_MODE_NORMALS);
	g_assert_cmpuint (flags, ==, LRG_WORLD3D_RENDER_FLAG_SKY | LRG_WORLD3D_RENDER_FLAG_POST);
	lrg_world3d_renderer_set_post_processor (r, post);
	g_assert_true (lrg_world3d_renderer_get_post_processor (r) == post);
	lrg_world3d_renderer_set_debug_draw (r, dd);
	g_assert_true (lrg_world3d_renderer_get_debug_draw (r) == dd);
	lrg_world3d_renderer_set_background (r, &bg);
	lrg_world3d_renderer_get_background (r, &out);
	g_assert_cmpuint (out.g, ==, 20);

	/* The preview processor holds the four effects the renderer reads */
	g_assert_nonnull (lrg_post_processor_get_effect (post, "fxaa"));
	g_assert_nonnull (lrg_post_processor_get_effect (post, "bloom"));
	g_assert_nonnull (lrg_post_processor_get_effect (post, "vignette"));
	g_assert_nonnull (lrg_post_processor_get_effect (post, "color-grade"));

	/* Enum and flags types are registered with their nicks */
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_WORLD3D_RENDER_MODE));
	g_assert_true (G_TYPE_IS_FLAGS (LRG_TYPE_WORLD3D_RENDER_FLAGS));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_WORLD3D_RENDER_PASS));
	g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_WORLD3D_VIEW));

	/* Shader sources: the five built-ins */
	g_assert_nonnull (lrg_world3d_renderer_get_shader_source ("scene.vs"));
	g_assert_nonnull (lrg_world3d_renderer_get_shader_source ("post.fs"));
	g_assert_null (lrg_world3d_renderer_get_shader_source ("nope.fs"));

	/* Release without a window is harmless (and twice) */
	lrg_world3d_renderer_release (r);
	lrg_world3d_renderer_release (r);
	g_assert_nonnull (lrg_world3d_renderer_get_light_set (r));
}

/* ==========================================================================
 * A world through its camera
 * ========================================================================== */

/* An entity with a mesh renderer drawing @prim, added to @world. */
static LrgEntity3D *
add_shape (LrgWorld3D       *world,
           const gchar      *name,
           LrgPrimitiveType  prim,
           gfloat            x,
           gfloat            y,
           gfloat            z)
{
	LrgEntity3D                        *e = lrg_entity3d_new_with_name (name);
	g_autoptr(LrgMeshRendererComponent) mr = lrg_mesh_renderer_component_new_primitive (prim);

	lrg_entity3d_set_position_xyz (e, x, y, z);
	lrg_entity3d_add_component (e, LRG_COMPONENT (mr));
	lrg_world3d_add_entity (world, e);
	g_object_unref (e);
	return e;
}

/* A sun entity with a light component, synced into the registry. */
static void
add_sun (LrgWorld3D *world)
{
	LrgEntity3D                 *e = lrg_entity3d_new_with_name ("Sun");
	g_autoptr(LrgLightComponent) light = lrg_light_component_new_with_kind (LRG_LIGHT3D_DIRECTIONAL);
	gfloat                       euler[3] = { -1.1f, 0.5f, 0.0f };

	lrg_entity3d_set_position_xyz (e, 0.0f, 6.0f, 0.0f);
	lrg_entity3d_set_rotation_euler (e, euler);
	lrg_light_component_set_cast_shadows (light, TRUE);
	lrg_entity3d_add_component (e, LRG_COMPONENT (light));
	lrg_world3d_add_entity (world, e);
	g_object_unref (e);
	lrg_light_component_sync (light);
}

static LrgCamera3D *
new_camera (void)
{
	LrgCamera3D *cam = lrg_camera3d_new ();

	lrg_camera3d_set_position_xyz (cam, 5.0f, 5.0f, 6.0f);
	lrg_camera3d_set_target_xyz (cam, 0.0f, 0.0f, 0.0f);
	lrg_camera3d_set_fovy (cam, 45.0f);
	return cam;
}

static void
test_world (void)
{
	g_autoptr(LrgWorld3DRenderer) r = new_headless ();
	g_autoptr(LrgWorld3D)         world = lrg_world3d_new ();
	g_autoptr(LrgCamera3D)        cam = new_camera ();
	LrgEntity3D                  *cube;
	LrgEntity3D                  *ball;
	LrgWorld3DView                view;

	add_shape (world, "Ground", LRG_PRIMITIVE_PLANE, 0.0f, 0.0f, 0.0f);
	cube = add_shape (world, "Cube", LRG_PRIMITIVE_CUBE, 0.0f, 1.0f, 0.0f);
	ball = add_shape (world, "Ball", LRG_PRIMITIVE_UV_SPHERE, 2.0f, 0.5f, 0.0f);
	add_sun (world);
	g_assert_nonnull (cube);
	g_assert_cmpuint (lrg_world3d_get_n_lights (world), ==, 1);

	/* No camera given and none registered: nothing */
	g_assert_false (lrg_world3d_renderer_render (r, world, NULL, NULL, 64, 64));
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), ==, 0);

	/* Through the camera: three shapes, the sun synced from the world */
	lrg_world3d_renderer_render (r, world, cam, NULL, 128, 96);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), ==, 3);
	g_assert_true (lrg_world3d_renderer_has_scene_sun (r));
	g_assert_cmpuint (lrg_world3d_renderer_get_shadow_pass_count (r), >, 0);

	/* The world's active camera is used when none is given */
	lrg_world3d_set_active_camera (world, LRG_CAMERA (cam));
	lrg_world3d_renderer_render (r, world, NULL, NULL, 128, 96);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), ==, 3);

	/* render_view: no frustum culling, the view's cull mask instead */
	lrg_entity3d_set_layer (ball, 7);
	lrg_world3d_view_init (&view, 128.0f, 96.0f);
	view.cull_mask = LRG_RENDER_LAYERS_MASK_ALL & ~LRG_RENDER_LAYERS_BIT (7);
	lrg_world3d_renderer_render_view (r, world, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), ==, 2);

	/* Static batching takes the static entities out of the opaque replay */
	lrg_entity3d_set_static (cube, TRUE);
	lrg_world3d_renderer_set_flags (r, LRG_WORLD3D_RENDER_FLAGS_DEFAULT | LRG_WORLD3D_RENDER_FLAG_BATCH_STATIC);
	lrg_world3d_renderer_render (r, world, cam, NULL, 128, 96);
	g_assert_cmpuint (lrg_world3d_renderer_get_batched_count (r), ==, 1);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), ==, 2);

	/* ...in the lit mode only */
	lrg_world3d_renderer_set_render_mode (r, LRG_WORLD3D_RENDER_MODE_UNLIT);
	lrg_world3d_renderer_render (r, world, cam, NULL, 128, 96);
	g_assert_cmpuint (lrg_world3d_renderer_get_batched_count (r), ==, 0);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), ==, 3);
}

/* ==========================================================================
 * Assets
 * ========================================================================== */

/* Resolves "guid:<name>" to <dir>/<name>.png. */
static gchar *
guid_resolver (const gchar *asset,
               gpointer     user_data)
{
	if (!g_str_has_prefix (asset, "guid:"))
		return NULL;
	return g_strdup_printf ("%s/%s.png", (const gchar *) user_data, asset + 5);
}

static void
test_assets (void)
{
	g_autoptr(LrgWorld3DRenderer) r = new_headless ();
	g_autofree gchar             *dir = g_dir_make_tmp ("lrg-w3dr-XXXXXX", NULL);
	g_autofree gchar             *file = NULL;
	g_autofree gchar             *resolved = NULL;
	guint                         gen;

	g_assert_nonnull (dir);
	file = g_build_filename (dir, "brick.png", NULL);
	g_assert_true (g_file_set_contents (file, "not really a png", -1, NULL));

	/* Relative paths against the base directory */
	gen = lrg_world3d_renderer_get_generation (r);
	lrg_world3d_renderer_set_base_dir (r, dir);
	g_assert_cmpstr (lrg_world3d_renderer_get_base_dir (r), ==, dir);
	g_assert_cmpuint (lrg_world3d_renderer_get_generation (r), >, gen);
	resolved = lrg_world3d_renderer_resolve_asset (r, "brick.png");
	g_assert_cmpstr (resolved, ==, file);
	g_assert_false (lrg_world3d_renderer_is_asset_missing (r, "brick.png"));
	g_assert_true (lrg_world3d_renderer_is_asset_missing (r, "nope.png"));
	g_assert_true (lrg_world3d_renderer_is_asset_missing (r, ""));

	/* A resolver for GUID strings, default resolution otherwise */
	lrg_world3d_renderer_set_asset_resolver (r, guid_resolver, g_strdup (dir), g_free);
	g_clear_pointer (&resolved, g_free);
	resolved = lrg_world3d_renderer_resolve_asset (r, "guid:brick");
	g_assert_cmpstr (resolved, ==, file);
	g_assert_false (lrg_world3d_renderer_is_asset_missing (r, "guid:brick"));
	g_assert_true (lrg_world3d_renderer_is_asset_missing (r, "guid:stone"));
	g_assert_false (lrg_world3d_renderer_is_asset_missing (r, "brick.png"));

	/* Without a window nothing loads and nothing is cached */
	if (!gpu_ready)
	{
		g_assert_null (lrg_world3d_renderer_get_texture (r, "brick.png"));
		g_assert_null (lrg_world3d_renderer_get_model (r, "brick.png"));
		g_assert_false (lrg_world3d_renderer_forget_asset (r, file));
	}

	g_unlink (file);
	g_rmdir (dir);
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

static gboolean
gpu_init (void)
{
	const gchar *display = g_getenv ("DISPLAY");

	/* Never on a live Wayland session: xrun.sh unsets WAYLAND_DISPLAY */
	if (display == NULL || *display == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
		return FALSE;
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (64, 64, "test-world3d-renderer");
	return IsWindowReady ();
}

/* Renders @world through @cam with @flags into a fresh RGBA image. */
static Image
gpu_render (LrgWorld3DRenderer    *r,
            LrgWorld3D            *world,
            LrgCamera3D           *cam,
            LrgWorld3DRenderFlags  flags)
{
	g_autoptr(GrlRenderTexture) target = grl_render_texture_new (GPU_W, GPU_H);
	g_autoptr(GrlTexture)       tex = NULL;
	g_autoptr(GrlImage)         grl = NULL;
	const guint8               *pixels;
	gsize                       size = 0;
	Image                       img;

	lrg_world3d_renderer_set_flags (r, flags);
	g_assert_true (lrg_world3d_renderer_render (r, world, cam, target, 0, 0));

	/* Read back through graylib, into a raylib image (render textures
	 * come back upside down) */
	tex = grl_render_texture_get_texture (target);   /* transfer full: a wrapper */
	grl = grl_texture_to_image (tex);
	pixels = grl_image_get_pixels (grl, &size);
	g_assert_cmpuint (size, ==, (gsize) GPU_W * GPU_H * 4);
	memset (&img, 0, sizeof img);
	img.data = MemAlloc ((guint) size);
	memcpy (img.data, pixels, size);
	img.width = GPU_W;
	img.height = GPU_H;
	img.mipmaps = 1;
	img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	ImageFlipVertical (&img);
	return img;
}

/* Mean luminance of the 5x5 block around (@x, @y). */
static gfloat
luma_at (Image img,
         gint  x,
         gint  y)
{
	const guint8 *p = img.data;
	gfloat        sum = 0.0f;
	gint          n = 0, i, j;

	for (j = y - 2; j <= y + 2; j++)
		for (i = x - 2; i <= x + 2; i++)
		{
			const guint8 *px;

			if (i < 0 || j < 0 || i >= img.width || j >= img.height)
				continue;
			px = p + ((gsize) j * (gsize) img.width + (gsize) i) * 4;
			sum += 0.299f * px[0] + 0.587f * px[1] + 0.114f * px[2];
			n++;
		}
	return n > 0 ? sum / (gfloat) n : 0.0f;
}

/* Pixels differing from the background colour. */
static guint
non_background (Image          img,
                const GrlColor *bg)
{
	const guint8 *p = img.data;
	guint         n = 0;
	gint          i;

	for (i = 0; i < img.width * img.height; i++)
		if (ABS ((gint) p[i * 4] - (gint) bg->r) + ABS ((gint) p[i * 4 + 1] - (gint) bg->g) +
		    ABS ((gint) p[i * 4 + 2] - (gint) bg->b) > 24)
			n++;
	return n;
}

/* Projects a world point to image pixels through @cam's view. */
static void
project (LrgCamera3D  *cam,
         const gfloat *p,
         gint         *out_x,
         gint         *out_y)
{
	LrgWorld3DView view;
	gfloat         v[16], pr[16], vp[16], clip[4];
	gint           k;

	lrg_world3d_view_init_from_camera (&view, cam, GPU_W, GPU_H);
	lrg_world3d_view_get_view_matrix (&view, v);
	lrg_world3d_view_get_projection_matrix (&view, pr);
	lrg_math3d_multiply (pr, v, vp);
	for (k = 0; k < 4; k++)
		clip[k] = vp[k] * p[0] + vp[4 + k] * p[1] + vp[8 + k] * p[2] + vp[12 + k];
	*out_x = (gint) ((clip[0] / clip[3] * 0.5f + 0.5f) * GPU_W);
	*out_y = (gint) ((0.5f - clip[1] / clip[3] * 0.5f) * GPU_H);
}

static void
test_gpu (void)
{
	g_autoptr(LrgWorld3DRenderer) r = NULL;
	g_autoptr(LrgWorld3D)         world = NULL;
	g_autoptr(LrgCamera3D)        cam = NULL;
	g_autoptr(GObject)            sun_src = NULL;
	LrgLight3DDesc                light;
	LrgEntity3D                  *ground;
	GrlColor                      bg;
	Image                         plain, shadowed, sky;
	gfloat                        sun[3], shadow_pt[3], lit_pt[3];
	const gfloat                  cube_center[3] = { 0.0f, 1.0f, 0.0f };
	gint                          sx, sy, lx, ly;
	gfloat                        t;

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}

	r = lrg_world3d_renderer_new ();
	world = lrg_world3d_new ();
	cam = new_camera ();
	ground = add_shape (world, "Ground", LRG_PRIMITIVE_PLANE, 0.0f, 0.0f, 0.0f);
	lrg_entity3d_set_scale_xyz (ground, 12.0f, 1.0f, 12.0f);
	add_shape (world, "Cube", LRG_PRIMITIVE_CUBE, cube_center[0], cube_center[1], cube_center[2]);

	/* A low sun from behind the cube, so its shadow falls on the ground
	 * between the cube and the camera (registered directly, as any light
	 * source may) */
	sun_src = g_object_new (G_TYPE_OBJECT, NULL);
	lrg_light3d_desc_init (&light);
	light.kind = LRG_LIGHT3D_DIRECTIONAL;
	light.direction[0] = 0.55f;
	light.direction[1] = -0.6f;
	light.direction[2] = 0.58f;
	light.cast_shadows = TRUE;
	lrg_world3d_register_light (world, sun_src, &light);
	lrg_world3d_renderer_get_background (r, &bg);

	/* Lit pixels: the scene is far from an empty clear */
	plain = gpu_render (r, world, cam, LRG_WORLD3D_RENDER_FLAG_CLEAR);
	g_assert_cmpuint (non_background (plain, &bg), >, (guint) (GPU_W * GPU_H / 4));
	g_assert_true (lrg_world3d_renderer_has_scene_sun (r));
	g_assert_cmpuint (lrg_world3d_renderer_get_drawn (r), >=, 2);

	/* The shadow: the cube centre pushed along the sun to the ground */
	lrg_world3d_renderer_get_sun_direction (r, sun);
	g_assert_cmpfloat (sun[1], <, -0.1f);
	t = -cube_center[1] / sun[1];
	shadow_pt[0] = cube_center[0] + sun[0] * t;
	shadow_pt[1] = 0.0f;
	shadow_pt[2] = cube_center[2] + sun[2] * t;
	/* ...and a ground point well away from the cube, in the light */
	lit_pt[0] = 2.5f;
	lit_pt[1] = 0.0f;
	lit_pt[2] = -2.0f;
	project (cam, shadow_pt, &sx, &sy);
	project (cam, lit_pt, &lx, &ly);
	g_assert_cmpint (sx, >=, 0);
	g_assert_cmpint (sx, <, GPU_W);
	g_assert_cmpint (sy, >=, 0);
	g_assert_cmpint (sy, <, GPU_H);

	shadowed = gpu_render (r, world, cam, LRG_WORLD3D_RENDER_FLAG_CLEAR | LRG_WORLD3D_RENDER_FLAG_SHADOWS);
	g_assert_cmpuint (lrg_world3d_renderer_get_shadow_pass_count (r), >, 0);
	g_test_message ("shadow point (%d,%d): %.1f -> %.1f; lit point (%d,%d): %.1f -> %.1f", sx, sy,
	                luma_at (plain, sx, sy), luma_at (shadowed, sx, sy), lx, ly, luma_at (plain, lx, ly),
	                luma_at (shadowed, lx, ly));
	g_assert_cmpfloat (luma_at (shadowed, sx, sy), <, luma_at (plain, sx, sy) * 0.75f);
	g_assert_cmpfloat (luma_at (shadowed, lx, ly), >, luma_at (plain, lx, ly) * 0.9f);

	/* The sky fills the top of the image above the horizon */
	sky = gpu_render (r, world, cam, LRG_WORLD3D_RENDER_FLAG_CLEAR | LRG_WORLD3D_RENDER_FLAG_SKY);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SKY), ==, 1);
	{
		const guint8 *top = (const guint8 *) sky.data + ((gsize) 2 * GPU_W + GPU_W / 2) * 4;
		const guint8 *was = (const guint8 *) plain.data + ((gsize) 2 * GPU_W + GPU_W / 2) * 4;

		g_test_message ("top pixel: plain %d,%d,%d sky %d,%d,%d", was[0], was[1], was[2], top[0], top[1], top[2]);
		g_assert_cmpint (ABS ((gint) top[0] - (gint) bg.r) + ABS ((gint) top[1] - (gint) bg.g) +
		                 ABS ((gint) top[2] - (gint) bg.b), >, 60);
		g_assert_cmpint (top[2], >, top[0]);   /* a blue sky */
	}

	/* Post-processing runs into the target and changes the image */
	{
		g_autoptr(LrgPostProcessor) post = lrg_world3d_renderer_new_preview_post_processor ();
		Image                       with_post;
		guint64                     diff = 0;
		gint                        i;

		lrg_world3d_renderer_set_post_processor (r, post);
		with_post = gpu_render (r, world, cam, LRG_WORLD3D_RENDER_FLAG_CLEAR | LRG_WORLD3D_RENDER_FLAG_POST);
		g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_POST), ==, 1);
		for (i = 0; i < GPU_W * GPU_H * 4; i++)
			diff += (guint64) ABS ((gint) ((guint8 *) with_post.data)[i] - (gint) ((guint8 *) plain.data)[i]);
		g_assert_cmpuint (diff, >, 500);
		UnloadImage (with_post);
		lrg_world3d_renderer_set_post_processor (r, NULL);
	}

	/* Static batching draws the same picture */
	{
		Image   batched;
		guint64 diff = 0;
		gint    i;

		lrg_entity3d_set_static (ground, TRUE);
		batched = gpu_render (r, world, cam, LRG_WORLD3D_RENDER_FLAG_CLEAR | LRG_WORLD3D_RENDER_FLAG_BATCH_STATIC);
		g_assert_cmpuint (lrg_world3d_renderer_get_batched_count (r), ==, 1);
		for (i = 0; i < GPU_W * GPU_H * 4; i++)
			diff += (guint64) ABS ((gint) ((guint8 *) batched.data)[i] - (gint) ((guint8 *) plain.data)[i]);
		g_test_message ("batched vs plain diff %" G_GUINT64_FORMAT, diff);
		g_assert_cmpuint (diff, <, (guint64) GPU_W * GPU_H * 4);   /* under 1 level per channel on average */
		UnloadImage (batched);
	}

	UnloadImage (plain);
	UnloadImage (shadowed);
	UnloadImage (sky);
	lrg_world3d_renderer_release (r);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	gpu_ready = gpu_init ();

	g_test_add_func ("/world3d-renderer/view", test_view);
	g_test_add_func ("/world3d-renderer/lights", test_lights);
	g_test_add_func ("/world3d-renderer/pass-order", test_pass_order);
	g_test_add_func ("/world3d-renderer/shadow-replay", test_shadow_replay);
	g_test_add_func ("/world3d-renderer/cull-mask", test_cull_mask);
	g_test_add_func ("/world3d-renderer/settings", test_settings);
	g_test_add_func ("/world3d-renderer/world", test_world);
	g_test_add_func ("/world3d-renderer/assets", test_assets);
	g_test_add_func ("/world3d-renderer/gpu", test_gpu);

	result = g_test_run ();
	if (gpu_ready)
		CloseWindow ();
	return result;
}
