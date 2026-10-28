/* lrg-portal-system.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Portal-based visibility system implementation.
 */

#include "config.h"
#include "lrg-portal-system.h"
#include "lrg-sector-private.h"

#include <string.h>

#define DEFAULT_MAX_PORTAL_DEPTH 4

/*
 * Topology cache, rebuilt lazily after any sector or portal is added or
 * removed. Sectors are listed in the sector table's iteration order (the
 * order find_sector_at() has always scanned, so the first containing
 * sector is the same one), each with its portal links resolved: the
 * portal and the index of the sector across it (-1 when that sector is
 * not in the system). A traversal is then index walks and a per-sector
 * visit stamp, instead of three string-hashed table operations per step.
 * Sectors and portals are owned copies only changed through this API.
 */
typedef struct
{
    const gchar            *key;          /* the sector table's key */
    const LrgBoundingBox3D *bounds;       /* the owned sector's bounds */
    const LrgSector        *sector;
    guint                   first_link;   /* into topo_links */
    guint                   n_links;
    guint                   visit_stamp;  /* == self->visit_stamp: visited */
} TopoSector;

typedef struct
{
    const LrgPortal *portal;
    gint             target;              /* TopoSector index, or -1 */
} TopoLink;

struct _LrgPortalSystem
{
    GObject          parent_instance;

    GHashTable      *sectors;          /* gchar* -> LrgSector* */
    GHashTable      *portals;          /* gchar* -> LrgPortal* */

    gchar           *current_sector;   /* Sector camera is in */
    GPtrArray       *visible_sectors;  /* gchar* sector IDs currently visible */

    guint            max_portal_depth; /* Recursion limit for portal traversal */

    /* update() scratch, reused every call: the traversal's visited set
     * and the visible IDs it found, both borrowing the sector table's
     * keys for the duration of the call. */
    GPtrArray       *visit_order;

    /* Topology cache (see TopoSector) */
    GArray          *topo_sectors;     /* TopoSector */
    GArray          *topo_links;       /* TopoLink */
    GHashTable      *topo_index;       /* sector key -> TopoSector index + 1 */
    gboolean         topo_dirty;
    guint            visit_stamp;
};

