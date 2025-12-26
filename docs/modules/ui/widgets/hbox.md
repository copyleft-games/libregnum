# HBox (Horizontal Box) Widget

Layout container that stacks children horizontally.

## Creation

```c
g_autoptr(LrgHBox) hbox = lrg_hbox_new();
lrg_container_set_spacing(LRG_CONTAINER(hbox), 10);
lrg_container_add_child(container, LRG_WIDGET(hbox));
```

## Adding Children

```c
g_autoptr(LrgButton) ok = lrg_button_new("OK");
g_autoptr(LrgButton) cancel = lrg_button_new("Cancel");
g_autoptr(LrgButton) help = lrg_button_new("Help");

lrg_container_add_child(LRG_CONTAINER(hbox), LRG_WIDGET(ok));
lrg_container_add_child(LRG_CONTAINER(hbox), LRG_WIDGET(cancel));
lrg_container_add_child(LRG_CONTAINER(hbox), LRG_WIDGET(help));

/* Children arrange left to right */
```

## Homogeneous Layout

```c
/* All children get equal width */
lrg_hbox_set_homogeneous(hbox, TRUE);

/* Variable widths */
lrg_hbox_set_homogeneous(hbox, FALSE);
```

## API Reference

- `lrg_hbox_new()`
- `lrg_hbox_get_homogeneous/set_homogeneous`
- (inherits from Container: add_child, spacing, padding, etc.)

## Related

- [VBox](vbox.md)
- [Container](../container.md)
- [UI Module Overview](../index.md)
