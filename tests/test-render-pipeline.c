/* test-render-pipeline.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The 0.9.0 frame of LrgWorld3DRenderer: materials, LOD fade, decals,
 * depth prepass + SSAO, environment (sky, IBL, reflection probes,
 * volumetric fog), real post-processing and the debug view modes.
 *
 * Headless (every machine): the new settings and their defaults, the
 * look precedence (renderer override > world > fallback), the pass
 * counts of the prepass, SSAO and volumetric passes, the new render
 * modes and their names.
 *
 * GPU (only under the isolated X display, tests/xrun.sh style): each
 * pass switched on and off with a pixel assertion on a small scene (a
 * ground plane and a cube lit by a sun, rendered from a fixed view).
 */

#include <glib.h>
#include <libregnum.h>
#include <raylib.h>
#include <math.h>
#include <string.h>

#define GPU_W (160)
#define GPU_H (120)

static gboolean gpu_ready = FALSE;

/* ==========================================================================
 * Scene helpers
 * ========================================================================== */

/* The fixed view every case renders through. */
static void
scene_view (LrgWorld3DView *view)
{
	lrg_world3d_view_init (view, GPU_W, GPU_H);
	view->position[0] = 4.0f;
	view->position[1] = 3.0f;
	view->position[2] = 4.0f;
	view->target[0] = 0.0f;
	view->target[1] = 0.4f;
	view->target[2] = 0.0f;
	view->fovy = 50.0f;
	view->near_plane = 0.1f;
	view->far_plane = 100.0f;
}

/* A primitive item at @x,@y,@z scaled by @sx,@sy,@sz. */
static void
add_prim (LrgRenderList    *list,
          LrgPrimitiveType  prim,
          gfloat            x,
          gfloat            y,
          gfloat            z,
          gfloat            sx,
          gfloat            sy,
          gfloat            sz,
          gpointer          material)
{
	LrgRenderItem item;

	lrg_render_item_init (&item);
	item.kind = LRG_RENDER_ITEM_PRIMITIVE;
	item.primitive = prim;
	item.world_matrix[0] = sx;
	item.world_matrix[5] = sy;
	item.world_matrix[10] = sz;
	item.world_matrix[12] = x;
	item.world_matrix[13] = y;
	item.world_matrix[14] = z;
	item.color[0] = item.color[1] = item.color[2] = 0.8f;
	item.material = material;
	lrg_render_list_add (list, &item);
}

/* The test scene: a 12 x 12 ground, a unit cube on it, a sun. */
static LrgRenderList *
scene_list (gpointer cube_material)
{
	LrgRenderList  *list = lrg_render_list_new ();
	LrgLight3DDesc  sun;

	add_prim (list, LRG_PRIMITIVE_PLANE, 0.0f, 0.0f, 0.0f, 12.0f, 1.0f, 12.0f, NULL);
	add_prim (list, LRG_PRIMITIVE_CUBE, 0.0f, 0.5f, 0.0f, 1.0f, 1.0f, 1.0f, cube_material);
	lrg_light3d_desc_init (&sun);
	sun.kind = LRG_LIGHT3D_DIRECTIONAL;
	sun.direction[0] = -0.3f;
	sun.direction[1] = -1.0f;
	sun.direction[2] = -0.5f;
	lrg_render_list_add_light (list, &sun);
	return list;
}

/* A world with the same scene as entities (for world-driven passes). */
static LrgWorld3D *
scene_world (void)
{
	LrgWorld3D               *world = lrg_world3d_new ();
	GObject                  *src = g_object_new (G_TYPE_OBJECT, NULL);
	LrgEntity3D              *e;
	LrgMeshRendererComponent *mr;
	LrgLight3DDesc            sun;

	e = lrg_entity3d_new_with_name ("Ground");
	mr = lrg_mesh_renderer_component_new_primitive (LRG_PRIMITIVE_PLANE);
	lrg_entity3d_add_component (e, LRG_COMPONENT (mr));
	lrg_entity3d_set_scale_xyz (e, 12.0f, 1.0f, 12.0f);
	lrg_world3d_add_entity (world, e);
	g_object_unref (mr);
	g_object_unref (e);

	e = lrg_entity3d_new_with_name ("Cube");
	mr = lrg_mesh_renderer_component_new_primitive (LRG_PRIMITIVE_CUBE);
	lrg_entity3d_add_component (e, LRG_COMPONENT (mr));
	lrg_entity3d_set_position_xyz (e, 0.0f, 0.5f, 0.0f);
	lrg_world3d_add_entity (world, e);
	g_object_unref (mr);
	g_object_unref (e);

	lrg_light3d_desc_init (&sun);
	sun.kind = LRG_LIGHT3D_DIRECTIONAL;
	sun.direction[0] = -0.3f;
	sun.direction[1] = -1.0f;
	sun.direction[2] = -0.5f;
	/* the source must outlive the registration: the world keeps it */
	lrg_world3d_register_light (world, src, &sun);
	g_object_set_data_full (G_OBJECT (world), "test-sun", src, g_object_unref);
	return world;
}

/* An environment with a blue gradient sky. */
static LrgEnvironment *
blue_environment (void)
{
	LrgEnvironment *env = lrg_environment_new ();
	const gfloat    zenith[3] = { 0.15f, 0.35f, 0.9f };
	const gfloat    horizon[3] = { 0.6f, 0.75f, 0.95f };
	const gfloat    ground[3] = { 0.25f, 0.22f, 0.2f };

	lrg_environment_set_sky_source (env, LRG_ENVIRONMENT_SKY_GRADIENT);
	lrg_environment_set_gradient (env, zenith, horizon, ground);
	lrg_environment_set_resolution (env, 32);
	return env;
}

/* ==========================================================================
 * Headless
 * ========================================================================== */

