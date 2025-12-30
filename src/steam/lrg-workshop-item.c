/* lrg-workshop-item.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWorkshopItem - Steam Workshop item wrapper.
 */

#include "config.h"

#include "lrg-workshop-item.h"
#include "lrg-steam-types.h"
#include "../lrg-log.h"

struct _LrgWorkshopItem
{
    GObject parent_instance;

    /* Identity */
    guint64 file_id;

    /* Metadata */
    gchar *title;
    gchar *description;
    guint64 owner_id;
    guint32 time_created;
    guint32 time_updated;
    LrgWorkshopItemVisibility visibility;

    /* Tags */
    GPtrArray *tags;

    /* Stats */
    guint32 votes_up;
    guint32 votes_down;
    gfloat score;

    /* State */
    LrgWorkshopItemState state;
    gboolean banned;

    /* Install info */
    gchar *install_path;
    guint64 file_size;

    /* Preview */
    gchar *preview_url;
};

enum
{
    PROP_0,
    PROP_FILE_ID,
    PROP_TITLE,
    PROP_DESCRIPTION,
    PROP_OWNER_ID,
    PROP_TIME_CREATED,
    PROP_TIME_UPDATED,
    PROP_VISIBILITY,
    PROP_VOTES_UP,
    PROP_VOTES_DOWN,
    PROP_SCORE,
    PROP_STATE,
    PROP_BANNED,
    PROP_INSTALL_PATH,
    PROP_FILE_SIZE,
    PROP_PREVIEW_URL,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgWorkshopItem, lrg_workshop_item, G_TYPE_OBJECT)

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_workshop_item_finalize (GObject *object)
{
    LrgWorkshopItem *self = LRG_WORKSHOP_ITEM (object);

    g_clear_pointer (&self->title, g_free);
    g_clear_pointer (&self->description, g_free);
    g_clear_pointer (&self->install_path, g_free);
    g_clear_pointer (&self->preview_url, g_free);
    g_clear_pointer (&self->tags, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_workshop_item_parent_class)->finalize (object);
}

