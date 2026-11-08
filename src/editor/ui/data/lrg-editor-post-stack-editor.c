/* lrg-editor-post-stack-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPostStackEditor: the `.rpost` data editor.
 *
 * The document (an LrgEditorDocument of format POST_STACK) is the source
 * of truth; every widget writes YAML paths through it, so undo, dirty
 * state, revert and external-change handling come from the data editor
 * base:
 *
 *   /name, /quality, /hdr                   stack settings
 *   /effects/<i>/type                        effect type name
 *   /effects/<i>/enabled, /effects/<i>/weight
 *   /effects/<i>/params/<param>              one effect property, written
 *                                            snake_case unless the file
 *                                            already spells it with dashes
 *
 * Content: an LrgSplitter with the effect list (rows rebuilt from the
 * document on every change) and an "Add effect" picker on the left, and
 * an LrgPropertyGrid over an LrgEditorDataSource on the right. The data
 * source's fields are YAML paths; they are rebuilt when the selection or
 * the selected effect's type changes, else only refreshed.
 *
 * Preview: one editor per app may drive the viewport. Its stack, parsed
 * from the document text after every change (unsaved edits included), is
 * handed to the viewport's LrgWorld3DRenderer with
 * lrg_world3d_renderer_set_post_stack(), which overrides the level's look
 * until the preview stops (tab closed, toggle off, another editor takes
 * over).
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL

#include "lrg-editor-post-stack-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../lrg-editor-environment-panel.h"
#include "../lrg-editor-viewport-panel.h"
#include "../lrg-editor-viewport-split.h"
#include "../lrg-editor-scene-renderer.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-scroll-box.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-checkbox.h"
#include "../../../ui/lrg-combo-box.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../postprocess/lrg-post-effect.h"
#include "../../../world3d/lrg-world3d-renderer.h"
#include <json-glib/json-glib.h>
#include <string.h>
#include <math.h>

#define PREVIEW_KEY  "lrg-post-stack-preview"
#define INSTALL_KEY  "lrg-post-stack-installed"

struct _LrgEditorPostStackEditor
{
	LrgEditorDataEditor  parent_instance;

	LrgWidget           *rows;         /* LrgVBox of effect rows */
	LrgComboBox         *add_combo;
	LrgPropertyGrid     *grid;
	LrgEditorDataSource *source;
	LrgWidget           *preview_chip;
	gchar               *grid_sig;     /* selection + type the fields were built for */
	gboolean             grid_stale;
	gint                 selected;
	gboolean             preview;
	gboolean             syncing;
	gchar              **type_names;   /* the add picker's items */
	gchar               *type_cache;   /* lrg_..._get_effect_type() result */
};

G_DEFINE_FINAL_TYPE (LrgEditorPostStackEditor, lrg_editor_post_stack_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void rebuild_rows (LrgEditorPostStackEditor *self);
static void rebuild_grid (LrgEditorPostStackEditor *self);
static void apply_preview (LrgEditorPostStackEditor *self);

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgEditorDocument *
doc_of (LrgEditorPostStackEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static LrgEditorApp *
app_of (LrgEditorPostStackEditor *self)
{
	return lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
}

static gchar *
effect_path (guint        index,
             const gchar *key)
{
	return (key != NULL) ? g_strdup_printf ("/effects/%u/%s", index, key) : g_strdup_printf ("/effects/%u", index);
}

/* "color-grade" -> "Color Grade" */
static gchar *
pretty_type (const gchar *type)
{
	gchar *s = g_strdup (type != NULL ? type : "?");
	gchar *p;
	gboolean start = TRUE;

	for (p = s; *p != '\0'; p++)
	{
		if (*p == '-' || *p == '_')
		{
			*p = ' ';
			start = TRUE;
		}
		else if (start)
		{
			*p = g_ascii_toupper (*p);
			start = FALSE;
		}
	}
	return s;
}

/* The pspecs of @type an .rpost may set: its own writable properties
 * below LrgPostEffect (the base's enabled/intensity/priority are the
 * stack's enabled/weight/order). */
static GPtrArray *
effect_params (GType type)
{
	GPtrArray    *out = g_ptr_array_new ();
	GObjectClass *klass;
	GParamSpec  **specs;
	guint         n = 0, i;

	if (type == G_TYPE_INVALID || !g_type_is_a (type, LRG_TYPE_POST_EFFECT))
		return out;
	klass = g_type_class_ref (type);
	specs = g_object_class_list_properties (klass, &n);
	for (i = 0; i < n; i++)
	{
		GParamSpec *p = specs[i];

		if (p->owner_type == LRG_TYPE_POST_EFFECT || !g_type_is_a (p->owner_type, LRG_TYPE_POST_EFFECT))
			continue;
		if (!(p->flags & G_PARAM_WRITABLE) || (p->flags & G_PARAM_CONSTRUCT_ONLY))
			continue;
		g_ptr_array_add (out, p);
	}
	g_free (specs);
	g_type_class_unref (klass);
	return out;
}

/* The document key of parameter @prop of effect @index: the spelling the
 * file already uses, else snake_case. */
static gchar *
param_key (LrgEditorDocument *doc,
           guint              index,
           const gchar       *prop)
{
	g_autofree gchar *snake = g_strdelimit (g_strdup (prop), "-", '_');
	g_autofree gchar *kebab = g_strdelimit (g_strdup (prop), "_", '-');
	g_autofree gchar *kp = g_strdup_printf ("/effects/%u/params/%s", index, kebab);

	if (lrg_editor_document_has_path (doc, kp))
		return g_steal_pointer (&kebab);
	return g_steal_pointer (&snake);
}

/* Writes @value at @path, creating missing parent mappings, one step. */
static gboolean
doc_write (LrgEditorDocument  *doc,
           const gchar        *path,
           const GValue       *value,
           const gchar        *label,
           GError            **error)
{
	g_auto(GStrv)     parts = lrg_editor_document_path_split (path, error);
	g_autofree gchar *parent = g_strdup ("");
	gboolean          ok = parts != NULL;
	guint             i;

	if (!ok)
		return FALSE;
	lrg_editor_document_begin_group (doc, label);
	for (i = 0; ok && parts[i] != NULL && parts[i + 1] != NULL; i++)
	{
		g_autofree gchar *next = lrg_editor_document_path_join (parent, parts[i]);

		if (!lrg_editor_document_has_path (doc, next))
			ok = lrg_editor_document_add_key (doc, parent, parts[i], "{}", -1, error);
		g_free (parent);
		parent = g_steal_pointer (&next);
	}
	if (ok)
		ok = lrg_editor_document_set_value (doc, path, value, 0, error);
	lrg_editor_document_end_group (doc);
	return ok;
}

static void
queue_viewports (LrgEditorApp *app)
{
	guint i;

	if (app == NULL || app->viewport_split == NULL)
		return;
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
	{
		LrgEditorViewportPanel *p = lrg_editor_viewport_split_get_panel (app->viewport_split, i);

		if (p != NULL)
			lrg_editor_viewport_panel_queue_render (p);
	}
}

/* ==========================================================================
 * Public API: reading
 * ========================================================================== */

guint
lrg_editor_post_stack_editor_get_n_effects (LrgEditorPostStackEditor *self)
{
	gint n;

	g_return_val_if_fail (LRG_IS_EDITOR_POST_STACK_EDITOR (self), 0);
	n = lrg_editor_document_get_length (doc_of (self), "/effects");
	return n > 0 ? (guint) n : 0;
}

const gchar *
lrg_editor_post_stack_editor_get_effect_type (LrgEditorPostStackEditor *self,
                                              guint                     index)
{
	g_autofree gchar *p = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_POST_STACK_EDITOR (self), NULL);
	p = effect_path (index, "type");
	g_free (self->type_cache);
	self->type_cache = g_strdup (lrg_editor_document_get_scalar (doc_of (self), p));
	return self->type_cache;
}

gint
lrg_editor_post_stack_editor_get_selected (LrgEditorPostStackEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_POST_STACK_EDITOR (self), -1);
	return self->selected;
}

