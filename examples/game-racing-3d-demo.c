/* game-racing-3d-demo.c
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D Racing demo using LrgRacing3DTemplate.
 * Port of game-taco-racing.c to the template system.
 *
 * Controls:
 *   W/Up     - Accelerate
 *   S/Down   - Brake / Reverse
 *   A/D      - Steer left / right
 *   Space    - Handbrake / Jump
 *   Shift    - Boost
 *   C        - Cycle camera mode
 *   R        - Reset position
 *   Enter    - Start race
 *   Escape   - Pause / Exit
 *
 * Features demonstrated:
 *   - LrgRacing3DTemplate usage
 *   - Procedural track generation
 *   - Arcade vehicle physics
 *   - Chase camera system
 *   - Lap and checkpoint tracking
 *   - Boost/nitro system
 */

#include <libregnum.h>
#include <graylib.h>
#include <rlgl.h>
#include <locale.h>
#include <math.h>

/* ========================================================================== */
/* Type Declarations                                                          */
/* ========================================================================== */

#define RACING_TYPE_DEMO (racing_demo_get_type ())
G_DECLARE_FINAL_TYPE (RacingDemo, racing_demo, RACING, DEMO, LrgRacing3DTemplate)

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

#define SCREEN_WIDTH      1280
#define SCREEN_HEIGHT     720

#define TRACK_WIDTH       60.0f
#define TRACK_HEIGHT      80.0f
#define TRACK_LANE_WIDTH  12.0f
#define NUM_CHECKPOINTS   4

#define VEHICLE_ACCEL     35.0f
#define VEHICLE_MAX_SPEED 25.0f
#define VEHICLE_BRAKE     50.0f
#define VEHICLE_STEER     120.0f
#define VEHICLE_GRIP      0.9f

#define BOOST_SPEED_MULT  1.6f
#define BOOST_DRAIN       0.5f
#define BOOST_RECHARGE    0.15f

#define JUMP_VELOCITY     8.0f
#define GRAVITY           20.0f

/* ========================================================================== */
/* Data Structures                                                            */
/* ========================================================================== */

typedef struct
{
    gfloat x;
    gfloat z;
    gfloat radius;
} Checkpoint;

typedef struct
{
    GrlModel   *model;
    GrlVector3 *position;
    GrlVector3 *rotation;
    GrlVector3 *scale;
    GrlColor   *color;
} MeshModelEntry;

/* ========================================================================== */
/* Private Structure                                                          */
/* ========================================================================== */

struct _RacingDemo
{
    LrgRacing3DTemplate parent_instance;

    /* Track */
    GPtrArray   *track_markers;
    GPtrArray   *checkpoint_gates;
    Checkpoint   checkpoints[NUM_CHECKPOINTS];
    LrgPlane3D  *ground;

    /* Vehicle model */
    LrgScene    *taco_scene;
    GPtrArray   *mesh_models;

    /* Extended physics */
    gfloat       vertical_velocity;
    gfloat       jump_height;
    gboolean     is_jumping;
    gfloat       boost_fuel;

    /* Camera */
    LrgCameraThirdPerson *chase_camera;

    /* State */
    gboolean     paused;
};

G_DEFINE_FINAL_TYPE (RacingDemo, racing_demo, LRG_TYPE_RACING_3D_TEMPLATE)

/* ========================================================================== */
/* Forward Declarations                                                       */
/* ========================================================================== */

static void create_track           (RacingDemo *self);
static void create_checkpoints     (RacingDemo *self);
static void load_vehicle_model     (RacingDemo *self);
static void reset_vehicle          (RacingDemo *self);
static RacingDemo *racing_demo_new (void);

/* ========================================================================== */
/* Mesh Model Helpers                                                         */
/* ========================================================================== */

static MeshModelEntry *
mesh_model_entry_new (GrlModel   *model,
                      GrlVector3 *position,
                      GrlVector3 *rotation,
                      GrlVector3 *scale,
                      GrlColor   *color)
{
    MeshModelEntry *entry;

    entry = g_new0 (MeshModelEntry, 1);
    entry->model = g_object_ref (model);
    entry->position = grl_vector3_copy (position);
    entry->rotation = grl_vector3_copy (rotation);
    entry->scale = grl_vector3_copy (scale);
    entry->color = grl_color_copy (color);

    return entry;
}

static void
mesh_model_entry_free (MeshModelEntry *entry)
{
    if (entry == NULL)
        return;

    g_clear_object (&entry->model);
    g_clear_pointer (&entry->position, grl_vector3_free);
    g_clear_pointer (&entry->rotation, grl_vector3_free);
    g_clear_pointer (&entry->scale, grl_vector3_free);
    g_clear_pointer (&entry->color, grl_color_free);
    g_free (entry);
}

/* ========================================================================== */
/* Mesh Triangulation                                                         */
/* ========================================================================== */

