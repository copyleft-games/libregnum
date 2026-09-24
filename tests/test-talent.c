/* test-talent.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgTalentNode, LrgTalentTree, LrgTalentLoadout and
 * LrgTalentBook: classic tiered talent trees with dual spec.
 */

#include <glib.h>
#include <glib-object.h>
#include <string.h>

#include "lrg-enums.h"
#include "progression/lrg-talent-tree.h"
#include "progression/lrg-talent-loadout.h"
#include "progression/lrg-talent-book.h"

#define CAP_LEVEL (60)

/* ========================================================================== */
/*                               Test fixture                                 */
/* ========================================================================== */

/*
 * Warrior trees used by most tests.
 *
 * arms (tier-gate 5):
 *   t0 c0 a_str      max 5  effect strength  values 1..5
 *   t0 c1 a_crit     max 3  effect crit      values .01 .02 .03
 *   t1 c0 a_mastery  max 5
 *   t1 c1 a_sweep    max 1  prereq a_crit    grants sweeping_strikes
 *   t2 c1 a_capstone max 1  prereq a_sweep   grants mortal_strike, strength 10
 * fury (tier-gate 5):
 *   t0 c0 f_rage     max 5  effect strength  values .5 1 1.5 2 2.5
 *   t1 c0 f_frenzy   max 1  grants frenzy
 * holy (class priest):
 *   t0 c0 h_heal     max 5
 */
typedef struct
{
    GHashTable       *trees;
    LrgTalentTree    *arms;
    LrgTalentTree    *fury;
    LrgTalentTree    *holy;
    LrgTalentLoadout *loadout;
} TalentFixture;

static LrgTalentNode *
make_node (const gchar *id,
           guint        tier,
           guint        column,
           guint        max_rank,
           const gchar *prereq,
           const gchar *grants,
           const gchar *effect)
{
    LrgTalentNode *node;

    node = lrg_talent_node_new (id, tier, column, max_rank);
    lrg_talent_node_set_prerequisite (node, prereq);
    lrg_talent_node_set_grants_ability (node, grants);
    lrg_talent_node_set_effect (node, effect);
    return node;
}

static void
add_ok (LrgTalentTree *tree,
        LrgTalentNode *node)
{
    g_autoptr(GError) error = NULL;

    g_assert_true (lrg_talent_tree_add_node (tree, node, &error));
    g_assert_no_error (error);
}

static void
register_tree (GHashTable    *trees,
               LrgTalentTree *tree)
{
    g_hash_table_insert (trees, g_strdup (lrg_talent_tree_get_id (tree)), tree);
}

static void
fixture_set_up (TalentFixture *fx,
                gconstpointer  user_data)
{
    LrgTalentNode *node;
    guint i;

    (void)user_data;

    fx->trees = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

    /* arms */
    fx->arms = lrg_talent_tree_new ("arms", "warrior");
    node = make_node ("a_str", 0, 0, 5, NULL, NULL, "strength");
    for (i = 1; i <= 5; i++)
        lrg_talent_node_add_rank_value (node, (gdouble)i);
    add_ok (fx->arms, node);
    node = make_node ("a_crit", 0, 1, 3, NULL, NULL, "crit");
    lrg_talent_node_add_rank_value (node, 0.01);
    lrg_talent_node_add_rank_value (node, 0.02);
    lrg_talent_node_add_rank_value (node, 0.03);
    add_ok (fx->arms, node);
    add_ok (fx->arms, make_node ("a_mastery", 1, 0, 5, NULL, NULL, NULL));
    add_ok (fx->arms, make_node ("a_sweep", 1, 1, 1, "a_crit", "sweeping_strikes", NULL));
    node = make_node ("a_capstone", 2, 1, 1, "a_sweep", "mortal_strike", "strength");
    lrg_talent_node_add_rank_value (node, 10.0);
    add_ok (fx->arms, node);
    register_tree (fx->trees, fx->arms);

    /* fury */
    fx->fury = lrg_talent_tree_new ("fury", "warrior");
    node = make_node ("f_rage", 0, 0, 5, NULL, NULL, "strength");
    for (i = 1; i <= 5; i++)
        lrg_talent_node_add_rank_value (node, 0.5 * i);
    add_ok (fx->fury, node);
    add_ok (fx->fury, make_node ("f_frenzy", 1, 0, 1, NULL, "frenzy", NULL));
    register_tree (fx->trees, fx->fury);

    /* holy (another class) */
    fx->holy = lrg_talent_tree_new ("holy", "priest");
    add_ok (fx->holy, make_node ("h_heal", 0, 0, 5, NULL, "renew", "healing"));
    register_tree (fx->trees, fx->holy);

    fx->loadout = lrg_talent_loadout_new ("warrior");
}

static void
fixture_tear_down (TalentFixture *fx,
                   gconstpointer  user_data)
{
    (void)user_data;

    g_clear_object (&fx->loadout);
    g_clear_pointer (&fx->trees, g_hash_table_unref);
}

/* Spend @n points in @node_id, asserting success */
static void
spend_n (LrgTalentLoadout *loadout,
         LrgTalentTree    *tree,
         const gchar      *node_id,
         guint             level,
         guint             n)
{
    guint i;

    for (i = 0; i < n; i++)
    {
        g_autoptr(GError) error = NULL;

        g_assert_true (lrg_talent_loadout_spend (loadout, tree, node_id, level, &error));
        g_assert_no_error (error);
    }
}

/*
 * Assert that both can_spend and spend reject with @code and that the
 * loadout's serialised state is unchanged.
 */
static void
assert_spend_rejected (LrgTalentLoadout *loadout,
                       LrgTalentTree    *tree,
                       const gchar      *node_id,
                       guint             level,
                       gint              code)
{
    g_autoptr(GVariant) before = NULL;
    g_autoptr(GVariant) after = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GError) error2 = NULL;
    guint spent;

    before = lrg_talent_loadout_to_variant (loadout);
    spent = lrg_talent_loadout_get_points_spent (loadout);

    g_assert_false (lrg_talent_loadout_can_spend (loadout, tree, node_id, level, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, code);
    g_assert_nonnull (error->message);

    g_assert_false (lrg_talent_loadout_spend (loadout, tree, node_id, level, &error2));
    g_assert_error (error2, LRG_PROGRESSION_ERROR, code);

    after = lrg_talent_loadout_to_variant (loadout);
    g_assert_true (g_variant_equal (before, after));
    g_assert_cmpuint (lrg_talent_loadout_get_points_spent (loadout), ==, spent);
}

/* Build a "(smsa(ssu))" loadout variant from parallel arrays */
static GVariant *
build_loadout_variant (const gchar  *class_id,
                       const gchar  *spec,
                       const gchar **tree_ids,
                       const gchar **node_ids,
                       const guint  *ranks,
                       guint         n_rows)
{
    GVariantBuilder rows;
    guint i;

    g_variant_builder_init (&rows, G_VARIANT_TYPE ("a(ssu)"));
    for (i = 0; i < n_rows; i++)
        g_variant_builder_add (&rows, "(ssu)", tree_ids[i], node_ids[i], ranks[i]);

    return g_variant_ref_sink (g_variant_new ("(sms@a(ssu))", class_id, spec,
                                              g_variant_builder_end (&rows)));
}

/* Restore from a variant and assert rejection with INVALID */
static void
assert_loadout_variant_rejected (GVariant *variant)
{
    g_autoptr(GError) error = NULL;
    LrgTalentLoadout *loadout;

    loadout = lrg_talent_loadout_new_from_variant (variant, &error);
    g_assert_null (loadout);
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
}

/* Restore a loadout from a variant, asserting success */
static LrgTalentLoadout *
restore_loadout (GVariant *variant)
{
    g_autoptr(GError) error = NULL;
    LrgTalentLoadout *loadout;

    loadout = lrg_talent_loadout_new_from_variant (variant, &error);
    g_assert_no_error (error);
    g_assert_nonnull (loadout);
    return loadout;
}

/* Validate a restored single-row-set loadout and expect @code (0 = success) */
static void
assert_validate (TalentFixture *fx,
                 GVariant      *variant,
                 guint          level,
                 gint           code)
{
    g_autoptr(LrgTalentLoadout) loadout = NULL;
    g_autoptr(GVariant) before = NULL;
    g_autoptr(GVariant) after = NULL;
    g_autoptr(GError) error = NULL;
    gboolean ok;

    loadout = restore_loadout (variant);
    before = lrg_talent_loadout_to_variant (loadout);

    ok = lrg_talent_loadout_validate (loadout, fx->trees, level, &error);
    if (code == 0)
    {
        g_assert_no_error (error);
        g_assert_true (ok);
    }
    else
    {
        g_assert_false (ok);
        g_assert_error (error, LRG_PROGRESSION_ERROR, code);
    }

    /* validate never modifies the loadout */
    after = lrg_talent_loadout_to_variant (loadout);
    g_assert_true (g_variant_equal (before, after));
}

/* Corrupt the first byte of a variant's serialised form */
static GVariant *
corrupt_first_byte (GVariant *variant)
{
    gsize size;
    guint8 *data;
    GBytes *bytes;
    GVariant *result;

    size = g_variant_get_size (variant);
    data = g_memdup2 (g_variant_get_data (variant), size);
    data[0] = 0xff;
    bytes = g_bytes_new_take (data, size);
    result = g_variant_ref_sink (g_variant_new_from_bytes (g_variant_get_type (variant),
                                                           bytes, FALSE));
    g_bytes_unref (bytes);
    return result;
}

static void
on_notify_count (GObject    *object,
                 GParamSpec *pspec,
                 gpointer    user_data)
{
    guint *count = user_data;

    (void)object;
    (void)pspec;
    (*count)++;
}

/* ========================================================================== */
/*                              LrgTalentNode                                 */
/* ========================================================================== */

static void
test_node_new_defaults (void)
{
    g_autoptr(LrgTalentNode) node = NULL;

    node = lrg_talent_node_new ("n", 2, 3, 4);
    g_assert_cmpstr (node->id, ==, "n");
    g_assert_cmpuint (node->tier, ==, 2);
    g_assert_cmpuint (node->column, ==, 3);
    g_assert_cmpuint (node->max_rank, ==, 4);
    g_assert_null (node->name);
    g_assert_null (node->description);
    g_assert_null (node->prerequisite);
    g_assert_null (node->grants_ability);
    g_assert_null (node->effect);
    g_assert_nonnull (node->rank_values);
    g_assert_cmpuint (node->rank_values->len, ==, 0);
}

static void
test_node_new_rejects_zero_rank (void)
{
    LrgTalentNode *node;

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*max_rank >= 1*");
    node = lrg_talent_node_new ("n", 0, 0, 0);
    g_test_assert_expected_messages ();
    g_assert_null (node);
}

