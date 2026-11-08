/* lrg-ssao.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSsao: screen-space ambient occlusion in the classic hemisphere
 * form.  For every pixel of the (optionally half-resolution) target the
 * occlusion pass rebuilds the view-space position from the prepass
 * depth and the inverse projection, takes the normal from the prepass
 * normal texture (or rebuilds it from neighbouring depths), rotates a
 * fixed hemisphere kernel by a tiled 4 x 4 noise vector, and counts the
 * kernel points that lie behind the stored surface (with a range check
 * so distant geometry does not darken edges).  A depth-aware 4 x 4 blur
 * then removes the noise pattern.  The result is a texture of ambient
 * visibility that the lit shader reads through lrg_ssao().
 *
 * Kernel and noise come from GRand (Mersenne Twister) seeded with the
 * seed property, so they are identical on every machine.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-ssao.h"
#include "lrg-ssao-glsl-private.h"
#include "../core/lrg-math3d.h"
#include "lrg-light-set-private.h"

/* shared fragment-shader prelude: raylib's default vertex shader feeds
 * these; the passes work from gl_FragCoord instead */
#define PASS_PRELUDE \
	"#version 330\n" \
	"in vec2 fragTexCoord;\n" \
	"in vec4 fragColor;\n" \
	"out vec4 finalColor;\n" \
	"uniform sampler2D ssaoDepth;\n" \
	"uniform mat4 ssaoInvProj;\n" \
	"uniform vec4 ssaoParams;\n" \
	"uniform vec4 ssaoSize;\n" \
	"vec3 view_pos(vec2 uv) {\n" \
	"    float d = texture(ssaoDepth, uv).r;\n" \
	"    vec4 v = ssaoInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);\n" \
	"    return v.xyz / v.w;\n" \
	"}\n"

/*
 * Occlusion pass.  ssaoParams = (radius, bias, intensity, power);
 * ssaoSize = (output width, output height, depth width, depth height).
 */
static const gchar ao_fs[] =
	PASS_PRELUDE
	"uniform sampler2D ssaoNormals;\n"
	"uniform sampler2D ssaoNoise;\n"
	"uniform int ssaoUseNormals;\n"
	"uniform mat4 ssaoProj;\n"
	"uniform vec3 ssaoKernel[64];\n"
	"uniform int ssaoKernelSize;\n"
	"vec3 depth_normal(vec2 uv, vec3 p) {\n"
	"    vec2 px = 1.0 / ssaoSize.zw;\n"
	"    vec3 r = view_pos(uv + vec2(px.x, 0.0)) - p;\n"
	"    vec3 l = p - view_pos(uv - vec2(px.x, 0.0));\n"
	"    vec3 u = view_pos(uv + vec2(0.0, px.y)) - p;\n"
	"    vec3 d = p - view_pos(uv - vec2(0.0, px.y));\n"
	"    vec3 dx = abs(r.z) < abs(l.z) ? r : l;\n"
	"    vec3 dy = abs(u.z) < abs(d.z) ? u : d;\n"
	"    vec3 n = cross(dx, dy);\n"
	"    float len = length(n);\n"
	"    return len > 0.0 ? n / len : vec3(0.0, 0.0, 1.0);\n"
	"}\n"
	"void main() {\n"
	"    vec2 uv = gl_FragCoord.xy / ssaoSize.xy;\n"
	"    if (texture(ssaoDepth, uv).r >= 1.0) { finalColor = vec4(1.0); return; }\n"
	"    vec3 p = view_pos(uv);\n"
	"    vec3 n = ssaoUseNormals == 1 ? normalize(texture(ssaoNormals, uv).xyz * 2.0 - 1.0)\n"
	"                                 : depth_normal(uv, p);\n"
	"    vec3 rv = vec3(texture(ssaoNoise, gl_FragCoord.xy / 4.0).xy, 0.0);\n"
	"    vec3 t = rv - n * dot(rv, n);\n"
	"    if (dot(t, t) < 1e-6) t = abs(n.x) < 0.9 ? cross(n, vec3(1.0, 0.0, 0.0)) : cross(n, vec3(0.0, 1.0, 0.0));\n"
	"    t = normalize(t);\n"
	"    mat3 tbn = mat3(t, cross(n, t), n);\n"
	"    float occ = 0.0;\n"
	"    for (int i = 0; i < 64; i++) {\n"
	"        if (i >= ssaoKernelSize) break;\n"
	"        vec3 s = p + tbn * ssaoKernel[i] * ssaoParams.x;\n"
	"        vec4 o = ssaoProj * vec4(s, 1.0);\n"
	"        if (o.w <= 0.0) continue;\n"
	"        vec2 suv = o.xy / o.w * 0.5 + 0.5;\n"
	"        if (suv.x < 0.0 || suv.y < 0.0 || suv.x > 1.0 || suv.y > 1.0) continue;\n"
	"        float sz = view_pos(suv).z;\n"
	"        float range = smoothstep(0.0, 1.0, ssaoParams.x / max(abs(p.z - sz), 1e-4));\n"
	"        occ += (sz >= s.z + ssaoParams.y ? 1.0 : 0.0) * range;\n"
	"    }\n"
	"    occ /= float(max(ssaoKernelSize, 1));\n"
	"    float ao = pow(clamp(1.0 - ssaoParams.z * occ, 0.0, 1.0), ssaoParams.w);\n"
	"    finalColor = vec4(ao, ao, ao, 1.0);\n"
	"}\n";

