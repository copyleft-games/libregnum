/* test-editor-app-material.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The 0.9.0 material and shader tools of lrgeditor, in a headless
 * LrgEditorApp (tests/editor-app-fixture.h): the `.rmat` material editor
 * (grid edits as document undo steps, instances with override markers
 * and revert, the uniform inspector of custom shading, preview chips,
 * assignment to nodes), the `.rshadergraph` editor (palette, canvas
 * wires, validation on the cards, undo, save writing the generated
 * `.rshader`), the assets panel (New Material / Shader / Shader Graph,
 * Create Variant for materials and prefabs, double-click routing, LOD
 * import keys) and the code panel's shader support (YAML/GLSL syntax,
 * diagnostics strip and problems after a save), plus every material.*,
 * shader.* and shadergraph.* IPC method.
 *
 * The /gpu/ cases need a display (only ever the isolated Xvfb of
 * xrun.sh): they open a hidden window and check the rendered previews,
 * thumbnails and real GLSL compile diagnostics mapped to file lines.
 */

#include "editor-app-fixture.h"
#include <raylib.h>

/* ==========================================================================
 * Fixtures and helpers
 * ========================================================================== */

static const gchar *BASE_RMAT =
	"version: 1\n"
	"name: base\n"
	"base_color: [0.2, 0.4, 0.6, 1]\n"
	"metallic: 0.25\n"
	"roughness: 0.75\n";

static const gchar *RED_RMAT =
	"version: 1\n"
	"name: red\n"
	"parent: base.rmat\n"
	"base_color: [1, 0, 0, 1]\n";

static const gchar *TINT_FRAG =
	"void main()\n"
	"{\n"
	"    finalColor = vec4(tint.rgb * glow, 1.0);\n"
	"}\n";

static const gchar *TINT_RSHADER =
	"version: 1\n"
	"name: tint\n"
	"fragment: tint.frag\n"
	"uniforms:\n"
	"  tint:\n"
	"    type: color\n"
	"    default: [0, 1, 0, 1]\n"
	"    hint: color\n"
	"  glow:\n"
	"    type: float\n"
	"    default: 1\n"
	"    range: [0, 4]\n"
	"    hint: slider\n"
	"  noise_tex:\n"
	"    type: sampler2D\n"
	"  secret:\n"
	"    type: float\n"
	"    hint: hidden\n";

static const gchar *CUSTOM_RMAT =
	"version: 1\n"
	"name: custom\n"
	"shading_model: custom\n"
	"shader: tint.rshader\n";

/* A project with @rel holding @text; returns the absolute path. */
static gchar *
write_project_file (EditorAppFixture *f,
                    const gchar      *rel,
                    const gchar      *text)
{
	g_autoptr(GError) error = NULL;
	gchar            *path;
	g_autofree gchar *dir = NULL;

	if (lrg_editor_app_get_project_dir (f->app) == NULL)
	{
		g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Demo", &error));
		g_assert_no_error (error);
	}
	path = g_build_filename (f->project, rel, NULL);
	dir = g_path_get_dirname (path);
	g_mkdir_with_parents (dir, 0755);
	g_assert_true (g_file_set_contents (path, text, -1, &error));
	g_assert_no_error (error);
	return path;
}

static gchar *
read_file (const gchar *path)
{
	gchar *text = NULL;

	g_assert_true (g_file_get_contents (path, &text, NULL, NULL));
	return text;
}

static gchar *
doc_text (LrgEditorDataEditor *editor)
{
	return lrg_editor_document_to_data (lrg_editor_data_editor_get_document (editor), NULL);
}

static LrgEditorMaterialEditor *
open_material (EditorAppFixture *f,
               const gchar      *rel)
{
	g_autoptr(GError)        error = NULL;
	LrgEditorMaterialEditor *ed = lrg_editor_material_editor_open (f->app, rel, &error);

	g_assert_no_error (error);
	g_assert_nonnull (ed);
	efx_frames (f, 2);
	return ed;
}

static LrgPropertyGrid *
material_grid (LrgEditorMaterialEditor *ed)
{
	LrgWidget *w = lrg_widget_find (LRG_WIDGET (ed), "material-grid");

	g_assert_true (LRG_IS_PROPERTY_GRID (w));
	return LRG_PROPERTY_GRID (w);
}

/* Clicks a toolbar chip through the canvas. */
static void
editor_chip_click (EditorAppFixture *f,
                   LrgWidget        *chip)
{
	efx_frame (f);
	efx_click_widget (f, chip);
}

/* Loader warnings from parsing broken documents are expected. */
static gboolean
loader_warnings_ok (const gchar    *domain,
                    GLogLevelFlags  level,
                    const gchar    *message,
                    gpointer        user_data)
{
	return !(domain != NULL && g_str_has_prefix (domain, "Libregnum-") && (level & G_LOG_LEVEL_WARNING) &&
	         g_strcmp0 (domain, "Libregnum-Editor") != 0);
}

/* ==========================================================================
 * Material editor
 * ========================================================================== */

/* .rmat opens in the material editor (through the data editor registry),
 * the preview material parses, and the grid shows every section. */
static void
test_material_open (EditorAppFixture *f,
                    gconstpointer     data)
{
	g_autofree gchar        *path = write_project_file (f, "materials/base.rmat", BASE_RMAT);
	g_autoptr(GError)        error = NULL;
	LrgEditorDataEditor     *e;
	LrgEditorMaterialEditor *ed;
	LrgPropertyGrid         *grid;
	g_autofree gchar        *state = NULL;

	g_assert_true (lrg_editor_data_editor_can_open (path));
	e = lrg_editor_data_editor_open (f->app, "materials/base.rmat", &error);
	g_assert_no_error (error);
	g_assert_true (LRG_IS_EDITOR_MATERIAL_EDITOR (e));
	g_assert_cmpint (lrg_editor_document_get_format (lrg_editor_data_editor_get_document (e)), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_MATERIAL);
	ed = LRG_EDITOR_MATERIAL_EDITOR (e);
	efx_frames (f, 2);

	g_assert_nonnull (lrg_editor_material_editor_get_material (ed));
	g_assert_cmpfloat_with_epsilon (lrg_material_get_metallic (lrg_editor_material_editor_get_material (ed)), 0.25f,
	                                1e-4);
	grid = material_grid (ed);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "base-color"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "tex-albedo"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "blend"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "render-queue"));
	g_assert_null (lrg_property_grid_get_row (grid, "u-tint"));

	/* no parent: nothing is an override, and Revert All hides */
	g_assert_false (lrg_editor_material_editor_is_overridden (ed, "metallic"));
	g_assert_false (lrg_widget_get_visible (lrg_widget_find (LRG_WIDGET (ed), "material-revert-all")));
	state = lrg_editor_data_editor_get_state (e);
	g_assert_nonnull (strstr (state, "shape=sphere"));
	g_assert_nonnull (strstr (state, "model=lit"));
	g_assert_cmpuint (lrg_editor_data_editor_validate (e), ==, 0);

	/* the shader graph and post formats are recognised too */
	g_assert_cmpint (lrg_editor_document_detect_format ("x.rshadergraph", NULL, 0), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_SHADER_GRAPH);
	g_assert_cmpint (lrg_editor_document_detect_format ("x.rpost", NULL, 0), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_POST_STACK);
	g_assert_cmpstr (lrg_editor_document_format_to_string (LRG_EDITOR_DOCUMENT_FORMAT_MATERIAL), ==, "material");
}

