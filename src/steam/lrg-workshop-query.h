/* lrg-workshop-query.h - Steam Workshop query builder
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_WORKSHOP_QUERY_H
#define LRG_WORKSHOP_QUERY_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_WORKSHOP_QUERY (lrg_workshop_query_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWorkshopQuery, lrg_workshop_query, LRG, WORKSHOP_QUERY, GObject)

/**
 * LrgWorkshopQueryType:
 * @LRG_WORKSHOP_QUERY_RANKED_BY_VOTE: Ranked by votes
 * @LRG_WORKSHOP_QUERY_RANKED_BY_PUBLICATION_DATE: Ranked by publication date
 * @LRG_WORKSHOP_QUERY_RANKED_BY_TREND: Ranked by trend
 * @LRG_WORKSHOP_QUERY_RANKED_BY_TEXT_SEARCH: Ranked by text search relevance
 * @LRG_WORKSHOP_QUERY_RANKED_BY_SUBSCRIPTIONS: Ranked by subscription count
 * @LRG_WORKSHOP_QUERY_RANKED_BY_PLAYTIME: Ranked by playtime
 * @LRG_WORKSHOP_QUERY_RANKED_BY_LAST_UPDATED: Ranked by last update date
 *
 * Query sorting options for Workshop searches.
 */
typedef enum
{
    LRG_WORKSHOP_QUERY_RANKED_BY_VOTE              = 0,
    LRG_WORKSHOP_QUERY_RANKED_BY_PUBLICATION_DATE  = 1,
    LRG_WORKSHOP_QUERY_RANKED_BY_TREND             = 3,
    LRG_WORKSHOP_QUERY_RANKED_BY_TEXT_SEARCH       = 11,
    LRG_WORKSHOP_QUERY_RANKED_BY_SUBSCRIPTIONS     = 12,
    LRG_WORKSHOP_QUERY_RANKED_BY_PLAYTIME          = 14,
    LRG_WORKSHOP_QUERY_RANKED_BY_LAST_UPDATED      = 19
} LrgWorkshopQueryType;

/**
 * LrgWorkshopContentType:
 * @LRG_WORKSHOP_CONTENT_ITEMS: User-created items
 * @LRG_WORKSHOP_CONTENT_COLLECTIONS: Collections
 * @LRG_WORKSHOP_CONTENT_ARTWORK: Artwork
 * @LRG_WORKSHOP_CONTENT_VIDEOS: Videos
 * @LRG_WORKSHOP_CONTENT_SCREENSHOTS: Screenshots
 * @LRG_WORKSHOP_CONTENT_GUIDES: All guides
 * @LRG_WORKSHOP_CONTENT_ALL: All content types
 *
 * Content types to query in the Workshop.
 */
typedef enum
{
    LRG_WORKSHOP_CONTENT_ITEMS       = 0,
    LRG_WORKSHOP_CONTENT_COLLECTIONS = 3,
    LRG_WORKSHOP_CONTENT_ARTWORK     = 4,
    LRG_WORKSHOP_CONTENT_VIDEOS      = 5,
    LRG_WORKSHOP_CONTENT_SCREENSHOTS = 6,
    LRG_WORKSHOP_CONTENT_GUIDES      = 7,
    LRG_WORKSHOP_CONTENT_ALL         = ~0
} LrgWorkshopContentType;

/**
 * lrg_workshop_query_new:
 * @query_type: the type of query/sorting
 *
 * Creates a new Workshop query builder.
 *
 * Returns: (transfer full): a new #LrgWorkshopQuery
 */
LRG_AVAILABLE_IN_ALL
LrgWorkshopQuery *
lrg_workshop_query_new (LrgWorkshopQueryType query_type);

/**
 * lrg_workshop_query_new_for_user:
 * @steam_id: the user's Steam ID
 *
 * Creates a new query for a specific user's Workshop items.
 *
 * Returns: (transfer full): a new #LrgWorkshopQuery
 */
LRG_AVAILABLE_IN_ALL
LrgWorkshopQuery *
lrg_workshop_query_new_for_user (guint64 steam_id);

