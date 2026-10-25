/* lrg-editor-validate.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Level validation. See lrg-editor-validate.h.
 */

#include "lrg-editor-validate.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "lrg-component-desc.h"
#include "lrg-script-binding.h"
#include "../core/lrg-registry.h"
#include "../lrg-enums.h"
#include <graylib.h>
#include <math.h>
#include <string.h>

G_DEFINE_BOXED_TYPE (LrgEditorProblem, lrg_editor_problem,
                     lrg_editor_problem_copy, lrg_editor_problem_free)

LrgEditorProblem *
lrg_editor_problem_copy (const LrgEditorProblem *problem)
{
	LrgEditorProblem *copy;

	g_return_val_if_fail (problem != NULL, NULL);

	copy = g_new0 (LrgEditorProblem, 1);
	copy->severity = problem->severity;
	copy->code = g_strdup (problem->code);
	copy->node_guid = g_strdup (problem->node_guid);
	copy->node_path = g_strdup (problem->node_path);
	copy->message = g_strdup (problem->message);
	copy->file = g_strdup (problem->file);
	return copy;
}

void
lrg_editor_problem_free (LrgEditorProblem *problem)
{
	if (problem == NULL)
		return;

	g_free (problem->code);
	g_free (problem->node_guid);
	g_free (problem->node_path);
	g_free (problem->message);
	g_free (problem->file);
	g_free (problem);
}

const gchar *
lrg_editor_problem_severity_to_string (LrgEditorProblemSeverity severity)
{
	switch (severity)
	{
	case LRG_EDITOR_PROBLEM_ERROR:
		return "error";
	case LRG_EDITOR_PROBLEM_WARNING:
		return "warning";
	default:
		return "info";
	}
}

typedef struct
{
	const gchar *base_dir;
	LrgRegistry *registry;
	GHashTable  *guids;      /* guid -> first node path */
	GPtrArray   *out;
} Ctx;

static void G_GNUC_PRINTF (6, 7)
report (Ctx                      *ctx,
        LrgEditorProblemSeverity  severity,
        const gchar              *code,
        LrgNode                  *node,
        const gchar              *file,
        const gchar              *format,
        ...)
{
	LrgEditorProblem *p = g_new0 (LrgEditorProblem, 1);
	va_list           args;

	p->severity = severity;
	p->code = g_strdup (code);
	if (node != NULL)
	{
		p->node_guid = g_strdup (lrg_node_get_guid (node));
		p->node_path = lrg_node_get_path (node);
	}
	p->file = g_strdup (file);
	va_start (args, format);
	p->message = g_strdup_vprintf (format, args);
	va_end (args);

	g_ptr_array_add (ctx->out, p);
}

/* Absolute path for an asset/script reference, or NULL for GUID refs. */
static gchar *
resolve_ref (Ctx         *ctx,
             const gchar *ref)
{
	if (ref == NULL || *ref == '\0')
		return NULL;
	if (g_path_is_absolute (ref))
		return g_strdup (ref);
	if (ctx->base_dir == NULL)
		return NULL;
	return g_build_filename (ctx->base_dir, ref, NULL);
}

static gboolean
finite3 (const GrlVector3 *v)
{
	return isfinite (v->x) && isfinite (v->y) && isfinite (v->z);
}

