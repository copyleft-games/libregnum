/* lrg-collectible-def.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base definition for everything a character can collect. The kind is
 * construct-only; the mount and pet subclasses pin it to MOUNT / PET in
 * constructed() so a subclass instance can never masquerade as another kind.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "collection/lrg-collectible-def.h"
#include "collection/lrg-mount-def.h"
#include "collection/lrg-pet-def.h"

typedef struct
{
    gchar                *id;
    gchar                *name;
    gchar                *description;
    gchar                *icon;
    LrgCollectibleKind    kind;
    LrgCollectibleRarity  rarity;
    gchar                *source;
    gchar                *model;
    guint                 required_level;
} LrgCollectibleDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgCollectibleDef, lrg_collectible_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_ICON,
    PROP_KIND,
    PROP_RARITY,
    PROP_SOURCE,
    PROP_MODEL,
    PROP_REQUIRED_LEVEL,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* replace_string:
 * Swaps *slot for a copy of @value and notifies @prop on @self, but only
 * when the content actually changed. */
static void
replace_string (LrgCollectibleDef *self,
                gchar            **slot,
                const gchar       *value,
                guint              prop)
{
    if (g_strcmp0 (*slot, value) == 0)
        return;

    g_free (*slot);
    *slot = g_strdup (value);
    g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static gboolean
lrg_collectible_def_real_can_obtain (LrgCollectibleDef  *self,
                                     guint               level,
                                     GError            **error)
{
    LrgCollectibleDefPrivate *priv = lrg_collectible_def_get_instance_private (self);

    if (level < priv->required_level)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Collectible '%s' requires level %u (have %u)",
                     priv->id != NULL ? priv->id : "",
                     priv->required_level, level);
        return FALSE;
    }

    return TRUE;
}

static void
lrg_collectible_def_constructed (GObject *object)
{
    LrgCollectibleDef        *self = LRG_COLLECTIBLE_DEF (object);
    LrgCollectibleDefPrivate *priv = lrg_collectible_def_get_instance_private (self);

    G_OBJECT_CLASS (lrg_collectible_def_parent_class)->constructed (object);

    /* Subclasses have a fixed kind; whatever was passed at construction
     * (including the base default) is overridden. */
    if (LRG_IS_MOUNT_DEF (self))
        priv->kind = LRG_COLLECTIBLE_KIND_MOUNT;
    else if (LRG_IS_PET_DEF (self))
        priv->kind = LRG_COLLECTIBLE_KIND_PET;
}

static void
lrg_collectible_def_finalize (GObject *object)
{
    LrgCollectibleDef        *self = LRG_COLLECTIBLE_DEF (object);
    LrgCollectibleDefPrivate *priv = lrg_collectible_def_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->icon, g_free);
    g_clear_pointer (&priv->source, g_free);
    g_clear_pointer (&priv->model, g_free);

    G_OBJECT_CLASS (lrg_collectible_def_parent_class)->finalize (object);
}