static guint16 *
triangulate_faces (const gint *faces,
                   guint       n_faces,
                   guint       total_indices,
                   gboolean    reverse_winding,
                   guint      *out_n_triangles)
{
    guint    i, pos, idx;
    guint    n_triangles;
    guint16 *indices;

    n_triangles = 0;
    pos = 0;
    for (i = 0; i < n_faces && pos < total_indices; i++)
    {
        gint n_verts = faces[pos];
        if (n_verts >= 3)
            n_triangles += (n_verts - 2);
        pos += n_verts + 1;
    }

    if (n_triangles == 0)
    {
        *out_n_triangles = 0;
        return NULL;
    }

    indices = g_new (guint16, n_triangles * 3);

    pos = 0;
    idx = 0;
    for (i = 0; i < n_faces && pos < total_indices; i++)
    {
        gint n_verts = faces[pos];
        gint v0, j;

        pos++;

        if (n_verts < 3)
        {
            pos += n_verts;
            continue;
        }

        v0 = faces[pos];

        for (j = 1; j < n_verts - 1; j++)
        {
            indices[idx++] = (guint16)v0;
            if (reverse_winding)
            {
                indices[idx++] = (guint16)faces[pos + j + 1];
                indices[idx++] = (guint16)faces[pos + j];
            }
            else
            {
                indices[idx++] = (guint16)faces[pos + j];
                indices[idx++] = (guint16)faces[pos + j + 1];
            }
        }

        pos += n_verts;
    }

    *out_n_triangles = n_triangles;
    return indices;
}

static GrlModel *
mesh_data_to_model (LrgMeshData *mesh_data)
{
    const gfloat *vertices;
    const gint   *faces;
    guint         n_vertices, n_faces, total_indices;
    gboolean      reverse_winding;
    guint16      *tri_indices;
    guint         n_triangles;
    GrlMesh      *mesh;
    GrlModel     *model;

    if (mesh_data == NULL || lrg_mesh_data_is_empty (mesh_data))
        return NULL;

    vertices = lrg_mesh_data_get_vertices (mesh_data, &n_vertices);
    if (vertices == NULL || n_vertices == 0)
        return NULL;

    faces = lrg_mesh_data_get_faces (mesh_data, &n_faces, &total_indices);
    if (faces == NULL || n_faces == 0)
        return NULL;

    reverse_winding = lrg_mesh_data_get_reverse_winding (mesh_data);
    tri_indices = triangulate_faces (faces, n_faces, total_indices, reverse_winding, &n_triangles);
    if (tri_indices == NULL)
        return NULL;

    mesh = grl_mesh_new_custom (vertices, n_vertices, NULL, tri_indices, n_triangles * 3);
    g_free (tri_indices);

    if (mesh == NULL)
        return NULL;

    model = grl_model_new_from_mesh (mesh);
    g_object_unref (mesh);

    return model;
}

/* ========================================================================== */
/* Track Generation                                                           */
/* ========================================================================== */

static void
create_track (RacingDemo *self)
{
    g_autoptr(GrlColor) cone_color = grl_color_new (255, 140, 0, 255);
    g_autoptr(GrlColor) line_color = grl_color_new (255, 255, 255, 255);
    g_autoptr(GrlColor) ground_color = grl_color_new (60, 100, 60, 255);
    gfloat hw, hh, inner_hw, inner_hh;
    gint   i;

    self->track_markers = g_ptr_array_new_with_free_func (g_object_unref);

    hw = TRACK_WIDTH / 2.0f;
    hh = TRACK_HEIGHT / 2.0f;
    inner_hw = hw - TRACK_LANE_WIDTH;
    inner_hh = hh - TRACK_LANE_WIDTH;

    /* Outer boundary cones */
    for (i = 0; i < 12; i++)
    {
        gfloat x = -hw + (i * (TRACK_WIDTH / 11.0f));
        LrgCone3D *cone = lrg_cone3d_new_full (x, 0.0f, hh, 0.5f, 0.0f, 1.0f, 8, cone_color);
        g_ptr_array_add (self->track_markers, cone);
        cone = lrg_cone3d_new_full (x, 0.0f, -hh, 0.5f, 0.0f, 1.0f, 8, cone_color);
        g_ptr_array_add (self->track_markers, cone);
    }

    for (i = 1; i < 15; i++)
    {
        gfloat z = -hh + (i * (TRACK_HEIGHT / 15.0f));
        LrgCone3D *cone = lrg_cone3d_new_full (-hw, 0.0f, z, 0.5f, 0.0f, 1.0f, 8, cone_color);
        g_ptr_array_add (self->track_markers, cone);
        cone = lrg_cone3d_new_full (hw, 0.0f, z, 0.5f, 0.0f, 1.0f, 8, cone_color);
        g_ptr_array_add (self->track_markers, cone);
    }

    /* Inner boundary cones */
    for (i = 0; i < 8; i++)
    {
        gfloat x = -inner_hw + (i * (inner_hw * 2.0f / 7.0f));
        LrgCone3D *cone = lrg_cone3d_new_full (x, 0.0f, inner_hh, 0.4f, 0.0f, 0.8f, 8, cone_color);
        g_ptr_array_add (self->track_markers, cone);
        cone = lrg_cone3d_new_full (x, 0.0f, -inner_hh, 0.4f, 0.0f, 0.8f, 8, cone_color);
        g_ptr_array_add (self->track_markers, cone);
    }

    for (i = 1; i < 11; i++)
    {
        gfloat z = -inner_hh + (i * (inner_hh * 2.0f / 11.0f));
        LrgCone3D *cone = lrg_cone3d_new_full (-inner_hw, 0.0f, z, 0.4f, 0.0f, 0.8f, 8, cone_color);
        g_ptr_array_add (self->track_markers, cone);
        cone = lrg_cone3d_new_full (inner_hw, 0.0f, z, 0.4f, 0.0f, 0.8f, 8, cone_color);
        g_ptr_array_add (self->track_markers, cone);
    }

    /* Start/finish line */
    for (i = 0; i < 6; i++)
    {
        gfloat x = inner_hw + (i * (TRACK_LANE_WIDTH / 6.0f));
        LrgCube3D *cube = lrg_cube3d_new_at (x, 0.05f, -hh + 2.0f, 0.8f, 0.1f, 0.3f);
        if (i % 2 == 0)
            lrg_shape_set_color (LRG_SHAPE (cube), line_color);
        g_ptr_array_add (self->track_markers, cube);
    }

    /* Ground plane */
    self->ground = lrg_plane3d_new_at (0.0f, -0.1f, 0.0f, 200.0f, 200.0f);
    lrg_shape_set_color (LRG_SHAPE (self->ground), ground_color);
}