static void
lrg_workshop_item_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgWorkshopItem *self = LRG_WORKSHOP_ITEM (object);

    switch (prop_id)
    {
    case PROP_FILE_ID:
        g_value_set_uint64 (value, self->file_id);
        break;
    case PROP_TITLE:
        g_value_set_string (value, self->title);
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, self->description);
        break;
    case PROP_OWNER_ID:
        g_value_set_uint64 (value, self->owner_id);
        break;
    case PROP_TIME_CREATED:
        g_value_set_uint (value, self->time_created);
        break;
    case PROP_TIME_UPDATED:
        g_value_set_uint (value, self->time_updated);
        break;
    case PROP_VISIBILITY:
        g_value_set_int (value, self->visibility);
        break;
    case PROP_VOTES_UP:
        g_value_set_uint (value, self->votes_up);
        break;
    case PROP_VOTES_DOWN:
        g_value_set_uint (value, self->votes_down);
        break;
    case PROP_SCORE:
        g_value_set_float (value, self->score);
        break;
    case PROP_STATE:
        g_value_set_uint (value, self->state);
        break;
    case PROP_BANNED:
        g_value_set_boolean (value, self->banned);
        break;
    case PROP_INSTALL_PATH:
        g_value_set_string (value, self->install_path);
        break;
    case PROP_FILE_SIZE:
        g_value_set_uint64 (value, self->file_size);
        break;
    case PROP_PREVIEW_URL:
        g_value_set_string (value, self->preview_url);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_workshop_item_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgWorkshopItem *self = LRG_WORKSHOP_ITEM (object);

    switch (prop_id)
    {
    case PROP_FILE_ID:
        self->file_id = g_value_get_uint64 (value);
        break;
    case PROP_TITLE:
        lrg_workshop_item_set_title (self, g_value_get_string (value));
        break;
    case PROP_DESCRIPTION:
        lrg_workshop_item_set_description (self, g_value_get_string (value));
        break;
    case PROP_OWNER_ID:
        self->owner_id = g_value_get_uint64 (value);
        break;
    case PROP_TIME_CREATED:
        self->time_created = g_value_get_uint (value);
        break;
    case PROP_TIME_UPDATED:
        self->time_updated = g_value_get_uint (value);
        break;
    case PROP_VISIBILITY:
        self->visibility = g_value_get_int (value);
        break;
    case PROP_VOTES_UP:
        self->votes_up = g_value_get_uint (value);
        break;
    case PROP_VOTES_DOWN:
        self->votes_down = g_value_get_uint (value);
        break;
    case PROP_SCORE:
        self->score = g_value_get_float (value);
        break;
    case PROP_STATE:
        self->state = g_value_get_uint (value);
        break;
    case PROP_BANNED:
        self->banned = g_value_get_boolean (value);
        break;
    case PROP_INSTALL_PATH:
        g_free (self->install_path);
        self->install_path = g_value_dup_string (value);
        break;
    case PROP_FILE_SIZE:
        self->file_size = g_value_get_uint64 (value);
        break;
    case PROP_PREVIEW_URL:
        g_free (self->preview_url);
        self->preview_url = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_workshop_item_class_init (LrgWorkshopItemClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_workshop_item_finalize;
    object_class->get_property = lrg_workshop_item_get_property;
    object_class->set_property = lrg_workshop_item_set_property;

    properties[PROP_FILE_ID] =
        g_param_spec_uint64 ("file-id",
                             "File ID",
                             "Workshop file ID",
                             0, G_MAXUINT64, 0,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_TITLE] =
        g_param_spec_string ("title",
                             "Title",
                             "Item title",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Item description",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_OWNER_ID] =
        g_param_spec_uint64 ("owner-id",
                             "Owner ID",
                             "Steam ID of owner",
                             0, G_MAXUINT64, 0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_TIME_CREATED] =
        g_param_spec_uint ("time-created",
                           "Time Created",
                           "Unix timestamp of creation",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    properties[PROP_TIME_UPDATED] =
        g_param_spec_uint ("time-updated",
                           "Time Updated",
                           "Unix timestamp of last update",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    properties[PROP_VISIBILITY] =
        g_param_spec_int ("visibility",
                          "Visibility",
                          "Item visibility setting",
                          LRG_WORKSHOP_ITEM_VISIBILITY_PUBLIC,
                          LRG_WORKSHOP_ITEM_VISIBILITY_UNLISTED,
                          LRG_WORKSHOP_ITEM_VISIBILITY_PUBLIC,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_VOTES_UP] =
        g_param_spec_uint ("votes-up",
                           "Votes Up",
                           "Number of upvotes",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    properties[PROP_VOTES_DOWN] =
        g_param_spec_uint ("votes-down",
                           "Votes Down",
                           "Number of downvotes",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    properties[PROP_SCORE] =
        g_param_spec_float ("score",
                            "Score",
                            "Item score (0.0 to 1.0)",
                            0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_STATE] =
        g_param_spec_uint ("state",
                           "State",
                           "Item state flags",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    properties[PROP_BANNED] =
        g_param_spec_boolean ("banned",
                              "Banned",
                              "Whether item is banned",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    properties[PROP_INSTALL_PATH] =
        g_param_spec_string ("install-path",
                             "Install Path",
                             "Local installation path",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_FILE_SIZE] =
        g_param_spec_uint64 ("file-size",
                             "File Size",
                             "File size in bytes",
                             0, G_MAXUINT64, 0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_PREVIEW_URL] =
        g_param_spec_string ("preview-url",
                             "Preview URL",
                             "URL for preview image",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_workshop_item_init (LrgWorkshopItem *self)
{
    self->file_id = 0;
    self->title = NULL;
    self->description = NULL;
    self->owner_id = 0;
    self->time_created = 0;
    self->time_updated = 0;
    self->visibility = LRG_WORKSHOP_ITEM_VISIBILITY_PUBLIC;
    self->tags = g_ptr_array_new_with_free_func (g_free);
    self->votes_up = 0;
    self->votes_down = 0;
    self->score = 0.0f;
    self->state = LRG_WORKSHOP_ITEM_STATE_NONE;
    self->banned = FALSE;
    self->install_path = NULL;
    self->file_size = 0;
    self->preview_url = NULL;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgWorkshopItem *
lrg_workshop_item_new (guint64 file_id)
{
    return g_object_new (LRG_TYPE_WORKSHOP_ITEM,
                         "file-id", file_id,
                         NULL);
}

guint64
lrg_workshop_item_get_file_id (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), 0);

    return self->file_id;
}

const gchar *
lrg_workshop_item_get_title (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), NULL);

    return self->title;
}

void
lrg_workshop_item_set_title (LrgWorkshopItem *self,
                             const gchar     *title)
{
    g_return_if_fail (LRG_IS_WORKSHOP_ITEM (self));

    if (g_strcmp0 (self->title, title) != 0)
    {
        g_free (self->title);
        self->title = g_strdup (title);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
    }
}

const gchar *
lrg_workshop_item_get_description (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), NULL);

    return self->description;
}

void
lrg_workshop_item_set_description (LrgWorkshopItem *self,
                                   const gchar     *description)
{
    g_return_if_fail (LRG_IS_WORKSHOP_ITEM (self));

    if (g_strcmp0 (self->description, description) != 0)
    {
        g_free (self->description);
        self->description = g_strdup (description);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESCRIPTION]);
    }
}

guint64
lrg_workshop_item_get_owner_id (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), 0);

    return self->owner_id;
}

guint32
lrg_workshop_item_get_time_created (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), 0);

    return self->time_created;
}

