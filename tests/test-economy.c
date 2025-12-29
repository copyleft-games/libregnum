/* test-economy.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the economy module.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgResource       *gold;
    LrgResource       *wood;
    LrgResource       *iron;
    LrgResource       *coal;
    LrgResourcePool   *pool;
    LrgResourcePool   *pool2;
    LrgProductionRecipe *recipe;
    LrgMarket         *market;
} EconomyFixture;

static void
economy_fixture_set_up (EconomyFixture *fixture,
                        gconstpointer   user_data)
{
    (void)user_data;

    /* Create resources */
    fixture->gold = lrg_resource_new ("gold");
    lrg_resource_set_name (fixture->gold, "Gold");
    lrg_resource_set_category (fixture->gold, LRG_RESOURCE_CATEGORY_CURRENCY);
    lrg_resource_set_decimal_places (fixture->gold, 2);

    fixture->wood = lrg_resource_new ("wood");
    lrg_resource_set_name (fixture->wood, "Wood");
    lrg_resource_set_category (fixture->wood, LRG_RESOURCE_CATEGORY_MATERIAL);

    fixture->iron = lrg_resource_new ("iron");
    lrg_resource_set_name (fixture->iron, "Iron Ore");
    lrg_resource_set_category (fixture->iron, LRG_RESOURCE_CATEGORY_MATERIAL);

    fixture->coal = lrg_resource_new ("coal");
    lrg_resource_set_name (fixture->coal, "Coal");
    lrg_resource_set_category (fixture->coal, LRG_RESOURCE_CATEGORY_ENERGY);

    /* Create resource pools */
    fixture->pool = lrg_resource_pool_new ();
    fixture->pool2 = lrg_resource_pool_new ();

    /* Create a production recipe: 2 iron + 1 coal -> 1 steel */
    fixture->recipe = lrg_production_recipe_new ("smelt_steel");
    lrg_production_recipe_set_name (fixture->recipe, "Smelt Steel");
    lrg_production_recipe_set_production_time (fixture->recipe, 5.0);

    /* Create market */
    fixture->market = lrg_market_new ();
}

static void
economy_fixture_tear_down (EconomyFixture *fixture,
                           gconstpointer   user_data)
{
    (void)user_data;

    g_clear_object (&fixture->gold);
    g_clear_object (&fixture->wood);
    g_clear_object (&fixture->iron);
    g_clear_object (&fixture->coal);
    g_clear_object (&fixture->pool);
    g_clear_object (&fixture->pool2);
    g_clear_object (&fixture->recipe);
    g_clear_object (&fixture->market);
}

/* ==========================================================================
 * LrgResource Tests
 * ========================================================================== */

static void
test_resource_new (void)
{
    g_autoptr(LrgResource) res = NULL;

    res = lrg_resource_new ("test_resource");

    g_assert_nonnull (res);
    g_assert_cmpstr (lrg_resource_get_id (res), ==, "test_resource");
    g_assert_null (lrg_resource_get_name (res));
    g_assert_null (lrg_resource_get_description (res));
    g_assert_null (lrg_resource_get_icon (res));
    g_assert_cmpint (lrg_resource_get_category (res), ==, LRG_RESOURCE_CATEGORY_CUSTOM);
    g_assert_cmpfloat (lrg_resource_get_min_value (res), ==, 0.0);
    g_assert_cmpfloat (lrg_resource_get_max_value (res), ==, G_MAXDOUBLE);
    g_assert_cmpuint (lrg_resource_get_decimal_places (res), ==, 0);
    g_assert_false (lrg_resource_get_hidden (res));
}

static void
test_resource_properties (EconomyFixture *fixture,
                          gconstpointer   user_data)
{
    (void)user_data;

    /* Test gold properties */
    g_assert_cmpstr (lrg_resource_get_id (fixture->gold), ==, "gold");
    g_assert_cmpstr (lrg_resource_get_name (fixture->gold), ==, "Gold");
    g_assert_cmpint (lrg_resource_get_category (fixture->gold), ==, LRG_RESOURCE_CATEGORY_CURRENCY);
    g_assert_cmpuint (lrg_resource_get_decimal_places (fixture->gold), ==, 2);

    /* Test wood properties */
    g_assert_cmpstr (lrg_resource_get_id (fixture->wood), ==, "wood");
    g_assert_cmpstr (lrg_resource_get_name (fixture->wood), ==, "Wood");
    g_assert_cmpint (lrg_resource_get_category (fixture->wood), ==, LRG_RESOURCE_CATEGORY_MATERIAL);
}

static void
test_resource_format_value (EconomyFixture *fixture,
                            gconstpointer   user_data)
{
    g_autofree gchar *formatted1 = NULL;
    g_autofree gchar *formatted2 = NULL;
    g_autofree gchar *formatted3 = NULL;
    g_autofree gchar *formatted4 = NULL;

    (void)user_data;

    /* Currency formatting with decimals (below 1000 threshold) */
    formatted1 = lrg_resource_format_value (fixture->gold, 999.56);
    g_assert_cmpstr (formatted1, ==, "999.56");

    /* Large number formatting (should show K suffix at >= 1000) */
    formatted2 = lrg_resource_format_value (fixture->gold, 1500.0);
    g_assert_cmpstr (formatted2, ==, "1.50K");

    /* Material without decimals */
    formatted3 = lrg_resource_format_value (fixture->wood, 100.0);
    g_assert_cmpstr (formatted3, ==, "100");

    /* Very large number */
    formatted4 = lrg_resource_format_value (fixture->gold, 1500000.0);
    g_assert_cmpstr (formatted4, ==, "1.50M");
}