static void
create_checkpoints (RacingDemo *self)
{
    g_autoptr(GrlColor) gate_color = grl_color_new (100, 200, 255, 255);
    gint i;

    /*
     * Checkpoint positions - ordered for counter-clockwise circuit.
     * Vehicle starts at bottom-right facing left, circuit goes: left -> top -> right -> bottom.
     * Lap completes when returning to CP0 (left) after hitting CP3 (bottom).
     */

    /* CP0: Left side (first checkpoint after starting) */
    self->checkpoints[0].x = -(TRACK_WIDTH / 2.0f) + (TRACK_LANE_WIDTH / 2.0f);
    self->checkpoints[0].z = 0.0f;
    self->checkpoints[0].radius = TRACK_LANE_WIDTH;

    /* CP1: Top side */
    self->checkpoints[1].x = 0.0f;
    self->checkpoints[1].z = (TRACK_HEIGHT / 2.0f) - (TRACK_LANE_WIDTH / 2.0f);
    self->checkpoints[1].radius = TRACK_LANE_WIDTH;

    /* CP2: Right side */
    self->checkpoints[2].x = (TRACK_WIDTH / 2.0f) - (TRACK_LANE_WIDTH / 2.0f);
    self->checkpoints[2].z = 0.0f;
    self->checkpoints[2].radius = TRACK_LANE_WIDTH;

    /* CP3: Bottom side (start/finish area) */
    self->checkpoints[3].x = 0.0f;
    self->checkpoints[3].z = -(TRACK_HEIGHT / 2.0f) + (TRACK_LANE_WIDTH / 2.0f);
    self->checkpoints[3].radius = TRACK_LANE_WIDTH;

    lrg_racing_3d_template_set_total_checkpoints (LRG_RACING_3D_TEMPLATE (self), NUM_CHECKPOINTS);

    /* Checkpoint gate visuals */
    self->checkpoint_gates = g_ptr_array_new_with_free_func (g_object_unref);

    for (i = 0; i < NUM_CHECKPOINTS; i++)
    {
        Checkpoint *cp = &self->checkpoints[i];
        LrgCube3D  *left_post, *right_post, *top_bar;
        gfloat      post_offset = 4.0f;

        if (i == 0 || i == 2)
        {
            /* Left/right sides - gate spans X axis (perpendicular to Z travel) */
            left_post = lrg_cube3d_new_at (cp->x - post_offset, 2.0f, cp->z, 0.3f, 4.0f, 0.3f);
            right_post = lrg_cube3d_new_at (cp->x + post_offset, 2.0f, cp->z, 0.3f, 4.0f, 0.3f);
            top_bar = lrg_cube3d_new_at (cp->x, 4.0f, cp->z, post_offset * 2.0f, 0.3f, 0.3f);
        }
        else
        {
            /* Top/bottom sides - gate spans Z axis (perpendicular to X travel) */
            left_post = lrg_cube3d_new_at (cp->x, 2.0f, cp->z - post_offset, 0.3f, 4.0f, 0.3f);
            right_post = lrg_cube3d_new_at (cp->x, 2.0f, cp->z + post_offset, 0.3f, 4.0f, 0.3f);
            top_bar = lrg_cube3d_new_at (cp->x, 4.0f, cp->z, 0.3f, 0.3f, post_offset * 2.0f);
        }

        lrg_shape_set_color (LRG_SHAPE (left_post), gate_color);
        lrg_shape_set_color (LRG_SHAPE (right_post), gate_color);
        lrg_shape_set_color (LRG_SHAPE (top_bar), gate_color);

        g_ptr_array_add (self->checkpoint_gates, left_post);
        g_ptr_array_add (self->checkpoint_gates, right_post);
        g_ptr_array_add (self->checkpoint_gates, top_bar);
    }
}

/* ========================================================================== */
/* Vehicle Model Loading                                                      */
/* ========================================================================== */

