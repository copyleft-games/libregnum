/* lrg-window.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract window base class for different backends.
 */

#include "config.h"
#include "lrg-window.h"

/**
 * SECTION:lrg-window
 * @title: LrgWindow
 * @short_description: Abstract window base class
 *
 * #LrgWindow is an abstract base class for window implementations.
 * It defines the interface that all window backends must implement.
 *
 * The primary implementation is #LrgGrlWindow which wraps graylib's
 * window system. Other backends (like GTK for editor windows) can
 * be added by subclassing #LrgWindow.
 *
 * ## Properties
 *
 * - **title**: The window title (string)
 * - **width**: The window width in pixels (int, construct-only)
 * - **height**: The window height in pixels (int, construct-only)
 * - **target-fps**: The target frames per second (int)
 *
 * ## Signals
 *
 * - **resize**: Emitted when the window is resized
 * - **close-requested**: Emitted when the user requests to close
 * - **key-pressed**: Emitted when a key is pressed
 * - **key-released**: Emitted when a key is released
 * - **mouse-button-pressed**: Emitted when a mouse button is pressed
 * - **mouse-button-released**: Emitted when a mouse button is released
 * - **mouse-moved**: Emitted when the mouse moves
 */

typedef struct
{
	gchar *title;
	gint   width;
	gint   height;
	gint   target_fps;
} LrgWindowPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgWindow, lrg_window, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_TITLE,
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_TARGET_FPS,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_RESIZE,
	SIGNAL_CLOSE_REQUESTED,
	SIGNAL_KEY_PRESSED,
	SIGNAL_KEY_RELEASED,
	SIGNAL_MOUSE_BUTTON_PRESSED,
	SIGNAL_MOUSE_BUTTON_RELEASED,
	SIGNAL_MOUSE_MOVED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Virtual Method Defaults
 * ========================================================================== */

static void
lrg_window_default_show (LrgWindow *self)
{
	/* Default implementation does nothing */
}

