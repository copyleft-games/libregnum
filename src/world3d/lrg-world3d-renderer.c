/* lrg-world3d-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWorld3DRenderer: the shared 3D renderer, extracted from the editor
 * viewport (lrg-editor-scene-renderer.c) so games and the editor draw
 * identically.
 *
 * A render takes an LrgRenderList (collected from an LrgWorld3D, or built
 * by the editor from its authoring nodes) and a view:
 *
 *   lights   the list's LrgLight3DDesc records -> LrgLightSet
 *   shadows  LrgShadowRenderer passes (cascades, cube faces), each one
 *            replaying the SHADOW_CASTER category with the caster shader
 *   clear    into the post image when post-processing runs
 *   3D       view matrices loaded straight into rlgl (the same ones
 *            picking uses), sky dome, "draw-background", OPAQUE (static
 *            batch first), CUTOUT, TRANSPARENT, "draw-overlay", debug draw
 *   2D       debug labels, the post chain into the original target
 *
 * Primitives are unit-sized meshes (the shapes the editor's picking
 * bounds describe) drawn with each item's world matrix; models, meshes and
 * textures come straight from the items (borrowed GrlModel / GrlMesh /
 * GrlTexture handles).  The renderer also keeps the model and texture
 * caches that turn asset strings into those handles.
 *
 * Without a graphics context (or with the "headless" property) the same
 * pass logic runs but every draw is only recorded: per-pass counts and the
 * entities in draw order, which is what the headless tests check.
 */

#include "config.h"

#include "lrg-world3d-renderer.h"
#include "../debug/lrg-profiler.h"
#include "../core/lrg-math3d.h"
#include "../graphics/lrg-camera.h"
#include "../graphics/lrg-camera3d.h"
#include "../graphics/lrg-static-batch.h"
#include "../scene/lrg-material3d.h"
#include "../postprocess/effects/lrg-fxaa.h"
#include "../postprocess/effects/lrg-bloom.h"
#include "../postprocess/effects/lrg-vignette.h"
#include "../postprocess/effects/lrg-color-grade.h"
#include "components/lrg-mesh-renderer-component.h"
#include "components/lrg-billboard-component.h"
#include "components/lrg-skinned-model-component.h"
#include "components/lrg-camera-component.h"
#include "components/lrg-decal-component.h"
#include "components/lrg-lod-component.h"
#include "../graphics/lrg-material.h"
#include "../graphics/lrg-depth-prepass.h"
#include "../graphics/lrg-lod-group.h"
#include "../graphics/lrg-shader-asset.h"
#include "../lighting/lrg-reflection-probe.h"
#include "../lighting/lrg-volumetric-fog.h"
#include "../postprocess/lrg-post-effect.h"
#include "../postprocess/effects/lrg-tonemap.h"
#include "../graphics/lrg-light-set-private.h"
#include "../lighting/lrg-environment-glsl-private.h"
#include "../lighting/lrg-baked-glsl-private.h"
#include "../graphics/lrg-uv-unwrap.h"
#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#define RENDER_LOG_DOMAIN "Libregnum-World3D-Render"

#define TORUS_MAJOR       (0.375f)
#define TORUS_MINOR       (0.125f)

#define DEFAULT_SUN_X     (0.5f)
#define DEFAULT_SUN_Y     (-0.75f)
#define DEFAULT_SUN_Z     (-0.45f)

#define CUTOUT_ALPHA      (0.5f)

/* raylib's MAX_MATERIAL_MAPS (its config.h is private): every Material
 * raylib allocates has this many maps */
#define RAYLIB_MATERIAL_MAPS (12)

/* ==========================================================================
 * Enum and boxed types (hand-written: the enums live in this header)
 * ========================================================================== */

