# UI Basics Example

A complete example showing how to create a basic UI with the UI module.

## Setup

```c
#include <libregnum.h>

typedef struct
{
    LrgCanvas *ui_canvas;
    LrgTheme *theme;
    gboolean dialog_visible;
    gfloat volume;
    gboolean sound_enabled;
} UIExample;

UIExample *example_new(void)
{
    UIExample *ex = g_new0(UIExample, 1);
    ex->ui_canvas = lrg_canvas_new();
    ex->theme = lrg_theme_get_default();
    ex->dialog_visible = TRUE;
    ex->volume = 0.7f;
    ex->sound_enabled = TRUE;
    return ex;
}
```

## Setting Up Theme

```c
void setup_dark_theme(UIExample *ex)
{
    LrgTheme *theme = ex->theme;

    /* Colors */
    GrlColor bg = GRL_COLOR(20, 20, 30, 255);
    GrlColor surface = GRL_COLOR(40, 40, 50, 255);
    GrlColor primary = GRL_COLOR(100, 150, 255, 255);
    GrlColor text = GRL_COLOR(220, 220, 220, 255);
    GrlColor border = GRL_COLOR(80, 80, 100, 255);

    lrg_theme_set_background_color(theme, &bg);
    lrg_theme_set_surface_color(theme, &surface);
    lrg_theme_set_primary_color(theme, &primary);
    lrg_theme_set_text_color(theme, &text);
    lrg_theme_set_border_color(theme, &border);

    /* Typography */
    lrg_theme_set_font_size_small(theme, 12.0f);
    lrg_theme_set_font_size_normal(theme, 16.0f);
    lrg_theme_set_font_size_large(theme, 24.0f);

    /* Spacing */
    lrg_theme_set_padding_small(theme, 4.0f);
    lrg_theme_set_padding_normal(theme, 8.0f);
    lrg_theme_set_padding_large(theme, 16.0f);
    lrg_theme_set_border_width(theme, 1.0f);
    lrg_theme_set_corner_radius(theme, 4.0f);
}
```

## Creating UI Layout

