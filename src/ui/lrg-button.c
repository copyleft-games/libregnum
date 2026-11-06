/* lrg-button.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Clickable button widget.
 *
 * The button records itself through paint(): a BOX part (rounded
 * surface with a soft shadow that lifts on hover) and a LABEL part
 * (the centred text). Its built-in looks come from the theme in
 * style_defaults(); the "primary", "flat" and "danger" style classes
 * pick other colour families. The legacy colour properties still work:
 * once a colour has been set explicitly it replaces the theme default
 * for its state.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-button.h"
#include "lrg-canvas.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

/* How much the button shrinks while fully pressed (fraction of its size) */
#define BUTTON_SQUASH (0.04f)

/* Opacity of a disabled button */
#define BUTTON_DISABLED_OPACITY (0.45f)

struct _LrgButton
{
    LrgWidget  parent_instance;

    gchar     *text;
    GrlFont   *font;

    /* Legacy colours and whether the caller set each one explicitly
     * (an explicit colour replaces the theme default for its state) */
    GrlColor   normal_color;
    GrlColor   hover_color;
    GrlColor   pressed_color;
    GrlColor   text_color;
    gboolean   normal_set;
    gboolean   hover_set;
    gboolean   pressed_set;
    gboolean   text_set;

    gfloat     corner_radius;
    gboolean   radius_set;

    /* Mirrors of the HOVERED / PRESSED state flags */
    gboolean   is_hovered;
    gboolean   is_pressed;