gboolean
lrg_editor_post_stack_editor_select (LrgEditorPostStackEditor *self,
                                     gint                      index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_POST_STACK_EDITOR (self), FALSE);

	if (index >= (gint) lrg_editor_post_stack_editor_get_n_effects (self))
		return FALSE;
	self->selected = MAX (index, -1);
	rebuild_rows (self);
	rebuild_grid (self);
	return TRUE;
}

LrgPostStack *
lrg_editor_post_stack_editor_build_stack (LrgEditorPostStackEditor  *self,
                                          GError                   **error)
{
	LrgEditorDocument *doc;
	g_autofree gchar  *text = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_POST_STACK_EDITOR (self), NULL);

	doc = doc_of (self);
	text = lrg_editor_document_to_data (doc, NULL);
	if (text == NULL)
	{
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_PARSE, "The document is empty");
		return NULL;
	}
	return lrg_post_stack_new_from_data (text, -1, lrg_editor_document_get_base_dir (doc), error);
}

/* ==========================================================================
 * Public API: editing
 * ========================================================================== */

gint
lrg_editor_post_stack_editor_add_effect (LrgEditorPostStackEditor  *self,
                                         const gchar               *type_name,
                                         gint                       index,
                                         GError                   **error)
{
	LrgEditorDocument *doc;
	g_autofree gchar  *yaml = NULL;
	g_autofree gchar  *label = NULL;
	gint               n;

	g_return_val_if_fail (LRG_IS_EDITOR_POST_STACK_EDITOR (self), -1);
	g_return_val_if_fail (type_name != NULL, -1);

	if (lrg_post_stack_lookup_effect_type (type_name) == G_TYPE_INVALID)
	{
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_UNKNOWN_EFFECT,
		             "Unknown post effect type '%s'", type_name);
		return -1;
	}
	doc = doc_of (self);
	n = (gint) lrg_editor_post_stack_editor_get_n_effects (self);
	if (index < 0 || index > n)
		index = n;

	label = g_strdup_printf ("Add %s", type_name);
	lrg_editor_document_begin_group (doc, label);
	if (!lrg_editor_document_has_path (doc, "/effects") &&
	    !lrg_editor_document_add_key (doc, "", "effects", "[]", -1, error))
	{
		lrg_editor_document_end_group (doc);
		return -1;
	}
	yaml = g_strdup_printf ("type: %s\nenabled: true\n", type_name);
	if (!lrg_editor_document_insert_item (doc, "/effects", index, yaml, error))
	{
		lrg_editor_document_end_group (doc);
		return -1;
	}
	lrg_editor_document_end_group (doc);
	lrg_editor_post_stack_editor_select (self, index);
	return index;
}

gboolean
lrg_editor_post_stack_editor_remove_effect (LrgEditorPostStackEditor  *self,
                                            guint                      index,
                                            GError                   **error)
{
	g_autofree gchar *p = NULL;
	guint             n;

	g_return_val_if_fail (LRG_IS_EDITOR_POST_STACK_EDITOR (self), FALSE);

	n = lrg_editor_post_stack_editor_get_n_effects (self);
	if (index >= n)
	{
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_INVALID_VALUE, "No effect %u", index);
		return FALSE;
	}
	p = effect_path (index, NULL);
	if (!lrg_editor_document_remove (doc_of (self), p, error))
		return FALSE;
	if (self->selected >= (gint) n - 1)
		self->selected = (gint) n - 2;
	rebuild_rows (self);
	rebuild_grid (self);
	return TRUE;
}

gboolean
lrg_editor_post_stack_editor_move_effect (LrgEditorPostStackEditor  *self,
                                          guint                      from,
                                          guint                      to,
                                          GError                   **error)
{
	guint n;

	g_return_val_if_fail (LRG_IS_EDITOR_POST_STACK_EDITOR (self), FALSE);

	n = lrg_editor_post_stack_editor_get_n_effects (self);
	if (from >= n || to >= n)
	{
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_INVALID_VALUE,
		             "Effect index out of range (%u effects)", n);
		return FALSE;
	}
	if (from == to)
		return TRUE;
	if (!lrg_editor_document_move_item (doc_of (self), "/effects", from, to, error))
		return FALSE;
	if (self->selected == (gint) from)
		self->selected = (gint) to;
	rebuild_rows (self);
	rebuild_grid (self);
	return TRUE;
}

gboolean
lrg_editor_post_stack_editor_set_value (LrgEditorPostStackEditor  *self,
                                        guint                      index,
                                        const gchar               *key,
                                        const GValue              *value,
                                        GError                   **error)
{
	LrgEditorDocument *doc;
	g_autofree gchar  *path = NULL;
	g_autofree gchar  *label = NULL;
	g_auto(GValue)     v = G_VALUE_INIT;

	g_return_val_if_fail (LRG_IS_EDITOR_POST_STACK_EDITOR (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (G_IS_VALUE (value), FALSE);

	doc = doc_of (self);
	if (index >= lrg_editor_post_stack_editor_get_n_effects (self))
	{
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_INVALID_VALUE, "No effect %u", index);
		return FALSE;
	}

	if (g_strcmp0 (key, "enabled") == 0 || g_strcmp0 (key, "weight") == 0)
		path = effect_path (index, key);
	else
	{
		/* a parameter: it must be a property of the effect's type */
		GType             type = lrg_post_stack_lookup_effect_type (
			lrg_editor_post_stack_editor_get_effect_type (self, index));
		g_autofree gchar *prop = g_strdelimit (g_strdup (key), "_", '-');
		g_autoptr(GPtrArray) params = effect_params (type);
		GParamSpec       *pspec = NULL;
		g_autofree gchar *k = NULL;
		guint             i;

		for (i = 0; i < params->len; i++)
			if (g_strcmp0 (((GParamSpec *) g_ptr_array_index (params, i))->name, prop) == 0)
				pspec = g_ptr_array_index (params, i);
		if (pspec == NULL)
		{
			g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_UNKNOWN_PARAM,
			             "'%s' is not a parameter of %s", key, g_type_name (type));
			return FALSE;
		}
		/* enums go in as nicks */
		if (G_IS_PARAM_SPEC_ENUM (pspec) && !G_VALUE_HOLDS_STRING (value))
		{
			g_autoptr(GEnumClass) klass = g_type_class_ref (pspec->value_type);
			GEnumValue           *ev = NULL;

			if (G_VALUE_HOLDS_ENUM (value))
				ev = g_enum_get_value (klass, g_value_get_enum (value));
			else if (G_VALUE_HOLDS_INT (value))
				ev = g_enum_get_value (klass, g_value_get_int (value));
			if (ev == NULL)
			{
				g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_INVALID_VALUE,
				             "Bad value for %s", key);
				return FALSE;
			}
			g_value_init (&v, G_TYPE_STRING);
			g_value_set_string (&v, ev->value_nick);
			value = &v;
		}
		k = param_key (doc, index, pspec->name);
		path = g_strdup_printf ("/effects/%u/params/%s", index, k);
	}
	label = g_strdup_printf ("Set %s", key);
	return doc_write (doc, path, value, label, error);
}

/* ==========================================================================
 * Preview
 * ========================================================================== */

static LrgWorld3DRenderer *
runtime_renderer (LrgEditorApp *app)
{
	if (app == NULL || app->renderer == NULL)
		return NULL;
	return lrg_editor_scene_renderer_get_runtime_renderer (app->renderer);
}

