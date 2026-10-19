/* lrg-nav-level.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The walk and sight queries of one level.  The level's triangles are
 * staged once and feed two structures at the bake:
 *
 *   walk   an LrgNavPolyMesh; each door's box (lowered by max-climb so
 *          the floor under it, and widened by a cell so a thin slab
 *          covers cells) becomes a tagged area; the path filter crosses
 *          a door whose state is at most what the query's agent passes.
 *   sight  an LrgCollisionMesh of the same triangles; each door that is
 *          not open is an instance of one shared unit box scaled and
 *          moved onto the door's box, so opening a door only removes an
 *          instance.
 *
 * An asynchronous bake runs both builds on a worker thread; until it
 * ends the level answers as an unbaked one and the door states set in
 * the meantime are applied when it does.
 */

#include "lrg-nav-level.h"
#include <math.h>
#include <string.h>

/* the thinnest a door box may be (a door modelled as a plane) */
#define MIN_DOOR_THICKNESS (0.01f)

typedef struct
{
    guint32         tag;
    gfloat          min[3];
    gfloat          max[3];
    LrgNavDoorState state;
    guint           instance;   /* sight instance, INVALID while open */
} Door;

struct _LrgNavLevel
{
    GObject           parent_instance;
    LrgNavPolyMesh   *nav;
    LrgCollisionMesh *collision;
    LrgCollisionMesh *door_shape;  /* the unit box every door instance uses */
    GArray           *triangles;   /* gfloat, nine per triangle */
    GHashTable       *doors;       /* GUINT tag -> Door* */
    gboolean          baked;
    gboolean          baking;
};

G_DEFINE_FINAL_TYPE (LrgNavLevel, lrg_nav_level, G_TYPE_OBJECT)

/* the twelve triangles of the box [0,1]^3 */
static LrgCollisionMesh *
unit_box (void)
{
    static const gfloat p[8][3] = {
        { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 },
        { 0, 1, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 0, 1, 1 },
    };
    static const guint faces[6][4] = {
        { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 },
        { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 3, 0, 4, 7 },
    };
    LrgCollisionMesh *mesh;
    gfloat vertices[6 * 2 * 9];
    guint f;
    guint n;

    n = 0;
    for (f = 0; f < 6; f++)
    {
        const guint order[6] = { 0, 1, 2, 0, 2, 3 };
        guint k;

        for (k = 0; k < 6; k++)
        {
            memcpy (vertices + n, p[faces[f][order[k]]], sizeof (gfloat) * 3);
            n += 3;
        }
    }
    mesh = lrg_collision_mesh_new ();
    /* fixed, finite input: cannot fail */
    lrg_collision_mesh_set_triangles (mesh, vertices, n, NULL, NULL);
    return mesh;
}

/* the column-major transform that puts the unit box onto @door's box */
static void
door_transform (const Door *door,
                gfloat     *m)
{
    guint i;

    memset (m, 0, sizeof (gfloat) * 16);
    for (i = 0; i < 3; i++)
    {
        m[i * 5] = door->max[i] - door->min[i];
        m[12 + i] = door->min[i];
    }
    m[15] = 1.0f;
}

/* makes the walk and sight structures match @door's state (main thread) */
static void
apply_door (LrgNavLevel *self,
            Door        *door)
{
    gboolean blocks;

    blocks = door->state != LRG_NAV_DOOR_STATE_OPEN;
    if (blocks && door->instance == LRG_COLLISION_MESH_INVALID_INSTANCE)
    {
        gfloat m[16];

        door_transform (door, m);
        door->instance = lrg_collision_mesh_add_instance (self->collision, self->door_shape, m,
                                                          (guint64) door->tag);
    }
    else if (!blocks && door->instance != LRG_COLLISION_MESH_INVALID_INSTANCE)
    {
        lrg_collision_mesh_remove_instance (self->collision, door->instance);
        door->instance = LRG_COLLISION_MESH_INVALID_INSTANCE;
    }
}

static void
apply_all_doors (LrgNavLevel *self)
{
    GHashTableIter iter;
    gpointer value;

    g_hash_table_iter_init (&iter, self->doors);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        apply_door (self, (Door *) value);
}

