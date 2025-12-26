# VBox (Vertical Box) Widget

Layout container that stacks children vertically.

## Creation

```c
g_autoptr(LrgVBox) vbox = lrg_vbox_new();
lrg_container_set_spacing(LRG_CONTAINER(vbox), 10);
lrg_container_set_padding(LRG_CONTAINER(vbox), 5);
lrg_container_add_child(container, LRG_WIDGET(vbox));
```

## Adding Children

```c
g_autoptr(LrgLabel) title = lrg_label_new("Settings");
g_autoptr(LrgSlider) volume = lrg_slider_new();
g_autoptr(LrgButton) ok = lrg_button_new("OK");

lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(title));
lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(volume));
lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(ok));

/* Children stack vertically from top to bottom */
```

## Homogeneous Layout

```c
/* All children get equal height */
lrg_vbox_set_homogeneous(vbox, TRUE);

/* Variable heights */
lrg_vbox_set_homogeneous(vbox, FALSE);
```

## API Reference

- `lrg_vbox_new()`
- `lrg_vbox_get_homogeneous/set_homogeneous`
- (inherits from Container: add_child, spacing, padding, etc.)

## Related

- [HBox](hbox.md)
- [Container](../container.md)
- [UI Module Overview](../index.md)