/* A grid gesture (begin, several values, end) is one document undo step;
 * a cancelled gesture leaves nothing. */
static void
test_material_grid_edit (EditorAppFixture *f,
                         gconstpointer     data)
{
	g_autofree gchar        *path = write_project_file (f, "materials/base.rmat", BASE_RMAT);
	LrgEditorMaterialEditor *ed = open_material (f, "materials/base.rmat");
	LrgEditorDataEditor     *e = LRG_EDITOR_DATA_EDITOR (ed);
	LrgPropertyRow          *row = lrg_property_grid_get_row (material_grid (ed), "metallic");
	LrgPropertyRow          *color = lrg_property_grid_get_row (material_grid (ed), "base-color");
	g_auto(GValue)           v = G_VALUE_INIT;
	g_autofree gchar        *text = NULL;
	GrlColor                 red = { 255, 0, 0, 255 };

	g_assert_nonnull (row);
	g_value_init (&v, G_TYPE_DOUBLE);

	/* a drag: three values, one step */
	lrg_property_row_begin_edit (row);
	g_value_set_double (&v, 0.4);
	lrg_property_row_edit_value (row, &v, TRUE);
	g_value_set_double (&v, 0.6);
	lrg_property_row_edit_value (row, &v, TRUE);
	g_value_set_double (&v, 0.8);
	lrg_property_row_edit_value (row, &v, TRUE);
	lrg_property_row_end_edit (row, TRUE);
	efx_frame (f);
	text = doc_text (e);
	g_assert_nonnull (strstr (text, "metallic: 0.8"));
	g_assert_cmpfloat_with_epsilon (lrg_material_get_metallic (lrg_editor_material_editor_get_material (ed)), 0.8f,
	                                1e-4);
	g_assert_true (lrg_editor_data_editor_get_dirty (e));
	g_assert_true (lrg_editor_data_editor_undo (e));
	g_clear_pointer (&text, g_free);
	text = doc_text (e);
	g_assert_nonnull (strstr (text, "metallic: 0.25"));
	g_assert_false (lrg_editor_data_editor_get_dirty (e));

	/* a cancelled drag leaves the document as it was */
	lrg_property_row_begin_edit (row);
	g_value_set_double (&v, 0.1);
	lrg_property_row_edit_value (row, &v, TRUE);
	lrg_property_row_end_edit (row, FALSE);
	g_clear_pointer (&text, g_free);
	text = doc_text (e);
	g_assert_nonnull (strstr (text, "metallic: 0.25"));

	/* a colour is a sequence */
	{
		g_auto(GValue) c = G_VALUE_INIT;

		g_value_init (&c, GRL_TYPE_COLOR);
		g_value_set_boxed (&c, &red);
		lrg_property_row_apply_value (color, &c);
	}
	g_clear_pointer (&text, g_free);
	text = doc_text (e);
	{
		gfloat c[4];

		lrg_material_get_base_color (lrg_editor_material_editor_get_material (ed), c);
		g_assert_cmpfloat_with_epsilon (c[0], 1.0f, 1e-3);
		g_assert_cmpfloat_with_epsilon (c[1], 0.0f, 1e-3);
		g_assert_cmpfloat_with_epsilon (c[3], 1.0f, 1e-3);
	}

	/* nested keys create their mapping */
	g_assert_true (lrg_editor_material_editor_set_key (ed, "render_state.blend", "alpha", NULL));
	g_assert_true (lrg_editor_material_editor_set_key (ed, "textures.albedo", "\"albedo.png\"", NULL));
	g_assert_cmpint (lrg_material_get_blend_mode (lrg_editor_material_editor_get_material (ed)), ==,
	                 LRG_MATERIAL_BLEND_ALPHA);
	g_assert_cmpstr (lrg_material_get_texture_ref (lrg_editor_material_editor_get_material (ed),
	                                               LRG_MATERIAL_SLOT_ALBEDO), ==, "albedo.png");
	/* the missing texture is a warning at its key */
	g_assert_cmpuint (lrg_editor_data_editor_validate (e), ==, 0);
	g_assert_cmpuint (lrg_editor_data_editor_get_issues (e)->len, ==, 1);

	/* save writes the file */
	g_assert_true (lrg_editor_data_editor_save (e, NULL));
	g_clear_pointer (&text, g_free);
	text = read_file (path);
	g_assert_nonnull (strstr (text, "blend: alpha"));
}

/* An instance: overrides marked, inherited values shown, revert and
 * Revert All (one step), live parent edits from another editor. */
static void
test_material_instance (EditorAppFixture *f,
                        gconstpointer     data)
{
	g_autofree gchar        *base = write_project_file (f, "materials/base.rmat", BASE_RMAT);
	g_autofree gchar        *red = write_project_file (f, "materials/red.rmat", RED_RMAT);
	LrgEditorMaterialEditor *ed = open_material (f, "materials/red.rmat");
	LrgEditorMaterialEditor *parent;
	LrgPropertyGrid         *grid = material_grid (ed);
	g_auto(GValue)           v = G_VALUE_INIT;
	g_auto(GStrv)            keys = NULL;
	g_autofree gchar        *text = NULL;

	g_assert_true (lrg_editor_material_editor_is_overridden (ed, "base_color"));
	g_assert_false (lrg_editor_material_editor_is_overridden (ed, "metallic"));
	g_assert_cmpuint (lrg_property_row_get_state (lrg_property_grid_get_row (grid, "base-color")) &
	                  LRG_PROPERTY_STATE_OVERRIDDEN, !=, 0);
	g_assert_cmpuint (lrg_property_row_get_state (lrg_property_grid_get_row (grid, "metallic")) &
	                  LRG_PROPERTY_STATE_DEFAULT, !=, 0);

	/* the inherited value is what the grid shows */
	g_assert_true (lrg_property_row_get_value (lrg_property_grid_get_row (grid, "metallic"), &v));
	g_assert_cmpfloat_with_epsilon (g_value_get_double (&v), 0.25, 1e-4);
	g_assert_true (lrg_widget_get_visible (lrg_widget_find (LRG_WIDGET (ed), "material-revert-all")));

	/* override two more, then revert one and Revert All */
	g_assert_true (lrg_editor_material_editor_set_key (ed, "metallic", "1", NULL));
	g_assert_true (lrg_editor_material_editor_set_key (ed, "blend", "additive", NULL));
	keys = lrg_editor_material_editor_list_overrides (ed);
	g_assert_cmpuint (g_strv_length (keys), ==, 3);
	g_assert_true (lrg_editor_material_editor_revert_key (ed, "metallic"));
	g_assert_false (lrg_editor_material_editor_revert_key (ed, "metallic"));
	g_assert_cmpfloat_with_epsilon (lrg_material_get_metallic (lrg_editor_material_editor_get_material (ed)), 0.25f,
	                                1e-4);
	g_assert_cmpuint (lrg_editor_material_editor_revert_all (ed), ==, 2);
	text = doc_text (LRG_EDITOR_DATA_EDITOR (ed));
	g_assert_null (strstr (text, "base_color"));
	g_assert_null (strstr (text, "render_state"));
	g_assert_nonnull (strstr (text, "parent: base.rmat"));
	/* Revert All was one undo step */
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	g_assert_true (lrg_editor_material_editor_is_overridden (ed, "base_color"));
	g_assert_true (lrg_editor_material_editor_is_overridden (ed, "render_state.blend"));

	/* grid reset reverts */
	lrg_property_grid_reset_property (grid, "base-color");
	g_assert_false (lrg_editor_material_editor_is_overridden (ed, "base_color"));

	/* the base, open and edited, shows through in the variant */
	parent = open_material (f, "materials/base.rmat");
	g_assert_true (lrg_editor_material_editor_set_key (parent, "roughness", "0.1", NULL));
	g_assert_cmpfloat_with_epsilon (lrg_material_get_roughness (lrg_editor_material_editor_get_material (ed)), 0.1f,
	                                1e-4);
	(void) base;
	(void) red;
}

