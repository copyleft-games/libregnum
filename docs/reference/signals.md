# Signals Reference

Complete list of GObject signals by type and module.

## World3D Module

### LrgLevel3D

| Signal | Parameters | Description |
|--------|-----------|-------------|
| (none documented) | - | No signals currently defined |

### LrgPortalSystem

| Signal | Parameters | Description |
|--------|-----------|-------------|
| visibility-changed | (void) | Emitted when visible sectors change |

### LrgOctree

| Signal | Parameters | Description |
|--------|-----------|-------------|
| (none documented) | - | No signals currently defined |

## Mod System Module

### LrgModManager

| Signal | Parameters | Description |
|--------|-----------|-------------|
| mod-discovered | (gpointer mod) | Emitted when a mod is discovered |
| mod-loaded | (gpointer mod) | Emitted when a mod finishes loading |
| mod-unloaded | (gpointer mod) | Emitted when a mod is unloaded |
| mod-error | (gpointer mod, gpointer error) | Emitted when a mod fails to load |

### LrgMod

| Signal | Parameters | Description |
|--------|-----------|-------------|
| state-changed | (gint new_state) | Emitted when mod state changes |

### LrgModLoader

| Signal | Parameters | Description |
|--------|-----------|-------------|
| (none documented) | - | No signals currently defined |

## Core Module

### LrgEngine

| Signal | Parameters | Description |
|--------|-----------|-------------|
| startup-complete | (void) | Emitted after engine startup |
| shutdown-begin | (void) | Emitted before engine shutdown |
| update | (gfloat delta_time) | Emitted on each frame update |
| frame-complete | (void) | Emitted after frame processing |

### LrgRegistry

| Signal | Parameters | Description |
|--------|-----------|-------------|
| type-registered | (GType type, gpointer description) | Emitted when type registered |

## Signal Usage

### Connecting to a Signal

```c
g_signal_connect(object, "signal-name", G_CALLBACK(callback_function), user_data);
```

### Example: Mod Manager Signals

```c
static void
on_mod_loaded(LrgModManager *manager, gpointer mod, gpointer user_data)
{
    g_print("Mod loaded!\n");
}

g_signal_connect(mod_mgr, "mod-loaded", G_CALLBACK(on_mod_loaded), NULL);
```

### Example: Engine Signals

```c
static void
on_engine_update(LrgEngine *engine, gfloat delta_time, gpointer user_data)
{
    g_print("Update: %.2f ms\n", delta_time * 1000);
}

g_signal_connect(engine, "update", G_CALLBACK(on_engine_update), NULL);
```

## See Also

- [Properties](properties.md) - GObject properties
- [Error Codes](error-codes.md) - Error enumerations
