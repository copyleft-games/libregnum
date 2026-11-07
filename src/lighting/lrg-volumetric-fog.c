/* lrg-volumetric-fog.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVolumetricFog: a screen-space march of an exponential height fog
 * lit by the light set's sun through its cascaded shadow map, at a
 * reduced resolution, upsampled with depth-aware weights and composited
 * as colour x transmittance + scattering.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-volumetric-fog.h"
#include "lrg-shadow-renderer.h"
#include "lrg-shadow-renderer-private.h"
#include "../core/lrg-math3d.h"

#define FOG_PI (3.14159265358979323846)

/* texture units of the two passes (the shadow atlases use theirs) */
#define FOG_DEPTH_UNIT   (1)
#define FOG_SCATTER_UNIT (2)

struct _LrgVolumetricFog
{
	GObject  parent_instance;

	gfloat   density;
	gfloat   anisotropy;
	guint    steps;
	gfloat   max_distance;
	gfloat   intensity;
	gfloat   base_height;
	gfloat   falloff;
	gfloat   tint[3];
	gfloat   ambient[3];
	gfloat   scale;

	/* GPU */
	gboolean           shaders_ok;
	Shader             march;
	Shader             composite;
	LrgShadowLocations shadow_locs;
	guint              target;
	guint              fbo;
	gint               target_w;
	gint               target_h;
	guint              frame;
};

G_DEFINE_FINAL_TYPE (LrgVolumetricFog, lrg_volumetric_fog, G_TYPE_OBJECT)

static void
release (LrgVolumetricFog *self)
{
	if (IsWindowReady ())
	{
		if (self->shaders_ok)
		{
			UnloadShader (self->march);
			UnloadShader (self->composite);
		}
		if (self->fbo != 0)
			rlUnloadFramebuffer (self->fbo);
		if (self->target != 0)
			rlUnloadTexture (self->target);
	}
	self->shaders_ok = FALSE;
	memset (&self->march, 0, sizeof (self->march));
	memset (&self->composite, 0, sizeof (self->composite));
	self->fbo = 0;
	self->target = 0;
	self->target_w = 0;
	self->target_h = 0;
}

static void
lrg_volumetric_fog_finalize (GObject *object)
{
	release (LRG_VOLUMETRIC_FOG (object));
	G_OBJECT_CLASS (lrg_volumetric_fog_parent_class)->finalize (object);
}

static void
lrg_volumetric_fog_class_init (LrgVolumetricFogClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_volumetric_fog_finalize;
}

static void
lrg_volumetric_fog_init (LrgVolumetricFog *self)
{
	self->density = 0.02f;
	self->anisotropy = 0.6f;
	self->steps = 32;
	self->max_distance = 100.0f;
	self->intensity = 1.0f;
	self->base_height = 0.0f;
	self->falloff = 0.1f;
	self->tint[0] = self->tint[1] = self->tint[2] = 1.0f;
	self->scale = 0.5f;
}

LrgVolumetricFog *
lrg_volumetric_fog_new (void)
{
	return g_object_new (LRG_TYPE_VOLUMETRIC_FOG, NULL);
}

/* ==========================================================================
 * Parameters
 * ========================================================================== */

void
lrg_volumetric_fog_set_density (LrgVolumetricFog *self,
                                gfloat            density)
{
	g_return_if_fail (LRG_IS_VOLUMETRIC_FOG (self));
	self->density = MAX (density, 0.0f);
}

gfloat
lrg_volumetric_fog_get_density (LrgVolumetricFog *self)
{
	g_return_val_if_fail (LRG_IS_VOLUMETRIC_FOG (self), 0.0f);
	return self->density;
}

void
lrg_volumetric_fog_set_anisotropy (LrgVolumetricFog *self,
                                   gfloat            g)
{
	g_return_if_fail (LRG_IS_VOLUMETRIC_FOG (self));
	self->anisotropy = CLAMP (g, -0.95f, 0.95f);
}

gfloat
lrg_volumetric_fog_get_anisotropy (LrgVolumetricFog *self)
{
	g_return_val_if_fail (LRG_IS_VOLUMETRIC_FOG (self), 0.0f);
	return self->anisotropy;
}

void
lrg_volumetric_fog_set_steps (LrgVolumetricFog *self,
                              guint             steps)
{
	g_return_if_fail (LRG_IS_VOLUMETRIC_FOG (self));
	self->steps = CLAMP (steps, 4u, 128u);
}

