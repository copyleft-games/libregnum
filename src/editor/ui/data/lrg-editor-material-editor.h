/* lrg-editor-material-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorMaterialEditor: the data editor of `.rmat` material assets.
 *
 * A live lit preview (a sphere, cube or plane turning under the engine's
 * light set and a baked gradient environment) beside a property grid of
 * every `.rmat` key: shading model and shader, the PBR parameters,
 * emission, the texture slots (drop textures from the assets panel),
 * render state and render queue. An instance (a material with
 * `parent:`) shows every key it sets itself as an override (accent bar)
 * and the inherited ones as defaults; "Reset" on a row reverts the
 * override. Materials with the custom shading model get a uniform
 * inspector built from the `.rshader`'s uniform descriptions (types,
 * defaults, ranges, hints).
 *
 * Every edit is an undoable command of the material's #LrgEditorDocument;
 * a slider drag or a colour pick is one undo step.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../graphics/lrg-material.h"
#include "../../../graphics/lrg-shader-asset.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

/**
 * LrgEditorMaterialPreviewShape:
 * @LRG_EDITOR_MATERIAL_PREVIEW_SPHERE: a UV sphere
 * @LRG_EDITOR_MATERIAL_PREVIEW_CUBE: a cube
 * @LRG_EDITOR_MATERIAL_PREVIEW_PLANE: a flat square tilted towards the
 *   camera (decals, terrain layers, textures)
 *
 * The mesh the material preview draws.
 */
typedef enum
{
	LRG_EDITOR_MATERIAL_PREVIEW_SPHERE,
	LRG_EDITOR_MATERIAL_PREVIEW_CUBE,
	LRG_EDITOR_MATERIAL_PREVIEW_PLANE
} LrgEditorMaterialPreviewShape;

/**
 * lrg_editor_material_preview_shape_get_type:
 *
 * Returns: the #GType of #LrgEditorMaterialPreviewShape
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_material_preview_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_MATERIAL_PREVIEW_SHAPE (lrg_editor_material_preview_shape_get_type ())

#define LRG_TYPE_EDITOR_MATERIAL_EDITOR (lrg_editor_material_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorMaterialEditor, lrg_editor_material_editor, LRG, EDITOR_MATERIAL_EDITOR,
                      LrgEditorDataEditor)

/**
 * lrg_editor_material_editor_new:
 * @app: (nullable): the editor application
 * @document: a material document (%LRG_EDITOR_DOCUMENT_FORMAT_MATERIAL)
 *
 * Creates the editor without docking it; lrg_editor_data_editor_open()
 * (or lrg_editor_material_editor_open()) is what the editor uses.
 *
 * Returns: (transfer full): a new #LrgEditorMaterialEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorMaterialEditor * lrg_editor_material_editor_new (LrgEditorApp      *app,
                                                          LrgEditorDocument *document);

/**
 * lrg_editor_material_editor_install:
 * @app: the editor application
 *
 * Registers the `material.*` IPC methods and the material actions on
 * @app. Safe to call more than once; the assets panel calls it when it
 * is created, so embedders rarely need to.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_material_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_material_editor_open:
 * @app: the editor application
 * @path: a `.rmat` file, absolute or relative to the project directory
 * @error: return location for a #GError
 *
 * Opens @path in a material editor tab (or presents its open tab).
 *
 * Returns: (transfer none) (nullable): the editor, or %NULL with @error
 *   set
 */
LRG_AVAILABLE_IN_ALL
LrgEditorMaterialEditor * lrg_editor_material_editor_open (LrgEditorApp  *app,
                                                           const gchar   *path,
                                                           GError       **error);

/* ==========================================================================
 * Editing
 * ========================================================================== */

/**
 * lrg_editor_material_editor_get_material:
 * @self: an #LrgEditorMaterialEditor
 *
 * The material the preview draws: the document's current text parsed
 * with its parent chain resolved. It is replaced (not mutated) after
 * every edit, so do not keep it.
 *
 * Returns: (transfer none) (nullable): the material, or %NULL while the
 *   document does not parse
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial * lrg_editor_material_editor_get_material (LrgEditorMaterialEditor *self);

/**
 * lrg_editor_material_editor_set_key:
 * @self: an #LrgEditorMaterialEditor
 * @key: a `.rmat` key, dotted for nested keys ("metallic", "base_color",
 *   "textures.albedo", "render_state.blend", "uniforms.tint")
 * @yaml: the new value as YAML (or JSON) text ("0.5", "[1, 0, 0, 1]",
 *   "lit", "null" for an explicit "no texture")
 * @error: return location for a #GError
 *
 * Sets one key as one undoable edit, creating the `textures:`,
 * `render_state:` or `uniforms:` mapping when it is missing. On an
 * instance the key becomes an override of the parent's value.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_material_editor_set_key (LrgEditorMaterialEditor  *self,
                                             const gchar              *key,
                                             const gchar              *yaml,
                                             GError                  **error);

/**
 * lrg_editor_material_editor_revert_key:
 * @self: an #LrgEditorMaterialEditor
 * @key: a dotted `.rmat` key (see lrg_editor_material_editor_set_key())
 *
 * Removes the key from the document, so an instance inherits the
 * parent's value again (a base material falls back to the default).
 *
 * Returns: %TRUE when the key was set and is now removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_material_editor_revert_key (LrgEditorMaterialEditor *self,
                                                const gchar             *key);

/**
 * lrg_editor_material_editor_is_overridden:
 * @self: an #LrgEditorMaterialEditor
 * @key: a dotted `.rmat` key
 *
 * Returns: %TRUE when the material has a parent and sets @key itself
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_material_editor_is_overridden (LrgEditorMaterialEditor *self,
                                                   const gchar             *key);

/**
 * lrg_editor_material_editor_list_overrides:
 * @self: an #LrgEditorMaterialEditor
 *
 * Returns: (transfer full): the dotted keys the material sets itself
 *   (empty for a material without a parent: nothing is overridden)
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_material_editor_list_overrides (LrgEditorMaterialEditor *self);

/**
 * lrg_editor_material_editor_revert_all:
 * @self: an #LrgEditorMaterialEditor
 *
 * Reverts every override of an instance as one undo step (the toolbar's
 * Revert All). Does nothing for a material without a parent.
 *
 * Returns: the number of keys reverted
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_material_editor_revert_all (LrgEditorMaterialEditor *self);

/* ==========================================================================
 * Preview
 * ========================================================================== */

