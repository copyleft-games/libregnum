/* lrg-world-widget.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWorldWidget: anchor, offset, scale, fade and hold time, box
 * placement over 2D and 3D cameras, and hit testing of the last box.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-world-widget.h"
#include "../graphics/lrg-camera.h"
#include "../lrg-log.h"

#include <raylib.h>

typedef struct
{
	gfloat   anchor_x;
	gfloat   anchor_y;
	gfloat   anchor_z;
	gfloat   offset_x;
	gfloat   offset_y;
	gfloat   scale;

	gfloat   alpha;
	gfloat   fade_rate;     /* alpha per second, signed; 0 = steady */
	gboolean shown;

	gfloat   hold_time;
	gfloat   hold_fade;
	gfloat   held;

	gboolean     has_box;
	GrlRectangle last_box;
} LrgWorldWidgetPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgWorldWidget, lrg_world_widget, G_TYPE_OBJECT)

enum
{
	SIGNAL_HIDDEN,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_world_widget_real_update (LrgWorldWidget *self,
                              gfloat          delta)
{
	LrgWorldWidgetPrivate *priv = lrg_world_widget_get_instance_private (self);

	if (delta <= 0.0f)
		return;

	/* hold timer: counts while fully shown, then starts the fade-out */
	if (priv->shown && priv->hold_time > 0.0f && priv->fade_rate == 0.0f)
	{
		priv->held += delta;
		if (priv->held >= priv->hold_time)
			lrg_world_widget_hide (self, priv->hold_fade);
	}

	if (priv->fade_rate != 0.0f)
	{
		priv->alpha += priv->fade_rate * delta;
		if (priv->alpha >= 1.0f)
		{
			priv->alpha = 1.0f;
			priv->fade_rate = 0.0f;
		}
		else if (priv->alpha <= 0.0f)
		{
			priv->alpha = 0.0f;
			priv->fade_rate = 0.0f;
			priv->has_box = FALSE;
			g_signal_emit (self, signals[SIGNAL_HIDDEN], 0);
		}
	}
}

static void
lrg_world_widget_real_measure (LrgWorldWidget *self,
                               gfloat         *width,
                               gfloat         *height)
{
	*width = 0.0f;
	*height = 0.0f;
}

static void
lrg_world_widget_class_init (LrgWorldWidgetClass *klass)
{
	klass->update = lrg_world_widget_real_update;
	klass->measure = lrg_world_widget_real_measure;

	/**
	 * LrgWorldWidget::hidden:
	 * @self: the widget
	 *
	 * Emitted when a fade-out (or an instant hide) brings the alpha to 0.
	 *
	 * Since: 0.11.0
	 */
	signals[SIGNAL_HIDDEN] = g_signal_new ("hidden", G_TYPE_FROM_CLASS (klass),
	                                       G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                       G_TYPE_NONE, 0);
}

static void
lrg_world_widget_init (LrgWorldWidget *self)
{
	LrgWorldWidgetPrivate *priv = lrg_world_widget_get_instance_private (self);

	priv->scale = 1.0f;
	priv->offset_y = 8.0f;
	priv->alpha = 1.0f;
	priv->shown = TRUE;
	priv->hold_fade = 0.3f;
}

void
lrg_world_widget_set_anchor (LrgWorldWidget *self,
                             gfloat          x,
                             gfloat          y,
                             gfloat          z)
{
	LrgWorldWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	priv = lrg_world_widget_get_instance_private (self);
	priv->anchor_x = x;
	priv->anchor_y = y;
	priv->anchor_z = z;
}

void
lrg_world_widget_get_anchor (LrgWorldWidget *self,
                             gfloat         *x,
                             gfloat         *y,
                             gfloat         *z)
{
	LrgWorldWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	priv = lrg_world_widget_get_instance_private (self);
	if (x != NULL)
		*x = priv->anchor_x;
	if (y != NULL)
		*y = priv->anchor_y;
	if (z != NULL)
		*z = priv->anchor_z;
}

void
lrg_world_widget_set_offset (LrgWorldWidget *self,
                             gfloat          dx,
                             gfloat          dy)
{
	LrgWorldWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	priv = lrg_world_widget_get_instance_private (self);
	priv->offset_x = dx;
	priv->offset_y = dy;
}

void
lrg_world_widget_get_offset (LrgWorldWidget *self,
                             gfloat         *dx,
                             gfloat         *dy)
{
	LrgWorldWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	priv = lrg_world_widget_get_instance_private (self);
	if (dx != NULL)
		*dx = priv->offset_x;
	if (dy != NULL)
		*dy = priv->offset_y;
}

void
lrg_world_widget_set_scale (LrgWorldWidget *self,
                            gfloat          scale)
{
	LrgWorldWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	g_return_if_fail (scale > 0.0f);
	priv = lrg_world_widget_get_instance_private (self);
	priv->scale = scale;
}

gfloat
lrg_world_widget_get_scale (LrgWorldWidget *self)
{
	LrgWorldWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WORLD_WIDGET (self), 1.0f);
	priv = lrg_world_widget_get_instance_private (self);
	return priv->scale;
}

