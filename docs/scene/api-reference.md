# Scene Module API Reference

Complete API documentation for the libregnum scene module.

## LrgScene

Root container for scene data.

### Type Information

```c
#define LRG_TYPE_SCENE (lrg_scene_get_type ())
G_DECLARE_FINAL_TYPE (LrgScene, lrg_scene, LRG, SCENE, GObject)
```

### Constructors

#### lrg_scene_new

```c
LrgScene * lrg_scene_new (const gchar *name);
```

Creates a new empty scene.

**Parameters:**
- `name`: Scene name (may be NULL)

**Returns:** (transfer full): A new `LrgScene`

### Properties

#### name

```c
const gchar * lrg_scene_get_name (LrgScene *self);
void lrg_scene_set_name (LrgScene *self, const gchar *name);
```

Scene name/identifier.

#### exported_from

```c
const gchar * lrg_scene_get_exported_from (LrgScene *self);
void lrg_scene_set_exported_from (LrgScene *self, const gchar *exported_from);
```

Source application string (e.g., "Blender 5.0.1").

#### export_date

```c
GDateTime * lrg_scene_get_export_date (LrgScene *self);
void lrg_scene_set_export_date (LrgScene *self, GDateTime *date);
```

Export timestamp.

### Entity Management

#### lrg_scene_add_entity

```c
void lrg_scene_add_entity (LrgScene *self, LrgSceneEntity *entity);
```

Adds an entity to the scene. The entity's name is used as the key.

**Parameters:**
- `self`: A `LrgScene`
- `entity`: (transfer none): The entity to add

#### lrg_scene_get_entity

```c
LrgSceneEntity * lrg_scene_get_entity (LrgScene *self, const gchar *name);
```

Retrieves an entity by name.

**Parameters:**
- `self`: A `LrgScene`
- `name`: Entity name

**Returns:** (transfer none) (nullable): The entity, or NULL if not found

#### lrg_scene_get_entity_names

```c
GList * lrg_scene_get_entity_names (LrgScene *self);
```

Gets a list of all entity names.

**Returns:** (transfer container) (element-type utf8): List of entity names. Free with `g_list_free()`.

#### lrg_scene_get_entity_count

```c
guint lrg_scene_get_entity_count (LrgScene *self);
```

Gets the number of entities in the scene.

**Returns:** Entity count

#### lrg_scene_foreach_entity

```c
typedef void (*LrgSceneEntityFunc) (const gchar *name,
                                    LrgSceneEntity *entity,
                                    gpointer user_data);

void lrg_scene_foreach_entity (LrgScene *self,
                               LrgSceneEntityFunc func,
                               gpointer user_data);
```

Iterates over all entities in the scene.

---

## LrgSceneEntity

Groups related scene objects.

### Type Information

```c
#define LRG_TYPE_SCENE_ENTITY (lrg_scene_entity_get_type ())
G_DECLARE_FINAL_TYPE (LrgSceneEntity, lrg_scene_entity, LRG, SCENE_ENTITY, GObject)
```

### Constructors

#### lrg_scene_entity_new

```c
LrgSceneEntity * lrg_scene_entity_new (const gchar *name);
```

Creates a new entity.

**Parameters:**
- `name`: Entity name

**Returns:** (transfer full): A new `LrgSceneEntity`

### Properties

#### name

```c
const gchar * lrg_scene_entity_get_name (LrgSceneEntity *self);
void lrg_scene_entity_set_name (LrgSceneEntity *self, const gchar *name);
```

Entity name/identifier.

### Transform

#### lrg_scene_entity_get_location / set_location

```c
GrlVector3 * lrg_scene_entity_get_location (LrgSceneEntity *self);
void lrg_scene_entity_set_location (LrgSceneEntity *self, GrlVector3 *location);
void lrg_scene_entity_set_location_xyz (LrgSceneEntity *self,
                                        gfloat x, gfloat y, gfloat z);
```

Entity world position.

#### lrg_scene_entity_get_rotation / set_rotation

```c
GrlVector3 * lrg_scene_entity_get_rotation (LrgSceneEntity *self);
void lrg_scene_entity_set_rotation (LrgSceneEntity *self, GrlVector3 *rotation);
void lrg_scene_entity_set_rotation_xyz (LrgSceneEntity *self,
                                        gfloat rx, gfloat ry, gfloat rz);
```

Entity rotation (Euler angles in radians).

#### lrg_scene_entity_get_scale / set_scale

```c
GrlVector3 * lrg_scene_entity_get_scale (LrgSceneEntity *self);
void lrg_scene_entity_set_scale (LrgSceneEntity *self, GrlVector3 *scale);
void lrg_scene_entity_set_scale_xyz (LrgSceneEntity *self,
                                     gfloat sx, gfloat sy, gfloat sz);
```

Entity scale factors.

### Object Management

#### lrg_scene_entity_add_object

```c
void lrg_scene_entity_add_object (LrgSceneEntity *self, LrgSceneObject *object);
```

