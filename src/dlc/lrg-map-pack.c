/* lrg-map-pack.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Map pack DLC implementation.
 */

#include "lrg-map-pack.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

struct _LrgMapPack
{
    LrgDlc parent_instance;

    GPtrArray *map_ids;
    gchar *biome_type;
};

G_DEFINE_TYPE (LrgMapPack, lrg_map_pack, LRG_TYPE_DLC)

enum
{
    PROP_0,
    PROP_BIOME_TYPE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_map_pack_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgMapPack *self = LRG_MAP_PACK (object);

    switch (prop_id)
    {
    case PROP_BIOME_TYPE:
        g_value_set_string (value, self->biome_type);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_map_pack_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgMapPack *self = LRG_MAP_PACK (object);

    switch (prop_id)
    {
    case PROP_BIOME_TYPE:
        g_free (self->biome_type);
        self->biome_type = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_map_pack_dispose (GObject *object)
{
    LrgMapPack *self = LRG_MAP_PACK (object);

    g_clear_pointer (&self->map_ids, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_map_pack_parent_class)->dispose (object);
}

static void
lrg_map_pack_finalize (GObject *object)
{
    LrgMapPack *self = LRG_MAP_PACK (object);

    g_clear_pointer (&self->biome_type, g_free);

    G_OBJECT_CLASS (lrg_map_pack_parent_class)->finalize (object);
}

static void
lrg_map_pack_class_init (LrgMapPackClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_map_pack_get_property;
    object_class->set_property = lrg_map_pack_set_property;
    object_class->dispose = lrg_map_pack_dispose;
    object_class->finalize = lrg_map_pack_finalize;

    properties[PROP_BIOME_TYPE] =
        g_param_spec_string ("biome-type",
                             "Biome Type",
                             "Biome/theme type of maps",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_map_pack_init (LrgMapPack *self)
{
    self->map_ids = g_ptr_array_new_with_free_func (g_free);
}

LrgMapPack *
lrg_map_pack_new (LrgModManifest *manifest,
                   const gchar    *base_path)
{
    return g_object_new (LRG_TYPE_MAP_PACK,
                         "manifest", manifest,
                         "base-path", base_path,
                         "dlc-type", LRG_DLC_TYPE_MAP,
                         NULL);
}

GPtrArray *
lrg_map_pack_get_map_ids (LrgMapPack *self)
{
    g_return_val_if_fail (LRG_IS_MAP_PACK (self), NULL);
    return self->map_ids;
}

void
lrg_map_pack_add_map_id (LrgMapPack  *self,
                          const gchar *map_id)
{
    g_return_if_fail (LRG_IS_MAP_PACK (self));
    g_return_if_fail (map_id != NULL);
    g_ptr_array_add (self->map_ids, g_strdup (map_id));
}

const gchar *
lrg_map_pack_get_biome_type (LrgMapPack *self)
{
    g_return_val_if_fail (LRG_IS_MAP_PACK (self), NULL);
    return self->biome_type;
}

void
lrg_map_pack_set_biome_type (LrgMapPack  *self,
                              const gchar *biome_type)
{
    g_return_if_fail (LRG_IS_MAP_PACK (self));
    g_object_set (self, "biome-type", biome_type, NULL);
}
