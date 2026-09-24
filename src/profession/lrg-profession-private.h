/* lrg-profession-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal helpers shared by the profession module. Not installed.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* Maximum identifier length in bytes accepted by the profession module. */
#define LRG_PROFESSION_ID_MAX_BYTES (128)

/*
 * _lrg_profession_id_valid:
 * @id: (nullable): identifier to check
 *
 * An identifier is valid when it is non-NULL, non-empty, at most
 * LRG_PROFESSION_ID_MAX_BYTES bytes long and valid UTF-8.
 */
G_GNUC_INTERNAL
gboolean _lrg_profession_id_valid (const gchar *id);

G_END_DECLS
