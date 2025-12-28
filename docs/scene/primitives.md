# Primitive Shapes Reference

This document describes all primitive shapes available in libregnum, including their properties, Blender equivalents, and usage examples.

## Shape Hierarchy

All shapes inherit from a common hierarchy:

```
LrgShape (abstract)
├── LrgShape2D (abstract)
│   ├── LrgText2D
│   ├── LrgRectangle2D
│   └── LrgCircle2D
└── LrgShape3D (abstract)
    ├── LrgPlane3D
    ├── LrgCube3D
    ├── LrgSphere3D (UV Sphere)
    ├── LrgIcoSphere3D
    ├── LrgCylinder3D
    ├── LrgCone3D
    ├── LrgCircle3D
    ├── LrgTorus3D
    └── LrgGrid3D
```

## Common Properties (LrgShape3D)

All 3D shapes inherit these properties:

| Property | Type | Description |
|----------|------|-------------|
| `position` | GrlVector3* | World position (x, y, z) |
| `rotation` | GrlVector3* | Euler rotation in radians |
| `scale` | GrlVector3* | Scale factors |
| `color` | GrlColor* | Shape color |
| `wireframe` | gboolean | Draw as wireframe |

```c
/* Common operations for all shapes */
lrg_shape3d_set_position_xyz (shape, 0.0f, 1.0f, 0.0f);
lrg_shape3d_set_rotation_xyz (shape, 0.0f, G_PI / 4, 0.0f);
lrg_shape3d_set_scale_xyz (shape, 2.0f, 2.0f, 2.0f);
lrg_shape3d_set_wireframe (shape, TRUE);
lrg_shape_set_color (LRG_SHAPE (shape), color);
```

---

## LrgPlane3D

A flat rectangular plane in 3D space.

**Blender Equivalent:** Add > Mesh > Plane

### Properties

| Property | Type | Default | Range | Description |
|----------|------|---------|-------|-------------|
| `width` | gfloat | 1.0 | > 0 | Plane width (X axis) |
| `length` | gfloat | 1.0 | > 0 | Plane length (Z axis) |

### Constructors

```c
LrgPlane3D * lrg_plane3d_new (void);

LrgPlane3D * lrg_plane3d_new_at (gfloat x, gfloat y, gfloat z,
                                 gfloat width, gfloat length);

LrgPlane3D * lrg_plane3d_new_full (gfloat x, gfloat y, gfloat z,
                                   gfloat width, gfloat length,
                                   GrlColor *color);
```

### Example

```c
g_autoptr(LrgPlane3D) floor = lrg_plane3d_new_at (0.0f, 0.0f, 0.0f, 20.0f, 20.0f);
g_autoptr(GrlColor) gray = grl_color_new (128, 128, 128, 255);
lrg_shape_set_color (LRG_SHAPE (floor), gray);
```

### YAML

```yaml
primitive: primitive_plane
params:
  size: 2.0
```

---

## LrgCube3D

A 3D cube/box shape.

**Blender Equivalent:** Add > Mesh > Cube

### Properties

| Property | Type | Default | Range | Description |
|----------|------|---------|-------|-------------|
| `width` | gfloat | 1.0 | > 0 | X dimension |
| `height` | gfloat | 1.0 | > 0 | Y dimension |
| `length` | gfloat | 1.0 | > 0 | Z dimension |

### Constructors

```c
LrgCube3D * lrg_cube3d_new (void);

LrgCube3D * lrg_cube3d_new_at (gfloat x, gfloat y, gfloat z,
                               gfloat width, gfloat height, gfloat length);
```

### Example

```c
g_autoptr(LrgCube3D) box = lrg_cube3d_new_at (0.0f, 0.5f, 0.0f, 1.0f, 1.0f, 1.0f);
```

---

## LrgSphere3D (UV Sphere)

A UV-mapped sphere with latitude/longitude segments.

**Blender Equivalent:** Add > Mesh > UV Sphere

### Properties

| Property | Type | Default | Range | Description |
|----------|------|---------|-------|-------------|
| `radius` | gfloat | 1.0 | > 0 | Sphere radius |
| `rings` | gint | 16 | >= 3 | Latitude rings |
| `slices` | gint | 16 | >= 3 | Longitude slices |

### Constructors

```c
LrgSphere3D * lrg_sphere3d_new (void);

LrgSphere3D * lrg_sphere3d_new_at (gfloat x, gfloat y, gfloat z,
                                   gfloat radius);

LrgSphere3D * lrg_sphere3d_new_full (gfloat x, gfloat y, gfloat z,
                                     gfloat radius, gint rings, gint slices,
                                     GrlColor *color);
```

### Example

