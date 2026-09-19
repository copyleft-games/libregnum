/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-prediction.h"
#include "lrg-mmo-service-private.h"
#include <math.h>
typedef struct { guint64 sequence; gdouble delta[3]; } Input;
struct _LrgMmoPrediction
{
    GObject parent_instance;
    GArray *inputs;
    guint capacity;
    guint64 sequence;
    guint64 acknowledged;
    gdouble position[3];
};
G_DEFINE_TYPE (LrgMmoPrediction, lrg_mmo_prediction, G_TYPE_OBJECT)
static void
lrg_mmo_prediction_finalize (GObject *object)
{
    g_array_unref (LRG_MMO_PREDICTION (object)->inputs);
    G_OBJECT_CLASS (lrg_mmo_prediction_parent_class)->finalize (object);
}
static void
lrg_mmo_prediction_class_init (LrgMmoPredictionClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_mmo_prediction_finalize;
}
static void
lrg_mmo_prediction_init (LrgMmoPrediction *self)
{
    self->inputs = g_array_new (FALSE, FALSE, sizeof (Input));
}
LrgMmoPrediction *
lrg_mmo_prediction_new (guint capacity)
{
    LrgMmoPrediction *self;
    g_return_val_if_fail (capacity > 0 && capacity <= 4096, NULL);
    self = g_object_new (LRG_TYPE_MMO_PREDICTION, NULL);
    self->capacity = capacity;
    return self;
}

gboolean
lrg_mmo_prediction_push (LrgMmoPrediction *self, guint64 sequence,
                         gdouble dx, gdouble dy, gdouble dz, GError **error)
{
    Input input;
    guint i;
    g_return_val_if_fail (LRG_IS_MMO_PREDICTION (self), FALSE);
    input.sequence = sequence;
    input.delta[0] = dx;
    input.delta[1] = dy;
    input.delta[2] = dz;
    if (sequence <= self->sequence || sequence == 0 || self->inputs->len >= self->capacity)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid input sequence or full prediction buffer");
    for (i = 0; i < 3; i++)
        if (!isfinite (input.delta[i]) || !isfinite (self->position[i] + input.delta[i]))
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Nonfinite predicted position");
    for (i = 0; i < 3; i++)
        self->position[i] += input.delta[i];
    self->sequence = sequence;
    g_array_append_val (self->inputs, input);
    return TRUE;
}

gboolean
lrg_mmo_prediction_reconcile (LrgMmoPrediction *self, guint64 acknowledged,
                              gdouble x, gdouble y, gdouble z, GError **error)
{
    gdouble position[3];
    guint remove = 0;
    guint i, axis;
    g_return_val_if_fail (LRG_IS_MMO_PREDICTION (self), FALSE);
    if (acknowledged < self->acknowledged || acknowledged > self->sequence ||
        !isfinite (x) || !isfinite (y) || !isfinite (z))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid reconciliation");
    position[0] = x;
    position[1] = y;
    position[2] = z;
    for (i = 0; i < self->inputs->len; i++)
    {
        Input *input = &g_array_index (self->inputs, Input, i);
        if (input->sequence <= acknowledged)
            remove++;
        else
            for (axis = 0; axis < 3; axis++)
            {
                position[axis] += input->delta[axis];
                if (!isfinite (position[axis]))
                    return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Reconciliation overflow");
            }
    }
    if (remove > 0)
        g_array_remove_range (self->inputs, 0, remove);
    memcpy (self->position, position, sizeof position);
    self->acknowledged = acknowledged;
    return TRUE;
}

GVariant *
lrg_mmo_prediction_get_position (LrgMmoPrediction *self)
{
    g_return_val_if_fail (LRG_IS_MMO_PREDICTION (self), NULL);
    return g_variant_ref_sink (g_variant_new ("(ddd)", self->position[0], self->position[1], self->position[2]));
}