GType
lrg_world3d_render_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_WORLD3D_RENDER_MODE_LIT, "LRG_WORLD3D_RENDER_MODE_LIT", "lit" },
			{ LRG_WORLD3D_RENDER_MODE_UNLIT, "LRG_WORLD3D_RENDER_MODE_UNLIT", "unlit" },
			{ LRG_WORLD3D_RENDER_MODE_WIREFRAME, "LRG_WORLD3D_RENDER_MODE_WIREFRAME", "wireframe" },
			{ LRG_WORLD3D_RENDER_MODE_NORMALS, "LRG_WORLD3D_RENDER_MODE_NORMALS", "normals" },
			{ LRG_WORLD3D_RENDER_MODE_LIGHTING_ONLY, "LRG_WORLD3D_RENDER_MODE_LIGHTING_ONLY", "lighting-only" },
			{ LRG_WORLD3D_RENDER_MODE_OVERDRAW, "LRG_WORLD3D_RENDER_MODE_OVERDRAW", "overdraw" },
			{ LRG_WORLD3D_RENDER_MODE_UV, "LRG_WORLD3D_RENDER_MODE_UV", "uv" },
			{ LRG_WORLD3D_RENDER_MODE_LIGHT_COMPLEXITY, "LRG_WORLD3D_RENDER_MODE_LIGHT_COMPLEXITY", "light-complexity" },
			{ LRG_WORLD3D_RENDER_MODE_AO, "LRG_WORLD3D_RENDER_MODE_AO", "ao" },
			{ LRG_WORLD3D_RENDER_MODE_REFLECTIONS, "LRG_WORLD3D_RENDER_MODE_REFLECTIONS", "reflections" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgWorld3DRenderMode"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_world3d_render_flags_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_WORLD3D_RENDER_FLAG_NONE, "LRG_WORLD3D_RENDER_FLAG_NONE", "none" },
			{ LRG_WORLD3D_RENDER_FLAG_SHADOWS, "LRG_WORLD3D_RENDER_FLAG_SHADOWS", "shadows" },
			{ LRG_WORLD3D_RENDER_FLAG_SKY, "LRG_WORLD3D_RENDER_FLAG_SKY", "sky" },
			{ LRG_WORLD3D_RENDER_FLAG_POST, "LRG_WORLD3D_RENDER_FLAG_POST", "post" },
			{ LRG_WORLD3D_RENDER_FLAG_CLEAR, "LRG_WORLD3D_RENDER_FLAG_CLEAR", "clear" },
			{ LRG_WORLD3D_RENDER_FLAG_DEBUG_DRAW, "LRG_WORLD3D_RENDER_FLAG_DEBUG_DRAW", "debug-draw" },
			{ LRG_WORLD3D_RENDER_FLAG_BATCH_STATIC, "LRG_WORLD3D_RENDER_FLAG_BATCH_STATIC", "batch-static" },
			{ 0, NULL, NULL }
		};
		GType id = g_flags_register_static (g_intern_static_string ("LrgWorld3DRenderFlags"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_world3d_render_pass_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_WORLD3D_RENDER_PASS_SHADOW, "LRG_WORLD3D_RENDER_PASS_SHADOW", "shadow" },
			{ LRG_WORLD3D_RENDER_PASS_SKY, "LRG_WORLD3D_RENDER_PASS_SKY", "sky" },
			{ LRG_WORLD3D_RENDER_PASS_OPAQUE, "LRG_WORLD3D_RENDER_PASS_OPAQUE", "opaque" },
			{ LRG_WORLD3D_RENDER_PASS_CUTOUT, "LRG_WORLD3D_RENDER_PASS_CUTOUT", "cutout" },
			{ LRG_WORLD3D_RENDER_PASS_TRANSPARENT, "LRG_WORLD3D_RENDER_PASS_TRANSPARENT", "transparent" },
			{ LRG_WORLD3D_RENDER_PASS_POST, "LRG_WORLD3D_RENDER_PASS_POST", "post" },
			{ LRG_WORLD3D_RENDER_PASS_PREPASS, "LRG_WORLD3D_RENDER_PASS_PREPASS", "prepass" },
			{ LRG_WORLD3D_RENDER_PASS_SSAO, "LRG_WORLD3D_RENDER_PASS_SSAO", "ssao" },
			{ LRG_WORLD3D_RENDER_PASS_VOLUMETRIC, "LRG_WORLD3D_RENDER_PASS_VOLUMETRIC", "volumetric" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgWorld3DRenderPass"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

G_DEFINE_BOXED_TYPE (LrgWorld3DView, lrg_world3d_view, lrg_world3d_view_copy, lrg_world3d_view_free)

/* ==========================================================================
 * Instance structure
 * ========================================================================== */

/*
 * The scene image of one post processor: an RGBA16F render texture (RGBA8
 * when the driver cannot render to half floats) the colour passes draw
 * into; lrg_post_processor_process() runs the chain over it.  Kept as
 * qdata on the processor so every viewport's processor has its own.
 */
typedef struct
{
	RenderTexture2D scene;
	gint            width;
	gint            height;
	gboolean        hdr;
} PostTargets;

struct _LrgWorld3DRenderer
{
	GObject                  parent_instance;

	/* Settings */
	LrgWorld3DRenderMode     mode;
	LrgWorld3DRenderFlags    flags;
	GrlColor                 background;
	GrlColor                 ambient;
	gboolean                 fallback_sun;
	gboolean                 headless;
	LrgPostProcessor        *post;
	LrgDebugDraw            *debug_draw;

	/* Asset resolution and caches */
	gchar                   *base_dir;
	LrgWorld3DAssetResolver  resolver;
	gpointer                 resolver_data;
	GDestroyNotify           resolver_destroy;
	GHashTable              *models;            /* abs path -> GrlModel*, NULL = failed */
	GHashTable              *textures;          /* abs path -> GrlTexture*, NULL = failed */
	GHashTable              *model_by_asset;    /* asset string -> borrowed GrlModel* */
	GHashTable              *texture_by_asset;  /* asset string -> borrowed GrlTexture* */
	GHashTable              *materials;         /* abs path -> LrgMaterial*, NULL = failed */
	GHashTable              *material_by_asset; /* asset string -> borrowed LrgMaterial* */
	guint                    generation;

	/* GPU resources, valid while has_gl */
	gboolean                 has_gl;
	Material                 material;
	Shader                   flat;       /* rlgl's default shader */
	Texture2D                white;
	Mesh                     cube;
	Mesh                     sphere;
	Mesh                     plane;
	Mesh                     cylinder;
	Mesh                     cone;
	Mesh                     torus;
	Mesh                     disc;
	Shader                   lit;
	gboolean                 has_lit;
	gint                     loc_light;
	gint                     loc_ambient;
	Shader                   unlit;
	gboolean                 has_unlit;
	Shader                   normals;
	gboolean                 has_normals;
	GSList                  *post_owners;   /* processors carrying PostTargets (weak) */

	/* Debug view shaders (overdraw and UV on the scene vertex shader;
	 * light complexity, AO and reflections on the lit shader library) */
	Shader                   overdraw;
	gboolean                 has_overdraw;
	Shader                   uv;
	gboolean                 has_uv;
	GrlShader               *dbg_lit[3];    /* complexity, AO, reflections; NULL until used */
	gboolean                 dbg_tried[3];
	gboolean                 env_fog;       /* the last frame's environment set the fog */
	guint                    m3d_shader;    /* program the LrgMaterial3D locations belong to */
	gint                     m3d_loc_pbr;
	gint                     m3d_loc_emission;

	/* What the current pass draws with */
	gboolean                 gpu;
	Shader                   cur_shader;
	gboolean                 white_albedo;
	gfloat                   cur_view[16];
	LrgMaterial             *cur_mat;       /* the drawn item's LrgMaterial (borrowed), or NULL */
	GHashTable              *faded;         /* shader ids whose lodFade is not 0 */
	guint32                  cur_layers;    /* draw layers last uploaded to the light set */
	LrgEnvironment          *probe_env;     /* environment whose probes are picked per item (borrowed) */
	gpointer                 probe_pick[3]; /* probes + program bound for the last item */

	/* Engine lighting */
	LrgLightSet             *lights;
	LrgShadowRenderer       *shadows;
	LrgSkyDome              *sky;
	gboolean                 scene_sun;
	gfloat                   sun_dir[3];

	/* Optional static batching */
	LrgStaticBatch          *batch;
	guint64                  batch_signature;
	guint                    batch_items;
	GrlShader               *batch_shader;   /* borrowed: the light-set shader it was set to */

	/* The list render()/render_view() collect into */
	LrgRenderList           *list;

	/* The 0.9.0 look: environment (sky, IBL, probes, fog), post stack,
	 * depth prepass + SSAO, decals, volumetric fog */
	LrgEnvironment          *environment;       /* own override (ref) */
	LrgEnvironment          *active_env;        /* what the last render used (ref) */
	LrgWorld3D              *frame_world;       /* borrowed while a world renders */
	LrgPostStack            *post_stack;        /* own override (ref) */
	LrgEnvironment          *fallback_env;      /* below the world's (ref) */
	LrgPostStack            *fallback_stack;    /* below the world's (ref) */
	LrgPostProcessor        *stack_post;        /* the stack in effect, built */
	gboolean                 ssao_enabled;
	gboolean                 decals_enabled;
	gboolean                 ibl_enabled;
	gboolean                 volumetric_enabled;
	LrgSsao                 *ssao;
	LrgDepthPrepass         *prepass;
	LrgDecalSet             *decals;
	LrgVolumetricFog        *vfog;
	LrgCamera               *view_camera;       /* begins a LrgWorld3DView for the prepass */
	GHashTable              *custom_shaders;    /* abs .rshader path -> LrgShaderAsset, NULL = failed */
	GHashTable              *lightmap_prims;    /* "prim:params" -> LightmapMesh (0.9.0 M3 uv2 fix) */
	gfloat                   cur_proj[16];

	/* Per-pass timing (lrg_world3d_renderer_set_timing()) */
	gboolean                 timing;
	LrgRenderList           *list_in_flight;    /* the list the prepass replays */
	gdouble                  pass_time[LRG_WORLD3D_RENDER_PASS_COUNT];

	/* Statistics of the last render */
	guint                    drawn;
	guint                    pass_counts[LRG_WORLD3D_RENDER_PASS_COUNT];
	GPtrArray               *records[LRG_WORLD3D_RENDER_PASS_COUNT];
	guint                    shadow_passes;
	guint                    batched;

	/* Frame debugger (0.9.0).  capture is reffed; replay_rt is ours. */
	LrgFrameCapture         *capture;
	LrgRenderList           *last_list;     /* borrowed: the list of the last render */
	LrgWorld3DView           last_view;
	gboolean                 have_view;
	gboolean                 in_render;
	RenderTexture2D          replay_rt;
};

enum
{
	PROP_0,
	PROP_RENDER_MODE,
	PROP_FLAGS,
	PROP_POST_PROCESSOR,
	PROP_DEBUG_DRAW,
	PROP_BASE_DIR,
	PROP_HEADLESS,
	PROP_FALLBACK_SUN,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_DRAW_BACKGROUND,
	SIGNAL_DRAW_OVERLAY,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgWorld3DRenderer, lrg_world3d_renderer, G_TYPE_OBJECT)

static GQuark
post_targets_quark (void)
{
	return g_quark_from_static_string ("lrg-world3d-renderer-post-targets");
}

/* ==========================================================================
 * LrgWorld3DView
 * ========================================================================== */

void
lrg_world3d_view_init (LrgWorld3DView *view,
                       gfloat          width,
                       gfloat          height)
{
	g_return_if_fail (view != NULL);

	memset (view, 0, sizeof *view);
	view->position[0] = 8.0f;
	view->position[1] = 6.0f;
	view->position[2] = 8.0f;
	view->up[1] = 1.0f;
	view->fovy = 45.0f;
	view->ortho_height = 10.0f;
	view->orthographic = FALSE;
	view->near_plane = 0.05f;
	view->far_plane = 2000.0f;
	view->width = MAX (width, 1.0f);
	view->height = MAX (height, 1.0f);
	view->cull_mask = LRG_RENDER_LAYERS_MASK_ALL;
}

void
lrg_world3d_view_init_from_camera (LrgWorld3DView *view,
                                   LrgCamera3D    *camera,
                                   gfloat          width,
                                   gfloat          height)
{
	GrlVector3            eye, target;
	g_autoptr(GrlVector3) up = NULL;

	g_return_if_fail (view != NULL);
	g_return_if_fail (LRG_IS_CAMERA3D (camera));

	lrg_world3d_view_init (view, width, height);

	/* The drawn (interpolated) pose, as lrg_camera_begin() uses it */
	lrg_camera3d_get_render_pose (camera, &eye, &target);
	up = lrg_camera3d_get_up (camera);
	view->position[0] = eye.x;
	view->position[1] = eye.y;
	view->position[2] = eye.z;
	view->target[0] = target.x;
	view->target[1] = target.y;
	view->target[2] = target.z;
	if (up != NULL)
	{
		view->up[0] = up->x;
		view->up[1] = up->y;
		view->up[2] = up->z;
	}

	/* raylib reads fovy as the ortho height for orthographic cameras */
	view->orthographic = (lrg_camera3d_get_projection (camera) == LRG_PROJECTION_ORTHOGRAPHIC);
	view->fovy = lrg_camera3d_get_fovy (camera);
	view->ortho_height = view->fovy;
	view->near_plane = (gfloat) rlGetCullDistanceNear ();
	view->far_plane = (gfloat) rlGetCullDistanceFar ();

	/* A camera component's clip planes win over rlgl's defaults */
	{
		LrgCameraComponent *owner = lrg_camera_component_from_camera (LRG_CAMERA (camera));

		if (owner != NULL)
		{
			view->near_plane = lrg_camera_component_get_near (owner);
			view->far_plane = lrg_camera_component_get_far (owner);
		}
	}
	view->cull_mask = lrg_camera_get_cull_mask (LRG_CAMERA (camera));
}

LrgWorld3DView *
lrg_world3d_view_copy (const LrgWorld3DView *view)
{
	g_return_val_if_fail (view != NULL, NULL);

	return (LrgWorld3DView *) g_memdup2 (view, sizeof *view);
}

void
lrg_world3d_view_free (LrgWorld3DView *view)
{
	g_free (view);
}

void
lrg_world3d_view_get_view_matrix (const LrgWorld3DView *view,
                                  gfloat               *out)
{
	g_return_if_fail (view != NULL);
	g_return_if_fail (out != NULL);

	lrg_math3d_look_at (view->position, view->target, view->up, out);
}

void
lrg_world3d_view_get_projection_matrix (const LrgWorld3DView *view,
                                        gfloat               *out)
{
	gfloat n, f, aspect;

	g_return_if_fail (view != NULL);
	g_return_if_fail (out != NULL);

	/* Sanitise the clip range exactly like the editor's camera does, so
	 * the editor's picking and this projection agree pixel for pixel. */
	n = view->near_plane > 0.0f ? view->near_plane : 0.05f;
	f = view->far_plane > n ? view->far_plane : n + 1000.0f;
	aspect = (view->height > 0.0f) ? view->width / view->height : 1.0f;

	if (view->orthographic)
	{
		gfloat top = view->ortho_height * 0.5f;
		gfloat right = top * aspect;

		lrg_math3d_ortho (-right, right, -top, top, n, f, out);
	}
	else
		lrg_math3d_perspective (view->fovy * (gfloat) G_PI / 180.0f, aspect, n, f, out);
}

/* ==========================================================================
 * Small math helpers (raymath.h is not gnu89-clean)
 * ========================================================================== */

static Matrix
to_matrix (const gfloat *m)
{
	Matrix out;

	/* Column-major like OpenGL; raylib's fields are named by that same
	 * storage index. */
	out.m0 = m[0];   out.m1 = m[1];   out.m2 = m[2];   out.m3 = m[3];
	out.m4 = m[4];   out.m5 = m[5];   out.m6 = m[6];   out.m7 = m[7];
	out.m8 = m[8];   out.m9 = m[9];   out.m10 = m[10]; out.m11 = m[11];
	out.m12 = m[12]; out.m13 = m[13]; out.m14 = m[14]; out.m15 = m[15];
	return out;
}

static void
from_matrix (Matrix  m,
             gfloat *out)
{
	out[0] = m.m0;   out[1] = m.m1;   out[2] = m.m2;   out[3] = m.m3;
	out[4] = m.m4;   out[5] = m.m5;   out[6] = m.m6;   out[7] = m.m7;
	out[8] = m.m8;   out[9] = m.m9;   out[10] = m.m10; out[11] = m.m11;
	out[12] = m.m12; out[13] = m.m13; out[14] = m.m14; out[15] = m.m15;
}

/* raylib convention: mat_mul (a, b) applies a first, then b. */
static Matrix
mat_mul (Matrix a,
         Matrix b)
{
	gfloat x[16], y[16], o[16];

	from_matrix (a, x);
	from_matrix (b, y);
	lrg_math3d_multiply (y, x, o);
	return to_matrix (o);
}

static Matrix
mat_compose (gfloat tx,
             gfloat ty,
             gfloat tz,
             gfloat rx,
             gfloat sx,
             gfloat sy,
             gfloat sz)
{
	gfloat loc[3], rot[3], scl[3];
	gfloat m[16];

	loc[0] = tx;
	loc[1] = ty;
	loc[2] = tz;
	rot[0] = rx;
	rot[1] = 0.0f;
	rot[2] = 0.0f;
	scl[0] = sx;
	scl[1] = sy;
	scl[2] = sz;
	lrg_math3d_compose (loc, rot, scl, m);
	return to_matrix (m);
}

static Matrix
mat_translate (gfloat x,
               gfloat y,
               gfloat z)
{
	return mat_compose (x, y, z, 0.0f, 1.0f, 1.0f, 1.0f);
}

static Matrix
mat_rotate_x (gfloat angle)
{
	return mat_compose (0.0f, 0.0f, 0.0f, angle, 1.0f, 1.0f, 1.0f);
}

static Matrix
mat_scale (gfloat x,
           gfloat y,
           gfloat z)
{
	return mat_compose (0.0f, 0.0f, 0.0f, 0.0f, x, y, z);
}

static void
normalize3 (gfloat *v)
{
	gfloat len = sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

	if (len < 1e-6f)
		return;
	v[0] /= len;
	v[1] /= len;
	v[2] /= len;
}

/* 0..1 to 0..255, truncating like the editor always did; the tiny bias
 * makes byte colours stored as n / 255.0 come back as n. */
static guchar
unit_to_byte (gfloat v)
{
	return (guchar) MIN (CLAMP (v, 0.0f, 1.0f) * 255.0f + 0.001f, 255.0f);
}

/* ==========================================================================
 * Shaders (the readable copies are data/editor/shaders/<name>.glsl)
 * ========================================================================== */

/*
 * The render-mode shaders.  data/editor/shaders/<name>.glsl holds the
 * readable copies; these literals are generated from them byte for byte
 * (tests/test-editor-app-viewport.c checks they match), so an installed
 * engine works without data files.  They are named after the editor,
 * where they started.
 */
static const gchar shader_scene_vs[] =
	"#version 330\n"
	"/* lrgeditor scene vertex shader (lit, unlit and normals modes).\n"
	" * raylib attribute and uniform names, so DrawMesh() feeds it. */\n"
	"in vec3 vertexPosition;\n"
	"in vec2 vertexTexCoord;\n"
	"in vec3 vertexNormal;\n"
	"uniform mat4 mvp;\n"
	"uniform mat4 matNormal;\n"
	"out vec2 fragTexCoord;\n"
	"out vec3 fragNormal;\n"
	"void main ()\n"
	"{\n"
	"    fragTexCoord = vertexTexCoord;\n"
	"    fragNormal = normalize (vec3 (matNormal * vec4 (vertexNormal, 0.0)));\n"
	"    gl_Position = mvp * vec4 (vertexPosition, 1.0);\n"
	"}\n"
;

static const gchar shader_lit_fs[] =
	"#version 330\n"
	"/* lrgeditor fallback lit shader: one two-sided directional light plus\n"
	" * ambient. Used when the engine light set's shader is unavailable. */\n"
	"in vec2 fragTexCoord;\n"
	"in vec3 fragNormal;\n"
	"uniform sampler2D texture0;\n"
	"uniform vec4 colDiffuse;\n"
	"uniform vec3 lightDir;\n"
	"uniform float ambient;\n"
	"out vec4 finalColor;\n"
	"void main ()\n"
	"{\n"
	"    vec4 base = texture (texture0, fragTexCoord) * colDiffuse;\n"
	"    float d = abs (dot (normalize (fragNormal), -lightDir));\n"
	"    float l = ambient + (1.0 - ambient) * d;\n"
	"    if (base.a < 0.01) discard;\n"
	"    finalColor = vec4 (base.rgb * l, base.a);\n"
	"}\n"
;

static const gchar shader_unlit_fs[] =
	"#version 330\n"
	"/* lrgeditor unlit mode: flat albedo (texture times material colour). */\n"
	"in vec2 fragTexCoord;\n"
	"in vec3 fragNormal;\n"
	"uniform sampler2D texture0;\n"
	"uniform vec4 colDiffuse;\n"
	"out vec4 finalColor;\n"
	"void main ()\n"
	"{\n"
	"    vec4 base = texture (texture0, fragTexCoord) * colDiffuse;\n"
	"    if (base.a < 0.01) discard;\n"
	"    finalColor = base;\n"
	"}\n"
;

static const gchar shader_normals_fs[] =
	"#version 330\n"
	"/* lrgeditor normals mode: world-space normals as colours\n"
	" * (x -> red, y -> green, z -> blue, remapped from -1..1 to 0..1). */\n"
	"in vec2 fragTexCoord;\n"
	"in vec3 fragNormal;\n"
	"out vec4 finalColor;\n"
	"void main ()\n"
	"{\n"
	"    vec3 n = normalize (fragNormal);\n"
	"    finalColor = vec4 (n * 0.5 + 0.5, 1.0);\n"
	"}\n"
;

/* Overdraw view: every fragment adds a dim orange (additive blending, no
 * depth test), so the brightness counts the layers. */
static const gchar shader_overdraw_fs[] =
	"#version 330\n"
	"out vec4 finalColor;\n"
	"void main ()\n"
	"{\n"
	"    finalColor = vec4 (0.10, 0.045, 0.015, 1.0);\n"
	"}\n"
;

/* UV view: U in red, V in green (wrapped), over an 8 x 8 checker that
 * shows texel density, stretching and seams. */
static const gchar shader_uv_fs[] =
	"#version 330\n"
	"in vec2 fragTexCoord;\n"
	"in vec3 fragNormal;\n"
	"out vec4 finalColor;\n"
	"void main ()\n"
	"{\n"
	"    vec2 f = fract (fragTexCoord);\n"
	"    float checker = mod (floor (f.x * 8.0) + floor (f.y * 8.0), 2.0);\n"
	"    finalColor = vec4 (vec3 (f, 0.15) * (0.7 + 0.3 * checker), 1.0);\n"
	"}\n"
;

/* Head of the lit-library debug views: raylib's varyings as the light
 * set's vertex shader writes them (the library follows). */
static const gchar shader_dbg_head[] =
	"#version 330\n"
	"in vec3 fragPosition;\n"
	"in vec2 fragTexCoord;\n"
	"in vec4 fragColor;\n"
	"in vec3 fragNormal;\n"
	"out vec4 finalColor;\n"
;

/* Light complexity: the point and spot lights whose range reaches the
 * fragment, walked like lrg_light_pbr_ex() does (grid cell or the active
 * list), mapped onto a ramp: none, 1, 2-3, 4-7, 8 and more. */
static const gchar shader_dbg_complexity_main[] =
	"vec3 lrg_dbg_ramp(float n) {\n"
	"    if (n < 0.5) return vec3(0.04, 0.04, 0.06);\n"
	"    if (n < 1.5) return vec3(0.10, 0.25, 0.95);\n"
	"    if (n < 3.5) return vec3(0.15, 0.80, 0.25);\n"
	"    if (n < 7.5) return vec3(0.95, 0.85, 0.15);\n"
	"    return vec3(0.95, 0.15, 0.10);\n"
	"}\n"
	"void main() {\n"
	"    vec3 p = fragPosition;\n"
	"    float n = 0.0;\n"
	"    if (useGrid == 1) {\n"
	"        ivec2 base;\n"
	"        if (lrg_cluster_base(p, base)) {\n"
	"            int per = int(gridLayers.w + 0.5) / 4;\n"
	"            bool done = false;\n"
	"            for (int t = 0; t < 8 && !done; t++) {\n"
	"                if (t >= per) break;\n"
	"                vec4 ids = texelFetch(lightGrid, base + ivec2(t, 0), 0);\n"
	"                for (int j = 0; j < 4; j++) {\n"
	"                    if (ids[j] < 0.0) { done = true; break; }\n"
	"                    vec4 pr = texelFetch(lightData, ivec2(0, int(ids[j] + 0.5)), 0);\n"
	"                    if (length(pr.xyz - p) < pr.w) n += 1.0;\n"
	"                }\n"
	"            }\n"
	"        }\n"
	"    } else {\n"
	"        for (int i = 0; i < 16; i++) {\n"
	"            if (i >= lightCount) break;\n"
	"            if (length(lightPosition[i] - p) < lightRadius[i]) n += 1.0;\n"
	"        }\n"
	"    }\n"
	"    finalColor = vec4(lrg_dbg_ramp(n), 1.0);\n"
	"}\n"
;

/* Ambient occlusion: the material's occlusion times the SSAO term. */
static const gchar shader_dbg_ao_main[] =
	"void main() {\n"
	"    float nlen = length(fragNormal);\n"
	"    vec3 ng = nlen > 0.0 ? fragNormal / nlen : vec3(0.0, 1.0, 0.0);\n"
	"    LrgSurface s = lrg_surface(fragPosition, ng, fragTexCoord, fragColor);\n"
	"    if (s.albedo.a < alphaCutoff) discard;\n"
	"    float ao = clamp(s.occlusion * lrg_ssao(), 0.0, 1.0);\n"
	"    finalColor = vec4(vec3(ao), 1.0);\n"
	"}\n"
;

/* Reflections: the indirect specular radiance the surface receives at
 * its own roughness (environment, blended probes), without Fresnel. */
static const gchar shader_dbg_reflections_main[] =
	"void main() {\n"
	"    float nlen = length(fragNormal);\n"
	"    vec3 ng = nlen > 0.0 ? fragNormal / nlen : vec3(0.0, 1.0, 0.0);\n"
	"    LrgSurface s = lrg_surface(fragPosition, ng, fragTexCoord, fragColor);\n"
	"    if (s.albedo.a < alphaCutoff) discard;\n"
	"    vec3 dv = mapCamera - fragPosition;\n"
	"    vec3 v = dot(dv, dv) > 1e-10 ? normalize(dv) : ng;\n"
	"    vec3 c = vec3(0.0);\n"
	"    if (lrg_env_active()) {\n"
	"        lrg_env_set_point(fragPosition);\n"
	"        c = lrg_env_specular(s.normal, v, s.roughness);\n"
	"    }\n"
	"    finalColor = vec4(lrg_to_display(c), 1.0);\n"
	"}\n"
;

static const struct
{
	const gchar *name;
	const gchar *source;
} shader_sources[] = {
	{ "scene.vs", shader_scene_vs },
	{ "lit.fs", shader_lit_fs },
	{ "unlit.fs", shader_unlit_fs },
	{ "normals.fs", shader_normals_fs },
};

/* A shader that compiled to something other than rlgl's default one. */
static gboolean
load_shader (const gchar *vs,
             const gchar *fs,
             Shader      *out)
{
	*out = LoadShaderFromMemory (vs, fs);
	if (IsShaderValid (*out) && out->id != rlGetShaderIdDefault ())
		return TRUE;
	memset (out, 0, sizeof *out);
	return FALSE;
}

/* ==========================================================================
 * GPU setup and teardown
 * ========================================================================== */

/* A torus around +Y, sized to the editor's picking bounds. */
static Mesh
gen_torus (gint major_segments,
           gint minor_segments)
{
	Mesh mesh;
	gint i, j, v = 0, t = 0;

	memset (&mesh, 0, sizeof mesh);
	mesh.vertexCount = (major_segments + 1) * (minor_segments + 1);
	mesh.triangleCount = major_segments * minor_segments * 2;
	mesh.vertices = (gfloat *) MemAlloc ((guint) mesh.vertexCount * 3 * sizeof (gfloat));
	mesh.normals = (gfloat *) MemAlloc ((guint) mesh.vertexCount * 3 * sizeof (gfloat));
	mesh.texcoords = (gfloat *) MemAlloc ((guint) mesh.vertexCount * 2 * sizeof (gfloat));
	mesh.indices = (gushort *) MemAlloc ((guint) mesh.triangleCount * 3 * sizeof (gushort));

	/* Vertices: a ring of minor circles around the major circle. */
	for (i = 0; i <= major_segments; i++)
	{
		gfloat u = (gfloat) i / (gfloat) major_segments * 2.0f * (gfloat) G_PI;

		for (j = 0; j <= minor_segments; j++)
		{
			gfloat w = (gfloat) j / (gfloat) minor_segments * 2.0f * (gfloat) G_PI;
			gfloat nx = cosf (w) * cosf (u);
			gfloat ny = sinf (w);
			gfloat nz = cosf (w) * sinf (u);

			mesh.vertices[v * 3 + 0] = (TORUS_MAJOR + TORUS_MINOR * cosf (w)) * cosf (u);
			mesh.vertices[v * 3 + 1] = TORUS_MINOR * ny;
			mesh.vertices[v * 3 + 2] = (TORUS_MAJOR + TORUS_MINOR * cosf (w)) * sinf (u);
			mesh.normals[v * 3 + 0] = nx;
			mesh.normals[v * 3 + 1] = ny;
			mesh.normals[v * 3 + 2] = nz;
			mesh.texcoords[v * 2 + 0] = (gfloat) i / (gfloat) major_segments;
			mesh.texcoords[v * 2 + 1] = (gfloat) j / (gfloat) minor_segments;
			v++;
		}
	}

	/* Two triangles per quad of the grid. */
	for (i = 0; i < major_segments; i++)
		for (j = 0; j < minor_segments; j++)
		{
			gushort a = (gushort) (i * (minor_segments + 1) + j);
			gushort b = (gushort) ((i + 1) * (minor_segments + 1) + j);

			mesh.indices[t++] = a;
			mesh.indices[t++] = (gushort) (a + 1);
			mesh.indices[t++] = b;
			mesh.indices[t++] = b;
			mesh.indices[t++] = (gushort) (a + 1);
			mesh.indices[t++] = (gushort) (b + 1);
		}

	UploadMesh (&mesh, FALSE);
	return mesh;
}

/*
 * Creates the primitive meshes, the default material and the render-mode
 * shaders on first use.  Returns FALSE without a window (nothing can be
 * created then).
 */
static gboolean
ensure_gl (LrgWorld3DRenderer *self)
{
	if (self->has_gl)
		return TRUE;
	if (!IsWindowReady ())
		return FALSE;

	self->cube = GenMeshCube (1.0f, 1.0f, 1.0f);
	self->sphere = GenMeshSphere (0.5f, 16, 24);
	self->plane = GenMeshPlane (1.0f, 1.0f, 1, 1);
	self->cylinder = GenMeshCylinder (0.5f, 1.0f, 24);
	self->cone = GenMeshCone (0.5f, 1.0f, 24);
	self->torus = gen_torus (32, 12);
	self->disc = GenMeshPoly (32, 0.5f);

	self->material = LoadMaterialDefault ();
	self->white = self->material.maps[MATERIAL_MAP_DIFFUSE].texture;
	self->flat = self->material.shader;
	self->cur_shader = self->flat;

	/* The shaders are optional: without GL 3.3 everything is flat
	 * (raylib's default shader). */
	self->has_lit = load_shader (shader_scene_vs, shader_lit_fs, &self->lit);
	if (self->has_lit)
	{
		self->loc_light = GetShaderLocation (self->lit, "lightDir");
		self->loc_ambient = GetShaderLocation (self->lit, "ambient");
		self->cur_shader = self->lit;
	}
	self->has_unlit = load_shader (shader_scene_vs, shader_unlit_fs, &self->unlit);
	self->has_normals = load_shader (shader_scene_vs, shader_normals_fs, &self->normals);
	self->has_overdraw = load_shader (shader_scene_vs, shader_overdraw_fs, &self->overdraw);
	self->has_uv = load_shader (shader_scene_vs, shader_uv_fs, &self->uv);

	self->has_gl = TRUE;
	return TRUE;
}

/* Frees one processor's post images (GPU only with a live context). */
static void
post_targets_free (gpointer data)
{
	PostTargets *t = (PostTargets *) data;

	if (t == NULL)
		return;
	if (IsWindowReady () && t->scene.id != 0)
		UnloadRenderTexture (t->scene);
	g_free (t);
}

/* A processor that carries post images died: forget it. */
static void
post_owner_gone (gpointer  data,
                 GObject  *where_the_object_was)
{
	LrgWorld3DRenderer *self = LRG_WORLD3D_RENDERER (data);

	self->post_owners = g_slist_remove (self->post_owners, where_the_object_was);
}

/* Drops the post images of every processor this renderer drew with. */
static void
release_post_targets (LrgWorld3DRenderer *self)
{
	while (self->post_owners != NULL)
	{
		GObject *owner = (GObject *) self->post_owners->data;

		self->post_owners = g_slist_delete_link (self->post_owners, self->post_owners);
		g_object_weak_unref (owner, post_owner_gone, self);
		g_object_set_qdata (owner, post_targets_quark (), NULL);
	}
}

/*
 * Empties a cache of GrlModel / GrlTexture objects.  Their finalizers
 * unload GPU memory, which crashes without a context: then the objects
 * are leaked on purpose (the driver reclaimed the memory with the
 * context; this only happens when the window closed before release).
 */
static void
clear_object_cache (GHashTable *cache,
                    gboolean    gl)
{
	GHashTableIter iter;
	gpointer       value;

	if (cache == NULL)
		return;
	if (!gl)
	{
		g_hash_table_iter_init (&iter, cache);
		while (g_hash_table_iter_next (&iter, NULL, &value))
			g_hash_table_iter_steal (&iter);
	}
	g_hash_table_remove_all (cache);
}

static void release_debug_shaders (LrgWorld3DRenderer *self,
                                   gboolean            gl);

/*
 * Frees the GPU resources.  With @gl FALSE (no context any more) only the
 * CPU-side bookkeeping is dropped: GL calls without a context crash.
 */
static void
release_resources (LrgWorld3DRenderer *self,
                   gboolean            gl)
{
	/* The by-asset maps borrow their values: drop them first. */
	if (self->model_by_asset != NULL)
		g_hash_table_remove_all (self->model_by_asset);
	if (self->texture_by_asset != NULL)
		g_hash_table_remove_all (self->texture_by_asset);
	if (self->material_by_asset != NULL)
		g_hash_table_remove_all (self->material_by_asset);
	clear_object_cache (self->models, gl);
	clear_object_cache (self->textures, gl);
	clear_object_cache (self->materials, gl);   /* they hold their textures */

	/* The static batch keeps CPU data; only its buffers go */
	if (self->batch != NULL && gl)
		lrg_static_batch_unload (self->batch);
	self->batch_signature = 0;
	self->batch_shader = NULL;

	if (gl)
		release_post_targets (self);
	if (gl && self->replay_rt.id != 0)
	{
		UnloadRenderTexture (self->replay_rt);
		memset (&self->replay_rt, 0, sizeof self->replay_rt);
	}

	/* The 0.9.0 passes: prepass, SSAO, decal textures, volumetric fog,
	 * the stack's processor, the environment maps, custom and debug
	 * programs (each recreated on the next use) */
	release_debug_shaders (self, gl);
	if (gl)
	{
		lrg_depth_prepass_release (self->prepass);
		lrg_ssao_release (self->ssao);
		lrg_decal_set_release (self->decals);
		lrg_volumetric_fog_release_gpu (self->vfog);
		if (self->stack_post != NULL)
			lrg_post_processor_release (self->stack_post);
		if (self->environment != NULL)
			lrg_environment_release_gpu (self->environment);
		if (self->active_env != NULL)
			lrg_environment_release_gpu (self->active_env);
	}
	g_clear_object (&self->active_env);
	clear_object_cache (self->custom_shaders, gl);
	/* the unwrapped lightmap primitives: GPU buffers with a context,
	 * CPU arrays only otherwise (lightmap_mesh_free() checks) */
	if (self->lightmap_prims != NULL)
		g_hash_table_remove_all (self->lightmap_prims);
	if (self->faded != NULL)
		g_hash_table_remove_all (self->faded);
	self->m3d_shader = 0;

	/* Engine objects: the shadow atlases, and a fresh light set (its
	 * shader belongs to the old context) */
	if (gl && self->shadows != NULL)
		lrg_shadow_renderer_release_gpu (self->shadows);
	if (self->lights != NULL)
		lrg_light_set_set_shadow_renderer (self->lights, NULL);
	g_clear_object (&self->lights);
	self->lights = lrg_light_set_new ();

	if (!self->has_gl)
		return;

	if (gl)
	{
		UnloadMesh (self->cube);
		UnloadMesh (self->sphere);
		UnloadMesh (self->plane);
		UnloadMesh (self->cylinder);
		UnloadMesh (self->cone);
		UnloadMesh (self->torus);
		UnloadMesh (self->disc);
		if (self->has_lit)
			UnloadShader (self->lit);
		if (self->has_unlit)
			UnloadShader (self->unlit);
		if (self->has_normals)
			UnloadShader (self->normals);
		if (self->has_overdraw)
			UnloadShader (self->overdraw);
		if (self->has_uv)
			UnloadShader (self->uv);
	}
	/* The default material's shader and texture belong to rlgl; only its
	 * map array is ours (plain CPU memory). */
	MemFree (self->material.maps);
	memset (&self->material, 0, sizeof self->material);
	self->has_lit = FALSE;
	self->has_unlit = FALSE;
	self->has_normals = FALSE;
	self->has_overdraw = FALSE;
	self->has_uv = FALSE;
	self->has_gl = FALSE;
}

/* ==========================================================================
 * Asset caches
 * ========================================================================== */

/* Default resolution: absolute paths unchanged, relative ones against the
 * base directory (or the working directory). */
static gchar *
resolve_path (LrgWorld3DRenderer *self,
              const gchar        *asset)
{
	if (self->resolver != NULL)
	{
		gchar *resolved = self->resolver (asset, self->resolver_data);

		if (resolved != NULL)
			return resolved;
	}
	if (g_path_is_absolute (asset))
		return g_strdup (asset);
	if (self->base_dir != NULL)
		return g_build_filename (self->base_dir, asset, NULL);
	return g_canonicalize_filename (asset, NULL);
}

/* Loads (or finds) the model at @abs; NULL on failure, cached either way. */
static GrlModel *
load_model_abs (LrgWorld3DRenderer *self,
                const gchar        *abs)
{
	g_autoptr(GError) error = NULL;
	GrlModel         *model;

	if (g_hash_table_contains (self->models, abs))
		return (GrlModel *) g_hash_table_lookup (self->models, abs);
	if (!IsWindowReady ())
		return NULL;   /* not cached: a window may come later */

	/* Failures are cached too, and reported once. */
	if (!g_file_test (abs, G_FILE_TEST_IS_REGULAR))
	{
		g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Model not found: %s", abs);
		g_hash_table_insert (self->models, g_strdup (abs), NULL);
		return NULL;
	}
	model = grl_model_new_from_file (abs, &error);
	if (model == NULL)
		g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Could not load model %s", abs);
	g_hash_table_insert (self->models, g_strdup (abs), model);
	return model;
}

/* As load_model_abs(), for images (.pep through graylib's decoder). */
static GrlTexture *
load_texture_abs (LrgWorld3DRenderer *self,
                  const gchar        *abs)
{
	GrlTexture *tex = NULL;

	if (g_hash_table_contains (self->textures, abs))
		return (GrlTexture *) g_hash_table_lookup (self->textures, abs);
	if (!IsWindowReady ())
		return NULL;

	if (g_file_test (abs, G_FILE_TEST_IS_REGULAR))
	{
		tex = grl_texture_new_from_file (abs);
		if (tex != NULL && !grl_texture_is_valid (tex))
			g_clear_object (&tex);
	}
	if (tex == NULL)
		g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Could not load texture %s", abs);
	g_hash_table_insert (self->textures, g_strdup (abs), tex);
	return tex;
}

/* ==========================================================================
 * Statistics
 * ========================================================================== */

static void
stats_reset (LrgWorld3DRenderer *self)
{
	guint i;

	self->drawn = 0;
	self->shadow_passes = 0;
	self->batched = 0;
	for (i = 0; i < LRG_WORLD3D_RENDER_PASS_COUNT; i++)
	{
		self->pass_counts[i] = 0;
		g_ptr_array_set_size (self->records[i], 0);
	}
}

/* One item drawn (or recorded) in @pass. */
static void
record_item (LrgWorld3DRenderer   *self,
             LrgWorld3DRenderPass  pass,
             const LrgRenderItem  *item)
{
	self->pass_counts[pass]++;
	g_ptr_array_add (self->records[pass], item != NULL ? item->entity : NULL);
}

/* ==========================================================================
 * Lights
 * ========================================================================== */

/* A light colour (0..1 floats) times @scale, clamped to 8 bits. */
static GrlColor
light_color (const LrgLight3DDesc *light,
             gfloat                scale)
{
	GrlColor c;

	c.r = (guint8) CLAMP (light->color[0] * scale * 255.0f, 0.0f, 255.0f);
	c.g = (guint8) CLAMP (light->color[1] * scale * 255.0f, 0.0f, 255.0f);
	c.b = (guint8) CLAMP (light->color[2] * scale * 255.0f, 0.0f, 255.0f);
	c.a = 255;
	return c;
}

typedef struct
{
	guint    count;
	gboolean have_sun;
} LightSync;

static void
sync_begin (LrgWorld3DRenderer *self,
            LightSync          *ls)
{
	lrg_light_set_clear_point_lights (self->lights);
	lrg_light_set_set_ambient (self->lights, &self->ambient);
	ls->count = 0;
	ls->have_sun = FALSE;
}

/* Adds one light: the first directional light is the sun, points and
 * spots go through lrg_light_set_add_light_desc(). */
static void
sync_add (LrgWorld3DRenderer   *self,
          LightSync            *ls,
          const LrgLight3DDesc *light)
{
	if (light == NULL || !light->enabled)
		return;

	if (light->kind == LRG_LIGHT3D_DIRECTIONAL)
	{
		GrlVector3 dir;
		GrlColor   c;

		if (ls->have_sun)
			return;
		c = light_color (light, light->intensity);
		self->sun_dir[0] = light->direction[0];
		self->sun_dir[1] = light->direction[1];
		self->sun_dir[2] = light->direction[2];
		normalize3 (self->sun_dir);
		dir.x = self->sun_dir[0];
		dir.y = self->sun_dir[1];
		dir.z = self->sun_dir[2];
		lrg_light_set_set_directional (self->lights, &dir, &c);
		ls->have_sun = TRUE;
		ls->count++;
	}
	else
	{
		/* Point and spot lights through the light set's description
		 * path: cones, spot shadows, layer masks, shadow priority and
		 * (from a light component source) the shadow bias */
		if (lrg_light_set_add_light_desc (self->lights, light) != LRG_LIGHT_SET_INVALID_LIGHT)
			ls->count++;
	}
}

static guint
sync_end (LrgWorld3DRenderer *self,
          LightSync          *ls)
{
	/* No directional light: the default sun keeps the scene readable */
	if (!ls->have_sun)
	{
		GrlVector3 dir;
		GrlColor   sun = { 214, 206, 192, 255 };
		GrlColor   none = { 0, 0, 0, 255 };

		self->sun_dir[0] = DEFAULT_SUN_X;
		self->sun_dir[1] = DEFAULT_SUN_Y;
		self->sun_dir[2] = DEFAULT_SUN_Z;
		normalize3 (self->sun_dir);
		dir.x = self->sun_dir[0];
		dir.y = self->sun_dir[1];
		dir.z = self->sun_dir[2];
		lrg_light_set_set_directional (self->lights, &dir, self->fallback_sun ? &sun : &none);
		/* the default sun is never baked (a removed baked sun's mode stays otherwise) */
		lrg_light_set_set_directional_mode (self->lights, LRG_LIGHT_MODE_REALTIME);
	}
	self->scene_sun = ls->have_sun;
	return ls->count;
}

static void
sync_from_list (LrgWorld3DRenderer *self,
                LrgRenderList      *list)
{
	LightSync ls;
	guint     i, n;

	sync_begin (self, &ls);
	n = lrg_render_list_get_n_lights (list);
	for (i = 0; i < n; i++)
		sync_add (self, &ls, lrg_render_list_get_light (list, i));
	sync_end (self, &ls);
}

/* ==========================================================================
 * Render targets
 * ========================================================================== */

/*
 * Binds framebuffer @id (a render texture of @w x @h) as BeginTextureMode()
 * would; 0 returns to the screen.  raylib's texture modes do not nest, so
 * switching between the caller's target, the post images and the shadow
 * passes goes through here; the caller's own EndTextureMode() closes
 * whichever is bound last, which is always its target again.
 */
static void
enter_target (guint id,
              gint  w,
              gint  h)
{
	RenderTexture2D rt;

	if (id == 0)
	{
		EndTextureMode ();
		return;
	}
	memset (&rt, 0, sizeof rt);
	rt.id = id;
	rt.texture.width = w;
	rt.texture.height = h;
	BeginTextureMode (rt);
}

/* The shader the colour passes draw with for @mode (NULL light-set
 * shader: the fallbacks, then rlgl's default shader). */
static Shader
mode_shader (LrgWorld3DRenderer   *self,
             LrgWorld3DRenderMode  mode,
             GrlShader            *light_shader)
{
	switch (mode)
	{
	case LRG_WORLD3D_RENDER_MODE_UNLIT:
	case LRG_WORLD3D_RENDER_MODE_WIREFRAME:
		return self->has_unlit ? self->unlit : self->flat;
	case LRG_WORLD3D_RENDER_MODE_NORMALS:
		return self->has_normals ? self->normals : self->flat;
	case LRG_WORLD3D_RENDER_MODE_OVERDRAW:
		return self->has_overdraw ? self->overdraw : self->flat;
	case LRG_WORLD3D_RENDER_MODE_UV:
		return self->has_uv ? self->uv : self->flat;
	case LRG_WORLD3D_RENDER_MODE_LIT:
	case LRG_WORLD3D_RENDER_MODE_LIGHTING_ONLY:
	default:
		if (light_shader != NULL)
			return *(Shader *) grl_shader_get_handle (light_shader);
		return self->has_lit ? self->lit : self->flat;
	}
}

/* ==========================================================================
 * Item drawing
 * ========================================================================== */

/*
 * The item's colour.  Components already fold their material's colour
 * into it (a mesh renderer copies its LrgMaterial's base colour, or its
 * own colour that also feeds its scalar LrgMaterial3D), so it is used as
 * is: multiplying the material colour in again squared it.
 */
static Color
item_color (const LrgRenderItem *item)
{
	Color c;

	c.r = unit_to_byte (item->color[0]);
	c.g = unit_to_byte (item->color[1]);
	c.b = unit_to_byte (item->color[2]);
	c.a = unit_to_byte (item->color[3]);
	return c;
}

/* The raylib texture of an item's GrlTexture, or NULL.  A billboard
 * component that left the texture to us has its texture path resolved
 * through the cache. */
static GrlTexture *lrg_world3d_renderer_get_texture_internal (LrgWorld3DRenderer *self,
                                                              const gchar        *asset);

static Texture2D *
item_texture (LrgWorld3DRenderer  *self,
              const LrgRenderItem *item)
{
	GrlTexture *tex = NULL;

	if (item->texture != NULL && GRL_IS_TEXTURE (item->texture))
		tex = GRL_TEXTURE (item->texture);
	else if (item->texture == NULL && item->component != NULL && LRG_IS_BILLBOARD_COMPONENT (item->component))
		tex = lrg_world3d_renderer_get_texture_internal (
			self, lrg_billboard_component_get_texture_path (LRG_BILLBOARD_COMPONENT (item->component)));
	if (tex == NULL)
		return NULL;
	return (Texture2D *) grl_texture_get_handle (tex);
}

static void
draw_mesh (LrgWorld3DRenderer *self,
           Mesh                mesh,
           Matrix              transform,
           Color               color,
           Texture2D          *texture)
{
	Material    m = self->material;
	MaterialMap maps[RAYLIB_MATERIAL_MAPS];

	/* Lighting-only mode lights a white surface */
	if (self->white_albedo)
	{
		color = WHITE;
		texture = NULL;
	}

	/* An LrgMaterial's maps on a copy of the default ones (DrawMesh()
	 * binds unit i to map i, the units lrg_material_apply() pointed the
	 * samplers at); the albedo map stays unless the item overrides it */
	if (self->cur_mat != NULL && !self->white_albedo)
	{
		memcpy (maps, self->material.maps, sizeof maps);
		maps[MATERIAL_MAP_DIFFUSE].texture = self->white;
		lrg_material_fill_raylib_maps (self->cur_mat, maps);
		m.maps = maps;
		if (texture != NULL)
			m.maps[MATERIAL_MAP_DIFFUSE].texture = *texture;
	}
	else
		m.maps[MATERIAL_MAP_DIFFUSE].texture = (texture != NULL) ? *texture : self->white;
	m.shader = self->cur_shader;
	m.maps[MATERIAL_MAP_DIFFUSE].color = color;
	DrawMesh (mesh, m, transform);
	self->drawn++;
}

/* Lines drawn in an item's local space (rlgl immediate mode). */
static void
push_local (Matrix m)
{
	gfloat f[16];

	from_matrix (m, f);
	rlPushMatrix ();
	rlMultMatrixf (f);
}

/*
 * The placement of a unit primitive: its size from the item params
 * (0 = the unit size), applied before the world matrix.  Unit shapes:
 * a 1x1x1 cube, spheres and discs of radius 0.5, cylinders and cones of
 * radius 0.5 and height 1, a 1x1 plane, a torus of major radius 0.375.
 */
static Matrix
primitive_matrix (const LrgRenderItem *item)
{
	const gfloat *p = item->params;
	Matrix        m = to_matrix (item->world_matrix);
	gfloat        sx = 1.0f, sy = 1.0f, sz = 1.0f;

	switch (item->primitive)
	{
	case LRG_PRIMITIVE_CUBE:
		sx = p[0] > 0.0f ? p[0] : 1.0f;
		sy = p[1] > 0.0f ? p[1] : 1.0f;
		sz = p[2] > 0.0f ? p[2] : 1.0f;
		break;
	case LRG_PRIMITIVE_UV_SPHERE:
	case LRG_PRIMITIVE_ICO_SPHERE:
		if (p[0] > 0.0f)
			sx = sy = sz = p[0] * 2.0f;
		break;
	case LRG_PRIMITIVE_CYLINDER:
	case LRG_PRIMITIVE_CONE:
		if (p[0] > 0.0f)
			sx = sz = p[0] * 2.0f;
		if (p[1] > 0.0f)
			sy = p[1];
		break;
	case LRG_PRIMITIVE_PLANE:
	case LRG_PRIMITIVE_RECTANGLE_2D:
		sx = p[0] > 0.0f ? p[0] : 1.0f;
		sz = p[1] > 0.0f ? p[1] : 1.0f;
		break;
	case LRG_PRIMITIVE_CIRCLE:
	case LRG_PRIMITIVE_CIRCLE_2D:
		if (p[0] > 0.0f)
			sx = sz = p[0] * 2.0f;
		break;
	case LRG_PRIMITIVE_TORUS:
		if (p[0] > 0.0f)
			sx = sy = sz = p[0] / TORUS_MAJOR;
		break;
	default:
		break;
	}
	/* The identity case keeps the world matrix bit for bit */
	if (sx != 1.0f || sy != 1.0f || sz != 1.0f)
		m = mat_mul (mat_scale (sx, sy, sz), m);
	return m;
}

/* Whether a primitive is a solid mesh (shadow casters, batching). */
static gboolean
primitive_is_solid (LrgPrimitiveType prim)
{
	switch (prim)
	{
	case LRG_PRIMITIVE_CUBE:
	case LRG_PRIMITIVE_UV_SPHERE:
	case LRG_PRIMITIVE_ICO_SPHERE:
	case LRG_PRIMITIVE_PLANE:
	case LRG_PRIMITIVE_CIRCLE:
	case LRG_PRIMITIVE_CYLINDER:
	case LRG_PRIMITIVE_CONE:
	case LRG_PRIMITIVE_TORUS:
	case LRG_PRIMITIVE_RECTANGLE_2D:
	case LRG_PRIMITIVE_CIRCLE_2D:
		return TRUE;
	default:
		return FALSE;
	}
}

/*
 * The unit mesh of a solid primitive and the object-space placement it
 * needs (cylinders and cones stand on y = 0 in raylib; the 2D shapes
 * face +Z).  Returns FALSE for non-mesh primitives.
 */
static gboolean
primitive_mesh (LrgWorld3DRenderer *self,
                LrgPrimitiveType    prim,
                Mesh               *out_mesh,
                Matrix             *out_pre,
                gboolean           *out_has_pre)
{
	*out_has_pre = FALSE;
	switch (prim)
	{
	case LRG_PRIMITIVE_CUBE:
		*out_mesh = self->cube;
		return TRUE;
	case LRG_PRIMITIVE_UV_SPHERE:
	case LRG_PRIMITIVE_ICO_SPHERE:
		*out_mesh = self->sphere;
		return TRUE;
	case LRG_PRIMITIVE_PLANE:
		*out_mesh = self->plane;
		return TRUE;
	case LRG_PRIMITIVE_CIRCLE:
		*out_mesh = self->disc;
		return TRUE;
	case LRG_PRIMITIVE_CYLINDER:
		*out_mesh = self->cylinder;
		*out_pre = mat_translate (0.0f, -0.5f, 0.0f);
		*out_has_pre = TRUE;
		return TRUE;
	case LRG_PRIMITIVE_CONE:
		*out_mesh = self->cone;
		*out_pre = mat_translate (0.0f, -0.5f, 0.0f);
		*out_has_pre = TRUE;
		return TRUE;
	case LRG_PRIMITIVE_TORUS:
		*out_mesh = self->torus;
		return TRUE;
	case LRG_PRIMITIVE_RECTANGLE_2D:
		*out_mesh = self->plane;
		*out_pre = mat_rotate_x ((gfloat) G_PI * 0.5f);
		*out_has_pre = TRUE;
		return TRUE;
	case LRG_PRIMITIVE_CIRCLE_2D:
		*out_mesh = self->disc;
		*out_pre = mat_rotate_x ((gfloat) G_PI * 0.5f);
		*out_has_pre = TRUE;
		return TRUE;
	default:
		return FALSE;
	}
}

/* ==========================================================================
 * Lightmap uv2 for geometry without one (0.9.0 M3)
 *
 * The lightmap baker unwraps every mesh that has no second UV set
 * (LrgUvUnwrap, default settings, deterministic) and bakes into that
 * layout.  raylib's GenMesh* primitives and glTF models without
 * TEXCOORD_1 have no uv2, so a lightmapped draw of them would read the
 * single texel at (0, 0).  The renderer therefore regenerates the exact
 * mesh the baker baked and draws that instead, for lightmapped items
 * only:
 *
 *   primitives  lrg_uv_unwrap_new_lightmap_primitive (prim, params): the
 *               size is in the vertices, so the draw uses the item's
 *               world matrix alone; cached per (primitive, params)
 *   models      every mesh merged in object space through the model's
 *               transform (the baker's merge, bit for bit), unwrapped as
 *               one object, then split back per source mesh (the unwrap
 *               keeps the triangle order) so each keeps its material;
 *               cached on the GrlModel (qdata), drawn with the world
 *               matrix alone.  A model whose meshes all carry texcoords2
 *               is drawn as it is (the baker used its uv2).
 *
 * Shadow and depth passes keep the plain meshes: the geometry is the
 * same, only the uv2 differ.
 * ========================================================================== */

typedef struct
{
	Mesh     mesh;
	gboolean uploaded;
} LightmapMesh;

/* Frees @m's GPU buffers (with a context) and CPU arrays. */
static void
lightmap_mesh_release (Mesh *m)
{
	if (IsWindowReady ())
	{
		UnloadMesh (*m);
	}
	else
	{
		/* no context: GL calls would crash; the driver reclaimed the
		 * buffers with the context, only the CPU side is ours */
		MemFree (m->vertices);
		MemFree (m->normals);
		MemFree (m->texcoords);
		MemFree (m->texcoords2);
		MemFree (m->indices);
		MemFree (m->vboId);
	}
	memset (m, 0, sizeof (Mesh));
}

static void
lightmap_mesh_free (gpointer data)
{
	LightmapMesh *lm = (LightmapMesh *) data;

	if (lm == NULL)
		return;
	if (lm->uploaded)
		lightmap_mesh_release (&lm->mesh);
	g_free (lm);
}

/* Uploads @uv (copied into raylib arrays) as a static mesh. */
static gboolean
lightmap_mesh_upload (LrgUvMesh *uv,
                      Mesh      *out)
{
	lrg_uv_mesh_to_raylib (uv, out);
	if (out->vertexCount <= 0 || out->vertices == NULL)
	{
		memset (out, 0, sizeof (Mesh));
		return FALSE;
	}
	UploadMesh (out, FALSE);
	return TRUE;
}

/*
 * The unwrapped mesh of a lightmapped primitive item, made and uploaded
 * on first use.  NULL for primitives without a surface or when the
 * unwrap fails (the caller then draws the plain mesh).
 */
static Mesh *
lightmap_primitive_mesh (LrgWorld3DRenderer  *self,
                         const LrgRenderItem *item)
{
	g_autofree gchar *key = NULL;
	LightmapMesh     *lm;

	key = g_strdup_printf ("%d:%a:%a:%a:%a", (gint) item->primitive, (gdouble) item->params[0],
	                       (gdouble) item->params[1], (gdouble) item->params[2], (gdouble) item->params[3]);
	lm = g_hash_table_lookup (self->lightmap_prims, key);
	if (lm == NULL)
	{
		g_autoptr(LrgUvMesh) uv = lrg_uv_unwrap_new_lightmap_primitive (item->primitive, item->params);

		lm = g_new0 (LightmapMesh, 1);
		if (uv != NULL)
			lm->uploaded = lightmap_mesh_upload (uv, &lm->mesh);
		/* failures are cached too: the plain mesh is drawn from then on */
		g_hash_table_insert (self->lightmap_prims, g_steal_pointer (&key), lm);
	}
	return lm->uploaded ? &lm->mesh : NULL;
}

/* The unwrapped meshes of one model, one per model mesh (index order). */
typedef struct
{
	gint      n;
	Mesh     *meshes;      /* n entries; vertexCount 0 = nothing to draw */
	gboolean  unwrapped;   /* FALSE: the model has uv2 or the unwrap failed */
} LightmapModel;

static void
lightmap_model_free (gpointer data)
{
	LightmapModel *lm = (LightmapModel *) data;
	gint           i;

	for (i = 0; i < lm->n; i++)
		if (lm->meshes[i].vertexCount > 0)
			lightmap_mesh_release (&lm->meshes[i]);
	g_free (lm->meshes);
	g_free (lm);
}

static GQuark
lightmap_model_quark (void)
{
	return g_quark_from_static_string ("lrg-world3d-renderer-lightmap-model");
}

/*
 * Triangles [@t0, @t1) of @uv as a raylib mesh with its own compact
 * vertex range (16-bit indices; one vertex per corner when the range
 * has more than 65535 vertices), uploaded.  Positions, normals, uvs and
 * uv2 are copied through.
 */
static gboolean
lightmap_submesh_upload (LrgUvMesh *uv,
                         guint      t0,
                         guint      t1,
                         Mesh      *out)
{
	gsize            n_pos = 0;
	gsize            n_idx = 0;
	const gfloat    *pos = lrg_uv_mesh_get_positions (uv, &n_pos);
	const guint32   *idx = lrg_uv_mesh_get_indices (uv, &n_idx);
	const gfloat    *nrm = lrg_uv_mesh_get_normals (uv, NULL);
	const gfloat    *tex = lrg_uv_mesh_get_uvs (uv, NULL);
	const gfloat    *tex2 = lrg_uv_mesh_get_uv2 (uv, NULL);
	g_autoptr(GHashTable) remap = NULL;
	GArray          *order;
	guint            nt = t1 - t0;
	guint            c, nv, k;

	memset (out, 0, sizeof (Mesh));
	if (nt == 0 || (gsize) t1 * 3u > n_idx || pos == NULL)
		return FALSE;

	/* the vertices the range uses, in first-use order */
	remap = g_hash_table_new (g_direct_hash, g_direct_equal);
	order = g_array_new (FALSE, FALSE, sizeof (guint32));
	for (c = t0 * 3u; c < t1 * 3u; c++)
	{
		if (!g_hash_table_contains (remap, GUINT_TO_POINTER (idx[c] + 1u)))
		{
			g_hash_table_insert (remap, GUINT_TO_POINTER (idx[c] + 1u), GUINT_TO_POINTER (order->len));
			g_array_append_val (order, idx[c]);
		}
	}

	nv = (order->len <= 65535u) ? order->len : nt * 3u;
	out->vertexCount = (gint) nv;
	out->triangleCount = (gint) nt;
	out->vertices = (gfloat *) MemAlloc ((guint) ((gsize) nv * 3u * sizeof (gfloat)));
	if (nrm != NULL)
		out->normals = (gfloat *) MemAlloc ((guint) ((gsize) nv * 3u * sizeof (gfloat)));
	if (tex != NULL)
		out->texcoords = (gfloat *) MemAlloc ((guint) ((gsize) nv * 2u * sizeof (gfloat)));
	if (tex2 != NULL)
		out->texcoords2 = (gfloat *) MemAlloc ((guint) ((gsize) nv * 2u * sizeof (gfloat)));

	for (k = 0; k < nv; k++)
	{
		/* indexed: the k-th used vertex; de-indexed: the k-th corner */
		guint32 v = (order->len <= 65535u) ? g_array_index (order, guint32, k) : idx[t0 * 3u + k];

		memcpy (out->vertices + (gsize) k * 3u, pos + (gsize) v * 3u, 3 * sizeof (gfloat));
		if (out->normals != NULL)
			memcpy (out->normals + (gsize) k * 3u, nrm + (gsize) v * 3u, 3 * sizeof (gfloat));
		if (out->texcoords != NULL)
			memcpy (out->texcoords + (gsize) k * 2u, tex + (gsize) v * 2u, 2 * sizeof (gfloat));
		if (out->texcoords2 != NULL)
			memcpy (out->texcoords2 + (gsize) k * 2u, tex2 + (gsize) v * 2u, 2 * sizeof (gfloat));
	}
	if (order->len <= 65535u)
	{
		out->indices = (unsigned short *) MemAlloc ((guint) ((gsize) nt * 3u * sizeof (unsigned short)));
		for (c = 0; c < nt * 3u; c++)
			out->indices[c] = (unsigned short) GPOINTER_TO_UINT (
				g_hash_table_lookup (remap, GUINT_TO_POINTER (idx[t0 * 3u + c] + 1u)));
	}
	g_array_unref (order);
	UploadMesh (out, FALSE);
	return TRUE;
}

/*
 * The baker's object-space merge of @model (positions and normals through
 * model->transform, indices or sequential triangles), plus the uvs when
 * every mesh has them; @first_tri gets the first merged triangle of each
 * mesh (model->meshCount + 1 entries).  NULL when there is no geometry.
 */
static LrgUvMesh *
lightmap_model_merge (const Model *model,
                      guint       *first_tri)
{
	gfloat    mt[16];
	GArray   *pos = g_array_new (FALSE, FALSE, sizeof (gfloat));
	GArray   *nrm = g_array_new (FALSE, FALSE, sizeof (gfloat));
	GArray   *tex = g_array_new (FALSE, FALSE, sizeof (gfloat));
	GArray   *ind = g_array_new (FALSE, FALSE, sizeof (guint32));
	gboolean  all_nrm = TRUE, all_tex = TRUE;
	LrgUvMesh *mesh = NULL;
	gint      i;

	mt[0] = model->transform.m0;   mt[1] = model->transform.m1;
	mt[2] = model->transform.m2;   mt[3] = model->transform.m3;
	mt[4] = model->transform.m4;   mt[5] = model->transform.m5;
	mt[6] = model->transform.m6;   mt[7] = model->transform.m7;
	mt[8] = model->transform.m8;   mt[9] = model->transform.m9;
	mt[10] = model->transform.m10; mt[11] = model->transform.m11;
	mt[12] = model->transform.m12; mt[13] = model->transform.m13;
	mt[14] = model->transform.m14; mt[15] = model->transform.m15;

	for (i = 0; i < model->meshCount; i++)
	{
		const Mesh *me = &model->meshes[i];
		guint       base = pos->len / 3u;
		guint       nv, k;

		first_tri[i] = ind->len / 3u;
		if (me->vertices == NULL || me->vertexCount <= 0)
			continue;
		nv = (guint) me->vertexCount;
		for (k = 0; k < nv; k++)
		{
			gfloat p[3];

			lrg_math3d_transform_point (mt, me->vertices + (gsize) k * 3u, p);
			g_array_append_vals (pos, p, 3);
			if (me->normals != NULL)
			{
				gfloat n[3], len;

				lrg_math3d_transform_direction (mt, me->normals + (gsize) k * 3u, n);
				len = sqrtf (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
				if (len > 1e-30f)
				{
					n[0] /= len;
					n[1] /= len;
					n[2] /= len;
				}
				g_array_append_vals (nrm, n, 3);
			}
			if (me->texcoords != NULL)
				g_array_append_vals (tex, me->texcoords + (gsize) k * 2u, 2);
		}
		if (me->normals == NULL)
			all_nrm = FALSE;
		if (me->texcoords == NULL)
			all_tex = FALSE;
		if (me->indices != NULL)
		{
			for (k = 0; k < (guint) MAX (me->triangleCount, 0) * 3u; k++)
			{
				guint32 v = base + me->indices[k];

				g_array_append_val (ind, v);
			}
		}
		else
		{
			for (k = 0; k < nv - nv % 3u; k++)
			{
				guint32 v = base + k;

				g_array_append_val (ind, v);
			}
		}
	}
	first_tri[model->meshCount] = ind->len / 3u;

	if (ind->len >= 3u)
	{
		mesh = lrg_uv_mesh_new ((const gfloat *) (gpointer) pos->data, pos->len,
		                        (const guint32 *) (gpointer) ind->data, ind->len);
		if (mesh != NULL && all_nrm && nrm->len == pos->len)
			lrg_uv_mesh_set_normals (mesh, (const gfloat *) (gpointer) nrm->data, nrm->len);
		if (mesh != NULL && all_tex && tex->len * 3u == pos->len * 2u)
			lrg_uv_mesh_set_uvs (mesh, (const gfloat *) (gpointer) tex->data, tex->len);
	}
	g_array_unref (pos);
	g_array_unref (nrm);
	g_array_unref (tex);
	g_array_unref (ind);
	return mesh;
}

/*
 * The lightmap meshes of @grl (made on first use and kept on the model).
 * Returns NULL when the model is drawn as it is: every mesh has uv2, or
 * the unwrap failed.
 */
static LightmapModel *
lightmap_model (GrlModel *grl)
{
	LightmapModel *lm = g_object_get_qdata (G_OBJECT (grl), lightmap_model_quark ());
	const Model   *model;

	if (lm != NULL)
		return lm->unwrapped ? lm : NULL;

	model = (const Model *) grl_model_get_handle (grl);
	lm = g_new0 (LightmapModel, 1);
	if (model != NULL && model->meshCount > 0)
	{
		gboolean all_uv2 = TRUE;
		gint     i;

		for (i = 0; i < model->meshCount; i++)
			if (model->meshes[i].vertices != NULL && model->meshes[i].vertexCount > 0 &&
			    model->meshes[i].texcoords2 == NULL)
				all_uv2 = FALSE;
		if (!all_uv2)
		{
			g_autofree guint      *first = g_new0 (guint, (gsize) model->meshCount + 1u);
			g_autoptr(LrgUvMesh)   merged = lightmap_model_merge (model, first);
			g_autoptr(LrgUvUnwrap) unwrap = lrg_uv_unwrap_new ();
			g_autoptr(LrgUvMesh)   out = NULL;
			g_autoptr(GError)      error = NULL;

			if (merged != NULL)
				out = lrg_uv_unwrap_run (unwrap, merged, &error);
			if (out != NULL)
			{
				lm->n = model->meshCount;
				lm->meshes = g_new0 (Mesh, (gsize) lm->n);
				for (i = 0; i < model->meshCount; i++)
					lightmap_submesh_upload (out, first[i], first[i + 1], &lm->meshes[i]);
				lm->unwrapped = TRUE;
			}
			else if (merged != NULL)
				g_message ("lightmap uv2 for a model failed: %s", error != NULL ? error->message : "?");
		}
	}
	g_object_set_qdata_full (G_OBJECT (grl), lightmap_model_quark (), lm, lightmap_model_free);
	return lm->unwrapped ? lm : NULL;
}

static LrgLightmap3D *item_lightmap (const LrgRenderItem *item,
                                     guint               *page,
                                     gfloat              *scale_offset);

static void
draw_primitive (LrgWorld3DRenderer  *self,
                const LrgRenderItem *item,
                gboolean             casters_only)
{
	Matrix   m = primitive_matrix (item);
	Matrix   pre;
	Mesh     mesh;
	gboolean has_pre;

	/* a lightmapped primitive draws the baker's unwrapped mesh (its uv2
	 * address the lightmap entry); size is in its vertices */
	if (!casters_only && primitive_is_solid (item->primitive) && item_lightmap (item, NULL, NULL) != NULL)
	{
		Mesh *lm = lightmap_primitive_mesh (self, item);

		if (lm != NULL)
		{
			draw_mesh (self, *lm, to_matrix (item->world_matrix), item_color (item), item_texture (self, item));
			return;
		}
	}
	if (primitive_mesh (self, item->primitive, &mesh, &pre, &has_pre))
	{
		draw_mesh (self, mesh, has_pre ? mat_mul (pre, m) : m, item_color (item), item_texture (self, item));
		return;
	}
	if (casters_only)
		return;   /* lines cast no shadows */

	push_local (m);
	if (item->primitive == LRG_PRIMITIVE_GRID)
		DrawGrid (10, 0.1f);
	else
		DrawCubeWires ((Vector3) { 0.0f, 0.0f, 0.0f }, 1.0f, 1.0f, 1.0f, item_color (item));
	rlPopMatrix ();
}

/* Every mesh of a model (or just mesh_index), each with its own material
 * map: colour times the item colour, texture unless the item overrides. */
static void
draw_model (LrgWorld3DRenderer  *self,
            const LrgRenderItem *item,
            gboolean             casters_only)
{
	GrlModel  *grl;
	Model     *model;
	Matrix     m = to_matrix (item->world_matrix);
	Color      tint = item_color (item);
	Texture2D *override = item_texture (self, item);
	LightmapModel *lmodel;
	gint       i, first, last;

	grl = (item->model != NULL && GRL_IS_MODEL (item->model)) ? GRL_MODEL (item->model) : NULL;

	/* A mesh renderer without an asset manager leaves the model to us */
	if (grl == NULL && item->component != NULL && LRG_IS_MESH_RENDERER_COMPONENT (item->component))
		grl = lrg_world3d_renderer_get_model (self, lrg_mesh_renderer_component_get_mesh_asset (
		                                                LRG_MESH_RENDERER_COMPONENT (item->component)));
	/* Skinned components may share one model: pose it for this one */
	if (grl != NULL && item->component != NULL && LRG_IS_SKINNED_MODEL_COMPONENT (item->component))
		lrg_skinned_model_component_apply_pose (LRG_SKINNED_MODEL_COMPONENT (item->component));
	model = (grl != NULL) ? (Model *) grl_model_get_handle (grl) : NULL;
	if (model == NULL || model->meshCount == 0)
	{
		/* Missing model: a magenta placeholder (colour passes only) */
		if (!casters_only)
		{
			push_local (m);
			DrawCubeWires ((Vector3) { 0.0f, 0.0f, 0.0f }, 1.0f, 1.0f, 1.0f, MAGENTA);
			rlPopMatrix ();
		}
		return;
	}

	first = 0;
	last = model->meshCount;
	if (item->mesh_index >= 0)
	{
		if (item->mesh_index >= model->meshCount)
			return;
		first = item->mesh_index;
		last = first + 1;
	}

	/* a lightmapped model without uv2 draws the baker's unwrap of it
	 * (object space through model->transform already) */
	lmodel = (!casters_only && item_lightmap (item, NULL, NULL) != NULL) ? lightmap_model (grl) : NULL;
	if (lmodel == NULL)
		m = mat_mul (model->transform, m);
	for (i = first; i < last; i++)
	{
		Material    mat = model->materials[model->meshMaterial[i]];
		MaterialMap maps[RAYLIB_MATERIAL_MAPS];
		Color       c;

		/* The maps array is shared with the cached model: draw with a
		 * copy so nothing leaks into the next user of the model.  An
		 * LrgMaterial's maps go over the model's own (empty slots keep
		 * the model's textures). */
		memcpy (maps, mat.maps, sizeof maps);
		mat.maps = maps;
		if (self->cur_mat != NULL && !self->white_albedo)
		{
			lrg_material_fill_raylib_maps (self->cur_mat, maps);
			c = tint;   /* the item colour is the material's base colour */
		}
		else
		{
			c = maps[MATERIAL_MAP_DIFFUSE].color;
			c.r = (guchar) ((guint) c.r * tint.r / 255u);
			c.g = (guchar) ((guint) c.g * tint.g / 255u);
			c.b = (guchar) ((guint) c.b * tint.b / 255u);
			c.a = (guchar) ((guint) c.a * tint.a / 255u);
		}
		mat.shader = self->cur_shader;
		maps[MATERIAL_MAP_DIFFUSE].color = self->white_albedo ? WHITE : c;
		if (self->white_albedo)
			maps[MATERIAL_MAP_DIFFUSE].texture = self->white;
		else if (override != NULL)
			maps[MATERIAL_MAP_DIFFUSE].texture = *override;
		if (lmodel != NULL && i < lmodel->n)
		{
			if (lmodel->meshes[i].vertexCount > 0)
				DrawMesh (lmodel->meshes[i], mat, m);
		}
		else
			DrawMesh (model->meshes[i], mat, m);
		self->drawn++;
	}
}

/* A camera-facing quad at the item's position (unlit, rlgl batch). */
static void
draw_billboard (LrgWorld3DRenderer  *self,
                const LrgRenderItem *item)
{
	const gfloat *w = item->world_matrix;
	Texture2D    *tex = item_texture (self, item);
	Color         c = item_color (item);
	gfloat        hw = (item->params[0] > 0.0f ? item->params[0] : 1.0f) * 0.5f;
	gfloat        hh = (item->params[1] > 0.0f ? item->params[1] : hw * 2.0f) * 0.5f;
	gfloat        right[3], up[3];
	gint          k;

	/* The view matrix's first two rows are the camera's right and up */
	for (k = 0; k < 3; k++)
	{
		right[k] = self->cur_view[k * 4 + 0] * hw;
		up[k] = self->cur_view[k * 4 + 1] * hh;
	}

	/* Axis-Y billboards (params[2], an LrgBillboardMode) stay upright:
	 * world up, the camera's right flattened onto the ground plane */
	if ((gint) item->params[2] == LRG_BILLBOARD_MODE_AXIS_Y)
	{
		gfloat rx = self->cur_view[0], rz = self->cur_view[8];
		gfloat len = sqrtf (rx * rx + rz * rz);

		if (len < 1e-6f)
		{
			rx = 1.0f;
			rz = 0.0f;
			len = 1.0f;
		}
		right[0] = rx / len * hw;
		right[1] = 0.0f;
		right[2] = rz / len * hw;
		up[0] = 0.0f;
		up[1] = hh;
		up[2] = 0.0f;
	}

	rlSetTexture (tex != NULL ? tex->id : self->white.id);
	rlBegin (RL_QUADS);
	rlColor4ub (c.r, c.g, c.b, c.a);
	rlTexCoord2f (0.0f, 0.0f);
	rlVertex3f (w[12] - right[0] + up[0], w[13] - right[1] + up[1], w[14] - right[2] + up[2]);
	rlTexCoord2f (0.0f, 1.0f);
	rlVertex3f (w[12] - right[0] - up[0], w[13] - right[1] - up[1], w[14] - right[2] - up[2]);
	rlTexCoord2f (1.0f, 1.0f);
	rlVertex3f (w[12] + right[0] - up[0], w[13] + right[1] - up[1], w[14] + right[2] - up[2]);
	rlTexCoord2f (1.0f, 0.0f);
	rlVertex3f (w[12] + right[0] + up[0], w[13] + right[1] + up[1], w[14] + right[2] + up[2]);
	rlEnd ();
	rlSetTexture (0);
	self->drawn++;
}

/* Context of one replay of a list category. */
typedef struct
{
	LrgWorld3DRenderer   *self;
	LrgWorld3DRenderPass  pass;
	guint32               mask;          /* view cull mask (and caster mask) */
	gboolean              casters_only;  /* shadow and prepass passes: solid geometry only */
	gboolean              skip_batched;  /* static items went into the batch */
	gboolean              materials;     /* lit_shader follows the material contract */
	gboolean              layered;       /* lit_shader is the light set's: per-item draw layers */
	gboolean              customs;       /* custom-shaded LrgMaterials switch to their shader asset */
	gfloat                cutoff;        /* the pass's alphaCutoff, put back after a material */
	Shader                lit_shader;    /* what non-UNLIT items use */
	Shader                unlit_shader;  /* what UNLIT items use in lit modes */
} Replay;

/* Whether the static batch takes @item instead of the opaque pass. */
/*
 * The lightmap holding @item's baked light (0.9.0 M3): the lightmap of
 * the world the item's entity lives in, when the entity's GUID has an
 * entry there; @page and @scale_offset (4 floats) get the entry.  NULL
 * for items without an entity, world, lightmap or entry.
 */
static LrgLightmap3D *
item_lightmap (const LrgRenderItem *item,
               guint               *page,
               gfloat              *scale_offset)
{
	LrgWorld3D    *world;
	LrgLightmap3D *lightmap;

	if (item->entity == NULL)
		return NULL;
	world = lrg_entity3d_get_world (item->entity);
	if (world == NULL)
		return NULL;
	lightmap = lrg_world3d_get_lightmap (world);
	if (lightmap == NULL || lrg_lightmap3d_get_n_entries (lightmap) == 0)
		return NULL;
	if (!lrg_lightmap3d_get_entry (lightmap, lrg_entity3d_get_guid (item->entity), page, scale_offset))
		return NULL;
	return lightmap;
}

/* The centre of @item: its bounds' when known, else its origin. */
static void
item_center (const LrgRenderItem *item,
             gfloat              *c)
{
	if (item->bounds.min.x <= item->bounds.max.x && item->bounds.min.y <= item->bounds.max.y &&
	    item->bounds.min.z <= item->bounds.max.z)
	{
		c[0] = (item->bounds.min.x + item->bounds.max.x) * 0.5f;
		c[1] = (item->bounds.min.y + item->bounds.max.y) * 0.5f;
		c[2] = (item->bounds.min.z + item->bounds.max.z) * 0.5f;
	}
	else
	{
		c[0] = item->world_matrix[12];
		c[1] = item->world_matrix[13];
		c[2] = item->world_matrix[14];
	}
}

/*
 * Binds @item's baked light on the lit program @shader_id (0.9.0 M3):
 * its lightmap entry, else the world's probe grid around its centre,
 * else nothing.  Returns the hook mode bound (0 none, 1 lightmap, 2
 * probes) so the caller puts the inactive state back after the draw.
 */
static gint
bind_baked (LrgWorld3DRenderer  *self,
            const LrgRenderItem *item,
            guint                shader_id)
{
	LrgLightmap3D *lightmap;
	LrgProbeGrid  *grid = NULL;
	gfloat         so[4];
	guint          page = 0;
	gdouble        origin[3] = { 0.0, 0.0, 0.0 };

	lightmap = item_lightmap (item, &page, so);
	if (lightmap == NULL && item->entity != NULL && lrg_entity3d_get_world (item->entity) != NULL)
	{
		LrgWorld3D *world = lrg_entity3d_get_world (item->entity);
		gfloat      c[3];

		if (lrg_world3d_get_probe_grids (world)->len > 0)
		{
			item_center (item, c);
			grid = lrg_world3d_find_probe_grid (world, c);
		}
	}
	if (self->lights != NULL)
		lrg_light_set_get_origin (self->lights, &origin[0], &origin[1], &origin[2]);
	return lrg_baked_lighting_set_draw_private (shader_id, lightmap, page, lightmap != NULL ? so : NULL,
	                                            grid, origin);
}

static gboolean
item_is_batchable (const LrgRenderItem *item)
{
	if (item->entity == NULL || !lrg_entity3d_get_static (item->entity))
		return FALSE;
	/* a lightmapped item keeps its own draw: it needs its uv2 and entry */
	if (item_lightmap (item, NULL, NULL) != NULL)
		return FALSE;
	if (lrg_render_item_get_category (item) != LRG_RENDER_CATEGORY_OPAQUE)
		return FALSE;
	if ((item->flags & (LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED | LRG_RENDER_ITEM_FLAG_UNLIT)) != 0 || item->texture != NULL)
		return FALSE;
	/* An LrgMaterial binds maps and parameters per draw: not mergeable */
	if (item->material != NULL && LRG_IS_MATERIAL (item->material))
		return FALSE;
	if (item->kind == LRG_RENDER_ITEM_PRIMITIVE)
		return primitive_is_solid (item->primitive);
	return item->kind == LRG_RENDER_ITEM_MESH && item->mesh != NULL && GRL_IS_MESH (item->mesh);
}

/*
 * Sets lodFade on @shader_id for the next draw.  Most items draw with 0,
 * so the uniform is only touched for fading items and for the first
 * opaque draw on a program that still holds a non-zero fade.
 */
static void
bind_lod_fade (LrgWorld3DRenderer *self,
               guint               shader_id,
               gfloat              fade)
{
	gpointer key = GUINT_TO_POINTER (shader_id);

	if (shader_id == 0)
		return;
	if (fade != 0.0f)
	{
		lrg_lod_bind_fade (shader_id, fade);
		g_hash_table_add (self->faded, key);
	}
	else if (g_hash_table_remove (self->faded, key))
		lrg_lod_bind_fade (shader_id, 0.0f);
}

/* A float uniform by name (no-op when the program lacks it). */
static void
shader_set_float (Shader       shader,
                  const gchar *name,
                  gfloat       value)
{
	gint loc = GetShaderLocation (shader, name);

	if (loc >= 0)
		SetShaderValue (shader, loc, &value, SHADER_UNIFORM_FLOAT);
}

/*
 * The scalar parameters of a legacy LrgMaterial3D on the lit contract:
 * pbrParams (metallic, roughness, occlusion 1, normal scale 1) and
 * emissionOwn (emission colour times strength).  Its colour is already
 * in the item colour.
 */
static void
apply_material3d (LrgWorld3DRenderer *self,
                  LrgMaterial3D      *m3d,
                  Shader              shader)
{
	gfloat pbr[4];
	gfloat emission[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	gfloat strength;

	if (self->m3d_shader != shader.id)
	{
		self->m3d_shader = shader.id;
		self->m3d_loc_pbr = GetShaderLocation (shader, "pbrParams");
		self->m3d_loc_emission = GetShaderLocation (shader, "emissionOwn");
	}
	pbr[0] = CLAMP (lrg_material3d_get_metallic (m3d), 0.0f, 1.0f);
	pbr[1] = CLAMP (lrg_material3d_get_roughness (m3d), 0.0f, 1.0f);
	pbr[2] = 1.0f;
	pbr[3] = 1.0f;
	strength = lrg_material3d_get_emission_strength (m3d);
	if (strength > 0.0f)
	{
		lrg_material3d_get_emission_color (m3d, &emission[0], &emission[1], &emission[2], &emission[3]);
		emission[0] *= strength;
		emission[1] *= strength;
		emission[2] *= strength;
		emission[3] = 1.0f;
	}
	if (self->m3d_loc_pbr >= 0)
		SetShaderValue (shader, self->m3d_loc_pbr, pbr, SHADER_UNIFORM_VEC4);
	if (self->m3d_loc_emission >= 0)
		SetShaderValue (shader, self->m3d_loc_emission, emission, SHADER_UNIFORM_VEC4);
}

/* Puts back the defaults apply_material3d() changed. */
static void
reset_material3d (LrgWorld3DRenderer *self,
                  Shader              shader)
{
	const gfloat pbr[4] = { 0.0f, 0.5f, 1.0f, 1.0f };
	const gfloat none[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	if (self->m3d_shader != shader.id)
		return;
	if (self->m3d_loc_pbr >= 0)
		SetShaderValue (shader, self->m3d_loc_pbr, pbr, SHADER_UNIFORM_VEC4);
	if (self->m3d_loc_emission >= 0)
		SetShaderValue (shader, self->m3d_loc_emission, none, SHADER_UNIFORM_VEC4);
}

/*
 * The compiled shader asset of a custom-shaded material (cached by path,
 * failures too, reported once), or NULL.
 */
static LrgShaderAsset *
custom_shader (LrgWorld3DRenderer *self,
               LrgMaterial        *mat)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *abs = NULL;
	const gchar      *ref = lrg_material_get_shader (mat);
	LrgShaderAsset   *asset;

	if (ref == NULL || *ref == '\0')
		return NULL;
	abs = lrg_material_resolve_reference (mat, ref);
	if (abs == NULL)
		return NULL;
	if (g_hash_table_contains (self->custom_shaders, abs))
		return (LrgShaderAsset *) g_hash_table_lookup (self->custom_shaders, abs);

	asset = lrg_shader_asset_new_from_file (abs, &error);
	if (asset != NULL && !lrg_shader_asset_compile (asset, &error))
		g_clear_object (&asset);
	if (asset == NULL)
		g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Custom shader %s: %s", abs,
		       error != NULL ? error->message : "failed");
	g_hash_table_insert (self->custom_shaders, g_strdup (abs), asset);
	return asset;
}

/*
 * Selects the reflection probes for @item: the environment's reference
 * point moves to the item's centre and, when that changes which two
 * probes win (or the program changed), the environment hook is bound
 * again on @shader_id.  The environment is the first hook, so it takes
 * the light set's first hook unit, as lrg_light_set_apply() binds it.
 */
static void
pick_probes (LrgWorld3DRenderer  *self,
             const LrgRenderItem *item,
             guint                shader_id)
{
	LrgReflectionProbe *a = NULL, *b = NULL;
	gfloat              c[3];
	gint                unit;

	if (item->bounds.min.x <= item->bounds.max.x && item->bounds.min.y <= item->bounds.max.y &&
	    item->bounds.min.z <= item->bounds.max.z)
	{
		c[0] = (item->bounds.min.x + item->bounds.max.x) * 0.5f;
		c[1] = (item->bounds.min.y + item->bounds.max.y) * 0.5f;
		c[2] = (item->bounds.min.z + item->bounds.max.z) * 0.5f;
	}
	else
	{
		c[0] = item->world_matrix[12];
		c[1] = item->world_matrix[13];
		c[2] = item->world_matrix[14];
	}
	lrg_environment_select_probes (self->probe_env, c, &a, &b);
	if (a == self->probe_pick[0] && b == self->probe_pick[1] && GUINT_TO_POINTER (shader_id) == self->probe_pick[2])
		return;
	self->probe_pick[0] = a;
	self->probe_pick[1] = b;
	self->probe_pick[2] = GUINT_TO_POINTER (shader_id);
	lrg_environment_set_reference_point (self->probe_env, c[0], c[1], c[2]);
	rlDrawRenderBatchActive ();
	unit = lrg_light_set_hook_unit_first ();
	lrg_environment_bind_private (self->probe_env, shader_id, &unit);
	rlActiveTextureSlot (0);
}

/* ==========================================================================
 * Profiler sections and frame capture
 *
 * Sections are the default profiler's.  Disabled, each is a flag test
 * (the singleton already exists once anything has asked for it).  GPU
 * sections go through the profiler's timer, which does not wait.
 *
 * Frame capture notes only while recording or replaying.  note() FALSE
 * means this call is past the scrub: the caller skips the draw.
 * ========================================================================== */

static void
prof_begin (const gchar *name)
{
	LrgProfiler *profiler = lrg_profiler_get_default ();

	if (G_UNLIKELY (lrg_profiler_is_enabled (profiler)))
		lrg_profiler_begin_section (profiler, name);
	if (G_UNLIKELY (lrg_profiler_get_gpu_enabled (profiler)))
		lrg_profiler_begin_gpu_section (profiler, name);
}

static void
prof_end (const gchar *name)
{
	LrgProfiler *profiler = lrg_profiler_get_default ();

	if (G_UNLIKELY (lrg_profiler_get_gpu_enabled (profiler)))
		lrg_profiler_end_gpu_section (profiler);
	if (G_UNLIKELY (lrg_profiler_is_enabled (profiler)))
		lrg_profiler_end_section (profiler, name);
}

static const gchar *
pass_nick (LrgWorld3DRenderPass pass)
{
	switch (pass)
	{
	case LRG_WORLD3D_RENDER_PASS_SHADOW:
		return "shadow";
	case LRG_WORLD3D_RENDER_PASS_SKY:
		return "sky";
	case LRG_WORLD3D_RENDER_PASS_OPAQUE:
		return "opaque";
	case LRG_WORLD3D_RENDER_PASS_CUTOUT:
		return "cutout";
	case LRG_WORLD3D_RENDER_PASS_TRANSPARENT:
		return "transparent";
	case LRG_WORLD3D_RENDER_PASS_POST:
		return "post";
	case LRG_WORLD3D_RENDER_PASS_PREPASS:
		return "prepass";
	case LRG_WORLD3D_RENDER_PASS_SSAO:
		return "ssao";
	case LRG_WORLD3D_RENDER_PASS_VOLUMETRIC:
		return "volumetric";
	default:
		return "opaque";
	}
}

static const gchar *
primitive_nick (LrgPrimitiveType primitive)
{
	switch (primitive)
	{
	case LRG_PRIMITIVE_PLANE:
		return "plane";
	case LRG_PRIMITIVE_CUBE:
		return "cube";
	case LRG_PRIMITIVE_CIRCLE:
		return "circle";
	case LRG_PRIMITIVE_UV_SPHERE:
		return "uv-sphere";
	case LRG_PRIMITIVE_ICO_SPHERE:
		return "ico-sphere";
	case LRG_PRIMITIVE_CYLINDER:
		return "cylinder";
	case LRG_PRIMITIVE_CONE:
		return "cone";
	case LRG_PRIMITIVE_TORUS:
		return "torus";
	case LRG_PRIMITIVE_GRID:
		return "grid";
	case LRG_PRIMITIVE_MESH:
		return "mesh";
	case LRG_PRIMITIVE_RECTANGLE_2D:
		return "rectangle";
	case LRG_PRIMITIVE_CIRCLE_2D:
		return "circle-2d";
	default:
		return "primitive";
	}
}

/* TRUE when the caller should draw.  Idle captures return TRUE without
 * filling a call. */
static gboolean
capture_note_special (LrgWorld3DRenderer *self,
                      const gchar        *pass,
                      LrgFrameCallKind    kind,
                      const gchar        *shader,
                      guint               shader_id,
                      guint               framebuffer)
{
	LrgFrameCall call;

	if (self->capture == NULL || !lrg_frame_capture_is_active (self->capture))
		return TRUE;
	memset (&call, 0, sizeof call);
	call.pass = pass;
	call.kind = kind;
	call.shader = shader;
	call.shader_id = shader_id;
	call.instances = 1;
	call.color[0] = call.color[1] = call.color[2] = call.color[3] = 1.0f;
	call.transform[0] = call.transform[5] = call.transform[10] = call.transform[15] = 1.0f;
	call.depth_test = TRUE;
	call.depth_write = TRUE;
	call.cull = TRUE;
	call.framebuffer = framebuffer;
	return lrg_frame_capture_note (self->capture, &call);
}

static gboolean
capture_note_item (LrgWorld3DRenderer  *self,
                   const Replay        *r,
                   const LrgRenderItem *item)
{
	LrgFrameCall  call;
	const gchar  *shader_name;
	guint         shader_id = 0;
	guint         texture_id = 0;
	guint         vertices = 0;
	guint         triangles = 0;

	if (self->capture == NULL || !lrg_frame_capture_is_active (self->capture))
		return TRUE;

	memset (&call, 0, sizeof call);
	call.pass = pass_nick (r->pass);
	call.instances = 1;
	call.depth_test = TRUE;
	call.depth_write = (r->pass != LRG_WORLD3D_RENDER_PASS_TRANSPARENT);
	call.cull = (item->flags & LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED) == 0;
	call.blend = (item->flags & LRG_RENDER_ITEM_FLAG_TRANSPARENT) != 0 ||
	             r->pass == LRG_WORLD3D_RENDER_PASS_TRANSPARENT;
	call.wireframe = (self->mode == LRG_WORLD3D_RENDER_MODE_WIREFRAME);
	if (self->gpu)
		call.framebuffer = rlGetActiveFramebuffer ();
	memcpy (call.color, item->color, sizeof call.color);
	memcpy (call.transform, item->world_matrix, sizeof call.transform);

	shader_name = r->casters_only ? "caster" : "lit";
	if (self->gpu)
	{
		if (r->casters_only)
			shader_id = r->lit_shader.id;
		else if ((item->flags & LRG_RENDER_ITEM_FLAG_UNLIT) != 0)
		{
			shader_name = "unlit";
			shader_id = r->unlit_shader.id;
		}
		else
			shader_id = r->lit_shader.id;
	}
	else if ((item->flags & LRG_RENDER_ITEM_FLAG_UNLIT) != 0 && !r->casters_only)
		shader_name = "unlit";
	call.shader = shader_name;
	call.shader_id = shader_id;

	switch (item->kind)
	{
	case LRG_RENDER_ITEM_PRIMITIVE:
		call.kind = LRG_FRAME_CALL_KIND_PRIMITIVE;
		call.mesh = primitive_nick (item->primitive);
		break;
	case LRG_RENDER_ITEM_MESH:
		call.kind = LRG_FRAME_CALL_KIND_MESH;
		call.mesh = "mesh";
		if (item->mesh != NULL && GRL_IS_MESH (item->mesh))
		{
			vertices = (guint) MAX (grl_mesh_get_vertex_count (GRL_MESH (item->mesh)), 0);
			triangles = (guint) MAX (grl_mesh_get_triangle_count (GRL_MESH (item->mesh)), 0);
		}
		break;
	case LRG_RENDER_ITEM_MODEL:
		call.kind = LRG_FRAME_CALL_KIND_MODEL;
		call.mesh = "model";
		break;
	case LRG_RENDER_ITEM_BILLBOARD:
		call.kind = LRG_FRAME_CALL_KIND_BILLBOARD;
		call.mesh = "billboard";
		vertices = 4;
		triangles = 2;
		break;
	case LRG_RENDER_ITEM_CUSTOM:
		call.kind = LRG_FRAME_CALL_KIND_CUSTOM;
		break;
	default:
		call.kind = LRG_FRAME_CALL_KIND_CUSTOM;
		break;
	}
	call.vertices = vertices;
	call.triangles = triangles;

	if (item->texture != NULL && GRL_IS_TEXTURE (item->texture))
	{
		Texture2D *handle = (Texture2D *) grl_texture_get_handle (GRL_TEXTURE (item->texture));

		if (handle != NULL)
			texture_id = handle->id;
	}
	call.texture_id = texture_id;

	if (item->material != NULL && LRG_IS_MATERIAL (item->material))
		call.material = lrg_material_get_name (LRG_MATERIAL (item->material));
	if (item->entity != NULL && LRG_IS_ENTITY3D (item->entity))
	{
		call.entity = lrg_entity3d_get_name (item->entity);
		call.guid = lrg_entity3d_get_guid (item->entity);
	}
	return lrg_frame_capture_note (self->capture, &call);
}

static void
replay_item (const LrgRenderItem *item,
             LrgRenderCategory    category,
             gpointer             user_data)
{
	Replay             *r = (Replay *) user_data;
	LrgWorld3DRenderer *self = r->self;
	LrgMaterial        *mat = NULL;
	LrgMaterial3D      *m3d = NULL;
	LrgShaderAsset     *custom = NULL;
	Shader              shader;
	gboolean            two_sided, bound = FALSE;
	gint                baked = 0;

	if ((r->mask & LRG_RENDER_LAYERS_BIT (item->layer)) == 0)
		return;
	if (r->skip_batched && item_is_batchable (item))
		return;
	if (r->casters_only)
	{
		/* Shadow and depth passes draw solid geometry only */
		if (item->kind == LRG_RENDER_ITEM_BILLBOARD)
			return;
		if (item->kind == LRG_RENDER_ITEM_PRIMITIVE && !primitive_is_solid (item->primitive))
			return;
	}

	/* Past the frame-debugger scrub this draw does not happen, and it
	 * is not counted in the pass.  Idle captures return before any work. */
	if (!capture_note_item (self, r, item))
		return;

	record_item (self, r->pass, item);
	if (!self->gpu)
		return;

	if (item->material != NULL && LRG_IS_MATERIAL (item->material))
		mat = LRG_MATERIAL (item->material);
	else if (item->material != NULL && LRG_IS_MATERIAL3D (item->material))
		m3d = LRG_MATERIAL3D (item->material);

	/* The program: UNLIT items keep their albedo in the lit modes (an
	 * LrgMaterial's unlit model is handled by the lit shader itself, with
	 * its emission); custom-shaded materials use their shader asset */
	shader = r->lit_shader;
	if (!r->casters_only && (item->flags & LRG_RENDER_ITEM_FLAG_UNLIT) != 0 && !(mat != NULL && r->materials))
		shader = r->unlit_shader;
	if (mat != NULL && r->customs && lrg_material_get_shading_model (mat) == LRG_SHADING_MODEL_CUSTOM)
	{
		custom = custom_shader (self, mat);
		if (custom != NULL && lrg_shader_asset_get_shader (custom) != NULL)
			shader = *(Shader *) grl_shader_get_handle (lrg_shader_asset_get_shader (custom));
		else
			custom = NULL;
	}
	self->cur_shader = shader;
	self->cur_mat = mat;

	/* Per item: the LOD cross-fade, and the item's layer for the lights'
	 * layer masks (light-set shader only) */
	bind_lod_fade (self, shader.id, lrg_lod_component_get_item_fade (item, NULL));
	if (r->layered && shader.id == r->lit_shader.id)
	{
		guint32 bit = LRG_RENDER_LAYERS_BIT (item->layer);

		if (bit != self->cur_layers)
		{
			lrg_light_set_set_draw_layers (self->lights, bit);
			self->cur_layers = bit;
		}
	}

	/* Reflection probes: the two around the item's centre (its bounds,
	 * else its origin), rebound only when the pick changes */
	if (self->probe_env != NULL && r->materials && !r->casters_only)
		pick_probes (self, item, shader.id);

	/* Baked light (0.9.0 M3): the item's lightmap entry or probe grid on
	 * the lit programs (the light set's, a lit custom shader's) */
	if (r->materials && !r->casters_only && shader.id != r->unlit_shader.id)
		baked = bind_baked (self, item, shader.id);

	/* Material uniforms and render state (colour passes) */
	if (r->materials && !r->casters_only)
	{
		if (custom != NULL)
		{
			if (lrg_shader_asset_get_lit (custom))
				lrg_shader_asset_bind_lights (custom, self->lights);
			lrg_shader_asset_apply (custom, NULL);
		}
		if (mat != NULL)
		{
			lrg_material_apply (mat, shader.id);
			lrg_material_apply_state (mat);
			bound = TRUE;
		}
		else if (m3d != NULL && shader.id == r->lit_shader.id)
		{
			apply_material3d (self, m3d, shader);
			bound = TRUE;
		}
	}

	two_sided = (item->flags & LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED) != 0 && !r->casters_only;
	if (two_sided)
	{
		rlDrawRenderBatchActive ();
		rlDisableBackfaceCulling ();
	}

	switch (item->kind)
	{
	case LRG_RENDER_ITEM_PRIMITIVE:
		draw_primitive (self, item, r->casters_only);
		break;
	case LRG_RENDER_ITEM_MESH:
		if (item->mesh != NULL && GRL_IS_MESH (item->mesh))
		{
			Mesh *mesh = (Mesh *) grl_mesh_get_handle (GRL_MESH (item->mesh));

			if (mesh != NULL)
				draw_mesh (self, *mesh, to_matrix (item->world_matrix), item_color (item), item_texture (self, item));
		}
		break;
	case LRG_RENDER_ITEM_MODEL:
		draw_model (self, item, r->casters_only);
		break;
	case LRG_RENDER_ITEM_BILLBOARD:
		draw_billboard (self, item);
		break;
	case LRG_RENDER_ITEM_CUSTOM:
		if (item->draw_func != NULL)
		{
			item->draw_func (item, category, self);
			self->drawn++;
		}
		break;
	default:
		break;
	}

	if (two_sided)
	{
		rlDrawRenderBatchActive ();
		rlEnableBackfaceCulling ();
	}

	/* Back to the "no material" state for the next item (an
	 * LrgMaterial3D only touched two uniforms: put back just those) */
	if (bound && mat != NULL)
	{
		rlDrawRenderBatchActive ();
		lrg_material_reset_uniforms (shader.id);
		lrg_material_restore_state (mat);
		if (r->cutoff > 0.0f)
			shader_set_float (shader, "alphaCutoff", r->cutoff);
	}
	else if (bound)
		reset_material3d (self, shader);
	/* the next draw (maybe a batch or terrain) must not inherit it */
	if (baked != 0)
		lrg_baked_lighting_set_draw_private (shader.id, NULL, 0, NULL, NULL, NULL);
	self->cur_mat = NULL;
}

/* ==========================================================================
 * Static batching
 * ========================================================================== */

/* FNV-1a over a few bytes. */
static guint64
hash_bytes (guint64       h,
            gconstpointer data,
            gsize         len)
{
	const guint8 *p = (const guint8 *) data;
	gsize         i;

	for (i = 0; i < len; i++)
	{
		h ^= p[i];
		h *= G_GUINT64_CONSTANT (1099511628211);
	}
	return h;
}

/* Adds one raylib mesh to the batch with @transform and a baked colour. */
static void
batch_add_mesh (LrgWorld3DRenderer *self,
                const Mesh         *mesh,
                Matrix              transform,
                Color               color,
                guint               layer)
{
	g_autofree guint8  *colors = NULL;
	g_autofree guint32 *indices = NULL;
	g_autoptr(GError)   error = NULL;
	GrlMatrix           gm;
	guint               n_vertices, n_indices, i;

	if (mesh->vertices == NULL || mesh->vertexCount <= 0)
		return;
	n_vertices = (guint) mesh->vertexCount;
	colors = g_new (guint8, n_vertices * 4);
	for (i = 0; i < n_vertices; i++)
	{
		colors[i * 4 + 0] = color.r;
		colors[i * 4 + 1] = color.g;
		colors[i * 4 + 2] = color.b;
		colors[i * 4 + 3] = color.a;
	}
	/* Unindexed meshes (raylib's par_shapes spheres) get 0 .. n-1 */
	n_indices = (mesh->indices != NULL) ? (guint) mesh->triangleCount * 3 : n_vertices;
	indices = g_new (guint32, MAX (n_indices, 1u));
	for (i = 0; i < n_indices; i++)
		indices[i] = (mesh->indices != NULL) ? (guint32) mesh->indices[i] : i;

	gm.m0 = transform.m0;   gm.m1 = transform.m1;   gm.m2 = transform.m2;   gm.m3 = transform.m3;
	gm.m4 = transform.m4;   gm.m5 = transform.m5;   gm.m6 = transform.m6;   gm.m7 = transform.m7;
	gm.m8 = transform.m8;   gm.m9 = transform.m9;   gm.m10 = transform.m10; gm.m11 = transform.m11;
	gm.m12 = transform.m12; gm.m13 = transform.m13; gm.m14 = transform.m14; gm.m15 = transform.m15;

	if (!lrg_static_batch_add_mesh (self->batch, mesh->vertices, mesh->normals, mesh->texcoords, colors, n_vertices,
	                                indices, n_indices, &gm, 0, layer, &error))
		g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Static batch: %s", error->message);
}

/*
 * Merges the batchable opaque items into the static batch when their set
 * changed (entity, world version, shape, colour), uploads it and draws it
 * with the light-set shader.  Headless it only counts.  Returns whether
 * the batch is in use (the opaque replay then skips those items).
 */
static gboolean
draw_static_batch (LrgWorld3DRenderer *self,
                   LrgRenderList      *list,
                   guint32             mask,
                   GrlShader          *light_shader,
                   gboolean            draw)
{
	guint64 sig = G_GUINT64_CONSTANT (14695981039346656037);
	guint   i, n, count = 0;

	n = lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_OPAQUE);
	for (i = 0; i < n; i++)
	{
		const LrgRenderItem *item = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_OPAQUE, i);
		guint64              version;

		if (!item_is_batchable (item))
			continue;
		version = lrg_entity3d_get_world_version (item->entity);
		sig = hash_bytes (sig, &item->entity, sizeof item->entity);
		sig = hash_bytes (sig, &version, sizeof version);
		sig = hash_bytes (sig, &item->kind, sizeof item->kind);
		sig = hash_bytes (sig, &item->primitive, sizeof item->primitive);
		sig = hash_bytes (sig, &item->mesh, sizeof item->mesh);
		sig = hash_bytes (sig, item->params, sizeof item->params);
		sig = hash_bytes (sig, item->color, sizeof item->color);
		sig = hash_bytes (sig, item->world_matrix, sizeof item->world_matrix);
		sig = hash_bytes (sig, &item->layer, sizeof item->layer);
		count++;
	}
	self->batched = count;
	if (count == 0)
		return FALSE;
	/* Headless, or a scrubbed replay: the batch still owns those items
	 * (skip_batched) but nothing is drawn. */
	if (!self->gpu || !draw)
		return TRUE;

	/* Rebuild on change: CPU merge, then upload */
	if (self->batch == NULL)
		self->batch = lrg_static_batch_new ();
	if (sig != self->batch_signature || !lrg_static_batch_is_uploaded (self->batch))
	{
		g_autoptr(GError) error = NULL;

		lrg_static_batch_clear (self->batch);
		for (i = 0; i < n; i++)
		{
			const LrgRenderItem *item = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_OPAQUE, i);
			Mesh                 mesh;
			Matrix               pre;
			gboolean             has_pre;

			if (!item_is_batchable (item))
				continue;
			if (item->kind == LRG_RENDER_ITEM_PRIMITIVE)
			{
				Matrix m = primitive_matrix (item);

				if (primitive_mesh (self, item->primitive, &mesh, &pre, &has_pre))
					batch_add_mesh (self, &mesh, has_pre ? mat_mul (pre, m) : m, item_color (item), item->layer);
			}
			else
			{
				Mesh *handle = (Mesh *) grl_mesh_get_handle (GRL_MESH (item->mesh));

				if (handle != NULL)
					batch_add_mesh (self, handle, to_matrix (item->world_matrix), item_color (item), item->layer);
			}
		}
		if (!lrg_static_batch_upload (self->batch, &error))
		{
			g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Static batch upload failed: %s", error->message);
			self->batched = 0;
			return FALSE;
		}
		self->batch_signature = sig;
	}
	if (self->batch_shader != light_shader)
	{
		lrg_static_batch_set_shader (self->batch, light_shader);
		self->batch_shader = light_shader;
	}
	lrg_static_batch_draw (self->batch, mask);
	self->pass_counts[LRG_WORLD3D_RENDER_PASS_OPAQUE] += lrg_static_batch_get_chunk_count (self->batch);
	self->drawn += lrg_static_batch_get_chunk_count (self->batch);
	return TRUE;
}

