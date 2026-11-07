/* test-editor-app-physasset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The 0.9.0 physics editing tools of lrgeditor that EDPHYSASSET owns, in
 * a headless LrgEditorApp (tests/editor-app-fixture.h):
 *
 *  - the `.rphysasset` document format: detection by extension and by
 *    content, the new-document skeleton loads, the data editor registry;
 *  - the physics asset editor: create from a model (one part per bone),
 *    the preview model in the `.meta` sidecar, select / add / remove parts,
 *    a grid drag is one undo step written in canonical form, shape and
 *    joint kind changes, Generate, Stabilize, exclusions, validation
 *    issues, the canvas (part rows, Add Part), Simulate drops the ragdoll
 *    and Reset puts it back;
 *  - every physasset.* IPC method and its errors;
 *  - physics.layers.*: names, the collide matrix, world settings written
 *    to project.ryaml's physics: block and kept across other saves;
 *  - Project Settings > Physics: layer names, a click in the collide
 *    matrix, gravity, Save; Cancel keeps the project unchanged;
 *  - the assets panel's New Physics Asset and Physics Asset from Model;
 *  - /gpu/: the preview renders (only under the isolated Xvfb of
 *    build-aux/xrun.sh); with LRG_TEST_SHOT_DIR set the frames are saved.
 *
 * Every model is written by the test itself: a 14-bone T-pose humanoid
 * (the skeleton of tests/test-ragdoll.c) skinned with one box per bone.
 * Nothing is downloaded.
 */

#include "editor-app-fixture.h"
#include "lrg-test-glb.h"
#include <raylib.h>
#include <math.h>

/* ==========================================================================
 * The humanoid model
 * ========================================================================== */

typedef struct
{
	const gchar *name;
	gint         parent;
	gfloat       x, y, z;     /* relative to the parent */
} BoneDef;

static const BoneDef HUMANOID[] = {
	{ "pelvis", -1, 0.0f, 1.0f, 0.0f },
	{ "spine", 0, 0.0f, 0.25f, 0.0f },
	{ "head", 1, 0.0f, 0.35f, 0.0f },
	{ "upper_arm_l", 1, 0.2f, 0.25f, 0.0f },
	{ "lower_arm_l", 3, 0.3f, 0.0f, 0.0f },
	{ "hand_l", 4, 0.25f, 0.0f, 0.0f },
	{ "upper_arm_r", 1, -0.2f, 0.25f, 0.0f },
	{ "lower_arm_r", 6, -0.3f, 0.0f, 0.0f },
	{ "hand_r", 7, -0.25f, 0.0f, 0.0f },
	{ "thigh_l", 0, 0.12f, -0.05f, 0.0f },
	{ "shin_l", 9, 0.0f, -0.45f, 0.0f },
	{ "foot_l", 10, 0.0f, -0.42f, 0.0f },
	{ "thigh_r", 0, -0.12f, -0.05f, 0.0f },
	{ "shin_r", 12, 0.0f, -0.45f, 0.0f },
	{ "foot_r", 13, 0.0f, -0.42f, 0.0f },
};

#define N_BONES (G_N_ELEMENTS (HUMANOID))

static void
bone_world (guint   i,
            gfloat *p)
{
	gint b = (gint) i;

	p[0] = p[1] = p[2] = 0.0f;
	while (b >= 0)
	{
		p[0] += HUMANOID[b].x;
		p[1] += HUMANOID[b].y;
		p[2] += HUMANOID[b].z;
		b = HUMANOID[b].parent;
	}
}

/*
 * write_humanoid:
 * Nodes: 0 "Armature" (the root bone and the mesh), 1..N the bones,
 * N + 1 "Body" (mesh 0, skin 0). The mesh is one axis-aligned box per bone
 * from the bone to its first child (a small cube on leaves), weighted
 * fully to the bone. Rest rotations are identity, so the inverse bind
 * matrices are plain translations.
 */
