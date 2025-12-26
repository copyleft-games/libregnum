/* lrg-text-input.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Text input widget for single-line text entry.
 */

#include "config.h"
#include "lrg-text-input.h"
#include <string.h>

/**
 * SECTION:lrg-text-input
 * @title: LrgTextInput
 * @short_description: Single-line text input widget
 *
 * #LrgTextInput is an interactive widget that allows users to enter
 * and edit single-line text. It supports placeholder text, password
 * masking, and basic cursor navigation.
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * LrgTextInput *input = lrg_text_input_new_with_placeholder ("Enter name...");
 *
 * g_signal_connect (input, "text-changed",
 *                   G_CALLBACK (on_text_changed), NULL);
 * g_signal_connect (input, "submitted",
 *                   G_CALLBACK (on_submitted), NULL);
 * ]|
 */

struct _LrgTextInput
{
    LrgWidget  parent_instance;

    gchar     *text;
    gchar     *placeholder;
    guint      max_length;
    gboolean   password_mode;
    gint       cursor_position;
    gboolean   focused;

    gfloat     font_size;
    GrlColor   text_color;
    GrlColor   background_color;
    GrlColor   border_color;
    GrlColor   placeholder_color;
    gfloat     corner_radius;
    gfloat     padding;

    /* Cursor blinking state */
    gdouble    cursor_blink_timer;
    gboolean   cursor_visible;
};