/*
 * Blur pass: the 4 x 4 block of texels around the pixel (one noise
 * tile), each weighted down as its view depth departs from the centre's
 * by up to the radius (so the blur does not bleed across silhouettes).
 */
static const gchar blur_fs[] =
	PASS_PRELUDE
	"uniform sampler2D ssaoInput;\n"
	"void main() {\n"
	"    vec2 texel = 1.0 / ssaoSize.xy;\n"
	"    vec2 base = floor(gl_FragCoord.xy);\n"
	"    float zc = view_pos(gl_FragCoord.xy * texel).z;\n"
	"    float sum = 0.0;\n"
	"    float wsum = 0.0;\n"
	"    for (int y = -2; y < 2; y++) {\n"
	"        for (int x = -2; x < 2; x++) {\n"
	"            vec2 uv = (base + vec2(float(x), float(y)) + 0.5) * texel;\n"
	"            float w = max(1.0 - abs(view_pos(uv).z - zc) / max(ssaoParams.x, 1e-4), 0.0);\n"
	"            if (x == 0 && y == 0) w = 1.0;\n"
	"            sum += texture(ssaoInput, uv).r * w;\n"
	"            wsum += w;\n"
	"        }\n"
	"    }\n"
	"    float ao = sum / max(wsum, 1e-4);\n"
	"    finalColor = vec4(ao, ao, ao, 1.0);\n"
	"}\n";

/* uniform locations of one pass shader */
typedef struct
{
	gint depth;
	gint normals;
	gint noise;
	gint input;
	gint use_normals;
	gint proj;
	gint inv_proj;
	gint kernel;
	gint kernel_size;
	gint params;
	gint size;
} PassLocations;

/* one render target (framebuffer + RGBA8 colour texture) */
typedef struct
{
	guint fbo;
	guint tex;
	gint  width;
	gint  height;
} Target;

struct _LrgSsao
{
	GObject        parent_instance;

	/* parameters */
	gfloat         radius;
	gfloat         bias;
	gfloat         intensity;
	gfloat         power;
	guint          kernel_size;
	guint32        seed;
	gboolean       half;
	gboolean       blur;

	/* kernel (tangent space) and noise, from the seed */
	gfloat         kernel[LRG_SSAO_MAX_KERNEL * 3];
	gfloat         noise[LRG_SSAO_NOISE_SIZE * LRG_SSAO_NOISE_SIZE * 3];
	gboolean       noise_dirty;

	/* GPU objects */
	gboolean       shaders_tried;
	Shader         ao_shader;
	Shader         blur_shader;
	PassLocations  ao_locs;
	PassLocations  blur_locs;
	guint          noise_tex;
	Target         ao;
	Target         blurred;
	gboolean       has_result;
	gboolean       result_blurred;

	/* where the lit pass draws (width 0: the prepass size) */
	gint           viewport[4];
	gint           auto_viewport[4];
};