static void
test_resource_validate_amount (EconomyFixture *fixture,
                               gconstpointer   user_data)
{
    g_autoptr(LrgResource) bounded = NULL;

    (void)user_data;

    /* Create a resource with min/max bounds */
    bounded = lrg_resource_new ("bounded");
    lrg_resource_set_min_value (bounded, 0.0);
    lrg_resource_set_max_value (bounded, 100.0);

    /* Test validation */
    g_assert_true (lrg_resource_validate_amount (bounded, 50.0));
    g_assert_true (lrg_resource_validate_amount (bounded, 0.0));
    g_assert_true (lrg_resource_validate_amount (bounded, 100.0));
    g_assert_false (lrg_resource_validate_amount (bounded, -1.0));
    g_assert_false (lrg_resource_validate_amount (bounded, 101.0));

    /* Unbounded resource should accept any positive value */
    g_assert_true (lrg_resource_validate_amount (fixture->gold, 1000000.0));
}

static void
test_resource_category_enum (void)
{
    GType category_type;
    GEnumClass *enum_class = NULL;
    GEnumValue *value = NULL;

    /* Verify enum is registered */
    category_type = LRG_TYPE_RESOURCE_CATEGORY;
    g_assert_true (G_TYPE_IS_ENUM (category_type));

    enum_class = g_type_class_ref (category_type);
    g_assert_nonnull (enum_class);

    /* Verify values exist */
    value = g_enum_get_value (enum_class, LRG_RESOURCE_CATEGORY_CURRENCY);
    g_assert_nonnull (value);
    g_assert_cmpstr (value->value_nick, ==, "currency");

    value = g_enum_get_value (enum_class, LRG_RESOURCE_CATEGORY_MATERIAL);
    g_assert_nonnull (value);
    g_assert_cmpstr (value->value_nick, ==, "material");

    value = g_enum_get_value (enum_class, LRG_RESOURCE_CATEGORY_ENERGY);
    g_assert_nonnull (value);
    g_assert_cmpstr (value->value_nick, ==, "energy");

    g_type_class_unref (enum_class);
}

/* ==========================================================================
 * LrgResourcePool Tests
 * ========================================================================== */

static void
test_resource_pool_new (void)
{
    g_autoptr(LrgResourcePool) pool = NULL;

    pool = lrg_resource_pool_new ();

    g_assert_nonnull (pool);
    g_assert_true (lrg_resource_pool_is_empty (pool));
    g_assert_cmpuint (lrg_resource_pool_get_count (pool), ==, 0);
}

static void
test_resource_pool_add (EconomyFixture *fixture,
                        gconstpointer   user_data)
{
    gboolean result;

    (void)user_data;

    /* Add resources */
    result = lrg_resource_pool_add (fixture->pool, fixture->gold, 100.0);
    g_assert_true (result);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->gold), ==, 100.0);

    /* Add more to existing */
    result = lrg_resource_pool_add (fixture->pool, fixture->gold, 50.0);
    g_assert_true (result);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->gold), ==, 150.0);

    /* Add a different resource */
    lrg_resource_pool_add (fixture->pool, fixture->wood, 25.0);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->wood), ==, 25.0);
    g_assert_cmpuint (lrg_resource_pool_get_count (fixture->pool), ==, 2);
}

static void
test_resource_pool_remove (EconomyFixture *fixture,
                           gconstpointer   user_data)
{
    gboolean result;

    (void)user_data;

    lrg_resource_pool_add (fixture->pool, fixture->gold, 100.0);

    /* Remove some */
    result = lrg_resource_pool_remove (fixture->pool, fixture->gold, 30.0);
    g_assert_true (result);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->gold), ==, 70.0);

    /* Try to remove more than available */
    result = lrg_resource_pool_remove (fixture->pool, fixture->gold, 100.0);
    g_assert_false (result);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->gold), ==, 70.0);

    /* Remove exact amount */
    result = lrg_resource_pool_remove (fixture->pool, fixture->gold, 70.0);
    g_assert_true (result);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->gold), ==, 0.0);
}

static void
test_resource_pool_has (EconomyFixture *fixture,
                        gconstpointer   user_data)
{
    (void)user_data;

    lrg_resource_pool_add (fixture->pool, fixture->gold, 100.0);

    g_assert_true (lrg_resource_pool_has (fixture->pool, fixture->gold, 50.0));
    g_assert_true (lrg_resource_pool_has (fixture->pool, fixture->gold, 100.0));
    g_assert_false (lrg_resource_pool_has (fixture->pool, fixture->gold, 101.0));
    g_assert_false (lrg_resource_pool_has (fixture->pool, fixture->wood, 1.0));
}

static void
test_resource_pool_transfer (EconomyFixture *fixture,
                             gconstpointer   user_data)
{
    gboolean result;

    (void)user_data;

    lrg_resource_pool_add (fixture->pool, fixture->gold, 100.0);
    lrg_resource_pool_add (fixture->pool, fixture->wood, 50.0);

    /* Transfer gold */
    result = lrg_resource_pool_transfer (fixture->pool, fixture->pool2, fixture->gold, 30.0);
    g_assert_true (result);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->gold), ==, 70.0);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool2, fixture->gold), ==, 30.0);

    /* Try to transfer more than available */
    result = lrg_resource_pool_transfer (fixture->pool, fixture->pool2, fixture->gold, 100.0);
    g_assert_false (result);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->gold), ==, 70.0);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool2, fixture->gold), ==, 30.0);
}

static void
test_resource_pool_multiplier (EconomyFixture *fixture,
                               gconstpointer   user_data)
{
    (void)user_data;

    lrg_resource_pool_add (fixture->pool, fixture->gold, 100.0);

    /* Default multiplier is 1.0 */
    g_assert_cmpfloat (lrg_resource_pool_get_multiplier (fixture->pool, fixture->gold), ==, 1.0);

    /* Set a multiplier */
    lrg_resource_pool_set_multiplier (fixture->pool, fixture->gold, 1.5);
    g_assert_cmpfloat (lrg_resource_pool_get_multiplier (fixture->pool, fixture->gold), ==, 1.5);

    /* Multiplier affects additions */
    lrg_resource_pool_add (fixture->pool, fixture->gold, 100.0);
    /* 100 + (100 * 1.5) = 250 */
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->gold), ==, 250.0);
}

