/* lrg-character-pack.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Character pack DLC implementation.
 */

#include "lrg-character-pack.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

struct _LrgCharacterPack
{
    LrgDlc parent_instance;

    GPtrArray *character_ids;
    gboolean is_playable;
    gboolean is_companion;
};

G_DEFINE_TYPE (LrgCharacterPack, lrg_character_pack, LRG_TYPE_DLC)

enum
{
    PROP_0,
    PROP_IS_PLAYABLE,
    PROP_IS_COMPANION,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_character_pack_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgCharacterPack *self = LRG_CHARACTER_PACK (object);

    switch (prop_id)
    {
    case PROP_IS_PLAYABLE:
        g_value_set_boolean (value, self->is_playable);
        break;
    case PROP_IS_COMPANION:
        g_value_set_boolean (value, self->is_companion);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_character_pack_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgCharacterPack *self = LRG_CHARACTER_PACK (object);

    switch (prop_id)
    {
    case PROP_IS_PLAYABLE:
        self->is_playable = g_value_get_boolean (value);
        break;
    case PROP_IS_COMPANION:
        self->is_companion = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_character_pack_dispose (GObject *object)
{
    LrgCharacterPack *self = LRG_CHARACTER_PACK (object);

    g_clear_pointer (&self->character_ids, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_character_pack_parent_class)->dispose (object);
}

static void
lrg_character_pack_class_init (LrgCharacterPackClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_character_pack_get_property;
    object_class->set_property = lrg_character_pack_set_property;
    object_class->dispose = lrg_character_pack_dispose;

    properties[PROP_IS_PLAYABLE] =
        g_param_spec_boolean ("is-playable",
                              "Is Playable",
                              "Whether characters are playable",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_IS_COMPANION] =
        g_param_spec_boolean ("is-companion",
                              "Is Companion",
                              "Whether characters are companions",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_character_pack_init (LrgCharacterPack *self)
{
    self->character_ids = g_ptr_array_new_with_free_func (g_free);
}

LrgCharacterPack *
lrg_character_pack_new (LrgModManifest *manifest,
                         const gchar    *base_path)
{
    return g_object_new (LRG_TYPE_CHARACTER_PACK,
                         "manifest", manifest,
                         "base-path", base_path,
                         "dlc-type", LRG_DLC_TYPE_CHARACTER,
                         NULL);
}

GPtrArray *
lrg_character_pack_get_character_ids (LrgCharacterPack *self)
{
    g_return_val_if_fail (LRG_IS_CHARACTER_PACK (self), NULL);
    return self->character_ids;
}

void
lrg_character_pack_add_character_id (LrgCharacterPack *self,
                                      const gchar      *character_id)
{
    g_return_if_fail (LRG_IS_CHARACTER_PACK (self));
    g_return_if_fail (character_id != NULL);
    g_ptr_array_add (self->character_ids, g_strdup (character_id));
}

gboolean
lrg_character_pack_get_is_playable (LrgCharacterPack *self)
{
    g_return_val_if_fail (LRG_IS_CHARACTER_PACK (self), FALSE);
    return self->is_playable;
}

void
lrg_character_pack_set_is_playable (LrgCharacterPack *self,
                                     gboolean          is_playable)
{
    g_return_if_fail (LRG_IS_CHARACTER_PACK (self));
    g_object_set (self, "is-playable", is_playable, NULL);
}

gboolean
lrg_character_pack_get_is_companion (LrgCharacterPack *self)
{
    g_return_val_if_fail (LRG_IS_CHARACTER_PACK (self), FALSE);
    return self->is_companion;
}

void
lrg_character_pack_set_is_companion (LrgCharacterPack *self,
                                      gboolean          is_companion)
{
    g_return_if_fail (LRG_IS_CHARACTER_PACK (self));
    g_object_set (self, "is-companion", is_companion, NULL);
}
