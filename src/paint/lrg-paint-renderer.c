/* lrg-paint-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Draws paint lists with one instanced SDF shader.
 *
 * Every instance is expanded by the vertex shader into a quad that
 * covers the shape plus an anti-aliasing margin (and the blur for
 * shadows), clamped to its clip rectangle. The fragment shader
 * evaluates the shape's signed distance and turns it into coverage, so
 * edges are anti-aliased analytically at any scale with no MSAA and no
 * tessellation. Output is premultiplied alpha.
 *
 * The rounded-box distance is the standard construction (see Inigo
 * Quilez's 2D SDF notes); the instance-per-shape design follows the
 * Raven UI library (Apache-2.0), reimplemented here for rlgl.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PAINT

#include <math.h>
#include <stddef.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-paint-renderer.h"
#include "../lrg-log.h"

/* Initial and minimum instance capacity of the GPU buffer */
#define MIN_CAPACITY (1024)

/* Attribute locations, matching the layout() qualifiers below */
#define ATTR_CORNER (0)
#define ATTR_RECT   (1)
#define ATTR_RADII  (2)
#define ATTR_CLIP   (3)
#define ATTR_PARAMS (4)
#define ATTR_EXTRA  (5)
#define ATTR_COLOR  (6)
#define ATTR_COLOR2 (7)
#define ATTR_BORDER (8)

static const gchar *vertex_source =
	"#version 330\n"
	"layout(location = 0) in vec2 a_corner;\n"
	"layout(location = 1) in vec4 i_rect;\n"
	"layout(location = 2) in vec4 i_radii;\n"
	"layout(location = 3) in vec4 i_clip;\n"
	"layout(location = 4) in vec4 i_params;\n"
	"layout(location = 5) in vec4 i_extra;\n"
	"layout(location = 6) in vec4 i_color;\n"
	"layout(location = 7) in vec4 i_color2;\n"
	"layout(location = 8) in vec4 i_border;\n"
	"uniform mat4 u_mvp;\n"
	"out vec2 v_pos;\n"
	"flat out vec4 v_rect;\n"
	"flat out vec4 v_radii;\n"
	"flat out vec4 v_clip;\n"
	"flat out vec4 v_params;\n"
	"flat out vec4 v_extra;\n"
	"flat out vec4 v_color;\n"
	"flat out vec4 v_color2;\n"
	"flat out vec4 v_border;\n"
	"void main ()\n"
	"{\n"
	"    int  prim = int (i_params.z + 0.5);\n"
	"    vec2 lo;\n"
	"    vec2 hi;\n"
	"    if (prim == 2) {\n"
	"        float pad = i_params.y * 0.5 + 1.0;\n"
	"        lo = min (i_rect.xy, i_rect.zw) - pad;\n"
	"        hi = max (i_rect.xy, i_rect.zw) + pad;\n"
	"    } else if (prim == 4) {\n"
	"        lo = min (min (i_rect.xy, i_rect.zw), i_extra.xy) - 1.0;\n"
	"        hi = max (max (i_rect.xy, i_rect.zw), i_extra.xy) + 1.0;\n"
	"    } else {\n"
	"        float pad = (prim == 1) ? i_params.y + 1.0 : 1.0;\n"
	"        lo = i_rect.xy - pad;\n"
	"        hi = i_rect.zw + pad;\n"
	"    }\n"
	"    lo = max (lo, i_clip.xy);\n"
	"    hi = max (min (hi, i_clip.zw), lo);\n"
	"    vec2 p = mix (lo, hi, a_corner);\n"
	"    v_pos = p;\n"
	"    v_rect = i_rect;\n"
	"    v_radii = i_radii;\n"
	"    v_clip = i_clip;\n"
	"    v_params = i_params;\n"
	"    v_extra = i_extra;\n"
	"    v_color = i_color;\n"
	"    v_color2 = i_color2;\n"
	"    v_border = i_border;\n"
	"    gl_Position = u_mvp * vec4 (p, 0.0, 1.0);\n"
	"}\n";