G_DEFINE_TYPE (LrgTextInput, lrg_text_input, LRG_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_TEXT,
    PROP_PLACEHOLDER,
    PROP_MAX_LENGTH,
    PROP_PASSWORD_MODE,
    PROP_CURSOR_POSITION,
    PROP_FOCUSED,
    PROP_FONT_SIZE,
    PROP_TEXT_COLOR,
    PROP_BACKGROUND_COLOR,
    PROP_BORDER_COLOR,
    PROP_PLACEHOLDER_COLOR,
    PROP_CORNER_RADIUS,
    PROP_PADDING,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_TEXT_CHANGED,
    SIGNAL_SUBMITTED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Default colors */
static const GrlColor DEFAULT_TEXT        = { 255, 255, 255, 255 };
static const GrlColor DEFAULT_BACKGROUND  = { 40, 40, 40, 255 };
static const GrlColor DEFAULT_BORDER      = { 100, 100, 100, 255 };
static const GrlColor DEFAULT_PLACEHOLDER = { 120, 120, 120, 255 };

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

/*
 * Get display text (either actual text or masked with asterisks).
 */
static gchar *
get_display_text (LrgTextInput *self)
{
    glong len;

    if (self->text == NULL || self->text[0] == '\0')
        return g_strdup ("");

    if (self->password_mode)
    {
        len = g_utf8_strlen (self->text, -1);
        return g_strnfill ((gsize)len, '*');
    }

    return g_strdup (self->text);
}

/*
 * Insert a character at the current cursor position.
 */
static void
insert_char_at_cursor (LrgTextInput *self,
                       gunichar      ch)
{
    GString *str;
    gchar   *before;
    gchar   *after;
    gchar    utf8_buf[7];
    gint     utf8_len;
    glong    text_len;

    /* Check max length */
    if (self->max_length > 0)
    {
        text_len = self->text ? g_utf8_strlen (self->text, -1) : 0;
        if ((guint)text_len >= self->max_length)
            return;
    }

    /* Convert character to UTF-8 */
    utf8_len = g_unichar_to_utf8 (ch, utf8_buf);
    utf8_buf[utf8_len] = '\0';

    if (self->text == NULL || self->text[0] == '\0')
    {
        g_free (self->text);
        self->text = g_strdup (utf8_buf);
        self->cursor_position = 1;
    }
    else
    {
        /* Split at cursor position */
        gchar *cursor_ptr = g_utf8_offset_to_pointer (self->text, self->cursor_position);
        before = g_strndup (self->text, cursor_ptr - self->text);
        after = g_strdup (cursor_ptr);

        str = g_string_new (before);
        g_string_append (str, utf8_buf);
        g_string_append (str, after);

        g_free (self->text);
        self->text = g_string_free (str, FALSE);
        self->cursor_position++;

        g_free (before);
        g_free (after);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
    g_signal_emit (self, signals[SIGNAL_TEXT_CHANGED], 0);
}

/*
 * Delete the character before the cursor (backspace).
 */
static void
delete_char_before_cursor (LrgTextInput *self)
{
    GString *str;
    gchar   *before_char;
    gchar   *cursor_ptr;
    gchar   *after;

    if (self->text == NULL || self->text[0] == '\0')
        return;

    if (self->cursor_position <= 0)
        return;

    cursor_ptr = g_utf8_offset_to_pointer (self->text, self->cursor_position);
    before_char = g_utf8_offset_to_pointer (self->text, self->cursor_position - 1);
    after = g_strdup (cursor_ptr);

    str = g_string_new (NULL);
    g_string_append_len (str, self->text, before_char - self->text);
    g_string_append (str, after);

    g_free (self->text);
    self->text = g_string_free (str, FALSE);
    self->cursor_position--;

    g_free (after);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
    g_signal_emit (self, signals[SIGNAL_TEXT_CHANGED], 0);
}

/*
 * Delete the character at the cursor (delete key).
 */
static void
delete_char_at_cursor (LrgTextInput *self)
{
    GString *str;
    gchar   *cursor_ptr;
    gchar   *after_char;
    glong    text_len;

    if (self->text == NULL || self->text[0] == '\0')
        return;

    text_len = g_utf8_strlen (self->text, -1);
    if (self->cursor_position >= text_len)
        return;

    cursor_ptr = g_utf8_offset_to_pointer (self->text, self->cursor_position);
    after_char = g_utf8_next_char (cursor_ptr);

    str = g_string_new (NULL);
    g_string_append_len (str, self->text, cursor_ptr - self->text);
    g_string_append (str, after_char);

    g_free (self->text);
    self->text = g_string_free (str, FALSE);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
    g_signal_emit (self, signals[SIGNAL_TEXT_CHANGED], 0);
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_text_input_draw (LrgWidget *widget)
{
    LrgTextInput *self = LRG_TEXT_INPUT (widget);
    GrlRectangle  bg_rect;
    gfloat        world_x, world_y, width, height;
    g_autofree gchar *display_text = NULL;
    const gchar  *text_to_draw;
    const GrlColor *text_color;
    gfloat        text_x, text_y;
    gfloat        char_width;

    world_x = lrg_widget_get_world_x (widget);
    world_y = lrg_widget_get_world_y (widget);
    width = lrg_widget_get_width (widget);
    height = lrg_widget_get_height (widget);

    /* Background rectangle */
    bg_rect.x = world_x;
    bg_rect.y = world_y;
    bg_rect.width = width;
    bg_rect.height = height;

    /* Draw background */
    if (self->corner_radius > 0.0f)
    {
        gfloat roundness = self->corner_radius / (width < height ? width : height);
        if (roundness > 1.0f)
            roundness = 1.0f;
        grl_draw_rectangle_rounded (&bg_rect, roundness, 8, &self->background_color);
    }
    else
    {
        grl_draw_rectangle_rec (&bg_rect, &self->background_color);
    }

    /* Draw border */
    grl_draw_rectangle_lines_ex (&bg_rect, 2.0f, &self->border_color);

    /* Determine what text to display */
    if (self->text == NULL || self->text[0] == '\0')
    {
        /* Show placeholder */
        if (self->placeholder != NULL && self->placeholder[0] != '\0')
        {
            text_to_draw = self->placeholder;
            text_color = &self->placeholder_color;
        }
        else
        {
            text_to_draw = NULL;
            text_color = NULL;
        }
    }
    else
    {
        display_text = get_display_text (self);
        text_to_draw = display_text;
        text_color = &self->text_color;
    }

    /* Calculate text position (vertically centered) */
    text_x = world_x + self->padding;
    text_y = world_y + (height - self->font_size) / 2.0f;

    /* Draw text */
    if (text_to_draw != NULL && text_color != NULL)
    {
        grl_draw_text (text_to_draw, (gint)text_x, (gint)text_y,
                       (gint)self->font_size, text_color);
    }

    /* Draw cursor when focused */
    if (self->focused && self->cursor_visible)
    {
        gfloat cursor_x;

        /* Approximate character width */
        char_width = self->font_size * 0.6f;

        /* Calculate cursor X position based on cursor position in text */
        if (display_text != NULL)
        {
            cursor_x = text_x + (self->cursor_position * char_width);
        }
        else
        {
            cursor_x = text_x;
        }

        /* Draw cursor line */
        grl_draw_line ((gint)cursor_x, (gint)(text_y),
                       (gint)cursor_x, (gint)(text_y + self->font_size),
                       &self->text_color);
    }
}

static void
lrg_text_input_measure (LrgWidget *widget,
                        gfloat    *preferred_width,
                        gfloat    *preferred_height)
{
    LrgTextInput *self = LRG_TEXT_INPUT (widget);

    if (preferred_width != NULL)
        *preferred_width = 200.0f;  /* Default width */
    if (preferred_height != NULL)
        *preferred_height = self->font_size + self->padding * 2.0f;
}

static gboolean
lrg_text_input_handle_event (LrgWidget        *widget,
                             const LrgUIEvent *event)
{
    LrgTextInput   *self = LRG_TEXT_INPUT (widget);
    LrgUIEventType  type;
    gfloat          x, y;
    gboolean        inside;
    GrlKey          key;
    glong           text_len;

    type = lrg_ui_event_get_event_type (event);
    x = lrg_ui_event_get_x (event);
    y = lrg_ui_event_get_y (event);

    inside = lrg_widget_contains_point (widget, x, y);

    switch (type)
    {
    case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
        if (inside && lrg_ui_event_get_button (event) == 0)
        {
            /* Click to focus */
            if (!self->focused)
            {
                lrg_text_input_set_focused (self, TRUE);
            }
            return TRUE;
        }
        else if (!inside && self->focused)
        {
            /* Click outside to unfocus */
            lrg_text_input_set_focused (self, FALSE);
        }
        break;

    case LRG_UI_EVENT_KEY_DOWN:
        if (!self->focused)
            break;

        key = lrg_ui_event_get_key (event);
        text_len = self->text ? g_utf8_strlen (self->text, -1) : 0;

        switch (key)
        {
        case GRL_KEY_BACKSPACE:
            delete_char_before_cursor (self);
            return TRUE;

        case GRL_KEY_DELETE:
            delete_char_at_cursor (self);
            return TRUE;

        case GRL_KEY_LEFT:
            if (self->cursor_position > 0)
            {
                self->cursor_position--;
                g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURSOR_POSITION]);
            }
            return TRUE;

        case GRL_KEY_RIGHT:
            if (self->cursor_position < text_len)
            {
                self->cursor_position++;
                g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURSOR_POSITION]);
            }
            return TRUE;

        case GRL_KEY_HOME:
            if (self->cursor_position != 0)
            {
                self->cursor_position = 0;
                g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURSOR_POSITION]);
            }
            return TRUE;

        case GRL_KEY_END:
            if (self->cursor_position != text_len)
            {
                self->cursor_position = (gint)text_len;
                g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURSOR_POSITION]);
            }
            return TRUE;

        case GRL_KEY_ENTER:
        case GRL_KEY_KP_ENTER:
            g_signal_emit (self, signals[SIGNAL_SUBMITTED], 0);
            return TRUE;

        default:
            /* Handle printable characters */
            if (key >= 32 && key <= 126)
            {
                insert_char_at_cursor (self, (gunichar)key);
                return TRUE;
            }
            break;
        }
        break;

    case LRG_UI_EVENT_FOCUS_IN:
        self->focused = TRUE;
        self->cursor_visible = TRUE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOCUSED]);
        return TRUE;

    case LRG_UI_EVENT_FOCUS_OUT:
        self->focused = FALSE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOCUSED]);
        return TRUE;

    default:
        break;
    }

    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_text_input_finalize (GObject *object)
{
    LrgTextInput *self = LRG_TEXT_INPUT (object);

    g_free (self->text);
    g_free (self->placeholder);

    G_OBJECT_CLASS (lrg_text_input_parent_class)->finalize (object);
}

