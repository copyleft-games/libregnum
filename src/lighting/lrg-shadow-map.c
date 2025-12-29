/* lrg-shadow-map.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shadow map implementation.
 */

#include "lrg-shadow-map.h"
#include "../lrg-log.h"
#include <math.h>

struct _LrgShadowMap
{
    GObject parent_instance;

    guint width;
    guint height;
    guint texture_id;
    guint8 *data;
};

G_DEFINE_FINAL_TYPE (LrgShadowMap, lrg_shadow_map, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_shadow_map_finalize (GObject *object)
{
    LrgShadowMap *self = LRG_SHADOW_MAP (object);

    g_clear_pointer (&self->data, g_free);

    G_OBJECT_CLASS (lrg_shadow_map_parent_class)->finalize (object);
}

static void
lrg_shadow_map_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    LrgShadowMap *self = LRG_SHADOW_MAP (object);
    switch (prop_id)
    {
    case PROP_WIDTH: g_value_set_uint (value, self->width); break;
    case PROP_HEIGHT: g_value_set_uint (value, self->height); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_shadow_map_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    LrgShadowMap *self = LRG_SHADOW_MAP (object);
    switch (prop_id)
    {
    case PROP_WIDTH: self->width = g_value_get_uint (value); break;
    case PROP_HEIGHT: self->height = g_value_get_uint (value); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_shadow_map_class_init (LrgShadowMapClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_shadow_map_finalize;
    object_class->get_property = lrg_shadow_map_get_property;
    object_class->set_property = lrg_shadow_map_set_property;

    properties[PROP_WIDTH] = g_param_spec_uint ("width", "Width", "Shadow map width", 1, 8192, 512, G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);
    properties[PROP_HEIGHT] = g_param_spec_uint ("height", "Height", "Shadow map height", 1, 8192, 512, G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_shadow_map_init (LrgShadowMap *self)
{
    self->width = 512;
    self->height = 512;
    self->texture_id = 0;
    self->data = NULL;
}

/* Public API */

LrgShadowMap *
lrg_shadow_map_new (guint width, guint height)
{
    LrgShadowMap *self = g_object_new (LRG_TYPE_SHADOW_MAP,
                                       "width", width,
                                       "height", height,
                                       NULL);
    self->data = g_new0 (guint8, width * height);
    return self;
}

guint lrg_shadow_map_get_width (LrgShadowMap *self) { g_return_val_if_fail (LRG_IS_SHADOW_MAP (self), 0); return self->width; }
guint lrg_shadow_map_get_height (LrgShadowMap *self) { g_return_val_if_fail (LRG_IS_SHADOW_MAP (self), 0); return self->height; }

void
lrg_shadow_map_resize (LrgShadowMap *self,
                       guint         width,
                       guint         height)
{
    g_return_if_fail (LRG_IS_SHADOW_MAP (self));

    if (self->width == width && self->height == height)
        return;

    self->width = width;
    self->height = height;
    g_clear_pointer (&self->data, g_free);
    self->data = g_new0 (guint8, width * height);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
}

void
lrg_shadow_map_clear (LrgShadowMap *self)
{
    g_return_if_fail (LRG_IS_SHADOW_MAP (self));

    if (self->data != NULL)
        memset (self->data, 0, self->width * self->height);
}

void
lrg_shadow_map_render_shadows (LrgShadowMap *self,
                               gfloat        light_x,
                               gfloat        light_y,
                               GPtrArray    *casters)
{
    guint i;

    g_return_if_fail (LRG_IS_SHADOW_MAP (self));
    g_return_if_fail (casters != NULL);

    lrg_shadow_map_clear (self);

    for (i = 0; i < casters->len; i++)
    {
        LrgShadowCaster *caster = g_ptr_array_index (casters, i);
        GPtrArray *edges = lrg_shadow_caster_get_edges (caster);
        guint j;

        for (j = 0; j < edges->len; j++)
        {
            LrgShadowEdge *edge = g_ptr_array_index (edges, j);

            /*
             * Shadow volume calculation:
             * 1. Calculate vectors from light to edge vertices
             * 2. Project rays to edge of shadow map
             * 3. Fill shadow polygon
             */
            (void)light_x;
            (void)light_y;
            (void)edge;
        }

        g_ptr_array_unref (edges);
    }
}

guint
lrg_shadow_map_get_texture_id (LrgShadowMap *self)
{
    g_return_val_if_fail (LRG_IS_SHADOW_MAP (self), 0);
    return self->texture_id;
}
