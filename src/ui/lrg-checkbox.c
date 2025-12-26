/* lrg-checkbox.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Checkbox widget with toggle state and optional label.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-checkbox.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgCheckbox
{
    LrgWidget  parent_instance;

    gchar     *label;
    gboolean   checked;
    gfloat     box_size;
    gfloat     spacing;
    gfloat     font_size;
    GrlColor   box_color;
    GrlColor   check_color;
    GrlColor   text_color;
};

G_DEFINE_TYPE (LrgCheckbox, lrg_checkbox, LRG_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_CHECKED,
    PROP_LABEL,
    PROP_BOX_SIZE,
    PROP_SPACING,
    PROP_FONT_SIZE,
    PROP_BOX_COLOR,
    PROP_CHECK_COLOR,
    PROP_TEXT_COLOR,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_TOGGLED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Default colors */
static const GrlColor DEFAULT_BOX   = { 100, 100, 100, 255 };
static const GrlColor DEFAULT_CHECK = { 50, 200, 50, 255 };
static const GrlColor DEFAULT_TEXT  = { 255, 255, 255, 255 };

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_checkbox_draw (LrgWidget *widget)
{
    LrgCheckbox   *self = LRG_CHECKBOX (widget);
    GrlRectangle   box_rect;
    gfloat         world_x, world_y;

    world_x = lrg_widget_get_world_x (widget);
    world_y = lrg_widget_get_world_y (widget);

    /* Draw checkbox box */
    box_rect.x = world_x;
    box_rect.y = world_y;
    box_rect.width = self->box_size;
    box_rect.height = self->box_size;

    /* Draw box background */
    grl_draw_rectangle_rec (&box_rect, &self->box_color);

    /* Draw box border */
    grl_draw_rectangle_lines_ex (&box_rect, 2.0f, &self->text_color);

    /* Draw checkmark if checked */
    if (self->checked)
    {
        gfloat padding = self->box_size * 0.2f;
        GrlVector2 start, mid, end;

        /* Draw checkmark as two lines forming a check shape */
        start.x = box_rect.x + padding;
        start.y = box_rect.y + self->box_size * 0.5f;

        mid.x = box_rect.x + self->box_size * 0.4f;
        mid.y = box_rect.y + self->box_size - padding;

        end.x = box_rect.x + self->box_size - padding;
        end.y = box_rect.y + padding;

        grl_draw_line_ex (&start, &mid, 3.0f, &self->check_color);
        grl_draw_line_ex (&mid, &end, 3.0f, &self->check_color);
    }

    /* Draw label if present */
    if (self->label != NULL && self->label[0] != '\0')
    {
        gint text_x = (gint)(world_x + self->box_size + self->spacing);
        gint text_y = (gint)(world_y + (self->box_size - self->font_size) / 2.0f);

        grl_draw_text (self->label, text_x, text_y,
                       (gint)self->font_size, &self->text_color);
    }
}

static void
lrg_checkbox_measure (LrgWidget *widget,
                      gfloat    *preferred_width,
                      gfloat    *preferred_height)
{
    LrgCheckbox *self = LRG_CHECKBOX (widget);
    gfloat       label_width = 0.0f;

    /* Calculate label width if present */
    if (self->label != NULL && self->label[0] != '\0')
    {
        /* Approximate width based on font size and character count */
        label_width = (gfloat)g_utf8_strlen (self->label, -1) *
                      (self->font_size * 0.6f);
    }

    if (preferred_width != NULL)
    {
        *preferred_width = self->box_size + self->spacing + label_width;
    }

    if (preferred_height != NULL)
    {
        *preferred_height = self->box_size > self->font_size ?
                            self->box_size : self->font_size;
    }
}

