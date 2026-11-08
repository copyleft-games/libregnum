/* lrg-editor-terrain-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The lrgeditor Terrain panel, the terrain / water viewport tools and the
 * editor's shared terrain and water store (see the header for the tour).
 *
 * Layout (top to bottom): a header (title, the node's name, Rebake, Save),
 * then in a scroll box the tool strip (eleven painted tool cells), the
 * brush grid, the surface layer list with Add and the selected layer's
 * grid, the foliage layer list with a kind picker, Add and the selected
 * foliage layer's grid, and a one-line chunk grid summary. Without a
 * terrain node an empty state explains what to do.
 *
 * The store.  An EditorTerrainStore hangs on the LrgEditor (object data)
 * and owns every loaded LrgTerrainAsset and LrgWaterBody by absolute path,
 * the set of dirty paths, and one NodeProxy per drawn terrain / water
 * node: a private LrgEntity3D carrying the node's world transform and an
 * LrgTerrainComponent / LrgWaterComponent showing the stored asset. The
 * scene renderer draws the proxies' components, the brush picks through
 * lrg_terrain_component_raycast() on them, so all of them see one asset.
 * The editor's "saved" signal writes the dirty files.
 *
 * Undo.  Terrain data does not live in the level, so its history entries
 * are LrgEditorCommand set-property commands on a private EditorAssetUndo
 * object: its boolean "applied" property calls a restore function with
 * "before" (FALSE) or "after" (TRUE). A brush stroke restores its
 * LrgTerrainEdit (lrg_terrain_edit_apply()); a layer / foliage change
 * restores a settings snapshot (surface layers, foliage layers and, for
 * structural changes, the full splat, ids and density maps); a water point
 * drag restores the body's point list. Each undo object belongs to one
 * command, so nothing merges by accident; a grid gesture keeps updating
 * the "after" side of its first command.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL

#include "lrg-editor-terrain-panel.h"
#include "lrg-editor-app-private.h"
#include "lrg-editor-console-panel.h"
#include "data/lrg-editor-water-editor.h"
#include "../lrg-editor-math.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-scroll-box.h"
#include "../../ui/lrg-combo-box.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../ui/lrg-property-source.h"
#include "../../ui/lrg-ui-event.h"
#include "../../paint/lrg-paint-icons.h"
#include "../../world3d/lrg-entity3d.h"
#include "../../world3d/components/lrg-terrain-component.h"
#include "../../world3d/components/lrg-water-component.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <raylib.h>

#define PANEL_KEY       "lrg-editor-terrain-panel"
#define STORE_KEY       "lrg-editor-terrain-store"
#define TOOL_CELL_W     (58.0f)
#define TOOL_CELL_H     (50.0f)
#define LIST_ROW_H      (24.0f)
#define LIST_BTN_W      (22.0f)
#define RING_SEGMENTS   (48)
#define HANDLE_RADIUS   (6.0f)
#define HANDLE_PICK     (9.0f)
#define PICK_DISTANCE   (100000.0f)
#define DEFAULT_RADIUS  (8.0f)
#define DEFAULT_STRENGTH (6.0f)
#define PRESS_AMOUNT    (1.0f / 60.0f)
#define MAX_LAYERS      (8)

/* ==========================================================================
 * Enums
 * ========================================================================== */

GType
lrg_editor_terrain_tool_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_TERRAIN_TOOL_SELECT, "LRG_EDITOR_TERRAIN_TOOL_SELECT", "select" },
			{ LRG_EDITOR_TERRAIN_TOOL_RAISE, "LRG_EDITOR_TERRAIN_TOOL_RAISE", "raise" },
			{ LRG_EDITOR_TERRAIN_TOOL_LOWER, "LRG_EDITOR_TERRAIN_TOOL_LOWER", "lower" },
			{ LRG_EDITOR_TERRAIN_TOOL_FLATTEN, "LRG_EDITOR_TERRAIN_TOOL_FLATTEN", "flatten" },
			{ LRG_EDITOR_TERRAIN_TOOL_SMOOTH, "LRG_EDITOR_TERRAIN_TOOL_SMOOTH", "smooth" },
			{ LRG_EDITOR_TERRAIN_TOOL_NOISE, "LRG_EDITOR_TERRAIN_TOOL_NOISE", "noise" },
			{ LRG_EDITOR_TERRAIN_TOOL_PAINT, "LRG_EDITOR_TERRAIN_TOOL_PAINT", "paint" },
			{ LRG_EDITOR_TERRAIN_TOOL_HOLE, "LRG_EDITOR_TERRAIN_TOOL_HOLE", "hole" },
			{ LRG_EDITOR_TERRAIN_TOOL_FILL, "LRG_EDITOR_TERRAIN_TOOL_FILL", "fill" },
			{ LRG_EDITOR_TERRAIN_TOOL_FOLIAGE_PAINT, "LRG_EDITOR_TERRAIN_TOOL_FOLIAGE_PAINT", "foliage-paint" },
			{ LRG_EDITOR_TERRAIN_TOOL_FOLIAGE_ERASE, "LRG_EDITOR_TERRAIN_TOOL_FOLIAGE_ERASE", "foliage-erase" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorTerrainTool"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_editor_terrain_stroke_flags_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_EDITOR_TERRAIN_STROKE_NONE, "LRG_EDITOR_TERRAIN_STROKE_NONE", "none" },
			{ LRG_EDITOR_TERRAIN_STROKE_INVERT, "LRG_EDITOR_TERRAIN_STROKE_INVERT", "invert" },
			{ LRG_EDITOR_TERRAIN_STROKE_SMOOTH, "LRG_EDITOR_TERRAIN_STROKE_SMOOTH", "smooth" },
			{ 0, NULL, NULL }
		};
		GType id = g_flags_register_static (g_intern_static_string ("LrgEditorTerrainStrokeFlags"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* Tool table: label and brush operation, in LrgEditorTerrainTool order. */
static const struct
{
	const gchar       *label;
	LrgTerrainBrushOp  op;
} tool_table[] = {
	{ "Select", LRG_TERRAIN_BRUSH_RAISE },
	{ "Raise", LRG_TERRAIN_BRUSH_RAISE },
	{ "Lower", LRG_TERRAIN_BRUSH_LOWER },
	{ "Flatten", LRG_TERRAIN_BRUSH_FLATTEN },
	{ "Smooth", LRG_TERRAIN_BRUSH_SMOOTH },
	{ "Noise", LRG_TERRAIN_BRUSH_NOISE },
	{ "Paint", LRG_TERRAIN_BRUSH_PAINT },
	{ "Hole", LRG_TERRAIN_BRUSH_HOLE },
	{ "Fill", LRG_TERRAIN_BRUSH_FILL },
	{ "Foliage", LRG_TERRAIN_BRUSH_FOLIAGE_PAINT },
	{ "Erase", LRG_TERRAIN_BRUSH_FOLIAGE_ERASE },
};

#define N_TOOLS (G_N_ELEMENTS (tool_table))

LrgTerrainBrushOp
lrg_editor_terrain_tool_get_op (LrgEditorTerrainTool tool)
{
	g_return_val_if_fail ((guint) tool < N_TOOLS, LRG_TERRAIN_BRUSH_RAISE);
	return tool_table[tool].op;
}

const gchar *
lrg_editor_terrain_tool_get_label (LrgEditorTerrainTool tool)
{
	g_return_val_if_fail ((guint) tool < N_TOOLS, "");
	return tool_table[tool].label;
}

/* The operation a stroke runs: the tool's, inverted by Ctrl, smoothing
 * with Shift. */
static LrgTerrainBrushOp
stroke_op (LrgEditorTerrainTool        tool,
           LrgEditorTerrainStrokeFlags flags)
{
	LrgTerrainBrushOp op = lrg_editor_terrain_tool_get_op (tool);

	if (flags & LRG_EDITOR_TERRAIN_STROKE_SMOOTH)
		return LRG_TERRAIN_BRUSH_SMOOTH;
	if (!(flags & LRG_EDITOR_TERRAIN_STROKE_INVERT))
		return op;
	switch (op)
	{
	case LRG_TERRAIN_BRUSH_RAISE:         return LRG_TERRAIN_BRUSH_LOWER;
	case LRG_TERRAIN_BRUSH_LOWER:         return LRG_TERRAIN_BRUSH_RAISE;
	case LRG_TERRAIN_BRUSH_HOLE:          return LRG_TERRAIN_BRUSH_FILL;
	case LRG_TERRAIN_BRUSH_FILL:          return LRG_TERRAIN_BRUSH_HOLE;
	case LRG_TERRAIN_BRUSH_FOLIAGE_PAINT: return LRG_TERRAIN_BRUSH_FOLIAGE_ERASE;
	case LRG_TERRAIN_BRUSH_FOLIAGE_ERASE: return LRG_TERRAIN_BRUSH_FOLIAGE_PAINT;
	default:                              return op;
	}
}

static const gchar *
op_label (LrgTerrainBrushOp op)
{
	switch (op)
	{
	case LRG_TERRAIN_BRUSH_RAISE:         return "Raise";
	case LRG_TERRAIN_BRUSH_LOWER:         return "Lower";
	case LRG_TERRAIN_BRUSH_FLATTEN:       return "Flatten";
	case LRG_TERRAIN_BRUSH_SMOOTH:        return "Smooth";
	case LRG_TERRAIN_BRUSH_NOISE:         return "Noise";
	case LRG_TERRAIN_BRUSH_PAINT:         return "Paint";
	case LRG_TERRAIN_BRUSH_HOLE:          return "Hole";
	case LRG_TERRAIN_BRUSH_FILL:          return "Fill";
	case LRG_TERRAIN_BRUSH_FOLIAGE_PAINT: return "Foliage paint";
	case LRG_TERRAIN_BRUSH_FOLIAGE_ERASE: return "Foliage erase";
	default:                              return "Brush";
	}
}

static gboolean
op_is_foliage (LrgTerrainBrushOp op)
{
	return op == LRG_TERRAIN_BRUSH_FOLIAGE_PAINT || op == LRG_TERRAIN_BRUSH_FOLIAGE_ERASE;
}

/* ==========================================================================
 * EditorAssetUndo: a history entry that restores asset data
 * ========================================================================== */

typedef EditorTerrainRestoreFunc UndoRestoreFunc;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (EditorAssetUndo, g_object_unref)

G_GNUC_INTERNAL GType editor_asset_undo_get_type (void);
#define EDITOR_TYPE_ASSET_UNDO (editor_asset_undo_get_type ())
#define EDITOR_ASSET_UNDO(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), EDITOR_TYPE_ASSET_UNDO, EditorAssetUndo))
typedef struct { GObjectClass parent_class; } EditorAssetUndoClass;

struct _EditorAssetUndo
{
	GObject         parent_instance;
	UndoRestoreFunc func;
	gpointer        data;
	GDestroyNotify  destroy;
	gboolean        applied;
};

G_DEFINE_TYPE (EditorAssetUndo, editor_asset_undo, G_TYPE_OBJECT)

enum
{
	UNDO_PROP_0,
	UNDO_PROP_APPLIED
};

static void
editor_asset_undo_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	EditorAssetUndo *self = EDITOR_ASSET_UNDO (object);

	if (prop_id != UNDO_PROP_APPLIED)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}
	self->applied = g_value_get_boolean (value);
	if (self->func != NULL)
		self->func (self->data, self->applied);
}

static void
editor_asset_undo_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	EditorAssetUndo *self = EDITOR_ASSET_UNDO (object);

	if (prop_id != UNDO_PROP_APPLIED)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}
	g_value_set_boolean (value, self->applied);
}

static void
editor_asset_undo_finalize (GObject *object)
{
	EditorAssetUndo *self = EDITOR_ASSET_UNDO (object);

	if (self->destroy != NULL && self->data != NULL)
		self->destroy (self->data);
	G_OBJECT_CLASS (editor_asset_undo_parent_class)->finalize (object);
}

