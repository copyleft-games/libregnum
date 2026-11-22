/* lrg-speech-bubble.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSpeechBubble: greedy word wrap at the measured width, a rounded
 * box with an outline and a tail down to the anchor.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-speech-bubble.h"
#include "../lrg-log.h"

#include <raylib.h>
#include <string.h>

struct _LrgSpeechBubble
{
	LrgWorldWidget parent_instance;

	gchar     *text;
	gfloat     max_width;
	gfloat     font_size;
	gfloat     padding;
	gfloat     tail_size;
	GrlColor   background;
	GrlColor   foreground;
	GrlColor   border;

	GPtrArray *lines;          /* gchar *, wrapped at layout_scale */
	gboolean   dirty;
	gfloat     layout_scale;
	gboolean   layout_with_gl; /* measured with MeasureText, not the estimate */
};

G_DEFINE_FINAL_TYPE (LrgSpeechBubble, lrg_speech_bubble, LRG_TYPE_WORLD_WIDGET)

static gfloat
line_height (gfloat font_size)
{
	return font_size * 1.25f;
}

/* Appends @piece to the wrapped lines, breaking it between characters. */
static void
break_long_word (GPtrArray   *lines,
                 GString     *line,
                 const gchar *word,
                 gfloat       max_width,
                 gfloat       font_size)
{
	const gchar *p = word;

	while (*p != '\0')
	{
		const gchar *next = g_utf8_next_char (p);
		g_autofree gchar *candidate = NULL;

		candidate = g_strdup_printf ("%s%.*s", line->str, (gint) (next - p), p);
		if (line->len > 0 && lrg_world_widget_measure_text (candidate, font_size) > max_width)
		{
			g_ptr_array_add (lines, g_strdup (line->str));
			g_string_truncate (line, 0);
			continue;
		}
		g_string_append_len (line, p, next - p);
		p = next;
	}
}

static void
relayout (LrgSpeechBubble *self)
{
	gfloat scale = lrg_world_widget_get_scale (LRG_WORLD_WIDGET (self));
	gfloat font = self->font_size * scale;
	gfloat max_width = self->max_width * scale;
	g_auto(GStrv) paragraphs = NULL;
	guint p;

	g_ptr_array_set_size (self->lines, 0);
	self->dirty = FALSE;
	self->layout_scale = scale;
	self->layout_with_gl = IsWindowReady ();

	if (self->text == NULL || *self->text == '\0')
		return;

	paragraphs = g_strsplit (self->text, "\n", -1);
	for (p = 0; paragraphs[p] != NULL; p++)
	{
		g_auto(GStrv) words = g_strsplit (paragraphs[p], " ", -1);
		g_autoptr(GString) line = g_string_new (NULL);
		guint w;

		for (w = 0; words[w] != NULL; w++)
		{
			g_autofree gchar *candidate = NULL;

			if (*words[w] == '\0')
				continue;
			candidate = (line->len > 0) ? g_strdup_printf ("%s %s", line->str, words[w])
			                            : g_strdup (words[w]);
			if (lrg_world_widget_measure_text (candidate, font) <= max_width)
			{
				g_string_assign (line, candidate);
				continue;
			}
			/* the word does not fit on this line: start a new one */
			if (line->len > 0)
			{
				g_ptr_array_add (self->lines, g_strdup (line->str));
				g_string_truncate (line, 0);
			}
			if (lrg_world_widget_measure_text (words[w], font) <= max_width)
				g_string_assign (line, words[w]);
			else
				break_long_word (self->lines, line, words[w], max_width, font);
		}
		/* an empty paragraph is an empty line, as typed */
		g_ptr_array_add (self->lines, g_strdup (line->str));
	}
}

static void
ensure_layout (LrgSpeechBubble *self)
{
	if (self->dirty ||
	    self->layout_scale != lrg_world_widget_get_scale (LRG_WORLD_WIDGET (self)) ||
	    self->layout_with_gl != (gboolean) IsWindowReady ())
		relayout (self);
}