static void
test_resource_pool_transfer_all (EconomyFixture *fixture,
                                 gconstpointer   user_data)
{
    gdouble transferred;

    (void)user_data;

    lrg_resource_pool_add (fixture->pool, fixture->gold, 100.0);
    lrg_resource_pool_add (fixture->pool, fixture->wood, 50.0);

    /* Transfer all gold from pool to pool2 */
    transferred = lrg_resource_pool_transfer_all (fixture->pool, fixture->pool2, fixture->gold);

    g_assert_cmpfloat (transferred, ==, 100.0);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->gold), ==, 0.0);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool2, fixture->gold), ==, 100.0);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->wood), ==, 50.0);
}

static void
test_resource_pool_clear (EconomyFixture *fixture,
                          gconstpointer   user_data)
{
    (void)user_data;

    lrg_resource_pool_add (fixture->pool, fixture->gold, 100.0);
    lrg_resource_pool_add (fixture->pool, fixture->wood, 50.0);
    lrg_resource_pool_add (fixture->pool, fixture->iron, 25.0);

    g_assert_false (lrg_resource_pool_is_empty (fixture->pool));

    lrg_resource_pool_clear (fixture->pool);

    g_assert_true (lrg_resource_pool_is_empty (fixture->pool));
    g_assert_cmpuint (lrg_resource_pool_get_count (fixture->pool), ==, 0);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->gold), ==, 0.0);
}

typedef struct
{
    gboolean resource_changed_fired;
    gboolean resource_depleted_fired;
    gdouble  old_amount;
    gdouble  new_amount;
} PoolSignalData;

static void
on_resource_changed (LrgResourcePool *pool,
                     LrgResource     *resource,
                     gdouble          old_amount,
                     gdouble          new_amount,
                     gpointer         user_data)
{
    PoolSignalData *data = (PoolSignalData *)user_data;

    (void)pool;
    (void)resource;

    data->resource_changed_fired = TRUE;
    data->old_amount = old_amount;
    data->new_amount = new_amount;
}

static void
on_resource_depleted (LrgResourcePool *pool,
                      LrgResource     *resource,
                      gpointer         user_data)
{
    PoolSignalData *data = (PoolSignalData *)user_data;

    (void)pool;
    (void)resource;

    data->resource_depleted_fired = TRUE;
}

static void
test_resource_pool_signals (EconomyFixture *fixture,
                            gconstpointer   user_data)
{
    PoolSignalData signal_data = { FALSE, FALSE, 0.0, 0.0 };

    (void)user_data;

    g_signal_connect (fixture->pool, "resource-changed",
                      G_CALLBACK (on_resource_changed), &signal_data);
    g_signal_connect (fixture->pool, "resource-depleted",
                      G_CALLBACK (on_resource_depleted), &signal_data);

    /* Add fires resource-changed */
    lrg_resource_pool_add (fixture->pool, fixture->gold, 100.0);
    g_assert_true (signal_data.resource_changed_fired);
    g_assert_cmpfloat (signal_data.old_amount, ==, 0.0);
    g_assert_cmpfloat (signal_data.new_amount, ==, 100.0);

    /* Reset */
    signal_data.resource_changed_fired = FALSE;

    /* Remove fires resource-changed */
    lrg_resource_pool_remove (fixture->pool, fixture->gold, 50.0);
    g_assert_true (signal_data.resource_changed_fired);
    g_assert_cmpfloat (signal_data.old_amount, ==, 100.0);
    g_assert_cmpfloat (signal_data.new_amount, ==, 50.0);

    /* Reset */
    signal_data.resource_changed_fired = FALSE;
    signal_data.resource_depleted_fired = FALSE;

    /* Remove all fires resource-depleted */
    lrg_resource_pool_remove (fixture->pool, fixture->gold, 50.0);
    g_assert_true (signal_data.resource_changed_fired);
    g_assert_true (signal_data.resource_depleted_fired);
}

/* ==========================================================================
 * LrgProductionRecipe Tests
 * ========================================================================== */

static void
test_recipe_new (void)
{
    g_autoptr(LrgProductionRecipe) recipe = NULL;

    recipe = lrg_production_recipe_new ("test_recipe");

    g_assert_nonnull (recipe);
    g_assert_cmpstr (lrg_production_recipe_get_id (recipe), ==, "test_recipe");
    g_assert_null (lrg_production_recipe_get_name (recipe));
    g_assert_cmpfloat (lrg_production_recipe_get_production_time (recipe), ==, 1.0);
}

static void
test_recipe_inputs_outputs (EconomyFixture *fixture,
                            gconstpointer   user_data)
{
    g_autoptr(GList) inputs = NULL;
    g_autoptr(GList) outputs = NULL;
    g_autoptr(LrgResource) steel = NULL;

    (void)user_data;

    /* Create steel resource for output */
    steel = lrg_resource_new ("steel");
    lrg_resource_set_name (steel, "Steel");

    /* Add inputs */
    lrg_production_recipe_add_input (fixture->recipe, fixture->iron, 2.0);
    lrg_production_recipe_add_input (fixture->recipe, fixture->coal, 1.0);

    /* Add output */
    lrg_production_recipe_add_output (fixture->recipe, steel, 1.0, 1.0);

    /* Get inputs */
    inputs = lrg_production_recipe_get_inputs (fixture->recipe);
    g_assert_cmpuint (g_list_length (inputs), ==, 2);
    g_assert_cmpfloat (lrg_production_recipe_get_input_amount (fixture->recipe, fixture->iron), ==, 2.0);
    g_assert_cmpfloat (lrg_production_recipe_get_input_amount (fixture->recipe, fixture->coal), ==, 1.0);

    /* Get outputs */
    outputs = lrg_production_recipe_get_outputs (fixture->recipe);
    g_assert_cmpuint (g_list_length (outputs), ==, 1);
    g_assert_cmpfloat (lrg_production_recipe_get_output_amount (fixture->recipe, steel), ==, 1.0);
    g_assert_cmpfloat (lrg_production_recipe_get_output_chance (fixture->recipe, steel), ==, 1.0);
}

