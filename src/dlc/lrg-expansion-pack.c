/* lrg-expansion-pack.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Expansion pack DLC implementation.
 */

#include "lrg-expansion-pack.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

struct _LrgExpansionPack
{
    LrgDlc parent_instance;

    gchar *campaign_name;
    guint level_cap_increase;
    GPtrArray *new_areas;
};

G_DEFINE_TYPE (LrgExpansionPack, lrg_expansion_pack, LRG_TYPE_DLC)

enum
{
    PROP_0,
    PROP_CAMPAIGN_NAME,
    PROP_LEVEL_CAP_INCREASE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_expansion_pack_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgExpansionPack *self = LRG_EXPANSION_PACK (object);

    switch (prop_id)
    {
    case PROP_CAMPAIGN_NAME:
        g_value_set_string (value, self->campaign_name);
        break;
    case PROP_LEVEL_CAP_INCREASE:
        g_value_set_uint (value, self->level_cap_increase);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_expansion_pack_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgExpansionPack *self = LRG_EXPANSION_PACK (object);

    switch (prop_id)
    {
    case PROP_CAMPAIGN_NAME:
        g_free (self->campaign_name);
        self->campaign_name = g_value_dup_string (value);
        break;
    case PROP_LEVEL_CAP_INCREASE:
        self->level_cap_increase = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_expansion_pack_dispose (GObject *object)
{
    LrgExpansionPack *self = LRG_EXPANSION_PACK (object);

    g_clear_pointer (&self->new_areas, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_expansion_pack_parent_class)->dispose (object);
}

static void
lrg_expansion_pack_finalize (GObject *object)
{
    LrgExpansionPack *self = LRG_EXPANSION_PACK (object);

    g_clear_pointer (&self->campaign_name, g_free);

    G_OBJECT_CLASS (lrg_expansion_pack_parent_class)->finalize (object);
}

static void
lrg_expansion_pack_class_init (LrgExpansionPackClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_expansion_pack_get_property;
    object_class->set_property = lrg_expansion_pack_set_property;
    object_class->dispose = lrg_expansion_pack_dispose;
    object_class->finalize = lrg_expansion_pack_finalize;

    properties[PROP_CAMPAIGN_NAME] =
        g_param_spec_string ("campaign-name",
                             "Campaign Name",
                             "Main campaign/storyline name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LEVEL_CAP_INCREASE] =
        g_param_spec_uint ("level-cap-increase",
                           "Level Cap Increase",
                           "Level cap increase amount",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_expansion_pack_init (LrgExpansionPack *self)
{
    self->new_areas = g_ptr_array_new_with_free_func (g_free);
}

LrgExpansionPack *
lrg_expansion_pack_new (LrgModManifest *manifest,
                         const gchar    *base_path)
{
    return g_object_new (LRG_TYPE_EXPANSION_PACK,
                         "manifest", manifest,
                         "base-path", base_path,
                         "dlc-type", LRG_DLC_TYPE_EXPANSION,
                         NULL);
}

const gchar *
lrg_expansion_pack_get_campaign_name (LrgExpansionPack *self)
{
    g_return_val_if_fail (LRG_IS_EXPANSION_PACK (self), NULL);
    return self->campaign_name;
}

void
lrg_expansion_pack_set_campaign_name (LrgExpansionPack *self,
                                       const gchar      *name)
{
    g_return_if_fail (LRG_IS_EXPANSION_PACK (self));
    g_object_set (self, "campaign-name", name, NULL);
}

guint
lrg_expansion_pack_get_level_cap_increase (LrgExpansionPack *self)
{
    g_return_val_if_fail (LRG_IS_EXPANSION_PACK (self), 0);
    return self->level_cap_increase;
}

void
lrg_expansion_pack_set_level_cap_increase (LrgExpansionPack *self,
                                            guint             increase)
{
    g_return_if_fail (LRG_IS_EXPANSION_PACK (self));
    g_object_set (self, "level-cap-increase", increase, NULL);
}

GPtrArray *
lrg_expansion_pack_get_new_areas (LrgExpansionPack *self)
{
    g_return_val_if_fail (LRG_IS_EXPANSION_PACK (self), NULL);
    return self->new_areas;
}

void
lrg_expansion_pack_add_new_area (LrgExpansionPack *self,
                                  const gchar      *area_id)
{
    g_return_if_fail (LRG_IS_EXPANSION_PACK (self));
    g_return_if_fail (area_id != NULL);
    g_ptr_array_add (self->new_areas, g_strdup (area_id));
}
