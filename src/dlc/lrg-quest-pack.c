/* lrg-quest-pack.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Quest pack DLC implementation.
 */

#include "lrg-quest-pack.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

struct _LrgQuestPack
{
    LrgDlc parent_instance;

    GPtrArray *quest_ids;
    guint estimated_hours;
};

G_DEFINE_TYPE (LrgQuestPack, lrg_quest_pack, LRG_TYPE_DLC)

enum
{
    PROP_0,
    PROP_ESTIMATED_HOURS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_quest_pack_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgQuestPack *self = LRG_QUEST_PACK (object);

    switch (prop_id)
    {
    case PROP_ESTIMATED_HOURS:
        g_value_set_uint (value, self->estimated_hours);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_quest_pack_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgQuestPack *self = LRG_QUEST_PACK (object);

    switch (prop_id)
    {
    case PROP_ESTIMATED_HOURS:
        self->estimated_hours = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_quest_pack_dispose (GObject *object)
{
    LrgQuestPack *self = LRG_QUEST_PACK (object);

    g_clear_pointer (&self->quest_ids, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_quest_pack_parent_class)->dispose (object);
}

static void
lrg_quest_pack_class_init (LrgQuestPackClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_quest_pack_get_property;
    object_class->set_property = lrg_quest_pack_set_property;
    object_class->dispose = lrg_quest_pack_dispose;

    properties[PROP_ESTIMATED_HOURS] =
        g_param_spec_uint ("estimated-hours",
                           "Estimated Hours",
                           "Estimated playtime in hours",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_quest_pack_init (LrgQuestPack *self)
{
    self->quest_ids = g_ptr_array_new_with_free_func (g_free);
}

LrgQuestPack *
lrg_quest_pack_new (LrgModManifest *manifest,
                     const gchar    *base_path)
{
    return g_object_new (LRG_TYPE_QUEST_PACK,
                         "manifest", manifest,
                         "base-path", base_path,
                         "dlc-type", LRG_DLC_TYPE_QUEST,
                         NULL);
}

GPtrArray *
lrg_quest_pack_get_quest_ids (LrgQuestPack *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_PACK (self), NULL);
    return self->quest_ids;
}

void
lrg_quest_pack_add_quest_id (LrgQuestPack *self,
                              const gchar  *quest_id)
{
    g_return_if_fail (LRG_IS_QUEST_PACK (self));
    g_return_if_fail (quest_id != NULL);
    g_ptr_array_add (self->quest_ids, g_strdup (quest_id));
}

guint
lrg_quest_pack_get_estimated_hours (LrgQuestPack *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_PACK (self), 0);
    return self->estimated_hours;
}

void
lrg_quest_pack_set_estimated_hours (LrgQuestPack *self,
                                     guint         hours)
{
    g_return_if_fail (LRG_IS_QUEST_PACK (self));
    g_object_set (self, "estimated-hours", hours, NULL);
}