static void
write_humanoid (const gchar *path)
{
	static const gint   faces[6][4][3] = {
		{ { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } }, { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } },
		{ { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } }, { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } },
		{ { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 } }, { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } }
	};
	static const gfloat normals[6][3] = { { 0, 0, -1 }, { 0, 0, 1 }, { 0, -1, 0 }, { 0, 1, 0 }, { -1, 0, 0 }, { 1, 0, 0 } };
	g_autoptr(GByteArray) bin = g_byte_array_new ();
	g_autoptr(GArray)     pos = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(GArray)     nrm = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(GArray)     jnt = g_array_new (FALSE, FALSE, sizeof (guint8));
	g_autoptr(GArray)     wgt = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(GArray)     idx = g_array_new (FALSE, FALSE, sizeof (guint16));
	g_autoptr(GString)    json = g_string_new (NULL);
	gfloat                mn[3] = { G_MAXFLOAT, G_MAXFLOAT, G_MAXFLOAT };
	gfloat                mx[3] = { -G_MAXFLOAT, -G_MAXFLOAT, -G_MAXFLOAT };
	gsize                 o_nrm, o_jnt, o_wgt, o_idx, o_ibm, l_idx;
	guint                 i, k, f, c, nv;

	for (i = 0; i < N_BONES; i++)
	{
		gfloat p0[3], p1[3], lo[3], hi[3];
		gint   child = -1;
		gfloat half = (i == 2) ? 0.11f : (i <= 1 ? 0.13f : 0.055f);

		bone_world (i, p0);
		for (k = 0; k < N_BONES && child < 0; k++)
			if (HUMANOID[k].parent == (gint) i)
				child = (gint) k;
		if (child >= 0)
			bone_world ((guint) child, p1);
		else
		{
			memcpy (p1, p0, sizeof p1);
			p1[1] += (i == 11 || i == 14) ? -0.06f : 0.0f;
			p1[0] += (i == 5) ? 0.12f : (i == 8 ? -0.12f : 0.0f);
		}
		for (k = 0; k < 3; k++)
		{
			lo[k] = MIN (p0[k], p1[k]) - half;
			hi[k] = MAX (p0[k], p1[k]) + half;
		}
		/* the head: a box around the head bone */
		if (i == 2)
		{
			lo[1] = p0[1] - 0.02f;
			hi[1] = p0[1] + 0.24f;
		}
		for (f = 0; f < 6; f++)
		{
			guint16 base = (guint16) (pos->len / 3);
			guint16 tri[6] = { 0, 1, 2, 0, 2, 3 };

			for (c = 0; c < 4; c++)
			{
				gfloat v[3];
				guint8 jj[4] = { (guint8) i, 0, 0, 0 };
				gfloat ww[4] = { 1, 0, 0, 0 };

				for (k = 0; k < 3; k++)
				{
					v[k] = faces[f][c][k] ? hi[k] : lo[k];
					mn[k] = MIN (mn[k], v[k]);
					mx[k] = MAX (mx[k], v[k]);
				}
				g_array_append_vals (pos, v, 3);
				g_array_append_vals (nrm, normals[f], 3);
				g_array_append_vals (jnt, jj, 4);
				g_array_append_vals (wgt, ww, 4);
			}
			for (c = 0; c < 6; c++)
			{
				guint16 x = (guint16) (base + tri[c]);

				g_array_append_val (idx, x);
			}
		}
	}
	nv = pos->len / 3;

	/* binary: positions | normals | joints | weights | indices | ibm */
	test_glb_put_floats (bin, (const gfloat *) (gconstpointer) pos->data, pos->len);
	o_nrm = bin->len;
	test_glb_put_floats (bin, (const gfloat *) (gconstpointer) nrm->data, nrm->len);
	o_jnt = bin->len;
	g_byte_array_append (bin, (const guint8 *) jnt->data, jnt->len);
	test_glb_pad4 (bin);
	o_wgt = bin->len;
	test_glb_put_floats (bin, (const gfloat *) (gconstpointer) wgt->data, wgt->len);
	o_idx = bin->len;
	g_byte_array_append (bin, (const guint8 *) idx->data, idx->len * 2);
	l_idx = idx->len * 2;
	test_glb_pad4 (bin);
	o_ibm = bin->len;
	for (i = 0; i < N_BONES; i++)
	{
		gfloat p[3];
		gfloat m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

		bone_world (i, p);
		m[12] = -p[0];
		m[13] = -p[1];
		m[14] = -p[2];
		test_glb_put_floats (bin, m, 16);
	}

	g_string_append (json, "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[");
	g_string_append_printf (json, "{\"name\":\"Armature\",\"children\":[1,%u]}", (guint) N_BONES + 1);
	for (i = 0; i < N_BONES; i++)
	{
		gboolean first = TRUE;

		g_string_append_printf (json, ",{\"name\":\"%s\",\"translation\":[%g,%g,%g]", HUMANOID[i].name,
		                        HUMANOID[i].x, HUMANOID[i].y, HUMANOID[i].z);
		for (k = 0; k < N_BONES; k++)
			if (HUMANOID[k].parent == (gint) i)
			{
				g_string_append_printf (json, "%s%u", first ? ",\"children\":[" : ",", k + 1);
				first = FALSE;
			}
		g_string_append (json, first ? "}" : "]}");
	}
	g_string_append_printf (json, ",{\"name\":\"Body\",\"mesh\":0,\"skin\":0}]");
	g_string_append (json, ",\"meshes\":[{\"name\":\"BodyMesh\",\"primitives\":[{\"attributes\":{\"POSITION\":0,"
	                       "\"NORMAL\":1,\"JOINTS_0\":2,\"WEIGHTS_0\":3},\"indices\":4}]}]");
	g_string_append (json, ",\"skins\":[{\"name\":\"Rig\",\"inverseBindMatrices\":5,\"joints\":[");
	for (i = 0; i < N_BONES; i++)
		g_string_append_printf (json, "%s%u", i ? "," : "", i + 1);
	g_string_append_printf (json, "]}],\"buffers\":[{\"byteLength\":%u}],\"bufferViews\":["
	                        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%u},"
	                        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
	                        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
	                        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
	                        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
	                        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}],",
	                        bin->len, (guint) o_nrm, (guint) o_nrm,
	                        (guint) (o_jnt - o_nrm), (guint) o_jnt, jnt->len, (guint) o_wgt, (guint) (o_idx - o_wgt),
	                        (guint) o_idx, (guint) l_idx, (guint) o_ibm, (guint) (16 * N_BONES * sizeof (gfloat)));
	g_string_append_printf (json, "\"accessors\":["
	                        "{\"bufferView\":0,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\","
	                        "\"min\":[%g,%g,%g],\"max\":[%g,%g,%g]},"
	                        "{\"bufferView\":1,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"},"
	                        "{\"bufferView\":2,\"componentType\":5121,\"count\":%u,\"type\":\"VEC4\"},"
	                        "{\"bufferView\":3,\"componentType\":5126,\"count\":%u,\"type\":\"VEC4\"},"
	                        "{\"bufferView\":4,\"componentType\":5123,\"count\":%u,\"type\":\"SCALAR\"},"
	                        "{\"bufferView\":5,\"componentType\":5126,\"count\":%u,\"type\":\"MAT4\"}]}",
	                        nv, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2], nv, nv, nv, idx->len, (guint) N_BONES);
	test_glb_write (path, json->str, bin->data, bin->len);
}

/* ==========================================================================
 * Fixtures and helpers
 * ========================================================================== */

/* Opens a project (once) and returns the absolute path of @rel. */
static gchar *
project_path (EditorAppFixture *f,
              const gchar      *rel)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	gchar            *path;

	if (lrg_editor_app_get_project_dir (f->app) == NULL)
	{
		g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Demo", &error));
		g_assert_no_error (error);
		efx_frames (f, 2);
	}
	path = g_build_filename (f->project, rel, NULL);
	dir = g_path_get_dirname (path);
	g_mkdir_with_parents (dir, 0755);
	return path;
}

static gchar *
write_model (EditorAppFixture *f,
             const gchar      *rel)
{
	gchar *path = project_path (f, rel);

	write_humanoid (path);
	return path;
}

static gboolean
loader_warnings_ok (const gchar    *domain,
                    GLogLevelFlags  level,
                    const gchar    *message,
                    gpointer        user_data)
{
	return !(domain != NULL && g_str_has_prefix (domain, "Libregnum-") && (level & G_LOG_LEVEL_WARNING) &&
	         g_strcmp0 (domain, "Libregnum-Editor") != 0);
}

static gchar *
doc_text (LrgEditorPhysicsAssetEditor *ed)
{
	return lrg_editor_document_to_data (lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (ed)), NULL);
}

static LrgEditorDocument *
doc_of (LrgEditorPhysicsAssetEditor *ed)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (ed));
}

