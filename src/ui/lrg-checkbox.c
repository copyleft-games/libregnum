/* lrg-checkbox.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Toggle widget drawn as a check box, a radio button or a switch.
 *
 * The indicator sits on the left with the label to its right. A
 * "check" spring follows the checked state and drives every
 * transition: the check mark draws itself in, the radio dot grows and
 * the switch knob slides (with the theme's overshoot). Colours cross-
 * fade between the unchecked and checked looks by the same spring, and
 * between resting / hovered / pressed by the widget's own springs.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-checkbox.h"
#include "lrg-canvas.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

/* Switch track is this many box sizes wide */
#define SWITCH_ASPECT (1.75f)

/* Gap between the switch track and its knob */
#define SWITCH_INSET (3.0f)

/* Opacity of a disabled checkbox */
#define CHECKBOX_DISABLED_OPACITY (0.45f)

struct _LrgCheckbox
{
    LrgWidget         parent_instance;

    gchar            *label;
    gboolean          checked;
    LrgCheckboxStyle  style;

    /* Legacy sizes and colours; each *_set flag records an explicit
     * value, which then wins over the theme default */
    gfloat            box_size;
    gfloat            spacing;
    gfloat            font_size;
    gboolean          box_size_set;
    gboolean          spacing_set;
    gboolean          font_size_set;
    GrlColor          box_color;
    GrlColor          check_color;
    GrlColor          text_color;
    gboolean          box_color_set;
    gboolean          check_color_set;
    gboolean          text_color_set;

    /* Check animation: 0 = unchecked, 1 = checked. Until the first
     * step() the spring is ignored and the checked state is shown
     * directly, so freshly built UIs do not animate in. */
    LrgSpring         check;
    gboolean          stepped;
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
    PROP_STYLE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_TOGGLED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Legacy default colours, reported by the getters until set */
static const GrlColor DEFAULT_BOX   = { 100, 100, 100, 255 };
static const GrlColor DEFAULT_CHECK = { 50, 200, 50, 255 };
static const GrlColor DEFAULT_TEXT  = { 255, 255, 255, 255 };

/* ==========================================================================
 * Metrics
 * ========================================================================== */

static gfloat
effective_box_size (LrgCheckbox *self)
{
    if (self->box_size_set)
        return self->box_size;
    return lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)),
                                 LRG_THEME_METRIC_CHECK_SIZE);
}

static gfloat
effective_spacing (LrgCheckbox *self)
{
    if (self->spacing_set)
        return self->spacing;
    return lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)),
                                 LRG_THEME_METRIC_SPACING);
}

static gfloat
effective_font_size (LrgCheckbox *self)
{
    if (self->font_size_set)
        return self->font_size;
    return lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)),
                                 LRG_THEME_METRIC_FONT_SIZE);
}

/*
 * indicator_size:
 *
 * Width and height of the indicator: a square box for CHECK / RADIO,
 * a wider pill for SWITCH.
 */
static void
indicator_size (LrgCheckbox *self,
                gfloat      *width,
                gfloat      *height)
{
    gfloat box = effective_box_size (self);

    *height = box;
    *width = self->style == LRG_CHECKBOX_STYLE_SWITCH ? box * SWITCH_ASPECT : box;
}

