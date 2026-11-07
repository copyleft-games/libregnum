/* test-ssao.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgDepthPrepass and LrgSsao.  Parameters, the kernel
 * and the noise are headless (determinism, hemisphere shape).  With a
 * display (run under the isolated Xvfb) a box on a floor is recorded
 * into the prepass and the occlusion pass must darken the crease where
 * the box meets the floor more than open floor, with prepass normals,
 * with normals rebuilt from depth, at half resolution with the blur,
 * and through the lit-shader hook lrg_ssao().
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-ssao-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Headless
 * ========================================================================== */

static void
test_ssao_defaults (void)
{
	g_autoptr(LrgSsao) ssao = lrg_ssao_new ();

	g_assert_cmpfloat_with_epsilon (lrg_ssao_get_radius (ssao), 0.5f, 1e-6);
	g_assert_cmpfloat_with_epsilon (lrg_ssao_get_bias (ssao), 0.025f, 1e-6);
	g_assert_cmpfloat_with_epsilon (lrg_ssao_get_intensity (ssao), 1.0f, 1e-6);
	g_assert_cmpfloat_with_epsilon (lrg_ssao_get_power (ssao), 1.5f, 1e-6);
	g_assert_cmpuint (lrg_ssao_get_kernel_size (ssao), ==, 16);
	g_assert_cmpuint (lrg_ssao_get_seed (ssao), ==, 1);
	g_assert_true (lrg_ssao_get_half_resolution (ssao));
	g_assert_true (lrg_ssao_get_blur (ssao));
	g_assert_false (lrg_ssao_has_result (ssao));
	g_assert_cmpuint (lrg_ssao_get_texture_id (ssao), ==, 0);
	g_assert_cmpint (lrg_ssao_get_width (ssao), ==, 0);

	/* clamping and properties */
	lrg_ssao_set_kernel_size (ssao, 1000);
	g_assert_cmpuint (lrg_ssao_get_kernel_size (ssao), ==, LRG_SSAO_MAX_KERNEL);
	lrg_ssao_set_kernel_size (ssao, 1);
	g_assert_cmpuint (lrg_ssao_get_kernel_size (ssao), ==, 4);
	g_object_set (ssao, "radius", 2.0f, "half-resolution", FALSE, "blur", FALSE, NULL);
	g_assert_cmpfloat_with_epsilon (lrg_ssao_get_radius (ssao), 2.0f, 1e-6);
	g_assert_false (lrg_ssao_get_half_resolution (ssao));
	g_assert_false (lrg_ssao_get_blur (ssao));
	lrg_ssao_set_bias (ssao, -1.0f);
	g_assert_cmpfloat (lrg_ssao_get_bias (ssao), ==, 0.0f);
	lrg_ssao_set_intensity (ssao, 2.0f);
	g_assert_cmpfloat_with_epsilon (lrg_ssao_get_intensity (ssao), 2.0f, 1e-6);
	lrg_ssao_set_power (ssao, 3.0f);
	g_assert_cmpfloat_with_epsilon (lrg_ssao_get_power (ssao), 3.0f, 1e-6);

	/* the glsl hook defines lrg_ssao() */
	g_assert_nonnull (strstr (lrg_ssao_get_glsl (), "float lrg_ssao()"));
}

static void
test_ssao_kernel_determinism (void)
{
	g_autoptr(LrgSsao) a = lrg_ssao_new ();
	g_autoptr(LrgSsao) b = lrg_ssao_new ();
	gfloat             ka[3], kb[3];
	guint              i;
	gboolean           differs = FALSE;
	gfloat             first_len = 0.0f;
	gfloat             last_len = 0.0f;

	lrg_ssao_set_kernel_size (a, 32);
	lrg_ssao_set_kernel_size (b, 32);

	/* same seed and size: identical kernels, all in the +Z hemisphere
	 * with lengths growing from 0.1 to 1 */
	for (i = 0; i < 32; i++)
	{
		gfloat len;

		lrg_ssao_get_kernel_sample (a, i, ka);
		lrg_ssao_get_kernel_sample (b, i, kb);
		g_assert_cmpmem (ka, sizeof ka, kb, sizeof kb);
		len = sqrtf (ka[0] * ka[0] + ka[1] * ka[1] + ka[2] * ka[2]);
		g_assert_cmpfloat (ka[2], >, 0.0f);
		g_assert_cmpfloat (len, <=, 1.0f + 1e-5f);
		g_assert_cmpfloat (ka[2] / len, >=, 0.15f - 1e-5f);
		if (i == 0)
			first_len = len;
		last_len = len;
	}
	g_assert_cmpfloat (first_len, <, 0.2f);
	g_assert_cmpfloat_with_epsilon (last_len, 1.0f, 1e-4);

	/* the noise: unit vectors in the tangent plane, same for same seed */
	for (i = 0; i < LRG_SSAO_NOISE_SIZE * LRG_SSAO_NOISE_SIZE; i++)
	{
		gfloat na[2], nb[2];

		lrg_ssao_get_noise_sample (a, i, na);
		lrg_ssao_get_noise_sample (b, i, nb);
		g_assert_cmpmem (na, sizeof na, nb, sizeof nb);
		g_assert_cmpfloat_with_epsilon (sqrtf (na[0] * na[0] + na[1] * na[1]), 1.0f, 1e-5);
	}

	/* another seed: another kernel; back to the old seed: the old one */
	lrg_ssao_get_kernel_sample (a, 5, ka);
	lrg_ssao_set_seed (b, 7);
	lrg_ssao_get_kernel_sample (b, 5, kb);
	for (i = 0; i < 3; i++)
		if (ka[i] != kb[i])
			differs = TRUE;
	g_assert_true (differs);
	lrg_ssao_set_seed (b, 1);
	lrg_ssao_get_kernel_sample (b, 5, kb);
	g_assert_cmpmem (ka, sizeof ka, kb, sizeof kb);
}