static void
test_api (void)
{
	g_autoptr(LrgWorld3DRenderer) r = lrg_world3d_renderer_new ();
	g_autoptr(LrgEnvironment)     env = lrg_environment_new ();
	g_autoptr(LrgPostStack)       stack = lrg_post_stack_new ();

	/* Defaults */
	g_assert_null (lrg_world3d_renderer_get_environment (r));
	g_assert_null (lrg_world3d_renderer_get_active_environment (r));
	g_assert_null (lrg_world3d_renderer_get_post_stack (r));
	g_assert_false (lrg_world3d_renderer_get_ssao_enabled (r));
	g_assert_true (LRG_IS_SSAO (lrg_world3d_renderer_get_ssao (r)));
	g_assert_true (LRG_IS_DECAL_SET (lrg_world3d_renderer_get_decal_set (r)));
	g_assert_true (lrg_world3d_renderer_get_decals_enabled (r));
	g_assert_true (lrg_world3d_renderer_get_ibl_enabled (r));
	g_assert_true (lrg_world3d_renderer_get_volumetric_enabled (r));

	/* Setters */
	lrg_world3d_renderer_set_environment (r, env);
	g_assert_true (lrg_world3d_renderer_get_environment (r) == env);
	lrg_world3d_renderer_set_post_stack (r, stack);
	g_assert_true (lrg_world3d_renderer_get_post_stack (r) == stack);
	lrg_world3d_renderer_set_ssao_enabled (r, TRUE);
	g_assert_true (lrg_world3d_renderer_get_ssao_enabled (r));
	lrg_world3d_renderer_set_decals_enabled (r, FALSE);
	g_assert_false (lrg_world3d_renderer_get_decals_enabled (r));
	lrg_world3d_renderer_set_ibl_enabled (r, FALSE);
	g_assert_false (lrg_world3d_renderer_get_ibl_enabled (r));
	lrg_world3d_renderer_set_volumetric_enabled (r, FALSE);
	g_assert_false (lrg_world3d_renderer_get_volumetric_enabled (r));
	lrg_world3d_renderer_set_environment (r, NULL);
	lrg_world3d_renderer_set_post_stack (r, NULL);
	g_assert_null (lrg_world3d_renderer_get_environment (r));

	/* Without a window baking does nothing and succeeds */
	lrg_world3d_renderer_set_headless (r, TRUE);
	g_assert_true (lrg_world3d_renderer_bake_environment (r, NULL));
	g_assert_cmpuint (lrg_world3d_renderer_capture_probes (r, NULL), ==, 0);
	g_assert_cmpfloat (lrg_world3d_renderer_get_pass_time (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), ==, 0.0);
}

/* The renderer's own environment beats the world's, which beats the
 * fallback layer. */
static void
test_precedence (void)
{
	g_autoptr(LrgWorld3DRenderer) r = lrg_world3d_renderer_new ();
	g_autoptr(LrgWorld3D)         world = scene_world ();
	g_autoptr(LrgEnvironment)     own = lrg_environment_new ();
	g_autoptr(LrgEnvironment)     world_env = lrg_environment_new ();
	g_autoptr(LrgEnvironment)     fallback = lrg_environment_new ();
	g_autoptr(LrgRenderList)      list = scene_list (NULL);
	LrgWorld3DView                view;

	lrg_world3d_renderer_set_headless (r, TRUE);
	scene_view (&view);

	/* Nothing set */
	lrg_world3d_renderer_render_view (r, world, &view);
	g_assert_null (lrg_world3d_renderer_get_active_environment (r));

	/* Fallback only */
	lrg_world3d_renderer_set_fallback_look (r, fallback, NULL);
	lrg_world3d_renderer_render_view (r, world, &view);
	g_assert_true (lrg_world3d_renderer_get_active_environment (r) == fallback);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_true (lrg_world3d_renderer_get_active_environment (r) == fallback);

	/* The world's beats the fallback, only in world renders */
	lrg_world3d_set_environment (world, world_env);
	lrg_world3d_renderer_render_view (r, world, &view);
	g_assert_true (lrg_world3d_renderer_get_active_environment (r) == world_env);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_true (lrg_world3d_renderer_get_active_environment (r) == fallback);

	/* The renderer's own beats both */
	lrg_world3d_renderer_set_environment (r, own);
	lrg_world3d_renderer_render_view (r, world, &view);
	g_assert_true (lrg_world3d_renderer_get_active_environment (r) == own);

	lrg_world3d_renderer_set_fallback_look (r, NULL, NULL);
	lrg_world3d_renderer_set_environment (r, NULL);
	lrg_world3d_set_environment (world, NULL);
	lrg_world3d_renderer_render_view (r, world, &view);
	g_assert_null (lrg_world3d_renderer_get_active_environment (r));
}

/* Prepass, SSAO and volumetric passes run only when they are needed. */
static void
test_headless_passes (void)
{
	g_autoptr(LrgWorld3DRenderer) r = lrg_world3d_renderer_new ();
	g_autoptr(LrgRenderList)      list = scene_list (NULL);
	g_autoptr(LrgEnvironment)     env = blue_environment ();
	LrgWorld3DView                view;

	lrg_world3d_renderer_set_headless (r, TRUE);
	scene_view (&view);

	/* Plain: no prepass, no SSAO, no volumetric */
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_PREPASS), ==, 0);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SSAO), ==, 0);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_VOLUMETRIC), ==, 0);

	/* SSAO: the prepass replays the opaque items, then SSAO runs */
	lrg_world3d_renderer_set_ssao_enabled (r, TRUE);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_PREPASS), ==, 2);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SSAO), ==, 1);

	/* ...not in an orthographic view, nor in the unlit mode */
	view.orthographic = TRUE;
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SSAO), ==, 0);
	view.orthographic = FALSE;
	lrg_world3d_renderer_set_render_mode (r, LRG_WORLD3D_RENDER_MODE_UNLIT);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_PREPASS), ==, 0);
	/* the AO view needs it */
	lrg_world3d_renderer_set_render_mode (r, LRG_WORLD3D_RENDER_MODE_AO);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SSAO), ==, 1);
	lrg_world3d_renderer_set_render_mode (r, LRG_WORLD3D_RENDER_MODE_LIT);
	lrg_world3d_renderer_set_ssao_enabled (r, FALSE);

	/* Volumetric fog: only with an environment that enables it */
	lrg_world3d_renderer_set_environment (r, env);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_VOLUMETRIC), ==, 0);
	lrg_environment_set_volumetric (env, TRUE, 0.08f, 0.3f, 24, 40.0f, 1.0f);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_VOLUMETRIC), ==, 1);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_PREPASS), ==, 2);
	lrg_world3d_renderer_set_volumetric_enabled (r, FALSE);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_VOLUMETRIC), ==, 0);

	/* Debug views skip the sky */
	lrg_world3d_renderer_set_flags (r, LRG_WORLD3D_RENDER_FLAGS_DEFAULT);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SKY), ==, 1);
	lrg_world3d_renderer_set_render_mode (r, LRG_WORLD3D_RENDER_MODE_OVERDRAW);
	lrg_world3d_renderer_render_list (r, list, &view);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SKY), ==, 0);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), ==, 2);
}