guint
lrg_volumetric_fog_get_steps (LrgVolumetricFog *self)
{
	g_return_val_if_fail (LRG_IS_VOLUMETRIC_FOG (self), 0);
	return self->steps;
}

void
lrg_volumetric_fog_set_max_distance (LrgVolumetricFog *self,
                                     gfloat            distance)
{
	g_return_if_fail (LRG_IS_VOLUMETRIC_FOG (self));
	self->max_distance = MAX (distance, 0.01f);
}

gfloat
lrg_volumetric_fog_get_max_distance (LrgVolumetricFog *self)
{
	g_return_val_if_fail (LRG_IS_VOLUMETRIC_FOG (self), 0.0f);
	return self->max_distance;
}

void
lrg_volumetric_fog_set_intensity (LrgVolumetricFog *self,
                                  gfloat            intensity)
{
	g_return_if_fail (LRG_IS_VOLUMETRIC_FOG (self));
	self->intensity = MAX (intensity, 0.0f);
}

gfloat
lrg_volumetric_fog_get_intensity (LrgVolumetricFog *self)
{
	g_return_val_if_fail (LRG_IS_VOLUMETRIC_FOG (self), 0.0f);
	return self->intensity;
}

void
lrg_volumetric_fog_set_height (LrgVolumetricFog *self,
                               gfloat            base_height,
                               gfloat            falloff)
{
	g_return_if_fail (LRG_IS_VOLUMETRIC_FOG (self));
	self->base_height = base_height;
	self->falloff = MAX (falloff, 0.0f);
}

void
lrg_volumetric_fog_set_tint (LrgVolumetricFog *self,
                             const gfloat     *rgb)
{
	g_return_if_fail (LRG_IS_VOLUMETRIC_FOG (self));
	g_return_if_fail (rgb != NULL);
	memcpy (self->tint, rgb, sizeof (self->tint));
}

void
lrg_volumetric_fog_set_ambient (LrgVolumetricFog *self,
                                const gfloat     *rgb)
{
	g_return_if_fail (LRG_IS_VOLUMETRIC_FOG (self));
	g_return_if_fail (rgb != NULL);
	memcpy (self->ambient, rgb, sizeof (self->ambient));
}

void
lrg_volumetric_fog_set_resolution_scale (LrgVolumetricFog *self,
                                         gfloat            scale)
{
	g_return_if_fail (LRG_IS_VOLUMETRIC_FOG (self));
	self->scale = CLAMP (scale, 0.25f, 1.0f);
}

gfloat
lrg_volumetric_fog_get_resolution_scale (LrgVolumetricFog *self)
{
	g_return_val_if_fail (LRG_IS_VOLUMETRIC_FOG (self), 0.5f);
	return self->scale;
}

gboolean
lrg_volumetric_fog_set_from_environment (LrgVolumetricFog *self,
                                         LrgEnvironment   *environment)
{
	gfloat   density;
	gfloat   g;
	guint    steps;
	gfloat   dist;
	gfloat   intensity;
	gfloat   base;
	gfloat   falloff;
	gfloat   rgba[4];
	gfloat   amb[3];
	gfloat   amb_k;
	gfloat   k;
	gboolean enabled;
	guint    i;

	g_return_val_if_fail (LRG_IS_VOLUMETRIC_FOG (self), FALSE);
	g_return_val_if_fail (LRG_IS_ENVIRONMENT (environment), FALSE);

	enabled = lrg_environment_get_volumetric (environment, &density, &g, &steps, &dist, &intensity);
	lrg_environment_get_fog_height (environment, &base, &falloff);
	lrg_environment_get_fog (environment, NULL, rgba, NULL, NULL, NULL);
	lrg_environment_get_ambient (environment, amb, &amb_k);
	k = amb_k * exp2f (lrg_environment_get_exposure (environment));

	lrg_volumetric_fog_set_density (self, density);
	lrg_volumetric_fog_set_anisotropy (self, g);
	lrg_volumetric_fog_set_steps (self, steps);
	lrg_volumetric_fog_set_max_distance (self, dist);
	lrg_volumetric_fog_set_intensity (self, intensity);
	lrg_volumetric_fog_set_height (self, base, falloff);
	for (i = 0; i < 3; i++)
	{
		self->tint[i] = powf (MAX (rgba[i], 0.0f), 2.2f);
		self->ambient[i] = powf (MAX (amb[i], 0.0f), 2.2f) * k;
	}
	return enabled;
}

