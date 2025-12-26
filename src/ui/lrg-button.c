/* lrg-button.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Clickable button widget with visual feedback.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-button.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgButton
{
    LrgWidget  parent_instance;

    gchar     *text;
    GrlFont   *font;
    gfloat     font_size;
    GrlColor   normal_color;
    GrlColor   hover_color;
    GrlColor   pressed_color;
    GrlColor   text_color;
    gfloat     corner_radius;
    gboolean   is_hovered;
    gboolean   is_pressed;
};

G_DEFINE_TYPE (LrgButton, lrg_button, LRG_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_TEXT,
    PROP_NORMAL_COLOR,
    PROP_HOVER_COLOR,
    PROP_PRESSED_COLOR,
    PROP_TEXT_COLOR,
    PROP_CORNER_RADIUS,
    PROP_IS_HOVERED,
    PROP_IS_PRESSED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_CLICKED,
    SIGNAL_HOVERED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Default colors */
static const GrlColor DEFAULT_NORMAL  = { 80, 80, 80, 255 };
static const GrlColor DEFAULT_HOVER   = { 100, 100, 100, 255 };
static const GrlColor DEFAULT_PRESSED = { 60, 60, 60, 255 };
static const GrlColor DEFAULT_TEXT    = { 255, 255, 255, 255 };

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_button_draw (LrgWidget *widget)
{
    LrgButton    *self = LRG_BUTTON (widget);
    GrlRectangle  rect;
    GrlColor              *bg_color;
    g_autoptr(GrlVector2)  text_pos = NULL;
    g_autoptr(GrlVector2)  text_size = NULL;
    gfloat                 text_width;

    rect.x = lrg_widget_get_world_x (widget);
    rect.y = lrg_widget_get_world_y (widget);
    rect.width = lrg_widget_get_width (widget);
    rect.height = lrg_widget_get_height (widget);

    /* Choose background color based on state */
    if (self->is_pressed)
    {
        bg_color = &self->pressed_color;
    }
    else if (self->is_hovered)
    {
        bg_color = &self->hover_color;
    }
    else
    {
        bg_color = &self->normal_color;
    }

    /* Draw background */
    if (self->corner_radius > 0.0f)
    {
        gfloat roundness;
        roundness = self->corner_radius / (rect.width < rect.height ?
                                           rect.width : rect.height);
        if (roundness > 1.0f)
        {
            roundness = 1.0f;
        }
        grl_draw_rectangle_rounded (&rect, roundness, 8, bg_color);
    }
    else
    {
        grl_draw_rectangle_rec (&rect, bg_color);
    }

    /* Draw text centered */
    if (self->text != NULL && self->text[0] != '\0')
    {
        if (self->font != NULL)
        {
            text_size = grl_font_measure_text (self->font, self->text,
                                               self->font_size, 1.0f);
            text_width = text_size->x;
        }
        else
        {
            text_width = (gfloat)g_utf8_strlen (self->text, -1) *
                         (self->font_size * 0.6f);
        }

        text_pos = grl_vector2_new (rect.x + (rect.width - text_width) / 2.0f,
                                    rect.y + (rect.height - self->font_size) / 2.0f);

        if (self->font != NULL)
        {
            grl_draw_text_ex (self->font, self->text, text_pos,
                              self->font_size, 1.0f, &self->text_color);
        }
        else
        {
            grl_draw_text (self->text, (gint)text_pos->x, (gint)text_pos->y,
                           (gint)self->font_size, &self->text_color);
        }
    }
}

static void
lrg_button_measure (LrgWidget *widget,
                    gfloat    *preferred_width,
                    gfloat    *preferred_height)
{
    LrgButton             *self = LRG_BUTTON (widget);
    g_autoptr(GrlVector2)  text_size = NULL;
    gfloat                 padding = 16.0f;

    if (self->text == NULL || self->text[0] == '\0')
    {
        if (preferred_width != NULL)
        {
            *preferred_width = padding * 2;
        }
        if (preferred_height != NULL)
        {
            *preferred_height = self->font_size + padding;
        }
        return;
    }

    if (self->font != NULL)
    {
        text_size = grl_font_measure_text (self->font, self->text,
                                           self->font_size, 1.0f);
    }
    else
    {
        text_size = grl_vector2_new ((gfloat)g_utf8_strlen (self->text, -1) *
                                     (self->font_size * 0.6f),
                                     self->font_size);
    }

    if (preferred_width != NULL)
    {
        *preferred_width = text_size->x + padding * 2;
    }
    if (preferred_height != NULL)
    {
        *preferred_height = text_size->y + padding;
    }
}