static void
editor_asset_undo_class_init (EditorAssetUndoClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = editor_asset_undo_set_property;
	object_class->get_property = editor_asset_undo_get_property;
	object_class->finalize = editor_asset_undo_finalize;
	g_object_class_install_property (object_class, UNDO_PROP_APPLIED,
	                                 g_param_spec_boolean ("applied", NULL, NULL, TRUE,
	                                                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
editor_asset_undo_init (EditorAssetUndo *self)
{
	self->applied = TRUE;
}

/*
 * Records an already applied change as one history step: executing the
 * command sets "applied" to TRUE, which restores the "after" state the
 * data is already in. Returns the undo object (owned by the command).
 */
EditorAssetUndo *
editor_terrain_push_undo (LrgEditor       *editor,
                          const gchar     *label,
                          UndoRestoreFunc  func,
                          gpointer         data,
                          GDestroyNotify   destroy)
{
	g_autoptr(EditorAssetUndo) undo = g_object_new (EDITOR_TYPE_ASSET_UNDO, NULL);
	g_auto(GValue)             before = G_VALUE_INIT;
	g_auto(GValue)             after = G_VALUE_INIT;
	LrgEditorCommand          *cmd;

	undo->func = func;
	undo->data = data;
	undo->destroy = destroy;
	g_value_init (&before, G_TYPE_BOOLEAN);
	g_value_set_boolean (&before, FALSE);
	g_value_init (&after, G_TYPE_BOOLEAN);
	g_value_set_boolean (&after, TRUE);
	cmd = lrg_editor_command_new_set_property (G_OBJECT (undo), "applied", &before, &after);
	lrg_editor_command_set_label (cmd, label);
	lrg_editor_execute (editor, cmd);
	return undo;   /* the command holds the reference */
}

/* ==========================================================================
 * The store
 * ========================================================================== */

typedef struct _TerrainStore TerrainStore;

typedef struct
{
	TerrainStore *store;
	LrgNode      *node;        /* weak-referenced */
	LrgEntity3D  *entity;      /* owned */
	LrgComponent *comp;        /* held by the entity */
	gchar        *path;
	gpointer      shown;       /* the asset / body set on the component */
	guint         serial;      /* water: the store serial it was built for */
	gfloat        matrix[16];
	gboolean      placed;
} NodeProxy;

struct _TerrainStore
{
	LrgEditor  *editor;        /* the owner (not referenced) */
	GHashTable *terrains;      /* path -> LrgTerrainAsset */
	GHashTable *waters;        /* path -> LrgWaterBody */
	GHashTable *dirty;         /* path set */
	GHashTable *serials;       /* path -> water serial (GUINT_TO_POINTER) */
	GHashTable *proxies;       /* LrgNode* -> NodeProxy */
	gulong      saved_id;
};

static void proxy_node_gone (gpointer data, GObject *where_the_object_was);

static void
proxy_free (gpointer data)
{
	NodeProxy *p = data;

	if (p->node != NULL)
		g_object_weak_unref (G_OBJECT (p->node), proxy_node_gone, p->store);
	g_clear_object (&p->entity);
	g_free (p->path);
	g_free (p);
}

static void
proxy_node_gone (gpointer  data,
                 GObject  *where_the_object_was)
{
	TerrainStore *store = data;
	NodeProxy    *p = g_hash_table_lookup (store->proxies, where_the_object_was);

	if (p != NULL)
	{
		p->node = NULL;   /* already finalizing: no weak_unref */
		g_hash_table_remove (store->proxies, where_the_object_was);
	}
}

/* Writes the dirty files after the level was saved. */
static void
on_editor_saved (LrgEditor   *editor,
                 const gchar *path,
                 gpointer     user_data)
{
	g_autoptr(GError) error = NULL;

	lrg_editor_terrain_store_save_all (editor, &error);
	if (error != NULL)
		g_warning ("Terrain/water save: %s", error->message);
}

static void
store_free (gpointer data)
{
	TerrainStore *store = data;

	g_hash_table_destroy (store->proxies);
	g_hash_table_destroy (store->terrains);
	g_hash_table_destroy (store->waters);
	g_hash_table_destroy (store->dirty);
	g_hash_table_destroy (store->serials);
	g_free (store);
}

static TerrainStore *
store_of (LrgEditor *editor)
{
	TerrainStore *store = g_object_get_data (G_OBJECT (editor), STORE_KEY);

	if (store != NULL)
		return store;
	store = g_new0 (TerrainStore, 1);
	store->editor = editor;
	store->terrains = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	store->waters = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	store->dirty = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	store->serials = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	store->proxies = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, proxy_free);
	store->saved_id = g_signal_connect (editor, "saved", G_CALLBACK (on_editor_saved), NULL);
	g_object_set_data_full (G_OBJECT (editor), STORE_KEY, store, store_free);
	return store;
}

static void
bump_serial (TerrainStore *store,
             const gchar  *path)
{
	guint s = GPOINTER_TO_UINT (g_hash_table_lookup (store->serials, path));

	g_hash_table_insert (store->serials, g_strdup (path), GUINT_TO_POINTER (s + 1));
}

LrgTerrainAsset *
lrg_editor_terrain_store_get_terrain (LrgEditor    *editor,
                                      const gchar  *path,
                                      GError      **error)
{
	TerrainStore    *store;
	LrgTerrainAsset *asset;

	g_return_val_if_fail (LRG_IS_EDITOR (editor), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	store = store_of (editor);
	asset = g_hash_table_lookup (store->terrains, path);
	if (asset == NULL)
	{
		asset = lrg_terrain_asset_new_from_file (path, error);
		if (asset == NULL)
			return NULL;
		g_hash_table_insert (store->terrains, g_strdup (path), asset);
	}
	return asset;
}

LrgWaterBody *
lrg_editor_terrain_store_get_water (LrgEditor    *editor,
                                    const gchar  *path,
                                    GError      **error)
{
	TerrainStore *store;
	LrgWaterBody *body;

	g_return_val_if_fail (LRG_IS_EDITOR (editor), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	store = store_of (editor);
	body = g_hash_table_lookup (store->waters, path);
	if (body == NULL)
	{
		body = lrg_water_body_new_from_file (path, error);
		if (body == NULL)
			return NULL;
		g_hash_table_insert (store->waters, g_strdup (path), body);
	}
	return body;
}

void
lrg_editor_terrain_store_set_water (LrgEditor    *editor,
                                    const gchar  *path,
                                    LrgWaterBody *body)
{
	TerrainStore *store;
	LrgWaterBody *stored;

	g_return_if_fail (LRG_IS_EDITOR (editor));
	g_return_if_fail (path != NULL);
	g_return_if_fail (LRG_IS_WATER_BODY (body));

	store = store_of (editor);
	stored = g_hash_table_lookup (store->waters, path);
	if (stored == NULL)
		g_hash_table_insert (store->waters, g_strdup (path), g_object_ref (body));
	else if (stored != body)
		editor_water_copy_into (stored, body);
	bump_serial (store, path);
}

void
lrg_editor_terrain_store_set_dirty (LrgEditor   *editor,
                                    const gchar *path,
                                    gboolean     dirty)
{
	TerrainStore *store;

	g_return_if_fail (LRG_IS_EDITOR (editor));
	g_return_if_fail (path != NULL);

	store = store_of (editor);
	if (dirty)
		g_hash_table_add (store->dirty, g_strdup (path));
	else
		g_hash_table_remove (store->dirty, path);
	if (g_hash_table_contains (store->waters, path))
		bump_serial (store, path);
}

gboolean
lrg_editor_terrain_store_is_dirty (LrgEditor   *editor,
                                   const gchar *path)
{
	g_return_val_if_fail (LRG_IS_EDITOR (editor), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	return g_hash_table_contains (store_of (editor)->dirty, path);
}

gboolean
lrg_editor_terrain_store_save (LrgEditor    *editor,
                               const gchar  *path,
                               GError      **error)
{
	TerrainStore    *store;
	LrgTerrainAsset *asset;
	LrgWaterBody    *body;
	gboolean         ok;

	g_return_val_if_fail (LRG_IS_EDITOR (editor), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	store = store_of (editor);
	asset = g_hash_table_lookup (store->terrains, path);
	body = g_hash_table_lookup (store->waters, path);
	if (asset != NULL)
		ok = lrg_terrain_asset_save_to_file (asset, path, error);
	else if (body != NULL)
		ok = lrg_water_body_save_to_file (body, path, error);
	else
	{
		g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "%s is not loaded", path);
		return FALSE;
	}
	if (ok)
		g_hash_table_remove (store->dirty, path);
	return ok;
}

guint
lrg_editor_terrain_store_save_all (LrgEditor  *editor,
                                   GError    **error)
{
	TerrainStore        *store;
	g_autoptr(GPtrArray) paths = NULL;
	GHashTableIter       it;
	gpointer             key;
	guint                i, n = 0;

	g_return_val_if_fail (LRG_IS_EDITOR (editor), 0);

	store = store_of (editor);
	paths = g_ptr_array_new_with_free_func (g_free);
	g_hash_table_iter_init (&it, store->dirty);
	while (g_hash_table_iter_next (&it, &key, NULL))
		g_ptr_array_add (paths, g_strdup (key));
	for (i = 0; i < paths->len; i++)
	{
		g_autoptr(GError) local = NULL;

		if (lrg_editor_terrain_store_save (editor, g_ptr_array_index (paths, i), &local))
			n++;
		else if (error != NULL && *error == NULL)
			g_propagate_error (error, g_steal_pointer (&local));
	}
	return n;
}

void
lrg_editor_terrain_store_forget (LrgEditor   *editor,
                                 const gchar *path)
{
	TerrainStore *store;

	g_return_if_fail (LRG_IS_EDITOR (editor));
	g_return_if_fail (path != NULL);

	store = store_of (editor);
	g_hash_table_remove (store->terrains, path);
	g_hash_table_remove (store->waters, path);
	g_hash_table_remove (store->dirty, path);
	bump_serial (store, path);
}

gchar *
lrg_editor_terrain_node_path (LrgEditor          *editor,
                              LrgWorld3DRenderer *resolver,
                              LrgNode            *node)
{
	LrgNodeVisual     *visual;
	LrgNodeVisualKind  kind;
	const gchar       *ref;
	const gchar       *level_path;

	g_return_val_if_fail (LRG_IS_EDITOR (editor), NULL);
	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	visual = lrg_node_get_visual (node);
	kind = (visual != NULL) ? lrg_node_visual_get_kind (visual) : LRG_NODE_VISUAL_NONE;
	if (kind != LRG_NODE_VISUAL_TERRAIN && kind != LRG_NODE_VISUAL_WATER)
		return NULL;
	ref = lrg_node_visual_get_asset (visual);
	if (ref == NULL || *ref == '\0')
		return NULL;
	if (g_path_is_absolute (ref))
		return g_strdup (ref);

	/* relative to the level's directory first (the runtime's rule), then
	 * the renderer's resolver (GUIDs, the project directory) */
	level_path = lrg_editor_get_path (editor);
	if (level_path != NULL && !g_str_has_prefix (ref, "guid:"))
	{
		g_autofree gchar *dir = g_path_get_dirname (level_path);
		g_autofree gchar *path = g_build_filename (dir, ref, NULL);

		if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
			return g_steal_pointer (&path);
	}
	if (resolver != NULL)
		return lrg_world3d_renderer_resolve_asset (resolver, ref);
	return NULL;
}

LrgComponent *
lrg_editor_terrain_node_component (LrgEditor          *editor,
                                   LrgWorld3DRenderer *resolver,
                                   LrgNode            *node)
{
	TerrainStore      *store;
	NodeProxy         *p;
	LrgNodeVisual     *visual;
	LrgNodeVisualKind  kind;
	g_autofree gchar  *path = NULL;
	gpointer           asset;
	gfloat             world[16];
	GType              want;

	g_return_val_if_fail (LRG_IS_EDITOR (editor), NULL);
	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	path = lrg_editor_terrain_node_path (editor, resolver, node);
	if (path == NULL)
		return NULL;
	visual = lrg_node_get_visual (node);
	kind = lrg_node_visual_get_kind (visual);
	asset = (kind == LRG_NODE_VISUAL_TERRAIN) ? (gpointer) lrg_editor_terrain_store_get_terrain (editor, path, NULL)
	                                          : (gpointer) lrg_editor_terrain_store_get_water (editor, path, NULL);
	if (asset == NULL)
		return NULL;

	/* One proxy per node, remade when the node changed kind */
	store = store_of (editor);
	want = (kind == LRG_NODE_VISUAL_TERRAIN) ? LRG_TYPE_TERRAIN_COMPONENT : LRG_TYPE_WATER_COMPONENT;
	p = g_hash_table_lookup (store->proxies, node);
	if (p != NULL && !g_type_is_a (G_OBJECT_TYPE (p->comp), want))
	{
		g_hash_table_remove (store->proxies, node);
		p = NULL;
	}
	if (p == NULL)
	{
		g_autoptr(LrgComponent) comp = g_object_new (want, NULL);

		p = g_new0 (NodeProxy, 1);
		p->store = store;
		p->node = node;
		p->entity = lrg_entity3d_new_with_name (lrg_node_get_name (node));
		p->comp = comp;
		lrg_entity3d_add_component (p->entity, comp);
		g_object_weak_ref (G_OBJECT (node), proxy_node_gone, store);
		g_hash_table_insert (store->proxies, node, p);
	}

	/* The asset: a new one (or a replaced water body) is set again */
	if (p->shown != asset || g_strcmp0 (p->path, path) != 0)
	{
		g_free (p->path);
		p->path = g_strdup (path);
		p->shown = asset;
		if (kind == LRG_NODE_VISUAL_TERRAIN)
			lrg_terrain_component_set_asset (LRG_TERRAIN_COMPONENT (p->comp), asset);
		else
		{
			lrg_water_component_set_body (LRG_WATER_COMPONENT (p->comp), asset);
			p->serial = GPOINTER_TO_UINT (g_hash_table_lookup (store->serials, path));
		}
	}
	else if (kind == LRG_NODE_VISUAL_WATER)
	{
		guint serial = GPOINTER_TO_UINT (g_hash_table_lookup (store->serials, path));

		if (serial != p->serial)
		{
			p->serial = serial;
			lrg_water_component_rebuild (LRG_WATER_COMPONENT (p->comp));
		}
	}

	/* The node's params and world transform */
	if (kind == LRG_NODE_VISUAL_TERRAIN)
	{
		LrgTerrainComponent *tc = LRG_TERRAIN_COMPONENT (p->comp);
		gboolean             shadows = lrg_node_visual_get_param_double (visual, "cast_shadows", 1.0) != 0.0;
		gboolean             foliage = lrg_node_visual_get_param_double (visual, "foliage", 1.0) != 0.0;

		if (lrg_terrain_component_get_cast_shadows (tc) != shadows)
			lrg_terrain_component_set_cast_shadows (tc, shadows);
		if (lrg_terrain_component_get_foliage (tc) != foliage)
			lrg_terrain_component_set_foliage (tc, foliage);
	}
	lrg_editor_node_get_world_matrix (node, world);
	if (!p->placed || memcmp (world, p->matrix, sizeof world) != 0)
	{
		memcpy (p->matrix, world, sizeof world);
		lrg_entity3d_set_local_matrix (p->entity, world);
		p->placed = TRUE;
	}
	return p->comp;
}

/* ==========================================================================
 * Settings snapshots (surface layers, foliage layers, maps)
 * ========================================================================== */

typedef struct
{
	gchar  *name;
	gchar  *texture;
	gchar  *material;
	gfloat  tiling;
	gfloat  color[4];
} LayerState;

typedef struct
{
	gchar          *name;
	LrgFoliageKind  kind;
	gchar          *mesh;
	gchar          *lod_mesh;
	gfloat          lod_distance;
	gchar          *texture;
	gfloat          color[4];
	gfloat          density;
	gfloat          material_density[MAX_LAYERS];
	gfloat          scale[2];
	gfloat          slope[2];
	gfloat          height[2];
	gfloat          align;
	gfloat          size[2];
	gfloat          fade[2];
	guint32         seed;
} FoliageState;

typedef struct
{
	GPtrArray      *layers;     /* LayerState */
	GPtrArray      *foliage;    /* FoliageState */
	gboolean        structural; /* maps captured, lists rebuilt on restore */
	LrgTerrainEdit *splat;
	LrgTerrainEdit *ids;
	GPtrArray      *densities;  /* LrgTerrainEdit per foliage layer */
} Snapshot;

static void
layer_state_free (gpointer data)
{
	LayerState *s = data;

	g_free (s->name);
	g_free (s->texture);
	g_free (s->material);
	g_free (s);
}

static void
foliage_state_free (gpointer data)
{
	FoliageState *s = data;

	g_free (s->name);
	g_free (s->mesh);
	g_free (s->lod_mesh);
	g_free (s->texture);
	g_free (s);
}

static void
snapshot_free (Snapshot *snap)
{
	if (snap == NULL)
		return;
	g_ptr_array_unref (snap->layers);
	g_ptr_array_unref (snap->foliage);
	g_clear_pointer (&snap->splat, lrg_terrain_edit_free);
	g_clear_pointer (&snap->ids, lrg_terrain_edit_free);
	g_clear_pointer (&snap->densities, g_ptr_array_unref);
	g_free (snap);
}

/* The whole-grid edit of one data set (before = after = now). */
static LrgTerrainEdit *
capture_all (LrgTerrainAsset    *asset,
             LrgTerrainEditKind  kind,
             guint               layer)
{
	guint w, h;

	if (kind == LRG_TERRAIN_EDIT_HEIGHTS || kind == LRG_TERRAIN_EDIT_SPLAT)
		lrg_terrain_asset_get_samples (asset, &w, &h);
	else
		lrg_terrain_asset_get_cells (asset, &w, &h);
	return lrg_terrain_asset_capture_edit (asset, kind, layer, 0, 0, w, h);
}

static Snapshot *
snapshot_take (LrgTerrainAsset *asset,
               gboolean         structural)
{
	Snapshot   *snap = g_new0 (Snapshot, 1);
	LrgFoliage *foliage = lrg_terrain_asset_get_foliage (asset);
	guint       i, k;

	snap->layers = g_ptr_array_new_with_free_func (layer_state_free);
	snap->foliage = g_ptr_array_new_with_free_func (foliage_state_free);
	snap->structural = structural;

	for (i = 0; i < lrg_terrain_asset_get_n_layers (asset); i++)
	{
		LrgTerrainLayer *layer = lrg_terrain_asset_get_layer (asset, i);
		LayerState      *s = g_new0 (LayerState, 1);

		s->name = g_strdup (lrg_terrain_layer_get_name (layer));
		s->texture = g_strdup (lrg_terrain_layer_get_texture (layer));
		s->material = g_strdup (lrg_terrain_layer_get_material (layer));
		s->tiling = lrg_terrain_layer_get_tiling (layer);
		lrg_terrain_layer_get_color (layer, s->color);
		g_ptr_array_add (snap->layers, s);
	}
	for (i = 0; i < lrg_foliage_get_n_layers (foliage); i++)
	{
		LrgFoliageLayer *fl = lrg_foliage_get_layer (foliage, i);
		FoliageState    *s = g_new0 (FoliageState, 1);

		s->name = g_strdup (lrg_foliage_layer_get_name (fl));
		s->kind = lrg_foliage_layer_get_kind (fl);
		s->mesh = g_strdup (lrg_foliage_layer_get_mesh (fl));
		s->lod_mesh = g_strdup (lrg_foliage_layer_get_lod_mesh (fl));
		s->lod_distance = lrg_foliage_layer_get_lod_distance (fl);
		s->texture = g_strdup (lrg_foliage_layer_get_texture (fl));
		lrg_foliage_layer_get_color (fl, s->color);
		s->density = lrg_foliage_layer_get_density (fl);
		for (k = 0; k < MAX_LAYERS; k++)
			s->material_density[k] = lrg_foliage_layer_get_material_density (fl, k);
		lrg_foliage_layer_get_scale_range (fl, &s->scale[0], &s->scale[1]);
		lrg_foliage_layer_get_slope_range (fl, &s->slope[0], &s->slope[1]);
		lrg_foliage_layer_get_height_range (fl, &s->height[0], &s->height[1]);
		s->align = lrg_foliage_layer_get_align (fl);
		lrg_foliage_layer_get_size (fl, &s->size[0], &s->size[1]);
		lrg_foliage_layer_get_fade (fl, &s->fade[0], &s->fade[1]);
		s->seed = lrg_foliage_layer_get_seed (fl);
		g_ptr_array_add (snap->foliage, s);
	}
	if (structural)
	{
		snap->splat = capture_all (asset, LRG_TERRAIN_EDIT_SPLAT, 0);
		snap->ids = capture_all (asset, LRG_TERRAIN_EDIT_MATERIAL_IDS, 0);
		snap->densities = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_terrain_edit_free);
		for (i = 0; i < lrg_foliage_get_n_layers (foliage); i++)
			g_ptr_array_add (snap->densities, capture_all (asset, LRG_TERRAIN_EDIT_FOLIAGE, i));
	}
	return snap;
}

static void
foliage_apply_state (LrgFoliageLayer    *fl,
                     const FoliageState *s)
{
	guint k;

	lrg_foliage_layer_set_mesh (fl, s->mesh);
	lrg_foliage_layer_set_lod_mesh (fl, s->lod_mesh, s->lod_distance);
	lrg_foliage_layer_set_texture (fl, s->texture);
	lrg_foliage_layer_set_color (fl, s->color[0], s->color[1], s->color[2], s->color[3]);
	lrg_foliage_layer_set_density (fl, s->density);
	for (k = 0; k < MAX_LAYERS; k++)
		lrg_foliage_layer_set_material_density (fl, k, s->material_density[k]);
	lrg_foliage_layer_set_scale_range (fl, s->scale[0], s->scale[1]);
	lrg_foliage_layer_set_slope_range (fl, s->slope[0], s->slope[1]);
	lrg_foliage_layer_set_height_range (fl, s->height[0], s->height[1]);
	lrg_foliage_layer_set_align (fl, s->align);
	lrg_foliage_layer_set_size (fl, s->size[0], s->size[1]);
	lrg_foliage_layer_set_fade (fl, s->fade[0], s->fade[1]);
	lrg_foliage_layer_set_seed (fl, s->seed);
}

/* Marks every foliage patch for re-scattering. */
static void
foliage_dirty_all (LrgTerrainAsset *asset)
{
	guint cx, cz;

	lrg_terrain_asset_get_cells (asset, &cx, &cz);
	lrg_foliage_mark_dirty (lrg_terrain_asset_get_foliage (asset), 0, 0, cx, cz);
}

/* Every chunk shows the layers: rewriting the whole splat set (same data)
 * emits one region change, so the components bake every chunk again. */
static void
rebake_all (LrgTerrainAsset *asset)
{
	LrgTerrainEdit *e = capture_all (asset, LRG_TERRAIN_EDIT_SPLAT, 0);

	if (e != NULL)
	{
		lrg_terrain_edit_apply (e, asset, TRUE);
		lrg_terrain_edit_free (e);
	}
}

static void
snapshot_restore (LrgTerrainAsset *asset,
                  const Snapshot  *snap,
                  gboolean         maps_after)
{
	LrgFoliage *foliage = lrg_terrain_asset_get_foliage (asset);
	guint       i;

	/* Surface layers: the count, then every field */
	while (lrg_terrain_asset_get_n_layers (asset) > snap->layers->len &&
	       lrg_terrain_asset_remove_layer (asset, lrg_terrain_asset_get_n_layers (asset) - 1))
		;
	while (lrg_terrain_asset_get_n_layers (asset) < snap->layers->len &&
	       lrg_terrain_asset_add_layer (asset, "layer") != NULL)
		;
	for (i = 0; i < snap->layers->len && i < lrg_terrain_asset_get_n_layers (asset); i++)
	{
		const LayerState *s = g_ptr_array_index (snap->layers, i);
		LrgTerrainLayer  *layer = lrg_terrain_asset_get_layer (asset, i);

		lrg_terrain_layer_set_name (layer, s->name);
		lrg_terrain_layer_set_texture (layer, s->texture);
		lrg_terrain_layer_set_material (layer, s->material);
		lrg_terrain_layer_set_tiling (layer, s->tiling);
		lrg_terrain_layer_set_color (layer, s->color[0], s->color[1], s->color[2], s->color[3]);
	}

	/* Foliage layers: a structural snapshot rebuilds the list (kinds and
	 * order come back), else the fields of the layers there are */
	if (snap->structural)
	{
		while (lrg_foliage_get_n_layers (foliage) > 0)
			lrg_foliage_remove_layer (foliage, lrg_foliage_get_n_layers (foliage) - 1);
		for (i = 0; i < snap->foliage->len; i++)
		{
			const FoliageState        *s = g_ptr_array_index (snap->foliage, i);
			g_autoptr(LrgFoliageLayer) fl = lrg_foliage_layer_new (s->name, s->kind);

			foliage_apply_state (fl, s);
			lrg_foliage_add_layer (foliage, fl);
		}
		if (snap->splat != NULL)
			lrg_terrain_edit_apply (snap->splat, asset, maps_after);
		if (snap->ids != NULL)
			lrg_terrain_edit_apply (snap->ids, asset, maps_after);
		for (i = 0; snap->densities != NULL && i < snap->densities->len; i++)
			lrg_terrain_edit_apply (g_ptr_array_index (snap->densities, i), asset, maps_after);
	}
	else
	{
		for (i = 0; i < snap->foliage->len && i < lrg_foliage_get_n_layers (foliage); i++)
			foliage_apply_state (lrg_foliage_get_layer (foliage, i), g_ptr_array_index (snap->foliage, i));
	}
	if (!snap->structural)
		rebake_all (asset);
	foliage_dirty_all (asset);
}

/* ==========================================================================
 * Undo payloads
 * ========================================================================== */

typedef struct
{
	LrgEditor       *editor;   /* weak */
	LrgTerrainAsset *asset;
	gchar           *path;
	LrgTerrainEdit  *edit;
} StrokeUndo;

static void
stroke_undo_free (gpointer data)
{
	StrokeUndo *u = data;

	g_clear_weak_pointer (&u->editor);
	g_object_unref (u->asset);
	g_free (u->path);
	lrg_terrain_edit_free (u->edit);
	g_free (u);
}

static void
stroke_undo_restore (gpointer data,
                     gboolean after)
{
	StrokeUndo *u = data;

	lrg_terrain_edit_apply (u->edit, u->asset, after);
	if (lrg_terrain_edit_get_kind (u->edit) == LRG_TERRAIN_EDIT_HEIGHTS)
		foliage_dirty_all (u->asset);
	if (u->editor != NULL)
		lrg_editor_terrain_store_set_dirty (u->editor, u->path, TRUE);
}

typedef struct
{
	LrgEditor       *editor;   /* weak */
	LrgTerrainAsset *asset;
	gchar           *path;
	Snapshot        *before;
	Snapshot        *after;
} SettingsUndo;

static void
settings_undo_free (gpointer data)
{
	SettingsUndo *u = data;

	g_clear_weak_pointer (&u->editor);
	g_object_unref (u->asset);
	g_free (u->path);
	snapshot_free (u->before);
	snapshot_free (u->after);
	g_free (u);
}

static void
settings_undo_restore (gpointer data,
                       gboolean after)
{
	SettingsUndo *u = data;

	/* A structural step keeps its maps in "before"; redo applies their
	 * "after" side (captured when the step was recorded) */
	if (after)
		snapshot_restore (u->asset, u->after, TRUE);
	else
		snapshot_restore (u->asset, u->before, FALSE);
	if (u->editor != NULL)
		lrg_editor_terrain_store_set_dirty (u->editor, u->path, TRUE);
}

typedef struct
{
	LrgEditor    *editor;      /* weak */
	LrgEditorApp *app;         /* weak */
	LrgWaterBody *body;
	gchar        *path;
	GArray       *before;      /* 4 floats per point */
	GArray       *after;
} PointsUndo;

static void
points_undo_free (gpointer data)
{
	PointsUndo *u = data;

	g_clear_weak_pointer (&u->editor);
	g_clear_weak_pointer (&u->app);
	g_object_unref (u->body);
	g_free (u->path);
	g_array_unref (u->before);
	g_array_unref (u->after);
	g_free (u);
}

static GArray *
points_take (LrgWaterBody *body)
{
	GArray *a = g_array_new (FALSE, FALSE, sizeof (gfloat));
	guint   i;

	for (i = 0; i < lrg_water_body_get_n_points (body); i++)
	{
		gfloat p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		lrg_water_body_get_point (body, i, p);
		g_array_append_vals (a, p, 4);
	}
	return a;
}

static void
points_put (LrgWaterBody *body,
            GArray       *points)
{
	guint i;

	lrg_water_body_clear_points (body);
	for (i = 0; i + 3 < points->len; i += 4)
	{
		const gfloat *p = &g_array_index (points, gfloat, i);

		lrg_water_body_add_point (body, p[0], p[1], p[2], p[3]);
	}
}

static void
points_undo_restore (gpointer data,
                     gboolean after)
{
	PointsUndo *u = data;

	points_put (u->body, after ? u->after : u->before);
	if (u->editor != NULL)
		lrg_editor_terrain_store_set_dirty (u->editor, u->path, TRUE);
	if (u->app != NULL)
		lrg_editor_water_editor_sync_from_store (u->app, u->path);
}

/* ==========================================================================
 * The panel
 * ========================================================================== */

typedef enum
{
	PART_BRUSH,
	PART_LAYER,
	PART_FOLIAGE
} Part;

#define EDITOR_TYPE_TERRAIN_SOURCE (editor_terrain_source_get_type ())
G_DECLARE_FINAL_TYPE (EditorTerrainSource, editor_terrain_source, EDITOR, TERRAIN_SOURCE, GObject)

struct _EditorTerrainSource
{
	GObject                parent_instance;
	LrgEditorTerrainPanel *panel;   /* weak */
	Part                   part;
};

static void editor_terrain_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (EditorTerrainSource, editor_terrain_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE, editor_terrain_source_iface_init))

struct _LrgEditorTerrainPanel
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;          /* weak */
	LrgEditor            *editor;       /* owned */
	LrgEditorSelection   *selection;    /* owned */
	LrgEditorPlaySession *play;         /* owned */
	LrgNode              *node;         /* weak: the terrain node shown */
	LrgTerrainBrush      *brush;
	LrgEditorTerrainTool  tool;
	gint                  sel_layer;
	gint                  sel_foliage;
	gchar                *signature;

	/* widgets (owned by the container tree) */
	LrgWidget            *top;
	LrgWidget            *header_row;
	LrgLabel             *node_label;
	LrgWidget            *rebake_chip;
	LrgWidget            *save_chip;
	LrgWidget            *scroll;
	LrgWidget            *content;
	LrgWidget            *empty;
	LrgWidget            *hint_none;
	LrgWidget            *hint_bad;
	LrgWidget            *tools;
	LrgPropertyGrid      *brush_grid;
	LrgWidget            *layer_list;
	LrgPropertyGrid      *layer_grid;
	LrgComboBox          *foliage_kind;
	LrgWidget            *foliage_list;
	LrgPropertyGrid      *foliage_grid;
	LrgWidget            *info;
	LrgWidget            *add_layer_chip;

	EditorTerrainSource  *src[3];

	/* grid gestures: the first command's undo payload takes later values */
	gboolean              in_gesture;
	SettingsUndo         *gesture_undo;  /* owned by its command */

	/* the viewport brush */
	gboolean              cursor_valid;
	gfloat                cursor_local[3];
	gfloat                cursor_world[3];
	LrgInputModifiers     cursor_mods;
	gboolean              stroking;
	LrgTerrainAsset      *stroke_asset;  /* ref while stroking */
	gchar                *stroke_path;
	LrgTerrainBrushOp     stroke_saved_op;
	guint                 stroke_saved_layer;
	LrgTerrainBrushOp     stroke_op;
	gfloat                stroke_x;
	gfloat                stroke_z;
	guint                 stroke_dabs;

	/* water point handles */
	gint                  water_hot;
	gint                  water_drag;
	LrgNode              *water_node;    /* weak: the node being edited */
	GArray               *water_before;
	gfloat                water_plane_y;
};

G_DEFINE_FINAL_TYPE (LrgEditorTerrainPanel, lrg_editor_terrain_panel, LRG_TYPE_CONTAINER)

static void sync_widgets (LrgEditorTerrainPanel *self);

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgWorld3DRenderer *
resolver_of (LrgEditorTerrainPanel *self)
{
	if (self->app == NULL || self->app->renderer == NULL)
		return NULL;
	return lrg_editor_scene_renderer_get_runtime_renderer (self->app->renderer);
}

static gboolean
node_is_kind (LrgNode           *node,
              LrgNodeVisualKind  kind)
{
	LrgNodeVisual *visual = (node != NULL) ? lrg_node_get_visual (node) : NULL;

	return visual != NULL && lrg_node_visual_get_kind (visual) == kind;
}

static LrgNode *
primary (LrgEditorTerrainPanel *self)
{
	LrgEditorSelection *sel = (self->editor != NULL) ? lrg_editor_get_selection (self->editor) : NULL;

	return (sel != NULL) ? lrg_editor_selection_get_primary (sel) : NULL;
}

static LrgNode *
resolve_node (LrgEditorTerrainPanel *self,
              LrgNode               *node)
{
	return (node != NULL) ? node : self->node;
}

/* The stored asset of a terrain node (and its path). */
static LrgTerrainAsset *
asset_of (LrgEditorTerrainPanel  *self,
          LrgNode                *node,
          gchar                 **out_path)
{
	g_autofree gchar *path = NULL;
	LrgTerrainAsset  *asset;

	if (out_path != NULL)
		*out_path = NULL;
	if (self->editor == NULL || !node_is_kind (node, LRG_NODE_VISUAL_TERRAIN))
		return NULL;
	path = lrg_editor_terrain_node_path (self->editor, resolver_of (self), node);
	if (path == NULL)
		return NULL;
	asset = lrg_editor_terrain_store_get_terrain (self->editor, path, NULL);
	if (asset != NULL && out_path != NULL)
		*out_path = g_steal_pointer (&path);
	return asset;
}

static void
set_status (LrgEditorTerrainPanel *self,
            const gchar           *text)
{
	if (self->app != NULL)
		editor_app_set_status (self->app, "%s", text);
}

/* Asks every viewport for a new render (the store changed under them). */
static void
queue_render (LrgEditorTerrainPanel *self)
{
	if (self->app != NULL && self->app->viewport_panel != NULL)
		lrg_editor_viewport_panel_queue_render (self->app->viewport_panel);
}

/* ==========================================================================
 * Settings edits
 * ========================================================================== */

/*
 * Runs one settings change as one history step. @change edits the asset
 * (it returns FALSE when nothing changed); a structural change captures
 * the maps too. Inside a grid gesture later values update the first
 * step's "after" instead of adding steps.
 */
typedef gboolean (*SettingsChange) (LrgTerrainAsset *asset,
                                    gpointer         data);

static gboolean
settings_edit (LrgEditorTerrainPanel *self,
               LrgNode               *node,
               const gchar           *label,
               gboolean               structural,
               SettingsChange         change,
               gpointer               data)
{
	g_autofree gchar *path = NULL;
	LrgTerrainAsset  *asset = asset_of (self, resolve_node (self, node), &path);
	SettingsUndo     *u;
	Snapshot         *before;
	guint             i;

	if (asset == NULL)
		return FALSE;

	/* A gesture's later values: change, then refresh the step's after */
	if (!structural && self->in_gesture && self->gesture_undo != NULL && self->gesture_undo->asset == asset)
	{
		if (!change (asset, data))
			return FALSE;
		snapshot_free (self->gesture_undo->after);
		self->gesture_undo->after = snapshot_take (asset, FALSE);
		lrg_editor_terrain_store_set_dirty (self->editor, path, TRUE);
		lrg_editor_mark_modified (self->editor);
		queue_render (self);
		return TRUE;
	}

	before = snapshot_take (asset, structural);
	if (!change (asset, data))
	{
		snapshot_free (before);
		return FALSE;
	}

	/* The maps' "after" states, for redo */
	if (structural)
	{
		if (before->splat != NULL)
			lrg_terrain_edit_capture_after (before->splat, asset);
		if (before->ids != NULL)
			lrg_terrain_edit_capture_after (before->ids, asset);
	}

	u = g_new0 (SettingsUndo, 1);
	g_set_weak_pointer (&u->editor, self->editor);
	u->asset = g_object_ref (asset);
	u->path = g_strdup (path);
	u->before = before;
	u->after = snapshot_take (asset, structural);
	if (structural)
	{
		/* redo restores the after snapshot with the maps as they are now */
		g_clear_pointer (&u->after->splat, lrg_terrain_edit_free);
		g_clear_pointer (&u->after->ids, lrg_terrain_edit_free);
		u->after->splat = (before->splat != NULL) ? lrg_terrain_edit_copy (before->splat) : NULL;
		u->after->ids = (before->ids != NULL) ? lrg_terrain_edit_copy (before->ids) : NULL;
		for (i = 0; i < u->after->densities->len; i++)
			lrg_terrain_edit_capture_after (g_ptr_array_index (u->after->densities, i), asset);
	}
	lrg_editor_terrain_store_set_dirty (self->editor, path, TRUE);
	editor_terrain_push_undo (self->editor, label, settings_undo_restore, u, settings_undo_free);
	if (self->in_gesture && !structural)
		self->gesture_undo = u;
	queue_render (self);
	return TRUE;
}

/* --- layers --- */

typedef struct
{
	const gchar  *name;
	const GValue *value;
	guint         index;
	gint          result;
	LrgFoliageKind kind;
} FieldEdit;

static gboolean
change_add_layer (LrgTerrainAsset *asset,
                  gpointer         data)
{
	FieldEdit       *e = data;
	LrgTerrainLayer *layer;
	guint            n = lrg_terrain_asset_get_n_layers (asset);
	static const gfloat palette[MAX_LAYERS][3] = {
		{ 0.45f, 0.62f, 0.32f }, { 0.55f, 0.50f, 0.42f }, { 0.78f, 0.72f, 0.52f }, { 0.50f, 0.50f, 0.52f },
		{ 0.35f, 0.45f, 0.25f }, { 0.62f, 0.45f, 0.30f }, { 0.85f, 0.88f, 0.90f }, { 0.30f, 0.32f, 0.36f },
	};

	if (n >= MAX_LAYERS)
		return FALSE;
	layer = lrg_terrain_asset_add_layer (asset, e->name);
	if (layer == NULL)
		return FALSE;
	lrg_terrain_layer_set_color (layer, palette[n][0], palette[n][1], palette[n][2], 1.0f);
	e->result = (gint) n;
	return TRUE;
}

static gboolean
change_remove_layer (LrgTerrainAsset *asset,
                     gpointer         data)
{
	FieldEdit *e = data;

	return lrg_terrain_asset_remove_layer (asset, e->index);
}

/* A colour from a GValue: GrlColor, "#rrggbb[aa]" or "r,g,b[,a]" (0..1). */
static gboolean
value_to_rgba (const GValue *value,
               gfloat       *rgba)
{
	if (G_VALUE_HOLDS (value, GRL_TYPE_COLOR))
	{
		const GrlColor *c = g_value_get_boxed (value);

		if (c == NULL)
			return FALSE;
		rgba[0] = (gfloat) c->r / 255.0f;
		rgba[1] = (gfloat) c->g / 255.0f;
		rgba[2] = (gfloat) c->b / 255.0f;
		rgba[3] = (gfloat) c->a / 255.0f;
		return TRUE;
	}
	if (G_VALUE_HOLDS_STRING (value) && g_value_get_string (value) != NULL)
	{
		const gchar *s = g_value_get_string (value);

		if (*s == '#')
		{
			guint64 v = g_ascii_strtoull (s + 1, NULL, 16);
			gsize   len = strlen (s + 1);

			if (len == 6)
				v = (v << 8) | 0xff;
			else if (len != 8)
				return FALSE;
			rgba[0] = (gfloat) ((v >> 24) & 0xff) / 255.0f;
			rgba[1] = (gfloat) ((v >> 16) & 0xff) / 255.0f;
			rgba[2] = (gfloat) ((v >> 8) & 0xff) / 255.0f;
			rgba[3] = (gfloat) (v & 0xff) / 255.0f;
			return TRUE;
		}
		else
		{
			g_auto(GStrv) parts = g_strsplit (s, ",", 4);
			guint         n = g_strv_length (parts), k;

			if (n < 3)
				return FALSE;
			for (k = 0; k < 4; k++)
				rgba[k] = (k < n) ? (gfloat) g_ascii_strtod (parts[k], NULL) : 1.0f;
			return TRUE;
		}
	}
	return FALSE;
}

/* A number from any numeric GValue. */
static gboolean
value_to_double (const GValue *value,
                 gdouble      *out)
{
	GValue d = G_VALUE_INIT;

	g_value_init (&d, G_TYPE_DOUBLE);
	if (!g_value_type_transformable (G_VALUE_TYPE (value), G_TYPE_DOUBLE) || !g_value_transform (value, &d))
	{
		g_value_unset (&d);
		return FALSE;
	}
	*out = g_value_get_double (&d);
	g_value_unset (&d);
	return TRUE;
}

/* A pair from a GrlVector2 or "a,b". */
static gboolean
value_to_pair (const GValue *value,
               gfloat       *pair)
{
	if (G_VALUE_HOLDS (value, GRL_TYPE_VECTOR2))
	{
		const GrlVector2 *v = g_value_get_boxed (value);

		if (v == NULL)
			return FALSE;
		pair[0] = v->x;
		pair[1] = v->y;
		return TRUE;
	}
	if (G_VALUE_HOLDS_STRING (value) && g_value_get_string (value) != NULL)
	{
		g_auto(GStrv) parts = g_strsplit (g_value_get_string (value), ",", 2);

		if (g_strv_length (parts) != 2)
			return FALSE;
		pair[0] = (gfloat) g_ascii_strtod (parts[0], NULL);
		pair[1] = (gfloat) g_ascii_strtod (parts[1], NULL);
		return TRUE;
	}
	return FALSE;
}

static const gchar *
value_string (const GValue *value)
{
	const gchar *s;

	if (!G_VALUE_HOLDS_STRING (value))
		return NULL;
	s = g_value_get_string (value);
	return (s != NULL && *s != '\0') ? s : NULL;
}

static gboolean
change_layer_field (LrgTerrainAsset *asset,
                    gpointer         data)
{
	FieldEdit       *e = data;
	LrgTerrainLayer *layer = lrg_terrain_asset_get_layer (asset, e->index);
	gdouble          d;
	gfloat           rgba[4];

	if (layer == NULL)
		return FALSE;
	if (g_strcmp0 (e->name, "name") == 0)
	{
		if (!G_VALUE_HOLDS_STRING (e->value) || value_string (e->value) == NULL)
			return FALSE;
		lrg_terrain_layer_set_name (layer, value_string (e->value));
	}
	else if (g_strcmp0 (e->name, "texture") == 0 && G_VALUE_HOLDS_STRING (e->value))
		lrg_terrain_layer_set_texture (layer, value_string (e->value));
	else if (g_strcmp0 (e->name, "material") == 0 && G_VALUE_HOLDS_STRING (e->value))
		lrg_terrain_layer_set_material (layer, value_string (e->value));
	else if (g_strcmp0 (e->name, "tiling") == 0 && value_to_double (e->value, &d) && d > 0.0)
		lrg_terrain_layer_set_tiling (layer, (gfloat) d);
	else if (g_strcmp0 (e->name, "color") == 0 && value_to_rgba (e->value, rgba))
		lrg_terrain_layer_set_color (layer, rgba[0], rgba[1], rgba[2], rgba[3]);
	else
		return FALSE;

	rebake_all (asset);
	return TRUE;
}

/* --- foliage --- */

static gboolean
change_add_foliage (LrgTerrainAsset *asset,
                    gpointer         data)
{
	FieldEdit                 *e = data;
	LrgFoliage                *foliage = lrg_terrain_asset_get_foliage (asset);
	g_autoptr(LrgFoliageLayer) fl = NULL;

	if (lrg_foliage_get_n_layers (foliage) >= LRG_FOLIAGE_MAX_LAYERS)
		return FALSE;
	fl = lrg_foliage_layer_new (e->name, e->kind);
	switch (e->kind)
	{
	case LRG_FOLIAGE_KIND_MESH:
		lrg_foliage_layer_set_mesh (fl, "builtin:conifer");
		lrg_foliage_layer_set_density (fl, 0.01f);
		break;
	case LRG_FOLIAGE_KIND_GRASS:
		lrg_foliage_layer_set_density (fl, 2.0f);
		lrg_foliage_layer_set_fade (fl, 30.0f, 50.0f);
		break;
	case LRG_FOLIAGE_KIND_BILLBOARD:
	default:
		lrg_foliage_layer_set_density (fl, 0.05f);
		break;
	}
	e->result = lrg_foliage_add_layer (foliage, fl);
	return e->result >= 0;
}

static gboolean
change_remove_foliage (LrgTerrainAsset *asset,
                       gpointer         data)
{
	FieldEdit *e = data;

	return lrg_foliage_remove_layer (lrg_terrain_asset_get_foliage (asset), e->index);
}

static gboolean
change_foliage_field (LrgTerrainAsset *asset,
                      gpointer         data)
{
	FieldEdit       *e = data;
	LrgFoliageLayer *fl = lrg_foliage_get_layer (lrg_terrain_asset_get_foliage (asset), e->index);
	gdouble          d;
	gfloat           pair[2], rgba[4];
	const gchar     *n = e->name;

	if (fl == NULL)
		return FALSE;
	if (g_strcmp0 (n, "mesh") == 0 && G_VALUE_HOLDS_STRING (e->value))
		lrg_foliage_layer_set_mesh (fl, value_string (e->value));
	else if (g_strcmp0 (n, "lod-mesh") == 0 && G_VALUE_HOLDS_STRING (e->value))
		lrg_foliage_layer_set_lod_mesh (fl, value_string (e->value), lrg_foliage_layer_get_lod_distance (fl));
	else if (g_strcmp0 (n, "lod-distance") == 0 && value_to_double (e->value, &d))
		lrg_foliage_layer_set_lod_mesh (fl, lrg_foliage_layer_get_lod_mesh (fl), (gfloat) MAX (d, 0.0));
	else if (g_strcmp0 (n, "texture") == 0 && G_VALUE_HOLDS_STRING (e->value))
		lrg_foliage_layer_set_texture (fl, value_string (e->value));
	else if (g_strcmp0 (n, "color") == 0 && value_to_rgba (e->value, rgba))
		lrg_foliage_layer_set_color (fl, rgba[0], rgba[1], rgba[2], rgba[3]);
	else if (g_strcmp0 (n, "density") == 0 && value_to_double (e->value, &d))
		lrg_foliage_layer_set_density (fl, (gfloat) MAX (d, 0.0));
	else if (g_strcmp0 (n, "scale") == 0 && value_to_pair (e->value, pair))
		lrg_foliage_layer_set_scale_range (fl, pair[0], pair[1]);
	else if (g_strcmp0 (n, "slope") == 0 && value_to_pair (e->value, pair))
		lrg_foliage_layer_set_slope_range (fl, pair[0], pair[1]);
	else if (g_strcmp0 (n, "height") == 0 && value_to_pair (e->value, pair))
		lrg_foliage_layer_set_height_range (fl, pair[0], pair[1]);
	else if (g_strcmp0 (n, "align") == 0 && value_to_double (e->value, &d))
		lrg_foliage_layer_set_align (fl, (gfloat) CLAMP (d, 0.0, 1.0));
	else if (g_strcmp0 (n, "size") == 0 && value_to_pair (e->value, pair))
		lrg_foliage_layer_set_size (fl, pair[0], pair[1]);
	else if (g_strcmp0 (n, "fade") == 0 && value_to_pair (e->value, pair))
		lrg_foliage_layer_set_fade (fl, pair[0], pair[1]);
	else if (g_strcmp0 (n, "seed") == 0 && value_to_double (e->value, &d))
		lrg_foliage_layer_set_seed (fl, (guint32) MAX (d, 0.0));
	else
		return FALSE;
	foliage_dirty_all (asset);
	return TRUE;
}

/* ==========================================================================
 * Public: selection, tool, layers, foliage
 * ========================================================================== */

LrgNode *
lrg_editor_terrain_panel_get_node (LrgEditorTerrainPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), NULL);
	return self->node;
}

