/* lrg-save-context-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal save-context helpers shared with the save manager: finishing a
 * save context into a document on the caller's thread and generating its
 * text elsewhere (the save manager's background writer).
 */

#pragma once

#include <yaml-glib.h>
#include "lrg-save-context.h"

G_BEGIN_DECLS

YamlDocument *lrg_save_context_take_document      (LrgSaveContext  *self,
                                                   GError         **error);

gchar        *lrg_save_context_document_to_string (YamlDocument    *doc,
                                                   GError         **error);

G_END_DECLS
