/* lrg-post-processor.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The post-processing pipeline: a capture target, two ping-pong targets
 * and a chain of LrgPostEffect passes.
 *
 * Frame flow (GPU):
 *   begin_capture  remember the bound target, bind target[0], clear
 *   ...            the caller draws the scene
 *   end_capture    flush, rebind the remembered target
 *   render         process (target[0] -> effects ping-ponging between
 *                  target[1] and target[0]) then draw the result over the
 *                  bound target with alpha blending
 *
 * Every effect pass runs with colour blending disabled so alpha and HDR
 * values pass through untouched; only the final composite blends.
 */

#include "config.h"
#include "lrg-post-processor.h"
#include <raylib.h>
#include <rlgl.h>
#include <string.h>

/**
 * SECTION:lrg-post-processor
 * @Title: LrgPostProcessor
 * @Short_description: Post-processing pipeline manager
 *
 * #LrgPostProcessor manages a chain of post-processing effects. The scene
 * is rendered into an offscreen target (RGBA16F when available), each
 * enabled effect runs as a shader pass in priority order, and the result
 * is composited into whatever target was bound when
 * lrg_post_processor_render() is called.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * g_autoptr(LrgPostProcessor) processor = lrg_post_processor_new (1280, 720);
 * g_autoptr(LrgTonemap) tonemap = lrg_tonemap_new ();
 * g_autoptr(LrgVignette) vignette = lrg_vignette_new ();
 *
 * lrg_post_effect_set_priority (LRG_POST_EFFECT (tonemap), 10);
 * lrg_post_effect_set_priority (LRG_POST_EFFECT (vignette), 20);
 * lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (tonemap));
 * lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (vignette));
 *
 * BeginDrawing ();
 * lrg_post_processor_begin_capture (processor);
 * ClearBackground (BLACK);
 * draw_scene ();
 * lrg_post_processor_end_capture (processor);
 * lrg_post_processor_render (processor, GetFrameTime ());
 * draw_hud ();
 * EndDrawing ();
 * ]|
 */

typedef struct
{
	GList           *effects;          /* LrgPostEffect, sorted by priority */
	guint            width;
	guint            height;
	gboolean         enabled;
	gboolean         capturing;
	gboolean         initialized;
	gboolean         hdr;              /* requested */
	gboolean         hdr_active;       /* what the live targets are */
	gboolean         auto_resize;
	LrgPostQuality   quality;

	/* GPU state: targets[0] is the capture, both ping-pong */
	RenderTexture2D  targets[2];
	guint            target_w;
	guint            target_h;
	gboolean         has_capture;      /* target[0] holds this frame's scene */
	guint            outer_fbo;        /* bound before the capture */
	guint            outer_w;
	guint            outer_h;
	guint            pass_count;
} LrgPostProcessorPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgPostProcessor, lrg_post_processor, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_ENABLED,
	PROP_EFFECT_COUNT,
	PROP_HDR,
	PROP_QUALITY,
	PROP_AUTO_RESIZE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gint
compare_effect_priority (gconstpointer a,
                         gconstpointer b)
{
	LrgPostEffect *effect_a = LRG_POST_EFFECT ((gpointer) a);
	LrgPostEffect *effect_b = LRG_POST_EFFECT ((gpointer) b);
	gint           pa = lrg_post_effect_get_priority (effect_a);
	gint           pb = lrg_post_effect_get_priority (effect_b);

	/* no subtraction: priorities span the whole gint range */
	return (pa > pb) - (pa < pb);
}

/* ==========================================================================
 * GPU targets
 * ========================================================================== */

/* The size of the target bound right now (the window's render size for
 * the default framebuffer). */
static void
bound_target_size (guint  fbo,
                   guint *w,
                   guint *h)
{
	gint tw, th;

	if (fbo == 0)
	{
		tw = GetRenderWidth ();
		th = GetRenderHeight ();
	}
	else
	{
		tw = rlGetFramebufferWidth ();
		th = rlGetFramebufferHeight ();
	}
	*w = (guint) MAX (tw, 1);
	*h = (guint) MAX (th, 1);
}

static void
unload_targets (LrgPostProcessorPrivate *priv)
{
	gint i;

	for (i = 0; i < 2; i++)
	{
		if (priv->targets[i].id != 0 && IsWindowReady ())
			UnloadRenderTexture (priv->targets[i]);
		memset (&priv->targets[i], 0, sizeof priv->targets[i]);
	}
	priv->target_w = 0;
	priv->target_h = 0;
	priv->has_capture = FALSE;
	priv->hdr_active = FALSE;
}