/* ==========================================================================
 * Frame helpers: 3D matrices, timing
 * ========================================================================== */

/* Loads @proj and @view into rlgl like BeginMode3D() would. */
static void
begin_3d (const gfloat *proj,
          const gfloat *view)
{
	rlDrawRenderBatchActive ();
	rlMatrixMode (RL_PROJECTION);
	rlPushMatrix ();
	rlLoadIdentity ();
	rlMultMatrixf (proj);
	rlMatrixMode (RL_MODELVIEW);
	rlLoadIdentity ();
	rlMultMatrixf (view);
	rlEnableDepthTest ();
}

/* Starts timing a pass (0 when timing is off). */
static gint64
pass_clock (LrgWorld3DRenderer *self)
{
	if (!self->timing)
		return 0;
	return g_get_monotonic_time ();
}

/*
 * Ends timing @pass: waits for the GPU (a one-pixel read-back of the
 * bound framebuffer, which only returns once everything drawn into it
 * finished) and adds the elapsed time.
 */
static void
pass_clock_end (LrgWorld3DRenderer   *self,
                LrgWorld3DRenderPass  pass,
                gint64                start)
{
	if (!self->timing)
		return;
	if (self->gpu)
	{
		guchar *px;

		rlDrawRenderBatchActive ();
		px = rlReadScreenPixels (1, 1);
		RL_FREE (px);
	}
	self->pass_time[pass] += (gdouble) (g_get_monotonic_time () - start) / 1000.0;
}

