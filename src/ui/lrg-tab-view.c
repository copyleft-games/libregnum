/* lrg-tab-view.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tab view container widget implementation.
 *
 * The view records itself through paint(): a BOX part behind the
 * content area, one TAB part per header (resting, hovered and
 * SELECTED looks, cross-faded by a per-tab hover spring) and an
 * INDICATOR strip that slides and resizes towards the active tab on
 * springs. The active tab's content is painted last, clipped to the
 * content area.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-tab-view.h"
#include "lrg-canvas.h"
#include "lrg-widget-private.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"
#include <graylib.h>

/* Longest frame step used for the view's own springs */
#define MAX_FRAME_STEP (0.1f)

/* Default indicator thickness (the INDICATOR look's size overrides) */
#define INDICATOR_THICKNESS (3.0f)

/* ==========================================================================
 * Tab Entry Structure
 * ========================================================================== */

typedef struct
{
    gchar     *label;
    LrgWidget *content;
    LrgSpring  hover;    /* 0 resting .. 1 hovered, for the header look */
} LrgTabEntry;

static void
lrg_tab_entry_free (LrgTabEntry *entry)
{
    if (entry == NULL)
        return;

    g_free (entry->label);
    g_clear_object (&entry->content);
    g_free (entry);
}

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgTabView
{
    LrgContainer    parent_instance;

    GPtrArray      *tabs;           /* Array of LrgTabEntry */
    guint           active_tab;
    LrgTabPosition  tab_position;
    gfloat          tab_height;
    gint            hovered_tab;    /* -1 if none */

    /* Indicator position / width relative to the view's left edge */
    LrgSpring       indicator_x;
    LrgSpring       indicator_w;
    gboolean        painted;        /* springs snap until the first paint */
    gint64          last_us;        /* monotonic time of the last paint */
};

G_DEFINE_TYPE (LrgTabView, lrg_tab_view, LRG_TYPE_CONTAINER)

