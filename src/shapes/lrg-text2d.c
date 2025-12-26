/* lrg-text2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 2D text shape.
 */

#include "lrg-text2d.h"

/**
 * LrgText2D:
 *
 * A 2D text shape.
 *
 * Renders text using graylib's text drawing functions.
 * Supports custom fonts, font size, and character spacing.
 */
struct _LrgText2D
{
	LrgShape2D parent_instance;

	gchar   *text;
	gfloat   font_size;
	gfloat   spacing;
	GrlFont *font;
};

G_DEFINE_FINAL_TYPE (LrgText2D, lrg_text2d, LRG_TYPE_SHAPE2D)

enum
{
	PROP_0,
	PROP_TEXT,
	PROP_FONT_SIZE,
	PROP_SPACING,
	PROP_FONT,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Virtual Method Implementation
 * ========================================================================== */

static void
lrg_text2d_draw (LrgShape *shape,
                 gfloat    delta)
{
	LrgText2D *self  = LRG_TEXT2D (shape);
	GrlColor  *color = lrg_shape_get_color (shape);
	gfloat     x     = lrg_shape2d_get_x (LRG_SHAPE2D (self));
	gfloat     y     = lrg_shape2d_get_y (LRG_SHAPE2D (self));

	if (self->text == NULL || self->text[0] == '\0')
		return;

	if (self->font != NULL)
	{
		g_autoptr(GrlVector2) position = grl_vector2_new (x, y);

		grl_draw_text_ex (self->font,
		                  self->text,
		                  position,
		                  self->font_size,
		                  self->spacing,
		                  color);
	}
	else
	{
		/* Use default font */
		grl_draw_text (self->text,
		               (gint) x,
		               (gint) y,
		               (gint) self->font_size,
		               color);
	}
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_text2d_finalize (GObject *object)
{
	LrgText2D *self = LRG_TEXT2D (object);

	g_clear_pointer (&self->text, g_free);
	g_clear_object (&self->font);

	G_OBJECT_CLASS (lrg_text2d_parent_class)->finalize (object);
}

static void
lrg_text2d_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	LrgText2D *self = LRG_TEXT2D (object);

	switch (prop_id)
	{
	case PROP_TEXT:
		g_value_set_string (value, self->text);
		break;
	case PROP_FONT_SIZE:
		g_value_set_float (value, self->font_size);
		break;
	case PROP_SPACING:
		g_value_set_float (value, self->spacing);
		break;
	case PROP_FONT:
		g_value_set_object (value, self->font);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_text2d_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	LrgText2D *self = LRG_TEXT2D (object);

	switch (prop_id)
	{
	case PROP_TEXT:
		g_clear_pointer (&self->text, g_free);
		self->text = g_value_dup_string (value);
		break;
	case PROP_FONT_SIZE:
		self->font_size = g_value_get_float (value);
		break;
	case PROP_SPACING:
		self->spacing = g_value_get_float (value);
		break;
	case PROP_FONT:
		g_clear_object (&self->font);
		self->font = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_text2d_class_init (LrgText2DClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	LrgShapeClass *shape_class  = LRG_SHAPE_CLASS (klass);

	object_class->finalize     = lrg_text2d_finalize;
	object_class->get_property = lrg_text2d_get_property;
	object_class->set_property = lrg_text2d_set_property;

	shape_class->draw = lrg_text2d_draw;

	/**
	 * LrgText2D:text:
	 *
	 * The text string to display.
	 */
	properties[PROP_TEXT] =
		g_param_spec_string ("text",
		                     "Text",
		                     "The text string to display",
		                     "",
		                     G_PARAM_READWRITE |
		                     G_PARAM_CONSTRUCT |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgText2D:font-size:
	 *
	 * The font size in pixels.
	 */
	properties[PROP_FONT_SIZE] =
		g_param_spec_float ("font-size",
		                    "Font Size",
		                    "The font size in pixels",
		                    1.0f, 1000.0f, 20.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgText2D:spacing:
	 *
	 * The character spacing.
	 */
	properties[PROP_SPACING] =
		g_param_spec_float ("spacing",
		                    "Spacing",
		                    "The character spacing",
		                    -100.0f, 100.0f, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgText2D:font:
	 *
	 * The font to use. If %NULL, the default font is used.
	 */
	properties[PROP_FONT] =
		g_param_spec_object ("font",
		                     "Font",
		                     "The font to use",
		                     GRL_TYPE_FONT,
		                     G_PARAM_READWRITE |
		                     G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_text2d_init (LrgText2D *self)
{
	self->text      = g_strdup ("");
	self->font_size = 20.0f;
	self->spacing   = 1.0f;
	self->font      = NULL;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_text2d_new:
 *
 * Creates a new empty text at the origin.
 *
 * Returns: (transfer full): A new #LrgText2D
 */
LrgText2D *
lrg_text2d_new (void)
{
	return g_object_new (LRG_TYPE_TEXT2D, NULL);
}

/**
 * lrg_text2d_new_with_text:
 * @text: the text to display
 *
 * Creates a new text shape at the origin with specified text.
 *
 * Returns: (transfer full): A new #LrgText2D
 */
LrgText2D *
lrg_text2d_new_with_text (const gchar *text)
{
	return g_object_new (LRG_TYPE_TEXT2D,
	                     "text", text,
	                     NULL);
}

/**
 * lrg_text2d_new_at:
 * @x: X position
 * @y: Y position
 * @text: the text to display
 *
 * Creates a new text at the specified position.
 *
 * Returns: (transfer full): A new #LrgText2D
 */
LrgText2D *
lrg_text2d_new_at (gfloat       x,
                   gfloat       y,
                   const gchar *text)
{
	return g_object_new (LRG_TYPE_TEXT2D,
	                     "x", x,
	                     "y", y,
	                     "text", text,
	                     NULL);
}

/**
 * lrg_text2d_new_full:
 * @x: X position
 * @y: Y position
 * @text: the text to display
 * @font_size: the font size
 * @color: (transfer none): the text color
 *
 * Creates a new text with full configuration.
 *
 * Returns: (transfer full): A new #LrgText2D
 */
LrgText2D *
lrg_text2d_new_full (gfloat       x,
                     gfloat       y,
                     const gchar *text,
                     gfloat       font_size,
                     GrlColor    *color)
{
	return g_object_new (LRG_TYPE_TEXT2D,
	                     "x", x,
	                     "y", y,
	                     "text", text,
	                     "font-size", font_size,
	                     "color", color,
	                     NULL);
}

/* Property accessors */

/**
 * lrg_text2d_get_text:
 * @self: an #LrgText2D
 *
 * Gets the text string.
 *
 * Returns: (transfer none): The text string
 */
const gchar *
lrg_text2d_get_text (LrgText2D *self)
{
	g_return_val_if_fail (LRG_IS_TEXT2D (self), NULL);

	return self->text;
}

/**
 * lrg_text2d_set_text:
 * @self: an #LrgText2D
 * @text: the text to set
 *
 * Sets the text string.
 */
void
lrg_text2d_set_text (LrgText2D   *self,
                     const gchar *text)
{
	g_return_if_fail (LRG_IS_TEXT2D (self));

	if (g_strcmp0 (self->text, text) != 0)
	{
		g_clear_pointer (&self->text, g_free);
		self->text = g_strdup (text);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
	}
}

/**
 * lrg_text2d_get_font_size:
 * @self: an #LrgText2D
 *
 * Gets the font size.
 *
 * Returns: The font size
 */
gfloat
lrg_text2d_get_font_size (LrgText2D *self)
{
	g_return_val_if_fail (LRG_IS_TEXT2D (self), 0.0f);

	return self->font_size;
}

/**
 * lrg_text2d_set_font_size:
 * @self: an #LrgText2D
 * @font_size: the font size
 *
 * Sets the font size.
 */
void
lrg_text2d_set_font_size (LrgText2D *self,
                          gfloat     font_size)
{
	g_return_if_fail (LRG_IS_TEXT2D (self));
	g_return_if_fail (font_size >= 1.0f);

	if (self->font_size != font_size)
	{
		self->font_size = font_size;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE]);
	}
}

/**
 * lrg_text2d_get_spacing:
 * @self: an #LrgText2D
 *
 * Gets the character spacing.
 *
 * Returns: The character spacing
 */
gfloat
lrg_text2d_get_spacing (LrgText2D *self)
{
	g_return_val_if_fail (LRG_IS_TEXT2D (self), 0.0f);

	return self->spacing;
}

/**
 * lrg_text2d_set_spacing:
 * @self: an #LrgText2D
 * @spacing: the character spacing
 *
 * Sets the character spacing.
 */
void
lrg_text2d_set_spacing (LrgText2D *self,
                        gfloat     spacing)
{
	g_return_if_fail (LRG_IS_TEXT2D (self));

	if (self->spacing != spacing)
	{
		self->spacing = spacing;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPACING]);
	}
}

/**
 * lrg_text2d_get_font:
 * @self: an #LrgText2D
 *
 * Gets the font.
 *
 * Returns: (transfer none) (nullable): The font, or %NULL for default
 */
GrlFont *
lrg_text2d_get_font (LrgText2D *self)
{
	g_return_val_if_fail (LRG_IS_TEXT2D (self), NULL);

	return self->font;
}

/**
 * lrg_text2d_set_font:
 * @self: an #LrgText2D
 * @font: (nullable) (transfer none): the font, or %NULL for default
 *
 * Sets the font.
 */
void
lrg_text2d_set_font (LrgText2D *self,
                     GrlFont   *font)
{
	g_return_if_fail (LRG_IS_TEXT2D (self));
	g_return_if_fail (font == NULL || GRL_IS_FONT (font));

	if (self->font != font)
	{
		g_clear_object (&self->font);
		if (font != NULL)
			self->font = g_object_ref (font);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT]);
	}
}
