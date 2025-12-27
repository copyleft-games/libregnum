# Scene Module Tutorial

This tutorial walks through the complete workflow of using the scene module, from creating scenes in Blender to loading and rendering them in libregnum.

## Prerequisites

- Blender 5.x with YAML exporter addon installed
- libregnum built with scene module support
- Basic understanding of GObject and C

## Part 1: Creating a Scene in Blender

### Step 1: Set Up Your Scene

Create a simple scene using Blender's mesh primitives:

1. Open Blender and delete the default cube
2. Add a cylinder: **Add > Mesh > Cylinder**
   - Set radius to 0.3, depth to 2.0
   - Name it "pole"
3. Add an ico sphere: **Add > Mesh > Ico Sphere**
   - Position at (0, 0, 2.5)
   - Set radius to 0.3
   - Name it "lamp"
4. Select both objects and press **Ctrl+G** to group them
5. Name the group "lamp_post"

### Step 2: Assign Materials

1. Select the pole
2. Create a new material named "metal"
   - Base Color: (0.3, 0.3, 0.3)
   - Metallic: 0.9
   - Roughness: 0.7
3. Select the lamp
4. Create a new material named "glow"
   - Base Color: (1.0, 0.9, 0.7)
   - Emission Color: (1.0, 0.9, 0.7)
   - Emission Strength: 5.0

### Step 3: Export to YAML

1. **File > Export > Scene YAML (.yaml)**
2. Save as `lamp_post.yaml`

The exported file will look like:

```yaml
scene:
  name: "lamp_post"
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
          radius: 0.3
          depth: 2.0
          vertices: 32

      - name: "lamp"
        primitive: primitive_ico_sphere
        transform:
          location: [0.0, 0.0, 2.5]
          rotation: [0.0, 0.0, 0.0]
          scale: [1.0, 1.0, 1.0]
        material:
          color: [1.0, 0.9, 0.7, 1.0]
          roughness: 0.5
          metallic: 0.0
          emission_color: [1.0, 0.9, 0.7, 1.0]
          emission_strength: 5.0
        params:
          radius: 0.3
          subdivisions: 2
```

---

## Part 2: Loading the Scene in C

> **Note:** Use `LrgSceneSerializerBlender` for Blender-exported scenes.
> It automatically converts from Blender's Z-up coordinate system to
> raylib's Y-up system. For generic YAML scenes without coordinate
> conversion, use the base `LrgSceneSerializerYaml` class.

### Step 1: Basic Scene Loading

```c
#include <libregnum.h>
#include <glib.h>

int
main (int argc, char *argv[])
{
    g_autoptr(LrgSceneSerializerBlender) serializer = NULL;
    g_autoptr(LrgScene)                  scene = NULL;
    g_autoptr(GError)                    error = NULL;

    /* Create Blender YAML serializer (handles Z-up to Y-up conversion) */
    serializer = lrg_scene_serializer_blender_new ();

    /* Load the scene file */
    scene = lrg_scene_serializer_load_from_file (
        LRG_SCENE_SERIALIZER (serializer),
        "assets/lamp_post.yaml",
        &error
    );

    if (scene == NULL)
    {
        g_printerr ("Failed to load scene: %s\n", error->message);
        return 1;
    }

    g_print ("Loaded scene: %s\n", lrg_scene_get_name (scene));
    g_print ("Exported from: %s\n", lrg_scene_get_exported_from (scene));
    g_print ("Entity count: %u\n", lrg_scene_get_entity_count (scene));

    return 0;
}
```

### Step 2: Iterating Entities and Objects

```c
static void
print_entity_info (const gchar    *name,
                   LrgSceneEntity *entity,
                   gpointer        user_data)
{
    GPtrArray  *objects;
    GrlVector3 *location;
    guint       i;

    g_print ("Entity: %s\n", name);

    location = lrg_scene_entity_get_location (entity);
    g_print ("  Location: (%.2f, %.2f, %.2f)\n",
             grl_vector3_get_x (location),
             grl_vector3_get_y (location),
             grl_vector3_get_z (location));

    objects = lrg_scene_entity_get_objects (entity);
    g_print ("  Objects: %u\n", objects->len);

    for (i = 0; i < objects->len; i++)
    {
        LrgSceneObject  *obj = g_ptr_array_index (objects, i);
        LrgPrimitiveType prim = lrg_scene_object_get_primitive (obj);

        g_print ("    [%u] %s (primitive: %d)\n",
                 i,
                 lrg_scene_object_get_name (obj),
                 prim);
    }
}

/* In main(): */
lrg_scene_foreach_entity (scene, print_entity_info, NULL);
```

