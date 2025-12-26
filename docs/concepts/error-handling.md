---
title: Error Handling
concept: core
---

# Error Handling

Libregnum uses GLib's GError pattern for consistent, recoverable error handling throughout the library. This document explains error handling patterns and best practices.

> **[Home](../../index.md)** > **[Concepts](index.md)** > Error Handling

## GError Basics

GError is a standard GLib pattern for reporting errors:

```c
GError *error = NULL;

gboolean result = lrg_engine_startup (engine, &error);

if (!result)
{
    if (error)
    {
        g_warning ("Failed: %s", error->message);
        g_error_free (error);
    }
    return 1;
}
```

Always use `g_autoptr(GError)` for automatic cleanup:

```c
g_autoptr(GError) error = NULL;

if (!lrg_engine_startup (engine, &error))
{
    g_warning ("Failed: %s", error->message);
    return 1;
}
/* error automatically freed here */
```

## Error Structure

Each error has three components:

```c
struct _GError
{
    GQuark domain;      /* Error category (e.g., LRG_ENGINE_ERROR) */
    gint code;          /* Specific error code */
    gchar *message;     /* Human-readable message */
};
```

## Error Domains

Libregnum uses error domains for different subsystems:

| Domain | Errors |
|--------|--------|
| `LRG_ENGINE_ERROR` | Engine startup/state errors |
| `LRG_DATA_LOADER_ERROR` | YAML loading errors |
| `LRG_ASSET_MANAGER_ERROR` | Asset loading errors |
| `LRG_MOD_ERROR` | Mod system errors |
| `LRG_DIALOG_ERROR` | Dialog system errors |
| `LRG_SAVE_ERROR` | Save system errors |
| `LRG_PATHFINDING_ERROR` | Pathfinding errors |

## Engine Errors

```c
typedef enum
{
    LRG_ENGINE_ERROR_FAILED,      /* Generic failure */
    LRG_ENGINE_ERROR_INIT,        /* Initialization failed */
    LRG_ENGINE_ERROR_STATE        /* Invalid state */
} LrgEngineError;
```

### Example: Handling Engine Errors

```c
g_autoptr(GError) error = NULL;

if (!lrg_engine_startup (engine, &error))
{
    if (error->domain == LRG_ENGINE_ERROR)
    {
        switch (error->code)
        {
        case LRG_ENGINE_ERROR_INIT:
            g_warning ("Failed to initialize subsystems");
            return 1;
        case LRG_ENGINE_ERROR_STATE:
            g_warning ("Engine already running");
            return 1;
        default:
            g_warning ("Unknown engine error: %s", error->message);
            return 1;
        }
    }
}
```

## Data Loader Errors

```c
typedef enum
{
    LRG_DATA_LOADER_ERROR_FAILED,      /* Generic failure */
    LRG_DATA_LOADER_ERROR_IO,          /* File not found, permission denied */
    LRG_DATA_LOADER_ERROR_PARSE,       /* Invalid YAML syntax */
    LRG_DATA_LOADER_ERROR_TYPE,        /* Type not registered */
    LRG_DATA_LOADER_ERROR_PROPERTY     /* Property error */
} LrgDataLoaderError;
```

### Example: Handling Data Loader Errors

```c
g_autoptr(GError) error = NULL;

GObject *obj = lrg_data_loader_load_file (loader, "player.yaml", &error);

if (!obj && error)
{
    if (error->domain == LRG_DATA_LOADER_ERROR)
    {
        switch (error->code)
        {
        case LRG_DATA_LOADER_ERROR_IO:
            g_warning ("File not found: player.yaml");
            return 1;
        case LRG_DATA_LOADER_ERROR_PARSE:
            g_warning ("Invalid YAML syntax: %s", error->message);
            return 1;
        case LRG_DATA_LOADER_ERROR_TYPE:
            g_warning ("Type not registered: %s", error->message);
            /* Could attempt recovery by registering the type */
            return 1;
        case LRG_DATA_LOADER_ERROR_PROPERTY:
            g_warning ("Property error: %s", error->message);
            return 1;
        default:
            g_warning ("Unknown loader error: %s", error->message);
            return 1;
        }
    }
}
```

## Asset Manager Errors

