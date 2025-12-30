# Steam Workshop Integration

The Steam Workshop module provides integration with Steam Workshop for mod distribution, subscription management, and content publishing.

## Overview

The Workshop module consists of three main types:

- **LrgWorkshopItem**: Represents a Workshop item (mod) with metadata
- **LrgWorkshopQuery**: Builder for Workshop search queries
- **LrgWorkshopManager**: Manages Workshop operations (subscribe, publish, query)

## Building with Steam Support

Workshop functionality requires building with Steam support:

```bash
make STEAM=1
```

Without Steam support, all operations return gracefully with "not available" errors.

## Types

### LrgWorkshopItem

Represents a Steam Workshop item with its metadata.

**Properties:**

| Property | Type | Description |
|----------|------|-------------|
| file-id | guint64 | Workshop file ID (read-only after construction) |
| title | string | Item title |
| description | string | Item description |
| owner-id | guint64 | Steam ID of the owner |
| time-created | guint | Unix timestamp of creation |
| time-updated | guint | Unix timestamp of last update |
| visibility | LrgWorkshopItemVisibility | Visibility setting |
| votes-up | guint | Number of upvotes |
| votes-down | guint | Number of downvotes |
| score | float | Item score (0.0 to 1.0) |
| state | LrgWorkshopItemState | Current state flags |
| banned | gboolean | Whether item is banned |
| install-path | string | Local installation path |
| file-size | guint64 | File size in bytes |
| preview-url | string | URL for preview image |

**State Flags (LrgWorkshopItemState):**

- `LRG_WORKSHOP_ITEM_STATE_NONE`: No state
- `LRG_WORKSHOP_ITEM_STATE_SUBSCRIBED`: Item is subscribed
- `LRG_WORKSHOP_ITEM_STATE_INSTALLED`: Item is installed locally
- `LRG_WORKSHOP_ITEM_STATE_NEEDS_UPDATE`: Update available
- `LRG_WORKSHOP_ITEM_STATE_DOWNLOADING`: Currently downloading
- `LRG_WORKSHOP_ITEM_STATE_DOWNLOAD_PENDING`: Download queued

**Visibility (LrgWorkshopItemVisibility):**

- `LRG_WORKSHOP_ITEM_VISIBILITY_PUBLIC`: Visible to everyone
- `LRG_WORKSHOP_ITEM_VISIBILITY_FRIENDS_ONLY`: Visible to friends
- `LRG_WORKSHOP_ITEM_VISIBILITY_PRIVATE`: Only visible to owner
- `LRG_WORKSHOP_ITEM_VISIBILITY_UNLISTED`: Accessible via direct link

### LrgWorkshopQuery

Builder for constructing Workshop search queries.

**Query Types (LrgWorkshopQueryType):**

- `LRG_WORKSHOP_QUERY_RANKED_BY_VOTE`: Sort by votes
- `LRG_WORKSHOP_QUERY_RANKED_BY_PUBLICATION_DATE`: Sort by publish date
- `LRG_WORKSHOP_QUERY_RANKED_BY_TREND`: Sort by trending
- `LRG_WORKSHOP_QUERY_RANKED_BY_TEXT_SEARCH`: Sort by search relevance
- `LRG_WORKSHOP_QUERY_RANKED_BY_SUBSCRIPTIONS`: Sort by subscription count
- `LRG_WORKSHOP_QUERY_RANKED_BY_PLAYTIME`: Sort by playtime
- `LRG_WORKSHOP_QUERY_RANKED_BY_LAST_UPDATED`: Sort by update date

**Content Types (LrgWorkshopContentType):**

- `LRG_WORKSHOP_CONTENT_ITEMS`: User-created items (mods)
- `LRG_WORKSHOP_CONTENT_COLLECTIONS`: Collections
- `LRG_WORKSHOP_CONTENT_ARTWORK`: Artwork
- `LRG_WORKSHOP_CONTENT_VIDEOS`: Videos
- `LRG_WORKSHOP_CONTENT_SCREENSHOTS`: Screenshots
- `LRG_WORKSHOP_CONTENT_GUIDES`: Guides
- `LRG_WORKSHOP_CONTENT_ALL`: All content types

### LrgWorkshopManager

Manages all Workshop operations. Operations are asynchronous and results are delivered via signals.

**Signals:**

| Signal | Parameters | Description |
|--------|------------|-------------|
| item-subscribed | file_id, success | Subscription result |
| item-unsubscribed | file_id, success | Unsubscription result |
| item-installed | file_id | Item finished installing |
| item-created | file_id, success | New item created |
| item-updated | file_id, success | Item update completed |
| item-deleted | file_id, success | Item deleted |
| query-completed | items, total, success | Query results |
| download-progress | file_id, downloaded, total | Download progress |

## Usage Examples

### Getting Subscribed Items

```c
LrgWorkshopManager *manager = lrg_workshop_manager_new (app_id);
g_autoptr(GPtrArray) items = lrg_workshop_manager_get_subscribed_items (manager);

for (guint i = 0; i < items->len; i++)
{
    LrgWorkshopItem *item = g_ptr_array_index (items, i);
    g_print ("Subscribed: %s\n", lrg_workshop_item_get_title (item));

    if (lrg_workshop_item_is_installed (item))
    {
        const gchar *path = lrg_workshop_item_get_install_path (item);
        g_print ("  Installed at: %s\n", path);
    }
}
```

### Searching the Workshop