/* drops every door's sight instance (before the sight mesh is rebuilt) */
static void
remove_door_instances (LrgNavLevel *self)
{
    GHashTableIter iter;
    gpointer value;

    g_hash_table_iter_init (&iter, self->doors);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        Door *door = (Door *) value;

        if (door->instance != LRG_COLLISION_MESH_INVALID_INSTANCE)
            lrg_collision_mesh_remove_instance (self->collision, door->instance);
        door->instance = LRG_COLLISION_MESH_INVALID_INSTANCE;
    }
}

/* ------------------------------------------------------------------ */
/* The bake                                                             */

typedef struct
{
    GArray *triangles;   /* a copy: the worker owns it */
    GArray *boxes;       /* gfloat min[3] max[3] and the tag as a float bit pattern */
} BakeInput;

static void
bake_input_free (gpointer data)
{
    BakeInput *input = (BakeInput *) data;

    g_array_unref (input->triangles);
    g_array_unref (input->boxes);
    g_free (input);
}

/* what a bake needs, copied so a worker reads nothing the caller owns */
static BakeInput *
bake_input_new (LrgNavLevel *self)
{
    BakeInput *input;
    GHashTableIter iter;
    gpointer value;
    gfloat climb;
    gfloat cell;

    input = g_new0 (BakeInput, 1);
    input->triangles = g_array_sized_new (FALSE, FALSE, sizeof (gfloat), self->triangles->len);
    g_array_append_vals (input->triangles, self->triangles->data, self->triangles->len);
    input->boxes = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_object_get (self->nav, "max-climb", &climb, "cell-size", &cell, NULL);
    g_hash_table_iter_init (&iter, self->doors);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        Door *door = (Door *) value;
        gfloat box[7];
        guint32 tag = door->tag;

        memcpy (box, door->min, sizeof (gfloat) * 3);
        memcpy (box + 3, door->max, sizeof (gfloat) * 3);
        /* the floor the door stands on lies at (or a little under) its
         * bottom; a door slab thinner than a cell holds no cell centre,
         * so the area reaches a cell further on every side */
        box[1] -= climb;
        box[0] -= cell;
        box[2] -= cell;
        box[3] += cell;
        box[5] += cell;
        memcpy (&box[6], &tag, sizeof (gfloat));
        g_array_append_vals (input->boxes, box, 7);
    }
    return input;
}

/* the bake proper: touches only the two meshes and @input (any thread) */
static gboolean
bake_run (LrgNavPolyMesh    *nav,
          LrgCollisionMesh  *collision,
          BakeInput         *input,
          GError           **error)
{
    guint i;

    lrg_nav_poly_mesh_clear_input (nav);
    lrg_nav_poly_mesh_add_triangles (nav, (const gfloat *) input->triangles->data,
                                     input->triangles->len, TRUE);
    for (i = 0; i + 7 <= input->boxes->len; i += 7)
    {
        const gfloat *box = &g_array_index (input->boxes, gfloat, i);
        guint32 tag;

        memcpy (&tag, &box[6], sizeof (guint32));
        lrg_nav_poly_mesh_add_area_box (nav, box, box + 3, tag);
    }
    if (!lrg_nav_poly_mesh_bake (nav, error))
        return FALSE;
    return lrg_collision_mesh_set_triangles (collision, (const gfloat *) input->triangles->data,
                                             input->triangles->len, NULL, error);
}

static void
bake_thread (GTask        *task,
             gpointer      source,
             gpointer      task_data,
             GCancellable *cancellable)
{
    LrgNavLevel *self = LRG_NAV_LEVEL (source);
    GError *error = NULL;

    if (g_task_return_error_if_cancelled (task))
        return;
    if (!bake_run (self->nav, self->collision, (BakeInput *) task_data, &error))
        g_task_return_error (task, error);
    else
        g_task_return_boolean (task, TRUE);
}

/* ------------------------------------------------------------------ */
/* GObject                                                              */

static void
lrg_nav_level_finalize (GObject *object)
{
    LrgNavLevel *self = LRG_NAV_LEVEL (object);

    g_clear_object (&self->nav);
    g_clear_object (&self->collision);
    g_clear_object (&self->door_shape);
    g_clear_pointer (&self->triangles, g_array_unref);
    g_clear_pointer (&self->doors, g_hash_table_unref);
    G_OBJECT_CLASS (lrg_nav_level_parent_class)->finalize (object);
}

static void
lrg_nav_level_class_init (LrgNavLevelClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_nav_level_finalize;
}

