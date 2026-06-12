/* lrg-cad-manager.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Singleton service for CAD documents in scenes: a document cache
 * keyed by absolute path with file monitoring (the ::document-changed
 * signal drives rebakes), a bake cache keyed by (path, overrides,
 * deflection), and the bridge from LRG_NODE_VISUAL_CAD_PART nodes'
 * "cad:"-prefixed param bags to cad-glib parameter overrides.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <cad-glib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-cad-baker.h"

G_BEGIN_DECLS

#define LRG_TYPE_CAD_MANAGER (lrg_cad_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCadManager, lrg_cad_manager,
                      LRG, CAD_MANAGER, GObject)

/**
 * lrg_cad_manager_get_default:
 *
 * Returns: (transfer none): the process-wide CAD manager
 */
LRG_AVAILABLE_IN_ALL
LrgCadManager * lrg_cad_manager_get_default (void);

/**
 * lrg_cad_manager_load:
 * @self: the manager
 * @path: a part source file (.cad/.ccad)
 * @error: return location for a #GError
 *
 * Loads (or returns the cached) document for @path and starts
 * monitoring the file; on-disk changes invalidate caches and emit
 * #LrgCadManager::document-changed.
 *
 * Returns: (transfer none) (nullable): the document, or %NULL
 */
LRG_AVAILABLE_IN_ALL
CadDocument * lrg_cad_manager_load (LrgCadManager  *self,
                                    const gchar    *path,
                                    GError        **error);

/**
 * lrg_cad_manager_bake:
 * @self: the manager
 * @path: a part source file
 * @overrides: (nullable): parameter overrides (name -> gdouble*)
 * @deflection: tessellation quality (<= 0 for default)
 * @part: (nullable): which part, or %NULL for the first
 * @error: return location for a #GError
 *
 * Returns the cached bake for this exact (path, overrides,
 * deflection, part) combination, baking on miss.  Headless-safe.
 *
 * Returns: (transfer none) (nullable): the bake result, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgCadBakeResult * lrg_cad_manager_bake (LrgCadManager  *self,
                                         const gchar    *path,
                                         GHashTable     *overrides,
                                         gdouble         deflection,
                                         const gchar    *part,
                                         GError        **error);

/**
 * lrg_cad_manager_set_source:
 * @self: the manager
 * @path: the part source path
 * @source: replacement source text (e.g. an unsaved editor buffer)
 * @error: return location for a #GError
 *
 * Replaces the cached document's in-memory source and drops its bakes
 * so the next bake reflects @source instead of the on-disk file.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cad_manager_set_source (LrgCadManager  *self,
                                     const gchar    *path,
                                     const gchar    *source,
                                     GError        **error);

/**
 * lrg_cad_manager_invalidate:
 * @self: the manager
 * @path: the part source whose caches to drop
 *
 * Drops the document and all bakes for @path (e.g. after an in-editor
 * source change that has not hit disk).
 */
LRG_AVAILABLE_IN_ALL
void lrg_cad_manager_invalidate (LrgCadManager *self,
                                 const gchar   *path);

#ifdef LRG_BUILD_EDITOR
/**
 * lrg_cad_manager_overrides_for_node:
 * @node: a node whose visual is %LRG_NODE_VISUAL_CAD_PART
 *
 * Builds a cad-glib override table from the node's "cad:"-prefixed
 * numeric visual params (the reserved keys "cad:part" and
 * "cad:deflection" are skipped).
 *
 * Returns: (transfer full) (nullable) (element-type utf8 gdouble): the
 *   overrides, or %NULL when there are none
 */
LRG_AVAILABLE_IN_ALL
GHashTable * lrg_cad_manager_overrides_for_node (LrgNode *node);
#endif

G_END_DECLS
