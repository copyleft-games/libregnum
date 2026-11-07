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

/* Uniform locations of the post shader, in post_uniform_names order. */
enum
{
	POST_LOC_RESOLUTION,
	POST_LOC_FXAA_ON,
	POST_LOC_FXAA_EDGE,
	POST_LOC_FXAA_EDGE_MIN,
	POST_LOC_BLOOM_ON,
	POST_LOC_BLOOM_THRESHOLD,
	POST_LOC_BLOOM_KNEE,
	POST_LOC_BLOOM_INTENSITY,
	POST_LOC_BLOOM_SIZE,
	POST_LOC_BLOOM_TINT,
	POST_LOC_VIGNETTE_ON,
	POST_LOC_VIGNETTE_INTENSITY,
	POST_LOC_VIGNETTE_RADIUS,
	POST_LOC_VIGNETTE_SMOOTHNESS,
	POST_LOC_VIGNETTE_COLOR,
	N_POST_LOCS
};

static const gchar * const post_uniform_names[N_POST_LOCS] = {
	"resolution", "fxaaOn", "fxaaEdge", "fxaaEdgeMin", "bloomOn", "bloomThreshold", "bloomKnee",
	"bloomIntensity", "bloomSize", "bloomTint", "vignetteOn", "vignetteIntensity", "vignetteRadius",
	"vignetteSmoothness", "vignetteColor"
};

/*
 * The images of one post processor: the scene and the image after the
 * post pass, which LrgColorGrade then draws into the outer target.  Kept
 * as qdata on the processor so every viewport's processor has its own.
 */