/* Custom shading: the uniform inspector follows the .rshader's
 * descriptions (types, ranges, hidden). */
static void
test_material_uniforms (EditorAppFixture *f,
                        gconstpointer     data)
{
	g_autofree gchar        *frag = write_project_file (f, "shaders/tint.frag", TINT_FRAG);
	g_autofree gchar        *shader = write_project_file (f, "shaders/tint.rshader", TINT_RSHADER);
	g_autofree gchar        *mat = write_project_file (f, "shaders/custom.rmat", CUSTOM_RMAT);
	LrgEditorMaterialEditor *ed = open_material (f, "shaders/custom.rmat");
	LrgPropertyGrid         *grid = material_grid (ed);
	LrgPropertyRow          *glow;
	g_auto(GValue)           v = G_VALUE_INIT;
	g_autofree gchar        *text = NULL;
	gfloat                   vals[4];
	LrgMaterialUniformType   type;

	glow = lrg_property_grid_get_row (grid, "u-glow");
	g_assert_nonnull (glow);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "u-tint"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "u-noise-tex"));
	g_assert_null (lrg_property_grid_get_row (grid, "u-secret"));
	g_assert_cmpstr (lrg_property_row_get_hint (glow), ==, "range:0,4,0.04");
	g_assert_cmpuint (lrg_property_info_get_value_type (lrg_property_row_get_info (
	                      lrg_property_grid_get_row (grid, "u-tint"))), ==, GRL_TYPE_COLOR);

	/* default from the shader, then an override of the material */
	g_assert_true (lrg_property_row_get_value (glow, &v));
	g_assert_cmpfloat_with_epsilon (g_value_get_double (&v), 1.0, 1e-4);
	g_value_set_double (&v, 2.5);
	lrg_property_row_apply_value (glow, &v);
	text = doc_text (LRG_EDITOR_DATA_EDITOR (ed));
	g_assert_nonnull (strstr (text, "type: float"));
	g_assert_true (lrg_material_get_uniform (lrg_editor_material_editor_get_material (ed), "glow", &type, vals));
	g_assert_cmpfloat_with_epsilon (vals[0], 2.5f, 1e-4);

	/* switching away from custom rebuilds the grid without uniforms */
	g_assert_true (lrg_editor_material_editor_set_key (ed, "shading_model", "lit", NULL));
	efx_frame (f);
	g_assert_null (lrg_property_grid_get_row (grid, "u-tint"));
	/* entries the shader does not describe still show */
	g_assert_nonnull (lrg_property_grid_get_row (grid, "u-glow"));
	(void) frag;
	(void) shader;
	(void) mat;
}