static void
test_recipe_can_produce (EconomyFixture *fixture,
                         gconstpointer   user_data)
{
    g_autoptr(LrgResource) steel = NULL;

    (void)user_data;

    steel = lrg_resource_new ("steel");

    /* Set up recipe: 2 iron + 1 coal -> 1 steel */
    lrg_production_recipe_add_input (fixture->recipe, fixture->iron, 2.0);
    lrg_production_recipe_add_input (fixture->recipe, fixture->coal, 1.0);
    lrg_production_recipe_add_output (fixture->recipe, steel, 1.0, 1.0);

    /* Empty pool cannot produce */
    g_assert_false (lrg_production_recipe_can_produce (fixture->recipe, fixture->pool));

    /* Not enough resources */
    lrg_resource_pool_add (fixture->pool, fixture->iron, 1.0);
    g_assert_false (lrg_production_recipe_can_produce (fixture->recipe, fixture->pool));

    /* Missing coal */
    lrg_resource_pool_add (fixture->pool, fixture->iron, 1.0);  /* Now have 2 iron */
    g_assert_false (lrg_production_recipe_can_produce (fixture->recipe, fixture->pool));

    /* Now have enough */
    lrg_resource_pool_add (fixture->pool, fixture->coal, 1.0);
    g_assert_true (lrg_production_recipe_can_produce (fixture->recipe, fixture->pool));
}

static void
test_recipe_produce (EconomyFixture *fixture,
                     gconstpointer   user_data)
{
    g_autoptr(LrgResource) steel = NULL;
    gboolean result;

    (void)user_data;

    steel = lrg_resource_new ("steel");

    /* Set up recipe */
    lrg_production_recipe_add_input (fixture->recipe, fixture->iron, 2.0);
    lrg_production_recipe_add_input (fixture->recipe, fixture->coal, 1.0);
    lrg_production_recipe_add_output (fixture->recipe, steel, 1.0, 1.0);

    /* Add resources */
    lrg_resource_pool_add (fixture->pool, fixture->iron, 5.0);
    lrg_resource_pool_add (fixture->pool, fixture->coal, 3.0);

    /* Produce (using produce_to_pool for separate source/destination) */
    result = lrg_production_recipe_produce_to_pool (fixture->recipe, fixture->pool, fixture->pool2);
    g_assert_true (result);

    /* Check inputs consumed */
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->iron), ==, 3.0);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->coal), ==, 2.0);

    /* Check output produced */
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool2, steel), ==, 1.0);
}

static void
test_recipe_produce_insufficient (EconomyFixture *fixture,
                                  gconstpointer   user_data)
{
    g_autoptr(LrgResource) steel = NULL;
    gboolean result;

    (void)user_data;

    steel = lrg_resource_new ("steel");

    /* Set up recipe */
    lrg_production_recipe_add_input (fixture->recipe, fixture->iron, 2.0);
    lrg_production_recipe_add_input (fixture->recipe, fixture->coal, 1.0);
    lrg_production_recipe_add_output (fixture->recipe, steel, 1.0, 1.0);

    /* Not enough resources */
    lrg_resource_pool_add (fixture->pool, fixture->iron, 1.0);
    lrg_resource_pool_add (fixture->pool, fixture->coal, 1.0);

    /* Production should fail */
    result = lrg_production_recipe_produce_to_pool (fixture->recipe, fixture->pool, fixture->pool2);
    g_assert_false (result);

    /* Resources unchanged */
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->iron), ==, 1.0);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->coal), ==, 1.0);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool2, steel), ==, 0.0);
}

static void
test_recipe_output_chance (EconomyFixture *fixture,
                           gconstpointer   user_data)
{
    g_autoptr(LrgResource) gem = NULL;

    (void)user_data;

    gem = lrg_resource_new ("gem");

    /* Add output with 50% chance */
    lrg_production_recipe_add_output (fixture->recipe, gem, 1.0, 0.5);

    g_assert_cmpfloat (lrg_production_recipe_get_output_chance (fixture->recipe, gem), ==, 0.5);

    /* Use produce_guaranteed for deterministic testing (single pool for I/O) */
    lrg_production_recipe_produce_guaranteed (fixture->recipe, fixture->pool);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, gem), ==, 1.0);
}

/* ==========================================================================
 * LrgMarket Tests
 * ========================================================================== */

static void
test_market_new (void)
{
    g_autoptr(LrgMarket) market = NULL;

    market = lrg_market_new ();

    g_assert_nonnull (market);
    g_assert_cmpfloat (lrg_market_get_volatility (market), ==, 0.1);
    g_assert_cmpfloat (lrg_market_get_buy_markup (market), ==, 1.0);
    g_assert_cmpfloat (lrg_market_get_sell_markdown (market), ==, 1.0);
}

static void
test_market_register_resource (EconomyFixture *fixture,
                               gconstpointer   user_data)
{
    g_autoptr(GList) resources = NULL;

    (void)user_data;

    g_assert_false (lrg_market_is_registered (fixture->market, fixture->gold));

    /* Register */
    lrg_market_register_resource (fixture->market, fixture->gold, 10.0, 1.0, 100.0);

    g_assert_true (lrg_market_is_registered (fixture->market, fixture->gold));
    g_assert_cmpfloat (lrg_market_get_base_price (fixture->market, fixture->gold), ==, 10.0);
    g_assert_cmpfloat (lrg_market_get_price (fixture->market, fixture->gold), ==, 10.0);

    /* Get registered resources */
    resources = lrg_market_get_resources (fixture->market);
    g_assert_cmpuint (g_list_length (resources), ==, 1);
}

