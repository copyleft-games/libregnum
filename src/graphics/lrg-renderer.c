/* lrg-renderer.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Render management.
 */

#include "config.h"
#include "lrg-renderer.h"

/**
 * SECTION:lrg-renderer
 * @title: LrgRenderer
 * @short_description: Render management
 *
 * #LrgRenderer provides frame and layer management for rendering.
 * It manages the active camera and coordinates rendering operations
 * with the window.
 *
 * ## Frame Lifecycle
 *
 * A typical frame consists of:
 * 1. lrg_renderer_begin_frame() - Start the frame
 * 2. lrg_renderer_clear() - Clear with background color
 * 3. lrg_renderer_begin_layer() / end_layer() - Render each layer
 * 4. lrg_renderer_end_frame() - Present the frame
 *
 * ## Layers
 *
 * Layers help organize render order:
 * - %LRG_RENDER_LAYER_BACKGROUND: Sky, parallax backgrounds
 * - %LRG_RENDER_LAYER_WORLD: Main game content (uses camera transform)
 * - %LRG_RENDER_LAYER_EFFECTS: Particles, visual effects
 * - %LRG_RENDER_LAYER_UI: HUD, menus (screen space)
 * - %LRG_RENDER_LAYER_DEBUG: Debug overlays
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * g_autoptr(LrgRenderer) renderer = lrg_renderer_new (window);
 * g_autoptr(LrgCamera3D) camera = lrg_camera3d_new ();
 * g_autoptr(GrlColor) bg = grl_color_new (40, 40, 60, 255);
 *
 * lrg_renderer_set_camera (renderer, LRG_CAMERA (camera));
 *
 * while (!lrg_window_should_close (window))
 * {
 *     lrg_renderer_begin_frame (renderer);
 *     lrg_renderer_clear (renderer, bg);
 *
 *     lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_WORLD);
 *     // Draw 3D world with camera transform
 *     lrg_renderer_end_layer (renderer);
 *
 *     lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_UI);
 *     // Draw 2D UI in screen space
 *     lrg_renderer_end_layer (renderer);
 *
 *     lrg_renderer_end_frame (renderer);
 * }
 * ]|
 */

typedef struct
{
	LrgWindow      *window;
	LrgCamera      *camera;
	GrlColor       *background_color;
	LrgRenderLayer  current_layer;
	gboolean        in_frame;
	gboolean        in_layer;
	gboolean        camera_active;
} LrgRendererPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgRenderer, lrg_renderer, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_WINDOW,
	PROP_CAMERA,
	PROP_BACKGROUND_COLOR,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_FRAME_BEGIN,
	SIGNAL_FRAME_END,
	SIGNAL_LAYER_RENDER,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static void
lrg_renderer_real_begin_frame (LrgRenderer *self)
{
	LrgRendererPrivate *priv = lrg_renderer_get_instance_private (self);

	if (priv->window != NULL)
		lrg_window_begin_frame (priv->window);

	priv->in_frame = TRUE;
}

static void
lrg_renderer_real_end_frame (LrgRenderer *self)
{
	LrgRendererPrivate *priv = lrg_renderer_get_instance_private (self);

	/* Make sure any active layer is ended */
	if (priv->in_layer)
	{
		if (priv->camera_active && priv->camera != NULL)
		{
			lrg_camera_end (priv->camera);
			priv->camera_active = FALSE;
		}
		priv->in_layer = FALSE;
	}

	if (priv->window != NULL)
		lrg_window_end_frame (priv->window);

	priv->in_frame = FALSE;
}

static void
lrg_renderer_real_render_layer (LrgRenderer    *self,
                                LrgRenderLayer  layer)
{
	/* Default implementation does nothing special */
	(void)self;
	(void)layer;
}

