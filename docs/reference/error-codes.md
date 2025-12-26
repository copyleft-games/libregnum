# Error Codes Reference

Complete list of error enumerations used throughout Libregnum.

## Error Domains

### LRG_ENGINE_ERROR

Engine initialization and management errors.

| Code | Name | Description |
|------|------|-------------|
| 0 | INITIALIZATION_FAILED | Engine failed to initialize |
| 1 | REGISTRY_ERROR | Registry operation failed |
| 2 | ALREADY_RUNNING | Engine already initialized |
| 3 | NOT_RUNNING | Engine not initialized |

**Macro:** `LRG_ENGINE_ERROR`

### LRG_DATA_LOADER_ERROR

Data loading and parsing errors.

| Code | Name | Description |
|------|------|-------------|
| 0 | FILE_NOT_FOUND | Data file not found |
| 1 | PARSE_ERROR | YAML/JSON parse error |
| 2 | INVALID_TYPE | Invalid type in data |
| 3 | MISSING_FIELD | Required field missing |
| 4 | TYPE_NOT_FOUND | Type not registered |

**Macro:** `LRG_DATA_LOADER_ERROR`

### LRG_MOD_ERROR

Mod system errors.

| Code | Name | Description |
|------|------|-------------|
| 0 | DISCOVERY_FAILED | Failed to discover mods |
| 1 | LOAD_FAILED | Mod failed to load |
| 2 | UNLOAD_FAILED | Mod failed to unload |
| 3 | DEPENDENCY_FAILED | Dependency not satisfied |
| 4 | DEPENDENCY_CIRCULAR | Circular dependency detected |
| 5 | MANIFEST_ERROR | Manifest parsing error |
| 6 | INVALID_MOD | Mod structure invalid |
| 7 | ALREADY_LOADED | Mod already loaded |
| 8 | NOT_LOADED | Mod not loaded |
| 9 | DISABLED | Mod is disabled |

**Macro:** `LRG_MOD_ERROR`

### LRG_WORLD3D_ERROR

3D world and spatial errors.

| Code | Name | Description |
|------|------|-------------|
| 0 | BOUNDS_INVALID | Invalid bounding box |
| 1 | OCTREE_ERROR | Octree operation failed |
| 2 | SECTOR_NOT_FOUND | Sector not found |
| 3 | PORTAL_NOT_FOUND | Portal not found |

**Macro:** `LRG_WORLD3D_ERROR`

## Error Handling Pattern

```c
g_autoptr(GError) error = NULL;

if (!lrg_mod_manager_load_all(mod_mgr, &error))
{
    if (error->domain == LRG_MOD_ERROR)
    {
        switch (error->code)
        {
            case LRG_MOD_ERROR_DEPENDENCY_FAILED:
                g_print("Dependency missing: %s\n", error->message);
                break;
            case LRG_MOD_ERROR_MANIFEST_ERROR:
                g_print("Manifest parsing failed: %s\n", error->message);
                break;
            default:
                g_print("Mod error: %s\n", error->message);
                break;
        }
    }
    return FALSE;
}
```

## Common Patterns

### Checking Error Codes

```c
if (error->domain == LRG_DATA_LOADER_ERROR &&
    error->code == LRG_DATA_LOADER_ERROR_FILE_NOT_FOUND)
{
    g_print("File not found, using defaults\n");
}
else
{
    g_print("Unexpected error: %s\n", error->message);
}
```

### Propagating Errors

```c
gboolean
my_function(const gchar *path, GError **error)
{
    g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new_from_file(path, error);
    if (!manifest)
        return FALSE;  /* Error already set */

    /* Continue processing */
    return TRUE;
}
```

### Creating Custom Errors

```c
if (condition_fails)
{
    g_set_error(error, LRG_MOD_ERROR, LRG_MOD_ERROR_INVALID_MOD,
                "Mod structure invalid: missing 'mod.yaml'");
    return FALSE;
}
```

## See Also

- [Signals](signals.md) - Event signals
- [Properties](properties.md) - Object properties
- [Glossary](glossary.md) - Terminology