static void
test_market_prices (EconomyFixture *fixture,
                    gconstpointer   user_data)
{
    (void)user_data;

    lrg_market_register_resource (fixture->market, fixture->gold, 10.0, 5.0, 20.0);

    /* Base price */
    g_assert_cmpfloat (lrg_market_get_price (fixture->market, fixture->gold), ==, 10.0);

    /* Change base price */
    lrg_market_set_base_price (fixture->market, fixture->gold, 15.0);
    g_assert_cmpfloat (lrg_market_get_base_price (fixture->market, fixture->gold), ==, 15.0);
}

static void
test_market_buy_sell_markup (EconomyFixture *fixture,
                             gconstpointer   user_data)
{
    (void)user_data;

    lrg_market_register_resource (fixture->market, fixture->gold, 10.0, 1.0, 100.0);

    /* No markup by default */
    g_assert_cmpfloat (lrg_market_get_buy_price (fixture->market, fixture->gold), ==, 10.0);
    g_assert_cmpfloat (lrg_market_get_sell_price (fixture->market, fixture->gold), ==, 10.0);

    /* Set markup and markdown */
    lrg_market_set_buy_markup (fixture->market, 1.1);    /* 10% markup */
    lrg_market_set_sell_markdown (fixture->market, 0.9);  /* 10% markdown */

    g_assert_cmpfloat_with_epsilon (lrg_market_get_buy_price (fixture->market, fixture->gold), 11.0, 0.01);
    g_assert_cmpfloat_with_epsilon (lrg_market_get_sell_price (fixture->market, fixture->gold), 9.0, 0.01);
}

static void
test_market_supply_demand (EconomyFixture *fixture,
                           gconstpointer   user_data)
{
    (void)user_data;

    lrg_market_register_resource (fixture->market, fixture->gold, 10.0, 1.0, 100.0);

    g_assert_cmpfloat (lrg_market_get_supply (fixture->market, fixture->gold), ==, 0.0);
    g_assert_cmpfloat (lrg_market_get_demand (fixture->market, fixture->gold), ==, 0.0);

    /* Add supply */
    lrg_market_add_supply (fixture->market, fixture->gold, 50.0);
    g_assert_cmpfloat (lrg_market_get_supply (fixture->market, fixture->gold), ==, 50.0);

    /* Add demand */
    lrg_market_add_demand (fixture->market, fixture->gold, 30.0);
    g_assert_cmpfloat (lrg_market_get_demand (fixture->market, fixture->gold), ==, 30.0);

    /* Clear supply/demand */
    lrg_market_clear_supply_demand (fixture->market);
    g_assert_cmpfloat (lrg_market_get_supply (fixture->market, fixture->gold), ==, 0.0);
    g_assert_cmpfloat (lrg_market_get_demand (fixture->market, fixture->gold), ==, 0.0);
}

static void
test_market_buy (EconomyFixture *fixture,
                 gconstpointer   user_data)
{
    g_autoptr(LrgResource) currency = NULL;
    gboolean result;

    (void)user_data;

    currency = lrg_resource_new ("coins");

    lrg_market_register_resource (fixture->market, fixture->wood, 5.0, 1.0, 50.0);

    /* Buyer has 100 coins */
    lrg_resource_pool_add (fixture->pool, currency, 100.0);

    /* Buy 10 wood at 5 coins each = 50 coins */
    result = lrg_market_buy (fixture->market, fixture->wood, 10.0, currency, fixture->pool);
    g_assert_true (result);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, currency), ==, 50.0);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->wood), ==, 10.0);

    /* Demand should increase */
    g_assert_cmpfloat (lrg_market_get_demand (fixture->market, fixture->wood), >, 0.0);
}

static void
test_market_sell (EconomyFixture *fixture,
                  gconstpointer   user_data)
{
    g_autoptr(LrgResource) currency = NULL;
    gboolean result;

    (void)user_data;

    currency = lrg_resource_new ("coins");

    lrg_market_register_resource (fixture->market, fixture->wood, 5.0, 1.0, 50.0);

    /* Seller has 20 wood */
    lrg_resource_pool_add (fixture->pool, fixture->wood, 20.0);

    /* Sell 10 wood at 5 coins each = 50 coins */
    result = lrg_market_sell (fixture->market, fixture->wood, 10.0, currency, fixture->pool);
    g_assert_true (result);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, fixture->wood), ==, 10.0);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool, currency), ==, 50.0);

    /* Supply should increase */
    g_assert_cmpfloat (lrg_market_get_supply (fixture->market, fixture->wood), >, 0.0);
}

static void
test_market_update (EconomyFixture *fixture,
                    gconstpointer   user_data)
{
    gdouble initial_price;
    gdouble new_price;

    (void)user_data;

    lrg_market_register_resource (fixture->market, fixture->gold, 10.0, 5.0, 20.0);
    initial_price = lrg_market_get_price (fixture->market, fixture->gold);

    /* Add high demand, low supply */
    lrg_market_add_demand (fixture->market, fixture->gold, 100.0);
    lrg_market_add_supply (fixture->market, fixture->gold, 10.0);

    /* Update market (simulate 1 second) */
    lrg_market_update (fixture->market, 1.0);

    /* Price should increase due to high demand */
    new_price = lrg_market_get_price (fixture->market, fixture->gold);
    g_assert_cmpfloat (new_price, >, initial_price);
}

static void
test_market_reset_prices (EconomyFixture *fixture,
                          gconstpointer   user_data)
{
    (void)user_data;

    lrg_market_register_resource (fixture->market, fixture->gold, 10.0, 5.0, 20.0);

    /* Modify price through supply/demand */
    lrg_market_add_demand (fixture->market, fixture->gold, 100.0);
    lrg_market_update (fixture->market, 1.0);

    /* Price changed */
    g_assert_cmpfloat (lrg_market_get_price (fixture->market, fixture->gold), !=, 10.0);

    /* Reset prices */
    lrg_market_reset_prices (fixture->market);
    g_assert_cmpfloat (lrg_market_get_price (fixture->market, fixture->gold), ==, 10.0);
}