    /* A left press started on the button (for direct event delivery) */
    gboolean   armed;
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

/* Legacy default colours, reported by the getters until a colour is
 * set; the button draws with the theme until then */
static const GrlColor DEFAULT_NORMAL  = { 80, 80, 80, 255 };
static const GrlColor DEFAULT_HOVER   = { 100, 100, 100, 255 };
static const GrlColor DEFAULT_PRESSED = { 60, 60, 60, 255 };
static const GrlColor DEFAULT_TEXT    = { 255, 255, 255, 255 };

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/*
 * readable_on:
 * @bg: a background colour
 * @out: (out): white or near-black, whichever reads better on @bg
 */
static void
readable_on (const GrlColor *bg,
             GrlColor       *out)
{
    static const GrlColor light = { 255, 255, 255, 255 };
    static const GrlColor dark = { 18, 18, 24, 255 };
    GrlColor              opaque = *bg;

    opaque.a = 255;
    if (lrg_paint_color_contrast (&opaque, &light) >= lrg_paint_color_contrast (&opaque, &dark))
        *out = light;
    else
        *out = dark;
}

/*
 * button_font:
 *
 * The font the button draws with: its own (none can be set at the
 * moment, kept for future use) or the theme's UI font. May be %NULL,
 * which the paint functions read as raylib's default font.
 */
static GrlFont *
button_font (LrgButton *self)
{
    if (self->font != NULL)
        return self->font;
    return lrg_theme_get_ui_font (lrg_widget_get_theme (LRG_WIDGET (self)));
}

/* in_canvas: TRUE when the canvas manages our hover / press state */
static gboolean
in_canvas (LrgButton *self)
{
    return LRG_IS_CANVAS (lrg_widget_get_root (LRG_WIDGET (self)));
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

/*
 * box_defaults:
 *
 * The built-in BOX look. The colour family depends on the style
 * class: CONTROL by default, ACCENT for "primary", DANGER for "danger"
 * and a surface that only appears on hover for "flat". The shadow
 * lifts (bigger, further) on hover and settles almost flat while
 * pressed, which together with the squash in paint() makes the button
 * feel physical.
 */
static void
box_defaults (LrgButton      *self,
              LrgWidgetState  state,
              LrgPartLook    *look)
{
    LrgWidget      *widget = LRG_WIDGET (self);
    LrgTheme       *theme = lrg_widget_get_theme (widget);
    gboolean        hovered = (state & LRG_WIDGET_STATE_HOVERED) != 0;
    gboolean        pressed = (state & LRG_WIDGET_STATE_PRESSED) != 0;
    gboolean        primary = lrg_widget_has_class (widget, "primary");
    gboolean        danger = lrg_widget_has_class (widget, "danger");
    gboolean        flat = lrg_widget_has_class (widget, "flat");
    gfloat          blur = lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR);
    gfloat          offset = lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y);
    const GrlColor *shadow = lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW);
    GrlColor        bg;
    GrlColor        sh;
    GrlColor        glow;
    gfloat          shadow_alpha;
    gfloat          lift;

    /* 1. Background colour for the colour family and state */
    if (primary)
    {
        bg = *lrg_theme_get_color (theme, pressed ? LRG_THEME_COLOR_ACCENT_PRESSED
                                          : hovered ? LRG_THEME_COLOR_ACCENT_HOVER
                                          : LRG_THEME_COLOR_ACCENT);
    }
    else if (danger)
    {
        const GrlColor *base = lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);

        lrg_paint_color_shade (base, pressed ? -0.14f : hovered ? 0.10f : 0.0f, &bg);
    }
    else if (flat)
    {
        if (pressed)
            bg = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_PRESSED);
        else if (hovered)
            bg = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER);
        else
            lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER),
                                        0.0f, &bg);
    }
    else
    {
        bg = *lrg_theme_get_color (theme, pressed ? LRG_THEME_COLOR_CONTROL_PRESSED
                                          : hovered ? LRG_THEME_COLOR_CONTROL_HOVER
                                          : LRG_THEME_COLOR_CONTROL);
    }

    /* 2. Explicit legacy colours replace the theme default for their
     * state; a missing hover / pressed colour is derived from the
     * normal one so a single set_normal_color() still animates */
    if (pressed && self->pressed_set)
        bg = self->pressed_color;
    else if (pressed && self->normal_set)
        lrg_paint_color_shade (&self->normal_color, -0.15f, &bg);
    else if (!pressed && hovered && self->hover_set)
        bg = self->hover_color;
    else if (!pressed && hovered && self->normal_set)
        lrg_paint_color_shade (&self->normal_color, 0.12f, &bg);
    else if (!pressed && !hovered && self->normal_set)
        bg = self->normal_color;

    lrg_part_look_set_background (look, &bg);

    /* 3. Shape */
    lrg_part_look_set_radius (look, self->radius_set
                                    ? self->corner_radius
                                    : lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));

    /* 4. Hairline border on neutral buttons so they read on any panel
     * (fades out with the flat surface) */
    if (!primary && !danger && !self->normal_set)
    {
        GrlColor border;

        if (flat && !hovered && !pressed)
            lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
                                        0.0f, &border);
        else
            border = *lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER);
        lrg_part_look_set_border (look, &border,
                                  lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
    }

    /* 5. Shadow: resting, lifted on hover, pressed into the surface */
    lift = pressed ? 0.15f : hovered ? 0.65f : 0.35f;
    shadow_alpha = pressed ? 0.35f : hovered ? 0.75f : 0.5f;
    if (flat && !hovered && !pressed)
        shadow_alpha = 0.0f;
    lrg_paint_color_with_alpha (shadow, ((gfloat) shadow->a / 255.0f) * shadow_alpha, &sh);
    lrg_part_look_set_shadow (look, &sh, MAX (blur * lift, 1.0f), offset * lift);

    /* 6. Soft accent glow while a primary button is hovered */
    if (primary || danger)
    {
        lrg_paint_color_with_alpha (&bg, (hovered && !pressed) ? 0.45f : 0.0f, &glow);
        lrg_part_look_set_glow (look, &glow, MAX (blur * 0.7f, 4.0f));
    }

    if (state & LRG_WIDGET_STATE_DISABLED)
        lrg_part_look_set_opacity (look, BUTTON_DISABLED_OPACITY);
}

/*
 * label_defaults:
 *
 * The built-in LABEL look: TEXT on neutral buttons, TEXT_ON_ACCENT on
 * primary ones and the readable one of white / near-black on danger
 * buttons. An explicit text colour wins.
 */