static void
load_vehicle_model (RacingDemo *self)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgSceneSerializerBlender) serializer = NULL;
    GList *entity_names, *iter;

    serializer = lrg_scene_serializer_blender_new ();
    self->taco_scene = lrg_scene_serializer_load_from_file (
        LRG_SCENE_SERIALIZER (serializer),
        "data/taco_truck.yaml",
        &error);

    if (self->taco_scene == NULL)
    {
        g_warning ("Failed to load taco truck: %s", error->message);
        return;
    }

    self->mesh_models = g_ptr_array_new_with_free_func ((GDestroyNotify)mesh_model_entry_free);

    entity_names = lrg_scene_get_entity_names (self->taco_scene);

    for (iter = entity_names; iter != NULL; iter = iter->next)
    {
        const gchar    *name = iter->data;
        LrgSceneEntity *entity = lrg_scene_get_entity (self->taco_scene, name);
        GPtrArray      *objects = lrg_scene_entity_get_objects (entity);
        guint           i;

        for (i = 0; i < objects->len; i++)
        {
            LrgSceneObject   *obj = g_ptr_array_index (objects, i);
            LrgPrimitiveType  prim = lrg_scene_object_get_primitive (obj);

            if (prim == LRG_PRIMITIVE_MESH)
            {
                LrgMeshData    *mesh_data = lrg_scene_object_get_mesh_data (obj);
                GrlModel       *model = mesh_data_to_model (mesh_data);

                if (model != NULL)
                {
                    GrlVector3     *loc = lrg_scene_object_get_location (obj);
                    GrlVector3     *rot = lrg_scene_object_get_rotation (obj);
                    GrlVector3     *scl = lrg_scene_object_get_scale (obj);
                    LrgMaterial3D  *mat = lrg_scene_object_get_material (obj);
                    GrlColor       *color = lrg_material3d_get_color_grl (mat);

                    MeshModelEntry *entry = mesh_model_entry_new (model, loc, rot, scl, color);
                    g_ptr_array_add (self->mesh_models, entry);
                    g_object_unref (model);
                }
            }
        }
    }

    g_list_free (entity_names);
}

/* ========================================================================== */
/* Vehicle Reset                                                              */
/* ========================================================================== */

static void
reset_vehicle (RacingDemo *self)
{
    gfloat start_x = (TRACK_WIDTH / 2.0f) - (TRACK_LANE_WIDTH / 2.0f);
    gfloat start_z = -(TRACK_HEIGHT / 2.0f) + (TRACK_LANE_WIDTH / 2.0f);
    gfloat start_rot = 90.0f;  /* Face left (counter-clockwise travel) */

    lrg_racing_3d_template_set_position (LRG_RACING_3D_TEMPLATE (self),
                                          start_x, 0.0f, start_z);
    lrg_racing_3d_template_set_rotation (LRG_RACING_3D_TEMPLATE (self), start_rot);

    self->vertical_velocity = 0.0f;
    self->jump_height = 0.0f;
    self->is_jumping = FALSE;
    self->boost_fuel = 1.0f;

    /* Snap camera */
    if (self->chase_camera != NULL)
    {
        lrg_camera_thirdperson_snap_to_target (self->chase_camera, start_x, 1.0f, start_z);
    }
}

/* ========================================================================== */
/* Checkpoint Detection                                                       */
/* ========================================================================== */

static void
racing_demo_check_checkpoints (LrgRacing3DTemplate *template)
{
    RacingDemo *self = RACING_DEMO (template);
    gfloat      px, py, pz;
    gint        current_cp, next_cp, total_cp;
    gfloat      dx, dz, dist_sq;
    Checkpoint *cp;

    lrg_racing_3d_template_get_position (template, &px, &py, &pz);
    current_cp = lrg_racing_3d_template_get_current_checkpoint (template);
    total_cp = lrg_racing_3d_template_get_total_checkpoints (template);

    /* Calculate the next expected checkpoint */
    next_cp = (current_cp + 1) % total_cp;

    /* Check if we've reached the next checkpoint */
    cp = &self->checkpoints[next_cp];
    dx = px - cp->x;
    dz = pz - cp->z;
    dist_sq = dx * dx + dz * dz;

    if (dist_sq < (cp->radius * cp->radius))
    {
        lrg_racing_3d_template_reach_checkpoint (template, next_cp);
    }
}

/* ========================================================================== */
/* Vehicle Update                                                             */
/* ========================================================================== */

