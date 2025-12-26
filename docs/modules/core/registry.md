---
title: LrgRegistry
module: core
type: Class
parent: GObject
since: 1.0
---

# LrgRegistry

Type registry mapping string names to GTypes for data-driven object instantiation.

> **[Home](../../index.md)** > **[Modules](../index.md)** > **[Core](index.md)** > LrgRegistry

## Overview

LrgRegistry enables fully data-driven gameplay by mapping readable type names to GTypes:

```c
lrg_registry_register (registry, "player", MY_TYPE_PLAYER);
GObject *player = lrg_registry_create (registry, "player", NULL);
```

See [Type Registry Concept](../../concepts/type-registry.md) for detailed information.

## Key Methods

### Registration

- `lrg_registry_register()` - Register a type
- `lrg_registry_unregister()` - Remove a registration
- `lrg_registry_is_registered()` - Check if type is registered

### Lookup

- `lrg_registry_lookup()` - Get GType from name
- `lrg_registry_lookup_name()` - Get name from GType

### Creation

- `lrg_registry_create()` - Create object by name
- `lrg_registry_create_with_properties()` - Create with properties

### Enumeration

- `lrg_registry_get_names()` - List all names
- `lrg_registry_get_count()` - Count of registrations
- `lrg_registry_foreach()` - Iterate registrations

### Management

- `lrg_registry_clear()` - Remove all registrations
- `lrg_registry_register_builtin()` - Register built-in types

## Complete API Reference

See `/var/home/zach/Source/Projects/libregnum/src/core/lrg-registry.h` for full function signatures.

## Example

```c
#include <libregnum.h>

int main ()
{
    LrgRegistry *registry = lrg_registry_new ();

    /* Register types */
    lrg_registry_register (registry, "player", MY_TYPE_PLAYER);

    /* Create by name */
    GObject *obj = lrg_registry_create (registry, "player",
                                        "name", "Hero",
                                        NULL);

    g_object_unref (obj);
    g_object_unref (registry);
    return 0;
}
```

## See Also

- [Type Registry Concept](../../concepts/type-registry.md)
- [LrgEngine](engine.md)
- [LrgDataLoader](data-loader.md)