LrgTerrainAsset *
lrg_editor_terrain_panel_get_asset (LrgEditorTerrainPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), NULL);
	return asset_of (self, self->node, NULL);
}

LrgTerrainBrush *
lrg_editor_terrain_panel_get_brush (LrgEditorTerrainPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), NULL);
	return self->brush;
}

LrgPropertyGrid *
lrg_editor_terrain_panel_get_brush_grid (LrgEditorTerrainPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), NULL);
	return self->brush_grid;
}

LrgPropertyGrid *
lrg_editor_terrain_panel_get_layer_grid (LrgEditorTerrainPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), NULL);
	return self->layer_grid;
}

LrgPropertyGrid *
lrg_editor_terrain_panel_get_foliage_grid (LrgEditorTerrainPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), NULL);
	return self->foliage_grid;
}

LrgEditorTerrainTool
lrg_editor_terrain_panel_get_tool (LrgEditorTerrainPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), LRG_EDITOR_TERRAIN_TOOL_SELECT);
	return self->tool;
}

void
lrg_editor_terrain_panel_set_tool (LrgEditorTerrainPanel *self,
                                   LrgEditorTerrainTool   tool)
{
	g_return_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self));
	g_return_if_fail ((guint) tool < N_TOOLS);

	if (self->stroking)
		lrg_editor_terrain_panel_end_stroke (self, TRUE);
	if (self->tool == tool)
		return;
	self->tool = tool;
	sync_widgets (self);
	queue_render (self);
}

gboolean
lrg_editor_terrain_panel_get_brush_active (LrgEditorTerrainPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);

	if (self->tool == LRG_EDITOR_TERRAIN_TOOL_SELECT || self->node == NULL || primary (self) != self->node)
		return FALSE;
	if (self->play != NULL && lrg_editor_play_session_is_playing (self->play))
		return FALSE;
	if (lrg_node_get_locked (self->node))
		return FALSE;
	return asset_of (self, self->node, NULL) != NULL;
}

gint
lrg_editor_terrain_panel_get_selected_layer (LrgEditorTerrainPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), -1);
	return self->sel_layer;
}

void
lrg_editor_terrain_panel_set_selected_layer (LrgEditorTerrainPanel *self,
                                             gint                   index)
{
	LrgTerrainAsset *asset;

	g_return_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self));
	asset = asset_of (self, self->node, NULL);
	if (asset == NULL || index < 0 || (guint) index >= lrg_terrain_asset_get_n_layers (asset))
		index = -1;
	self->sel_layer = index;
	sync_widgets (self);
}

gint
lrg_editor_terrain_panel_get_selected_foliage (LrgEditorTerrainPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), -1);
	return self->sel_foliage;
}

void
lrg_editor_terrain_panel_set_selected_foliage (LrgEditorTerrainPanel *self,
                                               gint                   index)
{
	LrgTerrainAsset *asset;

	g_return_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self));
	asset = asset_of (self, self->node, NULL);
	if (asset == NULL || index < 0 || (guint) index >= lrg_foliage_get_n_layers (lrg_terrain_asset_get_foliage (asset)))
		index = -1;
	self->sel_foliage = index;
	sync_widgets (self);
}

gint
lrg_editor_terrain_panel_add_layer (LrgEditorTerrainPanel *self,
                                    LrgNode               *node,
                                    const gchar           *name)
{
	FieldEdit e = { 0 };

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), -1);
	e.name = (name != NULL && *name != '\0') ? name : "layer";
	e.result = -1;
	if (!settings_edit (self, node, "Terrain: add layer", TRUE, change_add_layer, &e))
		return -1;
	if (resolve_node (self, node) == self->node)
		self->sel_layer = e.result;
	sync_widgets (self);
	return e.result;
}

gboolean
lrg_editor_terrain_panel_remove_layer (LrgEditorTerrainPanel *self,
                                       LrgNode               *node,
                                       guint                  index)
{
	FieldEdit e = { 0 };
	gboolean  ok;

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);
	e.index = index;
	ok = settings_edit (self, node, "Terrain: remove layer", TRUE, change_remove_layer, &e);
	sync_widgets (self);
	return ok;
}

gboolean
lrg_editor_terrain_panel_set_layer_field (LrgEditorTerrainPanel *self,
                                          LrgNode               *node,
                                          guint                  index,
                                          const gchar           *name,
                                          const GValue          *value)
{
	FieldEdit         e = { 0 };
	g_autofree gchar *label = NULL;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);
	g_return_val_if_fail (name != NULL && value != NULL, FALSE);
	e.name = name;
	e.value = value;
	e.index = index;
	label = g_strdup_printf ("Terrain: layer %u %s", index, name);
	ok = settings_edit (self, node, label, FALSE, change_layer_field, &e);
	if (ok && !self->in_gesture)
		sync_widgets (self);
	return ok;
}

gint
lrg_editor_terrain_panel_add_foliage (LrgEditorTerrainPanel *self,
                                      LrgNode               *node,
                                      const gchar           *name,
                                      LrgFoliageKind         kind)
{
	FieldEdit e = { 0 };

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), -1);
	e.name = (name != NULL && *name != '\0') ? name : "foliage";
	e.kind = kind;
	e.result = -1;
	if (!settings_edit (self, node, "Terrain: add foliage layer", TRUE, change_add_foliage, &e))
		return -1;
	if (resolve_node (self, node) == self->node)
		self->sel_foliage = e.result;
	sync_widgets (self);
	return e.result;
}

gboolean
lrg_editor_terrain_panel_remove_foliage (LrgEditorTerrainPanel *self,
                                         LrgNode               *node,
                                         guint                  index)
{
	FieldEdit e = { 0 };
	gboolean  ok;

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);
	e.index = index;
	ok = settings_edit (self, node, "Terrain: remove foliage layer", TRUE, change_remove_foliage, &e);
	sync_widgets (self);
	return ok;
}

gboolean
lrg_editor_terrain_panel_set_foliage_field (LrgEditorTerrainPanel *self,
                                            LrgNode               *node,
                                            guint                  index,
                                            const gchar           *name,
                                            const GValue          *value)
{
	FieldEdit         e = { 0 };
	g_autofree gchar *label = NULL;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);
	g_return_val_if_fail (name != NULL && value != NULL, FALSE);
	e.name = name;
	e.value = value;
	e.index = index;
	label = g_strdup_printf ("Terrain: foliage %u %s", index, name);
	ok = settings_edit (self, node, label, FALSE, change_foliage_field, &e);
	if (ok && !self->in_gesture)
		sync_widgets (self);
	return ok;
}

guint
lrg_editor_terrain_panel_rebake (LrgEditorTerrainPanel *self,
                                 LrgNode               *node)
{
	LrgTerrainAsset *asset;
	LrgComponent    *comp;
	guint            n;

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), 0);
	node = resolve_node (self, node);
	asset = asset_of (self, node, NULL);
	if (asset == NULL)
		return 0;
	foliage_dirty_all (asset);
	n = lrg_terrain_asset_scatter_foliage (asset);
	comp = lrg_editor_terrain_node_component (self->editor, resolver_of (self), node);
	if (comp != NULL && LRG_IS_TERRAIN_COMPONENT (comp) && IsWindowReady ())
		lrg_terrain_component_release_gpu (LRG_TERRAIN_COMPONENT (comp));
	queue_render (self);
	if (self->app != NULL)
		editor_app_set_status (self->app, "Terrain rebaked: %u foliage instances", n);
	sync_widgets (self);
	return n;
}

gboolean
lrg_editor_terrain_panel_save (LrgEditorTerrainPanel  *self,
                               LrgNode                *node,
                               GError                **error)
{
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);
	node = resolve_node (self, node);
	if (asset_of (self, node, &path) == NULL)
	{
		g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "No terrain asset to save");
		return FALSE;
	}
	if (!lrg_editor_terrain_store_save (self->editor, path, error))
		return FALSE;
	if (self->app != NULL)
	{
		g_autofree gchar *rel = editor_app_relative_path (self->app, path);

		editor_app_set_status (self->app, "Saved %s", rel);
	}
	sync_widgets (self);
	return TRUE;
}

gchar *
lrg_editor_terrain_panel_describe (LrgEditorTerrainPanel *self)
{
	g_autofree gchar *path = NULL;
	LrgTerrainAsset  *asset;
	guint             cx, cz, sx, sz, i, inst = 0;
	gfloat            w, d;
	LrgFoliage       *foliage;

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), NULL);
	asset = asset_of (self, self->node, &path);
	if (asset == NULL)
		return g_strdup ("");
	lrg_terrain_asset_get_chunks (asset, &cx, &cz);
	lrg_terrain_asset_get_samples (asset, &sx, &sz);
	lrg_terrain_asset_get_size (asset, &w, &d);
	foliage = lrg_terrain_asset_get_foliage (asset);
	for (i = 0; i < lrg_foliage_get_n_layers (foliage); i++)
		inst += lrg_foliage_get_instance_count (foliage, i);
	return g_strdup_printf ("%u x %u chunks of %u cells (%.2g m) - %u x %u samples - %.0f x %.0f m - %u LODs - "
	                        "%u layers - %u foliage (%u inst.) - %u holes%s",
	                        cx, cz, lrg_terrain_asset_get_resolution (asset),
	                        (gdouble) lrg_terrain_asset_get_cell_size (asset), sx, sz, (gdouble) w, (gdouble) d,
	                        lrg_terrain_asset_get_lod_levels (asset), lrg_terrain_asset_get_n_layers (asset),
	                        lrg_foliage_get_n_layers (foliage), inst, lrg_terrain_asset_get_hole_count (asset),
	                        lrg_editor_terrain_store_is_dirty (self->editor, path) ? " - unsaved" : "");
}

/* ==========================================================================
 * Strokes
 * ========================================================================== */

gboolean
lrg_editor_terrain_panel_is_stroking (LrgEditorTerrainPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);
	return self->stroking;
}

gboolean
lrg_editor_terrain_panel_begin_stroke (LrgEditorTerrainPanel       *self,
                                       LrgNode                     *node,
                                       gfloat                       local_x,
                                       gfloat                       local_z,
                                       LrgEditorTerrainStrokeFlags  flags)
{
	g_autofree gchar  *path = NULL;
	LrgTerrainAsset   *asset;
	LrgTerrainBrushOp  op;
	guint              layer = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);

	if (self->stroking)
		lrg_editor_terrain_panel_end_stroke (self, TRUE);
	if (self->tool == LRG_EDITOR_TERRAIN_TOOL_SELECT)
		return FALSE;
	node = resolve_node (self, node);
	asset = asset_of (self, node, &path);
	if (asset == NULL)
		return FALSE;

	/* The operation and the layer it works on */
	op = stroke_op (self->tool, flags);
	if (op == LRG_TERRAIN_BRUSH_PAINT)
	{
		if (self->sel_layer < 0 || (guint) self->sel_layer >= lrg_terrain_asset_get_n_layers (asset))
		{
			set_status (self, "Terrain: pick a surface layer to paint");
			return FALSE;
		}
		layer = (guint) self->sel_layer;
	}
	else if (op_is_foliage (op))
	{
		if (self->sel_foliage < 0 ||
		    (guint) self->sel_foliage >= lrg_foliage_get_n_layers (lrg_terrain_asset_get_foliage (asset)))
		{
			set_status (self, "Terrain: add or pick a foliage layer to paint");
			return FALSE;
		}
		layer = (guint) self->sel_foliage;
	}

	self->stroke_saved_op = lrg_terrain_brush_get_op (self->brush);
	self->stroke_saved_layer = lrg_terrain_brush_get_layer (self->brush);
	lrg_terrain_brush_set_op (self->brush, op);
	lrg_terrain_brush_set_layer (self->brush, layer);
	lrg_terrain_brush_begin_stroke (self->brush, asset, local_x, local_z);

	self->stroking = TRUE;
	self->stroke_op = op;
	self->stroke_asset = g_object_ref (asset);
	self->stroke_path = g_steal_pointer (&path);
	self->stroke_x = local_x;
	self->stroke_z = local_z;
	self->stroke_dabs = 0;
	lrg_editor_terrain_panel_stroke_to (self, local_x, local_z, PRESS_AMOUNT);
	return TRUE;
}

void
lrg_editor_terrain_panel_stroke_to (LrgEditorTerrainPanel *self,
                                    gfloat                 local_x,
                                    gfloat                 local_z,
                                    gfloat                 amount)
{
	g_return_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self));

	if (!self->stroking || self->stroke_asset == NULL)
		return;
	self->stroke_x = local_x;
	self->stroke_z = local_z;
	lrg_terrain_edit_free (lrg_terrain_brush_apply (self->brush, self->stroke_asset, local_x, local_z, amount));
	self->stroke_dabs++;
	queue_render (self);
}