/* ==========================================================================
 * LrgEconomyManager Tests
 * ========================================================================== */

static void
test_economy_manager_singleton (void)
{
    LrgEconomyManager *manager1;
    LrgEconomyManager *manager2;

    manager1 = lrg_economy_manager_get_default ();
    manager2 = lrg_economy_manager_get_default ();

    g_assert_nonnull (manager1);
    g_assert_true (manager1 == manager2);

    /* Clean up by clearing the manager */
    lrg_economy_manager_clear (manager1);
}

static void
test_economy_manager_register_resource (EconomyFixture *fixture,
                                        gconstpointer   user_data)
{
    LrgEconomyManager *manager;
    LrgResource *found;
    g_autoptr(GList) resources = NULL;

    (void)user_data;

    manager = lrg_economy_manager_get_default ();

    /* Register resources */
    lrg_economy_manager_register_resource (manager, fixture->gold);
    lrg_economy_manager_register_resource (manager, fixture->wood);

    /* Look up by ID */
    found = lrg_economy_manager_get_resource (manager, "gold");
    g_assert_nonnull (found);
    g_assert_true (found == fixture->gold);

    found = lrg_economy_manager_get_resource (manager, "wood");
    g_assert_nonnull (found);
    g_assert_true (found == fixture->wood);

    /* Not found */
    found = lrg_economy_manager_get_resource (manager, "nonexistent");
    g_assert_null (found);

    /* Get all resources */
    resources = lrg_economy_manager_get_resources (manager);
    g_assert_cmpuint (g_list_length (resources), ==, 2);

    /* Unregister */
    g_assert_true (lrg_economy_manager_unregister_resource (manager, "gold"));
    g_assert_null (lrg_economy_manager_get_resource (manager, "gold"));

    lrg_economy_manager_clear (manager);
}

static void
test_economy_manager_resources_by_category (EconomyFixture *fixture,
                                            gconstpointer   user_data)
{
    LrgEconomyManager *manager;
    g_autoptr(GList) currencies = NULL;
    g_autoptr(GList) materials = NULL;

    (void)user_data;

    manager = lrg_economy_manager_get_default ();

    lrg_economy_manager_register_resource (manager, fixture->gold);
    lrg_economy_manager_register_resource (manager, fixture->wood);
    lrg_economy_manager_register_resource (manager, fixture->iron);
    lrg_economy_manager_register_resource (manager, fixture->coal);

    /* Filter by category */
    currencies = lrg_economy_manager_get_resources_by_category (manager, LRG_RESOURCE_CATEGORY_CURRENCY);
    g_assert_cmpuint (g_list_length (currencies), ==, 1);

    materials = lrg_economy_manager_get_resources_by_category (manager, LRG_RESOURCE_CATEGORY_MATERIAL);
    g_assert_cmpuint (g_list_length (materials), ==, 2);

    lrg_economy_manager_clear (manager);
}

static void
test_economy_manager_register_recipe (EconomyFixture *fixture,
                                      gconstpointer   user_data)
{
    LrgEconomyManager *manager;
    LrgProductionRecipe *found;
    g_autoptr(GList) recipes = NULL;

    (void)user_data;

    manager = lrg_economy_manager_get_default ();

    /* Register recipe */
    lrg_economy_manager_register_recipe (manager, fixture->recipe);

    /* Look up by ID */
    found = lrg_economy_manager_get_recipe (manager, "smelt_steel");
    g_assert_nonnull (found);
    g_assert_true (found == fixture->recipe);

    /* Get all recipes */
    recipes = lrg_economy_manager_get_recipes (manager);
    g_assert_cmpuint (g_list_length (recipes), ==, 1);

    /* Unregister */
    g_assert_true (lrg_economy_manager_unregister_recipe (manager, "smelt_steel"));
    g_assert_null (lrg_economy_manager_get_recipe (manager, "smelt_steel"));

    lrg_economy_manager_clear (manager);
}

static void
test_economy_manager_market (void)
{
    LrgEconomyManager *manager;
    LrgMarket *market;
    g_autoptr(LrgMarket) new_market = NULL;

    manager = lrg_economy_manager_get_default ();

    /* Default market exists */
    market = lrg_economy_manager_get_market (manager);
    g_assert_nonnull (market);

    /* Set custom market */
    new_market = lrg_market_new ();
    lrg_market_set_volatility (new_market, 0.5);

    lrg_economy_manager_set_market (manager, new_market);

    market = lrg_economy_manager_get_market (manager);
    g_assert_cmpfloat (lrg_market_get_volatility (market), ==, 0.5);

    lrg_economy_manager_clear (manager);
}

/* ==========================================================================
 * LrgOfflineCalculator Tests
 * ========================================================================== */

static void
test_offline_calculator_new (void)
{
    g_autoptr(LrgOfflineCalculator) calc = NULL;

    calc = lrg_offline_calculator_new ();

    g_assert_nonnull (calc);
    g_assert_cmpuint (lrg_offline_calculator_get_producer_count (calc), ==, 0);
    g_assert_true (lrg_offline_calculator_get_snapshot_time (calc) == 0);
    g_assert_cmpfloat (lrg_offline_calculator_get_efficiency (calc), ==, 1.0);
    g_assert_cmpfloat (lrg_offline_calculator_get_max_hours (calc), ==, 24.0);
    g_assert_cmpfloat (lrg_offline_calculator_get_min_seconds (calc), ==, 60.0);
}