static void
label_defaults (LrgButton      *self,
                LrgWidgetState  state,
                LrgPartLook    *look)
{
    LrgWidget *widget = LRG_WIDGET (self);
    LrgTheme  *theme = lrg_widget_get_theme (widget);
    GrlColor   fg;

    if (self->text_set)
        fg = self->text_color;
    else if (lrg_widget_has_class (widget, "primary"))
        fg = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_ON_ACCENT);
    else if (lrg_widget_has_class (widget, "danger"))
        readable_on (lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER), &fg);
    else
        fg = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);

    lrg_part_look_set_foreground (look, &fg);

    if (state & LRG_WIDGET_STATE_DISABLED)
        lrg_part_look_set_opacity (look, BUTTON_DISABLED_OPACITY);
}

static void
lrg_button_style_defaults (LrgWidget      *widget,
                           LrgUIPart       part,
                           LrgWidgetState  state,
                           LrgPartLook    *look)
{
    LrgButton *self = LRG_BUTTON (widget);

    switch (part)
    {
    case LRG_UI_PART_BOX:
        box_defaults (self, state, look);
        break;
    case LRG_UI_PART_LABEL:
        label_defaults (self, state, look);
        break;
    case LRG_UI_PART_FOCUS:
        /* The ring follows the button's own corner radius */
        LRG_WIDGET_CLASS (lrg_button_parent_class)->style_defaults (widget, part, state, look);
        if (self->radius_set)
            lrg_part_look_set_radius (look, self->corner_radius + look->border_width + 2.0f);
        break;
    default:
        LRG_WIDGET_CLASS (lrg_button_parent_class)->style_defaults (widget, part, state, look);
        break;
    }
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

/*
 * lrg_button_paint:
 *
 * Records the surface (glow, shadow, fill, border) and the centred
 * text, then squashes both about the centre by the press spring.
 */
static void
lrg_button_paint (LrgWidget    *widget,
                  LrgPaintList *list)
{
    LrgButton   *self = LRG_BUTTON (widget);
    LrgTheme    *theme = lrg_widget_get_theme (widget);
    LrgPartLook  box;
    LrgPartLook  label;
    gfloat       x = lrg_widget_get_world_x (widget);
    gfloat       y = lrg_widget_get_world_y (widget);
    gfloat       w = lrg_widget_get_width (widget);
    gfloat       h = lrg_widget_get_height (widget);
    gfloat       press;
    guint        mark;

    mark = lrg_paint_list_begin_group (list);

    /* Surface */
    lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
    lrg_widget_paint_box (widget, list, &box, x, y, w, h);

    /* Text, centred in both directions */
    if (self->text != NULL && self->text[0] != '\0')
    {
        GrlFont  *font = button_font (self);
        gfloat    size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
        gfloat    tw;
        gfloat    th;
        GrlColor  fg;

        lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &label);
        lrg_paint_measure_text (font, self->text, size, 1.0f, &tw, &th);
        lrg_paint_color_with_alpha (&label.foreground,
                                    ((gfloat) label.foreground.a / 255.0f) * label.opacity, &fg);
        lrg_paint_list_text (list, font, self->text,
                             x + (w - tw) * 0.5f, y + (h - th) * 0.5f,
                             size, 1.0f, &fg);
    }

    /* Squash about the centre while pressed */
    press = lrg_widget_get_press_progress (widget);
    if (press > 0.001f)
        lrg_paint_list_transform_group (list, mark, 1.0f - BUTTON_SQUASH * press,
                                        x + w * 0.5f, y + h * 0.5f, 0.0f, 0.0f, 1.0f);
}

/*
 * lrg_button_measure:
 *
 * Text size plus the theme's control padding, never shorter than the
 * theme's control height.
 */