gboolean
lrg_editor_terrain_panel_end_stroke (LrgEditorTerrainPanel *self,
                                     gboolean               commit)
{
	LrgTerrainEdit *edit;
	gboolean        recorded = FALSE;

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);

	if (!self->stroking)
		return FALSE;
	self->stroking = FALSE;
	edit = lrg_terrain_brush_end_stroke (self->brush);
	lrg_terrain_brush_set_op (self->brush, self->stroke_saved_op);
	lrg_terrain_brush_set_layer (self->brush, self->stroke_saved_layer);

	if (edit != NULL && !lrg_terrain_edit_is_empty (edit))
	{
		if (commit)
		{
			StrokeUndo       *u = g_new0 (StrokeUndo, 1);
			g_autofree gchar *label = g_strdup_printf ("Terrain: %s stroke", op_label (self->stroke_op));

			g_set_weak_pointer (&u->editor, self->editor);
			u->asset = g_object_ref (self->stroke_asset);
			u->path = g_strdup (self->stroke_path);
			u->edit = edit;
			edit = NULL;
			if (lrg_terrain_edit_get_kind (u->edit) == LRG_TERRAIN_EDIT_HEIGHTS)
				foliage_dirty_all (u->asset);
			lrg_editor_terrain_store_set_dirty (self->editor, u->path, TRUE);
			editor_terrain_push_undo (self->editor, label, stroke_undo_restore, u, stroke_undo_free);
			recorded = TRUE;
		}
		else
			lrg_terrain_edit_apply (edit, self->stroke_asset, FALSE);
	}
	g_clear_pointer (&edit, lrg_terrain_edit_free);
	g_clear_object (&self->stroke_asset);
	g_clear_pointer (&self->stroke_path, g_free);
	queue_render (self);
	sync_widgets (self);
	return recorded;
}

gboolean
lrg_editor_terrain_panel_stroke (LrgEditorTerrainPanel       *self,
                                 LrgNode                     *node,
                                 LrgEditorTerrainTool         tool,
                                 const gfloat                *points,
                                 guint                        n_points,
                                 gfloat                       amount,
                                 LrgEditorTerrainStrokeFlags  flags)
{
	LrgEditorTerrainTool saved;
	gboolean             ok;
	guint                i;

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);
	g_return_val_if_fail (points != NULL || n_points == 0, FALSE);

	if (n_points < 2 || tool == LRG_EDITOR_TERRAIN_TOOL_SELECT)
		return FALSE;
	saved = self->tool;
	self->tool = tool;
	ok = lrg_editor_terrain_panel_begin_stroke (self, node, points[0], points[1], flags);
	self->tool = saved;
	if (!ok)
		return FALSE;

	/* begin_stroke dabbed once with the press amount: the first point gets
	 * the rest of its amount, then one dab per point */
	if (amount > PRESS_AMOUNT)
		lrg_editor_terrain_panel_stroke_to (self, points[0], points[1], amount - PRESS_AMOUNT);
	for (i = 2; i + 1 < n_points; i += 2)
		lrg_editor_terrain_panel_stroke_to (self, points[i], points[i + 1], amount);
	return lrg_editor_terrain_panel_end_stroke (self, TRUE);
}

/* ==========================================================================
 * EditorTerrainSource: the brush, layer and foliage grids
 * ========================================================================== */

static GObject *
source_layer (EditorTerrainSource *src,
              LrgTerrainAsset    **out_asset)
{
	LrgTerrainAsset *asset = (src->panel != NULL) ? asset_of (src->panel, src->panel->node, NULL) : NULL;

	*out_asset = asset;
	if (asset == NULL)
		return NULL;
	if (src->part == PART_LAYER)
		return (src->panel->sel_layer >= 0)
		       ? G_OBJECT (lrg_terrain_asset_get_layer (asset, (guint) src->panel->sel_layer)) : NULL;
	return (src->panel->sel_foliage >= 0)
	       ? G_OBJECT (lrg_foliage_get_layer (lrg_terrain_asset_get_foliage (asset), (guint) src->panel->sel_foliage))
	       : NULL;
}

/* Virtual pspecs for the rows (created once). */
static GParamSpec *
vpspec (const gchar *name,
        GType        type)
{
	static GHashTable *specs = NULL;
	g_autofree gchar  *key = g_strdup_printf ("%s/%s", name, g_type_name (type));
	GParamSpec        *pspec;

	if (specs == NULL)
		specs = g_hash_table_new (g_str_hash, g_str_equal);
	pspec = g_hash_table_lookup (specs, key);
	if (pspec != NULL)
		return pspec;
	if (type == G_TYPE_STRING)
		pspec = g_param_spec_string (g_intern_string (name), NULL, NULL, NULL,
		                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	else if (type == G_TYPE_DOUBLE)
		pspec = g_param_spec_double (g_intern_string (name), NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
		                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	else if (type == G_TYPE_UINT)
		pspec = g_param_spec_uint (g_intern_string (name), NULL, NULL, 0, G_MAXUINT32, 0,
		                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	else
		pspec = g_param_spec_boxed (g_intern_string (name), NULL, NULL, type,
		                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_param_spec_ref_sink (pspec);
	g_hash_table_insert (specs, (gpointer) g_intern_string (key), pspec);
	return pspec;
}

static void
add_row (GPtrArray   *infos,
         GParamSpec  *pspec,
         const gchar *label,
         const gchar *hint,
         const gchar *tooltip,
         gboolean     read_only)
{
	LrgPropertyInfo *info;

	if (pspec == NULL)
		return;
	info = lrg_property_info_new (pspec);
	lrg_property_info_set_label (info, label);
	if (hint != NULL)
		lrg_property_info_set_hint (info, hint);
	if (tooltip != NULL)
		lrg_property_info_set_tooltip (info, tooltip);
	if (read_only)
		lrg_property_info_set_read_only (info, TRUE);
	g_ptr_array_add (infos, info);
}

static GParamSpec *
brush_pspec (const gchar *name)
{
	return g_object_class_find_property (G_OBJECT_CLASS (g_type_class_peek (LRG_TYPE_TERRAIN_BRUSH)), name);
}

static GPtrArray *
editor_terrain_source_list_properties (LrgPropertySource *source)
{
	EditorTerrainSource   *src = EDITOR_TERRAIN_SOURCE (source);
	LrgEditorTerrainPanel *panel = src->panel;
	GPtrArray             *infos = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	LrgTerrainAsset       *asset = NULL;

	if (panel == NULL)
		return infos;

	switch (src->part)
	{
	case PART_BRUSH:
		{
			LrgTerrainBrushOp op = lrg_editor_terrain_tool_get_op (panel->tool);

			g_type_class_ref (LRG_TYPE_TERRAIN_BRUSH);
			add_row (infos, brush_pspec ("radius"), "Radius", "range:0.1,256,0.1",
			         "World units ([ and ] in the viewport)", FALSE);
			add_row (infos, brush_pspec ("strength"), "Strength", "range:0,64,0.05",
			         "Height per second (sculpt) or blend rate per second (paint)", FALSE);
			add_row (infos, brush_pspec ("falloff"), "Falloff", NULL, "How the effect fades to the rim", FALSE);
			if (lrg_terrain_brush_get_falloff (panel->brush) == LRG_TERRAIN_BRUSH_FALLOFF_CURVE)
				add_row (infos, brush_pspec ("falloff-curve"), "Curve", "curve",
				         "Weight from the centre (0) to the rim (1)", FALSE);
			if (panel->tool == LRG_EDITOR_TERRAIN_TOOL_FLATTEN || op == LRG_TERRAIN_BRUSH_FLATTEN)
			{
				add_row (infos, brush_pspec ("target-height"), "Target height", "range:-1000,1000,0.1",
				         "The height flatten pulls towards", FALSE);
				add_row (infos, brush_pspec ("sample-target"), "Sample target", NULL,
				         "Take the target from the terrain under the stroke's start", FALSE);
			}
			if (panel->tool == LRG_EDITOR_TERRAIN_TOOL_NOISE)
			{
				add_row (infos, brush_pspec ("noise-scale"), "Noise scale", "range:0.5,512,0.5",
				         "Feature size of the noise", FALSE);
				add_row (infos, brush_pspec ("seed"), "Seed", NULL, NULL, FALSE);
			}
		}
		break;

	case PART_LAYER:
		if (source_layer (src, &asset) == NULL)
			break;
		add_row (infos, vpspec ("name", G_TYPE_STRING), "Name", NULL, NULL, FALSE);
		add_row (infos, vpspec ("texture", G_TYPE_STRING), "Texture", "asset:texture",
		         "Albedo texture, relative to the .rterrain (empty: the colour alone)", FALSE);
		add_row (infos, vpspec ("material", G_TYPE_STRING), "Material", "asset:material",
		         "An .rmat kept for tools and future shading", FALSE);
		add_row (infos, vpspec ("tiling", G_TYPE_DOUBLE), "Tiling", "range:0.05,512,0.05",
		         "World units per texture repeat", FALSE);
		add_row (infos, vpspec ("color", GRL_TYPE_COLOR), "Colour", "color", "Multiplies the texture", FALSE);
		break;

	case PART_FOLIAGE:
	default:
		{
			GObject *target = source_layer (src, &asset);
			LrgFoliageKind kind;

			if (target == NULL)
				break;
			kind = lrg_foliage_layer_get_kind (LRG_FOLIAGE_LAYER (target));
			add_row (infos, vpspec ("kind", G_TYPE_STRING), "Kind", NULL, NULL, TRUE);
			if (kind == LRG_FOLIAGE_KIND_MESH)
			{
				add_row (infos, vpspec ("mesh", G_TYPE_STRING), "Mesh", "asset:model",
				         "A model, or builtin:conifer / builtin:broadleaf / builtin:rock", FALSE);
				add_row (infos, vpspec ("lod-mesh", G_TYPE_STRING), "LOD mesh", "asset:model",
				         "A cheaper model past the LOD distance", FALSE);
				add_row (infos, vpspec ("lod-distance", G_TYPE_DOUBLE), "LOD distance", "range:0,2000,1", NULL,
				         FALSE);
			}
			else
				add_row (infos, vpspec ("texture", G_TYPE_STRING), "Texture", "asset:texture", NULL, FALSE);
			add_row (infos, vpspec ("color", GRL_TYPE_COLOR), "Colour", "color", NULL, FALSE);
			add_row (infos, vpspec ("density", G_TYPE_DOUBLE), "Density", "range:0,20,0.001",
			         "Instances per square unit at full density", FALSE);
			add_row (infos, vpspec ("scale", GRL_TYPE_VECTOR2), "Scale", "vector2", "Random scale, min and max",
			         FALSE);
			add_row (infos, vpspec ("slope", GRL_TYPE_VECTOR2), "Slope", "vector2",
			         "Allowed ground slope in degrees, min and max", FALSE);
			add_row (infos, vpspec ("height", GRL_TYPE_VECTOR2), "Height", "vector2",
			         "Allowed terrain height, min and max", FALSE);
			add_row (infos, vpspec ("align", G_TYPE_DOUBLE), "Align", "range:0,1,0.01",
			         "0 upright, 1 along the ground normal", FALSE);
			if (kind != LRG_FOLIAGE_KIND_MESH)
				add_row (infos, vpspec ("size", GRL_TYPE_VECTOR2), "Size", "vector2", "Card width and height",
				         FALSE);
			add_row (infos, vpspec ("fade", GRL_TYPE_VECTOR2), "Fade", "vector2",
			         "Fade out between these distances (0, 0: never)", FALSE);
			add_row (infos, vpspec ("seed", G_TYPE_UINT), "Seed", NULL, NULL, FALSE);
		}
		break;
	}
	return infos;
}

static void
set_color_value (GValue       *value,
                 const gfloat *rgba)
{
	GrlColor c;

	c.r = (guint8) CLAMP (rgba[0] * 255.0f + 0.5f, 0.0f, 255.0f);
	c.g = (guint8) CLAMP (rgba[1] * 255.0f + 0.5f, 0.0f, 255.0f);
	c.b = (guint8) CLAMP (rgba[2] * 255.0f + 0.5f, 0.0f, 255.0f);
	c.a = (guint8) CLAMP (rgba[3] * 255.0f + 0.5f, 0.0f, 255.0f);
	g_value_init (value, GRL_TYPE_COLOR);
	g_value_set_boxed (value, &c);
}

static void
set_pair_value (GValue *value,
                gfloat  a,
                gfloat  b)
{
	GrlVector2 v;

	v.x = a;
	v.y = b;
	g_value_init (value, GRL_TYPE_VECTOR2);
	g_value_set_boxed (value, &v);
}

static gboolean
editor_terrain_source_get_value (LrgPropertySource *source,
                                 const gchar       *name,
                                 GValue            *value)
{
	EditorTerrainSource *src = EDITOR_TERRAIN_SOURCE (source);
	LrgTerrainAsset     *asset = NULL;
	GObject             *target;
	gfloat               v4[4], a, b;

	if (src->panel == NULL)
		return FALSE;
	if (src->part == PART_BRUSH)
	{
		GParamSpec *pspec = brush_pspec (name);

		if (pspec == NULL)
			return FALSE;
		g_value_init (value, pspec->value_type);
		g_object_get_property (G_OBJECT (src->panel->brush), name, value);
		return TRUE;
	}
	target = source_layer (src, &asset);
	if (target == NULL)
		return FALSE;

	if (src->part == PART_LAYER)
	{
		LrgTerrainLayer *layer = LRG_TERRAIN_LAYER (target);

		if (g_strcmp0 (name, "name") == 0 || g_strcmp0 (name, "texture") == 0 || g_strcmp0 (name, "material") == 0)
		{
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, g_strcmp0 (name, "name") == 0 ? lrg_terrain_layer_get_name (layer)
			                           : g_strcmp0 (name, "texture") == 0 ? lrg_terrain_layer_get_texture (layer)
			                                                              : lrg_terrain_layer_get_material (layer));
		}
		else if (g_strcmp0 (name, "tiling") == 0)
		{
			g_value_init (value, G_TYPE_DOUBLE);
			g_value_set_double (value, (gdouble) lrg_terrain_layer_get_tiling (layer));
		}
		else if (g_strcmp0 (name, "color") == 0)
		{
			lrg_terrain_layer_get_color (layer, v4);
			set_color_value (value, v4);
		}
		else
			return FALSE;
		return TRUE;
	}

	{
		LrgFoliageLayer *fl = LRG_FOLIAGE_LAYER (target);

		if (g_strcmp0 (name, "kind") == 0)
		{
			GEnumClass *klass = g_type_class_ref (LRG_TYPE_FOLIAGE_KIND);
			GEnumValue *ev = g_enum_get_value (klass, (gint) lrg_foliage_layer_get_kind (fl));

			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, ev != NULL ? ev->value_nick : "");
			g_type_class_unref (klass);
		}
		else if (g_strcmp0 (name, "mesh") == 0 || g_strcmp0 (name, "lod-mesh") == 0 ||
		         g_strcmp0 (name, "texture") == 0)
		{
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, g_strcmp0 (name, "mesh") == 0 ? lrg_foliage_layer_get_mesh (fl)
			                           : g_strcmp0 (name, "lod-mesh") == 0 ? lrg_foliage_layer_get_lod_mesh (fl)
			                                                               : lrg_foliage_layer_get_texture (fl));
		}
		else if (g_strcmp0 (name, "color") == 0)
		{
			lrg_foliage_layer_get_color (fl, v4);
			set_color_value (value, v4);
		}
		else if (g_strcmp0 (name, "density") == 0 || g_strcmp0 (name, "align") == 0 ||
		         g_strcmp0 (name, "lod-distance") == 0)
		{
			g_value_init (value, G_TYPE_DOUBLE);
			g_value_set_double (value, (gdouble) (g_strcmp0 (name, "density") == 0 ? lrg_foliage_layer_get_density (fl)
			                                      : g_strcmp0 (name, "align") == 0 ? lrg_foliage_layer_get_align (fl)
			                                                                       : lrg_foliage_layer_get_lod_distance (fl)));
		}
		else if (g_strcmp0 (name, "seed") == 0)
		{
			g_value_init (value, G_TYPE_UINT);
			g_value_set_uint (value, lrg_foliage_layer_get_seed (fl));
		}
		else
		{
			if (g_strcmp0 (name, "scale") == 0)
				lrg_foliage_layer_get_scale_range (fl, &a, &b);
			else if (g_strcmp0 (name, "slope") == 0)
				lrg_foliage_layer_get_slope_range (fl, &a, &b);
			else if (g_strcmp0 (name, "height") == 0)
				lrg_foliage_layer_get_height_range (fl, &a, &b);
			else if (g_strcmp0 (name, "size") == 0)
				lrg_foliage_layer_get_size (fl, &a, &b);
			else if (g_strcmp0 (name, "fade") == 0)
				lrg_foliage_layer_get_fade (fl, &a, &b);
			else
				return FALSE;
			set_pair_value (value, a, b);
		}
	}
	return TRUE;
}

static gboolean
editor_terrain_source_set_value (LrgPropertySource *source,
                                 const gchar       *name,
                                 const GValue      *value)
{
	EditorTerrainSource   *src = EDITOR_TERRAIN_SOURCE (source);
	LrgEditorTerrainPanel *panel = src->panel;
	gboolean               ok;

	if (panel == NULL)
		return FALSE;
	switch (src->part)
	{
	case PART_BRUSH:
		if (brush_pspec (name) == NULL)
			return FALSE;
		g_object_set_property (G_OBJECT (panel->brush), name, value);
		if (g_strcmp0 (name, "falloff") == 0)
		{
			g_clear_pointer (&panel->signature, g_free);
			sync_widgets (panel);
		}
		queue_render (panel);
		ok = TRUE;
		break;
	case PART_LAYER:
		ok = panel->sel_layer >= 0 &&
		     lrg_editor_terrain_panel_set_layer_field (panel, NULL, (guint) panel->sel_layer, name, value);
		break;
	case PART_FOLIAGE:
	default:
		ok = panel->sel_foliage >= 0 &&
		     lrg_editor_terrain_panel_set_foliage_field (panel, NULL, (guint) panel->sel_foliage, name, value);
		break;
	}
	if (ok)
		lrg_property_source_emit_changed (source, name);
	return ok;
}

static void
editor_terrain_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = editor_terrain_source_list_properties;
	iface->get_value = editor_terrain_source_get_value;
	iface->set_value = editor_terrain_source_set_value;
}

static void
editor_terrain_source_finalize (GObject *object)
{
	EditorTerrainSource *self = EDITOR_TERRAIN_SOURCE (object);

	g_clear_weak_pointer (&self->panel);
	G_OBJECT_CLASS (editor_terrain_source_parent_class)->finalize (object);
}

static void
editor_terrain_source_class_init (EditorTerrainSourceClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = editor_terrain_source_finalize;
}

static void
editor_terrain_source_init (EditorTerrainSource *self)
{
}

static EditorTerrainSource *
editor_terrain_source_new (LrgEditorTerrainPanel *panel,
                           Part                   part)
{
	EditorTerrainSource *src = g_object_new (EDITOR_TYPE_TERRAIN_SOURCE, NULL);

	g_set_weak_pointer (&src->panel, panel);
	src->part = part;
	return src;
}

static void
on_edit_begin (LrgPropertyGrid *grid,
               const gchar     *name,
               guint            gesture_id,
               gpointer         user_data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (user_data);

	self->in_gesture = TRUE;
	self->gesture_undo = NULL;
}

static void
on_edit_end (LrgPropertyGrid *grid,
             const gchar     *name,
             guint            gesture_id,
             gboolean         commit_,
             gpointer         user_data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (user_data);

	self->in_gesture = FALSE;
	self->gesture_undo = NULL;
	sync_widgets (self);
}

/* ==========================================================================
 * The tool strip
 * ========================================================================== */

/* Tool cells per row for a width. */
static guint
tool_columns (gfloat width)
{
	guint cols = (guint) MAX (width / TOOL_CELL_W, 1.0f);

	return MIN (cols, (guint) N_TOOLS);
}

static void
tools_measure (LrgCustomWidget     *widget,
               gfloat               avail_width,
               gfloat               avail_height,
               LrgCustomWidgetSize *size,
               gpointer             user_data)
{
	guint cols = tool_columns (avail_width > 0.0f ? avail_width : 6.0f * TOOL_CELL_W);
	guint rows = ((guint) N_TOOLS + cols - 1) / cols;

	size->width = TOOL_CELL_W * 4.0f;
	size->height = TOOL_CELL_H * (gfloat) rows + 2.0f;
}

/* A small vector glyph per tool, in a box at (x, y) of side s. */
static void
paint_tool_glyph (LrgPaintList         *list,
                  LrgEditorTerrainTool  tool,
                  gfloat                x,
                  gfloat                y,
                  gfloat                s,
                  const GrlColor       *color,
                  const GrlColor       *accent)
{
	gfloat cx = x + s * 0.5f, cy = y + s * 0.5f;
	gfloat base = y + s * 0.82f;
	guint  i;

	switch (tool)
	{
	case LRG_EDITOR_TERRAIN_TOOL_SELECT:
		lrg_paint_list_triangle (list, x + s * 0.3f, y + s * 0.15f, x + s * 0.3f, y + s * 0.85f, x + s * 0.75f,
		                         y + s * 0.6f, color);
		break;
	case LRG_EDITOR_TERRAIN_TOOL_RAISE:
	case LRG_EDITOR_TERRAIN_TOOL_LOWER:
		/* a hill (or a pit) with an arrow */
		lrg_paint_list_line (list, x, base, x + s, base, 1.5f, color);
		for (i = 0; i < 8; i++)
		{
			gfloat t0 = (gfloat) i / 8.0f, t1 = (gfloat) (i + 1) / 8.0f;
			gfloat h0 = sinf (t0 * (gfloat) G_PI) * s * 0.35f, h1 = sinf (t1 * (gfloat) G_PI) * s * 0.35f;
			gfloat sign = (tool == LRG_EDITOR_TERRAIN_TOOL_RAISE) ? -1.0f : 1.0f;

			lrg_paint_list_line (list, x + t0 * s, base + sign * h0 - (sign > 0 ? s * 0.38f : 0.0f),
			                     x + t1 * s, base + sign * h1 - (sign > 0 ? s * 0.38f : 0.0f), 2.0f, accent);
		}
		if (tool == LRG_EDITOR_TERRAIN_TOOL_RAISE)
			lrg_paint_list_triangle (list, cx, y, cx - s * 0.14f, y + s * 0.22f, cx + s * 0.14f, y + s * 0.22f, color);
		else
			lrg_paint_list_triangle (list, cx, y + s * 0.62f, cx - s * 0.14f, y + s * 0.40f, cx + s * 0.14f,
			                         y + s * 0.40f, color);
		break;
	case LRG_EDITOR_TERRAIN_TOOL_FLATTEN:
		lrg_paint_list_line (list, x, base, x + s, base, 1.5f, color);
		lrg_paint_list_rect (list, x + s * 0.1f, y + s * 0.45f, s * 0.8f, s * 0.12f, 1.0f, accent);
		lrg_paint_list_line (list, cx, y + s * 0.1f, cx, y + s * 0.4f, 1.5f, color);
		break;
	case LRG_EDITOR_TERRAIN_TOOL_SMOOTH:
		for (i = 0; i < 12; i++)
		{
			gfloat t0 = (gfloat) i / 12.0f, t1 = (gfloat) (i + 1) / 12.0f;

			lrg_paint_list_line (list, x + t0 * s, cy + sinf (t0 * 6.28f) * s * 0.18f, x + t1 * s,
			                     cy + sinf (t1 * 6.28f) * s * 0.18f, 2.0f, accent);
		}
		lrg_paint_list_line (list, x, base, x + s, base, 1.5f, color);
		break;
	case LRG_EDITOR_TERRAIN_TOOL_NOISE:
		{
			static const gfloat hs[] = { 0.3f, 0.65f, 0.2f, 0.55f, 0.4f, 0.75f, 0.35f };

			for (i = 0; i + 1 < G_N_ELEMENTS (hs); i++)
				lrg_paint_list_line (list, x + s * (gfloat) i / 6.0f, y + s * hs[i], x + s * (gfloat) (i + 1) / 6.0f,
				                     y + s * hs[i + 1], 2.0f, accent);
			lrg_paint_list_line (list, x, base, x + s, base, 1.5f, color);
		}
		break;
	case LRG_EDITOR_TERRAIN_TOOL_PAINT:
		lrg_paint_list_rect (list, x + s * 0.55f, y + s * 0.08f, s * 0.18f, s * 0.45f, 2.0f, color);
		lrg_paint_list_triangle (list, x + s * 0.5f, y + s * 0.53f, x + s * 0.78f, y + s * 0.53f, x + s * 0.64f,
		                         y + s * 0.78f, accent);
		lrg_paint_list_rect (list, x + s * 0.05f, base - s * 0.04f, s * 0.9f, s * 0.08f, 2.0f, accent);
		break;
	case LRG_EDITOR_TERRAIN_TOOL_HOLE:
	case LRG_EDITOR_TERRAIN_TOOL_FILL:
		lrg_paint_list_rect (list, x + s * 0.05f, y + s * 0.35f, s * 0.9f, s * 0.4f, 2.0f, color);
		if (tool == LRG_EDITOR_TERRAIN_TOOL_HOLE)
			lrg_paint_list_circle (list, cx, y + s * 0.55f, s * 0.15f, accent);
		else
			lrg_paint_list_line (list, cx - s * 0.18f, y + s * 0.55f, cx + s * 0.18f, y + s * 0.55f, 2.5f, accent);
		break;
	case LRG_EDITOR_TERRAIN_TOOL_FOLIAGE_PAINT:
	case LRG_EDITOR_TERRAIN_TOOL_FOLIAGE_ERASE:
		/* two little trees */
		for (i = 0; i < 2; i++)
		{
			gfloat tx = x + s * (0.3f + 0.4f * (gfloat) i);

			lrg_paint_list_triangle (list, tx, y + s * 0.1f, tx - s * 0.18f, y + s * 0.6f, tx + s * 0.18f,
			                         y + s * 0.6f, accent);
			lrg_paint_list_line (list, tx, y + s * 0.6f, tx, base, 2.0f, color);
		}
		if (tool == LRG_EDITOR_TERRAIN_TOOL_FOLIAGE_ERASE)
			lrg_paint_list_line (list, x, y + s * 0.15f, x + s, base, 2.0f, color);
		break;
	default:
		break;
	}
}