/* The five debug views: values after LIGHTING_ONLY, in order, named. */
static void
test_modes (void)
{
	g_autoptr(LrgWorld3DRenderer) r = lrg_world3d_renderer_new ();
	GEnumClass                   *klass = g_type_class_ref (LRG_TYPE_WORLD3D_RENDER_MODE);
	static const gchar           *nicks[] = { "overdraw", "uv", "light-complexity", "ao", "reflections" };
	guint                         i;

	g_assert_cmpint (LRG_WORLD3D_RENDER_MODE_OVERDRAW, ==, LRG_WORLD3D_RENDER_MODE_LIGHTING_ONLY + 1);
	for (i = 0; i < G_N_ELEMENTS (nicks); i++)
	{
		GEnumValue *v = g_enum_get_value_by_nick (klass, nicks[i]);

		g_assert_nonnull (v);
		g_assert_cmpint (v->value, ==, LRG_WORLD3D_RENDER_MODE_OVERDRAW + (gint) i);
		lrg_world3d_renderer_set_render_mode (r, (LrgWorld3DRenderMode) v->value);
		g_assert_cmpint (lrg_world3d_renderer_get_render_mode (r), ==, v->value);
	}
	g_type_class_unref (klass);

	/* The new passes are named too */
	klass = g_type_class_ref (LRG_TYPE_WORLD3D_RENDER_PASS);
	g_assert_nonnull (g_enum_get_value_by_nick (klass, "prepass"));
	g_assert_nonnull (g_enum_get_value_by_nick (klass, "ssao"));
	g_assert_nonnull (g_enum_get_value_by_nick (klass, "volumetric"));
	g_type_class_unref (klass);
	g_assert_cmpint (LRG_WORLD3D_RENDER_PASS_COUNT, ==, LRG_WORLD3D_RENDER_PASS_VOLUMETRIC + 1);
}