Adds an object to the entity.

**Parameters:**
- `self`: A `LrgSceneEntity`
- `object`: (transfer none): The object to add

#### lrg_scene_entity_get_objects

```c
GPtrArray * lrg_scene_entity_get_objects (LrgSceneEntity *self);
```

Gets the array of objects.

**Returns:** (transfer none) (element-type LrgSceneObject): Object array

#### lrg_scene_entity_find_object

```c
LrgSceneObject * lrg_scene_entity_find_object (LrgSceneEntity *self,
                                               const gchar *name);
```

Finds an object by name.

**Returns:** (transfer none) (nullable): The object, or NULL if not found

#### lrg_scene_entity_remove_object

```c
gboolean lrg_scene_entity_remove_object (LrgSceneEntity *self,
                                         LrgSceneObject *object);
```

Removes an object from the entity.

**Returns:** TRUE if the object was found and removed

---

## LrgSceneObject

Individual primitive with transform, material, and parameters.

### Type Information

```c
#define LRG_TYPE_SCENE_OBJECT (lrg_scene_object_get_type ())
G_DECLARE_FINAL_TYPE (LrgSceneObject, lrg_scene_object, LRG, SCENE_OBJECT, GObject)
```

### Constructors

#### lrg_scene_object_new

```c
LrgSceneObject * lrg_scene_object_new (const gchar *name,
                                       LrgPrimitiveType primitive);
```

Creates a new scene object.

**Parameters:**
- `name`: Object name
- `primitive`: Primitive type

**Returns:** (transfer full): A new `LrgSceneObject`

### Properties

#### name

```c
const gchar * lrg_scene_object_get_name (LrgSceneObject *self);
void lrg_scene_object_set_name (LrgSceneObject *self, const gchar *name);
```

Object name/identifier.

#### primitive

```c
LrgPrimitiveType lrg_scene_object_get_primitive (LrgSceneObject *self);
void lrg_scene_object_set_primitive (LrgSceneObject *self,
                                     LrgPrimitiveType primitive);
```

Primitive type enum.

### Transform

Same pattern as `LrgSceneEntity`:

```c
GrlVector3 * lrg_scene_object_get_location (LrgSceneObject *self);
void lrg_scene_object_set_location_xyz (LrgSceneObject *self,
                                        gfloat x, gfloat y, gfloat z);

GrlVector3 * lrg_scene_object_get_rotation (LrgSceneObject *self);
void lrg_scene_object_set_rotation_xyz (LrgSceneObject *self,
                                        gfloat rx, gfloat ry, gfloat rz);

GrlVector3 * lrg_scene_object_get_scale (LrgSceneObject *self);
void lrg_scene_object_set_scale_xyz (LrgSceneObject *self,
                                     gfloat sx, gfloat sy, gfloat sz);
```

### Material

#### lrg_scene_object_get_material / set_material

```c
LrgMaterial3D * lrg_scene_object_get_material (LrgSceneObject *self);
void lrg_scene_object_set_material (LrgSceneObject *self, LrgMaterial3D *material);
```

Object's PBR material.

### Parameters

Parameters store primitive-specific values (radius, vertices, etc.).

#### Float Parameters

```c
void lrg_scene_object_set_param_float (LrgSceneObject *self,
                                       const gchar *name,
                                       gfloat value);

gfloat lrg_scene_object_get_param_float (LrgSceneObject *self,
                                         const gchar *name,
                                         gfloat default_value);
```

#### Integer Parameters

```c
void lrg_scene_object_set_param_int (LrgSceneObject *self,
                                     const gchar *name,
                                     gint value);

gint lrg_scene_object_get_param_int (LrgSceneObject *self,
                                     const gchar *name,
                                     gint default_value);
```

#### Boolean Parameters

```c
void lrg_scene_object_set_param_bool (LrgSceneObject *self,
                                      const gchar *name,
                                      gboolean value);

gboolean lrg_scene_object_get_param_bool (LrgSceneObject *self,
                                          const gchar *name,
                                          gboolean default_value);
```

---

## LrgMaterial3D

PBR material properties.

### Type Information

```c
#define LRG_TYPE_MATERIAL3D (lrg_material3d_get_type ())
G_DECLARE_FINAL_TYPE (LrgMaterial3D, lrg_material3d, LRG, MATERIAL3D, GObject)
```

### Constructors

#### lrg_material3d_new

```c
LrgMaterial3D * lrg_material3d_new (void);
```

Creates a new material with default values.

**Returns:** (transfer full): A new `LrgMaterial3D`

### Color

```c
void lrg_material3d_set_color (LrgMaterial3D *self,
                               gfloat r, gfloat g, gfloat b, gfloat a);

void lrg_material3d_get_color (LrgMaterial3D *self,
                               gfloat *r, gfloat *g, gfloat *b, gfloat *a);

GrlColor * lrg_material3d_get_color_grl (LrgMaterial3D *self);
```

Base color (RGBA, 0.0-1.0).

### Roughness