static void
lrg_nav_level_init (LrgNavLevel *self)
{
    self->nav = lrg_nav_poly_mesh_new ();
    self->collision = lrg_collision_mesh_new ();
    self->door_shape = unit_box ();
    self->triangles = g_array_new (FALSE, FALSE, sizeof (gfloat));
    self->doors = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */

LrgNavLevel *
lrg_nav_level_new (void)
{
    return (LrgNavLevel *) g_object_new (LRG_TYPE_NAV_LEVEL, NULL);
}

LrgNavPolyMesh *
lrg_nav_level_get_nav_mesh (LrgNavLevel *self)
{
    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), NULL);
    return self->nav;
}

LrgCollisionMesh *
lrg_nav_level_get_collision (LrgNavLevel *self)
{
    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), NULL);
    return self->collision;
}

guint
lrg_nav_level_add_triangles (LrgNavLevel  *self,
                             const gfloat *vertices,
                             guint         n_floats)
{
    guint added = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), 0);
    g_return_val_if_fail (vertices != NULL || n_floats == 0, 0);
    g_return_val_if_fail (!self->baking, 0);

    for (i = 0; i + 9 <= n_floats; i += 9)
    {
        gboolean finite = TRUE;
        guint k;

        for (k = 0; k < 9; k++)
            finite = finite && isfinite (vertices[i + k]);
        if (!finite)
            continue;
        g_array_append_vals (self->triangles, vertices + i, 9);
        added++;
    }
    return added;
}

gboolean
lrg_nav_level_add_door (LrgNavLevel     *self,
                        guint32          tag,
                        const gfloat    *min,
                        const gfloat    *max,
                        LrgNavDoorState  state)
{
    Door *door;
    Door *old;
    guint i;

    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), FALSE);
    g_return_val_if_fail (min != NULL && max != NULL, FALSE);
    g_return_val_if_fail (!self->baking, FALSE);

    if (tag == 0)
        return FALSE;
    for (i = 0; i < 3; i++)
        if (!isfinite (min[i]) || !isfinite (max[i]) || min[i] > max[i])
            return FALSE;
    door = g_new0 (Door, 1);
    door->tag = tag;
    door->state = state;
    door->instance = LRG_COLLISION_MESH_INVALID_INSTANCE;
    for (i = 0; i < 3; i++)
    {
        gfloat mid = (min[i] + max[i]) * 0.5f;
        gfloat half = MAX ((max[i] - min[i]) * 0.5f, MIN_DOOR_THICKNESS * 0.5f);

        door->min[i] = mid - half;
        door->max[i] = mid + half;
    }
    /* a replaced door gives back its sight instance */
    old = (Door *) g_hash_table_lookup (self->doors, GUINT_TO_POINTER (tag));
    if (old != NULL && old->instance != LRG_COLLISION_MESH_INVALID_INSTANCE)
        lrg_collision_mesh_remove_instance (self->collision, old->instance);
    g_hash_table_replace (self->doors, GUINT_TO_POINTER (tag), door);
    if (self->baked)
        apply_door (self, door);
    return TRUE;
}

gboolean
lrg_nav_level_set_door_state (LrgNavLevel     *self,
                              guint32          tag,
                              LrgNavDoorState  state)
{
    Door *door;

    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), FALSE);

    door = (Door *) g_hash_table_lookup (self->doors, GUINT_TO_POINTER (tag));
    if (door == NULL)
        return FALSE;
    door->state = state;
    /* a running bake owns the meshes: its end applies every state */
    if (!self->baking && self->baked)
        apply_door (self, door);
    return TRUE;
}

LrgNavDoorState
lrg_nav_level_get_door_state (LrgNavLevel *self,
                              guint32      tag)
{
    Door *door;

    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), LRG_NAV_DOOR_STATE_OPEN);

    door = (Door *) g_hash_table_lookup (self->doors, GUINT_TO_POINTER (tag));
    return door != NULL ? door->state : LRG_NAV_DOOR_STATE_OPEN;
}

guint
lrg_nav_level_get_door_count (LrgNavLevel *self)
{
    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), 0);
    return g_hash_table_size (self->doors);
}

guint
lrg_nav_level_get_triangle_count (LrgNavLevel *self)
{
    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), 0);
    return self->triangles->len / 9;
}

