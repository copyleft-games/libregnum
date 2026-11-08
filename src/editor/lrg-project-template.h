/* lrg-project-template.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Built-in project templates.
 *
 * "empty" writes project.ryaml, an empty asset directory and a one-node
 * level. "mod" is that project plus mods/example/mod.yaml and a script
 * entry point.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_PROJECT_TEMPLATE_ERROR (lrg_project_template_error_quark ())

typedef enum
{
	LRG_PROJECT_TEMPLATE_ERROR_FAILED,
	LRG_PROJECT_TEMPLATE_ERROR_EXISTS,
	LRG_PROJECT_TEMPLATE_ERROR_UNKNOWN
} LrgProjectTemplateError;

typedef struct
{
	const gchar *id;
	const gchar *title;
	const gchar *summary;
} LrgProjectTemplateInfo;

LRG_AVAILABLE_IN_ALL
GQuark lrg_project_template_error_quark (void);

LRG_AVAILABLE_IN_ALL
guint lrg_project_template_count (void);

/**
 * lrg_project_template_get:
 * @index: an index below lrg_project_template_count()
 *
 * Returns: (transfer none) (nullable): the template, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const LrgProjectTemplateInfo * lrg_project_template_get (guint index);

/**
 * lrg_project_template_instantiate:
 * @id: "empty" or "mod"
 * @dest: (type filename): a directory that does not yet contain project.ryaml
 * @name: project name
 * @error: (nullable): return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_project_template_instantiate (const gchar  *id,
                                           const gchar  *dest,
                                           const gchar  *name,
                                           GError      **error);

G_END_DECLS
