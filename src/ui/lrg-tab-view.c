/* lrg-tab-view.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tab view container widget implementation.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-tab-view.h"
#include "../lrg-log.h"
#include <graylib.h>

/* ==========================================================================
 * Tab Entry Structure
 * ========================================================================== */

typedef struct
{
    gchar     *label;
    LrgWidget *content;
} LrgTabEntry;

static void
lrg_tab_entry_free (LrgTabEntry *entry)
{
    if (entry == NULL)
    {
        return;
    }

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

    /* Colors */
    GrlColor        tab_normal_color;
    GrlColor        tab_hover_color;
    GrlColor        tab_active_color;
    GrlColor        tab_text_color;
    GrlColor        content_bg_color;
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

/* Default colors */
static const GrlColor DEFAULT_TAB_NORMAL  = { 60, 60, 60, 255 };
static const GrlColor DEFAULT_TAB_HOVER   = { 80, 80, 80, 255 };
static const GrlColor DEFAULT_TAB_ACTIVE  = { 100, 100, 100, 255 };
static const GrlColor DEFAULT_TAB_TEXT    = { 255, 255, 255, 255 };
static const GrlColor DEFAULT_CONTENT_BG  = { 40, 40, 40, 255 };

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static LrgTabEntry *
lrg_tab_view_get_entry (LrgTabView *self,
                        guint       index)
{
    if (index >= self->tabs->len)
    {
        return NULL;
    }

    return g_ptr_array_index (self->tabs, index);
}

static gfloat
lrg_tab_view_get_tab_width (LrgTabView *self)
{
    gfloat widget_width;
    guint  tab_count;

    if (self->tabs->len == 0)
    {
        return 0.0f;
    }

    widget_width = lrg_widget_get_width (LRG_WIDGET (self));
    tab_count = self->tabs->len;

    return widget_width / (gfloat)tab_count;
}

static gint
lrg_tab_view_get_tab_at_point (LrgTabView *self,
                               gfloat      x,
                               gfloat      y)
{
    gfloat world_x;
    gfloat world_y;
    gfloat widget_height;
    gfloat tab_bar_y;
    gfloat tab_bar_end_y;
    gfloat tab_width;
    gint   tab_index;

    if (self->tabs->len == 0)
    {
        return -1;
    }

    world_x = lrg_widget_get_world_x (LRG_WIDGET (self));
    world_y = lrg_widget_get_world_y (LRG_WIDGET (self));
    widget_height = lrg_widget_get_height (LRG_WIDGET (self));

    /* Calculate tab bar position based on tab_position */
    if (self->tab_position == LRG_TAB_POSITION_TOP)
    {
        tab_bar_y = world_y;
        tab_bar_end_y = world_y + self->tab_height;
    }
    else
    {
        tab_bar_y = world_y + widget_height - self->tab_height;
        tab_bar_end_y = world_y + widget_height;
    }

    /* Check if point is within tab bar vertically */
    if (y < tab_bar_y || y > tab_bar_end_y)
    {
        return -1;
    }

    /* Calculate which tab was hit */
    tab_width = lrg_tab_view_get_tab_width (self);
    tab_index = (gint)((x - world_x) / tab_width);

    if (tab_index < 0 || (guint)tab_index >= self->tabs->len)
    {
        return -1;
    }

    return tab_index;
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_tab_view_draw (LrgWidget *widget)
{
    LrgTabView   *self = LRG_TAB_VIEW (widget);
    gfloat        world_x;
    gfloat        world_y;
    gfloat        widget_width;
    gfloat        widget_height;
    gfloat        tab_bar_y;
    gfloat        content_y;
    gfloat        content_height;
    gfloat        tab_width;
    guint         i;

    world_x = lrg_widget_get_world_x (widget);
    world_y = lrg_widget_get_world_y (widget);
    widget_width = lrg_widget_get_width (widget);
    widget_height = lrg_widget_get_height (widget);

    /* Calculate positions based on tab position */
    if (self->tab_position == LRG_TAB_POSITION_TOP)
    {
        tab_bar_y = world_y;
        content_y = world_y + self->tab_height;
        content_height = widget_height - self->tab_height;
    }
    else
    {
        tab_bar_y = world_y + widget_height - self->tab_height;
        content_y = world_y;
        content_height = widget_height - self->tab_height;
    }

    /* Draw content background */
    grl_draw_rectangle (world_x, content_y, widget_width, content_height,
                        &self->content_bg_color);

    /* Draw tabs */
    tab_width = lrg_tab_view_get_tab_width (self);

    for (i = 0; i < self->tabs->len; i++)
    {
        LrgTabEntry *entry;
        GrlColor    *tab_color;
        gfloat       tab_x;

        entry = g_ptr_array_index (self->tabs, i);
        tab_x = world_x + (gfloat)i * tab_width;

        /* Choose tab color based on state */
        if (i == self->active_tab)
        {
            tab_color = &self->tab_active_color;
        }
        else if ((gint)i == self->hovered_tab)
        {
            tab_color = &self->tab_hover_color;
        }
        else
        {
            tab_color = &self->tab_normal_color;
        }

        /* Draw tab background */
        grl_draw_rectangle (tab_x, tab_bar_y, tab_width, self->tab_height,
                            tab_color);

        /* Draw tab border (right edge separator) */
        if (i < self->tabs->len - 1)
        {
            g_autoptr(GrlColor) border = grl_color_new (30, 30, 30, 255);
            grl_draw_rectangle (tab_x + tab_width - 1.0f, tab_bar_y,
                                1.0f, self->tab_height, border);
        }

        /* Draw tab label centered */
        if (entry->label != NULL && entry->label[0] != '\0')
        {
            gfloat text_width;
            gfloat text_x;
            gfloat text_y;
            gint   font_size = 16;

            /* Estimate text width (rough approximation) */
            text_width = (gfloat)g_utf8_strlen (entry->label, -1) *
                         (font_size * 0.6f);

            text_x = tab_x + (tab_width - text_width) / 2.0f;
            text_y = tab_bar_y + (self->tab_height - font_size) / 2.0f;

            grl_draw_text (entry->label, (gint)text_x, (gint)text_y,
                           font_size, &self->tab_text_color);
        }
    }

    /* Draw active tab indicator line */
    if (self->tabs->len > 0)
    {
        g_autoptr(GrlColor) indicator = grl_color_new (120, 180, 255, 255);
        gfloat indicator_y;
        gfloat indicator_x;

        indicator_x = world_x + (gfloat)self->active_tab * tab_width;

        if (self->tab_position == LRG_TAB_POSITION_TOP)
        {
            indicator_y = tab_bar_y + self->tab_height - 3.0f;
        }
        else
        {
            indicator_y = tab_bar_y;
        }

        grl_draw_rectangle (indicator_x + 4.0f, indicator_y,
                            tab_width - 8.0f, 3.0f, indicator);
    }

    /* Draw active tab's content */
    if (self->active_tab < self->tabs->len)
    {
        LrgTabEntry *active_entry;

        active_entry = g_ptr_array_index (self->tabs, self->active_tab);
        if (active_entry->content != NULL &&
            lrg_widget_get_visible (active_entry->content))
        {
            lrg_widget_draw (active_entry->content);
        }
    }
}

static void
lrg_tab_view_measure (LrgWidget *widget,
                      gfloat    *preferred_width,
                      gfloat    *preferred_height)
{
    LrgTabView *self = LRG_TAB_VIEW (widget);
    gfloat      max_width = 0.0f;
    gfloat      max_height = 0.0f;
    guint       i;

    /* Find the maximum content size across all tabs */
    for (i = 0; i < self->tabs->len; i++)
    {
        LrgTabEntry *entry;
        gfloat       child_width;
        gfloat       child_height;

        entry = g_ptr_array_index (self->tabs, i);

        if (entry->content != NULL)
        {
            lrg_widget_measure (entry->content, &child_width, &child_height);

            if (child_width > max_width)
            {
                max_width = child_width;
            }
            if (child_height > max_height)
            {
                max_height = child_height;
            }
        }
    }

    if (preferred_width != NULL)
    {
        *preferred_width = max_width;
    }
    if (preferred_height != NULL)
    {
        *preferred_height = max_height + self->tab_height;
    }
}

static gboolean
lrg_tab_view_handle_event (LrgWidget        *widget,
                           const LrgUIEvent *event)
{
    LrgTabView     *self = LRG_TAB_VIEW (widget);
    LrgUIEventType  event_type;
    gfloat          x;
    gfloat          y;
    gint            tab_index;

    event_type = lrg_ui_event_get_event_type (event);

    /* Forward events to active content if within content area */
    if (self->active_tab < self->tabs->len)
    {
        LrgTabEntry *active_entry;

        active_entry = g_ptr_array_index (self->tabs, self->active_tab);
        if (active_entry->content != NULL &&
            lrg_widget_get_visible (active_entry->content) &&
            lrg_widget_get_enabled (active_entry->content))
        {
            if (lrg_widget_handle_event (active_entry->content, event))
            {
                return TRUE;
            }
        }
    }

    /* Handle mouse events for tab switching */
    x = lrg_ui_event_get_x (event);
    y = lrg_ui_event_get_y (event);

    switch (event_type)
    {
    case LRG_UI_EVENT_MOUSE_MOVE:
        tab_index = lrg_tab_view_get_tab_at_point (self, x, y);
        if (tab_index != self->hovered_tab)
        {
            self->hovered_tab = tab_index;
        }
        return FALSE;

    case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
        if (lrg_ui_event_get_button (event) == 0) /* Left button */
        {
            tab_index = lrg_tab_view_get_tab_at_point (self, x, y);
            if (tab_index >= 0 && (guint)tab_index != self->active_tab)
            {
                lrg_tab_view_set_active_tab (self, (guint)tab_index);
                return TRUE;
            }
        }
        break;

    default:
        break;
    }

    return FALSE;
}

static void
lrg_tab_view_layout_children (LrgContainer *container)
{
    LrgTabView *self = LRG_TAB_VIEW (container);
    gfloat      padding;
    gfloat      content_x;
    gfloat      content_y;
    gfloat      content_width;
    gfloat      content_height;
    gfloat      widget_width;
    gfloat      widget_height;
    guint       i;

    padding = lrg_container_get_padding (container);
    widget_width = lrg_widget_get_width (LRG_WIDGET (container));
    widget_height = lrg_widget_get_height (LRG_WIDGET (container));

    /* Calculate content area */
    content_x = padding;
    content_width = widget_width - padding * 2.0f;
    content_height = widget_height - self->tab_height - padding * 2.0f;

    if (self->tab_position == LRG_TAB_POSITION_TOP)
    {
        content_y = self->tab_height + padding;
    }
    else
    {
        content_y = padding;
    }

    /* Position all content widgets in the content area */
    for (i = 0; i < self->tabs->len; i++)
    {
        LrgTabEntry *entry;

        entry = g_ptr_array_index (self->tabs, i);

        if (entry->content != NULL)
        {
            lrg_widget_set_position (entry->content, content_x, content_y);
            lrg_widget_set_size (entry->content, content_width, content_height);

            /* Only the active tab's content should be visible */
            lrg_widget_set_visible (entry->content, i == self->active_tab);
        }
    }
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

    widget_class->draw = lrg_tab_view_draw;
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
    self->tabs = g_ptr_array_new_with_free_func (
        (GDestroyNotify)lrg_tab_entry_free);
    self->active_tab = 0;
    self->tab_position = LRG_TAB_POSITION_TOP;
    self->tab_height = 32.0f;
    self->hovered_tab = -1;

    /* Set default colors */
    self->tab_normal_color = DEFAULT_TAB_NORMAL;
    self->tab_hover_color = DEFAULT_TAB_HOVER;
    self->tab_active_color = DEFAULT_TAB_ACTIVE;
    self->tab_text_color = DEFAULT_TAB_TEXT;
    self->content_bg_color = DEFAULT_CONTENT_BG;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
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

/**
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

    g_ptr_array_add (self->tabs, entry);

    /* Also add as a child of the container for proper parent tracking */
    lrg_container_add_child (LRG_CONTAINER (self), content);

    /* Trigger layout update */
    lrg_container_layout_children (LRG_CONTAINER (self));
}

/**
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

/**
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

/**
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

/**
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
        g_signal_emit (self, signals[SIGNAL_TAB_CHANGED], 0, index);
        lrg_container_layout_children (LRG_CONTAINER (self));
    }
}

/**
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

/**
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

/**
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

/**
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
    }
}

/**
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

/**
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

/**
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