/* chars/hero.glb and chars/hero.rphysasset generated from it, opened. */
static LrgEditorPhysicsAssetEditor *
hero_asset (EditorAppFixture *f)
{
	g_autofree gchar            *glb = write_model (f, "chars/hero.glb");
	g_autofree gchar            *path = project_path (f, "chars/hero.rphysasset");
	g_autoptr(GError)            error = NULL;
	LrgEditorPhysicsAssetEditor *ed;
	guint                        n = 0;

	g_assert_true (lrg_editor_physics_asset_editor_create (f->app, "chars/hero.rphysasset", "chars/hero.glb", 70.0f,
	                                                       0.2f, &n, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (n, >=, 10);
	ed = lrg_editor_physics_asset_editor_open (f->app, path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (ed);
	efx_frames (f, 3);
	return ed;
}

static LrgWidget *
find_in (LrgEditorPhysicsAssetEditor *ed,
         const gchar                 *name)
{
	LrgWidget *w = lrg_widget_find (LRG_WIDGET (ed), name);

	g_assert_nonnull (w);
	return w;
}

/* Scrolls @name into view inside the editor's lower scroll box, clicks it. */
static void
click_in (EditorAppFixture            *f,
          LrgEditorPhysicsAssetEditor *ed,
          const gchar                 *name)
{
	LrgWidget *w = find_in (ed, name);

	lrg_scroll_box_scroll_to_widget (LRG_SCROLL_BOX (find_in (ed, "physasset-grid-scroll")), w);
	efx_frames (f, 30);
	efx_click_widget (f, w);
}

/* ==========================================================================
 * Formats
 * ========================================================================== */

static void
test_formats (EditorAppFixture *f,
              gconstpointer     data)
{
	static const gchar         text[] = "version: 1\nparts:\n- bone: pelvis\n  shape: sphere\n  radius: 0.2\n";
	g_autoptr(LrgEditorDocument) doc = lrg_editor_document_new (LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET);
	g_autofree gchar            *fresh = lrg_editor_document_to_data (doc, NULL);
	g_autoptr(LrgPhysicsAsset)   asset = NULL;
	g_autoptr(GError)            error = NULL;

	g_assert_cmpint (lrg_editor_document_detect_format ("a/hero.rphysasset", NULL, 0), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET);
	g_assert_cmpint (lrg_editor_document_detect_format ("ragdoll.yaml", text, -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET);
	g_assert_cmpint (lrg_editor_document_detect_format ("x.yaml", "version: 1\nparts: []\n", -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET);
	/* a parts: list of non-bone items is not a physics asset */
	g_assert_cmpint (lrg_editor_document_detect_format ("x.yaml", "version: 1\nparts: [a, b]\n", -1), !=,
	                 LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET);
	g_assert_cmpstr (lrg_editor_document_format_to_string (LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET), ==,
	                 "physics-asset");
	/* the new-document skeleton loads with the engine's strict loader */
	asset = lrg_physics_asset_new_from_data (fresh, -1, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_physics_asset_get_part_count (asset), ==, 0);
	g_assert_true (lrg_editor_data_editor_lookup_type (LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET) ==
	               LRG_TYPE_EDITOR_PHYSICS_ASSET_EDITOR);
	{
		g_autofree gchar *p = project_path (f, "r.rphysasset");

		g_assert_true (g_file_set_contents (p, text, -1, NULL));
		g_assert_true (lrg_editor_data_editor_can_open (p));
	}
}

/* ==========================================================================
 * The editor
 * ========================================================================== */

static void
test_open_generate (EditorAppFixture *f,
                    gconstpointer     data)
{
	LrgEditorPhysicsAssetEditor *ed = hero_asset (f);
	LrgPhysicsAsset             *asset = lrg_editor_physics_asset_editor_get_asset (ed);
	g_autofree gchar            *meta = NULL;
	g_autofree gchar            *meta_text = NULL;
	g_autoptr(GError)            error = NULL;
	gfloat                       before, after;
	guint                        n, i;

	g_assert_nonnull (asset);
	g_assert_null (lrg_editor_physics_asset_editor_get_load_error (ed));
	g_assert_nonnull (lrg_editor_physics_asset_editor_get_skeleton (ed));
	g_assert_true (g_str_has_suffix (lrg_editor_physics_asset_editor_get_model (ed), "chars/hero.glb"));
	g_assert_cmpstr (lrg_physics_asset_get_name (asset), ==, "hero");
	g_assert_cmpint (lrg_editor_physics_asset_editor_get_selected_part (ed), ==, 0);
	/* the model is remembered in the sidecar, not in the strict format */
	meta = g_build_filename (f->project, "chars", "hero.rphysasset.meta", NULL);
	g_assert_true (g_file_get_contents (meta, &meta_text, NULL, NULL));
	g_assert_nonnull (strstr (meta_text, "preview_model"));
	g_assert_nonnull (strstr (meta_text, "hero.glb"));

	/* a part per bone that the generator gives one */
	n = lrg_physics_asset_get_part_count (asset);
	g_assert_cmpint (lrg_physics_asset_find_part (asset, "pelvis"), >=, 0);
	g_assert_cmpint (lrg_physics_asset_find_part (asset, "shin_l"), >=, 0);
	g_assert_nonnull (find_in (ed, "physasset-part-pelvis"));

	/* Generate with another mass replaces every part, one undo step */
	before = 0.0f;
	for (i = 0; i < n; i++)
		before += lrg_physics_asset_get_part (asset, i)->mass;
	g_assert_cmpfloat_with_epsilon (before, 70.0, 0.05);
	g_assert_true (lrg_editor_physics_asset_editor_generate (ed, 90.0f, 0.25f, &error));
	g_assert_no_error (error);
	efx_frames (f, 2);
	asset = lrg_editor_physics_asset_editor_get_asset (ed);
	after = 0.0f;
	for (i = 0; i < lrg_physics_asset_get_part_count (asset); i++)
		after += lrg_physics_asset_get_part (asset, i)->mass;
	g_assert_cmpfloat_with_epsilon (after, 90.0, 0.05);
	g_assert_cmpstr (lrg_physics_asset_get_name (asset), ==, "hero");
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc_of (ed)), ==, "Generate from Skeleton");
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	efx_frames (f, 1);
	asset = lrg_editor_physics_asset_editor_get_asset (ed);
	after = 0.0f;
	for (i = 0; i < lrg_physics_asset_get_part_count (asset); i++)
		after += lrg_physics_asset_get_part (asset, i)->mass;
	g_assert_cmpfloat_with_epsilon (after, 70.0, 0.05);

	/* Stabilize: a heavy hand gets evened out against its forearm */
	{
		gint                           hi = lrg_physics_asset_find_part (asset, "lower_arm_l");
		g_autoptr(LrgPhysicsAssetPart) p = lrg_physics_asset_part_copy (lrg_physics_asset_get_part (asset,
		                                                                                         (guint) hi));
		g_autoptr(GPtrArray)           issues = NULL;
		gboolean                       warned = FALSE;
		guint                          k;

		p->mass = 40.0f;
		g_assert_true (lrg_editor_physics_asset_editor_set_part (ed, (guint) hi, p, &error));
		g_assert_no_error (error);
		efx_frames (f, 1);
		lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (ed));
		issues = g_ptr_array_ref (lrg_editor_data_editor_get_issues (LRG_EDITOR_DATA_EDITOR (ed)));
		for (k = 0; k < issues->len; k++)
			if (strstr (((LrgEditorDocumentIssue *) g_ptr_array_index (issues, k))->message, "Stabilize") != NULL)
				warned = TRUE;
		g_assert_true (warned);
		g_assert_true (lrg_editor_physics_asset_editor_stabilize (ed, &error));
		g_assert_no_error (error);
		efx_frames (f, 1);
		asset = lrg_editor_physics_asset_editor_get_asset (ed);
		g_assert_cmpfloat (lrg_physics_asset_get_part (asset, (guint) hi)->mass, <, 40.0f);
		g_assert_cmpstr (lrg_editor_document_get_undo_label (doc_of (ed)), ==, "Stabilize Masses");
	}
}

static void
test_parts (EditorAppFixture *f,
            gconstpointer     data)
{
	LrgEditorPhysicsAssetEditor   *ed = hero_asset (f);
	LrgPhysicsAsset               *asset;
	g_autoptr(GError)              error = NULL;
	g_autoptr(LrgPhysicsAssetPart) part = NULL;
	g_autofree gchar              *text = NULL;
	gint                           foot, index;
	guint                          n;

	asset = lrg_editor_physics_asset_editor_get_asset (ed);
	n = lrg_physics_asset_get_part_count (asset);

	/* remove one, then add it back through the canvas (bone picker) */
	foot = lrg_physics_asset_find_part (asset, "foot_r");
	if (foot < 0)
		foot = lrg_physics_asset_find_part (asset, "shin_r");
	g_assert_cmpint (foot, >=, 0);
	{
		g_autofree gchar *bone = g_strdup (lrg_physics_asset_get_part (asset, (guint) foot)->bone);
		LrgComboBox      *combo;
		guint             k;
		gboolean          listed = FALSE;

		g_assert_true (lrg_editor_physics_asset_editor_remove_part (ed, (guint) foot, &error));
		g_assert_no_error (error);
		efx_frames (f, 2);
		asset = lrg_editor_physics_asset_editor_get_asset (ed);
		g_assert_cmpuint (lrg_physics_asset_get_part_count (asset), ==, n - 1);
		combo = LRG_COMBO_BOX (find_in (ed, "physasset-add-bone"));
		for (k = 0; k < (guint) lrg_combo_box_get_n_items (combo); k++)
			if (g_strcmp0 (lrg_combo_box_get_item_text (combo, (gint) k), bone) == 0)
			{
				lrg_combo_box_set_selected (combo, (gint) k);
				listed = TRUE;
			}
		g_assert_true (listed);
		efx_click_widget (f, find_in (ed, "physasset-add-part"));
		efx_frames (f, 2);
		asset = lrg_editor_physics_asset_editor_get_asset (ed);
		g_assert_cmpuint (lrg_physics_asset_get_part_count (asset), ==, n);
		g_assert_cmpint (lrg_physics_asset_find_part (asset, bone), ==, (gint) n - 1);
		g_assert_cmpint (lrg_editor_physics_asset_editor_get_selected_part (ed), ==, (gint) n - 1);
	}

	/* a click on a part row selects it */
	efx_click_widget (f, find_in (ed, "physasset-part-spine"));
	efx_frames (f, 1);
	index = lrg_physics_asset_find_part (lrg_editor_physics_asset_editor_get_asset (ed), "spine");
	g_assert_cmpint (lrg_editor_physics_asset_editor_get_selected_part (ed), ==, index);

	/* adding a bone that has a part (or no such bone) fails */
	g_assert_cmpint (lrg_editor_physics_asset_editor_add_part (ed, "spine", &error), <, 0);
	g_clear_error (&error);
	g_assert_cmpint (lrg_editor_physics_asset_editor_add_part (ed, "tail", &error), <, 0);
	g_clear_error (&error);

	/* shape change: capsule -> box keeps the size around it */
	asset = lrg_editor_physics_asset_editor_get_asset (ed);
	part = lrg_physics_asset_part_copy (lrg_physics_asset_get_part (asset, (guint) index));
	part->shape = LRG_PHYSICS_ASSET_SHAPE_BOX;
	part->half_extents[0] = 0.15f;
	part->half_extents[1] = 0.1f;
	part->half_extents[2] = 0.08f;
	part->joint = LRG_JOINT_HINGE;
	part->twist_min = -0.1f;
	part->twist_max = 1.2f;
	g_assert_true (lrg_editor_physics_asset_editor_set_part (ed, (guint) index, part, &error));
	g_assert_no_error (error);
	efx_frames (f, 2);
	text = doc_text (ed);
	/* canonical: a box has no radius, a hinge no swing */
	{
		g_autoptr(LrgPhysicsAsset) re = lrg_physics_asset_new_from_data (text, -1, &error);
		const LrgPhysicsAssetPart *p;

		g_assert_no_error (error);
		p = lrg_physics_asset_get_part (re, (guint) index);
		g_assert_cmpint (p->shape, ==, LRG_PHYSICS_ASSET_SHAPE_BOX);
		g_assert_cmpint (p->joint, ==, LRG_JOINT_HINGE);
		g_assert_cmpfloat_with_epsilon (p->twist_max, 1.2, 1e-5);
	}
	/* invalid values are refused and leave the document alone */
	part->mass = -1.0f;
	g_assert_false (lrg_editor_physics_asset_editor_set_part (ed, (guint) index, part, &error));
	g_assert_nonnull (error);
	g_clear_error (&error);
	{
		g_autofree gchar *again = doc_text (ed);

		g_assert_cmpstr (again, ==, text);
	}

	/* exclusions */
	g_assert_true (lrg_editor_physics_asset_editor_set_exclusion (ed, "thigh_l", "thigh_r", TRUE, &error));
	g_assert_no_error (error);
	efx_frames (f, 2);
	g_assert_cmpuint (lrg_physics_asset_get_exclusion_count (lrg_editor_physics_asset_editor_get_asset (ed)), ==, 1);
	click_in (f, ed, "physasset-exclusion-remove-0");
	efx_frames (f, 2);
	g_assert_cmpuint (lrg_physics_asset_get_exclusion_count (lrg_editor_physics_asset_editor_get_asset (ed)), ==, 0);
	g_assert_false (lrg_editor_physics_asset_editor_set_exclusion (ed, "spine", "spine", TRUE, &error));
	g_clear_error (&error);
}

/* A drag on a grid row is one undo step, written canonically. */
static void
test_grid_gesture (EditorAppFixture *f,
                   gconstpointer     data)
{
	LrgEditorPhysicsAssetEditor *ed = hero_asset (f);
	LrgPropertyGrid             *grid = LRG_PROPERTY_GRID (find_in (ed, "physasset-grid"));
	LrgPropertyRow              *row;
	g_autofree gchar            *start = doc_text (ed);
	GValue                       v = G_VALUE_INIT;
	const LrgPhysicsAssetPart   *p;
	gint                         spine;
	guint                        k;

	spine = lrg_physics_asset_find_part (lrg_editor_physics_asset_editor_get_asset (ed), "spine");
	g_assert_true (lrg_editor_physics_asset_editor_select_part (ed, spine));
	efx_frames (f, 2);
	row = lrg_property_grid_get_row (grid, "mass");
	g_assert_nonnull (row);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "twist-min"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "swing-y"));
	g_assert_null (lrg_property_grid_get_row (grid, "extent-x"));

	g_value_init (&v, G_TYPE_DOUBLE);
	lrg_property_row_begin_edit (row);
	for (k = 1; k <= 5; k++)
	{
		g_value_set_double (&v, 3.0 + k);
		lrg_property_row_edit_value (row, &v, TRUE);
		efx_frame (f);
	}
	lrg_property_row_end_edit (row, TRUE);
	g_value_unset (&v);
	efx_frames (f, 2);
	p = lrg_physics_asset_get_part (lrg_editor_physics_asset_editor_get_asset (ed), (guint) spine);
	g_assert_cmpfloat_with_epsilon (p->mass, 8.0, 1e-4);
	/* one undo step back to the start */
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	efx_frames (f, 1);
	{
		g_autofree gchar *now = doc_text (ed);

		g_assert_cmpstr (now, ==, start);
	}

	/* the shape row switches the rows shown */
	row = lrg_property_grid_get_row (grid, "shape");
	g_assert_nonnull (row);
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "box");
	lrg_property_row_apply_value (row, &v);
	g_value_unset (&v);
	efx_frames (f, 3);
	p = lrg_physics_asset_get_part (lrg_editor_physics_asset_editor_get_asset (ed), (guint) spine);
	g_assert_cmpint (p->shape, ==, LRG_PHYSICS_ASSET_SHAPE_BOX);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "extent-x"));
	g_assert_null (lrg_property_grid_get_row (grid, "radius"));

	/* the joint kind too; angles go through in radians */
	row = lrg_property_grid_get_row (grid, "joint");
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "cone");
	lrg_property_row_apply_value (row, &v);
	g_value_unset (&v);
	efx_frames (f, 3);
	g_assert_null (lrg_property_grid_get_row (grid, "twist-min"));
	row = lrg_property_grid_get_row (grid, "swing-z");
	g_assert_nonnull (row);
	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, 0.75);
	lrg_property_row_apply_value (row, &v);
	g_value_unset (&v);
	efx_frames (f, 2);
	p = lrg_physics_asset_get_part (lrg_editor_physics_asset_editor_get_asset (ed), (guint) spine);
	g_assert_cmpint (p->joint, ==, LRG_JOINT_CONE);
	g_assert_cmpfloat_with_epsilon (p->swing_z, 0.75, 1e-5);

	/* a rotation row writes a normalised quaternion */
	row = lrg_property_grid_get_row (grid, "rotation-z");
	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, G_PI / 2.0);
	lrg_property_row_apply_value (row, &v);
	g_value_unset (&v);
	efx_frames (f, 2);
	p = lrg_physics_asset_get_part (lrg_editor_physics_asset_editor_get_asset (ed), (guint) spine);
	g_assert_cmpfloat_with_epsilon (p->rotation[2], sinf (G_PI / 4.0), 1e-4);
	g_assert_cmpfloat_with_epsilon (p->rotation[3], cosf (G_PI / 4.0), 1e-4);
}