/* ==========================================================================
 * GPU helpers
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
	InitWindow (64, 64, "test-render-pipeline");
	return IsWindowReady ();
}

/* Reads a render texture back into an upright RGBA image. */
static Image
read_target (GrlRenderTexture *target)
{
	g_autoptr(GrlTexture) tex = grl_render_texture_get_texture (target);
	g_autoptr(GrlImage)   grl = grl_texture_to_image (tex);
	const guint8         *pixels;
	gsize                 size = 0;
	Image                 img;

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

/* Renders @list (or @world when set) into a fresh image. */
static Image
render (LrgWorld3DRenderer *r,
        LrgRenderList      *list,
        LrgWorld3D         *world)
{
	g_autoptr(GrlRenderTexture) target = grl_render_texture_new (GPU_W, GPU_H);
	LrgWorld3DView              view;
	Image                       img;

	scene_view (&view);
	BeginDrawing ();
	grl_render_texture_begin (target);
	if (world != NULL)
		g_assert_true (lrg_world3d_renderer_render_view (r, world, &view));
	else
		g_assert_true (lrg_world3d_renderer_render_list (r, list, &view));
	grl_render_texture_end (target);
	EndDrawing ();
	img = read_target (target);
	return img;
}

/* Saves @img when LRG_TEST_SHOTS names a directory (visual checks). */
static void
maybe_save (Image        img,
            const gchar *name)
{
	const gchar      *dir = g_getenv ("LRG_TEST_SHOTS");
	g_autofree gchar *path = NULL;

	if (dir == NULL || *dir == '\0')
		return;
	path = g_strdup_printf ("%s/%s.png", dir, name);
	ExportImage (img, path);
}

/* One pixel's RGB. */
static void
pixel (Image   img,
       gint    x,
       gint    y,
       guint8 *out)
{
	const guint8 *p = (const guint8 *) img.data + ((gsize) y * (gsize) img.width + (gsize) x) * 4;

	out[0] = p[0];
	out[1] = p[1];
	out[2] = p[2];
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

/* Sum of absolute channel differences. */
static guint64
image_diff (Image a,
            Image b)
{
	guint64 diff = 0;
	gint    i;

	for (i = 0; i < GPU_W * GPU_H * 4; i++)
		diff += (guint64) ABS ((gint) ((guint8 *) a.data)[i] - (gint) ((guint8 *) b.data)[i]);
	return diff;
}

/* Projects a world point to image pixels through the scene view. */
static void
project (const gfloat *p,
         gint         *out_x,
         gint         *out_y)
{
	LrgWorld3DView view;
	gfloat         v[16], pr[16], vp[16], clip[4];
	gint           k;

	scene_view (&view);
	lrg_world3d_view_get_view_matrix (&view, v);
	lrg_world3d_view_get_projection_matrix (&view, pr);
	lrg_math3d_multiply (pr, v, vp);
	for (k = 0; k < 4; k++)
		clip[k] = vp[k] * p[0] + vp[4 + k] * p[1] + vp[8 + k] * p[2] + vp[12 + k];
	*out_x = (gint) ((clip[0] / clip[3] * 0.5f + 0.5f) * GPU_W);
	*out_y = (gint) ((0.5f - clip[1] / clip[3] * 0.5f) * GPU_H);
}

/* A renderer drawing into a cleared target without sky or debug draw. */
static LrgWorld3DRenderer *
gpu_renderer (void)
{
	LrgWorld3DRenderer *r = lrg_world3d_renderer_new ();

	lrg_world3d_renderer_set_flags (r, LRG_WORLD3D_RENDER_FLAG_CLEAR);
	return r;
}

#define NEED_GPU() \
	G_STMT_START { if (!gpu_ready) { g_test_skip ("needs the isolated X display (xrun.sh)"); return; } } G_STMT_END

/* ==========================================================================
 * GPU cases
 * ========================================================================== */

/* An LrgMaterial colours the cube through the lit contract, metallic
 * changes the shading, and a legacy LrgMaterial3D no longer squares the
 * item colour. */
static void
test_gpu_materials (void)
{
	g_autoptr(LrgWorld3DRenderer) r = NULL;
	g_autoptr(LrgMaterial)        red = NULL;
	g_autoptr(LrgMaterial3D)      grey = NULL;
	const gfloat                  top[3] = { 0.0f, 1.0f, 0.0f };
	Image                         plain, mat_img, metal_img, m3d_img;
	guint8                        px[3];
	gint                          cx, cy;

	NEED_GPU ();
	r = gpu_renderer ();
	project (top, &cx, &cy);
	cy += 6;   /* on the top face, below its far edge */

	{
		g_autoptr(LrgRenderList) list = scene_list (NULL);

		plain = render (r, list, NULL);
	}

	/* A red LrgMaterial: the item colour is its base colour */
	red = lrg_material_new ();
	lrg_material_set_base_color (red, 0.9f, 0.1f, 0.1f, 1.0f);
	{
		g_autoptr(LrgRenderList) list = scene_list (red);
		LrgRenderItem           *cube = (LrgRenderItem *) lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_OPAQUE, 1);

		cube->color[0] = 0.9f;
		cube->color[1] = 0.1f;
		cube->color[2] = 0.1f;
		mat_img = render (r, list, NULL);
		maybe_save (mat_img, "material-red");
		pixel (mat_img, cx, cy, px);
		g_test_message ("material cube pixel %d,%d,%d", px[0], px[1], px[2]);
		g_assert_cmpint (px[0], >, px[1] + 40);
		g_assert_cmpint (px[0], >, px[2] + 40);

		/* Fully metallic and smooth: no diffuse, darker under a plain
		 * ambient */
		lrg_material_set_metallic (red, 1.0f);
		lrg_material_set_roughness (red, 0.1f);
		metal_img = render (r, list, NULL);
		g_test_message ("metal luma %.1f vs dielectric %.1f", luma_at (metal_img, cx, cy), luma_at (mat_img, cx, cy));
		g_assert_cmpfloat (fabsf (luma_at (metal_img, cx, cy) - luma_at (mat_img, cx, cy)), >, 4.0f);
	}

	/* An LrgMaterial3D of colour 0.8 on an item of colour 0.8 shades like
	 * the plain 0.8 item (it was 0.64 before 0.9.0) */
	grey = lrg_material3d_new ();
	lrg_material3d_set_color (grey, 0.8f, 0.8f, 0.8f, 1.0f);
	lrg_material3d_set_roughness (grey, 0.5f);
	lrg_material3d_set_metallic (grey, 0.0f);
	{
		g_autoptr(LrgRenderList) list = scene_list (grey);

		m3d_img = render (r, list, NULL);
		g_test_message ("material3d luma %.1f vs plain %.1f", luma_at (m3d_img, cx, cy), luma_at (plain, cx, cy));
		g_assert_cmpfloat (fabsf (luma_at (m3d_img, cx, cy) - luma_at (plain, cx, cy)), <, 6.0f);
	}

	UnloadImage (plain);
	UnloadImage (mat_img);
	UnloadImage (metal_img);
	UnloadImage (m3d_img);
	lrg_world3d_renderer_release (r);
}

/* The lit shader dithers fragments out by lodFade; the renderer leaves
 * every program at 0 after its items. */
static void
test_gpu_lod_fade (void)
{
	g_autoptr(LrgWorld3DRenderer) r = NULL;
	g_autoptr(LrgRenderList)      list = scene_list (NULL);
	GrlShader                    *shader;
	guint                         id;
	GrlColor                      bg;
	Image                         plain, faded;
	const gfloat                  side[3] = { 0.5f, 0.5f, 0.0f };
	gint                          x, y;

	NEED_GPU ();
	r = gpu_renderer ();
	lrg_world3d_renderer_get_background (r, &bg);
	plain = render (r, list, NULL);
	shader = lrg_light_set_get_shader (lrg_world3d_renderer_get_light_set (r));
	g_assert_nonnull (shader);
	id = ((Shader *) grl_shader_get_handle (shader))->id;

	/* A tiny positive fade (fading in) keeps almost no fragment: the
	 * background shows where the cube was */
	lrg_lod_bind_fade (id, 0.02f);
	faded = render (r, list, NULL);
	lrg_lod_bind_fade (id, 0.0f);
	maybe_save (faded, "lod-fade");
	project (side, &x, &y);
	g_test_message ("cube side luma %.1f -> %.1f (bg %d)", luma_at (plain, x - 3, y), luma_at (faded, x - 3, y), bg.r);
	g_assert_cmpfloat (image_diff (plain, faded), >, (guint64) GPU_W * GPU_H * 20);

	UnloadImage (plain);
	UnloadImage (faded);
	lrg_world3d_renderer_release (r);
}

/* A decal in the renderer's set changes the ground inside its box only. */
static void
test_gpu_decals (void)
{
	g_autoptr(LrgWorld3DRenderer) r = NULL;
	g_autoptr(LrgRenderList)      list = scene_list (NULL);
	g_autoptr(LrgDecal)           decal = lrg_decal_new ();
	const gfloat                  pos[3] = { 1.6f, 0.0f, 1.6f };
	const gfloat                  size[3] = { 1.2f, 1.0f, 1.2f };
	const gfloat                  outside[3] = { -1.8f, 0.0f, 1.6f };
	Image                         plain, decaled, off;
	guint8                        a[3], b[3];
	gint                          x, y, ox, oy;

	NEED_GPU ();
	r = gpu_renderer ();
	plain = render (r, list, NULL);

	lrg_decal_set_trs (decal, pos, NULL, size);
	lrg_decal_set_color (decal, 0.9f, 0.05f, 0.05f, 1.0f);
	lrg_decal_set_add (lrg_world3d_renderer_get_decal_set (r), decal);
	decaled = render (r, list, NULL);
	maybe_save (decaled, "decal");
	project (pos, &x, &y);
	project (outside, &ox, &oy);
	pixel (plain, x, y, a);
	pixel (decaled, x, y, b);
	g_test_message ("decal centre %d,%d,%d -> %d,%d,%d", a[0], a[1], a[2], b[0], b[1], b[2]);
	g_assert_cmpint (b[0], >, b[1] + 40);
	g_assert_cmpfloat (fabsf (luma_at (plain, ox, oy) - luma_at (decaled, ox, oy)), <, 2.0f);

	/* Switched off: the plain picture */
	lrg_world3d_renderer_set_decals_enabled (r, FALSE);
	off = render (r, list, NULL);
	g_assert_cmpuint (image_diff (plain, off), <, (guint64) GPU_W * GPU_H);

	UnloadImage (plain);
	UnloadImage (decaled);
	UnloadImage (off);
	lrg_world3d_renderer_release (r);
}

/* SSAO darkens the crease where the cube meets the ground; the AO view
 * shows it on white. */
static void
test_gpu_ssao (void)
{
	g_autoptr(LrgWorld3DRenderer) r = NULL;
	g_autoptr(LrgRenderList)      list = scene_list (NULL);
	const gfloat                  crease[3] = { 0.55f, 0.03f, 0.55f };
	const gfloat                  open[3] = { 2.5f, 0.0f, -1.5f };
	Image                         off, on, ao_off, ao_on;
	gint                          cx, cy, ox, oy;

	NEED_GPU ();
	r = gpu_renderer ();
	project (crease, &cx, &cy);
	project (open, &ox, &oy);
	off = render (r, list, NULL);

	lrg_world3d_renderer_set_ssao_enabled (r, TRUE);
	lrg_ssao_set_intensity (lrg_world3d_renderer_get_ssao (r), 1.5f);
	on = render (r, list, NULL);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SSAO), ==, 1);
	maybe_save (on, "ssao-lit");
	g_test_message ("crease luma %.1f -> %.1f, open %.1f -> %.1f", luma_at (off, cx, cy), luma_at (on, cx, cy),
	                luma_at (off, ox, oy), luma_at (on, ox, oy));
	g_assert_cmpfloat (luma_at (on, cx, cy), <, luma_at (off, cx, cy) - 1.0f);

	/* The AO view: open ground near white, the crease darker; it runs
	 * SSAO even while SSAO is off */
	lrg_world3d_renderer_set_render_mode (r, LRG_WORLD3D_RENDER_MODE_AO);
	ao_on = render (r, list, NULL);
	maybe_save (ao_on, "view-ao");
	lrg_world3d_renderer_set_ssao_enabled (r, FALSE);
	ao_off = render (r, list, NULL);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_SSAO), ==, 1);
	g_test_message ("AO view: open %.1f, crease %.1f (SSAO off: crease %.1f)", luma_at (ao_on, ox, oy),
	                luma_at (ao_on, cx, cy), luma_at (ao_off, cx, cy));
	g_assert_cmpfloat (luma_at (ao_on, ox, oy), >, 230.0f);
	g_assert_cmpfloat (luma_at (ao_on, cx, cy), <, luma_at (ao_on, ox, oy) - 10.0f);
	g_assert_cmpfloat (luma_at (ao_off, cx, cy), <, luma_at (ao_off, ox, oy) - 10.0f);

	UnloadImage (off);
	UnloadImage (on);
	UnloadImage (ao_off);
	UnloadImage (ao_on);
	lrg_world3d_renderer_release (r);
}

