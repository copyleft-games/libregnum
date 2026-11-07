/* test-data-loader-composite.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgDataLoader composite documents: typed sub-objects in object
 * properties, typed lists in GListModel/GPtrArray properties, child lists
 * on LrgDataContainer objects, `ref:` references (document ids, pointers,
 * forward references, an external resolver), strict validation messages
 * with paths and positions, and the save writer's byte-stable round trip.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <libregnum.h>

/* ==========================================================================
 * Test types
 *
 * TestMaterial: a leaf with an id, a colour and a bounded roughness.
 * TestShiny:    a TestMaterial subclass (to exercise `type:` on subtypes).
 * TestNode:     an LrgDataContainer of TestNode children, with an object
 *               property (material), a reference property (target), a
 *               GListModel of materials (layers) and a GPtrArray of
 *               materials (decals).
 * ========================================================================== */

typedef enum
{
	TEST_SHAPE_BOX,
	TEST_SHAPE_SPHERE,
	TEST_SHAPE_CAPSULE
} TestShape;

static GType test_shape_get_type (void);

G_DEFINE_ENUM_TYPE (TestShape, test_shape,
	G_DEFINE_ENUM_VALUE (TEST_SHAPE_BOX, "box"),
	G_DEFINE_ENUM_VALUE (TEST_SHAPE_SPHERE, "sphere"),
	G_DEFINE_ENUM_VALUE (TEST_SHAPE_CAPSULE, "capsule"))
#define TEST_TYPE_SHAPE (test_shape_get_type ())

/* --- TestMaterial --- */

#define TEST_TYPE_MATERIAL (test_material_get_type ())
G_DECLARE_DERIVABLE_TYPE (TestMaterial, test_material, TEST, MATERIAL, GObject)

struct _TestMaterialClass
{
	GObjectClass parent_class;
};

typedef struct
{
	gchar  *id;
	gchar  *color;
	gfloat  roughness;
} TestMaterialPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (TestMaterial, test_material, G_TYPE_OBJECT)

enum
{
	MAT_PROP_0,
	MAT_PROP_ID,
	MAT_PROP_COLOR,
	MAT_PROP_ROUGHNESS,
	MAT_N_PROPS
};

static void
test_material_finalize (GObject *object)
{
	TestMaterialPrivate *priv = test_material_get_instance_private (TEST_MATERIAL (object));

	g_free (priv->id);
	g_free (priv->color);
	G_OBJECT_CLASS (test_material_parent_class)->finalize (object);
}