static void
test_simulate (EditorAppFixture *f,
               gconstpointer     data)
{
	LrgEditorPhysicsAssetEditor *ed = hero_asset (f);
	LrgSkeleton                 *skel = lrg_editor_physics_asset_editor_get_skeleton (ed);
	LrgBone                     *head = lrg_skeleton_get_bone_by_name (skel, "head");
	g_autoptr(GError)            error = NULL;
	gfloat                       y0 = lrg_bone_get_world_pose (head)->position_y;

	g_assert_true (lrg_editor_physics_asset_editor_set_simulating (ed, TRUE, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_editor_physics_asset_editor_get_simulating (ed));
	g_assert_nonnull (lrg_editor_physics_asset_editor_get_ragdoll (ed));
	lrg_editor_physics_asset_editor_advance (ed, 2.5f);
	/* the limp body falls: the head ends well below its standing height
	 * but stays above the ground plane */
	g_assert_cmpfloat (lrg_bone_get_world_pose (head)->position_y, <, y0 - 0.5f);
	g_assert_cmpfloat (lrg_bone_get_world_pose (head)->position_y, >, -0.2f);
	/* the frames also step it */
	efx_frames (f, 3);
	g_assert_true (lrg_editor_physics_asset_editor_get_simulating (ed));

	/* the Stop button ends it and puts the model back */
	efx_click_widget (f, find_in (ed, "physasset-simulate"));
	efx_frames (f, 1);
	g_assert_false (lrg_editor_physics_asset_editor_get_simulating (ed));
	g_assert_cmpfloat_with_epsilon (lrg_bone_get_world_pose (head)->position_y, y0, 1e-4);

	/* an edit while simulating stops it (the ragdoll holds the old asset) */
	g_assert_true (lrg_editor_physics_asset_editor_set_simulating (ed, TRUE, &error));
	g_assert_true (lrg_editor_physics_asset_editor_set_exclusion (ed, "thigh_l", "thigh_r", TRUE, &error));
	efx_frames (f, 1);
	g_assert_false (lrg_editor_physics_asset_editor_get_simulating (ed));

	/* without a model there is nothing to simulate */
	g_assert_true (lrg_editor_physics_asset_editor_set_model (ed, NULL, &error));
	g_assert_null (lrg_editor_physics_asset_editor_get_skeleton (ed));
	g_assert_false (lrg_editor_physics_asset_editor_set_simulating (ed, TRUE, &error));
	g_assert_nonnull (error);
}

/* ==========================================================================
 * IPC: physasset.*
 * ========================================================================== */

/* Whether the request is answered with an error. */
static gboolean
ipc_fails (EditorAppFixture *f,
           const gchar      *request)
{
	g_autofree gchar *reply = efx_ipc (f, request);

	return reply != NULL && strstr (reply, "\"error\"") != NULL;
}

static JsonObject *
ipc_object (EditorAppFixture *f,
            const gchar      *request,
            JsonNode        **keep)
{
	*keep = efx_ipc_result (f, request);
	g_assert_nonnull (*keep);
	return json_node_get_object (*keep);
}

static void
test_ipc (EditorAppFixture *f,
          gconstpointer     data)
{
	g_autofree gchar    *glb = write_model (f, "chars/hero.glb");
	g_autoptr(JsonNode)  r = NULL;
	JsonObject          *o;
	JsonArray           *parts;
	guint                n;

	o = ipc_object (f, "{'id':1,'method':'physasset.create','params':{'path':'chars/hero.rphysasset',"
	                   "'model':'chars/hero.glb','total_mass':80}}", &r);
	g_assert_true (g_str_has_suffix (json_object_get_string_member (o, "path"), "chars/hero.rphysasset"));
	n = (guint) json_object_get_int_member (o, "parts");
	g_assert_cmpuint (n, >=, 10);
	g_clear_pointer (&r, json_node_unref);

	/* creating it again fails */
	g_assert_true (ipc_fails (f, "{'id':2,'method':'physasset.create','params':{'path':'chars/hero.rphysasset'}}"));

	o = ipc_object (f, "{'id':3,'method':'physasset.get','params':{'path':'chars/hero.rphysasset'}}", &r);
	parts = json_object_get_array_member (o, "parts");
	g_assert_cmpuint (json_array_get_length (parts), ==, n);
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (o, "total_mass"), 80.0, 0.05);
	g_assert_true (g_str_has_suffix (json_object_get_string_member (o, "model"), "hero.glb"));
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "bones")), ==, N_BONES);
	g_assert_true (json_object_get_null_member (json_array_get_object_element (parts, 0), "parent"));
	g_clear_pointer (&r, json_node_unref);

	/* set_part by bone name with degrees */
	o = ipc_object (f, "{'id':4,'method':'physasset.set_part','params':{'path':'chars/hero.rphysasset',"
	                   "'part':'shin_l','values':{'joint':'hinge','twist_degrees':[0,140],'mass':4.5,"
	                   "'offset':[0,-0.2,0]}}}", &r);
	g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (o, "joint"), "kind"), ==, "hinge");
	g_assert_cmpfloat_with_epsilon (json_array_get_double_element (json_object_get_array_member (
	                                    json_object_get_object_member (o, "joint"), "twist"), 1), 140.0 * G_PI / 180.0,
	                                1e-4);
	g_assert_cmpstr (json_object_get_string_member (o, "parent"), ==, "thigh_l");
	g_clear_pointer (&r, json_node_unref);
	g_assert_true (ipc_fails (f, "{'id':5,'method':'physasset.set_part','params':{'path':'chars/hero.rphysasset',"
	                "'part':'shin_l','values':{'mass':-2}}}"));
	g_assert_true (ipc_fails (f, "{'id':6,'method':'physasset.set_part','params':{'path':'chars/hero.rphysasset',"
	                "'part':'nope','values':{'mass':2}}}"));

	/* remove / add a part */
	o = ipc_object (f, "{'id':7,'method':'physasset.remove_part','params':{'path':'chars/hero.rphysasset',"
	                   "'part':'head'}}", &r);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "parts")), ==, n - 1);
	g_clear_pointer (&r, json_node_unref);
	o = ipc_object (f, "{'id':8,'method':'physasset.add_part','params':{'path':'chars/hero.rphysasset',"
	                   "'bone':'head','values':{'shape':'sphere','radius':0.12}}}", &r);
	g_assert_cmpint (json_object_get_int_member (o, "index"), ==, (gint64) n - 1);
	g_clear_pointer (&r, json_node_unref);

	/* exclusions, asset fields, select */
	o = ipc_object (f, "{'id':9,'method':'physasset.exclude','params':{'path':'chars/hero.rphysasset',"
	                   "'a':'thigh_l','b':'thigh_r'}}", &r);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "exclusions")), ==, 1);
	g_clear_pointer (&r, json_node_unref);
	o = ipc_object (f, "{'id':10,'method':'physasset.set','params':{'path':'chars/hero.rphysasset',"
	                   "'name':'Hero ragdoll','exclude_adjacent':false}}", &r);
	g_assert_cmpstr (json_object_get_string_member (o, "name"), ==, "Hero ragdoll");
	g_assert_false (json_object_get_boolean_member (o, "exclude_adjacent"));
	g_clear_pointer (&r, json_node_unref);
	o = ipc_object (f, "{'id':11,'method':'physasset.select','params':{'path':'chars/hero.rphysasset',"
	                   "'part':'spine'}}", &r);
	g_assert_cmpstr (json_object_get_string_member (o, "bone"), ==, "spine");
	g_clear_pointer (&r, json_node_unref);

	/* generate, stabilize */
	o = ipc_object (f, "{'id':12,'method':'physasset.generate','params':{'path':'chars/hero.rphysasset',"
	                   "'total_mass':60,'radius_ratio':0.3}}", &r);
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (o, "total_mass"), 60.0, 0.05);
	g_clear_pointer (&r, json_node_unref);
	o = ipc_object (f, "{'id':13,'method':'physasset.stabilize','params':{'path':'chars/hero.rphysasset'}}", &r);
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (o, "total_mass"), 60.0, 0.05);
	g_clear_pointer (&r, json_node_unref);

	/* simulate with advance, then reset */
	o = ipc_object (f, "{'id':14,'method':'physasset.simulate','params':{'path':'chars/hero.rphysasset',"
	                   "'advance':1.0}}", &r);
	g_assert_true (json_object_get_boolean_member (o, "simulating"));
	g_assert_cmpfloat (json_object_get_double_member (o, "time"), >=, 0.99);
	g_clear_pointer (&r, json_node_unref);
	o = ipc_object (f, "{'id':15,'method':'physasset.simulate','params':{'path':'chars/hero.rphysasset',"
	                   "'run':false}}", &r);
	g_assert_false (json_object_get_boolean_member (o, "simulating"));
	g_clear_pointer (&r, json_node_unref);

	/* save, the file loads in the engine */
	o = ipc_object (f, "{'id':16,'method':'physasset.save','params':{'path':'chars/hero.rphysasset'}}", &r);
	{
		g_autoptr(GError)          error = NULL;
		g_autoptr(LrgPhysicsAsset) a = lrg_physics_asset_new_from_file (json_object_get_string_member (o, "path"),
		                                                                &error);

		g_assert_no_error (error);
		g_assert_cmpstr (lrg_physics_asset_get_name (a), ==, "Hero ragdoll");
	}
	g_clear_pointer (&r, json_node_unref);

	/* set_model to a missing file fails; open of a non physics asset too */
	g_assert_true (ipc_fails (f, "{'id':17,'method':'physasset.set_model','params':{'path':'chars/hero.rphysasset',"
	                "'model':'missing.glb'}}"));
	g_assert_true (ipc_fails (f, "{'id':18,'method':'physasset.open','params':{'path':'nope.rphysasset'}}"));
}