/* The environment's sky replaces the dome, its IBL lights a metal
 * sphere, a reflection probe captures and shows in the reflections view,
 * and the volumetric fog changes the picture. */
static void
test_gpu_environment (void)
{
	g_autoptr(LrgWorld3DRenderer) r = NULL;
	g_autoptr(LrgEnvironment)     env = blue_environment ();
	g_autoptr(LrgMaterial)        metal = lrg_material_new ();
	g_autoptr(LrgReflectionProbe) probe = lrg_reflection_probe_new ();
	g_autoptr(LrgRenderList)      list = NULL;
	const gfloat                  top[3] = { 0.0f, 1.0f, 0.0f };
	Image                         dome, sky, ibl_off, ibl_on, refl_none, refl, fog_off, fog_on;
	guint8                        px[3];
	gint                          cx, cy;

	NEED_GPU ();
	r = gpu_renderer ();
	lrg_material_set_metallic (metal, 1.0f);
	lrg_material_set_roughness (metal, 0.2f);
	list = scene_list (metal);
	project (top, &cx, &cy);
	cy += 6;

	/* Sky: the dome, then the environment's blue gradient */
	lrg_world3d_renderer_set_flags (r, LRG_WORLD3D_RENDER_FLAG_CLEAR | LRG_WORLD3D_RENDER_FLAG_SKY);
	dome = render (r, list, NULL);
	lrg_world3d_renderer_set_environment (r, env);
	g_assert_true (lrg_world3d_renderer_bake_environment (r, NULL));
	g_assert_true (lrg_environment_is_baked (env));
	sky = render (r, list, NULL);
	maybe_save (sky, "environment-sky");
	pixel (sky, GPU_W / 2, 2, px);
	g_test_message ("sky top %d,%d,%d", px[0], px[1], px[2]);
	g_assert_cmpint (px[2], >, px[0] + 30);
	g_assert_cmpuint (image_diff (dome, sky), >, (guint64) GPU_W * GPU_H * 4);

	/* IBL: the metal top face reflects the sky only with IBL on */
	lrg_world3d_renderer_set_flags (r, LRG_WORLD3D_RENDER_FLAG_CLEAR);
	lrg_world3d_renderer_set_ibl_enabled (r, FALSE);
	ibl_off = render (r, list, NULL);
	lrg_world3d_renderer_set_ibl_enabled (r, TRUE);
	ibl_on = render (r, list, NULL);
	maybe_save (ibl_on, "environment-ibl");
	g_test_message ("metal top luma IBL off %.1f, on %.1f", luma_at (ibl_off, cx, cy), luma_at (ibl_on, cx, cy));
	g_assert_cmpfloat (fabsf (luma_at (ibl_on, cx, cy) - luma_at (ibl_off, cx, cy)), >, 5.0f);

	/* Reflections view: black without an environment, the sky with one */
	lrg_world3d_renderer_set_render_mode (r, LRG_WORLD3D_RENDER_MODE_REFLECTIONS);
	refl = render (r, list, NULL);
	maybe_save (refl, "view-reflections");
	lrg_world3d_renderer_set_environment (r, NULL);
	refl_none = render (r, list, NULL);
	g_test_message ("reflections top luma: none %.1f, env %.1f", luma_at (refl_none, cx, cy), luma_at (refl, cx, cy));
	g_assert_cmpfloat (luma_at (refl_none, cx, cy), <, 8.0f);
	g_assert_cmpfloat (luma_at (refl, cx, cy), >, 30.0f);
	lrg_world3d_renderer_set_render_mode (r, LRG_WORLD3D_RENDER_MODE_LIT);
	lrg_world3d_renderer_set_environment (r, env);

	/* A probe over the cube: one capture, ready afterwards */
	lrg_reflection_probe_set_position (probe, 0.0f, 1.5f, 0.0f);
	lrg_reflection_probe_set_extents (probe, 4.0f, 3.0f, 4.0f);
	lrg_reflection_probe_set_resolution (probe, 32);
	lrg_environment_add_probe (env, probe);
	{
		g_autoptr(LrgWorld3D) world = scene_world ();

		g_assert_cmpuint (lrg_world3d_renderer_capture_probes (r, world), ==, 1);
	}
	g_assert_true (lrg_reflection_probe_is_ready (probe));
	g_assert_true (lrg_world3d_renderer_capture_probe (r, probe, list, env, NULL));
	lrg_environment_remove_probe (env, probe);

	/* Volumetric fog */
	fog_off = render (r, list, NULL);
	lrg_environment_set_volumetric (env, TRUE, 0.25f, 0.4f, 24, 30.0f, 2.0f);
	fog_on = render (r, list, NULL);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_VOLUMETRIC), ==, 1);
	maybe_save (fog_on, "volumetric");
	g_test_message ("volumetric diff %" G_GUINT64_FORMAT, image_diff (fog_off, fog_on));
	g_assert_cmpuint (image_diff (fog_off, fog_on), >, (guint64) GPU_W * GPU_H);

	UnloadImage (dome);
	UnloadImage (sky);
	UnloadImage (ibl_off);
	UnloadImage (ibl_on);
	UnloadImage (refl_none);
	UnloadImage (refl);
	UnloadImage (fog_off);
	UnloadImage (fog_on);
	lrg_world3d_renderer_release (r);
}