static gboolean
lrg_checkbox_handle_event (LrgWidget        *widget,
                           const LrgUIEvent *event)
{
    LrgCheckbox    *self = LRG_CHECKBOX (widget);
    LrgUIEventType  type;
    gfloat          x, y;
    gboolean        inside;

    type = lrg_ui_event_get_event_type (event);
    x = lrg_ui_event_get_x (event);
    y = lrg_ui_event_get_y (event);

    inside = lrg_widget_contains_point (widget, x, y);

    /* Toggle on mouse button up inside the widget */
    if (type == LRG_UI_EVENT_MOUSE_BUTTON_UP && inside)
    {
        if (lrg_ui_event_get_button (event) == 0)  /* Left button */
        {
            lrg_checkbox_toggle (self);
            return TRUE;
        }
    }

    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_checkbox_finalize (GObject *object)
{
    LrgCheckbox *self = LRG_CHECKBOX (object);

    g_free (self->label);

    G_OBJECT_CLASS (lrg_checkbox_parent_class)->finalize (object);
}

static void
lrg_checkbox_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgCheckbox *self = LRG_CHECKBOX (object);

    switch (prop_id)
    {
    case PROP_CHECKED:
        g_value_set_boolean (value, self->checked);
        break;
    case PROP_LABEL:
        g_value_set_string (value, self->label);
        break;
    case PROP_BOX_SIZE:
        g_value_set_float (value, self->box_size);
        break;
    case PROP_SPACING:
        g_value_set_float (value, self->spacing);
        break;
    case PROP_FONT_SIZE:
        g_value_set_float (value, self->font_size);
        break;
    case PROP_BOX_COLOR:
        g_value_set_boxed (value, &self->box_color);
        break;
    case PROP_CHECK_COLOR:
        g_value_set_boxed (value, &self->check_color);
        break;
    case PROP_TEXT_COLOR:
        g_value_set_boxed (value, &self->text_color);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_checkbox_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgCheckbox *self = LRG_CHECKBOX (object);

    switch (prop_id)
    {
    case PROP_CHECKED:
        lrg_checkbox_set_checked (self, g_value_get_boolean (value));
        break;
    case PROP_LABEL:
        lrg_checkbox_set_label (self, g_value_get_string (value));
        break;
    case PROP_BOX_SIZE:
        lrg_checkbox_set_box_size (self, g_value_get_float (value));
        break;
    case PROP_SPACING:
        lrg_checkbox_set_spacing (self, g_value_get_float (value));
        break;
    case PROP_FONT_SIZE:
        lrg_checkbox_set_font_size (self, g_value_get_float (value));
        break;
    case PROP_BOX_COLOR:
        lrg_checkbox_set_box_color (self, g_value_get_boxed (value));
        break;
    case PROP_CHECK_COLOR:
        lrg_checkbox_set_check_color (self, g_value_get_boxed (value));
        break;
    case PROP_TEXT_COLOR:
        lrg_checkbox_set_text_color (self, g_value_get_boxed (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_checkbox_class_init (LrgCheckboxClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->finalize = lrg_checkbox_finalize;
    object_class->get_property = lrg_checkbox_get_property;
    object_class->set_property = lrg_checkbox_set_property;

    widget_class->draw = lrg_checkbox_draw;
    widget_class->measure = lrg_checkbox_measure;
    widget_class->handle_event = lrg_checkbox_handle_event;

    /**
     * LrgCheckbox:checked:
     *
     * Whether the checkbox is checked.
     */
    properties[PROP_CHECKED] =
        g_param_spec_boolean ("checked",
                              "Checked",
                              "Whether the checkbox is checked",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgCheckbox:label:
     *
     * The label text displayed next to the checkbox.
     */
    properties[PROP_LABEL] =
        g_param_spec_string ("label",
                             "Label",
                             "The label text",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgCheckbox:box-size:
     *
     * The size of the checkbox box in pixels.
     */
    properties[PROP_BOX_SIZE] =
        g_param_spec_float ("box-size",
                            "Box Size",
                            "The checkbox box size",
                            1.0f, G_MAXFLOAT, 20.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgCheckbox:spacing:
     *
     * The spacing between box and label.
     */
    properties[PROP_SPACING] =
        g_param_spec_float ("spacing",
                            "Spacing",
                            "Space between box and label",
                            0.0f, G_MAXFLOAT, 8.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgCheckbox:font-size:
     *
     * The label font size in pixels.
     */
    properties[PROP_FONT_SIZE] =
        g_param_spec_float ("font-size",
                            "Font Size",
                            "The label font size",
                            1.0f, G_MAXFLOAT, 20.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_BOX_COLOR] =
        g_param_spec_boxed ("box-color",
                            "Box Color",
                            "The checkbox box color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_CHECK_COLOR] =
        g_param_spec_boxed ("check-color",
                            "Check Color",
                            "The checkmark color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_TEXT_COLOR] =
        g_param_spec_boxed ("text-color",
                            "Text Color",
                            "The label text color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgCheckbox::toggled:
     * @self: the checkbox that was toggled
     * @checked: the new checked state
     *
     * Emitted when the checkbox is toggled.
     */
    signals[SIGNAL_TOGGLED] =
        g_signal_new ("toggled",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
lrg_checkbox_init (LrgCheckbox *self)
{
    self->label = NULL;
    self->checked = FALSE;
    self->box_size = 20.0f;
    self->spacing = 8.0f;
    self->font_size = 20.0f;
    self->box_color = DEFAULT_BOX;
    self->check_color = DEFAULT_CHECK;
    self->text_color = DEFAULT_TEXT;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgCheckbox *
lrg_checkbox_new (const gchar *label)
{
    return g_object_new (LRG_TYPE_CHECKBOX,
                         "label", label,
                         NULL);
}

gboolean
lrg_checkbox_get_checked (LrgCheckbox *self)
{
    g_return_val_if_fail (LRG_IS_CHECKBOX (self), FALSE);
    return self->checked;
}

void
lrg_checkbox_set_checked (LrgCheckbox *self,
                          gboolean     checked)
{
    g_return_if_fail (LRG_IS_CHECKBOX (self));

    checked = !!checked;

    if (self->checked != checked)
    {
        self->checked = checked;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CHECKED]);
        g_signal_emit (self, signals[SIGNAL_TOGGLED], 0, checked);
    }
}

void
lrg_checkbox_toggle (LrgCheckbox *self)
{
    g_return_if_fail (LRG_IS_CHECKBOX (self));
    lrg_checkbox_set_checked (self, !self->checked);
}

const gchar *
lrg_checkbox_get_label (LrgCheckbox *self)
{
    g_return_val_if_fail (LRG_IS_CHECKBOX (self), NULL);
    return self->label;
}

void
lrg_checkbox_set_label (LrgCheckbox *self,
                        const gchar *label)
{
    g_return_if_fail (LRG_IS_CHECKBOX (self));

    if (g_strcmp0 (self->label, label) != 0)
    {
        g_free (self->label);
        self->label = g_strdup (label);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LABEL]);
    }
}

gfloat
lrg_checkbox_get_box_size (LrgCheckbox *self)
{
    g_return_val_if_fail (LRG_IS_CHECKBOX (self), 20.0f);
    return self->box_size;
}

void
lrg_checkbox_set_box_size (LrgCheckbox *self,
                           gfloat       size)
{
    g_return_if_fail (LRG_IS_CHECKBOX (self));
    g_return_if_fail (size > 0.0f);

    if (self->box_size != size)
    {
        self->box_size = size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOX_SIZE]);
    }
}

gfloat
lrg_checkbox_get_spacing (LrgCheckbox *self)
{
    g_return_val_if_fail (LRG_IS_CHECKBOX (self), 8.0f);
    return self->spacing;
}

void
lrg_checkbox_set_spacing (LrgCheckbox *self,
                          gfloat       spacing)
{
    g_return_if_fail (LRG_IS_CHECKBOX (self));
    g_return_if_fail (spacing >= 0.0f);

    if (self->spacing != spacing)
    {
        self->spacing = spacing;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPACING]);
    }
}

const GrlColor *
lrg_checkbox_get_box_color (LrgCheckbox *self)
{
    g_return_val_if_fail (LRG_IS_CHECKBOX (self), &DEFAULT_BOX);
    return &self->box_color;
}

void
lrg_checkbox_set_box_color (LrgCheckbox    *self,
                            const GrlColor *color)
{
    g_return_if_fail (LRG_IS_CHECKBOX (self));
    g_return_if_fail (color != NULL);

    self->box_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOX_COLOR]);
}

const GrlColor *
lrg_checkbox_get_check_color (LrgCheckbox *self)
{
    g_return_val_if_fail (LRG_IS_CHECKBOX (self), &DEFAULT_CHECK);
    return &self->check_color;
}

void
lrg_checkbox_set_check_color (LrgCheckbox    *self,
                              const GrlColor *color)
{
    g_return_if_fail (LRG_IS_CHECKBOX (self));
    g_return_if_fail (color != NULL);

    self->check_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CHECK_COLOR]);
}

const GrlColor *
lrg_checkbox_get_text_color (LrgCheckbox *self)
{
    g_return_val_if_fail (LRG_IS_CHECKBOX (self), &DEFAULT_TEXT);
    return &self->text_color;
}

void
lrg_checkbox_set_text_color (LrgCheckbox    *self,
                             const GrlColor *color)
{
    g_return_if_fail (LRG_IS_CHECKBOX (self));
    g_return_if_fail (color != NULL);

    self->text_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_COLOR]);
}

gfloat
lrg_checkbox_get_font_size (LrgCheckbox *self)
{
    g_return_val_if_fail (LRG_IS_CHECKBOX (self), 20.0f);
    return self->font_size;
}

void
lrg_checkbox_set_font_size (LrgCheckbox *self,
                            gfloat       size)
{
    g_return_if_fail (LRG_IS_CHECKBOX (self));
    g_return_if_fail (size > 0.0f);

    if (self->font_size != size)
    {
        self->font_size = size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE]);
    }
}