/* ==========================================================================
 * CPU mirror
 * ========================================================================== */

static gfloat
hg_phase (gfloat g,
          gfloat cos_theta)
{
	gfloat d = 1.0f + g * g - 2.0f * g * cos_theta;

	return (1.0f - g * g) / (4.0f * (gfloat)FOG_PI * powf (MAX (d, 1e-6f), 1.5f));
}

gfloat
lrg_volumetric_fog_sample (LrgVolumetricFog *self,
                           const gfloat     *from,
                           const gfloat     *to,
                           const gfloat     *sun_direction,
                           const gfloat     *sun_rgb,
                           gfloat           *out_rgb)
{
	gfloat rd[3];
	gfloat len;
	gfloat dt;
	gfloat t_trans = 1.0f;
	gfloat phase = 0.0f;
	gfloat sun[3] = { 0.0f, 0.0f, 0.0f };
	guint  i;
	guint  c;

	g_return_val_if_fail (LRG_IS_VOLUMETRIC_FOG (self), 1.0f);
	g_return_val_if_fail (from != NULL && to != NULL && out_rgb != NULL, 1.0f);

	out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
	for (c = 0; c < 3; c++)
		rd[c] = to[c] - from[c];
	len = sqrtf (rd[0] * rd[0] + rd[1] * rd[1] + rd[2] * rd[2]);
	if (len <= 0.0f)
		return 1.0f;
	for (c = 0; c < 3; c++)
		rd[c] /= len;
	len = MIN (len, self->max_distance);
	dt = len / (gfloat)self->steps;

	if (sun_direction != NULL && sun_rgb != NULL)
	{
		gfloat sl = sqrtf (sun_direction[0] * sun_direction[0] + sun_direction[1] * sun_direction[1] +
		                   sun_direction[2] * sun_direction[2]);

		/* cosine between the light's travel and the ray toward the eye */
		if (sl > 0.0f)
			phase = hg_phase (self->anisotropy,
			                  -(sun_direction[0] * rd[0] + sun_direction[1] * rd[1] +
			                    sun_direction[2] * rd[2]) / sl);
		memcpy (sun, sun_rgb, sizeof (sun));
	}

	for (i = 0; i < self->steps; i++)
	{
		gfloat t = ((gfloat)i + 0.5f) * dt;
		gfloat y = from[1] + rd[1] * t;
		gfloat sigma = self->density * expf (-self->falloff * MAX (y - self->base_height, 0.0f));
		gfloat step_t = expf (-sigma * dt);

		/* energy-conserving step integral: (1 - exp (-s dt)) / s x L */
		for (c = 0; c < 3; c++)
		{
			gfloat l = (sun[c] * phase + self->ambient[c] / (4.0f * (gfloat)FOG_PI)) *
			           self->tint[c] * self->intensity;

			out_rgb[c] += t_trans * (1.0f - step_t) * l;
		}
		t_trans *= step_t;
	}
	return t_trans;
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

static const gchar *fullscreen_vs =
	"#version 330\n"
	"in vec3 vertexPosition;\n"
	"out vec2 fragUv;\n"
	"void main() {\n"
	"    fragUv = vertexPosition.xy;\n"
	"    gl_Position = vec4(vertexPosition.xy * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

#define FOG_COMMON_GLSL \
	"uniform sampler2D depthMap;\n" \
	"uniform mat4 invViewProj;\n" \
	"uniform vec3 eye;\n" \
	"vec3 fog_world(vec2 uv, float depth) {\n" \
	"    vec4 w = invViewProj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);\n" \
	"    return w.xyz / w.w;\n" \
	"}\n"

static const gchar *march_fs_head =
	"#version 330\n"
	"in vec2 fragUv;\n"
	"out vec4 finalColor;\n"
	FOG_COMMON_GLSL
	"uniform vec4 fogParams;\n"	/* density, g, max distance, intensity */
	"uniform vec2 fogHeight;\n"	/* base, falloff */
	"uniform int fogSteps;\n"
	"uniform vec3 fogSunDir;\n"
	"uniform vec3 fogSunColor;\n"
	"uniform vec3 fogAmbient;\n"
	"uniform vec3 fogTint;\n"
	"uniform float fogJitter;\n";

static const gchar *march_fs_body =
	"const float FOG_PI = 3.14159265;\n"
	"float fog_phase(float g, float c) {\n"
	"    float d = 1.0 + g * g - 2.0 * g * c;\n"
	"    return (1.0 - g * g) / (4.0 * FOG_PI * pow(max(d, 1e-6), 1.5));\n"
	"}\n"
	"void main() {\n"
	"    float depth = texture(depthMap, fragUv).r;\n"
	"    vec3 end = fog_world(fragUv, depth);\n"
	"    vec3 rd = end - eye;\n"
	"    float len = length(rd);\n"
	"    rd = len > 0.0 ? rd / len : vec3(0.0, 0.0, -1.0);\n"
	"    if (depth >= 0.99999) len = fogParams.z;\n"
	"    len = min(len, fogParams.z);\n"
	"    float dt = len / float(fogSteps);\n"
	"    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + fogJitter);\n"
	"    float ph = fog_phase(fogParams.y, dot(fogSunDir, -rd));\n"
	"    vec3 scat = vec3(0.0);\n"
	"    float trans = 1.0;\n"
	"    for (int i = 0; i < 128; i++) {\n"
	"        if (i >= fogSteps) break;\n"
	"        float t = (float(i) + noise) * dt;\n"
	"        vec3 p = eye + rd * t;\n"
	"        float sigma = fogParams.x * exp(-fogHeight.y * max(p.y - fogHeight.x, 0.0));\n"
	"        float st = exp(-sigma * dt);\n"
	"        float vis = lrg_sun_shadow_f(p, vec3(0.0), 0);\n"
	"        vec3 l = (fogSunColor * (ph * vis) + fogAmbient / (4.0 * FOG_PI)) * fogTint * fogParams.w;\n"
	"        scat += trans * (1.0 - st) * l;\n"
	"        trans *= st;\n"
	"    }\n"
	"    finalColor = vec4(scat, trans);\n"
	"}\n";

/* bilateral upsample: the four nearest march texels weighted by
 * bilinear position and by how close their surface distance is to this
 * pixel's */
static const gchar *composite_fs =
	"#version 330\n"
	"in vec2 fragUv;\n"
	"out vec4 finalColor;\n"
	FOG_COMMON_GLSL
	"uniform sampler2D scatterMap;\n"
	"uniform vec2 scatterSize;\n"
	"uniform int outputLinear;\n"
	"float fog_dist(vec2 uv) { return length(fog_world(uv, texture(depthMap, uv).r) - eye); }\n"
	"void main() {\n"
	"    float d0 = fog_dist(fragUv);\n"
	"    vec2 f = fragUv * scatterSize - 0.5;\n"
	"    vec2 b = floor(f);\n"
	"    vec2 w = f - b;\n"
	"    vec4 acc = vec4(0.0);\n"
	"    float wsum = 0.0;\n"
	"    for (int j = 0; j < 2; j++) {\n"
	"        for (int i = 0; i < 2; i++) {\n"
	"            vec2 c = (b + vec2(float(i), float(j)) + 0.5) / scatterSize;\n"
	"            c = clamp(c, vec2(0.0), vec2(1.0));\n"
	"            float bw = (i == 0 ? 1.0 - w.x : w.x) * (j == 0 ? 1.0 - w.y : w.y);\n"
	"            float dw = 1.0 / (1e-3 + abs(fog_dist(c) - d0) / max(d0, 1e-3));\n"
	"            float k = bw * dw + 1e-5;\n"
	"            acc += texture(scatterMap, c) * k;\n"
	"            wsum += k;\n"
	"        }\n"
	"    }\n"
	"    vec4 s = acc / wsum;\n"
	"    vec3 col = outputLinear != 0 ? s.rgb : pow(max(s.rgb, vec3(0.0)), vec3(1.0 / 2.2));\n"
	"    finalColor = vec4(col, s.a);\n"
	"}\n";

static gboolean
ensure_shaders (LrgVolumetricFog  *self,
                GError           **error)
{
	g_autofree gchar *march_fs = NULL;

	if (self->shaders_ok)
		return TRUE;
	march_fs = g_strconcat (march_fs_head, LRG_SHADOW_GLSL, march_fs_body, NULL);
	self->march = LoadShaderFromMemory (fullscreen_vs, march_fs);
	self->composite = LoadShaderFromMemory (fullscreen_vs, composite_fs);
	if (self->march.id == 0 || self->march.id == rlGetShaderIdDefault () ||
	    self->composite.id == 0 || self->composite.id == rlGetShaderIdDefault ())
	{
		if (self->march.id != 0 && self->march.id != rlGetShaderIdDefault ())
			UnloadShader (self->march);
		if (self->composite.id != 0 && self->composite.id != rlGetShaderIdDefault ())
			UnloadShader (self->composite);
		memset (&self->march, 0, sizeof (self->march));
		memset (&self->composite, 0, sizeof (self->composite));
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU,
		             "The volumetric fog shaders did not compile (GLSL 330 needed)");
		return FALSE;
	}
	lrg_shadow_locations (self->march, &self->shadow_locs);
	self->shaders_ok = TRUE;
	return TRUE;
}