enum
{
    PROP_0,
    PROP_ACTIVE_TAB,
    PROP_TAB_POSITION,
    PROP_TAB_HEIGHT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_TAB_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static LrgTabEntry *
lrg_tab_view_get_entry (LrgTabView *self,
                        guint       index)
{
    if (index >= self->tabs->len)
        return NULL;

    return g_ptr_array_index (self->tabs, index);
}

/* Every header gets an equal share of the view's width */
static gfloat
lrg_tab_view_get_tab_width (LrgTabView *self)
{
    if (self->tabs->len == 0)
        return 0.0f;

    return lrg_widget_get_width (LRG_WIDGET (self)) / (gfloat) self->tabs->len;
}

/* tab_bar_top: world Y of the header strip */
static gfloat
tab_bar_top (LrgTabView *self)
{
    gfloat y = lrg_widget_get_world_y (LRG_WIDGET (self));

    if (self->tab_position == LRG_TAB_POSITION_TOP)
        return y;
    return y + lrg_widget_get_height (LRG_WIDGET (self)) - self->tab_height;
}

static gint
lrg_tab_view_get_tab_at_point (LrgTabView *self,
                               gfloat      x,
                               gfloat      y)
{
    gfloat top;
    gfloat tab_width;
    gint   tab_index;

    if (self->tabs->len == 0)
        return -1;

    /* Within the header strip vertically? */
    top = tab_bar_top (self);
    if (y < top || y > top + self->tab_height)
        return -1;

    tab_width = lrg_tab_view_get_tab_width (self);
    if (tab_width <= 0.0f)
        return -1;
    tab_index = (gint) ((x - lrg_widget_get_world_x (LRG_WIDGET (self))) / tab_width);

    if (tab_index < 0 || (guint) tab_index >= self->tabs->len)
        return -1;

    return tab_index;
}

/*
 * indicator_target:
 *
 * Where the indicator should sit for the active tab, relative to the
 * view's left edge: under the header, inset from its sides so that
 * neighbouring indicators would never touch.
 */
static void
indicator_target (LrgTabView *self,
                  gfloat     *x,
                  gfloat     *w)
{
    gfloat tab_w = lrg_tab_view_get_tab_width (self);
    gfloat inset = MAX (tab_w * 0.12f, 4.0f);

    if (inset * 2.0f >= tab_w)
        inset = 0.0f;
    *x = (gfloat) self->active_tab * tab_w + inset;
    *w = tab_w - inset * 2.0f;
}

/* retarget: point the indicator springs at the active tab */
static void
retarget (LrgTabView *self)
{
    gfloat x;
    gfloat w;

    indicator_target (self, &x, &w);
    lrg_spring_set_target (&self->indicator_x, x);
    lrg_spring_set_target (&self->indicator_w, w);
}

/*
 * advance:
 *
 * Steps the view's own springs (indicator, per-tab hover) by the real
 * time since the last paint, clamped to %MAX_FRAME_STEP. Before the
 * first paint everything snaps, so a new view does not animate in.
 */
static void
advance (LrgTabView *self)
{
    LrgMotion motion = lrg_widget_get_motion (LRG_WIDGET (self));
    gint64    now = g_get_monotonic_time ();
    gfloat    dt;
    guint     i;

    retarget (self);
    dt = self->painted ? CLAMP ((gfloat) (now - self->last_us) / (gfloat) G_USEC_PER_SEC,
                                0.0f, MAX_FRAME_STEP)
                       : 0.0f;
    self->last_us = now;

    lrg_spring_set_motion (&self->indicator_x, motion);
    lrg_spring_set_motion (&self->indicator_w, motion);
    if (!self->painted)
    {
        lrg_spring_snap (&self->indicator_x, self->indicator_x.target);
        lrg_spring_snap (&self->indicator_w, self->indicator_w.target);
    }
    lrg_spring_step (&self->indicator_x, dt);
    lrg_spring_step (&self->indicator_w, dt);

    for (i = 0; i < self->tabs->len; i++)
    {
        LrgTabEntry *entry = g_ptr_array_index (self->tabs, i);

        lrg_spring_set_motion (&entry->hover, motion);
        lrg_spring_set_target (&entry->hover, (gint) i == self->hovered_tab ? 1.0f : 0.0f);
        lrg_spring_step (&entry->hover, dt);
    }

    self->painted = TRUE;
}

/* set_hovered_tab: which header is under the pointer (-1 for none) */
static void
set_hovered_tab (LrgTabView *self,
                 gint        index)
{
    self->hovered_tab = index;
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

/*
 * lrg_tab_view_style_defaults:
 *
 * BOX: the content surface, PANEL with the theme's corner radius.
 * TAB: transparent with TEXT_MUTED text at rest; CONTROL_HOVER behind
 * a TEXT label when hovered; TEXT (and a faint accent wash) when
 * SELECTED. INDICATOR: an ACCENT pill whose @size is its thickness,
 * with a soft accent glow.
 */
static void
lrg_tab_view_style_defaults (LrgWidget      *widget,
                             LrgUIPart       part,
                             LrgWidgetState  state,
                             LrgPartLook    *look)
{
    LrgTheme *theme = lrg_widget_get_theme (widget);
    gboolean  hovered = (state & LRG_WIDGET_STATE_HOVERED) != 0;
    gboolean  selected = (state & LRG_WIDGET_STATE_SELECTED) != 0;
    GrlColor  c;

    switch (part)
    {
    case LRG_UI_PART_BOX:
        lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
        lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
                                  lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
        lrg_part_look_set_radius (look, lrg_theme_get_metric (theme,
                                                              LRG_THEME_METRIC_CORNER_RADIUS));
        break;

    case LRG_UI_PART_TAB:
        if (hovered)
            c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER);
        else if (selected)
            lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT),
                                        0.10f, &c);
        else
            lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER),
                                        0.0f, &c);
        lrg_part_look_set_background (look, &c);
        lrg_part_look_set_foreground (look,
                                      lrg_theme_get_color (theme, (hovered || selected)
                                                                  ? LRG_THEME_COLOR_TEXT
                                                                  : LRG_THEME_COLOR_TEXT_MUTED));
        lrg_part_look_set_radius (look, lrg_theme_get_metric (theme,
                                                              LRG_THEME_METRIC_CORNER_RADIUS));
        break;

    case LRG_UI_PART_INDICATOR:
        c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
        lrg_part_look_set_background (look, &c);
        lrg_part_look_set_radius (look, G_MAXFLOAT);
        lrg_part_look_set_size (look, INDICATOR_THICKNESS);
        lrg_paint_color_with_alpha (&c, 0.35f, &c);
        lrg_part_look_set_glow (look, &c, 6.0f);
        break;

    default:
        LRG_WIDGET_CLASS (lrg_tab_view_parent_class)->style_defaults (widget, part, state, look);
        return;
    }

    if (state & LRG_WIDGET_STATE_DISABLED)
        lrg_part_look_set_opacity (look, 0.45f);
}

