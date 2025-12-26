/* lrg-grl-window.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Graylib window backend implementation.
 */

#include "config.h"
#include "lrg-grl-window.h"

/**
 * SECTION:lrg-grl-window
 * @title: LrgGrlWindow
 * @short_description: Graylib window backend
 *
 * #LrgGrlWindow is the graylib-based window implementation.
 * It wraps #GrlWindow and provides all the functionality
 * needed for game rendering.
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * g_autoptr(LrgGrlWindow) window = NULL;
 *
 * window = lrg_grl_window_new (800, 600, "My Game");
 * lrg_window_set_target_fps (LRG_WINDOW (window), 60);
 *
 * while (!lrg_window_should_close (LRG_WINDOW (window)))
 * {
 *     gfloat delta = lrg_window_get_frame_time (LRG_WINDOW (window));
 *
 *     lrg_window_begin_frame (LRG_WINDOW (window));
 *     lrg_window_clear (LRG_WINDOW (window), bg_color);
 *     // ... draw ...
 *     lrg_window_end_frame (LRG_WINDOW (window));
 * }
 * ]|
 */

struct _LrgGrlWindow
{
	LrgWindow   parent_instance;

	GrlWindow  *grl_window;
	gboolean    vsync;
};

G_DEFINE_TYPE (LrgGrlWindow, lrg_grl_window, LRG_TYPE_WINDOW)

enum
{
	PROP_0,
	PROP_VSYNC,
	N_PROPS
};

static GParamSpec *grl_window_properties[N_PROPS];

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_grl_window_begin_frame (LrgWindow *window)
{
	LrgGrlWindow *self = LRG_GRL_WINDOW (window);

	grl_window_begin_drawing (self->grl_window);
}

static void
lrg_grl_window_end_frame (LrgWindow *window)
{
	LrgGrlWindow *self = LRG_GRL_WINDOW (window);

	grl_window_end_drawing (self->grl_window);
}

static gboolean
lrg_grl_window_should_close (LrgWindow *window)
{
	LrgGrlWindow *self = LRG_GRL_WINDOW (window);

	return grl_window_should_close (self->grl_window);
}

static void
lrg_grl_window_set_should_close_impl (LrgWindow *window,
                                      gboolean   close)
{
	LrgGrlWindow *self = LRG_GRL_WINDOW (window);

	grl_window_set_should_close (self->grl_window, close);
}

static void
lrg_grl_window_poll_input (LrgWindow *window)
{
	LrgGrlWindow *self = LRG_GRL_WINDOW (window);

	grl_window_poll_input (self->grl_window);
}

static gfloat
lrg_grl_window_get_frame_time_impl (LrgWindow *window)
{
	LrgGrlWindow *self = LRG_GRL_WINDOW (window);

	return grl_window_get_frame_time (self->grl_window);
}

static gint
lrg_grl_window_get_fps_impl (LrgWindow *window)
{
	LrgGrlWindow *self = LRG_GRL_WINDOW (window);

	return grl_window_get_fps (self->grl_window);
}

static void
lrg_grl_window_clear (LrgWindow *window,
                      GrlColor  *color)
{
	LrgGrlWindow *self = LRG_GRL_WINDOW (window);

	grl_window_clear_background (self->grl_window, color);
}

static void
lrg_grl_window_show (LrgWindow *window)
{
	LrgGrlWindow *self = LRG_GRL_WINDOW (window);

	/* GrlWindow doesn't have a hide/show API, but we could use
	 * window state flags in the future */
	(void)self;
}