### Step 3: Finding Specific Objects

```c
/* Get entity by name */
LrgSceneEntity *lamp_post = lrg_scene_get_entity (scene, "lamp_post");

if (lamp_post != NULL)
{
    /* Find the lamp object */
    LrgSceneObject *lamp = lrg_scene_entity_find_object (lamp_post, "lamp");

    if (lamp != NULL)
    {
        LrgMaterial3D *material = lrg_scene_object_get_material (lamp);
        gfloat         strength = lrg_material3d_get_emission_strength (material);

        g_print ("Lamp emission strength: %.2f\n", strength);
    }
}
```

---

## Part 3: Rendering Scene Objects

### Step 1: Converting to Renderable Shapes

Scene objects store data; to render them, convert to `LrgShape3D` subclasses:

```c
static LrgShape3D *
scene_object_to_shape (LrgSceneObject *obj)
{
    LrgPrimitiveType prim;
    LrgMaterial3D   *mat;
    GrlVector3      *loc;
    GrlVector3      *rot;
    GrlVector3      *scl;
    GrlColor        *color;
    LrgShape3D      *shape = NULL;

    prim = lrg_scene_object_get_primitive (obj);
    mat  = lrg_scene_object_get_material (obj);
    loc  = lrg_scene_object_get_location (obj);
    rot  = lrg_scene_object_get_rotation (obj);
    scl  = lrg_scene_object_get_scale (obj);

    switch (prim)
    {
    case LRG_PRIMITIVE_CYLINDER:
        {
            gfloat radius = lrg_scene_object_get_param_float (obj, "radius", 1.0f);
            gfloat depth  = lrg_scene_object_get_param_float (obj, "depth", 2.0f);
            gint   slices = lrg_scene_object_get_param_int (obj, "vertices", 32);

            shape = LRG_SHAPE3D (lrg_cylinder3d_new_full (
                grl_vector3_get_x (loc),
                grl_vector3_get_y (loc),
                grl_vector3_get_z (loc),
                radius, depth, slices,
                lrg_material3d_get_color_grl (mat)
            ));
        }
        break;

    case LRG_PRIMITIVE_ICO_SPHERE:
        {
            gfloat radius = lrg_scene_object_get_param_float (obj, "radius", 1.0f);
            gint   subdiv = lrg_scene_object_get_param_int (obj, "subdivisions", 2);

            shape = LRG_SHAPE3D (lrg_icosphere3d_new_full (
                grl_vector3_get_x (loc),
                grl_vector3_get_y (loc),
                grl_vector3_get_z (loc),
                radius, subdiv,
                lrg_material3d_get_color_grl (mat)
            ));
        }
        break;

    case LRG_PRIMITIVE_UV_SPHERE:
        {
            gfloat radius = lrg_scene_object_get_param_float (obj, "radius", 1.0f);
            gint   rings  = lrg_scene_object_get_param_int (obj, "ring_count", 16);
            gint   slices = lrg_scene_object_get_param_int (obj, "segments", 32);

            shape = LRG_SHAPE3D (lrg_sphere3d_new_full (
                grl_vector3_get_x (loc),
                grl_vector3_get_y (loc),
                grl_vector3_get_z (loc),
                radius, rings, slices,
                lrg_material3d_get_color_grl (mat)
            ));
        }
        break;

    case LRG_PRIMITIVE_CUBE:
        {
            gfloat size = lrg_scene_object_get_param_float (obj, "size", 2.0f);

            shape = LRG_SHAPE3D (lrg_cube3d_new_at (
                grl_vector3_get_x (loc),
                grl_vector3_get_y (loc),
                grl_vector3_get_z (loc),
                size, size, size
            ));
        }
        break;

    case LRG_PRIMITIVE_CONE:
        {
            gfloat radius1 = lrg_scene_object_get_param_float (obj, "radius1", 1.0f);
            gfloat radius2 = lrg_scene_object_get_param_float (obj, "radius2", 0.0f);
            gfloat depth   = lrg_scene_object_get_param_float (obj, "depth", 2.0f);
            gint   slices  = lrg_scene_object_get_param_int (obj, "vertices", 32);

            shape = LRG_SHAPE3D (lrg_cone3d_new_full (
                grl_vector3_get_x (loc),
                grl_vector3_get_y (loc),
                grl_vector3_get_z (loc),
                radius1, radius2, depth, slices,
                lrg_material3d_get_color_grl (mat)
            ));
        }
        break;

    case LRG_PRIMITIVE_PLANE:
        {
            gfloat size = lrg_scene_object_get_param_float (obj, "size", 2.0f);

            shape = LRG_SHAPE3D (lrg_plane3d_new_at (
                grl_vector3_get_x (loc),
                grl_vector3_get_y (loc),
                grl_vector3_get_z (loc),
                size, size
            ));
        }
        break;

    case LRG_PRIMITIVE_TORUS:
        {
            gfloat major_r = lrg_scene_object_get_param_float (obj, "major_radius", 1.0f);
            gfloat minor_r = lrg_scene_object_get_param_float (obj, "minor_radius", 0.25f);
            gint   major_s = lrg_scene_object_get_param_int (obj, "major_segments", 48);
            gint   minor_s = lrg_scene_object_get_param_int (obj, "minor_segments", 12);

            shape = LRG_SHAPE3D (lrg_torus3d_new_full (
                grl_vector3_get_x (loc),
                grl_vector3_get_y (loc),
                grl_vector3_get_z (loc),
                major_r, minor_r, major_s, minor_s,
                lrg_material3d_get_color_grl (mat)
            ));
        }
        break;

    case LRG_PRIMITIVE_CIRCLE:
        {
            gfloat radius   = lrg_scene_object_get_param_float (obj, "radius", 1.0f);
            gint   vertices = lrg_scene_object_get_param_int (obj, "vertices", 32);

            shape = LRG_SHAPE3D (lrg_circle3d_new_full (
                grl_vector3_get_x (loc),
                grl_vector3_get_y (loc),
                grl_vector3_get_z (loc),
                radius, vertices,
                lrg_material3d_get_color_grl (mat)
            ));
        }
        break;

    case LRG_PRIMITIVE_GRID:
        {
            gint   slices  = lrg_scene_object_get_param_int (obj, "x_subdivisions", 10);
            gfloat spacing = lrg_scene_object_get_param_float (obj, "size", 1.0f);

            shape = LRG_SHAPE3D (lrg_grid3d_new_sized (slices, spacing));
            lrg_shape3d_set_position (shape, loc);
        }
        break;

    default:
        g_warning ("Unknown primitive type: %d", prim);
        return NULL;
    }

    /* Apply rotation and scale */
    if (shape != NULL)
    {
        lrg_shape3d_set_rotation (shape, rot);
        lrg_shape3d_set_scale (shape, scl);

        /* Apply color from material */
        color = lrg_material3d_get_color_grl (mat);
        lrg_shape_set_color (LRG_SHAPE (shape), color);
    }

    return shape;
}
```