static void
lrg_collectible_def_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgCollectibleDef        *self = LRG_COLLECTIBLE_DEF (object);
    LrgCollectibleDefPrivate *priv = lrg_collectible_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, priv->id);
        break;
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, priv->description);
        break;
    case PROP_ICON:
        g_value_set_string (value, priv->icon);
        break;
    case PROP_KIND:
        g_value_set_enum (value, (gint)priv->kind);
        break;
    case PROP_RARITY:
        g_value_set_enum (value, (gint)priv->rarity);
        break;
    case PROP_SOURCE:
        g_value_set_string (value, priv->source);
        break;
    case PROP_MODEL:
        g_value_set_string (value, priv->model);
        break;
    case PROP_REQUIRED_LEVEL:
        g_value_set_uint (value, priv->required_level);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_collectible_def_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgCollectibleDef        *self = LRG_COLLECTIBLE_DEF (object);
    LrgCollectibleDefPrivate *priv = lrg_collectible_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (priv->id);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        lrg_collectible_def_set_name (self, g_value_get_string (value));
        break;
    case PROP_DESCRIPTION:
        lrg_collectible_def_set_description (self, g_value_get_string (value));
        break;
    case PROP_ICON:
        lrg_collectible_def_set_icon (self, g_value_get_string (value));
        break;
    case PROP_KIND:
        priv->kind = (LrgCollectibleKind)g_value_get_enum (value);
        break;
    case PROP_RARITY:
        lrg_collectible_def_set_rarity (self, (LrgCollectibleRarity)g_value_get_enum (value));
        break;
    case PROP_SOURCE:
        lrg_collectible_def_set_source (self, g_value_get_string (value));
        break;
    case PROP_MODEL:
        lrg_collectible_def_set_model (self, g_value_get_string (value));
        break;
    case PROP_REQUIRED_LEVEL:
        lrg_collectible_def_set_required_level (self, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_collectible_def_class_init (LrgCollectibleDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->constructed = lrg_collectible_def_constructed;
    object_class->finalize = lrg_collectible_def_finalize;
    object_class->get_property = lrg_collectible_def_get_property;
    object_class->set_property = lrg_collectible_def_set_property;

    klass->can_obtain = lrg_collectible_def_real_can_obtain;

    /**
     * LrgCollectibleDef:id:
     *
     * Unique identifier (construct-only).
     */
    properties[PROP_ID] =
        g_param_spec_string ("id", "ID", "Collectible identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCollectibleDef:name:
     *
     * Display name.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name", "Name", "Display name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCollectibleDef:description:
     *
     * Description text.
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description", "Description", "Description text",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCollectibleDef:icon:
     *
     * Icon key.
     */
    properties[PROP_ICON] =
        g_param_spec_string ("icon", "Icon", "Icon key",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCollectibleDef:kind:
     *
     * Collectible kind (construct-only). Pinned to MOUNT for #LrgMountDef
     * and PET for #LrgPetDef.
     */
    properties[PROP_KIND] =
        g_param_spec_enum ("kind", "Kind", "Collectible kind",
                           LRG_TYPE_COLLECTIBLE_KIND,
                           LRG_COLLECTIBLE_KIND_TOY,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgCollectibleDef:rarity:
     *
     * Rarity tier.
     */
    properties[PROP_RARITY] =
        g_param_spec_enum ("rarity", "Rarity", "Rarity tier",
                           LRG_TYPE_COLLECTIBLE_RARITY,
                           LRG_COLLECTIBLE_RARITY_COMMON,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgCollectibleDef:source:
     *
     * Human readable acquisition source text.
     */
    properties[PROP_SOURCE] =
        g_param_spec_string ("source", "Source", "Acquisition source text",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCollectibleDef:model:
     *
     * Model / visual key used by the client renderer.
     */
    properties[PROP_MODEL] =
        g_param_spec_string ("model", "Model", "Model or visual key",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCollectibleDef:required-level:
     *
     * Minimum character level.
     */
    properties[PROP_REQUIRED_LEVEL] =
        g_param_spec_uint ("required-level", "Required Level",
                           "Minimum character level",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_collectible_def_init (LrgCollectibleDef *self)
{
    LrgCollectibleDefPrivate *priv = lrg_collectible_def_get_instance_private (self);

    priv->kind = LRG_COLLECTIBLE_KIND_TOY;
    priv->rarity = LRG_COLLECTIBLE_RARITY_COMMON;
}

LrgCollectibleDef *
lrg_collectible_def_new (const gchar        *id,
                         LrgCollectibleKind  kind)
{
    g_return_val_if_fail (id != NULL && *id != '\0', NULL);

    return g_object_new (LRG_TYPE_COLLECTIBLE_DEF,
                         "id", id,
                         "kind", kind,
                         NULL);
}

const gchar *
lrg_collectible_def_get_id (LrgCollectibleDef *self)
{
    LrgCollectibleDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_COLLECTIBLE_DEF (self), NULL);
    priv = lrg_collectible_def_get_instance_private (self);
    return priv->id;
}

LrgCollectibleKind
lrg_collectible_def_get_kind (LrgCollectibleDef *self)
{
    LrgCollectibleDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_COLLECTIBLE_DEF (self), LRG_COLLECTIBLE_KIND_TOY);
    priv = lrg_collectible_def_get_instance_private (self);
    return priv->kind;
}

const gchar *
lrg_collectible_def_get_name (LrgCollectibleDef *self)
{
    LrgCollectibleDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_COLLECTIBLE_DEF (self), NULL);
    priv = lrg_collectible_def_get_instance_private (self);
    return priv->name;
}

void
lrg_collectible_def_set_name (LrgCollectibleDef *self,
                              const gchar       *name)
{
    LrgCollectibleDefPrivate *priv;

    g_return_if_fail (LRG_IS_COLLECTIBLE_DEF (self));
    priv = lrg_collectible_def_get_instance_private (self);
    replace_string (self, &priv->name, name, PROP_NAME);
}

const gchar *
lrg_collectible_def_get_description (LrgCollectibleDef *self)
{
    LrgCollectibleDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_COLLECTIBLE_DEF (self), NULL);
    priv = lrg_collectible_def_get_instance_private (self);
    return priv->description;
}

void
lrg_collectible_def_set_description (LrgCollectibleDef *self,
                                     const gchar       *description)
{
    LrgCollectibleDefPrivate *priv;

    g_return_if_fail (LRG_IS_COLLECTIBLE_DEF (self));
    priv = lrg_collectible_def_get_instance_private (self);
    replace_string (self, &priv->description, description, PROP_DESCRIPTION);
}

const gchar *
lrg_collectible_def_get_icon (LrgCollectibleDef *self)
{
    LrgCollectibleDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_COLLECTIBLE_DEF (self), NULL);
    priv = lrg_collectible_def_get_instance_private (self);
    return priv->icon;
}

void
lrg_collectible_def_set_icon (LrgCollectibleDef *self,
                              const gchar       *icon)
{
    LrgCollectibleDefPrivate *priv;

    g_return_if_fail (LRG_IS_COLLECTIBLE_DEF (self));
    priv = lrg_collectible_def_get_instance_private (self);
    replace_string (self, &priv->icon, icon, PROP_ICON);
}

LrgCollectibleRarity
lrg_collectible_def_get_rarity (LrgCollectibleDef *self)
{
    LrgCollectibleDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_COLLECTIBLE_DEF (self), LRG_COLLECTIBLE_RARITY_COMMON);
    priv = lrg_collectible_def_get_instance_private (self);
    return priv->rarity;
}

void
lrg_collectible_def_set_rarity (LrgCollectibleDef    *self,
                                LrgCollectibleRarity  rarity)
{
    LrgCollectibleDefPrivate *priv;

    g_return_if_fail (LRG_IS_COLLECTIBLE_DEF (self));
    g_return_if_fail ((guint)rarity <= (guint)LRG_COLLECTIBLE_RARITY_LEGENDARY);
    priv = lrg_collectible_def_get_instance_private (self);

    if (priv->rarity == rarity)
        return;

    priv->rarity = rarity;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RARITY]);
}

const gchar *
lrg_collectible_def_get_source (LrgCollectibleDef *self)
{
    LrgCollectibleDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_COLLECTIBLE_DEF (self), NULL);
    priv = lrg_collectible_def_get_instance_private (self);
    return priv->source;
}