/* ==========================================================================
 * The view camera: an LrgCamera that begins a view's own matrices, so the
 * depth prepass (which records through an LrgCamera) sees exactly the
 * matrices the colour passes use.
 * ========================================================================== */

#define LRG_TYPE_WORLD3D_VIEW_CAMERA (lrg_world3d_view_camera_get_type ())
G_DECLARE_FINAL_TYPE (LrgWorld3DViewCamera, lrg_world3d_view_camera, LRG, WORLD3D_VIEW_CAMERA, LrgCamera)

struct _LrgWorld3DViewCamera
{
	LrgCamera parent_instance;
	gfloat    view[16];
	gfloat    proj[16];
};

G_DEFINE_FINAL_TYPE (LrgWorld3DViewCamera, lrg_world3d_view_camera, LRG_TYPE_CAMERA)

static void
view_camera_begin (LrgCamera *camera)
{
	LrgWorld3DViewCamera *self = LRG_WORLD3D_VIEW_CAMERA (camera);

	begin_3d (self->proj, self->view);
}

static void
view_camera_end (LrgCamera *camera)
{
	(void) camera;
	EndMode3D ();
}

static void
lrg_world3d_view_camera_class_init (LrgWorld3DViewCameraClass *klass)
{
	LrgCameraClass *camera_class = LRG_CAMERA_CLASS (klass);

	camera_class->begin = view_camera_begin;
	camera_class->end = view_camera_end;
}

