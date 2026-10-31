/* lrgeditorctl-format.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Output and input formats of lrgeditorctl: YAML (the default for single
 * objects), JSON, tables and names; parsing YAML/JSON documents for
 * `edit` and `apply`; and turning `key=value` arguments into JSON.
 */

#pragma once

#include <glib.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

typedef enum
{
	LRG_CTL_FORMAT_DEFAULT,   /* table for lists, YAML for objects */
	LRG_CTL_FORMAT_YAML,
	LRG_CTL_FORMAT_JSON,
	LRG_CTL_FORMAT_TABLE,
	LRG_CTL_FORMAT_WIDE,
	LRG_CTL_FORMAT_NAME
} LrgCtlFormat;

/**
 * LrgCtlColumn:
 * @header: column title
 * @path: member of each row object (dots descend: "selection.primary")
 * @wide: only shown with -o wide
 *
 * One column of a table.
 */
typedef struct
{
	const gchar *header;
	const gchar *path;
	gboolean     wide;
} LrgCtlColumn;

gboolean   lrg_ctl_parse_format   (const gchar         *text,
                                   LrgCtlFormat        *format);
gchar *    lrg_ctl_to_yaml        (JsonNode            *node);
gchar *    lrg_ctl_to_json        (JsonNode            *node,
                                   gboolean             pretty);
void       lrg_ctl_print          (JsonNode            *node,
                                   LrgCtlFormat         format);
void       lrg_ctl_print_table    (JsonArray           *rows,
                                   const LrgCtlColumn  *columns,
                                   gsize                n_columns,
                                   LrgCtlFormat         format,
                                   const gchar         *name_path);
gchar *    lrg_ctl_cell           (JsonNode            *row,
                                   const gchar         *path);
GPtrArray * lrg_ctl_parse_documents (const gchar       *text,
                                     GError           **error);
JsonNode * lrg_ctl_parse_value    (const gchar         *text);
gboolean   lrg_ctl_parse_assignments (gchar * const    *args,
                                      JsonObject       *into,
                                      GError          **error);

G_END_DECLS