static const gchar *fragment_source =
	"#version 330\n"
	"in vec2 v_pos;\n"
	"flat in vec4 v_rect;\n"
	"flat in vec4 v_radii;\n"
	"flat in vec4 v_clip;\n"
	"flat in vec4 v_params;\n"
	"flat in vec4 v_extra;\n"
	"flat in vec4 v_color;\n"
	"flat in vec4 v_color2;\n"
	"flat in vec4 v_border;\n"
	"uniform sampler2D u_tex;\n"
	"out vec4 o_color;\n"
	"\n"
	/* signed distance to a rounded box centred at the origin; radii are
	 * (top-left, top-right, bottom-right, bottom-left), y points down */
	"float sd_round_box (vec2 p, vec2 b, vec4 r)\n"
	"{\n"
	"    float rad = (p.x < 0.0) ? ((p.y < 0.0) ? r.x : r.w)\n"
	"                            : ((p.y < 0.0) ? r.y : r.z);\n"
	"    vec2 q = abs (p) - b + rad;\n"
	"    return min (max (q.x, q.y), 0.0) + length (max (q, 0.0)) - rad;\n"
	"}\n"
	"\n"
	"float sd_segment (vec2 p, vec2 a, vec2 b)\n"
	"{\n"
	"    vec2  pa = p - a;\n"
	"    vec2  ba = b - a;\n"
	"    float h = clamp (dot (pa, ba) / max (dot (ba, ba), 1e-6), 0.0, 1.0);\n"
	"    return length (pa - ba * h);\n"
	"}\n"
	"\n"
	/* exact signed distance to a triangle, negative inside, any winding */
	"float sd_triangle (vec2 p, vec2 p0, vec2 p1, vec2 p2)\n"
	"{\n"
	"    vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;\n"
	"    vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;\n"
	"    vec2 q0 = v0 - e0 * clamp (dot (v0, e0) / dot (e0, e0), 0.0, 1.0);\n"
	"    vec2 q1 = v1 - e1 * clamp (dot (v1, e1) / dot (e1, e1), 0.0, 1.0);\n"
	"    vec2 q2 = v2 - e2 * clamp (dot (v2, e2) / dot (e2, e2), 0.0, 1.0);\n"
	"    float s = sign (e0.x * e2.y - e0.y * e2.x);\n"
	"    vec2 d = min (min (vec2 (dot (q0, q0), s * (v0.x * e0.y - v0.y * e0.x)),\n"
	"                       vec2 (dot (q1, q1), s * (v1.x * e1.y - v1.y * e1.x))),\n"
	"                  vec2 (dot (q2, q2), s * (v2.x * e2.y - v2.y * e2.x)));\n"
	"    return -sqrt (d.x) * sign (d.y);\n"
	"}\n"
	"\n"
	"vec4 premul (vec4 c) { return vec4 (c.rgb * c.a, c.a); }\n"
	"\n"
	"void main ()\n"
	"{\n"
	"    if (v_pos.x < v_clip.x || v_pos.y < v_clip.y ||\n"
	"        v_pos.x > v_clip.z || v_pos.y > v_clip.w)\n"
	"        discard;\n"
	"\n"
	"    int  prim = int (v_params.z + 0.5);\n"
	"    int  gk = int (v_params.w + 0.5);\n"
	"    vec2 c = (v_rect.xy + v_rect.zw) * 0.5;\n"
	"    vec2 hb = abs (v_rect.zw - v_rect.xy) * 0.5;\n"
	"    vec4 fill = v_color;\n"
	"\n"
	/* gradients: linear spans the box along the direction, radial runs
	 * from a centre given as a fraction of the box out to a radius */
	"    if (gk == 1) {\n"
	"        vec2  dir = v_extra.xy;\n"
	"        float ext = abs (dir.x) * hb.x + abs (dir.y) * hb.y;\n"
	"        float t = clamp (dot (v_pos - c, dir) / (2.0 * max (ext, 1e-4)) + 0.5, 0.0, 1.0);\n"
	"        fill = mix (v_color, v_color2, t);\n"
	"    } else if (gk == 2) {\n"
	"        vec2  ctr = mix (v_rect.xy, v_rect.zw, v_extra.xy);\n"
	"        float t = clamp (length (v_pos - ctr) / max (v_extra.z, 1e-4), 0.0, 1.0);\n"
	"        fill = mix (v_color, v_color2, t);\n"
	"    }\n"
	"\n"
	"    vec4 outc;\n"
	"    if (prim == 2) {\n"
	"        float d = sd_segment (v_pos, v_rect.xy, v_rect.zw) - v_params.y * 0.5;\n"
	"        outc = premul (fill) * clamp (0.5 - d, 0.0, 1.0);\n"
	"    } else if (prim == 4) {\n"
	"        float d = sd_triangle (v_pos, v_rect.xy, v_rect.zw, v_extra.xy);\n"
	"        outc = premul (v_color) * clamp (0.5 - d, 0.0, 1.0);\n"
	"    } else {\n"
	"        float d = sd_round_box (v_pos - c, hb, v_radii);\n"
	"        if (prim == 1) {\n"
	"            float blur = max (v_params.y, 0.5);\n"
	"            outc = premul (fill) * (1.0 - smoothstep (-blur, blur, d));\n"
	"        } else {\n"
	"            vec4 body = premul (fill);\n"
	"            if (prim == 3) {\n"
	"                vec2 f = (v_pos - v_rect.xy) / max (v_rect.zw - v_rect.xy, vec2 (1e-4));\n"
	/* textures (images, font atlases) are straight alpha */
	"                vec4 t = texture (u_tex, mix (v_extra.xy, v_extra.zw, f));\n"
	"                body *= vec4 (t.rgb * t.a, t.a);\n"
	"            }\n"
	"            float bw = v_params.x;\n"
	"            if (bw > 0.0)\n"
	"                body = mix (premul (v_border), body, clamp (0.5 - (d + bw), 0.0, 1.0));\n"
	"            outc = body * clamp (0.5 - d, 0.0, 1.0);\n"
	"        }\n"
	"    }\n"
	"    if (outc.a <= 0.0)\n"
	"        discard;\n"
	"    o_color = outc;\n"
	"}\n";

