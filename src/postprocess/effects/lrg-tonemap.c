/* lrg-tonemap.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tonemapping: decode (gamma 2.2, unless the input is linear), multiply
 * by 2^exposure, map with the operator's curve, encode (1/2.2). The
 * curves:
 *   none      clamp(x)
 *   reinhard  x (1 + x / W^2) / (1 + x)                     (extended)
 *   filmic    h(2x) / h(W), h(x) = (x(Ax+CB)+DE)/(x(Ax+B)+DF) - E/F
 *             A .15 B .50 C .10 D .20 E .02 F .30          (Hable)
 *   aces      x (2.51x + .03) / (x (2.43x + .59) + .14)     (Narkowicz)
 * applied per channel. tonemap_rgb() is the same arithmetic on the CPU
 * (lrg_tonemap_apply_rgb(); the tests compare the GPU result with it).
 */

#include "config.h"
#include "lrg-tonemap.h"
#include <math.h>
#include <raylib.h>
#include <rlgl.h>
#include "core/lrg-prewarm.h"

static const gchar tonemap_fs[] =
	"#version 330\n"
	"in vec2 fragTexCoord;\n"
	"in vec4 fragColor;\n"
	"uniform sampler2D texture0;\n"
	"uniform vec4 params;\n"   /* exposure scale, white point, linear input, intensity */
	"uniform int op;\n"
	"out vec4 finalColor;\n"
	"vec3 hable(vec3 x)\n"
	"{\n"
	"    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;\n"
	"    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;\n"
	"}\n"
	"void main()\n"
	"{\n"
	"    vec4 c = texture(texture0, fragTexCoord);\n"
	"    vec3 x = max(c.rgb, 0.0);\n"
	"    vec3 m;\n"
	"    float w = params.y;\n"
	"    if (params.z < 0.5)\n"
	"        x = pow(x, vec3(2.2));\n"
	"    x *= params.x;\n"
	"    if (op == 1)\n"
	"        m = x * (1.0 + x / (w * w)) / (1.0 + x);\n"
	"    else if (op == 2)\n"
	"        m = hable(x * 2.0) / hable(vec3(w)).x;\n"
	"    else if (op == 3)\n"
	"        m = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);\n"
	"    else\n"
	"        m = x;\n"
	"    m = pow(clamp(m, 0.0, 1.0), vec3(1.0 / 2.2));\n"
	"    finalColor = vec4(mix(c.rgb, m, params.w), c.a);\n"
	"}\n";

struct _LrgTonemap
{
	LrgPostEffect       parent_instance;

	LrgTonemapOperator  op;
	gfloat              exposure;
	gfloat              white_point;
	gboolean            linear_input;

	GrlShader          *shader;
	gboolean            shader_tried;
	gint                loc_params;
	gint                loc_op;
};

static void lrg_tonemap_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgTonemap, lrg_tonemap, LRG_TYPE_POST_EFFECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                      lrg_tonemap_prewarmable_init))