static gboolean
ensure_target (LrgVolumetricFog  *self,
               gint               w,
               gint               h,
               GError           **error)
{
	if (self->target != 0 && self->target_w == w && self->target_h == h)
		return TRUE;
	if (self->fbo != 0)
		rlUnloadFramebuffer (self->fbo);
	if (self->target != 0)
		rlUnloadTexture (self->target);
	self->target = rlLoadTexture (NULL, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
	self->fbo = rlLoadFramebuffer ();
	if (self->target == 0 || self->fbo == 0)
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU,
		             "Cannot create the volumetric fog target");
		return FALSE;
	}
	rlTextureParameters (self->target, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
	rlTextureParameters (self->target, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
	rlTextureParameters (self->target, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
	rlTextureParameters (self->target, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
	rlFramebufferAttach (self->fbo, self->target, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
	if (!rlFramebufferComplete (self->fbo))
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU,
		             "The volumetric fog framebuffer is incomplete");
		return FALSE;
	}
	self->target_w = w;
	self->target_h = h;
	return TRUE;
}

static void
draw_quad (void)
{
	rlBegin (RL_QUADS);
	rlColor4ub (255, 255, 255, 255);
	rlVertex3f (0.0f, 0.0f, 0.0f);
	rlVertex3f (1.0f, 0.0f, 0.0f);
	rlVertex3f (1.0f, 1.0f, 0.0f);
	rlVertex3f (0.0f, 1.0f, 0.0f);
	rlEnd ();
	rlDrawRenderBatchActive ();
}

static Matrix
to_matrix (const gfloat *m)
{
	Matrix r;

	r.m0 = m[0];   r.m1 = m[1];   r.m2 = m[2];   r.m3 = m[3];
	r.m4 = m[4];   r.m5 = m[5];   r.m6 = m[6];   r.m7 = m[7];
	r.m8 = m[8];   r.m9 = m[9];   r.m10 = m[10]; r.m11 = m[11];
	r.m12 = m[12]; r.m13 = m[13]; r.m14 = m[14]; r.m15 = m[15];
	return r;
}

gboolean
lrg_volumetric_fog_render (LrgVolumetricFog  *self,
                           guint              depth_texture,
                           gint               width,
                           gint               height,
                           const gfloat      *view,
                           const gfloat      *projection,
                           LrgLightSet       *lights,
                           GError           **error)
{
	gfloat vp[16];
	gfloat inv[16];
	gfloat inv_view[16];
	gfloat eye[3];
	gfloat params[4];
	gfloat hgt[2];
	gfloat sun_dir[3] = { 0.0f, -1.0f, 0.0f };
	gfloat sun_rgb[3] = { 0.0f, 0.0f, 0.0f };
	gfloat ssize[2];
	gfloat jitter;
	gint   steps;
	gint   unit;
	gint   linear = 0;
	gint   hw;
	gint   hh;
	guint  prev_fbo;
	guint  c;
	LrgShadowRenderer *shadows = NULL;

	g_return_val_if_fail (LRG_IS_VOLUMETRIC_FOG (self), FALSE);
	g_return_val_if_fail (view != NULL && projection != NULL, FALSE);
	g_return_val_if_fail (lights == NULL || LRG_IS_LIGHT_SET (lights), FALSE);

	if (!IsWindowReady ())
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_GPU, "No GL context");
		return FALSE;
	}
	if (width <= 0 || height <= 0 || depth_texture == 0)
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_FAILED,
		             "Volumetric fog needs a depth texture and a size");
		return FALSE;
	}
	if (!ensure_shaders (self, error))
		return FALSE;
	hw = MAX ((gint)((gfloat)width * self->scale + 0.5f), 1);
	hh = MAX ((gint)((gfloat)height * self->scale + 0.5f), 1);
	rlDrawRenderBatchActive ();
	prev_fbo = rlGetActiveFramebuffer ();
	if (!ensure_target (self, hw, hh, error))
		return FALSE;

	/* matrices: world from (uv, depth) and the eye */
	lrg_math3d_multiply (projection, view, vp);
	if (!lrg_math3d_invert (vp, inv) || !lrg_math3d_invert (view, inv_view))
	{
		g_set_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_FAILED,
		             "The view or projection matrix is singular");
		return FALSE;
	}
	eye[0] = inv_view[12];
	eye[1] = inv_view[13];
	eye[2] = inv_view[14];

	/* the sun of the light set, linearized */
	if (lights != NULL)
	{
		if (lrg_light_set_get_directional (lights, sun_dir, sun_rgb))
			for (c = 0; c < 3; c++)
				sun_rgb[c] = powf (MAX (sun_rgb[c], 0.0f), 2.2f);
		shadows = lrg_light_set_get_shadow_renderer (lights);
	}

	params[0] = self->density;
	params[1] = self->anisotropy;
	params[2] = self->max_distance;
	params[3] = self->intensity;
	hgt[0] = self->base_height;
	hgt[1] = self->falloff;
	steps = (gint)self->steps;
	self->frame++;
	jitter = (gfloat)(self->frame % 16) * 0.618034f;

	/* 1. march into the reduced target */
	rlEnableFramebuffer (self->fbo);
	rlViewport (0, 0, hw, hh);
	rlDisableDepthTest ();
	rlDisableBackfaceCulling ();
	rlDisableColorBlend ();
	BeginShaderMode (self->march);
	unit = FOG_DEPTH_UNIT;
	SetShaderValue (self->march, GetShaderLocation (self->march, "depthMap"), &unit, SHADER_UNIFORM_INT);
	SetShaderValueMatrix (self->march, GetShaderLocation (self->march, "invViewProj"), to_matrix (inv));
	SetShaderValue (self->march, GetShaderLocation (self->march, "eye"), eye, SHADER_UNIFORM_VEC3);
	SetShaderValue (self->march, GetShaderLocation (self->march, "fogParams"), params, SHADER_UNIFORM_VEC4);
	SetShaderValue (self->march, GetShaderLocation (self->march, "fogHeight"), hgt, SHADER_UNIFORM_VEC2);
	SetShaderValue (self->march, GetShaderLocation (self->march, "fogSteps"), &steps, SHADER_UNIFORM_INT);
	SetShaderValue (self->march, GetShaderLocation (self->march, "fogSunDir"), sun_dir, SHADER_UNIFORM_VEC3);
	SetShaderValue (self->march, GetShaderLocation (self->march, "fogSunColor"), sun_rgb, SHADER_UNIFORM_VEC3);
	SetShaderValue (self->march, GetShaderLocation (self->march, "fogAmbient"), self->ambient, SHADER_UNIFORM_VEC3);
	SetShaderValue (self->march, GetShaderLocation (self->march, "fogTint"), self->tint, SHADER_UNIFORM_VEC3);
	SetShaderValue (self->march, GetShaderLocation (self->march, "fogJitter"), &jitter, SHADER_UNIFORM_FLOAT);
	lrg_shadow_renderer_upload (shadows, self->march, &self->shadow_locs);
	rlActiveTextureSlot (FOG_DEPTH_UNIT);
	rlEnableTexture (depth_texture);
	rlActiveTextureSlot (0);
	draw_quad ();
	EndShaderMode ();

	/* 2. upsample and composite: colour x T + scattering */
	if (prev_fbo != 0)
		rlEnableFramebuffer (prev_fbo);
	else
		rlDisableFramebuffer ();
	rlViewport (0, 0, width, height);
	rlEnableColorBlend ();
	rlSetBlendFactors (RL_ONE, RL_SRC_ALPHA, RL_FUNC_ADD);
	rlSetBlendMode (RL_BLEND_CUSTOM);
	BeginShaderMode (self->composite);
	unit = FOG_DEPTH_UNIT;
	SetShaderValue (self->composite, GetShaderLocation (self->composite, "depthMap"), &unit, SHADER_UNIFORM_INT);
	unit = FOG_SCATTER_UNIT;
	SetShaderValue (self->composite, GetShaderLocation (self->composite, "scatterMap"), &unit, SHADER_UNIFORM_INT);
	SetShaderValueMatrix (self->composite, GetShaderLocation (self->composite, "invViewProj"), to_matrix (inv));
	SetShaderValue (self->composite, GetShaderLocation (self->composite, "eye"), eye, SHADER_UNIFORM_VEC3);
	ssize[0] = (gfloat)hw;
	ssize[1] = (gfloat)hh;
	SetShaderValue (self->composite, GetShaderLocation (self->composite, "scatterSize"), ssize, SHADER_UNIFORM_VEC2);
	SetShaderValue (self->composite, GetShaderLocation (self->composite, "outputLinear"), &linear, SHADER_UNIFORM_INT);
	rlActiveTextureSlot (FOG_DEPTH_UNIT);
	rlEnableTexture (depth_texture);
	rlActiveTextureSlot (FOG_SCATTER_UNIT);
	rlEnableTexture (self->target);
	rlActiveTextureSlot (0);
	draw_quad ();
	EndShaderMode ();
	rlSetBlendMode (RL_BLEND_ALPHA);
	rlActiveTextureSlot (FOG_DEPTH_UNIT);
	rlDisableTexture ();
	rlActiveTextureSlot (FOG_SCATTER_UNIT);
	rlDisableTexture ();
	rlActiveTextureSlot (0);
	rlEnableDepthTest ();
	rlEnableBackfaceCulling ();
	return TRUE;
}