/* ==========================================================================
 * physics.layers.*
 * ========================================================================== */

static void
test_layers_ipc (EditorAppFixture *f,
                 gconstpointer     data)
{
	g_autofree gchar      *manifest = project_path (f, "project.ryaml");
	g_autoptr(JsonNode)    r = NULL;
	g_autoptr(LrgProject)  reopened = NULL;
	g_autoptr(GError)      error = NULL;
	g_autofree gchar      *text = NULL;
	JsonObject            *o;
	JsonArray             *names;
	LrgPhysicsLayers      *layers;

	o = ipc_object (f, "{'id':1,'method':'physics.layers.get'}", &r);
	names = json_object_get_array_member (o, "layers");
	g_assert_cmpuint (json_array_get_length (names), ==, 32);
	g_assert_cmpstr (json_array_get_string_element (names, 7), ==, "ragdoll");
	g_assert_cmpint (json_object_get_int_member (o, "count"), ==, 9);
	g_assert_cmpfloat_with_epsilon (json_array_get_double_element (json_object_get_array_member (
	                                    json_object_get_object_member (o, "settings"), "gravity"), 1), -9.81, 1e-4);
	g_clear_pointer (&r, json_node_unref);

	o = ipc_object (f, "{'id':2,'method':'physics.layers.set_name','params':{'index':9,'name':'water'}}", &r);
	g_assert_cmpint (json_object_get_int_member (o, "count"), ==, 10);
	g_clear_pointer (&r, json_node_unref);
	g_assert_true (ipc_fails (f, "{'id':3,'method':'physics.layers.set_name','params':{'index':10,'name':'water'}}"));
	o = ipc_object (f, "{'id':4,'method':'physics.layers.set_collides','params':{'a':'water','b':'debris',"
	                   "'collides':false}}", &r);
	g_clear_pointer (&r, json_node_unref);
	g_assert_true (ipc_fails (f, "{'id':5,'method':'physics.layers.set_collides','params':{'a':'lava','b':2,'collides':false}}"));

	o = ipc_object (f, "{'id':6,'method':'physics.layers.settings','params':{'gravity':[0,-3.7,0],'iterations':16,"
	                   "'fixed_dt':0.008333}}", &r);
	g_assert_cmpint (json_object_get_int_member (o, "iterations"), ==, 16);
	g_assert_cmpfloat_with_epsilon (json_array_get_double_element (json_object_get_array_member (o, "gravity"), 1),
	                                -3.7, 1e-4);
	g_clear_pointer (&r, json_node_unref);
	g_assert_true (ipc_fails (f, "{'id':7,'method':'physics.layers.settings','params':{'iterations':99}}"));

	/* the file: LrgProject reads the layers, the settings sit beside them */
	reopened = lrg_project_open (f->project, &error);
	g_assert_no_error (error);
	layers = lrg_project_get_physics_layers (reopened);
	g_assert_cmpint (lrg_physics_layers_find (layers, "water"), ==, 9);
	g_assert_false (lrg_physics_layers_get_collides (layers, 9, LRG_PHYSICS_LAYER_DEBRIS));
	g_assert_true (g_file_get_contents (manifest, &text, NULL, NULL));
	g_assert_nonnull (strstr (text, "iterations: 16"));
	g_assert_nonnull (strstr (text, "-3.7"));

	/* another layer edit keeps the settings (LrgProject rewrites the block) */
	o = ipc_object (f, "{'id':8,'method':'physics.layers.set_name','params':{'index':9,'name':null}}", &r);
	g_assert_cmpint (json_object_get_int_member (o, "count"), ==, 9);
	g_assert_cmpint (json_object_get_int_member (json_object_get_object_member (o, "settings"), "iterations"), ==, 16);
	g_clear_pointer (&r, json_node_unref);
	o = ipc_object (f, "{'id':9,'method':'physics.layers.reset'}", &r);
	g_assert_cmpstr (json_array_get_string_element (json_object_get_array_member (o, "layers"), 1), ==, "static");
	g_clear_pointer (&r, json_node_unref);
	o = ipc_object (f, "{'id':10,'method':'physics.layers.settings','params':{'reset':true}}", &r);
	g_assert_cmpint (json_object_get_int_member (o, "iterations"), ==, 10);
	g_clear_pointer (&r, json_node_unref);
}