static void
tools_paint (LrgCustomWidget *widget,
             LrgPaintList    *list,
             gfloat           x,
             gfloat           y,
             gfloat           width,
             gfloat           height,
             gpointer         user_data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (user_data);
	LrgTheme              *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat                 fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                 radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	GrlFont               *font = lrg_theme_get_ui_font (theme);
	guint                  cols = tool_columns (width), i;
	gfloat                 cw = width / (gfloat) cols;

	for (i = 0; i < N_TOOLS; i++)
	{
		gfloat          cx = x + cw * (gfloat) (i % cols);
		gfloat          cy = y + TOOL_CELL_H * (gfloat) (i / cols);
		gboolean        on = self->tool == (LrgEditorTerrainTool) i;
		const GrlColor *fg = lrg_theme_get_color (theme, on ? LRG_THEME_COLOR_TEXT_ON_ACCENT : LRG_THEME_COLOR_TEXT);
		const GrlColor *accent = lrg_theme_get_color (theme, on ? LRG_THEME_COLOR_TEXT_ON_ACCENT
		                                                        : (i == LRG_EDITOR_TERRAIN_TOOL_LOWER ||
		                                                           i == LRG_EDITOR_TERRAIN_TOOL_HOLE ||
		                                                           i == LRG_EDITOR_TERRAIN_TOOL_FOLIAGE_ERASE)
		                                                          ? LRG_THEME_COLOR_DANGER
		                                                          : (i >= LRG_EDITOR_TERRAIN_TOOL_PAINT)
		                                                            ? LRG_THEME_COLOR_SUCCESS
		                                                            : LRG_THEME_COLOR_ACCENT);
		gfloat          tw = 0.0f;
		gfloat          gs = 20.0f;

		lrg_paint_list_rect (list, cx + 2.0f, cy + 2.0f, cw - 4.0f, TOOL_CELL_H - 4.0f, radius,
		                     lrg_theme_get_color (theme, on ? LRG_THEME_COLOR_ACCENT : LRG_THEME_COLOR_CONTROL));
		paint_tool_glyph (list, (LrgEditorTerrainTool) i, cx + (cw - gs) * 0.5f, cy + 6.0f, gs, fg, accent);
		lrg_paint_measure_text (font, tool_table[i].label, fs, 0.0f, &tw, NULL);
		lrg_paint_list_text (list, font, tool_table[i].label, roundf (cx + (cw - tw) * 0.5f),
		                     roundf (cy + TOOL_CELL_H - fs - 6.0f), fs, 0.0f, fg);
	}
}

/* The tool cell under a canvas point, or -1. */
static gint
tool_at (LrgEditorTerrainPanel *self,
         gfloat                 px,
         gfloat                 py)
{
	LrgWidget *w = self->tools;
	gfloat     x = lrg_widget_get_world_x (w), y = lrg_widget_get_world_y (w);
	gfloat     width = lrg_widget_get_width (w);
	guint      cols = tool_columns (width), col, row, i;
	gfloat     cw = width / (gfloat) cols;

	if (px < x || py < y || px >= x + width)
		return -1;
	col = (guint) ((px - x) / cw);
	row = (guint) ((py - y) / TOOL_CELL_H);
	i = row * cols + col;
	return (i < N_TOOLS) ? (gint) i : -1;
}

static gboolean
tools_event (LrgCustomWidget  *widget,
             const LrgUIEvent *event,
             gpointer          user_data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (user_data);
	gint                   i;

	if (lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN || lrg_ui_event_get_button (event) != 0)
		return FALSE;
	i = tool_at (self, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event));
	if (i < 0)
		return FALSE;
	lrg_editor_terrain_panel_set_tool (self, (LrgEditorTerrainTool) i);
	return TRUE;
}

/* ==========================================================================
 * Layer and foliage lists
 * ========================================================================== */

static guint
list_count (LrgEditorTerrainPanel *self,
            gboolean               foliage)
{
	LrgTerrainAsset *asset = asset_of (self, self->node, NULL);

	if (asset == NULL)
		return 0;
	return foliage ? lrg_foliage_get_n_layers (lrg_terrain_asset_get_foliage (asset))
	               : lrg_terrain_asset_get_n_layers (asset);
}

static void
list_measure (LrgCustomWidget     *widget,
              gfloat               avail_width,
              gfloat               avail_height,
              LrgCustomWidgetSize *size,
              gpointer             user_data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (user_data);
	gboolean               foliage = widget == LRG_CUSTOM_WIDGET (self->foliage_list);

	size->width = 160.0f;
	size->height = (gfloat) MAX (list_count (self, foliage), 1u) * LIST_ROW_H + 2.0f;
}

static const gchar *
basename_or (const gchar *path,
             const gchar *fallback,
             gchar      **owned)
{
	if (path == NULL || *path == '\0')
		return fallback;
	*owned = g_path_get_basename (path);
	return *owned;
}

static void
list_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (user_data);
	LrgTheme              *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gboolean               foliage = widget == LRG_CUSTOM_WIDGET (self->foliage_list);
	LrgTerrainAsset       *asset = asset_of (self, self->node, NULL);
	guint                  n = list_count (self, foliage), i;
	gint                   sel = foliage ? self->sel_foliage : self->sel_layer;
	gfloat                 fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                 radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	GrlFont               *font = lrg_theme_get_ui_font (theme);
	const GrlColor        *text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	const GrlColor        *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	GrlColor               track = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);

	track.a = (guint8) (track.a / 2);
	lrg_paint_list_rect (list, x, y, width, height, radius, &track);
	if (n == 0 || asset == NULL)
	{
		lrg_paint_list_text (list, font, foliage ? "No foliage layers: pick a kind and Add" : "No layers",
		                     x + 10.0f, roundf (y + (LIST_ROW_H - fs) * 0.5f), fs, 0.0f, muted);
		return;
	}
	for (i = 0; i < n; i++)
	{
		gfloat            ry = y + 1.0f + (gfloat) i * LIST_ROW_H;
		g_autofree gchar *label = NULL;
		g_autofree gchar *owned = NULL;
		GrlColor          swatch;
		gfloat            rgba[4];
		gfloat            is = 12.0f;
		gfloat            bx = x + width - LIST_BTN_W - 2.0f;
		gboolean          can_remove = foliage || n > 1;

		if ((gint) i == sel)
			lrg_paint_list_rect (list, x + 2.0f, ry, width - 4.0f, LIST_ROW_H - 1.0f, radius,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION));
		if (foliage)
		{
			LrgFoliage      *fo = lrg_terrain_asset_get_foliage (asset);
			LrgFoliageLayer *fl = lrg_foliage_get_layer (fo, i);
			static const gchar * const kinds[] = { "mesh", "billboard", "grass" };
			LrgFoliageKind   kind = lrg_foliage_layer_get_kind (fl);

			lrg_foliage_layer_get_color (fl, rgba);
			label = g_strdup_printf ("%s  %s  %.3g/m2  %s  %u inst.", lrg_foliage_layer_get_name (fl),
			                         (guint) kind < G_N_ELEMENTS (kinds) ? kinds[kind] : "?",
			                         (gdouble) lrg_foliage_layer_get_density (fl),
			                         basename_or (kind == LRG_FOLIAGE_KIND_MESH ? lrg_foliage_layer_get_mesh (fl)
			                                                                   : lrg_foliage_layer_get_texture (fl),
			                                      "-", &owned),
			                         lrg_foliage_get_instance_count (fo, i));
		}
		else
		{
			LrgTerrainLayer *layer = lrg_terrain_asset_get_layer (asset, i);

			lrg_terrain_layer_get_color (layer, rgba);
			label = g_strdup_printf ("%u  %s  %s  x%.3g", i, lrg_terrain_layer_get_name (layer),
			                         basename_or (lrg_terrain_layer_get_texture (layer), "colour", &owned),
			                         (gdouble) lrg_terrain_layer_get_tiling (layer));
		}
		swatch.r = (guint8) CLAMP (rgba[0] * 255.0f, 0.0f, 255.0f);
		swatch.g = (guint8) CLAMP (rgba[1] * 255.0f, 0.0f, 255.0f);
		swatch.b = (guint8) CLAMP (rgba[2] * 255.0f, 0.0f, 255.0f);
		swatch.a = 255;
		if (foliage)
			lrg_paint_list_triangle (list, x + 11.0f, ry + 5.0f, x + 5.0f, ry + LIST_ROW_H - 6.0f, x + 17.0f,
			                         ry + LIST_ROW_H - 6.0f, &swatch);
		else
		{
			lrg_paint_list_rect (list, x + 5.0f, ry + 5.0f, 13.0f, LIST_ROW_H - 10.0f, 3.0f, &swatch);
			lrg_paint_list_border (list, x + 5.0f, ry + 5.0f, 13.0f, LIST_ROW_H - 10.0f, 3.0f, 1.0f, muted);
		}
		lrg_paint_list_push_clip (list, x + 24.0f, ry, MAX (width - 24.0f - LIST_BTN_W - 6.0f, 0.0f), LIST_ROW_H);
		lrg_paint_list_text (list, font, label, x + 24.0f, roundf (ry + (LIST_ROW_H - fs) * 0.5f - 1.0f), fs, 0.0f,
		                     text);
		lrg_paint_list_pop_clip (list);
		lrg_paint_list_icon (list, LRG_ICON_TRASH, bx + (LIST_BTN_W - is) * 0.5f, ry + (LIST_ROW_H - is) * 0.5f, is,
		                     0.0f, can_remove ? lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER) : muted);
	}
}

static gboolean
list_event (LrgCustomWidget  *widget,
            const LrgUIEvent *event,
            gpointer          user_data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (user_data);
	gboolean               foliage = widget == LRG_CUSTOM_WIDGET (self->foliage_list);
	gfloat                 x = lrg_widget_get_world_x (LRG_WIDGET (widget));
	gfloat                 y = lrg_widget_get_world_y (LRG_WIDGET (widget));
	gfloat                 w = lrg_widget_get_width (LRG_WIDGET (widget));
	gfloat                 px, py;
	guint                  n, row;

	if (lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN || lrg_ui_event_get_button (event) != 0)
		return FALSE;
	px = lrg_ui_event_get_x (event);
	py = lrg_ui_event_get_y (event);
	n = list_count (self, foliage);
	if (py < y + 1.0f || n == 0)
		return FALSE;
	row = (guint) ((py - y - 1.0f) / LIST_ROW_H);
	if (row >= n)
		return TRUE;
	if (px >= x + w - LIST_BTN_W - 2.0f)
	{
		if (foliage)
			lrg_editor_terrain_panel_remove_foliage (self, NULL, row);
		else if (n > 1)
			lrg_editor_terrain_panel_remove_layer (self, NULL, row);
		return TRUE;
	}
	if (foliage)
		lrg_editor_terrain_panel_set_selected_foliage (self, (gint) row);
	else
		lrg_editor_terrain_panel_set_selected_layer (self, (gint) row);
	return TRUE;
}

/* The summary line under the lists. */
static void
info_measure (LrgCustomWidget     *widget,
              gfloat               avail_width,
              gfloat               avail_height,
              LrgCustomWidgetSize *size,
              gpointer             user_data)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat    fs = (theme != NULL) ? lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL) : 12.0f;

	size->width = 160.0f;
	size->height = (fs + 5.0f) * 3.0f + 6.0f;
}

static void
info_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (user_data);
	LrgTheme              *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	g_autofree gchar      *text = lrg_editor_terrain_panel_describe (self);
	gfloat                 fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	GrlFont               *font = lrg_theme_get_ui_font (theme);
	g_auto(GStrv)          parts = g_strsplit (text, " - ", -1);
	GString               *line = g_string_new (NULL);
	gfloat                 ly = y + 3.0f;
	guint                  i;

	/* wraps the " - " separated facts into as many lines as fit */
	for (i = 0; parts[i] != NULL; i++)
	{
		g_autofree gchar *probe = g_strdup_printf ("%s%s%s", line->str, line->len > 0 ? "  ·  " : "", parts[i]);
		gfloat            tw = 0.0f;

		lrg_paint_measure_text (font, probe, fs, 0.0f, &tw, NULL);
		if (tw > width - 8.0f && line->len > 0)
		{
			lrg_paint_list_text (list, font, line->str, x + 4.0f, ly, fs, 0.0f,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
			ly += fs + 5.0f;
			g_string_assign (line, parts[i]);
		}
		else
			g_string_assign (line, probe);
	}
	if (line->len > 0)
		lrg_paint_list_text (list, font, line->str, x + 4.0f, ly, fs, 0.0f,
		                     lrg_theme_get_color (theme, strstr (line->str, "unsaved") != NULL ? LRG_THEME_COLOR_WARNING
		                                                                                       : LRG_THEME_COLOR_TEXT_MUTED));
	g_string_free (line, TRUE);
}

/* ==========================================================================
 * Viewport tools: the brush cursor and strokes
 * ========================================================================== */

/* Raycasts the view ray through (u, v) onto the shown terrain. */
static gboolean
pick_terrain (LrgEditorTerrainPanel *self,
              const LrgEditorView   *view,
              gfloat                 u,
              gfloat                 v,
              gfloat                *out_local,
              gfloat                *out_world)
{
	LrgComponent *comp;
	gfloat        o[3], d[3];

	if (self->node == NULL)
		return FALSE;
	comp = lrg_editor_terrain_node_component (self->editor, resolver_of (self), self->node);
	if (comp == NULL || !LRG_IS_TERRAIN_COMPONENT (comp))
		return FALSE;
	lrg_editor_view_get_ray (view, u, v, o, d);
	return lrg_terrain_component_raycast (LRG_TERRAIN_COMPONENT (comp), o, d, PICK_DISTANCE, out_world, out_local);
}

static LrgEditorTerrainStrokeFlags
flags_from_mods (LrgInputModifiers mods)
{
	LrgEditorTerrainStrokeFlags flags = LRG_EDITOR_TERRAIN_STROKE_NONE;

	if (mods & LRG_INPUT_MODIFIER_CTRL)
		flags |= LRG_EDITOR_TERRAIN_STROKE_INVERT;
	if (mods & LRG_INPUT_MODIFIER_SHIFT)
		flags |= LRG_EDITOR_TERRAIN_STROKE_SMOOTH;
	return flags;
}

/* ==========================================================================
 * Viewport tools: water outline handles
 * ========================================================================== */

/* The selected water node with an outline (lake or river), its body and
 * path. */
static LrgWaterBody *
water_target (LrgEditorTerrainPanel  *self,
              LrgNode               **out_node,
              gchar                 **out_path)
{
	LrgNode          *node = primary (self);
	g_autofree gchar *path = NULL;
	LrgWaterBody     *body;

	if (out_path != NULL)
		*out_path = NULL;
	if (!node_is_kind (node, LRG_NODE_VISUAL_WATER) || lrg_node_get_locked (node) ||
	    (self->play != NULL && lrg_editor_play_session_is_playing (self->play)))
		return NULL;
	path = lrg_editor_terrain_node_path (self->editor, resolver_of (self), node);
	body = (path != NULL) ? lrg_editor_terrain_store_get_water (self->editor, path, NULL) : NULL;
	if (body == NULL || lrg_water_body_get_shape (body) == LRG_WATER_BODY_PLANE)
		return NULL;
	*out_node = node;
	if (out_path != NULL)
		*out_path = g_steal_pointer (&path);
	return body;
}

/* A point's world position: the body is drawn at the node's world
 * position (the water component ignores rotation and scale). */
static void
water_point_world (LrgNode      *node,
                   LrgWaterBody *body,
                   guint         index,
                   gfloat       *out)
{
	gfloat pos[3], p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	lrg_editor_node_get_world_position (node, pos);
	lrg_water_body_get_point (body, index, p);
	out[0] = pos[0] + p[0];
	out[1] = pos[1] + (lrg_water_body_get_shape (body) == LRG_WATER_BODY_RIVER ? p[1] : lrg_water_body_get_level (body));
	out[2] = pos[2] + p[2];
}

static gint
water_hit (LrgEditorTerrainPanel *self,
           const LrgEditorView   *view,
           LrgNode               *node,
           LrgWaterBody          *body,
           gfloat                 u,
           gfloat                 v)
{
	gint   best = -1;
	gfloat best_d = HANDLE_PICK * HANDLE_PICK;
	guint  i;

	for (i = 0; i < lrg_water_body_get_n_points (body); i++)
	{
		gfloat w[3], su, sv, dd;

		water_point_world (node, body, i, w);
		if (!lrg_editor_view_project (view, w, &su, &sv, NULL))
			continue;
		dd = (su - u) * (su - u) + (sv - v) * (sv - v);
		if (dd <= best_d)
		{
			best_d = dd;
			best = (gint) i;
		}
	}
	return best;
}

/* The view ray meets the horizontal plane y = @plane_y at @out. */
static gboolean
ray_to_plane (const LrgEditorView *view,
              gfloat               u,
              gfloat               v,
              gfloat               plane_y,
              gfloat              *out)
{
	gfloat       o[3], d[3], t;
	const gfloat n[3] = { 0.0f, 1.0f, 0.0f };
	gfloat       p[3] = { 0.0f, 0.0f, 0.0f };

	p[1] = plane_y;
	lrg_editor_view_get_ray (view, u, v, o, d);
	if (!lrg_editor_ray_plane (o, d, p, n, &t) || t < 0.0f)
		return FALSE;
	out[0] = o[0] + d[0] * t;
	out[1] = plane_y;
	out[2] = o[2] + d[2] * t;
	return TRUE;
}

/* Records the outline change since self->water_before as one step. */
static void
water_commit (LrgEditorTerrainPanel *self,
              LrgNode               *node,
              LrgWaterBody          *body,
              const gchar           *path,
              const gchar           *label)
{
	PointsUndo *u;

	if (self->water_before == NULL)
		return;
	u = g_new0 (PointsUndo, 1);
	g_set_weak_pointer (&u->editor, self->editor);
	g_set_weak_pointer (&u->app, self->app);
	u->body = g_object_ref (body);
	u->path = g_strdup (path);
	u->before = g_steal_pointer (&self->water_before);
	u->after = points_take (body);
	lrg_editor_terrain_store_set_dirty (self->editor, path, TRUE);
	editor_terrain_push_undo (self->editor, label, points_undo_restore, u, points_undo_free);
	if (self->app != NULL)
		lrg_editor_water_editor_sync_from_store (self->app, path);
	queue_render (self);
}

/* Ctrl+click: a new point on the water plane, inserted after the nearest
 * point (appended for a river's ends). */
static gboolean
water_add_point (LrgEditorTerrainPanel *self,
                 const LrgEditorView   *view,
                 LrgNode               *node,
                 LrgWaterBody          *body,
                 const gchar           *path,
                 gfloat                 u,
                 gfloat                 v)
{
	gfloat  pos[3], hit[3], plane;
	GArray *pts;
	guint   n = lrg_water_body_get_n_points (body), i, best = 0;
	gfloat  best_d = G_MAXFLOAT;
	gfloat  np[4];

	lrg_editor_node_get_world_position (node, pos);
	plane = pos[1] + lrg_water_body_get_level (body);
	if (n > 0 && lrg_water_body_get_shape (body) == LRG_WATER_BODY_RIVER)
	{
		gfloat w[3];

		water_point_world (node, body, n - 1, w);
		plane = w[1];
	}
	if (!ray_to_plane (view, u, v, plane, hit) || n >= LRG_WATER_BODY_MAX_POINTS)
		return FALSE;
	np[0] = hit[0] - pos[0];
	np[1] = plane - pos[1];
	np[2] = hit[2] - pos[2];
	np[3] = 4.0f;
	for (i = 0; i < n; i++)
	{
		gfloat w[3], dd;

		water_point_world (node, body, i, w);
		dd = (w[0] - hit[0]) * (w[0] - hit[0]) + (w[2] - hit[2]) * (w[2] - hit[2]);
		if (dd < best_d)
		{
			best_d = dd;
			best = i;
		}
	}
	self->water_before = points_take (body);
	pts = points_take (body);
	if (n > 0)
	{
		np[3] = g_array_index (pts, gfloat, best * 4 + 3);
		g_array_insert_vals (pts, (best + 1) * 4, np, 4);
	}
	else
		g_array_append_vals (pts, np, 4);
	points_put (body, pts);
	g_array_unref (pts);
	water_commit (self, node, body, path, "Water: add point");
	return TRUE;
}

/* ==========================================================================
 * Viewport hooks
 * ========================================================================== */

gboolean
lrg_editor_terrain_panel_viewport_press (LrgEditorTerrainPanel *self,
                                         const LrgEditorView   *view,
                                         gfloat                 u,
                                         gfloat                 v,
                                         LrgInputModifiers      mods)
{
	LrgNode          *wnode = NULL;
	g_autofree gchar *wpath = NULL;
	LrgWaterBody     *body;

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);
	g_return_val_if_fail (view != NULL, FALSE);

	sync_widgets (self);
	self->cursor_mods = mods;

	/* The brush: a stroke where the ray meets the terrain; a press off
	 * the terrain is still taken so the terrain stays selected */
	if (lrg_editor_terrain_panel_get_brush_active (self))
	{
		self->cursor_valid = pick_terrain (self, view, u, v, self->cursor_local, self->cursor_world);
		if (self->cursor_valid)
			lrg_editor_terrain_panel_begin_stroke (self, NULL, self->cursor_local[0], self->cursor_local[2],
			                                       flags_from_mods (mods));
		return TRUE;
	}

	/* Water outline handles */
	body = water_target (self, &wnode, &wpath);
	if (body != NULL)
	{
		gint hit = water_hit (self, view, wnode, body, u, v);

		if (hit >= 0 && (mods & LRG_INPUT_MODIFIER_SHIFT))
		{
			guint min = (lrg_water_body_get_shape (body) == LRG_WATER_BODY_LAKE) ? 3u : 2u;

			if (lrg_water_body_get_n_points (body) <= min)
			{
				set_status (self, "Water: an outline keeps at least its minimum points");
				return TRUE;
			}
			self->water_before = points_take (body);
			lrg_water_body_remove_point (body, (guint) hit);
			water_commit (self, wnode, body, wpath, "Water: remove point");
			self->water_hot = -1;
			return TRUE;
		}
		if (hit >= 0)
		{
			gfloat w[3];

			water_point_world (wnode, body, (guint) hit, w);
			self->water_drag = hit;
			self->water_plane_y = w[1];
			g_set_weak_pointer (&self->water_node, wnode);
			g_clear_pointer (&self->water_before, g_array_unref);
			self->water_before = points_take (body);
			return TRUE;
		}
		if (mods & LRG_INPUT_MODIFIER_CTRL)
			return water_add_point (self, view, wnode, body, wpath, u, v);
	}
	return FALSE;
}