/*
 * tab_look:
 *
 * The look of header @index: the view's state without its own hover /
 * press, plus SELECTED for the active tab, cross-faded to the hovered
 * variant by the tab's hover spring.
 */
static void
tab_look (LrgTabView  *self,
          guint        index,
          LrgPartLook *look)
{
    LrgWidget      *widget = LRG_WIDGET (self);
    LrgTabEntry    *entry = g_ptr_array_index (self->tabs, index);
    LrgWidgetState  base;
    gfloat          h = CLAMP (entry->hover.value, 0.0f, 1.0f);

    base = lrg_widget_get_state_flags (widget) &
           ~(LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED | LRG_WIDGET_STATE_SELECTED);
    if (index == self->active_tab)
        base |= LRG_WIDGET_STATE_SELECTED;

    lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_TAB, base, look);
    if (h > 0.001f)
    {
        LrgPartLook hovered;

        lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_TAB,
                                           base | LRG_WIDGET_STATE_HOVERED, &hovered);
        lrg_part_look_lerp (look, &hovered, h, look);
    }
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_tab_view_paint (LrgWidget    *widget,
                    LrgPaintList *list)
{
    LrgTabView  *self = LRG_TAB_VIEW (widget);
    LrgTheme    *theme = lrg_widget_get_theme (widget);
    GrlFont     *font = lrg_theme_get_ui_font (theme);
    gfloat       size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
    gfloat       x = lrg_widget_get_world_x (widget);
    gfloat       y = lrg_widget_get_world_y (widget);
    gfloat       w = lrg_widget_get_width (widget);
    gfloat       h = lrg_widget_get_height (widget);
    gfloat       top = tab_bar_top (self);
    gfloat       content_y;
    gfloat       content_h = MAX (h - self->tab_height, 0.0f);
    gfloat       tab_w;
    LrgPartLook  look;
    guint        i;

    advance (self);

    /* 1. Content surface */
    content_y = self->tab_position == LRG_TAB_POSITION_TOP ? y + self->tab_height : y;
    lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &look);
    lrg_widget_paint_box (widget, list, &look, x, content_y, w, content_h);

    /* 2. Headers, each with its own look and a centred label */
    tab_w = lrg_tab_view_get_tab_width (self);
    for (i = 0; i < self->tabs->len; i++)
    {
        LrgTabEntry *entry = g_ptr_array_index (self->tabs, i);
        gfloat       tx = x + (gfloat) i * tab_w;

        tab_look (self, i, &look);
        lrg_widget_paint_box (widget, list, &look, tx + 2.0f, top + 2.0f,
                              tab_w - 4.0f, self->tab_height - 4.0f);

        if (entry->label != NULL && entry->label[0] != '\0')
        {
            gfloat   lw;
            gfloat   lh;
            GrlColor fg;

            lrg_paint_measure_text (font, entry->label, size, 1.0f, &lw, &lh);
            lrg_paint_color_with_alpha (&look.foreground,
                                        ((gfloat) look.foreground.a / 255.0f) * look.opacity, &fg);
            lrg_paint_list_push_clip (list, tx, top, tab_w, self->tab_height);
            lrg_paint_list_text (list, font, entry->label, tx + (tab_w - lw) * 0.5f,
                                 top + (self->tab_height - lh) * 0.5f, size, 1.0f, &fg);
            lrg_paint_list_pop_clip (list);
        }
    }

    /* 3. Indicator, on the content side of the header strip */
    if (self->tabs->len > 0)
    {
        gfloat thick;
        gfloat iy;

        lrg_widget_resolve_look (widget, LRG_UI_PART_INDICATOR, &look);
        thick = look.size > 0.0f ? look.size : INDICATOR_THICKNESS;
        iy = self->tab_position == LRG_TAB_POSITION_TOP ? top + self->tab_height - thick : top;
        lrg_widget_paint_box (widget, list, &look, x + self->indicator_x.value, iy,
                              MAX (self->indicator_w.value, 0.0f), thick);
    }

    /* 4. Active content (the others are hidden), clipped to its area */
    lrg_paint_list_push_clip (list, x, content_y, w, content_h);
    _lrg_container_default_paint (widget, list);
    lrg_paint_list_pop_clip (list);
}