enum
{
	PROP_0,
	PROP_OPERATOR,
	PROP_EXPOSURE,
	PROP_WHITE_POINT,
	PROP_LINEAR_INPUT,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

GType
lrg_tonemap_operator_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TONEMAP_OPERATOR_NONE, "LRG_TONEMAP_OPERATOR_NONE", "none" },
			{ LRG_TONEMAP_OPERATOR_REINHARD, "LRG_TONEMAP_OPERATOR_REINHARD", "reinhard" },
			{ LRG_TONEMAP_OPERATOR_FILMIC, "LRG_TONEMAP_OPERATOR_FILMIC", "filmic" },
			{ LRG_TONEMAP_OPERATOR_ACES, "LRG_TONEMAP_OPERATOR_ACES", "aces" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgTonemapOperator"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * The curve on the CPU
 * ========================================================================== */

static gfloat
hable (gfloat x)
{
	const gfloat a = 0.15f, b = 0.50f, c = 0.10f, d = 0.20f, e = 0.02f, f = 0.30f;

	return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f;
}

static gfloat
curve (LrgTonemapOperator op,
       gfloat             x,
       gfloat             w)
{
	switch (op)
	{
	case LRG_TONEMAP_OPERATOR_REINHARD:
		return x * (1.0f + x / (w * w)) / (1.0f + x);
	case LRG_TONEMAP_OPERATOR_FILMIC:
		return hable (x * 2.0f) / hable (w);
	case LRG_TONEMAP_OPERATOR_ACES:
		return (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
	case LRG_TONEMAP_OPERATOR_NONE:
	default:
		return x;
	}
}

void
lrg_tonemap_apply_rgb (LrgTonemap   *self,
                       const gfloat *rgb,
                       gfloat       *out)
{
	gfloat scale, k, in[3];
	gint   i;

	g_return_if_fail (LRG_IS_TONEMAP (self));
	g_return_if_fail (rgb != NULL && out != NULL);

	scale = exp2f (self->exposure);
	k = lrg_post_effect_get_intensity (LRG_POST_EFFECT (self));
	for (i = 0; i < 3; i++)
		in[i] = rgb[i];
	for (i = 0; i < 3; i++)
	{
		gfloat x = MAX (in[i], 0.0f);
		gfloat m;

		if (!self->linear_input)
			x = powf (x, 2.2f);
		m = curve (self->op, x * scale, self->white_point);
		m = powf (CLAMP (m, 0.0f, 1.0f), 1.0f / 2.2f);
		out[i] = in[i] + (m - in[i]) * k;
	}
}

const gchar *
lrg_tonemap_get_fragment_shader (void)
{
	return tonemap_fs;
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

/* Compiles the pass shader once (needs a window) */
static GrlShader *
tonemap_get_shader (LrgTonemap *self)
{
	g_autoptr(GError) error = NULL;

	if (self->shader != NULL || self->shader_tried || !IsWindowReady ())
		return self->shader;
	self->shader_tried = TRUE;
	self->shader = lrg_post_effect_compile_shader (tonemap_fs, &error);
	if (self->shader == NULL)
	{
		g_warning ("tonemap: %s", error->message);
		return NULL;
	}
	self->loc_params = grl_shader_get_location (self->shader, "params");
	self->loc_op = grl_shader_get_location (self->shader, "op");
	return self->shader;
}

static void
lrg_tonemap_real_shutdown (LrgPostEffect *effect)
{
	LrgTonemap *self = LRG_TONEMAP (effect);

	g_clear_object (&self->shader);
	self->shader_tried = FALSE;
}

static void
lrg_tonemap_real_apply (LrgPostEffect *effect,
                        guint          source_texture_id,
                        guint          target_texture_id,
                        guint          width,
                        guint          height,
                        gfloat         delta_time)
{
	LrgTonemap *self = LRG_TONEMAP (effect);
	GrlShader  *shader;
	gfloat      k;

	(void) target_texture_id;
	(void) delta_time;

	k = lrg_post_effect_get_intensity (effect);
	shader = k > 0.0f ? tonemap_get_shader (self) : NULL;
	if (shader != NULL)
	{
		grl_shader_set_value_vec4 (shader, self->loc_params, exp2f (self->exposure),
		                           self->white_point, self->linear_input ? 1.0f : 0.0f, k);
		grl_shader_set_value_int (shader, self->loc_op, (gint) self->op);
		grl_shader_begin (shader);
	}
	lrg_post_effect_draw_fullscreen (source_texture_id, width, height);
	if (shader != NULL)
		grl_shader_end (shader);
}

static const gchar *
lrg_tonemap_real_get_name (LrgPostEffect *effect)
{
	(void) effect;
	return "tonemap";
}

/* lrg_tonemap_prewarm: compiles the pass shader ahead of the first frame */
static gboolean
lrg_tonemap_prewarm (LrgPrewarmable *prewarmable)
{
	tonemap_get_shader (LRG_TONEMAP (prewarmable));
	return TRUE;
}

static void
lrg_tonemap_prewarmable_init (LrgPrewarmableInterface *iface)
{
	iface->prewarm = lrg_tonemap_prewarm;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_tonemap_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	LrgTonemap *self = LRG_TONEMAP (object);

	switch (prop_id)
	{
	case PROP_OPERATOR:
		g_value_set_enum (value, (gint) self->op);
		break;
	case PROP_EXPOSURE:
		g_value_set_float (value, self->exposure);
		break;
	case PROP_WHITE_POINT:
		g_value_set_float (value, self->white_point);
		break;
	case PROP_LINEAR_INPUT:
		g_value_set_boolean (value, self->linear_input);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_tonemap_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	LrgTonemap *self = LRG_TONEMAP (object);

	switch (prop_id)
	{
	case PROP_OPERATOR:
		lrg_tonemap_set_operator (self, (LrgTonemapOperator) g_value_get_enum (value));
		break;
	case PROP_EXPOSURE:
		lrg_tonemap_set_exposure (self, g_value_get_float (value));
		break;
	case PROP_WHITE_POINT:
		lrg_tonemap_set_white_point (self, g_value_get_float (value));
		break;
	case PROP_LINEAR_INPUT:
		lrg_tonemap_set_linear_input (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_tonemap_class_init (LrgTonemapClass *klass)
{
	GObjectClass       *object_class = G_OBJECT_CLASS (klass);
	LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);

	object_class->get_property = lrg_tonemap_get_property;
	object_class->set_property = lrg_tonemap_set_property;
	effect_class->shutdown = lrg_tonemap_real_shutdown;
	effect_class->apply = lrg_tonemap_real_apply;
	effect_class->get_name = lrg_tonemap_real_get_name;

	/**
	 * LrgTonemap:operator:
	 *
	 * The tonemapping curve.
	 */
	properties[PROP_OPERATOR] =
		g_param_spec_enum ("operator", "Operator", "Tonemapping curve",
		                   LRG_TYPE_TONEMAP_OPERATOR, LRG_TONEMAP_OPERATOR_ACES,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTonemap:exposure:
	 *
	 * Exposure in stops (EV): linear values are multiplied by 2^exposure.
	 */
	properties[PROP_EXPOSURE] =
		g_param_spec_float ("exposure", "Exposure", "Exposure in stops",
		                    -16.0f, 16.0f, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTonemap:white-point:
	 *
	 * The linear value mapped to display white (Reinhard and filmic).
	 */
	properties[PROP_WHITE_POINT] =
		g_param_spec_float ("white-point", "White Point", "Linear value mapped to white",
		                    0.1f, 100.0f, 4.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTonemap:linear-input:
	 *
	 * Whether the image holds linear light (else it is decoded with
	 * gamma 2.2 first).
	 */
	properties[PROP_LINEAR_INPUT] =
		g_param_spec_boolean ("linear-input", "Linear Input", "The image holds linear light",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_tonemap_init (LrgTonemap *self)
{
	self->op = LRG_TONEMAP_OPERATOR_ACES;
	self->exposure = 0.0f;
	self->white_point = 4.0f;
	self->linear_input = FALSE;

	/* compile ahead of the first frame when a loading screen prewarms */
	lrg_prewarm_track (LRG_PREWARMABLE (self));
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgTonemap *
lrg_tonemap_new (void)
{
	return g_object_new (LRG_TYPE_TONEMAP, NULL);
}

LrgTonemapOperator
lrg_tonemap_get_operator (LrgTonemap *self)
{
	g_return_val_if_fail (LRG_IS_TONEMAP (self), LRG_TONEMAP_OPERATOR_ACES);

	return self->op;
}

void
lrg_tonemap_set_operator (LrgTonemap         *self,
                          LrgTonemapOperator  op)
{
	g_return_if_fail (LRG_IS_TONEMAP (self));
	g_return_if_fail (op <= LRG_TONEMAP_OPERATOR_ACES);

	if (self->op == op)
		return;
	self->op = op;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OPERATOR]);
}

gfloat
lrg_tonemap_get_exposure (LrgTonemap *self)
{
	g_return_val_if_fail (LRG_IS_TONEMAP (self), 0.0f);

	return self->exposure;
}

void
lrg_tonemap_set_exposure (LrgTonemap *self,
                          gfloat      ev)
{
	g_return_if_fail (LRG_IS_TONEMAP (self));

	if (!isfinite (ev))
		return;
	ev = CLAMP (ev, -16.0f, 16.0f);
	if (self->exposure == ev)
		return;
	self->exposure = ev;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXPOSURE]);
}

gfloat
lrg_tonemap_get_white_point (LrgTonemap *self)
{
	g_return_val_if_fail (LRG_IS_TONEMAP (self), 4.0f);

	return self->white_point;
}

void
lrg_tonemap_set_white_point (LrgTonemap *self,
                             gfloat      white)
{
	g_return_if_fail (LRG_IS_TONEMAP (self));

	if (!isfinite (white))
		return;
	white = CLAMP (white, 0.1f, 100.0f);
	if (self->white_point == white)
		return;
	self->white_point = white;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WHITE_POINT]);
}

gboolean
lrg_tonemap_get_linear_input (LrgTonemap *self)
{
	g_return_val_if_fail (LRG_IS_TONEMAP (self), FALSE);

	return self->linear_input;
}

void
lrg_tonemap_set_linear_input (LrgTonemap *self,
                              gboolean    linear)
{
	g_return_if_fail (LRG_IS_TONEMAP (self));

	linear = !!linear;
	if (self->linear_input == linear)
		return;
	self->linear_input = linear;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LINEAR_INPUT]);
}
