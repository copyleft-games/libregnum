/* test-render-layers.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for render layers: the named layer registry, camera cull masks
 * and the active mask they set, the layers of static batch chunks,
 * billboard batches, terrain chunks and particle systems, the shadow
 * renderer's caster mask, and (with a display) that a drawable on a
 * layer the camera leaves out draws no pixel.
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

	if ((display == NULL || display[0] == '\0') &&
	    (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-render-layers-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

#define SKIP_WITHOUT_GL() \
	G_STMT_START { \
		if (!graphics_available) \
		{ \
			g_test_skip ("Graphics context not available"); \
			return; \
		} \
	} G_STMT_END

/* ==========================================================================
 * A camera whose begin/end draw nothing, for headless mask tests
 * ========================================================================== */

#define TEST_TYPE_CAMERA (test_camera_get_type ())
G_DECLARE_FINAL_TYPE (TestCamera, test_camera, TEST, CAMERA, LrgCamera)

struct _TestCamera
{
	LrgCamera parent_instance;
	guint     begins;
	guint32   mask_seen;
};

G_DEFINE_TYPE (TestCamera, test_camera, LRG_TYPE_CAMERA)

static void
test_camera_begin (LrgCamera *camera)
{
	TestCamera *self = TEST_CAMERA (camera);

	/* what a draw inside the camera would see */
	self->begins++;
	self->mask_seen = lrg_render_layers_get_active_mask ();
}

static void
test_camera_end (LrgCamera *camera)
{
	(void)camera;
}

static void
test_camera_class_init (TestCameraClass *klass)
{
	LRG_CAMERA_CLASS (klass)->begin = test_camera_begin;
	LRG_CAMERA_CLASS (klass)->end = test_camera_end;
}

static void
test_camera_init (TestCamera *self)
{
	(void)self;
}

/* ==========================================================================
 * Registry and masks
 * ========================================================================== */

static void
test_registry (void)
{
	const gchar *names[] = { "default", "water", NULL };
	const gchar *bad[] = { "ui", "no-such-layer", NULL };
	guint32      mask = 0;

	lrg_render_layers_reset_names ();
	g_assert_cmpstr (lrg_render_layers_get_name (LRG_RENDER_LAYERS_DEFAULT), ==, "default");
	g_assert_cmpstr (lrg_render_layers_get_name (LRG_RENDER_LAYERS_UI), ==, "ui");
	g_assert_cmpstr (lrg_render_layers_get_name (LRG_RENDER_LAYERS_EDITOR_ONLY), ==, "editor-only");
	g_assert_cmpstr (lrg_render_layers_get_name (LRG_RENDER_LAYERS_IGNORE_RAYCAST), ==,
	                 "ignore-raycast");
	g_assert_cmpstr (lrg_render_layers_get_name (LRG_RENDER_LAYERS_TRANSPARENT_FX), ==,
	                 "transparent-fx");
	g_assert_cmpstr (lrg_render_layers_get_name (LRG_RENDER_LAYERS_WATER), ==, "water");
	g_assert_cmpstr (lrg_render_layers_get_name (LRG_RENDER_LAYERS_SKY), ==, "sky");
	g_assert_null (lrg_render_layers_get_name (7));
	g_assert_null (lrg_render_layers_get_name (LRG_RENDER_LAYERS_COUNT));
	g_assert_cmpint (lrg_render_layers_lookup ("editor-only"), ==, 2);
	g_assert_cmpint (lrg_render_layers_lookup ("minimap"), ==, -1);

	/* custom names, interned (stable pointers) */
	lrg_render_layers_set_name (12, "minimap");
	g_assert_cmpint (lrg_render_layers_lookup ("minimap"), ==, 12);
	g_assert_true (lrg_render_layers_get_name (12) == g_intern_string ("minimap"));
	lrg_render_layers_set_name (LRG_RENDER_LAYERS_WATER, "ocean");
	g_assert_cmpint (lrg_render_layers_lookup ("water"), ==, -1);
	g_assert_cmpint (lrg_render_layers_lookup ("ocean"), ==, 5);
	/* NULL restores the built-in name */
	lrg_render_layers_set_name (LRG_RENDER_LAYERS_WATER, NULL);
	g_assert_cmpstr (lrg_render_layers_get_name (5), ==, "water");

	g_assert_true (lrg_render_layers_mask_from_names (names, &mask));
	g_assert_cmpuint (mask, ==, LRG_RENDER_LAYERS_BIT (0) | LRG_RENDER_LAYERS_BIT (5));
	g_assert_false (lrg_render_layers_mask_from_names (bad, &mask));
	g_assert_cmpuint (mask, ==, LRG_RENDER_LAYERS_BIT (1));
	g_assert_true (lrg_render_layers_mask_from_names (NULL, &mask));
	g_assert_cmpuint (mask, ==, 0);

	lrg_render_layers_reset_names ();
	g_assert_null (lrg_render_layers_get_name (12));
	g_assert_cmpuint (LRG_RENDER_LAYERS_BIT (31), ==, 0x80000000u);
}