/* One render texture of @format; FALSE (and nothing loaded) when the
 * driver cannot render to it. */
static gboolean
load_target (RenderTexture2D *rt,
             guint            w,
             guint            h,
             gint             format)
{
	*rt = LoadRenderTextureEx ((gint) w, (gint) h, format);
	if (rt->id == 0 || rt->texture.id == 0 || !rlFramebufferComplete (rt->id))
	{
		if (rt->id != 0 || rt->texture.id != 0)
			UnloadRenderTexture (*rt);
		memset (rt, 0, sizeof *rt);
		return FALSE;
	}
	SetTextureFilter (rt->texture, TEXTURE_FILTER_BILINEAR);
	SetTextureWrap (rt->texture, TEXTURE_WRAP_CLAMP);
	return TRUE;
}

/*
 * Makes sure both targets exist at the processor size. Creating a render
 * texture binds and unbinds framebuffers, so the batch is flushed first
 * and the caller rebinds what it needs afterwards.
 */
static gboolean
ensure_targets (LrgPostProcessorPrivate *priv)
{
	gboolean hdr_ok = FALSE;
	gint     i;

	if (!IsWindowReady ())
		return FALSE;
	/* set_hdr() zeroes target_w to force a rebuild */
	if (priv->targets[0].id != 0 && priv->target_w == priv->width &&
	    priv->target_h == priv->height)
		return TRUE;

	rlDrawRenderBatchActive ();
	unload_targets (priv);

	/* HDR first (RGBA16F), RGBA8 when the FBO is incomplete */
	if (priv->hdr)
	{
		hdr_ok = TRUE;
		for (i = 0; i < 2 && hdr_ok; i++)
			hdr_ok = load_target (&priv->targets[i], priv->width, priv->height,
			                      PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
		if (!hdr_ok)
		{
			unload_targets (priv);
			g_debug ("post processor: no float render targets, using RGBA8");
		}
	}
	if (!hdr_ok)
	{
		for (i = 0; i < 2; i++)
		{
			if (!load_target (&priv->targets[i], priv->width, priv->height,
			                  PIXELFORMAT_UNCOMPRESSED_R8G8B8A8))
			{
				g_warning ("post processor: cannot create a %ux%u render target",
				           priv->width, priv->height);
				unload_targets (priv);
				return FALSE;
			}
		}
	}
	priv->hdr_active = hdr_ok;
	priv->target_w = priv->width;
	priv->target_h = priv->height;
	return TRUE;
}

/* ==========================================================================
 * Default virtual method implementations
 * ========================================================================== */

static void
lrg_post_processor_real_begin_capture (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);
	guint                    outer, ow, oh;

	if (!priv->enabled)
		return;

	priv->capturing = TRUE;
	priv->has_capture = FALSE;
	if (!IsWindowReady ())
		return;   /* headless: state only */

	/* What to return to, read before creating targets rebinds anything */
	rlDrawRenderBatchActive ();
	outer = rlGetActiveFramebuffer ();
	bound_target_size (outer, &ow, &oh);
	priv->outer_fbo = outer;
	priv->outer_w = ow;
	priv->outer_h = oh;

	if (priv->auto_resize && (ow != priv->width || oh != priv->height))
		lrg_post_processor_resize (self, ow, oh);

	if (!ensure_targets (priv))
	{
		lrg_post_effect_bind_framebuffer (outer, ow, oh);
		return;
	}

	lrg_post_effect_bind_framebuffer (priv->targets[0].id, priv->target_w, priv->target_h);
	ClearBackground (BLANK);
	priv->has_capture = TRUE;
}

static void
lrg_post_processor_real_end_capture (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);

	if (!priv->capturing)
		return;
	priv->capturing = FALSE;
	if (priv->has_capture)
		lrg_post_effect_bind_framebuffer (priv->outer_fbo, priv->outer_w, priv->outer_h);
}