void
lrg_collectible_def_set_source (LrgCollectibleDef *self,
                                const gchar       *source)
{
    LrgCollectibleDefPrivate *priv;

    g_return_if_fail (LRG_IS_COLLECTIBLE_DEF (self));
    priv = lrg_collectible_def_get_instance_private (self);
    replace_string (self, &priv->source, source, PROP_SOURCE);
}

const gchar *
lrg_collectible_def_get_model (LrgCollectibleDef *self)
{
    LrgCollectibleDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_COLLECTIBLE_DEF (self), NULL);
    priv = lrg_collectible_def_get_instance_private (self);
    return priv->model;
}

void
lrg_collectible_def_set_model (LrgCollectibleDef *self,
                               const gchar       *model)
{
    LrgCollectibleDefPrivate *priv;

    g_return_if_fail (LRG_IS_COLLECTIBLE_DEF (self));
    priv = lrg_collectible_def_get_instance_private (self);
    replace_string (self, &priv->model, model, PROP_MODEL);
}

guint
lrg_collectible_def_get_required_level (LrgCollectibleDef *self)
{
    LrgCollectibleDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_COLLECTIBLE_DEF (self), 0);
    priv = lrg_collectible_def_get_instance_private (self);
    return priv->required_level;
}

void
lrg_collectible_def_set_required_level (LrgCollectibleDef *self,
                                        guint              level)
{
    LrgCollectibleDefPrivate *priv;

    g_return_if_fail (LRG_IS_COLLECTIBLE_DEF (self));
    priv = lrg_collectible_def_get_instance_private (self);

    if (priv->required_level == level)
        return;

    priv->required_level = level;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_REQUIRED_LEVEL]);
}

gboolean
lrg_collectible_def_can_obtain (LrgCollectibleDef  *self,
                                guint               level,
                                GError            **error)
{
    LrgCollectibleDefClass *klass;

    g_return_val_if_fail (LRG_IS_COLLECTIBLE_DEF (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    klass = LRG_COLLECTIBLE_DEF_GET_CLASS (self);
    if (klass->can_obtain == NULL)
        return TRUE;

    return klass->can_obtain (self, level, error);
}
