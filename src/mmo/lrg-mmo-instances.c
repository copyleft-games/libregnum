/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-instances.h"
#include "lrg-mmo-service-private.h"

/*
 * An instance holds its members in the order they joined and who of
 * them is inside.  @empty_since is when the last member inside left (or
 * when the instance opened), so the host can close instances nobody
 * uses.  Accounts map to their instance through @by_account.
 */
typedef struct
{
    guint64 id;
    gchar *content;
    guint capacity;
    gint64 created;
    gint64 empty_since;
    GPtrArray *members;   /* gchar* */
    GHashTable *inside;   /* gchar* (owned) set */
} Instance;

struct _LrgMmoInstances
{
    GObject parent_instance;
    GHashTable *instances;   /* guint64* -> Instance* */
    GHashTable *by_account;  /* account (owned) -> guint64* id (owned) */
    guint max_instances;
    guint64 next_id;
};

G_DEFINE_TYPE (LrgMmoInstances, lrg_mmo_instances, G_TYPE_OBJECT)

#define MAX_CAPACITY (1024)

static void
instance_free (gpointer data)
{
    Instance *instance = data;
    g_free (instance->content);
    g_ptr_array_unref (instance->members);
    g_hash_table_unref (instance->inside);
    g_free (instance);
}

static void
lrg_mmo_instances_finalize (GObject *object)
{
    LrgMmoInstances *self = LRG_MMO_INSTANCES (object);
    g_hash_table_unref (self->instances);
    g_hash_table_unref (self->by_account);
    G_OBJECT_CLASS (lrg_mmo_instances_parent_class)->finalize (object);
}

static void
lrg_mmo_instances_class_init (LrgMmoInstancesClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_mmo_instances_finalize;
}

static void
lrg_mmo_instances_init (LrgMmoInstances *self)
{
    self->instances = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, instance_free);
    self->by_account = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    self->next_id = 1;
}

LrgMmoInstances *
lrg_mmo_instances_new (guint max_instances)
{
    LrgMmoInstances *self;
    g_return_val_if_fail (max_instances > 0, NULL);
    self = g_object_new (LRG_TYPE_MMO_INSTANCES, NULL);
    self->max_instances = max_instances;
    return self;
}

static Instance *
lookup (LrgMmoInstances *self, guint64 id)
{
    return g_hash_table_lookup (self->instances, &id);
}

static void
map_account (LrgMmoInstances *self, const gchar *account, guint64 id)
{
    guint64 *value = g_new (guint64, 1);
    *value = id;
    g_hash_table_replace (self->by_account, g_strdup (account), value);
}

/* adds a checked instance to the table */
static Instance *
insert (LrgMmoInstances *self, guint64 id, const gchar *content, guint capacity,
        const gchar * const *members, gint64 created, gint64 now_us)
{
    Instance *instance;
    guint64 *key;
    guint i;
    instance = g_new0 (Instance, 1);
    instance->id = id;
    instance->content = g_strdup (content);
    instance->capacity = capacity;
    instance->created = created;
    instance->empty_since = now_us;
    instance->members = g_ptr_array_new_with_free_func (g_free);
    instance->inside = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    for (i = 0; members != NULL && members[i] != NULL; i++)
    {
        g_ptr_array_add (instance->members, g_strdup (members[i]));
        map_account (self, members[i], id);
    }
    key = g_new (guint64, 1);
    *key = id;
    g_hash_table_replace (self->instances, key, instance);
    return instance;
}

/* checks a member list against the table; @seen catches duplicates across lists */
static gboolean
check_members (LrgMmoInstances *self, const gchar * const *members, guint capacity,
               GHashTable *seen, GError **error)
{
    guint i;
    for (i = 0; members != NULL && members[i] != NULL; i++)
    {
        if (!_lrg_mmo_id_valid (members[i]))
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid instance member");
        if (g_hash_table_contains (seen, members[i]))
            return _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "Duplicate instance member");
        if (g_hash_table_contains (self->by_account, members[i]))
            return _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "Account already in an instance");
        g_hash_table_add (seen, g_strdup (members[i]));
    }
    if (i > capacity)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "More members than seats");
    return TRUE;
}

guint64
lrg_mmo_instances_create (LrgMmoInstances *self, const gchar *content, guint capacity,
                          const gchar * const *members, gint64 now_us, GError **error)
{
    g_autoptr(GHashTable) seen = NULL;
    guint64 id;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), 0);
    if (!_lrg_mmo_id_valid (content) || capacity == 0 || capacity > MAX_CAPACITY || now_us < 0)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid instance");
        return 0;
    }
    if (g_hash_table_size (self->instances) >= self->max_instances)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Every instance is in use");
        return 0;
    }
    seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    if (!check_members (self, members, capacity, seen, error))
        return 0;
    id = self->next_id++;
    insert (self, id, content, capacity, members, now_us, now_us);
    return id;
}