static void
apply_preview (LrgEditorPostStackEditor *self)
{
	LrgEditorApp             *app = app_of (self);
	LrgWorld3DRenderer       *rt = runtime_renderer (app);
	g_autoptr(LrgPostStack)   stack = NULL;

	if (!self->preview || rt == NULL)
		return;
	stack = lrg_editor_post_stack_editor_build_stack (self, NULL);
	if (stack != NULL)
		lrg_world3d_renderer_set_post_stack (rt, stack);
	queue_viewports (app);
}

static void
stop_preview (LrgEditorPostStackEditor *self)
{
	LrgEditorApp       *app = app_of (self);
	LrgWorld3DRenderer *rt = runtime_renderer (app);

	if (app != NULL && g_object_get_data (G_OBJECT (app), PREVIEW_KEY) == self)
	{
		g_object_set_data (G_OBJECT (app), PREVIEW_KEY, NULL);
		if (rt != NULL)
			lrg_world3d_renderer_set_post_stack (rt, NULL);
		queue_viewports (app);
	}
}

void
lrg_editor_post_stack_editor_set_preview (LrgEditorPostStackEditor *self,
                                          gboolean                  preview)
{
	LrgEditorApp *app;

	g_return_if_fail (LRG_IS_EDITOR_POST_STACK_EDITOR (self));

	app = app_of (self);
	if (!preview)
	{
		stop_preview (self);
		self->preview = FALSE;
	}
	else
	{
		LrgEditorPostStackEditor *other = (app != NULL) ? g_object_get_data (G_OBJECT (app), PREVIEW_KEY) : NULL;

		if (other != NULL && other != self)
		{
			other->preview = FALSE;
			if (other->preview_chip != NULL)
				editor_chip_set_checked (other->preview_chip, FALSE);
		}
		self->preview = TRUE;
		if (app != NULL)
		{
			LrgEditorViewOverlays ov = lrg_editor_view_state_get_overlays (app->view, -1);

			g_object_set_data (G_OBJECT (app), PREVIEW_KEY, self);
			/* the look only shows with the post overlay on */
			if (!(ov & LRG_EDITOR_VIEW_OVERLAY_POST))
				lrg_editor_view_state_set_overlays (app->view, -1, ov | LRG_EDITOR_VIEW_OVERLAY_POST);
		}
		apply_preview (self);
	}
	if (self->preview_chip != NULL)
		editor_chip_set_checked (self->preview_chip, self->preview);
}

gboolean
lrg_editor_post_stack_editor_get_preview (LrgEditorPostStackEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_POST_STACK_EDITOR (self), FALSE);
	return self->preview;
}

/* ==========================================================================
 * Save current look
 * ========================================================================== */

LrgPostStack *
lrg_editor_post_stack_capture_look (LrgEditorApp *app,
                                    gint          viewport)
{
	LrgWorld3DRenderer *rt;
	LrgPostProcessor   *proc;
	guint               slot;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	rt = runtime_renderer (app);
	if (rt != NULL && lrg_world3d_renderer_get_post_stack (rt) != NULL)
		return lrg_post_stack_copy (lrg_world3d_renderer_get_post_stack (rt));
	if (app->renderer == NULL)
		return NULL;
	slot = (viewport < 0) ? (guint) lrg_editor_view_state_get_active_viewport (app->view) : (guint) viewport;
	proc = lrg_editor_scene_renderer_get_post_processor (app->renderer, slot);
	return (proc != NULL) ? lrg_post_stack_new_from_processor (proc) : NULL;
}

gboolean
lrg_editor_post_stack_editor_save_current_look (LrgEditorPostStackEditor  *self,
                                                GError                   **error)
{
	LrgEditorApp            *app;
	g_autoptr(LrgPostStack)  look = NULL;
	g_autofree gchar        *text = NULL;
	g_autofree gchar        *name = NULL;
	LrgEditorDocument       *doc;

	g_return_val_if_fail (LRG_IS_EDITOR_POST_STACK_EDITOR (self), FALSE);

	app = app_of (self);
	look = (app != NULL) ? lrg_editor_post_stack_capture_look (app, -1) : NULL;
	if (look == NULL)
	{
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_IO, "No viewport look to capture");
		return FALSE;
	}
	/* keep the document's own name */
	doc = doc_of (self);
	name = g_strdup (lrg_editor_document_get_scalar (doc, "/name"));
	if (name != NULL)
		lrg_post_stack_set_name (look, name);
	text = lrg_post_stack_to_data (look, error);
	if (text == NULL)
		return FALSE;
	if (!lrg_editor_document_replace (doc, "", text, error))
		return FALSE;
	self->selected = (lrg_post_stack_get_effect_count (look) > 0) ? 0 : -1;
	rebuild_rows (self);
	rebuild_grid (self);
	return TRUE;
}

/* ==========================================================================
 * Effect rows
 * ========================================================================== */

static gint
row_index (LrgWidget *w)
{
	return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (w), "lrg-post-index")) - 1;
}

static void
set_row_index (LrgWidget *w,
               guint      index)
{
	g_object_set_data (G_OBJECT (w), "lrg-post-index", GINT_TO_POINTER ((gint) index + 1));
}

static void
on_row_select (LrgWidget *chip,
               gpointer   user_data)
{
	lrg_editor_post_stack_editor_select (LRG_EDITOR_POST_STACK_EDITOR (user_data), row_index (chip));
}