static void
lrg_post_processor_real_render (LrgPostProcessor *self,
                                gfloat            delta_time)
{
	LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);
	Texture2D                tex;
	Rectangle                src, dst;
	Vector2                  origin = { 0.0f, 0.0f };
	guint                    outer, ow, oh, result;

	if (priv->capturing)
		lrg_post_processor_end_capture (self);
	if (!priv->has_capture || !IsWindowReady ())
		return;
	priv->has_capture = FALSE;

	rlDrawRenderBatchActive ();
	outer = rlGetActiveFramebuffer ();
	bound_target_size (outer, &ow, &oh);

	/* A processor disabled after the capture still shows the scene */
	result = priv->enabled
	         ? lrg_post_processor_process (self, priv->targets[0].texture.id, delta_time)
	         : priv->targets[0].texture.id;

	/* Composite: alpha blended over the bound target, scaled to it */
	memset (&tex, 0, sizeof tex);
	tex.id = result;
	tex.width = (gint) priv->target_w;
	tex.height = (gint) priv->target_h;
	tex.mipmaps = 1;
	tex.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	src.x = 0.0f;
	src.y = 0.0f;
	src.width = (gfloat) priv->target_w;
	src.height = -(gfloat) priv->target_h;
	dst.x = 0.0f;
	dst.y = 0.0f;
	dst.width = (gfloat) ow;
	dst.height = (gfloat) oh;
	DrawTexturePro (tex, src, dst, origin, 0.0f, WHITE);
	rlDrawRenderBatchActive ();
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_post_processor_finalize (GObject *object)
{
	LrgPostProcessor        *self = LRG_POST_PROCESSOR (object);
	LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);
	GList                   *l;

	for (l = priv->effects; l != NULL; l = l->next)
		lrg_post_effect_shutdown (LRG_POST_EFFECT (l->data));
	g_list_free_full (priv->effects, g_object_unref);
	priv->effects = NULL;

	/* GL objects only while the context lives (else they died with it) */
	unload_targets (priv);

	G_OBJECT_CLASS (lrg_post_processor_parent_class)->finalize (object);
}

static void
lrg_post_processor_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	LrgPostProcessor        *self = LRG_POST_PROCESSOR (object);
	LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_WIDTH:
		g_value_set_uint (value, priv->width);
		break;
	case PROP_HEIGHT:
		g_value_set_uint (value, priv->height);
		break;
	case PROP_ENABLED:
		g_value_set_boolean (value, priv->enabled);
		break;
	case PROP_EFFECT_COUNT:
		g_value_set_uint (value, g_list_length (priv->effects));
		break;
	case PROP_HDR:
		g_value_set_boolean (value, priv->hdr);
		break;
	case PROP_QUALITY:
		g_value_set_enum (value, (gint) priv->quality);
		break;
	case PROP_AUTO_RESIZE:
		g_value_set_boolean (value, priv->auto_resize);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_post_processor_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	LrgPostProcessor        *self = LRG_POST_PROCESSOR (object);
	LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_WIDTH:
		priv->width = MAX (g_value_get_uint (value), 1u);
		break;
	case PROP_HEIGHT:
		priv->height = MAX (g_value_get_uint (value), 1u);
		break;
	case PROP_ENABLED:
		lrg_post_processor_set_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_HDR:
		lrg_post_processor_set_hdr (self, g_value_get_boolean (value));
		break;
	case PROP_QUALITY:
		lrg_post_processor_set_quality (self, (LrgPostQuality) g_value_get_enum (value));
		break;
	case PROP_AUTO_RESIZE:
		lrg_post_processor_set_auto_resize (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_post_processor_constructed (GObject *object)
{
	LrgPostProcessor        *self = LRG_POST_PROCESSOR (object);
	LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);

	G_OBJECT_CLASS (lrg_post_processor_parent_class)->constructed (object);

	/* GPU targets are created lazily by the first capture (a processor
	 * may be built before the window opens); effects are initialised
	 * (CPU side) as they are added. */
	priv->initialized = TRUE;
}

