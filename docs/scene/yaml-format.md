# YAML Scene Format Reference

This document describes the YAML format used for scene serialization in libregnum, designed for compatibility with Blender exports.

## Format Overview

```yaml
scene:
  name: "scene_name"
  exported_from: "Blender 5.0.1"
  export_date: "2025-12-26T22:51:15.268597"

entities:
  entity_name:
    objects:
      - name: "part_name"
        primitive: "primitive_cylinder"
        transform:
          location: [x, y, z]
          rotation: [rx, ry, rz]
          scale: [sx, sy, sz]
        material:
          color: [r, g, b, a]
          roughness: 0.5
          metallic: 0.0
          emission_color: [r, g, b, a]
          emission_strength: 0.0
        params:
          radius: 1.0
          depth: 2.0
          vertices: 32
```

## Scene Metadata

The `scene` section contains optional metadata:

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Scene name/identifier |
| `exported_from` | string | Source application (e.g., "Blender 5.0.1") |
| `export_date` | string | ISO 8601 timestamp |

```yaml
scene:
  name: "santa_sleigh"
  exported_from: "Blender 5.0.1"
  export_date: "2025-12-26T22:51:15.268597"
```

## Entities

Entities are named groups of objects. Each entity is a key under the `entities` mapping:

```yaml
entities:
  santa:
    objects:
      - name: "body"
        # ...
      - name: "head"
        # ...
  sleigh:
    objects:
      - name: "base"
        # ...
```

## Objects

Each object represents a single primitive with its transform, material, and parameters.

### Object Fields

| Field | Required | Description |
|-------|----------|-------------|
| `name` | Yes | Object/part name |
| `primitive` | Yes | Primitive type string |
| `transform` | No | Location, rotation, scale |
| `material` | No | PBR material properties |
| `params` | No | Primitive-specific parameters |

### Primitive Types

| String Value | Enum |
|--------------|------|
| `primitive_plane` | `LRG_PRIMITIVE_PLANE` |
| `primitive_cube` | `LRG_PRIMITIVE_CUBE` |
| `primitive_circle` | `LRG_PRIMITIVE_CIRCLE` |
| `primitive_uv_sphere` | `LRG_PRIMITIVE_UV_SPHERE` |
| `primitive_ico_sphere` | `LRG_PRIMITIVE_ICO_SPHERE` |
| `primitive_cylinder` | `LRG_PRIMITIVE_CYLINDER` |
| `primitive_cone` | `LRG_PRIMITIVE_CONE` |
| `primitive_torus` | `LRG_PRIMITIVE_TORUS` |
| `primitive_grid` | `LRG_PRIMITIVE_GRID` |

### Transform

All transform values use Blender's coordinate system (Z-up, right-handed).

```yaml
transform:
  location: [1.0, 2.0, 3.0]      # X, Y, Z position
  rotation: [0.0, 0.0, 1.5708]   # Euler angles in radians (X, Y, Z)
  scale: [1.0, 1.0, 1.0]         # Scale factors
```

Default values if omitted:
- `location`: `[0.0, 0.0, 0.0]`
- `rotation`: `[0.0, 0.0, 0.0]`
- `scale`: `[1.0, 1.0, 1.0]`

### Material

PBR material properties with physically-based values:

```yaml
material:
  color: [0.8, 0.2, 0.1, 1.0]           # RGBA (0.0-1.0)
  roughness: 0.5                         # 0.0 = smooth, 1.0 = rough
  metallic: 0.0                          # 0.0 = dielectric, 1.0 = metal
  emission_color: [0.0, 0.0, 0.0, 1.0]  # Emission RGBA
  emission_strength: 0.0                 # Emission multiplier
```

Default values if omitted:
- `color`: `[0.8, 0.8, 0.8, 1.0]` (light gray)
- `roughness`: `0.5`
- `metallic`: `0.0`
- `emission_color`: `[0.0, 0.0, 0.0, 1.0]`
- `emission_strength`: `0.0`

## Primitive Parameters

Each primitive type has specific parameters.

### Plane

```yaml
primitive: primitive_plane
params:
  size: 2.0  # Width and length (square)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `size` | float | 2.0 | Plane dimensions |

### Cube

```yaml
primitive: primitive_cube
params:
  size: 2.0  # Edge length
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `size` | float | 2.0 | Cube edge length |