```c
g_autoptr(LrgSphere3D) planet = lrg_sphere3d_new_at (0.0f, 0.0f, 0.0f, 5.0f);
lrg_sphere3d_set_rings (planet, 32);
lrg_sphere3d_set_slices (planet, 32);
```

### YAML

```yaml
primitive: primitive_uv_sphere
params:
  segments: 32
  ring_count: 16
  radius: 1.0
```

---

## LrgIcoSphere3D

A geodesic sphere created by subdividing an icosahedron. More uniform polygon distribution than UV spheres.

**Blender Equivalent:** Add > Mesh > Ico Sphere

### Properties

| Property | Type | Default | Range | Description |
|----------|------|---------|-------|-------------|
| `radius` | gfloat | 1.0 | > 0 | Sphere radius |
| `subdivisions` | gint | 2 | 1-6 | Subdivision level |

Subdivision levels:
- 1: 80 triangles (low poly)
- 2: 320 triangles (default)
- 3: 1,280 triangles (medium)
- 4: 5,120 triangles (high)
- 5: 20,480 triangles (very high)
- 6: 81,920 triangles (extreme)

### Constructors

```c
LrgIcoSphere3D * lrg_icosphere3d_new (void);

LrgIcoSphere3D * lrg_icosphere3d_new_at (gfloat x, gfloat y, gfloat z,
                                         gfloat radius);

LrgIcoSphere3D * lrg_icosphere3d_new_full (gfloat x, gfloat y, gfloat z,
                                           gfloat radius, gint subdivisions,
                                           GrlColor *color);
```

### Example

```c
g_autoptr(LrgIcoSphere3D) ball = lrg_icosphere3d_new_at (0.0f, 1.0f, 0.0f, 0.5f);
lrg_icosphere3d_set_subdivisions (ball, 3);
```

### YAML

```yaml
primitive: primitive_ico_sphere
params:
  subdivisions: 2
  radius: 1.0
```

---

## LrgCylinder3D

A cylinder with optional end caps.

**Blender Equivalent:** Add > Mesh > Cylinder

### Properties

| Property | Type | Default | Range | Description |
|----------|------|---------|-------|-------------|
| `radius` | gfloat | 0.5 | > 0 | Cylinder radius |
| `height` | gfloat | 2.0 | > 0 | Cylinder height |
| `slices` | gint | 16 | >= 3 | Number of sides |
| `cap_ends` | gboolean | TRUE | - | Draw end caps |

### Constructors

```c
LrgCylinder3D * lrg_cylinder3d_new (void);

LrgCylinder3D * lrg_cylinder3d_new_at (gfloat x, gfloat y, gfloat z,
                                       gfloat radius, gfloat height);

LrgCylinder3D * lrg_cylinder3d_new_full (gfloat x, gfloat y, gfloat z,
                                         gfloat radius, gfloat height,
                                         gint slices, GrlColor *color);
```

### Example

```c
g_autoptr(LrgCylinder3D) pillar = lrg_cylinder3d_new_at (0.0f, 1.0f, 0.0f, 0.3f, 2.0f);
lrg_cylinder3d_set_slices (pillar, 32);
lrg_cylinder3d_set_cap_ends (pillar, TRUE);
```

### YAML

```yaml
primitive: primitive_cylinder
params:
  vertices: 32
  radius: 1.0
  depth: 2.0
  cap_ends: true
```

---

## LrgCone3D

A cone or truncated cone (frustum).

**Blender Equivalent:** Add > Mesh > Cone

### Properties

| Property | Type | Default | Range | Description |
|----------|------|---------|-------|-------------|
| `radius_bottom` | gfloat | 1.0 | >= 0 | Bottom radius |
| `radius_top` | gfloat | 0.0 | >= 0 | Top radius (0 = point) |
| `height` | gfloat | 2.0 | > 0 | Cone height |
| `slices` | gint | 16 | >= 3 | Number of sides |

Setting `radius_top > 0` creates a truncated cone (frustum).

### Constructors

```c
LrgCone3D * lrg_cone3d_new (void);

LrgCone3D * lrg_cone3d_new_at (gfloat x, gfloat y, gfloat z,
                               gfloat radius, gfloat height);

LrgCone3D * lrg_cone3d_new_full (gfloat x, gfloat y, gfloat z,
                                 gfloat radius_bottom, gfloat radius_top,
                                 gfloat height, gint slices,
                                 GrlColor *color);
```

### Example

```c
/* Pointed cone */
g_autoptr(LrgCone3D) cone = lrg_cone3d_new_at (0.0f, 0.0f, 0.0f, 1.0f, 2.0f);

/* Truncated cone (frustum) */
g_autoptr(LrgCone3D) frustum = lrg_cone3d_new ();
lrg_cone3d_set_radius_bottom (frustum, 1.0f);
lrg_cone3d_set_radius_top (frustum, 0.5f);
lrg_cone3d_set_height (frustum, 1.5f);
```