static void
racing_demo_update_vehicle (LrgRacing3DTemplate *template, gdouble delta)
{
    RacingDemo *self = RACING_DEMO (template);
    gfloat      px, py, pz;
    gfloat      rotation;
    gfloat      speed;
    gfloat      steer_input = 0.0f;
    gfloat      accel_input = 0.0f;
    gfloat      max_speed;
    gfloat      dir_x, dir_z;
    gboolean    is_boosting;

    LrgRacing3DRaceState state = lrg_racing_3d_template_get_race_state (template);
    if (state != LRG_RACING_3D_RACE_STATE_RACING)
        return;

    lrg_racing_3d_template_get_position (template, &px, &py, &pz);
    rotation = lrg_racing_3d_template_get_rotation (template);
    speed = lrg_racing_3d_template_get_speed (template);

    /* Input */
    if (grl_input_is_key_down (GRL_KEY_W) || grl_input_is_key_down (GRL_KEY_UP))
        accel_input = 1.0f;
    else if (grl_input_is_key_down (GRL_KEY_S) || grl_input_is_key_down (GRL_KEY_DOWN))
        accel_input = -1.0f;

    if (grl_input_is_key_down (GRL_KEY_A) || grl_input_is_key_down (GRL_KEY_LEFT))
        steer_input = 1.0f;
    else if (grl_input_is_key_down (GRL_KEY_D) || grl_input_is_key_down (GRL_KEY_RIGHT))
        steer_input = -1.0f;

    /* Boost */
    is_boosting = FALSE;
    if ((grl_input_is_key_down (GRL_KEY_LEFT_SHIFT) || grl_input_is_key_down (GRL_KEY_RIGHT_SHIFT)) &&
        self->boost_fuel > 0.0f)
    {
        is_boosting = TRUE;
        self->boost_fuel -= BOOST_DRAIN * delta;
        if (self->boost_fuel < 0.0f)
            self->boost_fuel = 0.0f;
    }
    else
    {
        self->boost_fuel += BOOST_RECHARGE * delta;
        if (self->boost_fuel > 1.0f)
            self->boost_fuel = 1.0f;
    }

    lrg_racing_3d_template_set_boost (template, self->boost_fuel);

    /* Jump */
    if ((grl_input_is_key_pressed (GRL_KEY_SPACE) || grl_input_is_key_pressed (GRL_KEY_ENTER)) &&
        !self->is_jumping && self->jump_height <= 0.01f)
    {
        self->is_jumping = TRUE;
        self->vertical_velocity = JUMP_VELOCITY;
    }

    /* Apply gravity */
    if (self->is_jumping || self->jump_height > 0.0f)
    {
        self->vertical_velocity -= GRAVITY * delta;
        self->jump_height += self->vertical_velocity * delta;

        if (self->jump_height <= 0.0f)
        {
            self->jump_height = 0.0f;
            self->vertical_velocity = 0.0f;
            self->is_jumping = FALSE;
        }
    }

    /* Steering (only when moving) */
    if (fabsf (speed) > 0.5f)
    {
        gfloat steer_factor = (speed > 0.0f) ? 1.0f : -1.0f;
        rotation += steer_input * VEHICLE_STEER * delta * steer_factor;
        lrg_racing_3d_template_set_rotation (template, rotation);
    }

    /* Acceleration / braking */
    max_speed = is_boosting ? VEHICLE_MAX_SPEED * BOOST_SPEED_MULT : VEHICLE_MAX_SPEED;

    if (accel_input > 0.0f)
    {
        speed += VEHICLE_ACCEL * delta;
        if (speed > max_speed)
            speed = max_speed;
    }
    else if (accel_input < 0.0f)
    {
        speed -= VEHICLE_BRAKE * delta;
        if (speed < -max_speed * 0.4f)
            speed = -max_speed * 0.4f;
    }
    else
    {
        /* Friction */
        gfloat friction = 3.0f;
        if (speed > 0.0f)
        {
            speed -= friction * delta;
            if (speed < 0.0f)
                speed = 0.0f;
        }
        else if (speed < 0.0f)
        {
            speed += friction * delta;
            if (speed > 0.0f)
                speed = 0.0f;
        }
    }

    /* Apply velocity */
    dir_x = -sinf (rotation * G_PI / 180.0f);
    dir_z = -cosf (rotation * G_PI / 180.0f);
    px += dir_x * speed * delta;
    pz += dir_z * speed * delta;
    py = self->jump_height;

    lrg_racing_3d_template_set_position (template, px, py, pz);
    lrg_racing_3d_template_set_speed (template, speed);
}

/* ========================================================================== */
/* Camera Update                                                              */
/* ========================================================================== */

static void
racing_demo_update_chase_camera (LrgRacing3DTemplate *template, gdouble delta)
{
    RacingDemo *self = RACING_DEMO (template);
    gfloat      px, py, pz;
    gfloat      rotation;

    if (self->chase_camera == NULL)
        return;

    lrg_racing_3d_template_get_position (template, &px, &py, &pz);
    rotation = lrg_racing_3d_template_get_rotation (template);

    lrg_camera_thirdperson_set_yaw (self->chase_camera, rotation);
    lrg_camera_thirdperson_follow (self->chase_camera, px, py + 1.0f, pz, delta);
}

/* ========================================================================== */
/* Drawing                                                                    */
/* ========================================================================== */

static void
draw_mesh_model_at (MeshModelEntry *entry)
{
    g_autoptr(GrlVector3) pos = NULL;
    g_autoptr(GrlVector3) rot_axis = NULL;
    gfloat rx, ry, rz, rot_angle;

    pos = grl_vector3_new (entry->position->x, entry->position->y, entry->position->z);

    rx = entry->rotation->x;
    ry = entry->rotation->y;
    rz = entry->rotation->z;

    if (fabsf (rz) >= fabsf (rx) && fabsf (rz) >= fabsf (ry))
    {
        rot_axis = grl_vector3_new (0.0f, 0.0f, 1.0f);
        rot_angle = rz * (180.0f / G_PI);
    }
    else if (fabsf (ry) >= fabsf (rx))
    {
        rot_axis = grl_vector3_new (0.0f, 1.0f, 0.0f);
        rot_angle = ry * (180.0f / G_PI);
    }
    else
    {
        rot_axis = grl_vector3_new (1.0f, 0.0f, 0.0f);
        rot_angle = rx * (180.0f / G_PI);
    }

    grl_model_draw_ex (entry->model, pos, rot_axis, rot_angle, entry->scale, entry->color);
}

