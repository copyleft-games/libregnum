# Scene Module Overview

The Scene module provides a complete system for loading, manipulating, and saving 3D scene data. It is designed to work seamlessly with Blender YAML exports, enabling a data-driven workflow for game development.

## Architecture

The scene system follows a hierarchical structure:

```
LrgScene
├── Metadata (name, exported_from, export_date)
└── Entities (GHashTable)
    └── LrgSceneEntity
        ├── Transform (location, rotation, scale)
        └── Objects (GPtrArray)
            └── LrgSceneObject
                ├── Transform (location, rotation, scale)
                ├── Primitive Type
                ├── Material (LrgMaterial3D)
                ├── Parameters (GHashTable)
                └── Mesh Data (LrgMeshData, for custom meshes)
```

### Core Types

| Type | Description |
|------|-------------|
| `LrgScene` | Root container for all scene data |
| `LrgSceneEntity` | Groups related objects (e.g., a character with multiple parts) |
| `LrgSceneObject` | Individual primitive shape with transform and material |
| `LrgMaterial3D` | PBR material properties (color, roughness, metallic, emission) |
| `LrgMeshData` | Custom mesh geometry (vertices, faces) for `LRG_PRIMITIVE_MESH` |
| `LrgSceneSerializer` | Interface for loading/saving scenes |
| `LrgSceneSerializerYaml` | Derivable YAML format base class |
| `LrgSceneSerializerBlender` | Blender-specific YAML with Z-up → Y-up conversion |

### Primitive Types

The scene system supports all standard Blender primitives plus custom mesh geometry:

| Enum Value | Blender Type | Libregnum Shape |
|------------|--------------|-----------------|
| `LRG_PRIMITIVE_PLANE` | Plane | `LrgPlane3D` |
| `LRG_PRIMITIVE_CUBE` | Cube | `LrgCube3D` |
| `LRG_PRIMITIVE_CIRCLE` | Circle | `LrgCircle3D` |
| `LRG_PRIMITIVE_UV_SPHERE` | UV Sphere | `LrgSphere3D` |
| `LRG_PRIMITIVE_ICO_SPHERE` | Ico Sphere | `LrgIcoSphere3D` |
| `LRG_PRIMITIVE_CYLINDER` | Cylinder | `LrgCylinder3D` |
| `LRG_PRIMITIVE_CONE` | Cone | `LrgCone3D` |
| `LRG_PRIMITIVE_TORUS` | Torus | `LrgTorus3D` |
| `LRG_PRIMITIVE_GRID` | Grid | `LrgGrid3D` |
| `LRG_PRIMITIVE_MESH` | Custom Mesh | N/A (uses `LrgMeshData`) |

## Transform Hierarchy

The scene uses a two-level transform hierarchy:

1. **Entity Transform**: World-space position, rotation, and scale for the entire entity
2. **Object Transform**: Local-space transform relative to the entity

When rendering, object transforms are combined with entity transforms to produce final world positions.

```c
/* Entity at world position (10, 0, 0) */
lrg_scene_entity_set_location_xyz (entity, 10.0f, 0.0f, 0.0f);

/* Object at local position (1, 0, 0) relative to entity */
lrg_scene_object_set_location_xyz (object, 1.0f, 0.0f, 0.0f);

/* Final world position: (11, 0, 0) */
```

## Material System

`LrgMaterial3D` provides PBR (Physically Based Rendering) material properties:

| Property | Type | Range | Description |
|----------|------|-------|-------------|
| `color` | RGBA floats | 0.0-1.0 | Base color with alpha |
| `roughness` | float | 0.0-1.0 | Surface roughness (0=smooth, 1=rough) |
| `metallic` | float | 0.0-1.0 | Metallic factor (0=dielectric, 1=metal) |
| `emission_color` | RGBA floats | 0.0-1.0 | Emission/glow color |
| `emission_strength` | float | 0.0+ | Emission intensity multiplier |

## Serialization

The `LrgSceneSerializer` interface defines methods for loading and saving scenes:

```c
/* Load from file */
LrgScene * lrg_scene_serializer_load_from_file (serializer, path, &error);

/* Load from string data */
LrgScene * lrg_scene_serializer_load_from_data (serializer, data, length, &error);

/* Save to file */
gboolean lrg_scene_serializer_save_to_file (serializer, scene, path, &error);

/* Save to string */
gchar * lrg_scene_serializer_save_to_data (serializer, scene, &length);
```

The YAML implementation (`LrgSceneSerializerYaml`) uses high-precision float formatting (`%.17g`) to ensure lossless round-trips.

## Blender Integration

The scene module is designed to work with YAML exports from Blender. A typical workflow:

1. Create your scene in Blender using standard primitives
2. Export using the Blender YAML exporter addon
3. Load the YAML in libregnum using `LrgSceneSerializerBlender`
4. Iterate entities and objects to create game objects
5. Modify and save back to YAML if needed

> **Note:** `LrgSceneSerializerBlender` automatically converts from Blender's Z-up
> coordinate system to raylib's Y-up system. For generic YAML scenes without
> coordinate conversion, use the base `LrgSceneSerializerYaml` class.

See [Tutorial](tutorial.md) for a step-by-step guide.

## Memory Management

All scene types follow GObject conventions:

- Constructors return owned references (transfer full)
- Getter methods return borrowed references (transfer none)
- Add methods take borrowed references and internally ref objects
- Use `g_object_unref()` or `g_autoptr()` for cleanup

```c
g_autoptr(LrgScene) scene = lrg_scene_new ("my-scene");
g_autoptr(LrgSceneEntity) entity = lrg_scene_entity_new ("player");

/* Entity is ref'd when added - safe to unref our reference */
lrg_scene_add_entity (scene, entity);
```

## Thread Safety

Scene objects are not thread-safe. If you need to access scene data from multiple threads:

- Use a mutex to protect access
- Clone scene data for read-only access in other threads
- Perform all modifications on the main thread

## Related Documentation

- [YAML Format Reference](yaml-format.md) - Complete YAML specification
- [API Reference](api-reference.md) - Full API documentation
- [Primitives](primitives.md) - Detailed primitive type reference
- [Tutorial](tutorial.md) - Step-by-step usage guide