```c
void create_settings_dialog(UIExample *ex)
{
    LrgCanvas *canvas = ex->ui_canvas;

    /* Main dialog panel */
    g_autoptr(LrgPanel) dialog = lrg_panel_new();
    lrg_widget_set_position(LRG_WIDGET(dialog), 150, 100);
    lrg_widget_set_size(LRG_WIDGET(dialog), 500, 400);

    GrlColor bg = GRL_COLOR(40, 40, 50, 255);
    GrlColor border = GRL_COLOR(100, 100, 120, 255);
    lrg_panel_set_background_color(dialog, &bg);
    lrg_panel_set_border_color(dialog, &border);
    lrg_panel_set_border_width(dialog, 2.0f);
    lrg_panel_set_corner_radius(dialog, 8.0f);

    lrg_container_add_child(LRG_CONTAINER(canvas), LRG_WIDGET(dialog));

    /* Main layout: VBox */
    g_autoptr(LrgVBox) main_layout = lrg_vbox_new();
    lrg_container_set_spacing(LRG_CONTAINER(main_layout), 15);
    lrg_container_set_padding(LRG_CONTAINER(main_layout), 20);
    lrg_container_add_child(LRG_CONTAINER(dialog), LRG_WIDGET(main_layout));

    /* Title */
    g_autoptr(LrgLabel) title = lrg_label_new("Settings");
    GrlColor title_color = GRL_COLOR(100, 150, 255, 255);
    lrg_label_set_color(title, &title_color);
    lrg_label_set_font_size(title, 24.0f);
    lrg_container_add_child(LRG_CONTAINER(main_layout), LRG_WIDGET(title));

    /* Audio section */
    g_autoptr(LrgLabel) audio_label = lrg_label_new("Audio");
    GrlColor section_color = GRL_COLOR(150, 150, 180, 255);
    lrg_label_set_color(audio_label, &section_color);
    lrg_container_add_child(LRG_CONTAINER(main_layout), LRG_WIDGET(audio_label));

    /* Sound enable checkbox */
    g_autoptr(LrgCheckbox) sound_check = lrg_checkbox_new("Enable Sound");
    lrg_checkbox_set_checked(sound_check, TRUE);
    lrg_container_add_child(LRG_CONTAINER(main_layout), LRG_WIDGET(sound_check));

    /* Volume slider with label */
    g_autoptr(LrgHBox) volume_container = lrg_hbox_new();
    lrg_container_set_spacing(LRG_CONTAINER(volume_container), 10);

    g_autoptr(LrgLabel) volume_label = lrg_label_new("Volume:");
    g_autoptr(LrgSlider) volume_slider = lrg_slider_new_with_range(0, 100, 5);
    lrg_slider_set_value(volume_slider, 70.0);

    GrlColor fill_color = GRL_COLOR(100, 150, 255, 255);
    lrg_slider_set_fill_color(volume_slider, &fill_color);

    lrg_widget_set_width(LRG_WIDGET(volume_label), 60);
    lrg_widget_set_width(LRG_WIDGET(volume_slider), 300);

    lrg_container_add_child(LRG_CONTAINER(volume_container), LRG_WIDGET(volume_label));
    lrg_container_add_child(LRG_CONTAINER(volume_container), LRG_WIDGET(volume_slider));

    lrg_container_add_child(LRG_CONTAINER(main_layout), LRG_WIDGET(volume_container));

    /* Graphics section */
    g_autoptr(LrgLabel) graphics_label = lrg_label_new("Graphics");
    lrg_label_set_color(graphics_label, &section_color);
    lrg_container_add_child(LRG_CONTAINER(main_layout), LRG_WIDGET(graphics_label));

    /* Brightness slider */
    g_autoptr(LrgHBox) brightness_container = lrg_hbox_new();
    lrg_container_set_spacing(LRG_CONTAINER(brightness_container), 10);

    g_autoptr(LrgLabel) brightness_label = lrg_label_new("Brightness:");
    g_autoptr(LrgSlider) brightness_slider = lrg_slider_new_with_range(50, 150, 5);
    lrg_slider_set_value(brightness_slider, 100.0);

    lrg_slider_set_fill_color(brightness_slider, &fill_color);

    lrg_widget_set_width(LRG_WIDGET(brightness_label), 80);
    lrg_widget_set_width(LRG_WIDGET(brightness_slider), 280);

    lrg_container_add_child(LRG_CONTAINER(brightness_container), LRG_WIDGET(brightness_label));
    lrg_container_add_child(LRG_CONTAINER(brightness_container), LRG_WIDGET(brightness_slider));

    lrg_container_add_child(LRG_CONTAINER(main_layout), LRG_WIDGET(brightness_container));

    /* Button row at bottom */
    g_autoptr(LrgHBox) button_row = lrg_hbox_new();
    lrg_container_set_spacing(LRG_CONTAINER(button_row), 10);

    g_autoptr(LrgButton) ok_btn = lrg_button_new("OK");
    g_autoptr(LrgButton) cancel_btn = lrg_button_new("Cancel");
    g_autoptr(LrgButton) apply_btn = lrg_button_new("Apply");

    lrg_widget_set_width(LRG_WIDGET(ok_btn), 100);
    lrg_widget_set_width(LRG_WIDGET(cancel_btn), 100);
    lrg_widget_set_width(LRG_WIDGET(apply_btn), 100);

    GrlColor btn_normal = GRL_COLOR(100, 100, 100, 255);
    GrlColor btn_hover = GRL_COLOR(120, 120, 120, 255);
    GrlColor btn_text = GRL_COLOR(255, 255, 255, 255);

    for (guint i = 0; i < 3; i++)
    {
        LrgButton *btn = (i == 0) ? ok_btn : (i == 1) ? cancel_btn : apply_btn;
        lrg_button_set_normal_color(btn, &btn_normal);
        lrg_button_set_hover_color(btn, &btn_hover);
        lrg_button_set_text_color(btn, &btn_text);
        lrg_button_set_corner_radius(btn, 4.0f);
    }

    lrg_container_add_child(LRG_CONTAINER(button_row), LRG_WIDGET(ok_btn));
    lrg_container_add_child(LRG_CONTAINER(button_row), LRG_WIDGET(cancel_btn));
    lrg_container_add_child(LRG_CONTAINER(button_row), LRG_WIDGET(apply_btn));

    lrg_container_add_child(LRG_CONTAINER(main_layout), LRG_WIDGET(button_row));
}
```

## Game Loop Integration

```c
void ui_update(UIExample *ex)
{
    /* Process input and dispatch to UI */
    lrg_canvas_handle_input(ex->ui_canvas);

    /* Render entire UI */
    lrg_canvas_render(ex->ui_canvas);
}

void ui_destroy(UIExample *ex)
{
    g_object_unref(ex->ui_canvas);
    g_free(ex);
}
```

## Example: Interactive Dialog