```c
typedef enum
{
    LRG_ASSET_MANAGER_ERROR_NOT_FOUND,    /* Asset file not found */
    LRG_ASSET_MANAGER_ERROR_LOAD_FAILED,  /* Loading failed */
    LRG_ASSET_MANAGER_ERROR_INVALID_TYPE  /* Wrong asset type */
} LrgAssetManagerError;
```

### Example: Handling Asset Errors

```c
g_autoptr(GError) error = NULL;

GrlTexture *texture = lrg_asset_manager_load_texture (manager, "sprites/player.png", &error);

if (!texture && error)
{
    if (error->domain == LRG_ASSET_MANAGER_ERROR)
    {
        switch (error->code)
        {
        case LRG_ASSET_MANAGER_ERROR_NOT_FOUND:
            g_warning ("Asset not found in search paths: sprites/player.png");
            /* Could provide a placeholder texture */
            break;
        case LRG_ASSET_MANAGER_ERROR_LOAD_FAILED:
            g_warning ("Failed to load texture: %s", error->message);
            break;
        default:
            g_warning ("Unknown asset error: %s", error->message);
        }
        return 1;
    }
}
```

## Error Checking Patterns

### Simple Success Check

For simple success/failure:

```c
if (!lrg_engine_startup (engine, &error))
{
    g_warning ("Startup failed: %s", error->message);
    return 1;
}
```

### Specific Domain Check

When multiple error types are possible:

```c
if (error->domain != LRG_DATA_LOADER_ERROR)
{
    g_warning ("Unexpected error domain");
    return 1;
}
```

### Ignore Specific Errors

Continue on certain errors:

```c
GObject *obj = lrg_data_loader_load_file (loader, "config.yaml", &error);

if (!obj && error)
{
    if (error->code == LRG_DATA_LOADER_ERROR_IO)
    {
        g_print ("Config file not found, using defaults\n");
        g_clear_error (&error);
        /* Continue with defaults */
    }
    else
    {
        g_warning ("Failed to load config: %s", error->message);
        return 1;
    }
}
```

### Multiple Operations

Handle errors from multiple calls:

```c
g_autoptr(GError) error = NULL;

if (!lrg_engine_startup (engine, &error))
{
    g_warning ("Startup failed: %s", error->message);
    return 1;
}

GObject *obj = lrg_data_loader_load_file (loader, "player.yaml", &error);
if (!obj)
{
    g_warning ("Load failed: %s", error->message);
    lrg_engine_shutdown (engine);
    return 1;
}
```

## Error Recovery

### Retry Pattern

```c
gint max_retries = 3;

for (gint i = 0; i < max_retries; i++)
{
    g_clear_error (&error);

    GObject *obj = lrg_data_loader_load_file (loader, "player.yaml", &error);

    if (obj)
    {
        g_print ("Loaded successfully\n");
        return obj;
    }

    if (i < max_retries - 1)
    {
        g_print ("Retry %d/%d\n", i + 1, max_retries);
        g_usleep (100000);  /* Wait 100ms */
    }
}

g_warning ("Failed after %d retries", max_retries);
return NULL;
```

### Fallback Pattern

```c
g_autoptr(GError) error = NULL;

GObject *obj = lrg_data_loader_load_file (loader, "player.yaml", &error);

if (!obj)
{
    g_warning ("Failed to load player.yaml, trying fallback: %s", error->message);
    g_clear_error (&error);

    /* Try fallback */
    obj = lrg_data_loader_load_file (loader, "player-default.yaml", &error);

    if (!obj)
    {
        g_error ("Fallback also failed: %s", error->message);
    }
}

return obj;
```

## Functions That Don't Use GError

Not all functions use GError. Check the documentation:

- `lrg_engine_shutdown()` - Always succeeds
- `lrg_engine_update()` - Never fails
- `lrg_registry_register()` - Never fails
- `lrg_object_new()` - Returns NULL on failure (no error)

For functions without GError, check return values:

```c
/* No error parameter */
GObject *obj = lrg_registry_create (registry, "player", NULL);

if (!obj)
{
    g_warning ("Failed to create player");
}
```

## Creating Errors (Library Implementers)

If implementing Libregnum subsystems, create errors properly:

