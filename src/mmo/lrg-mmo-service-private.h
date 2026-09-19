/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "lrg-mmo-store.h"
G_GNUC_INTERNAL GVariant *_lrg_mmo_load (LrgMmoStore *store, const gchar *key, const gchar *type, guint64 *revision, GError **error);
G_GNUC_INTERNAL gboolean _lrg_mmo_put (LrgMmoStore *store, const gchar *key, guint64 revision, GVariant *value, GError **error);
G_GNUC_INTERNAL void _lrg_mmo_change (GVariantBuilder *batch, const gchar *key, guint64 revision, GVariant *value);
G_GNUC_INTERNAL gboolean _lrg_mmo_id_valid (const gchar *id);
G_GNUC_INTERNAL gboolean _lrg_mmo_fail (GError **error, GIOErrorEnum code, const gchar *message);
G_GNUC_INTERNAL gint _lrg_mmo_operation_check (LrgMmoStore *store, const gchar *operation, GVariant *intent, GError **error);
G_GNUC_INTERNAL void _lrg_mmo_operation_add (GVariantBuilder *batch, const gchar *operation, GVariant *intent);