static GrlFont *
checkbox_font (LrgCheckbox *self)
{
    return lrg_theme_get_ui_font (lrg_widget_get_theme (LRG_WIDGET (self)));
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

/*
 * indicator_defaults:
 *
 * BOX (check / radio) and TRACK (switch) share one recipe: CONTROL
 * with a BORDER outline when off, ACCENT when on, each with hover and
 * pressed shades. Explicit box / check colours replace the off / on
 * fills.
 */
static void
indicator_defaults (LrgCheckbox    *self,
                    LrgUIPart       part,
                    LrgWidgetState  state,
                    LrgPartLook    *look)
{
    LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));
    gboolean  on = (state & LRG_WIDGET_STATE_CHECKED) != 0;
    gboolean  hovered = (state & LRG_WIDGET_STATE_HOVERED) != 0;
    gboolean  pressed = (state & LRG_WIDGET_STATE_PRESSED) != 0;
    gfloat    bw = lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH);
    GrlColor  bg;
    GrlColor  border;

    if (on)
    {
        if (self->check_color_set)
            lrg_paint_color_shade (&self->check_color, pressed ? -0.12f : hovered ? 0.1f : 0.0f, &bg);
        else
            bg = *lrg_theme_get_color (theme, pressed ? LRG_THEME_COLOR_ACCENT_PRESSED
                                              : hovered ? LRG_THEME_COLOR_ACCENT_HOVER
                                              : LRG_THEME_COLOR_ACCENT);
        border = bg;
    }
    else
    {
        if (self->box_color_set)
            lrg_paint_color_shade (&self->box_color, pressed ? -0.12f : hovered ? 0.1f : 0.0f, &bg);
        else if (part == LRG_UI_PART_TRACK)
            lrg_paint_color_shade (lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK),
                                   pressed ? -0.06f : hovered ? 0.08f : 0.0f, &bg);
        else
            bg = *lrg_theme_get_color (theme, pressed ? LRG_THEME_COLOR_CONTROL_PRESSED
                                              : hovered ? LRG_THEME_COLOR_CONTROL_HOVER
                                              : LRG_THEME_COLOR_CONTROL);
        border = *lrg_theme_get_color (theme, hovered ? LRG_THEME_COLOR_TEXT_MUTED
                                                      : LRG_THEME_COLOR_BORDER);
    }

    lrg_part_look_set_background (look, &bg);
    if (part == LRG_UI_PART_BOX)
        lrg_part_look_set_border (look, &border, MAX (bw, 1.5f));

    /* Radius: rounded square, circle for radio, pill for switch */
    if (self->style == LRG_CHECKBOX_STYLE_CHECK)
        lrg_part_look_set_radius (look, MAX (lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS) * 0.6f,
                                             2.0f));
    else
        lrg_part_look_set_radius (look, G_MAXFLOAT);

    /* A soft accent glow on hover once checked */
    if (on)
    {
        GrlColor glow;

        lrg_paint_color_with_alpha (&bg, hovered ? 0.35f : 0.0f, &glow);
        lrg_part_look_set_glow (look, &glow, 8.0f);
    }

    if (state & LRG_WIDGET_STATE_DISABLED)
        lrg_part_look_set_opacity (look, CHECKBOX_DISABLED_OPACITY);
}

static void
lrg_checkbox_style_defaults (LrgWidget      *widget,
                             LrgUIPart       part,
                             LrgWidgetState  state,
                             LrgPartLook    *look)
{
    LrgCheckbox *self = LRG_CHECKBOX (widget);
    LrgTheme    *theme = lrg_widget_get_theme (widget);

    switch (part)
    {
    case LRG_UI_PART_BOX:
    case LRG_UI_PART_TRACK:
        indicator_defaults (self, part, state, look);
        break;

    case LRG_UI_PART_MARK:
        /* Check mark / radio dot: TEXT_ON_ACCENT, size = stroke width */
        lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_ON_ACCENT));
        lrg_part_look_set_size (look, MAX (effective_box_size (self) * 0.12f, 1.5f));
        if (state & LRG_WIDGET_STATE_DISABLED)
            lrg_part_look_set_opacity (look, CHECKBOX_DISABLED_OPACITY);
        break;

    case LRG_UI_PART_KNOB:
        {
            /* Switch knob: HANDLE with a small shadow, grows on press */
            const GrlColor *shadow = lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW);
            GrlColor        sh;
            gfloat          box = effective_box_size (self);

            lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_HANDLE));
            lrg_part_look_set_radius (look, G_MAXFLOAT);
            lrg_paint_color_with_alpha (shadow, ((gfloat) shadow->a / 255.0f) * 0.6f, &sh);
            lrg_part_look_set_shadow (look, &sh, 4.0f, 1.0f);
            lrg_part_look_set_size (look, (state & LRG_WIDGET_STATE_PRESSED)
                                          ? box - SWITCH_INSET * 2.0f + 2.0f
                                          : box - SWITCH_INSET * 2.0f);
            if (state & LRG_WIDGET_STATE_DISABLED)
                lrg_part_look_set_opacity (look, CHECKBOX_DISABLED_OPACITY);
        }
        break;

    case LRG_UI_PART_LABEL:
        if (state & LRG_WIDGET_STATE_DISABLED)
            lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_DISABLED));
        else if (self->text_color_set)
            lrg_part_look_set_foreground (look, &self->text_color);
        else
            lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
        break;

    default:
        LRG_WIDGET_CLASS (lrg_checkbox_parent_class)->style_defaults (widget, part, state, look);
        break;
    }
}