static void
lrg_post_processor_class_init (LrgPostProcessorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_post_processor_finalize;
	object_class->get_property = lrg_post_processor_get_property;
	object_class->set_property = lrg_post_processor_set_property;
	object_class->constructed = lrg_post_processor_constructed;

	klass->begin_capture = lrg_post_processor_real_begin_capture;
	klass->end_capture = lrg_post_processor_real_end_capture;
	klass->render = lrg_post_processor_real_render;

	/**
	 * LrgPostProcessor:width:
	 *
	 * Render target width.
	 */
	properties[PROP_WIDTH] =
		g_param_spec_uint ("width", "Width", "Render target width",
		                   1, G_MAXUINT, 800,
		                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgPostProcessor:height:
	 *
	 * Render target height.
	 */
	properties[PROP_HEIGHT] =
		g_param_spec_uint ("height", "Height", "Render target height",
		                   1, G_MAXUINT, 600,
		                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgPostProcessor:enabled:
	 *
	 * Whether post-processing is enabled. When disabled, captures do
	 * nothing and the scene draws straight to the bound target.
	 */
	properties[PROP_ENABLED] =
		g_param_spec_boolean ("enabled", "Enabled", "Whether post-processing is enabled",
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPostProcessor:effect-count:
	 *
	 * Number of effects in the chain.
	 */
	properties[PROP_EFFECT_COUNT] =
		g_param_spec_uint ("effect-count", "Effect Count", "Number of effects in the chain",
		                   0, G_MAXUINT, 0,
		                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgPostProcessor:hdr:
	 *
	 * Whether floating-point (RGBA16F) targets are requested; falls back
	 * to RGBA8 when the driver cannot render to them.
	 */
	properties[PROP_HDR] =
		g_param_spec_boolean ("hdr", "HDR", "Request floating-point render targets",
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPostProcessor:quality:
	 *
	 * The quality tier handed to every effect.
	 */
	properties[PROP_QUALITY] =
		g_param_spec_enum ("quality", "Quality", "Quality tier for every effect",
		                   LRG_TYPE_POST_QUALITY, LRG_POST_QUALITY_HIGH,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPostProcessor:auto-resize:
	 *
	 * Whether a capture resizes the processor to the bound target.
	 */
	properties[PROP_AUTO_RESIZE] =
		g_param_spec_boolean ("auto-resize", "Auto Resize", "Follow the bound target's size",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_post_processor_init (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);

	priv->width = 800;
	priv->height = 600;
	priv->enabled = TRUE;
	priv->hdr = TRUE;
	priv->quality = LRG_POST_QUALITY_HIGH;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgPostProcessor *
lrg_post_processor_new (guint width,
                        guint height)
{
	return g_object_new (LRG_TYPE_POST_PROCESSOR,
	                     "width", width,
	                     "height", height,
	                     NULL);
}

void
lrg_post_processor_add_effect (LrgPostProcessor *self,
                               LrgPostEffect    *effect)
{
	LrgPostProcessorPrivate *priv;
	g_autoptr(GError)        error = NULL;

	g_return_if_fail (LRG_IS_POST_PROCESSOR (self));
	g_return_if_fail (LRG_IS_POST_EFFECT (effect));

	priv = lrg_post_processor_get_instance_private (self);

	if (g_list_find (priv->effects, effect) != NULL)
		return;

	/* CPU-side initialisation; shaders compile lazily on the first pass */
	if (priv->initialized && !lrg_post_effect_is_initialized (effect))
	{
		if (!lrg_post_effect_initialize (effect, priv->width, priv->height, &error))
		{
			g_warning ("Failed to initialize effect: %s",
			           error ? error->message : "Unknown error");
			return;
		}
	}
	lrg_post_effect_set_quality_tier (effect, priv->quality);

	priv->effects = g_list_insert_sorted (priv->effects, g_object_ref (effect),
	                                      compare_effect_priority);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EFFECT_COUNT]);
}

void
lrg_post_processor_remove_effect (LrgPostProcessor *self,
                                  LrgPostEffect    *effect)
{
	LrgPostProcessorPrivate *priv;
	GList                   *l;

	g_return_if_fail (LRG_IS_POST_PROCESSOR (self));
	g_return_if_fail (LRG_IS_POST_EFFECT (effect));

	priv = lrg_post_processor_get_instance_private (self);
	l = g_list_find (priv->effects, effect);
	if (l == NULL)
		return;
	lrg_post_effect_shutdown (effect);
	priv->effects = g_list_delete_link (priv->effects, l);
	g_object_unref (effect);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EFFECT_COUNT]);
}

LrgPostEffect *
lrg_post_processor_get_effect (LrgPostProcessor *self,
                               const gchar      *name)
{
	LrgPostProcessorPrivate *priv;
	GList                   *l;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	priv = lrg_post_processor_get_instance_private (self);
	for (l = priv->effects; l != NULL; l = l->next)
		if (g_strcmp0 (lrg_post_effect_get_name (LRG_POST_EFFECT (l->data)), name) == 0)
			return LRG_POST_EFFECT (l->data);
	return NULL;
}

GList *
lrg_post_processor_get_effects (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), NULL);

	priv = lrg_post_processor_get_instance_private (self);
	return priv->effects;
}

guint
lrg_post_processor_get_effect_count (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), 0);

	priv = lrg_post_processor_get_instance_private (self);
	return g_list_length (priv->effects);
}