gboolean
lrg_mmo_instances_close (LrgMmoInstances *self, guint64 id)
{
    Instance *instance;
    guint i;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), FALSE);
    instance = lookup (self, id);
    if (instance == NULL)
        return FALSE;
    for (i = 0; i < instance->members->len; i++)
        g_hash_table_remove (self->by_account, g_ptr_array_index (instance->members, i));
    g_hash_table_remove (self->instances, &id);
    return TRUE;
}

gboolean
lrg_mmo_instances_has (LrgMmoInstances *self, guint64 id)
{
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), FALSE);
    return lookup (self, id) != NULL;
}

guint
lrg_mmo_instances_get_count (LrgMmoInstances *self)
{
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), 0);
    return g_hash_table_size (self->instances);
}

static gint
compare_ids (gconstpointer a, gconstpointer b)
{
    guint64 x = *(const guint64 *) a, y = *(const guint64 *) b;
    return x < y ? -1 : x > y ? 1 : 0;
}

GArray *
lrg_mmo_instances_list (LrgMmoInstances *self)
{
    GHashTableIter iter;
    gpointer value;
    GArray *out;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), NULL);
    out = g_array_new (FALSE, FALSE, sizeof (guint64));
    g_hash_table_iter_init (&iter, self->instances);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        g_array_append_val (out, ((Instance *) value)->id);
    /* IDs grow with time: ascending is oldest first */
    g_array_sort (out, compare_ids);
    return out;
}

const gchar *
lrg_mmo_instances_get_content (LrgMmoInstances *self, guint64 id)
{
    Instance *instance;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), NULL);
    instance = lookup (self, id);
    return instance != NULL ? instance->content : NULL;
}

guint
lrg_mmo_instances_get_capacity (LrgMmoInstances *self, guint64 id)
{
    Instance *instance;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), 0);
    instance = lookup (self, id);
    return instance != NULL ? instance->capacity : 0;
}

gint64
lrg_mmo_instances_get_created (LrgMmoInstances *self, guint64 id)
{
    Instance *instance;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), 0);
    instance = lookup (self, id);
    return instance != NULL ? instance->created : 0;
}

GPtrArray *
lrg_mmo_instances_get_members (LrgMmoInstances *self, guint64 id)
{
    Instance *instance;
    GPtrArray *out;
    guint i;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), NULL);
    instance = lookup (self, id);
    if (instance == NULL)
        return NULL;
    out = g_ptr_array_new_with_free_func (g_free);
    for (i = 0; i < instance->members->len; i++)
        g_ptr_array_add (out, g_strdup (g_ptr_array_index (instance->members, i)));
    return out;
}

guint64
lrg_mmo_instances_find (LrgMmoInstances *self, const gchar *account)
{
    guint64 *id;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), 0);
    if (account == NULL)
        return 0;
    id = g_hash_table_lookup (self->by_account, account);
    return id != NULL ? *id : 0;
}

gboolean
lrg_mmo_instances_add_member (LrgMmoInstances *self, guint64 id, const gchar *account,
                              GError **error)
{
    Instance *instance;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), FALSE);
    instance = lookup (self, id);
    if (instance == NULL)
        return _lrg_mmo_fail (error, G_IO_ERROR_NOT_FOUND, "No such instance");
    if (!_lrg_mmo_id_valid (account))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid instance member");
    if (g_hash_table_contains (self->by_account, account))
        return _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "Account already in an instance");
    if (instance->members->len >= instance->capacity)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "The instance is full");
    g_ptr_array_add (instance->members, g_strdup (account));
    map_account (self, account, id);
    return TRUE;
}

static void
leave_instance (Instance *instance, const gchar *account, gint64 now_us)
{
    if (g_hash_table_remove (instance->inside, account) && g_hash_table_size (instance->inside) == 0)
        instance->empty_since = now_us;
}

guint64
lrg_mmo_instances_remove_member (LrgMmoInstances *self, const gchar *account, gint64 now_us)
{
    Instance *instance;
    guint64 id;
    guint i;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), 0);
    id = lrg_mmo_instances_find (self, account);
    instance = lookup (self, id);
    if (instance == NULL)
        return 0;
    leave_instance (instance, account, now_us);
    for (i = 0; i < instance->members->len; i++)
        if (g_str_equal (g_ptr_array_index (instance->members, i), account))
        {
            g_ptr_array_remove_index (instance->members, i);
            break;
        }
    g_hash_table_remove (self->by_account, account);
    return id;
}

gboolean
lrg_mmo_instances_enter (LrgMmoInstances *self, guint64 id, const gchar *account, GError **error)
{
    Instance *instance;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), FALSE);
    instance = lookup (self, id);
    if (instance == NULL)
        return _lrg_mmo_fail (error, G_IO_ERROR_NOT_FOUND, "No such instance");
    if (lrg_mmo_instances_find (self, account) != id)
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Not a member of the instance");
    g_hash_table_add (instance->inside, g_strdup (account));
    return TRUE;
}