static void
test_node_copy_deep (void)
{
    g_autoptr(LrgTalentNode) node = NULL;
    g_autoptr(LrgTalentNode) copy = NULL;
    LrgTalentNode *boxed;

    node = make_node ("n", 1, 2, 3, "pre", "ability", "effect");
    lrg_talent_node_set_name (node, "Name");
    lrg_talent_node_set_description (node, "Desc");
    lrg_talent_node_add_rank_value (node, 1.5);
    lrg_talent_node_add_rank_value (node, 2.5);
    lrg_talent_node_add_rank_value (node, 3.5);

    copy = lrg_talent_node_copy (node);

    /* Mutating the original must not leak into the copy */
    lrg_talent_node_set_name (node, "Changed");
    lrg_talent_node_add_rank_value (node, 9.0);
    g_array_index (node->rank_values, gdouble, 0) = 42.0;

    g_assert_cmpstr (copy->id, ==, "n");
    g_assert_cmpstr (copy->name, ==, "Name");
    g_assert_cmpstr (copy->description, ==, "Desc");
    g_assert_cmpstr (copy->prerequisite, ==, "pre");
    g_assert_cmpstr (copy->grants_ability, ==, "ability");
    g_assert_cmpstr (copy->effect, ==, "effect");
    g_assert_cmpuint (copy->tier, ==, 1);
    g_assert_cmpuint (copy->column, ==, 2);
    g_assert_cmpuint (copy->max_rank, ==, 3);
    g_assert_cmpuint (copy->rank_values->len, ==, 3);
    g_assert_cmpfloat (g_array_index (copy->rank_values, gdouble, 0), ==, 1.5);
    g_assert_cmpfloat (g_array_index (copy->rank_values, gdouble, 2), ==, 3.5);

    /* Boxed GType copy/free */
    boxed = g_boxed_copy (LRG_TYPE_TALENT_NODE, copy);
    g_assert_cmpstr (boxed->name, ==, "Name");
    g_boxed_free (LRG_TYPE_TALENT_NODE, boxed);

    /* Clearing optional strings */
    lrg_talent_node_set_prerequisite (node, NULL);
    lrg_talent_node_set_grants_ability (node, NULL);
    lrg_talent_node_set_effect (node, NULL);
    g_assert_null (node->prerequisite);
    g_assert_null (node->grants_ability);
    g_assert_null (node->effect);

    lrg_talent_node_free (NULL);
}

static void
test_node_get_value (void)
{
    g_autoptr(LrgTalentNode) node = NULL;
    g_autoptr(LrgTalentNode) partial = NULL;
    g_autoptr(LrgTalentNode) empty = NULL;

    node = lrg_talent_node_new ("n", 0, 0, 3);
    lrg_talent_node_add_rank_value (node, 2.0);
    lrg_talent_node_add_rank_value (node, 4.0);
    lrg_talent_node_add_rank_value (node, 6.0);
    g_assert_cmpfloat (lrg_talent_node_get_value (node, 0), ==, 0.0);
    g_assert_cmpfloat (lrg_talent_node_get_value (node, 1), ==, 2.0);
    g_assert_cmpfloat (lrg_talent_node_get_value (node, 2), ==, 4.0);
    g_assert_cmpfloat (lrg_talent_node_get_value (node, 3), ==, 6.0);
    /* Beyond the provided values clamps to the last value */
    g_assert_cmpfloat (lrg_talent_node_get_value (node, 4), ==, 6.0);
    g_assert_cmpfloat (lrg_talent_node_get_value (node, G_MAXUINT), ==, 6.0);

    partial = lrg_talent_node_new ("p", 0, 0, 5);
    lrg_talent_node_add_rank_value (partial, 7.0);
    g_assert_cmpfloat (lrg_talent_node_get_value (partial, 1), ==, 7.0);
    g_assert_cmpfloat (lrg_talent_node_get_value (partial, 5), ==, 7.0);

    empty = lrg_talent_node_new ("e", 0, 0, 5);
    g_assert_cmpfloat (lrg_talent_node_get_value (empty, 3), ==, 0.0);
}

/* ========================================================================== */
/*                              LrgTalentTree                                 */
/* ========================================================================== */

static void
test_tree_properties (void)
{
    g_autoptr(LrgTalentTree) tree = NULL;
    g_autofree gchar *id = NULL;
    g_autofree gchar *name = NULL;
    g_autofree gchar *description = NULL;
    g_autofree gchar *icon = NULL;
    g_autofree gchar *class_id = NULL;
    g_autofree gchar *role = NULL;
    guint tier_gate;
    guint notifies;

    tree = lrg_talent_tree_new ("arms", "warrior");
    g_assert_cmpstr (lrg_talent_tree_get_id (tree), ==, "arms");
    g_assert_cmpstr (lrg_talent_tree_get_class_id (tree), ==, "warrior");
    g_assert_cmpuint (lrg_talent_tree_get_tier_gate (tree), ==, 5);
    g_assert_null (lrg_talent_tree_get_name (tree));
    g_assert_null (lrg_talent_tree_get_role (tree));

    g_object_set (tree,
                  "name", "Arms",
                  "description", "Weapons",
                  "icon", "sword",
                  "class-id", "knight",
                  "role", "damage",
                  "tier-gate", 3u,
                  NULL);
    g_object_get (tree,
                  "id", &id,
                  "name", &name,
                  "description", &description,
                  "icon", &icon,
                  "class-id", &class_id,
                  "role", &role,
                  "tier-gate", &tier_gate,
                  NULL);
    g_assert_cmpstr (id, ==, "arms");
    g_assert_cmpstr (name, ==, "Arms");
    g_assert_cmpstr (description, ==, "Weapons");
    g_assert_cmpstr (icon, ==, "sword");
    g_assert_cmpstr (class_id, ==, "knight");
    g_assert_cmpstr (role, ==, "damage");
    g_assert_cmpuint (tier_gate, ==, 3);
    g_assert_cmpstr (lrg_talent_tree_get_description (tree), ==, "Weapons");
    g_assert_cmpstr (lrg_talent_tree_get_icon (tree), ==, "sword");

    /* Notify only fires on change */
    notifies = 0;
    g_signal_connect (tree, "notify", G_CALLBACK (on_notify_count), &notifies);
    lrg_talent_tree_set_name (tree, "Arms");
    lrg_talent_tree_set_tier_gate (tree, 3);
    g_assert_cmpuint (notifies, ==, 0);
    lrg_talent_tree_set_name (tree, "Arms II");
    lrg_talent_tree_set_tier_gate (tree, 0);
    lrg_talent_tree_set_role (tree, NULL);
    lrg_talent_tree_set_description (tree, NULL);
    lrg_talent_tree_set_icon (tree, NULL);
    lrg_talent_tree_set_class_id (tree, "warrior");
    g_assert_cmpuint (notifies, ==, 6);
    g_assert_cmpuint (lrg_talent_tree_get_tier_gate (tree), ==, 0);

    /* Out-of-range tier gate is a programmer error and is ignored */
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*tier_gate*");
    lrg_talent_tree_set_tier_gate (tree, LRG_TALENT_TREE_MAX_TIER_GATE + 1);
    g_test_assert_expected_messages ();
    g_assert_cmpuint (lrg_talent_tree_get_tier_gate (tree), ==, 0);
    lrg_talent_tree_set_tier_gate (tree, LRG_TALENT_TREE_MAX_TIER_GATE);
    g_assert_cmpuint (lrg_talent_tree_get_tier_gate (tree), ==, LRG_TALENT_TREE_MAX_TIER_GATE);
}

static void
test_tree_add_nodes_sorted (void)
{
    g_autoptr(LrgTalentTree) tree = NULL;
    GPtrArray *nodes;
    const LrgTalentNode *node;

    tree = lrg_talent_tree_new ("t", "c");
    g_assert_cmpuint (lrg_talent_tree_get_tier_count (tree), ==, 0);
    g_assert_cmpuint (lrg_talent_tree_get_max_points (tree), ==, 0);
    g_assert_cmpuint (lrg_talent_tree_get_nodes (tree)->len, ==, 0);

    /* Insert out of order; get_nodes sorts by (tier, column, id) */
    add_ok (tree, lrg_talent_node_new ("z", 1, 0, 2));
    add_ok (tree, lrg_talent_node_new ("b", 0, 1, 3));
    add_ok (tree, lrg_talent_node_new ("c", 0, 0, 1));
    add_ok (tree, lrg_talent_node_new ("a", 3, 0, 1));
    add_ok (tree, lrg_talent_node_new ("y", 1, 0, 1)); /* same position as z */

    nodes = lrg_talent_tree_get_nodes (tree);
    g_assert_cmpuint (nodes->len, ==, 5);
    g_assert_cmpstr (((LrgTalentNode *)g_ptr_array_index (nodes, 0))->id, ==, "c");
    g_assert_cmpstr (((LrgTalentNode *)g_ptr_array_index (nodes, 1))->id, ==, "b");
    g_assert_cmpstr (((LrgTalentNode *)g_ptr_array_index (nodes, 2))->id, ==, "y");
    g_assert_cmpstr (((LrgTalentNode *)g_ptr_array_index (nodes, 3))->id, ==, "z");
    g_assert_cmpstr (((LrgTalentNode *)g_ptr_array_index (nodes, 4))->id, ==, "a");

    g_assert_cmpuint (lrg_talent_tree_get_node_count (tree), ==, 5);
    g_assert_cmpuint (lrg_talent_tree_get_tier_count (tree), ==, 4);
    g_assert_cmpuint (lrg_talent_tree_get_max_points (tree), ==, 8);

    node = lrg_talent_tree_get_node (tree, "b");
    g_assert_nonnull (node);
    g_assert_cmpuint (node->max_rank, ==, 3);
    g_assert_null (lrg_talent_tree_get_node (tree, "nope"));
    g_assert_null (lrg_talent_tree_get_node (tree, NULL));
}

