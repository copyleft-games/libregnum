/* lrg-depth-prepass.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDepthPrepass: an offscreen framebuffer with a sampleable depth
 * texture and an RGBA8 colour texture that the prepass shader fills with
 * view-space normals.  The caller draws its opaque geometry between
 * begin() and end() (or through record()); begin() records the camera's
 * projection and view matrices so consumers can rebuild view-space
 * positions from depth (LrgSsao) without knowing the camera.
 *
 * GPU objects are made lazily in begin() (a window must exist) and
 * remade when the size changes; everything else works headless.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-depth-prepass.h"
#include "lrg-camera.h"
#include "../core/lrg-math3d.h"

/* raylib's PIXELFORMAT_DEPTH_COMPONENT_24BIT (not in rlgl's enum names
 * we use elsewhere); only stored in the Texture struct for bookkeeping */
#define PREPASS_DEPTH_FORMAT (19)

struct _LrgDepthPrepass
{
	GObject    parent_instance;

	/* requested size and options */
	gint       width;
	gint       height;
	gboolean   normals;
	gfloat     alpha_cutoff;

	/* GPU objects (0: not made) and the size they were made at */
	guint      fbo;
	guint      color_tex;
	guint      depth_tex;
	gint       gpu_width;
	gint       gpu_height;

	/* prepass shader, compiled on first use */
	GrlShader *shader;
	gboolean   shader_tried;
	gint       loc_cutoff;

	/* the camera between begin() and end() (borrowed) */
	LrgCamera *recording;
	gboolean   valid;

	/* matrices of the last recording, column-major */
	gfloat     projection[16];
	gfloat     view[16];
};

