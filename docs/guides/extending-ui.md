# How to Create Custom Widgets

This guide explains how to extend the UI system by creating custom widgets for game interfaces.

## Widget Basics

A widget:
- Derives from `LrgWidget` or `LrgContainer`
- Manages rendering via draw() virtual function
- Handles input via handle_event() virtual function
- Integrates with layout and theme systems
- Can contain child widgets (if container)

## Simple Custom Widget

### Step 1: Define the Widget

```c
/* my-progress-bar.h */
#pragma once

#include <libregnum.h>

G_BEGIN_DECLS

#define MY_TYPE_PROGRESS_BAR (my_progress_bar_get_type())

G_DECLARE_FINAL_TYPE(MyProgressBar,
                     my_progress_bar,
                     MY,
                     PROGRESS_BAR,
                     LrgWidget)

/* Construction */
MyProgressBar *
my_progress_bar_new(void);

/* Properties */
gfloat
my_progress_bar_get_progress(MyProgressBar *self);

void
my_progress_bar_set_progress(MyProgressBar *self,
                             gfloat         progress);

const gchar *
my_progress_bar_get_color_bg(MyProgressBar *self);

void
my_progress_bar_set_color_bg(MyProgressBar *self,
                             const gchar   *color);

const gchar *
my_progress_bar_get_color_fg(MyProgressBar *self);

void
my_progress_bar_set_color_fg(MyProgressBar *self,
                             const gchar   *color);

G_END_DECLS
```

### Step 2: Implement the Widget

```c
/* my-progress-bar.c */
#include "my-progress-bar.h"

struct _MyProgressBar
{
    LrgWidget parent_instance;
    gfloat    progress;           /* 0.0 to 1.0 */
    gchar    *color_bg;           /* Background color */
    gchar    *color_fg;           /* Foreground color */
    gint      bar_height;         /* Height of bar */
};

G_DEFINE_TYPE(MyProgressBar,
              my_progress_bar,
              LRG_TYPE_WIDGET)

/* Draw the widget */

static void
my_progress_bar_draw(LrgWidget    *widget,
                     LrgRenderCtx *ctx)
{
    MyProgressBar *self = MY_PROGRESS_BAR(widget);
    GdkRectangle bounds;

    /* Get widget bounds */
    lrg_widget_get_bounds(widget, &bounds);

    /* Draw background */
    lrg_render_ctx_set_color(ctx, self->color_bg);
    lrg_render_ctx_fill_rect(ctx,
                             bounds.x, bounds.y,
                             bounds.width, self->bar_height);

    /* Draw foreground (progress) */
    gint progress_width = (bounds.width * self->progress);

    lrg_render_ctx_set_color(ctx, self->color_fg);
    lrg_render_ctx_fill_rect(ctx,
                             bounds.x, bounds.y,
                             progress_width, self->bar_height);

    /* Draw border */
    lrg_render_ctx_set_color(ctx, "#ffffff");
    lrg_render_ctx_draw_rect(ctx,
                             bounds.x, bounds.y,
                             bounds.width, self->bar_height);

    /* Call parent to draw children if any */
    LRG_WIDGET_CLASS(my_progress_bar_parent_class)->draw(widget, ctx);
}

/* Handle input events */

static gboolean
my_progress_bar_handle_event(LrgWidget     *widget,
                             LrgInputEvent *event)
{
    MyProgressBar *self = MY_PROGRESS_BAR(widget);

    switch (event->type) {
    case LRG_INPUT_EVENT_MOUSE_CLICK:
        /* Allow setting progress by clicking */
        {
            GdkRectangle bounds;
            lrg_widget_get_bounds(widget, &bounds);

            gfloat relative_x = event->x - bounds.x;
            gfloat click_progress = relative_x / bounds.width;

            my_progress_bar_set_progress(self,
                                        CLAMP(click_progress, 0.0f, 1.0f));
            return TRUE;  /* Event handled */
        }

    default:
        break;
    }

    /* Call parent handler */
    return LRG_WIDGET_CLASS(my_progress_bar_parent_class)->handle_event(widget,
                                                                       event);
}

/* Class initialization */

static void
my_progress_bar_class_init(MyProgressBarClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS(klass);

    /* Default size */
    widget_class->default_width = 200;
    widget_class->default_height = 20;

    /* Virtual functions */
    widget_class->draw = my_progress_bar_draw;
    widget_class->handle_event = my_progress_bar_handle_event;
}

/* Instance initialization */

static void
my_progress_bar_init(MyProgressBar *self)
{
    self->progress = 0.5f;
    self->color_bg = g_strdup("#333333");
    self->color_fg = g_strdup("#00ff00");
    self->bar_height = 20;
}

/* Public API */

MyProgressBar *
my_progress_bar_new(void)
{
    return g_object_new(MY_TYPE_PROGRESS_BAR, NULL);
}

gfloat
my_progress_bar_get_progress(MyProgressBar *self)
{
    g_return_val_if_fail(MY_IS_PROGRESS_BAR(self), 0.0f);
    return self->progress;
}

void
my_progress_bar_set_progress(MyProgressBar *self,
                             gfloat         progress)
{
    g_return_if_fail(MY_IS_PROGRESS_BAR(self));

    self->progress = CLAMP(progress, 0.0f, 1.0f);

    /* Request redraw */
    lrg_widget_queue_draw(LRG_WIDGET(self));

    /* Emit signal */
    g_signal_emit_by_name(self, "progress-changed", self->progress);
}

const gchar *
my_progress_bar_get_color_bg(MyProgressBar *self)
{
    g_return_val_if_fail(MY_IS_PROGRESS_BAR(self), NULL);
    return self->color_bg;
}

void
my_progress_bar_set_color_bg(MyProgressBar *self,
                             const gchar   *color)
{
    g_return_if_fail(MY_IS_PROGRESS_BAR(self));

    g_free(self->color_bg);
    self->color_bg = g_strdup(color);

    lrg_widget_queue_draw(LRG_WIDGET(self));
}

const gchar *
my_progress_bar_get_color_fg(MyProgressBar *self)
{
    g_return_val_if_fail(MY_IS_PROGRESS_BAR(self), NULL);
    return self->color_fg;
}

void
my_progress_bar_set_color_fg(MyProgressBar *self,
                             const gchar   *color)
{
    g_return_if_fail(MY_IS_PROGRESS_BAR(self));

    g_free(self->color_fg);
    self->color_fg = g_strdup(color);

    lrg_widget_queue_draw(LRG_WIDGET(self));
}
```