/* Without a project the methods report it. */
static void
test_layers_no_project (EditorAppFixture *f,
                        gconstpointer     data)
{
	efx_frames (f, 2);
	g_assert_true (ipc_fails (f, "{'id':1,'method':'physics.layers.get'}"));
}

/* ==========================================================================
 * Project Settings > Physics
 * ========================================================================== */

static gchar *
ui_dialog (EditorAppFixture *f)
{
	g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'ui.state'}");
	JsonNode           *d = json_object_get_member (json_node_get_object (r), "dialog");

	return JSON_NODE_HOLDS_NULL (d) ? NULL : g_strdup (json_node_get_string (d));
}

static LrgWidget *
popup_find (EditorAppFixture *f,
            const gchar      *name)
{
	LrgWidget *popup = lrg_canvas_get_top_popup (lrg_editor_app_get_canvas (f->app));
	LrgWidget *w;

	g_assert_nonnull (popup);
	w = lrg_widget_find (popup, name);
	g_assert_nonnull (w);
	return w;
}

static LrgWidget *
find_button (LrgWidget   *root,
             const gchar *text)
{
	GList *l;

	if (LRG_IS_BUTTON (root) && g_strcmp0 (lrg_button_get_text (LRG_BUTTON (root)), text) == 0)
		return root;
	if (!LRG_IS_CONTAINER (root))
		return NULL;
	for (l = lrg_container_get_children (LRG_CONTAINER (root)); l != NULL; l = l->next)
	{
		LrgWidget *found = find_button (l->data, text);

		if (found != NULL)
			return found;
	}
	return NULL;
}