/* The active mask: all by default, set by a begun camera, restored by
 * its end. */
static void
test_camera_mask (void)
{
	g_autoptr(TestCamera)  camera = g_object_new (TEST_TYPE_CAMERA, NULL);
	g_autoptr(LrgCamera3D) cam3d = lrg_camera3d_new ();
	g_autoptr(LrgCamera2D) cam2d = lrg_camera2d_new ();
	guint                  prop = 0;

	g_assert_cmpuint (lrg_render_layers_get_active_mask (), ==, LRG_RENDER_LAYERS_MASK_ALL);
	g_assert_cmpuint (lrg_camera_get_cull_mask (LRG_CAMERA (camera)), ==,
	                  LRG_RENDER_LAYERS_MASK_ALL);
	g_assert_cmpuint (lrg_camera_get_cull_mask (LRG_CAMERA (cam3d)), ==,
	                  LRG_RENDER_LAYERS_MASK_ALL);
	g_assert_cmpuint (lrg_camera_get_cull_mask (LRG_CAMERA (cam2d)), ==,
	                  LRG_RENDER_LAYERS_MASK_ALL);
	g_assert_true (lrg_render_layers_is_visible (31));
	g_assert_false (lrg_render_layers_is_visible (32));

	/* property and per-layer toggles */
	lrg_camera_set_layer_visible (LRG_CAMERA (camera), LRG_RENDER_LAYERS_EDITOR_ONLY, FALSE);
	g_assert_false (lrg_camera_get_layer_visible (LRG_CAMERA (camera), 2));
	g_assert_true (lrg_camera_get_layer_visible (LRG_CAMERA (camera), 3));
	g_assert_false (lrg_camera_get_layer_visible (LRG_CAMERA (camera), 40));
	g_object_get (camera, "cull-mask", &prop, NULL);
	g_assert_cmpuint (prop, ==, ~LRG_RENDER_LAYERS_BIT (2));
	g_object_set (cam3d, "cull-mask", 0x5u, NULL);
	g_assert_cmpuint (lrg_camera_get_cull_mask (LRG_CAMERA (cam3d)), ==, 0x5u);

	/* begin makes it active; end restores */
	lrg_camera_begin (LRG_CAMERA (camera));
	g_assert_cmpuint (camera->mask_seen, ==, ~LRG_RENDER_LAYERS_BIT (2));
	g_assert_false (lrg_render_layers_is_visible (2));
	g_assert_true (lrg_render_layers_is_visible (0));
	/* a change while begun applies at once */
	lrg_camera_set_cull_mask (LRG_CAMERA (camera), LRG_RENDER_LAYERS_BIT (4));
	g_assert_cmpuint (lrg_render_layers_get_active_mask (), ==, LRG_RENDER_LAYERS_BIT (4));
	lrg_camera_end (LRG_CAMERA (camera));
	g_assert_cmpuint (lrg_render_layers_get_active_mask (), ==, LRG_RENDER_LAYERS_MASK_ALL);
	/* an unbalanced end leaves the mask alone */
	lrg_render_layers_set_active_mask (0x3u);
	lrg_camera_end (LRG_CAMERA (camera));
	g_assert_cmpuint (lrg_render_layers_get_active_mask (), ==, 0x3u);
	/* begin restores to what was active before it */
	lrg_camera_begin (LRG_CAMERA (camera));
	lrg_camera_end (LRG_CAMERA (camera));
	g_assert_cmpuint (lrg_render_layers_get_active_mask (), ==, 0x3u);
	g_assert_cmpuint (lrg_render_layers_set_active_mask (LRG_RENDER_LAYERS_MASK_ALL), ==, 0x3u);
	g_assert_cmpuint (camera->begins, ==, 2);
}

