/* lrg-input-prompt.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Input prompt widget for tutorial system.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TUTORIAL

#include "lrg-input-prompt.h"
#include "../lrg-log.h"

#include <math.h>

struct _LrgInputPrompt
{
    LrgWidget parent_instance;

    gchar *action_name;
    gchar *prompt_text;

    LrgInputDeviceType device_type;
    LrgGamepadStyle    gamepad_style;

    GrlFont *font;
    gfloat   font_size;
    GrlColor text_color;
    gfloat   glyph_size;

    /* Animation */
    gboolean animated;
    gfloat   animation_time;
};

G_DEFINE_TYPE (LrgInputPrompt, lrg_input_prompt, LRG_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_ACTION_NAME,
    PROP_PROMPT_TEXT,
    PROP_DEVICE_TYPE,
    PROP_GAMEPAD_STYLE,
    PROP_FONT,
    PROP_FONT_SIZE,
    PROP_TEXT_COLOR,
    PROP_GLYPH_SIZE,
    PROP_ANIMATED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Default white color */
static const GrlColor DEFAULT_TEXT_COLOR = { 255, 255, 255, 255 };

/*
 * get_keyboard_key_text:
 * @action_name: The input action name
 *
 * Maps an action name to keyboard key text.
 * In a real implementation, this would query the input manager.
 *
 * Returns: Key text string
 */
static const gchar *
get_keyboard_key_text (const gchar *action_name)
{
    /* Simplified mapping for common actions */
    if (g_str_equal (action_name, "confirm") || g_str_equal (action_name, "accept"))
        return "Enter";
    if (g_str_equal (action_name, "cancel") || g_str_equal (action_name, "back"))
        return "Esc";
    if (g_str_equal (action_name, "jump"))
        return "Space";
    if (g_str_equal (action_name, "attack"))
        return "Z";
    if (g_str_equal (action_name, "interact"))
        return "E";
    if (g_str_equal (action_name, "move_up"))
        return "W";
    if (g_str_equal (action_name, "move_down"))
        return "S";
    if (g_str_equal (action_name, "move_left"))
        return "A";
    if (g_str_equal (action_name, "move_right"))
        return "D";
    if (g_str_equal (action_name, "pause") || g_str_equal (action_name, "menu"))
        return "Esc";
    if (g_str_equal (action_name, "inventory"))
        return "I";

    /* Default: return the action name itself */
    return action_name;
}

/*
 * get_gamepad_button_text:
 * @action_name: The input action name
 * @style: The gamepad style
 *
 * Maps an action name to gamepad button text.
 *
 * Returns: Button text string
 */
static const gchar *
get_gamepad_button_text (const gchar    *action_name,
                         LrgGamepadStyle style)
{
    /* Determine which face button to show based on action */
    gboolean is_confirm = g_str_equal (action_name, "confirm") ||
                          g_str_equal (action_name, "accept") ||
                          g_str_equal (action_name, "interact");
    gboolean is_cancel = g_str_equal (action_name, "cancel") ||
                         g_str_equal (action_name, "back");
    gboolean is_jump = g_str_equal (action_name, "jump");
    gboolean is_attack = g_str_equal (action_name, "attack");

    switch (style)
    {
    case LRG_GAMEPAD_STYLE_XBOX:
        if (is_confirm) return "A";
        if (is_cancel) return "B";
        if (is_jump) return "A";
        if (is_attack) return "X";
        if (g_str_equal (action_name, "pause")) return "Menu";
        return "A";

    case LRG_GAMEPAD_STYLE_PLAYSTATION:
        if (is_confirm) return "X";   /* Cross */
        if (is_cancel) return "O";    /* Circle */
        if (is_jump) return "X";
        if (is_attack) return "[]";   /* Square */
        if (g_str_equal (action_name, "pause")) return "Options";
        return "X";

    case LRG_GAMEPAD_STYLE_NINTENDO:
        /* Nintendo has confirm/cancel swapped from Xbox */
        if (is_confirm) return "A";
        if (is_cancel) return "B";
        if (is_jump) return "B";
        if (is_attack) return "Y";
        if (g_str_equal (action_name, "pause")) return "+";
        return "A";

    case LRG_GAMEPAD_STYLE_GENERIC:
    default:
        if (is_confirm) return "1";
        if (is_cancel) return "2";
        if (is_jump) return "1";
        if (is_attack) return "3";
        return "1";
    }
}

static void
lrg_input_prompt_draw (LrgWidget *widget)
{
    LrgInputPrompt       *self = LRG_INPUT_PROMPT (widget);
    g_autoptr(GrlVector2) pos = NULL;
    const gchar          *key_text;
    gfloat                world_x, world_y;
    gfloat                offset_x;
    gfloat                scale;
    GrlColor              draw_color;

    if (self->action_name == NULL)
        return;

    world_x = lrg_widget_get_world_x (widget);
    world_y = lrg_widget_get_world_y (widget);

    /* Get the key/button text for this action */
    if (self->device_type == LRG_INPUT_DEVICE_GAMEPAD)
    {
        key_text = get_gamepad_button_text (self->action_name, self->gamepad_style);
    }
    else
    {
        key_text = get_keyboard_key_text (self->action_name);
    }

    /* Apply animation (subtle scale pulse) */
    scale = 1.0f;
    if (self->animated)
    {
        scale = 1.0f + sinf (self->animation_time * 3.0f) * 0.05f;
    }

    draw_color = self->text_color;
    offset_x = 0;

    /* Draw the key/button glyph with background */
    {
        g_autoptr(GrlRectangle) glyph_rect = NULL;
        g_autoptr(GrlVector2) text_size = NULL;
        GrlColor glyph_bg = { 60, 60, 60, 220 };
        GrlColor glyph_border = { 150, 150, 150, 255 };
        gfloat glyph_w, glyph_h;
        gfloat padding = 6.0f;
        gfloat text_x, text_y;

        /* Measure the key text */
        if (self->font != NULL)
        {
            text_size = grl_font_measure_text (self->font, key_text,
                                               self->glyph_size * scale, 1.0f);
        }
        else
        {
            text_size = grl_vector2_new ((gfloat)g_utf8_strlen (key_text, -1) *
                                          (self->glyph_size * scale * 0.6f),
                                         self->glyph_size * scale);
        }

        glyph_w = text_size->x + (padding * 2.0f);
        glyph_h = self->glyph_size * scale + (padding * 2.0f);

        /* Draw rounded background */
        glyph_rect = grl_rectangle_new (world_x, world_y, glyph_w, glyph_h);
        grl_draw_rectangle_rounded (glyph_rect, 0.3f, 8, &glyph_bg);
        grl_draw_rectangle_rounded_lines_ex (glyph_rect, 0.3f, 8, 2.0f, &glyph_border);

        /* Draw the key text centered */
        text_x = world_x + padding + (glyph_w - padding * 2.0f - text_size->x) / 2.0f;
        text_y = world_y + padding;

        if (self->font != NULL)
        {
            pos = grl_vector2_new (text_x, text_y);
            grl_draw_text_ex (self->font, key_text, pos,
                              self->glyph_size * scale, 1.0f, &draw_color);
        }
        else
        {
            grl_draw_text (key_text, (gint)text_x, (gint)text_y,
                           (gint)(self->glyph_size * scale), &draw_color);
        }

        offset_x = glyph_w + 8.0f;
    }

    /* Draw optional prompt text */
    if (self->prompt_text != NULL && self->prompt_text[0] != '\0')
    {
        gfloat text_y = world_y + (self->glyph_size - self->font_size) / 2.0f + 6.0f;

        if (self->font != NULL)
        {
            pos = grl_vector2_new (world_x + offset_x, text_y);
            grl_draw_text_ex (self->font, self->prompt_text, pos,
                              self->font_size, 1.0f, &draw_color);
        }
        else
        {
            grl_draw_text (self->prompt_text, (gint)(world_x + offset_x),
                           (gint)text_y, (gint)self->font_size, &draw_color);
        }
    }
}

static void
lrg_input_prompt_measure (LrgWidget *widget,
                          gfloat    *preferred_width,
                          gfloat    *preferred_height)
{
    LrgInputPrompt *self = LRG_INPUT_PROMPT (widget);
    gfloat total_width = 0;
    gfloat padding = 6.0f;

    /* Estimate glyph box width */
    total_width = self->glyph_size + (padding * 2.0f);

    /* Add prompt text width */
    if (self->prompt_text != NULL && self->prompt_text[0] != '\0')
    {
        if (self->font != NULL)
        {
            g_autoptr(GrlVector2) text_size = NULL;
            text_size = grl_font_measure_text (self->font, self->prompt_text,
                                               self->font_size, 1.0f);
            total_width += 8.0f + text_size->x;
        }
        else
        {
            total_width += 8.0f + ((gfloat)g_utf8_strlen (self->prompt_text, -1) *
                                   self->font_size * 0.6f);
        }
    }

    if (preferred_width != NULL)
        *preferred_width = total_width;
    if (preferred_height != NULL)
        *preferred_height = self->glyph_size + (padding * 2.0f);
}

static void
lrg_input_prompt_finalize (GObject *object)
{
    LrgInputPrompt *self = LRG_INPUT_PROMPT (object);

    g_clear_pointer (&self->action_name, g_free);
    g_clear_pointer (&self->prompt_text, g_free);
    g_clear_object (&self->font);

    G_OBJECT_CLASS (lrg_input_prompt_parent_class)->finalize (object);
}

static void
lrg_input_prompt_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgInputPrompt *self = LRG_INPUT_PROMPT (object);

    switch (prop_id)
    {
    case PROP_ACTION_NAME:
        g_value_set_string (value, self->action_name);
        break;
    case PROP_PROMPT_TEXT:
        g_value_set_string (value, self->prompt_text);
        break;
    case PROP_DEVICE_TYPE:
        g_value_set_enum (value, self->device_type);
        break;
    case PROP_GAMEPAD_STYLE:
        g_value_set_enum (value, self->gamepad_style);
        break;
    case PROP_FONT:
        g_value_set_object (value, self->font);
        break;
    case PROP_FONT_SIZE:
        g_value_set_float (value, self->font_size);
        break;
    case PROP_TEXT_COLOR:
        g_value_set_boxed (value, &self->text_color);
        break;
    case PROP_GLYPH_SIZE:
        g_value_set_float (value, self->glyph_size);
        break;
    case PROP_ANIMATED:
        g_value_set_boolean (value, self->animated);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_input_prompt_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgInputPrompt *self = LRG_INPUT_PROMPT (object);

    switch (prop_id)
    {
    case PROP_ACTION_NAME:
        lrg_input_prompt_set_action_name (self, g_value_get_string (value));
        break;
    case PROP_PROMPT_TEXT:
        lrg_input_prompt_set_prompt_text (self, g_value_get_string (value));
        break;
    case PROP_DEVICE_TYPE:
        lrg_input_prompt_set_device_type (self, g_value_get_enum (value));
        break;
    case PROP_GAMEPAD_STYLE:
        lrg_input_prompt_set_gamepad_style (self, g_value_get_enum (value));
        break;
    case PROP_FONT:
        lrg_input_prompt_set_font (self, g_value_get_object (value));
        break;
    case PROP_FONT_SIZE:
        lrg_input_prompt_set_font_size (self, g_value_get_float (value));
        break;
    case PROP_TEXT_COLOR:
        lrg_input_prompt_set_text_color (self, g_value_get_boxed (value));
        break;
    case PROP_GLYPH_SIZE:
        lrg_input_prompt_set_glyph_size (self, g_value_get_float (value));
        break;
    case PROP_ANIMATED:
        lrg_input_prompt_set_animated (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_input_prompt_class_init (LrgInputPromptClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->finalize = lrg_input_prompt_finalize;
    object_class->get_property = lrg_input_prompt_get_property;
    object_class->set_property = lrg_input_prompt_set_property;

    widget_class->draw = lrg_input_prompt_draw;
    widget_class->measure = lrg_input_prompt_measure;

    /**
     * LrgInputPrompt:action-name:
     *
     * The input action name to display.
     *
     * Since: 1.0
     */
    properties[PROP_ACTION_NAME] =
        g_param_spec_string ("action-name", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgInputPrompt:prompt-text:
     *
     * Optional prompt text to display with the input glyph.
     *
     * Since: 1.0
     */
    properties[PROP_PROMPT_TEXT] =
        g_param_spec_string ("prompt-text", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgInputPrompt:device-type:
     *
     * The input device type to display glyphs for.
     *
     * Since: 1.0
     */
    properties[PROP_DEVICE_TYPE] =
        g_param_spec_enum ("device-type", NULL, NULL,
                           LRG_TYPE_INPUT_DEVICE_TYPE,
                           LRG_INPUT_DEVICE_KEYBOARD,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgInputPrompt:gamepad-style:
     *
     * The gamepad button style for glyphs.
     *
     * Since: 1.0
     */
    properties[PROP_GAMEPAD_STYLE] =
        g_param_spec_enum ("gamepad-style", NULL, NULL,
                           LRG_TYPE_GAMEPAD_STYLE,
                           LRG_GAMEPAD_STYLE_XBOX,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgInputPrompt:font:
     *
     * The font for text rendering.
     *
     * Since: 1.0
     */
    properties[PROP_FONT] =
        g_param_spec_object ("font", NULL, NULL,
                             GRL_TYPE_FONT,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgInputPrompt:font-size:
     *
     * The font size in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_FONT_SIZE] =
        g_param_spec_float ("font-size", NULL, NULL,
                            8.0f, 128.0f, 20.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgInputPrompt:text-color:
     *
     * The text color.
     *
     * Since: 1.0
     */
    properties[PROP_TEXT_COLOR] =
        g_param_spec_boxed ("text-color", NULL, NULL,
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgInputPrompt:glyph-size:
     *
     * The input glyph size in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_GLYPH_SIZE] =
        g_param_spec_float ("glyph-size", NULL, NULL,
                            12.0f, 128.0f, 24.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgInputPrompt:animated:
     *
     * Whether the prompt is animated.
     *
     * Since: 1.0
     */
    properties[PROP_ANIMATED] =
        g_param_spec_boolean ("animated", NULL, NULL,
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_input_prompt_init (LrgInputPrompt *self)
{
    self->action_name = NULL;
    self->prompt_text = NULL;
    self->device_type = LRG_INPUT_DEVICE_KEYBOARD;
    self->gamepad_style = LRG_GAMEPAD_STYLE_XBOX;
    self->font = NULL;
    self->font_size = 20.0f;
    self->text_color = DEFAULT_TEXT_COLOR;
    self->glyph_size = 24.0f;
    self->animated = TRUE;
    self->animation_time = 0.0f;
}

/**
 * lrg_input_prompt_new:
 *
 * Creates a new input prompt widget.
 *
 * Returns: (transfer full): A new #LrgInputPrompt
 *
 * Since: 1.0
 */
LrgInputPrompt *
lrg_input_prompt_new (void)
{
    return g_object_new (LRG_TYPE_INPUT_PROMPT, NULL);
}

/**
 * lrg_input_prompt_new_with_action:
 * @action_name: The input action to display
 *
 * Creates a new input prompt widget for the specified action.
 *
 * Returns: (transfer full): A new #LrgInputPrompt
 *
 * Since: 1.0
 */
LrgInputPrompt *
lrg_input_prompt_new_with_action (const gchar *action_name)
{
    return g_object_new (LRG_TYPE_INPUT_PROMPT,
                         "action-name", action_name,
                         NULL);
}

/**
 * lrg_input_prompt_get_action_name:
 * @self: An #LrgInputPrompt
 *
 * Gets the input action name being displayed.
 *
 * Returns: (transfer none) (nullable): The action name
 *
 * Since: 1.0
 */
const gchar *
lrg_input_prompt_get_action_name (LrgInputPrompt *self)
{
    g_return_val_if_fail (LRG_IS_INPUT_PROMPT (self), NULL);
    return self->action_name;
}

/**
 * lrg_input_prompt_set_action_name:
 * @self: An #LrgInputPrompt
 * @action_name: (nullable): The action name to display
 *
 * Sets the input action name to display.
 *
 * Since: 1.0
 */
void
lrg_input_prompt_set_action_name (LrgInputPrompt *self,
                                  const gchar    *action_name)
{
    g_return_if_fail (LRG_IS_INPUT_PROMPT (self));

    if (g_strcmp0 (self->action_name, action_name) != 0)
    {
        g_free (self->action_name);
        self->action_name = g_strdup (action_name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTION_NAME]);
    }
}

/**
 * lrg_input_prompt_get_prompt_text:
 * @self: An #LrgInputPrompt
 *
 * Gets the optional prompt text.
 *
 * Returns: (transfer none) (nullable): The prompt text
 *
 * Since: 1.0
 */
const gchar *
lrg_input_prompt_get_prompt_text (LrgInputPrompt *self)
{
    g_return_val_if_fail (LRG_IS_INPUT_PROMPT (self), NULL);
    return self->prompt_text;
}

/**
 * lrg_input_prompt_set_prompt_text:
 * @self: An #LrgInputPrompt
 * @text: (nullable): The prompt text
 *
 * Sets the optional prompt text.
 *
 * Since: 1.0
 */
void
lrg_input_prompt_set_prompt_text (LrgInputPrompt *self,
                                  const gchar    *text)
{
    g_return_if_fail (LRG_IS_INPUT_PROMPT (self));

    if (g_strcmp0 (self->prompt_text, text) != 0)
    {
        g_free (self->prompt_text);
        self->prompt_text = g_strdup (text);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROMPT_TEXT]);
    }
}

/**
 * lrg_input_prompt_get_device_type:
 * @self: An #LrgInputPrompt
 *
 * Gets the current input device type.
 *
 * Returns: The device type
 *
 * Since: 1.0
 */
LrgInputDeviceType
lrg_input_prompt_get_device_type (LrgInputPrompt *self)
{
    g_return_val_if_fail (LRG_IS_INPUT_PROMPT (self), LRG_INPUT_DEVICE_KEYBOARD);
    return self->device_type;
}

/**
 * lrg_input_prompt_set_device_type:
 * @self: An #LrgInputPrompt
 * @device_type: The device type to display glyphs for
 *
 * Sets the input device type.
 *
 * Since: 1.0
 */
void
lrg_input_prompt_set_device_type (LrgInputPrompt    *self,
                                  LrgInputDeviceType device_type)
{
    g_return_if_fail (LRG_IS_INPUT_PROMPT (self));

    if (self->device_type != device_type)
    {
        self->device_type = device_type;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEVICE_TYPE]);
    }
}

/**
 * lrg_input_prompt_get_gamepad_style:
 * @self: An #LrgInputPrompt
 *
 * Gets the gamepad button style.
 *
 * Returns: The gamepad style
 *
 * Since: 1.0
 */
LrgGamepadStyle
lrg_input_prompt_get_gamepad_style (LrgInputPrompt *self)
{
    g_return_val_if_fail (LRG_IS_INPUT_PROMPT (self), LRG_GAMEPAD_STYLE_XBOX);
    return self->gamepad_style;
}

/**
 * lrg_input_prompt_set_gamepad_style:
 * @self: An #LrgInputPrompt
 * @style: The gamepad style
 *
 * Sets the gamepad button style.
 *
 * Since: 1.0
 */
void
lrg_input_prompt_set_gamepad_style (LrgInputPrompt *self,
                                    LrgGamepadStyle style)
{
    g_return_if_fail (LRG_IS_INPUT_PROMPT (self));

    if (self->gamepad_style != style)
    {
        self->gamepad_style = style;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GAMEPAD_STYLE]);
    }
}

/**
 * lrg_input_prompt_get_font:
 * @self: An #LrgInputPrompt
 *
 * Gets the font used for text.
 *
 * Returns: (transfer none) (nullable): The font
 *
 * Since: 1.0
 */
GrlFont *
lrg_input_prompt_get_font (LrgInputPrompt *self)
{
    g_return_val_if_fail (LRG_IS_INPUT_PROMPT (self), NULL);
    return self->font;
}

/**
 * lrg_input_prompt_set_font:
 * @self: An #LrgInputPrompt
 * @font: (nullable): The font to use
 *
 * Sets the font for text display.
 *
 * Since: 1.0
 */
void
lrg_input_prompt_set_font (LrgInputPrompt *self,
                           GrlFont        *font)
{
    g_return_if_fail (LRG_IS_INPUT_PROMPT (self));

    if (g_set_object (&self->font, font))
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT]);
    }
}

/**
 * lrg_input_prompt_get_font_size:
 * @self: An #LrgInputPrompt
 *
 * Gets the font size.
 *
 * Returns: The font size in pixels
 *
 * Since: 1.0
 */
gfloat
lrg_input_prompt_get_font_size (LrgInputPrompt *self)
{
    g_return_val_if_fail (LRG_IS_INPUT_PROMPT (self), 20.0f);
    return self->font_size;
}

/**
 * lrg_input_prompt_set_font_size:
 * @self: An #LrgInputPrompt
 * @size: The font size in pixels
 *
 * Sets the font size.
 *
 * Since: 1.0
 */
void
lrg_input_prompt_set_font_size (LrgInputPrompt *self,
                                gfloat          size)
{
    g_return_if_fail (LRG_IS_INPUT_PROMPT (self));

    if (self->font_size != size)
    {
        self->font_size = size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE]);
    }
}

