/* lrg-nameplate.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNameplate: short and full names, a status dot, hover from the
 * pointer and a pill-shaped box.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-nameplate.h"
#include "../lrg-log.h"

#include <raylib.h>

struct _LrgNameplate
{
	LrgWorldWidget parent_instance;

	gchar    *text;
	gchar    *short_text;
	guint     max_chars;
	gboolean  hovered;
	gboolean  has_badge;
	GrlColor  badge;
	GrlColor  background;
	GrlColor  foreground;
	gfloat    font_size;
};

G_DEFINE_FINAL_TYPE (LrgNameplate, lrg_nameplate, LRG_TYPE_WORLD_WIDGET)

#define NAMEPLATE_PAD_X (6.0f)
#define NAMEPLATE_PAD_Y (3.0f)

static void
rebuild_short (LrgNameplate *self)
{
	glong n = g_utf8_strlen (self->text, -1);

	g_free (self->short_text);
	if (self->max_chars == 0 || n <= (glong) self->max_chars)
	{
		self->short_text = g_strdup (self->text);
		return;
	}
	/* keep room for the three dots; raylib's default font has no U+2026 */
	{
		glong keep = MAX ((glong) self->max_chars - 3, 1);
		const gchar *end = g_utf8_offset_to_pointer (self->text, keep);
		g_autofree gchar *head = g_strndup (self->text, end - self->text);

		self->short_text = g_strconcat (g_strchomp (head), "...", NULL);
	}
}

static gfloat
dot_space (LrgNameplate *self,
           gfloat        font)
{
	return self->has_badge ? font * 0.8f : 0.0f;
}

static void
measure_text (LrgNameplate *self,
              const gchar  *text,
              gfloat       *width,
              gfloat       *height)
{
	gfloat scale = lrg_world_widget_get_scale (LRG_WORLD_WIDGET (self));
	gfloat font = self->font_size * scale;

	*width = lrg_world_widget_measure_text (text, font) + dot_space (self, font) +
	         2.0f * NAMEPLATE_PAD_X * scale;
	*height = font + 2.0f * NAMEPLATE_PAD_Y * scale;
}

static void
lrg_nameplate_measure (LrgWorldWidget *widget,
                       gfloat         *width,
                       gfloat         *height)
{
	LrgNameplate *self = LRG_NAMEPLATE (widget);

	measure_text (self, lrg_nameplate_get_display_text (self), width, height);
}

static Color
with_alpha (const GrlColor *c,
            gfloat          alpha)
{
	Color out;

	out.r = c->r;
	out.g = c->g;
	out.b = c->b;
	out.a = (unsigned char) (c->a * CLAMP (alpha, 0.0f, 1.0f));
	return out;
}

static void
lrg_nameplate_draw_box (LrgWorldWidget *widget,
                        gfloat          x,
                        gfloat          y,
                        gfloat          anchor_x,
                        gfloat          anchor_y,
                        gfloat          alpha)
{
	LrgNameplate *self = LRG_NAMEPLATE (widget);
	gfloat scale = lrg_world_widget_get_scale (widget);
	gfloat font = self->font_size * scale;
	gfloat bg_alpha = alpha * (self->hovered ? 1.0f : 0.8f);
	Rectangle box;
	gfloat w;
	gfloat h;
	gfloat tx;

	lrg_world_widget_measure (widget, &w, &h);
	box.x = x;
	box.y = y;
	box.width = w;
	box.height = h;
	DrawRectangleRounded (box, 1.0f, 6, with_alpha (&self->background, bg_alpha));

	tx = x + NAMEPLATE_PAD_X * scale;
	if (self->has_badge)
	{
		Vector2 c = { tx + font * 0.3f, y + h * 0.5f };

		DrawCircleV (c, font * 0.28f, with_alpha (&self->badge, alpha));
		tx += dot_space (self, font);
	}
	DrawText (lrg_nameplate_get_display_text (self), (gint) tx,
	          (gint) (y + NAMEPLATE_PAD_Y * scale), (gint) (font + 0.5f),
	          with_alpha (&self->foreground, alpha));
}

static void
lrg_nameplate_finalize (GObject *object)
{
	LrgNameplate *self = LRG_NAMEPLATE (object);

	g_free (self->text);
	g_free (self->short_text);
	G_OBJECT_CLASS (lrg_nameplate_parent_class)->finalize (object);
}

