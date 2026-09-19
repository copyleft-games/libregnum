/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-social.h"
#include "lrg-mmo-service-private.h"

struct _LrgMmoSocial
{
    GObject parent_instance;
    LrgMmoStore *store;
};
G_DEFINE_TYPE (LrgMmoSocial, lrg_mmo_social, G_TYPE_OBJECT)
static void
lrg_mmo_social_dispose (GObject *object)
{
    g_clear_object (&LRG_MMO_SOCIAL (object)->store);
    G_OBJECT_CLASS (lrg_mmo_social_parent_class)->dispose (object);
}
static void
lrg_mmo_social_class_init (LrgMmoSocialClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = lrg_mmo_social_dispose;
}
static void
lrg_mmo_social_init (LrgMmoSocial *self)
{
}
LrgMmoSocial *
lrg_mmo_social_new (LrgMmoStore *store)
{
    LrgMmoSocial *self;
    g_return_val_if_fail (LRG_IS_MMO_STORE (store), NULL);
    self = g_object_new (LRG_TYPE_MMO_SOCIAL, NULL);
    self->store = g_object_ref (store);
    return self;
}

static GVariant *
load_guild (LrgMmoSocial *self, const gchar *guild, guint64 *revision, GError **error)
{
    g_autofree gchar *key = NULL;
    if (!_lrg_mmo_id_valid (guild))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid guild ID");
        return NULL;
    }
    key = g_strconcat ("social/guild/", guild, NULL);
    return _lrg_mmo_load (self->store, key, "(sa{su}a{sx})", revision, error);
}

GVariant *
lrg_mmo_social_get_guild (LrgMmoSocial *self, const gchar *guild, GError **error)
{
    guint64 revision;
    g_return_val_if_fail (LRG_IS_MMO_SOCIAL (self), NULL);
    return load_guild (self, guild, &revision, error);
}