/* The preview chain runs every effect through the processor; a post
 * stack replaces the processor. */
static void
test_gpu_post (void)
{
	g_autoptr(LrgWorld3DRenderer) r = NULL;
	g_autoptr(LrgRenderList)      list = scene_list (NULL);
	g_autoptr(LrgPostProcessor)   preview = lrg_world3d_renderer_new_preview_post_processor ();
	g_autoptr(LrgPostStack)       stack = lrg_post_stack_new ();
	Image                         plain, chain, stacked;

	NEED_GPU ();
	r = gpu_renderer ();
	plain = render (r, list, NULL);

	g_assert_cmpuint (lrg_post_processor_get_effect_count (preview), ==, 5);
	g_assert_nonnull (lrg_post_processor_get_effect (preview, "tonemap"));
	lrg_world3d_renderer_set_post_processor (r, preview);
	lrg_world3d_renderer_set_flags (r, LRG_WORLD3D_RENDER_FLAG_CLEAR | LRG_WORLD3D_RENDER_FLAG_POST);
	chain = render (r, list, NULL);
	maybe_save (chain, "post-preview");
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_POST), ==, 5);
	g_assert_cmpuint (image_diff (plain, chain), >, (guint64) GPU_W * GPU_H);

	/* A stack with only a strong vignette: one pass, dark corners */
	g_assert_cmpint (lrg_post_stack_add_effect (stack, "vignette", NULL), ==, 0);
	lrg_world3d_renderer_set_post_stack (r, stack);
	stacked = render (r, list, NULL);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_POST), ==, 1);
	g_test_message ("corner luma plain %.1f, vignette %.1f", luma_at (plain, 3, 3), luma_at (stacked, 3, 3));
	g_assert_cmpfloat (luma_at (stacked, 3, 3), <, luma_at (plain, 3, 3));

	lrg_world3d_renderer_set_post_stack (r, NULL);
	lrg_world3d_renderer_set_post_processor (r, NULL);
	UnloadImage (plain);
	UnloadImage (chain);
	UnloadImage (stacked);
	lrg_world3d_renderer_release (r);
}