void
lrg_world_widget_show (LrgWorldWidget *self,
                       gfloat          fade_seconds)
{
	LrgWorldWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	priv = lrg_world_widget_get_instance_private (self);

	priv->shown = TRUE;
	priv->held = 0.0f;
	if (fade_seconds <= 0.0f)
	{
		priv->alpha = 1.0f;
		priv->fade_rate = 0.0f;
	}
	else
		priv->fade_rate = 1.0f / fade_seconds;
}

void
lrg_world_widget_hide (LrgWorldWidget *self,
                       gfloat          fade_seconds)
{
	LrgWorldWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	priv = lrg_world_widget_get_instance_private (self);

	priv->shown = FALSE;
	if (fade_seconds <= 0.0f || priv->alpha <= 0.0f)
	{
		gboolean was_visible = priv->alpha > 0.0f;

		priv->alpha = 0.0f;
		priv->fade_rate = 0.0f;
		priv->has_box = FALSE;
		if (was_visible)
			g_signal_emit (self, signals[SIGNAL_HIDDEN], 0);
	}
	else
		priv->fade_rate = -1.0f / fade_seconds;
}

gboolean
lrg_world_widget_is_shown (LrgWorldWidget *self)
{
	LrgWorldWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WORLD_WIDGET (self), FALSE);
	priv = lrg_world_widget_get_instance_private (self);
	return priv->shown;
}

gfloat
lrg_world_widget_get_alpha (LrgWorldWidget *self)
{
	LrgWorldWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WORLD_WIDGET (self), 0.0f);
	priv = lrg_world_widget_get_instance_private (self);
	return priv->alpha;
}

void
lrg_world_widget_set_hold_time (LrgWorldWidget *self,
                                gfloat          seconds,
                                gfloat          fade_out_seconds)
{
	LrgWorldWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	priv = lrg_world_widget_get_instance_private (self);
	priv->hold_time = MAX (seconds, 0.0f);
	priv->hold_fade = MAX (fade_out_seconds, 0.0f);
	priv->held = 0.0f;
}

void
lrg_world_widget_update (LrgWorldWidget *self,
                         gfloat          delta)
{
	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	LRG_WORLD_WIDGET_GET_CLASS (self)->update (self, delta);
}

void
lrg_world_widget_measure (LrgWorldWidget *self,
                          gfloat         *width,
                          gfloat         *height)
{
	gfloat w = 0.0f;
	gfloat h = 0.0f;

	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	LRG_WORLD_WIDGET_GET_CLASS (self)->measure (self, &w, &h);
	if (width != NULL)
		*width = w;
	if (height != NULL)
		*height = h;
}

void
lrg_world_widget_get_box_at (LrgWorldWidget *self,
                             gfloat          anchor_x,
                             gfloat          anchor_y,
                             GrlRectangle   *out_box)
{
	LrgWorldWidgetPrivate *priv;
	gfloat w;
	gfloat h;

	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	g_return_if_fail (out_box != NULL);
	priv = lrg_world_widget_get_instance_private (self);

	lrg_world_widget_measure (self, &w, &h);
	out_box->width = w;
	out_box->height = h;
	out_box->x = anchor_x + priv->offset_x * priv->scale - w * 0.5f;
	out_box->y = anchor_y - priv->offset_y * priv->scale - h;
}