```c
/* Define error domain */
#define MY_MODULE_ERROR (my_module_error_quark ())

GQuark
my_module_error_quark (void)
{
    return g_quark_from_static_string ("my-module-error");
}

/* Use in functions */
gboolean
my_function (MyModule *self,
             GError  **error)
{
    if (some_failure)
    {
        g_set_error (error,
                     MY_MODULE_ERROR,
                     MY_ERROR_CODE_SOMETHING,
                     "Description: %s", reason);
        return FALSE;
    }

    return TRUE;
}
```

## Logging Errors

Always log detailed error information:

```c
g_autoptr(GError) error = NULL;

if (!lrg_engine_startup (engine, &error))
{
    g_error ("Engine startup failed [%s %d]: %s",
             g_quark_to_string (error->domain),
             error->code,
             error->message);
}
```

For warnings without stopping:

```c
if (!obj && error)
{
    g_warning ("Failed to load: [%s %d] %s",
               g_quark_to_string (error->domain),
               error->code,
               error->message);
}
```

## Debugging Errors

Enable verbose error output:

```bash
# Enable all debug messages
G_MESSAGES_DEBUG=all ./myapp

# Enable specific domains
G_MESSAGES_DEBUG=Libregnum-Core,Libregnum-Loader ./myapp
```

Print full error details in code:

```c
if (error)
{
    g_print ("Error Details:\n");
    g_print ("  Domain: %s (0x%x)\n", g_quark_to_string (error->domain), error->domain);
    g_print ("  Code: %d\n", error->code);
    g_print ("  Message: %s\n", error->message);
}
```

## Best Practices

1. **Always use g_autoptr for automatic cleanup**
   ```c
   g_autoptr(GError) error = NULL;  /* Good */
   GError *error = NULL;              /* Manual cleanup needed */
   ```

2. **Check for specific error codes**
   ```c
   if (error->code == SPECIFIC_CODE) { /* Handle specifically */ }
   ```

3. **Log errors with context**
   ```c
   g_warning ("Failed to load %s: %s", filename, error->message);
   ```

4. **Return early on errors**
   ```c
   if (!operation (arg, &error)) {
       return handle_error (error);
   }
   ```

5. **Don't mix error handling with success path**
   ```c
   /* Good */
   if (!operation (arg, &error))
       return error_handler ();

   /* Bad - unclear logic */
   result = operation (arg, &error);
   if (result) { ... success path ... }
   else { ... error path ... }
   ```

## Complete Example

```c
#include <libregnum.h>
#include <glib.h>

int
main (int argc, char *argv[])
{
    g_autoptr(GError) startup_error = NULL;
    g_autoptr(GError) load_error = NULL;
    LrgEngine *engine;
    LrgDataLoader *loader;
    g_autoptr(GObject) player = NULL;

    /* Startup with error handling */
    engine = lrg_engine_get_default ();
    if (!lrg_engine_startup (engine, &startup_error))
    {
        if (startup_error->domain == LRG_ENGINE_ERROR)
        {
            if (startup_error->code == LRG_ENGINE_ERROR_STATE)
            {
                g_warning ("Engine already running");
            }
            else
            {
                g_warning ("Startup failed: %s", startup_error->message);
            }
        }
        return 1;
    }

    /* Load with error handling */
    loader = lrg_engine_get_data_loader (engine);
    player = lrg_data_loader_load_file (loader, "player.yaml", &load_error);

    if (!player && load_error)
    {
        if (load_error->code == LRG_DATA_LOADER_ERROR_IO)
        {
            g_warning ("Player file not found");
        }
        else if (load_error->code == LRG_DATA_LOADER_ERROR_TYPE)
        {
            g_warning ("Player type not registered");
        }
        else
        {
            g_warning ("Failed to load player: %s", load_error->message);
        }

        lrg_engine_shutdown (engine);
        return 1;
    }

    /* Success */
    g_print ("Player loaded successfully\n");

    lrg_engine_shutdown (engine);
    return 0;
}
```

## See Also

- [Architecture Overview](../architecture.md) - Error handling patterns
- [GLib Error Documentation](https://developer.gnome.org/glib/stable/glib-Error-Reporting.html)
- [Engine Lifecycle](engine-lifecycle.md) - Error states