struct _LrgPaintRenderer
{
	GObject  parent_instance;

	gboolean gl_ready;
	gboolean gl_failed;
	guint    shader;
	gint     loc_mvp;
	gint     loc_tex;
	guint    vao;
	guint    quad_vbo;
	guint    instance_vbo;
	guint    capacity;
	guint    draw_calls;
};

G_DEFINE_FINAL_TYPE (LrgPaintRenderer, lrg_paint_renderer, G_TYPE_OBJECT)

static LrgPaintRenderer *default_renderer = NULL;

/* ==========================================================================
 * GL resources
 * ========================================================================== */

/*
 * point_instance_attributes:
 *
 * Points the per-instance attributes at instance @first of the bound
 * instance buffer. glDrawArraysInstanced has no base-instance argument
 * on GL 3.3, so each batch re-points the attribute offsets instead.
 */
static void
point_instance_attributes (guint first)
{
	gint stride = (gint) sizeof (LrgPaintInstance);
	gint base = (gint) first * stride;

	rlSetVertexAttribute (ATTR_RECT, 4, RL_FLOAT, false, stride,
	                      base + (gint) offsetof (LrgPaintInstance, rect));
	rlSetVertexAttribute (ATTR_RADII, 4, RL_FLOAT, false, stride,
	                      base + (gint) offsetof (LrgPaintInstance, radii));
	rlSetVertexAttribute (ATTR_CLIP, 4, RL_FLOAT, false, stride,
	                      base + (gint) offsetof (LrgPaintInstance, clip));
	rlSetVertexAttribute (ATTR_PARAMS, 4, RL_FLOAT, false, stride,
	                      base + (gint) offsetof (LrgPaintInstance, params));
	rlSetVertexAttribute (ATTR_EXTRA, 4, RL_FLOAT, false, stride,
	                      base + (gint) offsetof (LrgPaintInstance, extra));
	rlSetVertexAttribute (ATTR_COLOR, 4, RL_UNSIGNED_BYTE, true, stride,
	                      base + (gint) offsetof (LrgPaintInstance, color));
	rlSetVertexAttribute (ATTR_COLOR2, 4, RL_UNSIGNED_BYTE, true, stride,
	                      base + (gint) offsetof (LrgPaintInstance, color2));
	rlSetVertexAttribute (ATTR_BORDER, 4, RL_UNSIGNED_BYTE, true, stride,
	                      base + (gint) offsetof (LrgPaintInstance, border));
}