static void
racing_demo_draw_vehicle (LrgRacing3DTemplate *template)
{
    RacingDemo *self = RACING_DEMO (template);
    gfloat      px, py, pz;
    gfloat      rotation;
    guint       i;

    if (self->mesh_models == NULL)
        return;

    lrg_racing_3d_template_get_position (template, &px, &py, &pz);
    rotation = lrg_racing_3d_template_get_rotation (template);

    rlPushMatrix ();
    rlTranslatef (px, py, pz);
    rlRotatef (rotation, 0.0f, 1.0f, 0.0f);

    for (i = 0; i < self->mesh_models->len; i++)
    {
        MeshModelEntry *entry = g_ptr_array_index (self->mesh_models, i);
        draw_mesh_model_at (entry);
    }

    rlPopMatrix ();
}

static void
racing_demo_draw_track (LrgRacing3DTemplate *template)
{
    RacingDemo *self = RACING_DEMO (template);
    guint       i;

    /* Ground */
    if (self->ground != NULL)
        lrg_drawable_draw (LRG_DRAWABLE (self->ground), 0.0f);

    /* Track markers */
    if (self->track_markers != NULL)
    {
        for (i = 0; i < self->track_markers->len; i++)
        {
            LrgShape3D *marker = g_ptr_array_index (self->track_markers, i);
            lrg_drawable_draw (LRG_DRAWABLE (marker), 0.0f);
        }
    }

    /* Checkpoint gates */
    if (self->checkpoint_gates != NULL)
    {
        for (i = 0; i < self->checkpoint_gates->len; i++)
        {
            LrgShape3D *gate = g_ptr_array_index (self->checkpoint_gates, i);
            lrg_drawable_draw (LRG_DRAWABLE (gate), 0.0f);
        }
    }
}

static void
racing_demo_draw_speedometer (LrgRacing3DTemplate *template)
{
    RacingDemo         *self = RACING_DEMO (template);
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    g_autoptr(GrlColor) yellow = grl_color_new (255, 255, 0, 255);
    g_autoptr(GrlColor) gray = grl_color_new (150, 150, 150, 255);
    g_autoptr(GrlColor) bg = grl_color_new (0, 0, 0, 180);
    g_autoptr(GrlColor) boost_color = NULL;
    gchar               text[64];
    gfloat              speed;
    gfloat              boost_width;
    gboolean            is_boosting;

    speed = lrg_racing_3d_template_get_speed (template);
    is_boosting = lrg_racing_3d_template_is_boosting (template);

    /* Background */
    grl_draw_rectangle (5.0f, 5.0f, 220.0f, 80.0f, bg);

    /* Speed */
    g_snprintf (text, sizeof (text), "Speed: %.1f", fabsf (speed));
    grl_draw_text (text, 15, 15, 20, is_boosting ? yellow : white);

    /* Boost bar */
    grl_draw_text ("Boost:", 15, 45, 16, white);
    grl_draw_rectangle (75.0f, 45.0f, 100.0f, 16.0f, gray);

    boost_width = self->boost_fuel * 100.0f;
    if (self->boost_fuel > 0.5f)
        boost_color = grl_color_new (0, 200, 255, 255);
    else if (self->boost_fuel > 0.2f)
        boost_color = grl_color_new (255, 200, 0, 255);
    else
        boost_color = grl_color_new (255, 50, 50, 255);

    grl_draw_rectangle (75.0f, 45.0f, boost_width, 16.0f, boost_color);
}