### YAML

```yaml
primitive: primitive_cone
params:
  vertices: 32
  radius1: 1.0   # Bottom
  radius2: 0.0   # Top
  depth: 2.0
```

---

## LrgCircle3D

A circle or disc in 3D space.

**Blender Equivalent:** Add > Mesh > Circle

### Properties

| Property | Type | Default | Range | Description |
|----------|------|---------|-------|-------------|
| `radius` | gfloat | 1.0 | > 0 | Circle radius |
| `vertices` | gint | 32 | >= 3 | Number of vertices |
| `fill_type` | LrgCircleFillType | NGON | - | Fill mode |

Fill types:
- `LRG_CIRCLE_FILL_NOTHING`: Outline only
- `LRG_CIRCLE_FILL_NGON`: N-gon fill
- `LRG_CIRCLE_FILL_TRIFAN`: Triangle fan fill

### Constructors

```c
LrgCircle3D * lrg_circle3d_new (void);

LrgCircle3D * lrg_circle3d_new_at (gfloat x, gfloat y, gfloat z,
                                   gfloat radius);

LrgCircle3D * lrg_circle3d_new_full (gfloat x, gfloat y, gfloat z,
                                     gfloat radius, gint vertices,
                                     GrlColor *color);
```

### Example

```c
g_autoptr(LrgCircle3D) disc = lrg_circle3d_new_at (0.0f, 0.1f, 0.0f, 2.0f);
lrg_circle3d_set_vertices (disc, 64);
lrg_circle3d_set_fill_type (disc, LRG_CIRCLE_FILL_TRIFAN);
```

### YAML

```yaml
primitive: primitive_circle
params:
  vertices: 32
  radius: 1.0
  fill_type: "ngon"
```

---

## LrgTorus3D

A torus (donut/ring) shape.

**Blender Equivalent:** Add > Mesh > Torus

### Properties

| Property | Type | Default | Range | Description |
|----------|------|---------|-------|-------------|
| `major_radius` | gfloat | 1.0 | > 0 | Ring radius (center to tube center) |
| `minor_radius` | gfloat | 0.25 | > 0 | Tube radius |
| `major_segments` | gint | 48 | >= 3 | Segments around the ring |
| `minor_segments` | gint | 12 | >= 3 | Segments around the tube |

### Constructors

```c
LrgTorus3D * lrg_torus3d_new (void);

LrgTorus3D * lrg_torus3d_new_at (gfloat x, gfloat y, gfloat z,
                                 gfloat major_radius, gfloat minor_radius);

LrgTorus3D * lrg_torus3d_new_full (gfloat x, gfloat y, gfloat z,
                                   gfloat major_radius, gfloat minor_radius,
                                   gint major_segments, gint minor_segments,
                                   GrlColor *color);
```

### Example

```c
g_autoptr(LrgTorus3D) ring = lrg_torus3d_new_at (0.0f, 0.0f, 0.0f, 2.0f, 0.5f);
lrg_torus3d_set_major_segments (ring, 64);
lrg_torus3d_set_minor_segments (ring, 24);
```

### YAML

```yaml
primitive: primitive_torus
params:
  major_segments: 48
  minor_segments: 12
  major_radius: 1.0
  minor_radius: 0.25
```

---

## LrgGrid3D

A 3D grid for reference/debugging.

**Blender Equivalent:** Add > Mesh > Grid

### Properties

| Property | Type | Default | Range | Description |
|----------|------|---------|-------|-------------|
| `slices` | gint | 10 | >= 1 | Grid divisions |
| `spacing` | gfloat | 1.0 | > 0 | Space between lines |

### Constructors

```c
LrgGrid3D * lrg_grid3d_new (void);

LrgGrid3D * lrg_grid3d_new_sized (gint slices, gfloat spacing);
```

### Example

```c
g_autoptr(LrgGrid3D) grid = lrg_grid3d_new_sized (20, 1.0f);
g_autoptr(GrlColor) gray = grl_color_new (64, 64, 64, 255);
lrg_shape_set_color (LRG_SHAPE (grid), gray);
```

### YAML

```yaml
primitive: primitive_grid
params:
  x_subdivisions: 10
  y_subdivisions: 10
  size: 1.0
```

---

## Converting Scene Objects to Shapes

`LrgSceneObject` can be converted to renderable shapes:

