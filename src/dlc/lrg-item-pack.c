/* lrg-item-pack.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Item pack DLC implementation.
 */

#include "lrg-item-pack.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

struct _LrgItemPack
{
    LrgDlc parent_instance;

    GPtrArray *item_ids;
    GPtrArray *equipment_slots;
};

G_DEFINE_TYPE (LrgItemPack, lrg_item_pack, LRG_TYPE_DLC)

static void
lrg_item_pack_dispose (GObject *object)
{
    LrgItemPack *self = LRG_ITEM_PACK (object);

    g_clear_pointer (&self->item_ids, g_ptr_array_unref);
    g_clear_pointer (&self->equipment_slots, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_item_pack_parent_class)->dispose (object);
}

static void
lrg_item_pack_class_init (LrgItemPackClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_item_pack_dispose;
}

static void
lrg_item_pack_init (LrgItemPack *self)
{
    self->item_ids = g_ptr_array_new_with_free_func (g_free);
    self->equipment_slots = g_ptr_array_new_with_free_func (g_free);
}

LrgItemPack *
lrg_item_pack_new (LrgModManifest *manifest,
                    const gchar    *base_path)
{
    return g_object_new (LRG_TYPE_ITEM_PACK,
                         "manifest", manifest,
                         "base-path", base_path,
                         "dlc-type", LRG_DLC_TYPE_ITEM,
                         NULL);
}

GPtrArray *
lrg_item_pack_get_item_ids (LrgItemPack *self)
{
    g_return_val_if_fail (LRG_IS_ITEM_PACK (self), NULL);
    return self->item_ids;
}

void
lrg_item_pack_add_item_id (LrgItemPack *self,
                            const gchar *item_id)
{
    g_return_if_fail (LRG_IS_ITEM_PACK (self));
    g_return_if_fail (item_id != NULL);
    g_ptr_array_add (self->item_ids, g_strdup (item_id));
}

GPtrArray *
lrg_item_pack_get_equipment_slots (LrgItemPack *self)
{
    g_return_val_if_fail (LRG_IS_ITEM_PACK (self), NULL);
    return self->equipment_slots;
}

void
lrg_item_pack_add_equipment_slot (LrgItemPack *self,
                                   const gchar *slot)
{
    g_return_if_fail (LRG_IS_ITEM_PACK (self));
    g_return_if_fail (slot != NULL);
    g_ptr_array_add (self->equipment_slots, g_strdup (slot));
}