static void
test_material_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	TestMaterialPrivate *priv = test_material_get_instance_private (TEST_MATERIAL (object));

	switch (prop_id)
	{
	case MAT_PROP_ID:
		g_value_set_string (value, priv->id);
		break;
	case MAT_PROP_COLOR:
		g_value_set_string (value, priv->color);
		break;
	case MAT_PROP_ROUGHNESS:
		g_value_set_float (value, priv->roughness);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
test_material_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	TestMaterialPrivate *priv = test_material_get_instance_private (TEST_MATERIAL (object));

	switch (prop_id)
	{
	case MAT_PROP_ID:
		g_free (priv->id);
		priv->id = g_value_dup_string (value);
		break;
	case MAT_PROP_COLOR:
		g_free (priv->color);
		priv->color = g_value_dup_string (value);
		break;
	case MAT_PROP_ROUGHNESS:
		priv->roughness = g_value_get_float (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
test_material_class_init (TestMaterialClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = test_material_finalize;
	object_class->get_property = test_material_get_property;
	object_class->set_property = test_material_set_property;

	g_object_class_install_property (object_class, MAT_PROP_ID,
		g_param_spec_string ("id", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class, MAT_PROP_COLOR,
		g_param_spec_string ("color", NULL, NULL, "white", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class, MAT_PROP_ROUGHNESS,
		g_param_spec_float ("roughness", NULL, NULL, 0.0f, 1.0f, 0.5f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
test_material_init (TestMaterial *self)
{
	TestMaterialPrivate *priv = test_material_get_instance_private (self);

	priv->color = g_strdup ("white");
	priv->roughness = 0.5f;
}

static const gchar *
mat_color (gpointer material)
{
	TestMaterialPrivate *priv = test_material_get_instance_private (TEST_MATERIAL (material));

	return priv->color;
}

static const gchar *
mat_id (gpointer material)
{
	TestMaterialPrivate *priv = test_material_get_instance_private (TEST_MATERIAL (material));

	return priv->id;
}

static gfloat
mat_roughness (gpointer material)
{
	TestMaterialPrivate *priv = test_material_get_instance_private (TEST_MATERIAL (material));

	return priv->roughness;
}

/* --- TestShiny --- */

#define TEST_TYPE_SHINY (test_shiny_get_type ())
G_DECLARE_FINAL_TYPE (TestShiny, test_shiny, TEST, SHINY, TestMaterial)

struct _TestShiny
{
	TestMaterial parent_instance;
	gdouble      gloss;
};

G_DEFINE_TYPE (TestShiny, test_shiny, TEST_TYPE_MATERIAL)

static void
test_shiny_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	if (prop_id == 1)
		g_value_set_double (value, TEST_SHINY (object)->gloss);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
test_shiny_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	if (prop_id == 1)
		TEST_SHINY (object)->gloss = g_value_get_double (value);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
test_shiny_class_init (TestShinyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = test_shiny_get_property;
	object_class->set_property = test_shiny_set_property;
	g_object_class_install_property (object_class, 1,
		g_param_spec_double ("gloss", NULL, NULL, 0.0, 10.0, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
test_shiny_init (TestShiny *self)
{
	self->gloss = 1.0;
}

/* --- TestNode --- */

#define TEST_TYPE_NODE (test_node_get_type ())
G_DECLARE_FINAL_TYPE (TestNode, test_node, TEST, NODE, GObject)

struct _TestNode
{
	GObject       parent_instance;
	gchar        *id;
	gchar        *name;
	gint          count;
	gdouble       weight;
	TestShape     shape;
	gboolean      visible;
	gchar       **tags;
	TestMaterial *material;
	TestNode     *target;        /* a reference: may point anywhere */
	GListStore   *layers;        /* read-only, refilled in place */
	GPtrArray    *decals;
	GPtrArray    *children;
};

static void test_node_container_init (LrgDataContainerInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestNode, test_node, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_DATA_CONTAINER, test_node_container_init))

enum
{
	NODE_PROP_0,
	NODE_PROP_ID,
	NODE_PROP_NAME,
	NODE_PROP_COUNT,
	NODE_PROP_WEIGHT,
	NODE_PROP_SHAPE,
	NODE_PROP_VISIBLE,
	NODE_PROP_TAGS,
	NODE_PROP_MATERIAL,
	NODE_PROP_TARGET,
	NODE_PROP_LAYERS,
	NODE_PROP_DECALS,
	NODE_N_PROPS
};

static void
test_node_dispose (GObject *object)
{
	TestNode *self = TEST_NODE (object);

	g_clear_object (&self->material);
	g_clear_object (&self->target);
	g_clear_object (&self->layers);
	g_clear_pointer (&self->decals, g_ptr_array_unref);
	g_clear_pointer (&self->children, g_ptr_array_unref);
	G_OBJECT_CLASS (test_node_parent_class)->dispose (object);
}

static void
test_node_finalize (GObject *object)
{
	TestNode *self = TEST_NODE (object);

	g_free (self->id);
	g_free (self->name);
	g_strfreev (self->tags);
	G_OBJECT_CLASS (test_node_parent_class)->finalize (object);
}

static void
test_node_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	TestNode *self = TEST_NODE (object);

	switch (prop_id)
	{
	case NODE_PROP_ID:
		g_value_set_string (value, self->id);
		break;
	case NODE_PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case NODE_PROP_COUNT:
		g_value_set_int (value, self->count);
		break;
	case NODE_PROP_WEIGHT:
		g_value_set_double (value, self->weight);
		break;
	case NODE_PROP_SHAPE:
		g_value_set_enum (value, self->shape);
		break;
	case NODE_PROP_VISIBLE:
		g_value_set_boolean (value, self->visible);
		break;
	case NODE_PROP_TAGS:
		g_value_set_boxed (value, self->tags);
		break;
	case NODE_PROP_MATERIAL:
		g_value_set_object (value, self->material);
		break;
	case NODE_PROP_TARGET:
		g_value_set_object (value, self->target);
		break;
	case NODE_PROP_LAYERS:
		g_value_set_object (value, self->layers);
		break;
	case NODE_PROP_DECALS:
		g_value_set_boxed (value, self->decals);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
test_node_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	TestNode *self = TEST_NODE (object);

	switch (prop_id)
	{
	case NODE_PROP_ID:
		g_free (self->id);
		self->id = g_value_dup_string (value);
		break;
	case NODE_PROP_NAME:
		g_free (self->name);
		self->name = g_value_dup_string (value);
		break;
	case NODE_PROP_COUNT:
		self->count = g_value_get_int (value);
		break;
	case NODE_PROP_WEIGHT:
		self->weight = g_value_get_double (value);
		break;
	case NODE_PROP_SHAPE:
		self->shape = (TestShape)g_value_get_enum (value);
		break;
	case NODE_PROP_VISIBLE:
		self->visible = g_value_get_boolean (value);
		break;
	case NODE_PROP_TAGS:
		g_strfreev (self->tags);
		self->tags = g_value_dup_boxed (value);
		break;
	case NODE_PROP_MATERIAL:
		g_set_object (&self->material, g_value_get_object (value));
		break;
	case NODE_PROP_TARGET:
		g_set_object (&self->target, g_value_get_object (value));
		break;
	case NODE_PROP_DECALS:
		g_clear_pointer (&self->decals, g_ptr_array_unref);
		self->decals = g_value_dup_boxed (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
test_node_class_init (TestNodeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;

	object_class->dispose = test_node_dispose;
	object_class->finalize = test_node_finalize;
	object_class->get_property = test_node_get_property;
	object_class->set_property = test_node_set_property;

	g_object_class_install_property (object_class, NODE_PROP_ID,
		g_param_spec_string ("id", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class, NODE_PROP_NAME,
		g_param_spec_string ("name", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class, NODE_PROP_COUNT,
		g_param_spec_int ("count", NULL, NULL, 0, 100, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class, NODE_PROP_WEIGHT,
		g_param_spec_double ("weight", NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class, NODE_PROP_SHAPE,
		g_param_spec_enum ("shape", NULL, NULL, TEST_TYPE_SHAPE, TEST_SHAPE_BOX,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class, NODE_PROP_VISIBLE,
		g_param_spec_boolean ("visible", NULL, NULL, TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class, NODE_PROP_TAGS,
		g_param_spec_boxed ("tags", NULL, NULL, G_TYPE_STRV, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class, NODE_PROP_MATERIAL,
		g_param_spec_object ("material", NULL, NULL, TEST_TYPE_MATERIAL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class, NODE_PROP_TARGET,
		g_param_spec_object ("target", NULL, NULL, G_TYPE_OBJECT,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	/* read-only list: the loader refills the store the node holds */
	pspec = g_param_spec_object ("layers", NULL, NULL, G_TYPE_LIST_MODEL,
	                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, NODE_PROP_LAYERS, pspec);

	/* a GPtrArray is only composite with a declared item type */
	pspec = g_param_spec_boxed ("decals", NULL, NULL, G_TYPE_PTR_ARRAY,
	                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	lrg_data_loader_set_param_item_type (pspec, TEST_TYPE_MATERIAL);
	g_object_class_install_property (object_class, NODE_PROP_DECALS, pspec);
}

static void
test_node_init (TestNode *self)
{
	self->visible = TRUE;
	self->layers = g_list_store_new (TEST_TYPE_MATERIAL);
	self->children = g_ptr_array_new_with_free_func (g_object_unref);
}

static gboolean
test_node_add_child (LrgDataContainer  *container,
                     GObject           *child,
                     GError           **error)
{
	TestNode *self = TEST_NODE (container);

	if (!TEST_IS_NODE (child))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "a %s is not a node",
		             G_OBJECT_TYPE_NAME (child));
		return FALSE;
	}
	g_ptr_array_add (self->children, g_object_ref (child));
	return TRUE;
}

static guint
test_node_get_n_children (LrgDataContainer *container)
{
	return TEST_NODE (container)->children->len;
}

static GObject *
test_node_get_child (LrgDataContainer *container,
                     guint             index)
{
	return g_ptr_array_index (TEST_NODE (container)->children, index);
}

static GType
test_node_get_child_type (LrgDataContainer *container)
{
	return TEST_TYPE_NODE;
}

static void
test_node_container_init (LrgDataContainerInterface *iface)
{
	iface->add_child = test_node_add_child;
	iface->get_n_children = test_node_get_n_children;
	iface->get_child = test_node_get_child;
	iface->get_child_type = test_node_get_child_type;
}

static TestNode *
node_child (TestNode *node,
            guint     index)
{
	g_assert_cmpuint (index, <, node->children->len);
	return TEST_NODE (g_ptr_array_index (node->children, index));
}

/*
 * SCENE links the root and a grandchild both ways (strong references in
 * this test type), so tests break that cycle before dropping the tree.
 */
static void
unlink_targets (TestNode *node)
{
	guint i;

	g_clear_object (&node->target);
	for (i = 0; i < node->children->len; i++)
		unlink_targets (node_child (node, i));
}

/* ==========================================================================
 * Fixtures
 * ========================================================================== */

static LrgDataLoader *
new_loader (void)
{
	g_autoptr(LrgRegistry) registry = lrg_registry_new ();
	LrgDataLoader *loader = lrg_data_loader_new ();

	lrg_registry_register (registry, "node", TEST_TYPE_NODE);
	lrg_registry_register (registry, "material", TEST_TYPE_MATERIAL);
	lrg_registry_register (registry, "shiny", TEST_TYPE_SHINY);
	lrg_data_loader_set_registry (loader, registry);
	return loader;
}

static GObject *
load_ok (LrgDataLoader *loader,
         const gchar   *text)
{
	g_autoptr(GError) error = NULL;
	GObject *object;

	object = lrg_data_loader_load_composite_data (loader, text, -1, G_TYPE_INVALID, "scene.yaml", &error);
	g_assert_no_error (error);
	g_assert_nonnull (object);
	return object;
}

/* the message of a failed load (asserting it failed with @code) */
static gchar *
load_fail (LrgDataLoader *loader,
           const gchar   *text,
           gint           code)
{
	g_autoptr(GError) error = NULL;
	GObject *object;

	object = lrg_data_loader_load_composite_data (loader, text, -1, G_TYPE_INVALID, "scene.yaml", &error);
	g_assert_null (object);
	g_assert_error (error, LRG_DATA_LOADER_ERROR, code);
	return g_strdup (error->message);
}

static const gchar *SCENE =
	"type: node\n"
	"id: root\n"
	"name: Level\n"
	"count: 3\n"
	"weight: 0.1\n"
	"shape: sphere\n"
	"visible: false\n"
	"tags: [outdoor, night]\n"
	"material:\n"
	"  color: red\n"
	"  roughness: 0.25\n"
	"target: {ref: lamp}\n"
	"layers:\n"
	"  - color: green\n"
	"  - type: shiny\n"
	"    id: chrome\n"
	"    gloss: 2.5\n"
	"decals:\n"
	"  - {ref: chrome}\n"
	"  - color: blue\n"
	"children:\n"
	"  - id: lamp\n"
	"    name: Lamp\n"
	"    material: {ref: '#/material'}\n"
	"    children:\n"
	"      - id: bulb\n"
	"        target: {ref: '#'}\n"
	"  - {ref: '#/children/0/children/0'}\n";

/* ==========================================================================
 * Building
 * ========================================================================== */

static void
test_nested_sub_objects (void)
{
	g_autoptr(LrgDataLoader) loader = new_loader ();
	g_autoptr(GObject) object = NULL;
	TestNode *root;

	object = load_ok (loader,
		"type: node\n"
		"material:\n"
		"  color: red\n"
		"  roughness: 0.25\n");
	root = TEST_NODE (object);
	g_assert_nonnull (root->material);
	g_assert_true (G_OBJECT_TYPE (root->material) == TEST_TYPE_MATERIAL);
	g_assert_cmpstr (mat_color (root->material), ==, "red");
	g_assert_cmpfloat (mat_roughness (root->material), ==, 0.25f);
	g_clear_object (&object);

	/* a subtype names itself; a null clears */
	object = load_ok (loader,
		"type: node\n"
		"material: {type: shiny, gloss: 3}\n"
		"target: null\n");
	root = TEST_NODE (object);
	g_assert_true (TEST_IS_SHINY (root->material));
	g_assert_cmpfloat (TEST_SHINY (root->material)->gloss, ==, 3.0);
	g_assert_null (root->target);
}

static void
test_lists_and_children (void)
{
	g_autoptr(LrgDataLoader) loader = new_loader ();
	g_autoptr(GObject) object = NULL;
	g_autoptr(GObject) layer0 = NULL;
	g_autoptr(GObject) layer1 = NULL;
	TestNode *root;

	object = load_ok (loader, SCENE);
	root = TEST_NODE (object);

	/* scalars */
	g_assert_cmpstr (root->name, ==, "Level");
	g_assert_cmpint (root->count, ==, 3);
	g_assert_cmpfloat (root->weight, ==, 0.1);
	g_assert_cmpint (root->shape, ==, TEST_SHAPE_SPHERE);
	g_assert_false (root->visible);
	g_assert_cmpstrv (root->tags, ((const gchar *[]) { "outdoor", "night", NULL }));

	/* the read-only GListModel was refilled in place, in order */
	g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (root->layers)), ==, 2);
	layer0 = g_list_model_get_item (G_LIST_MODEL (root->layers), 0);
	layer1 = g_list_model_get_item (G_LIST_MODEL (root->layers), 1);
	g_assert_cmpstr (mat_color (layer0), ==, "green");
	g_assert_true (TEST_IS_SHINY (layer1));
	g_assert_cmpfloat (TEST_SHINY (layer1)->gloss, ==, 2.5);

	/* the GPtrArray: a reference first, then an inline material */
	g_assert_nonnull (root->decals);
	g_assert_cmpuint (root->decals->len, ==, 2);
	g_assert_true (g_ptr_array_index (root->decals, 0) == (gpointer)layer1);
	g_assert_cmpstr (mat_color (g_ptr_array_index (root->decals, 1)), ==, "blue");

	/* children default to TestNode, nest, and keep document order */
	g_assert_cmpuint (root->children->len, ==, 2);
	g_assert_cmpstr (node_child (root, 0)->name, ==, "Lamp");
	g_assert_cmpuint (node_child (root, 0)->children->len, ==, 1);
	g_assert_cmpstr (node_child (node_child (root, 0), 0)->id, ==, "bulb");
	unlink_targets (root);
}

static void
test_references (void)
{
	g_autoptr(LrgDataLoader) loader = new_loader ();
	g_autoptr(GObject) object = NULL;
	TestNode *root;
	TestNode *lamp;
	TestNode *bulb;

	object = load_ok (loader, SCENE);
	root = TEST_NODE (object);
	lamp = node_child (root, 0);
	bulb = node_child (lamp, 0);

	/* a forward reference by id */
	g_assert_true (root->target == lamp);
	/* a pointer to an object property */
	g_assert_true (lamp->material == root->material);
	/* "#" is the root (a cycle is fine) */
	g_assert_true (bulb->target == root);
	/* a reference inside a child list shares the object */
	g_assert_true (node_child (root, 1) == bulb);
	/* objects of the document carry no external reference */
	g_assert_null (lrg_data_loader_get_object_ref (G_OBJECT (lamp)));
	unlink_targets (root);
}

/* an external resolver: "lib/<color>.mat" makes a material of that colour */
static GObject *
resolve_library (LrgDataLoader  *loader,
                 const gchar    *ref,
                 GType           expected_type,
                 gpointer        user_data,
                 GError        **error)
{
	guint *calls = (guint *)user_data;
	g_autofree gchar *color = NULL;

	(*calls)++;
	if (g_str_equal (ref, "lib/broken.mat"))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no such asset");
		return NULL;
	}
	if (!g_str_has_prefix (ref, "lib/") || !g_str_has_suffix (ref, ".mat"))
		return NULL;
	g_assert_true (g_type_is_a (TEST_TYPE_MATERIAL, expected_type));
	color = g_strndup (ref + 4, strlen (ref) - 8);
	return g_object_new (TEST_TYPE_MATERIAL, "color", color, NULL);
}

static void
test_external_resolver (void)
{
	g_autoptr(LrgDataLoader) loader = new_loader ();
	g_autoptr(GObject) object = NULL;
	g_autofree gchar *message = NULL;
	g_autofree gchar *text = NULL;
	guint calls = 0;
	TestNode *root;

	lrg_data_loader_set_resolver (loader, resolve_library, &calls, NULL);
	object = load_ok (loader,
		"type: node\n"
		"material: {ref: lib/stone.mat}\n");
	root = TEST_NODE (object);
	g_assert_cmpuint (calls, ==, 1);
	g_assert_cmpstr (mat_color (root->material), ==, "stone");
	g_assert_cmpstr (lrg_data_loader_get_object_ref (G_OBJECT (root->material)), ==, "lib/stone.mat");

	/* saving writes the reference back, not the material */
	text = lrg_data_loader_save_to_data (loader, object, NULL, NULL);
	g_assert_cmpstr (text, ==, "type: node\nmaterial:\n  ref: lib/stone.mat\n");

	/* the resolver's error is reported with the path */
	message = load_fail (loader, "type: node\nmaterial: {ref: lib/broken.mat}\n",
	                     LRG_DATA_LOADER_ERROR_PROPERTY);
	g_assert_nonnull (strstr (message, "scene.yaml:2:1: /material: reference 'lib/broken.mat': no such asset"));
}

static void
test_unresolved_references (void)
{
	g_autoptr(LrgDataLoader) loader = new_loader ();
	g_autofree gchar *message = NULL;
	g_autofree gchar *wrong_type = NULL;
	g_autofree gchar *ambiguous = NULL;

	message = load_fail (loader,
		"type: node\n"
		"children:\n"
		"  - id: a\n"
		"  - {ref: nobody}\n"
		"target: {ref: '#/children/7'}\n",
		LRG_DATA_LOADER_ERROR_PROPERTY);
	g_assert_nonnull (strstr (message, "scene.yaml:4:5: /children/1: unresolved reference 'nobody'"));
	g_assert_nonnull (strstr (message, "scene.yaml:5:1: /target: unresolved reference '#/children/7'"));

	/* a reference of the wrong type */
	wrong_type = load_fail (loader,
		"type: node\n"
		"id: me\n"
		"material: {ref: me}\n",
		LRG_DATA_LOADER_ERROR_TYPE);
	g_assert_nonnull (strstr (wrong_type, "/material: reference 'me' is a TestNode, not a TestMaterial"));

	/* two objects with one id cannot be referenced by it */
	ambiguous = load_fail (loader,
		"type: node\n"
		"children: [{id: twin}, {id: twin}]\n"
		"target: {ref: twin}\n",
		LRG_DATA_LOADER_ERROR_PROPERTY);
	g_assert_nonnull (strstr (ambiguous, "ambiguous reference 'twin'"));
}

/* ==========================================================================
 * Strict validation
 * ========================================================================== */

static void
test_strict_validation (void)
{
	g_autoptr(LrgDataLoader) loader = new_loader ();
	g_autofree gchar *message = NULL;
	g_autofree gchar *types = NULL;
	g_autofree gchar *shapes = NULL;

	/* every problem is listed, each with its line, column and path */
	message = load_fail (loader,
		"type: node\n"
		"count: 300\n"
		"weight: heavy\n"
		"material:\n"
		"  colour: red\n"
		"  roughness: 2\n"
		"children:\n"
		"  - id: kid\n"
		"    visible: maybe\n",
		LRG_DATA_LOADER_ERROR_PROPERTY);
	g_assert_nonnull (strstr (message,
		"scene.yaml:2:1: /count: invalid value '300' for property 'count' (expected an integer from 0 to 100)"));
	g_assert_nonnull (strstr (message, "scene.yaml:3:1: /weight: invalid value 'heavy' for property 'weight'"));
	g_assert_nonnull (strstr (message, "scene.yaml:5:3: /material/colour: unknown property 'colour' of TestMaterial"));
	g_assert_nonnull (strstr (message, "scene.yaml:6:3: /material/roughness: invalid value '2'"));
	g_assert_nonnull (strstr (message,
		"scene.yaml:9:5: /children/0/visible: invalid value 'maybe' for property 'visible' (expected true or false)"));

	/* types: unknown, mismatched, missing where nothing is implied */
	types = load_fail (loader,
		"type: node\n"
		"material: {type: node}\n"
		"decals: [{type: rock}]\n"
		"target: {name: x}\n",
		LRG_DATA_LOADER_ERROR_TYPE);
	g_assert_nonnull (strstr (types, "/material/type: type 'node' (TestNode) is not a TestMaterial"));
	g_assert_nonnull (strstr (types, "/decals/0/type: unknown type 'rock'"));
	g_assert_nonnull (strstr (types, "/target: missing 'type'"));

	/* enums name their choices; a list where an object goes */
	shapes = load_fail (loader,
		"type: node\n"
		"shape: cone\n"
		"material: [1, 2]\n",
		LRG_DATA_LOADER_ERROR_PROPERTY);
	g_assert_nonnull (strstr (shapes, "invalid value 'cone' for property 'shape' (expected one of box, sphere, capsule)"));
	g_assert_nonnull (strstr (shapes, "/material: expected an object mapping, got a list"));
}

static void
test_non_strict (void)
{
	g_autoptr(LrgDataLoader) loader = new_loader ();
	g_autoptr(GObject) object = NULL;
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_data_loader_get_strict (loader));
	lrg_data_loader_set_strict (loader, FALSE);
	g_assert_false (lrg_data_loader_get_strict (loader));

	/* unknown keys are skipped (with a warning); bad values still fail */
	g_test_expect_message ("Libregnum-Core", G_LOG_LEVEL_WARNING, "*unknown property 'colour'*");
	object = load_ok (loader, "type: node\nmaterial: {colour: red, color: blue}\n");
	g_test_assert_expected_messages ();
	g_assert_cmpstr (mat_color (TEST_NODE (object)->material), ==, "blue");

	g_assert_null (lrg_data_loader_load_composite_data (loader, "type: node\ncount: -1\n", -1,
	                                                    G_TYPE_INVALID, NULL, &error));
	g_assert_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_PROPERTY);
	g_assert_nonnull (strstr (error->message, "(data):2:1: /count"));
}

static void
test_root_type_and_children_key (void)
{
	g_autoptr(LrgDataLoader) loader = new_loader ();
	g_autoptr(GObject) object = NULL;
	g_autoptr(GError) error = NULL;

	/* a root type lets the document leave out `type:` */
	object = lrg_data_loader_load_composite_data (loader, "color: teal\n", -1, TEST_TYPE_MATERIAL,
	                                              NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (mat_color (object), ==, "teal");
	g_clear_object (&object);

	/* a custom children key */
	lrg_data_loader_set_children_key (loader, "nodes");
	g_assert_cmpstr (lrg_data_loader_get_children_key (loader), ==, "nodes");
	object = load_ok (loader, "type: node\nnodes:\n  - name: one\n  - name: two\n");
	g_assert_cmpuint (TEST_NODE (object)->children->len, ==, 2);
	g_assert_cmpstr (node_child (TEST_NODE (object), 1)->name, ==, "two");
}

/* ==========================================================================
 * Saving
 * ========================================================================== */

/* the exact text the writer produces for SCENE */
static const gchar *SCENE_SAVED =
	"type: node\n"
	"id: root\n"
	"name: Level\n"
	"count: 3\n"
	"weight: 0.1\n"
	"shape: sphere\n"
	"visible: false\n"
	"tags:\n"
	"- outdoor\n"
	"- night\n"
	"material:\n"
	"  color: red\n"
	"  roughness: 0.25\n"
	"target:\n"
	"  type: node\n"
	"  id: lamp\n"
	"  name: Lamp\n"
	"  material:\n"
	"    ref: '#/material'\n"
	"  children:\n"
	"  - id: bulb\n"
	"    target:\n"
	"      ref: root\n"
	"layers:\n"
	"- color: green\n"
	"- type: shiny\n"
	"  id: chrome\n"
	"  gloss: 2.5\n"
	"decals:\n"
	"- ref: chrome\n"
	"- color: blue\n"
	"children:\n"
	"- ref: lamp\n"
	"- ref: bulb\n";

static void
test_save_round_trip (void)
{
	g_autoptr(LrgDataLoader) loader = new_loader ();
	g_autoptr(GObject) object = NULL;
	g_autoptr(GObject) again = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *first = NULL;
	g_autofree gchar *second = NULL;
	TestNode *root;

	object = load_ok (loader, SCENE);
	first = lrg_data_loader_save_to_data (loader, object, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (first, ==, SCENE_SAVED);

	/* the saved text loads into an equal tree... */
	again = load_ok (loader, first);
	root = TEST_NODE (again);
	g_assert_true (root->target == node_child (root, 0));
	g_assert_true (node_child (root, 0)->material == root->material);
	g_assert_true (node_child (root, 1) == node_child (node_child (root, 0), 0));
	g_assert_true (node_child (root, 1)->target == root);
	g_assert_true (g_ptr_array_index (root->decals, 0) ==
	               g_list_model_get_object (G_LIST_MODEL (root->layers), 1));
	g_object_unref (g_ptr_array_index (root->decals, 0));

	/* ...and saves to the same bytes */
	second = lrg_data_loader_save_to_data (loader, again, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (second, ==, first);

	unlink_targets (root);
	unlink_targets (TEST_NODE (object));
}

static void
test_save_file_and_errors (void)
{
	g_autoptr(LrgDataLoader) loader = new_loader ();
	g_autoptr(LrgDataLoader) bare = lrg_data_loader_new ();
	g_autoptr(GObject) object = NULL;
	g_autoptr(GObject) loaded = NULL;
	g_autoptr(GObject) plain = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *text = NULL;

	/* defaults are left out; floats are written in their shortest form */
	object = g_object_new (TEST_TYPE_MATERIAL, "roughness", 0.1f, NULL);
	text = lrg_data_loader_save_to_data (loader, object, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, "type: material\nroughness: 0.1\n");
	g_clear_pointer (&text, g_free);

	/* strings that would read back as null are quoted */
	g_object_set (object, "color", "null", "id", "", NULL);
	text = lrg_data_loader_save_to_data (loader, object, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, "type: material\nid: \"\"\ncolor: \"null\"\nroughness: 0.1\n");

	/* to a file and back */
	dir = g_dir_make_tmp ("lrg-composite-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "m.yaml", NULL);
	g_assert_true (lrg_data_loader_save_to_file (loader, object, path, &error));
	g_assert_no_error (error);
	loaded = lrg_data_loader_load_composite_file (loader, path, G_TYPE_INVALID, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (mat_color (loaded), ==, "null");
	g_assert_cmpstr (mat_id (loaded), ==, "");
	g_unlink (path);
	g_rmdir (dir);

	/* an unregistered type cannot be written */
	plain = g_object_new (TEST_TYPE_MATERIAL, NULL);
	g_assert_null (lrg_data_loader_save_to_data (bare, plain, NULL, &error));
	g_assert_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_TYPE);
	g_assert_nonnull (strstr (error->message, "not registered"));
}

/* ==========================================================================
 * Flat documents
 * ========================================================================== */

static void
test_flat_documents_unchanged (void)
{
	static const gchar *flat = "type: material\nid: m1\ncolor: red\nroughness: 0.75\n";
	g_autoptr(LrgDataLoader) loader = new_loader ();
	g_autoptr(GObject) permissive = NULL;
	g_autoptr(GObject) validated = NULL;
	g_autoptr(GObject) composite = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *saved = NULL;

	dir = g_dir_make_tmp ("lrg-composite-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "flat.yaml", NULL);
	g_assert_true (g_file_set_contents (path, flat, -1, NULL));

	/* the original APIs, as before */
	permissive = lrg_data_loader_load_data (loader, flat, -1, &error);
	g_assert_no_error (error);
	validated = lrg_data_loader_load_file_validated (loader, path, &error);
	g_assert_no_error (error);
	composite = lrg_data_loader_load_composite_file (loader, path, G_TYPE_INVALID, &error);
	g_assert_no_error (error);

	g_assert_cmpstr (mat_id (permissive), ==, "m1");
	g_assert_cmpstr (mat_color (validated), ==, mat_color (composite));
	g_assert_cmpfloat (mat_roughness (validated), ==, mat_roughness (composite));
	g_assert_cmpfloat (mat_roughness (permissive), ==, 0.75f);

	/* a flat object saves to the flat text it came from */
	saved = lrg_data_loader_save_to_data (loader, composite, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (saved, ==, flat);

	g_unlink (path);
	g_rmdir (dir);
}

static void
test_param_item_type (void)
{
	GParamSpec *pspec = g_param_spec_boxed ("x", NULL, NULL, G_TYPE_PTR_ARRAY, G_PARAM_READWRITE);

	g_param_spec_ref_sink (pspec);
	g_assert_true (lrg_data_loader_get_param_item_type (pspec) == G_TYPE_INVALID);
	lrg_data_loader_set_param_item_type (pspec, TEST_TYPE_MATERIAL);
	g_assert_true (lrg_data_loader_get_param_item_type (pspec) == TEST_TYPE_MATERIAL);
	g_param_spec_unref (pspec);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/data-loader/composite/nested-sub-objects", test_nested_sub_objects);
	g_test_add_func ("/data-loader/composite/lists-and-children", test_lists_and_children);
	g_test_add_func ("/data-loader/composite/references", test_references);
	g_test_add_func ("/data-loader/composite/external-resolver", test_external_resolver);
	g_test_add_func ("/data-loader/composite/unresolved-references", test_unresolved_references);
	g_test_add_func ("/data-loader/composite/strict-validation", test_strict_validation);
	g_test_add_func ("/data-loader/composite/non-strict", test_non_strict);
	g_test_add_func ("/data-loader/composite/root-type-and-children-key", test_root_type_and_children_key);
	g_test_add_func ("/data-loader/composite/save-round-trip", test_save_round_trip);
	g_test_add_func ("/data-loader/composite/save-file-and-errors", test_save_file_and_errors);
	g_test_add_func ("/data-loader/composite/flat-documents-unchanged", test_flat_documents_unchanged);
	g_test_add_func ("/data-loader/composite/param-item-type", test_param_item_type);

	return g_test_run ();
}