## Custom Container Widget

For widgets that contain other widgets:

```c
/* my-dialog-box.h */
#pragma once

#include <libregnum.h>

G_BEGIN_DECLS

#define MY_TYPE_DIALOG_BOX (my_dialog_box_get_type())

G_DECLARE_FINAL_TYPE(MyDialogBox,
                     my_dialog_box,
                     MY,
                     DIALOG_BOX,
                     LrgContainer)

MyDialogBox *
my_dialog_box_new(const gchar *title);

const gchar *
my_dialog_box_get_title(MyDialogBox *self);

void
my_dialog_box_set_title(MyDialogBox *self,
                        const gchar *title);

G_END_DECLS
```

```c
/* my-dialog-box.c */
#include "my-dialog-box.h"

struct _MyDialogBox
{
    LrgContainer parent_instance;
    gchar       *title;
    gint         padding;
    gint         title_height;
};

G_DEFINE_TYPE(MyDialogBox,
              my_dialog_box,
              LRG_TYPE_CONTAINER)

static void
my_dialog_box_draw(LrgWidget    *widget,
                   LrgRenderCtx *ctx)
{
    MyDialogBox *self = MY_DIALOG_BOX(widget);
    GdkRectangle bounds;

    lrg_widget_get_bounds(widget, &bounds);

    /* Draw background (semi-transparent) */
    lrg_render_ctx_set_color(ctx, "#000000");
    lrg_render_ctx_set_alpha(ctx, 0.8f);
    lrg_render_ctx_fill_rect(ctx,
                             bounds.x, bounds.y,
                             bounds.width, bounds.height);

    /* Draw border */
    lrg_render_ctx_set_alpha(ctx, 1.0f);
    lrg_render_ctx_set_color(ctx, "#ffffff");
    lrg_render_ctx_draw_rect(ctx,
                             bounds.x, bounds.y,
                             bounds.width, bounds.height);

    /* Draw title bar */
    lrg_render_ctx_set_color(ctx, "#333366");
    lrg_render_ctx_fill_rect(ctx,
                             bounds.x, bounds.y,
                             bounds.width, self->title_height);

    /* Draw title text */
    lrg_render_ctx_set_color(ctx, "#ffffff");
    lrg_render_ctx_draw_text(ctx,
                             self->title,
                             bounds.x + self->padding,
                             bounds.y + 4);

    /* Draw children */
    LRG_WIDGET_CLASS(my_dialog_box_parent_class)->draw(widget, ctx);
}

static void
my_dialog_box_layout(LrgContainer *container)
{
    MyDialogBox *self = MY_DIALOG_BOX(container);
    GdkRectangle bounds;
    GList *children;

    lrg_widget_get_bounds(LRG_WIDGET(container), &bounds);

    /* Get children and layout */
    children = lrg_container_get_children(container);

    gint child_y = bounds.y + self->title_height + self->padding;
    gint child_x = bounds.x + self->padding;
    gint child_width = bounds.width - 2 * self->padding;

    for (GList *l = children; l; l = l->next) {
        LrgWidget *child = LRG_WIDGET(l->data);
        GdkRectangle child_bounds = {
            child_x, child_y,
            child_width, 30
        };

        lrg_widget_set_bounds(child, &child_bounds);
        child_y += 30 + self->padding;
    }

    g_list_free(children);

    /* Call parent */
    LRG_CONTAINER_CLASS(my_dialog_box_parent_class)->layout(container);
}

static void
my_dialog_box_class_init(MyDialogBoxClass *klass)
{
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS(klass);
    LrgContainerClass *container_class = LRG_CONTAINER_CLASS(klass);

    widget_class->draw = my_dialog_box_draw;
    container_class->layout = my_dialog_box_layout;

    widget_class->default_width = 400;
    widget_class->default_height = 300;
}

static void
my_dialog_box_init(MyDialogBox *self)
{
    self->title = g_strdup("Dialog");
    self->padding = 10;
    self->title_height = 30;
}

MyDialogBox *
my_dialog_box_new(const gchar *title)
{
    MyDialogBox *self = g_object_new(MY_TYPE_DIALOG_BOX, NULL);
    self->title = g_strdup(title);
    return self;
}

const gchar *
my_dialog_box_get_title(MyDialogBox *self)
{
    g_return_val_if_fail(MY_IS_DIALOG_BOX(self), NULL);
    return self->title;
}

void
my_dialog_box_set_title(MyDialogBox *self,
                        const gchar *title)
{
    g_return_if_fail(MY_IS_DIALOG_BOX(self));

    g_free(self->title);
    self->title = g_strdup(title);

    lrg_widget_queue_draw(LRG_WIDGET(self));
}
```