```c
void lrg_material3d_set_roughness (LrgMaterial3D *self, gfloat roughness);
gfloat lrg_material3d_get_roughness (LrgMaterial3D *self);
```

Surface roughness (0.0 = smooth/mirror, 1.0 = rough/diffuse).

### Metallic

```c
void lrg_material3d_set_metallic (LrgMaterial3D *self, gfloat metallic);
gfloat lrg_material3d_get_metallic (LrgMaterial3D *self);
```

Metallic factor (0.0 = dielectric, 1.0 = pure metal).

### Emission

```c
void lrg_material3d_set_emission_color (LrgMaterial3D *self,
                                        gfloat r, gfloat g, gfloat b, gfloat a);

void lrg_material3d_get_emission_color (LrgMaterial3D *self,
                                        gfloat *r, gfloat *g, gfloat *b, gfloat *a);

void lrg_material3d_set_emission_strength (LrgMaterial3D *self, gfloat strength);
gfloat lrg_material3d_get_emission_strength (LrgMaterial3D *self);
```

Emission/glow properties.

---

## LrgSceneSerializer

Interface for scene serialization.

### Type Information

```c
#define LRG_TYPE_SCENE_SERIALIZER (lrg_scene_serializer_get_type ())
G_DECLARE_INTERFACE (LrgSceneSerializer, lrg_scene_serializer,
                     LRG, SCENE_SERIALIZER, GObject)
```

### Interface Methods

#### lrg_scene_serializer_load_from_file

```c
LrgScene * lrg_scene_serializer_load_from_file (LrgSceneSerializer *self,
                                                const gchar *path,
                                                GError **error);
```

Loads a scene from a file.

**Parameters:**
- `self`: A `LrgSceneSerializer`
- `path`: File path
- `error`: (nullable): Return location for errors

**Returns:** (transfer full) (nullable): The loaded scene, or NULL on error

#### lrg_scene_serializer_load_from_data

```c
LrgScene * lrg_scene_serializer_load_from_data (LrgSceneSerializer *self,
                                                const gchar *data,
                                                gssize length,
                                                GError **error);
```

Loads a scene from string data.

**Parameters:**
- `self`: A `LrgSceneSerializer`
- `data`: String data
- `length`: Data length, or -1 for NUL-terminated
- `error`: (nullable): Return location for errors

**Returns:** (transfer full) (nullable): The loaded scene, or NULL on error

#### lrg_scene_serializer_save_to_file

```c
gboolean lrg_scene_serializer_save_to_file (LrgSceneSerializer *self,
                                            LrgScene *scene,
                                            const gchar *path,
                                            GError **error);
```

Saves a scene to a file.

**Returns:** TRUE on success

#### lrg_scene_serializer_save_to_data

```c
gchar * lrg_scene_serializer_save_to_data (LrgSceneSerializer *self,
                                           LrgScene *scene,
                                           gsize *length);
```

Serializes a scene to a string.

**Parameters:**
- `self`: A `LrgSceneSerializer`
- `scene`: The scene to serialize
- `length`: (out) (nullable): Return location for string length

**Returns:** (transfer full): Serialized data string

---

## LrgSceneSerializerYaml

YAML format serializer implementation.

### Type Information

```c
#define LRG_TYPE_SCENE_SERIALIZER_YAML (lrg_scene_serializer_yaml_get_type ())
G_DECLARE_FINAL_TYPE (LrgSceneSerializerYaml, lrg_scene_serializer_yaml,
                      LRG, SCENE_SERIALIZER_YAML, GObject)
```

### Constructors

#### lrg_scene_serializer_yaml_new

```c
LrgSceneSerializerYaml * lrg_scene_serializer_yaml_new (void);
```

Creates a new YAML serializer.

**Returns:** (transfer full): A new `LrgSceneSerializerYaml`

---

## Enumerations

### LrgPrimitiveType

```c
typedef enum {
    LRG_PRIMITIVE_PLANE,
    LRG_PRIMITIVE_CUBE,
    LRG_PRIMITIVE_CIRCLE,
    LRG_PRIMITIVE_UV_SPHERE,
    LRG_PRIMITIVE_ICO_SPHERE,
    LRG_PRIMITIVE_CYLINDER,
    LRG_PRIMITIVE_CONE,
    LRG_PRIMITIVE_TORUS,
    LRG_PRIMITIVE_GRID
} LrgPrimitiveType;
```

### LrgCircleFillType

```c
typedef enum {
    LRG_CIRCLE_FILL_NOTHING,
    LRG_CIRCLE_FILL_NGON,
    LRG_CIRCLE_FILL_TRIFAN
} LrgCircleFillType;
```

---

## Error Domains

### LRG_SCENE_ERROR

```c
#define LRG_SCENE_ERROR (lrg_scene_error_quark ())

typedef enum {
    LRG_SCENE_ERROR_FAILED,
    LRG_SCENE_ERROR_PARSE,
    LRG_SCENE_ERROR_INVALID_FORMAT
} LrgSceneError;
```