```c
typedef struct
{
    LrgButton *ok_button;
    LrgButton *cancel_button;
    gboolean result;  /* TRUE for OK, FALSE for Cancel */
} DialogResult;

void create_yes_no_dialog(LrgCanvas *canvas,
                          const gchar *message,
                          DialogResult *result)
{
    result->result = FALSE;

    /* Dialog panel */
    g_autoptr(LrgPanel) dialog = lrg_panel_new();
    lrg_widget_set_position(LRG_WIDGET(dialog), 300, 200);
    lrg_widget_set_size(LRG_WIDGET(dialog), 400, 150);

    GrlColor bg = GRL_COLOR(40, 40, 50, 255);
    lrg_panel_set_background_color(dialog, &bg);

    lrg_container_add_child(LRG_CONTAINER(canvas), LRG_WIDGET(dialog));

    /* Layout */
    g_autoptr(LrgVBox) layout = lrg_vbox_new();
    lrg_container_set_padding(LRG_CONTAINER(layout), 20);
    lrg_container_set_spacing(LRG_CONTAINER(layout), 20);
    lrg_container_add_child(LRG_CONTAINER(dialog), LRG_WIDGET(layout));

    /* Message */
    g_autoptr(LrgLabel) msg_label = lrg_label_new(message);
    lrg_container_add_child(LRG_CONTAINER(layout), LRG_WIDGET(msg_label));

    /* Buttons */
    g_autoptr(LrgHBox) btn_row = lrg_hbox_new();
    lrg_container_set_spacing(LRG_CONTAINER(btn_row), 10);

    result->ok_button = lrg_button_new("Yes");
    result->cancel_button = lrg_button_new("No");

    lrg_widget_set_width(LRG_WIDGET(result->ok_button), 100);
    lrg_widget_set_width(LRG_WIDGET(result->cancel_button), 100);

    lrg_container_add_child(LRG_CONTAINER(btn_row), LRG_WIDGET(result->ok_button));
    lrg_container_add_child(LRG_CONTAINER(btn_row), LRG_WIDGET(result->cancel_button));

    lrg_container_add_child(LRG_CONTAINER(layout), LRG_WIDGET(btn_row));
}
```

## Common UI Patterns

### Menu with List

```c
g_autoptr(LrgVBox) menu = lrg_vbox_new();
lrg_container_set_spacing(LRG_CONTAINER(menu), 5);

const gchar *items[] = {"New Game", "Continue", "Settings", "Quit"};
for (guint i = 0; i < 4; i++)
{
    g_autoptr(LrgButton) item = lrg_button_new(items[i]);
    lrg_widget_set_width(LRG_WIDGET(item), 200);
    lrg_widget_set_height(LRG_WIDGET(item), 40);
    lrg_container_add_child(LRG_CONTAINER(menu), LRG_WIDGET(item));
}
```

### Form Layout

```c
g_autoptr(LrgGrid) form = lrg_grid_new(2);
lrg_grid_set_column_spacing(form, 10);
lrg_grid_set_row_spacing(form, 10);

/* Name field */
g_autoptr(LrgLabel) name_lbl = lrg_label_new("Name:");
g_autoptr(LrgTextInput) name_input = lrg_text_input_new_with_placeholder("Enter your name");
lrg_container_add_child(LRG_CONTAINER(form), LRG_WIDGET(name_lbl));
lrg_container_add_child(LRG_CONTAINER(form), LRG_WIDGET(name_input));

/* Email field */
g_autoptr(LrgLabel) email_lbl = lrg_label_new("Email:");
g_autoptr(LrgTextInput) email_input = lrg_text_input_new_with_placeholder("Enter your email");
lrg_container_add_child(LRG_CONTAINER(form), LRG_WIDGET(email_lbl));
lrg_container_add_child(LRG_CONTAINER(form), LRG_WIDGET(email_input));
```

### Progress Indicator

```c
g_autoptr(LrgVBox) progress_box = lrg_vbox_new();
lrg_container_set_spacing(LRG_CONTAINER(progress_box), 10);

g_autoptr(LrgLabel) status = lrg_label_new("Loading...");
lrg_container_add_child(LRG_CONTAINER(progress_box), LRG_WIDGET(status));

g_autoptr(LrgProgressBar) bar = lrg_progress_bar_new();
lrg_progress_bar_set_show_text(bar, TRUE);
lrg_progress_bar_set_value(bar, 45.0);
lrg_progress_bar_set_max(bar, 100.0);
lrg_widget_set_width(LRG_WIDGET(bar), 300);

lrg_container_add_child(LRG_CONTAINER(progress_box), LRG_WIDGET(bar));
```

## Complete Main Function

```c
int main(void)
{
    UIExample *ex = example_new();
    setup_dark_theme(ex);
    create_settings_dialog(ex);

    gboolean running = TRUE;

    while (running)
    {
        /* Process UI input and rendering */
        ui_update(ex);

        /* Game loop continues... */
        running = get_window_should_close() == FALSE;
    }

    ui_destroy(ex);
    return 0;
}
```

## Key Concepts Demonstrated

1. **Hierarchical layout** - Nested containers with VBox/HBox
2. **Theme customization** - Colors, fonts, spacing
3. **Widget composition** - Building complex UIs from simple widgets
4. **Event handling** - Buttons respond to clicks
5. **Focus management** - Text input gets keyboard focus
6. **Layout spacing** - Consistent padding and spacing
7. **Styled containers** - Panels with borders and colors
8. **Form layouts** - Using grids for aligned input fields

## Next Steps

- See [UI Module Overview](../modules/ui/index.md) for complete documentation
- See [Input Handling Example](input-handling.md) for input integration
- See [Widget Documentation](../modules/ui/widget.md) for widget details