static gboolean
visit (LrgNode  *node,
       gpointer  user_data)
{
	Ctx           *ctx = user_data;
	const gchar   *name = lrg_node_get_name (node);
	const gchar   *guid = lrg_node_get_guid (node);
	LrgNodeVisual *visual = lrg_node_get_visual (node);
	GPtrArray     *arr;
	GrlVector3    *scl;
	guint          i;

	/* The implicit level root is not a document node. */
	if (lrg_node_get_parent (node) == NULL)
		return TRUE;

	/* Identity. */
	if (guid == NULL || *guid == '\0')
		report (ctx, LRG_EDITOR_PROBLEM_ERROR, "missing-guid", node, NULL, "Node has no GUID");
	else if (g_hash_table_contains (ctx->guids, guid))
		report (ctx, LRG_EDITOR_PROBLEM_ERROR, "duplicate-guid", node, NULL,
		        "GUID %s is also used by %s", guid,
		        (const gchar *) g_hash_table_lookup (ctx->guids, guid));
	else
		g_hash_table_insert (ctx->guids, g_strdup (guid), lrg_node_get_path (node));

	if (name == NULL || *name == '\0')
		report (ctx, LRG_EDITOR_PROBLEM_WARNING, "unnamed", node, NULL,
		        "Node has no name (it cannot be referenced by path)");
	else if (strchr (name, '/') != NULL)
		report (ctx, LRG_EDITOR_PROBLEM_WARNING, "slash-in-name", node, NULL,
		        "Name \"%s\" contains '/', which breaks path references", name);

	/* Sibling names must be unique for paths to be unambiguous. */
	if (name != NULL)
	{
		GPtrArray *siblings = lrg_node_get_children (lrg_node_get_parent (node));

		for (i = 0; i < siblings->len; i++)
		{
			LrgNode *s = g_ptr_array_index (siblings, i);

			if (s == node)
				break;
			if (g_strcmp0 (lrg_node_get_name (s), name) == 0)
			{
				report (ctx, LRG_EDITOR_PROBLEM_WARNING, "duplicate-name", node, NULL,
				        "Another sibling is also named \"%s\"", name);
				break;
			}
		}
	}

	/* Transform. */
	if (!finite3 (lrg_node_get_location (node)) || !finite3 (lrg_node_get_rotation (node)) ||
	    !finite3 (lrg_node_get_scale (node)))
		report (ctx, LRG_EDITOR_PROBLEM_ERROR, "non-finite-transform", node, NULL,
		        "Transform contains NaN or infinite values");
	scl = lrg_node_get_scale (node);
	if (scl->x == 0.0f || scl->y == 0.0f || scl->z == 0.0f)
		report (ctx, LRG_EDITOR_PROBLEM_WARNING, "zero-scale", node, NULL,
		        "Scale has a zero component; the node is invisible and cannot be picked");

	/* Asset-backed visuals. */
	if (visual != NULL)
	{
		LrgNodeVisualKind kind = lrg_node_visual_get_kind (visual);
		const gchar      *asset = lrg_node_visual_get_asset (visual);
		gboolean          needs_asset = (kind == LRG_NODE_VISUAL_MESH_ASSET ||
		                                 kind == LRG_NODE_VISUAL_SPRITE ||
		                                 kind == LRG_NODE_VISUAL_TILEMAP ||
		                                 kind == LRG_NODE_VISUAL_PREFAB_INSTANCE ||
		                                 kind == LRG_NODE_VISUAL_CAD_PART);

		if (needs_asset && (asset == NULL || *asset == '\0'))
			report (ctx, LRG_EDITOR_PROBLEM_ERROR, "missing-asset-ref", node, NULL,
			        "Visual needs an asset but none is set");
		else if (needs_asset)
		{
			g_autofree gchar *path = resolve_ref (ctx, asset);

			if (path != NULL && !g_file_test (path, G_FILE_TEST_EXISTS))
				report (ctx, LRG_EDITOR_PROBLEM_ERROR, "missing-asset", node, path,
				        "Asset \"%s\" does not exist", asset);
		}
	}

	/* Components. */
	arr = lrg_node_get_components (node);
	for (i = 0; i < arr->len; i++)
	{
		LrgComponentDesc *desc = g_ptr_array_index (arr, i);
		const gchar      *type = lrg_component_desc_get_type_name (desc);

		if (type == NULL || *type == '\0')
			report (ctx, LRG_EDITOR_PROBLEM_ERROR, "component-no-type", node, NULL,
			        "Component #%u has no type", i);
		else if (ctx->registry != NULL && !lrg_registry_is_registered (ctx->registry, type))
			report (ctx, LRG_EDITOR_PROBLEM_WARNING, "unknown-component", node, NULL,
			        "Component type \"%s\" is not registered; it will be skipped at runtime", type);
	}

	/* Scripts. */
	arr = lrg_node_get_scripts (node);
	for (i = 0; i < arr->len; i++)
	{
		LrgScriptBinding *s = g_ptr_array_index (arr, i);
		const gchar      *script = lrg_script_binding_get_script (s);
		g_autofree gchar *path = NULL;

		if (script == NULL || *script == '\0')
		{
			report (ctx, LRG_EDITOR_PROBLEM_ERROR, "script-no-path", node, NULL,
			        "Script #%u has no file", i);
			continue;
		}
		if (lrg_script_binding_get_language (s) == LRG_SCRIPT_LANGUAGE_NONE)
			report (ctx, LRG_EDITOR_PROBLEM_WARNING, "script-no-language", node, NULL,
			        "Script \"%s\" has no language", script);
		path = resolve_ref (ctx, script);
		if (path != NULL && !g_file_test (path, G_FILE_TEST_EXISTS))
			report (ctx, LRG_EDITOR_PROBLEM_ERROR, "missing-script", node, path,
			        "Script \"%s\" does not exist", script);
		if (!lrg_script_binding_get_enabled (s))
			report (ctx, LRG_EDITOR_PROBLEM_INFO, "script-disabled", node, NULL,
			        "Script \"%s\" is disabled", script);
	}

	return TRUE;
}

static gint
by_severity (gconstpointer a,
             gconstpointer b)
{
	const LrgEditorProblem *x = *(LrgEditorProblem * const *) a;
	const LrgEditorProblem *y = *(LrgEditorProblem * const *) b;

	return (gint) y->severity - (gint) x->severity;
}

GPtrArray *
lrg_editor_validate_level (LrgLevel    *level,
                           const gchar *base_dir,
                           LrgRegistry *registry)
{
	Ctx ctx;

	g_return_val_if_fail (LRG_IS_LEVEL (level), NULL);

	ctx.base_dir = base_dir;
	ctx.registry = registry;
	ctx.guids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	ctx.out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_editor_problem_free);

	lrg_node_foreach (lrg_level_get_root (level), visit, &ctx);
	g_hash_table_unref (ctx.guids);

	/* GLib's sort is a stable merge sort: tree order is kept within a
	 * severity. */
	g_ptr_array_sort (ctx.out, by_severity);
	return ctx.out;
}