static void
lrg_renderer_real_clear (LrgRenderer *self,
                         GrlColor    *color)
{
	LrgRendererPrivate *priv = lrg_renderer_get_instance_private (self);

	if (priv->window != NULL)
		lrg_window_clear (priv->window, color);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_renderer_finalize (GObject *object)
{
	LrgRenderer        *self = LRG_RENDERER (object);
	LrgRendererPrivate *priv = lrg_renderer_get_instance_private (self);

	g_clear_object (&priv->window);
	g_clear_object (&priv->camera);
	g_clear_pointer (&priv->background_color, grl_color_free);

	G_OBJECT_CLASS (lrg_renderer_parent_class)->finalize (object);
}

static void
lrg_renderer_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgRenderer        *self = LRG_RENDERER (object);
	LrgRendererPrivate *priv = lrg_renderer_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_WINDOW:
		g_value_set_object (value, priv->window);
		break;
	case PROP_CAMERA:
		g_value_set_object (value, priv->camera);
		break;
	case PROP_BACKGROUND_COLOR:
		g_value_set_boxed (value, priv->background_color);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_renderer_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgRenderer        *self = LRG_RENDERER (object);
	LrgRendererPrivate *priv = lrg_renderer_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_WINDOW:
		g_clear_object (&priv->window);
		priv->window = g_value_dup_object (value);
		break;
	case PROP_CAMERA:
		lrg_renderer_set_camera (self, g_value_get_object (value));
		break;
	case PROP_BACKGROUND_COLOR:
		lrg_renderer_set_background_color (self, g_value_get_boxed (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_renderer_class_init (LrgRendererClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_renderer_finalize;
	object_class->get_property = lrg_renderer_get_property;
	object_class->set_property = lrg_renderer_set_property;

	/* Default virtual method implementations */
	klass->begin_frame = lrg_renderer_real_begin_frame;
	klass->end_frame = lrg_renderer_real_end_frame;
	klass->render_layer = lrg_renderer_real_render_layer;
	klass->clear = lrg_renderer_real_clear;

	/**
	 * LrgRenderer:window:
	 *
	 * The window to render to.
	 */
	properties[PROP_WINDOW] =
		g_param_spec_object ("window",
		                     "Window",
		                     "The window to render to",
		                     LRG_TYPE_WINDOW,
		                     G_PARAM_READWRITE |
		                     G_PARAM_CONSTRUCT_ONLY |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgRenderer:camera:
	 *
	 * The active camera for rendering.
	 */
	properties[PROP_CAMERA] =
		g_param_spec_object ("camera",
		                     "Camera",
		                     "The active camera",
		                     LRG_TYPE_CAMERA,
		                     G_PARAM_READWRITE |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgRenderer:background-color:
	 *
	 * The default background color.
	 */
	properties[PROP_BACKGROUND_COLOR] =
		g_param_spec_boxed ("background-color",
		                    "Background Color",
		                    "The default background color",
		                    GRL_TYPE_COLOR,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgRenderer::frame-begin:
	 * @self: the #LrgRenderer
	 *
	 * Emitted when a new frame begins.
	 */
	signals[SIGNAL_FRAME_BEGIN] =
		g_signal_new ("frame-begin",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE, 0);

	/**
	 * LrgRenderer::frame-end:
	 * @self: the #LrgRenderer
	 *
	 * Emitted when a frame ends.
	 */
	signals[SIGNAL_FRAME_END] =
		g_signal_new ("frame-end",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE, 0);

	/**
	 * LrgRenderer::layer-render:
	 * @self: the #LrgRenderer
	 * @layer: the render layer
	 *
	 * Emitted when a layer begins rendering.
	 */
	signals[SIGNAL_LAYER_RENDER] =
		g_signal_new ("layer-render",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE, 1,
		              LRG_TYPE_RENDER_LAYER);
}

static void
lrg_renderer_init (LrgRenderer *self)
{
	LrgRendererPrivate *priv = lrg_renderer_get_instance_private (self);

	priv->window = NULL;
	priv->camera = NULL;
	priv->background_color = grl_color_new (40, 40, 60, 255);
	priv->current_layer = LRG_RENDER_LAYER_BACKGROUND;
	priv->in_frame = FALSE;
	priv->in_layer = FALSE;
	priv->camera_active = FALSE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgRenderer *
lrg_renderer_new (LrgWindow *window)
{
	g_return_val_if_fail (LRG_IS_WINDOW (window), NULL);

	return g_object_new (LRG_TYPE_RENDERER,
	                     "window", window,
	                     NULL);
}

void
lrg_renderer_begin_frame (LrgRenderer *self)
{
	LrgRendererClass *klass;

	g_return_if_fail (LRG_IS_RENDERER (self));

	klass = LRG_RENDERER_GET_CLASS (self);
	if (klass->begin_frame != NULL)
		klass->begin_frame (self);

	g_signal_emit (self, signals[SIGNAL_FRAME_BEGIN], 0);
}

void
lrg_renderer_end_frame (LrgRenderer *self)
{
	LrgRendererClass *klass;

	g_return_if_fail (LRG_IS_RENDERER (self));

	klass = LRG_RENDERER_GET_CLASS (self);
	if (klass->end_frame != NULL)
		klass->end_frame (self);

	g_signal_emit (self, signals[SIGNAL_FRAME_END], 0);
}

void
lrg_renderer_clear (LrgRenderer *self,
                    GrlColor    *color)
{
	LrgRendererClass   *klass;
	LrgRendererPrivate *priv;
	GrlColor           *clear_color;

	g_return_if_fail (LRG_IS_RENDERER (self));

	priv = lrg_renderer_get_instance_private (self);

	/* Use provided color or fall back to background color */
	clear_color = (color != NULL) ? color : priv->background_color;

	klass = LRG_RENDERER_GET_CLASS (self);
	if (klass->clear != NULL)
		klass->clear (self, clear_color);
}

void
lrg_renderer_set_camera (LrgRenderer *self,
                         LrgCamera   *camera)
{
	LrgRendererPrivate *priv;

	g_return_if_fail (LRG_IS_RENDERER (self));
	g_return_if_fail (camera == NULL || LRG_IS_CAMERA (camera));

	priv = lrg_renderer_get_instance_private (self);

	if (priv->camera == camera)
		return;

	/* End active camera if switching during a layer */
	if (priv->camera_active && priv->camera != NULL)
	{
		lrg_camera_end (priv->camera);
		priv->camera_active = FALSE;
	}

	g_clear_object (&priv->camera);
	if (camera != NULL)
		priv->camera = g_object_ref (camera);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAMERA]);
}

LrgCamera *
lrg_renderer_get_camera (LrgRenderer *self)
{
	LrgRendererPrivate *priv;

	g_return_val_if_fail (LRG_IS_RENDERER (self), NULL);

	priv = lrg_renderer_get_instance_private (self);
	return priv->camera;
}

void
lrg_renderer_begin_layer (LrgRenderer    *self,
                          LrgRenderLayer  layer)
{
	LrgRendererPrivate *priv;
	LrgRendererClass   *klass;

	g_return_if_fail (LRG_IS_RENDERER (self));

	priv = lrg_renderer_get_instance_private (self);

	/* End previous layer if still active */
	if (priv->in_layer)
		lrg_renderer_end_layer (self);

	priv->current_layer = layer;
	priv->in_layer = TRUE;

	/* Apply camera transform for world layer */
	if (layer == LRG_RENDER_LAYER_WORLD && priv->camera != NULL)
	{
		lrg_camera_begin (priv->camera);
		priv->camera_active = TRUE;
	}

	/* Call virtual method */
	klass = LRG_RENDERER_GET_CLASS (self);
	if (klass->render_layer != NULL)
		klass->render_layer (self, layer);

	g_signal_emit (self, signals[SIGNAL_LAYER_RENDER], 0, layer);
}

void
lrg_renderer_end_layer (LrgRenderer *self)
{
	LrgRendererPrivate *priv;

	g_return_if_fail (LRG_IS_RENDERER (self));

	priv = lrg_renderer_get_instance_private (self);

	if (!priv->in_layer)
		return;

	/* End camera transform if it was active */
	if (priv->camera_active && priv->camera != NULL)
	{
		lrg_camera_end (priv->camera);
		priv->camera_active = FALSE;
	}

	priv->in_layer = FALSE;
}

LrgRenderLayer
lrg_renderer_get_current_layer (LrgRenderer *self)
{
	LrgRendererPrivate *priv;

	g_return_val_if_fail (LRG_IS_RENDERER (self), LRG_RENDER_LAYER_BACKGROUND);

	priv = lrg_renderer_get_instance_private (self);
	return priv->current_layer;
}

LrgWindow *
lrg_renderer_get_window (LrgRenderer *self)
{
	LrgRendererPrivate *priv;

	g_return_val_if_fail (LRG_IS_RENDERER (self), NULL);

	priv = lrg_renderer_get_instance_private (self);
	return priv->window;
}

void
lrg_renderer_render_drawable (LrgRenderer *self,
                              LrgDrawable *drawable,
                              gfloat       delta)
{
	g_return_if_fail (LRG_IS_RENDERER (self));
	g_return_if_fail (LRG_IS_DRAWABLE (drawable));

	lrg_drawable_draw (drawable, delta);
}

void
lrg_renderer_set_background_color (LrgRenderer *self,
                                   GrlColor    *color)
{
	LrgRendererPrivate *priv;

	g_return_if_fail (LRG_IS_RENDERER (self));

	priv = lrg_renderer_get_instance_private (self);

	g_clear_pointer (&priv->background_color, grl_color_free);
	if (color != NULL)
		priv->background_color = grl_color_copy (color);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKGROUND_COLOR]);
}

GrlColor *
lrg_renderer_get_background_color (LrgRenderer *self)
{
	LrgRendererPrivate *priv;

	g_return_val_if_fail (LRG_IS_RENDERER (self), NULL);

	priv = lrg_renderer_get_instance_private (self);
	return grl_color_copy (priv->background_color);
}
