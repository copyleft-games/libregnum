/* lrg-workshop-query.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWorkshopQuery - Steam Workshop query builder.
 */

#include "config.h"

#include "lrg-workshop-query.h"
#include "../lrg-log.h"

struct _LrgWorkshopQuery
{
    GObject parent_instance;

    /* Query type */
    LrgWorkshopQueryType query_type;
    LrgWorkshopContentType content_type;

    /* User query (optional) */
    guint64 user_id;
    gboolean is_user_query;

    /* Search */
    gchar *search_text;

    /* Tags */
    GPtrArray *required_tags;
    GPtrArray *excluded_tags;

    /* Pagination */
    guint page;
};

enum
{
    PROP_0,
    PROP_QUERY_TYPE,
    PROP_CONTENT_TYPE,
    PROP_SEARCH_TEXT,
    PROP_PAGE,
    PROP_USER_ID,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgWorkshopQuery, lrg_workshop_query, G_TYPE_OBJECT)

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_workshop_query_finalize (GObject *object)
{
    LrgWorkshopQuery *self = LRG_WORKSHOP_QUERY (object);

    g_clear_pointer (&self->search_text, g_free);
    g_clear_pointer (&self->required_tags, g_ptr_array_unref);
    g_clear_pointer (&self->excluded_tags, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_workshop_query_parent_class)->finalize (object);
}

static void
lrg_workshop_query_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgWorkshopQuery *self = LRG_WORKSHOP_QUERY (object);

    switch (prop_id)
    {
    case PROP_QUERY_TYPE:
        g_value_set_int (value, self->query_type);
        break;
    case PROP_CONTENT_TYPE:
        g_value_set_int (value, self->content_type);
        break;
    case PROP_SEARCH_TEXT:
        g_value_set_string (value, self->search_text);
        break;
    case PROP_PAGE:
        g_value_set_uint (value, self->page);
        break;
    case PROP_USER_ID:
        g_value_set_uint64 (value, self->user_id);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_workshop_query_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgWorkshopQuery *self = LRG_WORKSHOP_QUERY (object);

    switch (prop_id)
    {
    case PROP_QUERY_TYPE:
        self->query_type = g_value_get_int (value);
        break;
    case PROP_CONTENT_TYPE:
        self->content_type = g_value_get_int (value);
        break;
    case PROP_SEARCH_TEXT:
        lrg_workshop_query_set_search_text (self, g_value_get_string (value));
        break;
    case PROP_PAGE:
        self->page = g_value_get_uint (value);
        break;
    case PROP_USER_ID:
        self->user_id = g_value_get_uint64 (value);
        self->is_user_query = (self->user_id != 0);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_workshop_query_class_init (LrgWorkshopQueryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_workshop_query_finalize;
    object_class->get_property = lrg_workshop_query_get_property;
    object_class->set_property = lrg_workshop_query_set_property;

    properties[PROP_QUERY_TYPE] =
        g_param_spec_int ("query-type",
                          "Query Type",
                          "The query sorting type",
                          LRG_WORKSHOP_QUERY_RANKED_BY_VOTE,
                          LRG_WORKSHOP_QUERY_RANKED_BY_LAST_UPDATED,
                          LRG_WORKSHOP_QUERY_RANKED_BY_VOTE,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_CONTENT_TYPE] =
        g_param_spec_int ("content-type",
                          "Content Type",
                          "The content type filter",
                          0, G_MAXINT32,
                          LRG_WORKSHOP_CONTENT_ITEMS,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    properties[PROP_SEARCH_TEXT] =
        g_param_spec_string ("search-text",
                             "Search Text",
                             "Text search filter",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_PAGE] =
        g_param_spec_uint ("page",
                           "Page",
                           "Result page number (1-based)",
                           1, G_MAXUINT32, 1,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    properties[PROP_USER_ID] =
        g_param_spec_uint64 ("user-id",
                             "User ID",
                             "Steam ID for user queries",
                             0, G_MAXUINT64, 0,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_workshop_query_init (LrgWorkshopQuery *self)
{
    self->query_type = LRG_WORKSHOP_QUERY_RANKED_BY_VOTE;
    self->content_type = LRG_WORKSHOP_CONTENT_ITEMS;
    self->user_id = 0;
    self->is_user_query = FALSE;
    self->search_text = NULL;
    self->required_tags = g_ptr_array_new_with_free_func (g_free);
    self->excluded_tags = g_ptr_array_new_with_free_func (g_free);
    self->page = 1;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgWorkshopQuery *
lrg_workshop_query_new (LrgWorkshopQueryType query_type)
{
    return g_object_new (LRG_TYPE_WORKSHOP_QUERY,
                         "query-type", query_type,
                         NULL);
}

LrgWorkshopQuery *
lrg_workshop_query_new_for_user (guint64 steam_id)
{
    return g_object_new (LRG_TYPE_WORKSHOP_QUERY,
                         "query-type", LRG_WORKSHOP_QUERY_RANKED_BY_PUBLICATION_DATE,
                         "user-id", steam_id,
                         NULL);
}

LrgWorkshopQueryType
lrg_workshop_query_get_query_type (LrgWorkshopQuery *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_QUERY (self), LRG_WORKSHOP_QUERY_RANKED_BY_VOTE);

    return self->query_type;
}

void
lrg_workshop_query_set_content_type (LrgWorkshopQuery       *self,
                                     LrgWorkshopContentType  content_type)
{
    g_return_if_fail (LRG_IS_WORKSHOP_QUERY (self));

    if (self->content_type != content_type)
    {
        self->content_type = content_type;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONTENT_TYPE]);
    }
}

LrgWorkshopContentType
lrg_workshop_query_get_content_type (LrgWorkshopQuery *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_QUERY (self), LRG_WORKSHOP_CONTENT_ITEMS);

    return self->content_type;
}

void
lrg_workshop_query_set_search_text (LrgWorkshopQuery *self,
                                    const gchar      *text)
{
    g_return_if_fail (LRG_IS_WORKSHOP_QUERY (self));

    if (g_strcmp0 (self->search_text, text) != 0)
    {
        g_free (self->search_text);
        self->search_text = g_strdup (text);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SEARCH_TEXT]);
    }
}

const gchar *
lrg_workshop_query_get_search_text (LrgWorkshopQuery *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_QUERY (self), NULL);

    return self->search_text;
}

