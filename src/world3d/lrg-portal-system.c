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

#define DEFAULT_MAX_PORTAL_DEPTH 4

struct _LrgPortalSystem
{
    GObject          parent_instance;

    GHashTable      *sectors;          /* gchar* -> LrgSector* */
    GHashTable      *portals;          /* gchar* -> LrgPortal* */

    gchar           *current_sector;   /* Sector camera is in */
    GPtrArray       *visible_sectors;  /* gchar* sector IDs currently visible */

    guint            max_portal_depth; /* Recursion limit for portal traversal */
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
}

/**
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

/**
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

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SECTOR_COUNT]);
}

/**
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

    if (removed)
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SECTOR_COUNT]);

    return removed;
}

/**
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

/**
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

/**
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

/**
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
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_PORTAL_SYSTEM (self), NULL);
    g_return_val_if_fail (point != NULL, NULL);

    g_hash_table_iter_init (&iter, self->sectors);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgSector *sector = value;
        if (lrg_sector_contains_point (sector, point))
            return sector;
    }

    return NULL;
}

/* --- Portal Management --- */

/**
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

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PORTAL_COUNT]);
}

/**
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

    if (removed)
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PORTAL_COUNT]);

    return removed;
}

/**
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

/**
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

/**
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
 * Recursively traverse portals from a sector to find all visible sectors.
 */
static void
traverse_portals_recursive (LrgPortalSystem *self,
                            const gchar     *sector_id,
                            const GrlVector3 *camera_pos,
                            GHashTable      *visited,
                            guint            depth)
{
    const LrgSector *sector;
    GPtrArray *portal_ids;
    guint i;

    if (depth >= self->max_portal_depth)
        return;

    if (g_hash_table_contains (visited, sector_id))
        return;

    sector = lrg_portal_system_get_sector (self, sector_id);
    if (sector == NULL)
        return;

    /* Mark as visited */
    g_hash_table_add (visited, (gpointer)sector_id);
    g_ptr_array_add (self->visible_sectors, g_strdup (sector_id));

    /* Traverse connected portals */
    portal_ids = lrg_sector_get_portal_ids (sector);

    for (i = 0; i < portal_ids->len; i++)
    {
        const gchar *portal_id = g_ptr_array_index (portal_ids, i);
        const LrgPortal *portal = lrg_portal_system_get_portal (self, portal_id);

        if (portal == NULL)
            continue;

        /* Check if portal is visible from camera */
        if (lrg_portal_is_visible_from (portal, camera_pos))
        {
            const gchar *other_sector = lrg_portal_get_other_sector (portal, sector_id);
            if (other_sector != NULL)
                traverse_portals_recursive (self, other_sector, camera_pos, visited, depth + 1);
        }
    }

    g_ptr_array_unref (portal_ids);
}

/**
 * lrg_portal_system_update:
 * @self: An #LrgPortalSystem
 * @camera_pos: (transfer none): Camera position
 *
 * Updates sector visibility based on camera position.
 * This traverses portals to determine which sectors are visible.
 */
void
lrg_portal_system_update (LrgPortalSystem  *self,
                          const GrlVector3 *camera_pos)
{
    const LrgSector *current;
    const gchar *new_sector_id;
    g_autofree gchar *old_sector = NULL;
    g_autoptr(GHashTable) visited = NULL;

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

    /* Clear and rebuild visibility */
    g_ptr_array_set_size (self->visible_sectors, 0);

    /* Traverse portals to find visible sectors */
    visited = g_hash_table_new (g_str_hash, g_str_equal);
    traverse_portals_recursive (self, new_sector_id, camera_pos, visited, 0);

    g_signal_emit (self, signals[SIGNAL_VISIBILITY_CHANGED], 0);
}

/**
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

/**
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

/**
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

/**
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

/**
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

/**
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

/**
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
    g_clear_pointer (&self->current_sector, g_free);
    g_ptr_array_set_size (self->visible_sectors, 0);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SECTOR_COUNT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PORTAL_COUNT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_SECTOR]);
}

/**
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