/* ensure_capacity: grow the instance buffer to hold @needed instances */
static void
ensure_capacity (LrgPaintRenderer *self,
                 guint             needed)
{
	guint cap;

	if (needed <= self->capacity && self->instance_vbo != 0)
		return;

	cap = MAX (self->capacity, (guint) MIN_CAPACITY);
	while (cap < needed)
		cap *= 2;

	if (self->instance_vbo != 0)
		rlUnloadVertexBuffer (self->instance_vbo);

	self->instance_vbo = rlLoadVertexBuffer (NULL,
	                                         (gint) (cap * sizeof (LrgPaintInstance)),
	                                         true);
	self->capacity = cap;
}

/*
 * ensure_gl:
 *
 * Compiles the shader and builds the vertex array on first use. On
 * failure the renderer stays disabled for the rest of the process
 * instead of retrying every frame.
 */
static gboolean
ensure_gl (LrgPaintRenderer *self)
{
	/* Two triangles in raylib's quad winding: TL, BL, BR / TL, BR, TR */
	static const gfloat corners[] = {
		0.0f, 0.0f,  0.0f, 1.0f,  1.0f, 1.0f,
		0.0f, 0.0f,  1.0f, 1.0f,  1.0f, 0.0f
	};
	guint attr;

	if (self->gl_ready)
		return TRUE;
	if (self->gl_failed)
		return FALSE;

	self->shader = rlLoadShaderProgram (vertex_source, fragment_source);
	if (self->shader == 0 || self->shader == rlGetShaderIdDefault ())
	{
		lrg_warning (LRG_LOG_DOMAIN_PAINT,
		             "SDF paint shader failed to compile; UI shapes will not be drawn");
		self->gl_failed = TRUE;
		return FALSE;
	}
	self->loc_mvp = rlGetLocationUniform (self->shader, "u_mvp");
	self->loc_tex = rlGetLocationUniform (self->shader, "u_tex");

	self->vao = rlLoadVertexArray ();
	rlEnableVertexArray (self->vao);

	/* Static unit quad, one corner per vertex */
	self->quad_vbo = rlLoadVertexBuffer (corners, (gint) sizeof (corners), false);
	rlSetVertexAttribute (ATTR_CORNER, 2, RL_FLOAT, false, 0, 0);
	rlEnableVertexAttribute (ATTR_CORNER);

	/* Per-instance stream */
	ensure_capacity (self, MIN_CAPACITY);
	rlEnableVertexBuffer (self->instance_vbo);
	point_instance_attributes (0);
	for (attr = ATTR_RECT; attr <= ATTR_BORDER; attr++)
	{
		rlEnableVertexAttribute (attr);
		rlSetVertexAttributeDivisor (attr, 1);
	}

	rlDisableVertexArray ();
	rlDisableVertexBuffer ();

	self->gl_ready = TRUE;
	lrg_debug (LRG_LOG_DOMAIN_PAINT, "SDF paint renderer ready (shader %u)", self->shader);
	return TRUE;
}