static void
lrg_grl_window_hide (LrgWindow *window)
{
	LrgGrlWindow *self = LRG_GRL_WINDOW (window);

	/* GrlWindow doesn't have a hide/show API */
	(void)self;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_grl_window_constructed (GObject *object)
{
	LrgGrlWindow *self = LRG_GRL_WINDOW (object);
	gint          width;
	gint          height;
	const gchar  *title;
	gint          target_fps;

	G_OBJECT_CLASS (lrg_grl_window_parent_class)->constructed (object);

	/* Get properties from parent class */
	width = lrg_window_get_width (LRG_WINDOW (self));
	height = lrg_window_get_height (LRG_WINDOW (self));
	title = lrg_window_get_title (LRG_WINDOW (self));
	target_fps = lrg_window_get_target_fps (LRG_WINDOW (self));

	/* Create the underlying GrlWindow */
	self->grl_window = grl_window_new (width, height, title);
	grl_window_set_target_fps (self->grl_window, target_fps);

	/* Apply vsync setting */
	if (self->vsync)
		grl_window_set_state (self->grl_window, GRL_FLAG_VSYNC_HINT);
}

static void
lrg_grl_window_finalize (GObject *object)
{
	LrgGrlWindow *self = LRG_GRL_WINDOW (object);

	g_clear_object (&self->grl_window);

	G_OBJECT_CLASS (lrg_grl_window_parent_class)->finalize (object);
}

static void
lrg_grl_window_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgGrlWindow *self = LRG_GRL_WINDOW (object);

	switch (prop_id)
	{
	case PROP_VSYNC:
		g_value_set_boolean (value, self->vsync);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_grl_window_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgGrlWindow *self = LRG_GRL_WINDOW (object);

	switch (prop_id)
	{
	case PROP_VSYNC:
		lrg_grl_window_set_vsync (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_grl_window_class_init (LrgGrlWindowClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWindowClass *window_class = LRG_WINDOW_CLASS (klass);

	object_class->constructed = lrg_grl_window_constructed;
	object_class->finalize = lrg_grl_window_finalize;
	object_class->get_property = lrg_grl_window_get_property;
	object_class->set_property = lrg_grl_window_set_property;

	/* Override virtual methods */
	window_class->begin_frame = lrg_grl_window_begin_frame;
	window_class->end_frame = lrg_grl_window_end_frame;
	window_class->should_close = lrg_grl_window_should_close;
	window_class->set_should_close = lrg_grl_window_set_should_close_impl;
	window_class->poll_input = lrg_grl_window_poll_input;
	window_class->get_frame_time = lrg_grl_window_get_frame_time_impl;
	window_class->get_fps = lrg_grl_window_get_fps_impl;
	window_class->clear = lrg_grl_window_clear;
	window_class->show = lrg_grl_window_show;
	window_class->hide = lrg_grl_window_hide;

	/* Properties */
	grl_window_properties[PROP_VSYNC] =
		g_param_spec_boolean ("vsync",
		                      "VSync",
		                      "Whether vertical sync is enabled",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, grl_window_properties);
}

static void
lrg_grl_window_init (LrgGrlWindow *self)
{
	self->grl_window = NULL;
	self->vsync = FALSE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_grl_window_new:
 * @width: initial window width
 * @height: initial window height
 * @title: window title
 *
 * Create a new graylib window.
 *
 * Returns: (transfer full): a new #LrgGrlWindow
 */
LrgGrlWindow *
lrg_grl_window_new (gint         width,
                    gint         height,
                    const gchar *title)
{
	return g_object_new (LRG_TYPE_GRL_WINDOW,
	                     "width", width,
	                     "height", height,
	                     "title", title,
	                     NULL);
}

/**
 * lrg_grl_window_toggle_fullscreen:
 * @self: an #LrgGrlWindow
 *
 * Toggle fullscreen mode.
 */
void
lrg_grl_window_toggle_fullscreen (LrgGrlWindow *self)
{
	g_return_if_fail (LRG_IS_GRL_WINDOW (self));

	grl_window_toggle_fullscreen (self->grl_window);
}

/**
 * lrg_grl_window_set_vsync:
 * @self: an #LrgGrlWindow
 * @vsync: whether to enable vsync
 *
 * Enable or disable vertical sync.
 */
void
lrg_grl_window_set_vsync (LrgGrlWindow *self,
                          gboolean      vsync)
{
	g_return_if_fail (LRG_IS_GRL_WINDOW (self));

	if (self->vsync == vsync)
		return;

	self->vsync = vsync;

	if (self->grl_window != NULL)
	{
		if (vsync)
			grl_window_set_state (self->grl_window, GRL_FLAG_VSYNC_HINT);
		else
			grl_window_clear_state (self->grl_window, GRL_FLAG_VSYNC_HINT);
	}

	g_object_notify_by_pspec (G_OBJECT (self), grl_window_properties[PROP_VSYNC]);
}

/**
 * lrg_grl_window_get_vsync:
 * @self: an #LrgGrlWindow
 *
 * Check if vsync is enabled.
 *
 * Returns: %TRUE if vsync is enabled
 */
gboolean
lrg_grl_window_get_vsync (LrgGrlWindow *self)
{
	g_return_val_if_fail (LRG_IS_GRL_WINDOW (self), FALSE);

	return self->vsync;
}

/**
 * lrg_grl_window_get_grl_window:
 * @self: an #LrgGrlWindow
 *
 * Get the underlying GrlWindow for advanced usage.
 *
 * Returns: (transfer none): the underlying #GrlWindow
 */
GrlWindow *
lrg_grl_window_get_grl_window (LrgGrlWindow *self)
{
	g_return_val_if_fail (LRG_IS_GRL_WINDOW (self), NULL);

	return self->grl_window;
}