static gboolean
lrg_button_handle_event (LrgWidget        *widget,
                         const LrgUIEvent *event)
{
    LrgButton      *self = LRG_BUTTON (widget);
    LrgUIEventType  type;
    gfloat          x, y;
    gboolean        inside;

    type = lrg_ui_event_get_event_type (event);
    x = lrg_ui_event_get_x (event);
    y = lrg_ui_event_get_y (event);

    inside = lrg_widget_contains_point (widget, x, y);

    switch (type)
    {
    case LRG_UI_EVENT_MOUSE_MOVE:
        if (inside != self->is_hovered)
        {
            self->is_hovered = inside;
            g_signal_emit (self, signals[SIGNAL_HOVERED], 0, inside);
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_HOVERED]);
        }
        /* Don't consume move events */
        return FALSE;

    case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
        if (inside && lrg_ui_event_get_button (event) == 0)  /* Left button */
        {
            self->is_pressed = TRUE;
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_PRESSED]);
            return TRUE;
        }
        break;

    case LRG_UI_EVENT_MOUSE_BUTTON_UP:
        if (self->is_pressed && lrg_ui_event_get_button (event) == 0)
        {
            self->is_pressed = FALSE;
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_PRESSED]);

            /* Emit clicked if released inside button */
            if (inside)
            {
                g_signal_emit (self, signals[SIGNAL_CLICKED], 0);
            }
            return TRUE;
        }
        break;

    default:
        break;
    }

    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_button_finalize (GObject *object)
{
    LrgButton *self = LRG_BUTTON (object);

    g_free (self->text);
    g_clear_object (&self->font);

    G_OBJECT_CLASS (lrg_button_parent_class)->finalize (object);
}