static void
lrg_world3d_view_camera_init (LrgWorld3DViewCamera *self)
{
	lrg_math3d_identity (self->view);
	lrg_math3d_identity (self->proj);
}

/* ==========================================================================
 * Shadows, prepass, post
 * ========================================================================== */

/*
 * Renders this frame's shadow passes for @v: the cascades of the
 * directional light (cube faces of shadowed point lights, spot maps)
 * around the view, each replaying the shadow-caster list.  The passes
 * need the screen bound, so the caller's target is left and re-entered
 * afterwards.
 *
 * Returns: %TRUE when every pass rendered (the light set may sample them)
 */
static gboolean
render_shadows (LrgWorld3DRenderer   *self,
                LrgRenderList        *list,
                const LrgWorld3DView *v,
                guint                 outer,
                gint                  w,
                gint                  h)
{
	GrlShader *caster = NULL;
	Replay     r;
	gfloat     fwd[3];
	guint      n, p;

	fwd[0] = v->target[0] - v->position[0];
	fwd[1] = v->target[1] - v->position[1];
	fwd[2] = v->target[2] - v->position[2];
	normalize3 (fwd);
	n = lrg_shadow_renderer_update (self->shadows, self->lights, v->position, fwd, v->up, v->fovy,
	                                (gfloat) w / (gfloat) MAX (h, 1), MAX (v->near_plane, 0.01f));
	if (n == 0)
		return FALSE;
	if (self->gpu)
	{
		caster = lrg_shadow_renderer_get_caster_shader (self->shadows);
		if (caster == NULL)
			return FALSE;
	}

	memset (&r, 0, sizeof r);
	r.self = self;
	r.pass = LRG_WORLD3D_RENDER_PASS_SHADOW;
	r.mask = v->cull_mask & lrg_shadow_renderer_get_caster_mask (self->shadows);
	r.casters_only = TRUE;

	if (self->gpu)
	{
		rlDrawRenderBatchActive ();
		EndTextureMode ();
		r.lit_shader = *(Shader *) grl_shader_get_handle (caster);
		r.unlit_shader = r.lit_shader;
	}
	self->white_albedo = FALSE;
	for (p = 0; p < n; p++)
	{
		if (self->gpu && !lrg_shadow_renderer_begin_pass (self->shadows, p))
			continue;
		self->shadow_passes++;
		lrg_render_list_foreach (list, LRG_RENDER_CATEGORY_SHADOW_CASTER, replay_item, &r);
		if (self->gpu)
		{
			rlDrawRenderBatchActive ();
			lrg_shadow_renderer_end_pass (self->shadows);
		}
	}
	if (!self->gpu)
		return TRUE;
	bind_lod_fade (self, r.lit_shader.id, 0.0f);
	enter_target (outer, w, h);
	return lrg_shadow_renderer_is_ready (self->shadows);
}

/* The prepass recording: opaque and cutout items with the depth shader. */
static void
prepass_draw (LrgDepthPrepass *prepass,
              GrlShader       *shader,
              gpointer         user_data)
{
	Replay             *r = (Replay *) user_data;
	LrgWorld3DRenderer *self = r->self;
	LrgRenderList      *list = (LrgRenderList *) r->self->list_in_flight;

	(void) prepass;
	if (shader != NULL)
	{
		r->lit_shader = *(Shader *) grl_shader_get_handle (shader);
		r->unlit_shader = r->lit_shader;
	}
	lrg_render_list_foreach (list, LRG_RENDER_CATEGORY_OPAQUE, replay_item, r);
	lrg_render_list_foreach (list, LRG_RENDER_CATEGORY_CUTOUT, replay_item, r);
	rlDrawRenderBatchActive ();
	bind_lod_fade (self, r->lit_shader.id, 0.0f);
}

/*
 * Records the depth (and view-normal) prepass of @v and, when SSAO is on,
 * runs it.  Leaves the caller's target and re-enters @outer.  Headless
 * only counts the prepass draws.
 *
 * Returns: whether the prepass holds this frame's depth
 */