/**
 * lrg_editor_material_editor_get_preview_shape:
 * @self: an #LrgEditorMaterialEditor
 *
 * Returns: the mesh the preview draws
 */
LRG_AVAILABLE_IN_ALL
LrgEditorMaterialPreviewShape lrg_editor_material_editor_get_preview_shape (LrgEditorMaterialEditor *self);

/**
 * lrg_editor_material_editor_set_preview_shape:
 * @self: an #LrgEditorMaterialEditor
 * @shape: the mesh to draw
 *
 * Switches the preview mesh (the Sphere / Cube / Plane chips).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_material_editor_set_preview_shape (LrgEditorMaterialEditor       *self,
                                                   LrgEditorMaterialPreviewShape  shape);

/**
 * lrg_editor_material_editor_get_preview_rotate:
 * @self: an #LrgEditorMaterialEditor
 *
 * Returns: whether the preview mesh turns on its own
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_material_editor_get_preview_rotate (LrgEditorMaterialEditor *self);

/**
 * lrg_editor_material_editor_set_preview_rotate:
 * @self: an #LrgEditorMaterialEditor
 * @rotate: whether the mesh turns (the Rotate chip); dragging in the
 *   preview orbits the camera either way
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_material_editor_set_preview_rotate (LrgEditorMaterialEditor *self,
                                                    gboolean                 rotate);

/**
 * lrg_editor_material_editor_render_preview:
 * @self: an #LrgEditorMaterialEditor
 * @size: the edge of the square image in pixels
 *
 * Renders the current preview (shape, camera, material) into an image.
 * Needs a window; call it outside BeginDrawing().
 *
 * Returns: (transfer full) (nullable): the image, or %NULL without a
 *   graphics context or a parsable material
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_editor_material_editor_render_preview (LrgEditorMaterialEditor *self,
                                                      guint                    size);

/* ==========================================================================
 * Files, variants, assignment, thumbnails
 * ========================================================================== */