/*
 * lrg_tab_view_measure:
 *
 * The largest content's preferred size (plus its margin and the
 * container padding) with the header strip on top.
 */
static void
lrg_tab_view_measure (LrgWidget *widget,
                      gfloat    *preferred_width,
                      gfloat    *preferred_height)
{
    LrgTabView *self = LRG_TAB_VIEW (widget);
    gfloat      padding = lrg_container_get_padding (LRG_CONTAINER (widget));
    gfloat      max_width = 0.0f;
    gfloat      max_height = 0.0f;
    guint       i;

    for (i = 0; i < self->tabs->len; i++)
    {
        LrgTabEntry *entry = g_ptr_array_index (self->tabs, i);
        LrgInsets    margin;
        gfloat       cw;
        gfloat       ch;

        if (entry->content == NULL)
            continue;

        lrg_widget_get_preferred_size (entry->content, &cw, &ch);
        lrg_widget_get_margin (entry->content, &margin);
        max_width = MAX (max_width, cw + margin.left + margin.right);
        max_height = MAX (max_height, ch + margin.top + margin.bottom);
    }

    if (preferred_width != NULL)
        *preferred_width = max_width + padding * 2.0f;
    if (preferred_height != NULL)
        *preferred_height = max_height + padding * 2.0f + self->tab_height;
}

/*
 * lrg_tab_view_handle_event:
 *
 * Clicking a header activates its tab; moving over the headers tracks
 * the hovered one for its look. With focus on the view itself (not on
 * a widget inside a tab), Left / Right switch tabs and are consumed.
 * Without a canvas (events delivered straight to the view) events are
 * first offered to the active content, as before.
 */