static void
on_row_up (LrgWidget *chip,
           gpointer   user_data)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (user_data);
	g_autoptr(GError)         error = NULL;
	gint                      i = row_index (chip);

	if (i > 0 && !lrg_editor_post_stack_editor_move_effect (self, (guint) i, (guint) i - 1, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_row_down (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (user_data);
	g_autoptr(GError)         error = NULL;
	gint                      i = row_index (chip);

	if (i >= 0 && (guint) i + 1 < lrg_editor_post_stack_editor_get_n_effects (self) &&
	    !lrg_editor_post_stack_editor_move_effect (self, (guint) i, (guint) i + 1, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_row_remove (LrgWidget *chip,
               gpointer   user_data)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (user_data);
	g_autoptr(GError)         error = NULL;
	gint                      i = row_index (chip);

	if (i >= 0 && !lrg_editor_post_stack_editor_remove_effect (self, (guint) i, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_row_toggled (LrgCheckbox *box,
                gpointer     user_data)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (user_data);
	g_autoptr(GError)         error = NULL;
	g_auto(GValue)            v = G_VALUE_INIT;
	gint                      i = row_index (LRG_WIDGET (box));

	if (self->syncing || i < 0)
		return;
	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, lrg_checkbox_get_checked (box));
	if (!lrg_editor_post_stack_editor_set_value (self, (guint) i, "enabled", &v, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

static gboolean
effect_enabled (LrgEditorDocument *doc,
                guint              index)
{
	g_autofree gchar *p = effect_path (index, "enabled");
	const gchar      *s = lrg_editor_document_get_scalar (doc, p);

	return s == NULL || g_ascii_strcasecmp (s, "false") != 0;
}

static gdouble
effect_weight (LrgEditorDocument *doc,
               guint              index)
{
	g_autofree gchar *p = effect_path (index, "weight");
	const gchar      *s = lrg_editor_document_get_scalar (doc, p);

	return (s != NULL) ? g_ascii_strtod (s, NULL) : 1.0;
}

static void
rebuild_rows (LrgEditorPostStackEditor *self)
{
	LrgEditorDocument *doc = doc_of (self);
	guint              n = lrg_editor_post_stack_editor_get_n_effects (self);
	GList             *children;
	GList             *l;
	guint              i;

	if (self->rows == NULL)
		return;
	if (self->selected >= (gint) n)
		self->selected = (gint) n - 1;

	children = g_list_copy (lrg_container_get_children (LRG_CONTAINER (self->rows)));
	for (l = children; l != NULL; l = l->next)
		lrg_container_remove_child (LRG_CONTAINER (self->rows), l->data);
	g_list_free (children);

	self->syncing = TRUE;
	for (i = 0; i < n; i++)
	{
		LrgWidget        *row = LRG_WIDGET (lrg_hbox_new ());
		LrgCheckbox      *box = lrg_checkbox_new (NULL);
		LrgWidget        *chip;
		g_autofree gchar *type_path = effect_path (i, "type");
		g_autofree gchar *label = pretty_type (lrg_editor_document_get_scalar (doc, type_path));
		g_autofree gchar *rname = g_strdup_printf ("post-effect-%u", i);
		g_autofree gchar *bname = g_strdup_printf ("post-effect-%u-enabled", i);
		g_autofree gchar *uname = g_strdup_printf ("post-effect-%u-up", i);
		g_autofree gchar *dname = g_strdup_printf ("post-effect-%u-down", i);
		g_autofree gchar *xname = g_strdup_printf ("post-effect-%u-remove", i);
		g_autofree gchar *badge = g_strdup_printf ("%.2f", effect_weight (doc, i));
		GType             type = lrg_post_stack_lookup_effect_type (lrg_editor_document_get_scalar (doc, type_path));

		lrg_container_set_spacing (LRG_CONTAINER (row), 4.0f);
		lrg_widget_set_name (LRG_WIDGET (box), bname);
		lrg_checkbox_set_checked (box, effect_enabled (doc, i));
		lrg_widget_set_valign (LRG_WIDGET (box), LRG_ALIGN_CENTER);
		set_row_index (LRG_WIDGET (box), i);
		g_signal_connect (box, "toggled", G_CALLBACK (on_row_toggled), self);
		add_to (row, LRG_WIDGET (box));

		chip = editor_chip_new (rname, type != G_TYPE_INVALID ? LRG_ICON_STAR : LRG_ICON_WARNING, label,
		                        on_row_select, self);
		set_row_index (chip, i);
		editor_chip_set_badge (chip, badge);
		editor_chip_set_checked (chip, (gint) i == self->selected);
		if (type == G_TYPE_INVALID)
			editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
		else if (!effect_enabled (doc, i))
			editor_chip_set_tint (chip, LRG_THEME_COLOR_TEXT_MUTED);
		lrg_widget_set_fill_weight (chip, 1.0f);
		add_to (row, chip);

		chip = editor_chip_new (uname, LRG_ICON_CHEVRON_UP, NULL, on_row_up, self);
		set_row_index (chip, i);
		editor_chip_set_compact (chip, TRUE);
		lrg_widget_set_enabled (chip, i > 0);
		add_to (row, chip);
		chip = editor_chip_new (dname, LRG_ICON_CHEVRON_DOWN, NULL, on_row_down, self);
		set_row_index (chip, i);
		editor_chip_set_compact (chip, TRUE);
		lrg_widget_set_enabled (chip, i + 1 < n);
		add_to (row, chip);
		chip = editor_chip_new (xname, LRG_ICON_TRASH, NULL, on_row_remove, self);
		set_row_index (chip, i);
		editor_chip_set_compact (chip, TRUE);
		editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
		add_to (row, chip);

		add_to (self->rows, row);
	}
	if (n == 0)
	{
		LrgWidget *hint = editor_panel_hint_new ("post-empty", "No effects yet. Add bloom, tonemap, vignette...");

		add_to (self->rows, hint);
	}
	self->syncing = FALSE;
}

static void
on_add_changed (LrgComboBox *combo,
                gpointer     user_data)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (user_data);
	g_autoptr(GError)         error = NULL;
	gint                      i = lrg_combo_box_get_selected (combo);

	if (self->syncing || i <= 0 || self->type_names == NULL || (guint) (i - 1) >= g_strv_length (self->type_names))
		return;
	/* item 0 is the "Add effect" placeholder */
	if (lrg_editor_post_stack_editor_add_effect (self, self->type_names[i - 1],
	                                             self->selected >= 0 ? self->selected + 1 : -1, &error) < 0)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	self->syncing = TRUE;
	lrg_combo_box_set_selected (combo, 0);
	self->syncing = FALSE;
}

/* ==========================================================================
 * The grid
 * ========================================================================== */

static gchar *
grid_signature (LrgEditorPostStackEditor *self)
{
	const gchar *type = (self->selected >= 0) ? lrg_editor_post_stack_editor_get_effect_type (self,
	                                                                                           (guint) self->selected)
	                                          : NULL;

	return g_strdup_printf ("%d:%s", self->selected, type != NULL ? type : "");
}

static void
add_param_field (LrgEditorPostStackEditor *self,
                 guint                     index,
                 GParamSpec               *p,
                 const gchar              *group)
{
	LrgEditorDocument *doc = doc_of (self);
	g_autofree gchar  *key = param_key (doc, index, p->name);
	g_autofree gchar  *path = g_strdup_printf ("/effects/%u/params/%s", index, key);
	g_autofree gchar  *field = g_strconcat ("param-", p->name, NULL);
	g_autofree gchar  *hint = NULL;
	g_autofree gchar  *def = NULL;
	GType              type = G_TYPE_INVALID;
	const gchar       *nick = g_param_spec_get_nick (p);

	if (G_IS_PARAM_SPEC_FLOAT (p))
	{
		GParamSpecFloat *f = G_PARAM_SPEC_FLOAT (p);
		gdouble          step = (f->maximum - f->minimum > 50.0f) ? 0.1 : 0.01;

		type = G_TYPE_DOUBLE;
		hint = g_strdup_printf ("range:%g,%g,%g", (gdouble) f->minimum, (gdouble) MIN (f->maximum, 100000.0f), step);
		def = g_strdup_printf ("%g", (gdouble) f->default_value);
	}
	else if (G_IS_PARAM_SPEC_DOUBLE (p))
	{
		GParamSpecDouble *d = G_PARAM_SPEC_DOUBLE (p);

		type = G_TYPE_DOUBLE;
		hint = g_strdup_printf ("range:%g,%g,%g", d->minimum, MIN (d->maximum, 100000.0),
		                        (d->maximum - d->minimum > 50.0) ? 0.1 : 0.01);
		def = g_strdup_printf ("%g", d->default_value);
	}
	else if (G_IS_PARAM_SPEC_INT (p) || G_IS_PARAM_SPEC_UINT (p))
	{
		gint64 lo, hi, dv;

		if (G_IS_PARAM_SPEC_INT (p))
		{
			lo = G_PARAM_SPEC_INT (p)->minimum;
			hi = G_PARAM_SPEC_INT (p)->maximum;
			dv = G_PARAM_SPEC_INT (p)->default_value;
		}
		else
		{
			lo = G_PARAM_SPEC_UINT (p)->minimum;
			hi = G_PARAM_SPEC_UINT (p)->maximum;
			dv = G_PARAM_SPEC_UINT (p)->default_value;
		}
		type = G_TYPE_INT64;
		hint = g_strdup_printf ("range:%" G_GINT64_FORMAT ",%" G_GINT64_FORMAT ",1", lo, MIN (hi, (gint64) 100000));
		def = g_strdup_printf ("%" G_GINT64_FORMAT, dv);
	}
	else if (G_IS_PARAM_SPEC_BOOLEAN (p))
	{
		type = G_TYPE_BOOLEAN;
		def = g_strdup (G_PARAM_SPEC_BOOLEAN (p)->default_value ? "true" : "false");
	}
	else if (G_IS_PARAM_SPEC_ENUM (p))
	{
		GEnumClass *klass = G_PARAM_SPEC_ENUM (p)->enum_class;
		GString    *s = g_string_new ("enum-strings:");
		GEnumValue *dv = g_enum_get_value (klass, G_PARAM_SPEC_ENUM (p)->default_value);
		guint       i;

		for (i = 0; i < klass->n_values; i++)
			g_string_append_printf (s, "%s%s", i > 0 ? "," : "", klass->values[i].value_nick);
		type = G_TYPE_STRING;
		hint = g_string_free (s, FALSE);
		def = g_strdup (dv != NULL ? dv->value_nick : "");
	}
	else if (G_IS_PARAM_SPEC_STRING (p))
	{
		type = G_TYPE_STRING;
		hint = g_strdup (g_str_has_suffix (p->name, "lut") || g_str_has_suffix (p->name, "path") ? "asset" : NULL);
		def = g_strdup (G_PARAM_SPEC_STRING (p)->default_value);
	}
	if (type == G_TYPE_INVALID)
		return;
	lrg_editor_data_source_add_field (self->source, field, path, type, nick != NULL ? nick : p->name, group, hint,
	                                  def);
}

static void
rebuild_grid (LrgEditorPostStackEditor *self)
{
	if (self->source == NULL)
		return;
	lrg_editor_data_source_clear (self->source);
	lrg_editor_data_source_add_field (self->source, "stack-name", "/name", G_TYPE_STRING, "Name", "Stack", NULL,
	                                  NULL);
	lrg_editor_data_source_add_field (self->source, "stack-quality", "/quality", G_TYPE_STRING, "Quality", "Stack",
	                                  "enum-strings:low,medium,high,ultra", "high");
	lrg_editor_data_source_add_field (self->source, "stack-hdr", "/hdr", G_TYPE_BOOLEAN, "HDR targets", "Stack",
	                                  NULL, "true");
	if (self->selected >= 0)
	{
		guint             i = (guint) self->selected;
		const gchar      *type_name = lrg_editor_post_stack_editor_get_effect_type (self, i);
		g_autofree gchar *group = pretty_type (type_name);
		g_autofree gchar *pe = effect_path (i, "enabled");
		g_autofree gchar *pw = effect_path (i, "weight");
		g_autoptr(GPtrArray) params = effect_params (lrg_post_stack_lookup_effect_type (type_name));
		guint             k;

		lrg_editor_data_source_add_field (self->source, "effect-enabled", pe, G_TYPE_BOOLEAN, "Enabled", group, NULL,
		                                  "true");
		lrg_editor_data_source_add_field (self->source, "effect-weight", pw, G_TYPE_DOUBLE, "Weight", group,
		                                  "range:0,1,0.01", "1");
		for (k = 0; k < params->len; k++)
			add_param_field (self, i, g_ptr_array_index (params, k), group);
	}
	g_free (self->grid_sig);
	self->grid_sig = grid_signature (self);
	self->grid_stale = FALSE;
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_property_grid_rebuild (self->grid);
}

/* ==========================================================================
 * Data editor vfuncs
 * ========================================================================== */

static void
post_stack_refresh (LrgEditorDataEditor *editor,
                    const gchar         *path)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (editor);
	g_autofree gchar         *sig = NULL;

	rebuild_rows (self);
	sig = grid_signature (self);
	if (g_strcmp0 (sig, self->grid_sig) != 0)
		self->grid_stale = TRUE;
	else
	{
		lrg_editor_data_source_document_changed (self->source, path);
		lrg_property_grid_refresh (self->grid);
	}
	apply_preview (self);
}

static void
post_stack_reveal (LrgEditorDataEditor *editor,
                   const gchar         *path,
                   guint                line)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (editor);
	g_auto(GStrv)             parts = lrg_editor_document_path_split (path, NULL);

	if (parts != NULL && g_strcmp0 (parts[0], "effects") == 0 && parts[1] != NULL)
		lrg_editor_post_stack_editor_select (self, (gint) g_ascii_strtoll (parts[1], NULL, 10));
}

/* Unknown effect types and bad parameters, from the engine loader. */
static void
post_stack_validate (LrgEditorDataEditor *editor,
                     GPtrArray           *issues)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (editor);
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgPostStack)   stack = lrg_editor_post_stack_editor_build_stack (self, &error);
	guint                     n = lrg_editor_post_stack_editor_get_n_effects (self);
	guint                     i;

	for (i = 0; i < n; i++)
	{
		const gchar *type = lrg_editor_post_stack_editor_get_effect_type (self, i);

		if (type == NULL || lrg_post_stack_lookup_effect_type (type) == G_TYPE_INVALID)
		{
			g_autofree gchar *p = effect_path (i, "type");
			g_autofree gchar *msg = g_strdup_printf ("Unknown effect type '%s'", type != NULL ? type : "");

			g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, p, 0, 0, msg));
		}
	}
	if (stack == NULL && error != NULL && error->code != LRG_POST_STACK_ERROR_UNKNOWN_EFFECT)
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, "", 0, 0, error->message));
}