gboolean
lrg_mmo_social_create_guild (LrgMmoSocial *self, const gchar *actor, const gchar *guild, GError **error)
{
    g_autofree gchar *key = NULL;
    GVariantBuilder members, invites;
    g_return_val_if_fail (LRG_IS_MMO_SOCIAL (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (guild))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid guild or founder");
    key = g_strconcat ("social/guild/", guild, NULL);
    g_variant_builder_init (&members, G_VARIANT_TYPE ("a{su}"));
    g_variant_builder_init (&invites, G_VARIANT_TYPE ("a{sx}"));
    g_variant_builder_add (&members, "{su}", actor, 3u);
    return _lrg_mmo_put (self->store, key, 0,
                         g_variant_new ("(s@a{su}@a{sx})", actor,
                                        g_variant_builder_end (&members), g_variant_builder_end (&invites)), error);
}

static gboolean
change_guild (LrgMmoSocial *self, const gchar *actor, const gchar *guild,
              const gchar *target, guint role, guint action, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) members = NULL;
    g_autoptr(GVariant) invites = NULL;
    g_autoptr(GHashTable) member_map = NULL;
    g_autoptr(GHashTable) invite_map = NULL;
    g_autofree gchar *key = NULL;
    GVariantIter iter;
    const gchar *leader, *id;
    guint current, target_role;
    gint64 deadline;
    gint64 now = g_get_real_time () / G_TIME_SPAN_SECOND;
    guint64 revision;
    GVariantBuilder member_builder, invite_builder;
    GHashTableIter hash_iter;
    gpointer map_key, map_value;

    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (target) || role > 3)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid guild mutation");
    value = load_guild (self, guild, &revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get (value, "(&s@a{su}@a{sx})", &leader, &members, &invites);
    member_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    invite_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    g_variant_iter_init (&iter, members);
    while (g_variant_iter_next (&iter, "{&su}", &id, &current))
        g_hash_table_insert (member_map, g_strdup (id), GUINT_TO_POINTER (current));
    g_variant_iter_init (&iter, invites);
    while (g_variant_iter_next (&iter, "{&sx}", &id, &deadline))
        if (deadline > now)
        {
            gint64 *expiry = g_new (gint64, 1);
            *expiry = deadline;
            g_hash_table_insert (invite_map, g_strdup (id), expiry);
        }
    current = GPOINTER_TO_UINT (g_hash_table_lookup (member_map, actor));
    target_role = GPOINTER_TO_UINT (g_hash_table_lookup (member_map, target));
    if (action == 0)
    {
        gint64 *expiry;
        if (current < 2 || target_role != 0)
            goto denied;
        if (!g_hash_table_contains (invite_map, target) && g_hash_table_size (invite_map) >= 128)
            goto full;
        expiry = g_new (gint64, 1);
        *expiry = now + 3600;
        g_hash_table_replace (invite_map, g_strdup (target), expiry);
    }
    else if (action == 1)
    {
        if (current != 0 || *leader == '\0' || !g_hash_table_contains (invite_map, actor))
            goto denied;
        if (g_hash_table_size (member_map) >= 128)
            goto full;
        g_hash_table_remove (invite_map, actor);
        g_hash_table_insert (member_map, g_strdup (actor), GUINT_TO_POINTER (1));
    }
    else if (action == 2)
    {
        if (current != 3 || !g_str_equal (actor, leader) || target_role == 0 || g_str_equal (actor, target))
            goto denied;
        if (role == 0)
            g_hash_table_remove (member_map, target);
        else
            g_hash_table_replace (member_map, g_strdup (target), GUINT_TO_POINTER (role));
        if (role == 3)
        {
            leader = target;
            g_hash_table_replace (member_map, g_strdup (actor), GUINT_TO_POINTER (2));
        }
    }
    else
    {
        if (current == 0 || (current == 3 && g_hash_table_size (member_map) > 1))
            goto denied;
        g_hash_table_remove (member_map, actor);
        if (current == 3)
        {
            leader = "";
            g_hash_table_remove_all (invite_map);
        }
    }
    g_variant_builder_init (&member_builder, G_VARIANT_TYPE ("a{su}"));
    g_hash_table_iter_init (&hash_iter, member_map);
    while (g_hash_table_iter_next (&hash_iter, &map_key, &map_value))
        g_variant_builder_add (&member_builder, "{su}", map_key, GPOINTER_TO_UINT (map_value));
    g_variant_builder_init (&invite_builder, G_VARIANT_TYPE ("a{sx}"));
    g_hash_table_iter_init (&hash_iter, invite_map);
    while (g_hash_table_iter_next (&hash_iter, &map_key, &map_value))
        g_variant_builder_add (&invite_builder, "{sx}", map_key, *(gint64 *) map_value);
    key = g_strconcat ("social/guild/", guild, NULL);
    return _lrg_mmo_put (self->store, key, revision,
                         g_variant_new ("(s@a{su}@a{sx})", leader,
                                        g_variant_builder_end (&member_builder),
                                        g_variant_builder_end (&invite_builder)), error);
denied:
    return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Guild operation is not authorized");
full:
    return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Guild membership or invitation capacity reached");
}

gboolean
lrg_mmo_social_invite (LrgMmoSocial *self, const gchar *actor, const gchar *guild, const gchar *target, GError **error)
{
    g_return_val_if_fail (LRG_IS_MMO_SOCIAL (self), FALSE);
    return change_guild (self, actor, guild, target, 0, 0, error);
}

gboolean
lrg_mmo_social_join (LrgMmoSocial *self, const gchar *actor, const gchar *guild, GError **error)
{
    g_return_val_if_fail (LRG_IS_MMO_SOCIAL (self), FALSE);
    return change_guild (self, actor, guild, actor, 0, 1, error);
}

gboolean
lrg_mmo_social_set_role (LrgMmoSocial *self, const gchar *actor, const gchar *guild,
                        const gchar *target, guint role, GError **error)
{
    g_return_val_if_fail (LRG_IS_MMO_SOCIAL (self), FALSE);
    return change_guild (self, actor, guild, target, role, 2, error);
}

gboolean
lrg_mmo_social_leave (LrgMmoSocial *self, const gchar *actor, const gchar *guild, GError **error)
{
    g_return_val_if_fail (LRG_IS_MMO_SOCIAL (self), FALSE);
    return change_guild (self, actor, guild, actor, 0, 3, error);
}

static gchar *
block_key (const gchar *actor, const gchar *target)
{
    return g_strdup_printf ("social/block/%s/%s", actor, target);
}

static gboolean
get_block (LrgMmoSocial *self, const gchar *key, guint64 *revision, gboolean *blocked, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GError) local_error = NULL;
    value = _lrg_mmo_load (self->store, key, "b", revision, &local_error);
    if (value == NULL && !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
    }
    *blocked = value != NULL && g_variant_get_boolean (value);
    return TRUE;
}