### Step 2: Full Render Loop

```c
#include <libregnum.h>
#include <graylib.h>

typedef struct {
    GPtrArray *shapes;  /* LrgShape3D* */
} GameState;

static void
load_scene_shapes (GameState *state, LrgScene *scene)
{
    GList *entity_names;
    GList *iter;

    state->shapes = g_ptr_array_new_with_free_func (g_object_unref);
    entity_names = lrg_scene_get_entity_names (scene);

    for (iter = entity_names; iter != NULL; iter = iter->next)
    {
        const gchar    *name   = iter->data;
        LrgSceneEntity *entity = lrg_scene_get_entity (scene, name);
        GPtrArray      *objects = lrg_scene_entity_get_objects (entity);
        guint           i;

        for (i = 0; i < objects->len; i++)
        {
            LrgSceneObject *obj = g_ptr_array_index (objects, i);
            LrgShape3D     *shape = scene_object_to_shape (obj);

            if (shape != NULL)
            {
                g_ptr_array_add (state->shapes, shape);
            }
        }
    }

    g_list_free (entity_names);
}

static void
render_shapes (GameState *state)
{
    guint i;

    for (i = 0; i < state->shapes->len; i++)
    {
        LrgShape3D *shape = g_ptr_array_index (state->shapes, i);
        lrg_shape_draw (LRG_SHAPE (shape));
    }
}

int
main (int argc, char *argv[])
{
    g_autoptr(LrgSceneSerializerBlender) serializer = NULL;
    g_autoptr(LrgScene)                  scene = NULL;
    g_autoptr(GError)                    error = NULL;
    g_autoptr(GrlWindow)                 window = NULL;
    g_autoptr(GrlCamera3D)               camera = NULL;
    GameState                            state = { 0 };

    /* Initialize graylib */
    grl_init ();

    /* Load scene (Blender serializer handles coordinate conversion) */
    serializer = lrg_scene_serializer_blender_new ();
    scene = lrg_scene_serializer_load_from_file (
        LRG_SCENE_SERIALIZER (serializer),
        "assets/lamp_post.yaml",
        &error
    );

    if (scene == NULL)
    {
        g_printerr ("Failed to load scene: %s\n", error->message);
        return 1;
    }

    /* Convert scene objects to shapes */
    load_scene_shapes (&state, scene);
    g_print ("Loaded %u shapes\n", state.shapes->len);

    /* Create window and camera */
    window = grl_window_new (800, 600, "Scene Viewer");
    camera = grl_camera3d_new ();
    grl_camera3d_set_position_xyz (camera, 5.0f, 5.0f, 5.0f);
    grl_camera3d_set_target_xyz (camera, 0.0f, 1.0f, 0.0f);

    /* Main loop */
    while (!grl_window_should_close (window))
    {
        grl_begin_drawing ();
        grl_clear_background_color (grl_color_new (30, 30, 40, 255));

        grl_begin_mode_3d (camera);
        render_shapes (&state);
        grl_end_mode_3d ();

        grl_end_drawing ();
    }

    /* Cleanup */
    g_ptr_array_unref (state.shapes);
    grl_close_window (window);

    return 0;
}
```