gboolean
lrg_nav_level_bake (LrgNavLevel  *self,
                    GError      **error)
{
    BakeInput *input;
    gboolean ok;

    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (self->baking)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PENDING, "a bake is running");
        return FALSE;
    }
    remove_door_instances (self);
    input = bake_input_new (self);
    ok = bake_run (self->nav, self->collision, input, error);
    bake_input_free (input);
    /* a failed bake keeps the previous meshes: their doors come back too */
    self->baked = ok || self->baked;
    if (self->baked)
        apply_all_doors (self);
    return ok;
}

static void
bake_done (GObject      *source,
           GAsyncResult *result,
           gpointer      user_data)
{
    LrgNavLevel *self = LRG_NAV_LEVEL (source);
    GTask *outer = G_TASK (user_data);
    GError *error = NULL;

    self->baking = FALSE;
    if (g_task_propagate_boolean (G_TASK (result), &error))
    {
        self->baked = TRUE;
        apply_all_doors (self);
        g_task_return_boolean (outer, TRUE);
    }
    else
    {
        /* the meshes may be half-built (a cancelled or failed bake) */
        self->baked = FALSE;
        g_task_return_error (outer, error);
    }
    g_object_unref (outer);
}

void
lrg_nav_level_bake_async (LrgNavLevel         *self,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
    GTask *outer;
    GTask *inner;

    g_return_if_fail (LRG_IS_NAV_LEVEL (self));

    outer = g_task_new (self, cancellable, callback, user_data);
    g_task_set_source_tag (outer, lrg_nav_level_bake_async);
    if (self->baking)
    {
        g_task_return_new_error (outer, G_IO_ERROR, G_IO_ERROR_PENDING, "a bake is running");
        g_object_unref (outer);
        return;
    }
    remove_door_instances (self);
    self->baking = TRUE;
    self->baked = FALSE;
    /* the worker's own task: its result comes back to bake_done() on this
     * thread's context, which applies the doors before the caller hears */
    inner = g_task_new (self, cancellable, bake_done, outer);
    g_task_set_task_data (inner, bake_input_new (self), bake_input_free);
    g_task_run_in_thread (inner, bake_thread);
    g_object_unref (inner);
}

gboolean
lrg_nav_level_bake_finish (LrgNavLevel   *self,
                           GAsyncResult  *result,
                           GError       **error)
{
    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), FALSE);
    g_return_val_if_fail (g_task_is_valid (result, self), FALSE);

    return g_task_propagate_boolean (G_TASK (result), error);
}

gboolean
lrg_nav_level_is_baked (LrgNavLevel *self)
{
    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), FALSE);
    return self->baked && !self->baking;
}

typedef struct
{
    LrgNavLevel     *level;
    LrgNavDoorState  passes;
} Filter;

/* tagged polygons: a door no more closed than the agent passes */
static gboolean
door_filter (guint32  tag,
             gpointer user_data)
{
    Filter *filter = (Filter *) user_data;
    Door *door;

    door = (Door *) g_hash_table_lookup (filter->level->doors, GUINT_TO_POINTER (tag));
    return door == NULL || (gint) door->state <= (gint) filter->passes;
}

gfloat *
lrg_nav_level_find_path (LrgNavLevel   *self,
                         const gfloat  *start,
                         const gfloat  *goal,
                         const gfloat  *extents,
                         LrgNavDoorState passes,
                         guint          max_nodes,
                         guint         *n_floats,
                         GError       **error)
{
    Filter filter;

    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), NULL);
    g_return_val_if_fail (start != NULL && goal != NULL && extents != NULL, NULL);
    g_return_val_if_fail (n_floats != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    *n_floats = 0;
    if (!lrg_nav_level_is_baked (self))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "the level is not baked");
        return NULL;
    }
    filter.level = self;
    filter.passes = passes;
    return lrg_nav_poly_mesh_find_path (self->nav, start, goal, extents, max_nodes, door_filter,
                                        &filter, NULL, n_floats, error);
}

/* where the segment a + t (b - a), t in 0 .. 1, enters @door's box (slabs), or -1 */
static gfloat
segment_enters_box (const gfloat *a,
                    const gfloat *b,
                    const Door   *door)
{
    gfloat t0 = 0.0f;
    gfloat t1 = 1.0f;
    guint i;

    for (i = 0; i < 3; i++)
    {
        gfloat d = b[i] - a[i];

        if (fabsf (d) < 1e-7f)
        {
            if (a[i] < door->min[i] || a[i] > door->max[i])
                return -1.0f;
            continue;
        }
        {
            gfloat u = (door->min[i] - a[i]) / d;
            gfloat v = (door->max[i] - a[i]) / d;

            if (u > v)
            {
                gfloat swap = u;

                u = v;
                v = swap;
            }
            t0 = MAX (t0, u);
            t1 = MIN (t1, v);
            if (t0 > t1)
                return -1.0f;
        }
    }
    return t0;
}