/* The preview's chips through the canvas; the state IPC agrees. */
static void
test_material_preview_chips (EditorAppFixture *f,
                             gconstpointer     data)
{
	g_autofree gchar        *path = write_project_file (f, "materials/base.rmat", BASE_RMAT);
	LrgEditorMaterialEditor *ed = open_material (f, "materials/base.rmat");
	LrgWidget               *cube = lrg_widget_find (LRG_WIDGET (ed), "material-preview-cube");
	LrgWidget               *rotate = lrg_widget_find (LRG_WIDGET (ed), "material-preview-rotate");
	g_autoptr(JsonNode)      r = NULL;

	g_assert_nonnull (cube);
	g_assert_true (lrg_editor_material_editor_get_preview_rotate (ed));
	efx_click_widget (f, cube);
	g_assert_cmpint (lrg_editor_material_editor_get_preview_shape (ed), ==, LRG_EDITOR_MATERIAL_PREVIEW_CUBE);
	efx_click_widget (f, rotate);
	g_assert_false (lrg_editor_material_editor_get_preview_rotate (ed));

	r = efx_ipc_result (f, "{'id':1,'method':'material.preview','params':{'path':'materials/base.rmat',"
	                       "'shape':'plane','rotate':true}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "shape"), ==, "plane");
	g_assert_cmpint (lrg_editor_material_editor_get_preview_shape (ed), ==, LRG_EDITOR_MATERIAL_PREVIEW_PLANE);
	g_assert_true (lrg_editor_material_editor_get_preview_rotate (ed));
	(void) path;
}

/* assign_to_node: a guid/path reference on the visual, one undo step;
 * nodes without a mesh refuse. */
static void
test_material_assign (EditorAppFixture *f,
                      gconstpointer     data)
{
	g_autofree gchar          *path = write_project_file (f, "materials/base.rmat", BASE_RMAT);
	g_autoptr(LrgNode)         cube = lrg_node_new ("Cube");
	g_autoptr(LrgNode)         empty = lrg_node_new ("Empty");
	g_autoptr(LrgNodeVisual)   visual = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
	g_autoptr(GError)          error = NULL;
	g_autoptr(JsonNode)        r = NULL;
	const gchar               *ref;

	lrg_node_set_visual (cube, visual);
	lrg_editor_add_node (f->editor, cube, NULL);
	lrg_editor_add_node (f->editor, empty, NULL);

	g_assert_true (lrg_editor_material_editor_assign_to_node (f->app, lrg_node_get_guid (cube),
	                                                          "materials/base.rmat", &error));
	g_assert_no_error (error);
	ref = lrg_node_visual_get_material_ref (visual);
	g_assert_nonnull (ref);
	g_assert_true (g_str_has_prefix (ref, "guid:") || g_strcmp0 (ref, "materials/base.rmat") == 0);
	lrg_editor_undo (f->editor);
	g_assert_null (lrg_node_visual_get_material_ref (visual));

	g_assert_false (lrg_editor_material_editor_assign_to_node (f->app, lrg_node_get_guid (empty),
	                                                           "materials/base.rmat", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
	g_clear_error (&error);
	g_assert_false (lrg_editor_material_editor_assign_to_node (f->app, "nope", "materials/base.rmat", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);

	/* the IPC twin */
	r = efx_ipc_result (f, "{'id':1,'method':'material.assign','params':{'node':'Cube',"
	                       "'path':'materials/base.rmat'}}");
	g_assert_nonnull (json_object_get_string_member (json_node_get_object (r), "material"));
	g_assert_nonnull (lrg_node_visual_get_material_ref (visual));
	(void) path;
}

/* Every material.* method. */
static void
test_material_ipc (EditorAppFixture *f,
                   gconstpointer     data)
{
	g_autofree gchar    *base = write_project_file (f, "materials/base.rmat", BASE_RMAT);
	g_autoptr(JsonNode)  r = NULL;
	g_autofree gchar    *reply = NULL;
	g_autofree gchar    *variant = NULL;
	g_autofree gchar    *text = NULL;
	JsonObject          *o;

	/* create: a base and an instance; existing files refuse */
	r = efx_ipc_result (f, "{'id':1,'method':'material.create','params':{'path':'materials/new.rmat',"
	                       "'shading_model':'unlit'}}");
	g_assert_true (g_file_test (json_object_get_string_member (json_node_get_object (r), "path"),
	                            G_FILE_TEST_IS_REGULAR));
	g_clear_pointer (&r, json_node_unref);
	reply = efx_ipc (f, "{'id':2,'method':'material.create','params':{'path':'materials/new.rmat'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	g_clear_pointer (&reply, g_free);
	reply = efx_ipc (f, "{'id':2,'method':'material.create','params':{'path':'materials/x.rmat',"
	                    "'shading_model':'shiny'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));

	/* create_variant: next to the base, parent relative */
	r = efx_ipc_result (f, "{'id':3,'method':'material.create_variant','params':{'path':'materials/base.rmat'}}");
	variant = g_strdup (json_object_get_string_member (json_node_get_object (r), "path"));
	g_assert_true (g_str_has_suffix (variant, "base_variant.rmat"));
	text = read_file (variant);
	g_assert_nonnull (strstr (text, "parent: \"base.rmat\""));
	g_clear_pointer (&r, json_node_unref);

	/* get: effective values, overrides of an instance */
	r = efx_ipc_result (f, "{'id':4,'method':'material.get','params':{'path':'materials/base_variant.rmat'}}");
	o = json_node_get_object (r);
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (o, "metallic"), 0.25, 1e-4);
	g_assert_cmpstr (json_object_get_string_member (o, "shading_model"), ==, "lit");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "overrides")), ==, 0);
	g_clear_pointer (&r, json_node_unref);

	/* set (JSON value = YAML), revert, save */
	r = efx_ipc_result (f, "{'id':5,'method':'material.set','params':{'path':'materials/base_variant.rmat',"
	                       "'key':'emission_color','value':[1,0.5,0]}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "dirty"));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':6,'method':'material.get','params':{'path':'materials/base_variant.rmat'}}");
	o = json_node_get_object (r);
	g_assert_cmpfloat_with_epsilon (json_array_get_double_element (json_object_get_array_member (o, "emission_color"),
	                                                               1), 0.5, 1e-4);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "overrides")), ==, 1);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':7,'method':'material.revert','params':{'path':'materials/base_variant.rmat',"
	                       "'key':'emission_color'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "reverted"));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':8,'method':'material.set','params':{'path':'materials/base_variant.rmat',"
	                       "'key':'roughness','value':0.3}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':9,'method':'material.save','params':{'path':'materials/base_variant.rmat'}}");
	g_clear_pointer (&r, json_node_unref);
	g_clear_pointer (&text, g_free);
	text = read_file (variant);
	g_assert_nonnull (strstr (text, "roughness: 0.3"));

	/* list */
	r = efx_ipc_result (f, "{'id':10,'method':'material.list'}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "editors")),
	                  ==, 1);
	g_clear_pointer (&r, json_node_unref);

	/* missing file */
	g_clear_pointer (&reply, g_free);
	reply = efx_ipc (f, "{'id':11,'method':'material.get','params':{'path':'materials/none.rmat'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	(void) base;
}

/* ==========================================================================
 * Shader graph editor
 * ========================================================================== */

static LrgEditorShaderGraphEditor *
open_graph (EditorAppFixture *f,
            const gchar      *rel)
{
	g_autoptr(GError)           error = NULL;
	LrgEditorShaderGraphEditor *ed;

	if (lrg_editor_app_get_project_dir (f->app) == NULL)
		g_free (write_project_file (f, "readme.txt", "x"));
	g_assert_true (lrg_editor_shader_graph_editor_create (f->app, rel, "pbr", &error));
	g_assert_no_error (error);
	ed = lrg_editor_shader_graph_editor_open (f->app, rel, &error);
	g_assert_no_error (error);
	g_assert_nonnull (ed);
	efx_frames (f, 2);
	return ed;
}

/* The starter graph is valid; the canvas mirrors it; GLSL is shown. */
static void
test_graph_open (EditorAppFixture *f,
                 gconstpointer     data)
{
	LrgEditorShaderGraphEditor *ed = open_graph (f, "shaders/glow.rshadergraph");
	LrgShaderGraph             *g = lrg_editor_shader_graph_editor_get_graph (ed);
	LrgNodeGraph               *canvas = lrg_editor_shader_graph_editor_get_canvas (ed);
	LrgWidget                  *code;
	g_autofree gchar           *text = NULL;

	g_assert_cmpuint (lrg_shader_graph_get_n_nodes (g), ==, 5);
	g_assert_cmpuint (lrg_node_graph_get_n_nodes (canvas), ==, 5);
	g_assert_cmpuint (lrg_node_graph_get_n_links (canvas), ==, lrg_shader_graph_get_n_links (g));
	g_assert_cmpuint (lrg_editor_shader_graph_editor_get_problems (ed)->len, ==, 0);
	g_assert_nonnull (lrg_editor_shader_graph_editor_get_glsl (ed));
	g_assert_nonnull (strstr (lrg_editor_shader_graph_editor_get_glsl (ed), "lrg_light_pbr"));
	g_assert_nonnull (lrg_editor_shader_graph_editor_get_asset (ed));
	g_assert_true (lrg_shader_asset_get_lit (lrg_editor_shader_graph_editor_get_asset (ed)));
	code = lrg_widget_find (LRG_WIDGET (ed), "shadergraph-glsl");
	g_assert_true (LRG_IS_TEXT_EDITOR (code));
	text = lrg_text_editor_get_text (LRG_TEXT_EDITOR (code));
	g_assert_cmpstr (text, ==, lrg_editor_shader_graph_editor_get_glsl (ed));
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (ed)), ==, 0);
}

/* Edits through the API and the canvas are document steps; validation
 * problems paint the card red; undo restores. */