---

## Part 4: Modifying and Saving Scenes

### Step 1: Modifying Scene Data

```c
/* Get the lamp object and modify its emission */
LrgSceneEntity *lamp_post = lrg_scene_get_entity (scene, "lamp_post");
LrgSceneObject *lamp = lrg_scene_entity_find_object (lamp_post, "lamp");
LrgMaterial3D  *material = lrg_scene_object_get_material (lamp);

/* Increase emission strength */
lrg_material3d_set_emission_strength (material, 10.0f);

/* Move the lamp higher */
lrg_scene_object_set_location_xyz (lamp, 0.0f, 0.0f, 3.0f);

/* Change the lamp color to blue */
lrg_material3d_set_color (material, 0.5f, 0.7f, 1.0f, 1.0f);
lrg_material3d_set_emission_color (material, 0.5f, 0.7f, 1.0f, 1.0f);
```

### Step 2: Adding New Objects

```c
/* Create a new sphere object */
g_autoptr(LrgSceneObject) orb = lrg_scene_object_new ("orb", LRG_PRIMITIVE_UV_SPHERE);

/* Set transform */
lrg_scene_object_set_location_xyz (orb, 2.0f, 0.0f, 1.5f);
lrg_scene_object_set_scale_xyz (orb, 1.0f, 1.0f, 1.0f);

/* Set parameters */
lrg_scene_object_set_param_float (orb, "radius", 0.5f);
lrg_scene_object_set_param_int (orb, "segments", 24);
lrg_scene_object_set_param_int (orb, "ring_count", 12);

/* Create and set material */
g_autoptr(LrgMaterial3D) orb_mat = lrg_material3d_new ();
lrg_material3d_set_color (orb_mat, 0.8f, 0.2f, 0.2f, 1.0f);
lrg_material3d_set_roughness (orb_mat, 0.3f);
lrg_scene_object_set_material (orb, orb_mat);

/* Add to entity */
lrg_scene_entity_add_object (lamp_post, orb);
```

### Step 3: Creating New Entities

```c
/* Create a new entity */
g_autoptr(LrgSceneEntity) platform = lrg_scene_entity_new ("platform");
lrg_scene_entity_set_location_xyz (platform, 0.0f, -0.5f, 0.0f);

/* Add a plane object */
g_autoptr(LrgSceneObject) floor_obj = lrg_scene_object_new ("floor", LRG_PRIMITIVE_PLANE);
lrg_scene_object_set_param_float (floor_obj, "size", 10.0f);

g_autoptr(LrgMaterial3D) floor_mat = lrg_material3d_new ();
lrg_material3d_set_color (floor_mat, 0.4f, 0.4f, 0.4f, 1.0f);
lrg_scene_object_set_material (floor_obj, floor_mat);

lrg_scene_entity_add_object (platform, floor_obj);

/* Add entity to scene */
lrg_scene_add_entity (scene, platform);
```

### Step 4: Saving the Modified Scene

```c
g_autoptr(GError) save_error = NULL;
gboolean success;

/* Save to file */
success = lrg_scene_serializer_save_to_file (
    LRG_SCENE_SERIALIZER (serializer),
    scene,
    "assets/modified_scene.yaml",
    &save_error
);

if (!success)
{
    g_printerr ("Failed to save scene: %s\n", save_error->message);
}

/* Or save to string */
gsize   length;
gchar  *yaml_data = lrg_scene_serializer_save_to_data (
    LRG_SCENE_SERIALIZER (serializer),
    scene,
    &length
);

g_print ("YAML output (%zu bytes):\n%s\n", length, yaml_data);
g_free (yaml_data);
```

---

## Part 5: Complete Example Application