static void
test_offline_calculator_snapshot (void)
{
    g_autoptr(LrgOfflineCalculator) calc = NULL;
    gint64 before_time;
    gint64 snapshot_time;
    g_autoptr(GDateTime) now = NULL;

    calc = lrg_offline_calculator_new ();

    /* Get current time for comparison */
    now = g_date_time_new_now_utc ();
    before_time = g_date_time_to_unix (now);

    /* Take snapshot */
    lrg_offline_calculator_take_snapshot (calc);
    snapshot_time = lrg_offline_calculator_get_snapshot_time (calc);

    g_assert_true (snapshot_time >= before_time);

    /* Manual set */
    lrg_offline_calculator_set_snapshot_time (calc, 1000);
    g_assert_true (lrg_offline_calculator_get_snapshot_time (calc) == 1000);
}

static void
test_offline_calculator_settings (void)
{
    g_autoptr(LrgOfflineCalculator) calc = NULL;

    calc = lrg_offline_calculator_new ();

    /* Efficiency */
    lrg_offline_calculator_set_efficiency (calc, 0.5);
    g_assert_cmpfloat (lrg_offline_calculator_get_efficiency (calc), ==, 0.5);

    /* Clamps to valid range */
    lrg_offline_calculator_set_efficiency (calc, 2.0);
    g_assert_cmpfloat (lrg_offline_calculator_get_efficiency (calc), ==, 1.0);

    lrg_offline_calculator_set_efficiency (calc, -0.5);
    g_assert_cmpfloat (lrg_offline_calculator_get_efficiency (calc), ==, 0.0);

    /* Max hours */
    lrg_offline_calculator_set_max_hours (calc, 48.0);
    g_assert_cmpfloat (lrg_offline_calculator_get_max_hours (calc), ==, 48.0);

    /* Min seconds */
    lrg_offline_calculator_set_min_seconds (calc, 120.0);
    g_assert_cmpfloat (lrg_offline_calculator_get_min_seconds (calc), ==, 120.0);
}

static void
test_offline_calculator_calculate_duration (EconomyFixture *fixture,
                                            gconstpointer   user_data)
{
    g_autoptr(LrgOfflineCalculator) calc = NULL;
    g_autoptr(LrgProducer) producer = NULL;
    g_autoptr(LrgResource) output = NULL;

    (void)user_data;

    calc = lrg_offline_calculator_new ();

    /* Create output resource */
    output = lrg_resource_new ("product");

    /* Set up recipe: no inputs, produces 1 product per 10 seconds */
    lrg_production_recipe_set_production_time (fixture->recipe, 10.0);
    lrg_production_recipe_add_output (fixture->recipe, output, 1.0, 1.0);

    /* Create producer */
    producer = lrg_producer_new ();
    lrg_producer_set_recipe (producer, fixture->recipe);
    lrg_producer_set_resource_pool (producer, fixture->pool);

    /* Add producer to calculator */
    lrg_offline_calculator_add_producer (calc, producer);
    g_assert_cmpuint (lrg_offline_calculator_get_producer_count (calc), ==, 1);

    /* Calculate for 100 seconds = 10 cycles */
    lrg_offline_calculator_calculate_duration (calc, 100.0, fixture->pool2);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool2, output), ==, 10.0);
}

static void
test_offline_calculator_efficiency (EconomyFixture *fixture,
                                    gconstpointer   user_data)
{
    g_autoptr(LrgOfflineCalculator) calc = NULL;
    g_autoptr(LrgProducer) producer = NULL;
    g_autoptr(LrgResource) output = NULL;

    (void)user_data;

    calc = lrg_offline_calculator_new ();

    /* Create output resource */
    output = lrg_resource_new ("product");

    /* Set up recipe: produces 1 product per 10 seconds */
    lrg_production_recipe_set_production_time (fixture->recipe, 10.0);
    lrg_production_recipe_add_output (fixture->recipe, output, 1.0, 1.0);

    /* Create producer */
    producer = lrg_producer_new ();
    lrg_producer_set_recipe (producer, fixture->recipe);
    lrg_producer_set_resource_pool (producer, fixture->pool);

    lrg_offline_calculator_add_producer (calc, producer);

    /* Set 50% efficiency */
    lrg_offline_calculator_set_efficiency (calc, 0.5);

    /* Calculate for 100 seconds at 50% = 5 cycles */
    lrg_offline_calculator_calculate_duration (calc, 100.0, fixture->pool2);
    g_assert_cmpfloat (lrg_resource_pool_get (fixture->pool2, output), ==, 5.0);
}