void
lrg_world_widget_project_2d (LrgWorldWidget *self,
                             LrgCamera2D    *camera,
                             GrlVector2     *out_screen)
{
	LrgWorldWidgetPrivate *priv;
	GrlVector3 world;

	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	g_return_if_fail (LRG_IS_CAMERA2D (camera));
	g_return_if_fail (out_screen != NULL);
	priv = lrg_world_widget_get_instance_private (self);

	world.x = priv->anchor_x;
	world.y = priv->anchor_y;
	world.z = 0.0f;
	lrg_camera_world_to_screen (LRG_CAMERA (camera), &world, out_screen);
}

gboolean
lrg_world_widget_project_3d (LrgWorldWidget *self,
                             LrgCamera3D    *camera,
                             gint            viewport_width,
                             gint            viewport_height,
                             GrlVector2     *out_screen)
{
	LrgWorldWidgetPrivate *priv;
	GrlVector3 world;

	g_return_val_if_fail (LRG_IS_WORLD_WIDGET (self), FALSE);
	g_return_val_if_fail (LRG_IS_CAMERA3D (camera), FALSE);
	g_return_val_if_fail (out_screen != NULL, FALSE);
	priv = lrg_world_widget_get_instance_private (self);

	world.x = priv->anchor_x;
	world.y = priv->anchor_y;
	world.z = priv->anchor_z;
	return lrg_camera3d_project_point (camera, &world, viewport_width, viewport_height,
	                                   out_screen, NULL);
}

void
lrg_world_widget_draw_at (LrgWorldWidget *self,
                          gfloat          anchor_x,
                          gfloat          anchor_y)
{
	LrgWorldWidgetPrivate *priv;
	LrgWorldWidgetClass *klass;
	GrlRectangle box;

	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	priv = lrg_world_widget_get_instance_private (self);

	if (priv->alpha <= 0.0f)
	{
		priv->has_box = FALSE;
		return;
	}
	lrg_world_widget_get_box_at (self, anchor_x, anchor_y, &box);
	priv->last_box = box;
	priv->has_box = TRUE;

	klass = LRG_WORLD_WIDGET_GET_CLASS (self);
	if (klass->draw_box != NULL && IsWindowReady ())
		klass->draw_box (self, box.x, box.y, anchor_x, anchor_y, priv->alpha);
}

void
lrg_world_widget_draw_2d (LrgWorldWidget *self,
                          LrgCamera2D    *camera)
{
	GrlVector2 screen;

	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	lrg_world_widget_project_2d (self, camera, &screen);
	lrg_world_widget_draw_at (self, screen.x, screen.y);
}

void
lrg_world_widget_draw_3d (LrgWorldWidget *self,
                          LrgCamera3D    *camera,
                          gint            viewport_width,
                          gint            viewport_height)
{
	LrgWorldWidgetPrivate *priv;
	GrlVector2 screen;

	g_return_if_fail (LRG_IS_WORLD_WIDGET (self));
	priv = lrg_world_widget_get_instance_private (self);
	if (!lrg_world_widget_project_3d (self, camera, viewport_width, viewport_height, &screen))
	{
		priv->has_box = FALSE;
		return;
	}
	lrg_world_widget_draw_at (self, screen.x, screen.y);
}

gboolean
lrg_world_widget_get_last_box (LrgWorldWidget *self,
                               GrlRectangle   *out_box)
{
	LrgWorldWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WORLD_WIDGET (self), FALSE);
	g_return_val_if_fail (out_box != NULL, FALSE);
	priv = lrg_world_widget_get_instance_private (self);

	if (!priv->has_box)
		return FALSE;
	*out_box = priv->last_box;
	return TRUE;
}

gboolean
lrg_world_widget_contains_point (LrgWorldWidget *self,
                                 gfloat          x,
                                 gfloat          y)
{
	LrgWorldWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WORLD_WIDGET (self), FALSE);
	priv = lrg_world_widget_get_instance_private (self);

	if (!priv->has_box)
		return FALSE;
	return x >= priv->last_box.x && x <= priv->last_box.x + priv->last_box.width &&
	       y >= priv->last_box.y && y <= priv->last_box.y + priv->last_box.height;
}

gfloat
lrg_world_widget_measure_text (const gchar *text,
                               gfloat       font_size)
{
	if (text == NULL || *text == '\0')
		return 0.0f;
	if (IsWindowReady ())
		return (gfloat) MeasureText (text, (gint) (font_size + 0.5f));
	return (gfloat) g_utf8_strlen (text, -1) * font_size * 0.6f;
}