## Using Custom Widgets

```c
/* Create and add to UI */
g_autoptr(MyProgressBar) bar = my_progress_bar_new();
my_progress_bar_set_progress(bar, 0.75f);
my_progress_bar_set_color_fg(bar, "#ff0000");

GdkRectangle bounds = { 100, 100, 200, 20 };
lrg_widget_set_bounds(LRG_WIDGET(bar), &bounds);

/* Add to a container */
lrg_container_add_child(container, LRG_WIDGET(bar));

/* Listen for changes */
g_signal_connect(bar, "progress-changed",
                G_CALLBACK(on_progress_changed), user_data);
```

## Theme Integration

Widgets can use the theme system for colors:

```c
const gchar *
my_progress_bar_get_color_from_theme(MyProgressBar *self,
                                     const gchar   *theme_key)
{
    LrgTheme *theme = lrg_widget_get_theme(LRG_WIDGET(self));
    return lrg_theme_get_color(theme, theme_key);
}
```

## Best Practices

1. **Use inherited properties**: Leverage LrgWidget's built-in properties
2. **Queue redraws**: Use `lrg_widget_queue_draw()` to schedule updates
3. **Handle layout**: Properly layout children in containers
4. **Emit signals**: Allow external code to react to user interactions
5. **Use themes**: Integrate with the theme system for consistent look
6. **Document behavior**: Explain how widget responds to input
7. **Test rendering**: Verify appearance on different screen sizes

## See Also

- [UI Module Documentation](../modules/ui/index.md)
- [Implementing Saveable](implementing-saveable.md)
- [Theme System](../modules/ui/theming.md)