gboolean
lrg_editor_terrain_panel_viewport_move (LrgEditorTerrainPanel *self,
                                        const LrgEditorView   *view,
                                        gfloat                 u,
                                        gfloat                 v,
                                        LrgInputModifiers      mods)
{
	LrgNode          *wnode = NULL;
	g_autofree gchar *wpath = NULL;
	LrgWaterBody     *body;
	gboolean          was_valid = self->cursor_valid;

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);
	g_return_val_if_fail (view != NULL, FALSE);

	self->cursor_mods = mods;

	/* A water point drag follows the pointer on its plane */
	if (self->water_drag >= 0)
	{
		body = water_target (self, &wnode, &wpath);
		if (body != NULL && wnode == self->water_node && (guint) self->water_drag < lrg_water_body_get_n_points (body))
		{
			gfloat hit[3], pos[3], p[4];

			if (ray_to_plane (view, u, v, self->water_plane_y, hit))
			{
				lrg_editor_node_get_world_position (wnode, pos);
				lrg_water_body_get_point (body, (guint) self->water_drag, p);
				lrg_water_body_set_point (body, (guint) self->water_drag, hit[0] - pos[0], p[1], hit[2] - pos[2],
				                          p[3]);
				lrg_editor_terrain_store_set_dirty (self->editor, wpath, TRUE);
				queue_render (self);
			}
		}
		return TRUE;
	}

	/* The brush cursor */
	if (lrg_editor_terrain_panel_get_brush_active (self))
	{
		self->cursor_valid = pick_terrain (self, view, u, v, self->cursor_local, self->cursor_world);
		if (self->stroking && self->cursor_valid)
		{
			self->stroke_x = self->cursor_local[0];
			self->stroke_z = self->cursor_local[2];
		}
		return self->stroking;
	}
	self->cursor_valid = FALSE;
	if (was_valid)
		queue_render (self);

	/* The hovered water handle */
	body = water_target (self, &wnode, &wpath);
	{
		gint hot = (body != NULL) ? water_hit (self, view, wnode, body, u, v) : -1;

		if (hot != self->water_hot)
		{
			self->water_hot = hot;
			queue_render (self);
		}
	}
	return FALSE;
}

gboolean
lrg_editor_terrain_panel_viewport_release (LrgEditorTerrainPanel *self,
                                           gboolean               commit)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);

	if (self->stroking)
	{
		lrg_editor_terrain_panel_end_stroke (self, commit);
		return TRUE;
	}
	if (self->water_drag >= 0)
	{
		LrgNode          *wnode = NULL;
		g_autofree gchar *wpath = NULL;
		LrgWaterBody     *body = water_target (self, &wnode, &wpath);

		self->water_drag = -1;
		if (body != NULL && self->water_before != NULL)
		{
			if (commit)
				water_commit (self, wnode, body, wpath, "Water: move point");
			else
			{
				points_put (body, self->water_before);
				lrg_editor_terrain_store_set_dirty (self->editor, wpath, TRUE);
			}
		}
		g_clear_pointer (&self->water_before, g_array_unref);
		g_clear_weak_pointer (&self->water_node);
		queue_render (self);
		return TRUE;
	}
	return FALSE;
}

gboolean
lrg_editor_terrain_panel_viewport_tick (LrgEditorTerrainPanel *self,
                                        gfloat                 delta_time)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);

	if (!self->stroking)
		return FALSE;
	lrg_editor_terrain_panel_stroke_to (self, self->stroke_x, self->stroke_z, MAX (delta_time, 0.0f));
	return TRUE;
}

gboolean
lrg_editor_terrain_panel_viewport_key (LrgEditorTerrainPanel *self,
                                       GrlKey                 key,
                                       LrgInputModifiers      mods)
{
	gboolean shift = (mods & LRG_INPUT_MODIFIER_SHIFT) != 0;

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);

	if (key == GRL_KEY_ESCAPE && (self->stroking || self->water_drag >= 0))
	{
		lrg_editor_terrain_panel_viewport_release (self, FALSE);
		set_status (self, "Terrain: cancelled");
		return TRUE;
	}
	if (!lrg_editor_terrain_panel_get_brush_active (self))
		return FALSE;
	if (key == GRL_KEY_LEFT_BRACKET || key == GRL_KEY_RIGHT_BRACKET)
	{
		gfloat           f = (key == GRL_KEY_RIGHT_BRACKET) ? 1.1f : 1.0f / 1.1f;
		g_autofree gchar *msg = NULL;

		if (shift)
		{
			lrg_terrain_brush_set_strength (self->brush, CLAMP (lrg_terrain_brush_get_strength (self->brush) * f,
			                                                    0.01f, 64.0f));
			msg = g_strdup_printf ("Brush strength %.2f", (gdouble) lrg_terrain_brush_get_strength (self->brush));
		}
		else
		{
			lrg_terrain_brush_set_radius (self->brush, CLAMP (lrg_terrain_brush_get_radius (self->brush) * f,
			                                                  0.1f, 256.0f));
			msg = g_strdup_printf ("Brush radius %.2f", (gdouble) lrg_terrain_brush_get_radius (self->brush));
		}
		set_status (self, msg);
		lrg_property_grid_refresh (self->brush_grid);
		queue_render (self);
		return TRUE;
	}
	return FALSE;
}

void
lrg_editor_terrain_panel_viewport_leave (LrgEditorTerrainPanel *self)
{
	g_return_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self));

	if (self->cursor_valid && !self->stroking)
	{
		self->cursor_valid = FALSE;
		queue_render (self);
	}
	self->water_hot = -1;
}

gboolean
lrg_editor_terrain_panel_get_cursor (LrgEditorTerrainPanel *self,
                                     gfloat                *out_local,
                                     gfloat                *out_world)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), FALSE);

	if (!self->cursor_valid)
		return FALSE;
	if (out_local != NULL)
		memcpy (out_local, self->cursor_local, sizeof self->cursor_local);
	if (out_world != NULL)
		memcpy (out_world, self->cursor_world, sizeof self->cursor_world);
	return TRUE;
}

/* A ring of @radius around the cursor, draped on the terrain. */
static void
paint_ring (LrgPaintList        *list,
            LrgTerrainAsset     *asset,
            const gfloat        *node_world,
            const LrgEditorView *view,
            const gfloat        *center,
            gfloat               radius,
            gfloat               thickness,
            const GrlColor      *color,
            gfloat               ox,
            gfloat               oy,
            gfloat               scale)
{
	gfloat px = 0.0f, py = 0.0f;
	gboolean prev = FALSE;
	guint  i;

	for (i = 0; i <= RING_SEGMENTS; i++)
	{
		gfloat a = (gfloat) i / (gfloat) RING_SEGMENTS * 2.0f * (gfloat) G_PI;
		gfloat local[3], world[3], u, v, h;
		gboolean ok;

		local[0] = center[0] + cosf (a) * radius;
		local[2] = center[2] + sinf (a) * radius;
		local[1] = lrg_terrain_asset_sample_height (asset, local[0], local[2], &h) ? h : center[1];
		lrg_editor_math_transform_point (node_world, local, world);
		ok = lrg_editor_view_project (view, world, &u, &v, NULL);
		if (ok && prev)
			lrg_paint_list_line (list, px, py, ox + u * scale, oy + v * scale, thickness, color);
		prev = ok;
		px = ox + u * scale;
		py = oy + v * scale;
	}
}

guint
lrg_editor_terrain_panel_paint_overlay (LrgEditorTerrainPanel *self,
                                        LrgPaintList          *list,
                                        LrgTheme              *theme,
                                        const LrgEditorView   *view,
                                        gfloat                 ox,
                                        gfloat                 oy,
                                        gfloat                 scale)
{
	LrgNode          *wnode = NULL;
	g_autofree gchar *wpath = NULL;
	LrgWaterBody     *body;
	guint             painted = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_TERRAIN_PANEL (self), 0);
	g_return_val_if_fail (view != NULL, 0);

	/* The brush cursor: the rim, the half-weight ring and the centre */
	if (self->cursor_valid && lrg_editor_terrain_panel_get_brush_active (self))
	{
		LrgTerrainAsset   *asset = asset_of (self, self->node, NULL);
		LrgTerrainBrushOp  op = stroke_op (self->tool, flags_from_mods (self->cursor_mods));
		gfloat             world[16], r = lrg_terrain_brush_get_radius (self->brush);
		gfloat             lo = 0.0f, hi = 1.0f, cu, cv;
		GrlColor           col, soft;
		guint              k;
		LrgThemeColor      role = LRG_THEME_COLOR_ACCENT;

		if (op == LRG_TERRAIN_BRUSH_LOWER || op == LRG_TERRAIN_BRUSH_HOLE || op == LRG_TERRAIN_BRUSH_FOLIAGE_ERASE)
			role = LRG_THEME_COLOR_DANGER;
		else if (op == LRG_TERRAIN_BRUSH_PAINT || op == LRG_TERRAIN_BRUSH_FOLIAGE_PAINT || op == LRG_TERRAIN_BRUSH_FILL)
			role = LRG_THEME_COLOR_SUCCESS;
		else if (op == LRG_TERRAIN_BRUSH_SMOOTH)
			role = LRG_THEME_COLOR_WARNING;
		col = *lrg_theme_get_color (theme, role);
		soft = col;
		soft.a = (guint8) (col.a * 0.55f);

		/* where the falloff reaches one half */
		for (k = 0; k < 16; k++)
		{
			gfloat mid = (lo + hi) * 0.5f;

			if (lrg_terrain_brush_get_weight (self->brush, mid * r) > 0.5f)
				lo = mid;
			else
				hi = mid;
		}
		lrg_editor_node_get_world_matrix (self->node, world);
		if (asset != NULL)
		{
			paint_ring (list, asset, world, view, self->cursor_local, r, 2.0f, &col, ox, oy, scale);
			if (lo > 0.02f && lo < 0.98f)
				paint_ring (list, asset, world, view, self->cursor_local, lo * r, 1.0f, &soft, ox, oy, scale);
			painted += 2;
		}
		if (lrg_editor_view_project (view, self->cursor_world, &cu, &cv, NULL))
		{
			lrg_paint_list_circle (list, ox + cu * scale, oy + cv * scale, 3.0f, &col);
			painted++;
		}
	}

	/* The selected water node's outline and handles */
	body = water_target (self, &wnode, &wpath);
	if (body != NULL)
	{
		const GrlColor *line = lrg_theme_get_color (theme, LRG_THEME_COLOR_LINK);
		const GrlColor *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
		const GrlColor *handle = lrg_theme_get_color (theme, LRG_THEME_COLOR_HANDLE);
		guint           n = lrg_water_body_get_n_points (body), i;
		gboolean        closed = lrg_water_body_get_shape (body) == LRG_WATER_BODY_LAKE;

		for (i = 0; i < n; i++)
		{
			gfloat a[3], b[3], au, av, bu, bv;

			if (i + 1 == n && !closed)
				break;
			water_point_world (wnode, body, i, a);
			water_point_world (wnode, body, (i + 1) % n, b);
			if (lrg_editor_view_project (view, a, &au, &av, NULL) && lrg_editor_view_project (view, b, &bu, &bv, NULL))
				lrg_paint_list_line (list, ox + au * scale, oy + av * scale, ox + bu * scale, oy + bv * scale, 2.0f,
				                     line);
		}
		for (i = 0; i < n; i++)
		{
			gfloat   w[3], hu, hv;
			gboolean hot = (gint) i == self->water_hot || (gint) i == self->water_drag;

			water_point_world (wnode, body, i, w);
			if (!lrg_editor_view_project (view, w, &hu, &hv, NULL))
				continue;
			lrg_paint_list_circle (list, ox + hu * scale, oy + hv * scale, (hot ? HANDLE_RADIUS + 2.0f : HANDLE_RADIUS),
			                       hot ? accent : handle);
			lrg_paint_list_circle (list, ox + hu * scale, oy + hv * scale, HANDLE_RADIUS * 0.45f, line);
			painted++;
		}
	}
	return painted;
}

/* ==========================================================================
 * Sync
 * ========================================================================== */

static gchar *
grid_signature (LrgEditorTerrainPanel *self)
{
	LrgTerrainAsset *asset = asset_of (self, self->node, NULL);
	LrgFoliageLayer *fl = NULL;

	if (asset != NULL && self->sel_foliage >= 0)
		fl = lrg_foliage_get_layer (lrg_terrain_asset_get_foliage (asset), (guint) self->sel_foliage);
	return g_strdup_printf ("%p/%p/%d/%d/%d/%d/%d", (gpointer) self->node, (gpointer) asset, (gint) self->tool,
	                        self->sel_layer, self->sel_foliage, fl != NULL ? (gint) lrg_foliage_layer_get_kind (fl) : -1,
	                        (gint) lrg_terrain_brush_get_falloff (self->brush));
}

static void
sync_widgets (LrgEditorTerrainPanel *self)
{
	LrgNode          *node;
	LrgTerrainAsset  *asset;
	g_autofree gchar *sig = NULL;
	g_autofree gchar *path = NULL;
	guint             i;

	if (self->editor == NULL || self->top == NULL)
		return;
	node = primary (self);
	if (!node_is_kind (node, LRG_NODE_VISUAL_TERRAIN))
		node = NULL;
	if (node != self->node)
	{
		if (self->stroking)
			lrg_editor_terrain_panel_end_stroke (self, TRUE);
		g_set_weak_pointer (&self->node, node);
		self->sel_layer = 0;
		self->sel_foliage = 0;
		self->cursor_valid = FALSE;
	}
	asset = asset_of (self, self->node, &path);

	/* Clamp the list selections (undo can shorten the lists) */
	if (asset != NULL)
	{
		guint nl = lrg_terrain_asset_get_n_layers (asset);
		guint nf = lrg_foliage_get_n_layers (lrg_terrain_asset_get_foliage (asset));

		if (self->sel_layer >= (gint) nl)
			self->sel_layer = (gint) nl - 1;
		if (self->sel_foliage >= (gint) nf)
			self->sel_foliage = (gint) nf - 1;
		if (self->sel_foliage < 0 && nf > 0)
			self->sel_foliage = 0;
		if (self->sel_layer < 0 && nl > 0)
			self->sel_layer = 0;
	}
	else
	{
		self->sel_layer = -1;
		self->sel_foliage = -1;
	}

	/* Header and the empty state */
	lrg_label_set_text (self->node_label, self->node != NULL ? lrg_node_get_name (self->node) : "");
	lrg_widget_set_visible (self->rebake_chip, asset != NULL);
	lrg_widget_set_visible (self->save_chip, asset != NULL);
	if (asset != NULL)
		editor_chip_set_badge (self->save_chip, lrg_editor_terrain_store_is_dirty (self->editor, path) ? "*" : NULL);
	lrg_widget_set_visible (self->scroll, asset != NULL);
	lrg_widget_set_visible (self->empty, asset == NULL);
	lrg_widget_set_visible (self->hint_none, self->node == NULL);
	lrg_widget_set_visible (self->hint_bad, self->node != NULL && asset == NULL);
	if (asset != NULL)
		lrg_widget_set_visible (self->add_layer_chip, lrg_terrain_asset_get_n_layers (asset) < MAX_LAYERS);

	/* Grids: rebuilt when what they show changed shape, refreshed else */
	sig = grid_signature (self);
	if (g_strcmp0 (sig, self->signature) != 0)
	{
		g_free (self->signature);
		self->signature = g_steal_pointer (&sig);
		for (i = 0; i < 3; i++)
			lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self->src[i]), NULL);
		lrg_property_grid_rebuild (self->brush_grid);
		lrg_property_grid_rebuild (self->layer_grid);
		lrg_property_grid_rebuild (self->foliage_grid);
	}
	else if (!self->in_gesture)
	{
		lrg_property_grid_refresh (self->brush_grid);
		lrg_property_grid_refresh (self->layer_grid);
		lrg_property_grid_refresh (self->foliage_grid);
	}
	lrg_widget_set_visible (LRG_WIDGET (self->layer_grid), self->sel_layer >= 0);
	lrg_widget_set_visible (LRG_WIDGET (self->foliage_grid), self->sel_foliage >= 0);
	lrg_widget_queue_layout (self->layer_list);
	lrg_widget_queue_layout (self->foliage_list);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
on_changed (LrgEditorTerrainPanel *self)
{
	sync_widgets (self);
}

static void
on_level_replaced (LrgEditorTerrainPanel *self,
                   LrgLevel              *level)
{
	if (self->stroking)
		lrg_editor_terrain_panel_end_stroke (self, FALSE);
	g_clear_weak_pointer (&self->node);
	sync_widgets (self);
}

/* ==========================================================================
 * Chips
 * ========================================================================== */

static void
on_rebake (LrgWidget *chip,
           gpointer   user_data)
{
	lrg_editor_terrain_panel_rebake (LRG_EDITOR_TERRAIN_PANEL (user_data), NULL);
}

static void
on_save (LrgWidget *chip,
         gpointer   user_data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (user_data);
	g_autoptr(GError)      error = NULL;

	if (!lrg_editor_terrain_panel_save (self, NULL, &error) && self->app != NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Terrain: %s", error->message);
}

static void
on_add_layer (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (user_data);
	LrgTerrainAsset       *asset = asset_of (self, self->node, NULL);
	g_autofree gchar      *name = NULL;

	if (asset == NULL)
		return;
	name = g_strdup_printf ("layer%u", lrg_terrain_asset_get_n_layers (asset));
	lrg_editor_terrain_panel_add_layer (self, NULL, name);
}

static void
on_add_foliage (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (user_data);
	LrgTerrainAsset       *asset = asset_of (self, self->node, NULL);
	LrgFoliageKind         kind = (LrgFoliageKind) MAX (lrg_combo_box_get_selected (self->foliage_kind), 0);
	static const gchar * const names[] = { "trees", "cards", "grass" };
	g_autofree gchar      *name = NULL;

	if (asset == NULL)
		return;
	name = g_strdup_printf ("%s%u", names[MIN ((guint) kind, 2u)],
	                        lrg_foliage_get_n_layers (lrg_terrain_asset_get_foliage (asset)));
	lrg_editor_terrain_panel_add_foliage (self, NULL, name, kind);
}

/* ==========================================================================
 * IPC: terrain.*
 * ========================================================================== */

static JsonNode *
obj_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static LrgEditorTerrainPanel *
ipc_panel (gpointer   user_data,
           GError   **error)
{
	LrgEditorTerrainPanel *panel = lrg_editor_terrain_panel_find (LRG_EDITOR_APP (user_data));

	if (panel == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No terrain panel");
	return panel;
}

static gboolean
p_has (JsonObject  *params,
       const gchar *name)
{
	return params != NULL && json_object_has_member (params, name) &&
	       !JSON_NODE_HOLDS_NULL (json_object_get_member (params, name));
}

static const gchar *
p_string (JsonObject  *params,
          const gchar *name)
{
	JsonNode *n;

	if (!p_has (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	return (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING) ? json_node_get_string (n)
	                                                                                    : NULL;
}

static gboolean
p_number (JsonObject  *params,
          const gchar *name,
          gdouble     *out)
{
	JsonNode *n;

	if (!p_has (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	switch (json_node_get_value_type (n))
	{
	case G_TYPE_INT64:
		*out = (gdouble) json_node_get_int (n);
		return TRUE;
	case G_TYPE_DOUBLE:
		*out = json_node_get_double (n);
		return TRUE;
	default:
		return FALSE;
	}
}

static gboolean
p_bool (JsonObject  *params,
        const gchar *name,
        gboolean     fallback)
{
	JsonNode *n;

	if (!p_has (params, name))
		return fallback;
	n = json_object_get_member (params, name);
	return (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_BOOLEAN) ? json_node_get_boolean (n)
	                                                                                     : fallback;
}

/* A JSON array of numbers into @out (at most @max). Returns the count. */
static guint
p_floats (JsonObject  *params,
          const gchar *name,
          gfloat      *out,
          guint        max)
{
	JsonNode  *n;
	JsonArray *a;
	guint      i, len;

	if (!p_has (params, name))
		return 0;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_ARRAY (n))
		return 0;
	a = json_node_get_array (n);
	len = MIN (json_array_get_length (a), max);
	for (i = 0; i < len; i++)
		out[i] = (gfloat) json_array_get_double_element (a, i);
	return len;
}

static gboolean
bad_params (GError      **error,
            const gchar  *message)
{
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, message);
	return FALSE;
}

/* "node": GUID, path or name; default the terrain node shown. */
static LrgNode *
ipc_node (LrgEditorTerrainPanel  *self,
          JsonObject             *params,
          GError                **error)
{
	LrgNode *node;

	sync_widgets (self);
	if (p_has (params, "node"))
	{
		const gchar *ref = p_string (params, "node");

		node = (ref != NULL) ? lrg_editor_find_node (self->editor, ref) : NULL;
		if (node == NULL)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No node '%s'",
			             ref != NULL ? ref : "(not a string)");
			return NULL;
		}
	}
	else
		node = self->node;
	if (node == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "No terrain node: give \"node\" or select one");
		return NULL;
	}
	if (asset_of (self, node, NULL) == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "'%s' is not a terrain node with a loadable .rterrain", lrg_node_get_name (node));
		return NULL;
	}
	return node;
}

static JsonArray *
floats_array (const gfloat *v,
              guint         n)
{
	JsonArray *a = json_array_new ();
	guint      i;

	for (i = 0; i < n; i++)
		json_array_add_double_element (a, (gdouble) v[i]);
	return a;
}

static const gchar *
enum_nick (GType type,
           gint  value)
{
	GEnumClass *klass = g_type_class_ref (type);
	GEnumValue *ev = g_enum_get_value (klass, value);
	const gchar *nick = (ev != NULL) ? ev->value_nick : "";

	g_type_class_unref (klass);
	return nick;
}

static gboolean
enum_from_nick (GType        type,
                const gchar *nick,
                gint        *out)
{
	GEnumClass *klass;
	GEnumValue *ev;

	if (nick == NULL)
		return FALSE;
	klass = g_type_class_ref (type);
	ev = g_enum_get_value_by_nick (klass, nick);
	if (ev != NULL)
		*out = ev->value;
	g_type_class_unref (klass);
	return ev != NULL;
}

static void
set_nullable_string (JsonObject  *o,
                     const gchar *name,
                     const gchar *value)
{
	if (value != NULL)
		json_object_set_string_member (o, name, value);
	else
		json_object_set_null_member (o, name);
}

static JsonObject *
brush_json (LrgEditorTerrainPanel *self)
{
	JsonObject *b = json_object_new ();

	json_object_set_string_member (b, "tool", enum_nick (LRG_TYPE_EDITOR_TERRAIN_TOOL, (gint) self->tool));
	json_object_set_double_member (b, "radius", (gdouble) lrg_terrain_brush_get_radius (self->brush));
	json_object_set_double_member (b, "strength", (gdouble) lrg_terrain_brush_get_strength (self->brush));
	json_object_set_string_member (b, "falloff",
	                               enum_nick (LRG_TYPE_TERRAIN_BRUSH_FALLOFF,
	                                          (gint) lrg_terrain_brush_get_falloff (self->brush)));
	json_object_set_double_member (b, "target_height", (gdouble) lrg_terrain_brush_get_target_height (self->brush));
	json_object_set_boolean_member (b, "sample_target", lrg_terrain_brush_get_sample_target (self->brush));
	json_object_set_int_member (b, "layer", self->sel_layer);
	json_object_set_int_member (b, "foliage_layer", self->sel_foliage);
	json_object_set_double_member (b, "noise_scale", (gdouble) lrg_terrain_brush_get_noise_scale (self->brush));
	json_object_set_int_member (b, "seed", (gint64) lrg_terrain_brush_get_seed (self->brush));
	return b;
}