/* Overdraw, UV and light complexity views. */
static void
test_gpu_debug_views (void)
{
	g_autoptr(LrgWorld3DRenderer) r = NULL;
	g_autoptr(LrgRenderList)      list = scene_list (NULL);
	const gfloat                  side[3] = { 0.5f, 0.5f, 0.0f };
	const gfloat                  ground[3] = { 2.5f, 0.0f, -1.5f };
	const gfloat                  lamp[3] = { -2.0f, 0.3f, 2.0f };
	const gfloat                  far_pt[3] = { 2.5f, 0.0f, -2.5f };
	LrgLight3DDesc                point;
	Image                         over, uv, cx_img;
	guint8                        px[3];
	gint                          sx, sy, gx, gy, lx, ly, fx, fy;

	NEED_GPU ();
	r = gpu_renderer ();
	project (side, &sx, &sy);
	project (ground, &gx, &gy);
	sx -= 3;

	/* Overdraw: the cube over the ground is two layers, the ground one */
	lrg_world3d_renderer_set_render_mode (r, LRG_WORLD3D_RENDER_MODE_OVERDRAW);
	over = render (r, list, NULL);
	maybe_save (over, "view-overdraw");
	g_test_message ("overdraw ground %.1f, cube %.1f", luma_at (over, gx, gy), luma_at (over, sx, sy));
	g_assert_cmpfloat (luma_at (over, gx, gy), >, 5.0f);
	g_assert_cmpfloat (luma_at (over, sx, sy), >, luma_at (over, gx, gy) + 5.0f);

	/* UV: red and green follow the plane's coordinates */
	lrg_world3d_renderer_set_render_mode (r, LRG_WORLD3D_RENDER_MODE_UV);
	uv = render (r, list, NULL);
	maybe_save (uv, "view-uv");
	pixel (uv, gx, gy, px);
	g_test_message ("uv ground %d,%d,%d", px[0], px[1], px[2]);
	g_assert_cmpint ((gint) px[0] + (gint) px[1], >, 40);

	/* Light complexity: a point light reaches the ground near it only */
	lrg_light3d_desc_init (&point);
	point.kind = LRG_LIGHT3D_POINT;
	point.position[0] = lamp[0];
	point.position[1] = lamp[1];
	point.position[2] = lamp[2];
	point.range = 2.0f;
	lrg_render_list_add_light (list, &point);
	lrg_world3d_renderer_set_render_mode (r, LRG_WORLD3D_RENDER_MODE_LIGHT_COMPLEXITY);
	cx_img = render (r, list, NULL);
	maybe_save (cx_img, "view-light-complexity");
	project (lamp, &lx, &ly);
	project (far_pt, &fx, &fy);
	pixel (cx_img, lx, ly + 4, px);
	g_test_message ("complexity near %d,%d,%d far %.1f", px[0], px[1], px[2], luma_at (cx_img, fx, fy));
	g_assert_cmpint (px[2], >, 150);   /* one light: blue */
	g_assert_cmpfloat (luma_at (cx_img, fx, fy), <, 20.0f);

	UnloadImage (over);
	UnloadImage (uv);
	UnloadImage (cx_img);
	lrg_world3d_renderer_release (r);
}

/* Decal components of a world project through world renders, and the
 * world's post stack runs. */
static void
test_gpu_world_look (void)
{
	g_autoptr(LrgWorld3DRenderer) r = NULL;
	g_autoptr(LrgWorld3D)         world = scene_world ();
	g_autoptr(LrgPostStack)       stack = lrg_post_stack_new ();
	LrgDecalComponent            *dc;
	LrgEntity3D                  *e;
	const gfloat                  pos[3] = { 1.6f, 0.0f, 1.6f };
	Image                         plain, look;
	guint8                        px[3];
	gint                          x, y;

	NEED_GPU ();
	r = gpu_renderer ();
	lrg_world3d_renderer_set_flags (r, LRG_WORLD3D_RENDER_FLAG_CLEAR | LRG_WORLD3D_RENDER_FLAG_POST);
	plain = render (r, NULL, world);

	e = lrg_entity3d_new_with_name ("Decal");
	dc = lrg_decal_component_new ();
	g_object_set (dc, "size-x", 1.2f, "size-y", 1.0f, "size-z", 1.2f, "color-r", 0.05f, "color-g", 0.9f, "color-b",
	              0.05f, NULL);
	lrg_entity3d_add_component (e, LRG_COMPONENT (dc));
	lrg_entity3d_set_position_xyz (e, pos[0], pos[1], pos[2]);
	lrg_world3d_add_entity (world, e);
	g_object_unref (dc);
	g_object_unref (e);
	lrg_post_stack_add_effect (stack, "vignette", NULL);
	lrg_world3d_set_post_stack (world, stack);

	look = render (r, NULL, world);
	maybe_save (look, "world-look");
	project (pos, &x, &y);
	pixel (look, x, y, px);
	g_test_message ("world decal %d,%d,%d", px[0], px[1], px[2]);
	g_assert_cmpint (px[1], >, px[0] + 30);
	g_assert_cmpuint (lrg_world3d_renderer_get_pass_count (r, LRG_WORLD3D_RENDER_PASS_POST), ==, 1);

	UnloadImage (plain);
	UnloadImage (look);
	lrg_world3d_renderer_release (r);
}

/* A spot light lights its cone only (spots are real spots, not points). */
static void
test_gpu_spot (void)
{
	g_autoptr(LrgWorld3DRenderer) r = NULL;
	g_autoptr(LrgRenderList)      list = lrg_render_list_new ();
	LrgLight3DDesc                spot;
	const gfloat                  inside[3] = { 0.0f, 0.0f, 1.5f };
	const gfloat                  outside[3] = { 2.8f, 0.0f, 1.5f };
	Image                         img;
	gint                          ix, iy, ox, oy;

	NEED_GPU ();
	r = gpu_renderer ();
	lrg_world3d_renderer_set_fallback_sun (r, FALSE);
	add_prim (list, LRG_PRIMITIVE_PLANE, 0.0f, 0.0f, 0.0f, 12.0f, 1.0f, 12.0f, NULL);
	lrg_light3d_desc_init (&spot);
	spot.kind = LRG_LIGHT3D_SPOT;
	spot.position[0] = 0.0f;
	spot.position[1] = 3.0f;
	spot.position[2] = 1.5f;
	spot.direction[0] = 0.0f;
	spot.direction[1] = -1.0f;
	spot.direction[2] = 0.0f;
	spot.range = 8.0f;
	spot.intensity = 2.0f;
	spot.inner_angle = 0.2f;
	spot.outer_angle = 0.3f;
	lrg_render_list_add_light (list, &spot);

	img = render (r, list, NULL);
	maybe_save (img, "spot");
	project (inside, &ix, &iy);
	project (outside, &ox, &oy);
	g_test_message ("spot: inside %.1f, outside %.1f", luma_at (img, ix, iy), luma_at (img, ox, oy));
	/* outside the 17-degree cone only the ambient remains */
	g_assert_cmpfloat (luma_at (img, ix, iy), >, luma_at (img, ox, oy) + 40.0f);
	UnloadImage (img);
	lrg_world3d_renderer_release (r);
}