/**
 * lrg_editor_material_editor_create:
 * @app: (nullable): the editor application (relative paths resolve
 *   against its project directory)
 * @path: the new `.rmat` file
 * @parent: (nullable): a parent reference (path relative to @path's
 *   folder, absolute, or `guid:`) making the new file an instance
 * @shading_model: (nullable): "lit", "unlit" or "custom" (default lit;
 *   ignored for an instance, which inherits it)
 * @error: return location for a #GError
 *
 * Writes a new material file (`G_IO_ERROR_EXISTS` when @path exists).
 * A base material starts as a mid-grey dielectric (roughness 0.5).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_material_editor_create (LrgEditorApp  *app,
                                            const gchar   *path,
                                            const gchar   *parent,
                                            const gchar   *shading_model,
                                            GError       **error);

/**
 * lrg_editor_material_editor_create_shader:
 * @app: (nullable): the editor application (relative paths resolve
 *   against its project directory)
 * @path: the new `.rshader` file
 * @lit: %TRUE for a lit shader (the fragment stage calls
 *   `lrg_light_pbr()` from the light set's library), %FALSE for unlit
 * @error: return location for a #GError
 *
 * Writes a starter user shader for custom materials: the `.rshader`
 * document with three described uniforms (`tint` colour, `stripes`
 * and `roughness` sliders) and its fragment stage in a `.frag` file of
 * the same stem next to it, ready to edit in the code panel. Fails with
 * `G_IO_ERROR_EXISTS` when either file exists.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_material_editor_create_shader (LrgEditorApp  *app,
                                                   const gchar   *path,
                                                   gboolean       lit,
                                                   GError       **error);

/**
 * lrg_editor_material_editor_suggest_variant_path:
 * @path: a `.rmat` file
 *
 * Proposes `<stem>_variant.rmat` next to @path (`_variant2`,
 * `_variant3`, ... when taken).
 *
 * Returns: (transfer full): the proposed absolute or relative path (as
 *   @path is)
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_material_editor_suggest_variant_path (const gchar *path);

/**
 * lrg_editor_material_editor_create_variant:
 * @app: (nullable): the editor application
 * @path: the base `.rmat`
 * @dest: (nullable): the variant's file, %NULL for
 *   lrg_editor_material_editor_suggest_variant_path()
 * @error: return location for a #GError
 *
 * Writes an instance of @path with no overrides: `parent:` names @path
 * relative to the variant's folder and `name:` is the variant's stem.
 *
 * Returns: (transfer full) (nullable): the variant's absolute path, or
 *   %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_material_editor_create_variant (LrgEditorApp  *app,
                                                   const gchar   *path,
                                                   const gchar   *dest,
                                                   GError       **error);

/**
 * lrg_editor_material_editor_assign_to_node:
 * @app: the editor application
 * @node_ref: a node GUID or name path (lrg_editor_find_node())
 * @path: a `.rmat` file (absolute, project-relative or `guid:`)
 * @error: return location for a #GError
 *
 * Sets the node's #LrgNodeVisual:material-ref as one undoable level
 * edit: the material's `guid:` when the asset database knows one, else
 * its project-relative path. The viewport's material drop calls this.
 * Only primitive and mesh-asset visuals take materials
 * (%G_IO_ERROR_NOT_SUPPORTED otherwise).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_material_editor_assign_to_node (LrgEditorApp  *app,
                                                    const gchar   *node_ref,
                                                    const gchar   *path,
                                                    GError       **error);

/**
 * lrg_editor_material_editor_render_thumbnail:
 * @app: the editor application
 * @path: an absolute `.rmat` file
 * @size: the edge of the square image in pixels
 *
 * Renders the material on a sphere with the preview lighting (what the
 * assets panel shows on material tiles). Needs a window; call it
 * outside BeginDrawing().
 *
 * Returns: (transfer full) (nullable): the image, or %NULL without a
 *   graphics context or when the file does not load
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_editor_material_editor_render_thumbnail (LrgEditorApp *app,
                                                        const gchar  *path,
                                                        guint         size);

G_END_DECLS

/*
 * The preview renderer shared by the material editor, the shader graph
 * editor and the assets panel's material thumbnails; not part of the
 * public API.
 */
#ifdef LRG_EDITOR_PANEL_INTERNAL
#include "../../../core/lrg-asset-manager.h"

typedef struct _EditorMatPreview EditorMatPreview;

/*
 * EditorMatPreviewSource:
 * The preview view asks its owner, every frame, what to draw: a
 * material (nullable) and a shader asset (nullable: the material's own
 * shading). The returned pointers are borrowed.
 */
typedef void (*EditorMatPreviewSource) (gpointer         user_data,
                                        LrgMaterial    **material,
                                        LrgShaderAsset **shader);

G_GNUC_INTERNAL EditorMatPreview * editor_mat_preview_get          (LrgEditorApp *app);
G_GNUC_INTERNAL LrgAssetManager *  editor_mat_preview_get_manager  (EditorMatPreview *self);
G_GNUC_INTERNAL LrgShaderAsset *   editor_mat_preview_get_shader   (EditorMatPreview *self, const gchar *abs_path,
                                                                    GError **error);
G_GNUC_INTERNAL void               editor_mat_preview_file_changed (LrgEditorApp *app, const gchar *abs_path);
G_GNUC_INTERNAL void               editor_mat_preview_prepare      (EditorMatPreview *self);
G_GNUC_INTERNAL gboolean           editor_mat_preview_draw         (EditorMatPreview *self, LrgMaterial *material,
                                                                    LrgShaderAsset *shader, guint shape,
                                                                    gfloat yaw, gfloat pitch, gfloat spin,
                                                                    gint width, gint height);
G_GNUC_INTERNAL GrlImage *         editor_mat_preview_render_image (EditorMatPreview *self, LrgMaterial *material,
                                                                    LrgShaderAsset *shader, guint shape,
                                                                    gfloat yaw, gfloat pitch, gfloat spin,
                                                                    guint size);
G_GNUC_INTERNAL LrgWidget *        editor_mat_preview_view_new     (LrgEditorApp *app, const gchar *name,
                                                                    EditorMatPreviewSource source,
                                                                    gpointer user_data);
G_GNUC_INTERNAL guint              editor_mat_preview_view_get_shape  (LrgWidget *view);
G_GNUC_INTERNAL void               editor_mat_preview_view_set_shape  (LrgWidget *view, guint shape);
G_GNUC_INTERNAL gboolean           editor_mat_preview_view_get_rotate (LrgWidget *view);
G_GNUC_INTERNAL void               editor_mat_preview_view_set_rotate (LrgWidget *view, gboolean rotate);
G_GNUC_INTERNAL GrlImage *         editor_mat_preview_view_render     (LrgWidget *view, guint size);
G_GNUC_INTERNAL gchar *            editor_mat_relative_to             (const gchar *from_dir, const gchar *path);
#endif