G_DEFINE_FINAL_TYPE (LrgDepthPrepass, lrg_depth_prepass, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_NORMALS,
	PROP_ALPHA_CUTOFF,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* GLSL 330 with raylib's default attribute and matrix names, so DrawMesh()
 * feeds mvp, matModel, matView and matNormal. */
static const gchar prepass_vs[] =
	"#version 330\n"
	"in vec3 vertexPosition;\n"
	"in vec2 vertexTexCoord;\n"
	"in vec3 vertexNormal;\n"
	"uniform mat4 mvp;\n"
	"uniform mat4 matView;\n"
	"uniform mat4 matNormal;\n"
	"out vec2 fragTexCoord;\n"
	"out vec3 fragViewNormal;\n"
	"void main() {\n"
	"    fragTexCoord = vertexTexCoord;\n"
	"    vec3 wn = vec3(matNormal * vec4(vertexNormal, 0.0));\n"
	"    fragViewNormal = mat3(matView) * wn;\n"
	"    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
	"}\n";

/* Writes the view normal (towards the viewer on back faces) and applies
 * the alpha cutoff to the albedo map times the material colour. */
static const gchar prepass_fs[] =
	"#version 330\n"
	"in vec2 fragTexCoord;\n"
	"in vec3 fragViewNormal;\n"
	"uniform sampler2D texture0;\n"
	"uniform vec4 colDiffuse;\n"
	"uniform float alphaCutoff;\n"
	"out vec4 finalColor;\n"
	"void main() {\n"
	"    if (alphaCutoff > 0.0 && texture(texture0, fragTexCoord).a * colDiffuse.a < alphaCutoff) discard;\n"
	"    float len = length(fragViewNormal);\n"
	"    vec3 n = len > 0.0 ? fragViewNormal / len : vec3(0.0, 0.0, 1.0);\n"
	"    if (!gl_FrontFacing) n = -n;\n"
	"    finalColor = vec4(n * 0.5 + 0.5, 1.0);\n"
	"}\n";

/* ==========================================================================
 * GPU objects
 * ========================================================================== */

/* free_targets:
 * Drops the framebuffer and its textures (only with a live context; the
 * driver frees them with the context otherwise). */
static void
free_targets (LrgDepthPrepass *self)
{
	if (self->fbo != 0 && IsWindowReady ())
	{
		rlUnloadFramebuffer (self->fbo);
		if (self->color_tex != 0)
			rlUnloadTexture (self->color_tex);
		if (self->depth_tex != 0)
			rlUnloadTexture (self->depth_tex);
	}
	self->fbo = 0;
	self->color_tex = 0;
	self->depth_tex = 0;
	self->gpu_width = 0;
	self->gpu_height = 0;
}

/* ensure_targets:
 * Makes the framebuffer at the requested size (remaking it after a
 * resize).  The depth attachment is a texture, not a renderbuffer, so it
 * can be sampled; both textures are point sampled and clamped. */
static gboolean
ensure_targets (LrgDepthPrepass *self)
{
	if (self->fbo != 0 && self->gpu_width == self->width && self->gpu_height == self->height)
		return TRUE;
	free_targets (self);

	self->fbo = rlLoadFramebuffer ();
	if (self->fbo == 0)
		return FALSE;
	self->color_tex = rlLoadTexture (NULL, self->width, self->height,
	                                 RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
	self->depth_tex = rlLoadTextureDepth (self->width, self->height, false);
	if (self->color_tex == 0 || self->depth_tex == 0)
	{
		free_targets (self);
		return FALSE;
	}
	rlTextureParameters (self->color_tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
	rlTextureParameters (self->color_tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
	rlTextureParameters (self->color_tex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
	rlTextureParameters (self->color_tex, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
	rlTextureParameters (self->depth_tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
	rlTextureParameters (self->depth_tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
	rlTextureParameters (self->depth_tex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
	rlTextureParameters (self->depth_tex, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
	rlFramebufferAttach (self->fbo, self->color_tex, RL_ATTACHMENT_COLOR_CHANNEL0,
	                     RL_ATTACHMENT_TEXTURE2D, 0);
	rlFramebufferAttach (self->fbo, self->depth_tex, RL_ATTACHMENT_DEPTH,
	                     RL_ATTACHMENT_TEXTURE2D, 0);
	if (!rlFramebufferComplete (self->fbo))
	{
		g_warning ("LrgDepthPrepass: a %d x %d depth target is not renderable",
		           self->width, self->height);
		rlDisableFramebuffer ();
		free_targets (self);
		return FALSE;
	}
	rlDisableFramebuffer ();
	self->gpu_width = self->width;
	self->gpu_height = self->height;
	return TRUE;
}

/* render_texture:
 * The framebuffer as raylib's RenderTexture2D, for BeginTextureMode(). */
static RenderTexture2D
render_texture (LrgDepthPrepass *self)
{
	RenderTexture2D rt;

	memset (&rt, 0, sizeof rt);
	rt.id = self->fbo;
	rt.texture.id = self->color_tex;
	rt.texture.width = self->gpu_width;
	rt.texture.height = self->gpu_height;
	rt.texture.mipmaps = 1;
	rt.texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	rt.depth.id = self->depth_tex;
	rt.depth.width = self->gpu_width;
	rt.depth.height = self->gpu_height;
	rt.depth.mipmaps = 1;
	rt.depth.format = PREPASS_DEPTH_FORMAT;
	return rt;
}

/* matrix_to_floats:
 * raylib's Matrix fields are named by their column-major storage index,
 * so m0 .. m15 copy to out[0 .. 15] by name (never memcpy, see
 * AGENTS.md). */
static void
matrix_to_floats (Matrix  m,
                  gfloat *out)
{
	out[0] = m.m0;   out[1] = m.m1;   out[2] = m.m2;   out[3] = m.m3;
	out[4] = m.m4;   out[5] = m.m5;   out[6] = m.m6;   out[7] = m.m7;
	out[8] = m.m8;   out[9] = m.m9;   out[10] = m.m10; out[11] = m.m11;
	out[12] = m.m12; out[13] = m.m13; out[14] = m.m14; out[15] = m.m15;
}

/* floats_to_matrix: the inverse of matrix_to_floats(). */
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

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_depth_prepass_finalize (GObject *object)
{
	LrgDepthPrepass *self = LRG_DEPTH_PREPASS (object);

	free_targets (self);
	g_clear_object (&self->shader);

	G_OBJECT_CLASS (lrg_depth_prepass_parent_class)->finalize (object);
}

static void
lrg_depth_prepass_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgDepthPrepass *self = LRG_DEPTH_PREPASS (object);

	switch (prop_id)
	{
	case PROP_WIDTH:
		g_value_set_int (value, self->width);
		break;
	case PROP_HEIGHT:
		g_value_set_int (value, self->height);
		break;
	case PROP_NORMALS:
		g_value_set_boolean (value, self->normals);
		break;
	case PROP_ALPHA_CUTOFF:
		g_value_set_float (value, self->alpha_cutoff);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_depth_prepass_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgDepthPrepass *self = LRG_DEPTH_PREPASS (object);

	switch (prop_id)
	{
	case PROP_WIDTH:
		lrg_depth_prepass_resize (self, g_value_get_int (value), self->height);
		break;
	case PROP_HEIGHT:
		lrg_depth_prepass_resize (self, self->width, g_value_get_int (value));
		break;
	case PROP_NORMALS:
		lrg_depth_prepass_set_normals (self, g_value_get_boolean (value));
		break;
	case PROP_ALPHA_CUTOFF:
		lrg_depth_prepass_set_alpha_cutoff (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_depth_prepass_class_init (LrgDepthPrepassClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_depth_prepass_finalize;
	object_class->get_property = lrg_depth_prepass_get_property;
	object_class->set_property = lrg_depth_prepass_set_property;

	/**
	 * LrgDepthPrepass:width:
	 *
	 * Target width in pixels.
	 */
	properties[PROP_WIDTH] =
		g_param_spec_int ("width", NULL, "Target width in pixels", 1, 16384, 1,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDepthPrepass:height:
	 *
	 * Target height in pixels.
	 */
	properties[PROP_HEIGHT] =
		g_param_spec_int ("height", NULL, "Target height in pixels", 1, 16384, 1,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDepthPrepass:normals:
	 *
	 * Whether the colour texture holds view-space normals that consumers
	 * may read (see lrg_depth_prepass_set_normals()).
	 */
	properties[PROP_NORMALS] =
		g_param_spec_boolean ("normals", NULL, "Normal texture is valid", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDepthPrepass:alpha-cutoff:
	 *
	 * The prepass shader's alpha test (0: off).
	 */
	properties[PROP_ALPHA_CUTOFF] =
		g_param_spec_float ("alpha-cutoff", NULL, "Alpha test threshold", 0.0f, 1.0f, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_depth_prepass_init (LrgDepthPrepass *self)
{
	self->width = 1;
	self->height = 1;
	self->normals = TRUE;
	self->loc_cutoff = -1;
	lrg_math3d_identity (self->projection);
	lrg_math3d_identity (self->view);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgDepthPrepass *
lrg_depth_prepass_new (gint width,
                       gint height)
{
	g_return_val_if_fail (width >= 1 && height >= 1, NULL);

	return (LrgDepthPrepass *) g_object_new (LRG_TYPE_DEPTH_PREPASS,
	                                         "width", width, "height", height, NULL);
}

void
lrg_depth_prepass_resize (LrgDepthPrepass *self,
                          gint             width,
                          gint             height)
{
	gboolean w_changed;
	gboolean h_changed;

	g_return_if_fail (LRG_IS_DEPTH_PREPASS (self));
	g_return_if_fail (width >= 1 && height >= 1);

	w_changed = self->width != width;
	h_changed = self->height != height;
	if (!w_changed && !h_changed)
		return;
	self->width = width;
	self->height = height;
	self->valid = FALSE;
	if (w_changed)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
	if (h_changed)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
}

gint
lrg_depth_prepass_get_width (LrgDepthPrepass *self)
{
	g_return_val_if_fail (LRG_IS_DEPTH_PREPASS (self), 0);

	return self->width;
}

gint
lrg_depth_prepass_get_height (LrgDepthPrepass *self)
{
	g_return_val_if_fail (LRG_IS_DEPTH_PREPASS (self), 0);

	return self->height;
}

void
lrg_depth_prepass_set_normals (LrgDepthPrepass *self,
                               gboolean         normals)
{
	g_return_if_fail (LRG_IS_DEPTH_PREPASS (self));

	normals = !!normals;
	if (self->normals == normals)
		return;
	self->normals = normals;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NORMALS]);
}

gboolean
lrg_depth_prepass_get_normals (LrgDepthPrepass *self)
{
	g_return_val_if_fail (LRG_IS_DEPTH_PREPASS (self), FALSE);

	return self->normals;
}

void
lrg_depth_prepass_set_alpha_cutoff (LrgDepthPrepass *self,
                                    gfloat           cutoff)
{
	g_return_if_fail (LRG_IS_DEPTH_PREPASS (self));

	cutoff = CLAMP (cutoff, 0.0f, 1.0f);
	if (self->alpha_cutoff == cutoff)
		return;
	self->alpha_cutoff = cutoff;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ALPHA_CUTOFF]);
}

gfloat
lrg_depth_prepass_get_alpha_cutoff (LrgDepthPrepass *self)
{
	g_return_val_if_fail (LRG_IS_DEPTH_PREPASS (self), 0.0f);

	return self->alpha_cutoff;
}

GrlShader *
lrg_depth_prepass_get_shader (LrgDepthPrepass *self)
{
	g_autoptr(GError) error = NULL;
	Shader *handle;

	g_return_val_if_fail (LRG_IS_DEPTH_PREPASS (self), NULL);

	if (self->shader != NULL || self->shader_tried)
		return self->shader;
	if (!IsWindowReady ())
		return NULL;
	self->shader_tried = TRUE;

	/* On a compile failure raylib hands back its default shader. */
	self->shader = grl_shader_new_from_memory (prepass_vs, prepass_fs, &error);
	if (self->shader == NULL)
	{
		g_warning ("LrgDepthPrepass: shader: %s", error != NULL ? error->message : "failed");
		return NULL;
	}
	handle = (Shader *) grl_shader_get_handle (self->shader);
	if (handle->id == 0 || handle->id == rlGetShaderIdDefault ())
	{
		g_clear_object (&self->shader);
		return NULL;
	}
	self->loc_cutoff = GetShaderLocation (*handle, "alphaCutoff");
	return self->shader;
}

gboolean
lrg_depth_prepass_begin (LrgDepthPrepass *self,
                         LrgCamera       *camera)
{
	GrlShader *shader;

	g_return_val_if_fail (LRG_IS_DEPTH_PREPASS (self), FALSE);
	g_return_val_if_fail (LRG_IS_CAMERA (camera), FALSE);
	g_return_val_if_fail (self->recording == NULL, FALSE);

	if (!IsWindowReady () || !ensure_targets (self))
		return FALSE;

	/* the alpha test of this recording */
	shader = lrg_depth_prepass_get_shader (self);
	if (shader != NULL && self->loc_cutoff >= 0)
	{
		Shader *handle = (Shader *) grl_shader_get_handle (shader);

		SetShaderValue (*handle, self->loc_cutoff, &self->alpha_cutoff, SHADER_UNIFORM_FLOAT);
	}

	/* clear: far depth, normal facing the viewer */
	BeginTextureMode (render_texture (self));
	ClearBackground ((Color){ 128, 128, 255, 255 });
	lrg_camera_begin (camera);

	/* the camera's matrices, for consumers rebuilding positions */
	matrix_to_floats (rlGetMatrixProjection (), self->projection);
	matrix_to_floats (rlGetMatrixModelview (), self->view);
	self->recording = camera;
	return TRUE;
}

void
lrg_depth_prepass_end (LrgDepthPrepass *self)
{
	g_return_if_fail (LRG_IS_DEPTH_PREPASS (self));
	g_return_if_fail (self->recording != NULL);

	lrg_camera_end (self->recording);
	EndTextureMode ();
	self->recording = NULL;
	self->valid = TRUE;
}

gboolean
lrg_depth_prepass_record (LrgDepthPrepass         *self,
                          LrgCamera               *camera,
                          LrgDepthPrepassDrawFunc  func,
                          gpointer                 user_data)
{
	g_return_val_if_fail (LRG_IS_DEPTH_PREPASS (self), FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (!lrg_depth_prepass_begin (self, camera))
		return FALSE;
	func (self, self->shader, user_data);
	lrg_depth_prepass_end (self);
	return TRUE;
}

void
lrg_depth_prepass_draw_model (LrgDepthPrepass *self,
                              GrlModel        *model,
                              const gfloat    *transform)
{
	Model  *handle;
	Shader *shader;
	gfloat  own[16];
	gfloat  world[16];
	Matrix  matrix;
	gint    i;

	g_return_if_fail (LRG_IS_DEPTH_PREPASS (self));
	g_return_if_fail (GRL_IS_MODEL (model));

	if (lrg_depth_prepass_get_shader (self) == NULL)
		return;
	handle = (Model *) grl_model_get_handle (model);
	if (handle == NULL)
		return;
	shader = (Shader *) grl_shader_get_handle (self->shader);

	/* raylib applies the model's transform first, then the caller's:
	 * column-major world = transform * model.transform */
	matrix_to_floats (handle->transform, own);
	if (transform != NULL)
		lrg_math3d_multiply (transform, own, world);
	else
		memcpy (world, own, sizeof world);
	matrix = floats_to_matrix (world);

	/* each mesh with a copy of its material whose shader is ours, so the
	 * albedo map and colour still drive the alpha test */
	for (i = 0; i < handle->meshCount; i++)
	{
		Material mat;
		gint     mi = (handle->meshMaterial != NULL) ? handle->meshMaterial[i] : 0;

		if (handle->materials == NULL || mi < 0 || mi >= handle->materialCount)
			continue;
		mat = handle->materials[mi];
		mat.shader = *shader;
		DrawMesh (handle->meshes[i], mat, matrix);
	}
}

gboolean
lrg_depth_prepass_is_valid (LrgDepthPrepass *self)
{
	g_return_val_if_fail (LRG_IS_DEPTH_PREPASS (self), FALSE);

	return self->valid && self->fbo != 0;
}

guint
lrg_depth_prepass_get_depth_texture_id (LrgDepthPrepass *self)
{
	g_return_val_if_fail (LRG_IS_DEPTH_PREPASS (self), 0);

	return self->depth_tex;
}

guint
lrg_depth_prepass_get_normal_texture_id (LrgDepthPrepass *self)
{
	g_return_val_if_fail (LRG_IS_DEPTH_PREPASS (self), 0);

	return self->color_tex;
}

void
lrg_depth_prepass_get_projection (LrgDepthPrepass *self,
                                  gfloat          *out_matrix)
{
	g_return_if_fail (LRG_IS_DEPTH_PREPASS (self));
	g_return_if_fail (out_matrix != NULL);

	memcpy (out_matrix, self->projection, sizeof self->projection);
}

void
lrg_depth_prepass_get_view (LrgDepthPrepass *self,
                            gfloat          *out_matrix)
{
	g_return_if_fail (LRG_IS_DEPTH_PREPASS (self));
	g_return_if_fail (out_matrix != NULL);

	memcpy (out_matrix, self->view, sizeof self->view);
}

gfloat
lrg_depth_prepass_linearize_depth (LrgDepthPrepass *self,
                                   gfloat           depth)
{
	const gfloat *p;
	gdouble       ndc;
	gdouble       w;

	g_return_val_if_fail (LRG_IS_DEPTH_PREPASS (self), 0.0f);

	/*
	 * Window depth d -> NDC z = 2d - 1.  For a projection whose third and
	 * fourth rows are (0 0 A B) and (0 0 C D), a view point at z gives
	 * clip z = A z + B and w = C z + D, so NDC = (A z + B) / (C z + D)
	 * and z = (B - NDC D) / (NDC C - A).  Column-major: A = p[10],
	 * B = p[14], C = p[11], D = p[15].  This covers perspective
	 * (C = -1, D = 0) and orthographic (C = 0, D = 1) projections.
	 */
	p = self->projection;
	ndc = 2.0 * (gdouble) depth - 1.0;
	w = ndc * (gdouble) p[11] - (gdouble) p[10];
	if (fabs (w) < 1e-12)
		return 0.0f;
	return (gfloat) -(((gdouble) p[14] - ndc * (gdouble) p[15]) / w);
}

void
lrg_depth_prepass_release (LrgDepthPrepass *self)
{
	g_return_if_fail (LRG_IS_DEPTH_PREPASS (self));

	free_targets (self);
	if (IsWindowReady ())
		g_clear_object (&self->shader);
	self->shader_tried = FALSE;
	self->valid = FALSE;
}