static void
test_graph_edit_undo (EditorAppFixture *f,
                      gconstpointer     data)
{
	LrgEditorShaderGraphEditor *ed = open_graph (f, "shaders/glow.rshadergraph");
	LrgEditorDataEditor        *e = LRG_EDITOR_DATA_EDITOR (ed);
	LrgNodeGraph               *canvas = lrg_editor_shader_graph_editor_get_canvas (ed);
	g_autoptr(GError)           error = NULL;
	const gchar                *id;
	g_autofree gchar           *keep = NULL;
	GrlColor                    accent;
	g_autofree gchar           *text = NULL;

	/* a texture sample without its texture: a missing-input problem */
	id = lrg_editor_shader_graph_editor_add_node (ed, "texture_sample", "smp", 0.0f, 300.0f, &error);
	g_assert_no_error (error);
	keep = g_strdup (id);
	g_assert_cmpstr (lrg_editor_shader_graph_editor_get_selected (ed), ==, "smp");
	g_assert_true (lrg_editor_shader_graph_editor_connect (ed, "smp", "rgb", "out", "albedo", &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_editor_shader_graph_editor_get_problems (ed)->len, >, 0);
	g_assert_null (lrg_editor_shader_graph_editor_get_glsl (ed));
	g_assert_true (lrg_graph_node_get_accent (lrg_node_graph_get_node (canvas, "smp"), &accent));
	g_assert_nonnull (lrg_graph_node_get_body (lrg_node_graph_get_node (canvas, "smp")));
	g_assert_cmpuint (lrg_editor_data_editor_validate (e), >, 0);
	text = doc_text (e);
	g_assert_nonnull (strstr (text, "to: out.albedo"));

	/* two undos: the link, then the node; the graph is valid again */
	g_assert_true (lrg_editor_data_editor_undo (e));
	g_assert_true (lrg_editor_data_editor_undo (e));
	g_assert_false (lrg_shader_graph_has_node (lrg_editor_shader_graph_editor_get_graph (ed), keep));
	g_assert_null (lrg_node_graph_get_node (canvas, "smp"));
	g_assert_cmpuint (lrg_editor_shader_graph_editor_get_problems (ed)->len, ==, 0);
	g_assert_nonnull (lrg_editor_shader_graph_editor_get_glsl (ed));

	/* a wire drawn on the canvas reaches the model on the next frame */
	g_assert_true (lrg_editor_shader_graph_editor_disconnect (ed, "out", "emission"));
	g_assert_nonnull (lrg_node_graph_connect_by_name (canvas, "glow", "out", "out", "emission", NULL));
	efx_frame (f);
	g_clear_pointer (&text, g_free);
	text = doc_text (e);
	g_assert_nonnull (strstr (text, "to: out.emission"));

	/* parameters and input values */
	g_assert_true (lrg_editor_shader_graph_editor_set_param (ed, "tint", "name", "albedo_tint"));
	{
		const gfloat p = 2.0f;

		g_assert_true (lrg_editor_shader_graph_editor_set_input (ed, "rim", "power", &p, 1));
	}
	g_assert_false (lrg_editor_shader_graph_editor_set_param (ed, "rim", "mask", "xy"));
	g_assert_nonnull (strstr (lrg_editor_shader_graph_editor_get_glsl (ed), "albedo_tint"));

	/* the node grid follows the selection */
	g_assert_true (lrg_editor_shader_graph_editor_select_node (ed, "tint"));
	efx_frame (f);
	g_assert_nonnull (lrg_property_grid_get_row (LRG_PROPERTY_GRID (lrg_widget_find (LRG_WIDGET (ed),
	                                                                                 "shadergraph-grid")),
	                                             "p-name"));
	g_assert_true (lrg_editor_shader_graph_editor_select_node (ed, "rim"));
	efx_frame (f);
	g_assert_nonnull (lrg_property_grid_get_row (LRG_PROPERTY_GRID (lrg_widget_find (LRG_WIDGET (ed),
	                                                                                 "shadergraph-grid")),
	                                             "in-power"));
}

/* The palette adds a node of the clicked type; the filter narrows. */
static void
test_graph_palette (EditorAppFixture *f,
                    gconstpointer     data)
{
	LrgEditorShaderGraphEditor *ed = open_graph (f, "shaders/glow.rshadergraph");
	LrgListView                *palette = LRG_LIST_VIEW (lrg_widget_find (LRG_WIDGET (ed), "shadergraph-palette"));
	LrgTextInput               *filter = LRG_TEXT_INPUT (lrg_widget_find (LRG_WIDGET (ed), "shadergraph-filter"));
	GListModel                 *model = lrg_list_view_get_model (palette);
	guint                       n_all = g_list_model_get_n_items (model);
	guint                       before = lrg_shader_graph_get_n_nodes (lrg_editor_shader_graph_editor_get_graph (ed));

	g_assert_cmpuint (n_all, >, 30);
	lrg_text_input_set_text (filter, "noise");
	efx_frame (f);
	/* a header and the node */
	g_assert_cmpuint (g_list_model_get_n_items (model), ==, 2);
	lrg_list_view_select (palette, 1);
	efx_frame (f);
	g_assert_cmpuint (lrg_shader_graph_get_n_nodes (lrg_editor_shader_graph_editor_get_graph (ed)), ==, before + 1);
	g_assert_cmpstr (lrg_shader_graph_get_node_type (lrg_editor_shader_graph_editor_get_graph (ed),
	                                                 lrg_editor_shader_graph_editor_get_selected (ed)), ==, "noise");
	/* a header row adds nothing */
	lrg_list_view_select (palette, 0);
	efx_frame (f);
	g_assert_cmpuint (lrg_shader_graph_get_n_nodes (lrg_editor_shader_graph_editor_get_graph (ed)), ==, before + 1);
}

/* Save writes the generated .rshader; every shadergraph.* method. */
static void
test_graph_ipc_save (EditorAppFixture *f,
                     gconstpointer     data)
{
	g_autoptr(JsonNode) r = NULL;
	g_autofree gchar   *reply = NULL;
	g_autofree gchar   *path = NULL;
	g_autofree gchar   *shader = NULL;
	g_autofree gchar   *id = NULL;
	JsonObject         *o;

	g_free (write_project_file (f, "readme.txt", "x"));
	r = efx_ipc_result (f, "{'id':1,'method':'shadergraph.create','params':{'path':'fx/flat.rshadergraph',"
	                       "'master':'unlit'}}");
	path = g_strdup (json_object_get_string_member (json_node_get_object (r), "path"));
	g_clear_pointer (&r, json_node_unref);
	reply = efx_ipc (f, "{'id':1,'method':'shadergraph.create','params':{'path':'fx/x.rshadergraph',"
	                    "'master':'toon'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));

	r = efx_ipc_result (f, "{'id':2,'method':'shadergraph.node_types'}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "types")), >,
	                  30);
	g_clear_pointer (&r, json_node_unref);

	r = efx_ipc_result (f, "{'id':3,'method':'shadergraph.get','params':{'path':'fx/flat.rshadergraph'}}");
	o = json_node_get_object (r);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "nodes")), ==, 2);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "links")), ==, 1);
	g_clear_pointer (&r, json_node_unref);

	r = efx_ipc_result (f, "{'id':4,'method':'shadergraph.add_node','params':{'path':'fx/flat.rshadergraph',"
	                       "'type':'time','x':-300,'y':200}}");
	id = g_strdup (json_object_get_string_member (json_node_get_object (r), "id"));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':5,'method':'shadergraph.add_node','params':{'path':'fx/flat.rshadergraph',"
	                       "'type':'sin','id':'wave'}}");
	g_clear_pointer (&r, json_node_unref);
	g_clear_pointer (&reply, g_free);
	reply = g_strdup_printf ("{'id':6,'method':'shadergraph.connect','params':{'path':'fx/flat.rshadergraph',"
	                         "'from':'%s','from_port':'time','to':'wave','to_port':'in'}}", id);
	r = efx_ipc_result (f, reply);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':7,'method':'shadergraph.connect','params':{'path':'fx/flat.rshadergraph',"
	                       "'from':'wave','from_port':'out','to':'out','to_port':'alpha'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':8,'method':'shadergraph.set_input','params':{'path':'fx/flat.rshadergraph',"
	                       "'id':'out','port':'alpha_clip','value':0.2}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':9,'method':'shadergraph.set_param','params':{'path':'fx/flat.rshadergraph',"
	                       "'id':'out','key':'blend','value':'alpha'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':10,'method':'shadergraph.set_param','params':{'path':'fx/flat.rshadergraph',"
	                       "'id':'tint','key':'default','value':[0,1,0,1]}}");
	g_clear_pointer (&r, json_node_unref);

	r = efx_ipc_result (f, "{'id':11,'method':'shadergraph.validate','params':{'path':'fx/flat.rshadergraph'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "ok"));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':12,'method':'shadergraph.glsl','params':{'path':'fx/flat.rshadergraph'}}");
	g_assert_nonnull (strstr (json_object_get_string_member (json_node_get_object (r), "fragment"), "sin("));
	g_clear_pointer (&r, json_node_unref);

	r = efx_ipc_result (f, "{'id':13,'method':'shadergraph.save','params':{'path':'fx/flat.rshadergraph'}}");
	o = json_node_get_object (r);
	shader = g_strdup (json_object_get_string_member (o, "shader"));
	g_assert_nonnull (shader);
	g_assert_true (g_str_has_suffix (shader, "flat.rshader"));
	g_clear_pointer (&r, json_node_unref);
	{
		g_autoptr(LrgShaderAsset) asset = lrg_shader_asset_new_from_file (shader, NULL);

		g_assert_nonnull (asset);
		g_assert_false (lrg_shader_asset_get_lit (asset));
		g_assert_cmpstr (lrg_shader_asset_get_blend_mode (asset), ==, "alpha");
	}
	{
		g_autoptr(LrgShaderGraph) back = lrg_shader_graph_new_from_file (path, NULL);

		g_assert_nonnull (back);
		g_assert_cmpuint (lrg_shader_graph_get_n_nodes (back), ==, 4);
	}

	/* a broken wire is reported with its node */
	r = efx_ipc_result (f, "{'id':14,'method':'shadergraph.disconnect','params':{'path':'fx/flat.rshadergraph',"
	                       "'to':'wave','to_port':'in'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "removed"));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':15,'method':'shadergraph.remove_node','params':{'path':'fx/flat.rshadergraph',"
	                       "'id':'tint'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':16,'method':'shadergraph.validate','params':{'path':'fx/flat.rshadergraph'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "ok"));
	g_clear_pointer (&r, json_node_unref);
	g_clear_pointer (&reply, g_free);
	reply = efx_ipc (f, "{'id':17,'method':'shadergraph.connect','params':{'path':'fx/flat.rshadergraph',"
	                    "'from':'nope','from_port':'out','to':'out','to_port':'alpha'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
}

/* ==========================================================================
 * Assets panel
 * ========================================================================== */

static LrgEditorAssetsPanel *
assets_panel (EditorAppFixture *f)
{
	LrgDockPanel *panel = lrg_dock_area_get_panel (lrg_editor_app_get_dock (f->app), "assets");

	g_assert_nonnull (panel);
	return LRG_EDITOR_ASSETS_PANEL (lrg_dock_panel_get_child (panel));
}

/* New Material / Shader / Shader Graph and Create Variant, and where a
 * double click on them goes. */
static void
test_assets_render_assets (EditorAppFixture *f,
                           gconstpointer     data)
{
	g_autofree gchar     *prefab = NULL;
	g_autofree gchar     *mat = NULL;
	g_autofree gchar     *variant = NULL;
	g_autofree gchar     *pvariant = NULL;
	LrgEditorAssetsPanel *panel;
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *frag = NULL;

	mat = write_project_file (f, "materials/base.rmat", BASE_RMAT);
	efx_frame (f);
	panel = assets_panel (f);
	g_assert_true (lrg_editor_assets_panel_set_directory (panel, g_path_get_dirname (mat)));
	efx_frame (f);

	lrg_widget_activate_action (LRG_WIDGET (panel), "assets.new-material", NULL);
	lrg_widget_activate_action (LRG_WIDGET (panel), "assets.new-shader", NULL);
	lrg_widget_activate_action (LRG_WIDGET (panel), "assets.new-shader-graph", NULL);
	{
		g_autofree gchar *m = g_build_filename (f->project, "materials", "new_material.rmat", NULL);
		g_autofree gchar *s = g_build_filename (f->project, "materials", "new_shader.rshader", NULL);
		g_autofree gchar *g = g_build_filename (f->project, "materials", "new_shader_graph.rshadergraph", NULL);
		g_autoptr(LrgMaterial)    lm = lrg_material_new_from_file (m, NULL, NULL);
		g_autoptr(LrgShaderAsset) ls = lrg_shader_asset_new_from_file (s, NULL);
		g_autoptr(LrgShaderGraph) lg = lrg_shader_graph_new_from_file (g, NULL);

		g_assert_nonnull (lm);
		g_assert_nonnull (ls);
		g_assert_true (lrg_shader_asset_get_lit (ls));
		g_assert_cmpuint (lrg_shader_asset_get_n_uniforms (ls), ==, 3);
		g_assert_nonnull (lg);
		g_assert_true (lrg_shader_graph_validate (lg, NULL));
		frag = g_build_filename (f->project, "materials", "new_shader.frag", NULL);
		g_assert_true (g_file_test (frag, G_FILE_TEST_IS_REGULAR));
	}
	/* a second New Material picks a free name */
	lrg_widget_activate_action (LRG_WIDGET (panel), "assets.new-material", NULL);
	{
		g_autofree gchar *m2 = g_build_filename (f->project, "materials", "new_material2.rmat", NULL);

		g_assert_true (g_file_test (m2, G_FILE_TEST_IS_REGULAR));
	}
	/* stop the rename the panel started */
	efx_key (f, GRL_KEY_ESCAPE, 0);

	/* Create Variant: a material instance, a prefab variant (undoable) */
	variant = lrg_editor_assets_panel_create_variant (panel, mat, &error);
	g_assert_no_error (error);
	g_assert_true (g_str_has_suffix (variant, "base_variant.rmat"));
	prefab = write_project_file (f, "prefabs/crate.rprefab",
	                             "type: prefab\nname: crate\nnodes:\n  - name: Crate\n    guid: 6f1b0c3e-2a4d-4f5e-9b8c-1d2e3f405162\n");
	pvariant = lrg_editor_assets_panel_create_variant (panel, prefab, &error);
	g_assert_no_error (error);
	g_assert_nonnull (pvariant);
	g_assert_true (g_file_test (pvariant, G_FILE_TEST_IS_REGULAR));
	lrg_editor_undo (f->editor);
	g_assert_false (g_file_test (pvariant, G_FILE_TEST_EXISTS));
	g_assert_null (lrg_editor_assets_panel_create_variant (panel, frag, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);

	/* a .rmat double click opens the material editor, a stage the code
	 * panel */
	g_assert_true (lrg_editor_assets_panel_reveal (panel, mat));
	lrg_widget_activate_action (LRG_WIDGET (panel), "assets.open", NULL);
	g_assert_true (LRG_IS_EDITOR_MATERIAL_EDITOR (lrg_editor_data_editor_find (f->app, mat)));
	g_assert_true (lrg_editor_assets_panel_reveal (panel, frag));
	lrg_widget_activate_action (LRG_WIDGET (panel), "assets.open", NULL);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'code.list'}");
		JsonArray          *a = json_node_get_array (r);
		gboolean            found = FALSE;
		guint               i;

		for (i = 0; i < json_array_get_length (a); i++)
			if (g_str_has_suffix (json_object_get_string_member (json_array_get_object_element (a, i), "path"),
			                      "new_shader.frag"))
			{
				found = TRUE;
				g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (a, i), "language"),
				                 ==, "glsl");
			}
		g_assert_true (found);
	}
}