G_DEFINE_FINAL_TYPE (LrgSsao, lrg_ssao, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_RADIUS,
	PROP_BIAS,
	PROP_INTENSITY,
	PROP_POWER,
	PROP_KERNEL_SIZE,
	PROP_SEED,
	PROP_HALF_RESOLUTION,
	PROP_BLUR,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Kernel and noise
 * ========================================================================== */

/* generate_kernel:
 * Random directions in the +Z hemisphere (rejecting grazing ones with
 * z < 0.15, which only sample the surface itself) with lengths growing
 * quadratically from 0.1 to 1 over the index, so most samples sit near
 * the point where small creases are. */
static void
generate_kernel (LrgSsao *self)
{
	g_autoptr(GRand) rand = g_rand_new_with_seed (self->seed);
	guint i;

	for (i = 0; i < self->kernel_size; i++)
	{
		gdouble x, y, z, len, t, scale;

		do
		{
			x = g_rand_double_range (rand, -1.0, 1.0);
			y = g_rand_double_range (rand, -1.0, 1.0);
			z = g_rand_double (rand);
			len = sqrt (x * x + y * y + z * z);
		}
		while (len < 1e-3 || len > 1.0 || z / len < 0.15);

		t = (gdouble) (i + 1) / (gdouble) self->kernel_size;
		scale = 0.1 + 0.9 * t * t;
		self->kernel[i * 3 + 0] = (gfloat) (x / len * scale);
		self->kernel[i * 3 + 1] = (gfloat) (y / len * scale);
		self->kernel[i * 3 + 2] = (gfloat) (z / len * scale);
	}
}

/* generate_noise:
 * Unit rotation vectors in the tangent plane, one per texel of the 4 x 4
 * tile, from a stream separate from the kernel's so the kernel size
 * does not change the noise. */
static void
generate_noise (LrgSsao *self)
{
	g_autoptr(GRand) rand = g_rand_new_with_seed (self->seed ^ 0x9e3779b9u);
	guint i;

	for (i = 0; i < LRG_SSAO_NOISE_SIZE * LRG_SSAO_NOISE_SIZE; i++)
	{
		gdouble a = g_rand_double_range (rand, 0.0, 2.0 * G_PI);

		self->noise[i * 3 + 0] = (gfloat) cos (a);
		self->noise[i * 3 + 1] = (gfloat) sin (a);
		self->noise[i * 3 + 2] = 0.0f;
	}
	self->noise_dirty = TRUE;
}

/* ==========================================================================
 * GPU objects
 * ========================================================================== */

static void
target_free (Target *t)
{
	if (t->fbo != 0 && IsWindowReady ())
	{
		rlUnloadFramebuffer (t->fbo);
		rlUnloadTexture (t->tex);
	}
	memset (t, 0, sizeof *t);
}

/* target_ensure:
 * An RGBA8 colour-only framebuffer of @width x @height, linearly
 * filtered (the lit shader upsamples it) and clamped. */
static gboolean
target_ensure (Target *t,
               gint    width,
               gint    height)
{
	if (t->fbo != 0 && t->width == width && t->height == height)
		return TRUE;
	target_free (t);
	t->fbo = rlLoadFramebuffer ();
	if (t->fbo == 0)
		return FALSE;
	t->tex = rlLoadTexture (NULL, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
	if (t->tex == 0)
	{
		target_free (t);
		return FALSE;
	}
	rlTextureParameters (t->tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
	rlTextureParameters (t->tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
	rlTextureParameters (t->tex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
	rlTextureParameters (t->tex, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
	rlFramebufferAttach (t->fbo, t->tex, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
	if (!rlFramebufferComplete (t->fbo))
	{
		rlDisableFramebuffer ();
		target_free (t);
		return FALSE;
	}
	rlDisableFramebuffer ();
	t->width = width;
	t->height = height;
	return TRUE;
}

static RenderTexture2D
target_render_texture (const Target *t)
{
	RenderTexture2D rt;

	memset (&rt, 0, sizeof rt);
	rt.id = t->fbo;
	rt.texture.id = t->tex;
	rt.texture.width = t->width;
	rt.texture.height = t->height;
	rt.texture.mipmaps = 1;
	rt.texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	return rt;
}

static void
pass_locations (Shader         shader,
                PassLocations *locs)
{
	locs->depth = GetShaderLocation (shader, "ssaoDepth");
	locs->normals = GetShaderLocation (shader, "ssaoNormals");
	locs->noise = GetShaderLocation (shader, "ssaoNoise");
	locs->input = GetShaderLocation (shader, "ssaoInput");
	locs->use_normals = GetShaderLocation (shader, "ssaoUseNormals");
	locs->proj = GetShaderLocation (shader, "ssaoProj");
	locs->inv_proj = GetShaderLocation (shader, "ssaoInvProj");
	locs->kernel = GetShaderLocation (shader, "ssaoKernel");
	locs->kernel_size = GetShaderLocation (shader, "ssaoKernelSize");
	locs->params = GetShaderLocation (shader, "ssaoParams");
	locs->size = GetShaderLocation (shader, "ssaoSize");
}

/* load_pass_shader:
 * A fragment shader over raylib's default vertex shader; FALSE (and a
 * zeroed @out) when it does not compile (raylib then returns its default
 * shader, which must not be unloaded). */
static gboolean
load_pass_shader (const gchar *fs,
                  Shader      *out)
{
	*out = LoadShaderFromMemory (NULL, fs);
	if (out->id == 0 || out->id == rlGetShaderIdDefault ())
	{
		memset (out, 0, sizeof *out);
		return FALSE;
	}
	return TRUE;
}

/* ensure_gpu:
 * The pass shaders (tried once) and the noise texture. */
static gboolean
ensure_gpu (LrgSsao *self)
{
	if (!self->shaders_tried)
	{
		self->shaders_tried = TRUE;
		if (!load_pass_shader (ao_fs, &self->ao_shader))
			g_warning ("LrgSsao: the occlusion shader does not compile");
		else
			pass_locations (self->ao_shader, &self->ao_locs);
		if (!load_pass_shader (blur_fs, &self->blur_shader))
			g_warning ("LrgSsao: the blur shader does not compile");
		else
			pass_locations (self->blur_shader, &self->blur_locs);
	}
	if (self->ao_shader.id == 0)
		return FALSE;

	/* the 4 x 4 noise tile, repeated over the screen */
	if (self->noise_tex == 0 || self->noise_dirty)
	{
		rlActiveTextureSlot (0);
		if (self->noise_tex != 0)
			rlUnloadTexture (self->noise_tex);
		self->noise_tex = rlLoadTexture (self->noise, LRG_SSAO_NOISE_SIZE, LRG_SSAO_NOISE_SIZE,
		                                 RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32, 1);
		if (self->noise_tex == 0)
			return FALSE;
		rlTextureParameters (self->noise_tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
		rlTextureParameters (self->noise_tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
		rlTextureParameters (self->noise_tex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_REPEAT);
		rlTextureParameters (self->noise_tex, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_REPEAT);
		self->noise_dirty = FALSE;
	}
	return TRUE;
}

static void
free_gpu (LrgSsao *self)
{
	if (IsWindowReady ())
	{
		if (self->ao_shader.id != 0)
			UnloadShader (self->ao_shader);
		if (self->blur_shader.id != 0)
			UnloadShader (self->blur_shader);
		if (self->noise_tex != 0)
			rlUnloadTexture (self->noise_tex);
	}
	memset (&self->ao_shader, 0, sizeof self->ao_shader);
	memset (&self->blur_shader, 0, sizeof self->blur_shader);
	self->noise_tex = 0;
	self->shaders_tried = FALSE;
	target_free (&self->ao);
	target_free (&self->blurred);
	self->has_result = FALSE;
}

/* floats_to_matrix: column-major floats to raylib's Matrix by field
 * name (never memcpy, see AGENTS.md). */
static Matrix
floats_to_matrix (const gfloat *in)
{
	Matrix m;

	m.m0 = in[0];   m.m1 = in[1];   m.m2 = in[2];   m.m3 = in[3];
	m.m4 = in[4];   m.m5 = in[5];   m.m6 = in[6];   m.m7 = in[7];
	m.m8 = in[8];   m.m9 = in[9];   m.m10 = in[10]; m.m11 = in[11];
	m.m12 = in[12]; m.m13 = in[13]; m.m14 = in[14]; m.m15 = in[15];
	return m;
}

/* texture_ref: a texture id as raylib's Texture2D for
 * SetShaderValueTexture() (only the id is read). */
static Texture2D
texture_ref (guint id)
{
	Texture2D t;

	memset (&t, 0, sizeof t);
	t.id = id;
	return t;
}

/* set_common:
 * The prelude uniforms every pass shares. */
static void
set_common (LrgSsao             *self,
            Shader               shader,
            const PassLocations *locs,
            const gfloat        *inv_proj,
            guint                depth_tex,
            gint                 out_w,
            gint                 out_h,
            gint                 depth_w,
            gint                 depth_h)
{
	gfloat params[4];
	gfloat size[4];

	params[0] = self->radius;
	params[1] = self->bias;
	params[2] = self->intensity;
	params[3] = self->power;
	size[0] = (gfloat) out_w;
	size[1] = (gfloat) out_h;
	size[2] = (gfloat) depth_w;
	size[3] = (gfloat) depth_h;
	SetShaderValue (shader, locs->params, params, SHADER_UNIFORM_VEC4);
	SetShaderValue (shader, locs->size, size, SHADER_UNIFORM_VEC4);
	SetShaderValueMatrix (shader, locs->inv_proj, floats_to_matrix (inv_proj));
	SetShaderValueTexture (shader, locs->depth, texture_ref (depth_tex));
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_ssao_finalize (GObject *object)
{
	free_gpu (LRG_SSAO (object));

	G_OBJECT_CLASS (lrg_ssao_parent_class)->finalize (object);
}

static void
lrg_ssao_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
	LrgSsao *self = LRG_SSAO (object);

	switch (prop_id)
	{
	case PROP_RADIUS:
		g_value_set_float (value, self->radius);
		break;
	case PROP_BIAS:
		g_value_set_float (value, self->bias);
		break;
	case PROP_INTENSITY:
		g_value_set_float (value, self->intensity);
		break;
	case PROP_POWER:
		g_value_set_float (value, self->power);
		break;
	case PROP_KERNEL_SIZE:
		g_value_set_uint (value, self->kernel_size);
		break;
	case PROP_SEED:
		g_value_set_uint (value, self->seed);
		break;
	case PROP_HALF_RESOLUTION:
		g_value_set_boolean (value, self->half);
		break;
	case PROP_BLUR:
		g_value_set_boolean (value, self->blur);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_ssao_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
	LrgSsao *self = LRG_SSAO (object);

	switch (prop_id)
	{
	case PROP_RADIUS:
		lrg_ssao_set_radius (self, g_value_get_float (value));
		break;
	case PROP_BIAS:
		lrg_ssao_set_bias (self, g_value_get_float (value));
		break;
	case PROP_INTENSITY:
		lrg_ssao_set_intensity (self, g_value_get_float (value));
		break;
	case PROP_POWER:
		lrg_ssao_set_power (self, g_value_get_float (value));
		break;
	case PROP_KERNEL_SIZE:
		lrg_ssao_set_kernel_size (self, g_value_get_uint (value));
		break;
	case PROP_SEED:
		lrg_ssao_set_seed (self, g_value_get_uint (value));
		break;
	case PROP_HALF_RESOLUTION:
		lrg_ssao_set_half_resolution (self, g_value_get_boolean (value));
		break;
	case PROP_BLUR:
		lrg_ssao_set_blur (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_ssao_class_init (LrgSsaoClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   flags = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->finalize = lrg_ssao_finalize;
	object_class->get_property = lrg_ssao_get_property;
	object_class->set_property = lrg_ssao_set_property;

	/**
	 * LrgSsao:radius:
	 *
	 * Sampling radius in world units.
	 */
	properties[PROP_RADIUS] =
		g_param_spec_float ("radius", NULL, "Sampling radius", 0.001f, G_MAXFLOAT, 0.5f, flags);

	/**
	 * LrgSsao:bias:
	 *
	 * Depth bias in world units against self-occlusion.
	 */
	properties[PROP_BIAS] =
		g_param_spec_float ("bias", NULL, "Depth bias", 0.0f, G_MAXFLOAT, 0.025f, flags);

	/**
	 * LrgSsao:intensity:
	 *
	 * Occlusion scale (0: no effect).
	 */
	properties[PROP_INTENSITY] =
		g_param_spec_float ("intensity", NULL, "Occlusion scale", 0.0f, G_MAXFLOAT, 1.0f, flags);

	/**
	 * LrgSsao:power:
	 *
	 * Contrast exponent of the visibility.
	 */
	properties[PROP_POWER] =
		g_param_spec_float ("power", NULL, "Contrast exponent", 0.01f, 16.0f, 1.5f, flags);

	/**
	 * LrgSsao:kernel-size:
	 *
	 * Hemisphere samples per pixel.
	 */
	properties[PROP_KERNEL_SIZE] =
		g_param_spec_uint ("kernel-size", NULL, "Samples per pixel", 4, LRG_SSAO_MAX_KERNEL, 16,
		                   flags);

	/**
	 * LrgSsao:seed:
	 *
	 * Seed of the kernel and noise generator.
	 */
	properties[PROP_SEED] =
		g_param_spec_uint ("seed", NULL, "Generator seed", 0, G_MAXUINT32, 1, flags);

	/**
	 * LrgSsao:half-resolution:
	 *
	 * Compute at half the prepass resolution.
	 */
	properties[PROP_HALF_RESOLUTION] =
		g_param_spec_boolean ("half-resolution", NULL, "Half resolution", TRUE, flags);

	/**
	 * LrgSsao:blur:
	 *
	 * Run the depth-aware blur.
	 */
	properties[PROP_BLUR] =
		g_param_spec_boolean ("blur", NULL, "Blur the result", TRUE, flags);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_ssao_init (LrgSsao *self)
{
	self->radius = 0.5f;
	self->bias = 0.025f;
	self->intensity = 1.0f;
	self->power = 1.5f;
	self->kernel_size = 16;
	self->seed = 1;
	self->half = TRUE;
	self->blur = TRUE;
	generate_kernel (self);
	generate_noise (self);
}

/* ==========================================================================
 * Parameters
 * ========================================================================== */

LrgSsao *
lrg_ssao_new (void)
{
	return (LrgSsao *) g_object_new (LRG_TYPE_SSAO, NULL);
}

/* set_float:
 * Stores a clamped float parameter and notifies on change. */
static void
set_float (LrgSsao *self,
           gfloat  *field,
           gfloat   value,
           guint    prop)
{
	if (*field == value)
		return;
	*field = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

void
lrg_ssao_set_radius (LrgSsao *self,
                     gfloat   radius)
{
	g_return_if_fail (LRG_IS_SSAO (self));
	g_return_if_fail (radius > 0.0f);

	set_float (self, &self->radius, MAX (radius, 0.001f), PROP_RADIUS);
}

gfloat
lrg_ssao_get_radius (LrgSsao *self)
{
	g_return_val_if_fail (LRG_IS_SSAO (self), 0.0f);

	return self->radius;
}

void
lrg_ssao_set_bias (LrgSsao *self,
                   gfloat   bias)
{
	g_return_if_fail (LRG_IS_SSAO (self));

	set_float (self, &self->bias, MAX (bias, 0.0f), PROP_BIAS);
}

gfloat
lrg_ssao_get_bias (LrgSsao *self)
{
	g_return_val_if_fail (LRG_IS_SSAO (self), 0.0f);

	return self->bias;
}

void
lrg_ssao_set_intensity (LrgSsao *self,
                        gfloat   intensity)
{
	g_return_if_fail (LRG_IS_SSAO (self));

	set_float (self, &self->intensity, MAX (intensity, 0.0f), PROP_INTENSITY);
}

gfloat
lrg_ssao_get_intensity (LrgSsao *self)
{
	g_return_val_if_fail (LRG_IS_SSAO (self), 0.0f);

	return self->intensity;
}

void
lrg_ssao_set_power (LrgSsao *self,
                    gfloat   power)
{
	g_return_if_fail (LRG_IS_SSAO (self));
	g_return_if_fail (power > 0.0f);

	set_float (self, &self->power, CLAMP (power, 0.01f, 16.0f), PROP_POWER);
}

gfloat
lrg_ssao_get_power (LrgSsao *self)
{
	g_return_val_if_fail (LRG_IS_SSAO (self), 0.0f);

	return self->power;
}

void
lrg_ssao_set_kernel_size (LrgSsao *self,
                          guint    samples)
{
	g_return_if_fail (LRG_IS_SSAO (self));

	samples = CLAMP (samples, 4u, (guint) LRG_SSAO_MAX_KERNEL);
	if (self->kernel_size == samples)
		return;
	self->kernel_size = samples;
	generate_kernel (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KERNEL_SIZE]);
}

guint
lrg_ssao_get_kernel_size (LrgSsao *self)
{
	g_return_val_if_fail (LRG_IS_SSAO (self), 0);

	return self->kernel_size;
}

void
lrg_ssao_set_seed (LrgSsao *self,
                   guint32  seed)
{
	g_return_if_fail (LRG_IS_SSAO (self));

	if (self->seed == seed)
		return;
	self->seed = seed;
	generate_kernel (self);
	generate_noise (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SEED]);
}

guint32
lrg_ssao_get_seed (LrgSsao *self)
{
	g_return_val_if_fail (LRG_IS_SSAO (self), 0);

	return self->seed;
}

void
lrg_ssao_set_half_resolution (LrgSsao  *self,
                              gboolean  half)
{
	g_return_if_fail (LRG_IS_SSAO (self));

	half = !!half;
	if (self->half == half)
		return;
	self->half = half;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HALF_RESOLUTION]);
}

gboolean
lrg_ssao_get_half_resolution (LrgSsao *self)
{
	g_return_val_if_fail (LRG_IS_SSAO (self), FALSE);

	return self->half;
}

void
lrg_ssao_set_blur (LrgSsao  *self,
                   gboolean  blur)
{
	g_return_if_fail (LRG_IS_SSAO (self));

	blur = !!blur;
	if (self->blur == blur)
		return;
	self->blur = blur;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLUR]);
}

gboolean
lrg_ssao_get_blur (LrgSsao *self)
{
	g_return_val_if_fail (LRG_IS_SSAO (self), FALSE);

	return self->blur;
}

void
lrg_ssao_get_kernel_sample (LrgSsao *self,
                            guint    index,
                            gfloat  *out_xyz)
{
	g_return_if_fail (LRG_IS_SSAO (self));
	g_return_if_fail (out_xyz != NULL);
	g_return_if_fail (index < self->kernel_size);

	memcpy (out_xyz, &self->kernel[index * 3], 3 * sizeof (gfloat));
}

void
lrg_ssao_get_noise_sample (LrgSsao *self,
                           guint    index,
                           gfloat  *out_xy)
{
	g_return_if_fail (LRG_IS_SSAO (self));
	g_return_if_fail (out_xy != NULL);
	g_return_if_fail (index < LRG_SSAO_NOISE_SIZE * LRG_SSAO_NOISE_SIZE);

	out_xy[0] = self->noise[index * 3 + 0];
	out_xy[1] = self->noise[index * 3 + 1];
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

gboolean
lrg_ssao_render (LrgSsao         *self,
                 LrgDepthPrepass *prepass)
{
	gfloat           proj[16];
	gfloat           inv_proj[16];
	gint             pw, ph, w, h;
	gint             use_normals;
	gint             n_kernel;
	guint            depth_tex;
	RenderTexture2D  rt;

	g_return_val_if_fail (LRG_IS_SSAO (self), FALSE);
	g_return_val_if_fail (LRG_IS_DEPTH_PREPASS (prepass), FALSE);

	if (!IsWindowReady () || !lrg_depth_prepass_is_valid (prepass) || !ensure_gpu (self))
		return FALSE;

	/* the output size and the projection to rebuild positions with */
	pw = lrg_depth_prepass_get_width (prepass);
	ph = lrg_depth_prepass_get_height (prepass);
	w = self->half ? MAX ((pw + 1) / 2, 1) : pw;
	h = self->half ? MAX ((ph + 1) / 2, 1) : ph;
	lrg_depth_prepass_get_projection (prepass, proj);
	if (!lrg_math3d_invert (proj, inv_proj))
		return FALSE;
	depth_tex = lrg_depth_prepass_get_depth_texture_id (prepass);
	if (!target_ensure (&self->ao, w, h))
		return FALSE;

	/* occlusion pass */
	use_normals = lrg_depth_prepass_get_normals (prepass) ? 1 : 0;
	n_kernel = (gint) self->kernel_size;
	rt = target_render_texture (&self->ao);
	BeginTextureMode (rt);
	BeginShaderMode (self->ao_shader);
	set_common (self, self->ao_shader, &self->ao_locs, inv_proj, depth_tex, w, h, pw, ph);
	SetShaderValueMatrix (self->ao_shader, self->ao_locs.proj, floats_to_matrix (proj));
	SetShaderValue (self->ao_shader, self->ao_locs.use_normals, &use_normals, SHADER_UNIFORM_INT);
	SetShaderValue (self->ao_shader, self->ao_locs.kernel_size, &n_kernel, SHADER_UNIFORM_INT);
	SetShaderValueV (self->ao_shader, self->ao_locs.kernel, self->kernel, SHADER_UNIFORM_VEC3,
	                 n_kernel);
	SetShaderValueTexture (self->ao_shader, self->ao_locs.normals,
	                       texture_ref (lrg_depth_prepass_get_normal_texture_id (prepass)));
	SetShaderValueTexture (self->ao_shader, self->ao_locs.noise, texture_ref (self->noise_tex));
	DrawRectangle (0, 0, w, h, WHITE);
	EndShaderMode ();
	EndTextureMode ();
	self->result_blurred = FALSE;

	/* blur pass into the second target */
	if (self->blur && self->blur_shader.id != 0 && target_ensure (&self->blurred, w, h))
	{
		rt = target_render_texture (&self->blurred);
		BeginTextureMode (rt);
		BeginShaderMode (self->blur_shader);
		set_common (self, self->blur_shader, &self->blur_locs, inv_proj, depth_tex, w, h, pw, ph);
		SetShaderValueTexture (self->blur_shader, self->blur_locs.input,
		                       texture_ref (self->ao.tex));
		DrawRectangle (0, 0, w, h, WHITE);
		EndShaderMode ();
		EndTextureMode ();
		self->result_blurred = TRUE;
	}

	self->auto_viewport[0] = 0;
	self->auto_viewport[1] = 0;
	self->auto_viewport[2] = pw;
	self->auto_viewport[3] = ph;
	self->has_result = TRUE;
	return TRUE;
}

gboolean
lrg_ssao_has_result (LrgSsao *self)
{
	g_return_val_if_fail (LRG_IS_SSAO (self), FALSE);

	return self->has_result;
}

guint
lrg_ssao_get_texture_id (LrgSsao *self)
{
	g_return_val_if_fail (LRG_IS_SSAO (self), 0);

	if (!self->has_result)
		return 0;
	return self->result_blurred ? self->blurred.tex : self->ao.tex;
}

gint
lrg_ssao_get_width (LrgSsao *self)
{
	g_return_val_if_fail (LRG_IS_SSAO (self), 0);

	return self->has_result ? self->ao.width : 0;
}

gint
lrg_ssao_get_height (LrgSsao *self)
{
	g_return_val_if_fail (LRG_IS_SSAO (self), 0);

	return self->has_result ? self->ao.height : 0;
}

void
lrg_ssao_set_viewport (LrgSsao *self,
                       gint     x,
                       gint     y,
                       gint     width,
                       gint     height)
{
	g_return_if_fail (LRG_IS_SSAO (self));

	self->viewport[0] = x;
	self->viewport[1] = y;
	self->viewport[2] = MAX (width, 0);
	self->viewport[3] = MAX (height, 0);
}

void
lrg_ssao_release (LrgSsao *self)
{
	g_return_if_fail (LRG_IS_SSAO (self));

	free_gpu (self);
	self->noise_dirty = TRUE;
}

/* ==========================================================================
 * Lit-shader hook
 * ========================================================================== */

/* hook uniform locations of one program */
typedef struct
{
	gint on;
	gint texture;
	gint viewport;
} HookLocations;

/* program id -> HookLocations; drawing is single threaded */
static GHashTable *hook_cache = NULL;

static const HookLocations *
hook_locations (guint shader_id)
{
	HookLocations *locs;

	if (hook_cache == NULL)
		hook_cache = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	locs = g_hash_table_lookup (hook_cache, GUINT_TO_POINTER (shader_id));
	if (locs == NULL)
	{
		locs = g_new (HookLocations, 1);
		locs->on = rlGetLocationUniform (shader_id, "lrgSsaoOn");
		locs->texture = rlGetLocationUniform (shader_id, "lrgSsaoTexture");
		locs->viewport = rlGetLocationUniform (shader_id, "lrgSsaoViewport");
		g_hash_table_insert (hook_cache, GUINT_TO_POINTER (shader_id), locs);
	}
	return locs;
}

void
lrg_ssao_bind_private (LrgSsao *ssao,
                       guint    shader_id,
                       gint    *texture_unit)
{
	const HookLocations *locs;
	gint                 on = 0;
	gint                 unit;
	gfloat               vp[4];
	guint                tex;

	g_return_if_fail (ssao == NULL || LRG_IS_SSAO (ssao));
	g_return_if_fail (texture_unit != NULL);

	if (shader_id == 0)
		return;
	locs = hook_locations (shader_id);
	if (locs->on < 0)
		return;     /* the program has no (live) hook */
	rlEnableShader (shader_id);

	/* inactive: no result, no sampler, or no hook unit left; the sampler
	 * then points at unit 0 (texture0, also a sampler2D) */
	tex = (ssao != NULL) ? lrg_ssao_get_texture_id (ssao) : 0;
	if (tex == 0 || locs->texture < 0 || !lrg_light_set_hook_unit_usable (*texture_unit))
	{
		unit = 0;
		if (locs->texture >= 0)
			rlSetUniform (locs->texture, &unit, RL_SHADER_UNIFORM_INT, 1);
		rlSetUniform (locs->on, &on, RL_SHADER_UNIFORM_INT, 1);
		return;
	}

	/* the result on the next free unit */
	unit = *texture_unit;
	rlActiveTextureSlot (unit);
	rlEnableTexture (tex);
	rlActiveTextureSlot (0);
	rlSetUniform (locs->texture, &unit, RL_SHADER_UNIFORM_INT, 1);
	*texture_unit = unit + 1;

	/* the lit pass's viewport, explicit or the prepass size */
	if (ssao->viewport[2] > 0 && ssao->viewport[3] > 0)
	{
		vp[0] = (gfloat) ssao->viewport[0];
		vp[1] = (gfloat) ssao->viewport[1];
		vp[2] = (gfloat) ssao->viewport[2];
		vp[3] = (gfloat) ssao->viewport[3];
	}
	else
	{
		vp[0] = (gfloat) ssao->auto_viewport[0];
		vp[1] = (gfloat) ssao->auto_viewport[1];
		vp[2] = (gfloat) ssao->auto_viewport[2];
		vp[3] = (gfloat) ssao->auto_viewport[3];
	}
	if (locs->viewport >= 0)
		rlSetUniform (locs->viewport, vp, RL_SHADER_UNIFORM_VEC4, 1);
	on = 1;
	rlSetUniform (locs->on, &on, RL_SHADER_UNIFORM_INT, 1);
}

gint
lrg_ssao_bind (LrgSsao *self,
               guint    shader_id,
               gint     first_unit)
{
	gint unit = first_unit;

	g_return_val_if_fail (LRG_IS_SSAO (self), first_unit);

	lrg_ssao_bind_private (self, shader_id, &unit);
	return unit;
}

void
lrg_ssao_bind_inactive (guint shader_id)
{
	gint unit = 0;

	lrg_ssao_bind_private (NULL, shader_id, &unit);
}

const gchar *
lrg_ssao_get_glsl (void)
{
	return LRG_SSAO_GLSL;
}