G_DEFINE_FINAL_TYPE (LrgPortalSystem, lrg_portal_system, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_SECTOR_COUNT,
    PROP_PORTAL_COUNT,
    PROP_CURRENT_SECTOR,
    PROP_MAX_PORTAL_DEPTH,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum {
    SIGNAL_SECTOR_ENTERED,
    SIGNAL_SECTOR_EXITED,
    SIGNAL_VISIBILITY_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_portal_system_finalize (GObject *object)
{
    LrgPortalSystem *self = LRG_PORTAL_SYSTEM (object);

    g_clear_pointer (&self->sectors, g_hash_table_unref);
    g_clear_pointer (&self->portals, g_hash_table_unref);
    g_clear_pointer (&self->current_sector, g_free);
    g_clear_pointer (&self->visible_sectors, g_ptr_array_unref);
    g_clear_pointer (&self->visit_order, g_ptr_array_unref);
    g_clear_pointer (&self->topo_sectors, g_array_unref);
    g_clear_pointer (&self->topo_links, g_array_unref);
    g_clear_pointer (&self->topo_index, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_portal_system_parent_class)->finalize (object);
}

static void
lrg_portal_system_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgPortalSystem *self = LRG_PORTAL_SYSTEM (object);

    switch (prop_id)
    {
    case PROP_SECTOR_COUNT:
        g_value_set_uint (value, g_hash_table_size (self->sectors));
        break;
    case PROP_PORTAL_COUNT:
        g_value_set_uint (value, g_hash_table_size (self->portals));
        break;
    case PROP_CURRENT_SECTOR:
        g_value_set_string (value, self->current_sector);
        break;
    case PROP_MAX_PORTAL_DEPTH:
        g_value_set_uint (value, self->max_portal_depth);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_portal_system_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgPortalSystem *self = LRG_PORTAL_SYSTEM (object);

    switch (prop_id)
    {
    case PROP_MAX_PORTAL_DEPTH:
        self->max_portal_depth = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_portal_system_class_init (LrgPortalSystemClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_portal_system_finalize;
    object_class->get_property = lrg_portal_system_get_property;
    object_class->set_property = lrg_portal_system_set_property;

    /**
     * LrgPortalSystem:sector-count:
     *
     * The number of sectors in the system.
     */
    properties[PROP_SECTOR_COUNT] =
        g_param_spec_uint ("sector-count",
                           "Sector Count",
                           "Number of sectors",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPortalSystem:portal-count:
     *
     * The number of portals in the system.
     */
    properties[PROP_PORTAL_COUNT] =
        g_param_spec_uint ("portal-count",
                           "Portal Count",
                           "Number of portals",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPortalSystem:current-sector:
     *
     * The sector the camera is currently in.
     */
    properties[PROP_CURRENT_SECTOR] =
        g_param_spec_string ("current-sector",
                             "Current Sector",
                             "Sector the camera is in",
                             NULL,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPortalSystem:max-portal-depth:
     *
     * Maximum portal traversal depth for visibility determination.
     */
    properties[PROP_MAX_PORTAL_DEPTH] =
        g_param_spec_uint ("max-portal-depth",
                           "Max Portal Depth",
                           "Maximum portal traversal depth",
                           1, 16, DEFAULT_MAX_PORTAL_DEPTH,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgPortalSystem::sector-entered:
     * @self: An #LrgPortalSystem
     * @sector_id: The sector that was entered
     *
     * Emitted when the camera enters a new sector.
     */
    signals[SIGNAL_SECTOR_ENTERED] =
        g_signal_new ("sector-entered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_STRING);

    /**
     * LrgPortalSystem::sector-exited:
     * @self: An #LrgPortalSystem
     * @sector_id: The sector that was exited
     *
     * Emitted when the camera exits a sector.
     */
    signals[SIGNAL_SECTOR_EXITED] =
        g_signal_new ("sector-exited",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_STRING);

    /**
     * LrgPortalSystem::visibility-changed:
     * @self: An #LrgPortalSystem
     *
     * Emitted when the set of visible sectors changes.
     */
    signals[SIGNAL_VISIBILITY_CHANGED] =
        g_signal_new ("visibility-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_portal_system_init (LrgPortalSystem *self)
{
    self->sectors = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, (GDestroyNotify)lrg_sector_free);
    self->portals = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, (GDestroyNotify)lrg_portal_free);
    self->current_sector = NULL;
    self->visible_sectors = g_ptr_array_new_with_free_func (g_free);
    self->max_portal_depth = DEFAULT_MAX_PORTAL_DEPTH;
    self->visit_order = g_ptr_array_new ();
    self->topo_sectors = g_array_new (FALSE, FALSE, sizeof (TopoSector));
    self->topo_links = g_array_new (FALSE, FALSE, sizeof (TopoLink));
    self->topo_index = g_hash_table_new (g_str_hash, g_str_equal);
    self->topo_dirty = TRUE;
}

/*
 * lrg_portal_system_new:
 *
 * Creates a new portal system.
 *
 * Returns: (transfer full): A new #LrgPortalSystem
 */
LrgPortalSystem *
lrg_portal_system_new (void)
{
    return g_object_new (LRG_TYPE_PORTAL_SYSTEM, NULL);
}

/* --- Sector Management --- */

/*
 * lrg_portal_system_add_sector:
 * @self: An #LrgPortalSystem
 * @sector: (transfer none): Sector to add
 *
 * Adds a sector to the system.
 */
void
lrg_portal_system_add_sector (LrgPortalSystem *self,
                              const LrgSector *sector)
{
    const gchar *id;

    g_return_if_fail (LRG_IS_PORTAL_SYSTEM (self));
    g_return_if_fail (sector != NULL);

    id = lrg_sector_get_id (sector);
    g_hash_table_insert (self->sectors,
                         g_strdup (id),
                         lrg_sector_copy (sector));
    self->topo_dirty = TRUE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SECTOR_COUNT]);
}

/*
 * lrg_portal_system_remove_sector:
 * @self: An #LrgPortalSystem
 * @id: Sector ID to remove
 *
 * Removes a sector from the system.
 *
 * Returns: %TRUE if the sector was found and removed
 */
gboolean
lrg_portal_system_remove_sector (LrgPortalSystem *self,
                                 const gchar     *id)
{
    gboolean removed;

    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    removed = g_hash_table_remove (self->sectors, id);
    self->topo_dirty = TRUE;

    if (removed)
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SECTOR_COUNT]);

    return removed;
}

/*
 * lrg_portal_system_get_sector:
 * @self: An #LrgPortalSystem
 * @id: Sector ID
 *
 * Gets a sector by ID.
 *
 * Returns: (transfer none) (nullable): The sector, or %NULL if not found
 */
const LrgSector *
lrg_portal_system_get_sector (LrgPortalSystem *self,
                              const gchar     *id)
{
    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    return g_hash_table_lookup (self->sectors, id);
}

/*
 * lrg_portal_system_get_sectors:
 * @self: An #LrgPortalSystem
 *
 * Gets all sectors.
 *
 * Returns: (transfer container) (element-type LrgSector): Array of sectors
 */
GPtrArray *
lrg_portal_system_get_sectors (LrgPortalSystem *self)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->sectors);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        g_ptr_array_add (result, value);

    return result;
}

/*
 * lrg_portal_system_get_sector_count:
 * @self: An #LrgPortalSystem
 *
 * Gets the number of sectors.
 *
 * Returns: Sector count
 */
guint
lrg_portal_system_get_sector_count (LrgPortalSystem *self)
{
    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), 0);

    return g_hash_table_size (self->sectors);
}

/*
 * topo_index_of:
 *
 * The topology cache index of the sector called @id, or -1 when there is
 * none (the cache must be current).
 */
static gint
topo_index_of (LrgPortalSystem *self,
               const gchar     *id)
{
    return (gint) GPOINTER_TO_UINT (g_hash_table_lookup (self->topo_index, id)) - 1;
}

/*
 * topo_ensure:
 *
 * Rebuilds the topology cache if sectors or portals changed since it was
 * built (see TopoSector).
 */
static void
topo_ensure (LrgPortalSystem *self)
{
    GHashTableIter iter;
    gpointer key;
    gpointer value;
    guint i;

    if (!self->topo_dirty)
        return;

    g_array_set_size (self->topo_sectors, 0);
    g_array_set_size (self->topo_links, 0);

    /* Sectors, in table iteration order, and their table indices */
    g_hash_table_remove_all (self->topo_index);
    g_hash_table_iter_init (&iter, self->sectors);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        TopoSector entry;

        entry.key = key;
        entry.sector = value;
        entry.bounds = lrg_sector_peek_bounds (value);
        entry.first_link = 0;
        entry.n_links = 0;
        entry.visit_stamp = 0;
        g_hash_table_insert (self->topo_index, key,
                             GUINT_TO_POINTER (self->topo_sectors->len + 1));
        g_array_append_val (self->topo_sectors, entry);
    }

    /* Each sector's portals, in its own order; missing portals are
     * skipped, as the traversal always skipped them */
    for (i = 0; i < self->topo_sectors->len; i++)
    {
        TopoSector *entry = &g_array_index (self->topo_sectors, TopoSector, i);
        const GPtrArray *portal_ids = lrg_sector_peek_portal_ids (entry->sector);
        guint j;

        entry->first_link = self->topo_links->len;
        for (j = 0; j < portal_ids->len; j++)
        {
            const LrgPortal *portal;
            const gchar *other;
            TopoLink link;

            portal = g_hash_table_lookup (self->portals,
                                          g_ptr_array_index (portal_ids, j));
            if (portal == NULL)
                continue;

            other = lrg_portal_get_other_sector (portal, entry->key);
            link.portal = portal;
            link.target = other != NULL ?
                topo_index_of (self, other) : -1;
            g_array_append_val (self->topo_links, link);
        }
        entry->n_links = self->topo_links->len - entry->first_link;
    }

    self->visit_stamp = 0;
    self->topo_dirty = FALSE;
}