static void
lrg_button_measure (LrgWidget *widget,
                    gfloat    *preferred_width,
                    gfloat    *preferred_height)
{
    LrgButton *self = LRG_BUTTON (widget);
    LrgTheme  *theme = lrg_widget_get_theme (widget);
    gfloat     pad_x = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);
    gfloat     pad_y = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_Y);
    gfloat     size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
    gfloat     tw = 0.0f;
    gfloat     th = size;

    if (self->text != NULL && self->text[0] != '\0')
        lrg_paint_measure_text (button_font (self), self->text, size, 1.0f, &tw, &th);

    if (preferred_width != NULL)
        *preferred_width = tw + pad_x * 2.0f;
    if (preferred_height != NULL)
        *preferred_height = MAX (th + pad_y * 2.0f,
                                 lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT));
}

/*
 * lrg_button_handle_event:
 *
 * Inside a canvas the canvas maintains HOVERED / PRESSED; the button
 * only turns a release inside itself (after a press on it) or an
 * ACTIVATE into "clicked". When events are delivered directly, without
 * a canvas, the button keeps its own hover / press state from them.
 */
static gboolean
lrg_button_handle_event (LrgWidget        *widget,
                         const LrgUIEvent *event)
{
    LrgButton      *self = LRG_BUTTON (widget);
    LrgUIEventType  type = lrg_ui_event_get_event_type (event);
    gboolean        inside;
    gboolean        managed = in_canvas (self);

    switch (type)
    {
    case LRG_UI_EVENT_MOUSE_MOVE:
        if (!managed)
        {
            inside = lrg_widget_contains_point (widget, lrg_ui_event_get_x (event),
                                                lrg_ui_event_get_y (event));
            if (inside)
                lrg_widget_set_state_flags (widget, LRG_WIDGET_STATE_HOVERED);
            else
                lrg_widget_unset_state_flags (widget, LRG_WIDGET_STATE_HOVERED);
        }
        /* Moves are never consumed */
        return FALSE;

    case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
        inside = lrg_widget_contains_point (widget, lrg_ui_event_get_x (event),
                                            lrg_ui_event_get_y (event));
        if (inside && lrg_ui_event_get_button (event) == GRL_MOUSE_BUTTON_LEFT)
        {
            self->armed = TRUE;
            if (!managed)
                lrg_widget_set_state_flags (widget, LRG_WIDGET_STATE_PRESSED);
            return TRUE;
        }
        break;

    case LRG_UI_EVENT_MOUSE_BUTTON_UP:
        if (self->armed && lrg_ui_event_get_button (event) == GRL_MOUSE_BUTTON_LEFT)
        {
            inside = lrg_widget_contains_point (widget, lrg_ui_event_get_x (event),
                                                lrg_ui_event_get_y (event));
            self->armed = FALSE;
            if (!managed)
                lrg_widget_unset_state_flags (widget, LRG_WIDGET_STATE_PRESSED);
            if (inside)
                g_signal_emit (self, signals[SIGNAL_CLICKED], 0);
            return TRUE;
        }
        break;

    case LRG_UI_EVENT_ACTIVATE:
        g_signal_emit (self, signals[SIGNAL_CLICKED], 0);
        return TRUE;

    default:
        break;
    }

    return FALSE;
}

/*
 * on_state_changed:
 *
 * Keeps the legacy is-hovered / is-pressed properties and the
 * "hovered" signal in step with the widget state flags.
 */