/* ==========================================================================
 * Code panel shaders
 * ========================================================================== */

/* .rshader opens as YAML; saving a broken document lists a diagnostic in
 * the strip and the problems; fixing it clears both. */
static void
test_code_shader_diagnostics (EditorAppFixture *f,
                              gconstpointer     data)
{
	g_autofree gchar   *frag = write_project_file (f, "shaders/tint.frag", TINT_FRAG);
	g_autofree gchar   *shader = write_project_file (f, "shaders/tint.rshader", TINT_RSHADER);
	g_autoptr(JsonNode) r = NULL;
	LrgDockPanel       *dock = lrg_dock_area_get_panel (lrg_editor_app_get_dock (f->app), "code");
	LrgEditorCodePanel *code;
	LrgTextEditor      *editor;
	LrgWidget          *strip;
	LrgWidget          *validate;
	LrgEditorProblemsPanel *problems;
	guint               i, n0;

	g_assert_nonnull (dock);
	code = LRG_EDITOR_CODE_PANEL (lrg_dock_panel_get_child (dock));
	problems = LRG_EDITOR_PROBLEMS_PANEL (lrg_dock_panel_get_child (
	               lrg_dock_area_get_panel (lrg_editor_app_get_dock (f->app), "problems")));
	efx_frames (f, 2);
	n0 = lrg_editor_problems_panel_get_n_rows (problems);
	r = efx_ipc_result (f, "{'id':1,'method':'code.open','params':{'path':'shaders/tint.rshader'}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "language"), ==, "yaml");
	g_clear_pointer (&r, json_node_unref);

	/* break the stage reference and save */
	editor = lrg_editor_code_panel_get_editor (code, shader);
	lrg_text_editor_set_text (editor, "version: 1\nname: tint\nfragment: missing.frag\n");
	g_assert_true (lrg_editor_code_panel_save (code, shader, NULL));
	efx_frames (f, 2);
	strip = lrg_widget_find (LRG_WIDGET (code), "code-diagnostics");
	g_assert_nonnull (strip);
	g_assert_true (lrg_widget_get_visible (strip));
	r = efx_ipc_result (f, "{'id':2,'method':'shader.diagnostics','params':{'path':'shaders/tint.rshader'}}");
	g_assert_false (json_object_get_boolean_member (json_node_get_object (r), "ok"));
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r),
	                                                                       "diagnostics")), ==, 1);
	g_clear_pointer (&r, json_node_unref);
	g_assert_cmpuint (lrg_editor_problems_panel_get_n_rows (problems), ==, n0 + 1);
	{
		gint line = 0;
		gint column = 0;

		g_assert_true (lrg_editor_problems_panel_get_row_location (problems, n0, &line, &column));
		g_assert_cmpint (line, >, 0);
	}

	/* a level validation replaces the problems: ours come back */
	validate = lrg_widget_find (LRG_WIDGET (problems), "problems-validate");
	g_assert_nonnull (validate);
	editor_chip_click (f, validate);
	efx_frames (f, 2);
	g_assert_cmpuint (lrg_editor_problems_panel_get_n_rows (problems), ==, n0 + 1);

	/* fixed: clean */
	lrg_text_editor_set_text (editor, TINT_RSHADER);
	g_assert_true (lrg_editor_code_panel_save (code, shader, NULL));
	efx_frames (f, 2);
	g_assert_false (lrg_widget_get_visible (strip));
	g_assert_cmpuint (lrg_editor_problems_panel_get_n_rows (problems), ==, n0);
	r = efx_ipc_result (f, "{'id':5,'method':'shader.compile','params':{'path':'shaders/tint.frag'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "ok"));
	g_clear_pointer (&r, json_node_unref);

	/* uniforms and create */
	r = efx_ipc_result (f, "{'id':6,'method':'shader.uniforms','params':{'path':'shaders/tint.rshader'}}");
	{
		JsonArray *a = json_object_get_array_member (json_node_get_object (r), "uniforms");

		g_assert_cmpuint (json_array_get_length (a), ==, 4);
		for (i = 0; i < json_array_get_length (a); i++)
			if (g_strcmp0 (json_object_get_string_member (json_array_get_object_element (a, i), "name"), "glow") == 0)
				g_assert_cmpfloat (json_object_get_double_member (json_array_get_object_element (a, i), "max"), ==,
				                   4.0);
	}
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':7,'method':'shader.create','params':{'path':'shaders/flat.rshader','lit':false}}");
	g_assert_true (g_file_test (json_object_get_string_member (json_node_get_object (r), "fragment"),
	                            G_FILE_TEST_IS_REGULAR));
	(void) frag;
}

