/* lrg-emote.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEmote: glyph or texture icon, pop-in (ease-out-back) and a sine
 * bob, both advanced by update() so they are deterministic and testable.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-emote.h"
#include "../lrg-log.h"

#include <math.h>
#include <raylib.h>

#define EMOTE_POP_SECONDS (0.2f)

struct _LrgEmote
{
	LrgWorldWidget parent_instance;

	gchar        *glyph;
	GrlTexture   *texture;
	GrlRectangle  source;
	gfloat        size;
	gfloat        bob_amplitude;
	gfloat        bob_frequency;
	gfloat        time;
	gfloat        pop_time;      /* < EMOTE_POP_SECONDS while popping */
	GrlColor      background;
	GrlColor      foreground;
};

G_DEFINE_FINAL_TYPE (LrgEmote, lrg_emote, LRG_TYPE_WORLD_WIDGET)

static void
lrg_emote_measure (LrgWorldWidget *widget,
                   gfloat         *width,
                   gfloat         *height)
{
	LrgEmote *self = LRG_EMOTE (widget);
	gfloat d = self->size * lrg_world_widget_get_scale (widget) * lrg_emote_get_pop_scale (self);

	*width = d;
	*height = d;
}

static void
lrg_emote_update (LrgWorldWidget *widget,
                  gfloat          delta)
{
	LrgEmote *self = LRG_EMOTE (widget);

	LRG_WORLD_WIDGET_CLASS (lrg_emote_parent_class)->update (widget, delta);
	if (delta <= 0.0f)
		return;
	self->time += delta;
	/* keep the phase small so a long-lived emote does not lose precision */
	if (self->bob_frequency > 0.0f && self->time * self->bob_frequency > 1000.0f)
		self->time = fmodf (self->time, 1.0f / self->bob_frequency);
	if (self->pop_time < EMOTE_POP_SECONDS)
		self->pop_time = MIN (self->pop_time + delta, EMOTE_POP_SECONDS);
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
lrg_emote_draw_box (LrgWorldWidget *widget,
                    gfloat          x,
                    gfloat          y,
                    gfloat          anchor_x,
                    gfloat          anchor_y,
                    gfloat          alpha)
{
	LrgEmote *self = LRG_EMOTE (widget);
	gfloat d;
	gfloat bob = lrg_emote_get_bob_offset (self) * lrg_world_widget_get_scale (widget);
	Vector2 centre;

	lrg_world_widget_measure (widget, &d, NULL);
	centre.x = x + d * 0.5f;
	centre.y = y + d * 0.5f - bob;

	if (self->texture != NULL && grl_texture_is_valid (self->texture))
	{
		Texture2D *tex = grl_texture_get_handle (self->texture);
		Rectangle src = { self->source.x, self->source.y, self->source.width, self->source.height };
		Rectangle dst = { centre.x - d * 0.5f, centre.y - d * 0.5f, d, d };
		Vector2 origin = { 0.0f, 0.0f };

		DrawTexturePro (*tex, src, dst, origin, 0.0f, with_alpha (&self->foreground, alpha));
		return;
	}

	DrawCircleV (centre, d * 0.5f, with_alpha (&self->background, alpha));
	if (self->glyph != NULL && *self->glyph != '\0')
	{
		gfloat font = d * 0.6f;
		gfloat tw = lrg_world_widget_measure_text (self->glyph, font);

		DrawText (self->glyph, (gint) (centre.x - tw * 0.5f), (gint) (centre.y - font * 0.5f),
		          (gint) (font + 0.5f), with_alpha (&self->foreground, alpha));
	}
}

static void
lrg_emote_finalize (GObject *object)
{
	LrgEmote *self = LRG_EMOTE (object);

	g_free (self->glyph);
	g_clear_object (&self->texture);
	G_OBJECT_CLASS (lrg_emote_parent_class)->finalize (object);
}

static void
lrg_emote_class_init (LrgEmoteClass *klass)
{
	LrgWorldWidgetClass *widget_class = LRG_WORLD_WIDGET_CLASS (klass);

	G_OBJECT_CLASS (klass)->finalize = lrg_emote_finalize;
	widget_class->measure = lrg_emote_measure;
	widget_class->update = lrg_emote_update;
	widget_class->draw_box = lrg_emote_draw_box;
}

static void
lrg_emote_init (LrgEmote *self)
{
	self->size = 24.0f;
	self->bob_amplitude = 3.0f;
	self->bob_frequency = 1.5f;
	self->pop_time = EMOTE_POP_SECONDS;
	self->background = (GrlColor) { 255, 255, 255, 235 };
	self->foreground = (GrlColor) { 30, 30, 36, 255 };
}

LrgEmote *
lrg_emote_new_glyph (const gchar *glyph)
{
	LrgEmote *self = g_object_new (LRG_TYPE_EMOTE, NULL);

	lrg_emote_set_glyph (self, glyph);
	return self;
}

LrgEmote *
lrg_emote_new_icon (GrlTexture         *texture,
                    const GrlRectangle *source)
{
	LrgEmote *self;

	g_return_val_if_fail (GRL_IS_TEXTURE (texture), NULL);

	self = g_object_new (LRG_TYPE_EMOTE, NULL);
	self->texture = g_object_ref (texture);
	if (source != NULL)
		self->source = *source;
	else
	{
		self->source.x = 0.0f;
		self->source.y = 0.0f;
		self->source.width = (gfloat) grl_texture_get_width (texture);
		self->source.height = (gfloat) grl_texture_get_height (texture);
	}
	/* an icon draws untinted */
	self->foreground = (GrlColor) { 255, 255, 255, 255 };
	return self;
}

void
lrg_emote_set_glyph (LrgEmote    *self,
                     const gchar *glyph)
{
	g_return_if_fail (LRG_IS_EMOTE (self));

	g_free (self->glyph);
	self->glyph = g_strdup (glyph != NULL ? glyph : "");
	if (self->texture != NULL)
	{
		g_clear_object (&self->texture);
		self->foreground = (GrlColor) { 30, 30, 36, 255 };
	}
}

const gchar *
lrg_emote_get_glyph (LrgEmote *self)
{
	g_return_val_if_fail (LRG_IS_EMOTE (self), NULL);
	return (self->texture != NULL) ? NULL : self->glyph;
}

void
lrg_emote_set_size (LrgEmote *self,
                    gfloat    size)
{
	g_return_if_fail (LRG_IS_EMOTE (self));
	g_return_if_fail (size > 0.0f);
	self->size = size;
}

void
lrg_emote_set_bob (LrgEmote *self,
                   gfloat    amplitude,
                   gfloat    frequency)
{
	g_return_if_fail (LRG_IS_EMOTE (self));
	self->bob_amplitude = MAX (amplitude, 0.0f);
	self->bob_frequency = MAX (frequency, 0.0f);
}

gfloat
lrg_emote_get_bob_offset (LrgEmote *self)
{
	g_return_val_if_fail (LRG_IS_EMOTE (self), 0.0f);

	if (self->bob_amplitude <= 0.0f || self->bob_frequency <= 0.0f)
		return 0.0f;
	return self->bob_amplitude * sinf (2.0f * (gfloat) G_PI * self->bob_frequency * self->time);
}

gfloat
lrg_emote_get_pop_scale (LrgEmote *self)
{
	gfloat t;
	gfloat c1 = 1.70158f;
	gfloat c3 = c1 + 1.0f;
	gfloat back;

	g_return_val_if_fail (LRG_IS_EMOTE (self), 1.0f);

	if (self->pop_time >= EMOTE_POP_SECONDS)
		return 1.0f;
	/* ease-out-back from 0.5 to 1 */
	t = self->pop_time / EMOTE_POP_SECONDS - 1.0f;
	back = 1.0f + c3 * t * t * t + c1 * t * t;
	return 0.5f + 0.5f * back;
}

void
lrg_emote_pop (LrgEmote *self,
               gfloat    hold_seconds)
{
	g_return_if_fail (LRG_IS_EMOTE (self));

	self->pop_time = 0.0f;
	self->time = 0.0f;
	lrg_world_widget_set_hold_time (LRG_WORLD_WIDGET (self), hold_seconds, 0.3f);
	lrg_world_widget_show (LRG_WORLD_WIDGET (self), 0.08f);
}