static void
lrg_speech_bubble_measure (LrgWorldWidget *widget,
                           gfloat         *width,
                           gfloat         *height)
{
	LrgSpeechBubble *self = LRG_SPEECH_BUBBLE (widget);
	gfloat scale = lrg_world_widget_get_scale (widget);
	gfloat font = self->font_size * scale;
	gfloat text_w = 0.0f;
	guint i;

	ensure_layout (self);
	for (i = 0; i < self->lines->len; i++)
		text_w = MAX (text_w, lrg_world_widget_measure_text (g_ptr_array_index (self->lines, i), font));

	*width = text_w + 2.0f * self->padding * scale;
	*height = (gfloat) self->lines->len * line_height (font) + 2.0f * self->padding * scale +
	          self->tail_size * scale;
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
lrg_speech_bubble_draw_box (LrgWorldWidget *widget,
                            gfloat          x,
                            gfloat          y,
                            gfloat          anchor_x,
                            gfloat          anchor_y,
                            gfloat          alpha)
{
	LrgSpeechBubble *self = LRG_SPEECH_BUBBLE (widget);
	gfloat scale = lrg_world_widget_get_scale (widget);
	gfloat font = self->font_size * scale;
	gfloat tail = self->tail_size * scale;
	gfloat pad = self->padding * scale;
	gfloat w;
	gfloat h;
	Rectangle body;
	guint i;

	lrg_world_widget_measure (widget, &w, &h);
	body.x = x;
	body.y = y;
	body.width = w;
	body.height = h - tail;

	DrawRectangleRounded (body, 0.35f, 6, with_alpha (&self->background, alpha));
	DrawRectangleRoundedLinesEx (body, 0.35f, 6, 1.0f, with_alpha (&self->border, alpha));

	if (tail > 0.0f)
	{
		/* the tail leaves the box's bottom edge and points at the anchor */
		gfloat base_x = CLAMP (anchor_x, body.x + tail, body.x + body.width - tail);
		Vector2 a = { base_x - tail * 0.6f, body.y + body.height - 1.0f };
		Vector2 b = { base_x + tail * 0.6f, body.y + body.height - 1.0f };
		Vector2 tip = { base_x, body.y + body.height + tail };

		/* raylib wants counter-clockwise vertices */
		DrawTriangle (a, tip, b, with_alpha (&self->background, alpha));
		DrawLineEx (a, tip, 1.0f, with_alpha (&self->border, alpha));
		DrawLineEx (b, tip, 1.0f, with_alpha (&self->border, alpha));
	}

	for (i = 0; i < self->lines->len; i++)
		DrawText (g_ptr_array_index (self->lines, i), (gint) (x + pad),
		          (gint) (y + pad + (gfloat) i * line_height (font)),
		          (gint) (font + 0.5f), with_alpha (&self->foreground, alpha));
}

static void
lrg_speech_bubble_finalize (GObject *object)
{
	LrgSpeechBubble *self = LRG_SPEECH_BUBBLE (object);

	g_free (self->text);
	g_ptr_array_unref (self->lines);
	G_OBJECT_CLASS (lrg_speech_bubble_parent_class)->finalize (object);
}

static void
lrg_speech_bubble_class_init (LrgSpeechBubbleClass *klass)
{
	LrgWorldWidgetClass *widget_class = LRG_WORLD_WIDGET_CLASS (klass);

	G_OBJECT_CLASS (klass)->finalize = lrg_speech_bubble_finalize;
	widget_class->measure = lrg_speech_bubble_measure;
	widget_class->draw_box = lrg_speech_bubble_draw_box;
}

static void
lrg_speech_bubble_init (LrgSpeechBubble *self)
{
	self->text = g_strdup ("");
	self->max_width = 220.0f;
	self->font_size = 16.0f;
	self->padding = 8.0f;
	self->tail_size = 10.0f;
	self->background = (GrlColor) { 250, 248, 240, 235 };
	self->foreground = (GrlColor) { 30, 30, 36, 255 };
	self->border = (GrlColor) { 40, 40, 48, 200 };
	self->lines = g_ptr_array_new_with_free_func (g_free);
	self->dirty = TRUE;
}

LrgSpeechBubble *
lrg_speech_bubble_new (const gchar *text)
{
	LrgSpeechBubble *self = g_object_new (LRG_TYPE_SPEECH_BUBBLE, NULL);

	lrg_speech_bubble_set_text (self, text);
	return self;
}

void
lrg_speech_bubble_set_text (LrgSpeechBubble *self,
                            const gchar     *text)
{
	g_return_if_fail (LRG_IS_SPEECH_BUBBLE (self));

	g_free (self->text);
	self->text = g_strdup (text != NULL ? text : "");
	self->dirty = TRUE;
}

const gchar *
lrg_speech_bubble_get_text (LrgSpeechBubble *self)
{
	g_return_val_if_fail (LRG_IS_SPEECH_BUBBLE (self), NULL);
	return self->text;
}

void
lrg_speech_bubble_say (LrgSpeechBubble *self,
                       const gchar     *text,
                       gfloat           hold_seconds)
{
	g_return_if_fail (LRG_IS_SPEECH_BUBBLE (self));

	lrg_speech_bubble_set_text (self, text);
	lrg_world_widget_set_hold_time (LRG_WORLD_WIDGET (self), hold_seconds, 0.4f);
	lrg_world_widget_show (LRG_WORLD_WIDGET (self), 0.15f);
}

void
lrg_speech_bubble_set_max_width (LrgSpeechBubble *self,
                                 gfloat           width)
{
	g_return_if_fail (LRG_IS_SPEECH_BUBBLE (self));
	g_return_if_fail (width > 0.0f);

	self->max_width = width;
	self->dirty = TRUE;
}

gfloat
lrg_speech_bubble_get_max_width (LrgSpeechBubble *self)
{
	g_return_val_if_fail (LRG_IS_SPEECH_BUBBLE (self), 0.0f);
	return self->max_width;
}

void
lrg_speech_bubble_set_font_size (LrgSpeechBubble *self,
                                 gfloat           size)
{
	g_return_if_fail (LRG_IS_SPEECH_BUBBLE (self));
	g_return_if_fail (size > 0.0f);

	self->font_size = size;
	self->dirty = TRUE;
}

void
lrg_speech_bubble_set_style (LrgSpeechBubble *self,
                             const GrlColor  *background,
                             const GrlColor  *foreground,
                             const GrlColor  *border,
                             gfloat           padding,
                             gfloat           tail_size)
{
	g_return_if_fail (LRG_IS_SPEECH_BUBBLE (self));

	if (background != NULL)
		self->background = *background;
	if (foreground != NULL)
		self->foreground = *foreground;
	if (border != NULL)
		self->border = *border;
	self->padding = MAX (padding, 0.0f);
	self->tail_size = MAX (tail_size, 0.0f);
}

guint
lrg_speech_bubble_get_n_lines (LrgSpeechBubble *self)
{
	g_return_val_if_fail (LRG_IS_SPEECH_BUBBLE (self), 0);
	ensure_layout (self);
	return self->lines->len;
}

const gchar *
lrg_speech_bubble_get_line (LrgSpeechBubble *self,
                            guint            index)
{
	g_return_val_if_fail (LRG_IS_SPEECH_BUBBLE (self), NULL);
	ensure_layout (self);
	if (index >= self->lines->len)
		return NULL;
	return g_ptr_array_index (self->lines, index);
}