/* ==========================================================================
 * GPU (only under the isolated Xvfb)
 * ========================================================================== */

static gboolean gpu_ready = FALSE;

static gboolean
gpu_init (void)
{
	const gchar *display = g_getenv ("DISPLAY");

	/* never on a live Wayland session: xrun.sh unsets WAYLAND_DISPLAY */
	if (display == NULL || *display == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
		return FALSE;
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (64, 64, "test-editor-app-material");
	return IsWindowReady ();
}

static gboolean
gpu_skip (void)
{
	if (!gpu_ready)
		g_test_skip ("needs a display (run under xrun.sh)");
	return !gpu_ready;
}

static void
pixel (GrlImage *img,
       gint      x,
       gint      y,
       guint8   *rgba)
{
	GrlColor *c = grl_image_get_pixel (img, x, y);

	g_assert_nonnull (c);
	rgba[0] = c->r;
	rgba[1] = c->g;
	rgba[2] = c->b;
	rgba[3] = c->a;
	g_boxed_free (GRL_TYPE_COLOR, c);
}

/* The preview draws the mesh in the material's colour over the sky; a
 * colour edit shows on the next render; custom shading uses the
 * shader. */
static void
test_gpu_material_preview (EditorAppFixture *f,
                           gconstpointer     data)
{
	g_autofree gchar        *path = NULL;
	LrgEditorMaterialEditor *ed;
	g_autoptr(GrlImage)      img = NULL;
	guint8                   centre[4], corner[4];

	if (gpu_skip ())
		return;
	path = write_project_file (f, "materials/base.rmat", BASE_RMAT);
	ed = open_material (f, "materials/base.rmat");
	g_assert_true (lrg_editor_material_editor_set_key (ed, "base_color", "[1, 0, 0, 1]", NULL));
	g_assert_true (lrg_editor_material_editor_set_key (ed, "metallic", "0", NULL));
	img = lrg_editor_material_editor_render_preview (ed, 96);
	g_assert_nonnull (img);
	pixel (img, 48, 48, centre);
	pixel (img, 2, 2, corner);
	g_assert_cmpuint (centre[0], >, centre[1] + 40);
	g_assert_cmpuint (centre[0], >, centre[2] + 40);
	g_assert_true (ABS ((gint) centre[0] - (gint) corner[0]) + ABS ((gint) centre[2] - (gint) corner[2]) > 40);

	/* blue now */
	g_assert_true (lrg_editor_material_editor_set_key (ed, "base_color", "[0, 0.2, 1, 1]", NULL));
	g_clear_object (&img);
	img = lrg_editor_material_editor_render_preview (ed, 96);
	pixel (img, 48, 48, centre);
	g_assert_cmpuint (centre[2], >, centre[0] + 30);

	/* the thumbnail renders the file on disk */
	{
		g_autoptr(GrlImage) thumb = lrg_editor_material_editor_render_thumbnail (f->app, path, 64);

		g_assert_nonnull (thumb);
		g_assert_cmpint (grl_image_get_width (thumb), ==, 64);
	}
}

static void
test_gpu_custom_material (EditorAppFixture *f,
                          gconstpointer     data)
{
	g_autofree gchar        *frag = NULL;
	g_autofree gchar        *shader = NULL;
	g_autofree gchar        *mat = NULL;
	LrgEditorMaterialEditor *ed;
	g_autoptr(GrlImage)      img = NULL;
	guint8                   centre[4];

	if (gpu_skip ())
		return;
	frag = write_project_file (f, "shaders/tint.frag", TINT_FRAG);
	shader = write_project_file (f, "shaders/tint.rshader", TINT_RSHADER);
	mat = write_project_file (f, "shaders/custom.rmat", CUSTOM_RMAT);
	ed = open_material (f, "shaders/custom.rmat");
	/* the shader's default tint is green, glow 1 */
	img = lrg_editor_material_editor_render_preview (ed, 64);
	g_assert_nonnull (img);
	pixel (img, 32, 32, centre);
	g_assert_cmpuint (centre[1], >, 200);
	g_assert_cmpuint (centre[0], <, 40);
	/* the material's own tint wins */
	g_assert_true (lrg_editor_material_editor_set_key (ed, "uniforms.tint", "{ type: color, value: [1, 0, 1, 1] }",
	                                                   NULL));
	g_clear_object (&img);
	img = lrg_editor_material_editor_render_preview (ed, 64);
	pixel (img, 32, 32, centre);
	g_assert_cmpuint (centre[0], >, 200);
	g_assert_cmpuint (centre[1], <, 40);
}

/* The shader graph preview compiles the generated shader. */
static void
test_gpu_graph_preview (EditorAppFixture *f,
                        gconstpointer     data)
{
	LrgEditorShaderGraphEditor *ed;
	g_autoptr(GrlImage)         img = NULL;
	guint8                      centre[4];
	const gfloat                yellow[4] = { 1.0f, 1.0f, 0.0f, 1.0f };

	if (gpu_skip ())
		return;
	g_free (write_project_file (f, "readme.txt", "x"));
	g_assert_true (lrg_editor_shader_graph_editor_create (f->app, "fx/flat.rshadergraph", "unlit", NULL));
	ed = lrg_editor_shader_graph_editor_open (f->app, "fx/flat.rshadergraph", NULL);
	efx_frames (f, 2);
	g_assert_true (lrg_editor_shader_graph_editor_set_values (ed, "tint", "default", yellow, 4));
	img = lrg_editor_shader_graph_editor_render_preview (ed, 64);
	g_assert_nonnull (img);
	pixel (img, 32, 32, centre);
	g_assert_cmpuint (centre[0], >, 200);
	g_assert_cmpuint (centre[1], >, 200);
	g_assert_cmpuint (centre[2], <, 60);
}

/* A real GLSL error maps to the line of the .frag file. */
static void
test_gpu_shader_compile (EditorAppFixture *f,
                         gconstpointer     data)
{
	g_autofree gchar   *frag = NULL;
	g_autofree gchar   *shader = NULL;
	g_autoptr(JsonNode) r = NULL;
	JsonArray          *a;
	JsonObject         *d;

	if (gpu_skip ())
		return;
	frag = write_project_file (f, "shaders/tint.frag",
	                           "void main()\n{\n    vec3 c = tint.rgb * glow;\n    finalColor = vec4(c, oops);\n}\n");
	shader = write_project_file (f, "shaders/tint.rshader", TINT_RSHADER);
	r = efx_ipc_result (f, "{'id':1,'method':'shader.compile','params':{'path':'shaders/tint.frag'}}");
	g_assert_false (json_object_get_boolean_member (json_node_get_object (r), "ok"));
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "compiled"));
	a = json_object_get_array_member (json_node_get_object (r), "diagnostics");
	g_assert_cmpuint (json_array_get_length (a), >=, 1);
	d = json_array_get_object_element (a, 0);
	g_assert_cmpstr (json_object_get_string_member (d, "file"), ==, frag);
	g_assert_cmpint (json_object_get_int_member (d, "line"), ==, 4);
	g_assert_true (json_object_get_boolean_member (d, "error"));
}