static void
lrg_text_input_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgTextInput *self = LRG_TEXT_INPUT (object);

    switch (prop_id)
    {
    case PROP_TEXT:
        g_value_set_string (value, self->text);
        break;
    case PROP_PLACEHOLDER:
        g_value_set_string (value, self->placeholder);
        break;
    case PROP_MAX_LENGTH:
        g_value_set_uint (value, self->max_length);
        break;
    case PROP_PASSWORD_MODE:
        g_value_set_boolean (value, self->password_mode);
        break;
    case PROP_CURSOR_POSITION:
        g_value_set_int (value, self->cursor_position);
        break;
    case PROP_FOCUSED:
        g_value_set_boolean (value, self->focused);
        break;
    case PROP_FONT_SIZE:
        g_value_set_float (value, self->font_size);
        break;
    case PROP_TEXT_COLOR:
        g_value_set_boxed (value, &self->text_color);
        break;
    case PROP_BACKGROUND_COLOR:
        g_value_set_boxed (value, &self->background_color);
        break;
    case PROP_BORDER_COLOR:
        g_value_set_boxed (value, &self->border_color);
        break;
    case PROP_PLACEHOLDER_COLOR:
        g_value_set_boxed (value, &self->placeholder_color);
        break;
    case PROP_CORNER_RADIUS:
        g_value_set_float (value, self->corner_radius);
        break;
    case PROP_PADDING:
        g_value_set_float (value, self->padding);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_text_input_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgTextInput *self = LRG_TEXT_INPUT (object);

    switch (prop_id)
    {
    case PROP_TEXT:
        lrg_text_input_set_text (self, g_value_get_string (value));
        break;
    case PROP_PLACEHOLDER:
        lrg_text_input_set_placeholder (self, g_value_get_string (value));
        break;
    case PROP_MAX_LENGTH:
        lrg_text_input_set_max_length (self, g_value_get_uint (value));
        break;
    case PROP_PASSWORD_MODE:
        lrg_text_input_set_password_mode (self, g_value_get_boolean (value));
        break;
    case PROP_CURSOR_POSITION:
        lrg_text_input_set_cursor_position (self, g_value_get_int (value));
        break;
    case PROP_FOCUSED:
        lrg_text_input_set_focused (self, g_value_get_boolean (value));
        break;
    case PROP_FONT_SIZE:
        lrg_text_input_set_font_size (self, g_value_get_float (value));
        break;
    case PROP_TEXT_COLOR:
        lrg_text_input_set_text_color (self, g_value_get_boxed (value));
        break;
    case PROP_BACKGROUND_COLOR:
        lrg_text_input_set_background_color (self, g_value_get_boxed (value));
        break;
    case PROP_BORDER_COLOR:
        lrg_text_input_set_border_color (self, g_value_get_boxed (value));
        break;
    case PROP_PLACEHOLDER_COLOR:
        lrg_text_input_set_placeholder_color (self, g_value_get_boxed (value));
        break;
    case PROP_CORNER_RADIUS:
        lrg_text_input_set_corner_radius (self, g_value_get_float (value));
        break;
    case PROP_PADDING:
        lrg_text_input_set_padding (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_text_input_class_init (LrgTextInputClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->finalize = lrg_text_input_finalize;
    object_class->get_property = lrg_text_input_get_property;
    object_class->set_property = lrg_text_input_set_property;

    widget_class->draw = lrg_text_input_draw;
    widget_class->measure = lrg_text_input_measure;
    widget_class->handle_event = lrg_text_input_handle_event;

    /**
     * LrgTextInput:text:
     *
     * The current input text.
     */
    properties[PROP_TEXT] =
        g_param_spec_string ("text",
                             "Text",
                             "The current input text",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:placeholder:
     *
     * Placeholder text shown when input is empty.
     */
    properties[PROP_PLACEHOLDER] =
        g_param_spec_string ("placeholder",
                             "Placeholder",
                             "Placeholder text shown when input is empty",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:max-length:
     *
     * Maximum text length (0 = unlimited).
     */
    properties[PROP_MAX_LENGTH] =
        g_param_spec_uint ("max-length",
                           "Max Length",
                           "Maximum text length (0 = unlimited)",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:password-mode:
     *
     * Whether to mask input with asterisks.
     */
    properties[PROP_PASSWORD_MODE] =
        g_param_spec_boolean ("password-mode",
                              "Password Mode",
                              "Whether to mask input with asterisks",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:cursor-position:
     *
     * The cursor position in characters.
     */
    properties[PROP_CURSOR_POSITION] =
        g_param_spec_int ("cursor-position",
                          "Cursor Position",
                          "The cursor position in characters",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:focused:
     *
     * Whether the input has keyboard focus.
     */
    properties[PROP_FOCUSED] =
        g_param_spec_boolean ("focused",
                              "Focused",
                              "Whether the input has keyboard focus",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:font-size:
     *
     * The font size.
     */
    properties[PROP_FONT_SIZE] =
        g_param_spec_float ("font-size",
                            "Font Size",
                            "The font size",
                            1.0f, 200.0f, 20.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:text-color:
     *
     * The text color.
     */
    properties[PROP_TEXT_COLOR] =
        g_param_spec_boxed ("text-color",
                            "Text Color",
                            "The text color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:background-color:
     *
     * The background color.
     */
    properties[PROP_BACKGROUND_COLOR] =
        g_param_spec_boxed ("background-color",
                            "Background Color",
                            "The background color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:border-color:
     *
     * The border color.
     */
    properties[PROP_BORDER_COLOR] =
        g_param_spec_boxed ("border-color",
                            "Border Color",
                            "The border color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:placeholder-color:
     *
     * The placeholder text color.
     */
    properties[PROP_PLACEHOLDER_COLOR] =
        g_param_spec_boxed ("placeholder-color",
                            "Placeholder Color",
                            "The placeholder text color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:corner-radius:
     *
     * The corner radius for rounded corners.
     */
    properties[PROP_CORNER_RADIUS] =
        g_param_spec_float ("corner-radius",
                            "Corner Radius",
                            "The corner radius for rounded corners",
                            0.0f, 50.0f, 4.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:padding:
     *
     * The text padding from the edges.
     */
    properties[PROP_PADDING] =
        g_param_spec_float ("padding",
                            "Padding",
                            "The text padding from the edges",
                            0.0f, 50.0f, 8.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgTextInput::text-changed:
     * @self: the text input that emitted the signal
     *
     * Emitted when the input text changes.
     */
    signals[SIGNAL_TEXT_CHANGED] =
        g_signal_new ("text-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTextInput::submitted:
     * @self: the text input that emitted the signal
     *
     * Emitted when Enter is pressed.
     */
    signals[SIGNAL_SUBMITTED] =
        g_signal_new ("submitted",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_text_input_init (LrgTextInput *self)
{
    self->text = NULL;
    self->placeholder = NULL;
    self->max_length = 0;
    self->password_mode = FALSE;
    self->cursor_position = 0;
    self->focused = FALSE;

    self->font_size = 20.0f;
    self->text_color = DEFAULT_TEXT;
    self->background_color = DEFAULT_BACKGROUND;
    self->border_color = DEFAULT_BORDER;
    self->placeholder_color = DEFAULT_PLACEHOLDER;
    self->corner_radius = 4.0f;
    self->padding = 8.0f;

    self->cursor_blink_timer = 0.0;
    self->cursor_visible = TRUE;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_text_input_new:
 *
 * Creates a new text input widget.
 *
 * Returns: (transfer full): A new #LrgTextInput
 */
LrgTextInput *
lrg_text_input_new (void)
{
    return g_object_new (LRG_TYPE_TEXT_INPUT, NULL);
}

/**
 * lrg_text_input_new_with_placeholder:
 * @placeholder: (nullable): placeholder text
 *
 * Creates a new text input widget with placeholder text.
 *
 * Returns: (transfer full): A new #LrgTextInput
 */
LrgTextInput *
lrg_text_input_new_with_placeholder (const gchar *placeholder)
{
    return g_object_new (LRG_TYPE_TEXT_INPUT,
                         "placeholder", placeholder,
                         NULL);
}

/* ==========================================================================
 * Public API - Text
 * ========================================================================== */

/**
 * lrg_text_input_get_text:
 * @self: an #LrgTextInput
 *
 * Gets the current input text.
 *
 * Returns: (transfer none): The current text
 */
const gchar *
lrg_text_input_get_text (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), NULL);

    return self->text;
}

/**
 * lrg_text_input_set_text:
 * @self: an #LrgTextInput
 * @text: (nullable): the text to set
 *
 * Sets the input text.
 */
void
lrg_text_input_set_text (LrgTextInput *self,
                         const gchar  *text)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    if (g_strcmp0 (self->text, text) == 0)
        return;

    g_free (self->text);
    self->text = g_strdup (text);

    /* Update cursor position */
    if (self->text != NULL)
    {
        glong len = g_utf8_strlen (self->text, -1);
        if (self->cursor_position > len)
            self->cursor_position = (gint)len;
    }
    else
    {
        self->cursor_position = 0;
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
    g_signal_emit (self, signals[SIGNAL_TEXT_CHANGED], 0);
}

/**
 * lrg_text_input_get_placeholder:
 * @self: an #LrgTextInput
 *
 * Gets the placeholder text.
 *
 * Returns: (transfer none) (nullable): The placeholder text
 */
const gchar *
lrg_text_input_get_placeholder (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), NULL);

    return self->placeholder;
}

/**
 * lrg_text_input_set_placeholder:
 * @self: an #LrgTextInput
 * @placeholder: (nullable): the placeholder text
 *
 * Sets the placeholder text.
 */
void
lrg_text_input_set_placeholder (LrgTextInput *self,
                                const gchar  *placeholder)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    if (g_strcmp0 (self->placeholder, placeholder) == 0)
        return;

    g_free (self->placeholder);
    self->placeholder = g_strdup (placeholder);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLACEHOLDER]);
}

/* ==========================================================================
 * Public API - Input Behavior
 * ========================================================================== */

guint
lrg_text_input_get_max_length (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), 0);
    return self->max_length;
}

void
lrg_text_input_set_max_length (LrgTextInput *self,
                               guint         max_length)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    if (self->max_length == max_length)
        return;

    self->max_length = max_length;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_LENGTH]);
}

gboolean
lrg_text_input_get_password_mode (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), FALSE);
    return self->password_mode;
}

void
lrg_text_input_set_password_mode (LrgTextInput *self,
                                  gboolean      password_mode)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    password_mode = !!password_mode;
    if (self->password_mode == password_mode)
        return;

    self->password_mode = password_mode;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PASSWORD_MODE]);
}

/* ==========================================================================
 * Public API - Cursor
 * ========================================================================== */

gint
lrg_text_input_get_cursor_position (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), 0);
    return self->cursor_position;
}

void
lrg_text_input_set_cursor_position (LrgTextInput *self,
                                    gint          position)
{
    glong text_len;

    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    text_len = self->text ? g_utf8_strlen (self->text, -1) : 0;

    if (position < 0)
        position = 0;
    if (position > text_len)
        position = (gint)text_len;

    if (self->cursor_position == position)
        return;

    self->cursor_position = position;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURSOR_POSITION]);
}