gboolean
lrg_mmo_social_block (LrgMmoSocial *self, const gchar *actor, const gchar *target, gboolean blocked, GError **error)
{
    g_autofree gchar *key = NULL;
    guint64 revision;
    gboolean current;
    g_return_val_if_fail (LRG_IS_MMO_SOCIAL (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (target) || g_str_equal (actor, target))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid block target");
    key = block_key (actor, target);
    if (!get_block (self, key, &revision, &current, error))
        return FALSE;
    return _lrg_mmo_put (self->store, key, revision, g_variant_new_boolean (blocked), error);
}

static GVariant *
inbox (LrgMmoSocial *self, const gchar *actor, guint64 *revision, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GError) local_error = NULL;
    if (!_lrg_mmo_id_valid (actor))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid inbox owner");
        return NULL;
    }
    key = g_strconcat ("social/inbox/", actor, NULL);
    value = _lrg_mmo_load (self->store, key, "a(sss)", revision, &local_error);
    if (value == NULL)
    {
        if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            return g_variant_ref_sink (g_variant_new_array (G_VARIANT_TYPE ("(sss)"), NULL, 0));
        g_propagate_error (error, g_steal_pointer (&local_error));
        return NULL;
    }
    return g_steal_pointer (&value);
}

GVariant *
lrg_mmo_social_read_inbox (LrgMmoSocial *self, const gchar *actor, GError **error)
{
    guint64 revision;
    g_return_val_if_fail (LRG_IS_MMO_SOCIAL (self), NULL);
    return inbox (self, actor, &revision, error);
}

gboolean
lrg_mmo_social_send_message (LrgMmoSocial *self, const gchar *actor, const gchar *recipient,
                            const gchar *text, const gchar *operation, GError **error)
{
    g_autofree gchar *forward = NULL;
    g_autofree gchar *reverse = NULL;
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) intent = NULL;
    g_autoptr(GVariant) messages = NULL;
    g_autoptr(GVariant) batch = NULL;
    guint64 forward_revision, reverse_revision, inbox_revision;
    gboolean forward_block, reverse_block;
    gint status;
    gsize i, count;
    GVariantBuilder history, changes;
    g_return_val_if_fail (LRG_IS_MMO_SOCIAL (self), FALSE);
    if (!_lrg_mmo_id_valid (actor) || !_lrg_mmo_id_valid (recipient) || g_str_equal (actor, recipient) ||
        text == NULL || *text == '\0' || strlen (text) > 2048 || !g_utf8_validate (text, -1, NULL))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid private message");
    intent = g_variant_ref_sink (g_variant_new ("(ssss)", "message", actor, recipient, text));
    status = _lrg_mmo_operation_check (self->store, operation, intent, error);
    if (status != 0)
        return status > 0;
    forward = block_key (actor, recipient);
    reverse = block_key (recipient, actor);
    if (!get_block (self, forward, &forward_revision, &forward_block, error) ||
        !get_block (self, reverse, &reverse_revision, &reverse_block, error))
        return FALSE;
    if (forward_block || reverse_block)
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Private messaging is blocked");
    messages = inbox (self, recipient, &inbox_revision, error);
    if (messages == NULL)
        return FALSE;
    count = g_variant_n_children (messages);
    g_variant_builder_init (&history, G_VARIANT_TYPE ("a(sss)"));
    for (i = count > 199 ? count - 199 : 0; i < count; i++)
    {
        g_autoptr(GVariant) item = g_variant_get_child_value (messages, i);
        g_variant_builder_add_value (&history, item);
    }
    g_variant_builder_add (&history, "(sss)", operation, actor, text);
    key = g_strconcat ("social/inbox/", recipient, NULL);
    g_variant_builder_init (&changes, G_VARIANT_TYPE ("a(stay)"));
    _lrg_mmo_change (&changes, key, inbox_revision, g_variant_builder_end (&history));
    /* Include block revisions in the write set to close the authorization race. */
    _lrg_mmo_change (&changes, forward, forward_revision, g_variant_new_boolean (FALSE));
    _lrg_mmo_change (&changes, reverse, reverse_revision, g_variant_new_boolean (FALSE));
    _lrg_mmo_operation_add (&changes, operation, intent);
    batch = g_variant_ref_sink (g_variant_builder_end (&changes));
    return lrg_mmo_store_commit (self->store, batch, error);
}