static void
test_offline_calculator_producers (void)
{
    g_autoptr(LrgOfflineCalculator) calc = NULL;
    g_autoptr(LrgProducer) producer1 = NULL;
    g_autoptr(LrgProducer) producer2 = NULL;

    calc = lrg_offline_calculator_new ();
    producer1 = lrg_producer_new ();
    producer2 = lrg_producer_new ();

    /* Add producers */
    lrg_offline_calculator_add_producer (calc, producer1);
    g_assert_cmpuint (lrg_offline_calculator_get_producer_count (calc), ==, 1);

    lrg_offline_calculator_add_producer (calc, producer2);
    g_assert_cmpuint (lrg_offline_calculator_get_producer_count (calc), ==, 2);

    /* Adding same producer again is ignored */
    lrg_offline_calculator_add_producer (calc, producer1);
    g_assert_cmpuint (lrg_offline_calculator_get_producer_count (calc), ==, 2);

    /* Remove producer */
    g_assert_true (lrg_offline_calculator_remove_producer (calc, producer1));
    g_assert_cmpuint (lrg_offline_calculator_get_producer_count (calc), ==, 1);

    g_assert_false (lrg_offline_calculator_remove_producer (calc, producer1));

    /* Clear all */
    lrg_offline_calculator_clear_producers (calc);
    g_assert_cmpuint (lrg_offline_calculator_get_producer_count (calc), ==, 0);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgResource tests */
    g_test_add_func ("/economy/resource/new", test_resource_new);
    g_test_add ("/economy/resource/properties", EconomyFixture, NULL,
                economy_fixture_set_up, test_resource_properties, economy_fixture_tear_down);
    g_test_add ("/economy/resource/format-value", EconomyFixture, NULL,
                economy_fixture_set_up, test_resource_format_value, economy_fixture_tear_down);
    g_test_add ("/economy/resource/validate-amount", EconomyFixture, NULL,
                economy_fixture_set_up, test_resource_validate_amount, economy_fixture_tear_down);
    g_test_add_func ("/economy/resource/category-enum", test_resource_category_enum);

    /* LrgResourcePool tests */
    g_test_add_func ("/economy/resource-pool/new", test_resource_pool_new);
    g_test_add ("/economy/resource-pool/add", EconomyFixture, NULL,
                economy_fixture_set_up, test_resource_pool_add, economy_fixture_tear_down);
    g_test_add ("/economy/resource-pool/remove", EconomyFixture, NULL,
                economy_fixture_set_up, test_resource_pool_remove, economy_fixture_tear_down);
    g_test_add ("/economy/resource-pool/has", EconomyFixture, NULL,
                economy_fixture_set_up, test_resource_pool_has, economy_fixture_tear_down);
    g_test_add ("/economy/resource-pool/transfer", EconomyFixture, NULL,
                economy_fixture_set_up, test_resource_pool_transfer, economy_fixture_tear_down);
    g_test_add ("/economy/resource-pool/multiplier", EconomyFixture, NULL,
                economy_fixture_set_up, test_resource_pool_multiplier, economy_fixture_tear_down);
    g_test_add ("/economy/resource-pool/transfer-all", EconomyFixture, NULL,
                economy_fixture_set_up, test_resource_pool_transfer_all, economy_fixture_tear_down);
    g_test_add ("/economy/resource-pool/clear", EconomyFixture, NULL,
                economy_fixture_set_up, test_resource_pool_clear, economy_fixture_tear_down);
    g_test_add ("/economy/resource-pool/signals", EconomyFixture, NULL,
                economy_fixture_set_up, test_resource_pool_signals, economy_fixture_tear_down);

    /* LrgProductionRecipe tests */
    g_test_add_func ("/economy/recipe/new", test_recipe_new);
    g_test_add ("/economy/recipe/inputs-outputs", EconomyFixture, NULL,
                economy_fixture_set_up, test_recipe_inputs_outputs, economy_fixture_tear_down);
    g_test_add ("/economy/recipe/can-produce", EconomyFixture, NULL,
                economy_fixture_set_up, test_recipe_can_produce, economy_fixture_tear_down);
    g_test_add ("/economy/recipe/produce", EconomyFixture, NULL,
                economy_fixture_set_up, test_recipe_produce, economy_fixture_tear_down);
    g_test_add ("/economy/recipe/produce-insufficient", EconomyFixture, NULL,
                economy_fixture_set_up, test_recipe_produce_insufficient, economy_fixture_tear_down);
    g_test_add ("/economy/recipe/output-chance", EconomyFixture, NULL,
                economy_fixture_set_up, test_recipe_output_chance, economy_fixture_tear_down);

    /* LrgMarket tests */
    g_test_add_func ("/economy/market/new", test_market_new);
    g_test_add ("/economy/market/register-resource", EconomyFixture, NULL,
                economy_fixture_set_up, test_market_register_resource, economy_fixture_tear_down);
    g_test_add ("/economy/market/prices", EconomyFixture, NULL,
                economy_fixture_set_up, test_market_prices, economy_fixture_tear_down);
    g_test_add ("/economy/market/buy-sell-markup", EconomyFixture, NULL,
                economy_fixture_set_up, test_market_buy_sell_markup, economy_fixture_tear_down);
    g_test_add ("/economy/market/supply-demand", EconomyFixture, NULL,
                economy_fixture_set_up, test_market_supply_demand, economy_fixture_tear_down);
    g_test_add ("/economy/market/buy", EconomyFixture, NULL,
                economy_fixture_set_up, test_market_buy, economy_fixture_tear_down);
    g_test_add ("/economy/market/sell", EconomyFixture, NULL,
                economy_fixture_set_up, test_market_sell, economy_fixture_tear_down);
    g_test_add ("/economy/market/update", EconomyFixture, NULL,
                economy_fixture_set_up, test_market_update, economy_fixture_tear_down);
    g_test_add ("/economy/market/reset-prices", EconomyFixture, NULL,
                economy_fixture_set_up, test_market_reset_prices, economy_fixture_tear_down);

    /* LrgEconomyManager tests */
    g_test_add_func ("/economy/manager/singleton", test_economy_manager_singleton);
    g_test_add ("/economy/manager/register-resource", EconomyFixture, NULL,
                economy_fixture_set_up, test_economy_manager_register_resource, economy_fixture_tear_down);
    g_test_add ("/economy/manager/resources-by-category", EconomyFixture, NULL,
                economy_fixture_set_up, test_economy_manager_resources_by_category, economy_fixture_tear_down);
    g_test_add ("/economy/manager/register-recipe", EconomyFixture, NULL,
                economy_fixture_set_up, test_economy_manager_register_recipe, economy_fixture_tear_down);
    g_test_add_func ("/economy/manager/market", test_economy_manager_market);

    /* LrgOfflineCalculator tests */
    g_test_add_func ("/economy/offline-calculator/new", test_offline_calculator_new);
    g_test_add_func ("/economy/offline-calculator/snapshot", test_offline_calculator_snapshot);
    g_test_add_func ("/economy/offline-calculator/settings", test_offline_calculator_settings);
    g_test_add ("/economy/offline-calculator/calculate-duration", EconomyFixture, NULL,
                economy_fixture_set_up, test_offline_calculator_calculate_duration, economy_fixture_tear_down);
    g_test_add ("/economy/offline-calculator/efficiency", EconomyFixture, NULL,
                economy_fixture_set_up, test_offline_calculator_efficiency, economy_fixture_tear_down);
    g_test_add_func ("/economy/offline-calculator/producers", test_offline_calculator_producers);

    return g_test_run ();
}