static gboolean
lrg_tab_view_handle_event (LrgWidget        *widget,
                           const LrgUIEvent *event)
{
    LrgTabView     *self = LRG_TAB_VIEW (widget);
    LrgUIEventType  event_type = lrg_ui_event_get_event_type (event);
    LrgWidget      *target = lrg_ui_event_get_target (event);
    gint            tab_index;

    /* Legacy direct delivery: forward to the active content first */
    if (!LRG_IS_CANVAS (lrg_widget_get_root (widget)) && self->active_tab < self->tabs->len)
    {
        LrgTabEntry *active_entry = g_ptr_array_index (self->tabs, self->active_tab);

        if (active_entry->content != NULL &&
            lrg_widget_handle_event (active_entry->content, event))
            return TRUE;
    }

    switch (event_type)
    {
    case LRG_UI_EVENT_MOUSE_MOVE:
        set_hovered_tab (self, lrg_tab_view_get_tab_at_point (self, lrg_ui_event_get_x (event),
                                                              lrg_ui_event_get_y (event)));
        return FALSE;

    case LRG_UI_EVENT_POINTER_LEAVE:
        set_hovered_tab (self, -1);
        return FALSE;

    case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
        if (lrg_ui_event_get_button (event) == GRL_MOUSE_BUTTON_LEFT)
        {
            tab_index = lrg_tab_view_get_tab_at_point (self, lrg_ui_event_get_x (event),
                                                       lrg_ui_event_get_y (event));
            if (tab_index >= 0)
            {
                lrg_tab_view_set_active_tab (self, (guint) tab_index);
                return TRUE;
            }
        }
        break;

    case LRG_UI_EVENT_KEY_DOWN:
        /* Only when the view itself is focused; arrows pressed inside
         * a tab's content keep navigating between its widgets */
        if (target != NULL && target != widget)
            break;
        if (self->tabs->len == 0)
            break;
        if (lrg_ui_event_get_key (event) == GRL_KEY_LEFT)
        {
            if (self->active_tab > 0)
                lrg_tab_view_set_active_tab (self, self->active_tab - 1);
            return TRUE;
        }
        if (lrg_ui_event_get_key (event) == GRL_KEY_RIGHT)
        {
            if (self->active_tab + 1 < self->tabs->len)
                lrg_tab_view_set_active_tab (self, self->active_tab + 1);
            return TRUE;
        }
        break;

    default:
        break;
    }

    return FALSE;
}

/*
 * lrg_tab_view_layout_children:
 *
 * Places every content widget in the content area (below or above the
 * header strip, inside the container padding), honouring its margin
 * and alignment, and shows only the active one.
 */