Here's a complete example that loads a scene, displays it, and allows saving modifications:

```c
/* scene_viewer.c */
#include <libregnum.h>
#include <graylib.h>

typedef struct {
    LrgScene                   *scene;
    LrgSceneSerializerBlender  *serializer;
    GPtrArray                  *shapes;
    GrlCamera3D                *camera;
    gboolean                    modified;
} SceneViewer;

static LrgShape3D *
convert_object (LrgSceneObject *obj)
{
    /* ... (use scene_object_to_shape from above) ... */
}

static void
viewer_load (SceneViewer *viewer, const gchar *path)
{
    g_autoptr(GError) error = NULL;
    GList *names, *iter;

    viewer->scene = lrg_scene_serializer_load_from_file (
        LRG_SCENE_SERIALIZER (viewer->serializer), path, &error);

    if (viewer->scene == NULL)
    {
        g_error ("Load failed: %s", error->message);
    }

    viewer->shapes = g_ptr_array_new_with_free_func (g_object_unref);
    names = lrg_scene_get_entity_names (viewer->scene);

    for (iter = names; iter; iter = iter->next)
    {
        LrgSceneEntity *entity = lrg_scene_get_entity (viewer->scene, iter->data);
        GPtrArray *objects = lrg_scene_entity_get_objects (entity);

        for (guint i = 0; i < objects->len; i++)
        {
            LrgShape3D *shape = convert_object (g_ptr_array_index (objects, i));
            if (shape)
                g_ptr_array_add (viewer->shapes, shape);
        }
    }

    g_list_free (names);
}

static void
viewer_save (SceneViewer *viewer, const gchar *path)
{
    g_autoptr(GError) error = NULL;

    if (!lrg_scene_serializer_save_to_file (
            LRG_SCENE_SERIALIZER (viewer->serializer),
            viewer->scene, path, &error))
    {
        g_warning ("Save failed: %s", error->message);
    }
    else
    {
        viewer->modified = FALSE;
        g_print ("Saved to %s\n", path);
    }
}

static void
viewer_render (SceneViewer *viewer)
{
    for (guint i = 0; i < viewer->shapes->len; i++)
    {
        lrg_shape_draw (LRG_SHAPE (g_ptr_array_index (viewer->shapes, i)));
    }
}

int
main (int argc, char *argv[])
{
    SceneViewer viewer = { 0 };
    const gchar *scene_path;

    if (argc < 2)
    {
        g_printerr ("Usage: %s <scene.yaml>\n", argv[0]);
        return 1;
    }

    scene_path = argv[1];

    grl_init ();
    viewer.serializer = lrg_scene_serializer_blender_new ();
    viewer_load (&viewer, scene_path);

    g_autoptr(GrlWindow) window = grl_window_new (1024, 768, "Scene Viewer");
    viewer.camera = grl_camera3d_new ();
    grl_camera3d_set_position_xyz (viewer.camera, 8.0f, 6.0f, 8.0f);
    grl_camera3d_set_target_xyz (viewer.camera, 0.0f, 0.0f, 0.0f);

    while (!grl_window_should_close (window))
    {
        /* Handle input */
        if (grl_is_key_pressed (GRL_KEY_S) && grl_is_key_down (GRL_KEY_LEFT_CONTROL))
        {
            viewer_save (&viewer, scene_path);
        }

        /* Render */
        grl_begin_drawing ();
        grl_clear_background_color (grl_color_new (40, 44, 52, 255));

        grl_begin_mode_3d (viewer.camera);
        viewer_render (&viewer);
        grl_end_mode_3d ();

        grl_draw_text ("Press Ctrl+S to save", 10, 10, 20, grl_color_white ());
        grl_end_drawing ();
    }

    g_ptr_array_unref (viewer.shapes);
    g_object_unref (viewer.scene);
    g_object_unref (viewer.serializer);
    g_object_unref (viewer.camera);
    grl_close_window (window);

    return 0;
}
```

---

## Troubleshooting

### Scene fails to load

```
Failed to load scene: Parse error at line X
```

Check your YAML syntax. Common issues:
- Missing colons after keys
- Incorrect indentation (use 2 spaces)
- Unquoted strings with special characters

### Objects not appearing

1. Check object locations - they may be outside camera view
2. Verify primitive type is supported
3. Check material alpha is > 0

### Round-trip precision loss

The YAML serializer uses `%.17g` format for maximum precision. If you still see precision issues, verify you're using the latest libregnum version.

---

## Next Steps

- Explore the [API Reference](api-reference.md) for complete API details
- See [Primitives](primitives.md) for all supported shapes
- Check [YAML Format](yaml-format.md) for format specification