/* Drawables carry their layer and test it against the active mask. */
static void
test_drawable_layers (void)
{
	g_autoptr(LrgBillboardBatch) billboards = lrg_billboard_batch_new ();
	g_autoptr(LrgTerrain)        terrain = lrg_terrain_new (1, 1.0f);
	g_autoptr(LrgParticleSystem) particles = lrg_particle_system_new (8);
	g_autoptr(LrgStaticBatch)    batch = lrg_static_batch_new ();
	static const gfloat heights[4] = { 0, 0, 0, 0 };
	static const gfloat positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
	guint               layer = 99;

	/* billboard batch */
	g_assert_cmpuint (lrg_billboard_batch_get_layer (billboards), ==, 0);
	lrg_billboard_batch_set_layer (billboards, 9);
	g_assert_cmpuint (lrg_billboard_batch_get_layer (billboards), ==, 9);

	/* terrain: the terrain's layer, per-chunk overrides that survive a
	 * replacement and apply to chunks loaded later */
	g_assert_true (lrg_terrain_set_chunk (terrain, 0, 0, heights, 4, NULL, NULL, NULL));
	g_assert_cmpuint (lrg_terrain_get_layer (terrain), ==, 0);
	lrg_terrain_set_layer (terrain, 6);
	g_assert_cmpuint (lrg_terrain_get_chunk_layer (terrain, 0, 0), ==, 6);
	lrg_terrain_set_chunk_layer (terrain, 0, 0, 11);
	lrg_terrain_set_chunk_layer (terrain, 3, 3, 12);
	g_assert_cmpuint (lrg_terrain_get_chunk_layer (terrain, 0, 0), ==, 11);
	g_assert_true (lrg_terrain_set_chunk (terrain, 0, 0, heights, 4, NULL, NULL, NULL));
	g_assert_cmpuint (lrg_terrain_get_chunk_layer (terrain, 0, 0), ==, 11);
	g_assert_cmpuint (lrg_terrain_get_chunk_layer (terrain, 3, 3), ==, 12);
	lrg_terrain_set_chunk_layer (terrain, 0, 0, -1);
	g_assert_cmpuint (lrg_terrain_get_chunk_layer (terrain, 0, 0), ==, 6);

	/* particle system: a property, visibility follows the active mask */
	g_assert_cmpuint (lrg_particle_system_get_layer (particles), ==, 0);
	g_object_set (particles, "layer", (guint)LRG_RENDER_LAYERS_TRANSPARENT_FX, NULL);
	g_object_get (particles, "layer", &layer, NULL);
	g_assert_cmpuint (layer, ==, LRG_RENDER_LAYERS_TRANSPARENT_FX);
	g_assert_true (lrg_particle_system_is_layer_visible (particles));
	lrg_render_layers_set_active_mask (~LRG_RENDER_LAYERS_BIT (LRG_RENDER_LAYERS_TRANSPARENT_FX));
	g_assert_false (lrg_particle_system_is_layer_visible (particles));
	/* drawing a hidden system is a no-op even headless */
	lrg_particle_system_draw (particles);
	lrg_render_layers_set_active_mask (LRG_RENDER_LAYERS_MASK_ALL);
	g_assert_true (lrg_particle_system_is_layer_visible (particles));

	/* static batch chunk layers are render layers */
	g_assert_true (lrg_static_batch_add_mesh (batch, positions, NULL, NULL, NULL, 3, NULL, 0,
	                                          NULL, 0, 20, NULL));
	g_assert_cmpuint (lrg_static_batch_get_chunk_layer (batch, 0), ==, 20);
}