/*
 * lrg_portal_system_find_sector_at:
 * @self: An #LrgPortalSystem
 * @point: (transfer none): Point to test
 *
 * Finds the sector containing a point.
 *
 * Returns: (transfer none) (nullable): The sector, or %NULL if none contains the point
 */
const LrgSector *
lrg_portal_system_find_sector_at (LrgPortalSystem  *self,
                                  const GrlVector3 *point)
{
    const TopoSector *entries;
    guint i;

    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), NULL);
    g_return_val_if_fail (point != NULL, NULL);

    /* The cached list is the table's iteration order: same first match */
    topo_ensure (self);
    entries = (const TopoSector *) (gconstpointer) self->topo_sectors->data;
    for (i = 0; i < self->topo_sectors->len; i++)
    {
        const LrgBoundingBox3D *b = entries[i].bounds;

        if (point->x >= b->min.x && point->x <= b->max.x &&
            point->y >= b->min.y && point->y <= b->max.y &&
            point->z >= b->min.z && point->z <= b->max.z)
            return entries[i].sector;
    }

    return NULL;
}

/* --- Portal Management --- */

/*
 * lrg_portal_system_add_portal:
 * @self: An #LrgPortalSystem
 * @portal: (transfer none): Portal to add
 *
 * Adds a portal to the system.
 */