```c
g_autoptr(LrgWorkshopQuery) query = NULL;
g_autoptr(GError) error = NULL;

/* Create a search query */
query = lrg_workshop_query_new (LRG_WORKSHOP_QUERY_RANKED_BY_VOTE);
lrg_workshop_query_set_content_type (query, LRG_WORKSHOP_CONTENT_ITEMS);
lrg_workshop_query_set_search_text (query, "sword");
lrg_workshop_query_add_required_tag (query, "weapons");
lrg_workshop_query_add_excluded_tag (query, "nsfw");
lrg_workshop_query_set_page (query, 1);

/* Connect to the result signal */
g_signal_connect (manager, "query-completed",
                  G_CALLBACK (on_query_completed), NULL);

/* Execute the query */
if (!lrg_workshop_manager_execute_query (manager, query, &error))
{
    g_warning ("Query failed: %s", error->message);
}

/* Handle results in callback */
static void
on_query_completed (LrgWorkshopManager *manager,
                    GPtrArray          *items,
                    guint               total_matching,
                    gboolean            success,
                    gpointer            user_data)
{
    if (!success)
        return;

    g_print ("Found %u items (showing %u)\n", total_matching, items->len);

    for (guint i = 0; i < items->len; i++)
    {
        LrgWorkshopItem *item = g_ptr_array_index (items, i);
        g_print ("  %s (score: %.2f)\n",
                 lrg_workshop_item_get_title (item),
                 lrg_workshop_item_get_score (item));
    }
}
```

### Subscribing to an Item

```c
g_autoptr(GError) error = NULL;

/* Connect to subscription signal */
g_signal_connect (manager, "item-subscribed",
                  G_CALLBACK (on_item_subscribed), NULL);

/* Subscribe */
if (!lrg_workshop_manager_subscribe (manager, file_id, &error))
{
    g_warning ("Subscribe failed: %s", error->message);
}

static void
on_item_subscribed (LrgWorkshopManager *manager,
                    guint64             file_id,
                    gboolean            success,
                    gpointer            user_data)
{
    if (success)
        g_print ("Subscribed to %" G_GUINT64_FORMAT "\n", file_id);
    else
        g_print ("Subscription failed\n");
}
```

### Publishing a New Item

```c
g_autoptr(LrgWorkshopItem) item = NULL;
g_autoptr(GError) error = NULL;

/* First, create the item (async) */
g_signal_connect (manager, "item-created",
                  G_CALLBACK (on_item_created), NULL);

if (!lrg_workshop_manager_create_item (manager, &error))
{
    g_warning ("Create failed: %s", error->message);
    return;
}

static void
on_item_created (LrgWorkshopManager *manager,
                 guint64             file_id,
                 gboolean            success,
                 gpointer            user_data)
{
    if (!success)
        return;

    /* Now update the item with content */
    g_autoptr(LrgWorkshopItem) item = lrg_workshop_item_new (file_id);
    lrg_workshop_item_set_title (item, "My Awesome Mod");
    lrg_workshop_item_set_description (item, "Adds awesome features.");
    lrg_workshop_item_set_visibility (item, LRG_WORKSHOP_ITEM_VISIBILITY_PUBLIC);

    const gchar *tags[] = { "gameplay", "content", NULL };
    lrg_workshop_item_set_tags (item, tags);

    g_autoptr(GError) error = NULL;
    if (!lrg_workshop_manager_update_item (manager, item,
                                            "/path/to/mod/content",
                                            "/path/to/preview.png",
                                            "Initial release",
                                            &error))
    {
        g_warning ("Update failed: %s", error->message);
    }
}
```

### Integration with Mod Manager

The Workshop module integrates with the existing mod system:

```c
/* Get installed Workshop items */
g_autoptr(GPtrArray) items = lrg_workshop_manager_get_subscribed_items (manager);

for (guint i = 0; i < items->len; i++)
{
    LrgWorkshopItem *item = g_ptr_array_index (items, i);

    if (lrg_workshop_item_is_installed (item))
    {
        const gchar *path = lrg_workshop_item_get_install_path (item);

        /* Add Workshop mod path to mod manager search paths */
        lrg_mod_manager_add_search_path (mod_manager, path);
    }
}

/* Discover and load mods (including Workshop mods) */
lrg_mod_manager_discover (mod_manager, NULL);
lrg_mod_manager_load_all (mod_manager, NULL);
```

## Error Handling

All Workshop errors use the `LRG_WORKSHOP_ERROR` domain:

- `LRG_WORKSHOP_ERROR_FAILED`: Generic failure
- `LRG_WORKSHOP_ERROR_NOT_AVAILABLE`: Steam not available
- `LRG_WORKSHOP_ERROR_QUERY`: Query failed
- `LRG_WORKSHOP_ERROR_SUBSCRIBE`: Subscription failed
- `LRG_WORKSHOP_ERROR_DOWNLOAD`: Download failed
- `LRG_WORKSHOP_ERROR_UPDATE`: Update failed
- `LRG_WORKSHOP_ERROR_CREATE`: Item creation failed
- `LRG_WORKSHOP_ERROR_DELETE`: Item deletion failed
- `LRG_WORKSHOP_ERROR_BUSY`: Operation already in progress

## Graceful Degradation

When built without Steam support (`STEAM=0`), all operations:

1. Return `FALSE` with `LRG_WORKSHOP_ERROR_NOT_AVAILABLE`
2. Return empty arrays for list operations
3. Return `LRG_WORKSHOP_ITEM_STATE_NONE` for state queries

This allows games to safely call Workshop APIs without runtime checks.

## See Also

- [Steam Module Overview](index.md)
- [Mod System](../mod/index.md)
- [Steam Workshop API Documentation](https://partner.steamgames.com/doc/features/workshop)