guint32
lrg_workshop_item_get_time_updated (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), 0);

    return self->time_updated;
}

LrgWorkshopItemVisibility
lrg_workshop_item_get_visibility (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), LRG_WORKSHOP_ITEM_VISIBILITY_PUBLIC);

    return self->visibility;
}

void
lrg_workshop_item_set_visibility (LrgWorkshopItem          *self,
                                  LrgWorkshopItemVisibility visibility)
{
    g_return_if_fail (LRG_IS_WORKSHOP_ITEM (self));

    if (self->visibility != visibility)
    {
        self->visibility = visibility;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBILITY]);
    }
}

const gchar * const *
lrg_workshop_item_get_tags (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), NULL);

    /* Ensure NULL terminator */
    if (self->tags->len == 0 ||
        g_ptr_array_index (self->tags, self->tags->len - 1) != NULL)
    {
        g_ptr_array_add (self->tags, NULL);
    }

    return (const gchar * const *)self->tags->pdata;
}

void
lrg_workshop_item_set_tags (LrgWorkshopItem      *self,
                            const gchar * const  *tags)
{
    g_return_if_fail (LRG_IS_WORKSHOP_ITEM (self));

    g_ptr_array_set_size (self->tags, 0);

    if (tags != NULL)
    {
        guint i;
        for (i = 0; tags[i] != NULL; i++)
        {
            g_ptr_array_add (self->tags, g_strdup (tags[i]));
        }
    }
}

void
lrg_workshop_item_add_tag (LrgWorkshopItem *self,
                           const gchar     *tag)
{
    g_return_if_fail (LRG_IS_WORKSHOP_ITEM (self));
    g_return_if_fail (tag != NULL);

    /* Remove NULL terminator if present */
    if (self->tags->len > 0 &&
        g_ptr_array_index (self->tags, self->tags->len - 1) == NULL)
    {
        g_ptr_array_remove_index (self->tags, self->tags->len - 1);
    }

    g_ptr_array_add (self->tags, g_strdup (tag));
}

gboolean
lrg_workshop_item_remove_tag (LrgWorkshopItem *self,
                              const gchar     *tag)
{
    guint i;

    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), FALSE);
    g_return_val_if_fail (tag != NULL, FALSE);

    for (i = 0; i < self->tags->len; i++)
    {
        gchar *existing = g_ptr_array_index (self->tags, i);
        if (existing != NULL && g_strcmp0 (existing, tag) == 0)
        {
            g_ptr_array_remove_index (self->tags, i);
            return TRUE;
        }
    }

    return FALSE;
}

guint32
lrg_workshop_item_get_votes_up (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), 0);

    return self->votes_up;
}

guint32
lrg_workshop_item_get_votes_down (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), 0);

    return self->votes_down;
}

gfloat
lrg_workshop_item_get_score (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), 0.0f);

    return self->score;
}

LrgWorkshopItemState
lrg_workshop_item_get_state (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), LRG_WORKSHOP_ITEM_STATE_NONE);

    return self->state;
}

gboolean
lrg_workshop_item_is_subscribed (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), FALSE);

    return (self->state & LRG_WORKSHOP_ITEM_STATE_SUBSCRIBED) != 0;
}

gboolean
lrg_workshop_item_is_installed (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), FALSE);

    return (self->state & LRG_WORKSHOP_ITEM_STATE_INSTALLED) != 0;
}

gboolean
lrg_workshop_item_needs_update (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), FALSE);

    return (self->state & LRG_WORKSHOP_ITEM_STATE_NEEDS_UPDATE) != 0;
}

const gchar *
lrg_workshop_item_get_install_path (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), NULL);

    return self->install_path;
}

guint64
lrg_workshop_item_get_file_size (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), 0);

    return self->file_size;
}

gboolean
lrg_workshop_item_get_download_progress (LrgWorkshopItem *self,
                                         guint64         *bytes_downloaded,
                                         guint64         *bytes_total)
{
#ifdef LRG_ENABLE_STEAM
    ISteamUGC *ugc;
    uint64_t downloaded = 0;
    uint64_t total = 0;
    gboolean result;

    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), FALSE);

    ugc = SteamAPI_SteamUGC_v018 ();
    if (ugc == NULL)
        return FALSE;

    result = SteamAPI_ISteamUGC_GetItemDownloadInfo (ugc, self->file_id,
                                                     &downloaded, &total);

    if (result)
    {
        if (bytes_downloaded != NULL)
            *bytes_downloaded = downloaded;
        if (bytes_total != NULL)
            *bytes_total = total;
    }

    return result;
#else
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), FALSE);

    return FALSE;
#endif
}

gboolean
lrg_workshop_item_is_banned (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), FALSE);

    return self->banned;
}

const gchar *
lrg_workshop_item_get_preview_url (LrgWorkshopItem *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (self), NULL);

    return self->preview_url;
}
