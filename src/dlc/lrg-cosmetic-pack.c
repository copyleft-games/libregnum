/* lrg-cosmetic-pack.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Cosmetic pack DLC implementation.
 */

#include "lrg-cosmetic-pack.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

struct _LrgCosmeticPack
{
    LrgDlc parent_instance;

    GPtrArray *skin_ids;
    GPtrArray *effect_ids;
    gchar *preview_image;
};

G_DEFINE_TYPE (LrgCosmeticPack, lrg_cosmetic_pack, LRG_TYPE_DLC)

enum
{
    PROP_0,
    PROP_PREVIEW_IMAGE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_cosmetic_pack_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgCosmeticPack *self = LRG_COSMETIC_PACK (object);

    switch (prop_id)
    {
    case PROP_PREVIEW_IMAGE:
        g_value_set_string (value, self->preview_image);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_cosmetic_pack_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgCosmeticPack *self = LRG_COSMETIC_PACK (object);

    switch (prop_id)
    {
    case PROP_PREVIEW_IMAGE:
        g_free (self->preview_image);
        self->preview_image = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_cosmetic_pack_dispose (GObject *object)
{
    LrgCosmeticPack *self = LRG_COSMETIC_PACK (object);

    g_clear_pointer (&self->skin_ids, g_ptr_array_unref);
    g_clear_pointer (&self->effect_ids, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_cosmetic_pack_parent_class)->dispose (object);
}

static void
lrg_cosmetic_pack_finalize (GObject *object)
{
    LrgCosmeticPack *self = LRG_COSMETIC_PACK (object);

    g_clear_pointer (&self->preview_image, g_free);

    G_OBJECT_CLASS (lrg_cosmetic_pack_parent_class)->finalize (object);
}

static void
lrg_cosmetic_pack_class_init (LrgCosmeticPackClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_cosmetic_pack_get_property;
    object_class->set_property = lrg_cosmetic_pack_set_property;
    object_class->dispose = lrg_cosmetic_pack_dispose;
    object_class->finalize = lrg_cosmetic_pack_finalize;

    properties[PROP_PREVIEW_IMAGE] =
        g_param_spec_string ("preview-image",
                             "Preview Image",
                             "Path to preview image",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_cosmetic_pack_init (LrgCosmeticPack *self)
{
    self->skin_ids = g_ptr_array_new_with_free_func (g_free);
    self->effect_ids = g_ptr_array_new_with_free_func (g_free);
}

LrgCosmeticPack *
lrg_cosmetic_pack_new (LrgModManifest *manifest,
                        const gchar    *base_path)
{
    return g_object_new (LRG_TYPE_COSMETIC_PACK,
                         "manifest", manifest,
                         "base-path", base_path,
                         "dlc-type", LRG_DLC_TYPE_COSMETIC,
                         NULL);
}

GPtrArray *
lrg_cosmetic_pack_get_skin_ids (LrgCosmeticPack *self)
{
    g_return_val_if_fail (LRG_IS_COSMETIC_PACK (self), NULL);
    return self->skin_ids;
}

void
lrg_cosmetic_pack_add_skin_id (LrgCosmeticPack *self,
                                const gchar     *skin_id)
{
    g_return_if_fail (LRG_IS_COSMETIC_PACK (self));
    g_return_if_fail (skin_id != NULL);
    g_ptr_array_add (self->skin_ids, g_strdup (skin_id));
}

GPtrArray *
lrg_cosmetic_pack_get_effect_ids (LrgCosmeticPack *self)
{
    g_return_val_if_fail (LRG_IS_COSMETIC_PACK (self), NULL);
    return self->effect_ids;
}

void
lrg_cosmetic_pack_add_effect_id (LrgCosmeticPack *self,
                                  const gchar     *effect_id)
{
    g_return_if_fail (LRG_IS_COSMETIC_PACK (self));
    g_return_if_fail (effect_id != NULL);
    g_ptr_array_add (self->effect_ids, g_strdup (effect_id));
}

const gchar *
lrg_cosmetic_pack_get_preview_image (LrgCosmeticPack *self)
{
    g_return_val_if_fail (LRG_IS_COSMETIC_PACK (self), NULL);
    return self->preview_image;
}

void
lrg_cosmetic_pack_set_preview_image (LrgCosmeticPack *self,
                                      const gchar     *path)
{
    g_return_if_fail (LRG_IS_COSMETIC_PACK (self));
    g_object_set (self, "preview-image", path, NULL);
}
