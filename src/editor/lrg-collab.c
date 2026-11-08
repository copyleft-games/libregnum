/* lrg-collab.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-collab.h"

#include <string.h>

typedef struct
{
	guint64  revision;
	gchar   *value;
} PropValue;

typedef struct
{
	gchar *user;
	gfloat x;
	gfloat y;
	gfloat z;
} Presence;

struct _LrgCollabSession
{
	GObject     parent_instance;
	GHashTable *props;     /* "guid\x1fprop" -> PropValue* */
	GHashTable *locks;     /* guid -> user (owned) */
	GArray     *presence;
};

G_DEFINE_QUARK (lrg-collab-error-quark, lrg_collab_error)
G_DEFINE_FINAL_TYPE (LrgCollabSession, lrg_collab_session, G_TYPE_OBJECT)

static void
prop_free (gpointer data)
{
	PropValue *prop = data;

	g_free (prop->value);
	g_free (prop);
}

static gchar *
prop_key (const gchar *guid,
          const gchar *prop)
{
	return g_strconcat (guid, "\x1f", prop, NULL);
}

static void
lrg_collab_session_finalize (GObject *object)
{
	LrgCollabSession *self = LRG_COLLAB_SESSION (object);
	guint             i;

	g_clear_pointer (&self->props, g_hash_table_unref);
	g_clear_pointer (&self->locks, g_hash_table_unref);
	if (self->presence != NULL)
	{
		for (i = 0; i < self->presence->len; i++)
			g_free (g_array_index (self->presence, Presence, i).user);
		g_array_free (self->presence, TRUE);
		self->presence = NULL;
	}
	G_OBJECT_CLASS (lrg_collab_session_parent_class)->finalize (object);
}

static void
lrg_collab_session_class_init (LrgCollabSessionClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_collab_session_finalize;
}

static void
lrg_collab_session_init (LrgCollabSession *self)
{
	self->props = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, prop_free);
	self->locks = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->presence = g_array_new (FALSE, FALSE, sizeof (Presence));
}

LrgCollabSession *
lrg_collab_session_new (void)
{
	return g_object_new (LRG_TYPE_COLLAB_SESSION, NULL);
}

gboolean
lrg_collab_apply (LrgCollabSession  *self,
                  const gchar       *user,
                  const gchar       *guid,
                  guint64            revision,
                  const gchar       *prop,
                  const gchar       *value,
                  GError           **error)
{
	gchar     *key;
	PropValue *stored;
	const gchar *holder;

	g_return_val_if_fail (LRG_IS_COLLAB_SESSION (self), FALSE);
	g_return_val_if_fail (user != NULL && guid != NULL && prop != NULL && value != NULL, FALSE);

	holder = g_hash_table_lookup (self->locks, guid);
	if (holder != NULL && g_strcmp0 (holder, user) != 0)
	{
		g_set_error (error, LRG_COLLAB_ERROR, LRG_COLLAB_ERROR_LOCKED,
		             "node %s is locked by %s", guid, holder);
		return FALSE;
	}
	key = prop_key (guid, prop);
	stored = g_hash_table_lookup (self->props, key);
	if (stored != NULL && revision < stored->revision)
	{
		g_free (key);
		g_set_error (error, LRG_COLLAB_ERROR, LRG_COLLAB_ERROR_STALE,
		             "revision %" G_GUINT64_FORMAT " is older than %" G_GUINT64_FORMAT,
		             revision, stored->revision);
		return FALSE;
	}
	if (stored == NULL)
	{
		stored = g_new0 (PropValue, 1);
		g_hash_table_insert (self->props, key, stored);
	}
	else
		g_free (key);
	stored->revision = revision;
	g_free (stored->value);
	stored->value = g_strdup (value);
	return TRUE;
}

const gchar *
lrg_collab_get (LrgCollabSession *self,
                const gchar      *guid,
                const gchar      *prop)
{
	g_autofree gchar *key = NULL;
	PropValue        *stored;

	g_return_val_if_fail (LRG_IS_COLLAB_SESSION (self), NULL);
	key = prop_key (guid, prop);
	stored = g_hash_table_lookup (self->props, key);
	if (stored == NULL)
		return NULL;
	return stored->value;
}

gboolean
lrg_collab_lock (LrgCollabSession  *self,
                 const gchar       *user,
                 const gchar       *guid,
                 GError           **error)
{
	const gchar *holder;

	g_return_val_if_fail (LRG_IS_COLLAB_SESSION (self), FALSE);
	g_return_val_if_fail (user != NULL && guid != NULL, FALSE);

	holder = g_hash_table_lookup (self->locks, guid);
	if (holder != NULL && g_strcmp0 (holder, user) != 0)
	{
		g_set_error (error, LRG_COLLAB_ERROR, LRG_COLLAB_ERROR_LOCKED,
		             "node %s is locked by %s", guid, holder);
		return FALSE;
	}
	if (holder == NULL)
		g_hash_table_insert (self->locks, g_strdup (guid), g_strdup (user));
	return TRUE;
}

gboolean
lrg_collab_unlock (LrgCollabSession  *self,
                   const gchar       *user,
                   const gchar       *guid,
                   GError           **error)
{
	const gchar *holder;

	g_return_val_if_fail (LRG_IS_COLLAB_SESSION (self), FALSE);
	holder = g_hash_table_lookup (self->locks, guid);
	if (holder == NULL || g_strcmp0 (holder, user) != 0)
	{
		g_set_error (error, LRG_COLLAB_ERROR, LRG_COLLAB_ERROR_FAILED,
		             "node %s is not locked by %s", guid, user);
		return FALSE;
	}
	g_hash_table_remove (self->locks, guid);
	return TRUE;
}

const gchar *
lrg_collab_lock_holder (LrgCollabSession *self,
                        const gchar      *guid)
{
	g_return_val_if_fail (LRG_IS_COLLAB_SESSION (self), NULL);
	return g_hash_table_lookup (self->locks, guid);
}

void
lrg_collab_set_cursor (LrgCollabSession *self,
                       const gchar      *user,
                       gfloat            x,
                       gfloat            y,
                       gfloat            z)
{
	guint i;

	g_return_if_fail (LRG_IS_COLLAB_SESSION (self));
	g_return_if_fail (user != NULL);
	for (i = 0; i < self->presence->len; i++)
	{
		Presence *row = &g_array_index (self->presence, Presence, i);

		if (g_strcmp0 (row->user, user) == 0)
		{
			row->x = x;
			row->y = y;
			row->z = z;
			return;
		}
	}
	{
		Presence row;

		row.user = g_strdup (user);
		row.x = x;
		row.y = y;
		row.z = z;
		g_array_append_val (self->presence, row);
	}
}

guint
lrg_collab_presence_count (LrgCollabSession *self)
{
	g_return_val_if_fail (LRG_IS_COLLAB_SESSION (self), 0);
	return self->presence->len;
}

gboolean
lrg_collab_presence_get (LrgCollabSession  *self,
                         guint              index,
                         const gchar      **user,
                         gfloat            *x,
                         gfloat            *y,
                         gfloat            *z)
{
	Presence *row;

	g_return_val_if_fail (LRG_IS_COLLAB_SESSION (self), FALSE);
	if (index >= self->presence->len)
		return FALSE;
	row = &g_array_index (self->presence, Presence, index);
	if (user != NULL)
		*user = row->user;
	if (x != NULL)
		*x = row->x;
	if (y != NULL)
		*y = row->y;
	if (z != NULL)
		*z = row->z;
	return TRUE;
}