static void
test_tree_add_node_rejections (void)
{
    g_autoptr(LrgTalentTree) tree = NULL;
    g_autofree gchar *id128 = NULL;
    g_autofree gchar *id129 = NULL;
    GError *error = NULL;
    LrgTalentNode *node;

    tree = lrg_talent_tree_new ("t", "c");
    add_ok (tree, lrg_talent_node_new ("a", 0, 0, 1));

    /* Duplicate id: DUPLICATE, node consumed, tree unchanged */
    g_assert_false (lrg_talent_tree_add_node (tree, lrg_talent_node_new ("a", 0, 1, 5), &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_DUPLICATE);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_talent_tree_get_node_count (tree), ==, 1);
    g_assert_cmpuint (lrg_talent_tree_get_node (tree, "a")->max_rank, ==, 1);

    /* Empty id */
    g_assert_false (lrg_talent_tree_add_node (tree, lrg_talent_node_new ("", 0, 1, 1), &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    /* Overlong id (129 bytes) is rejected, 128 accepted */
    id128 = g_strnfill (128, 'x');
    id129 = g_strnfill (129, 'y');
    g_assert_false (lrg_talent_tree_add_node (tree, lrg_talent_node_new (id129, 0, 1, 1), &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    add_ok (tree, lrg_talent_node_new (id128, 0, 2, 1));

    /* Invalid UTF-8 id */
    g_assert_false (lrg_talent_tree_add_node (tree, lrg_talent_node_new ("bad\xff", 0, 3, 1), &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    /* Max rank above the limit; exactly the limit is fine */
    g_assert_false (lrg_talent_tree_add_node (tree,
                                              lrg_talent_node_new ("big", 0, 4, LRG_TALENT_MAX_RANK + 1),
                                              &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    add_ok (tree, lrg_talent_node_new ("max", 0, 4, LRG_TALENT_MAX_RANK));

    /* Max rank tampered to 0 before adding */
    node = lrg_talent_node_new ("zero", 0, 5, 1);
    node->max_rank = 0;
    g_assert_false (lrg_talent_tree_add_node (tree, node, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    /* A node with a NULL rank_values array gets one on insertion */
    node = lrg_talent_node_new ("noarr", 0, 6, 1);
    g_array_unref (node->rank_values);
    node->rank_values = NULL;
    add_ok (tree, node);
    g_assert_nonnull (lrg_talent_tree_get_node (tree, "noarr")->rank_values);

    g_assert_cmpuint (lrg_talent_tree_get_node_count (tree), ==, 4);
    g_assert_true (lrg_talent_tree_validate (tree, NULL));
}

static void
test_tree_validate_ok (TalentFixture *fx,
                       gconstpointer  user_data)
{
    g_autoptr(LrgTalentTree) empty = NULL;
    GError *error = NULL;

    (void)user_data;

    g_assert_true (lrg_talent_tree_validate (fx->arms, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_talent_tree_validate (fx->fury, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_talent_tree_validate (fx->holy, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_talent_tree_get_tier_count (fx->arms), ==, 3);
    g_assert_cmpuint (lrg_talent_tree_get_max_points (fx->arms), ==, 15);

    empty = lrg_talent_tree_new ("empty", "warrior");
    g_assert_true (lrg_talent_tree_validate (empty, &error));
    g_assert_no_error (error);
}

static void
assert_tree_invalid (LrgTalentTree *tree,
                     gint           code)
{
    GError *error = NULL;

    g_assert_false (lrg_talent_tree_validate (tree, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, code);
    g_clear_error (&error);
}

static void
test_tree_validate_failures (void)
{
    LrgTalentTree *tree;
    LrgTalentNode *node;

    /* rank_values length neither 0 nor max_rank */
    tree = lrg_talent_tree_new ("t", "c");
    node = lrg_talent_node_new ("a", 0, 0, 3);
    lrg_talent_node_add_rank_value (node, 1.0);
    lrg_talent_node_add_rank_value (node, 2.0);
    add_ok (tree, node);
    assert_tree_invalid (tree, LRG_PROGRESSION_ERROR_INVALID);
    g_object_unref (tree);

    /* Too many rank values */
    tree = lrg_talent_tree_new ("t", "c");
    node = lrg_talent_node_new ("a", 0, 0, 1);
    lrg_talent_node_add_rank_value (node, 1.0);
    lrg_talent_node_add_rank_value (node, 2.0);
    add_ok (tree, node);
    assert_tree_invalid (tree, LRG_PROGRESSION_ERROR_INVALID);
    g_object_unref (tree);

    /* Missing prerequisite node */
    tree = lrg_talent_tree_new ("t", "c");
    add_ok (tree, make_node ("a", 0, 0, 5, NULL, NULL, NULL));
    add_ok (tree, make_node ("b", 1, 0, 1, "ghost", NULL, NULL));
    assert_tree_invalid (tree, LRG_PROGRESSION_ERROR_NOT_FOUND);
    g_object_unref (tree);

    /* Prerequisite in the same tier */
    tree = lrg_talent_tree_new ("t", "c");
    add_ok (tree, make_node ("a", 0, 0, 5, NULL, NULL, NULL));
    add_ok (tree, make_node ("b", 0, 1, 1, "a", NULL, NULL));
    assert_tree_invalid (tree, LRG_PROGRESSION_ERROR_INVALID);
    g_object_unref (tree);

    /* Prerequisite in a higher tier */
    tree = lrg_talent_tree_new ("t", "c");
    add_ok (tree, make_node ("a", 0, 0, 5, "b", NULL, NULL));
    add_ok (tree, make_node ("b", 1, 0, 1, NULL, NULL, NULL));
    assert_tree_invalid (tree, LRG_PROGRESSION_ERROR_INVALID);
    g_object_unref (tree);

    /* Prerequisite is itself */
    tree = lrg_talent_tree_new ("t", "c");
    add_ok (tree, make_node ("a", 0, 0, 5, "a", NULL, NULL));
    assert_tree_invalid (tree, LRG_PROGRESSION_ERROR_INVALID);
    g_object_unref (tree);

    /* Duplicate (tier, column) */
    tree = lrg_talent_tree_new ("t", "c");
    add_ok (tree, make_node ("a", 0, 0, 5, NULL, NULL, NULL));
    add_ok (tree, make_node ("b", 0, 0, 5, NULL, NULL, NULL));
    assert_tree_invalid (tree, LRG_PROGRESSION_ERROR_DUPLICATE);
    g_object_unref (tree);

    /* Unreachable tier 1: only 4 points below a gate of 5 */
    tree = lrg_talent_tree_new ("t", "c");
    add_ok (tree, make_node ("a", 0, 0, 4, NULL, NULL, NULL));
    add_ok (tree, make_node ("b", 1, 0, 1, NULL, NULL, NULL));
    assert_tree_invalid (tree, LRG_PROGRESSION_ERROR_REQUIREMENT);
    /* Boundary: exactly tier-gate points below makes it reachable */
    lrg_talent_tree_set_tier_gate (tree, 4);
    g_assert_true (lrg_talent_tree_validate (tree, NULL));
    g_object_unref (tree);

    /* Skipped tier: tier 2 needs 10, tiers 0..1 provide only 5 */
    tree = lrg_talent_tree_new ("t", "c");
    add_ok (tree, make_node ("a", 0, 0, 5, NULL, NULL, NULL));
    add_ok (tree, make_node ("b", 2, 0, 1, NULL, NULL, NULL));
    assert_tree_invalid (tree, LRG_PROGRESSION_ERROR_REQUIREMENT);
    /* A tier gate of 0 disables gating */
    lrg_talent_tree_set_tier_gate (tree, 0);
    g_assert_true (lrg_talent_tree_validate (tree, NULL));
    g_object_unref (tree);

    /* First node already above tier 0 */
    tree = lrg_talent_tree_new ("t", "c");
    add_ok (tree, make_node ("a", 1, 0, 5, NULL, NULL, NULL));
    assert_tree_invalid (tree, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_object_unref (tree);

    /* Tampered public max_rank after insertion */
    tree = lrg_talent_tree_new ("t", "c");
    add_ok (tree, make_node ("a", 0, 0, 5, NULL, NULL, NULL));
    ((LrgTalentNode *)lrg_talent_tree_get_node (tree, "a"))->max_rank = 0;
    assert_tree_invalid (tree, LRG_PROGRESSION_ERROR_INVALID);
    g_object_unref (tree);
}

/* ========================================================================== */
/*                            LrgTalentLoadout                                */
/* ========================================================================== */

static void
test_loadout_properties (void)
{
    g_autoptr(LrgTalentLoadout) loadout = NULL;
    g_autofree gchar *class_id = NULL;
    g_autofree gchar *spec = NULL;
    guint first_level;
    guint ppl;
    guint notifies;

    loadout = lrg_talent_loadout_new ("warrior");
    g_assert_cmpstr (lrg_talent_loadout_get_class_id (loadout), ==, "warrior");
    g_assert_cmpuint (lrg_talent_loadout_get_first_level (loadout), ==, 10);
    g_assert_cmpuint (lrg_talent_loadout_get_points_per_level (loadout), ==, 1);
    g_assert_null (lrg_talent_loadout_get_spec (loadout));

    g_object_set (loadout, "first-level", 5u, "points-per-level", 2u, "spec", "arms", NULL);
    g_object_get (loadout,
                  "class-id", &class_id,
                  "first-level", &first_level,
                  "points-per-level", &ppl,
                  "spec", &spec,
                  NULL);
    g_assert_cmpstr (class_id, ==, "warrior");
    g_assert_cmpuint (first_level, ==, 5);
    g_assert_cmpuint (ppl, ==, 2);
    g_assert_cmpstr (spec, ==, "arms");

    notifies = 0;
    g_signal_connect (loadout, "notify", G_CALLBACK (on_notify_count), &notifies);
    lrg_talent_loadout_set_first_level (loadout, 5);
    lrg_talent_loadout_set_points_per_level (loadout, 2);
    lrg_talent_loadout_set_spec (loadout, "arms");
    g_assert_cmpuint (notifies, ==, 0);
    lrg_talent_loadout_set_first_level (loadout, 10);
    lrg_talent_loadout_set_points_per_level (loadout, 1);
    lrg_talent_loadout_set_spec (loadout, NULL);
    g_assert_cmpuint (notifies, ==, 3);

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*first_level >= 1*");
    lrg_talent_loadout_set_first_level (loadout, 0);
    g_test_assert_expected_messages ();
    g_assert_cmpuint (lrg_talent_loadout_get_first_level (loadout), ==, 10);
}

static void
test_loadout_points_total (void)
{
    g_autoptr(LrgTalentLoadout) loadout = NULL;

    loadout = lrg_talent_loadout_new ("warrior");

    /* first-level - 1, first-level, cap */
    g_assert_cmpuint (lrg_talent_loadout_get_points_total (loadout, 0), ==, 0);
    g_assert_cmpuint (lrg_talent_loadout_get_points_total (loadout, 9), ==, 0);
    g_assert_cmpuint (lrg_talent_loadout_get_points_total (loadout, 10), ==, 1);
    g_assert_cmpuint (lrg_talent_loadout_get_points_total (loadout, CAP_LEVEL), ==, 51);
    g_assert_cmpuint (lrg_talent_loadout_get_points_available (loadout, 9), ==, 0);
    g_assert_cmpuint (lrg_talent_loadout_get_points_available (loadout, 10), ==, 1);

    lrg_talent_loadout_set_points_per_level (loadout, 2);
    g_assert_cmpuint (lrg_talent_loadout_get_points_total (loadout, CAP_LEVEL), ==, 102);

    lrg_talent_loadout_set_points_per_level (loadout, 0);
    g_assert_cmpuint (lrg_talent_loadout_get_points_total (loadout, CAP_LEVEL), ==, 0);

    /* Saturating arithmetic */
    lrg_talent_loadout_set_points_per_level (loadout, 100);
    lrg_talent_loadout_set_first_level (loadout, 1);
    g_assert_cmpuint (lrg_talent_loadout_get_points_total (loadout, G_MAXUINT), ==, G_MAXUINT);
    lrg_talent_loadout_set_first_level (loadout, G_MAXUINT);
    g_assert_cmpuint (lrg_talent_loadout_get_points_total (loadout, G_MAXUINT), ==, 100);
    g_assert_cmpuint (lrg_talent_loadout_get_points_total (loadout, G_MAXUINT - 1), ==, 0);
}

static void
test_loadout_spend_happy (TalentFixture *fx,
                          gconstpointer  user_data)
{
    g_autoptr(GPtrArray) trees = NULL;

    (void)user_data;

    g_assert_true (lrg_talent_loadout_can_spend (fx->loadout, fx->arms, "a_str", CAP_LEVEL, NULL));
    spend_n (fx->loadout, fx->arms, "a_str", CAP_LEVEL, 5);
    spend_n (fx->loadout, fx->fury, "f_rage", CAP_LEVEL, 2);

    g_assert_cmpuint (lrg_talent_loadout_get_rank (fx->loadout, "arms", "a_str"), ==, 5);
    g_assert_cmpuint (lrg_talent_loadout_get_rank (fx->loadout, "fury", "f_rage"), ==, 2);
    g_assert_cmpuint (lrg_talent_loadout_get_rank (fx->loadout, "arms", "a_crit"), ==, 0);
    g_assert_cmpuint (lrg_talent_loadout_get_rank (fx->loadout, "nope", "a_str"), ==, 0);
    g_assert_cmpuint (lrg_talent_loadout_get_rank (fx->loadout, NULL, NULL), ==, 0);
    g_assert_cmpuint (lrg_talent_loadout_get_points_in_tree (fx->loadout, "arms"), ==, 5);
    g_assert_cmpuint (lrg_talent_loadout_get_points_in_tree (fx->loadout, "fury"), ==, 2);
    g_assert_cmpuint (lrg_talent_loadout_get_points_in_tree (fx->loadout, "holy"), ==, 0);
    g_assert_cmpuint (lrg_talent_loadout_get_points_in_tree (fx->loadout, NULL), ==, 0);
    g_assert_cmpuint (lrg_talent_loadout_get_points_spent (fx->loadout), ==, 7);
    g_assert_cmpuint (lrg_talent_loadout_get_points_available (fx->loadout, CAP_LEVEL), ==, 44);
    /* Available saturates at 0 when queried below the spent level */
    g_assert_cmpuint (lrg_talent_loadout_get_points_available (fx->loadout, 12), ==, 0);

    trees = lrg_talent_loadout_get_trees (fx->loadout);
    g_assert_cmpuint (trees->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (trees, 0), ==, "arms");
    g_assert_cmpstr (g_ptr_array_index (trees, 1), ==, "fury");

    /* Tier 1 is now open in arms (5 points spent there) */
    spend_n (fx->loadout, fx->arms, "a_mastery", CAP_LEVEL, 1);
}

static void
test_loadout_spend_rejections (TalentFixture *fx,
                               gconstpointer  user_data)
{
    (void)user_data;

    /* 1. Wrong class */
    assert_spend_rejected (fx->loadout, fx->holy, "h_heal", CAP_LEVEL,
                           LRG_PROGRESSION_ERROR_INVALID);

    /* 2. Unknown node / NULL node id */
    assert_spend_rejected (fx->loadout, fx->arms, "ghost", CAP_LEVEL,
                           LRG_PROGRESSION_ERROR_INVALID);
    assert_spend_rejected (fx->loadout, fx->arms, NULL, CAP_LEVEL,
                           LRG_PROGRESSION_ERROR_INVALID);
    /* A node of another tree is unknown in this one */
    assert_spend_rejected (fx->loadout, fx->arms, "f_rage", CAP_LEVEL,
                           LRG_PROGRESSION_ERROR_INVALID);

    /* 4. No points below first-level */
    assert_spend_rejected (fx->loadout, fx->arms, "a_str", 9,
                           LRG_PROGRESSION_ERROR_LIMIT);

    /* 5. Tier gate with nothing spent */
    assert_spend_rejected (fx->loadout, fx->arms, "a_mastery", CAP_LEVEL,
                           LRG_PROGRESSION_ERROR_REQUIREMENT);

    /* 3. Max rank */
    spend_n (fx->loadout, fx->arms, "a_str", CAP_LEVEL, 5);
    assert_spend_rejected (fx->loadout, fx->arms, "a_str", CAP_LEVEL,
                           LRG_PROGRESSION_ERROR_LIMIT);

    /* Max rank takes precedence over running out of points */
    assert_spend_rejected (fx->loadout, fx->arms, "a_str", 9,
                           LRG_PROGRESSION_ERROR_LIMIT);

    /* 6. Prerequisite a_crit (max 3) not maxed */
    spend_n (fx->loadout, fx->arms, "a_crit", CAP_LEVEL, 2);
    assert_spend_rejected (fx->loadout, fx->arms, "a_sweep", CAP_LEVEL,
                           LRG_PROGRESSION_ERROR_REQUIREMENT);
    spend_n (fx->loadout, fx->arms, "a_crit", CAP_LEVEL, 1);
    spend_n (fx->loadout, fx->arms, "a_sweep", CAP_LEVEL, 1);

    /* Capstone: tier 2 needs 10 points in arms (have 9) */
    assert_spend_rejected (fx->loadout, fx->arms, "a_capstone", CAP_LEVEL,
                           LRG_PROGRESSION_ERROR_REQUIREMENT);
    spend_n (fx->loadout, fx->arms, "a_mastery", CAP_LEVEL, 1);
    spend_n (fx->loadout, fx->arms, "a_capstone", CAP_LEVEL, 1);
    g_assert_cmpuint (lrg_talent_loadout_get_points_in_tree (fx->loadout, "arms"), ==, 11);
}

static void
test_loadout_last_point (TalentFixture *fx,
                         gconstpointer  user_data)
{
    (void)user_data;

    /* Exactly one point at first-level */
    spend_n (fx->loadout, fx->arms, "a_str", 10, 1);
    assert_spend_rejected (fx->loadout, fx->arms, "a_str", 10, LRG_PROGRESSION_ERROR_LIMIT);
    assert_spend_rejected (fx->loadout, fx->fury, "f_rage", 10, LRG_PROGRESSION_ERROR_LIMIT);

    /* The next level grants exactly one more */
    spend_n (fx->loadout, fx->fury, "f_rage", 11, 1);
    assert_spend_rejected (fx->loadout, fx->fury, "f_rage", 11, LRG_PROGRESSION_ERROR_LIMIT);
    g_assert_cmpuint (lrg_talent_loadout_get_points_available (fx->loadout, 11), ==, 0);

    /* At the cap every one of the 51 points can be spent and no more */
    lrg_talent_loadout_reset (fx->loadout);
    lrg_talent_loadout_set_first_level (fx->loadout, CAP_LEVEL - 13);
    /* 14 points: arms has 15 max, so exhaust points before the tree */
    spend_n (fx->loadout, fx->arms, "a_str", CAP_LEVEL, 5);
    spend_n (fx->loadout, fx->arms, "a_crit", CAP_LEVEL, 3);
    spend_n (fx->loadout, fx->arms, "a_mastery", CAP_LEVEL, 5);
    spend_n (fx->loadout, fx->arms, "a_sweep", CAP_LEVEL, 1);
    g_assert_cmpuint (lrg_talent_loadout_get_points_available (fx->loadout, CAP_LEVEL), ==, 0);
    assert_spend_rejected (fx->loadout, fx->arms, "a_capstone", CAP_LEVEL,
                           LRG_PROGRESSION_ERROR_LIMIT);
}

static void
test_loadout_tier_gate_boundary (TalentFixture *fx,
                                 gconstpointer  user_data)
{
    (void)user_data;

    /* 4 points: tier 1 locked */
    spend_n (fx->loadout, fx->arms, "a_str", CAP_LEVEL, 4);
    assert_spend_rejected (fx->loadout, fx->arms, "a_mastery", CAP_LEVEL,
                           LRG_PROGRESSION_ERROR_REQUIREMENT);

    /* Points in another tree never count toward this tree's gate */
    spend_n (fx->loadout, fx->fury, "f_rage", CAP_LEVEL, 5);
    assert_spend_rejected (fx->loadout, fx->arms, "a_mastery", CAP_LEVEL,
                           LRG_PROGRESSION_ERROR_REQUIREMENT);

    /* Exactly tier-gate points opens tier 1 */
    spend_n (fx->loadout, fx->arms, "a_crit", CAP_LEVEL, 1);
    g_assert_true (lrg_talent_loadout_can_spend (fx->loadout, fx->arms, "a_mastery", CAP_LEVEL, NULL));

    /* A different tier gate is honoured */
    lrg_talent_tree_set_tier_gate (fx->arms, 6);
    assert_spend_rejected (fx->loadout, fx->arms, "a_mastery", CAP_LEVEL,
                           LRG_PROGRESSION_ERROR_REQUIREMENT);
    lrg_talent_tree_set_tier_gate (fx->arms, 0);
    spend_n (fx->loadout, fx->arms, "a_mastery", CAP_LEVEL, 1);
}

static void
test_loadout_prerequisite_chain (TalentFixture *fx,
                                 gconstpointer  user_data)
{
    g_autoptr(LrgTalentTree) chain = NULL;

    (void)user_data;

    /* A three-link chain with gate 0 so only prerequisites matter */
    chain = lrg_talent_tree_new ("chain", "warrior");
    lrg_talent_tree_set_tier_gate (chain, 0);
    add_ok (chain, make_node ("c0", 0, 0, 2, NULL, NULL, NULL));
    add_ok (chain, make_node ("c1", 1, 0, 2, "c0", NULL, NULL));
    add_ok (chain, make_node ("c2", 2, 0, 1, "c1", NULL, NULL));
    g_assert_true (lrg_talent_tree_validate (chain, NULL));

    assert_spend_rejected (fx->loadout, chain, "c1", CAP_LEVEL, LRG_PROGRESSION_ERROR_REQUIREMENT);
    assert_spend_rejected (fx->loadout, chain, "c2", CAP_LEVEL, LRG_PROGRESSION_ERROR_REQUIREMENT);
    spend_n (fx->loadout, chain, "c0", CAP_LEVEL, 1);
    assert_spend_rejected (fx->loadout, chain, "c1", CAP_LEVEL, LRG_PROGRESSION_ERROR_REQUIREMENT);
    spend_n (fx->loadout, chain, "c0", CAP_LEVEL, 1);
    spend_n (fx->loadout, chain, "c1", CAP_LEVEL, 1);
    /* c1 is ranked but not maxed */
    assert_spend_rejected (fx->loadout, chain, "c2", CAP_LEVEL, LRG_PROGRESSION_ERROR_REQUIREMENT);
    spend_n (fx->loadout, chain, "c1", CAP_LEVEL, 1);
    spend_n (fx->loadout, chain, "c2", CAP_LEVEL, 1);
    g_assert_cmpuint (lrg_talent_loadout_get_points_in_tree (fx->loadout, "chain"), ==, 5);
}

static void
test_loadout_missing_prereq_in_tree (TalentFixture *fx,
                                     gconstpointer  user_data)
{
    g_autoptr(LrgTalentTree) broken = NULL;

    (void)user_data;

    /* An (invalid) tree whose prerequisite does not exist blocks spending */
    broken = lrg_talent_tree_new ("broken", "warrior");
    lrg_talent_tree_set_tier_gate (broken, 0);
    add_ok (broken, make_node ("b", 1, 0, 1, "ghost", NULL, NULL));
    assert_spend_rejected (fx->loadout, broken, "b", CAP_LEVEL, LRG_PROGRESSION_ERROR_REQUIREMENT);
}

static void
test_loadout_reset_keeps_spec (TalentFixture *fx,
                               gconstpointer  user_data)
{
    g_autoptr(GPtrArray) trees = NULL;

    (void)user_data;

    lrg_talent_loadout_set_spec (fx->loadout, "arms");
    spend_n (fx->loadout, fx->arms, "a_str", CAP_LEVEL, 5);
    spend_n (fx->loadout, fx->fury, "f_rage", CAP_LEVEL, 1);

    lrg_talent_loadout_reset (fx->loadout);
    g_assert_cmpuint (lrg_talent_loadout_get_points_spent (fx->loadout), ==, 0);
    g_assert_cmpuint (lrg_talent_loadout_get_points_in_tree (fx->loadout, "arms"), ==, 0);
    g_assert_cmpuint (lrg_talent_loadout_get_rank (fx->loadout, "arms", "a_str"), ==, 0);
    g_assert_cmpuint (lrg_talent_loadout_get_points_available (fx->loadout, CAP_LEVEL), ==, 51);
    g_assert_cmpstr (lrg_talent_loadout_get_spec (fx->loadout), ==, "arms");
    trees = lrg_talent_loadout_get_trees (fx->loadout);
    g_assert_cmpuint (trees->len, ==, 0);

    /* Points can be re-spent after a reset */
    spend_n (fx->loadout, fx->fury, "f_rage", CAP_LEVEL, 5);
}

static void
test_loadout_granted_abilities (TalentFixture *fx,
                                gconstpointer  user_data)
{
    g_autoptr(GPtrArray) none = NULL;
    g_autoptr(GPtrArray) granted = NULL;
    g_autoptr(GPtrArray) partial = NULL;
    g_autoptr(GHashTable) only_arms = NULL;

    (void)user_data;

    none = lrg_talent_loadout_get_granted_abilities (fx->loadout, fx->trees);
    g_assert_cmpuint (none->len, ==, 0);

    /* Full arms line to the capstone plus fury frenzy */
    spend_n (fx->loadout, fx->arms, "a_str", CAP_LEVEL, 5);
    spend_n (fx->loadout, fx->arms, "a_crit", CAP_LEVEL, 3);
    spend_n (fx->loadout, fx->arms, "a_sweep", CAP_LEVEL, 1);
    spend_n (fx->loadout, fx->arms, "a_mastery", CAP_LEVEL, 1);
    spend_n (fx->loadout, fx->arms, "a_capstone", CAP_LEVEL, 1);
    spend_n (fx->loadout, fx->fury, "f_rage", CAP_LEVEL, 5);
    spend_n (fx->loadout, fx->fury, "f_frenzy", CAP_LEVEL, 1);

    granted = lrg_talent_loadout_get_granted_abilities (fx->loadout, fx->trees);
    g_assert_cmpuint (granted->len, ==, 3);
    g_assert_cmpstr (g_ptr_array_index (granted, 0), ==, "frenzy");
    g_assert_cmpstr (g_ptr_array_index (granted, 1), ==, "mortal_strike");
    g_assert_cmpstr (g_ptr_array_index (granted, 2), ==, "sweeping_strikes");

    /* Trees missing from the table are skipped */
    only_arms = g_hash_table_new (g_str_hash, g_str_equal);
    g_hash_table_insert (only_arms, (gpointer)"arms", fx->arms);
    partial = lrg_talent_loadout_get_granted_abilities (fx->loadout, only_arms);
    g_assert_cmpuint (partial->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (partial, 0), ==, "mortal_strike");
}

static void
test_loadout_granted_dedup (void)
{
    g_autoptr(LrgTalentTree) tree = NULL;
    g_autoptr(LrgTalentLoadout) loadout = NULL;
    g_autoptr(GHashTable) trees = NULL;
    g_autoptr(GPtrArray) granted = NULL;

    tree = lrg_talent_tree_new ("t", "c");
    add_ok (tree, make_node ("a", 0, 0, 1, NULL, "shout", NULL));
    add_ok (tree, make_node ("b", 0, 1, 1, NULL, "shout", NULL));
    add_ok (tree, make_node ("c", 0, 2, 1, NULL, "bash", NULL));
    trees = g_hash_table_new (g_str_hash, g_str_equal);
    g_hash_table_insert (trees, (gpointer)"t", tree);

    loadout = lrg_talent_loadout_new ("c");
    spend_n (loadout, tree, "a", CAP_LEVEL, 1);
    spend_n (loadout, tree, "b", CAP_LEVEL, 1);
    spend_n (loadout, tree, "c", CAP_LEVEL, 1);

    granted = lrg_talent_loadout_get_granted_abilities (loadout, trees);
    g_assert_cmpuint (granted->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (granted, 0), ==, "bash");
    g_assert_cmpstr (g_ptr_array_index (granted, 1), ==, "shout");
}

static void
test_loadout_sum_effect (TalentFixture *fx,
                         gconstpointer  user_data)
{
    g_autoptr(LrgTalentTree) sparse = NULL;
    LrgTalentNode *node;

    (void)user_data;

    g_assert_cmpfloat (lrg_talent_loadout_sum_effect (fx->loadout, fx->trees, "strength"), ==, 0.0);

    spend_n (fx->loadout, fx->arms, "a_str", CAP_LEVEL, 3);
    g_assert_cmpfloat (lrg_talent_loadout_sum_effect (fx->loadout, fx->trees, "strength"), ==, 3.0);
    spend_n (fx->loadout, fx->arms, "a_str", CAP_LEVEL, 2);
    spend_n (fx->loadout, fx->arms, "a_crit", CAP_LEVEL, 3);
    spend_n (fx->loadout, fx->arms, "a_sweep", CAP_LEVEL, 1);
    spend_n (fx->loadout, fx->arms, "a_mastery", CAP_LEVEL, 1);
    spend_n (fx->loadout, fx->arms, "a_capstone", CAP_LEVEL, 1);
    spend_n (fx->loadout, fx->fury, "f_rage", CAP_LEVEL, 2);

    /* a_str 5 + capstone 10 + f_rage rank 2 (1.0) */
    g_assert_cmpfloat_with_epsilon (lrg_talent_loadout_sum_effect (fx->loadout, fx->trees, "strength"),
                                    16.0, 1e-9);
    g_assert_cmpfloat_with_epsilon (lrg_talent_loadout_sum_effect (fx->loadout, fx->trees, "crit"),
                                    0.03, 1e-9);
    g_assert_cmpfloat (lrg_talent_loadout_sum_effect (fx->loadout, fx->trees, "haste"), ==, 0.0);
    g_assert_cmpfloat (lrg_talent_loadout_sum_effect (fx->loadout, fx->trees, NULL), ==, 0.0);

    /* Clamp: a node with fewer values than ranks uses its last value */
    sparse = lrg_talent_tree_new ("sparse", "warrior");
    node = make_node ("s", 0, 0, 3, NULL, NULL, "armor");
    lrg_talent_node_add_rank_value (node, 2.0);
    add_ok (sparse, node);
    g_hash_table_insert (fx->trees, g_strdup ("sparse"), g_object_ref (sparse));
    spend_n (fx->loadout, sparse, "s", CAP_LEVEL, 3);
    g_assert_cmpfloat (lrg_talent_loadout_sum_effect (fx->loadout, fx->trees, "armor"), ==, 2.0);
}

static void
test_loadout_validate_legit (TalentFixture *fx,
                             gconstpointer  user_data)
{
    GError *error = NULL;
    g_autoptr(GVariant) before = NULL;
    g_autoptr(GVariant) after = NULL;

    (void)user_data;

    /* Empty loadout is legal at any level, even below first-level */
    g_assert_true (lrg_talent_loadout_validate (fx->loadout, fx->trees, 1, &error));
    g_assert_no_error (error);

    /*
     * Spend in an order different from the replay order: tier 1 before
     * finishing tier 0 columns, then more tier 0 afterwards.
     */
    spend_n (fx->loadout, fx->arms, "a_str", CAP_LEVEL, 5);
    spend_n (fx->loadout, fx->arms, "a_mastery", CAP_LEVEL, 5);
    spend_n (fx->loadout, fx->arms, "a_crit", CAP_LEVEL, 3);
    spend_n (fx->loadout, fx->arms, "a_sweep", CAP_LEVEL, 1);
    spend_n (fx->loadout, fx->arms, "a_capstone", CAP_LEVEL, 1);
    spend_n (fx->loadout, fx->fury, "f_rage", CAP_LEVEL, 5);
    spend_n (fx->loadout, fx->fury, "f_frenzy", CAP_LEVEL, 1);
    lrg_talent_loadout_set_spec (fx->loadout, "arms");

    before = lrg_talent_loadout_to_variant (fx->loadout);
    g_assert_true (lrg_talent_loadout_validate (fx->loadout, fx->trees, CAP_LEVEL, &error));
    g_assert_no_error (error);
    after = lrg_talent_loadout_to_variant (fx->loadout);
    g_assert_true (g_variant_equal (before, after));

    /* 21 points: legal from level 30 (first 10), not at 29 */
    g_assert_true (lrg_talent_loadout_validate (fx->loadout, fx->trees, 30, NULL));
    g_assert_false (lrg_talent_loadout_validate (fx->loadout, fx->trees, 29, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_clear_error (&error);
}

static void
test_loadout_validate_spec (TalentFixture *fx,
                            gconstpointer  user_data)
{
    GError *error = NULL;

    (void)user_data;

    lrg_talent_loadout_set_spec (fx->loadout, "fury");
    g_assert_true (lrg_talent_loadout_validate (fx->loadout, fx->trees, CAP_LEVEL, &error));
    g_assert_no_error (error);

    /* Unknown spec tree */
    lrg_talent_loadout_set_spec (fx->loadout, "ghost");
    g_assert_false (lrg_talent_loadout_validate (fx->loadout, fx->trees, CAP_LEVEL, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    /* Spec tree of another class */
    lrg_talent_loadout_set_spec (fx->loadout, "holy");
    g_assert_false (lrg_talent_loadout_validate (fx->loadout, fx->trees, CAP_LEVEL, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_cmpstr (lrg_talent_loadout_get_spec (fx->loadout), ==, "holy");
}

static void
test_loadout_validate_tampered (TalentFixture *fx,
                                gconstpointer  user_data)
{
    (void)user_data;

    /* Skips the tier gate: a tier-1 node with no tier-0 points */
    {
        const gchar *t[] = { "arms" };
        const gchar *n[] = { "a_mastery" };
        const guint r[] = { 1 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 1);
        assert_validate (fx, v, CAP_LEVEL, LRG_PROGRESSION_ERROR_REQUIREMENT);
    }
    /* Tier gate short by one (4 below, then tier 1) */
    {
        const gchar *t[] = { "arms", "arms" };
        const gchar *n[] = { "a_str", "a_mastery" };
        const guint r[] = { 4, 1 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 2);
        assert_validate (fx, v, CAP_LEVEL, LRG_PROGRESSION_ERROR_REQUIREMENT);
    }
    /* Tier-1 points cannot pay for their own gate in the same tier */
    {
        const gchar *t[] = { "arms", "arms" };
        const gchar *n[] = { "a_str", "a_mastery" };
        const guint r[] = { 1, 5 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 2);
        assert_validate (fx, v, CAP_LEVEL, LRG_PROGRESSION_ERROR_REQUIREMENT);
    }
    /* Exceeds max rank (a_sweep max 1) */
    {
        const gchar *t[] = { "arms", "arms", "arms" };
        const gchar *n[] = { "a_str", "a_crit", "a_sweep" };
        const guint r[] = { 5, 3, 2 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 3);
        assert_validate (fx, v, CAP_LEVEL, LRG_PROGRESSION_ERROR_LIMIT);
    }
    /* Missing prerequisite (a_crit only rank 2) */
    {
        const gchar *t[] = { "arms", "arms", "arms" };
        const gchar *n[] = { "a_str", "a_crit", "a_sweep" };
        const guint r[] = { 5, 2, 1 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 3);
        assert_validate (fx, v, CAP_LEVEL, LRG_PROGRESSION_ERROR_REQUIREMENT);
    }
    /* Capstone without its prerequisite even though 10 points are spent */
    {
        const gchar *t[] = { "arms", "arms", "arms" };
        const gchar *n[] = { "a_str", "a_mastery", "a_capstone" };
        const guint r[] = { 5, 5, 1 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 3);
        assert_validate (fx, v, CAP_LEVEL, LRG_PROGRESSION_ERROR_REQUIREMENT);
    }
    /* Too many points for the level: 5 points at level 13 (4 available) */
    {
        const gchar *t[] = { "arms" };
        const gchar *n[] = { "a_str" };
        const guint r[] = { 5 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 1);
        assert_validate (fx, v, 13, LRG_PROGRESSION_ERROR_LIMIT);
        assert_validate (fx, v, 14, 0);
        assert_validate (fx, v, 9, LRG_PROGRESSION_ERROR_LIMIT);
    }
    /* Wrong class: a priest tree in a warrior loadout */
    {
        const gchar *t[] = { "holy" };
        const gchar *n[] = { "h_heal" };
        const guint r[] = { 1 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 1);
        assert_validate (fx, v, CAP_LEVEL, LRG_PROGRESSION_ERROR_INVALID);
    }
    /* Unknown tree */
    {
        const gchar *t[] = { "ghost" };
        const gchar *n[] = { "a_str" };
        const guint r[] = { 1 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 1);
        assert_validate (fx, v, CAP_LEVEL, LRG_PROGRESSION_ERROR_NOT_FOUND);
    }
    /* Unknown node in a known tree */
    {
        const gchar *t[] = { "arms" };
        const gchar *n[] = { "ghost" };
        const guint r[] = { 1 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 1);
        assert_validate (fx, v, CAP_LEVEL, LRG_PROGRESSION_ERROR_INVALID);
    }
    /* Legit rows listed in hostile order still validate */
    {
        const gchar *t[] = { "fury", "arms", "arms", "arms", "arms", "arms" };
        const gchar *n[] = { "f_rage", "a_capstone", "a_sweep", "a_mastery", "a_crit", "a_str" };
        const guint r[] = { 1, 1, 1, 1, 3, 5 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", "arms", t, n, r, 6);
        assert_validate (fx, v, CAP_LEVEL, 0);
    }
}

static void
test_loadout_validate_wrong_key (TalentFixture *fx,
                                 gconstpointer  user_data)
{
    g_autoptr(GHashTable) bad = NULL;
    GError *error = NULL;

    (void)user_data;

    /* A tree registered under another id is treated as unknown */
    spend_n (fx->loadout, fx->arms, "a_str", CAP_LEVEL, 1);
    bad = g_hash_table_new (g_str_hash, g_str_equal);
    g_hash_table_insert (bad, (gpointer)"arms", fx->fury);
    g_assert_false (lrg_talent_loadout_validate (fx->loadout, bad, CAP_LEVEL, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND);
    g_clear_error (&error);

    /* Tree data changed after spending (max rank lowered) is caught */
    spend_n (fx->loadout, fx->arms, "a_str", CAP_LEVEL, 4);
    ((LrgTalentNode *)lrg_talent_tree_get_node (fx->arms, "a_str"))->max_rank = 4;
    g_assert_false (lrg_talent_loadout_validate (fx->loadout, fx->trees, CAP_LEVEL, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_clear_error (&error);
}

static void
test_loadout_variant_round_trip (TalentFixture *fx,
                                 gconstpointer  user_data)
{
    g_autoptr(GVariant) v1 = NULL;
    g_autoptr(GVariant) v2 = NULL;
    g_autoptr(GVariant) e1 = NULL;
    g_autoptr(GVariant) e2 = NULL;
    g_autoptr(LrgTalentLoadout) restored = NULL;
    g_autoptr(LrgTalentLoadout) empty = NULL;
    GError *error = NULL;

    (void)user_data;

    /* Empty with no spec */
    e1 = lrg_talent_loadout_to_variant (fx->loadout);
    g_assert_false (g_variant_is_floating (e1));
    g_assert_cmpstr (g_variant_get_type_string (e1), ==, LRG_TALENT_LOADOUT_VARIANT_TYPE);
    empty = restore_loadout (e1);
    g_assert_null (lrg_talent_loadout_get_spec (empty));
    e2 = lrg_talent_loadout_to_variant (empty);
    g_assert_true (g_variant_equal (e1, e2));

    /* Populated with spec */
    lrg_talent_loadout_set_spec (fx->loadout, "arms");
    spend_n (fx->loadout, fx->arms, "a_str", CAP_LEVEL, 5);
    spend_n (fx->loadout, fx->arms, "a_crit", CAP_LEVEL, 3);
    spend_n (fx->loadout, fx->arms, "a_sweep", CAP_LEVEL, 1);
    spend_n (fx->loadout, fx->fury, "f_rage", CAP_LEVEL, 2);

    v1 = lrg_talent_loadout_to_variant (fx->loadout);
    g_assert_true (g_variant_is_normal_form (v1));
    restored = restore_loadout (v1);
    v2 = lrg_talent_loadout_to_variant (restored);
    g_assert_true (g_variant_equal (v1, v2));

    g_assert_cmpstr (lrg_talent_loadout_get_class_id (restored), ==, "warrior");
    g_assert_cmpstr (lrg_talent_loadout_get_spec (restored), ==, "arms");
    g_assert_cmpuint (lrg_talent_loadout_get_points_spent (restored), ==, 11);
    g_assert_cmpuint (lrg_talent_loadout_get_rank (restored, "arms", "a_crit"), ==, 3);
    g_assert_cmpuint (lrg_talent_loadout_get_points_in_tree (restored, "fury"), ==, 2);
    g_assert_true (lrg_talent_loadout_validate (restored, fx->trees, CAP_LEVEL, &error));
    g_assert_no_error (error);

    /* Rows are sorted by tree then node */
    {
        g_autoptr(GVariant) rows = g_variant_get_child_value (v1, 2);
        const gchar *tree_id;
        const gchar *node_id;
        guint rank;

        g_assert_cmpuint (g_variant_n_children (rows), ==, 4);
        g_variant_get_child (rows, 0, "(&s&su)", &tree_id, &node_id, &rank);
        g_assert_cmpstr (tree_id, ==, "arms");
        g_assert_cmpstr (node_id, ==, "a_crit");
        g_assert_cmpuint (rank, ==, 3);
        g_variant_get_child (rows, 3, "(&s&su)", &tree_id, &node_id, &rank);
        g_assert_cmpstr (tree_id, ==, "fury");
        g_assert_cmpstr (node_id, ==, "f_rage");
    }
}

static void
test_loadout_variant_hostile (void)
{
    g_autofree gchar *id128 = NULL;
    g_autofree gchar *id129 = NULL;

    id128 = g_strnfill (128, 'a');
    id129 = g_strnfill (129, 'b');

    /* Wrong type strings */
    {
        g_autoptr(GVariant) v = g_variant_ref_sink (g_variant_new ("(ssa(ssu))", "warrior", "arms", NULL));
        assert_loadout_variant_rejected (v);
    }
    {
        g_autoptr(GVariant) v = g_variant_ref_sink (g_variant_new_string ("warrior"));
        assert_loadout_variant_rejected (v);
    }
    {
        g_autoptr(GVariant) v = g_variant_ref_sink (g_variant_new ("(smsa(ssi))", "warrior", NULL, NULL));
        assert_loadout_variant_rejected (v);
    }
    /* Empty class id, empty spec, overlong class id; 128 bytes is fine */
    {
        g_autoptr(GVariant) v = build_loadout_variant ("", NULL, NULL, NULL, NULL, 0);
        assert_loadout_variant_rejected (v);
    }
    {
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", "", NULL, NULL, NULL, 0);
        assert_loadout_variant_rejected (v);
    }
    {
        g_autoptr(GVariant) v = build_loadout_variant (id129, NULL, NULL, NULL, NULL, 0);
        assert_loadout_variant_rejected (v);
    }
    {
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", id129, NULL, NULL, NULL, 0);
        assert_loadout_variant_rejected (v);
    }
    {
        g_autoptr(GVariant) v = build_loadout_variant (id128, id128, NULL, NULL, NULL, 0);
        g_autoptr(LrgTalentLoadout) ok = restore_loadout (v);
        g_assert_cmpstr (lrg_talent_loadout_get_class_id (ok), ==, id128);
    }
    /* Empty / overlong tree and node ids */
    {
        const gchar *t[] = { "" };
        const gchar *n[] = { "a" };
        const guint r[] = { 1 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 1);
        assert_loadout_variant_rejected (v);
    }
    {
        const gchar *t[] = { "arms" };
        const gchar *n[] = { "" };
        const guint r[] = { 1 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 1);
        assert_loadout_variant_rejected (v);
    }
    {
        const gchar *t[] = { "arms" };
        const gchar *n[] = { id129 };
        const guint r[] = { 1 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 1);
        assert_loadout_variant_rejected (v);
    }
    /* Rank 0 and 101 rejected; 100 accepted */
    {
        const gchar *t[] = { "arms" };
        const gchar *n[] = { "a" };
        const guint r[] = { 0 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 1);
        assert_loadout_variant_rejected (v);
    }
    {
        const gchar *t[] = { "arms" };
        const gchar *n[] = { "a" };
        const guint r[] = { 101 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 1);
        assert_loadout_variant_rejected (v);
    }
    {
        const gchar *t[] = { "arms" };
        const gchar *n[] = { "a" };
        const guint r[] = { 100 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 1);
        g_autoptr(LrgTalentLoadout) ok = restore_loadout (v);
        g_assert_cmpuint (lrg_talent_loadout_get_rank (ok, "arms", "a"), ==, 100);
    }
    /* Duplicate (tree, node) rows, even with different ranks */
    {
        const gchar *t[] = { "arms", "fury", "arms" };
        const gchar *n[] = { "a", "a", "a" };
        const guint r[] = { 1, 1, 2 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 3);
        assert_loadout_variant_rejected (v);
    }
    /* Same node id in different trees is not a duplicate */
    {
        const gchar *t[] = { "arms", "fury" };
        const gchar *n[] = { "a", "a" };
        const guint r[] = { 1, 2 };
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, t, n, r, 2);
        g_autoptr(LrgTalentLoadout) ok = restore_loadout (v);
        g_assert_cmpuint (lrg_talent_loadout_get_points_spent (ok), ==, 3);
    }
    /* Invalid UTF-8 in serialised data (non-normal form) */
    {
        g_autoptr(GVariant) good = build_loadout_variant ("warrior", NULL, NULL, NULL, NULL, 0);
        g_autoptr(GVariant) bad = corrupt_first_byte (good);
        g_assert_false (g_variant_is_normal_form (bad));
        assert_loadout_variant_rejected (bad);
    }
}

static void
test_loadout_variant_row_limit (void)
{
    g_autoptr(GPtrArray) ids = NULL;
    const gchar **tree_ids;
    const gchar **node_ids;
    guint *ranks;
    guint i;

    ids = g_ptr_array_new_with_free_func (g_free);
    tree_ids = g_new0 (const gchar *, LRG_TALENT_LOADOUT_MAX_ROWS + 1);
    node_ids = g_new0 (const gchar *, LRG_TALENT_LOADOUT_MAX_ROWS + 1);
    ranks = g_new0 (guint, LRG_TALENT_LOADOUT_MAX_ROWS + 1);
    for (i = 0; i < LRG_TALENT_LOADOUT_MAX_ROWS + 1; i++)
    {
        gchar *id = g_strdup_printf ("n%04u", i);

        g_ptr_array_add (ids, id);
        tree_ids[i] = "t";
        node_ids[i] = id;
        ranks[i] = 1;
    }

    /* Exactly the maximum is accepted */
    {
        g_autoptr(GVariant) v = build_loadout_variant ("c", NULL, tree_ids, node_ids, ranks,
                                                       LRG_TALENT_LOADOUT_MAX_ROWS);
        g_autoptr(LrgTalentLoadout) ok = restore_loadout (v);
        g_assert_cmpuint (lrg_talent_loadout_get_points_spent (ok), ==, LRG_TALENT_LOADOUT_MAX_ROWS);
    }
    /* One more is rejected */
    {
        g_autoptr(GVariant) v = build_loadout_variant ("c", NULL, tree_ids, node_ids, ranks,
                                                       LRG_TALENT_LOADOUT_MAX_ROWS + 1);
        assert_loadout_variant_rejected (v);
    }

    g_free (tree_ids);
    g_free (node_ids);
    g_free (ranks);
}

/* ========================================================================== */
/*                              LrgTalentBook                                 */
/* ========================================================================== */

static void
test_book_new (void)
{
    g_autoptr(LrgTalentBook) book = NULL;
    LrgTalentLoadout *first;

    book = lrg_talent_book_new ("warrior", 2);
    g_assert_cmpstr (lrg_talent_book_get_class_id (book), ==, "warrior");
    g_assert_cmpuint (lrg_talent_book_get_capacity (book), ==, 2);
    g_assert_cmpuint (lrg_talent_book_get_unlocked (book), ==, 1);
    g_assert_cmpuint (lrg_talent_book_get_active_index (book), ==, 0);
    g_assert_cmpuint (lrg_talent_book_get_respec_count (book), ==, 0);

    first = lrg_talent_book_get_loadout (book, 0);
    g_assert_nonnull (first);
    g_assert_true (lrg_talent_book_get_active (book) == first);
    g_assert_cmpstr (lrg_talent_loadout_get_class_id (first), ==, "warrior");
    g_assert_cmpuint (lrg_talent_loadout_get_points_spent (first), ==, 0);
    g_assert_null (lrg_talent_book_get_loadout (book, 1));
    g_assert_null (lrg_talent_book_get_loadout (book, 99));

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*capacity*");
    g_assert_null (lrg_talent_book_new ("warrior", 0));
    g_test_assert_expected_messages ();
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*capacity*");
    g_assert_null (lrg_talent_book_new ("warrior", LRG_TALENT_BOOK_MAX_CAPACITY + 1));
    g_test_assert_expected_messages ();
}

static void
test_book_properties (void)
{
    g_autoptr(LrgTalentBook) book = NULL;
    g_autoptr(LrgTalentBook) defaulted = NULL;
    g_autofree gchar *class_id = NULL;
    guint capacity;
    guint unlocked;
    guint active;
    guint respec;

    book = g_object_new (LRG_TYPE_TALENT_BOOK, "class-id", "mage", "capacity", 3u, NULL);
    g_object_set (book, "respec-count", 4u, NULL);
    g_object_get (book,
                  "class-id", &class_id,
                  "capacity", &capacity,
                  "unlocked", &unlocked,
                  "active-index", &active,
                  "respec-count", &respec,
                  NULL);
    g_assert_cmpstr (class_id, ==, "mage");
    g_assert_cmpuint (capacity, ==, 3);
    g_assert_cmpuint (unlocked, ==, 1);
    g_assert_cmpuint (active, ==, 0);
    g_assert_cmpuint (respec, ==, 4);

    defaulted = g_object_new (LRG_TYPE_TALENT_BOOK, "class-id", "mage", NULL);
    g_assert_cmpuint (lrg_talent_book_get_capacity (defaulted), ==, LRG_TALENT_BOOK_DEFAULT_CAPACITY);
}

static void
test_book_unlock_capacity (void)
{
    g_autoptr(LrgTalentBook) book = NULL;
    g_autoptr(LrgTalentBook) single = NULL;
    GError *error = NULL;
    guint notifies;
    guint i;

    book = lrg_talent_book_new ("warrior", 4);
    notifies = 0;
    g_signal_connect (book, "notify::unlocked", G_CALLBACK (on_notify_count), &notifies);
    for (i = 1; i < 4; i++)
    {
        g_assert_true (lrg_talent_book_unlock (book, &error));
        g_assert_no_error (error);
        g_assert_cmpuint (lrg_talent_book_get_unlocked (book), ==, i + 1);
        g_assert_nonnull (lrg_talent_book_get_loadout (book, i));
        g_assert_cmpstr (lrg_talent_loadout_get_class_id (lrg_talent_book_get_loadout (book, i)), ==, "warrior");
    }
    g_assert_cmpuint (notifies, ==, 3);

    /* At capacity: LIMIT, unchanged */
    g_assert_false (lrg_talent_book_unlock (book, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_talent_book_get_unlocked (book), ==, 4);
    g_assert_null (lrg_talent_book_get_loadout (book, 4));

    /* Capacity 1 cannot unlock anything */
    single = lrg_talent_book_new ("warrior", 1);
    g_assert_false (lrg_talent_book_unlock (single, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_talent_book_get_unlocked (single), ==, 1);
}

static void
test_book_set_active (TalentFixture *fx,
                      gconstpointer  user_data)
{
    g_autoptr(LrgTalentBook) book = NULL;
    GError *error = NULL;
    guint notifies;

    (void)user_data;

    book = lrg_talent_book_new ("warrior", 2);

    /* Locked slot: REQUIREMENT, unchanged */
    g_assert_false (lrg_talent_book_set_active (book, 1, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_talent_book_get_active_index (book), ==, 0);

    /* Beyond capacity: INVALID, unchanged */
    g_assert_false (lrg_talent_book_set_active (book, 2, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_false (lrg_talent_book_set_active (book, G_MAXUINT, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_talent_book_get_active_index (book), ==, 0);

    /* Same index is a no-op success */
    notifies = 0;
    g_signal_connect (book, "notify::active-index", G_CALLBACK (on_notify_count), &notifies);
    g_assert_true (lrg_talent_book_set_active (book, 0, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (notifies, ==, 0);

    /* Unlock and switch; loadouts are independent */
    g_assert_true (lrg_talent_book_unlock (book, NULL));
    spend_n (lrg_talent_book_get_active (book), fx->arms, "a_str", CAP_LEVEL, 3);
    g_assert_true (lrg_talent_book_set_active (book, 1, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (notifies, ==, 1);
    g_assert_cmpuint (lrg_talent_book_get_active_index (book), ==, 1);
    g_assert_true (lrg_talent_book_get_active (book) == lrg_talent_book_get_loadout (book, 1));
    g_assert_cmpuint (lrg_talent_loadout_get_points_spent (lrg_talent_book_get_active (book)), ==, 0);
    spend_n (lrg_talent_book_get_active (book), fx->fury, "f_rage", CAP_LEVEL, 2);
    g_assert_cmpuint (lrg_talent_loadout_get_points_spent (lrg_talent_book_get_loadout (book, 0)), ==, 3);
    g_assert_cmpuint (lrg_talent_loadout_get_points_in_tree (lrg_talent_book_get_loadout (book, 0), "fury"), ==, 0);
}

static void
test_book_respec_counter (TalentFixture *fx,
                          gconstpointer  user_data)
{
    g_autoptr(LrgTalentBook) book = NULL;
    LrgTalentLoadout *active;

    (void)user_data;

    book = lrg_talent_book_new ("warrior", 2);
    active = lrg_talent_book_get_active (book);
    lrg_talent_loadout_set_spec (active, "arms");
    spend_n (active, fx->arms, "a_str", CAP_LEVEL, 2);

    /* A respec is a reset plus a counter bump; the spec survives */
    lrg_talent_loadout_reset (active);
    lrg_talent_book_record_respec (book);
    lrg_talent_book_record_respec (book);
    lrg_talent_book_record_respec (book);
    g_assert_cmpuint (lrg_talent_book_get_respec_count (book), ==, 3);
    g_assert_cmpstr (lrg_talent_loadout_get_spec (active), ==, "arms");
    g_assert_cmpuint (lrg_talent_loadout_get_points_spent (active), ==, 0);

    lrg_talent_book_set_respec_count (book, 7);
    g_assert_cmpuint (lrg_talent_book_get_respec_count (book), ==, 7);

    /* Saturates */
    lrg_talent_book_set_respec_count (book, G_MAXUINT);
    lrg_talent_book_record_respec (book);
    g_assert_cmpuint (lrg_talent_book_get_respec_count (book), ==, G_MAXUINT);
}

static void
test_book_point_rules (void)
{
    g_autoptr(LrgTalentBook) book = NULL;

    book = lrg_talent_book_new ("warrior", 3);
    lrg_talent_book_set_point_rules (book, 5, 2);
    g_assert_cmpuint (lrg_talent_loadout_get_first_level (lrg_talent_book_get_loadout (book, 0)), ==, 5);
    g_assert_cmpuint (lrg_talent_loadout_get_points_per_level (lrg_talent_book_get_loadout (book, 0)), ==, 2);

    /* Future unlocks inherit the rules */
    g_assert_true (lrg_talent_book_unlock (book, NULL));
    g_assert_cmpuint (lrg_talent_loadout_get_first_level (lrg_talent_book_get_loadout (book, 1)), ==, 5);
    g_assert_cmpuint (lrg_talent_loadout_get_points_total (lrg_talent_book_get_loadout (book, 1), 5), ==, 2);
}

static void
test_book_validate (TalentFixture *fx,
                    gconstpointer  user_data)
{
    g_autoptr(LrgTalentBook) book = NULL;
    GError *error = NULL;

    (void)user_data;

    book = lrg_talent_book_new ("warrior", 2);
    g_assert_true (lrg_talent_book_unlock (book, NULL));
    spend_n (lrg_talent_book_get_loadout (book, 0), fx->arms, "a_str", CAP_LEVEL, 5);
    spend_n (lrg_talent_book_get_loadout (book, 1), fx->fury, "f_rage", CAP_LEVEL, 3);
    g_assert_true (lrg_talent_book_validate (book, fx->trees, CAP_LEVEL, &error));
    g_assert_no_error (error);

    /* With first-level 12, level 14 grants 3 points: loadout 0 (5 spent) fails */
    g_assert_true (lrg_talent_book_validate (book, fx->trees, 14, NULL));
    lrg_talent_book_set_point_rules (book, 12, 1);
    g_assert_false (lrg_talent_book_validate (book, fx->trees, 14, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_assert_true (g_str_has_prefix (error->message, "Talent loadout 0: "));
    g_clear_error (&error);
}

static void
test_book_variant_round_trip (TalentFixture *fx,
                              gconstpointer  user_data)
{
    g_autoptr(LrgTalentBook) book = NULL;
    g_autoptr(LrgTalentBook) restored = NULL;
    g_autoptr(LrgTalentBook) fresh_restored = NULL;
    g_autoptr(GVariant) v1 = NULL;
    g_autoptr(GVariant) v2 = NULL;
    g_autoptr(GVariant) f1 = NULL;
    g_autoptr(GVariant) f2 = NULL;
    g_autoptr(LrgTalentBook) fresh = NULL;
    GError *error = NULL;

    (void)user_data;

    /* Fresh book */
    fresh = lrg_talent_book_new ("warrior", 2);
    f1 = lrg_talent_book_to_variant (fresh);
    g_assert_false (g_variant_is_floating (f1));
    g_assert_cmpstr (g_variant_get_type_string (f1), ==, LRG_TALENT_BOOK_VARIANT_TYPE);
    fresh_restored = lrg_talent_book_new_from_variant (f1, &error);
    g_assert_no_error (error);
    f2 = lrg_talent_book_to_variant (fresh_restored);
    g_assert_true (g_variant_equal (f1, f2));

    /* Dual spec, second active, some respecs */
    book = lrg_talent_book_new ("warrior", 2);
    g_assert_true (lrg_talent_book_unlock (book, NULL));
    lrg_talent_loadout_set_spec (lrg_talent_book_get_loadout (book, 0), "arms");
    spend_n (lrg_talent_book_get_loadout (book, 0), fx->arms, "a_str", CAP_LEVEL, 5);
    spend_n (lrg_talent_book_get_loadout (book, 0), fx->arms, "a_mastery", CAP_LEVEL, 2);
    lrg_talent_loadout_set_spec (lrg_talent_book_get_loadout (book, 1), "fury");
    spend_n (lrg_talent_book_get_loadout (book, 1), fx->fury, "f_rage", CAP_LEVEL, 5);
    spend_n (lrg_talent_book_get_loadout (book, 1), fx->fury, "f_frenzy", CAP_LEVEL, 1);
    g_assert_true (lrg_talent_book_set_active (book, 1, NULL));
    lrg_talent_book_set_respec_count (book, 4);

    v1 = lrg_talent_book_to_variant (book);
    g_assert_true (g_variant_is_normal_form (v1));
    restored = lrg_talent_book_new_from_variant (v1, &error);
    g_assert_no_error (error);
    g_assert_nonnull (restored);
    v2 = lrg_talent_book_to_variant (restored);
    g_assert_true (g_variant_equal (v1, v2));

    g_assert_cmpstr (lrg_talent_book_get_class_id (restored), ==, "warrior");
    g_assert_cmpuint (lrg_talent_book_get_capacity (restored), ==, 2);
    g_assert_cmpuint (lrg_talent_book_get_unlocked (restored), ==, 2);
    g_assert_cmpuint (lrg_talent_book_get_active_index (restored), ==, 1);
    g_assert_cmpuint (lrg_talent_book_get_respec_count (restored), ==, 4);
    g_assert_cmpstr (lrg_talent_loadout_get_spec (lrg_talent_book_get_active (restored)), ==, "fury");
    g_assert_cmpuint (lrg_talent_loadout_get_rank (lrg_talent_book_get_loadout (restored, 0), "arms", "a_mastery"), ==, 2);
    g_assert_true (lrg_talent_book_validate (restored, fx->trees, CAP_LEVEL, &error));
    g_assert_no_error (error);
}

/* Build a "(suuuav)" book variant with @n_loadouts empty loadouts of @loadout_class */
static GVariant *
build_book_variant (const gchar *class_id,
                    guint        unlocked,
                    guint        active,
                    guint        respec,
                    guint        n_loadouts,
                    const gchar *loadout_class)
{
    GVariantBuilder av;
    guint i;

    g_variant_builder_init (&av, G_VARIANT_TYPE ("av"));
    for (i = 0; i < n_loadouts; i++)
    {
        g_autoptr(GVariant) inner = build_loadout_variant (loadout_class, NULL, NULL, NULL, NULL, 0);
        g_variant_builder_add (&av, "v", inner);
    }

    return g_variant_ref_sink (g_variant_new ("(suuu@av)", class_id, unlocked, active,
                                              respec, g_variant_builder_end (&av)));
}

static void
assert_book_variant_rejected (GVariant *variant,
                              guint     capacity)
{
    g_autoptr(GError) error = NULL;
    LrgTalentBook *book;

    if (capacity == 0)
        book = lrg_talent_book_new_from_variant (variant, &error);
    else
        book = lrg_talent_book_new_from_variant_full (variant, capacity, &error);
    g_assert_null (book);
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
}

static void
test_book_variant_capacity (void)
{
    GError *error = NULL;

    /* Capacity derives from unlocked when larger than the default */
    {
        g_autoptr(GVariant) v = build_book_variant ("warrior", 3, 2, 0, 3, "warrior");
        g_autoptr(LrgTalentBook) book = lrg_talent_book_new_from_variant (v, &error);

        g_assert_no_error (error);
        g_assert_cmpuint (lrg_talent_book_get_capacity (book), ==, 3);
        g_assert_cmpuint (lrg_talent_book_get_active_index (book), ==, 2);
    }
    /* Default capacity when fewer are unlocked */
    {
        g_autoptr(GVariant) v = build_book_variant ("warrior", 1, 0, 9, 1, "warrior");
        g_autoptr(LrgTalentBook) book = lrg_talent_book_new_from_variant (v, &error);

        g_assert_no_error (error);
        g_assert_cmpuint (lrg_talent_book_get_capacity (book), ==, LRG_TALENT_BOOK_DEFAULT_CAPACITY);
        g_assert_cmpuint (lrg_talent_book_get_respec_count (book), ==, 9);
    }
    /* Explicit capacity: unlocked > capacity rejected, equal accepted */
    {
        g_autoptr(GVariant) v = build_book_variant ("warrior", 3, 0, 0, 3, "warrior");
        g_autoptr(LrgTalentBook) book = NULL;

        assert_book_variant_rejected (v, 2);
        book = lrg_talent_book_new_from_variant_full (v, 3, &error);
        g_assert_no_error (error);
        g_assert_cmpuint (lrg_talent_book_get_capacity (book), ==, 3);
    }
    /* Maximum capacity boundary */
    {
        g_autoptr(GVariant) v = build_book_variant ("warrior", LRG_TALENT_BOOK_MAX_CAPACITY, 7, 0,
                                                    LRG_TALENT_BOOK_MAX_CAPACITY, "warrior");
        g_autoptr(LrgTalentBook) book = lrg_talent_book_new_from_variant (v, &error);

        g_assert_no_error (error);
        g_assert_cmpuint (lrg_talent_book_get_unlocked (book), ==, LRG_TALENT_BOOK_MAX_CAPACITY);
    }
}

static void
test_book_variant_hostile (void)
{
    g_autofree gchar *id129 = g_strnfill (129, 'c');

    /* Wrong type strings */
    {
        g_autoptr(GVariant) v = g_variant_ref_sink (g_variant_new ("(suuu)", "warrior", 1, 0, 0));
        assert_book_variant_rejected (v, 0);
    }
    {
        g_autoptr(GVariant) v = build_loadout_variant ("warrior", NULL, NULL, NULL, NULL, 0);
        assert_book_variant_rejected (v, 0);
    }
    /* Empty / overlong class id */
    {
        g_autoptr(GVariant) v = build_book_variant ("", 1, 0, 0, 1, "warrior");
        assert_book_variant_rejected (v, 0);
    }
    {
        g_autoptr(GVariant) v = build_book_variant (id129, 1, 0, 0, 1, id129);
        assert_book_variant_rejected (v, 0);
    }
    /* Unlocked 0 and above the maximum capacity (oversized array) */
    {
        g_autoptr(GVariant) v = build_book_variant ("warrior", 0, 0, 0, 0, "warrior");
        assert_book_variant_rejected (v, 0);
    }
    {
        g_autoptr(GVariant) v = build_book_variant ("warrior", LRG_TALENT_BOOK_MAX_CAPACITY + 1, 0, 0,
                                                    LRG_TALENT_BOOK_MAX_CAPACITY + 1, "warrior");
        assert_book_variant_rejected (v, 0);
        assert_book_variant_rejected (v, LRG_TALENT_BOOK_MAX_CAPACITY);
    }
    {
        g_autoptr(GVariant) v = build_book_variant ("warrior", G_MAXUINT32, 0, 0, 1, "warrior");
        assert_book_variant_rejected (v, 0);
    }
    /* Active index out of range */
    {
        g_autoptr(GVariant) v = build_book_variant ("warrior", 2, 2, 0, 2, "warrior");
        assert_book_variant_rejected (v, 0);
    }
    {
        g_autoptr(GVariant) v = build_book_variant ("warrior", 1, G_MAXUINT32, 0, 1, "warrior");
        assert_book_variant_rejected (v, 0);
    }
    /* Loadout count differs from unlocked */
    {
        g_autoptr(GVariant) v = build_book_variant ("warrior", 2, 0, 0, 1, "warrior");
        assert_book_variant_rejected (v, 0);
    }
    {
        g_autoptr(GVariant) v = build_book_variant ("warrior", 1, 0, 0, 2, "warrior");
        assert_book_variant_rejected (v, 0);
    }
    /* Loadout of another class */
    {
        g_autoptr(GVariant) v = build_book_variant ("warrior", 2, 0, 0, 2, "priest");
        assert_book_variant_rejected (v, 0);
    }
    /* Inner variant of the wrong type */
    {
        GVariantBuilder av;
        g_autoptr(GVariant) v = NULL;

        g_variant_builder_init (&av, G_VARIANT_TYPE ("av"));
        g_variant_builder_add (&av, "v", g_variant_new_string ("oops"));
        v = g_variant_ref_sink (g_variant_new ("(suuu@av)", "warrior", 1, 0, 0,
                                               g_variant_builder_end (&av)));
        assert_book_variant_rejected (v, 0);
    }
    /* Hostile inner loadout (rank 0) in the second slot */
    {
        const gchar *t[] = { "arms" };
        const gchar *n[] = { "a_str" };
        const guint r[] = { 0 };
        g_autoptr(GVariant) good = build_loadout_variant ("warrior", NULL, NULL, NULL, NULL, 0);
        g_autoptr(GVariant) bad = build_loadout_variant ("warrior", NULL, t, n, r, 1);
        GVariantBuilder av;
        g_autoptr(GVariant) v = NULL;

        g_variant_builder_init (&av, G_VARIANT_TYPE ("av"));
        g_variant_builder_add (&av, "v", good);
        g_variant_builder_add (&av, "v", bad);
        v = g_variant_ref_sink (g_variant_new ("(suuu@av)", "warrior", 2, 0, 0,
                                               g_variant_builder_end (&av)));
        assert_book_variant_rejected (v, 0);
    }
    /* Duplicate rows in an inner loadout */
    {
        const gchar *t[] = { "arms", "arms" };
        const gchar *n[] = { "a_str", "a_str" };
        const guint r[] = { 1, 1 };
        g_autoptr(GVariant) bad = build_loadout_variant ("warrior", NULL, t, n, r, 2);
        GVariantBuilder av;
        g_autoptr(GVariant) v = NULL;

        g_variant_builder_init (&av, G_VARIANT_TYPE ("av"));
        g_variant_builder_add (&av, "v", bad);
        v = g_variant_ref_sink (g_variant_new ("(suuu@av)", "warrior", 1, 0, 0,
                                               g_variant_builder_end (&av)));
        assert_book_variant_rejected (v, 0);
    }
    /* Invalid UTF-8 class id in serialised data */
    {
        g_autoptr(GVariant) good = build_book_variant ("warrior", 1, 0, 0, 1, "warrior");
        g_autoptr(GVariant) bad = corrupt_first_byte (good);
        assert_book_variant_rejected (bad, 0);
    }
}

/* ========================================================================== */
/*                                   Main                                     */
/* ========================================================================== */

#define ADD_FX(path, func) \
    g_test_add ((path), TalentFixture, NULL, fixture_set_up, (func), fixture_tear_down)

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Node */
    g_test_add_func ("/talent/node/new-defaults", test_node_new_defaults);
    g_test_add_func ("/talent/node/new-rejects-zero-rank", test_node_new_rejects_zero_rank);
    g_test_add_func ("/talent/node/copy-deep", test_node_copy_deep);
    g_test_add_func ("/talent/node/get-value", test_node_get_value);

    /* Tree */
    g_test_add_func ("/talent/tree/properties", test_tree_properties);
    g_test_add_func ("/talent/tree/add-nodes-sorted", test_tree_add_nodes_sorted);
    g_test_add_func ("/talent/tree/add-node-rejections", test_tree_add_node_rejections);
    ADD_FX ("/talent/tree/validate-ok", test_tree_validate_ok);
    g_test_add_func ("/talent/tree/validate-failures", test_tree_validate_failures);

    /* Loadout */
    g_test_add_func ("/talent/loadout/properties", test_loadout_properties);
    g_test_add_func ("/talent/loadout/points-total", test_loadout_points_total);
    ADD_FX ("/talent/loadout/spend-happy", test_loadout_spend_happy);
    ADD_FX ("/talent/loadout/spend-rejections", test_loadout_spend_rejections);
    ADD_FX ("/talent/loadout/last-point", test_loadout_last_point);
    ADD_FX ("/talent/loadout/tier-gate-boundary", test_loadout_tier_gate_boundary);
    ADD_FX ("/talent/loadout/prerequisite-chain", test_loadout_prerequisite_chain);
    ADD_FX ("/talent/loadout/missing-prereq-in-tree", test_loadout_missing_prereq_in_tree);
    ADD_FX ("/talent/loadout/reset-keeps-spec", test_loadout_reset_keeps_spec);
    ADD_FX ("/talent/loadout/granted-abilities", test_loadout_granted_abilities);
    g_test_add_func ("/talent/loadout/granted-dedup", test_loadout_granted_dedup);
    ADD_FX ("/talent/loadout/sum-effect", test_loadout_sum_effect);
    ADD_FX ("/talent/loadout/validate-legit", test_loadout_validate_legit);
    ADD_FX ("/talent/loadout/validate-spec", test_loadout_validate_spec);
    ADD_FX ("/talent/loadout/validate-tampered", test_loadout_validate_tampered);
    ADD_FX ("/talent/loadout/validate-wrong-key", test_loadout_validate_wrong_key);
    ADD_FX ("/talent/loadout/variant-round-trip", test_loadout_variant_round_trip);
    g_test_add_func ("/talent/loadout/variant-hostile", test_loadout_variant_hostile);
    g_test_add_func ("/talent/loadout/variant-row-limit", test_loadout_variant_row_limit);

    /* Book */
    g_test_add_func ("/talent/book/new", test_book_new);
    g_test_add_func ("/talent/book/properties", test_book_properties);
    g_test_add_func ("/talent/book/unlock-capacity", test_book_unlock_capacity);
    ADD_FX ("/talent/book/set-active", test_book_set_active);
    ADD_FX ("/talent/book/respec-counter", test_book_respec_counter);
    g_test_add_func ("/talent/book/point-rules", test_book_point_rules);
    ADD_FX ("/talent/book/validate", test_book_validate);
    ADD_FX ("/talent/book/variant-round-trip", test_book_variant_round_trip);
    g_test_add_func ("/talent/book/variant-capacity", test_book_variant_capacity);
    g_test_add_func ("/talent/book/variant-hostile", test_book_variant_hostile);

    return g_test_run ();
}