static void
test_depth_prepass_headless (void)
{
	g_autoptr(LrgDepthPrepass) prepass = lrg_depth_prepass_new (320, 200);
	gfloat                     m[16];

	g_assert_cmpint (lrg_depth_prepass_get_width (prepass), ==, 320);
	g_assert_cmpint (lrg_depth_prepass_get_height (prepass), ==, 200);
	g_assert_true (lrg_depth_prepass_get_normals (prepass));
	g_assert_false (lrg_depth_prepass_is_valid (prepass));
	g_assert_cmpuint (lrg_depth_prepass_get_depth_texture_id (prepass), ==, 0);
	g_assert_cmpuint (lrg_depth_prepass_get_normal_texture_id (prepass), ==, 0);

	lrg_depth_prepass_resize (prepass, 64, 32);
	g_assert_cmpint (lrg_depth_prepass_get_width (prepass), ==, 64);
	g_assert_cmpint (lrg_depth_prepass_get_height (prepass), ==, 32);
	lrg_depth_prepass_set_alpha_cutoff (prepass, 2.0f);
	g_assert_cmpfloat (lrg_depth_prepass_get_alpha_cutoff (prepass), ==, 1.0f);
	lrg_depth_prepass_set_normals (prepass, FALSE);
	g_assert_false (lrg_depth_prepass_get_normals (prepass));

	/* identity before any recording */
	lrg_depth_prepass_get_projection (prepass, m);
	g_assert_cmpfloat (m[0], ==, 1.0f);
	g_assert_cmpfloat (m[15], ==, 1.0f);
	lrg_depth_prepass_get_view (prepass, m);
	g_assert_cmpfloat (m[5], ==, 1.0f);
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

#define SIZE (128)

typedef struct
{
	GrlModel *floor;
	GrlModel *box;
	gfloat    box_transform[16];
} Scene;

/* draw_scene: the floor and the 2 x 2 x 2 box resting on it */
static void
draw_scene (LrgDepthPrepass *prepass,
            GrlShader       *shader,
            gpointer         user_data)
{
	Scene *scene = user_data;

	(void) shader;
	lrg_depth_prepass_draw_model (prepass, scene->floor, NULL);
	lrg_depth_prepass_draw_model (prepass, scene->box, scene->box_transform);
}

static void
scene_init (Scene *scene)
{
	g_autoptr(GrlMesh) floor_mesh = grl_mesh_new_plane (12.0f, 12.0f, 1, 1);
	g_autoptr(GrlMesh) box_mesh = grl_mesh_new_cube (2.0f, 2.0f, 2.0f);

	scene->floor = grl_model_new_from_mesh (floor_mesh);
	scene->box = grl_model_new_from_mesh (box_mesh);
	lrg_math3d_identity (scene->box_transform);
	scene->box_transform[13] = 1.0f;
}

static void
scene_clear (Scene *scene)
{
	g_clear_object (&scene->floor);
	g_clear_object (&scene->box);
}

static LrgCamera3D *
make_camera (void)
{
	LrgCamera3D *camera = lrg_camera3d_new ();

	lrg_camera3d_set_position_xyz (camera, 5.0f, 4.0f, 6.0f);
	lrg_camera3d_set_target_xyz (camera, 0.0f, 0.5f, 0.0f);
	lrg_camera3d_set_up_xyz (camera, 0.0f, 1.0f, 0.0f);
	lrg_camera3d_set_fovy (camera, 50.0f);
	return camera;
}

/* project: the GL pixel (bottom-left origin) of world point @p in the
 * prepass's last recording, for a target of @w x @h */
static void
project (LrgDepthPrepass *prepass,
         const gfloat    *p,
         gint             w,
         gint             h,
         gint            *out_x,
         gint            *out_y)
{
	gfloat proj[16], view[16], pv[16], clip[4];
	guint  r;

	lrg_depth_prepass_get_projection (prepass, proj);
	lrg_depth_prepass_get_view (prepass, view);
	lrg_math3d_multiply (proj, view, pv);
	for (r = 0; r < 4; r++)
		clip[r] = pv[r] * p[0] + pv[4 + r] * p[1] + pv[8 + r] * p[2] + pv[12 + r];
	*out_x = (gint) ((clip[0] / clip[3] * 0.5f + 0.5f) * (gfloat) w);
	*out_y = (gint) ((clip[1] / clip[3] * 0.5f + 0.5f) * (gfloat) h);
}

/* read_red: the red channel at GL pixel (@x, @y) of an RGBA8 texture */
static guint8
read_red (guint id,
          gint  w,
          gint  h,
          gint  x,
          gint  y)
{
	guint8 *px = rlReadTexturePixels (id, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	guint8  v;

	g_assert_nonnull (px);
	x = CLAMP (x, 0, w - 1);
	y = CLAMP (y, 0, h - 1);
	v = px[((gsize) y * (gsize) w + (gsize) x) * 4];
	RL_FREE (px);
	return v;
}

/* the floor right in front of the box's +X face (in the crease) and
 * open floor away from it, both visible from make_camera() */
static const gfloat crease_point[3] = { 1.08f, 0.0f, 0.3f };
static const gfloat open_point[3] = { 3.0f, 0.0f, 0.5f };

/* ao_at: the SSAO result at a world point */
static guint8
ao_at (LrgSsao         *ssao,
       LrgDepthPrepass *prepass,
       const gfloat    *p)
{
	gint w = lrg_ssao_get_width (ssao);
	gint h = lrg_ssao_get_height (ssao);
	gint x, y;

	project (prepass, p, w, h, &x, &y);
	return read_red (lrg_ssao_get_texture_id (ssao), w, h, x, y);
}

static void
test_depth_prepass_gpu (void)
{
	g_autoptr(LrgDepthPrepass) prepass = NULL;
	g_autoptr(LrgCamera3D)     camera = NULL;
	Scene                      scene;
	guint8                    *px;
	gint                       x, y;
	gfloat                     view[16];
	gfloat                     up_view[3];
	gfloat                     up[3] = { 0.0f, 1.0f, 0.0f };
	gfloat                     proj[16];
	gfloat                     d;

	if (!graphics_available)
	{
		g_test_skip ("No display available (headless environment)");
		return;
	}
	prepass = lrg_depth_prepass_new (SIZE, SIZE);
	if (lrg_depth_prepass_get_shader (prepass) == NULL)
	{
		g_test_skip ("GLSL 330 unavailable");
		return;
	}
	camera = make_camera ();
	scene_init (&scene);
	g_assert_true (lrg_depth_prepass_record (prepass, LRG_CAMERA (camera), draw_scene, &scene));
	g_assert_true (lrg_depth_prepass_is_valid (prepass));
	g_assert_cmpuint (lrg_depth_prepass_get_depth_texture_id (prepass), !=, 0);
	g_assert_cmpuint (lrg_depth_prepass_get_normal_texture_id (prepass), !=, 0);

	/* a perspective projection was recorded */
	lrg_depth_prepass_get_projection (prepass, proj);
	g_assert_cmpfloat (proj[11], ==, -1.0f);

	/* the floor's normal texel is the view-space up vector */
	lrg_depth_prepass_get_view (prepass, view);
	lrg_math3d_transform_direction (view, up, up_view);
	project (prepass, open_point, SIZE, SIZE, &x, &y);
	px = rlReadTexturePixels (lrg_depth_prepass_get_normal_texture_id (prepass), SIZE, SIZE,
	                          RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	g_assert_nonnull (px);
	{
		guint8 *t = px + ((gsize) y * SIZE + (gsize) x) * 4;
		guint   k;

		g_test_message ("normal texel at %d,%d: %u %u %u %u, expected %f %f %f", x, y, t[0], t[1], t[2], t[3],
		                up_view[0], up_view[1], up_view[2]);
		for (k = 0; k < 3; k++)
			g_assert_cmpfloat_with_epsilon ((gfloat) t[k] / 255.0f * 2.0f - 1.0f,
			                                up_view[k], 0.03);
	}
	RL_FREE (px);

	/* linearize_depth inverts the projection: a point d in front */
	d = 7.5f;
	{
		gfloat cz = proj[10] * -d + proj[14];
		gfloat cw = proj[11] * -d + proj[15];
		gfloat depth = (cz / cw) * 0.5f + 0.5f;

		g_assert_cmpfloat_with_epsilon (lrg_depth_prepass_linearize_depth (prepass, depth), d, 1e-2);
	}

	/* a resize invalidates until the next recording */
	lrg_depth_prepass_resize (prepass, SIZE / 2, SIZE / 2);
	g_assert_false (lrg_depth_prepass_is_valid (prepass));
	g_assert_true (lrg_depth_prepass_record (prepass, LRG_CAMERA (camera), draw_scene, &scene));
	g_assert_true (lrg_depth_prepass_is_valid (prepass));

	lrg_depth_prepass_release (prepass);
	g_assert_false (lrg_depth_prepass_is_valid (prepass));
	scene_clear (&scene);
}

/* run_crease_case: records the scene and checks the crease is darker
 * than open floor */
static void
run_crease_case (gboolean normals,
                 gboolean half,
                 gboolean blur)
{
	g_autoptr(LrgDepthPrepass) prepass = NULL;
	g_autoptr(LrgCamera3D)     camera = NULL;
	g_autoptr(LrgSsao)         ssao = NULL;
	Scene                      scene;
	guint8                     crease, open;

	if (!graphics_available)
	{
		g_test_skip ("No display available (headless environment)");
		return;
	}
	prepass = lrg_depth_prepass_new (SIZE, SIZE);
	if (lrg_depth_prepass_get_shader (prepass) == NULL)
	{
		g_test_skip ("GLSL 330 unavailable");
		return;
	}
	lrg_depth_prepass_set_normals (prepass, normals);
	camera = make_camera ();
	ssao = lrg_ssao_new ();
	lrg_ssao_set_half_resolution (ssao, half);
	lrg_ssao_set_blur (ssao, blur);
	lrg_ssao_set_radius (ssao, 0.6f);
	lrg_ssao_set_kernel_size (ssao, 32);

	/* no prepass recorded yet: nothing to do */
	g_assert_false (lrg_ssao_render (ssao, prepass));

	scene_init (&scene);
	g_assert_true (lrg_depth_prepass_record (prepass, LRG_CAMERA (camera), draw_scene, &scene));
	g_assert_true (lrg_ssao_render (ssao, prepass));
	g_assert_true (lrg_ssao_has_result (ssao));
	g_assert_cmpint (lrg_ssao_get_width (ssao), ==, half ? SIZE / 2 : SIZE);

	crease = ao_at (ssao, prepass, crease_point);
	open = ao_at (ssao, prepass, open_point);
	g_test_message ("normals %d half %d blur %d: crease %u, open %u", normals, half, blur,
	                crease, open);
	g_assert_cmpuint (open, >, 200);
	g_assert_cmpuint (crease + 40, <, open);

	lrg_ssao_release (ssao);
	g_assert_false (lrg_ssao_has_result (ssao));
	lrg_depth_prepass_release (prepass);
	scene_clear (&scene);
}

static void
test_ssao_crease_normals (void)
{
	run_crease_case (TRUE, FALSE, FALSE);
}

static void
test_ssao_crease_depth_normals (void)
{
	run_crease_case (FALSE, FALSE, FALSE);
}

static void
test_ssao_crease_half_blur (void)
{
	run_crease_case (TRUE, TRUE, TRUE);
}

/* the hook test shader: outputs lrg_ssao() as grey */
static const gchar hook_vs[] =
	"#version 330\n"
	"in vec3 vertexPosition;\n"
	"uniform mat4 mvp;\n"
	"void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }\n";

static void
draw_with_shader (GrlModel     *model,
                  Shader        shader,
                  const gfloat *transform)
{
	Model   *m = grl_model_get_handle (model);
	Material mat = m->materials[0];
	Matrix   t;

	/* identity, lifted by the transform's Y translation */
	memset (&t, 0, sizeof t);
	t.m0 = t.m5 = t.m10 = t.m15 = 1.0f;
	if (transform != NULL)
		t.m13 = transform[13];
	mat.shader = shader;
	DrawMesh (m->meshes[0], mat, t);
}

static void
test_ssao_hook (void)
{
	g_autoptr(LrgDepthPrepass) prepass = NULL;
	g_autoptr(LrgCamera3D)     camera = NULL;
	g_autoptr(LrgSsao)         ssao = NULL;
	g_autofree gchar          *fs = NULL;
	Scene                      scene;
	Shader                     shader;
	RenderTexture2D            rt;
	gint                       next;
	gint                       x, y;
	guint8                     crease, open;

	if (!graphics_available)
	{
		g_test_skip ("No display available (headless environment)");
		return;
	}
	prepass = lrg_depth_prepass_new (SIZE, SIZE);
	if (lrg_depth_prepass_get_shader (prepass) == NULL)
	{
		g_test_skip ("GLSL 330 unavailable");
		return;
	}
	fs = g_strconcat ("#version 330\nout vec4 finalColor;\n", lrg_ssao_get_glsl (),
	                  "void main() { float a = lrg_ssao(); finalColor = vec4(a, a, a, 1.0); }\n",
	                  NULL);
	shader = LoadShaderFromMemory (hook_vs, fs);
	g_assert_cmpuint (shader.id, !=, rlGetShaderIdDefault ());

	camera = make_camera ();
	ssao = lrg_ssao_new ();
	lrg_ssao_set_half_resolution (ssao, TRUE);
	lrg_ssao_set_radius (ssao, 0.6f);
	scene_init (&scene);

	/* inactive: lrg_ssao() is 1 everywhere */
	lrg_ssao_bind_inactive (shader.id);
	g_assert_cmpint (lrg_ssao_bind (ssao, shader.id, LRG_LIGHT_SET_HOOK_UNIT), ==,
	                 LRG_LIGHT_SET_HOOK_UNIT);

	g_assert_true (lrg_depth_prepass_record (prepass, LRG_CAMERA (camera), draw_scene, &scene));
	g_assert_true (lrg_ssao_render (ssao, prepass));

	/* the lit pass: same camera, a target of the prepass's size */
	next = lrg_ssao_bind (ssao, shader.id, LRG_LIGHT_SET_HOOK_UNIT);
	if (next == LRG_LIGHT_SET_HOOK_UNIT)
	{
		g_test_skip ("no hook texture unit on this driver");
		UnloadShader (shader);
		scene_clear (&scene);
		return;
	}
	g_assert_cmpint (next, ==, LRG_LIGHT_SET_HOOK_UNIT + 1);
	rt = LoadRenderTexture (SIZE, SIZE);
	BeginTextureMode (rt);
	ClearBackground (WHITE);
	lrg_camera_begin (LRG_CAMERA (camera));
	draw_with_shader (scene.floor, shader, NULL);
	draw_with_shader (scene.box, shader, scene.box_transform);
	lrg_camera_end (LRG_CAMERA (camera));
	EndTextureMode ();

	project (prepass, crease_point, SIZE, SIZE, &x, &y);
	crease = read_red (rt.texture.id, SIZE, SIZE, x, y);
	project (prepass, open_point, SIZE, SIZE, &x, &y);
	open = read_red (rt.texture.id, SIZE, SIZE, x, y);
	g_test_message ("hook: crease %u, open %u", crease, open);
	g_assert_cmpuint (open, >, 200);
	g_assert_cmpuint (crease + 40, <, open);

	/* off again: the crease reads fully open */
	lrg_ssao_bind_inactive (shader.id);
	BeginTextureMode (rt);
	ClearBackground (WHITE);
	lrg_camera_begin (LRG_CAMERA (camera));
	draw_with_shader (scene.floor, shader, NULL);
	draw_with_shader (scene.box, shader, scene.box_transform);
	lrg_camera_end (LRG_CAMERA (camera));
	EndTextureMode ();
	project (prepass, crease_point, SIZE, SIZE, &x, &y);
	g_assert_cmpuint (read_red (rt.texture.id, SIZE, SIZE, x, y), ==, 255);

	UnloadRenderTexture (rt);
	UnloadShader (shader);
	lrg_ssao_release (ssao);
	lrg_depth_prepass_release (prepass);
	scene_clear (&scene);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/ssao/defaults", test_ssao_defaults);
	g_test_add_func ("/ssao/kernel-determinism", test_ssao_kernel_determinism);
	g_test_add_func ("/depth-prepass/headless", test_depth_prepass_headless);
	g_test_add_func ("/depth-prepass/gpu", test_depth_prepass_gpu);
	g_test_add_func ("/ssao/crease-normals", test_ssao_crease_normals);
	g_test_add_func ("/ssao/crease-depth-normals", test_ssao_crease_depth_normals);
	g_test_add_func ("/ssao/crease-half-blur", test_ssao_crease_half_blur);
	g_test_add_func ("/ssao/hook", test_ssao_hook);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