static gchar *
post_stack_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (editor);

	return g_strdup_printf ("effects=%u selected=%d preview=%s", lrg_editor_post_stack_editor_get_n_effects (self),
	                        self->selected, self->preview ? "on" : "off");
}

/* ==========================================================================
 * Toolbar
 * ========================================================================== */

static void
on_preview_chip (LrgWidget *chip,
                 gpointer   user_data)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (user_data);

	lrg_editor_post_stack_editor_set_preview (self, !self->preview);
}

static void
on_look_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (user_data);
	g_autoptr(GError)         error = NULL;

	if (!lrg_editor_post_stack_editor_save_current_look (self, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* "Use for Level": the level environment's post_stack = this file */
static void
on_assign_chip (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorPostStackEditor  *self = LRG_EDITOR_POST_STACK_EDITOR (user_data);
	LrgEditorApp              *app = app_of (self);
	LrgEditorEnvironmentPanel *env = (app != NULL) ? lrg_editor_environment_panel_find (app) : NULL;
	g_autofree gchar          *rel = NULL;
	g_auto(GValue)             v = G_VALUE_INIT;

	if (env == NULL || lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)) == NULL)
		return;
	rel = editor_app_relative_path (app, lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, rel);
	if (lrg_editor_environment_panel_set_field (env, "post-stack", &v))
		editor_app_set_status (app, "The level now uses %s", rel);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static gboolean
lrg_editor_post_stack_editor_step (LrgWidget *widget,
                                   gfloat     delta_time)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (widget);
	gboolean                  busy = LRG_WIDGET_CLASS (lrg_editor_post_stack_editor_parent_class)->step (widget,
	                                                                                                     delta_time);

	if (self->grid_stale)
		rebuild_grid (self);
	return busy;
}

