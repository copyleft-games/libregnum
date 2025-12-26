# Slider Widget

Numeric value selector with configurable range, step, and orientation.

## Creation

```c
g_autoptr(LrgSlider) slider = lrg_slider_new();  /* 0-100 default */
g_autoptr(LrgSlider) slider_range = lrg_slider_new_with_range(0, 10, 0.5);
lrg_widget_set_size(LRG_WIDGET(slider), 200, 20);
```

## Value

```c
lrg_slider_set_value(slider, 50.0);
gdouble value = lrg_slider_get_value(slider);
gdouble fraction = lrg_slider_get_fraction(slider);  /* 0.0-1.0 */
```

## Range

```c
lrg_slider_set_min(slider, 0.0);
lrg_slider_set_max(slider, 100.0);
lrg_slider_set_step(slider, 5.0);

lrg_slider_set_range(slider, 10, 100);  /* Set min and max */

gdouble min = lrg_slider_get_min(slider);
gdouble max = lrg_slider_get_max(slider);
gdouble step = lrg_slider_get_step(slider);
```

## Orientation

```c
lrg_slider_set_orientation(slider, LRG_ORIENTATION_HORIZONTAL);
/* or LRG_ORIENTATION_VERTICAL */
```

## Appearance

```c
lrg_slider_set_track_color(slider, &track_color);
lrg_slider_set_fill_color(slider, &fill_color);
lrg_slider_set_handle_color(slider, &handle_color);
lrg_slider_set_handle_size(slider, 10.0f);
lrg_slider_set_track_thickness(slider, 4.0f);
```

## API Reference

- `lrg_slider_new()`
- `lrg_slider_new_with_range(gdouble min, gdouble max, gdouble step)`
- `lrg_slider_get_value(LrgSlider *self)`
- `lrg_slider_set_value(LrgSlider *self, gdouble value)`
- `lrg_slider_get_min/set_min/get_max/set_max/get_step/set_step`
- `lrg_slider_set_range(LrgSlider *self, gdouble min, gdouble max)`
- `lrg_slider_get_fraction(LrgSlider *self)`
- `lrg_slider_get_orientation/set_orientation`

## Related

- [UI Widgets Overview](../index.md)