void
lrg_post_processor_clear_effects (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;
	GList                   *l;

	g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

	priv = lrg_post_processor_get_instance_private (self);
	for (l = priv->effects; l != NULL; l = l->next)
		lrg_post_effect_shutdown (LRG_POST_EFFECT (l->data));
	g_list_free_full (priv->effects, g_object_unref);
	priv->effects = NULL;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EFFECT_COUNT]);
}

void
lrg_post_processor_begin_capture (LrgPostProcessor *self)
{
	LrgPostProcessorClass *klass;

	g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

	klass = LRG_POST_PROCESSOR_GET_CLASS (self);
	if (klass->begin_capture != NULL)
		klass->begin_capture (self);
}

void
lrg_post_processor_end_capture (LrgPostProcessor *self)
{
	LrgPostProcessorClass *klass;

	g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

	klass = LRG_POST_PROCESSOR_GET_CLASS (self);
	if (klass->end_capture != NULL)
		klass->end_capture (self);
}

void
lrg_post_processor_render (LrgPostProcessor *self,
                           gfloat            delta_time)
{
	LrgPostProcessorClass *klass;

	g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

	klass = LRG_POST_PROCESSOR_GET_CLASS (self);
	if (klass->render != NULL)
		klass->render (self, delta_time);
}

guint
lrg_post_processor_process (LrgPostProcessor *self,
                            guint             source_texture_id,
                            gfloat            delta_time)
{
	LrgPostProcessorPrivate *priv;
	GList                   *l;
	guint                    outer, ow, oh, src;
	gint                     dst;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), source_texture_id);

	priv = lrg_post_processor_get_instance_private (self);
	priv->pass_count = 0;
	if (source_texture_id == 0 || !IsWindowReady ())
		return source_texture_id;

	rlDrawRenderBatchActive ();
	outer = rlGetActiveFramebuffer ();
	bound_target_size (outer, &ow, &oh);
	if (!ensure_targets (priv))
	{
		lrg_post_effect_bind_framebuffer (outer, ow, oh);
		return source_texture_id;
	}

	/* Ping-pong: never write the texture being read */
	src = source_texture_id;
	dst = (src == priv->targets[0].texture.id) ? 1 : 0;
	rlDisableColorBlend ();
	for (l = priv->effects; l != NULL; l = l->next)
	{
		LrgPostEffect *effect = LRG_POST_EFFECT (l->data);

		if (!lrg_post_effect_is_enabled (effect))
			continue;
		lrg_post_effect_bind_framebuffer (priv->targets[dst].id, priv->target_w, priv->target_h);
		lrg_post_effect_apply (effect, src, priv->targets[dst].texture.id,
		                       priv->target_w, priv->target_h, delta_time);
		rlDrawRenderBatchActive ();
		src = priv->targets[dst].texture.id;
		dst = 1 - dst;
		priv->pass_count++;
	}
	rlEnableColorBlend ();
	lrg_post_effect_bind_framebuffer (outer, ow, oh);
	return src;
}

guint
lrg_post_processor_get_capture_texture (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), 0);

	priv = lrg_post_processor_get_instance_private (self);
	return priv->targets[0].texture.id;
}

void
lrg_post_processor_resize (LrgPostProcessor *self,
                           guint             width,
                           guint             height)
{
	LrgPostProcessorPrivate *priv;
	GList                   *l;

	g_return_if_fail (LRG_IS_POST_PROCESSOR (self));
	g_return_if_fail (width > 0 && height > 0);

	priv = lrg_post_processor_get_instance_private (self);
	if (priv->width == width && priv->height == height)
		return;

	/* Targets follow on the next capture (ensure_targets compares sizes) */
	priv->width = width;
	priv->height = height;
	for (l = priv->effects; l != NULL; l = l->next)
		lrg_post_effect_resize (LRG_POST_EFFECT (l->data), width, height);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
}

guint
lrg_post_processor_get_width (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), 0);

	priv = lrg_post_processor_get_instance_private (self);
	return priv->width;
}