static void
lrg_editor_post_stack_editor_constructed (GObject *object)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (object);
	LrgEditorDataEditor      *base = LRG_EDITOR_DATA_EDITOR (object);
	LrgSplitter              *split;
	LrgWidget                *left;
	LrgWidget                *scroll;
	LrgWidget                *heading;
	LrgEditorApp             *app;
	guint                     i;

	G_OBJECT_CLASS (lrg_editor_post_stack_editor_parent_class)->constructed (object);

	self->selected = (lrg_editor_post_stack_editor_get_n_effects (self) > 0) ? 0 : -1;

	/* Left: the chain */
	left = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (left, "post-chain");
	lrg_container_set_spacing (LRG_CONTAINER (left), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (left), 8.0f);
	heading = LRG_WIDGET (lrg_label_new ("Effects (in chain order)"));
	lrg_widget_add_class (heading, "dim-label");
	add_to (left, heading);
	self->rows = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->rows, "post-effects");
	lrg_container_set_spacing (LRG_CONTAINER (self->rows), 3.0f);
	scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (scroll), self->rows);
	g_object_unref (self->rows);
	lrg_widget_set_fill_weight (scroll, 1.0f);
	add_to (left, scroll);
	self->add_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->add_combo), "post-add");
	lrg_combo_box_append (self->add_combo, "+ Add effect...");
	self->type_names = lrg_post_stack_list_effect_types ();
	for (i = 0; self->type_names != NULL && self->type_names[i] != NULL; i++)
	{
		g_autofree gchar *label = pretty_type (self->type_names[i]);

		lrg_combo_box_append (self->add_combo, label);
	}
	lrg_combo_box_set_selected (self->add_combo, 0);
	g_signal_connect (self->add_combo, "changed", G_CALLBACK (on_add_changed), self);
	add_to (left, LRG_WIDGET (self->add_combo));

	/* Right: settings and the selected effect */
	self->source = lrg_editor_data_source_new (base);
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "post-grid");
	lrg_property_grid_set_label_width (self->grid, 110.0f);
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_editor_data_source_bind_grid (self->source, self->grid);
	scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (scroll, "post-grid-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (scroll), LRG_WIDGET (self->grid));
	g_object_unref (self->grid);

	split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_splitter_set_start_child (split, left);
	g_object_unref (left);
	lrg_splitter_set_end_child (split, scroll);
	g_object_unref (scroll);
	lrg_splitter_set_default_fraction (split, 0.42f);
	lrg_splitter_set_fraction (split, 0.42f, FALSE);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (split));

	/* Toolbar tools */
	self->preview_chip = editor_chip_new ("post-preview", LRG_ICON_EYE, "Preview", on_preview_chip, self);
	editor_chip_set_tint (self->preview_chip, LRG_THEME_COLOR_ACCENT);
	lrg_editor_data_editor_add_tool (base, self->preview_chip);
	lrg_editor_data_editor_add_tool (base, editor_chip_new ("post-look", LRG_ICON_DOWNLOAD, "Look", on_look_chip,
	                                                        self));
	lrg_editor_data_editor_add_tool (base, editor_chip_new ("post-assign", LRG_ICON_HOME, "Use for Level",
	                                                        on_assign_chip, self));

	rebuild_rows (self);
	rebuild_grid (self);

	/* Live preview by default when there is a viewport to show it */
	app = app_of (self);
	if (app != NULL)
	{
		lrg_editor_post_stack_editor_install (app);
		lrg_editor_post_stack_editor_set_preview (self, TRUE);
	}
}

static void
lrg_editor_post_stack_editor_dispose (GObject *object)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (object);

	stop_preview (self);
	self->preview_chip = NULL;
	self->rows = NULL;
	g_clear_object (&self->source);
	G_OBJECT_CLASS (lrg_editor_post_stack_editor_parent_class)->dispose (object);
}

static void
lrg_editor_post_stack_editor_finalize (GObject *object)
{
	LrgEditorPostStackEditor *self = LRG_EDITOR_POST_STACK_EDITOR (object);

	g_free (self->grid_sig);
	g_free (self->type_cache);
	g_strfreev (self->type_names);
	G_OBJECT_CLASS (lrg_editor_post_stack_editor_parent_class)->finalize (object);
}

static void
lrg_editor_post_stack_editor_class_init (LrgEditorPostStackEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_post_stack_editor_constructed;
	object_class->dispose = lrg_editor_post_stack_editor_dispose;
	object_class->finalize = lrg_editor_post_stack_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_post_stack_editor_step;
	data_class->refresh = post_stack_refresh;
	data_class->reveal = post_stack_reveal;
	data_class->validate = post_stack_validate;
	data_class->get_state = post_stack_get_state;
}

static void
lrg_editor_post_stack_editor_init (LrgEditorPostStackEditor *self)
{
	self->selected = -1;
}

LrgEditorPostStackEditor *
lrg_editor_post_stack_editor_new (LrgEditorApp      *app,
                                  LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_POST_STACK_EDITOR, "app", app, "document", document, NULL);
}

/* ==========================================================================
 * IPC: post.*
 * ========================================================================== */

static JsonNode *
obj_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static const gchar *
p_string (JsonObject  *params,
          const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	return (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING) ? json_node_get_string (n)
	                                                                                    : NULL;
}

static gboolean
p_int (JsonObject  *params,
       const gchar *name,
       gint64      *out)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	if (json_node_get_value_type (n) == G_TYPE_INT64)
		*out = json_node_get_int (n);
	else if (json_node_get_value_type (n) == G_TYPE_DOUBLE)
		*out = (gint64) json_node_get_double (n);
	else
		return FALSE;
	return TRUE;
}

static gboolean
bad_params (GError      **error,
            const gchar  *message)
{
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, message);
	return FALSE;
}

static gboolean
failed (GError      **error,
        GError       *local)
{
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
	                     local != NULL ? local->message : "failed");
	return FALSE;
}

/* The editor of params.path, opened when needed. */
static LrgEditorPostStackEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	const gchar         *path = p_string (params, "path");
	LrgEditorDataEditor *ed;
	g_autoptr(GError)    local = NULL;

	if (path == NULL)
	{
		bad_params (error, "Missing \"path\"");
		return NULL;
	}
	ed = lrg_editor_data_editor_open (app, path, &local);
	if (ed == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "%s", local->message);
		return NULL;
	}
	if (!LRG_IS_EDITOR_POST_STACK_EDITOR (ed))
	{
		bad_params (error, "Not a .rpost post stack");
		return NULL;
	}
	return LRG_EDITOR_POST_STACK_EDITOR (ed);
}