/* ==========================================================================
 * main
 * ========================================================================== */

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */
	g_test_log_set_fatal_handler (loader_warnings_ok, NULL);
	gpu_ready = gpu_init ();

#define ADD(name, fn) g_test_add ("/editor-app-material/" name, EditorAppFixture, NULL, efx_setup, fn, efx_teardown)
	ADD ("material/open", test_material_open);
	ADD ("material/grid-edit", test_material_grid_edit);
	ADD ("material/instance", test_material_instance);
	ADD ("material/uniforms", test_material_uniforms);
	ADD ("material/preview-chips", test_material_preview_chips);
	ADD ("material/assign", test_material_assign);
	ADD ("material/ipc", test_material_ipc);
	ADD ("shadergraph/open", test_graph_open);
	ADD ("shadergraph/edit-undo", test_graph_edit_undo);
	ADD ("shadergraph/palette", test_graph_palette);
	ADD ("shadergraph/ipc-save", test_graph_ipc_save);
	ADD ("assets/render-assets", test_assets_render_assets);
	ADD ("code/shader-diagnostics", test_code_shader_diagnostics);
	ADD ("gpu/material-preview", test_gpu_material_preview);
	ADD ("gpu/custom-material", test_gpu_custom_material);
	ADD ("gpu/graph-preview", test_gpu_graph_preview);
	ADD ("gpu/shader-compile", test_gpu_shader_compile);
#undef ADD

	{
		gint rc = g_test_run ();

		if (gpu_ready)
			CloseWindow ();
		return rc;
	}
}