/*
 * resolve_for:
 *
 * Like lrg_widget_resolve_look(), but for the widget's state with
 * CHECKED forced on or off, so the unchecked and checked looks can be
 * cross-faded by the check spring while hover / press still blend.
 */
static void
resolve_for (LrgCheckbox *self,
             LrgUIPart    part,
             gboolean     on,
             LrgPartLook *look)
{
    LrgWidget      *widget = LRG_WIDGET (self);
    LrgWidgetState  base;
    gfloat          h = CLAMP (lrg_widget_get_hover_progress (widget), 0.0f, 1.0f);
    gfloat          p = CLAMP (lrg_widget_get_press_progress (widget), 0.0f, 1.0f);
    LrgPartLook     other;

    base = lrg_widget_get_state_flags (widget) &
           ~(LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED | LRG_WIDGET_STATE_CHECKED);
    if (on)
        base |= LRG_WIDGET_STATE_CHECKED;

    lrg_widget_resolve_look_for_state (widget, part, base, look);
    if (h > 0.001f)
    {
        lrg_widget_resolve_look_for_state (widget, part, base | LRG_WIDGET_STATE_HOVERED, &other);
        lrg_part_look_lerp (look, &other, h, look);
    }
    if (p > 0.001f)
    {
        lrg_widget_resolve_look_for_state (widget, part,
                                           base | LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED,
                                           &other);
        lrg_part_look_lerp (look, &other, p, look);
    }
}

/* resolve_checked: the look blended between off and on by @t */
static void
resolve_checked (LrgCheckbox *self,
                 LrgUIPart    part,
                 gfloat       t,
                 LrgPartLook *look)
{
    LrgPartLook on;

    resolve_for (self, part, FALSE, look);
    if (t > 0.001f)
    {
        resolve_for (self, part, TRUE, &on);
        lrg_part_look_lerp (look, &on, CLAMP (t, 0.0f, 1.0f), look);
    }
}

/* look_color: @c with the look's opacity folded into its alpha */
static void
look_color (const LrgPartLook *look,
            const GrlColor    *c,
            GrlColor          *out)
{
    lrg_paint_color_with_alpha (c, ((gfloat) c->a / 255.0f) * look->opacity, out);
}

/* ==========================================================================
 * Painting
 * ========================================================================== */

/*
 * check_value:
 *
 * The check progress paint() shows: the spring once the widget has
 * been stepped, the checked state itself before that.
 */
static gfloat
check_value (LrgCheckbox *self)
{
    if (!self->stepped)
        return self->checked ? 1.0f : 0.0f;
    return self->check.value;
}

/*
 * lrg_checkbox_step:
 *
 * Advances the check spring by the canvas frame time. The first step
 * snaps the spring to the current state (a checkbox appearing for the
 * first time shows its state rather than animating to it).
 *
 * Returns: %TRUE while the check spring is still moving
 */
static gboolean
lrg_checkbox_step (LrgWidget *widget,
                   gfloat     delta_time)
{
    LrgCheckbox *self = LRG_CHECKBOX (widget);

    lrg_spring_set_motion (&self->check, lrg_widget_get_motion (widget));
    if (!self->stepped)
    {
        lrg_spring_snap (&self->check, self->checked ? 1.0f : 0.0f);
        self->stepped = TRUE;
        return FALSE;
    }

    return lrg_spring_step (&self->check, delta_time);
}

/*
 * paint_check_mark:
 *
 * Two thick strokes forming a tick, drawn in along their length as
 * @t goes from 0 to 1: the short stroke takes the first 40 %.
 */