/**
 * lrg_input_prompt_get_text_color:
 * @self: An #LrgInputPrompt
 *
 * Gets the text color.
 *
 * Returns: (transfer none): The color
 *
 * Since: 1.0
 */
const GrlColor *
lrg_input_prompt_get_text_color (LrgInputPrompt *self)
{
    g_return_val_if_fail (LRG_IS_INPUT_PROMPT (self), &DEFAULT_TEXT_COLOR);
    return &self->text_color;
}

/**
 * lrg_input_prompt_set_text_color:
 * @self: An #LrgInputPrompt
 * @color: The text color
 *
 * Sets the text color.
 *
 * Since: 1.0
 */
void
lrg_input_prompt_set_text_color (LrgInputPrompt *self,
                                 const GrlColor *color)
{
    g_return_if_fail (LRG_IS_INPUT_PROMPT (self));
    g_return_if_fail (color != NULL);

    if (self->text_color.r != color->r ||
        self->text_color.g != color->g ||
        self->text_color.b != color->b ||
        self->text_color.a != color->a)
    {
        self->text_color = *color;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_COLOR]);
    }
}

/**
 * lrg_input_prompt_get_glyph_size:
 * @self: An #LrgInputPrompt
 *
 * Gets the input glyph size.
 *
 * Returns: The glyph size in pixels
 *
 * Since: 1.0
 */