/* ==========================================================================
 * Public API - Focus
 * ========================================================================== */

gboolean
lrg_text_input_get_focused (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), FALSE);
    return self->focused;
}

void
lrg_text_input_set_focused (LrgTextInput *self,
                            gboolean      focused)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    focused = !!focused;
    if (self->focused == focused)
        return;

    self->focused = focused;
    self->cursor_visible = TRUE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOCUSED]);
}

/* ==========================================================================
 * Public API - Appearance
 * ========================================================================== */

gfloat
lrg_text_input_get_font_size (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), 20.0f);
    return self->font_size;
}

void
lrg_text_input_set_font_size (LrgTextInput *self,
                              gfloat        size)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));
    g_return_if_fail (size >= 1.0f);

    if (self->font_size == size)
        return;

    self->font_size = size;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE]);
}

const GrlColor *
lrg_text_input_get_text_color (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), NULL);
    return &self->text_color;
}

void
lrg_text_input_set_text_color (LrgTextInput   *self,
                               const GrlColor *color)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->text_color, color, sizeof (GrlColor)) == 0)
        return;

    self->text_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_COLOR]);
}

const GrlColor *
lrg_text_input_get_background_color (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), NULL);
    return &self->background_color;
}

void
lrg_text_input_set_background_color (LrgTextInput   *self,
                                     const GrlColor *color)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->background_color, color, sizeof (GrlColor)) == 0)
        return;

    self->background_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKGROUND_COLOR]);
}

const GrlColor *
lrg_text_input_get_border_color (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), NULL);
    return &self->border_color;
}

void
lrg_text_input_set_border_color (LrgTextInput   *self,
                                 const GrlColor *color)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->border_color, color, sizeof (GrlColor)) == 0)
        return;

    self->border_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_COLOR]);
}

const GrlColor *
lrg_text_input_get_placeholder_color (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), NULL);
    return &self->placeholder_color;
}

void
lrg_text_input_set_placeholder_color (LrgTextInput   *self,
                                      const GrlColor *color)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->placeholder_color, color, sizeof (GrlColor)) == 0)
        return;

    self->placeholder_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLACEHOLDER_COLOR]);
}

gfloat
lrg_text_input_get_corner_radius (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), 4.0f);
    return self->corner_radius;
}

void
lrg_text_input_set_corner_radius (LrgTextInput *self,
                                  gfloat        radius)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    if (self->corner_radius == radius)
        return;

    self->corner_radius = radius;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
}

gfloat
lrg_text_input_get_padding (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), 8.0f);
    return self->padding;
}

void
lrg_text_input_set_padding (LrgTextInput *self,
                            gfloat        padding)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    if (self->padding == padding)
        return;

    self->padding = padding;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PADDING]);
}
