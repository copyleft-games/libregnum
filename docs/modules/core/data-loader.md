---
title: LrgDataLoader
module: core
type: Class
parent: GObject
since: 1.0
---

# LrgDataLoader

YAML file deserialization with GObject integration.

> **[Home](../../index.md)** > **[Modules](../index.md)** > **[Core](index.md)** > LrgDataLoader

## Overview

LrgDataLoader loads GObjects from YAML files using the Registry for type resolution:

```yaml
# player.yaml
type: player
name: "Hero"
health: 100
```

```c
GObject *obj = lrg_data_loader_load_file (loader, "player.yaml", &error);
```

See [Data Loading Concept](../../concepts/data-loading.md) for detailed information.

## Key Methods

### Single File Loading

- `lrg_data_loader_load_file()` - Load from file path
- `lrg_data_loader_load_gfile()` - Load from GFile
- `lrg_data_loader_load_data()` - Load from YAML string
- `lrg_data_loader_load_typed()` - Load as specific type

### Batch Loading

- `lrg_data_loader_load_directory()` - Load all YAML files from directory
- `lrg_data_loader_load_files()` - Load multiple specific files

### Async Loading

- `lrg_data_loader_load_file_async()` - Async single file
- `lrg_data_loader_load_gfile_async()` - Async GFile
- `lrg_data_loader_load_directory_async()` - Async directory

### Configuration

- `lrg_data_loader_set_registry()` - Set type registry
- `lrg_data_loader_get_registry()` - Get type registry
- `lrg_data_loader_set_type_field_name()` - Set "type" field name
- `lrg_data_loader_get_type_field_name()` - Get "type" field name
- `lrg_data_loader_set_file_extensions()` - Set recognized extensions
- `lrg_data_loader_get_file_extensions()` - Get recognized extensions

## Complete API Reference

See `/var/home/zach/Source/Projects/libregnum/src/core/lrg-data-loader.h` for full function signatures.

## Example

```c
#include <libregnum.h>

int main ()
{
    g_autoptr(GError) error = NULL;
    LrgDataLoader *loader = lrg_data_loader_new ();

    /* Setup registry for type resolution */
    LrgRegistry *registry = lrg_registry_new ();
    lrg_registry_register (registry, "player", MY_TYPE_PLAYER);
    lrg_data_loader_set_registry (loader, registry);

    /* Load single file */
    GObject *obj = lrg_data_loader_load_file (loader, "player.yaml", &error);

    if (obj)
    {
        g_print ("Loaded %s\n", G_OBJECT_TYPE_NAME (obj));
        g_object_unref (obj);
    }

    g_object_unref (registry);
    g_object_unref (loader);
    return 0;
}
```

## See Also

- [Data Loading Concept](../../concepts/data-loading.md)
- [Type Registry](registry.md)
- [LrgEngine](engine.md)