static JsonNode *
terrain_json (LrgEditorTerrainPanel *self,
              LrgNode               *node)
{
	g_autofree gchar *path = NULL;
	LrgTerrainAsset  *asset = asset_of (self, node, &path);
	LrgFoliage       *foliage = lrg_terrain_asset_get_foliage (asset);
	JsonObject       *o = json_object_new ();
	JsonArray        *layers = json_array_new ();
	JsonArray        *fol = json_array_new ();
	guint             a, b, i;
	gfloat            f2[2], f4[4];

	json_object_set_string_member (o, "node", lrg_node_get_guid (node));
	json_object_set_string_member (o, "name", lrg_node_get_name (node));
	set_nullable_string (o, "asset", lrg_node_visual_get_asset (lrg_node_get_visual (node)));
	json_object_set_string_member (o, "path", path);
	json_object_set_boolean_member (o, "dirty", lrg_editor_terrain_store_is_dirty (self->editor, path));
	json_object_set_int_member (o, "resolution", lrg_terrain_asset_get_resolution (asset));
	json_object_set_double_member (o, "chunk_size", (gdouble) lrg_terrain_asset_get_chunk_size (asset));
	json_object_set_double_member (o, "cell_size", (gdouble) lrg_terrain_asset_get_cell_size (asset));
	lrg_terrain_asset_get_chunks (asset, &a, &b);
	f2[0] = (gfloat) a;
	f2[1] = (gfloat) b;
	json_object_set_array_member (o, "chunks", floats_array (f2, 2));
	lrg_terrain_asset_get_cells (asset, &a, &b);
	f2[0] = (gfloat) a;
	f2[1] = (gfloat) b;
	json_object_set_array_member (o, "cells", floats_array (f2, 2));
	lrg_terrain_asset_get_samples (asset, &a, &b);
	f2[0] = (gfloat) a;
	f2[1] = (gfloat) b;
	json_object_set_array_member (o, "samples", floats_array (f2, 2));
	lrg_terrain_asset_get_size (asset, &f2[0], &f2[1]);
	json_object_set_array_member (o, "size", floats_array (f2, 2));
	lrg_terrain_asset_get_height_range (asset, &f2[0], &f2[1]);
	json_object_set_array_member (o, "height_range", floats_array (f2, 2));
	json_object_set_string_member (o, "splat_mode",
	                               enum_nick (LRG_TYPE_TERRAIN_SPLAT_MODE, (gint) lrg_terrain_asset_get_splat_mode (asset)));
	json_object_set_int_member (o, "holes", lrg_terrain_asset_get_hole_count (asset));
	json_object_set_int_member (o, "lod_levels", lrg_terrain_asset_get_lod_levels (asset));
	json_object_set_int_member (o, "bake_resolution", lrg_terrain_asset_get_bake_resolution (asset));

	for (i = 0; i < lrg_terrain_asset_get_n_layers (asset); i++)
	{
		LrgTerrainLayer *layer = lrg_terrain_asset_get_layer (asset, i);
		JsonObject      *l = json_object_new ();

		json_object_set_string_member (l, "name", lrg_terrain_layer_get_name (layer));
		set_nullable_string (l, "texture", lrg_terrain_layer_get_texture (layer));
		set_nullable_string (l, "material", lrg_terrain_layer_get_material (layer));
		json_object_set_double_member (l, "tiling", (gdouble) lrg_terrain_layer_get_tiling (layer));
		lrg_terrain_layer_get_color (layer, f4);
		json_object_set_array_member (l, "color", floats_array (f4, 4));
		json_array_add_object_element (layers, l);
	}
	json_object_set_array_member (o, "layers", layers);

	for (i = 0; i < lrg_foliage_get_n_layers (foliage); i++)
	{
		LrgFoliageLayer *fl = lrg_foliage_get_layer (foliage, i);
		JsonObject      *l = json_object_new ();

		json_object_set_string_member (l, "name", lrg_foliage_layer_get_name (fl));
		json_object_set_string_member (l, "kind", enum_nick (LRG_TYPE_FOLIAGE_KIND, (gint) lrg_foliage_layer_get_kind (fl)));
		set_nullable_string (l, "mesh", lrg_foliage_layer_get_mesh (fl));
		set_nullable_string (l, "texture", lrg_foliage_layer_get_texture (fl));
		lrg_foliage_layer_get_color (fl, f4);
		json_object_set_array_member (l, "color", floats_array (f4, 4));
		json_object_set_double_member (l, "density", (gdouble) lrg_foliage_layer_get_density (fl));
		lrg_foliage_layer_get_scale_range (fl, &f2[0], &f2[1]);
		json_object_set_array_member (l, "scale", floats_array (f2, 2));
		lrg_foliage_layer_get_slope_range (fl, &f2[0], &f2[1]);
		json_object_set_array_member (l, "slope", floats_array (f2, 2));
		lrg_foliage_layer_get_height_range (fl, &f2[0], &f2[1]);
		json_object_set_array_member (l, "height", floats_array (f2, 2));
		json_object_set_double_member (l, "align", (gdouble) lrg_foliage_layer_get_align (fl));
		lrg_foliage_layer_get_size (fl, &f2[0], &f2[1]);
		json_object_set_array_member (l, "size", floats_array (f2, 2));
		lrg_foliage_layer_get_fade (fl, &f2[0], &f2[1]);
		json_object_set_array_member (l, "fade", floats_array (f2, 2));
		json_object_set_int_member (l, "seed", (gint64) lrg_foliage_layer_get_seed (fl));
		json_object_set_int_member (l, "instances", lrg_foliage_get_instance_count (foliage, i));
		json_array_add_object_element (fol, l);
	}
	json_object_set_array_member (o, "foliage", fol);
	json_object_set_object_member (o, "brush", brush_json (self));
	return obj_node (o);
}

#define IPC_PROLOGUE                                                  \
	LrgEditorTerrainPanel *self = ipc_panel (user_data, error);   \
	LrgNode               *node;                                  \
	if (self == NULL)                                             \
		return NULL;                                          \
	node = ipc_node (self, params, error);                        \
	if (node == NULL)                                             \
		return NULL

static JsonNode *
m_get (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	IPC_PROLOGUE;
	return terrain_json (self, node);
}

/* terrain.brush: sets any of the brush fields given, returns them all. */
static gboolean
apply_brush_params (LrgEditorTerrainPanel  *self,
                    JsonObject             *params,
                    gboolean                set_tool,
                    GError                **error)
{
	gdouble d;
	gint    e;

	if (set_tool && p_has (params, "tool"))
	{
		if (!enum_from_nick (LRG_TYPE_EDITOR_TERRAIN_TOOL, p_string (params, "tool"), &e))
			return bad_params (error, "Unknown \"tool\" (select raise lower flatten smooth noise paint hole fill "
			                          "foliage-paint foliage-erase)");
		lrg_editor_terrain_panel_set_tool (self, (LrgEditorTerrainTool) e);
	}
	if (p_has (params, "falloff"))
	{
		if (!enum_from_nick (LRG_TYPE_TERRAIN_BRUSH_FALLOFF, p_string (params, "falloff"), &e))
			return bad_params (error, "Unknown \"falloff\" (linear smooth sphere sharp constant curve)");
		lrg_terrain_brush_set_falloff (self->brush, (LrgTerrainBrushFalloff) e);
	}
	if (p_number (params, "radius", &d))
	{
		if (!(d > 0.0))
			return bad_params (error, "\"radius\" must be > 0");
		lrg_terrain_brush_set_radius (self->brush, (gfloat) d);
	}
	if (p_number (params, "strength", &d))
		lrg_terrain_brush_set_strength (self->brush, (gfloat) MAX (d, 0.0));
	if (p_number (params, "target_height", &d))
		lrg_terrain_brush_set_target_height (self->brush, (gfloat) d);
	if (p_has (params, "sample_target"))
		lrg_terrain_brush_set_sample_target (self->brush, p_bool (params, "sample_target", FALSE));
	if (p_number (params, "noise_scale", &d) && d > 0.0)
		lrg_terrain_brush_set_noise_scale (self->brush, (gfloat) d);
	if (p_number (params, "seed", &d))
		lrg_terrain_brush_set_seed (self->brush, (guint32) MAX (d, 0.0));
	if (p_number (params, "layer", &d))
		lrg_editor_terrain_panel_set_selected_layer (self, (gint) d);
	if (p_number (params, "foliage_layer", &d))
		lrg_editor_terrain_panel_set_selected_foliage (self, (gint) d);
	return TRUE;
}

static JsonNode *
m_brush (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	LrgEditorTerrainPanel *self = ipc_panel (user_data, error);

	if (self == NULL)
		return NULL;
	sync_widgets (self);
	if (!apply_brush_params (self, params, TRUE, error))
		return NULL;
	lrg_property_grid_refresh (self->brush_grid);
	return obj_node (brush_json (self));
}

/* {"points": [[x, z], ...], "local": false, "tool": "raise", "amount": 1,
 *  "invert": false, "smooth": false, + brush overrides} */
static JsonNode *
m_stroke (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	g_autoptr(LrgTerrainBrush) saved = NULL;
	g_autoptr(GArray)          pts = NULL;
	LrgEditorTerrainTool       tool;
	LrgEditorTerrainStrokeFlags flags = LRG_EDITOR_TERRAIN_STROKE_NONE;
	LrgComponent              *comp;
	JsonArray                 *arr;
	JsonObject                *o;
	gdouble                    amount = 1.0;
	gboolean                   local, changed;
	guint                      i, before;
	gint                       e;
	IPC_PROLOGUE;

	if (!p_has (params, "points") || !JSON_NODE_HOLDS_ARRAY (json_object_get_member (params, "points")))
	{
		bad_params (error, "Missing \"points\": [[x, z], ...]");
		return NULL;
	}
	tool = self->tool;
	if (p_has (params, "tool"))
	{
		if (!enum_from_nick (LRG_TYPE_EDITOR_TERRAIN_TOOL, p_string (params, "tool"), &e))
		{
			bad_params (error, "Unknown \"tool\"");
			return NULL;
		}
		tool = (LrgEditorTerrainTool) e;
	}
	if (tool == LRG_EDITOR_TERRAIN_TOOL_SELECT)
	{
		bad_params (error, "Give a brush \"tool\" (or pick one with terrain.brush)");
		return NULL;
	}
	p_number (params, "amount", &amount);
	if (p_bool (params, "invert", FALSE))
		flags |= LRG_EDITOR_TERRAIN_STROKE_INVERT;
	if (p_bool (params, "smooth", FALSE))
		flags |= LRG_EDITOR_TERRAIN_STROKE_SMOOTH;
	local = p_bool (params, "local", FALSE);

	/* The points: local x, z, or world x, z dropped straight down */
	comp = lrg_editor_terrain_node_component (self->editor, resolver_of (self), node);
	arr = json_object_get_array_member (params, "points");
	pts = g_array_new (FALSE, FALSE, sizeof (gfloat));
	for (i = 0; i < json_array_get_length (arr); i++)
	{
		JsonNode  *pn = json_array_get_element (arr, i);
		JsonArray *pa;
		gfloat     x, z;

		if (!JSON_NODE_HOLDS_ARRAY (pn) || json_array_get_length ((pa = json_node_get_array (pn))) < 2)
		{
			bad_params (error, "Every point is [x, z]");
			return NULL;
		}
		x = (gfloat) json_array_get_double_element (pa, 0);
		z = (gfloat) json_array_get_double_element (pa, json_array_get_length (pa) >= 3 ? 2 : 1);
		if (!local)
		{
			gfloat o3[3], d3[3] = { 0.0f, -1.0f, 0.0f }, lp[3];

			o3[0] = x;
			o3[1] = 1.0e5f;
			o3[2] = z;
			if (comp == NULL || !LRG_IS_TERRAIN_COMPONENT (comp) ||
			    !lrg_terrain_component_raycast (LRG_TERRAIN_COMPONENT (comp), o3, d3, 2.0e5f, NULL, lp))
				continue;   /* off the terrain (or over a hole) */
			x = lp[0];
			z = lp[2];
		}
		g_array_append_val (pts, x);
		g_array_append_val (pts, z);
	}
	if (pts->len < 2)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "No stroke point lies on the terrain");
		return NULL;
	}

	/* Brush overrides for this stroke only */
	saved = lrg_terrain_brush_new (lrg_terrain_brush_get_op (self->brush));
	g_object_set (saved, "radius", lrg_terrain_brush_get_radius (self->brush),
	              "strength", lrg_terrain_brush_get_strength (self->brush),
	              "falloff", lrg_terrain_brush_get_falloff (self->brush),
	              "target-height", lrg_terrain_brush_get_target_height (self->brush),
	              "sample-target", lrg_terrain_brush_get_sample_target (self->brush),
	              "noise-scale", lrg_terrain_brush_get_noise_scale (self->brush),
	              "seed", lrg_terrain_brush_get_seed (self->brush), NULL);
	if (!apply_brush_params (self, params, FALSE, error))
		return NULL;

	before = lrg_editor_get_undo_count (self->editor);
	changed = lrg_editor_terrain_panel_stroke (self, node, tool, (const gfloat *) pts->data, pts->len, (gfloat) amount,
	                                           flags);

	g_object_set (self->brush, "radius", lrg_terrain_brush_get_radius (saved),
	              "strength", lrg_terrain_brush_get_strength (saved),
	              "falloff", lrg_terrain_brush_get_falloff (saved),
	              "target-height", lrg_terrain_brush_get_target_height (saved),
	              "sample-target", lrg_terrain_brush_get_sample_target (saved),
	              "noise-scale", lrg_terrain_brush_get_noise_scale (saved),
	              "seed", lrg_terrain_brush_get_seed (saved), NULL);

	o = json_object_new ();
	json_object_set_boolean_member (o, "changed", changed);
	json_object_set_int_member (o, "dabs", pts->len / 2);
	json_object_set_string_member (o, "tool", enum_nick (LRG_TYPE_EDITOR_TERRAIN_TOOL, (gint) tool));
	if (changed && lrg_editor_get_undo_count (self->editor) > before)
	{
		g_autofree gchar *label = lrg_editor_command_describe (
			lrg_editor_get_undo_command (self->editor, lrg_editor_get_undo_count (self->editor) - 1));

		json_object_set_string_member (o, "label", label);
	}
	return obj_node (o);
}

static JsonNode *
m_sample (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgTerrainAsset *asset;
	LrgComponent    *comp;
	gdouble          x, z;
	gfloat           local[3], world[3], n[3], w[MAX_LAYERS], h = 0.0f;
	gboolean         hit;
	JsonObject      *o;
	guint            nl;
	IPC_PROLOGUE;

	if (!p_number (params, "x", &x) || !p_number (params, "z", &z))
	{
		bad_params (error, "Missing \"x\" / \"z\"");
		return NULL;
	}
	asset = asset_of (self, node, NULL);
	comp = lrg_editor_terrain_node_component (self->editor, resolver_of (self), node);
	if (p_bool (params, "local", FALSE))
	{
		local[0] = (gfloat) x;
		local[2] = (gfloat) z;
		hit = lrg_terrain_asset_sample_height (asset, local[0], local[2], &h);
		local[1] = h;
		{
			gfloat m[16];

			lrg_editor_node_get_world_matrix (node, m);
			lrg_editor_math_transform_point (m, local, world);
		}
	}
	else
	{
		gfloat o3[3], d3[3] = { 0.0f, -1.0f, 0.0f };

		o3[0] = (gfloat) x;
		o3[1] = 1.0e5f;
		o3[2] = (gfloat) z;
		hit = comp != NULL && LRG_IS_TERRAIN_COMPONENT (comp) &&
		      lrg_terrain_component_raycast (LRG_TERRAIN_COMPONENT (comp), o3, d3, 2.0e5f, world, local);
	}
	o = json_object_new ();
	json_object_set_boolean_member (o, "hit", hit);
	if (hit)
	{
		guint cx, cz, cs_x, cs_z;
		gfloat cs = lrg_terrain_asset_get_cell_size (asset);

		lrg_terrain_asset_sample_normal (asset, local[0], local[2], n);
		nl = lrg_terrain_asset_get_n_layers (asset);
		lrg_terrain_asset_sample_weights (asset, local[0], local[2], w);
		json_object_set_double_member (o, "height", (gdouble) local[1]);
		json_object_set_array_member (o, "normal", floats_array (n, 3));
		json_object_set_array_member (o, "weights", floats_array (w, nl));
		json_object_set_array_member (o, "local", floats_array (local, 3));
		json_object_set_array_member (o, "world", floats_array (world, 3));
		lrg_terrain_asset_get_cells (asset, &cs_x, &cs_z);
		cx = (guint) CLAMP (local[0] / cs, 0.0f, (gfloat) (cs_x - 1));
		cz = (guint) CLAMP (local[2] / cs, 0.0f, (gfloat) (cs_z - 1));
		json_object_set_boolean_member (o, "hole", lrg_terrain_asset_get_hole (asset, cx, cz));
		if (lrg_foliage_get_n_layers (lrg_terrain_asset_get_foliage (asset)) > 0)
		{
			gfloat dens[LRG_FOLIAGE_MAX_LAYERS];
			guint  k, nf = lrg_foliage_get_n_layers (lrg_terrain_asset_get_foliage (asset));

			for (k = 0; k < nf; k++)
				dens[k] = lrg_foliage_get_density (lrg_terrain_asset_get_foliage (asset), k, cx, cz);
			json_object_set_array_member (o, "foliage_density", floats_array (dens, nf));
		}
	}
	return obj_node (o);
}

/* A JSON member as the GValue a field setter takes: numbers, strings,
 * booleans; arrays of 2 a GrlVector2, of 3 or 4 a GrlColor (0..1). */
static gboolean
member_value (JsonObject  *params,
              const gchar *name,
              GValue      *value)
{
	JsonNode *n = json_object_get_member (params, name);

	if (JSON_NODE_HOLDS_ARRAY (n))
	{
		gfloat v[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		guint  len = p_floats (params, name, v, 4);

		if (len == 2)
			set_pair_value (value, v[0], v[1]);
		else if (len >= 3)
			set_color_value (value, v);
		else
			return FALSE;
		return TRUE;
	}
	if (JSON_NODE_HOLDS_NULL (n))
	{
		g_value_init (value, G_TYPE_STRING);
		return TRUE;
	}
	if (!JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	switch (json_node_get_value_type (n))
	{
	case G_TYPE_STRING:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, json_node_get_string (n));
		return TRUE;
	case G_TYPE_INT64:
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, (gdouble) json_node_get_int (n));
		return TRUE;
	case G_TYPE_DOUBLE:
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, json_node_get_double (n));
		return TRUE;
	default:
		return FALSE;
	}
}

static const gchar * const layer_fields[] = { "name", "texture", "material", "tiling", "color", NULL };
static const gchar * const foliage_fields[] = { "mesh", "lod_mesh", "lod_distance", "texture", "color", "density",
                                                "scale", "slope", "height", "align", "size", "fade", "seed", NULL };

/* Applies every listed field present in @params as one undo group. */
static gboolean
apply_fields (LrgEditorTerrainPanel  *self,
              LrgNode                *node,
              guint                   index,
              gboolean                foliage,
              JsonObject             *params,
              GError                **error)
{
	const gchar * const *fields = foliage ? foliage_fields : layer_fields;
	gboolean             any = FALSE;
	guint                i;

	lrg_editor_begin_group (self->editor, foliage ? "Terrain: foliage layer" : "Terrain: layer");
	for (i = 0; fields[i] != NULL; i++)
	{
		g_auto(GValue)    v = G_VALUE_INIT;
		g_autofree gchar *canon = NULL;

		if (!p_has (params, fields[i]) && !(params != NULL && json_object_has_member (params, fields[i])))
			continue;
		if (!member_value (params, fields[i], &v))
		{
			lrg_editor_end_group (self->editor);
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Bad \"%s\"",
			             fields[i]);
			return FALSE;
		}
		canon = g_strdelimit (g_strdup (fields[i]), "_", '-');
		if (!(foliage ? lrg_editor_terrain_panel_set_foliage_field (self, node, index, canon, &v)
		              : lrg_editor_terrain_panel_set_layer_field (self, node, index, canon, &v)))
		{
			lrg_editor_end_group (self->editor);
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Cannot set \"%s\" (bad value or index %u)", fields[i], index);
			return FALSE;
		}
		any = TRUE;
	}
	lrg_editor_end_group (self->editor);
	return any || TRUE;
}

static gboolean
ipc_index (JsonObject  *params,
           guint        n,
           guint       *out,
           GError     **error)
{
	gdouble d;

	if (!p_number (params, "index", &d))
		return bad_params (error, "Missing \"index\"");
	if (d < 0.0 || d >= (gdouble) n)
		return bad_params (error, "\"index\" out of range");
	*out = (guint) d;
	return TRUE;
}

static JsonNode *
with_index (gint index)
{
	JsonObject *o = json_object_new ();

	json_object_set_int_member (o, "index", index);
	return obj_node (o);
}

static JsonNode *
m_add_layer (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	gint index;
	IPC_PROLOGUE;

	lrg_editor_begin_group (self->editor, "Terrain: add layer");
	index = lrg_editor_terrain_panel_add_layer (self, node, p_string (params, "name"));
	if (index >= 0 && !apply_fields (self, node, (guint) index, FALSE, params, error))
	{
		lrg_editor_end_group (self->editor);
		return NULL;
	}
	lrg_editor_end_group (self->editor);
	if (index < 0)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "The terrain has the most layers it can (8)");
		return NULL;
	}
	return with_index (index);
}

static JsonNode *
m_remove_layer (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	JsonObject *o;
	guint       index;
	IPC_PROLOGUE;

	if (!ipc_index (params, lrg_terrain_asset_get_n_layers (asset_of (self, node, NULL)), &index, error))
		return NULL;
	if (!lrg_editor_terrain_panel_remove_layer (self, node, index))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "The last layer cannot be removed");
		return NULL;
	}
	o = json_object_new ();
	json_object_set_boolean_member (o, "removed", TRUE);
	return obj_node (o);
}

static JsonNode *
m_set_layer (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	guint index;
	IPC_PROLOGUE;

	if (!ipc_index (params, lrg_terrain_asset_get_n_layers (asset_of (self, node, NULL)), &index, error))
		return NULL;
	if (!apply_fields (self, node, index, FALSE, params, error))
		return NULL;
	return with_index ((gint) index);
}

static JsonNode *
m_add_foliage (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	gint kind = LRG_FOLIAGE_KIND_MESH;
	gint index;
	IPC_PROLOGUE;

	if (p_has (params, "kind") && !enum_from_nick (LRG_TYPE_FOLIAGE_KIND, p_string (params, "kind"), &kind))
	{
		bad_params (error, "Unknown \"kind\" (mesh billboard grass)");
		return NULL;
	}
	lrg_editor_begin_group (self->editor, "Terrain: add foliage layer");
	index = lrg_editor_terrain_panel_add_foliage (self, node, p_string (params, "name"), (LrgFoliageKind) kind);
	if (index >= 0 && !apply_fields (self, node, (guint) index, TRUE, params, error))
	{
		lrg_editor_end_group (self->editor);
		return NULL;
	}
	lrg_editor_end_group (self->editor);
	if (index < 0)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "The terrain has the most foliage layers it can");
		return NULL;
	}
	return with_index (index);
}

static JsonNode *
m_remove_foliage (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	JsonObject *o;
	guint       index;
	IPC_PROLOGUE;

	if (!ipc_index (params, lrg_foliage_get_n_layers (lrg_terrain_asset_get_foliage (asset_of (self, node, NULL))),
	                &index, error))
		return NULL;
	lrg_editor_terrain_panel_remove_foliage (self, node, index);
	o = json_object_new ();
	json_object_set_boolean_member (o, "removed", TRUE);
	return obj_node (o);
}

static JsonNode *
m_set_foliage (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	guint index;
	IPC_PROLOGUE;

	if (!ipc_index (params, lrg_foliage_get_n_layers (lrg_terrain_asset_get_foliage (asset_of (self, node, NULL))),
	                &index, error))
		return NULL;
	if (!apply_fields (self, node, index, TRUE, params, error))
		return NULL;
	return with_index ((gint) index);
}

static JsonNode *
m_rebake (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	JsonObject *o;
	IPC_PROLOGUE;

	o = json_object_new ();
	json_object_set_int_member (o, "instances", lrg_editor_terrain_panel_rebake (self, node));
	return obj_node (o);
}