guint
lrg_post_processor_get_height (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), 0);

	priv = lrg_post_processor_get_instance_private (self);
	return priv->height;
}

gboolean
lrg_post_processor_is_enabled (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), FALSE);

	priv = lrg_post_processor_get_instance_private (self);
	return priv->enabled;
}

void
lrg_post_processor_set_enabled (LrgPostProcessor *self,
                                gboolean          enabled)
{
	LrgPostProcessorPrivate *priv;

	g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

	priv = lrg_post_processor_get_instance_private (self);
	enabled = !!enabled;
	if (priv->enabled == enabled)
		return;
	priv->enabled = enabled;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}

gboolean
lrg_post_processor_is_capturing (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), FALSE);

	priv = lrg_post_processor_get_instance_private (self);
	return priv->capturing;
}

void
lrg_post_processor_sort_effects (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;

	g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

	priv = lrg_post_processor_get_instance_private (self);
	priv->effects = g_list_sort (priv->effects, compare_effect_priority);
}

gboolean
lrg_post_processor_get_hdr (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), FALSE);

	priv = lrg_post_processor_get_instance_private (self);
	return priv->hdr;
}

void
lrg_post_processor_set_hdr (LrgPostProcessor *self,
                            gboolean          hdr)
{
	LrgPostProcessorPrivate *priv;

	g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

	priv = lrg_post_processor_get_instance_private (self);
	hdr = !!hdr;
	if (priv->hdr == hdr)
		return;
	priv->hdr = hdr;

	/* Recreate on the next capture */
	priv->target_w = 0;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HDR]);
}

gboolean
lrg_post_processor_is_hdr_active (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), FALSE);

	priv = lrg_post_processor_get_instance_private (self);
	return priv->targets[0].id != 0 && priv->hdr_active;
}

LrgPostQuality
lrg_post_processor_get_quality (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), LRG_POST_QUALITY_HIGH);

	priv = lrg_post_processor_get_instance_private (self);
	return priv->quality;
}

void
lrg_post_processor_set_quality (LrgPostProcessor *self,
                                LrgPostQuality    quality)
{
	LrgPostProcessorPrivate *priv;
	GList                   *l;

	g_return_if_fail (LRG_IS_POST_PROCESSOR (self));
	g_return_if_fail (quality <= LRG_POST_QUALITY_ULTRA);

	priv = lrg_post_processor_get_instance_private (self);
	for (l = priv->effects; l != NULL; l = l->next)
		lrg_post_effect_set_quality_tier (LRG_POST_EFFECT (l->data), quality);
	if (priv->quality == quality)
		return;
	priv->quality = quality;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_QUALITY]);
}

gboolean
lrg_post_processor_get_auto_resize (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), FALSE);

	priv = lrg_post_processor_get_instance_private (self);
	return priv->auto_resize;
}

void
lrg_post_processor_set_auto_resize (LrgPostProcessor *self,
                                    gboolean          auto_resize)
{
	LrgPostProcessorPrivate *priv;

	g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

	priv = lrg_post_processor_get_instance_private (self);
	auto_resize = !!auto_resize;
	if (priv->auto_resize == auto_resize)
		return;
	priv->auto_resize = auto_resize;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTO_RESIZE]);
}

guint
lrg_post_processor_get_pass_count (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), 0);

	priv = lrg_post_processor_get_instance_private (self);
	return priv->pass_count;
}

void
lrg_post_processor_release (LrgPostProcessor *self)
{
	LrgPostProcessorPrivate *priv;
	GList                   *l;

	g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

	priv = lrg_post_processor_get_instance_private (self);
	if (priv->capturing)
		lrg_post_processor_end_capture (self);
	unload_targets (priv);

	/* Effects drop their shaders/targets in shutdown; initialise them
	 * again (CPU side) so the chain keeps working on the next capture */
	for (l = priv->effects; l != NULL; l = l->next)
	{
		LrgPostEffect     *effect = LRG_POST_EFFECT (l->data);
		g_autoptr(GError)  error = NULL;

		lrg_post_effect_shutdown (effect);
		if (!lrg_post_effect_initialize (effect, priv->width, priv->height, &error))
			g_warning ("post processor: re-initialising %s: %s",
			           lrg_post_effect_get_name (effect), error != NULL ? error->message : "failed");
	}
}