typedef struct
{
    gfloat  order;   /* leg index + entry fraction */
    guint32 tag;
} DoorHit;

static gint
door_hit_compare (gconstpointer a,
                  gconstpointer b)
{
    const DoorHit *x = (const DoorHit *) a;
    const DoorHit *y = (const DoorHit *) b;

    return x->order < y->order ? -1 : (x->order > y->order ? 1 : 0);
}

guint32 *
lrg_nav_level_find_doors_on_path (LrgNavLevel  *self,
                                  const gfloat *points,
                                  guint         n_floats,
                                  guint        *n_doors)
{
    g_autoptr(GArray) hits = NULL;
    GHashTableIter iter;
    gpointer value;
    guint32 *tags;
    gfloat climb;
    guint i;

    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), NULL);
    g_return_val_if_fail (points != NULL || n_floats == 0, NULL);
    g_return_val_if_fail (n_doors != NULL, NULL);

    *n_doors = 0;
    hits = g_array_new (FALSE, FALSE, sizeof (DoorHit));
    g_object_get (self->nav, "max-climb", &climb, NULL);
    g_hash_table_iter_init (&iter, self->doors);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        Door box = *(Door *) value;

        /* path points lie on the floor, which may be a little under the box */
        box.min[1] -= climb;
        for (i = 3; i + 3 <= n_floats; i += 3)
        {
            gfloat t = segment_enters_box (points + i - 3, points + i, &box);

            if (t >= 0.0f)
            {
                DoorHit hit;

                hit.order = (gfloat) (i / 3) + t;
                hit.tag = box.tag;
                g_array_append_val (hits, hit);
                break;
            }
        }
    }
    if (hits->len == 0)
        return NULL;
    g_array_sort (hits, door_hit_compare);
    tags = g_new (guint32, hits->len);
    for (i = 0; i < hits->len; i++)
        tags[i] = g_array_index (hits, DoorHit, i).tag;
    *n_doors = hits->len;
    return tags;
}

gboolean
lrg_nav_level_can_walk (LrgNavLevel  *self,
                        const gfloat *from,
                        const gfloat *to,
                        const gfloat *extents,
                        gfloat       *hit_fraction)
{
    Filter filter;
    gfloat fraction = 0.0f;
    gfloat end[3];
    gboolean blocked;

    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), FALSE);
    g_return_val_if_fail (from != NULL && to != NULL && extents != NULL, FALSE);

    if (!lrg_nav_level_is_baked (self))
    {
        if (hit_fraction != NULL)
            *hit_fraction = 0.0f;
        return FALSE;
    }
    filter.level = self;
    filter.passes = LRG_NAV_DOOR_STATE_OPEN;
    blocked = lrg_nav_poly_mesh_raycast (self->nav, from, to, extents, door_filter, &filter,
                                         &fraction, end);
    /* the walk follows the floor: it must end on the floor under @to, not
     * on a storey above or below it in the same X and Z */
    if (!blocked && fabsf (end[1] - to[1]) > extents[1])
    {
        blocked = TRUE;
        fraction = 1.0f;
    }
    if (hit_fraction != NULL)
        *hit_fraction = blocked ? fraction : 1.0f;
    return !blocked;
}

gboolean
lrg_nav_level_has_line_of_sight (LrgNavLevel     *self,
                                 const gfloat    *from,
                                 const gfloat    *to,
                                 LrgCollisionHit *out_hit)
{
    gfloat direction[3];
    gfloat length;
    guint i;

    g_return_val_if_fail (LRG_IS_NAV_LEVEL (self), TRUE);
    g_return_val_if_fail (from != NULL && to != NULL, TRUE);

    if (!lrg_nav_level_is_baked (self))
        return TRUE;
    for (i = 0; i < 3; i++)
        direction[i] = to[i] - from[i];
    length = sqrtf (direction[0] * direction[0] + direction[1] * direction[1] +
                    direction[2] * direction[2]);
    if (!isfinite (length) || length < 1e-5f)
        return TRUE;
    return !lrg_collision_mesh_raycast (self->collision, from, direction, length, out_hit);
}