static JsonNode *
m_save (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	g_autoptr(GError) local = NULL;
	g_autofree gchar *path = NULL;
	JsonObject       *o;
	IPC_PROLOGUE;

	asset_of (self, node, &path);
	if (!lrg_editor_terrain_panel_save (self, node, &local))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, local->message);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", path);
	return obj_node (o);
}

/* ==========================================================================
 * Creating terrain and water nodes
 * ========================================================================== */

/* A free path "<dir>/<stem><ext>", "<stem>2<ext>"... */
static gchar *
unique_path (const gchar *dir,
             const gchar *stem,
             const gchar *ext)
{
	guint i;

	for (i = 1; i < 10000; i++)
	{
		g_autofree gchar *name = (i == 1) ? g_strconcat (stem, ext, NULL) : g_strdup_printf ("%s%u%s", stem, i, ext);
		g_autofree gchar *path = g_build_filename (dir, name, NULL);

		if (!g_file_test (path, G_FILE_TEST_EXISTS))
			return g_steal_pointer (&path);
	}
	return g_build_filename (dir, stem, NULL);
}

/* "My Terrain" -> "my_terrain" */
static gchar *
file_stem (const gchar *name)
{
	GString *s = g_string_new (NULL);
	const gchar *p;

	for (p = (name != NULL) ? name : ""; *p != '\0'; p++)
	{
		if (g_ascii_isalnum (*p))
			g_string_append_c (s, g_ascii_tolower (*p));
		else if (s->len > 0 && s->str[s->len - 1] != '_')
			g_string_append_c (s, '_');
	}
	while (s->len > 0 && s->str[s->len - 1] == '_')
		g_string_truncate (s, s->len - 1);
	if (s->len == 0)
		g_string_append (s, "terrain");
	return g_string_free (s, FALSE);
}

gboolean
lrg_editor_terrain_init_node (LrgEditorApp  *app,
                              LrgNode       *node,
                              GError       **error)
{
	LrgNodeVisual    *visual;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *stem = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *rel = NULL;
	gboolean          terrain;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	visual = lrg_node_get_visual (node);
	if (visual == NULL || (lrg_node_visual_get_kind (visual) != LRG_NODE_VISUAL_TERRAIN &&
	                       lrg_node_visual_get_kind (visual) != LRG_NODE_VISUAL_WATER))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Not a terrain or water node");
		return FALSE;
	}
	terrain = lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_TERRAIN;
	dir = editor_app_project_path (app, "terrain");
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot create %s", dir);
		return FALSE;
	}
	stem = file_stem (lrg_node_get_name (node));
	path = unique_path (dir, stem, terrain ? ".rterrain" : ".rwater");

	if (terrain)
	{
		g_autoptr(LrgTerrainAsset) asset = lrg_terrain_asset_new (32, 32.0f, 4, 4);
		LrgTerrainLayer           *rock;

		lrg_terrain_asset_set_name (asset, stem);
		lrg_terrain_layer_set_name (lrg_terrain_asset_get_layer (asset, 0), "grass");
		lrg_terrain_layer_set_color (lrg_terrain_asset_get_layer (asset, 0), 0.42f, 0.60f, 0.30f, 1.0f);
		rock = lrg_terrain_asset_add_layer (asset, "rock");
		if (rock != NULL)
			lrg_terrain_layer_set_color (rock, 0.52f, 0.50f, 0.48f, 1.0f);
		if (!lrg_terrain_asset_save_to_file (asset, path, error))
			return FALSE;
		/* a 128 x 128 terrain centred on the node's spot */
		{
			GrlVector3 *loc = lrg_node_get_location (node);

			lrg_node_set_location_xyz (node, loc->x - 64.0f, loc->y, loc->z - 64.0f);
		}
	}
	else
	{
		g_autoptr(LrgWaterBody) body = lrg_water_body_new (LRG_WATER_BODY_LAKE);
		LrgWaterWave            wave = { { 1.0f, 0.3f }, 0.08f, 6.0f, 1.2f, 0.35f };

		lrg_water_body_set_name (body, stem);
		lrg_water_body_add_point (body, -10.0f, 0.0f, -10.0f, 4.0f);
		lrg_water_body_add_point (body, 10.0f, 0.0f, -10.0f, 4.0f);
		lrg_water_body_add_point (body, 12.0f, 0.0f, 8.0f, 4.0f);
		lrg_water_body_add_point (body, -8.0f, 0.0f, 10.0f, 4.0f);
		lrg_water_body_set_level (body, 0.5f);
		lrg_water_body_set_wave_count (body, 1);
		lrg_water_body_set_wave (body, 0, &wave);
		if (!lrg_water_body_save_to_file (body, path, error))
			return FALSE;
	}
	rel = editor_app_relative_path (app, path);
	lrg_node_visual_set_asset (visual, rel);
	editor_app_set_status (app, "Created %s", rel);
	return TRUE;
}

/* A new terrain / water node with its asset, under the selection's
 * parent or at the root. */
static LrgNode *
create_kind (LrgEditorApp      *app,
             LrgNodeVisualKind  kind,
             const gchar       *name,
             GError           **error)
{
	LrgNode *node = editor_app_create_node (app, name, kind, 0, NULL);

	if (node == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot create the node");
		return NULL;
	}
	if (!lrg_editor_terrain_init_node (app, node, error))
		return NULL;
	if (lrg_editor_get_level (app->editor) != NULL)
		lrg_level_notify_node_changed (lrg_editor_get_level (app->editor), node);
	return node;
}

static JsonNode *
m_create (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp     *app = LRG_EDITOR_APP (user_data);
	const gchar      *kind = p_string (params, "kind");
	const gchar      *name = p_string (params, "name");
	g_autoptr(GError) local = NULL;
	gboolean          water = g_strcmp0 (kind, "water") == 0;
	LrgNode          *node;
	JsonObject       *o;

	if (kind != NULL && !water && g_strcmp0 (kind, "terrain") != 0)
	{
		bad_params (error, "\"kind\" is terrain or water");
		return NULL;
	}
	if (lrg_editor_get_level (app->editor) == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No level open");
		return NULL;
	}
	node = create_kind (app, water ? LRG_NODE_VISUAL_WATER : LRG_NODE_VISUAL_TERRAIN,
	                    name != NULL ? name : (water ? "Water" : "Terrain"), &local);
	if (node == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, local->message);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "guid", lrg_node_get_guid (node));
	json_object_set_string_member (o, "name", lrg_node_get_name (node));
	json_object_set_string_member (o, "asset", lrg_node_visual_get_asset (lrg_node_get_visual (node)));
	return obj_node (o);
}

static JsonNode *
m_show (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);
	JsonObject   *o;

	editor_app_show_panel (app, LRG_EDITOR_TERRAIN_PANEL_ID);
	o = json_object_new ();
	json_object_set_string_member (o, "panel", LRG_EDITOR_TERRAIN_PANEL_ID);
	json_object_set_boolean_member (o, "shown", editor_app_panel_is_shown (app, LRG_EDITOR_TERRAIN_PANEL_ID));
	return obj_node (o);
}

/* ==========================================================================
 * Actions
 * ========================================================================== */

static void
a_show (gpointer data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (data);

	if (self->app != NULL)
		editor_app_show_panel (self->app, LRG_EDITOR_TERRAIN_PANEL_ID);
}

static void
a_create_terrain (gpointer data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (data);
	g_autoptr(GError)      error = NULL;

	if (self->app == NULL || lrg_editor_get_level (self->editor) == NULL)
		return;
	if (create_kind (self->app, LRG_NODE_VISUAL_TERRAIN, "Terrain", &error) == NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Create terrain: %s", error->message);
	else
		a_show (data);
}

static void
a_create_water (gpointer data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (data);
	g_autoptr(GError)      error = NULL;

	if (self->app == NULL || lrg_editor_get_level (self->editor) == NULL)
		return;
	if (create_kind (self->app, LRG_NODE_VISUAL_WATER, "Water", &error) == NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Create water: %s", error->message);
}

static void
a_save (gpointer data)
{
	on_save (NULL, data);
}

static void
a_rebake (gpointer data)
{
	lrg_editor_terrain_panel_rebake (LRG_EDITOR_TERRAIN_PANEL (data), NULL);
}

static gboolean
has_level (gpointer data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (data);

	return self->editor != NULL && lrg_editor_get_level (self->editor) != NULL;
}

static gboolean
has_terrain (gpointer data)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (data);

	return asset_of (self, primary (self), NULL) != NULL;
}

/* terrain.tool-<nick>: one action per tool (rebindable shortcuts). The
 * action data is a ToolAction kept on the app, so it outlives a panel
 * that is rebuilt. */
typedef struct
{
	LrgEditorApp         *app;   /* the owner (not referenced) */
	LrgEditorTerrainTool  tool;
} ToolAction;

static void
a_tool (gpointer data)
{
	ToolAction            *ta = data;
	LrgEditorTerrainPanel *self = lrg_editor_terrain_panel_find (ta->app);

	if (self != NULL)
		lrg_editor_terrain_panel_set_tool (self, ta->tool);
}

static gboolean
tool_enabled (gpointer data)
{
	ToolAction            *ta = data;
	LrgEditorTerrainPanel *self = lrg_editor_terrain_panel_find (ta->app);

	return self != NULL && has_terrain (self);
}

static gboolean
tool_checked (gpointer data)
{
	ToolAction            *ta = data;
	LrgEditorTerrainPanel *self = lrg_editor_terrain_panel_find (ta->app);

	return self != NULL && self->tool == ta->tool;
}

/* ==========================================================================
 * GObject / widget
 * ========================================================================== */

static void
lrg_editor_terrain_panel_layout_children (LrgContainer *container)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (container);

	editor_panel_fit_toolbar (self->header_row, lrg_widget_get_width (LRG_WIDGET (self)) - 20.0f);
	editor_panel_place_column (container, self->top, self->scroll, NULL);
	editor_panel_place_column (container, self->top, self->empty, NULL);
}

static void
lrg_editor_terrain_panel_dispose (GObject *object)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (object);
	guint                  i;

	if (self->stroking)
		lrg_editor_terrain_panel_end_stroke (self, FALSE);
	if (self->editor != NULL)
		g_signal_handlers_disconnect_by_data (self->editor, self);
	if (self->selection != NULL)
		g_signal_handlers_disconnect_by_data (self->selection, self);
	if (self->play != NULL)
		g_signal_handlers_disconnect_by_data (self->play, self);
	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), PANEL_KEY) == self)
		g_object_set_data (G_OBJECT (self->app), PANEL_KEY, NULL);
	g_clear_object (&self->editor);
	g_clear_object (&self->selection);
	g_clear_object (&self->play);
	g_clear_object (&self->brush);
	g_clear_weak_pointer (&self->app);
	g_clear_weak_pointer (&self->node);
	g_clear_weak_pointer (&self->water_node);
	g_clear_pointer (&self->water_before, g_array_unref);
	for (i = 0; i < 3; i++)
		g_clear_object (&self->src[i]);
	G_OBJECT_CLASS (lrg_editor_terrain_panel_parent_class)->dispose (object);
}

static void
lrg_editor_terrain_panel_finalize (GObject *object)
{
	LrgEditorTerrainPanel *self = LRG_EDITOR_TERRAIN_PANEL (object);

	g_free (self->signature);
	g_free (self->stroke_path);
	G_OBJECT_CLASS (lrg_editor_terrain_panel_parent_class)->finalize (object);
}

static void
lrg_editor_terrain_panel_class_init (LrgEditorTerrainPanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_terrain_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_terrain_panel_finalize;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_terrain_panel_layout_children;
}

static gpointer
add_to (gpointer box,
        gpointer child)
{
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (child));
	g_object_unref (child);
	return child;
}

static LrgPropertyGrid *
make_grid (LrgEditorTerrainPanel *self,
           const gchar           *name,
           Part                   part)
{
	LrgPropertyGrid *grid = lrg_property_grid_new ();

	lrg_widget_set_name (LRG_WIDGET (grid), name);
	lrg_property_grid_set_label_width (grid, 112.0f);
	g_signal_connect (grid, "edit-begin", G_CALLBACK (on_edit_begin), self);
	g_signal_connect (grid, "edit-end", G_CALLBACK (on_edit_end), self);
	lrg_property_grid_set_source (grid, LRG_PROPERTY_SOURCE (self->src[part]));
	return grid;
}

static LrgWidget *
heading_row (const gchar *name,
             const gchar *text)
{
	LrgWidget *row = LRG_WIDGET (lrg_hbox_new ());
	LrgWidget *label = LRG_WIDGET (lrg_label_new (text));

	lrg_widget_set_name (row, name);
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	lrg_widget_add_class (label, "heading");
	lrg_widget_set_valign (label, LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (label, 1.0f);
	add_to (row, label);
	return row;
}

static void
lrg_editor_terrain_panel_init (LrgEditorTerrainPanel *self)
{
	static const LrgCustomWidgetFuncs tool_funcs = { tools_measure, tools_paint, NULL, tools_event, { NULL } };
	static const LrgCustomWidgetFuncs list_funcs = { list_measure, list_paint, NULL, list_event, { NULL } };
	static const LrgCustomWidgetFuncs info_funcs = { info_measure, info_paint, NULL, NULL, { NULL } };
	static const gchar * const kinds[] = { "Trees (mesh)", "Cards (billboard)", "Grass" };
	const LrgInsets pad = { 8.0f, 6.0f, 8.0f, 4.0f };
	LrgWidget      *row;
	LrgWidget      *label;
	LrgWidget      *chip;
	guint           i;

	self->tool = LRG_EDITOR_TERRAIN_TOOL_SELECT;
	self->sel_layer = -1;
	self->sel_foliage = -1;
	self->water_hot = -1;
	self->water_drag = -1;
	self->brush = lrg_terrain_brush_new (LRG_TERRAIN_BRUSH_RAISE);
	lrg_terrain_brush_set_radius (self->brush, DEFAULT_RADIUS);
	lrg_terrain_brush_set_strength (self->brush, DEFAULT_STRENGTH);
	self->src[PART_BRUSH] = editor_terrain_source_new (self, PART_BRUSH);
	self->src[PART_LAYER] = editor_terrain_source_new (self, PART_LAYER);
	self->src[PART_FOLIAGE] = editor_terrain_source_new (self, PART_FOLIAGE);

	/* Header: title, node name, Rebake, Save */
	self->top = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->top, "terrain-top");
	lrg_container_set_spacing (LRG_CONTAINER (self->top), 6.0f);
	lrg_widget_set_margin (self->top, &pad);
	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (row, "terrain-header");
	self->header_row = row;
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	label = LRG_WIDGET (lrg_label_new ("Terrain"));
	lrg_widget_set_name (label, "terrain-title");
	lrg_widget_add_class (label, "heading");
	lrg_widget_set_valign (label, LRG_ALIGN_CENTER);
	add_to (row, label);
	self->node_label = lrg_label_new ("");
	lrg_widget_set_name (LRG_WIDGET (self->node_label), "terrain-node");
	lrg_widget_add_class (LRG_WIDGET (self->node_label), "dim");
	lrg_widget_set_valign (LRG_WIDGET (self->node_label), LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->node_label), 1.0f);
	add_to (row, self->node_label);
	self->rebake_chip = editor_chip_new ("terrain-rebake", LRG_ICON_REFRESH, "Rebake", on_rebake, self);
	add_to (row, self->rebake_chip);
	self->save_chip = editor_chip_new ("terrain-save", LRG_ICON_DOWNLOAD, "Save", on_save, self);
	editor_chip_set_tint (self->save_chip, LRG_THEME_COLOR_ACCENT);
	add_to (row, self->save_chip);
	add_to (self->top, row);
	lrg_container_add_child (LRG_CONTAINER (self), self->top);
	g_object_unref (self->top);

	/* Content */
	self->content = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->content, "terrain-content");
	lrg_container_set_spacing (LRG_CONTAINER (self->content), 6.0f);
	lrg_widget_set_margin (self->content, &pad);

	/* Tools and the brush */
	add_to (self->content, heading_row ("terrain-tools-heading", "Tools"));
	self->tools = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->tools, "terrain-tools");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->tools), &tool_funcs, self, NULL);
	add_to (self->content, self->tools);
	add_to (self->content, heading_row ("terrain-brush-heading", "Brush"));
	self->brush_grid = add_to (self->content, make_grid (self, "terrain-brush-grid", PART_BRUSH));

	/* Surface layers */
	row = heading_row ("terrain-layers-heading", "Surface layers");
	self->add_layer_chip = editor_chip_new ("terrain-add-layer", LRG_ICON_PLUS, "Add", on_add_layer, self);
	add_to (row, self->add_layer_chip);
	add_to (self->content, row);
	self->layer_list = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->layer_list, "terrain-layer-list");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->layer_list), &list_funcs, self, NULL);
	add_to (self->content, self->layer_list);
	self->layer_grid = add_to (self->content, make_grid (self, "terrain-layer-grid", PART_LAYER));

	/* Foliage */
	row = heading_row ("terrain-foliage-heading", "Foliage");
	self->foliage_kind = lrg_combo_box_new ();
	for (i = 0; i < G_N_ELEMENTS (kinds); i++)
		lrg_combo_box_append (self->foliage_kind, kinds[i]);
	lrg_combo_box_set_selected (self->foliage_kind, 0);
	lrg_widget_set_name (LRG_WIDGET (self->foliage_kind), "terrain-foliage-kind");
	lrg_widget_set_valign (LRG_WIDGET (self->foliage_kind), LRG_ALIGN_CENTER);
	lrg_widget_set_max_size (LRG_WIDGET (self->foliage_kind), 150.0f, 0.0f);
	add_to (row, self->foliage_kind);
	chip = editor_chip_new ("terrain-add-foliage", LRG_ICON_PLUS, "Add", on_add_foliage, self);
	add_to (row, chip);
	add_to (self->content, row);
	self->foliage_list = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->foliage_list, "terrain-foliage-list");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->foliage_list), &list_funcs, self, NULL);
	add_to (self->content, self->foliage_list);
	self->foliage_grid = add_to (self->content, make_grid (self, "terrain-foliage-grid", PART_FOLIAGE));

	/* Chunk grid */
	add_to (self->content, heading_row ("terrain-grid-heading", "Chunk grid"));
	self->info = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->info, "terrain-info");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->info), &info_funcs, self, NULL);
	add_to (self->content, self->info);

	self->scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (self->scroll, "terrain-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (self->scroll), self->content);
	g_object_unref (self->content);
	lrg_container_add_child (LRG_CONTAINER (self), self->scroll);
	g_object_unref (self->scroll);

	/* Empty state */
	self->empty = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->empty, "terrain-empty");
	lrg_container_set_spacing (LRG_CONTAINER (self->empty), 10.0f);
	lrg_widget_set_margin (self->empty, &pad);
	self->hint_none = add_to (self->empty, editor_panel_hint_new (
		"terrain-hint", "Select a terrain node to sculpt, paint and scatter foliage on it, or add one with "
		"Create > Terrain. Selecting a water lake or river shows its outline handles in the viewport."));
	self->hint_bad = add_to (self->empty, editor_panel_hint_new (
		"terrain-hint-missing", "This terrain node's .rterrain does not load. Point its asset at a terrain "
		"file in the Inspector, or create one with Create > Terrain."));
	lrg_container_add_child (LRG_CONTAINER (self), self->empty);
	g_object_unref (self->empty);
}

static void
on_play (LrgEditorTerrainPanel *self)
{
	if (self->stroking)
		lrg_editor_terrain_panel_end_stroke (self, TRUE);
	sync_widgets (self);
}

LrgEditorTerrainPanel *
lrg_editor_terrain_panel_new (LrgEditorApp *app)
{
	LrgEditorTerrainPanel *self;
	LrgEditorActions      *actions;
	guint                  i;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_TERRAIN_PANEL, NULL);
	lrg_widget_set_name (LRG_WIDGET (self), "terrain-panel");
	g_set_weak_pointer (&self->app, app);
	self->editor = g_object_ref (lrg_editor_app_get_editor (app));
	self->selection = g_object_ref (lrg_editor_get_selection (self->editor));
	self->play = (app->play != NULL) ? g_object_ref (app->play) : NULL;
	g_object_set_data (G_OBJECT (app), PANEL_KEY, self);
	store_of (self->editor);

	g_signal_connect_swapped (self->editor, "changed", G_CALLBACK (on_changed), self);
	g_signal_connect_swapped (self->editor, "level-replaced", G_CALLBACK (on_level_replaced), self);
	g_signal_connect_swapped (self->selection, "changed", G_CALLBACK (on_changed), self);
	if (self->play != NULL)
	{
		g_signal_connect_swapped (self->play, "started", G_CALLBACK (on_play), self);
		g_signal_connect_swapped (self->play, "stopped", G_CALLBACK (on_play), self);
	}

	/* Actions and IPC from this file */
	actions = lrg_editor_app_get_actions (app);
	lrg_editor_actions_add (actions, "terrain.show", "Terrain Panel", "View", NULL, a_show, NULL, NULL, self);
	lrg_editor_actions_add (actions, "terrain.create", "Terrain", "Create", NULL, a_create_terrain, has_level, NULL,
	                        self);
	lrg_editor_actions_add (actions, "terrain.create-water", "Water", "Create", NULL, a_create_water, has_level, NULL,
	                        self);
	lrg_editor_actions_add (actions, "terrain.save", "Save Terrain", "Terrain", NULL, a_save, has_terrain, NULL, self);
	lrg_editor_actions_add (actions, "terrain.rebake", "Rebake Terrain", "Terrain", NULL, a_rebake, has_terrain, NULL,
	                        self);
	{
		ToolAction *tools = g_object_get_data (G_OBJECT (app), "lrg-editor-terrain-tools");

		if (tools == NULL)
		{
			tools = g_new0 (ToolAction, N_TOOLS);
			g_object_set_data_full (G_OBJECT (app), "lrg-editor-terrain-tools", tools, g_free);
		}
		for (i = 0; i < N_TOOLS; i++)
		{
			const gchar      *nick = enum_nick (LRG_TYPE_EDITOR_TERRAIN_TOOL, (gint) i);
			g_autofree gchar *id = g_strdup_printf ("terrain.tool-%s", nick);
			g_autofree gchar *label = g_strdup_printf ("Terrain Tool: %s", tool_table[i].label);

			tools[i].app = app;
			tools[i].tool = (LrgEditorTerrainTool) i;
			lrg_editor_actions_add (actions, id, label, "Terrain", NULL, a_tool, tool_enabled, tool_checked,
			                        &tools[i]);
		}
	}
	if (app->server != NULL)
	{
		lrg_editor_server_register_method (app->server, "terrain.get", m_get, app, NULL);
		lrg_editor_server_register_method (app->server, "terrain.brush", m_brush, app, NULL);
		lrg_editor_server_register_method (app->server, "terrain.stroke", m_stroke, app, NULL);
		lrg_editor_server_register_method (app->server, "terrain.sample", m_sample, app, NULL);
		lrg_editor_server_register_method (app->server, "terrain.add_layer", m_add_layer, app, NULL);
		lrg_editor_server_register_method (app->server, "terrain.remove_layer", m_remove_layer, app, NULL);
		lrg_editor_server_register_method (app->server, "terrain.set_layer", m_set_layer, app, NULL);
		lrg_editor_server_register_method (app->server, "terrain.add_foliage", m_add_foliage, app, NULL);
		lrg_editor_server_register_method (app->server, "terrain.remove_foliage", m_remove_foliage, app, NULL);
		lrg_editor_server_register_method (app->server, "terrain.set_foliage", m_set_foliage, app, NULL);
		lrg_editor_server_register_method (app->server, "terrain.rebake", m_rebake, app, NULL);
		lrg_editor_server_register_method (app->server, "terrain.save", m_save, app, NULL);
		lrg_editor_server_register_method (app->server, "terrain.create", m_create, app, NULL);
		lrg_editor_server_register_method (app->server, "terrain.show", m_show, app, NULL);
	}
	lrg_editor_water_editor_install (app);

	sync_widgets (self);
	return self;
}

LrgEditorTerrainPanel *
lrg_editor_terrain_panel_find (LrgEditorApp *app)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	return g_object_get_data (G_OBJECT (app), PANEL_KEY);
}