static void
release_gl (LrgPaintRenderer *self)
{
	if (!self->gl_ready || !IsWindowReady ())
		return;

	rlUnloadVertexBuffer (self->instance_vbo);
	rlUnloadVertexBuffer (self->quad_vbo);
	rlUnloadVertexArray (self->vao);
	rlUnloadShaderProgram (self->shader);
	self->gl_ready = FALSE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_paint_renderer_finalize (GObject *object)
{
	LrgPaintRenderer *self = LRG_PAINT_RENDERER (object);

	release_gl (self);

	if (default_renderer == self)
		default_renderer = NULL;

	G_OBJECT_CLASS (lrg_paint_renderer_parent_class)->finalize (object);
}

static void
lrg_paint_renderer_class_init (LrgPaintRendererClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_paint_renderer_finalize;
}

static void
lrg_paint_renderer_init (LrgPaintRenderer *self)
{
	self->loc_mvp = -1;
	self->loc_tex = -1;
}

LrgPaintRenderer *
lrg_paint_renderer_new (void)
{
	return g_object_new (LRG_TYPE_PAINT_RENDERER, NULL);
}

LrgPaintRenderer *
lrg_paint_renderer_get_default (void)
{
	if (default_renderer == NULL)
		default_renderer = lrg_paint_renderer_new ();

	return default_renderer;
}

gboolean
lrg_paint_renderer_is_available (void)
{
	return IsWindowReady ();
}

guint
lrg_paint_renderer_get_draw_calls (LrgPaintRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PAINT_RENDERER (self), 0);

	return self->draw_calls;
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/*
 * mat_mul:
 *
 * The product raymath's MatrixMultiply() computes (raymath itself
 * needs C99 and this library builds as gnu89).
 */
static Matrix
mat_mul (const Matrix *l,
         const Matrix *r)
{
	Matrix out;

	out.m0 = l->m0 * r->m0 + l->m1 * r->m4 + l->m2 * r->m8 + l->m3 * r->m12;
	out.m1 = l->m0 * r->m1 + l->m1 * r->m5 + l->m2 * r->m9 + l->m3 * r->m13;
	out.m2 = l->m0 * r->m2 + l->m1 * r->m6 + l->m2 * r->m10 + l->m3 * r->m14;
	out.m3 = l->m0 * r->m3 + l->m1 * r->m7 + l->m2 * r->m11 + l->m3 * r->m15;
	out.m4 = l->m4 * r->m0 + l->m5 * r->m4 + l->m6 * r->m8 + l->m7 * r->m12;
	out.m5 = l->m4 * r->m1 + l->m5 * r->m5 + l->m6 * r->m9 + l->m7 * r->m13;
	out.m6 = l->m4 * r->m2 + l->m5 * r->m6 + l->m6 * r->m10 + l->m7 * r->m14;
	out.m7 = l->m4 * r->m3 + l->m5 * r->m7 + l->m6 * r->m11 + l->m7 * r->m15;
	out.m8 = l->m8 * r->m0 + l->m9 * r->m4 + l->m10 * r->m8 + l->m11 * r->m12;
	out.m9 = l->m8 * r->m1 + l->m9 * r->m5 + l->m10 * r->m9 + l->m11 * r->m13;
	out.m10 = l->m8 * r->m2 + l->m9 * r->m6 + l->m10 * r->m10 + l->m11 * r->m14;
	out.m11 = l->m8 * r->m3 + l->m9 * r->m7 + l->m10 * r->m11 + l->m11 * r->m15;
	out.m12 = l->m12 * r->m0 + l->m13 * r->m4 + l->m14 * r->m8 + l->m15 * r->m12;
	out.m13 = l->m12 * r->m1 + l->m13 * r->m5 + l->m14 * r->m9 + l->m15 * r->m13;
	out.m14 = l->m12 * r->m2 + l->m13 * r->m6 + l->m14 * r->m10 + l->m15 * r->m14;
	out.m15 = l->m12 * r->m3 + l->m13 * r->m7 + l->m14 * r->m11 + l->m15 * r->m15;

	return out;
}

/* begin_pipeline: bind shader, VAO and premultiplied blending */
static void
begin_pipeline (LrgPaintRenderer *self)
{
	Matrix transform;
	Matrix modelview;
	Matrix projection;
	Matrix tmp;
	Matrix mvp;
	gint   slot = 0;

	/* Same accumulation raylib applies to its own batch: the pushed
	 * transform, then the camera/model-view, then the projection */
	transform = rlGetMatrixTransform ();
	modelview = rlGetMatrixModelview ();
	projection = rlGetMatrixProjection ();
	tmp = mat_mul (&transform, &modelview);
	mvp = mat_mul (&tmp, &projection);

	/* Blend state first: rlSetBlendMode() flushes raylib's batch, and
	 * that flush unbinds whatever program and VAO are current */
	rlSetBlendFactors (RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD);
	rlSetBlendMode (RL_BLEND_CUSTOM);

	rlEnableShader (self->shader);
	rlSetUniformMatrix (self->loc_mvp, mvp);
	rlSetUniform (self->loc_tex, &slot, RL_SHADER_UNIFORM_INT, 1);

	rlEnableVertexArray (self->vao);
	rlEnableVertexBuffer (self->instance_vbo);
}

static void
end_pipeline (void)
{
	rlDisableVertexArray ();
	rlDisableVertexBuffer ();
	rlActiveTextureSlot (0);
	rlDisableTexture ();
	rlDisableShader ();
	rlSetBlendMode (RL_BLEND_ALPHA);
}

/*
 * run_callback:
 *
 * Runs a recorded immediate-mode callback with the clip that was
 * active when it was recorded applied as a scissor box, then flushes
 * raylib's batch so its output lands before the next instances.
 */
static void
run_callback (LrgPaintList *list,
              guint         index)
{
	gfloat x0;
	gfloat y0;
	gfloat x1;
	gfloat y1;

	if (lrg_paint_list_get_command_clip (list, index, &x0, &y0, &x1, &y1))
	{
		gint sx = (gint) floorf (x0);
		gint sy = (gint) floorf (y0);

		BeginScissorMode (sx, sy,
		                  MAX ((gint) ceilf (x1) - sx, 0),
		                  MAX ((gint) ceilf (y1) - sy, 0));
		lrg_paint_list_run_callback (list, index);
		EndScissorMode ();
		return;
	}

	lrg_paint_list_run_callback (list, index);
	rlDrawRenderBatchActive ();
}

gboolean
lrg_paint_renderer_render (LrgPaintRenderer *self,
                           LrgPaintList     *list)
{
	const LrgPaintInstance *instances;
	guint                   n_instances;
	guint                   n_commands;
	guint                   i;
	gboolean                pipeline_bound = FALSE;

	g_return_val_if_fail (LRG_IS_PAINT_RENDERER (self), FALSE);
	g_return_val_if_fail (LRG_IS_PAINT_LIST (list), FALSE);

	self->draw_calls = 0;

	if (!IsWindowReady ())
		return FALSE;

	n_commands = lrg_paint_list_get_n_commands (list);
	if (n_commands == 0)
		return TRUE;

	if (!ensure_gl (self))
		return FALSE;

	/* Anything raylib has queued must land underneath us */
	rlDrawRenderBatchActive ();

	/* Upload the whole frame once; batches re-point into it */
	instances = lrg_paint_list_get_instances (list, &n_instances);
	if (n_instances > 0)
	{
		ensure_capacity (self, n_instances);
		rlUpdateVertexBuffer (self->instance_vbo, instances,
		                      (gint) (n_instances * sizeof (LrgPaintInstance)), 0);
	}

	for (i = 0; i < n_commands; i++)
	{
		guint               first;
		guint               count;
		guint               texture;
		LrgPaintCommandKind kind;

		kind = lrg_paint_list_get_command (list, i, &first, &count, &texture);

		if (kind == LRG_PAINT_COMMAND_INSTANCES)
		{
			if (count == 0)
				continue;

			if (!pipeline_bound)
			{
				begin_pipeline (self);
				pipeline_bound = TRUE;
			}

			rlActiveTextureSlot (0);
			rlEnableTexture (texture != 0 ? texture : rlGetTextureIdDefault ());
			point_instance_attributes (first);
			rlDrawVertexArrayInstanced (0, 6, (gint) count);
			self->draw_calls++;
		}
		else
		{
			if (pipeline_bound)
			{
				end_pipeline ();
				pipeline_bound = FALSE;
			}
			run_callback (list, i);
		}
	}

	if (pipeline_bound)
		end_pipeline ();

	return TRUE;
}
