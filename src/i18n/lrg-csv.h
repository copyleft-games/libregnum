/* lrg-csv.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A small RFC 4180 CSV reader for string tables and data files.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_CSV_ERROR:
 *
 * Error domain for CSV parsing errors.
 */
#define LRG_CSV_ERROR (lrg_csv_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_csv_error_quark (void);

/**
 * LrgCsvError:
 * @LRG_CSV_ERROR_INVALID_UTF8: the text is not valid UTF-8
 * @LRG_CSV_ERROR_UNTERMINATED_QUOTE: a quoted field runs to the end of the text
 * @LRG_CSV_ERROR_MALFORMED: a closing quote is followed by something other
 *   than a separator or a line end
 *
 * Error codes of lrg_csv_parse().
 */
typedef enum
{
    LRG_CSV_ERROR_INVALID_UTF8,
    LRG_CSV_ERROR_UNTERMINATED_QUOTE,
    LRG_CSV_ERROR_MALFORMED
} LrgCsvError;

/**
 * lrg_csv_parse:
 * @text: (array length=length) (element-type guint8): the CSV text (UTF-8)
 * @length: length of @text in bytes, or -1 when it is nul-terminated
 * @separator: the field separator, usually ','
 * @error: return location for a #GError, or %NULL
 *
 * Parses CSV text into rows of fields (RFC 4180): fields are separated
 * by @separator; a field that starts with a double quote runs to the
 * matching closing quote and may hold separators, line breaks and
 * doubled quotes (`""` is one quote); rows end at LF, CRLF or a lone CR
 * (line breaks inside quoted fields are kept as they are written); a
 * leading UTF-8 byte order mark is skipped; empty lines produce no row.
 * Unquoted fields are taken verbatim (no trimming), a quote inside an
 * unquoted field is an ordinary character.
 *
 * Returns: (transfer full) (element-type GStrv) (nullable): one
 *   %NULL-terminated string array per row, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_csv_parse (const gchar  *text,
                          gssize        length,
                          gchar         separator,
                          GError      **error);

G_END_DECLS