static void
test_settings_dialog (EditorAppFixture *f,
                      gconstpointer     data)
{
	g_autofree gchar     *manifest = project_path (f, "project.ryaml");
	g_autofree gchar     *dialog = NULL;
	g_autoptr(JsonNode)   r = NULL;
	g_autoptr(LrgProject) reopened = NULL;
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *text = NULL;
	LrgWidget            *matrix;
	LrgTabView           *tabs;
	LrgPhysicsLayers     *layers;
	gfloat                x, y, w, h;
	gboolean              before;

	efx_frames (f, 2);
	r = efx_ipc_result (f, "{'id':1,'method':'physics.layers.open'}");
	g_clear_pointer (&r, json_node_unref);
	efx_frames (f, 4);
	dialog = ui_dialog (f);
	g_assert_cmpstr (dialog, ==, "project-settings");
	tabs = LRG_TAB_VIEW (popup_find (f, "project-settings-tabs"));
	g_assert_cmpuint (lrg_tab_view_get_active_tab (tabs), ==, 1);

	/* a name for slot 9 */
	lrg_text_input_set_text (LRG_TEXT_INPUT (popup_find (f, "project-physics-layer-9")), "water");
	efx_frames (f, 3);

	/* a click on the cell (debris, ragdoll): row 6, column 7 */
	matrix = popup_find (f, "project-physics-matrix");
	lrg_scroll_box_scroll_to_widget (LRG_SCROLL_BOX (popup_find (f, "project-physics-page")), matrix);
	efx_frames (f, 30);
	efx_rect_of (matrix, &x, &y, &w, &h);
	g_assert_cmpfloat (w, >, 150.0f + 10.0f * 19.0f - 1.0f);
	{
		g_autoptr(LrgProject) now = lrg_project_open (f->project, NULL);

		before = lrg_physics_layers_get_collides (lrg_project_get_physics_layers (now), 6, 7);
		g_assert_true (before);
	}
	efx_click (f, x + 150.0f + 7.5f * 19.0f, y + 22.0f + 6.5f * 19.0f);
	efx_frames (f, 2);
	g_assert_nonnull (strstr (lrg_label_get_text (LRG_LABEL (popup_find (f, "project-physics-status"))),
	                          "never collide"));

	/* gravity on the moon */
	lrg_spin_box_set_value (LRG_SPIN_BOX (popup_find (f, "project-physics-gravity-y")), -1.62);
	efx_frames (f, 2);
	efx_click_widget (f, find_button (lrg_canvas_get_top_popup (lrg_editor_app_get_canvas (f->app)), "Save"));
	efx_frames (f, 20);
	g_clear_pointer (&dialog, g_free);
	dialog = ui_dialog (f);
	g_assert_null (dialog);

	reopened = lrg_project_open (f->project, &error);
	g_assert_no_error (error);
	layers = lrg_project_get_physics_layers (reopened);
	g_assert_cmpint (lrg_physics_layers_find (layers, "water"), ==, 9);
	g_assert_false (lrg_physics_layers_get_collides (layers, LRG_PHYSICS_LAYER_DEBRIS, LRG_PHYSICS_LAYER_RAGDOLL));
	g_assert_true (g_file_get_contents (manifest, &text, NULL, NULL));
	g_assert_nonnull (strstr (text, "-1.62"));
	/* the live project has the table too (the world reads it) */
	r = efx_ipc_result (f, "{'id':2,'method':'physics.layers.get'}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "count"), ==, 10);
	g_clear_pointer (&r, json_node_unref);

	/* Cancel changes nothing */
	r = efx_ipc_result (f, "{'id':3,'method':'physics.layers.open'}");
	g_clear_pointer (&r, json_node_unref);
	efx_frames (f, 4);
	lrg_text_input_set_text (LRG_TEXT_INPUT (popup_find (f, "project-physics-layer-10")), "lava");
	efx_click_widget (f, popup_find (f, "project-physics-defaults"));
	efx_frames (f, 2);
	efx_click_widget (f, find_button (lrg_canvas_get_top_popup (lrg_editor_app_get_canvas (f->app)), "Cancel"));
	efx_frames (f, 20);
	r = efx_ipc_result (f, "{'id':4,'method':'physics.layers.get'}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "count"), ==, 10);
	g_assert_cmpfloat_with_epsilon (json_array_get_double_element (json_object_get_array_member (
	                                    json_object_get_object_member (json_node_get_object (r), "settings"),
	                                    "gravity"), 1), -1.62, 1e-4);
}

/* ==========================================================================
 * Assets panel
 * ========================================================================== */

static void
test_assets (EditorAppFixture *f,
             gconstpointer     data)
{
	g_autofree gchar     *glb = write_model (f, "hero.glb");
	g_autofree gchar     *empty = g_build_filename (f->project, "new_physics_asset.rphysasset", NULL);
	g_autofree gchar     *gen = g_build_filename (f->project, "hero.rphysasset", NULL);
	LrgWidget            *w;
	LrgEditorAssetsPanel *panel;
	g_autoptr(GError)     error = NULL;
	LrgEditorPhysicsAssetEditor *ed;

	efx_frames (f, 3);
	w = efx_find (f, "assets-panel");
	g_assert_nonnull (w);
	panel = LRG_EDITOR_ASSETS_PANEL (w);
	lrg_editor_assets_panel_set_directory (panel, f->project);
	efx_frames (f, 2);
	lrg_widget_activate_action (LRG_WIDGET (panel), "assets.new-physics-asset", NULL);
	efx_frames (f, 2);
	g_assert_true (g_file_test (empty, G_FILE_TEST_IS_REGULAR));
	ed = lrg_editor_physics_asset_editor_open (f->app, empty, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_physics_asset_get_part_count (lrg_editor_physics_asset_editor_get_asset (ed)), ==, 0);

	/* from a model: select it, run the entry action */
	g_assert_true (lrg_editor_assets_panel_reveal (panel, glb));
	lrg_widget_activate_action (LRG_WIDGET (panel), "assets.physics-asset-from-model", NULL);
	efx_frames (f, 2);
	g_assert_true (g_file_test (gen, G_FILE_TEST_IS_REGULAR));
	ed = lrg_editor_physics_asset_editor_open (f->app, gen, &error);
	g_assert_no_error (error);
	efx_frames (f, 2);
	g_assert_cmpuint (lrg_physics_asset_get_part_count (lrg_editor_physics_asset_editor_get_asset (ed)), >=, 10);
	g_assert_true (g_str_has_suffix (lrg_editor_physics_asset_editor_get_model (ed), "hero.glb"));
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
	InitWindow (64, 64, "test-editor-app-physasset");
	return IsWindowReady ();
}

static gboolean
gpu_skip (void)
{
	if (!gpu_ready)
		g_test_skip ("needs a display (run under xrun.sh)");
	return !gpu_ready;
}

/* Pixels that differ from the clear colour. */
static guint
lit_pixels (GrlImage *image)
{
	const guint8 *px = grl_image_get_pixels (image, NULL);
	gint          w = grl_image_get_width (image), h = grl_image_get_height (image);
	guint         n = 0;
	gint          i;

	for (i = 0; i < w * h; i++)
		if (abs ((gint) px[4 * i] - 26) > 30 || abs ((gint) px[4 * i + 1] - 28) > 30 ||
		    abs ((gint) px[4 * i + 2] - 38) > 30)
			n++;
	return n;
}

static void
save_shot (GrlImage    *image,
           const gchar *name)
{
	const gchar *dir = g_getenv ("LRG_TEST_SHOT_DIR");

	if (dir != NULL && *dir != '\0')
	{
		g_autofree gchar *file = g_build_filename (dir, name, NULL);

		grl_image_export (image, file);
	}
}

static void
test_gpu_preview (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgEditorPhysicsAssetEditor *ed;
	g_autoptr(GrlImage)          a = NULL;
	g_autoptr(GrlImage)          b = NULL;
	g_autoptr(GError)            error = NULL;

	if (gpu_skip ())
		return;
	ed = hero_asset (f);
	lrg_editor_physics_asset_editor_select_part (ed, lrg_physics_asset_find_part (
	                                                 lrg_editor_physics_asset_editor_get_asset (ed), "upper_arm_l"));
	a = lrg_editor_physics_asset_editor_render_preview (ed, 384);
	g_assert_nonnull (a);
	g_assert_cmpuint (lit_pixels (a), >, 2000);
	save_shot (a, "m5e-edphysasset-preview.png");
	g_assert_true (lrg_editor_physics_asset_editor_set_simulating (ed, TRUE, &error));
	lrg_editor_physics_asset_editor_advance (ed, 1.5f);
	b = lrg_editor_physics_asset_editor_render_preview (ed, 384);
	g_assert_nonnull (b);
	g_assert_cmpuint (lit_pixels (b), >, 2000);
	save_shot (b, "m5e-edphysasset-simulated.png");
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_log_set_fatal_handler (loader_warnings_ok, NULL);
	gpu_ready = gpu_init ();

#define ADD(name, fn) g_test_add ("/editor-app-physasset/" name, EditorAppFixture, NULL, efx_setup, fn, efx_teardown)
	ADD ("formats", test_formats);
	ADD ("editor/open-generate", test_open_generate);
	ADD ("editor/parts", test_parts);
	ADD ("editor/grid-gesture", test_grid_gesture);
	ADD ("editor/simulate", test_simulate);
	ADD ("ipc/physasset", test_ipc);
	ADD ("ipc/layers", test_layers_ipc);
	ADD ("ipc/layers-no-project", test_layers_no_project);
	ADD ("dialogs/project-physics", test_settings_dialog);
	ADD ("assets/new-physics-asset", test_assets);
	ADD ("gpu/preview", test_gpu_preview);
#undef ADD

	{
		gint rc = g_test_run ();

		if (gpu_ready)
			CloseWindow ();
		return rc;
	}
}
