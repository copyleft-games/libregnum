/* lrg-po.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Gettext PO interchange for #LrgLocale: import, export, `@key` / tr()
 * extraction and a pseudolocale for overflow testing.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "lrg-locale.h"

G_BEGIN_DECLS

#define LRG_PO_ERROR (lrg_po_error_quark ())

/**
 * LrgPoError:
 * @LRG_PO_ERROR_FAILED: the PO file could not be read or parsed
 */
typedef enum
{
	LRG_PO_ERROR_FAILED
} LrgPoError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_po_error_quark (void);

/**
 * lrg_po_import_file:
 * @locale: the locale that receives the strings
 * @path: (type filename): a gettext `.po` file
 * @error: (nullable): return location for a #GError
 *
 * Reads msgid/msgstr pairs. msgstr[0] is the singular and
 * #LRG_PLURAL_ONE form; msgstr[1] is #LRG_PLURAL_OTHER; further indexes
 * map onto FEW, MANY, TWO and ZERO. The empty header msgid is skipped.
 *
 * Returns: the number of entries imported, or -1 on error
 */
LRG_AVAILABLE_IN_ALL
gint lrg_po_import_file (LrgLocale    *locale,
                         const gchar  *path,
                         GError      **error);

/**
 * lrg_po_export:
 * @locale: a locale
 *
 * Writes a UTF-8 PO document of the locale's singular strings.
 *
 * Returns: (transfer full): the PO text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_po_export (LrgLocale *locale);

/**
 * lrg_po_export_file:
 * @locale: a locale
 * @path: (type filename): destination `.po` path
 * @error: (nullable): return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_po_export_file (LrgLocale    *locale,
                             const gchar  *path,
                             GError      **error);

/**
 * lrg_po_extract:
 * @text: source text (YAML, C or a script)
 *
 * Collects translation keys: an `@` token (`@hello.world`) and the
 * string inside `tr("...")`, `tr('...')` or `_("...")`.
 *
 * Returns: (transfer full) (array zero-terminated=1): unique keys, in order
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_po_extract (const gchar *text);

/**
 * lrg_po_pseudolocalize:
 * @source: the locale to transform
 *
 * Builds a `qps-ploc` locale. Each string is wrapped in brackets and
 * its letters are expanded with accented lookalikes so a layout that
 * overflows is obvious.
 *
 * Returns: (transfer full): the pseudolocale
 */
LRG_AVAILABLE_IN_ALL
LrgLocale * lrg_po_pseudolocalize (LrgLocale *source);

G_END_DECLS