static void
lrg_nameplate_class_init (LrgNameplateClass *klass)
{
	LrgWorldWidgetClass *widget_class = LRG_WORLD_WIDGET_CLASS (klass);

	G_OBJECT_CLASS (klass)->finalize = lrg_nameplate_finalize;
	widget_class->measure = lrg_nameplate_measure;
	widget_class->draw_box = lrg_nameplate_draw_box;
}

static void
lrg_nameplate_init (LrgNameplate *self)
{
	self->text = g_strdup ("");
	self->short_text = g_strdup ("");
	self->max_chars = 14;
	self->font_size = 12.0f;
	self->background = (GrlColor) { 20, 22, 30, 200 };
	self->foreground = (GrlColor) { 240, 240, 245, 255 };
	lrg_world_widget_set_offset (LRG_WORLD_WIDGET (self), 0.0f, 4.0f);
}

LrgNameplate *
lrg_nameplate_new (const gchar *text)
{
	LrgNameplate *self = g_object_new (LRG_TYPE_NAMEPLATE, NULL);

	lrg_nameplate_set_text (self, text);
	return self;
}

void
lrg_nameplate_set_text (LrgNameplate *self,
                        const gchar  *text)
{
	g_return_if_fail (LRG_IS_NAMEPLATE (self));

	g_free (self->text);
	self->text = g_strdup (text != NULL ? text : "");
	rebuild_short (self);
}

const gchar *
lrg_nameplate_get_text (LrgNameplate *self)
{
	g_return_val_if_fail (LRG_IS_NAMEPLATE (self), NULL);
	return self->text;
}

void
lrg_nameplate_set_max_chars (LrgNameplate *self,
                             guint         max_chars)
{
	g_return_if_fail (LRG_IS_NAMEPLATE (self));

	self->max_chars = max_chars;
	rebuild_short (self);
}

const gchar *
lrg_nameplate_get_display_text (LrgNameplate *self)
{
	g_return_val_if_fail (LRG_IS_NAMEPLATE (self), NULL);
	return self->hovered ? self->text : self->short_text;
}

void
lrg_nameplate_set_hovered (LrgNameplate *self,
                           gboolean      hovered)
{
	g_return_if_fail (LRG_IS_NAMEPLATE (self));
	self->hovered = hovered;
}

gboolean
lrg_nameplate_get_hovered (LrgNameplate *self)
{
	g_return_val_if_fail (LRG_IS_NAMEPLATE (self), FALSE);
	return self->hovered;
}

gboolean
lrg_nameplate_update_hover (LrgNameplate *self,
                            gfloat        pointer_x,
                            gfloat        pointer_y)
{
	GrlRectangle box;
	gfloat short_w;
	gfloat short_h;
	gfloat cx;

	g_return_val_if_fail (LRG_IS_NAMEPLATE (self), FALSE);

	if (!lrg_world_widget_get_last_box (LRG_WORLD_WIDGET (self), &box))
	{
		self->hovered = FALSE;
		return FALSE;
	}
	/* test the short box, centred where the drawn box is centred */
	measure_text (self, self->short_text, &short_w, &short_h);
	cx = box.x + box.width * 0.5f;
	self->hovered = pointer_x >= cx - short_w * 0.5f && pointer_x <= cx + short_w * 0.5f &&
	                pointer_y >= box.y + box.height - short_h &&
	                pointer_y <= box.y + box.height;
	return self->hovered;
}

void
lrg_nameplate_set_badge_color (LrgNameplate   *self,
                               const GrlColor *color)
{
	g_return_if_fail (LRG_IS_NAMEPLATE (self));

	self->has_badge = (color != NULL);
	if (color != NULL)
		self->badge = *color;
}

void
lrg_nameplate_set_style (LrgNameplate   *self,
                         const GrlColor *background,
                         const GrlColor *foreground,
                         gfloat          font_size)
{
	g_return_if_fail (LRG_IS_NAMEPLATE (self));
	g_return_if_fail (font_size > 0.0f);

	if (background != NULL)
		self->background = *background;
	if (foreground != NULL)
		self->foreground = *foreground;
	self->font_size = font_size;
}