/* A JSON value as a GValue for set_value (bool, number, string). */
static gboolean
json_to_value (JsonNode *n,
               GValue   *v)
{
	if (n == NULL || !JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	switch (json_node_get_value_type (n))
	{
	case G_TYPE_BOOLEAN:
		g_value_init (v, G_TYPE_BOOLEAN);
		g_value_set_boolean (v, json_node_get_boolean (n));
		return TRUE;
	case G_TYPE_INT64:
		g_value_init (v, G_TYPE_INT64);
		g_value_set_int64 (v, json_node_get_int (n));
		return TRUE;
	case G_TYPE_DOUBLE:
		g_value_init (v, G_TYPE_DOUBLE);
		g_value_set_double (v, json_node_get_double (n));
		return TRUE;
	case G_TYPE_STRING:
		g_value_init (v, G_TYPE_STRING);
		g_value_set_string (v, json_node_get_string (n));
		return TRUE;
	default:
		return FALSE;
	}
}

static JsonNode *
gvalue_json (const GValue *v)
{
	JsonNode *n = json_node_new (JSON_NODE_VALUE);

	if (G_VALUE_HOLDS_BOOLEAN (v))
		json_node_set_boolean (n, g_value_get_boolean (v));
	else if (G_VALUE_HOLDS_ENUM (v))
	{
		g_autoptr(GEnumClass) klass = g_type_class_ref (G_VALUE_TYPE (v));
		GEnumValue           *ev = g_enum_get_value (klass, g_value_get_enum (v));

		json_node_set_string (n, ev != NULL ? ev->value_nick : "");
	}
	else if (G_VALUE_HOLDS_STRING (v))
		json_node_set_string (n, g_value_get_string (v) != NULL ? g_value_get_string (v) : "");
	else
	{
		g_auto(GValue) d = G_VALUE_INIT;

		g_value_init (&d, G_TYPE_DOUBLE);
		if (g_value_transform (v, &d))
			json_node_set_double (n, g_value_get_double (&d));
		else
			json_node_init_null (n);
	}
	return n;
}

static JsonNode *
stack_json (LrgPostStack *stack,
            const gchar  *path,
            gboolean      dirty)
{
	JsonObject *o = json_object_new ();
	JsonArray  *effects = json_array_new ();
	g_autoptr(GEnumClass) qk = g_type_class_ref (LRG_TYPE_POST_QUALITY);
	GEnumValue *q = g_enum_get_value (qk, (gint) lrg_post_stack_get_quality (stack));
	guint       i;

	json_object_set_string_member (o, "path", path);
	if (lrg_post_stack_get_name (stack) != NULL)
		json_object_set_string_member (o, "name", lrg_post_stack_get_name (stack));
	else
		json_object_set_null_member (o, "name");
	json_object_set_string_member (o, "quality", q != NULL ? q->value_nick : "high");
	json_object_set_boolean_member (o, "hdr", lrg_post_stack_get_hdr (stack));
	json_object_set_boolean_member (o, "dirty", dirty);
	for (i = 0; i < lrg_post_stack_get_effect_count (stack); i++)
	{
		JsonObject   *e = json_object_new ();
		JsonObject   *ps = json_object_new ();
		g_auto(GStrv) names = lrg_post_stack_get_param_names (stack, i);
		guint         k;

		json_object_set_int_member (e, "index", i);
		json_object_set_string_member (e, "type", lrg_post_stack_get_effect_type (stack, i));
		json_object_set_boolean_member (e, "enabled", lrg_post_stack_get_effect_enabled (stack, i));
		json_object_set_double_member (e, "weight", lrg_post_stack_get_effect_weight (stack, i));
		for (k = 0; names != NULL && names[k] != NULL; k++)
		{
			g_auto(GValue) v = G_VALUE_INIT;

			if (lrg_post_stack_get_param (stack, i, names[k], &v))
				json_object_set_member (ps, names[k], gvalue_json (&v));
		}
		json_object_set_object_member (e, "params", ps);
		json_array_add_object_element (effects, e);
	}
	json_object_set_array_member (o, "effects", effects);
	return obj_node (o);
}

static JsonNode *
editor_json (LrgEditorPostStackEditor  *self,
             GError                   **error)
{
	g_autoptr(GError)       local = NULL;
	g_autoptr(LrgPostStack) stack = lrg_editor_post_stack_editor_build_stack (self, &local);

	if (stack == NULL)
	{
		failed (error, local);
		return NULL;
	}
	return stack_json (stack, lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)),
	                   lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (self)));
}

static JsonNode *
m_types (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	g_auto(GStrv) names = lrg_post_stack_list_effect_types ();
	JsonObject   *o = json_object_new ();
	JsonArray    *types = json_array_new ();
	guint         i, k;

	for (i = 0; names != NULL && names[i] != NULL; i++)
	{
		JsonObject          *t = json_object_new ();
		JsonArray           *ps = json_array_new ();
		g_autoptr(GPtrArray) specs = effect_params (lrg_post_stack_lookup_effect_type (names[i]));

		json_object_set_string_member (t, "type", names[i]);
		for (k = 0; k < specs->len; k++)
		{
			GParamSpec    *p = g_ptr_array_index (specs, k);
			JsonObject    *po = json_object_new ();
			g_auto(GValue) def = G_VALUE_INIT;

			json_object_set_string_member (po, "name", p->name);
			json_object_set_string_member (po, "type", g_type_name (p->value_type));
			g_value_init (&def, p->value_type);
			g_param_value_set_default (p, &def);
			json_object_set_member (po, "default", gvalue_json (&def));
			if (G_IS_PARAM_SPEC_FLOAT (p))
			{
				json_object_set_double_member (po, "min", G_PARAM_SPEC_FLOAT (p)->minimum);
				json_object_set_double_member (po, "max", G_PARAM_SPEC_FLOAT (p)->maximum);
			}
			else if (G_IS_PARAM_SPEC_DOUBLE (p))
			{
				json_object_set_double_member (po, "min", G_PARAM_SPEC_DOUBLE (p)->minimum);
				json_object_set_double_member (po, "max", G_PARAM_SPEC_DOUBLE (p)->maximum);
			}
			else if (G_IS_PARAM_SPEC_INT (p))
			{
				json_object_set_int_member (po, "min", G_PARAM_SPEC_INT (p)->minimum);
				json_object_set_int_member (po, "max", G_PARAM_SPEC_INT (p)->maximum);
			}
			else if (G_IS_PARAM_SPEC_UINT (p))
			{
				json_object_set_int_member (po, "min", G_PARAM_SPEC_UINT (p)->minimum);
				json_object_set_int_member (po, "max", G_PARAM_SPEC_UINT (p)->maximum);
			}
			json_array_add_object_element (ps, po);
		}
		json_object_set_array_member (t, "params", ps);
		json_array_add_object_element (types, t);
	}
	json_object_set_array_member (o, "types", types);
	return obj_node (o);
}

static JsonNode *
m_open (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorPostStackEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject               *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "panel", lrg_editor_data_editor_get_panel_id (LRG_EDITOR_DATA_EDITOR (self)));
	return obj_node (o);
}

/* {"path": "looks/dusk.rpost", "preset": "default" | "empty"} */
static JsonNode *
m_create (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp            *app = LRG_EDITOR_APP (user_data);
	const gchar             *path = p_string (params, "path");
	const gchar             *preset = p_string (params, "preset");
	g_autofree gchar        *abs = NULL;
	g_autofree gchar        *dir = NULL;
	g_autoptr(LrgPostStack)  stack = lrg_post_stack_new ();
	g_autoptr(GError)        local = NULL;
	JsonObject              *o;

	if (path == NULL)
	{
		bad_params (error, "Missing \"path\"");
		return NULL;
	}
	abs = editor_app_project_path (app, path);
	if (!g_str_has_suffix (abs, ".rpost"))
	{
		gchar *with = g_strconcat (abs, ".rpost", NULL);

		g_free (abs);
		abs = with;
	}
	if (g_file_test (abs, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s already exists", abs);
		return NULL;
	}
	/* the default preset is the common HDR chain */
	if (g_strcmp0 (preset, "empty") != 0)
	{
		static const gchar * const chain[] = { "bloom", "tonemap", "vignette", "fxaa" };
		guint                      i;

		for (i = 0; i < G_N_ELEMENTS (chain); i++)
			if (lrg_post_stack_lookup_effect_type (chain[i]) != G_TYPE_INVALID)
				lrg_post_stack_add_effect (stack, chain[i], NULL);
	}
	dir = g_path_get_dirname (abs);
	g_mkdir_with_parents (dir, 0755);
	if (!lrg_post_stack_save_to_file (stack, abs, &local))
	{
		failed (error, local);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", abs);
	return obj_node (o);
}

static JsonNode *
m_get (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorPostStackEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	return (self != NULL) ? editor_json (self, error) : NULL;
}

static JsonNode *
m_add (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorPostStackEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *type = p_string (params, "type");
	g_autoptr(GError)         local = NULL;
	gint64                    index = -1;
	gint                      at;
	JsonObject               *o;

	if (self == NULL)
		return NULL;
	if (type == NULL)
	{
		bad_params (error, "Missing \"type\"");
		return NULL;
	}
	p_int (params, "index", &index);
	at = lrg_editor_post_stack_editor_add_effect (self, type, (gint) index, &local);
	if (at < 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "%s", local->message);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_int_member (o, "index", at);
	return obj_node (o);
}

static JsonNode *
m_remove (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorPostStackEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)         local = NULL;
	gint64                    index = -1;
	JsonObject               *o;

	if (self == NULL)
		return NULL;
	if (!p_int (params, "index", &index) || index < 0)
	{
		bad_params (error, "Missing \"index\"");
		return NULL;
	}
	if (!lrg_editor_post_stack_editor_remove_effect (self, (guint) index, &local))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "%s", local->message);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_boolean_member (o, "removed", TRUE);
	return obj_node (o);
}