static void
on_state_changed (LrgWidget      *widget,
                  LrgWidgetState  previous,
                  gpointer        user_data)
{
    LrgButton      *self = LRG_BUTTON (widget);
    LrgWidgetState  state = lrg_widget_get_state_flags (widget);
    gboolean        hovered = (state & LRG_WIDGET_STATE_HOVERED) != 0;
    gboolean        pressed = (state & LRG_WIDGET_STATE_PRESSED) != 0;

    if (hovered != self->is_hovered)
    {
        self->is_hovered = hovered;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_HOVERED]);
        g_signal_emit (self, signals[SIGNAL_HOVERED], 0, hovered);
    }
    if (pressed != self->is_pressed)
    {
        self->is_pressed = pressed;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_PRESSED]);
    }
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
        if (g_value_get_boxed (value) != NULL)
            lrg_button_set_normal_color (self, g_value_get_boxed (value));
        break;
    case PROP_HOVER_COLOR:
        if (g_value_get_boxed (value) != NULL)
            lrg_button_set_hover_color (self, g_value_get_boxed (value));
        break;
    case PROP_PRESSED_COLOR:
        if (g_value_get_boxed (value) != NULL)
            lrg_button_set_pressed_color (self, g_value_get_boxed (value));
        break;
    case PROP_TEXT_COLOR:
        if (g_value_get_boxed (value) != NULL)
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

    widget_class->paint = lrg_button_paint;
    widget_class->style_defaults = lrg_button_style_defaults;
    widget_class->measure = lrg_button_measure;
    widget_class->handle_event = lrg_button_handle_event;

    /**
     * LrgButton:text:
     *
     * The button's label text. Changing it queues a layout.
     */
    properties[PROP_TEXT] =
        g_param_spec_string ("text",
                             "Text",
                             "The button label",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgButton:normal-color:
     *
     * Background at rest. Until it is set the button uses its theme
     * (CONTROL, or ACCENT / DANGER for the "primary" / "danger"
     * classes); once set it replaces that default.
     */
    properties[PROP_NORMAL_COLOR] =
        g_param_spec_boxed ("normal-color",
                            "Normal Color",
                            "Background when idle",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgButton:hover-color:
     *
     * Background under the pointer. Defaults to the theme's hover
     * colour, or a lighter shade of #LrgButton:normal-color when only
     * that was set.
     */
    properties[PROP_HOVER_COLOR] =
        g_param_spec_boxed ("hover-color",
                            "Hover Color",
                            "Background when hovered",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgButton:pressed-color:
     *
     * Background while pressed. Defaults to the theme's pressed
     * colour, or a darker shade of #LrgButton:normal-color when only
     * that was set.
     */
    properties[PROP_PRESSED_COLOR] =
        g_param_spec_boxed ("pressed-color",
                            "Pressed Color",
                            "Background when pressed",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgButton:text-color:
     *
     * Label colour. Defaults to the theme's TEXT (TEXT_ON_ACCENT for
     * "primary" buttons); once set it is used in every state.
     */
    properties[PROP_TEXT_COLOR] =
        g_param_spec_boxed ("text-color",
                            "Text Color",
                            "The label color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgButton:corner-radius:
     *
     * Corner radius. The default (4) is only reported: the button
     * follows the theme's CORNER_RADIUS until a radius is set.
     */
    properties[PROP_CORNER_RADIUS] =
        g_param_spec_float ("corner-radius",
                            "Corner Radius",
                            "Rounded corner radius",
                            0.0f, G_MAXFLOAT, 4.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgButton:is-hovered:
     *
     * Whether the pointer is over the button (mirrors
     * %LRG_WIDGET_STATE_HOVERED).
     */
    properties[PROP_IS_HOVERED] =
        g_param_spec_boolean ("is-hovered",
                              "Is Hovered",
                              "Whether the button is hovered",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgButton:is-pressed:
     *
     * Whether the button is held down (mirrors
     * %LRG_WIDGET_STATE_PRESSED).
     */
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
     * Emitted when the button is clicked (pressed and released
     * inside), or activated with Enter, Space or gamepad A while
     * focused.
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
    lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
    self->text = NULL;
    self->font = NULL;
    self->normal_color = DEFAULT_NORMAL;
    self->hover_color = DEFAULT_HOVER;
    self->pressed_color = DEFAULT_PRESSED;
    self->text_color = DEFAULT_TEXT;
    self->corner_radius = 4.0f;

    g_signal_connect (self, "state-changed", G_CALLBACK (on_state_changed), NULL);
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
        lrg_widget_queue_layout (LRG_WIDGET (self));
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
    self->normal_set = TRUE;
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
    self->hover_set = TRUE;
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
    self->pressed_set = TRUE;
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
    self->text_set = TRUE;
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

    self->radius_set = TRUE;
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
