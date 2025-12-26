# Properties Reference

GObject properties available on public types.

## World3D Module

### LrgLevel3D

| Property | Type | Access | Default | Description |
|----------|------|--------|---------|-------------|
| id | string | read | - | Level identifier |
| name | string | read/write | NULL | Display name |
| bounds | boxed | read/write | NULL | Level bounds (LrgBoundingBox3D) |

### LrgOctree

| Property | Type | Access | Default | Description |
|----------|------|--------|---------|-------------|
| bounds | boxed | read | - | World bounds |
| object-count | uint | read | 0 | Total objects |
| node-count | uint | read | 0 | Tree node count |
| max-depth | uint | read/write | 8 | Maximum subdivision depth |
| max-objects | uint | read/write | 16 | Objects per node threshold |

### LrgPortalSystem

| Property | Type | Access | Default | Description |
|----------|------|--------|---------|-------------|
| sector-count | uint | read | 0 | Total sectors |
| portal-count | uint | read | 0 | Total portals |
| visible-count | uint | read | 0 | Currently visible sectors |
| max-portal-depth | uint | read/write | 8 | Portal traversal depth limit |

## Mod System Module

### LrgModManager

| Property | Type | Access | Default | Description |
|----------|------|--------|---------|-------------|
| loader | object | read | - | LrgModLoader instance |

### LrgMod

| Property | Type | Access | Default | Description |
|----------|------|--------|---------|-------------|
| manifest | object | read | - | LrgModManifest |
| id | string | read | - | Mod identifier |
| base-path | string | read | - | Mod directory |
| data-path | string | read | NULL | Data directory path |
| state | int | read | DISCOVERED | Current mod state |
| enabled | boolean | read/write | TRUE | Enabled for loading |
| error | string | read | NULL | Error message if failed |

### LrgModManifest

| Property | Type | Access | Default | Description |
|----------|------|--------|---------|-------------|
| id | string | read | - | Mod ID (immutable) |
| name | string | read/write | NULL | Display name |
| version | string | read/write | NULL | Version string |
| author | string | read/write | NULL | Author name |
| description | string | read/write | NULL | Description |
| type | int | read/write | CONTENT | Mod type |
| priority | int | read/write | NORMAL | Load priority |
| data-path | string | read/write | NULL | Data directory |
| entry-point | string | read/write | NULL | Script entry point |

### LrgModLoader

| Property | Type | Access | Default | Description |
|----------|------|--------|---------|-------------|
| manifest-filename | string | read/write | "mod.yaml" | Manifest filename |

## Core Module Properties

### LrgEngine

| Property | Type | Access | Default | Description |
|----------|------|--------|---------|-------------|
| registry | object | read | - | LrgRegistry instance |
| data-loader | object | read | - | LrgDataLoader instance |

## Using Properties

### Getting Properties

```c
gchar *name;
g_object_get(object, "name", &name, NULL);
g_print("Name: %s\n", name);
g_free(name);
```

### Setting Properties

```c
lrg_level3d_set_name(level, "My Level");

/* Or via g_object_set: */
g_object_set(level, "name", "My Level", NULL);
```

### Property Notifications

```c
static void
on_property_notify(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    g_print("Property '%s' changed\n", pspec->name);
}

g_signal_connect(object, "notify::property-name",
                 G_CALLBACK(on_property_notify), NULL);
```

## See Also

- [Signals](signals.md) - GObject signals
- [Error Codes](error-codes.md) - Error types