static void
paint_check_mark (LrgPaintList   *list,
                  gfloat          x,
                  gfloat          y,
                  gfloat          s,
                  gfloat          t,
                  gfloat          stroke,
                  const GrlColor *color)
{
    gfloat ax = x + s * 0.26f;
    gfloat ay = y + s * 0.52f;
    gfloat bx = x + s * 0.43f;
    gfloat by = y + s * 0.69f;
    gfloat cx = x + s * 0.75f;
    gfloat cy = y + s * 0.33f;
    gfloat u;

    t = CLAMP (t, 0.0f, 1.0f);
    if (t <= 0.0f)
        return;

    u = MIN (t / 0.4f, 1.0f);
    lrg_paint_list_line (list, ax, ay, ax + (bx - ax) * u, ay + (by - ay) * u, stroke, color);
    if (t > 0.4f)
    {
        u = (t - 0.4f) / 0.6f;
        lrg_paint_list_line (list, bx, by, bx + (cx - bx) * u, by + (cy - by) * u, stroke, color);
    }
}

static void
lrg_checkbox_paint (LrgWidget    *widget,
                    LrgPaintList *list)
{
    LrgCheckbox *self = LRG_CHECKBOX (widget);
    LrgPartLook  box;
    LrgPartLook  mark;
    GrlColor     c;
    gfloat       x = lrg_widget_get_world_x (widget);
    gfloat       y = lrg_widget_get_world_y (widget);
    gfloat       h = lrg_widget_get_height (widget);
    gfloat       iw;
    gfloat       ih;
    gfloat       iy;
    gfloat       t;

    t = check_value (self);

    indicator_size (self, &iw, &ih);
    iy = y + (h - ih) * 0.5f;

    /* 1. The indicator */
    switch (self->style)
    {
    case LRG_CHECKBOX_STYLE_SWITCH:
        {
            LrgPartLook knob;
            gfloat      ks;
            gfloat      travel;
            gfloat      kx;

            resolve_checked (self, LRG_UI_PART_TRACK, t, &box);
            lrg_widget_paint_box (widget, list, &box, x, iy, iw, ih);

            /* Knob slides with the spring (overshoot allowed) */
            lrg_widget_resolve_look (widget, LRG_UI_PART_KNOB, &knob);
            ks = knob.size > 0.0f ? knob.size : ih - SWITCH_INSET * 2.0f;
            travel = iw - ih;
            kx = x + SWITCH_INSET + travel * t + ((ih - SWITCH_INSET * 2.0f) - ks) * 0.5f;
            lrg_widget_paint_box (widget, list, &knob, kx, iy + (ih - ks) * 0.5f, ks, ks);
        }
        break;

    case LRG_CHECKBOX_STYLE_RADIO:
        resolve_checked (self, LRG_UI_PART_BOX, t, &box);
        lrg_widget_paint_box (widget, list, &box, x, iy, iw, ih);

        /* The dot grows from the centre */
        resolve_checked (self, LRG_UI_PART_MARK, 1.0f, &mark);
        look_color (&mark, &mark.foreground, &c);
        if (t > 0.01f)
            lrg_paint_list_circle (list, x + iw * 0.5f, iy + ih * 0.5f,
                                   MAX (ih * 0.2f * t, 0.0f), &c);
        break;

    case LRG_CHECKBOX_STYLE_CHECK:
    default:
        resolve_checked (self, LRG_UI_PART_BOX, t, &box);
        lrg_widget_paint_box (widget, list, &box, x, iy, iw, ih);

        resolve_checked (self, LRG_UI_PART_MARK, 1.0f, &mark);
        look_color (&mark, &mark.foreground, &c);
        paint_check_mark (list, x, iy, ih, t, mark.size, &c);
        break;
    }

    /* 2. The label, vertically centred to the right */
    if (self->label != NULL && self->label[0] != '\0')
    {
        LrgPartLook label;
        GrlFont    *font = checkbox_font (self);
        gfloat      size = effective_font_size (self);
        gfloat      th;

        lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &label);
        look_color (&label, &label.foreground, &c);
        lrg_paint_measure_text (font, self->label, size, 1.0f, NULL, &th);
        lrg_paint_list_text (list, font, self->label, x + iw + effective_spacing (self),
                             y + (h - th) * 0.5f, size, 1.0f, &c);
    }
}