typedef struct
{
	RenderTexture2D scene;
	RenderTexture2D out;
	GrlTexture     *out_texture;   /* non-owning wrapper of out.texture */
	gint            width;
	gint            height;
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
	Shader                   post_shader;
	gboolean                 has_post;
	gint                     post_locs[N_POST_LOCS];
	GSList                  *post_owners;   /* processors carrying PostTargets (weak) */

	/* What the current pass draws with */
	gboolean                 gpu;
	Shader                   cur_shader;
	gboolean                 white_albedo;
	gfloat                   cur_view[16];

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

	/* Statistics of the last render */
	guint                    drawn;
	guint                    pass_counts[LRG_WORLD3D_RENDER_PASS_COUNT];
	GPtrArray               *records[LRG_WORLD3D_RENDER_PASS_COUNT];
	guint                    shadow_passes;
	guint                    batched;
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

static const gchar shader_post_fs[] =
	"#version 330\n"
	"/* lrgeditor preview post-processing, one pass over the scene image:\n"
	" * FXAA, soft-knee bloom, a gentle highlight shoulder and a vignette.\n"
	" * The parameters come from the viewport's LrgFxaa, LrgBloom and\n"
	" * LrgVignette effects; colour grading runs after this pass through\n"
	" * LrgColorGrade. Uses raylib's default vertex shader. */\n"
	"in vec2 fragTexCoord;\n"
	"in vec4 fragColor;\n"
	"uniform sampler2D texture0;\n"
	"uniform vec2 resolution;\n"
	"uniform int fxaaOn;\n"
	"uniform float fxaaEdge;\n"
	"uniform float fxaaEdgeMin;\n"
	"uniform int bloomOn;\n"
	"uniform float bloomThreshold;\n"
	"uniform float bloomKnee;\n"
	"uniform float bloomIntensity;\n"
	"uniform float bloomSize;\n"
	"uniform vec3 bloomTint;\n"
	"uniform int vignetteOn;\n"
	"uniform float vignetteIntensity;\n"
	"uniform float vignetteRadius;\n"
	"uniform float vignetteSmoothness;\n"
	"uniform vec3 vignetteColor;\n"
	"out vec4 finalColor;\n"
	"\n"
	"float luma (vec3 c)\n"
	"{\n"
	"    return dot (c, vec3 (0.299, 0.587, 0.114));\n"
	"}\n"
	"\n"
	"/* FXAA: blend along the local edge direction where contrast is high. */\n"
	"vec3 fxaa (vec2 uv)\n"
	"{\n"
	"    vec2 px = 1.0 / resolution;\n"
	"    vec3 rgb_nw = texture (texture0, uv + vec2 (-1.0, -1.0) * px).rgb;\n"
	"    vec3 rgb_ne = texture (texture0, uv + vec2 (1.0, -1.0) * px).rgb;\n"
	"    vec3 rgb_sw = texture (texture0, uv + vec2 (-1.0, 1.0) * px).rgb;\n"
	"    vec3 rgb_se = texture (texture0, uv + vec2 (1.0, 1.0) * px).rgb;\n"
	"    vec3 rgb_m = texture (texture0, uv).rgb;\n"
	"    float l_nw = luma (rgb_nw);\n"
	"    float l_ne = luma (rgb_ne);\n"
	"    float l_sw = luma (rgb_sw);\n"
	"    float l_se = luma (rgb_se);\n"
	"    float l_m = luma (rgb_m);\n"
	"    float l_min = min (l_m, min (min (l_nw, l_ne), min (l_sw, l_se)));\n"
	"    float l_max = max (l_m, max (max (l_nw, l_ne), max (l_sw, l_se)));\n"
	"    vec2 dir;\n"
	"    float reduce;\n"
	"    float rcp;\n"
	"    vec3 a;\n"
	"    vec3 b;\n"
	"    float l_b;\n"
	"\n"
	"    if (l_max - l_min < max (fxaaEdgeMin, l_max * fxaaEdge))\n"
	"        return rgb_m;\n"
	"    dir.x = -((l_nw + l_ne) - (l_sw + l_se));\n"
	"    dir.y = ((l_nw + l_sw) - (l_ne + l_se));\n"
	"    reduce = max ((l_nw + l_ne + l_sw + l_se) * 0.03125, 1.0 / 128.0);\n"
	"    rcp = 1.0 / (min (abs (dir.x), abs (dir.y)) + reduce);\n"
	"    dir = clamp (dir * rcp, vec2 (-8.0), vec2 (8.0)) * px;\n"
	"    a = 0.5 * (texture (texture0, uv + dir * (1.0 / 3.0 - 0.5)).rgb +\n"
	"               texture (texture0, uv + dir * (2.0 / 3.0 - 0.5)).rgb);\n"
	"    b = a * 0.5 + 0.25 * (texture (texture0, uv - dir * 0.5).rgb +\n"
	"                          texture (texture0, uv + dir * 0.5).rgb);\n"
	"    l_b = luma (b);\n"
	"    return (l_b < l_min || l_b > l_max) ? a : b;\n"
	"}\n"
	"\n"
	"/* The part of a colour above the bloom threshold, with a soft knee. */\n"
	"vec3 bright (vec3 c)\n"
	"{\n"
	"    float l = max (c.r, max (c.g, c.b));\n"
	"    float knee = max (bloomThreshold * bloomKnee, 0.0001);\n"
	"    float soft = clamp (l - bloomThreshold + knee, 0.0, 2.0 * knee);\n"
	"    float w;\n"
	"\n"
	"    soft = soft * soft / (4.0 * knee);\n"
	"    w = max (soft, l - bloomThreshold) / max (l, 0.0001);\n"
	"    return c * w;\n"
	"}\n"
	"\n"
	"/* Three rings of eight taps, weighted towards the centre. */\n"
	"vec3 bloom (vec2 uv)\n"
	"{\n"
	"    vec2 px = bloomSize / resolution;\n"
	"    vec3 sum = vec3 (0.0);\n"
	"    float total = 0.0;\n"
	"    int ring;\n"
	"    int i;\n"
	"\n"
	"    for (ring = 1; ring <= 3; ring++)\n"
	"    {\n"
	"        for (i = 0; i < 8; i++)\n"
	"        {\n"
	"            float a = 6.2831853 * (float (i) + 0.5 * float (ring)) / 8.0;\n"
	"            float w = 1.0 / float (ring);\n"
	"\n"
	"            sum += bright (texture (texture0, uv + vec2 (cos (a), sin (a)) * px * float (ring)).rgb) * w;\n"
	"            total += w;\n"
	"        }\n"
	"    }\n"
	"    return sum / total;\n"
	"}\n"
	"\n"
	"void main ()\n"
	"{\n"
	"    vec2 uv = fragTexCoord;\n"
	"    vec3 color = (fxaaOn != 0) ? fxaa (uv) : texture (texture0, uv).rgb;\n"
	"\n"
	"    if (bloomOn != 0)\n"
	"    {\n"
	"        color += bloom (uv) * bloomIntensity * bloomTint;\n"
	"        /* Shoulder: values above 0.8 roll off instead of clipping */\n"
	"        color = mix (color, 0.8 + 0.2 * (1.0 - exp (-(color - 0.8) * 5.0)), step (0.8, color));\n"
	"    }\n"
	"    if (vignetteOn != 0)\n"
	"    {\n"
	"        vec2 d = uv - 0.5;\n"
	"        float dist;\n"
	"        float v;\n"
	"\n"
	"        d.x *= resolution.x / max (resolution.y, 1.0);\n"
	"        dist = length (d) * 1.41421;\n"
	"        v = smoothstep (vignetteRadius, vignetteRadius + max (vignetteSmoothness, 0.001), dist) * vignetteIntensity;\n"
	"        color = mix (color, vignetteColor, v);\n"
	"    }\n"
	"    finalColor = vec4 (color, 1.0);\n"
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
	{ "post.fs", shader_post_fs },
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
	self->has_post = load_shader (NULL, shader_post_fs, &self->post_shader);
	if (self->has_post)
	{
		gint i;

		for (i = 0; i < N_POST_LOCS; i++)
			self->post_locs[i] = GetShaderLocation (self->post_shader, post_uniform_names[i]);
	}

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
	g_clear_object (&t->out_texture);
	if (IsWindowReady ())
	{
		if (t->scene.id != 0)
			UnloadRenderTexture (t->scene);
		if (t->out.id != 0)
			UnloadRenderTexture (t->out);
	}
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
	clear_object_cache (self->models, gl);
	clear_object_cache (self->textures, gl);

	/* The static batch keeps CPU data; only its buffers go */
	if (self->batch != NULL && gl)
		lrg_static_batch_unload (self->batch);
	self->batch_signature = 0;
	self->batch_shader = NULL;

	if (gl)
		release_post_targets (self);

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
		if (self->has_post)
			UnloadShader (self->post_shader);
	}
	/* The default material's shader and texture belong to rlgl; only its
	 * map array is ours (plain CPU memory). */
	MemFree (self->material.maps);
	memset (&self->material, 0, sizeof self->material);
	self->has_lit = FALSE;
	self->has_unlit = FALSE;
	self->has_normals = FALSE;
	self->has_post = FALSE;
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
 * spots (previewed as points) become point lights. */
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
		/* Point lights, and spot lights (no 3D spots in the light set
		 * until 0.9 M2: previewed as points) */
		GrlColor c = light_color (light, 1.0f);
		guint    id;

		id = lrg_light_set_add_point_light (self->lights, light->position[0], light->position[1], light->position[2],
		                                    &c, light->range > 0.0f ? light->range : 10.0f);
		lrg_light_set_set_point_light_intensity (self->lights, id, MAX (light->intensity, 0.0f));
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