static void
racing_demo_draw_race_hud (LrgRacing3DTemplate *template)
{
    g_autoptr(GrlColor)  white = grl_color_new (255, 255, 255, 255);
    g_autoptr(GrlColor)  green = grl_color_new (0, 255, 0, 255);
    g_autoptr(GrlColor)  gray = grl_color_new (150, 150, 150, 255);
    g_autoptr(GrlColor)  bg = grl_color_new (0, 0, 0, 180);
    gchar                text[64];
    gint                 lap, total_laps;
    gfloat               race_time, best_time;
    gint                 checkpoint, total_cp;
    LrgRacing3DRaceState state;
    gint                 countdown;

    lap = lrg_racing_3d_template_get_current_lap (template);
    total_laps = lrg_racing_3d_template_get_total_laps (template);
    race_time = lrg_racing_3d_template_get_race_time (template);
    best_time = lrg_racing_3d_template_get_best_lap_time (template);
    checkpoint = lrg_racing_3d_template_get_current_checkpoint (template);
    total_cp = lrg_racing_3d_template_get_total_checkpoints (template);
    state = lrg_racing_3d_template_get_race_state (template);
    countdown = lrg_racing_3d_template_get_countdown (template);

    /* Race info panel */
    grl_draw_rectangle (5.0f, 90.0f, 220.0f, 100.0f, bg);

    /* Time */
    {
        gint time_min = (gint)(race_time / 60.0f);
        gint time_sec = (gint)fmodf (race_time, 60.0f);
        gint time_ms = (gint)(fmodf (race_time, 1.0f) * 100.0f);
        g_snprintf (text, sizeof (text), "Time: %02d:%02d.%02d", time_min, time_sec, time_ms);
        grl_draw_text (text, 15, 100, 18, white);
    }

    /* Lap */
    g_snprintf (text, sizeof (text), "Lap: %d / %d", lap, total_laps);
    grl_draw_text (text, 15, 125, 18, white);

    /* Best time */
    if (best_time >= 0.0f)
    {
        gint best_min = (gint)(best_time / 60.0f);
        gint best_sec = (gint)fmodf (best_time, 60.0f);
        gint best_ms = (gint)(fmodf (best_time, 1.0f) * 100.0f);
        g_snprintf (text, sizeof (text), "Best: %02d:%02d.%02d", best_min, best_sec, best_ms);
    }
    else
    {
        g_snprintf (text, sizeof (text), "Best: --:--.--");
    }
    grl_draw_text (text, 15, 150, 18, green);

    /* Checkpoint */
    g_snprintf (text, sizeof (text), "CP: %d / %d", checkpoint, total_cp);
    grl_draw_text (text, 15, 175, 14, gray);

    /* Countdown / state */
    if (state == LRG_RACING_3D_RACE_STATE_COUNTDOWN)
    {
        g_autoptr(GrlColor) countdown_color = NULL;

        if (countdown > 0)
            g_snprintf (text, sizeof (text), "%d", countdown);
        else
            g_snprintf (text, sizeof (text), "GO!");

        countdown_color = grl_color_new (255, 255, 0, 255);
        grl_draw_text (text, SCREEN_WIDTH / 2 - 30, SCREEN_HEIGHT / 2 - 40, 80, countdown_color);
    }
    else if (state == LRG_RACING_3D_RACE_STATE_WAITING)
    {
        grl_draw_text ("Press ENTER to start", SCREEN_WIDTH / 2 - 100, SCREEN_HEIGHT / 2, 24, white);
    }
    else if (state == LRG_RACING_3D_RACE_STATE_FINISHED)
    {
        grl_draw_text ("RACE COMPLETE!", SCREEN_WIDTH / 2 - 100, SCREEN_HEIGHT / 2 - 40, 32, green);
        grl_draw_text ("Press R to restart", SCREEN_WIDTH / 2 - 80, SCREEN_HEIGHT / 2 + 20, 20, white);
    }

    /* Controls hint */
    grl_draw_text ("WASD:Drive Space:Jump Shift:Boost C:Camera R:Reset", 15, SCREEN_HEIGHT - 25, 14, gray);
}

/* ========================================================================== */
/* Virtual Method Overrides                                                   */
/* ========================================================================== */

static void
racing_demo_pre_update (LrgGameTemplate *template, gdouble delta)
{
    RacingDemo           *self = RACING_DEMO (template);
    LrgRacing3DRaceState  state;

    /* Pause toggle */
    if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
    {
        self->paused = !self->paused;
        if (self->paused)
            lrg_racing_3d_template_set_race_state (LRG_RACING_3D_TEMPLATE (self),
                                                    LRG_RACING_3D_RACE_STATE_PAUSED);
        else
            lrg_racing_3d_template_set_race_state (LRG_RACING_3D_TEMPLATE (self),
                                                    LRG_RACING_3D_RACE_STATE_RACING);
    }

    if (self->paused)
        return;

    state = lrg_racing_3d_template_get_race_state (LRG_RACING_3D_TEMPLATE (self));

    /* Start race */
    if (state == LRG_RACING_3D_RACE_STATE_WAITING && grl_input_is_key_pressed (GRL_KEY_ENTER))
    {
        lrg_racing_3d_template_start_countdown (LRG_RACING_3D_TEMPLATE (self));
    }

    /* Reset */
    if (grl_input_is_key_pressed (GRL_KEY_R))
    {
        reset_vehicle (self);
        lrg_racing_3d_template_set_race_state (LRG_RACING_3D_TEMPLATE (self),
                                                LRG_RACING_3D_RACE_STATE_WAITING);
    }

    /* Camera cycle */
    if (grl_input_is_key_pressed (GRL_KEY_C))
    {
        lrg_racing_3d_template_cycle_camera (LRG_RACING_3D_TEMPLATE (self));
    }

    /* Chain up */
    LRG_GAME_TEMPLATE_CLASS (racing_demo_parent_class)->pre_update (template, delta);
}

static void
racing_demo_pre_draw (LrgGameTemplate *template)
{
    RacingDemo *self = RACING_DEMO (template);
    g_autoptr(GrlColor) bg = NULL;

    /* Clear */
    bg = grl_color_new (135, 180, 220, 255);
    grl_draw_clear_background (bg);

    /* 3D scene */
    if (self->chase_camera != NULL)
    {
        lrg_camera_begin (LRG_CAMERA (self->chase_camera));

        racing_demo_draw_track (LRG_RACING_3D_TEMPLATE (self));
        racing_demo_draw_vehicle (LRG_RACING_3D_TEMPLATE (self));

        lrg_camera_end (LRG_CAMERA (self->chase_camera));
    }

    /* HUD */
    racing_demo_draw_speedometer (LRG_RACING_3D_TEMPLATE (self));
    racing_demo_draw_race_hud (LRG_RACING_3D_TEMPLATE (self));

    /* Pause overlay */
    if (self->paused)
    {
        g_autoptr(GrlColor) overlay = grl_color_new (0, 0, 0, 150);
        g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);

        grl_draw_rectangle (0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, overlay);
        grl_draw_text ("PAUSED", SCREEN_WIDTH / 2 - 60, SCREEN_HEIGHT / 2 - 20, 40, white);
        grl_draw_text ("Press ESC to resume", SCREEN_WIDTH / 2 - 90, SCREEN_HEIGHT / 2 + 30, 18, white);
    }
}

