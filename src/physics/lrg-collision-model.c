/* lrg-collision-model.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A collision model is a solid plus a world transform and a filter.
 * The world stores models in generation-tagged slots. Removing a slot
 * drops the world's reference and leaves the generation in place, so
 * the same handle removes twice. The next occupant bumps the generation
 * and the old handle fails closed.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <gio/gio.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "physics/lrg-solid-private.h"

typedef struct
{
    LrgCollisionModel *model;
    guint              generation;
} SolidSlot;

struct _LrgSolidWorld
{
    GObject    parent_instance;
    SolidSlot *slots;
    guint      n_slots;
};

G_DEFINE_FINAL_TYPE (LrgCollisionModel, lrg_collision_model, G_TYPE_OBJECT)
G_DEFINE_FINAL_TYPE (LrgSolidWorld, lrg_solid_world, G_TYPE_OBJECT)

static void
lrg_collision_model_finalize (GObject *object)
{
    LrgCollisionModel *self;

    self = LRG_COLLISION_MODEL (object);
    g_clear_object (&self->geometry);
    G_OBJECT_CLASS (lrg_collision_model_parent_class)->finalize (object);
}

static void
lrg_collision_model_class_init (LrgCollisionModelClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_collision_model_finalize;
}

static void
lrg_collision_model_init (LrgCollisionModel *self)
{
    self->scale = 1.0;
    self->group = 1;
    self->mask = 0xffffffffu;
}

static void
lrg_solid_world_finalize (GObject *object)
{
    LrgSolidWorld *self;
    guint i;

    self = LRG_SOLID_WORLD (object);
    for (i = 0; i < self->n_slots; i++)
        g_clear_object (&self->slots[i].model);
    g_free (self->slots);
    self->slots = NULL;
    self->n_slots = 0;
    G_OBJECT_CLASS (lrg_solid_world_parent_class)->finalize (object);
}

static void
lrg_solid_world_class_init (LrgSolidWorldClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_solid_world_finalize;
}

static void
lrg_solid_world_init (LrgSolidWorld *self)
{
    (void) self;
}

static gboolean
store_transform (LrgCollisionModel *self, const gfloat *transform, GError **error)
{
    gdouble rotation[3][3];
    gdouble translation[3];
    gdouble scale;
    guint i;

    if (!lrg_solid_transform_decompose (transform, rotation, translation, &scale, error))
        return FALSE;
    if (transform == NULL)
    {
        memset (self->transform, 0, sizeof self->transform);
        self->transform[0] = 1.0f;
        self->transform[5] = 1.0f;
        self->transform[10] = 1.0f;
        self->transform[15] = 1.0f;
    }
    else
    {
        for (i = 0; i < 16; i++)
            self->transform[i] = transform[i];
    }
    memcpy (self->rotation, rotation, sizeof rotation);
    memcpy (self->translation, translation, sizeof translation);
    self->scale = scale;
    return TRUE;
}

LrgCollisionModel *
lrg_collision_model_new (LrgSolidGeometry *geometry, const gfloat *transform,
                         guint32 group, guint32 mask, guint64 caller_id, GError **error)
{
    LrgCollisionModel *self;

    if (!LRG_IS_SOLID_GEOMETRY (geometry))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Collision model requires a solid geometry");
        return NULL;
    }
    self = g_object_new (LRG_TYPE_COLLISION_MODEL, NULL);
    if (!store_transform (self, transform, error))
    {
        g_object_unref (self);
        return NULL;
    }
    self->geometry = g_object_ref (geometry);
    self->group = group;
    self->mask = mask;
    self->caller_id = caller_id;
    return self;
}

gboolean
lrg_collision_model_set_transform (LrgCollisionModel *self, const gfloat *transform, GError **error)
{
    gfloat saved[16];
    gdouble rotation[3][3];
    gdouble translation[3];
    gdouble scale;

    g_return_val_if_fail (LRG_IS_COLLISION_MODEL (self), FALSE);
    g_return_val_if_fail (transform != NULL, FALSE);
    memcpy (saved, self->transform, sizeof saved);
    memcpy (rotation, self->rotation, sizeof rotation);
    memcpy (translation, self->translation, sizeof translation);
    scale = self->scale;
    if (!store_transform (self, transform, error))
    {
        memcpy (self->transform, saved, sizeof saved);
        memcpy (self->rotation, rotation, sizeof rotation);
        memcpy (self->translation, translation, sizeof translation);
        self->scale = scale;
        return FALSE;
    }
    return TRUE;
}

void
lrg_collision_model_get_transform (LrgCollisionModel *self, gfloat *transform)
{
    g_return_if_fail (LRG_IS_COLLISION_MODEL (self));
    g_return_if_fail (transform != NULL);
    memcpy (transform, self->transform, sizeof self->transform);
}

LrgSolidGeometry *
lrg_collision_model_get_geometry (LrgCollisionModel *self)
{
    g_return_val_if_fail (LRG_IS_COLLISION_MODEL (self), NULL);
    return self->geometry;
}

void
lrg_collision_model_get_filter (LrgCollisionModel *self, guint32 *group, guint32 *mask)
{
    g_return_if_fail (LRG_IS_COLLISION_MODEL (self));
    if (group != NULL)
        *group = self->group;
    if (mask != NULL)
        *mask = self->mask;
}

guint64
lrg_collision_model_get_caller_id (LrgCollisionModel *self)
{
    g_return_val_if_fail (LRG_IS_COLLISION_MODEL (self), 0);
    return self->caller_id;
}

LrgSolidWorld *
lrg_solid_world_new (void)
{
    return g_object_new (LRG_TYPE_SOLID_WORLD, NULL);
}

gboolean
lrg_solid_world_add (LrgSolidWorld *self, LrgCollisionModel *model,
                     LrgSolidHandle *handle, GError **error)
{
    guint i, slot;

    g_return_val_if_fail (LRG_IS_SOLID_WORLD (self), FALSE);
    g_return_val_if_fail (handle != NULL, FALSE);
    if (!LRG_IS_COLLISION_MODEL (model))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Solid world add requires a collision model");
        return FALSE;
    }
    slot = self->n_slots;
    for (i = 0; i < self->n_slots; i++)
    {
        if (self->slots[i].model == NULL)
        {
            slot = i;
            break;
        }
    }
    if (slot == self->n_slots && self->n_slots >= LRG_SOLID_MAX_MODELS)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                             "Solid world model budget is exhausted");
        return FALSE;
    }
    if (slot < self->n_slots && self->slots[slot].generation == G_MAXUINT)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                             "Solid world slot generation is exhausted");
        return FALSE;
    }
    if (slot == self->n_slots)
    {
        SolidSlot *grown;

        grown = g_renew (SolidSlot, self->slots, self->n_slots + 1);
        self->slots = grown;
        self->slots[self->n_slots].model = NULL;
        self->slots[self->n_slots].generation = 0;
        self->n_slots++;
    }
    if (self->slots[slot].generation == G_MAXUINT)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                             "Solid world slot generation is exhausted");
        return FALSE;
    }
    self->slots[slot].generation += 1;
    self->slots[slot].model = g_object_ref (model);
    handle->index = slot;
    handle->generation = self->slots[slot].generation;
    return TRUE;
}

gboolean
lrg_solid_world_remove (LrgSolidWorld *self, LrgSolidHandle handle, GError **error)
{
    SolidSlot *slot;

    g_return_val_if_fail (LRG_IS_SOLID_WORLD (self), FALSE);
    if (handle.index >= self->n_slots || handle.generation == 0 ||
        self->slots[handle.index].generation != handle.generation)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             "Solid handle is stale or was never issued");
        return FALSE;
    }
    slot = &self->slots[handle.index];
    if (slot->model == NULL)
        return TRUE;
    g_clear_object (&slot->model);
    return TRUE;
}

static LrgCollisionModel *
live_model (LrgSolidWorld *self, LrgSolidHandle handle, GError **error)
{
    if (handle.index >= self->n_slots || handle.generation == 0 ||
        self->slots[handle.index].generation != handle.generation ||
        self->slots[handle.index].model == NULL)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             "Solid handle is stale or empty");
        return NULL;
    }
    return self->slots[handle.index].model;
}

LrgCollisionModel *
lrg_solid_world_get_model (LrgSolidWorld *self, LrgSolidHandle handle, GError **error)
{
    g_return_val_if_fail (LRG_IS_SOLID_WORLD (self), NULL);
    return live_model (self, handle, error);
}

static gboolean
bake_model (LrgCollisionModel *model, LrgSolidPrim **prims, guint *n_prims, GError **error)
{
    return lrg_solid_geometry_bake (model->geometry, model->rotation, model->scale,
                                    model->translation, prims, n_prims, error);
}

static gboolean
filters_meet (const LrgCollisionModel *a, const LrgCollisionModel *b)
{
    return (a->group & b->mask) != 0 && (b->group & a->mask) != 0;
}

gboolean
lrg_solid_world_classify_point (LrgSolidWorld *self, LrgSolidHandle handle,
                                const gfloat *point, LrgSolidClass *result, GError **error)
{
    LrgCollisionModel *model;
    LrgSolidPrim *prims;
    guint n_prims;
    gdouble xyz[3];
    gboolean ok;

    g_return_val_if_fail (LRG_IS_SOLID_WORLD (self), FALSE);
    g_return_val_if_fail (point != NULL, FALSE);
    g_return_val_if_fail (result != NULL, FALSE);
    model = live_model (self, handle, error);
    if (model == NULL)
        return FALSE;
    xyz[0] = point[0];
    xyz[1] = point[1];
    xyz[2] = point[2];
    if (!isfinite (xyz[0]) || !isfinite (xyz[1]) || !isfinite (xyz[2]))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Solid query point is not finite");
        return FALSE;
    }
    prims = NULL;
    n_prims = 0;
    if (!bake_model (model, &prims, &n_prims, error))
        return FALSE;
    ok = lrg_solid_prims_classify (prims, n_prims, xyz, result);
    lrg_solid_prims_free (prims, n_prims);
    return ok;
}

gboolean
lrg_solid_world_overlap (LrgSolidWorld *self, LrgSolidHandle a, LrgSolidHandle b,
                         LrgSolidContact *contact, gboolean *overlapping, GError **error)
{
    LrgCollisionModel *model_a, *model_b;
    LrgSolidPrim *prims_a, *prims_b;
    guint n_a, n_b;
    gboolean ok;

    g_return_val_if_fail (LRG_IS_SOLID_WORLD (self), FALSE);
    g_return_val_if_fail (overlapping != NULL, FALSE);
    model_a = live_model (self, a, error);
    if (model_a == NULL)
        return FALSE;
    model_b = live_model (self, b, error);
    if (model_b == NULL)
        return FALSE;
    if (!filters_meet (model_a, model_b))
    {
        *overlapping = FALSE;
        return TRUE;
    }
    prims_a = NULL;
    prims_b = NULL;
    n_a = 0;
    n_b = 0;
    if (!bake_model (model_a, &prims_a, &n_a, error))
        return FALSE;
    if (!bake_model (model_b, &prims_b, &n_b, error))
    {
        lrg_solid_prims_free (prims_a, n_a);
        return FALSE;
    }
    ok = lrg_solid_prims_overlap (prims_a, n_a, prims_b, n_b, contact, overlapping, error);
    lrg_solid_prims_free (prims_a, n_a);
    lrg_solid_prims_free (prims_b, n_b);
    return ok;
}

gboolean
lrg_solid_world_contains (LrgSolidWorld *self, LrgSolidHandle outer, LrgSolidHandle inner,
                          gboolean *contained, GError **error)
{
    LrgCollisionModel *model_outer, *model_inner;
    LrgSolidPrim *prims_outer, *prims_inner;
    guint n_outer, n_inner;
    gboolean ok;

    g_return_val_if_fail (LRG_IS_SOLID_WORLD (self), FALSE);
    g_return_val_if_fail (contained != NULL, FALSE);
    model_outer = live_model (self, outer, error);
    if (model_outer == NULL)
        return FALSE;
    model_inner = live_model (self, inner, error);
    if (model_inner == NULL)
        return FALSE;
    prims_outer = NULL;
    prims_inner = NULL;
    if (!bake_model (model_outer, &prims_outer, &n_outer, error))
        return FALSE;
    if (!bake_model (model_inner, &prims_inner, &n_inner, error))
    {
        lrg_solid_prims_free (prims_outer, n_outer);
        return FALSE;
    }
    ok = lrg_solid_prims_contains (prims_outer, n_outer, prims_inner, n_inner, contained, error);
    lrg_solid_prims_free (prims_outer, n_outer);
    lrg_solid_prims_free (prims_inner, n_inner);
    return ok;
}

gboolean
lrg_solid_world_sweep_sphere (LrgSolidWorld *self, LrgSolidHandle moving, const gfloat *delta,
                              LrgSolidHandle against, LrgSolidHit *hit, gboolean *did_hit,
                              GError **error)
{
    LrgCollisionModel *mover, *static_model;
    LrgSolidPrim *moving_prims, *static_prims;
    guint n_moving, n_static;
    gdouble center[3], motion[3];
    gdouble radius;
    gboolean ok;
    guint i;

    g_return_val_if_fail (LRG_IS_SOLID_WORLD (self), FALSE);
    g_return_val_if_fail (delta != NULL, FALSE);
    g_return_val_if_fail (did_hit != NULL, FALSE);
    mover = live_model (self, moving, error);
    if (mover == NULL)
        return FALSE;
    static_model = live_model (self, against, error);
    if (static_model == NULL)
        return FALSE;
    if (!filters_meet (mover, static_model))
    {
        *did_hit = FALSE;
        return TRUE;
    }
    for (i = 0; i < 3; i++)
        motion[i] = delta[i];
    if (!isfinite (motion[0]) || !isfinite (motion[1]) || !isfinite (motion[2]))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Sweep motion is not finite");
        return FALSE;
    }
    moving_prims = NULL;
    static_prims = NULL;
    if (!bake_model (mover, &moving_prims, &n_moving, error))
        return FALSE;
    if (n_moving != 1 || moving_prims[0].kind != LRG_SOLID_PRIM_SPHERE)
    {
        lrg_solid_prims_free (moving_prims, n_moving);
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                             "World sphere sweep requires a single sphere model");
        return FALSE;
    }
    if (!bake_model (static_model, &static_prims, &n_static, error))
    {
        lrg_solid_prims_free (moving_prims, n_moving);
        return FALSE;
    }
    memcpy (center, moving_prims[0].center, sizeof center);
    radius = moving_prims[0].radius;
    ok = lrg_solid_prims_sweep_sphere (static_prims, n_static, center, radius, motion,
                                       hit, did_hit, error);
    lrg_solid_prims_free (moving_prims, n_moving);
    lrg_solid_prims_free (static_prims, n_static);
    return ok;
}