/* The item's colour, times its LrgMaterial3D's colour when it has one. */
static Color
item_color (const LrgRenderItem *item)
{
	gfloat rgba[4];
	Color  c;

	rgba[0] = item->color[0];
	rgba[1] = item->color[1];
	rgba[2] = item->color[2];
	rgba[3] = item->color[3];
	if (item->material != NULL && LRG_IS_MATERIAL3D (item->material))
	{
		gfloat m[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		lrg_material3d_get_color (LRG_MATERIAL3D (item->material), &m[0], &m[1], &m[2], &m[3]);
		rgba[0] *= m[0];
		rgba[1] *= m[1];
		rgba[2] *= m[2];
		rgba[3] *= m[3];
	}
	c.r = unit_to_byte (rgba[0]);
	c.g = unit_to_byte (rgba[1]);
	c.b = unit_to_byte (rgba[2]);
	c.a = unit_to_byte (rgba[3]);
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
	/* Lighting-only mode lights a white surface */
	if (self->white_albedo)
	{
		color = WHITE;
		texture = NULL;
	}
	self->material.shader = self->cur_shader;
	self->material.maps[MATERIAL_MAP_DIFFUSE].color = color;
	self->material.maps[MATERIAL_MAP_DIFFUSE].texture = (texture != NULL) ? *texture : self->white;
	DrawMesh (mesh, self->material, transform);
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

static void
draw_primitive (LrgWorld3DRenderer  *self,
                const LrgRenderItem *item,
                gboolean             casters_only)
{
	Matrix   m = primitive_matrix (item);
	Matrix   pre;
	Mesh     mesh;
	gboolean has_pre;

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

	m = mat_mul (model->transform, m);
	for (i = first; i < last; i++)
	{
		Material  mat = model->materials[model->meshMaterial[i]];
		Texture2D saved_tex = mat.maps[MATERIAL_MAP_DIFFUSE].texture;
		Color     saved_col = mat.maps[MATERIAL_MAP_DIFFUSE].color;
		Color     c = saved_col;

		/* The maps array is shared with the cached model: the overrides
		 * are undone right after the draw */
		c.r = (guchar) ((guint) c.r * tint.r / 255u);
		c.g = (guchar) ((guint) c.g * tint.g / 255u);
		c.b = (guchar) ((guint) c.b * tint.b / 255u);
		c.a = (guchar) ((guint) c.a * tint.a / 255u);
		mat.shader = self->cur_shader;
		mat.maps[MATERIAL_MAP_DIFFUSE].color = self->white_albedo ? WHITE : c;
		if (self->white_albedo)
			mat.maps[MATERIAL_MAP_DIFFUSE].texture = self->white;
		else if (override != NULL)
			mat.maps[MATERIAL_MAP_DIFFUSE].texture = *override;
		DrawMesh (model->meshes[i], mat, m);
		mat.maps[MATERIAL_MAP_DIFFUSE].texture = saved_tex;
		mat.maps[MATERIAL_MAP_DIFFUSE].color = saved_col;
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
	gboolean              casters_only;  /* shadow pass */
	gboolean              skip_batched;  /* static items went into the batch */
	Shader                lit_shader;    /* what non-UNLIT items use */
	Shader                unlit_shader;  /* what UNLIT items use in lit modes */
} Replay;

/* Whether the static batch takes @item instead of the opaque pass. */
static gboolean
item_is_batchable (const LrgRenderItem *item)
{
	if (item->entity == NULL || !lrg_entity3d_get_static (item->entity))
		return FALSE;
	if (lrg_render_item_get_category (item) != LRG_RENDER_CATEGORY_OPAQUE)
		return FALSE;
	if ((item->flags & (LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED | LRG_RENDER_ITEM_FLAG_UNLIT)) != 0 || item->texture != NULL)
		return FALSE;
	if (item->kind == LRG_RENDER_ITEM_PRIMITIVE)
		return primitive_is_solid (item->primitive);
	return item->kind == LRG_RENDER_ITEM_MESH && item->mesh != NULL && GRL_IS_MESH (item->mesh);
}

static void
replay_item (const LrgRenderItem *item,
             LrgRenderCategory    category,
             gpointer             user_data)
{
	Replay             *r = (Replay *) user_data;
	LrgWorld3DRenderer *self = r->self;
	gboolean            two_sided;

	if ((r->mask & LRG_RENDER_LAYERS_BIT (item->layer)) == 0)
		return;
	if (r->skip_batched && item_is_batchable (item))
		return;
	if (r->casters_only)
	{
		/* Shadow passes draw solid geometry only */
		if (item->kind == LRG_RENDER_ITEM_BILLBOARD)
			return;
		if (item->kind == LRG_RENDER_ITEM_PRIMITIVE && !primitive_is_solid (item->primitive))
			return;
	}

	record_item (self, r->pass, item);
	if (!self->gpu)
		return;

	/* UNLIT items keep their albedo in the lit modes */
	self->cur_shader = (!r->casters_only && (item->flags & LRG_RENDER_ITEM_FLAG_UNLIT) != 0) ? r->unlit_shader
	                                                                                        : r->lit_shader;
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
                   GrlShader          *light_shader)
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
	if (!self->gpu)
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
 * Shadows, sky, post
 * ========================================================================== */

/*
 * Renders this frame's shadow passes for @v: the cascades of the
 * directional light (and cube faces of shadowed point lights) around the
 * view, each replaying the shadow-caster list.  The passes need the
 * screen bound, so the caller's target is left and re-entered afterwards.
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
	enter_target (outer, w, h);
	return lrg_shadow_renderer_is_ready (self->shadows);
}

/* (Re)creates a processor's post images at @w x @h. */
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
	g_clear_object (&t->out_texture);
	if (t->scene.id != 0)
		UnloadRenderTexture (t->scene);
	if (t->out.id != 0)
		UnloadRenderTexture (t->out);
	t->scene = LoadRenderTexture (w, h);
	t->out = LoadRenderTexture (w, h);
	t->width = w;
	t->height = h;
	if (t->scene.id == 0 || t->out.id == 0)
		return NULL;
	SetTextureFilter (t->scene.texture, TEXTURE_FILTER_BILINEAR);
	SetTextureWrap (t->scene.texture, TEXTURE_WRAP_CLAMP);
	t->out_texture = grl_texture_new_from_handle (&t->out.texture);
	return t;
}

static void
post_uniform_float (LrgWorld3DRenderer *self,
                    gint                loc,
                    gfloat              value)
{
	SetShaderValue (self->post_shader, self->post_locs[loc], &value, SHADER_UNIFORM_FLOAT);
}

static void
post_uniform_int (LrgWorld3DRenderer *self,
                  gint                loc,
                  gint                value)
{
	SetShaderValue (self->post_shader, self->post_locs[loc], &value, SHADER_UNIFORM_INT);
}

static gboolean
effect_on (LrgPostEffect *effect)
{
	return effect != NULL && lrg_post_effect_is_enabled (effect);
}

/*
 * The post chain: scene image -> post shader (FXAA, bloom, vignette, with
 * the processor's effect parameters) -> LrgColorGrade into the outer
 * target.
 */
static void
apply_post (LrgWorld3DRenderer *self,
            PostTargets        *t,
            guint               outer,
            gint                w,
            gint                h)
{
	LrgPostEffect *fxaa = lrg_post_processor_get_effect (self->post, "fxaa");
	LrgPostEffect *bloom = lrg_post_processor_get_effect (self->post, "bloom");
	LrgPostEffect *vignette = lrg_post_processor_get_effect (self->post, "vignette");
	LrgPostEffect *grade = lrg_post_processor_get_effect (self->post, "color-grade");
	Rectangle      src = { 0.0f, 0.0f, (gfloat) w, -(gfloat) h };
	gfloat         res[2] = { (gfloat) w, (gfloat) h };
	gfloat         rgb[3] = { 1.0f, 1.0f, 1.0f };

	/* Pass 1: the post shader, scene -> out */
	enter_target (t->out.id, w, h);
	ClearBackground (BLACK);
	BeginShaderMode (self->post_shader);
	SetShaderValue (self->post_shader, self->post_locs[POST_LOC_RESOLUTION], res, SHADER_UNIFORM_VEC2);
	post_uniform_int (self, POST_LOC_FXAA_ON, effect_on (fxaa));
	if (fxaa != NULL && LRG_IS_FXAA (fxaa))
	{
		post_uniform_float (self, POST_LOC_FXAA_EDGE, lrg_fxaa_get_edge_threshold (LRG_FXAA (fxaa)));
		post_uniform_float (self, POST_LOC_FXAA_EDGE_MIN, lrg_fxaa_get_edge_threshold_min (LRG_FXAA (fxaa)));
	}
	post_uniform_int (self, POST_LOC_BLOOM_ON, effect_on (bloom));
	if (bloom != NULL && LRG_IS_BLOOM (bloom))
	{
		post_uniform_float (self, POST_LOC_BLOOM_THRESHOLD, lrg_bloom_get_threshold (LRG_BLOOM (bloom)));
		post_uniform_float (self, POST_LOC_BLOOM_KNEE, lrg_bloom_get_soft_knee (LRG_BLOOM (bloom)));
		post_uniform_float (self, POST_LOC_BLOOM_INTENSITY, lrg_bloom_get_intensity (LRG_BLOOM (bloom)));
		post_uniform_float (self, POST_LOC_BLOOM_SIZE, lrg_bloom_get_blur_size (LRG_BLOOM (bloom)));
		lrg_bloom_get_tint (LRG_BLOOM (bloom), &rgb[0], &rgb[1], &rgb[2]);
		SetShaderValue (self->post_shader, self->post_locs[POST_LOC_BLOOM_TINT], rgb, SHADER_UNIFORM_VEC3);
	}
	post_uniform_int (self, POST_LOC_VIGNETTE_ON, effect_on (vignette));
	if (vignette != NULL && LRG_IS_VIGNETTE (vignette))
	{
		post_uniform_float (self, POST_LOC_VIGNETTE_INTENSITY, lrg_vignette_get_intensity (LRG_VIGNETTE (vignette)));
		post_uniform_float (self, POST_LOC_VIGNETTE_RADIUS, lrg_vignette_get_radius (LRG_VIGNETTE (vignette)));
		post_uniform_float (self, POST_LOC_VIGNETTE_SMOOTHNESS,
		                    lrg_vignette_get_smoothness (LRG_VIGNETTE (vignette)));
		lrg_vignette_get_color (LRG_VIGNETTE (vignette), &rgb[0], &rgb[1], &rgb[2]);
		SetShaderValue (self->post_shader, self->post_locs[POST_LOC_VIGNETTE_COLOR], rgb, SHADER_UNIFORM_VEC3);
	}
	DrawTextureRec (t->scene.texture, src, (Vector2) { 0.0f, 0.0f }, WHITE);
	EndShaderMode ();

	/* Pass 2: colour grade, out -> the outer target */
	enter_target (outer, w, h);
	if (effect_on (grade) && LRG_IS_COLOR_GRADE (grade) && t->out_texture != NULL)
	{
		GrlRectangle gsrc = { 0.0f, 0.0f, (gfloat) w, -(gfloat) h };
		GrlRectangle gdst = { 0.0f, 0.0f, (gfloat) w, (gfloat) h };

		lrg_color_grade_draw_texture (LRG_COLOR_GRADE (grade), t->out_texture, &gsrc, &gdst);
	}
	else
		DrawTextureRec (t->out.texture, src, (Vector2) { 0.0f, 0.0f }, WHITE);
	rlDrawRenderBatchActive ();
	self->pass_counts[LRG_WORLD3D_RENDER_PASS_POST] = 1;
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
	r->pass = pass;
	lrg_render_list_foreach (list, category, replay_item, r);
	if (self->gpu)
		rlDrawRenderBatchActive ();
}

static gboolean
render_core (LrgWorld3DRenderer   *self,
             LrgRenderList        *list,
             const LrgWorld3DView *in_view)
{
	LrgWorld3DView  v;
	GrlShader      *light_shader = NULL;
	PostTargets    *post = NULL;
	Replay          r;
	gfloat          proj_m[16];
	gboolean        wire, lit_mode, perspective, shadows_ok = FALSE, batched = FALSE;
	guint           outer = 0;
	gint            iw, ih;

	stats_reset (self);
	self->gpu = !self->headless && ensure_gl (self);

	v = *in_view;
	v.width = MAX (v.width, 1.0f);
	v.height = MAX (v.height, 1.0f);
	iw = (gint) v.width;
	ih = (gint) v.height;
	wire = (self->mode == LRG_WORLD3D_RENDER_MODE_WIREFRAME);
	lit_mode = (self->mode == LRG_WORLD3D_RENDER_MODE_LIT || self->mode == LRG_WORLD3D_RENDER_MODE_LIGHTING_ONLY);
	perspective = !v.orthographic;
	if (self->gpu)
		outer = rlGetActiveFramebuffer ();

	/* --- Lights and shadows ---------------------------------------------- */
	sync_from_list (self, list);
	if (lit_mode && self->gpu)
		light_shader = lrg_light_set_get_shader (self->lights);
	if ((self->flags & LRG_WORLD3D_RENDER_FLAG_SHADOWS) && lit_mode && perspective &&
	    (!self->gpu || light_shader != NULL))
		shadows_ok = render_shadows (self, list, &v, outer, iw, ih);
	if (self->gpu)
		lrg_light_set_set_shadow_renderer (self->lights, shadows_ok ? self->shadows : NULL);

	/* --- Where the scene goes: the post image or the target itself ----- */
	if (self->gpu && (self->flags & LRG_WORLD3D_RENDER_FLAG_POST) && self->post != NULL && self->has_post)
	{
		post = ensure_post_targets (self, self->post, iw, ih);
		if (post != NULL)
			enter_target (post->scene.id, iw, ih);
	}

	if (self->gpu && (self->flags & LRG_WORLD3D_RENDER_FLAG_CLEAR))
	{
		Color bg = { self->background.r, self->background.g, self->background.b, self->background.a };

		ClearBackground (bg);
	}

	/* --- 3D pass ----------------------------------------------------------
	 * BeginMode3D() with our own matrices (the editor's picking uses the
	 * same ones). */
	lrg_world3d_view_get_view_matrix (&v, self->cur_view);
	lrg_world3d_view_get_projection_matrix (&v, proj_m);
	if (self->gpu)
	{
		rlDrawRenderBatchActive ();
		rlMatrixMode (RL_PROJECTION);
		rlPushMatrix ();
		rlLoadIdentity ();
		rlMultMatrixf (proj_m);
		rlMatrixMode (RL_MODELVIEW);
		rlLoadIdentity ();
		rlMultMatrixf (self->cur_view);
		rlEnableDepthTest ();
	}

	/* The sky first, behind everything (perspective views only) */
	if ((self->flags & LRG_WORLD3D_RENDER_FLAG_SKY) && perspective)
	{
		self->pass_counts[LRG_WORLD3D_RENDER_PASS_SKY] = 1;
		if (self->gpu)
		{
			GrlVector3 eye;
			GrlVector3 toward_sun;

			eye.x = v.position[0];
			eye.y = v.position[1];
			eye.z = v.position[2];
			toward_sun.x = -self->sun_dir[0];
			toward_sun.y = -self->sun_dir[1];
			toward_sun.z = -self->sun_dir[2];
			lrg_sky_dome_set_sun_direction (self->sky, &toward_sun);
			lrg_sky_dome_set_radius (self->sky, CLAMP (v.far_plane * 0.5f, 1.0f, 500.0f));
			lrg_sky_dome_draw (self->sky, &eye);
			rlDrawRenderBatchActive ();
		}
	}

	/* What the items draw with */
	memset (&r, 0, sizeof r);
	r.self = self;
	r.mask = v.cull_mask;
	if (self->gpu)
	{
		r.lit_shader = mode_shader (self, self->mode, light_shader);
		r.unlit_shader = lit_mode ? (self->has_unlit ? self->unlit : self->flat) : r.lit_shader;
		self->cur_shader = r.lit_shader;
		self->white_albedo = (self->mode == LRG_WORLD3D_RENDER_MODE_LIGHTING_ONLY);
		if (light_shader != NULL)
			lrg_light_set_apply (self->lights, v.position[0], v.position[1], v.position[2]);
		else if (lit_mode && self->has_lit)
		{
			gfloat ambient = 0.35f;

			SetShaderValue (self->lit, self->loc_light, self->sun_dir, SHADER_UNIFORM_VEC3);
			SetShaderValue (self->lit, self->loc_ambient, &ambient, SHADER_UNIFORM_FLOAT);
		}
		g_signal_emit (self, signals[SIGNAL_DRAW_BACKGROUND], 0, &v);
		if (wire)
			rlEnableWireMode ();
	}

	/* Opaque: the static batch first, then the rest front to back */
	if ((self->flags & LRG_WORLD3D_RENDER_FLAG_BATCH_STATIC) && self->mode == LRG_WORLD3D_RENDER_MODE_LIT &&
	    (!self->gpu || light_shader != NULL))
		batched = draw_static_batch (self, list, v.cull_mask, light_shader);
	r.skip_batched = batched;
	replay_category (self, list, LRG_RENDER_CATEGORY_OPAQUE, LRG_WORLD3D_RENDER_PASS_OPAQUE, &r);
	r.skip_batched = FALSE;

	/* Cutout: alpha tested at 0.5 by the light-set shader */
	if (light_shader != NULL && lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_CUTOUT) > 0)
	{
		lrg_light_set_set_alpha_cutoff (self->lights, CUTOUT_ALPHA);
		lrg_light_set_apply (self->lights, v.position[0], v.position[1], v.position[2]);
		replay_category (self, list, LRG_RENDER_CATEGORY_CUTOUT, LRG_WORLD3D_RENDER_PASS_CUTOUT, &r);
		lrg_light_set_set_alpha_cutoff (self->lights, 0.0f);
		lrg_light_set_apply (self->lights, v.position[0], v.position[1], v.position[2]);
	}
	else
		replay_category (self, list, LRG_RENDER_CATEGORY_CUTOUT, LRG_WORLD3D_RENDER_PASS_CUTOUT, &r);

	/* Transparent: back to front, blended, without depth writes */
	if (self->gpu)
		rlDisableDepthMask ();
	replay_category (self, list, LRG_RENDER_CATEGORY_TRANSPARENT, LRG_WORLD3D_RENDER_PASS_TRANSPARENT, &r);
	if (self->gpu)
		rlEnableDepthMask ();

	if (self->gpu)
	{
		/* Overlays still in the 3D mode (and in wire mode, as before) */
		g_signal_emit (self, signals[SIGNAL_DRAW_OVERLAY], 0, &v);
		rlDrawRenderBatchActive ();
		if (wire)
			rlDisableWireMode ();

		if ((self->flags & LRG_WORLD3D_RENDER_FLAG_DEBUG_DRAW) && self->debug_draw != NULL)
			lrg_debug_draw_draw_3d (self->debug_draw);

		self->white_albedo = FALSE;
		self->cur_shader = self->has_lit ? self->lit : self->flat;

		/* Back to the target's 2D projection (EndMode3D pops ours) */
		EndMode3D ();

		if ((self->flags & LRG_WORLD3D_RENDER_FLAG_DEBUG_DRAW) && self->debug_draw != NULL)
			lrg_debug_draw_draw_labels (self->debug_draw);

		if (post != NULL)
			apply_post (self, post, outer, iw, ih);
		rlDrawRenderBatchActive ();
	}
	return self->gpu;
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
	release_post_targets (self);
	g_clear_object (&self->post);
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
	self->sun_dir[0] = DEFAULT_SUN_X;
	self->sun_dir[1] = DEFAULT_SUN_Y;
	self->sun_dir[2] = DEFAULT_SUN_Z;
	normalize3 (self->sun_dir);
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

	if (target != NULL && !self->headless && IsWindowReady ())
	{
		grl_render_texture_begin (target);
		drawn = render_core (self, self->list, &view);
		grl_render_texture_end (target);
	}
	else
		drawn = render_core (self, self->list, &view);
	return drawn;
}

gboolean
lrg_world3d_renderer_render_view (LrgWorld3DRenderer   *self,
                                  LrgWorld3D           *world,
                                  const LrgWorld3DView *view)
{
	g_return_val_if_fail (LRG_IS_WORLD3D_RENDERER (self), FALSE);
	g_return_val_if_fail (LRG_IS_WORLD3D (world), FALSE);
	g_return_val_if_fail (view != NULL, FALSE);

	/* Everything visible, sorted for this eye; the cull mask applies at
	 * draw time */
	lrg_world3d_collect_render (world, NULL, self->list);
	lrg_render_list_set_view (self->list, view->position, view->cull_mask, NULL);
	lrg_render_list_sort (self->list);
	return render_core (self, self->list, view);
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
	g_return_if_fail ((guint) mode <= LRG_WORLD3D_RENDER_MODE_LIGHTING_ONLY);

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
	g_autoptr(LrgFxaa)       fxaa = lrg_fxaa_new ();
	g_autoptr(LrgBloom)      bloom = lrg_bloom_new ();
	g_autoptr(LrgVignette)   vignette = lrg_vignette_new ();
	g_autoptr(LrgColorGrade) grade = lrg_color_grade_new ();

	/* Preview defaults: a subtle look that reads in any scene */
	lrg_bloom_set_threshold (bloom, 0.75f);
	lrg_bloom_set_intensity (bloom, 0.6f);
	lrg_vignette_set_intensity (vignette, 0.35f);
	lrg_vignette_set_radius (vignette, 0.55f);
	lrg_vignette_set_smoothness (vignette, 0.45f);
	lrg_color_grade_set_contrast (grade, 1.05f);
	lrg_color_grade_set_saturation (grade, 1.08f);
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (fxaa));
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (bloom));
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (vignette));
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
	if (!g_hash_table_contains (self->models, abs_path) && !g_hash_table_contains (self->textures, abs_path))
		return FALSE;

	/* The by-asset maps borrow from the path maps, so they are cleared
	 * wholesale (they refill on the next lookup).  The static batch may
	 * have merged the old mesh: rebuild it. */
	forget_lookups (self);
	if (IsWindowReady ())
	{
		g_hash_table_remove (self->models, abs_path);
		g_hash_table_remove (self->textures, abs_path);
	}
	else
	{
		g_hash_table_steal (self->models, abs_path);
		g_hash_table_steal (self->textures, abs_path);
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

/* item_texture() runs before the public getter is defined. */
static GrlTexture *
lrg_world3d_renderer_get_texture_internal (LrgWorld3DRenderer *self,
                                           const gchar        *asset)
{
	return lrg_world3d_renderer_get_texture (self, asset);
}