void
lrg_workshop_query_add_required_tag (LrgWorkshopQuery *self,
                                     const gchar      *tag)
{
    g_return_if_fail (LRG_IS_WORKSHOP_QUERY (self));
    g_return_if_fail (tag != NULL);

    g_ptr_array_add (self->required_tags, g_strdup (tag));
}

void
lrg_workshop_query_add_excluded_tag (LrgWorkshopQuery *self,
                                     const gchar      *tag)
{
    g_return_if_fail (LRG_IS_WORKSHOP_QUERY (self));
    g_return_if_fail (tag != NULL);

    g_ptr_array_add (self->excluded_tags, g_strdup (tag));
}

GPtrArray *
lrg_workshop_query_get_required_tags (LrgWorkshopQuery *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_QUERY (self), NULL);

    return self->required_tags;
}

GPtrArray *
lrg_workshop_query_get_excluded_tags (LrgWorkshopQuery *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_QUERY (self), NULL);

    return self->excluded_tags;
}

void
lrg_workshop_query_clear_tags (LrgWorkshopQuery *self)
{
    g_return_if_fail (LRG_IS_WORKSHOP_QUERY (self));

    g_ptr_array_set_size (self->required_tags, 0);
    g_ptr_array_set_size (self->excluded_tags, 0);
}

void
lrg_workshop_query_set_page (LrgWorkshopQuery *self,
                             guint             page)
{
    g_return_if_fail (LRG_IS_WORKSHOP_QUERY (self));
    g_return_if_fail (page >= 1);

    if (self->page != page)
    {
        self->page = page;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAGE]);
    }
}

guint
lrg_workshop_query_get_page (LrgWorkshopQuery *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_QUERY (self), 1);

    return self->page;
}

guint64
lrg_workshop_query_get_user_id (LrgWorkshopQuery *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_QUERY (self), 0);

    return self->user_id;
}

gboolean
lrg_workshop_query_is_user_query (LrgWorkshopQuery *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_QUERY (self), FALSE);

    return self->is_user_query;
}