void
lrg_mmo_instances_leave (LrgMmoInstances *self, const gchar *account, gint64 now_us)
{
    Instance *instance;
    g_return_if_fail (LRG_IS_MMO_INSTANCES (self));
    instance = lookup (self, lrg_mmo_instances_find (self, account));
    if (instance != NULL)
        leave_instance (instance, account, now_us);
}

gboolean
lrg_mmo_instances_is_inside (LrgMmoInstances *self, const gchar *account)
{
    Instance *instance;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), FALSE);
    instance = lookup (self, lrg_mmo_instances_find (self, account));
    return instance != NULL && g_hash_table_contains (instance->inside, account);
}

guint
lrg_mmo_instances_get_inside (LrgMmoInstances *self, guint64 id)
{
    Instance *instance;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), 0);
    instance = lookup (self, id);
    return instance != NULL ? g_hash_table_size (instance->inside) : 0;
}

GArray *
lrg_mmo_instances_expire (LrgMmoInstances *self, gint64 empty_us, gint64 lifetime_us, gint64 now_us)
{
    g_autoptr(GArray) open = NULL;
    GArray *closed;
    guint i;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), NULL);
    closed = g_array_new (FALSE, FALSE, sizeof (guint64));
    open = lrg_mmo_instances_list (self);
    for (i = 0; i < open->len; i++)
    {
        guint64 id = g_array_index (open, guint64, i);
        Instance *instance = lookup (self, id);
        gboolean empty_long = g_hash_table_size (instance->inside) == 0 && now_us - instance->empty_since >= empty_us;
        gboolean too_old = lifetime_us != G_MAXINT64 && now_us - instance->created >= lifetime_us;
        if (empty_long || too_old)
        {
            lrg_mmo_instances_close (self, id);
            g_array_append_val (closed, id);
        }
    }
    return closed;
}

GVariant *
lrg_mmo_instances_save (LrgMmoInstances *self, gint64 now_us)
{
    g_autoptr(GArray) open = NULL;
    GVariantBuilder builder;
    guint i, j;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), NULL);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(tsuxas)"));
    open = lrg_mmo_instances_list (self);
    for (i = 0; i < open->len; i++)
    {
        Instance *instance = lookup (self, g_array_index (open, guint64, i));
        GVariantBuilder members;
        g_variant_builder_init (&members, G_VARIANT_TYPE ("as"));
        for (j = 0; j < instance->members->len; j++)
            g_variant_builder_add (&members, "s", (const gchar *) g_ptr_array_index (instance->members, j));
        g_variant_builder_add (&builder, "(tsuxas)", instance->id, instance->content, instance->capacity,
                               MAX (now_us - instance->created, (gint64) 0), &members);
    }
    return g_variant_ref_sink (g_variant_new ("(ta(tsuxas))", self->next_id, &builder));
}

gboolean
lrg_mmo_instances_load (LrgMmoInstances *self, GVariant *saved, gint64 now_us, GError **error)
{
    g_autoptr(GVariant) list = NULL;
    g_autoptr(GHashTable) seen = NULL;
    g_autoptr(GHashTable) ids = NULL;
    GVariantIter iter;
    guint64 next_id, id;
    const gchar *content;
    guint capacity;
    gint64 age;
    GVariant *members;
    g_return_val_if_fail (LRG_IS_MMO_INSTANCES (self), FALSE);
    if (saved == NULL || !g_variant_is_of_type (saved, G_VARIANT_TYPE ("(ta(tsuxas))")) || now_us < 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid saved instances");
    if (g_hash_table_size (self->instances) > 0)
        return _lrg_mmo_fail (error, G_IO_ERROR_EXISTS, "The instance table is not empty");
    g_variant_get (saved, "(t@a(tsuxas))", &next_id, &list);
    if (g_variant_n_children (list) > self->max_instances)
        return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Too many saved instances");

    /* --- every instance checked before any is opened --- */
    seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    ids = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
    g_variant_iter_init (&iter, list);
    while (g_variant_iter_next (&iter, "(t&sux@as)", &id, &content, &capacity, &age, &members))
    {
        g_autofree const gchar **strv = g_variant_get_strv (members, NULL);
        guint64 *key;
        gboolean ok;
        ok = id != 0 && id < next_id && !g_hash_table_contains (ids, &id) && _lrg_mmo_id_valid (content) &&
             capacity > 0 && capacity <= MAX_CAPACITY && age >= 0;
        if (!ok)
        {
            g_variant_unref (members);
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid saved instance");
        }
        ok = check_members (self, strv, capacity, seen, error);
        g_variant_unref (members);
        if (!ok)
            return FALSE;
        key = g_new (guint64, 1);
        *key = id;
        g_hash_table_add (ids, key);
    }

    /* --- then opened with their ages, everybody outside --- */
    g_variant_iter_init (&iter, list);
    while (g_variant_iter_next (&iter, "(t&sux@as)", &id, &content, &capacity, &age, &members))
    {
        g_autofree const gchar **strv = g_variant_get_strv (members, NULL);
        insert (self, id, content, capacity, strv, now_us - age, now_us);
        g_variant_unref (members);
    }
    self->next_id = next_id;
    return TRUE;
}
