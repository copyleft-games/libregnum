/* lrg-inspector-nested.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Read and write a value inside a nested GVariant document: a{sv}
 * dictionaries, av arrays and variant wrappers. The inspector stores
 * one document and edits it by a slash path such as "items/0/name".
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * lrg_inspector_nested_get:
 * @root: a dictionary, array or variant
 * @path: slash-separated keys and array indexes
 * @error: (nullable): return location for a #GError
 *
 * Walks @path. A variant wrapper is unwrapped without consuming a
 * token. The result is the leaf value.
 *
 * Returns: (transfer full) (nullable): the value at @path
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_inspector_nested_get (GVariant     *root,
                                     const gchar  *path,
                                     GError      **error);

/**
 * lrg_inspector_nested_set:
 * @root: a dictionary, array or variant
 * @path: slash-separated keys and array indexes
 * @value: (transfer none): the new leaf
 * @error: (nullable): return location for a #GError
 *
 * Returns a new tree with the leaf replaced. A missing dictionary key
 * at the leaf is created. A missing intermediate is an error. @root is
 * not modified.
 *
 * Returns: (transfer full) (nullable): the updated tree
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_inspector_nested_set (GVariant     *root,
                                     const gchar  *path,
                                     GVariant     *value,
                                     GError      **error);

G_END_DECLS
