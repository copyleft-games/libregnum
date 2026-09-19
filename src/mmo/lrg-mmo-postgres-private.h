/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "lrg-mmo-store.h"
#include <libpq-fe.h>
G_GNUC_INTERNAL PGconn *_lrg_mmo_store_postgres (LrgMmoStore *self);
G_GNUC_INTERNAL PGconn *_lrg_mmo_pg_open (const gchar *connection, GError **error);
G_GNUC_INTERNAL GBytes *_lrg_mmo_pg_read (PGconn *db, const gchar *key, guint64 *revision, GError **error);
G_GNUC_INTERNAL gboolean _lrg_mmo_pg_commit (PGconn *db, GVariant *changes, const gchar *operation, const gchar *digest, gboolean *duplicate, const gchar *zone, const gchar *owner, guint64 fence, GError **error);
G_GNUC_INTERNAL GVariant *_lrg_mmo_pg_audit (PGconn *db, guint64 after, guint limit, GError **error);
G_GNUC_INTERNAL guint64 _lrg_mmo_pg_lease (PGconn *db, const gchar *zone, const gchar *owner, guint64 fence, const gchar *destination, const gchar *endpoint, GBytes *state, guint ttl, guint operation, GError **error);
G_GNUC_INTERNAL GVariant *_lrg_mmo_pg_lookup (PGconn *db, const gchar *zone, GError **error);