/**
 * lrg_workshop_query_get_query_type:
 * @self: an #LrgWorkshopQuery
 *
 * Gets the query type.
 *
 * Returns: the query type
 */
LRG_AVAILABLE_IN_ALL
LrgWorkshopQueryType
lrg_workshop_query_get_query_type (LrgWorkshopQuery *self);

/**
 * lrg_workshop_query_set_content_type:
 * @self: an #LrgWorkshopQuery
 * @content_type: the content type to filter by
 *
 * Sets the content type filter.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_workshop_query_set_content_type (LrgWorkshopQuery       *self,
                                     LrgWorkshopContentType  content_type);

/**
 * lrg_workshop_query_get_content_type:
 * @self: an #LrgWorkshopQuery
 *
 * Gets the content type filter.
 *
 * Returns: the content type
 */
LRG_AVAILABLE_IN_ALL
LrgWorkshopContentType
lrg_workshop_query_get_content_type (LrgWorkshopQuery *self);

/**
 * lrg_workshop_query_set_search_text:
 * @self: an #LrgWorkshopQuery
 * @text: (nullable): the search text
 *
 * Sets the text search filter.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_workshop_query_set_search_text (LrgWorkshopQuery *self,
                                    const gchar      *text);

/**
 * lrg_workshop_query_get_search_text:
 * @self: an #LrgWorkshopQuery
 *
 * Gets the search text.
 *
 * Returns: (transfer none) (nullable): the search text
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_workshop_query_get_search_text (LrgWorkshopQuery *self);

/**
 * lrg_workshop_query_add_required_tag:
 * @self: an #LrgWorkshopQuery
 * @tag: the required tag
 *
 * Adds a required tag filter. Items must have this tag to match.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_workshop_query_add_required_tag (LrgWorkshopQuery *self,
                                     const gchar      *tag);

/**
 * lrg_workshop_query_add_excluded_tag:
 * @self: an #LrgWorkshopQuery
 * @tag: the excluded tag
 *
 * Adds an excluded tag filter. Items with this tag will not match.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_workshop_query_add_excluded_tag (LrgWorkshopQuery *self,
                                     const gchar      *tag);

/**
 * lrg_workshop_query_get_required_tags:
 * @self: an #LrgWorkshopQuery
 *
 * Gets the required tags.
 *
 * Returns: (transfer none) (element-type utf8): the required tags
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_workshop_query_get_required_tags (LrgWorkshopQuery *self);

/**
 * lrg_workshop_query_get_excluded_tags:
 * @self: an #LrgWorkshopQuery
 *
 * Gets the excluded tags.
 *
 * Returns: (transfer none) (element-type utf8): the excluded tags
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_workshop_query_get_excluded_tags (LrgWorkshopQuery *self);

/**
 * lrg_workshop_query_clear_tags:
 * @self: an #LrgWorkshopQuery
 *
 * Clears all tag filters.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_workshop_query_clear_tags (LrgWorkshopQuery *self);

/**
 * lrg_workshop_query_set_page:
 * @self: an #LrgWorkshopQuery
 * @page: the page number (1-based)
 *
 * Sets the page number for pagination.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_workshop_query_set_page (LrgWorkshopQuery *self,
                             guint             page);

/**
 * lrg_workshop_query_get_page:
 * @self: an #LrgWorkshopQuery
 *
 * Gets the page number.
 *
 * Returns: the page number
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_workshop_query_get_page (LrgWorkshopQuery *self);

/**
 * lrg_workshop_query_get_user_id:
 * @self: an #LrgWorkshopQuery
 *
 * Gets the user ID if this is a user query.
 *
 * Returns: the user Steam ID, or 0 if not a user query
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_workshop_query_get_user_id (LrgWorkshopQuery *self);

/**
 * lrg_workshop_query_is_user_query:
 * @self: an #LrgWorkshopQuery
 *
 * Checks if this is a user-specific query.
 *
 * Returns: %TRUE if this queries a specific user's items
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_query_is_user_query (LrgWorkshopQuery *self);

G_END_DECLS

#endif /* LRG_WORKSHOP_QUERY_H */