static gboolean
render_prepass (LrgWorld3DRenderer   *self,
                LrgRenderList        *list,
                const LrgWorld3DView *v,
                const gfloat         *proj,
                gboolean              want_ssao,
                guint                 outer,
                gint                  w,
                gint                  h,
                gboolean             *out_ssao)
{
	LrgWorld3DViewCamera *cam;
	Replay                r;
	gboolean              ok = FALSE;
	gint64                t0;

	*out_ssao = FALSE;
	memset (&r, 0, sizeof r);
	r.self = self;
	r.pass = LRG_WORLD3D_RENDER_PASS_PREPASS;
	r.mask = v->cull_mask;
	r.casters_only = TRUE;
	self->list_in_flight = list;

	if (!self->gpu)
	{
		lrg_render_list_foreach (list, LRG_RENDER_CATEGORY_OPAQUE, replay_item, &r);
		lrg_render_list_foreach (list, LRG_RENDER_CATEGORY_CUTOUT, replay_item, &r);
		if (want_ssao)
		{
			prof_begin ("world3d.ssao");
			if (capture_note_special (self, "ssao", LRG_FRAME_CALL_KIND_SSAO, "ssao", 0, 0))
				self->pass_counts[LRG_WORLD3D_RENDER_PASS_SSAO] = 1;
			prof_end ("world3d.ssao");
		}
		self->list_in_flight = NULL;
		return TRUE;
	}

	t0 = pass_clock (self);
	if (self->view_camera == NULL)
		self->view_camera = (LrgCamera *) g_object_new (LRG_TYPE_WORLD3D_VIEW_CAMERA, NULL);
	cam = LRG_WORLD3D_VIEW_CAMERA (self->view_camera);
	memcpy (cam->view, self->cur_view, sizeof cam->view);
	memcpy (cam->proj, proj, sizeof cam->proj);
	lrg_camera_set_cull_mask (self->view_camera, LRG_RENDER_LAYERS_MASK_ALL);

	rlDrawRenderBatchActive ();
	EndTextureMode ();
	lrg_depth_prepass_resize (self->prepass, w, h);
	lrg_depth_prepass_set_alpha_cutoff (self->prepass, CUTOUT_ALPHA);
	self->white_albedo = FALSE;
	ok = lrg_depth_prepass_record (self->prepass, self->view_camera, prepass_draw, &r);
	self->list_in_flight = NULL;
	pass_clock_end (self, LRG_WORLD3D_RENDER_PASS_PREPASS, t0);

	if (ok && want_ssao)
	{
		prof_begin ("world3d.ssao");
		t0 = pass_clock (self);
		if (capture_note_special (self, "ssao", LRG_FRAME_CALL_KIND_SSAO, "ssao", 0, outer))
		{
			*out_ssao = lrg_ssao_render (self->ssao, self->prepass);
			if (*out_ssao)
				self->pass_counts[LRG_WORLD3D_RENDER_PASS_SSAO] = 1;
		}
		pass_clock_end (self, LRG_WORLD3D_RENDER_PASS_SSAO, t0);
		prof_end ("world3d.ssao");
	}
	enter_target (outer, w, h);
	return ok;
}

/* (Re)creates a processor's scene image at @w x @h (half float first). */
static PostTargets *
ensure_post_targets (LrgWorld3DRenderer *self,
                     LrgPostProcessor   *processor,
                     gint                w,
                     gint                h)
{
	PostTargets *t = (PostTargets *) g_object_get_qdata (G_OBJECT (processor), post_targets_quark ());

	if (t == NULL)
	{
		t = g_new0 (PostTargets, 1);
		g_object_set_qdata_full (G_OBJECT (processor), post_targets_quark (), t, post_targets_free);
		g_object_weak_ref (G_OBJECT (processor), post_owner_gone, self);
		self->post_owners = g_slist_prepend (self->post_owners, processor);
	}
	if (t->scene.id != 0 && t->width == w && t->height == h)
		return t;

	/* Creating a render texture binds framebuffers: flush first */
	rlDrawRenderBatchActive ();
	if (t->scene.id != 0)
		UnloadRenderTexture (t->scene);
	t->scene = LoadRenderTextureEx (w, h, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
	t->hdr = TRUE;
	if (t->scene.id == 0 || t->scene.texture.id == 0 || !rlFramebufferComplete (t->scene.id))
	{
		/* No half-float colour attachments: an 8-bit scene */
		if (t->scene.id != 0 || t->scene.texture.id != 0)
			UnloadRenderTexture (t->scene);
		t->scene = LoadRenderTexture (w, h);
		t->hdr = FALSE;
	}
	t->width = w;
	t->height = h;
	if (t->scene.id == 0)
		return NULL;
	SetTextureFilter (t->scene.texture, TEXTURE_FILTER_BILINEAR);
	SetTextureWrap (t->scene.texture, TEXTURE_WRAP_CLAMP);
	return t;
}

/*
 * The post processor this frame runs, or NULL: the post stack in effect
 * (the renderer's, else the world's) built into the internal processor,
 * else #LrgWorld3DRenderer:post-processor.
 */
static LrgPostProcessor *
frame_post_processor (LrgWorld3DRenderer *self)
{
	LrgPostStack *stack = self->post_stack;

	if ((self->flags & LRG_WORLD3D_RENDER_FLAG_POST) == 0)
		return NULL;
	if (stack == NULL && self->frame_world != NULL)
		stack = lrg_world3d_get_post_stack (self->frame_world);
	if (stack == NULL)
		stack = self->fallback_stack;
	if (stack != NULL)
	{
		g_autoptr(GError) error = NULL;

		/* Building every frame costs only property sets when the
		 * effects already match (volumes rely on that too) */
		if (self->stack_post == NULL)
			self->stack_post = lrg_post_processor_new (1, 1);
		if (!lrg_post_stack_build (stack, self->stack_post, &error))
		{
			g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Post stack: %s", error->message);
			return self->post;
		}
		return self->stack_post;
	}
	return self->post;
}

/*
 * The post chain: lrg_post_processor_process() over the scene image, the
 * result copied into the outer target.
 */
static void
apply_post (LrgWorld3DRenderer *self,
            LrgPostProcessor   *processor,
            PostTargets        *t,
            guint               outer,
            gint                w,
            gint                h)
{
	guint result;

	rlDrawRenderBatchActive ();
	lrg_post_processor_resize (processor, (guint) w, (guint) h);
	result = lrg_post_processor_process (processor, t->scene.texture.id, IsWindowReady () ? GetFrameTime () : 0.0f);

	enter_target (outer, w, h);
	rlDisableColorBlend ();
	lrg_post_effect_draw_fullscreen (result, (guint) w, (guint) h);
	rlDrawRenderBatchActive ();
	rlEnableColorBlend ();
	self->pass_counts[LRG_WORLD3D_RENDER_PASS_POST] = MAX (lrg_post_processor_get_pass_count (processor), 1u);
}

/* ==========================================================================
 * Environment, decals, debug views
 * ========================================================================== */

/* Decal textures of components without an asset manager. */
static GrlTexture *
decal_texture (const gchar *path,
               gpointer     user_data)
{
	return lrg_world3d_renderer_get_texture_internal (LRG_WORLD3D_RENDERER (user_data), path);
}

/*
 * Puts the environment in effect on the light set: its ambient and fog
 * (lrg_environment_apply_to_light_set()), its IBL maps (baked lazily
 * here; the bake leaves the caller's target, which is re-entered) and the
 * probe reference point.  Without one, the renderer's ambient stays and
 * fog an earlier environment set is cleared.
 *
 * Returns: (nullable): the environment in effect
 */
static LrgEnvironment *
setup_environment (LrgWorld3DRenderer   *self,
                   const LrgWorld3DView *v,
                   guint                 outer,
                   gint                  w,
                   gint                  h)
{
	LrgEnvironment *env = self->environment;

	if (env == NULL && self->frame_world != NULL)
		env = lrg_world3d_get_environment (self->frame_world);
	if (env == NULL)
		env = self->fallback_env;
	g_set_object (&self->active_env, env);

	if (env == NULL)
	{
		if (self->env_fog)
			lrg_light_set_clear_fog (self->lights);
		self->env_fog = FALSE;
		lrg_light_set_set_environment (self->lights, NULL);
		return NULL;
	}

	if (self->gpu && lrg_environment_needs_bake (env))
	{
		g_autoptr(GError) error = NULL;

		rlDrawRenderBatchActive ();
		if (!lrg_environment_bake (env, &error))
			g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Environment bake failed: %s", error->message);
		enter_target (outer, w, h);
	}
	lrg_environment_apply_to_light_set (env, self->lights);
	self->env_fog = TRUE;
	lrg_environment_set_reference_point (env, v->position[0], v->position[1], v->position[2]);
	lrg_light_set_set_environment (self->lights,
	                               (self->ibl_enabled && lrg_environment_get_ibl_enabled (env) &&
	                                (!self->gpu || lrg_environment_is_baked (env))) ? env : NULL);
	return env;
}

/*
 * Selects this frame's decals: the world's set refilled from its decal
 * components (world renders), else the renderer's own set; bound to the
 * light set when decals are on and any were selected.
 */
static void
setup_decals (LrgWorld3DRenderer   *self,
              const LrgWorld3DView *v)
{
	LrgDecalSet *set = self->decals;
	gdouble      origin[3];

	if (self->frame_world != NULL)
	{
		lrg_decal_component_sync_world (self->frame_world, decal_texture, self);
		set = lrg_decal_component_get_world_set (self->frame_world);
	}
	if (!self->decals_enabled || set == NULL || lrg_decal_set_get_count (set) == 0)
	{
		lrg_light_set_set_decals (self->lights, NULL);
		return;
	}
	lrg_light_set_get_origin (self->lights, &origin[0], &origin[1], &origin[2]);
	lrg_decal_set_set_origin (set, origin[0], origin[1], origin[2]);
	lrg_decal_set_select (set, v->position[0], v->position[1], v->position[2]);
	lrg_light_set_set_decals (self->lights, lrg_decal_set_get_active_count (set) > 0 ? set : NULL);
}

/*
 * One of the lit-library debug programs (0 complexity, 1 AO, 2
 * reflections), compiled on first use; NULL when it does not compile.
 */
static GrlShader *
debug_lit_shader (LrgWorld3DRenderer *self,
                  guint               which)
{
	static const gchar *const mains[3] = {
		shader_dbg_complexity_main, shader_dbg_ao_main, shader_dbg_reflections_main
	};
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fs = NULL;
	Shader           *handle;

	if (self->dbg_lit[which] != NULL || self->dbg_tried[which])
		return self->dbg_lit[which];
	self->dbg_tried[which] = TRUE;

	fs = g_strconcat (shader_dbg_head, lrg_light_set_get_glsl_library (), mains[which], NULL);
	self->dbg_lit[which] = grl_shader_new_from_memory (lrg_light_set_get_vertex_source (), fs, &error);
	if (self->dbg_lit[which] == NULL)
	{
		g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Debug view shader %u: %s", which,
		       error != NULL ? error->message : "failed");
		return NULL;
	}
	handle = (Shader *) grl_shader_get_handle (self->dbg_lit[which]);
	if (handle == NULL || handle->id == 0 || handle->id == rlGetShaderIdDefault ())
	{
		g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Debug view shader %u did not compile", which);
		g_clear_object (&self->dbg_lit[which]);
	}
	return self->dbg_lit[which];
}

/* Frees the debug programs (with a context) and forgets them. */
static void
release_debug_shaders (LrgWorld3DRenderer *self,
                       gboolean            gl)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (self->dbg_lit); i++)
	{
		if (self->dbg_lit[i] != NULL)
		{
			if (self->lights != NULL)
				lrg_light_set_forget_shader (self->lights, self->dbg_lit[i]);
			if (gl)
				g_clear_object (&self->dbg_lit[i]);
			else
				self->dbg_lit[i] = NULL;   /* leaked with the dead context */
		}
		self->dbg_tried[i] = FALSE;
	}
}

/* Whether @mode is one of the light-set debug views. */
static gboolean
mode_is_lit_debug (LrgWorld3DRenderMode mode)
{
	return mode == LRG_WORLD3D_RENDER_MODE_LIGHT_COMPLEXITY || mode == LRG_WORLD3D_RENDER_MODE_AO ||
	       mode == LRG_WORLD3D_RENDER_MODE_REFLECTIONS;
}

/* ==========================================================================
 * The render
 * ========================================================================== */

/* Replays one colour category. */
static void
replay_category (LrgWorld3DRenderer   *self,
                 LrgRenderList        *list,
                 LrgRenderCategory     category,
                 LrgWorld3DRenderPass  pass,
                 Replay               *r)
{
	gint64 t0 = pass_clock (self);

	r->pass = pass;
	lrg_render_list_foreach (list, category, replay_item, r);
	if (self->gpu)
		rlDrawRenderBatchActive ();
	pass_clock_end (self, pass, t0);
}

static gboolean
render_core (LrgWorld3DRenderer   *self,
             LrgRenderList        *list,
             const LrgWorld3DView *in_view)
{
	LrgWorld3DView    v;
	LrgEnvironment   *env;
	LrgPostProcessor *processor = NULL;
	GrlShader        *light_shader = NULL;
	GrlShader        *debug_shader = NULL;
	PostTargets      *post = NULL;
	Replay            r;
	gfloat            proj_m[16];
	gboolean          wire, lit_mode, lights_mode, perspective, shadows_ok = FALSE, batched = FALSE;
	gboolean          want_ssao, want_volumetric = FALSE, prepass_ok = FALSE, ssao_ok = FALSE;
	gboolean          overdraw, debug_view;
	guint             outer = 0, target;
	gint              iw, ih;
	gint64            t0;

	self->in_render = TRUE;
	stats_reset (self);
	memset (self->pass_time, 0, sizeof self->pass_time);
	self->gpu = !self->headless && ensure_gl (self);
	self->last_list = list;

	v = *in_view;
	v.width = MAX (v.width, 1.0f);
	v.height = MAX (v.height, 1.0f);
	iw = (gint) v.width;
	ih = (gint) v.height;
	self->last_view = v;
	self->have_view = TRUE;
	if (self->capture != NULL)
		lrg_frame_capture_begin_frame (self->capture, iw, ih);
	wire = (self->mode == LRG_WORLD3D_RENDER_MODE_WIREFRAME);
	overdraw = (self->mode == LRG_WORLD3D_RENDER_MODE_OVERDRAW);
	debug_view = overdraw || self->mode == LRG_WORLD3D_RENDER_MODE_UV || mode_is_lit_debug (self->mode);
	/* lit_mode: shaded by the light set; lights_mode: any program that
	 * reads the light set's uniforms (the lit-library debug views too) */
	lit_mode = (self->mode == LRG_WORLD3D_RENDER_MODE_LIT || self->mode == LRG_WORLD3D_RENDER_MODE_LIGHTING_ONLY);
	lights_mode = lit_mode || mode_is_lit_debug (self->mode);
	perspective = !v.orthographic;
	if (self->gpu)
		outer = rlGetActiveFramebuffer ();
	lrg_world3d_view_get_view_matrix (&v, self->cur_view);
	lrg_world3d_view_get_projection_matrix (&v, proj_m);
	memcpy (self->cur_proj, proj_m, sizeof proj_m);

	/* --- Lights, environment, decals ------------------------------------ */
	sync_from_list (self, list);
	env = setup_environment (self, &v, outer, iw, ih);
	setup_decals (self, &v);
	/* Lighting-only shows the light on white: no decal albedo */
	if (self->mode == LRG_WORLD3D_RENDER_MODE_LIGHTING_ONLY)
		lrg_light_set_set_decals (self->lights, NULL);
	if (lights_mode && self->gpu)
		light_shader = lrg_light_set_get_shader (self->lights);
	if (self->gpu && mode_is_lit_debug (self->mode))
		debug_shader = debug_lit_shader (self, (guint) (self->mode - LRG_WORLD3D_RENDER_MODE_LIGHT_COMPLEXITY));

	/* --- Shadows ------------------------------------------------------------ */
	t0 = pass_clock (self);
	if ((self->flags & LRG_WORLD3D_RENDER_FLAG_SHADOWS) && lit_mode && perspective &&
	    (!self->gpu || light_shader != NULL))
	{
		prof_begin ("world3d.shadow");
		shadows_ok = render_shadows (self, list, &v, outer, iw, ih);
		prof_end ("world3d.shadow");
	}
	if (self->gpu)
		lrg_light_set_set_shadow_renderer (self->lights, shadows_ok ? self->shadows : NULL);
	pass_clock_end (self, LRG_WORLD3D_RENDER_PASS_SHADOW, t0);

	/* --- Depth prepass, SSAO ---------------------------------------------- */
	/* SSAO when it is on (lit modes), and always for the AO view, which
	 * would show nothing without it */
	want_ssao = perspective && ((self->ssao_enabled && lit_mode) || self->mode == LRG_WORLD3D_RENDER_MODE_AO);
	if (env != NULL && self->volumetric_enabled && lit_mode && perspective)
		want_volumetric = lrg_volumetric_fog_set_from_environment (self->vfog, env);
	if (want_ssao || want_volumetric)
	{
		prof_begin ("world3d.prepass");
		prepass_ok = render_prepass (self, list, &v, proj_m, want_ssao, outer, iw, ih, &ssao_ok);
		prof_end ("world3d.prepass");
	}
	if (self->gpu)
	{
		lrg_ssao_set_viewport (self->ssao, 0, 0, 0, 0);
		lrg_light_set_set_ssao (self->lights, ssao_ok ? self->ssao : NULL);
	}

	/* --- Where the scene goes: the post image or the target itself ----- */
	processor = (self->gpu && !debug_view) ? frame_post_processor (self) : NULL;
	if (processor != NULL && lrg_post_processor_get_effect_count (processor) > 0)
		post = ensure_post_targets (self, processor, iw, ih);
	else if ((self->flags & LRG_WORLD3D_RENDER_FLAG_POST) && !self->gpu)
		self->pass_counts[LRG_WORLD3D_RENDER_PASS_POST] = 0;
	target = (post != NULL) ? post->scene.id : outer;
	if (post != NULL)
		enter_target (post->scene.id, iw, ih);

	if (self->gpu && (self->flags & LRG_WORLD3D_RENDER_FLAG_CLEAR))
	{
		Color bg = { self->background.r, self->background.g, self->background.b, self->background.a };

		/* Debug views clear to black so their colours read */
		if (debug_view)
			bg = (Color) { 0, 0, 0, 255 };
		if (capture_note_special (self, "clear", LRG_FRAME_CALL_KIND_CLEAR, "clear", 0, target))
			ClearBackground (bg);
	}

	/* --- 3D pass ----------------------------------------------------------
	 * Our own matrices (the editor's picking uses the same ones). */
	if (self->gpu)
		begin_3d (proj_m, self->cur_view);

	/* The sky first, behind everything (perspective views, not the debug
	 * views): the environment's sky, else the sky dome */
	if ((self->flags & LRG_WORLD3D_RENDER_FLAG_SKY) && perspective && !debug_view)
	{
		gboolean draw_sky = capture_note_special (self, "sky", LRG_FRAME_CALL_KIND_SKY, "sky", 0, target);

		if (draw_sky)
			self->pass_counts[LRG_WORLD3D_RENDER_PASS_SKY] = 1;
		if (self->gpu && draw_sky)
		{
			GrlVector3 eye;

			prof_begin ("world3d.sky");
			t0 = pass_clock (self);
			eye.x = v.position[0];
			eye.y = v.position[1];
			eye.z = v.position[2];
			if (env != NULL)
				lrg_environment_draw_sky (env, &eye);
			else
			{
				GrlVector3 toward_sun;

				toward_sun.x = -self->sun_dir[0];
				toward_sun.y = -self->sun_dir[1];
				toward_sun.z = -self->sun_dir[2];
				lrg_sky_dome_set_sun_direction (self->sky, &toward_sun);
				lrg_sky_dome_set_radius (self->sky, CLAMP (v.far_plane * 0.5f, 1.0f, 500.0f));
				lrg_sky_dome_draw (self->sky, &eye);
			}
			rlDrawRenderBatchActive ();
			/* The sky may leave depth testing off */
			rlEnableDepthTest ();
			rlEnableDepthMask ();
			pass_clock_end (self, LRG_WORLD3D_RENDER_PASS_SKY, t0);
			prof_end ("world3d.sky");
		}
		else if (draw_sky)
		{
			prof_begin ("world3d.sky");
			prof_end ("world3d.sky");
		}
	}

	/* What the items draw with */
	memset (&r, 0, sizeof r);
	r.self = self;
	r.mask = v.cull_mask;
	if (self->gpu)
	{
		r.lit_shader = mode_shader (self, self->mode, light_shader);
		if (debug_shader != NULL)
			r.lit_shader = *(Shader *) grl_shader_get_handle (debug_shader);
		r.unlit_shader = lit_mode ? (self->has_unlit ? self->unlit : self->flat) : r.lit_shader;
		r.materials = (light_shader != NULL && lit_mode) || debug_shader != NULL;
		r.layered = (light_shader != NULL && lit_mode);
		r.customs = (light_shader != NULL && self->mode == LRG_WORLD3D_RENDER_MODE_LIT);
		self->cur_shader = r.lit_shader;
		self->white_albedo = (self->mode == LRG_WORLD3D_RENDER_MODE_LIGHTING_ONLY);
		if (light_shader != NULL)
		{
			lrg_light_set_apply (self->lights, v.position[0], v.position[1], v.position[2]);
			self->cur_layers = LRG_RENDER_LAYERS_MASK_ALL;
			if (debug_shader != NULL)
				lrg_light_set_bind_shader (self->lights, debug_shader);
		}
		else if (lit_mode && self->has_lit)
		{
			gfloat ambient = 0.35f;

			SetShaderValue (self->lit, self->loc_light, self->sun_dir, SHADER_UNIFORM_VEC3);
			SetShaderValue (self->lit, self->loc_ambient, &ambient, SHADER_UNIFORM_FLOAT);
		}
		/* Probes are picked per item when the bound environment has any */
		self->probe_env = (light_shader != NULL &&
		                   lrg_light_set_get_environment (self->lights) != NULL &&
		                   lrg_environment_get_n_probes (lrg_light_set_get_environment (self->lights)) > 0)
		                  ? lrg_light_set_get_environment (self->lights) : NULL;
		memset (self->probe_pick, 0, sizeof self->probe_pick);
		g_signal_emit (self, signals[SIGNAL_DRAW_BACKGROUND], 0, &v);
		if (wire)
			rlEnableWireMode ();
		if (overdraw)
		{
			rlDrawRenderBatchActive ();
			BeginBlendMode (BLEND_ADDITIVE);
			rlDisableDepthTest ();
		}
	}

	/* Opaque: the static batch first, then the rest front to back */
	prof_begin ("world3d.opaque");
	if ((self->flags & LRG_WORLD3D_RENDER_FLAG_BATCH_STATIC) && self->mode == LRG_WORLD3D_RENDER_MODE_LIT &&
	    (!self->gpu || light_shader != NULL))
	{
		guint    batch_shader = 0;
		gboolean draw_batch;

		if (self->gpu && light_shader != NULL)
		{
			Shader *sh = (Shader *) grl_shader_get_handle (light_shader);

			if (sh != NULL)
				batch_shader = sh->id;
		}
		draw_batch = capture_note_special (self, "batch", LRG_FRAME_CALL_KIND_BATCH, "lit",
		                                   batch_shader, self->gpu ? rlGetActiveFramebuffer () : 0);
		batched = draw_static_batch (self, list, v.cull_mask, light_shader, draw_batch);
	}
	r.skip_batched = batched;
	replay_category (self, list, LRG_RENDER_CATEGORY_OPAQUE, LRG_WORLD3D_RENDER_PASS_OPAQUE, &r);
	prof_end ("world3d.opaque");
	r.skip_batched = FALSE;

	/* Cutout: alpha tested at 0.5 by the light-set shader */
	prof_begin ("world3d.cutout");
	if (light_shader != NULL && lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_CUTOUT) > 0)
	{
		r.cutoff = CUTOUT_ALPHA;
		if (debug_shader != NULL)
			shader_set_float (r.lit_shader, "alphaCutoff", CUTOUT_ALPHA);
		else
		{
			lrg_light_set_set_alpha_cutoff (self->lights, CUTOUT_ALPHA);
			lrg_light_set_apply (self->lights, v.position[0], v.position[1], v.position[2]);
			self->cur_layers = LRG_RENDER_LAYERS_MASK_ALL;
			memset (self->probe_pick, 0, sizeof self->probe_pick);
		}
		replay_category (self, list, LRG_RENDER_CATEGORY_CUTOUT, LRG_WORLD3D_RENDER_PASS_CUTOUT, &r);
		r.cutoff = 0.0f;
		if (debug_shader != NULL)
			shader_set_float (r.lit_shader, "alphaCutoff", 0.0f);
		else
		{
			lrg_light_set_set_alpha_cutoff (self->lights, 0.0f);
			lrg_light_set_apply (self->lights, v.position[0], v.position[1], v.position[2]);
			self->cur_layers = LRG_RENDER_LAYERS_MASK_ALL;
			memset (self->probe_pick, 0, sizeof self->probe_pick);
		}
	}
	else
		replay_category (self, list, LRG_RENDER_CATEGORY_CUTOUT, LRG_WORLD3D_RENDER_PASS_CUTOUT, &r);
	prof_end ("world3d.cutout");

	/* Volumetric fog over the opaque scene, before the blended items */
	if (want_volumetric && prepass_ok)
	{
		gboolean draw_vol = capture_note_special (self, "volumetric", LRG_FRAME_CALL_KIND_VOLUMETRIC,
		                                          "volumetric", 0, target);

		prof_begin ("world3d.volumetric");
		if (draw_vol)
			self->pass_counts[LRG_WORLD3D_RENDER_PASS_VOLUMETRIC] = 1;
		if (self->gpu && draw_vol)
		{
			g_autoptr(GError) error = NULL;

			t0 = pass_clock (self);
			EndMode3D ();
			if (!lrg_volumetric_fog_render (self->vfog, lrg_depth_prepass_get_depth_texture_id (self->prepass), iw, ih,
			                                self->cur_view, proj_m, self->lights, &error))
			{
				g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Volumetric fog: %s", error->message);
				self->pass_counts[LRG_WORLD3D_RENDER_PASS_VOLUMETRIC] = 0;
			}
			enter_target (target, iw, ih);
			begin_3d (proj_m, self->cur_view);
			pass_clock_end (self, LRG_WORLD3D_RENDER_PASS_VOLUMETRIC, t0);
		}
		prof_end ("world3d.volumetric");
	}

	/* Transparent: back to front, blended, without depth writes */
	prof_begin ("world3d.transparent");
	if (self->gpu && !overdraw)
		rlDisableDepthMask ();
	replay_category (self, list, LRG_RENDER_CATEGORY_TRANSPARENT, LRG_WORLD3D_RENDER_PASS_TRANSPARENT, &r);
	if (self->gpu && !overdraw)
		rlEnableDepthMask ();
	prof_end ("world3d.transparent");

	if (self->gpu)
	{
		gboolean draw_debug = FALSE;

		if (overdraw)
		{
			rlDrawRenderBatchActive ();
			EndBlendMode ();
			rlEnableDepthTest ();
		}
		/* Lights reach every layer again, nothing fades */
		if (r.layered && self->cur_layers != LRG_RENDER_LAYERS_MASK_ALL)
		{
			lrg_light_set_set_draw_layers (self->lights, LRG_RENDER_LAYERS_MASK_ALL);
			self->cur_layers = LRG_RENDER_LAYERS_MASK_ALL;
		}
		bind_lod_fade (self, r.lit_shader.id, 0.0f);
		if (light_shader != NULL)
			bind_lod_fade (self, ((Shader *) grl_shader_get_handle (light_shader))->id, 0.0f);

		self->probe_env = NULL;

		/* Overlays still in the 3D mode (and in wire mode, as before) */
		g_signal_emit (self, signals[SIGNAL_DRAW_OVERLAY], 0, &v);
		rlDrawRenderBatchActive ();
		if (wire)
			rlDisableWireMode ();

		if ((self->flags & LRG_WORLD3D_RENDER_FLAG_DEBUG_DRAW) && self->debug_draw != NULL)
		{
			prof_begin ("world3d.debug");
			draw_debug = capture_note_special (self, "debug", LRG_FRAME_CALL_KIND_DEBUG, "debug", 0, target);
			if (draw_debug)
				lrg_debug_draw_draw_3d (self->debug_draw);
			prof_end ("world3d.debug");
		}

		self->white_albedo = FALSE;
		self->cur_shader = self->has_lit ? self->lit : self->flat;

		/* Back to the target's 2D projection (EndMode3D pops ours) */
		EndMode3D ();

		if (draw_debug)
			lrg_debug_draw_draw_labels (self->debug_draw);

		if (post != NULL)
		{
			prof_begin ("world3d.post");
			t0 = pass_clock (self);
			if (capture_note_special (self, "post", LRG_FRAME_CALL_KIND_POST, "post", 0, outer))
				apply_post (self, processor, post, outer, iw, ih);
			else
				enter_target (outer, iw, ih);
			pass_clock_end (self, LRG_WORLD3D_RENDER_PASS_POST, t0);
			prof_end ("world3d.post");
		}
		rlDrawRenderBatchActive ();
	}
	if (self->capture != NULL)
	{
		/* Idle (not recording, replaying or holding a list) publishes
		 * nothing: set_named_texture would touch the hash every frame. */
		if (self->gpu && (lrg_frame_capture_is_active (self->capture) ||
		                  lrg_frame_capture_is_captured (self->capture)))
		{
			if (self->prepass != NULL)
			{
				lrg_frame_capture_set_named_texture (self->capture, "depth",
				                                     lrg_depth_prepass_get_depth_texture_id (self->prepass), iw, ih);
				lrg_frame_capture_set_named_texture (self->capture, "normal",
				                                     lrg_depth_prepass_get_normal_texture_id (self->prepass), iw, ih);
			}
			if (ssao_ok && self->ssao != NULL)
				lrg_frame_capture_set_named_texture (self->capture, "ssao",
				                                     lrg_ssao_get_texture_id (self->ssao), iw, ih);
		}
		lrg_frame_capture_end_frame (self->capture);
	}
	if (G_UNLIKELY (lrg_profiler_is_enabled (lrg_profiler_get_default ())))
	{
		LrgProfiler *profiler = lrg_profiler_get_default ();

		lrg_profiler_set_counter (profiler, "world3d.draws", (gdouble) self->drawn);
		lrg_profiler_set_counter (profiler, "world3d.shadow_passes", (gdouble) self->shadow_passes);
	}
	self->in_render = FALSE;
	return self->gpu;
}