void
lrg_portal_system_add_portal (LrgPortalSystem *self,
                              const LrgPortal *portal)
{
    const gchar *id;

    g_return_if_fail (LRG_IS_PORTAL_SYSTEM (self));
    g_return_if_fail (portal != NULL);

    id = lrg_portal_get_id (portal);
    g_hash_table_insert (self->portals,
                         g_strdup (id),
                         lrg_portal_copy (portal));
    self->topo_dirty = TRUE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PORTAL_COUNT]);
}

/*
 * lrg_portal_system_remove_portal:
 * @self: An #LrgPortalSystem
 * @id: Portal ID to remove
 *
 * Removes a portal from the system.
 *
 * Returns: %TRUE if the portal was found and removed
 */
gboolean
lrg_portal_system_remove_portal (LrgPortalSystem *self,
                                 const gchar     *id)
{
    gboolean removed;

    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    removed = g_hash_table_remove (self->portals, id);
    self->topo_dirty = TRUE;

    if (removed)
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PORTAL_COUNT]);

    return removed;
}

/*
 * lrg_portal_system_get_portal:
 * @self: An #LrgPortalSystem
 * @id: Portal ID
 *
 * Gets a portal by ID.
 *
 * Returns: (transfer none) (nullable): The portal, or %NULL if not found
 */
const LrgPortal *
lrg_portal_system_get_portal (LrgPortalSystem *self,
                              const gchar     *id)
{
    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    return g_hash_table_lookup (self->portals, id);
}

/*
 * lrg_portal_system_get_portals:
 * @self: An #LrgPortalSystem
 *
 * Gets all portals.
 *
 * Returns: (transfer container) (element-type LrgPortal): Array of portals
 */
GPtrArray *
lrg_portal_system_get_portals (LrgPortalSystem *self)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->portals);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        g_ptr_array_add (result, value);

    return result;
}

/*
 * lrg_portal_system_get_portal_count:
 * @self: An #LrgPortalSystem
 *
 * Gets the number of portals.
 *
 * Returns: Portal count
 */
guint
lrg_portal_system_get_portal_count (LrgPortalSystem *self)
{
    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), 0);

    return g_hash_table_size (self->portals);
}

/*
 * traverse_portals_recursive:
 *
 * Recursively traverse portals from a sector (a topology cache index) to
 * find all visible sectors, appending each newly reached sector's ID (the
 * sector table's key) to self->visit_order in discovery order.
 */
static void
traverse_portals_recursive (LrgPortalSystem  *self,
                            gint              sector_index,
                            const GrlVector3 *camera_pos,
                            guint             depth)
{
    TopoSector *sector;
    const TopoLink *links;
    guint i;

    if (depth >= self->max_portal_depth)
        return;

    /* Not in the system: nothing to visit */
    if (sector_index < 0)
        return;

    sector = &g_array_index (self->topo_sectors, TopoSector, sector_index);
    if (sector->visit_stamp == self->visit_stamp)
        return;

    /* Mark as visited */
    sector->visit_stamp = self->visit_stamp;
    g_ptr_array_add (self->visit_order, (gpointer) sector->key);

    /* Traverse connected portals */
    links = &g_array_index (self->topo_links, TopoLink, sector->first_link);
    for (i = 0; i < sector->n_links; i++)
    {
        /* Check if portal is visible from camera */
        if (lrg_portal_is_visible_from (links[i].portal, camera_pos))
            traverse_portals_recursive (self, links[i].target, camera_pos, depth + 1);
    }
}