/* ========================================================================== */
/* GObject Implementation                                                     */
/* ========================================================================== */

static void
racing_demo_constructed (GObject *object)
{
    RacingDemo *self = RACING_DEMO (object);

    G_OBJECT_CLASS (racing_demo_parent_class)->constructed (object);

    /* Configure template - these don't require OpenGL context */
    lrg_racing_3d_template_set_max_speed (LRG_RACING_3D_TEMPLATE (self), VEHICLE_MAX_SPEED);
    lrg_racing_3d_template_set_acceleration (LRG_RACING_3D_TEMPLATE (self), VEHICLE_ACCEL);
    lrg_racing_3d_template_set_brake_power (LRG_RACING_3D_TEMPLATE (self), VEHICLE_BRAKE);
    lrg_racing_3d_template_set_steering_speed (LRG_RACING_3D_TEMPLATE (self), VEHICLE_STEER);
    lrg_racing_3d_template_set_grip (LRG_RACING_3D_TEMPLATE (self), VEHICLE_GRIP);
    lrg_racing_3d_template_set_boost_speed (LRG_RACING_3D_TEMPLATE (self), BOOST_SPEED_MULT);
    lrg_racing_3d_template_set_total_laps (LRG_RACING_3D_TEMPLATE (self), 3);
    lrg_racing_3d_template_set_chase_distance (LRG_RACING_3D_TEMPLATE (self), 12.0f);
    lrg_racing_3d_template_set_chase_height (LRG_RACING_3D_TEMPLATE (self), 3.0f);

    /* Initialize non-graphical state */
    self->boost_fuel = 1.0f;
    self->paused = FALSE;
}

/*
 * racing_demo_post_startup:
 *
 * Called after window/OpenGL context is created. Load all graphical
 * resources here (3D models, meshes, shapes, etc.).
 */
static void
racing_demo_post_startup (LrgGameTemplate *template)
{
    RacingDemo *self = RACING_DEMO (template);
    LrgGameTemplateClass *parent_class;

    /* Chain up first if parent has post_startup */
    parent_class = LRG_GAME_TEMPLATE_CLASS (racing_demo_parent_class);
    if (parent_class->post_startup != NULL)
        parent_class->post_startup (template);

    /* Create track and checkpoints (requires OpenGL for 3D shapes) */
    create_track (self);
    create_checkpoints (self);

    /* Load vehicle model (requires OpenGL for meshes) */
    load_vehicle_model (self);

    /* Create camera */
    self->chase_camera = lrg_camera_thirdperson_new ();
    lrg_camera_thirdperson_set_distance (self->chase_camera, 12.0f);
    lrg_camera_thirdperson_set_pitch (self->chase_camera, 25.0f);
    lrg_camera_thirdperson_set_height_offset (self->chase_camera, 3.0f);

    /* Initialize vehicle position */
    reset_vehicle (self);
}

static void
racing_demo_finalize (GObject *object)
{
    RacingDemo *self = RACING_DEMO (object);

    g_clear_pointer (&self->track_markers, g_ptr_array_unref);
    g_clear_pointer (&self->checkpoint_gates, g_ptr_array_unref);
    g_clear_pointer (&self->mesh_models, g_ptr_array_unref);
    g_clear_object (&self->ground);
    g_clear_object (&self->taco_scene);
    g_clear_object (&self->chase_camera);

    G_OBJECT_CLASS (racing_demo_parent_class)->finalize (object);
}

static void
racing_demo_class_init (RacingDemoClass *klass)
{
    GObjectClass             *object_class = G_OBJECT_CLASS (klass);
    LrgGameTemplateClass     *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgRacing3DTemplateClass *racing_class = LRG_RACING_3D_TEMPLATE_CLASS (klass);

    object_class->constructed = racing_demo_constructed;
    object_class->finalize = racing_demo_finalize;

    template_class->post_startup = racing_demo_post_startup;
    template_class->pre_update = racing_demo_pre_update;
    template_class->pre_draw = racing_demo_pre_draw;

    racing_class->update_vehicle = racing_demo_update_vehicle;
    racing_class->update_chase_camera = racing_demo_update_chase_camera;
    racing_class->check_checkpoints = racing_demo_check_checkpoints;
    racing_class->draw_vehicle = racing_demo_draw_vehicle;
    racing_class->draw_track = racing_demo_draw_track;
    racing_class->draw_speedometer = racing_demo_draw_speedometer;
    racing_class->draw_race_hud = racing_demo_draw_race_hud;
}

static void
racing_demo_init (RacingDemo *self)
{
}

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

static RacingDemo *
racing_demo_new (void)
{
    return g_object_new (RACING_TYPE_DEMO,
                         "title", "Racing 3D Demo",
                         "window-width", SCREEN_WIDTH,
                         "window-height", SCREEN_HEIGHT,
                         "target-fps", 60,
                         NULL);
}

/* ========================================================================== */
/* Main Entry Point                                                           */
/* ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_autoptr(RacingDemo) game = NULL;

    setlocale (LC_ALL, "");

    game = racing_demo_new ();
    lrg_game_template_run (LRG_GAME_TEMPLATE (game), argc, argv);

    return 0;
}