/* Re-renders the last view into a private target so the frame debugger
 * can show calls 0..scrub.  Not called from begin_frame (that would
 * recurse); lrg_frame_capture_replay() is the only caller. */
static gboolean
renderer_replay (LrgFrameCapture *capture,
                 gint             up_to,
                 gpointer         data,
                 GError         **error)
{
	LrgWorld3DRenderer *self = (LrgWorld3DRenderer *) data;
	gint                w;
	gint                h;

	(void) up_to;
	if (self->in_render || self->headless || !self->have_view || self->last_list == NULL || !IsWindowReady ())
	{
		g_set_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_GPU,
		             "Replaying a frame needs a window and a finished render");
		return FALSE;
	}
	w = (gint) self->last_view.width;
	h = (gint) self->last_view.height;
	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;
	if (self->replay_rt.id == 0 || self->replay_rt.texture.width != w || self->replay_rt.texture.height != h)
	{
		if (self->replay_rt.id != 0)
			UnloadRenderTexture (self->replay_rt);
		memset (&self->replay_rt, 0, sizeof self->replay_rt);
		self->replay_rt = LoadRenderTexture (w, h);
	}
	if (self->replay_rt.id == 0 || self->replay_rt.texture.id == 0)
	{
		g_set_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_GPU,
		             "Could not create the replay target");
		return FALSE;
	}
	BeginTextureMode (self->replay_rt);
	render_core (self, self->last_list, &self->last_view);
	EndTextureMode ();
	lrg_frame_capture_set_replay_image (capture, self->replay_rt.texture.id, w, h);
	return TRUE;
}

/* ==========================================================================
 * Reflection probe capture
 * ========================================================================== */

typedef struct
{
	LrgWorld3DRenderer *self;
	LrgEnvironment     *env;
	LrgRenderList      *list;
	Shader              shader;
	gboolean            lit;
} ProbeCapture;

/* Draws one probe face: sky, then the lit opaque and cutout items. */
static void
probe_draw_face (LrgReflectionProbe *probe,
                 guint               face,
                 const gfloat       *view,
                 const gfloat       *projection,
                 gpointer            user_data)
{
	ProbeCapture       *pc = (ProbeCapture *) user_data;
	LrgWorld3DRenderer *self = pc->self;
	GrlVector3          eye;
	gfloat              pos[3];
	Replay              r;

	(void) face;
	(void) projection;
	lrg_reflection_probe_get_position (probe, pos);
	eye.x = pos[0];
	eye.y = pos[1];
	eye.z = pos[2];
	memcpy (self->cur_view, view, sizeof self->cur_view);
	lrg_environment_draw_sky (pc->env, &eye);
	rlDrawRenderBatchActive ();
	rlEnableDepthTest ();
	rlEnableDepthMask ();

	if (pc->lit)
		lrg_light_set_apply (self->lights, pos[0], pos[1], pos[2]);
	memset (&r, 0, sizeof r);
	r.self = self;
	r.mask = LRG_RENDER_LAYERS_MASK_ALL;
	r.lit_shader = pc->shader;
	r.unlit_shader = self->has_unlit ? self->unlit : self->flat;
	r.materials = pc->lit;
	r.pass = LRG_WORLD3D_RENDER_PASS_OPAQUE;
	self->cur_layers = LRG_RENDER_LAYERS_MASK_ALL;
	lrg_render_list_foreach (pc->list, LRG_RENDER_CATEGORY_OPAQUE, replay_item, &r);
	r.pass = LRG_WORLD3D_RENDER_PASS_CUTOUT;
	lrg_render_list_foreach (pc->list, LRG_RENDER_CATEGORY_CUTOUT, replay_item, &r);
	rlDrawRenderBatchActive ();
}

/*
 * Prepares the light set for probe faces drawn from @list lit by @env:
 * lights as a frame would set them, the environment's ambient, fog and
 * IBL, decals; no shadows or SSAO (they belong to the main view).
 */
static void
capture_setup (LrgWorld3DRenderer *self,
               ProbeCapture       *pc)
{
	GrlShader *light_shader;

	self->gpu = TRUE;
	sync_from_list (self, pc->list);
	if (lrg_environment_needs_bake (pc->env))
	{
		g_autoptr(GError) error = NULL;

		if (!lrg_environment_bake (pc->env, &error))
			g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Environment bake failed: %s", error->message);
	}
	lrg_environment_apply_to_light_set (pc->env, self->lights);
	self->env_fog = TRUE;
	lrg_light_set_set_environment (self->lights, lrg_environment_is_baked (pc->env) ? pc->env : NULL);
	lrg_light_set_set_shadow_renderer (self->lights, NULL);
	lrg_light_set_set_ssao (self->lights, NULL);
	light_shader = lrg_light_set_get_shader (self->lights);
	pc->lit = (light_shader != NULL);
	pc->shader = pc->lit ? *(Shader *) grl_shader_get_handle (light_shader) : (self->has_lit ? self->lit : self->flat);
	self->white_albedo = FALSE;
}

/* Puts the renderer back after captures. */
static void
capture_done (LrgWorld3DRenderer *self)
{
	self->cur_shader = self->has_lit ? self->lit : self->flat;
	self->cur_layers = LRG_RENDER_LAYERS_MASK_ALL;
	stats_reset (self);
}

gboolean
lrg_world3d_renderer_capture_probe (LrgWorld3DRenderer  *self,
                                    LrgReflectionProbe  *probe,
                                    LrgRenderList       *list,
                                    LrgEnvironment      *environment,
                                    GError             **error)
{
	g_autoptr(LrgEnvironment) flat = NULL;
	ProbeCapture              pc;
	gboolean                  ok;

	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE (probe), FALSE);
	g_return_val_if_fail (environment == NULL || LRG_IS_ENVIRONMENT (environment), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (self->headless || !ensure_gl (self))
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU,
		             "Probe capture needs a window");
		return FALSE;
	}
	pc.self = self;
	pc.list = (list != NULL) ? list : self->list;
	pc.env = environment;
	if (pc.env == NULL)
		pc.env = (self->environment != NULL) ? self->environment : self->active_env;
	if (pc.env == NULL)
	{
		/* No environment: the probe reflects the scene over the default
		 * environment's sky, lit by its flat ambient */
		flat = lrg_environment_new ();
		pc.env = flat;
	}
	capture_setup (self, &pc);
	ok = lrg_reflection_probe_capture (probe, probe_draw_face, &pc, error);
	capture_done (self);
	return ok;
}

guint
lrg_world3d_renderer_capture_probes (LrgWorld3DRenderer *self,
                                     LrgWorld3D         *world)
{
	ProbeCapture   pc;
	LrgWorld3DView origin_view;
	guint          i, n, captured = 0;

	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), 0);
	g_return_val_if_fail (world == NULL || LRG_IS_WORLD3D (world), 0);

	if (self->headless || !ensure_gl (self))
		return 0;

	/* The environment in effect and everything it should reflect */
	pc.env = self->environment;
	if (pc.env == NULL)
		pc.env = (world != NULL) ? lrg_world3d_get_environment (world) : self->active_env;
	if (pc.env == NULL)
		pc.env = self->fallback_env;
	if (pc.env == NULL || lrg_environment_get_n_probes (pc.env) == 0)
		return 0;
	if (world != NULL)
		lrg_world3d_collect_render (world, NULL, self->list);
	pc.self = self;
	pc.list = self->list;

	self->frame_world = world;
	capture_setup (self, &pc);
	lrg_world3d_view_init (&origin_view, 1.0f, 1.0f);
	setup_decals (self, &origin_view);
	n = lrg_environment_get_n_probes (pc.env);
	for (i = 0; i < n; i++)
	{
		LrgReflectionProbe *probe = lrg_environment_get_probe (pc.env, i);
		g_autoptr(GError)   error = NULL;

		if (probe == NULL)
			continue;
		if (lrg_reflection_probe_capture (probe, probe_draw_face, &pc, &error))
			captured++;
		else
			g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Probe capture failed: %s", error->message);
	}
	self->frame_world = NULL;
	capture_done (self);
	return captured;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_world3d_renderer_dispose (GObject *object)
{
	LrgWorld3DRenderer *self = LRG_WORLD3D_RENDERER (object);

	/* GPU objects only with a live context; otherwise drop bookkeeping. */
	release_resources (self, IsWindowReady ());
	if (self->capture != NULL)
		lrg_frame_capture_set_replay_func (self->capture, NULL, NULL);
	g_clear_object (&self->capture);
	release_post_targets (self);
	g_clear_object (&self->post);
	g_clear_object (&self->stack_post);
	g_clear_object (&self->post_stack);
	g_clear_object (&self->environment);
	g_clear_object (&self->fallback_env);
	g_clear_object (&self->fallback_stack);
	g_clear_object (&self->active_env);
	g_clear_object (&self->debug_draw);
	g_clear_object (&self->batch);
	g_clear_pointer (&self->list, lrg_render_list_unref);

	G_OBJECT_CLASS (lrg_world3d_renderer_parent_class)->dispose (object);
}

static void
lrg_world3d_renderer_finalize (GObject *object)
{
	LrgWorld3DRenderer *self = LRG_WORLD3D_RENDERER (object);
	guint               i;

	/* The by-asset maps borrow from the path maps: free them first. */
	g_clear_pointer (&self->model_by_asset, g_hash_table_unref);
	g_clear_pointer (&self->texture_by_asset, g_hash_table_unref);
	g_clear_pointer (&self->material_by_asset, g_hash_table_unref);
	g_clear_pointer (&self->materials, g_hash_table_unref);
	g_clear_pointer (&self->models, g_hash_table_unref);
	g_clear_pointer (&self->textures, g_hash_table_unref);
	g_clear_pointer (&self->base_dir, g_free);
	if (self->resolver_destroy != NULL)
		self->resolver_destroy (self->resolver_data);
	for (i = 0; i < LRG_WORLD3D_RENDER_PASS_COUNT; i++)
		g_clear_pointer (&self->records[i], g_ptr_array_unref);
	g_clear_object (&self->lights);
	g_clear_object (&self->shadows);
	g_clear_object (&self->sky);
	g_clear_object (&self->ssao);
	g_clear_object (&self->prepass);
	g_clear_object (&self->decals);
	g_clear_object (&self->vfog);
	g_clear_object (&self->view_camera);
	g_clear_pointer (&self->custom_shaders, g_hash_table_unref);
	g_clear_pointer (&self->lightmap_prims, g_hash_table_unref);
	g_clear_pointer (&self->faded, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_world3d_renderer_parent_class)->finalize (object);
}

static void
lrg_world3d_renderer_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
	LrgWorld3DRenderer *self = LRG_WORLD3D_RENDERER (object);

	switch (prop_id)
	{
	case PROP_RENDER_MODE:
		g_value_set_enum (value, self->mode);
		break;
	case PROP_FLAGS:
		g_value_set_flags (value, self->flags);
		break;
	case PROP_POST_PROCESSOR:
		g_value_set_object (value, self->post);
		break;
	case PROP_DEBUG_DRAW:
		g_value_set_object (value, self->debug_draw);
		break;
	case PROP_BASE_DIR:
		g_value_set_string (value, self->base_dir);
		break;
	case PROP_HEADLESS:
		g_value_set_boolean (value, self->headless);
		break;
	case PROP_FALLBACK_SUN:
		g_value_set_boolean (value, self->fallback_sun);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_world3d_renderer_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
	LrgWorld3DRenderer *self = LRG_WORLD3D_RENDERER (object);

	switch (prop_id)
	{
	case PROP_RENDER_MODE:
		lrg_world3d_renderer_set_render_mode (self, (LrgWorld3DRenderMode) g_value_get_enum (value));
		break;
	case PROP_FLAGS:
		lrg_world3d_renderer_set_flags (self, (LrgWorld3DRenderFlags) g_value_get_flags (value));
		break;
	case PROP_POST_PROCESSOR:
		lrg_world3d_renderer_set_post_processor (self, g_value_get_object (value));
		break;
	case PROP_DEBUG_DRAW:
		lrg_world3d_renderer_set_debug_draw (self, g_value_get_object (value));
		break;
	case PROP_BASE_DIR:
		lrg_world3d_renderer_set_base_dir (self, g_value_get_string (value));
		break;
	case PROP_HEADLESS:
		lrg_world3d_renderer_set_headless (self, g_value_get_boolean (value));
		break;
	case PROP_FALLBACK_SUN:
		lrg_world3d_renderer_set_fallback_sun (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_world3d_renderer_class_init (LrgWorld3DRendererClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_world3d_renderer_dispose;
	object_class->finalize = lrg_world3d_renderer_finalize;
	object_class->get_property = lrg_world3d_renderer_get_property;
	object_class->set_property = lrg_world3d_renderer_set_property;

	/**
	 * LrgWorld3DRenderer:render-mode:
	 *
	 * How the colour passes shade surfaces.
	 */
	properties[PROP_RENDER_MODE] =
		g_param_spec_enum ("render-mode", NULL, NULL, LRG_TYPE_WORLD3D_RENDER_MODE, LRG_WORLD3D_RENDER_MODE_LIT,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWorld3DRenderer:flags:
	 *
	 * The optional parts of a render (shadows, sky, post, clear, debug
	 * draw, static batching).
	 */
	properties[PROP_FLAGS] =
		g_param_spec_flags ("flags", NULL, NULL, LRG_TYPE_WORLD3D_RENDER_FLAGS, LRG_WORLD3D_RENDER_FLAGS_DEFAULT,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWorld3DRenderer:post-processor:
	 *
	 * The post chain %LRG_WORLD3D_RENDER_FLAG_POST runs (see
	 * lrg_world3d_renderer_set_post_processor()).
	 */
	properties[PROP_POST_PROCESSOR] =
		g_param_spec_object ("post-processor", NULL, NULL, LRG_TYPE_POST_PROCESSOR,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWorld3DRenderer:debug-draw:
	 *
	 * The debug draw shown with %LRG_WORLD3D_RENDER_FLAG_DEBUG_DRAW.
	 */
	properties[PROP_DEBUG_DRAW] =
		g_param_spec_object ("debug-draw", NULL, NULL, LRG_TYPE_DEBUG_DRAW,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWorld3DRenderer:base-dir:
	 *
	 * The directory relative asset paths resolve against (%NULL: the
	 * working directory).
	 */
	properties[PROP_BASE_DIR] =
		g_param_spec_string ("base-dir", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWorld3DRenderer:headless:
	 *
	 * When set, renders run every pass without touching the GPU and only
	 * record what they would draw (also the behaviour without a window).
	 */
	properties[PROP_HEADLESS] =
		g_param_spec_boolean ("headless", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgWorld3DRenderer:fallback-sun:
	 *
	 * Whether a scene without an enabled directional light is lit by the
	 * default sun (on: the editor's behaviour, so nothing renders black).
	 */
	properties[PROP_FALLBACK_SUN] =
		g_param_spec_boolean ("fallback-sun", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgWorld3DRenderer::draw-background:
	 * @self: the renderer
	 * @view: (type LrgWorld3DView): the view being drawn
	 *
	 * Emitted inside the 3D pass after the sky and before the opaque
	 * items, with the view and projection loaded and depth testing on
	 * (the editor draws its grid here).  GPU renders only.
	 */
	signals[SIGNAL_DRAW_BACKGROUND] =
		g_signal_new ("draw-background", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, LRG_TYPE_WORLD3D_VIEW | G_SIGNAL_TYPE_STATIC_SCOPE);

	/**
	 * LrgWorld3DRenderer::draw-overlay:
	 * @self: the renderer
	 * @view: (type LrgWorld3DView): the view being drawn
	 *
	 * Emitted inside the 3D pass after the transparent items and before
	 * the debug draw (still in wire mode for
	 * %LRG_WORLD3D_RENDER_MODE_WIREFRAME): the editor draws node icons,
	 * selection bounds, tilemaps and placeholders here.  GPU renders only.
	 */
	signals[SIGNAL_DRAW_OVERLAY] =
		g_signal_new ("draw-overlay", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, LRG_TYPE_WORLD3D_VIEW | G_SIGNAL_TYPE_STATIC_SCOPE);
}


/* Cache value destructor: values may be NULL (a failed load). */
static void
object_unref_nullable (gpointer data)
{
	if (data != NULL)
		g_object_unref (data);
}

static void
lrg_world3d_renderer_init (LrgWorld3DRenderer *self)
{
	guint i;

	self->mode = LRG_WORLD3D_RENDER_MODE_LIT;
	self->flags = LRG_WORLD3D_RENDER_FLAGS_DEFAULT;
	self->background.r = 30;
	self->background.g = 30;
	self->background.b = 46;
	self->background.a = 255;
	self->ambient.r = 58;
	self->ambient.g = 62;
	self->ambient.b = 78;
	self->ambient.a = 255;
	self->fallback_sun = TRUE;

	self->models = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, object_unref_nullable);
	self->textures = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, object_unref_nullable);
	self->model_by_asset = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->texture_by_asset = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->materials = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, object_unref_nullable);
	self->material_by_asset = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (i = 0; i < LRG_WORLD3D_RENDER_PASS_COUNT; i++)
		self->records[i] = g_ptr_array_new ();
	self->list = lrg_render_list_new ();

	/* Engine lighting objects: headless bookkeeping, GPU parts on use */
	self->lights = lrg_light_set_new ();
	self->shadows = lrg_shadow_renderer_new ();
	lrg_shadow_renderer_set_sun (self->shadows, 2048, 3, 60.0f, 0.6f);
	lrg_shadow_renderer_set_filter (self->shadows, 1);
	lrg_shadow_renderer_set_strength (self->shadows, 0.85f);
	self->sky = lrg_sky_dome_new ();
	self->ssao = lrg_ssao_new ();
	self->prepass = lrg_depth_prepass_new (1, 1);
	self->decals = lrg_decal_set_new ();
	self->vfog = lrg_volumetric_fog_new ();
	self->decals_enabled = TRUE;
	self->ibl_enabled = TRUE;
	self->volumetric_enabled = TRUE;
	self->faded = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->cur_layers = LRG_RENDER_LAYERS_MASK_ALL;
	self->custom_shaders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, object_unref_nullable);
	self->lightmap_prims = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, lightmap_mesh_free);
	self->sun_dir[0] = DEFAULT_SUN_X;
	self->sun_dir[1] = DEFAULT_SUN_Y;
	self->sun_dir[2] = DEFAULT_SUN_Z;
	normalize3 (self->sun_dir);
}

/* ==========================================================================
 * Public API: environment, post stack, SSAO, decals (0.9.0)
 * ========================================================================== */

void
lrg_world3d_renderer_set_environment (LrgWorld3DRenderer *self,
                                      LrgEnvironment     *environment)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));
	g_return_if_fail (environment == NULL || LRG_IS_ENVIRONMENT (environment));

	g_set_object (&self->environment, environment);
}

LrgEnvironment *
lrg_world3d_renderer_get_environment (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	return self->environment;
}

LrgEnvironment *
lrg_world3d_renderer_get_active_environment (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	return self->active_env;
}

void
lrg_world3d_renderer_set_post_stack (LrgWorld3DRenderer *self,
                                     LrgPostStack       *stack)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));
	g_return_if_fail (stack == NULL || LRG_IS_POST_STACK (stack));

	g_set_object (&self->post_stack, stack);
}