/*
 * visible_set_matches:
 *
 * Whether the traversal just done found exactly the visible list we hold
 * (same IDs, same order).
 */
static gboolean
visible_set_matches (LrgPortalSystem *self)
{
    guint i;

    if (self->visit_order->len != self->visible_sectors->len)
        return FALSE;
    for (i = 0; i < self->visit_order->len; i++)
        if (strcmp (g_ptr_array_index (self->visit_order, i),
                    g_ptr_array_index (self->visible_sectors, i)) != 0)
            return FALSE;
    return TRUE;
}

/*
 * lrg_portal_system_update:
 * @self: An #LrgPortalSystem
 * @camera_pos: (transfer none): Camera position
 *
 * Updates sector visibility based on camera position.
 * This traverses portals to determine which sectors are visible.
 * #LrgPortalSystem::visibility-changed is emitted only when the visible
 * set (or its order) differs from the previous update's.
 */
void
lrg_portal_system_update (LrgPortalSystem  *self,
                          const GrlVector3 *camera_pos)
{
    const LrgSector *current;
    const gchar *new_sector_id;
    g_autofree gchar *old_sector = NULL;
    guint i;

    g_return_if_fail (LRG_IS_PORTAL_SYSTEM (self));
    g_return_if_fail (camera_pos != NULL);

    /* Find which sector the camera is in */
    current = lrg_portal_system_find_sector_at (self, camera_pos);

    if (current == NULL)
    {
        /* Camera is not in any sector */
        if (self->current_sector != NULL)
        {
            old_sector = self->current_sector;
            self->current_sector = NULL;
            g_signal_emit (self, signals[SIGNAL_SECTOR_EXITED], 0, old_sector);
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_SECTOR]);
        }

        /* Clear visibility */
        if (self->visible_sectors->len > 0)
        {
            g_ptr_array_set_size (self->visible_sectors, 0);
            g_signal_emit (self, signals[SIGNAL_VISIBILITY_CHANGED], 0);
        }
        return;
    }

    new_sector_id = lrg_sector_get_id (current);

    /* Check for sector change */
    if (g_strcmp0 (self->current_sector, new_sector_id) != 0)
    {
        old_sector = self->current_sector;
        self->current_sector = g_strdup (new_sector_id);

        if (old_sector != NULL)
            g_signal_emit (self, signals[SIGNAL_SECTOR_EXITED], 0, old_sector);

        g_signal_emit (self, signals[SIGNAL_SECTOR_ENTERED], 0, new_sector_id);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_SECTOR]);
    }

    /*
     * Traverse portals into the reusable scratch (no allocation once it
     * has grown), then replace the visible list only if it changed: the
     * camera usually sits still or moves within a sector, and the set
     * then stays the same frame after frame. Visibility depends on the
     * camera position, not just its sector, so the traversal itself
     * runs every update.
     */
    g_ptr_array_set_size (self->visit_order, 0);
    topo_ensure (self);
    if (++self->visit_stamp == 0)
    {
        /* Stamp wrapped: forget every old visit */
        for (i = 0; i < self->topo_sectors->len; i++)
            g_array_index (self->topo_sectors, TopoSector, i).visit_stamp = 0;
        self->visit_stamp = 1;
    }
    traverse_portals_recursive (self,
                                topo_index_of (self, self->current_sector),
                                camera_pos, 0);

    if (!visible_set_matches (self))
    {
        g_ptr_array_set_size (self->visible_sectors, 0);
        for (i = 0; i < self->visit_order->len; i++)
            g_ptr_array_add (self->visible_sectors,
                             g_strdup (g_ptr_array_index (self->visit_order, i)));
        g_ptr_array_set_size (self->visit_order, 0);
        g_signal_emit (self, signals[SIGNAL_VISIBILITY_CHANGED], 0);
    }
    else
    {
        /* Drop the borrowed keys before handlers can touch the tables. */
        g_ptr_array_set_size (self->visit_order, 0);
    }
}

/*
 * lrg_portal_system_get_visible_sectors:
 * @self: An #LrgPortalSystem
 *
 * Gets all currently visible sectors.
 *
 * Returns: (transfer container) (element-type LrgSector): Array of visible sectors
 */
