# Image Widget

Display textures with multiple scaling modes and tinting support.

## Creation

```c
g_autoptr(LrgImage) img = lrg_image_new();
g_autoptr(LrgImage) img_with_texture = lrg_image_new_with_texture(my_texture);
lrg_widget_set_size(LRG_WIDGET(img), 200, 150);
lrg_container_add_child(container, LRG_WIDGET(img));
```

## Texture

```c
GrlTexture *texture = grl_texture_load("assets/images/hero.png");
lrg_image_set_texture(img, texture);

GrlTexture *current = lrg_image_get_texture(img);
```

## Scale Mode

```c
/* LRG_IMAGE_SCALE_MODE_FIT - maintain aspect ratio */
/* LRG_IMAGE_SCALE_MODE_FILL - fill, may crop */
/* LRG_IMAGE_SCALE_MODE_STRETCH - stretch to fit */
/* LRG_IMAGE_SCALE_MODE_TILE - repeat pattern */

lrg_image_set_scale_mode(img, LRG_IMAGE_SCALE_MODE_FIT);
```

## Tinting

```c
GrlColor tint = GRL_COLOR(255, 100, 100, 255);  /* Red tint */
lrg_image_set_tint(img, &tint);

GrlColor white = GRL_COLOR(255, 255, 255, 255);  /* No tint */
lrg_image_set_tint(img, &white);
```

## Sprite Sheets

```c
GrlRectangle src_rect = {
    .x = 0.0f, .y = 0.0f,
    .width = 64.0f, .height = 64.0f
};
lrg_image_set_source_rect(img, &src_rect);

lrg_image_clear_source_rect(img);  /* Draw whole texture */
```

## API Reference

- `lrg_image_new()`
- `lrg_image_new_with_texture(GrlTexture *texture)`
- `lrg_image_get_texture/set_texture`
- `lrg_image_get_scale_mode/set_scale_mode`
- `lrg_image_get_tint/set_tint`
- `lrg_image_get_source_rect/set_source_rect`
- `lrg_image_clear_source_rect()`

## Related

- [UI Widgets Overview](../index.md)