```c
/* Get object from scene */
LrgSceneObject *obj = lrg_scene_entity_find_object (entity, "body");

/* Create appropriate shape based on primitive type */
LrgShape3D *shape = NULL;
LrgPrimitiveType prim = lrg_scene_object_get_primitive (obj);

switch (prim)
{
case LRG_PRIMITIVE_CYLINDER:
    {
        LrgCylinder3D *cyl = lrg_cylinder3d_new ();
        lrg_cylinder3d_set_radius (cyl,
            lrg_scene_object_get_param_float (obj, "radius", 1.0f));
        lrg_cylinder3d_set_height (cyl,
            lrg_scene_object_get_param_float (obj, "depth", 2.0f));
        shape = LRG_SHAPE3D (cyl);
    }
    break;

/* ... handle other primitive types ... */
}

/* Apply transform from scene object */
GrlVector3 *loc = lrg_scene_object_get_location (obj);
lrg_shape3d_set_position (shape, loc);

/* Apply material */
LrgMaterial3D *mat = lrg_scene_object_get_material (obj);
GrlColor *color = lrg_material3d_get_color_grl (mat);
lrg_shape_set_color (LRG_SHAPE (shape), color);
```

---

## 2D Primitive Shapes

The following 2D shapes are available for screen-space rendering (UI, HUD, etc.).

### Common Properties (LrgShape2D)

All 2D shapes inherit these properties:

| Property | Type | Description |
|----------|------|-------------|
| `x` | gfloat | Screen X position |
| `y` | gfloat | Screen Y position |
| `color` | GrlColor* | Shape color |
| `visible` | gboolean | Whether to render |
| `z-index` | gint | Draw order (higher = on top) |

---

## LrgRectangle2D

A 2D rectangle shape with support for filled/outline modes and rounded corners.

### Properties

| Property | Type | Default | Range | Description |
|----------|------|---------|-------|-------------|
| `width` | gfloat | 1.0 | >= 0 | Rectangle width in pixels |
| `height` | gfloat | 1.0 | >= 0 | Rectangle height in pixels |
| `filled` | gboolean | TRUE | - | Filled or outline mode |
| `line-thickness` | gfloat | 1.0 | >= 0 | Outline thickness |
| `corner-radius` | gfloat | 0.0 | >= 0 | Rounded corner radius |

### Constructors

```c
LrgRectangle2D * lrg_rectangle2d_new (void);

LrgRectangle2D * lrg_rectangle2d_new_at (gfloat x, gfloat y,
                                          gfloat width, gfloat height);

LrgRectangle2D * lrg_rectangle2d_new_full (gfloat x, gfloat y,
                                            gfloat width, gfloat height,
                                            GrlColor *color);
```

### Example

```c
/* HUD panel background */
g_autoptr(GrlColor) bg = grl_color_new (32, 32, 32, 200);
g_autoptr(LrgRectangle2D) panel = lrg_rectangle2d_new_full (
    10.0f, 10.0f, 200.0f, 150.0f, bg
);
lrg_drawable_draw (LRG_DRAWABLE (panel), delta);

/* Outlined button with rounded corners */
g_autoptr(LrgRectangle2D) button = lrg_rectangle2d_new_at (50.0f, 50.0f, 100.0f, 40.0f);
lrg_rectangle2d_set_filled (button, FALSE);
lrg_rectangle2d_set_line_thickness (button, 2.0f);
lrg_rectangle2d_set_corner_radius (button, 5.0f);
```

### YAML

```yaml
primitive: primitive_rectangle_2d
params:
  width: 100.0
  height: 50.0
  filled: true
  line_thickness: 1.0
  corner_radius: 0.0
```

---

## LrgCircle2D

A 2D circle shape with support for filled/outline modes.

### Properties

| Property | Type | Default | Range | Description |
|----------|------|---------|-------|-------------|
| `radius` | gfloat | 1.0 | >= 0 | Circle radius in pixels |
| `filled` | gboolean | TRUE | - | Filled or outline mode |

### Constructors

```c
LrgCircle2D * lrg_circle2d_new (void);

LrgCircle2D * lrg_circle2d_new_at (gfloat x, gfloat y, gfloat radius);

LrgCircle2D * lrg_circle2d_new_full (gfloat x, gfloat y,
                                      gfloat radius,
                                      GrlColor *color);
```

### Example

```c
/* Draw a ball */
g_autoptr(GrlColor) red = grl_color_new (255, 64, 64, 255);
g_autoptr(LrgCircle2D) ball = lrg_circle2d_new_full (
    ball_x, ball_y, 20.0f, red
);
lrg_drawable_draw (LRG_DRAWABLE (ball), delta);

/* Draw a ring (outline) */
g_autoptr(LrgCircle2D) ring = lrg_circle2d_new_at (center_x, center_y, 50.0f);
lrg_circle2d_set_filled (ring, FALSE);
lrg_drawable_draw (LRG_DRAWABLE (ring), delta);
```

### YAML

```yaml
primitive: primitive_circle_2d
params:
  radius: 25.0
  filled: true
```