static JsonNode *
m_move (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorPostStackEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)         local = NULL;
	gint64                    from = -1, to = -1;
	JsonObject               *o;

	if (self == NULL)
		return NULL;
	if (!p_int (params, "from", &from) || !p_int (params, "to", &to) || from < 0 || to < 0)
	{
		bad_params (error, "Give \"from\" and \"to\"");
		return NULL;
	}
	if (!lrg_editor_post_stack_editor_move_effect (self, (guint) from, (guint) to, &local))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "%s", local->message);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_int_member (o, "index", to);
	return obj_node (o);
}

static JsonNode *
m_set (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorPostStackEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *key = p_string (params, "key");
	g_autoptr(GError)         local = NULL;
	g_auto(GValue)            v = G_VALUE_INIT;
	gint64                    index = -1;
	JsonObject               *o;

	if (self == NULL)
		return NULL;
	if (!p_int (params, "index", &index) || index < 0 || key == NULL ||
	    !json_to_value (json_object_get_member (params, "value"), &v))
	{
		bad_params (error, "Give \"index\", \"key\" and a scalar \"value\"");
		return NULL;
	}
	if (!lrg_editor_post_stack_editor_set_value (self, (guint) index, key, &v, &local))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "%s", local->message);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_boolean_member (o, "ok", TRUE);
	return obj_node (o);
}

static JsonNode *
m_save (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorPostStackEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)         local = NULL;
	JsonObject               *o;

	if (self == NULL)
		return NULL;
	if (!lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (self), &local))
	{
		failed (error, local);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	return obj_node (o);
}

/* {"path"?: ..., "enabled": bool} */
static JsonNode *
m_preview (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorApp             *app = LRG_EDITOR_APP (user_data);
	LrgEditorPostStackEditor *self = NULL;
	LrgEditorPostStackEditor *cur;
	gboolean                  enabled = TRUE;
	JsonObject               *o;

	if (params != NULL && json_object_has_member (params, "enabled"))
		enabled = json_object_get_boolean_member (params, "enabled");
	if (p_string (params, "path") != NULL)
	{
		self = ipc_editor (app, params, error);
		if (self == NULL)
			return NULL;
		lrg_editor_post_stack_editor_set_preview (self, enabled);
	}
	else if (!enabled)
	{
		cur = g_object_get_data (G_OBJECT (app), PREVIEW_KEY);
		if (cur != NULL)
			lrg_editor_post_stack_editor_set_preview (cur, FALSE);
	}
	cur = g_object_get_data (G_OBJECT (app), PREVIEW_KEY);
	o = json_object_new ();
	if (cur != NULL)
		json_object_set_string_member (o, "preview", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (cur)));
	else
		json_object_set_null_member (o, "preview");
	return obj_node (o);
}

/* {"path": "looks/now.rpost", "viewport"?: 0}: the current look to a file */
static JsonNode *
m_save_look (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorApp            *app = LRG_EDITOR_APP (user_data);
	const gchar             *path = p_string (params, "path");
	g_autofree gchar        *abs = NULL;
	g_autoptr(LrgPostStack)  look = NULL;
	g_autoptr(GError)        local = NULL;
	gint64                   vp = -1;
	JsonObject              *o;

	if (path == NULL)
	{
		bad_params (error, "Missing \"path\"");
		return NULL;
	}
	p_int (params, "viewport", &vp);
	look = lrg_editor_post_stack_capture_look (app, (gint) vp);
	if (look == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No viewport look");
		return NULL;
	}
	abs = editor_app_project_path (app, path);
	if (!lrg_post_stack_save_to_file (look, abs, &local))
	{
		failed (error, local);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", abs);
	json_object_set_int_member (o, "effects", lrg_post_stack_get_effect_count (look));
	return obj_node (o);
}

/* {"path": "looks/dusk.rpost"} -> the level environment's post_stack */
static JsonNode *
m_assign (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp              *app = LRG_EDITOR_APP (user_data);
	LrgEditorEnvironmentPanel *env = lrg_editor_environment_panel_find (app);
	const gchar               *path = p_string (params, "path");
	g_autofree gchar          *abs = NULL;
	g_autofree gchar          *rel = NULL;
	g_auto(GValue)             v = G_VALUE_INIT;
	JsonObject                *o;

	/* An app whose window has no environment panel gets an undocked one
	 * (the app keeps it), so the edit is still one undoable command */
	if (env == NULL)
	{
		env = lrg_editor_environment_panel_new (app);
		g_object_set_data_full (G_OBJECT (app), "lrg-post-stack-env-panel", env, g_object_unref);
	}
	g_value_init (&v, G_TYPE_STRING);
	if (path != NULL && *path != '\0')
	{
		abs = editor_app_project_path (app, path);
		rel = editor_app_relative_path (app, abs);
	}
	g_value_set_string (&v, rel);
	if (!lrg_editor_environment_panel_set_field (env, "post-stack", &v))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No level is open");
		return NULL;
	}
	o = json_object_new ();
	if (rel != NULL)
		json_object_set_string_member (o, "post_stack", rel);
	else
		json_object_set_null_member (o, "post_stack");
	return obj_node (o);
}

static void
a_save_look (gpointer data)
{
	LrgEditorApp            *app = LRG_EDITOR_APP (data);
	LrgEditorDataEditor     *ed = lrg_editor_data_editor_find (app, NULL);
	g_autoptr(GError)        error = NULL;

	if (ed != NULL && LRG_IS_EDITOR_POST_STACK_EDITOR (ed))
	{
		if (!lrg_editor_post_stack_editor_save_current_look (LRG_EDITOR_POST_STACK_EDITOR (ed), &error))
			editor_app_notify (app, EDITOR_NOTIFY_ERROR, "%s", error->message);
		return;
	}
	editor_app_notify (app, EDITOR_NOTIFY_INFO, "Open a .rpost post stack to save the current look into it");
}

void
lrg_editor_post_stack_editor_install (LrgEditorApp *app)
{
	LrgEditorServer *server;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));

	if (g_object_get_data (G_OBJECT (app), INSTALL_KEY) != NULL)
		return;
	g_object_set_data (G_OBJECT (app), INSTALL_KEY, GINT_TO_POINTER (1));
	lrg_editor_data_editor_install (app);
	lrg_editor_actions_add (lrg_editor_app_get_actions (app), "post.save-look", "Save Current Look", "Post", NULL,
	                        a_save_look, NULL, NULL, app);
	server = lrg_editor_app_get_server (app);
	if (server == NULL)
		return;
	lrg_editor_server_register_method (server, "post.effect_types", m_types, app, NULL);
	lrg_editor_server_register_method (server, "post.open", m_open, app, NULL);
	lrg_editor_server_register_method (server, "post.create", m_create, app, NULL);
	lrg_editor_server_register_method (server, "post.get", m_get, app, NULL);
	lrg_editor_server_register_method (server, "post.add", m_add, app, NULL);
	lrg_editor_server_register_method (server, "post.remove", m_remove, app, NULL);
	lrg_editor_server_register_method (server, "post.move", m_move, app, NULL);
	lrg_editor_server_register_method (server, "post.set", m_set, app, NULL);
	lrg_editor_server_register_method (server, "post.save", m_save, app, NULL);
	lrg_editor_server_register_method (server, "post.preview", m_preview, app, NULL);
	lrg_editor_server_register_method (server, "post.save_look", m_save_look, app, NULL);
	lrg_editor_server_register_method (server, "post.assign", m_assign, app, NULL);
}