static void
lrg_button_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
    LrgButton *self = LRG_BUTTON (object);

    switch (prop_id)
    {
    case PROP_TEXT:
        g_value_set_string (value, self->text);
        break;
    case PROP_NORMAL_COLOR:
        g_value_set_boxed (value, &self->normal_color);
        break;
    case PROP_HOVER_COLOR:
        g_value_set_boxed (value, &self->hover_color);
        break;
    case PROP_PRESSED_COLOR:
        g_value_set_boxed (value, &self->pressed_color);
        break;
    case PROP_TEXT_COLOR:
        g_value_set_boxed (value, &self->text_color);
        break;
    case PROP_CORNER_RADIUS:
        g_value_set_float (value, self->corner_radius);
        break;
    case PROP_IS_HOVERED:
        g_value_set_boolean (value, self->is_hovered);
        break;
    case PROP_IS_PRESSED:
        g_value_set_boolean (value, self->is_pressed);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_button_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
    LrgButton *self = LRG_BUTTON (object);

    switch (prop_id)
    {
    case PROP_TEXT:
        lrg_button_set_text (self, g_value_get_string (value));
        break;
    case PROP_NORMAL_COLOR:
        lrg_button_set_normal_color (self, g_value_get_boxed (value));
        break;
    case PROP_HOVER_COLOR:
        lrg_button_set_hover_color (self, g_value_get_boxed (value));
        break;
    case PROP_PRESSED_COLOR:
        lrg_button_set_pressed_color (self, g_value_get_boxed (value));
        break;
    case PROP_TEXT_COLOR:
        lrg_button_set_text_color (self, g_value_get_boxed (value));
        break;
    case PROP_CORNER_RADIUS:
        lrg_button_set_corner_radius (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_button_class_init (LrgButtonClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->finalize = lrg_button_finalize;
    object_class->get_property = lrg_button_get_property;
    object_class->set_property = lrg_button_set_property;

    widget_class->draw = lrg_button_draw;
    widget_class->measure = lrg_button_measure;
    widget_class->handle_event = lrg_button_handle_event;

    /**
     * LrgButton:text:
     *
     * The button's label text.
     */
    properties[PROP_TEXT] =
        g_param_spec_string ("text",
                             "Text",
                             "The button label",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_NORMAL_COLOR] =
        g_param_spec_boxed ("normal-color",
                            "Normal Color",
                            "Background when idle",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_HOVER_COLOR] =
        g_param_spec_boxed ("hover-color",
                            "Hover Color",
                            "Background when hovered",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_PRESSED_COLOR] =
        g_param_spec_boxed ("pressed-color",
                            "Pressed Color",
                            "Background when pressed",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_TEXT_COLOR] =
        g_param_spec_boxed ("text-color",
                            "Text Color",
                            "The label color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_CORNER_RADIUS] =
        g_param_spec_float ("corner-radius",
                            "Corner Radius",
                            "Rounded corner radius",
                            0.0f, G_MAXFLOAT, 4.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_IS_HOVERED] =
        g_param_spec_boolean ("is-hovered",
                              "Is Hovered",
                              "Whether the button is hovered",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_IS_PRESSED] =
        g_param_spec_boolean ("is-pressed",
                              "Is Pressed",
                              "Whether the button is pressed",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgButton::clicked:
     * @self: the button that was clicked
     *
     * Emitted when the button is clicked (pressed and released inside).
     */
    signals[SIGNAL_CLICKED] =
        g_signal_new ("clicked",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgButton::hovered:
     * @self: the button
     * @is_hovered: whether the button is now hovered
     *
     * Emitted when the hover state changes.
     */
    signals[SIGNAL_HOVERED] =
        g_signal_new ("hovered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
lrg_button_init (LrgButton *self)
{
    self->text = NULL;
    self->font = NULL;
    self->font_size = 20.0f;
    self->normal_color = DEFAULT_NORMAL;
    self->hover_color = DEFAULT_HOVER;
    self->pressed_color = DEFAULT_PRESSED;
    self->text_color = DEFAULT_TEXT;
    self->corner_radius = 4.0f;
    self->is_hovered = FALSE;
    self->is_pressed = FALSE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgButton *
lrg_button_new (const gchar *text)
{
    return g_object_new (LRG_TYPE_BUTTON,
                         "text", text,
                         NULL);
}

const gchar *
lrg_button_get_text (LrgButton *self)
{
    g_return_val_if_fail (LRG_IS_BUTTON (self), NULL);
    return self->text;
}

void
lrg_button_set_text (LrgButton   *self,
                     const gchar *text)
{
    g_return_if_fail (LRG_IS_BUTTON (self));

    if (g_strcmp0 (self->text, text) != 0)
    {
        g_free (self->text);
        self->text = g_strdup (text);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
    }
}

const GrlColor *
lrg_button_get_normal_color (LrgButton *self)
{
    g_return_val_if_fail (LRG_IS_BUTTON (self), &DEFAULT_NORMAL);
    return &self->normal_color;
}

void
lrg_button_set_normal_color (LrgButton      *self,
                             const GrlColor *color)
{
    g_return_if_fail (LRG_IS_BUTTON (self));
    g_return_if_fail (color != NULL);

    self->normal_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NORMAL_COLOR]);
}

const GrlColor *
lrg_button_get_hover_color (LrgButton *self)
{
    g_return_val_if_fail (LRG_IS_BUTTON (self), &DEFAULT_HOVER);
    return &self->hover_color;
}

void
lrg_button_set_hover_color (LrgButton      *self,
                            const GrlColor *color)
{
    g_return_if_fail (LRG_IS_BUTTON (self));
    g_return_if_fail (color != NULL);

    self->hover_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOVER_COLOR]);
}

const GrlColor *
lrg_button_get_pressed_color (LrgButton *self)
{
    g_return_val_if_fail (LRG_IS_BUTTON (self), &DEFAULT_PRESSED);
    return &self->pressed_color;
}

void
lrg_button_set_pressed_color (LrgButton      *self,
                              const GrlColor *color)
{
    g_return_if_fail (LRG_IS_BUTTON (self));
    g_return_if_fail (color != NULL);

    self->pressed_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRESSED_COLOR]);
}

const GrlColor *
lrg_button_get_text_color (LrgButton *self)
{
    g_return_val_if_fail (LRG_IS_BUTTON (self), &DEFAULT_TEXT);
    return &self->text_color;
}

void
lrg_button_set_text_color (LrgButton      *self,
                           const GrlColor *color)
{
    g_return_if_fail (LRG_IS_BUTTON (self));
    g_return_if_fail (color != NULL);

    self->text_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_COLOR]);
}

gfloat
lrg_button_get_corner_radius (LrgButton *self)
{
    g_return_val_if_fail (LRG_IS_BUTTON (self), 4.0f);
    return self->corner_radius;
}

void
lrg_button_set_corner_radius (LrgButton *self,
                              gfloat     radius)
{
    g_return_if_fail (LRG_IS_BUTTON (self));
    g_return_if_fail (radius >= 0.0f);

    if (self->corner_radius != radius)
    {
        self->corner_radius = radius;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
    }
}

gboolean
lrg_button_get_is_hovered (LrgButton *self)
{
    g_return_val_if_fail (LRG_IS_BUTTON (self), FALSE);
    return self->is_hovered;
}

gboolean
lrg_button_get_is_pressed (LrgButton *self)
{
    g_return_val_if_fail (LRG_IS_BUTTON (self), FALSE);
    return self->is_pressed;
}