gboolean
lrg_volumetric_fog_render_camera (LrgVolumetricFog  *self,
                                  guint              depth_texture,
                                  gint               width,
                                  gint               height,
                                  LrgCamera3D       *camera,
                                  LrgLightSet       *lights,
                                  GError           **error)
{
	g_autoptr(GrlVector3) up = NULL;
	GrlVector3 pos;
	GrlVector3 target;
	gfloat     eye[3];
	gfloat     at[3];
	gfloat     upv[3];
	gfloat     view[16];
	gfloat     proj[16];

	g_return_val_if_fail (LRG_IS_VOLUMETRIC_FOG (self), FALSE);
	g_return_val_if_fail (LRG_IS_CAMERA3D (camera), FALSE);

	lrg_camera3d_get_render_pose (camera, &pos, &target);
	up = lrg_camera3d_get_up (camera);
	eye[0] = pos.x;
	eye[1] = pos.y;
	eye[2] = pos.z;
	at[0] = target.x;
	at[1] = target.y;
	at[2] = target.z;
	if (lights != NULL)
	{
		gdouble ox = 0.0;
		gdouble oy = 0.0;
		gdouble oz = 0.0;

		lrg_light_set_get_origin (lights, &ox, &oy, &oz);
		eye[0] -= (gfloat)ox;
		eye[1] -= (gfloat)oy;
		eye[2] -= (gfloat)oz;
		at[0] -= (gfloat)ox;
		at[1] -= (gfloat)oy;
		at[2] -= (gfloat)oz;
	}
	upv[0] = up != NULL ? up->x : 0.0f;
	upv[1] = up != NULL ? up->y : 1.0f;
	upv[2] = up != NULL ? up->z : 0.0f;
	lrg_math3d_look_at (eye, at, upv, view);
	lrg_math3d_perspective (lrg_camera3d_get_fovy (camera) * (gfloat)(FOG_PI / 180.0),
	                        (gfloat)width / (gfloat)MAX (height, 1),
	                        (gfloat)rlGetCullDistanceNear (), (gfloat)rlGetCullDistanceFar (), proj);
	return lrg_volumetric_fog_render (self, depth_texture, width, height, view, proj, lights, error);
}

guint
lrg_volumetric_fog_get_scatter_texture_id (LrgVolumetricFog *self)
{
	g_return_val_if_fail (LRG_IS_VOLUMETRIC_FOG (self), 0);
	return self->target;
}

void
lrg_volumetric_fog_release_gpu (LrgVolumetricFog *self)
{
	g_return_if_fail (LRG_IS_VOLUMETRIC_FOG (self));
	release (self);
}