### Circle

```yaml
primitive: primitive_circle
params:
  vertices: 32
  radius: 1.0
  fill_type: "ngon"  # "nothing", "ngon", or "trifan"
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `vertices` | int | 32 | Number of vertices |
| `radius` | float | 1.0 | Circle radius |
| `fill_type` | string | "ngon" | Fill mode |

### UV Sphere

```yaml
primitive: primitive_uv_sphere
params:
  segments: 32
  ring_count: 16
  radius: 1.0
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `segments` | int | 32 | Longitude segments |
| `ring_count` | int | 16 | Latitude rings |
| `radius` | float | 1.0 | Sphere radius |

### Ico Sphere

```yaml
primitive: primitive_ico_sphere
params:
  subdivisions: 2
  radius: 1.0
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `subdivisions` | int | 2 | Subdivision level (1-6) |
| `radius` | float | 1.0 | Sphere radius |

### Cylinder

```yaml
primitive: primitive_cylinder
params:
  vertices: 32
  radius: 1.0
  depth: 2.0
  cap_ends: true
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `vertices` | int | 32 | Number of sides |
| `radius` | float | 1.0 | Cylinder radius |
| `depth` | float | 2.0 | Cylinder height |
| `cap_ends` | bool | true | Draw end caps |

### Cone

```yaml
primitive: primitive_cone
params:
  vertices: 32
  radius1: 1.0   # Bottom radius
  radius2: 0.0   # Top radius (0 = point)
  depth: 2.0
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `vertices` | int | 32 | Number of sides |
| `radius1` | float | 1.0 | Bottom radius |
| `radius2` | float | 0.0 | Top radius |
| `depth` | float | 2.0 | Cone height |

### Torus

```yaml
primitive: primitive_torus
params:
  major_segments: 48
  minor_segments: 12
  major_radius: 1.0
  minor_radius: 0.25
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `major_segments` | int | 48 | Ring segments |
| `minor_segments` | int | 12 | Cross-section segments |
| `major_radius` | float | 1.0 | Ring radius |
| `minor_radius` | float | 0.25 | Tube radius |

### Grid

```yaml
primitive: primitive_grid
params:
  x_subdivisions: 10
  y_subdivisions: 10
  size: 1.0
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `x_subdivisions` | int | 10 | X divisions |
| `y_subdivisions` | int | 10 | Y divisions |
| `size` | float | 1.0 | Grid spacing |

## Complete Example

```yaml
scene:
  name: "example_scene"
  exported_from: "Blender 5.0.1"
  export_date: "2025-12-26T12:00:00.000000"

entities:
  lamp_post:
    objects:
      - name: "pole"
        primitive: primitive_cylinder
        transform:
          location: [0.0, 0.0, 1.0]
          rotation: [0.0, 0.0, 0.0]
          scale: [1.0, 1.0, 1.0]
        material:
          color: [0.3, 0.3, 0.3, 1.0]
          roughness: 0.7
          metallic: 0.9
        params:
          radius: 0.1
          depth: 2.0
          vertices: 16

      - name: "lamp"
        primitive: primitive_ico_sphere
        transform:
          location: [0.0, 0.0, 2.2]
          rotation: [0.0, 0.0, 0.0]
          scale: [1.0, 1.0, 1.0]
        material:
          color: [1.0, 0.9, 0.7, 1.0]
          roughness: 0.3
          metallic: 0.0
          emission_color: [1.0, 0.9, 0.7, 1.0]
          emission_strength: 5.0
        params:
          radius: 0.2
          subdivisions: 2
```

## Float Precision

For lossless round-trips, the YAML serializer uses `%.17g` format for floating-point values. This preserves the exact binary representation of IEEE 754 double-precision floats.

```yaml
# High-precision values are preserved exactly
location: [1.2345678901234567, 0.0, -3.141592653589793]
```

## Notes

- All arrays use YAML flow style: `[1.0, 2.0, 3.0]`
- Boolean values: `true` / `false`
- String values may be quoted or unquoted
- Comments are supported but not preserved on round-trip
- Entity order is not guaranteed (hash table storage)
- Object order within entities is preserved (array storage)