gfloat
lrg_input_prompt_get_glyph_size (LrgInputPrompt *self)
{
    g_return_val_if_fail (LRG_IS_INPUT_PROMPT (self), 24.0f);
    return self->glyph_size;
}

/**
 * lrg_input_prompt_set_glyph_size:
 * @self: An #LrgInputPrompt
 * @size: The glyph size in pixels
 *
 * Sets the input glyph size.
 *
 * Since: 1.0
 */
void
lrg_input_prompt_set_glyph_size (LrgInputPrompt *self,
                                 gfloat          size)
{
    g_return_if_fail (LRG_IS_INPUT_PROMPT (self));

    if (self->glyph_size != size)
    {
        self->glyph_size = size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GLYPH_SIZE]);
    }
}

/**
 * lrg_input_prompt_get_animated:
 * @self: An #LrgInputPrompt
 *
 * Gets whether the prompt is animated.
 *
 * Returns: %TRUE if animated
 *
 * Since: 1.0
 */
gboolean
lrg_input_prompt_get_animated (LrgInputPrompt *self)
{
    g_return_val_if_fail (LRG_IS_INPUT_PROMPT (self), TRUE);
    return self->animated;
}

/**
 * lrg_input_prompt_set_animated:
 * @self: An #LrgInputPrompt
 * @animated: Whether to animate
 *
 * Sets whether the prompt should animate.
 *
 * Since: 1.0
 */
void
lrg_input_prompt_set_animated (LrgInputPrompt *self,
                               gboolean        animated)
{
    g_return_if_fail (LRG_IS_INPUT_PROMPT (self));

    if (self->animated != animated)
    {
        self->animated = animated;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATED]);
    }
}

/**
 * lrg_input_prompt_update:
 * @self: An #LrgInputPrompt
 * @delta_time: Time since last update in seconds
 *
 * Updates the prompt animation state.
 *
 * Since: 1.0
 */
void
lrg_input_prompt_update (LrgInputPrompt *self,
                         gfloat          delta_time)
{
    g_return_if_fail (LRG_IS_INPUT_PROMPT (self));

    if (self->animated)
    {
        self->animation_time += delta_time;

        /* Wrap around to avoid float overflow */
        if (self->animation_time > 1000.0f)
        {
            self->animation_time = 0.0f;
        }
    }
}