static void
lrg_window_default_hide (LrgWindow *self)
{
	/* Default implementation does nothing */
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_window_finalize (GObject *object)
{
	LrgWindow        *self = LRG_WINDOW (object);
	LrgWindowPrivate *priv = lrg_window_get_instance_private (self);

	g_free (priv->title);

	G_OBJECT_CLASS (lrg_window_parent_class)->finalize (object);
}

static void
lrg_window_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	LrgWindow        *self = LRG_WINDOW (object);
	LrgWindowPrivate *priv = lrg_window_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_TITLE:
		g_value_set_string (value, priv->title);
		break;
	case PROP_WIDTH:
		g_value_set_int (value, priv->width);
		break;
	case PROP_HEIGHT:
		g_value_set_int (value, priv->height);
		break;
	case PROP_TARGET_FPS:
		g_value_set_int (value, priv->target_fps);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_window_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	LrgWindow        *self = LRG_WINDOW (object);
	LrgWindowPrivate *priv = lrg_window_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_TITLE:
		g_free (priv->title);
		priv->title = g_value_dup_string (value);
		break;
	case PROP_WIDTH:
		priv->width = g_value_get_int (value);
		break;
	case PROP_HEIGHT:
		priv->height = g_value_get_int (value);
		break;
	case PROP_TARGET_FPS:
		priv->target_fps = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_window_class_init (LrgWindowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_window_finalize;
	object_class->get_property = lrg_window_get_property;
	object_class->set_property = lrg_window_set_property;

	/* Default implementations for virtual methods */
	klass->show = lrg_window_default_show;
	klass->hide = lrg_window_default_hide;

	/* Properties */
	properties[PROP_TITLE] =
		g_param_spec_string ("title",
		                     "Title",
		                     "Window title",
		                     "Libregnum Window",
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

	properties[PROP_WIDTH] =
		g_param_spec_int ("width",
		                  "Width",
		                  "Window width in pixels",
		                  1, G_MAXINT, 800,
		                  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	properties[PROP_HEIGHT] =
		g_param_spec_int ("height",
		                  "Height",
		                  "Window height in pixels",
		                  1, G_MAXINT, 600,
		                  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	properties[PROP_TARGET_FPS] =
		g_param_spec_int ("target-fps",
		                  "Target FPS",
		                  "Target frames per second",
		                  0, G_MAXINT, 60,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/* Signals */

	/**
	 * LrgWindow::resize:
	 * @window: the window
	 * @width: new width
	 * @height: new height
	 *
	 * Emitted when the window is resized.
	 */
	signals[SIGNAL_RESIZE] =
		g_signal_new ("resize",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL, NULL,
		              G_TYPE_NONE, 2,
		              G_TYPE_INT, G_TYPE_INT);

	/**
	 * LrgWindow::close-requested:
	 * @window: the window
	 *
	 * Emitted when the user requests to close the window.
	 */
	signals[SIGNAL_CLOSE_REQUESTED] =
		g_signal_new ("close-requested",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL, NULL,
		              G_TYPE_NONE, 0);

	/**
	 * LrgWindow::key-pressed:
	 * @window: the window
	 * @key: the key code
	 *
	 * Emitted when a key is pressed.
	 */
	signals[SIGNAL_KEY_PRESSED] =
		g_signal_new ("key-pressed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL, NULL,
		              G_TYPE_NONE, 1,
		              G_TYPE_INT);

	/**
	 * LrgWindow::key-released:
	 * @window: the window
	 * @key: the key code
	 *
	 * Emitted when a key is released.
	 */
	signals[SIGNAL_KEY_RELEASED] =
		g_signal_new ("key-released",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL, NULL,
		              G_TYPE_NONE, 1,
		              G_TYPE_INT);

	/**
	 * LrgWindow::mouse-button-pressed:
	 * @window: the window
	 * @button: the mouse button
	 * @x: the x coordinate
	 * @y: the y coordinate
	 *
	 * Emitted when a mouse button is pressed.
	 */
	signals[SIGNAL_MOUSE_BUTTON_PRESSED] =
		g_signal_new ("mouse-button-pressed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL, NULL,
		              G_TYPE_NONE, 3,
		              G_TYPE_INT, G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgWindow::mouse-button-released:
	 * @window: the window
	 * @button: the mouse button
	 * @x: the x coordinate
	 * @y: the y coordinate
	 *
	 * Emitted when a mouse button is released.
	 */
	signals[SIGNAL_MOUSE_BUTTON_RELEASED] =
		g_signal_new ("mouse-button-released",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL, NULL,
		              G_TYPE_NONE, 3,
		              G_TYPE_INT, G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgWindow::mouse-moved:
	 * @window: the window
	 * @x: the x coordinate
	 * @y: the y coordinate
	 * @dx: the delta x
	 * @dy: the delta y
	 *
	 * Emitted when the mouse moves.
	 */
	signals[SIGNAL_MOUSE_MOVED] =
		g_signal_new ("mouse-moved",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL, NULL,
		              G_TYPE_NONE, 4,
		              G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT);
}

static void
lrg_window_init (LrgWindow *self)
{
	LrgWindowPrivate *priv = lrg_window_get_instance_private (self);

	priv->title = g_strdup ("Libregnum Window");
	priv->width = 800;
	priv->height = 600;
	priv->target_fps = 60;
}

/* ==========================================================================
 * Public API - Virtual Method Dispatchers
 * ========================================================================== */

void
lrg_window_begin_frame (LrgWindow *self)
{
	LrgWindowClass *klass;

	g_return_if_fail (LRG_IS_WINDOW (self));

	klass = LRG_WINDOW_GET_CLASS (self);
	g_return_if_fail (klass->begin_frame != NULL);

	klass->begin_frame (self);
}

void
lrg_window_end_frame (LrgWindow *self)
{
	LrgWindowClass *klass;

	g_return_if_fail (LRG_IS_WINDOW (self));

	klass = LRG_WINDOW_GET_CLASS (self);
	g_return_if_fail (klass->end_frame != NULL);

	klass->end_frame (self);
}

gboolean
lrg_window_should_close (LrgWindow *self)
{
	LrgWindowClass *klass;

	g_return_val_if_fail (LRG_IS_WINDOW (self), TRUE);

	klass = LRG_WINDOW_GET_CLASS (self);
	g_return_val_if_fail (klass->should_close != NULL, TRUE);

	return klass->should_close (self);
}

void
lrg_window_set_should_close (LrgWindow *self,
                             gboolean   close)
{
	LrgWindowClass *klass;

	g_return_if_fail (LRG_IS_WINDOW (self));

	klass = LRG_WINDOW_GET_CLASS (self);
	g_return_if_fail (klass->set_should_close != NULL);

	klass->set_should_close (self, close);
}

void
lrg_window_poll_input (LrgWindow *self)
{
	LrgWindowClass *klass;

	g_return_if_fail (LRG_IS_WINDOW (self));

	klass = LRG_WINDOW_GET_CLASS (self);
	g_return_if_fail (klass->poll_input != NULL);

	klass->poll_input (self);
}

gfloat
lrg_window_get_frame_time (LrgWindow *self)
{
	LrgWindowClass *klass;

	g_return_val_if_fail (LRG_IS_WINDOW (self), 0.0f);

	klass = LRG_WINDOW_GET_CLASS (self);
	g_return_val_if_fail (klass->get_frame_time != NULL, 0.0f);

	return klass->get_frame_time (self);
}

gint
lrg_window_get_fps (LrgWindow *self)
{
	LrgWindowClass *klass;

	g_return_val_if_fail (LRG_IS_WINDOW (self), 0);

	klass = LRG_WINDOW_GET_CLASS (self);
	g_return_val_if_fail (klass->get_fps != NULL, 0);

	return klass->get_fps (self);
}

void
lrg_window_clear (LrgWindow *self,
                  GrlColor  *color)
{
	LrgWindowClass *klass;

	g_return_if_fail (LRG_IS_WINDOW (self));
	g_return_if_fail (color != NULL);

	klass = LRG_WINDOW_GET_CLASS (self);
	g_return_if_fail (klass->clear != NULL);

	klass->clear (self, color);
}

void
lrg_window_show (LrgWindow *self)
{
	LrgWindowClass *klass;

	g_return_if_fail (LRG_IS_WINDOW (self));

	klass = LRG_WINDOW_GET_CLASS (self);
	if (klass->show != NULL)
		klass->show (self);
}

void
lrg_window_hide (LrgWindow *self)
{
	LrgWindowClass *klass;

	g_return_if_fail (LRG_IS_WINDOW (self));

	klass = LRG_WINDOW_GET_CLASS (self);
	if (klass->hide != NULL)
		klass->hide (self);
}

/* ==========================================================================
 * Public API - Property Accessors
 * ========================================================================== */

const gchar *
lrg_window_get_title (LrgWindow *self)
{
	LrgWindowPrivate *priv;

	g_return_val_if_fail (LRG_IS_WINDOW (self), NULL);

	priv = lrg_window_get_instance_private (self);
	return priv->title;
}

void
lrg_window_set_title (LrgWindow   *self,
                      const gchar *title)
{
	LrgWindowPrivate *priv;

	g_return_if_fail (LRG_IS_WINDOW (self));

	priv = lrg_window_get_instance_private (self);
	g_free (priv->title);
	priv->title = g_strdup (title);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

gint
lrg_window_get_width (LrgWindow *self)
{
	LrgWindowPrivate *priv;

	g_return_val_if_fail (LRG_IS_WINDOW (self), 0);

	priv = lrg_window_get_instance_private (self);
	return priv->width;
}

gint
lrg_window_get_height (LrgWindow *self)
{
	LrgWindowPrivate *priv;

	g_return_val_if_fail (LRG_IS_WINDOW (self), 0);

	priv = lrg_window_get_instance_private (self);
	return priv->height;
}

gint
lrg_window_get_target_fps (LrgWindow *self)
{
	LrgWindowPrivate *priv;

	g_return_val_if_fail (LRG_IS_WINDOW (self), 0);

	priv = lrg_window_get_instance_private (self);
	return priv->target_fps;
}

void
lrg_window_set_target_fps (LrgWindow *self,
                           gint       fps)
{
	LrgWindowPrivate *priv;

	g_return_if_fail (LRG_IS_WINDOW (self));
	g_return_if_fail (fps >= 0);

	priv = lrg_window_get_instance_private (self);
	priv->target_fps = fps;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_FPS]);
}