/* Probes are picked per item: an object inside a probe box reflects the
 * probe while the camera is outside it. */
static void
test_gpu_probe_per_item (void)
{
	g_autoptr(LrgWorld3DRenderer) r = NULL;
	g_autoptr(LrgEnvironment)     env = blue_environment ();
	g_autoptr(LrgReflectionProbe) probe = lrg_reflection_probe_new ();
	g_autoptr(LrgMaterial)        mirror = lrg_material_new ();
	g_autoptr(LrgRenderList)      list = NULL;
	g_autoptr(LrgRenderList)      room = lrg_render_list_new ();
	const gfloat                  top[3] = { 0.0f, 1.0f, 0.0f };
	Image                         without, with;
	gint                          cx, cy;

	NEED_GPU ();
	r = gpu_renderer ();
	lrg_material_set_metallic (mirror, 1.0f);
	lrg_material_set_roughness (mirror, 0.05f);
	list = scene_list (mirror);
	project (top, &cx, &cy);   /* the centre of the top face */
	lrg_world3d_renderer_set_environment (r, env);
	lrg_world3d_renderer_set_render_mode (r, LRG_WORLD3D_RENDER_MODE_REFLECTIONS);
	without = render (r, list, NULL);

	/* The probe sees a wide red ceiling above the cube (the grazing
	 * reflection off the top face hits it); its box holds the cube but
	 * not the camera at (4, 3, 4) */
	add_prim (room, LRG_PRIMITIVE_CUBE, 0.0f, 2.5f, 0.0f, 16.0f, 0.2f, 16.0f, NULL);
	{
		LrgRenderItem *ceiling = (LrgRenderItem *) lrg_render_list_get_item (room, LRG_RENDER_CATEGORY_OPAQUE, 0);

		ceiling->color[0] = 0.9f;
		ceiling->color[1] = 0.05f;
		ceiling->color[2] = 0.05f;
	}
	lrg_reflection_probe_set_position (probe, 0.0f, 1.2f, 0.0f);
	lrg_reflection_probe_set_extents (probe, 1.5f, 1.5f, 1.5f);
	lrg_reflection_probe_set_resolution (probe, 32);
	lrg_reflection_probe_set_box_projection (probe, FALSE);
	g_assert_true (lrg_world3d_renderer_capture_probe (r, probe, room, env, NULL));
	lrg_environment_add_probe (env, probe);
	with = render (r, list, NULL);
	maybe_save (with, "probe-per-item");
	{
		guint8 a[3], b[3];

		pixel (without, cx, cy, a);
		pixel (with, cx, cy, b);
		g_test_message ("cube top reflection %d,%d,%d -> %d,%d,%d", a[0], a[1], a[2], b[0], b[1], b[2]);
		g_assert_cmpint ((gint) b[0] - (gint) b[2], >, (gint) a[0] - (gint) a[2] + 30);
	}
	lrg_environment_remove_probe (env, probe);
	UnloadImage (without);
	UnloadImage (with);
	lrg_world3d_renderer_release (r);
}

/* Per-pass timing fills the passes that ran. */
static void
test_gpu_timing (void)
{
	g_autoptr(LrgWorld3DRenderer) r = NULL;
	g_autoptr(LrgRenderList)      list = scene_list (NULL);
	Image                         img;

	NEED_GPU ();
	r = gpu_renderer ();
	lrg_world3d_renderer_set_timing (r, TRUE);
	lrg_world3d_renderer_set_ssao_enabled (r, TRUE);
	img = render (r, list, NULL);
	g_assert_cmpfloat (lrg_world3d_renderer_get_pass_time (r, LRG_WORLD3D_RENDER_PASS_OPAQUE), >, 0.0);
	g_assert_cmpfloat (lrg_world3d_renderer_get_pass_time (r, LRG_WORLD3D_RENDER_PASS_PREPASS), >, 0.0);
	g_assert_cmpfloat (lrg_world3d_renderer_get_pass_time (r, LRG_WORLD3D_RENDER_PASS_SSAO), >, 0.0);
	g_assert_cmpfloat (lrg_world3d_renderer_get_pass_time (r, LRG_WORLD3D_RENDER_PASS_POST), ==, 0.0);
	UnloadImage (img);
	lrg_world3d_renderer_release (r);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	gpu_ready = gpu_init ();

	g_test_add_func ("/render-pipeline/api", test_api);
	g_test_add_func ("/render-pipeline/precedence", test_precedence);
	g_test_add_func ("/render-pipeline/headless-passes", test_headless_passes);
	g_test_add_func ("/render-pipeline/modes", test_modes);
	g_test_add_func ("/render-pipeline/gpu/materials", test_gpu_materials);
	g_test_add_func ("/render-pipeline/gpu/lod-fade", test_gpu_lod_fade);
	g_test_add_func ("/render-pipeline/gpu/decals", test_gpu_decals);
	g_test_add_func ("/render-pipeline/gpu/ssao", test_gpu_ssao);
	g_test_add_func ("/render-pipeline/gpu/environment", test_gpu_environment);
	g_test_add_func ("/render-pipeline/gpu/post", test_gpu_post);
	g_test_add_func ("/render-pipeline/gpu/debug-views", test_gpu_debug_views);
	g_test_add_func ("/render-pipeline/gpu/world-look", test_gpu_world_look);
	g_test_add_func ("/render-pipeline/gpu/spot", test_gpu_spot);
	g_test_add_func ("/render-pipeline/gpu/probe-per-item", test_gpu_probe_per_item);
	g_test_add_func ("/render-pipeline/gpu/timing", test_gpu_timing);

	result = g_test_run ();
	if (gpu_ready)
		CloseWindow ();
	return result;
}