/* The shadow renderer's caster mask (the pass narrowing is checked in
 * /render-layers/gl/shadow-pass). */
static void
test_shadow_caster_mask (void)
{
	g_autoptr(LrgShadowRenderer) shadows = lrg_shadow_renderer_new ();

	g_assert_cmpuint (lrg_shadow_renderer_get_caster_mask (shadows), ==,
	                  LRG_RENDER_LAYERS_MASK_ALL);
	lrg_shadow_renderer_set_caster_mask (shadows, ~LRG_RENDER_LAYERS_BIT (LRG_RENDER_LAYERS_UI));
	g_assert_cmpuint (lrg_shadow_renderer_get_caster_mask (shadows), ==,
	                  ~LRG_RENDER_LAYERS_BIT (LRG_RENDER_LAYERS_UI));
	/* outside a pass the active mask is untouched */
	g_assert_cmpuint (lrg_render_layers_get_active_mask (), ==, LRG_RENDER_LAYERS_MASK_ALL);
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

#define TARGET (16)

/* ortho_camera: an orthographic camera at @eye looking at @at. */
static LrgCamera3D *
ortho_camera (gfloat ex, gfloat ey, gfloat ez,
              gfloat ax, gfloat ay, gfloat az,
              gfloat ux, gfloat uy, gfloat uz)
{
	LrgCamera3D *camera = lrg_camera3d_new ();

	lrg_camera3d_set_position_xyz (camera, ex, ey, ez);
	lrg_camera3d_set_target_xyz (camera, ax, ay, az);
	lrg_camera3d_set_up_xyz (camera, ux, uy, uz);
	lrg_camera3d_set_fovy (camera, 1.5f);
	lrg_camera3d_set_projection (camera, LRG_PROJECTION_ORTHOGRAPHIC);
	return camera;
}

typedef void (*DrawFunc) (gpointer drawable, LrgCamera3D *camera);

/* centre_brightness: draws @drawable through @camera (begun with
 * lrg_camera_begin(), so its cull mask is active) into a black target
 * and returns the centre pixel's red + green + blue. */
static gint
centre_brightness (LrgCamera3D *camera,
                   DrawFunc     draw,
                   gpointer     drawable)
{
	RenderTexture2D target = LoadRenderTexture (TARGET, TARGET);
	Image           image;
	Color           pixel;

	BeginTextureMode (target);
	ClearBackground (BLACK);
	lrg_camera_begin (LRG_CAMERA (camera));
	draw (drawable, camera);
	lrg_camera_end (LRG_CAMERA (camera));
	EndTextureMode ();
	image = LoadImageFromTexture (target.texture);
	pixel = GetImageColor (image, TARGET / 2, TARGET / 2);
	UnloadImage (image);
	UnloadRenderTexture (target);
	g_assert_cmpuint (lrg_render_layers_get_active_mask (), ==, LRG_RENDER_LAYERS_MASK_ALL);
	return (gint)pixel.r + (gint)pixel.g + (gint)pixel.b;
}

static void
draw_static (gpointer     drawable,
             LrgCamera3D *camera)
{
	(void)camera;
	lrg_static_batch_draw (LRG_STATIC_BATCH (drawable), LRG_RENDER_LAYERS_MASK_ALL);
}

static void
draw_static_chunk (gpointer     drawable,
                   LrgCamera3D *camera)
{
	(void)camera;
	lrg_static_batch_draw_chunk (LRG_STATIC_BATCH (drawable), 0);
}

static void
draw_billboards (gpointer     drawable,
                 LrgCamera3D *camera)
{
	g_autoptr(GrlVector3) eye = lrg_camera3d_get_position (camera);

	lrg_billboard_batch_draw (LRG_BILLBOARD_BATCH (drawable), eye);
}

static void
draw_terrain (gpointer     drawable,
              LrgCamera3D *camera)
{
	(void)camera;
	lrg_terrain_draw (LRG_TERRAIN (drawable));
}

static void
draw_terrain_chunk (gpointer     drawable,
                    LrgCamera3D *camera)
{
	(void)camera;
	lrg_terrain_draw_chunk (LRG_TERRAIN (drawable), 0, 0);
}

/* A static batch chunk on layer 3 vanishes when the camera leaves 3 out. */
static void
test_gl_static_batch (void)
{
	g_autoptr(LrgStaticBatch) batch = NULL;
	g_autoptr(LrgCamera3D)    camera = NULL;
	static const gfloat positions[12] = { -1, -1, 0, 2, -1, 0, 2, 2, 0, -1, 2, 0 };
	static const guint32 indices[6] = { 0, 1, 2, 0, 2, 3 };

	SKIP_WITHOUT_GL ();
	batch = lrg_static_batch_new ();
	camera = ortho_camera (0.5f, 0.5f, 2, 0.5f, 0.5f, 0, 0, 1, 0);
	g_assert_true (lrg_static_batch_add_mesh (batch, positions, NULL, NULL, NULL, 4, indices, 6,
	                                          NULL, 0, 3, NULL));
	g_assert_true (lrg_static_batch_upload (batch, NULL));

	g_assert_cmpint (centre_brightness (camera, draw_static, batch), >, 300);
	g_assert_cmpint (centre_brightness (camera, draw_static_chunk, batch), >, 300);
	lrg_camera_set_layer_visible (LRG_CAMERA (camera), 3, FALSE);
	g_assert_cmpint (centre_brightness (camera, draw_static, batch), <, 30);
	g_assert_cmpint (centre_brightness (camera, draw_static_chunk, batch), <, 30);
	lrg_camera_set_layer_visible (LRG_CAMERA (camera), 3, TRUE);
	g_assert_cmpint (centre_brightness (camera, draw_static, batch), >, 300);
	g_clear_object (&batch);
}

/* A billboard batch on layer 5 vanishes when the camera leaves 5 out. */
static void
test_gl_billboards (void)
{
	g_autoptr(LrgBillboardBatch) batch = NULL;
	g_autoptr(LrgCamera3D)       camera = NULL;
	g_autoptr(GrlImage)          image = NULL;
	g_autoptr(GrlTexture)        texture = NULL;
	GrlVector3                   pivot = { 0.5f, -1.0f, 0.0f };

	SKIP_WITHOUT_GL ();
	batch = lrg_billboard_batch_new ();
	camera = ortho_camera (0.5f, 0.5f, 2, 0.5f, 0.5f, 0, 0, 1, 0);
	image = grl_image_new_color (4, 4, &(GrlColor){ 255, 255, 255, 255 });
	texture = grl_texture_new_from_image (image);
	lrg_billboard_batch_set_material_texture (batch, 1, texture);
	lrg_billboard_batch_add (batch, 1, &pivot, 3.0f, 3.0f);
	lrg_billboard_batch_set_layer (batch, 5);

	g_assert_cmpint (centre_brightness (camera, draw_billboards, batch), >, 300);
	lrg_camera_set_cull_mask (LRG_CAMERA (camera), ~LRG_RENDER_LAYERS_BIT (5));
	g_assert_cmpint (centre_brightness (camera, draw_billboards, batch), <, 30);
	/* a layer the camera keeps */
	lrg_billboard_batch_set_layer (batch, 0);
	g_assert_cmpint (centre_brightness (camera, draw_billboards, batch), >, 300);
	g_clear_object (&batch);
}

/* A terrain chunk moved to layer 7 vanishes when the camera leaves 7
 * out; the terrain's own layer works the same. */
static void
test_gl_terrain (void)
{
	g_autoptr(LrgTerrain)  terrain = NULL;
	g_autoptr(LrgCamera3D) camera = NULL;
	static const gfloat heights[4] = { 0, 0, 0, 0 };

	SKIP_WITHOUT_GL ();
	terrain = lrg_terrain_new (1, 1.0f);
	camera = ortho_camera (0.5f, 2, 0.5f, 0.5f, 0, 0.5f, 0, 0, -1);
	g_assert_true (lrg_terrain_set_chunk (terrain, 0, 0, heights, 4, NULL, NULL, NULL));
	g_assert_true (lrg_terrain_upload (terrain, NULL));

	g_assert_cmpint (centre_brightness (camera, draw_terrain, terrain), >, 300);
	lrg_terrain_set_chunk_layer (terrain, 0, 0, 7);
	lrg_camera_set_layer_visible (LRG_CAMERA (camera), 7, FALSE);
	g_assert_cmpint (centre_brightness (camera, draw_terrain, terrain), <, 30);
	g_assert_cmpint (centre_brightness (camera, draw_terrain_chunk, terrain), <, 30);
	lrg_terrain_set_chunk_layer (terrain, 0, 0, -1);
	g_assert_cmpint (centre_brightness (camera, draw_terrain, terrain), >, 300);
	lrg_terrain_set_layer (terrain, 7);
	g_assert_cmpint (centre_brightness (camera, draw_terrain_chunk, terrain), <, 30);
	g_clear_object (&terrain);
}

/* A shadow pass narrows the active mask to the caster mask and its end
 * restores it. */
static void
test_gl_shadow_pass (void)
{
	g_autoptr(LrgLightSet)       lights = NULL;
	g_autoptr(LrgShadowRenderer) shadows = NULL;
	GrlVector3 sun = { 0.3f, -1.0f, 0.2f };
	GrlColor   white = { 255, 255, 255, 255 };
	gfloat     camera[3] = { 0, 5, 10 };
	gfloat     forward[3] = { 0, -0.4f, -0.9f };
	gfloat     up[3] = { 0, 1, 0 };
	guint32    casters = ~LRG_RENDER_LAYERS_BIT (LRG_RENDER_LAYERS_UI);

	SKIP_WITHOUT_GL ();
	lights = lrg_light_set_new ();
	shadows = lrg_shadow_renderer_new ();
	lrg_light_set_set_directional (lights, &sun, &white);
	lrg_shadow_renderer_set_sun (shadows, 256, 1, 30.0f, 0.5f);
	lrg_shadow_renderer_set_caster_mask (shadows, casters);
	g_assert_cmpuint (lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 60.0f,
	                                              1.0f, 0.1f), ==, 1);
	g_assert_true (lrg_shadow_renderer_begin_pass (shadows, 0));
	g_assert_cmpuint (lrg_render_layers_get_active_mask (), ==, casters);
	g_assert_false (lrg_render_layers_is_visible (LRG_RENDER_LAYERS_UI));
	/* a change mid-pass applies at once */
	lrg_shadow_renderer_set_caster_mask (shadows, LRG_RENDER_LAYERS_BIT (0));
	g_assert_cmpuint (lrg_render_layers_get_active_mask (), ==, LRG_RENDER_LAYERS_BIT (0));
	lrg_shadow_renderer_end_pass (shadows);
	g_assert_cmpuint (lrg_render_layers_get_active_mask (), ==, LRG_RENDER_LAYERS_MASK_ALL);
	lrg_shadow_renderer_release_gpu (shadows);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	graphics_available = init_graphics_context ();

	g_test_add_func ("/render-layers/registry", test_registry);
	g_test_add_func ("/render-layers/camera-mask", test_camera_mask);
	g_test_add_func ("/render-layers/drawables", test_drawable_layers);
	g_test_add_func ("/render-layers/shadow-caster-mask", test_shadow_caster_mask);
	g_test_add_func ("/render-layers/gl/static-batch", test_gl_static_batch);
	g_test_add_func ("/render-layers/gl/billboards", test_gl_billboards);
	g_test_add_func ("/render-layers/gl/terrain", test_gl_terrain);
	g_test_add_func ("/render-layers/gl/shadow-pass", test_gl_shadow_pass);

	return g_test_run ();
}