static void
lrg_tab_view_layout_children (LrgContainer *container)
{
    LrgTabView *self = LRG_TAB_VIEW (container);
    gfloat      padding = lrg_container_get_padding (container);
    gfloat      width = lrg_widget_get_width (LRG_WIDGET (container));
    gfloat      height = lrg_widget_get_height (LRG_WIDGET (container));
    gfloat      content_y;
    guint       i;

    content_y = self->tab_position == LRG_TAB_POSITION_TOP ? self->tab_height + padding : padding;

    for (i = 0; i < self->tabs->len; i++)
    {
        LrgTabEntry *entry = g_ptr_array_index (self->tabs, i);

        if (entry->content == NULL)
            continue;

        lrg_widget_place_in_slot (entry->content, padding, content_y,
                                  MAX (width - padding * 2.0f, 0.0f),
                                  MAX (height - self->tab_height - padding * 2.0f, 0.0f));

        /* Only the active tab's content is visible */
        if (lrg_widget_get_visible (entry->content) != (i == self->active_tab))
            lrg_widget_set_visible (entry->content, i == self->active_tab);
    }

    retarget (self);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_tab_view_finalize (GObject *object)
{
    LrgTabView *self = LRG_TAB_VIEW (object);

    g_ptr_array_unref (self->tabs);

    G_OBJECT_CLASS (lrg_tab_view_parent_class)->finalize (object);
}

static void
lrg_tab_view_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgTabView *self = LRG_TAB_VIEW (object);

    switch (prop_id)
    {
    case PROP_ACTIVE_TAB:
        g_value_set_uint (value, self->active_tab);
        break;
    case PROP_TAB_POSITION:
        g_value_set_enum (value, self->tab_position);
        break;
    case PROP_TAB_HEIGHT:
        g_value_set_float (value, self->tab_height);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tab_view_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgTabView *self = LRG_TAB_VIEW (object);

    switch (prop_id)
    {
    case PROP_ACTIVE_TAB:
        lrg_tab_view_set_active_tab (self, g_value_get_uint (value));
        break;
    case PROP_TAB_POSITION:
        lrg_tab_view_set_tab_position (self, g_value_get_enum (value));
        break;
    case PROP_TAB_HEIGHT:
        lrg_tab_view_set_tab_height (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tab_view_class_init (LrgTabViewClass *klass)
{
    GObjectClass      *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
    LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

    object_class->finalize = lrg_tab_view_finalize;
    object_class->get_property = lrg_tab_view_get_property;
    object_class->set_property = lrg_tab_view_set_property;

    widget_class->paint = lrg_tab_view_paint;
    widget_class->style_defaults = lrg_tab_view_style_defaults;
    widget_class->measure = lrg_tab_view_measure;
    widget_class->handle_event = lrg_tab_view_handle_event;

    container_class->layout_children = lrg_tab_view_layout_children;

    /**
     * LrgTabView:active-tab:
     *
     * The index of the currently active tab.
     *
     * Since: 1.0
     */
    properties[PROP_ACTIVE_TAB] =
        g_param_spec_uint ("active-tab",
                           "Active Tab",
                           "The index of the currently active tab",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTabView:tab-position:
     *
     * The position of the tab bar (top or bottom).
     *
     * Since: 1.0
     */
    properties[PROP_TAB_POSITION] =
        g_param_spec_enum ("tab-position",
                           "Tab Position",
                           "Position of the tab bar",
                           LRG_TYPE_TAB_POSITION,
                           LRG_TAB_POSITION_TOP,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTabView:tab-height:
     *
     * The height of the tab bar in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_TAB_HEIGHT] =
        g_param_spec_float ("tab-height",
                            "Tab Height",
                            "Height of the tab bar in pixels",
                            0.0f, G_MAXFLOAT, 32.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgTabView::tab-changed:
     * @self: the #LrgTabView
     * @index: the new active tab index
     *
     * Emitted when the active tab changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TAB_CHANGED] =
        g_signal_new ("tab-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
lrg_tab_view_init (LrgTabView *self)
{
    lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
    self->tabs = g_ptr_array_new_with_free_func (
        (GDestroyNotify)lrg_tab_entry_free);
    self->active_tab = 0;
    self->tab_position = LRG_TAB_POSITION_TOP;
    self->tab_height = 32.0f;
    self->hovered_tab = -1;
    lrg_spring_init (&self->indicator_x, 0.0f, LRG_MOTION_SMOOTH);
    lrg_spring_init (&self->indicator_w, 0.0f, LRG_MOTION_SMOOTH);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/*
 * lrg_tab_view_new:
 *
 * Creates a new tab view widget.
 *
 * Returns: (transfer full): A new #LrgTabView
 */
LrgTabView *
lrg_tab_view_new (void)
{
    return g_object_new (LRG_TYPE_TAB_VIEW, NULL);
}

/*
 * lrg_tab_view_add_tab:
 * @self: an #LrgTabView
 * @label: the tab label text
 * @content: the widget to display when this tab is active
 *
 * Adds a new tab with the specified label and content widget.
 */
void
lrg_tab_view_add_tab (LrgTabView  *self,
                      const gchar *label,
                      LrgWidget   *content)
{
    LrgTabEntry *entry;

    g_return_if_fail (LRG_IS_TAB_VIEW (self));
    g_return_if_fail (label != NULL);
    g_return_if_fail (LRG_IS_WIDGET (content));

    entry = g_new0 (LrgTabEntry, 1);
    entry->label = g_strdup (label);
    entry->content = g_object_ref (content);
    lrg_spring_init (&entry->hover, 0.0f, LRG_MOTION_SMOOTH);

    g_ptr_array_add (self->tabs, entry);

    /* Also add as a child of the container for proper parent tracking */
    lrg_container_add_child (LRG_CONTAINER (self), content);

    /* Trigger layout update */
    lrg_container_layout_children (LRG_CONTAINER (self));
}

/*
 * lrg_tab_view_remove_tab:
 * @self: an #LrgTabView
 * @index: the tab index to remove
 *
 * Removes the tab at the specified index.
 */
void
lrg_tab_view_remove_tab (LrgTabView *self,
                         guint       index)
{
    LrgTabEntry *entry;

    g_return_if_fail (LRG_IS_TAB_VIEW (self));
    g_return_if_fail (index < self->tabs->len);

    entry = g_ptr_array_index (self->tabs, index);

    /* Remove from container's children list */
    if (entry->content != NULL)
    {
        lrg_container_remove_child (LRG_CONTAINER (self), entry->content);
    }

    g_ptr_array_remove_index (self->tabs, index);
    self->hovered_tab = -1;

    /* Adjust active tab if needed */
    if (self->tabs->len == 0)
    {
        self->active_tab = 0;
    }
    else if (self->active_tab >= self->tabs->len)
    {
        self->active_tab = self->tabs->len - 1;
    }

    lrg_container_layout_children (LRG_CONTAINER (self));
}

/*
 * lrg_tab_view_get_tab_count:
 * @self: an #LrgTabView
 *
 * Gets the number of tabs in the view.
 *
 * Returns: The tab count
 */
guint
lrg_tab_view_get_tab_count (LrgTabView *self)
{
    g_return_val_if_fail (LRG_IS_TAB_VIEW (self), 0);
    return self->tabs->len;
}

/*
 * lrg_tab_view_get_active_tab:
 * @self: an #LrgTabView
 *
 * Gets the index of the currently active tab.
 *
 * Returns: The active tab index
 */
guint
lrg_tab_view_get_active_tab (LrgTabView *self)
{
    g_return_val_if_fail (LRG_IS_TAB_VIEW (self), 0);
    return self->active_tab;
}

/*
 * lrg_tab_view_set_active_tab:
 * @self: an #LrgTabView
 * @index: the tab index to activate
 *
 * Sets which tab is currently active and visible.
 */
void
lrg_tab_view_set_active_tab (LrgTabView *self,
                             guint       index)
{
    g_return_if_fail (LRG_IS_TAB_VIEW (self));

    if (self->tabs->len == 0)
    {
        return;
    }

    if (index >= self->tabs->len)
    {
        index = self->tabs->len - 1;
    }

    if (self->active_tab != index)
    {
        self->active_tab = index;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_TAB]);
        lrg_container_layout_children (LRG_CONTAINER (self));
        g_signal_emit (self, signals[SIGNAL_TAB_CHANGED], 0, index);
    }
}

/*
 * lrg_tab_view_get_tab_position:
 * @self: an #LrgTabView
 *
 * Gets the position of the tab bar (top or bottom).
 *
 * Returns: The tab bar position
 */
LrgTabPosition
lrg_tab_view_get_tab_position (LrgTabView *self)
{
    g_return_val_if_fail (LRG_IS_TAB_VIEW (self), LRG_TAB_POSITION_TOP);
    return self->tab_position;
}

/*
 * lrg_tab_view_set_tab_position:
 * @self: an #LrgTabView
 * @position: the tab bar position
 *
 * Sets whether the tab bar appears at the top or bottom.
 */
void
lrg_tab_view_set_tab_position (LrgTabView     *self,
                               LrgTabPosition  position)
{
    g_return_if_fail (LRG_IS_TAB_VIEW (self));

    if (self->tab_position != position)
    {
        self->tab_position = position;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TAB_POSITION]);
        lrg_container_layout_children (LRG_CONTAINER (self));
    }
}

/*
 * lrg_tab_view_get_tab_height:
 * @self: an #LrgTabView
 *
 * Gets the height of the tab bar in pixels.
 *
 * Returns: The tab bar height
 */
gfloat
lrg_tab_view_get_tab_height (LrgTabView *self)
{
    g_return_val_if_fail (LRG_IS_TAB_VIEW (self), 32.0f);
    return self->tab_height;
}

/*
 * lrg_tab_view_set_tab_height:
 * @self: an #LrgTabView
 * @height: the tab bar height in pixels
 *
 * Sets the height of the tab bar.
 */
void
lrg_tab_view_set_tab_height (LrgTabView *self,
                             gfloat      height)
{
    g_return_if_fail (LRG_IS_TAB_VIEW (self));
    g_return_if_fail (height >= 0.0f);

    if (self->tab_height != height)
    {
        self->tab_height = height;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TAB_HEIGHT]);
        lrg_container_layout_children (LRG_CONTAINER (self));
        lrg_widget_queue_layout (LRG_WIDGET (self));
    }
}

/*
 * lrg_tab_view_get_tab_content:
 * @self: an #LrgTabView
 * @index: the tab index
 *
 * Gets the content widget for the tab at the specified index.
 *
 * Returns: (transfer none) (nullable): The content widget, or %NULL
 */
LrgWidget *
lrg_tab_view_get_tab_content (LrgTabView *self,
                              guint       index)
{
    LrgTabEntry *entry;

    g_return_val_if_fail (LRG_IS_TAB_VIEW (self), NULL);

    entry = lrg_tab_view_get_entry (self, index);
    if (entry == NULL)
    {
        return NULL;
    }

    return entry->content;
}

/*
 * lrg_tab_view_get_tab_label:
 * @self: an #LrgTabView
 * @index: the tab index
 *
 * Gets the label text for the tab at the specified index.
 *
 * Returns: (transfer none) (nullable): The tab label, or %NULL
 */
const gchar *
lrg_tab_view_get_tab_label (LrgTabView *self,
                            guint       index)
{
    LrgTabEntry *entry;

    g_return_val_if_fail (LRG_IS_TAB_VIEW (self), NULL);

    entry = lrg_tab_view_get_entry (self, index);
    if (entry == NULL)
    {
        return NULL;
    }

    return entry->label;
}

/*
 * lrg_tab_view_set_tab_label:
 * @self: an #LrgTabView
 * @index: the tab index
 * @label: the new label text
 *
 * Sets the label text for the tab at the specified index.
 */
void
lrg_tab_view_set_tab_label (LrgTabView  *self,
                            guint        index,
                            const gchar *label)
{
    LrgTabEntry *entry;

    g_return_if_fail (LRG_IS_TAB_VIEW (self));
    g_return_if_fail (label != NULL);

    entry = lrg_tab_view_get_entry (self, index);
    if (entry == NULL)
    {
        return;
    }

    g_free (entry->label);
    entry->label = g_strdup (label);
}

/*
 * lrg_tab_view_get_indicator_rect:
 * @self: an #LrgTabView
 * @x: (out) (optional): left edge, world coordinates
 * @y: (out) (optional): top edge, world coordinates
 * @width: (out) (optional): width
 * @height: (out) (optional): thickness
 *
 * Where the active-tab indicator is heading.
 *
 * Returns: %FALSE when there are no tabs
 */
gboolean
lrg_tab_view_get_indicator_rect (LrgTabView *self,
                                 gfloat     *x,
                                 gfloat     *y,
                                 gfloat     *width,
                                 gfloat     *height)
{
    LrgPartLook look;
    gfloat      ix = 0.0f;
    gfloat      iw = 0.0f;
    gfloat      thick = 0.0f;
    gfloat      iy = 0.0f;
    gboolean    any;

    g_return_val_if_fail (LRG_IS_TAB_VIEW (self), FALSE);

    any = self->tabs->len > 0;
    if (any)
    {
        lrg_widget_resolve_look (LRG_WIDGET (self), LRG_UI_PART_INDICATOR, &look);
        thick = look.size > 0.0f ? look.size : INDICATOR_THICKNESS;
        indicator_target (self, &ix, &iw);
        ix += lrg_widget_get_world_x (LRG_WIDGET (self));
        iy = self->tab_position == LRG_TAB_POSITION_TOP
             ? tab_bar_top (self) + self->tab_height - thick
             : tab_bar_top (self);
    }

    if (x != NULL)
        *x = ix;
    if (y != NULL)
        *y = iy;
    if (width != NULL)
        *width = iw;
    if (height != NULL)
        *height = thick;
    return any;
}