GPtrArray *
lrg_portal_system_get_visible_sectors (LrgPortalSystem *self)
{
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), NULL);

    result = g_ptr_array_new ();

    for (i = 0; i < self->visible_sectors->len; i++)
    {
        const gchar *sector_id = g_ptr_array_index (self->visible_sectors, i);
        const LrgSector *sector = lrg_portal_system_get_sector (self, sector_id);
        if (sector != NULL)
            g_ptr_array_add (result, (gpointer)sector);
    }

    return result;
}

/*
 * lrg_portal_system_get_visible_sector_count:
 * @self: An #LrgPortalSystem
 *
 * Gets the number of currently visible sectors.
 *
 * Returns: Visible sector count
 */
guint
lrg_portal_system_get_visible_sector_count (LrgPortalSystem *self)
{
    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), 0);

    return self->visible_sectors->len;
}

/*
 * lrg_portal_system_is_sector_visible:
 * @self: An #LrgPortalSystem
 * @id: Sector ID to check
 *
 * Checks if a sector is currently visible.
 *
 * Returns: %TRUE if the sector is visible
 */
gboolean
lrg_portal_system_is_sector_visible (LrgPortalSystem *self,
                                     const gchar     *id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    for (i = 0; i < self->visible_sectors->len; i++)
    {
        const gchar *sector_id = g_ptr_array_index (self->visible_sectors, i);
        if (g_strcmp0 (sector_id, id) == 0)
            return TRUE;
    }

    return FALSE;
}

/*
 * lrg_portal_system_get_current_sector:
 * @self: An #LrgPortalSystem
 *
 * Gets the sector the camera is currently in.
 *
 * Returns: (transfer none) (nullable): The current sector ID
 */
const gchar *
lrg_portal_system_get_current_sector (LrgPortalSystem *self)
{
    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), NULL);

    return self->current_sector;
}

/* --- Configuration --- */

/*
 * lrg_portal_system_get_max_portal_depth:
 * @self: An #LrgPortalSystem
 *
 * Gets the maximum portal traversal depth.
 *
 * Returns: Maximum depth
 */
guint
lrg_portal_system_get_max_portal_depth (LrgPortalSystem *self)
{
    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), 0);

    return self->max_portal_depth;
}

/*
 * lrg_portal_system_set_max_portal_depth:
 * @self: An #LrgPortalSystem
 * @max_depth: Maximum traversal depth
 *
 * Sets the maximum portal traversal depth.
 */
void
lrg_portal_system_set_max_portal_depth (LrgPortalSystem *self,
                                        guint            max_depth)
{
    g_return_if_fail (LRG_IS_PORTAL_SYSTEM (self));
    g_return_if_fail (max_depth >= 1 && max_depth <= 16);

    if (self->max_portal_depth != max_depth)
    {
        self->max_portal_depth = max_depth;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_PORTAL_DEPTH]);
    }
}

/* --- Utility --- */

/*
 * lrg_portal_system_clear:
 * @self: An #LrgPortalSystem
 *
 * Removes all sectors and portals.
 */
void
lrg_portal_system_clear (LrgPortalSystem *self)
{
    g_return_if_fail (LRG_IS_PORTAL_SYSTEM (self));

    g_hash_table_remove_all (self->sectors);
    g_hash_table_remove_all (self->portals);
    self->topo_dirty = TRUE;
    g_clear_pointer (&self->current_sector, g_free);
    g_ptr_array_set_size (self->visible_sectors, 0);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SECTOR_COUNT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PORTAL_COUNT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_SECTOR]);
}

/*
 * lrg_portal_system_get_sector_portals:
 * @self: An #LrgPortalSystem
 * @sector_id: Sector ID
 *
 * Gets all portals connected to a sector.
 *
 * Returns: (transfer container) (element-type LrgPortal): Array of portals
 */
GPtrArray *
lrg_portal_system_get_sector_portals (LrgPortalSystem *self,
                                      const gchar     *sector_id)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), NULL);
    g_return_val_if_fail (sector_id != NULL, NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->portals);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgPortal *portal = value;
        if (lrg_portal_connects_sector (portal, sector_id))
            g_ptr_array_add (result, portal);
    }

    return result;
}