/*
 * lrg_checkbox_measure:
 *
 * Indicator, spacing and label side by side; as tall as the taller of
 * the indicator and the text.
 */
static void
lrg_checkbox_measure (LrgWidget *widget,
                      gfloat    *preferred_width,
                      gfloat    *preferred_height)
{
    LrgCheckbox *self = LRG_CHECKBOX (widget);
    gfloat       iw;
    gfloat       ih;
    gfloat       tw = 0.0f;
    gfloat       th = 0.0f;

    indicator_size (self, &iw, &ih);
    if (self->label != NULL && self->label[0] != '\0')
    {
        lrg_paint_measure_text (checkbox_font (self), self->label, effective_font_size (self),
                                1.0f, &tw, &th);
        tw += effective_spacing (self);
    }

    if (preferred_width != NULL)
        *preferred_width = iw + tw;
    if (preferred_height != NULL)
        *preferred_height = MAX (ih, th);
}

/*
 * lrg_checkbox_handle_event:
 *
 * A left release inside toggles (as before); presses inside are
 * consumed so they do not reach the parent. ACTIVATE (Enter, Space,
 * gamepad A) toggles the focused checkbox.
 */
static gboolean
lrg_checkbox_handle_event (LrgWidget        *widget,
                           const LrgUIEvent *event)
{
    LrgCheckbox    *self = LRG_CHECKBOX (widget);
    LrgUIEventType  type = lrg_ui_event_get_event_type (event);
    gboolean        inside;

    switch (type)
    {
    case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
    case LRG_UI_EVENT_MOUSE_BUTTON_UP:
        if (lrg_ui_event_get_button (event) != GRL_MOUSE_BUTTON_LEFT)
            return FALSE;
        inside = lrg_widget_contains_point (widget, lrg_ui_event_get_x (event),
                                            lrg_ui_event_get_y (event));
        if (!inside)
            return FALSE;
        if (type == LRG_UI_EVENT_MOUSE_BUTTON_UP)
            lrg_checkbox_toggle (self);
        return TRUE;

    case LRG_UI_EVENT_ACTIVATE:
        lrg_checkbox_toggle (self);
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
    case PROP_STYLE:
        g_value_set_enum (value, self->style);
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
        if (g_value_get_boxed (value) != NULL)
            lrg_checkbox_set_box_color (self, g_value_get_boxed (value));
        break;
    case PROP_CHECK_COLOR:
        if (g_value_get_boxed (value) != NULL)
            lrg_checkbox_set_check_color (self, g_value_get_boxed (value));
        break;
    case PROP_TEXT_COLOR:
        if (g_value_get_boxed (value) != NULL)
            lrg_checkbox_set_text_color (self, g_value_get_boxed (value));
        break;
    case PROP_STYLE:
        lrg_checkbox_set_style (self, g_value_get_enum (value));
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

    widget_class->paint = lrg_checkbox_paint;
    widget_class->step = lrg_checkbox_step;
    widget_class->style_defaults = lrg_checkbox_style_defaults;
    widget_class->measure = lrg_checkbox_measure;
    widget_class->handle_event = lrg_checkbox_handle_event;

    /**
     * LrgCheckbox:checked:
     *
     * Whether the checkbox is checked. Mirrored by
     * %LRG_WIDGET_STATE_CHECKED, so style rules can target it.
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
     * The text shown to the right of the indicator.
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
     * Size of the indicator (its height; a switch is wider). The
     * default (20) is only reported: until set, the theme's
     * CHECK_SIZE is used.
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
     * Gap between the indicator and the label. Until set, the theme's
     * SPACING is used.
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
     * Label size. Until set, the theme's FONT_SIZE is used.
     */
    properties[PROP_FONT_SIZE] =
        g_param_spec_float ("font-size",
                            "Font Size",
                            "The label font size",
                            1.0f, G_MAXFLOAT, 20.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgCheckbox:box-color:
     *
     * Fill of the indicator while unchecked. Until set, the theme's
     * CONTROL (TRACK for switches) is used.
     */
    properties[PROP_BOX_COLOR] =
        g_param_spec_boxed ("box-color",
                            "Box Color",
                            "The checkbox box color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgCheckbox:check-color:
     *
     * Fill of the indicator while checked. Until set, the theme's
     * ACCENT is used.
     */
    properties[PROP_CHECK_COLOR] =
        g_param_spec_boxed ("check-color",
                            "Check Color",
                            "The checkmark color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgCheckbox:text-color:
     *
     * Label colour. Until set, the theme's TEXT is used.
     */
    properties[PROP_TEXT_COLOR] =
        g_param_spec_boxed ("text-color",
                            "Text Color",
                            "The label text color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgCheckbox:style:
     *
     * How the indicator is drawn: a check box, a radio button or a
     * sliding switch.
     */
    properties[PROP_STYLE] =
        g_param_spec_enum ("style",
                           "Style",
                           "Indicator style",
                           LRG_TYPE_CHECKBOX_STYLE,
                           LRG_CHECKBOX_STYLE_CHECK,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgCheckbox::toggled:
     * @self: the checkbox
     * @checked: the new state
     *
     * Emitted when the checked state changes, by the user or through
     * lrg_checkbox_set_checked().
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
    lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
    self->label = NULL;
    self->checked = FALSE;
    self->style = LRG_CHECKBOX_STYLE_CHECK;
    self->box_size = 20.0f;
    self->spacing = 8.0f;
    self->font_size = 20.0f;
    self->box_color = DEFAULT_BOX;
    self->check_color = DEFAULT_CHECK;
    self->text_color = DEFAULT_TEXT;
    lrg_spring_init (&self->check, 0.0f, LRG_MOTION_SMOOTH);
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

LrgCheckbox *
lrg_checkbox_new_with_style (const gchar      *label,
                             LrgCheckboxStyle  style)
{
    return g_object_new (LRG_TYPE_CHECKBOX,
                         "label", label,
                         "style", style,
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
    if (self->checked == checked)
        return;

    self->checked = checked;

    /* State flag for style rules, spring for the animation */
    if (checked)
        lrg_widget_set_state_flags (LRG_WIDGET (self), LRG_WIDGET_STATE_CHECKED);
    else
        lrg_widget_unset_state_flags (LRG_WIDGET (self), LRG_WIDGET_STATE_CHECKED);
    lrg_spring_set_target (&self->check, checked ? 1.0f : 0.0f);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CHECKED]);
    g_signal_emit (self, signals[SIGNAL_TOGGLED], 0, checked);
}

void
lrg_checkbox_toggle (LrgCheckbox *self)
{
    g_return_if_fail (LRG_IS_CHECKBOX (self));
    lrg_checkbox_set_checked (self, !self->checked);
}

LrgCheckboxStyle
lrg_checkbox_get_style (LrgCheckbox *self)
{
    g_return_val_if_fail (LRG_IS_CHECKBOX (self), LRG_CHECKBOX_STYLE_CHECK);
    return self->style;
}

void
lrg_checkbox_set_style (LrgCheckbox      *self,
                        LrgCheckboxStyle  style)
{
    g_return_if_fail (LRG_IS_CHECKBOX (self));

    if (self->style == style)
        return;
    self->style = style;
    lrg_widget_queue_layout (LRG_WIDGET (self));
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STYLE]);
}

gfloat
lrg_checkbox_get_check_progress (LrgCheckbox *self)
{
    g_return_val_if_fail (LRG_IS_CHECKBOX (self), 0.0f);
    return check_value (self);
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
        lrg_widget_queue_layout (LRG_WIDGET (self));
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

    self->box_size_set = TRUE;
    if (self->box_size != size)
    {
        self->box_size = size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOX_SIZE]);
    }
    lrg_widget_queue_layout (LRG_WIDGET (self));
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

    self->spacing_set = TRUE;
    if (self->spacing != spacing)
    {
        self->spacing = spacing;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPACING]);
    }
    lrg_widget_queue_layout (LRG_WIDGET (self));
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
    self->box_color_set = TRUE;
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
    self->check_color_set = TRUE;
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
    self->text_color_set = TRUE;
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

    self->font_size_set = TRUE;
    if (self->font_size != size)
    {
        self->font_size = size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE]);
    }
    lrg_widget_queue_layout (LRG_WIDGET (self));
}