LrgPostStack *
lrg_world3d_renderer_get_post_stack (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	return self->post_stack;
}

void
lrg_world3d_renderer_set_fallback_look (LrgWorld3DRenderer *self,
                                        LrgEnvironment     *environment,
                                        LrgPostStack       *stack)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));
	g_return_if_fail (environment == NULL || LRG_IS_ENVIRONMENT (environment));
	g_return_if_fail (stack == NULL || LRG_IS_POST_STACK (stack));

	g_set_object (&self->fallback_env, environment);
	g_set_object (&self->fallback_stack, stack);
}

void
lrg_world3d_renderer_set_ssao_enabled (LrgWorld3DRenderer *self,
                                       gboolean            enabled)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));

	self->ssao_enabled = enabled ? TRUE : FALSE;
}

gboolean
lrg_world3d_renderer_get_ssao_enabled (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);

	return self->ssao_enabled;
}

LrgSsao *
lrg_world3d_renderer_get_ssao (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	return self->ssao;
}

LrgDecalSet *
lrg_world3d_renderer_get_decal_set (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	return self->decals;
}

void
lrg_world3d_renderer_set_decals_enabled (LrgWorld3DRenderer *self,
                                         gboolean            enabled)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));

	self->decals_enabled = enabled ? TRUE : FALSE;
}

gboolean
lrg_world3d_renderer_get_decals_enabled (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);

	return self->decals_enabled;
}

void
lrg_world3d_renderer_set_ibl_enabled (LrgWorld3DRenderer *self,
                                      gboolean            enabled)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));

	self->ibl_enabled = enabled ? TRUE : FALSE;
}

gboolean
lrg_world3d_renderer_get_ibl_enabled (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);

	return self->ibl_enabled;
}

void
lrg_world3d_renderer_set_volumetric_enabled (LrgWorld3DRenderer *self,
                                             gboolean            enabled)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));

	self->volumetric_enabled = enabled ? TRUE : FALSE;
}

gboolean
lrg_world3d_renderer_get_volumetric_enabled (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);

	return self->volumetric_enabled;
}

gboolean
lrg_world3d_renderer_bake_environment (LrgWorld3DRenderer  *self,
                                       GError             **error)
{
	LrgEnvironment *env;

	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	env = (self->environment != NULL) ? self->environment : self->active_env;
	if (env == NULL || !IsWindowReady () || !lrg_environment_needs_bake (env))
		return TRUE;
	return lrg_environment_bake (env, error);
}

gdouble
lrg_world3d_renderer_get_pass_time (LrgWorld3DRenderer   *self,
                                    LrgWorld3DRenderPass  pass)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), 0.0);
	g_return_val_if_fail ((guint) pass < LRG_WORLD3D_RENDER_PASS_COUNT, 0.0);

	return self->pass_time[pass];
}

void
lrg_world3d_renderer_set_timing (LrgWorld3DRenderer *self,
                                 gboolean            timing)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));

	self->timing = timing ? TRUE : FALSE;
}

/* ==========================================================================
 * Public API: rendering
 * ========================================================================== */

LrgWorld3DRenderer *
lrg_world3d_renderer_new (void)
{
	return (LrgWorld3DRenderer *) g_object_new (LRG_TYPE_WORLD3D_RENDERER, NULL);
}

gboolean
lrg_world3d_renderer_render (LrgWorld3DRenderer *self,
                             LrgWorld3D         *world,
                             LrgCamera3D        *camera,
                             GrlRenderTexture   *target,
                             gint                width,
                             gint                height)
{
	LrgWorld3DView view;
	gboolean       drawn;
	gint           w = width, h = height;

	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);
	g_return_val_if_fail (LRG_IS_WORLD3D (world), FALSE);
	g_return_val_if_fail (camera == NULL || LRG_IS_CAMERA3D (camera), FALSE);
	g_return_val_if_fail (target == NULL || GRL_IS_RENDER_TEXTURE (target), FALSE);

	/* The world's active camera when none is given */
	if (camera == NULL)
	{
		LrgCamera *active = lrg_world3d_get_active_camera (world);

		if (active == NULL || !LRG_IS_CAMERA3D (active))
			return FALSE;
		camera = LRG_CAMERA3D (active);
	}

	/* The target's size (or the screen's) unless given */
	if (w <= 0)
		w = (target != NULL) ? grl_render_texture_get_width (target) : (IsWindowReady () ? GetRenderWidth () : 1);
	if (h <= 0)
		h = (target != NULL) ? grl_render_texture_get_height (target) : (IsWindowReady () ? GetRenderHeight () : 1);

	lrg_world3d_collect_render (world, LRG_CAMERA (camera), self->list);
	lrg_world3d_view_init_from_camera (&view, camera, (gfloat) w, (gfloat) h);

	self->frame_world = world;
	if (target != NULL && !self->headless && IsWindowReady ())
	{
		grl_render_texture_begin (target);
		drawn = render_core (self, self->list, &view);
		grl_render_texture_end (target);
	}
	else
		drawn = render_core (self, self->list, &view);
	self->frame_world = NULL;
	return drawn;
}

gboolean
lrg_world3d_renderer_render_view (LrgWorld3DRenderer   *self,
                                  LrgWorld3D           *world,
                                  const LrgWorld3DView *view)
{
	gboolean drawn;

	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);
	g_return_val_if_fail (LRG_IS_WORLD3D (world), FALSE);
	g_return_val_if_fail (view != NULL, FALSE);

	/* Everything visible, sorted for this eye; the cull mask applies at
	 * draw time */
	lrg_world3d_collect_render (world, NULL, self->list);
	lrg_render_list_set_view (self->list, view->position, view->cull_mask, NULL);
	lrg_render_list_sort (self->list);
	self->frame_world = world;
	drawn = render_core (self, self->list, view);
	self->frame_world = NULL;
	return drawn;
}

gboolean
lrg_world3d_renderer_render_list (LrgWorld3DRenderer   *self,
                                  LrgRenderList        *list,
                                  const LrgWorld3DView *view)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);
	g_return_val_if_fail (list != NULL, FALSE);
	g_return_val_if_fail (view != NULL, FALSE);

	return render_core (self, list, view);
}

LrgRenderList *
lrg_world3d_renderer_get_render_list (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	return self->list;
}

/* ==========================================================================
 * Public API: settings
 * ========================================================================== */

void
lrg_world3d_renderer_set_render_mode (LrgWorld3DRenderer   *self,
                                      LrgWorld3DRenderMode  mode)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));
	g_return_if_fail ((guint) mode <= LRG_WORLD3D_RENDER_MODE_REFLECTIONS);

	if (self->mode == mode)
		return;
	self->mode = mode;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RENDER_MODE]);
}

LrgWorld3DRenderMode
lrg_world3d_renderer_get_render_mode (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), LRG_WORLD3D_RENDER_MODE_LIT);

	return self->mode;
}

void
lrg_world3d_renderer_set_flags (LrgWorld3DRenderer    *self,
                                LrgWorld3DRenderFlags  flags)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));

	if (self->flags == flags)
		return;
	self->flags = flags;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLAGS]);
}

LrgWorld3DRenderFlags
lrg_world3d_renderer_get_flags (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), LRG_WORLD3D_RENDER_FLAG_NONE);

	return self->flags;
}

void
lrg_world3d_renderer_set_background (LrgWorld3DRenderer *self,
                                     const GrlColor     *color)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));
	g_return_if_fail (color != NULL);

	self->background = *color;
}

void
lrg_world3d_renderer_get_background (LrgWorld3DRenderer *self,
                                     GrlColor           *out)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));
	g_return_if_fail (out != NULL);

	*out = self->background;
}

void
lrg_world3d_renderer_set_ambient (LrgWorld3DRenderer *self,
                                  const GrlColor     *color)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));
	g_return_if_fail (color != NULL);

	self->ambient = *color;
}

void
lrg_world3d_renderer_set_fallback_sun (LrgWorld3DRenderer *self,
                                       gboolean            fallback_sun)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));

	fallback_sun = fallback_sun ? TRUE : FALSE;
	if (self->fallback_sun == fallback_sun)
		return;
	self->fallback_sun = fallback_sun;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FALLBACK_SUN]);
}

gboolean
lrg_world3d_renderer_get_fallback_sun (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);

	return self->fallback_sun;
}

void
lrg_world3d_renderer_set_post_processor (LrgWorld3DRenderer *self,
                                         LrgPostProcessor   *processor)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));
	g_return_if_fail (processor == NULL || LRG_IS_POST_PROCESSOR (processor));

	if (g_set_object (&self->post, processor))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POST_PROCESSOR]);
}

LrgPostProcessor *
lrg_world3d_renderer_get_post_processor (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	return self->post;
}

LrgPostProcessor *
lrg_world3d_renderer_new_preview_post_processor (void)
{
	LrgPostProcessor        *proc = lrg_post_processor_new (1, 1);
	g_autoptr(LrgBloom)      bloom = lrg_bloom_new ();
	g_autoptr(LrgTonemap)    tonemap = lrg_tonemap_new ();
	g_autoptr(LrgVignette)   vignette = lrg_vignette_new ();
	g_autoptr(LrgFxaa)       fxaa = lrg_fxaa_new ();
	g_autoptr(LrgColorGrade) grade = lrg_color_grade_new ();

	/* Preview defaults: a subtle look that reads in any scene.  Bloom
	 * wants the HDR values, the tonemap rolls them off, the rest works on
	 * display values. */
	lrg_bloom_set_threshold (bloom, 0.75f);
	lrg_bloom_set_intensity (bloom, 0.6f);
	/* Filmic at +1 EV keeps display mid grey (0.5) where it was and
	 * rolls values above 1 off instead of clipping them */
	lrg_tonemap_set_operator (tonemap, LRG_TONEMAP_OPERATOR_FILMIC);
	lrg_tonemap_set_exposure (tonemap, 1.0f);
	lrg_vignette_set_intensity (vignette, 0.35f);
	lrg_vignette_set_radius (vignette, 0.55f);
	lrg_vignette_set_smoothness (vignette, 0.45f);
	lrg_color_grade_set_contrast (grade, 1.05f);
	lrg_color_grade_set_saturation (grade, 1.08f);
	lrg_post_effect_set_priority (LRG_POST_EFFECT (bloom), 0);
	lrg_post_effect_set_priority (LRG_POST_EFFECT (tonemap), 10);
	lrg_post_effect_set_priority (LRG_POST_EFFECT (vignette), 20);
	lrg_post_effect_set_priority (LRG_POST_EFFECT (fxaa), 30);
	lrg_post_effect_set_priority (LRG_POST_EFFECT (grade), 40);
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (bloom));
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (tonemap));
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (vignette));
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (fxaa));
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (grade));
	return proc;
}

void
lrg_world3d_renderer_set_debug_draw (LrgWorld3DRenderer *self,
                                     LrgDebugDraw       *debug_draw)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));
	g_return_if_fail (debug_draw == NULL || LRG_IS_DEBUG_DRAW (debug_draw));

	if (g_set_object (&self->debug_draw, debug_draw))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEBUG_DRAW]);
}

LrgDebugDraw *
lrg_world3d_renderer_get_debug_draw (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	return self->debug_draw;
}

void
lrg_world3d_renderer_set_headless (LrgWorld3DRenderer *self,
                                   gboolean            headless)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));

	headless = headless ? TRUE : FALSE;
	if (self->headless == headless)
		return;
	self->headless = headless;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEADLESS]);
}

gboolean
lrg_world3d_renderer_get_headless (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);

	return self->headless;
}

/* ==========================================================================
 * Public API: lighting
 * ========================================================================== */

guint
lrg_world3d_renderer_sync_lights (LrgWorld3DRenderer   *self,
                                  const LrgLight3DDesc *lights,
                                  guint                 n_lights)
{
	LightSync ls;
	guint     i;

	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), 0);
	g_return_val_if_fail (lights != NULL || n_lights == 0, 0);

	sync_begin (self, &ls);
	for (i = 0; i < n_lights; i++)
		sync_add (self, &ls, &lights[i]);
	return sync_end (self, &ls);
}

gboolean
lrg_world3d_renderer_has_scene_sun (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);

	return self->scene_sun;
}

void
lrg_world3d_renderer_get_sun_direction (LrgWorld3DRenderer *self,
                                        gfloat             *out)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));
	g_return_if_fail (out != NULL);

	out[0] = self->sun_dir[0];
	out[1] = self->sun_dir[1];
	out[2] = self->sun_dir[2];
}

LrgLightSet *
lrg_world3d_renderer_get_light_set (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	return self->lights;
}

LrgShadowRenderer *
lrg_world3d_renderer_get_shadow_renderer (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	return self->shadows;
}

LrgSkyDome *
lrg_world3d_renderer_get_sky_dome (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	return self->sky;
}

/* ==========================================================================
 * Public API: assets
 * ========================================================================== */

/* Relative asset strings now resolve somewhere else. */
static void
forget_lookups (LrgWorld3DRenderer *self)
{
	g_hash_table_remove_all (self->model_by_asset);
	g_hash_table_remove_all (self->texture_by_asset);
	g_hash_table_remove_all (self->material_by_asset);
	self->generation++;
}

void
lrg_world3d_renderer_set_base_dir (LrgWorld3DRenderer *self,
                                   const gchar        *dir)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));

	if (g_strcmp0 (dir, self->base_dir) == 0)
		return;
	forget_lookups (self);
	g_free (self->base_dir);
	self->base_dir = g_strdup (dir);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BASE_DIR]);
}

const gchar *
lrg_world3d_renderer_get_base_dir (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	return self->base_dir;
}

void
lrg_world3d_renderer_set_asset_resolver (LrgWorld3DRenderer      *self,
                                         LrgWorld3DAssetResolver  resolver,
                                         gpointer                 user_data,
                                         GDestroyNotify           destroy)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));

	if (self->resolver_destroy != NULL)
		self->resolver_destroy (self->resolver_data);
	self->resolver = resolver;
	self->resolver_data = user_data;
	self->resolver_destroy = destroy;
	forget_lookups (self);
}

gchar *
lrg_world3d_renderer_resolve_asset (LrgWorld3DRenderer *self,
                                    const gchar        *asset)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);
	g_return_val_if_fail (asset != NULL, NULL);

	return resolve_path (self, asset);
}

GrlModel *
lrg_world3d_renderer_get_model (LrgWorld3DRenderer *self,
                                const gchar        *asset)
{
	g_autofree gchar *abs = NULL;
	gpointer          value;
	GrlModel         *model;

	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	if (asset == NULL || *asset == '\0' || !IsWindowReady ())
		return NULL;

	/* One hash lookup per item per frame; the path is resolved (and the
	 * model loaded) on first use.  Without a base directory or resolver
	 * relative paths follow the working directory, which is not tracked,
	 * so they always take the resolving path. */
	if (self->base_dir == NULL && self->resolver == NULL && !g_path_is_absolute (asset))
	{
		abs = resolve_path (self, asset);
		return load_model_abs (self, abs);
	}
	if (g_hash_table_lookup_extended (self->model_by_asset, asset, NULL, &value))
		return (GrlModel *) value;
	abs = resolve_path (self, asset);
	model = load_model_abs (self, abs);
	g_hash_table_insert (self->model_by_asset, g_strdup (asset), model);
	return model;
}

/* Loads (or finds) the .rmat at @abs; NULL on failure, cached either way. */
static LrgMaterial *
load_material_abs (LrgWorld3DRenderer *self,
                   const gchar        *abs)
{
	g_autoptr(GError) error = NULL;
	LrgMaterial      *material;

	if (g_hash_table_contains (self->materials, abs))
		return (LrgMaterial *) g_hash_table_lookup (self->materials, abs);
	material = lrg_material_new_from_file (abs, NULL, &error);
	if (material == NULL)
		g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Could not load material %s: %s", abs,
		       error != NULL ? error->message : "failed");
	g_hash_table_insert (self->materials, g_strdup (abs), material);
	return material;
}

LrgMaterial *
lrg_world3d_renderer_get_material (LrgWorld3DRenderer *self,
                                   const gchar        *asset)
{
	g_autofree gchar *abs = NULL;
	gpointer          value;
	LrgMaterial      *material;

	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	if (asset == NULL || *asset == '\0')
		return NULL;
	/* Like lrg_world3d_renderer_get_model(); materials load headless
	 * (their textures load on first draw) */
	if (self->base_dir == NULL && self->resolver == NULL && !g_path_is_absolute (asset))
	{
		abs = resolve_path (self, asset);
		return load_material_abs (self, abs);
	}
	if (g_hash_table_lookup_extended (self->material_by_asset, asset, NULL, &value))
		return (LrgMaterial *) value;
	abs = resolve_path (self, asset);
	material = load_material_abs (self, abs);
	g_hash_table_insert (self->material_by_asset, g_strdup (asset), material);
	return material;
}

GrlTexture *
lrg_world3d_renderer_get_texture (LrgWorld3DRenderer *self,
                                  const gchar        *asset)
{
	g_autofree gchar *abs = NULL;
	gpointer          value;
	GrlTexture       *tex;

	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);

	if (asset == NULL || *asset == '\0' || !IsWindowReady ())
		return NULL;
	if (self->base_dir == NULL && self->resolver == NULL && !g_path_is_absolute (asset))
	{
		abs = resolve_path (self, asset);
		return load_texture_abs (self, abs);
	}
	if (g_hash_table_lookup_extended (self->texture_by_asset, asset, NULL, &value))
		return (GrlTexture *) value;
	abs = resolve_path (self, asset);
	tex = load_texture_abs (self, abs);
	g_hash_table_insert (self->texture_by_asset, g_strdup (asset), tex);
	return tex;
}

gboolean
lrg_world3d_renderer_is_asset_missing (LrgWorld3DRenderer *self,
                                       const gchar        *asset)
{
	g_autofree gchar *abs = NULL;

	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);

	if (asset == NULL || *asset == '\0')
		return TRUE;

	/* A load that failed is missing too (cached as NULL values) */
	abs = resolve_path (self, asset);
	if (g_hash_table_contains (self->models, abs) && g_hash_table_lookup (self->models, abs) == NULL)
		return TRUE;
	if (g_hash_table_contains (self->textures, abs) && g_hash_table_lookup (self->textures, abs) == NULL)
		return TRUE;
	return !g_file_test (abs, G_FILE_TEST_IS_REGULAR);
}

gboolean
lrg_world3d_renderer_forget_asset (LrgWorld3DRenderer *self,
                                   const gchar        *abs_path)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);

	if (abs_path == NULL)
		return FALSE;
	if (!g_hash_table_contains (self->models, abs_path) && !g_hash_table_contains (self->textures, abs_path) &&
	    !g_hash_table_contains (self->materials, abs_path))
		return FALSE;

	/* The by-asset maps borrow from the path maps, so they are cleared
	 * wholesale (they refill on the next lookup).  The static batch may
	 * have merged the old mesh: rebuild it. */
	forget_lookups (self);
	if (IsWindowReady ())
	{
		g_hash_table_remove (self->models, abs_path);
		g_hash_table_remove (self->textures, abs_path);
		g_hash_table_remove (self->materials, abs_path);
	}
	else
	{
		g_hash_table_steal (self->models, abs_path);
		g_hash_table_steal (self->textures, abs_path);
		g_hash_table_steal (self->materials, abs_path);
	}
	self->batch_signature = 0;
	return TRUE;
}

gboolean
lrg_world3d_renderer_reload_asset (LrgWorld3DRenderer *self,
                                   const gchar        *abs_path)
{
	static const gchar * const model_ext[] = { ".glb", ".gltf", ".obj", ".iqm", ".vox", ".m3d" };
	gboolean                   as_model;
	guint                      i;

	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);
	g_return_val_if_fail (abs_path != NULL, FALSE);

	as_model = g_hash_table_contains (self->models, abs_path);
	if (!as_model && !g_hash_table_contains (self->textures, abs_path))
	{
		g_autofree gchar *lower = g_ascii_strdown (abs_path, -1);

		for (i = 0; i < G_N_ELEMENTS (model_ext); i++)
			if (g_str_has_suffix (lower, model_ext[i]))
				as_model = TRUE;
	}
	lrg_world3d_renderer_forget_asset (self, abs_path);
	self->generation++;
	if (g_str_has_suffix (abs_path, ".rmat"))
		return load_material_abs (self, abs_path) != NULL;
	if (as_model)
		return load_model_abs (self, abs_path) != NULL;
	return load_texture_abs (self, abs_path) != NULL;
}

guint
lrg_world3d_renderer_get_generation (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), 0);

	return self->generation;
}

/* ==========================================================================
 * Public API: statistics and resources
 * ========================================================================== */

guint
lrg_world3d_renderer_get_drawn (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), 0);

	/* Recorded renders count their items as draws */
	if (!self->gpu)
	{
		guint i, n = 0;

		for (i = 0; i < LRG_WORLD3D_RENDER_PASS_COUNT; i++)
			if (i != LRG_WORLD3D_RENDER_PASS_SKY && i != LRG_WORLD3D_RENDER_PASS_POST)
				n += self->pass_counts[i];
		return n;
	}
	return self->drawn;
}

guint
lrg_world3d_renderer_get_pass_count (LrgWorld3DRenderer   *self,
                                     LrgWorld3DRenderPass  pass)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), 0);
	g_return_val_if_fail ((guint) pass < LRG_WORLD3D_RENDER_PASS_COUNT, 0);

	return self->pass_counts[pass];
}

LrgEntity3D *
lrg_world3d_renderer_get_recorded_entity (LrgWorld3DRenderer   *self,
                                          LrgWorld3DRenderPass  pass,
                                          guint                 index)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);
	g_return_val_if_fail ((guint) pass < LRG_WORLD3D_RENDER_PASS_COUNT, NULL);

	if (index >= self->records[pass]->len)
		return NULL;
	return (LrgEntity3D *) g_ptr_array_index (self->records[pass], index);
}

guint
lrg_world3d_renderer_get_shadow_pass_count (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), 0);

	return self->shadow_passes;
}

guint
lrg_world3d_renderer_get_batched_count (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), 0);

	return self->batched;
}

const gchar *
lrg_world3d_renderer_get_shader_source (const gchar *name)
{
	guint i;

	g_return_val_if_fail (name != NULL, NULL);

	for (i = 0; i < G_N_ELEMENTS (shader_sources); i++)
		if (g_strcmp0 (shader_sources[i].name, name) == 0)
			return shader_sources[i].source;
	return NULL;
}

void
lrg_world3d_renderer_release (LrgWorld3DRenderer *self)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));

	release_resources (self, IsWindowReady ());
}

void
lrg_world3d_renderer_set_frame_capture (LrgWorld3DRenderer *self,
                                        LrgFrameCapture    *capture)
{
	g_return_if_fail (LRG_IS_WORLD3D_RENDERER (self));
	g_return_if_fail (capture == NULL || LRG_IS_FRAME_CAPTURE (capture));

	if (self->capture == capture)
		return;
	if (self->capture != NULL)
		lrg_frame_capture_set_replay_func (self->capture, NULL, NULL);
	g_set_object (&self->capture, capture);
	if (self->capture != NULL)
		lrg_frame_capture_set_replay_func (self->capture, renderer_replay, self);
}

LrgFrameCapture *
lrg_world3d_renderer_get_frame_capture (LrgWorld3DRenderer *self)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), NULL);
	return self->capture;
}

/* item_texture() runs before the public getter is defined. */
static GrlTexture *
lrg_world3d_renderer_get_texture_internal (LrgWorld3DRenderer *self,
                                           const gchar        *asset)
{
	return lrg_world3d_renderer_get_texture (self, asset);
}
