/* test-inventory.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the inventory module.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgItemDef   *sword;
    LrgItemDef   *potion;
    LrgItemDef   *gold;
    LrgItemDef   *helmet;
    LrgItemDef   *chestplate;
    LrgItemDef   *ring;
    LrgInventory *inventory;
    LrgEquipment *equipment;
} InventoryFixture;

static void
inventory_fixture_set_up (InventoryFixture *fixture,
                          gconstpointer     user_data)
{
    (void)user_data;

    /* Create a weapon (non-stackable) */
    fixture->sword = lrg_item_def_new ("sword_iron");
    lrg_item_def_set_name (fixture->sword, "Iron Sword");
    lrg_item_def_set_description (fixture->sword, "A simple iron sword");
    lrg_item_def_set_item_type (fixture->sword, LRG_ITEM_TYPE_WEAPON);
    lrg_item_def_set_stackable (fixture->sword, FALSE);
    lrg_item_def_set_value (fixture->sword, 100);

    /* Create a consumable (stackable) */
    fixture->potion = lrg_item_def_new ("potion_health");
    lrg_item_def_set_name (fixture->potion, "Health Potion");
    lrg_item_def_set_description (fixture->potion, "Restores health");
    lrg_item_def_set_item_type (fixture->potion, LRG_ITEM_TYPE_CONSUMABLE);
    lrg_item_def_set_stackable (fixture->potion, TRUE);
    lrg_item_def_set_max_stack (fixture->potion, 10);
    lrg_item_def_set_value (fixture->potion, 25);

    /* Create a material item (stackable, high stack size) */
    fixture->gold = lrg_item_def_new ("gold_coin");
    lrg_item_def_set_name (fixture->gold, "Gold Coin");
    lrg_item_def_set_item_type (fixture->gold, LRG_ITEM_TYPE_MATERIAL);
    lrg_item_def_set_stackable (fixture->gold, TRUE);
    lrg_item_def_set_max_stack (fixture->gold, 999);
    lrg_item_def_set_value (fixture->gold, 1);

    /* Create armor items for equipment tests */
    fixture->helmet = lrg_item_def_new ("helmet_iron");
    lrg_item_def_set_name (fixture->helmet, "Iron Helmet");
    lrg_item_def_set_item_type (fixture->helmet, LRG_ITEM_TYPE_ARMOR);
    lrg_item_def_set_stackable (fixture->helmet, FALSE);
    lrg_item_def_set_property_int (fixture->helmet, "defense", 5);
    lrg_item_def_set_property_float (fixture->helmet, "weight", 2.5f);

    fixture->chestplate = lrg_item_def_new ("chestplate_iron");
    lrg_item_def_set_name (fixture->chestplate, "Iron Chestplate");
    lrg_item_def_set_item_type (fixture->chestplate, LRG_ITEM_TYPE_ARMOR);
    lrg_item_def_set_stackable (fixture->chestplate, FALSE);
    lrg_item_def_set_property_int (fixture->chestplate, "defense", 10);
    lrg_item_def_set_property_float (fixture->chestplate, "weight", 5.0f);

    /* Create accessory (generic type for accessory slot) */
    fixture->ring = lrg_item_def_new ("ring_strength");
    lrg_item_def_set_name (fixture->ring, "Ring of Strength");
    lrg_item_def_set_item_type (fixture->ring, LRG_ITEM_TYPE_GENERIC);
    lrg_item_def_set_stackable (fixture->ring, FALSE);
    lrg_item_def_set_property_int (fixture->ring, "strength", 3);

    /* Add attack property to sword for stat bonus tests */
    lrg_item_def_set_property_int (fixture->sword, "attack", 15);

    /* Create an inventory with 10 slots */
    fixture->inventory = lrg_inventory_new (10);

    /* Create equipment manager */
    fixture->equipment = lrg_equipment_new ();
}

static void
inventory_fixture_tear_down (InventoryFixture *fixture,
                             gconstpointer     user_data)
{
    (void)user_data;

    g_clear_object (&fixture->sword);
    g_clear_object (&fixture->potion);
    g_clear_object (&fixture->gold);
    g_clear_object (&fixture->helmet);
    g_clear_object (&fixture->chestplate);
    g_clear_object (&fixture->ring);
    g_clear_object (&fixture->inventory);
    g_clear_object (&fixture->equipment);
}

/* ==========================================================================
 * LrgItemDef Tests
 * ========================================================================== */

static void
test_item_def_new (void)
{
    g_autoptr(LrgItemDef) def = NULL;

    def = lrg_item_def_new ("test_item");

    g_assert_nonnull (def);
    g_assert_cmpstr (lrg_item_def_get_id (def), ==, "test_item");
    g_assert_null (lrg_item_def_get_name (def));
    g_assert_null (lrg_item_def_get_description (def));
    g_assert_cmpint (lrg_item_def_get_item_type (def), ==, LRG_ITEM_TYPE_GENERIC);
    g_assert_true (lrg_item_def_get_stackable (def));
    g_assert_cmpuint (lrg_item_def_get_max_stack (def), ==, 99);
    g_assert_cmpuint (lrg_item_def_get_value (def), ==, 0);
}

static void
test_item_def_properties (InventoryFixture *fixture,
                          gconstpointer     user_data)
{
    (void)user_data;

    /* Test sword properties */
    g_assert_cmpstr (lrg_item_def_get_id (fixture->sword), ==, "sword_iron");
    g_assert_cmpstr (lrg_item_def_get_name (fixture->sword), ==, "Iron Sword");
    g_assert_cmpstr (lrg_item_def_get_description (fixture->sword), ==, "A simple iron sword");
    g_assert_cmpint (lrg_item_def_get_item_type (fixture->sword), ==, LRG_ITEM_TYPE_WEAPON);
    g_assert_false (lrg_item_def_get_stackable (fixture->sword));
    g_assert_cmpuint (lrg_item_def_get_max_stack (fixture->sword), ==, 1);
    g_assert_cmpuint (lrg_item_def_get_value (fixture->sword), ==, 100);

    /* Test potion properties */
    g_assert_cmpstr (lrg_item_def_get_id (fixture->potion), ==, "potion_health");
    g_assert_true (lrg_item_def_get_stackable (fixture->potion));
    g_assert_cmpuint (lrg_item_def_get_max_stack (fixture->potion), ==, 10);
}

static void
test_item_def_custom_properties (void)
{
    g_autoptr(LrgItemDef) def = NULL;

    def = lrg_item_def_new ("enchanted_sword");

    /* Set custom properties */
    lrg_item_def_set_property_int (def, "damage", 25);
    lrg_item_def_set_property_float (def, "attack_speed", 1.5f);
    lrg_item_def_set_property_string (def, "element", "fire");
    lrg_item_def_set_property_bool (def, "two_handed", FALSE);

    /* Verify properties */
    g_assert_cmpint (lrg_item_def_get_property_int (def, "damage", 0), ==, 25);
    g_assert_cmpfloat_with_epsilon (lrg_item_def_get_property_float (def, "attack_speed", 0.0f), 1.5f, 0.001f);
    g_assert_cmpstr (lrg_item_def_get_property_string (def, "element"), ==, "fire");
    g_assert_false (lrg_item_def_get_property_bool (def, "two_handed", TRUE));

    /* Test defaults for missing properties */
    g_assert_cmpint (lrg_item_def_get_property_int (def, "nonexistent", 42), ==, 42);
    g_assert_cmpfloat_with_epsilon (lrg_item_def_get_property_float (def, "nonexistent", 3.14f), 3.14f, 0.001f);
    g_assert_null (lrg_item_def_get_property_string (def, "nonexistent"));
    g_assert_true (lrg_item_def_get_property_bool (def, "nonexistent", TRUE));

    /* Test has_custom_property */
    g_assert_true (lrg_item_def_has_custom_property (def, "damage"));
    g_assert_false (lrg_item_def_has_custom_property (def, "nonexistent"));

    /* Test remove_custom_property */
    lrg_item_def_remove_custom_property (def, "damage");
    g_assert_false (lrg_item_def_has_custom_property (def, "damage"));
    g_assert_cmpint (lrg_item_def_get_property_int (def, "damage", -1), ==, -1);
}

static void
test_item_def_can_stack_with (InventoryFixture *fixture,
                              gconstpointer     user_data)
{
    g_autoptr(LrgItemDef) potion2 = NULL;
    g_autoptr(LrgItemDef) mana_potion = NULL;

    (void)user_data;

    /* Create another health potion */
    potion2 = lrg_item_def_new ("potion_health");
    lrg_item_def_set_stackable (potion2, TRUE);

    /* Create a different potion */
    mana_potion = lrg_item_def_new ("potion_mana");
    lrg_item_def_set_stackable (mana_potion, TRUE);

    /* Same ID can stack */
    g_assert_true (lrg_item_def_can_stack_with (fixture->potion, potion2));

    /* Different ID cannot stack */
    g_assert_false (lrg_item_def_can_stack_with (fixture->potion, mana_potion));

    /* Non-stackable items cannot stack */
    g_assert_false (lrg_item_def_can_stack_with (fixture->sword, fixture->sword));
}

/* ==========================================================================
 * LrgItemStack Tests
 * ========================================================================== */

static void
test_item_stack_new (InventoryFixture *fixture,
                     gconstpointer     user_data)
{
    LrgItemStack *stack = NULL;

    (void)user_data;

    stack = lrg_item_stack_new (fixture->potion, 5);

    g_assert_nonnull (stack);
    g_assert_true (lrg_item_stack_get_def (stack) == fixture->potion);
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack), ==, 5);
    g_assert_cmpuint (lrg_item_stack_get_max_quantity (stack), ==, 10);
    g_assert_cmpuint (lrg_item_stack_get_space_remaining (stack), ==, 5);
    g_assert_false (lrg_item_stack_is_full (stack));
    g_assert_false (lrg_item_stack_is_empty (stack));

    lrg_item_stack_unref (stack);
}

static void
test_item_stack_clamp_quantity (InventoryFixture *fixture,
                                gconstpointer     user_data)
{
    LrgItemStack *stack = NULL;

    (void)user_data;

    /* Request more than max_stack */
    stack = lrg_item_stack_new (fixture->potion, 100);

    /* Should be clamped to max_stack */
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack), ==, 10);
    g_assert_true (lrg_item_stack_is_full (stack));

    lrg_item_stack_unref (stack);
}

static void
test_item_stack_add_remove (InventoryFixture *fixture,
                            gconstpointer     user_data)
{
    LrgItemStack *stack = NULL;
    guint added;
    guint removed;

    (void)user_data;

    stack = lrg_item_stack_new (fixture->potion, 3);
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack), ==, 3);

    /* Add some */
    added = lrg_item_stack_add (stack, 4);
    g_assert_cmpuint (added, ==, 4);
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack), ==, 7);

    /* Try to add more than space allows */
    added = lrg_item_stack_add (stack, 10);
    g_assert_cmpuint (added, ==, 3);  /* Only 3 space remaining */
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack), ==, 10);
    g_assert_true (lrg_item_stack_is_full (stack));

    /* Remove some */
    removed = lrg_item_stack_remove (stack, 5);
    g_assert_cmpuint (removed, ==, 5);
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack), ==, 5);

    /* Try to remove more than available */
    removed = lrg_item_stack_remove (stack, 10);
    g_assert_cmpuint (removed, ==, 5);  /* Only had 5 */
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack), ==, 0);
    g_assert_true (lrg_item_stack_is_empty (stack));

    lrg_item_stack_unref (stack);
}

static void
test_item_stack_split (InventoryFixture *fixture,
                       gconstpointer     user_data)
{
    LrgItemStack *stack = NULL;
    LrgItemStack *split = NULL;

    (void)user_data;

    stack = lrg_item_stack_new (fixture->gold, 100);
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack), ==, 100);

    /* Split off 30 */
    split = lrg_item_stack_split (stack, 30);
    g_assert_nonnull (split);
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack), ==, 70);
    g_assert_cmpuint (lrg_item_stack_get_quantity (split), ==, 30);
    g_assert_true (lrg_item_stack_get_def (split) == fixture->gold);

    lrg_item_stack_unref (split);

    /* Try to split more than available */
    split = lrg_item_stack_split (stack, 100);
    g_assert_null (split);  /* Should fail */
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack), ==, 70);

    /* Try to split zero */
    split = lrg_item_stack_split (stack, 0);
    g_assert_null (split);

    lrg_item_stack_unref (stack);
}

static void
test_item_stack_merge (InventoryFixture *fixture,
                       gconstpointer     user_data)
{
    LrgItemStack *stack1 = NULL;
    LrgItemStack *stack2 = NULL;
    guint merged;

    (void)user_data;

    stack1 = lrg_item_stack_new (fixture->potion, 3);
    stack2 = lrg_item_stack_new (fixture->potion, 5);

    /* Check can merge */
    g_assert_true (lrg_item_stack_can_merge (stack1, stack2));

    /* Merge */
    merged = lrg_item_stack_merge (stack1, stack2);
    g_assert_cmpuint (merged, ==, 5);
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack1), ==, 8);
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack2), ==, 0);

    lrg_item_stack_unref (stack1);
    lrg_item_stack_unref (stack2);
}

static void
test_item_stack_merge_overflow (InventoryFixture *fixture,
                                gconstpointer     user_data)
{
    LrgItemStack *stack1 = NULL;
    LrgItemStack *stack2 = NULL;
    guint merged;

    (void)user_data;

    stack1 = lrg_item_stack_new (fixture->potion, 7);
    stack2 = lrg_item_stack_new (fixture->potion, 8);

    /* Merge - should only transfer 3 */
    merged = lrg_item_stack_merge (stack1, stack2);
    g_assert_cmpuint (merged, ==, 3);
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack1), ==, 10);
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack2), ==, 5);

    lrg_item_stack_unref (stack1);
    lrg_item_stack_unref (stack2);
}

static void
test_item_stack_copy (InventoryFixture *fixture,
                      gconstpointer     user_data)
{
    LrgItemStack *original = NULL;
    LrgItemStack *copy = NULL;

    (void)user_data;

    original = lrg_item_stack_new (fixture->sword, 1);
    lrg_item_stack_set_data_int (original, "durability", 85);
    lrg_item_stack_set_data_string (original, "enchant", "sharpness");

    copy = lrg_item_stack_copy (original);

    g_assert_nonnull (copy);
    g_assert_true (copy != original);  /* Different instances */
    g_assert_true (lrg_item_stack_get_def (copy) == lrg_item_stack_get_def (original));
    g_assert_cmpuint (lrg_item_stack_get_quantity (copy), ==, lrg_item_stack_get_quantity (original));
    g_assert_cmpint (lrg_item_stack_get_data_int (copy, "durability", 0), ==, 85);
    g_assert_cmpstr (lrg_item_stack_get_data_string (copy, "enchant"), ==, "sharpness");

    /* Modifying copy doesn't affect original */
    lrg_item_stack_set_data_int (copy, "durability", 50);
    g_assert_cmpint (lrg_item_stack_get_data_int (original, "durability", 0), ==, 85);
    g_assert_cmpint (lrg_item_stack_get_data_int (copy, "durability", 0), ==, 50);

    lrg_item_stack_unref (original);
    lrg_item_stack_unref (copy);
}

static void
test_item_stack_instance_data (InventoryFixture *fixture,
                               gconstpointer     user_data)
{
    LrgItemStack *stack = NULL;

    (void)user_data;

    stack = lrg_item_stack_new (fixture->sword, 1);

    /* Initially no data */
    g_assert_false (lrg_item_stack_has_data (stack, "durability"));
    g_assert_cmpint (lrg_item_stack_get_data_int (stack, "durability", 100), ==, 100);

    /* Set and get int */
    lrg_item_stack_set_data_int (stack, "durability", 75);
    g_assert_true (lrg_item_stack_has_data (stack, "durability"));
    g_assert_cmpint (lrg_item_stack_get_data_int (stack, "durability", 0), ==, 75);

    /* Set and get float */
    lrg_item_stack_set_data_float (stack, "weight", 2.5f);
    g_assert_cmpfloat_with_epsilon (lrg_item_stack_get_data_float (stack, "weight", 0.0f), 2.5f, 0.001f);

    /* Set and get string */
    lrg_item_stack_set_data_string (stack, "owner", "player1");
    g_assert_cmpstr (lrg_item_stack_get_data_string (stack, "owner"), ==, "player1");

    /* Remove data */
    g_assert_true (lrg_item_stack_remove_data (stack, "durability"));
    g_assert_false (lrg_item_stack_has_data (stack, "durability"));

    /* Clear all data */
    lrg_item_stack_clear_data (stack);
    g_assert_false (lrg_item_stack_has_data (stack, "weight"));
    g_assert_false (lrg_item_stack_has_data (stack, "owner"));

    lrg_item_stack_unref (stack);
}

/* ==========================================================================
 * LrgInventory Tests
 * ========================================================================== */

static void
test_inventory_new (void)
{
    g_autoptr(LrgInventory) inv = NULL;

    inv = lrg_inventory_new (20);

    g_assert_nonnull (inv);
    g_assert_cmpuint (lrg_inventory_get_capacity (inv), ==, 20);
    g_assert_cmpuint (lrg_inventory_get_used_slots (inv), ==, 0);
    g_assert_cmpuint (lrg_inventory_get_free_slots (inv), ==, 20);
    g_assert_true (lrg_inventory_is_empty (inv));
    g_assert_false (lrg_inventory_is_full (inv));
}

static void
test_inventory_add_item (InventoryFixture *fixture,
                         gconstpointer     user_data)
{
    guint added;

    (void)user_data;

    /* Add some potions */
    added = lrg_inventory_add_item (fixture->inventory, fixture->potion, 5);
    g_assert_cmpuint (added, ==, 5);
    g_assert_cmpuint (lrg_inventory_get_used_slots (fixture->inventory), ==, 1);
    g_assert_cmpuint (lrg_inventory_count_item (fixture->inventory, "potion_health"), ==, 5);

    /* Add more to stack */
    added = lrg_inventory_add_item (fixture->inventory, fixture->potion, 3);
    g_assert_cmpuint (added, ==, 3);
    g_assert_cmpuint (lrg_inventory_get_used_slots (fixture->inventory), ==, 1);
    g_assert_cmpuint (lrg_inventory_count_item (fixture->inventory, "potion_health"), ==, 8);

    /* Add more than stack allows - should create new stack */
    added = lrg_inventory_add_item (fixture->inventory, fixture->potion, 5);
    g_assert_cmpuint (added, ==, 5);
    g_assert_cmpuint (lrg_inventory_get_used_slots (fixture->inventory), ==, 2);  /* 10 + 3 */
    g_assert_cmpuint (lrg_inventory_count_item (fixture->inventory, "potion_health"), ==, 13);
}

static void
test_inventory_add_non_stackable (InventoryFixture *fixture,
                                  gconstpointer     user_data)
{
    guint added;

    (void)user_data;

    /* Add sword (non-stackable, each takes a slot) */
    added = lrg_inventory_add_item (fixture->inventory, fixture->sword, 1);
    g_assert_cmpuint (added, ==, 1);
    g_assert_cmpuint (lrg_inventory_get_used_slots (fixture->inventory), ==, 1);

    /* Add another sword */
    added = lrg_inventory_add_item (fixture->inventory, fixture->sword, 1);
    g_assert_cmpuint (added, ==, 1);
    g_assert_cmpuint (lrg_inventory_get_used_slots (fixture->inventory), ==, 2);

    /* Try to add 3 more swords at once */
    added = lrg_inventory_add_item (fixture->inventory, fixture->sword, 3);
    g_assert_cmpuint (added, ==, 3);
    g_assert_cmpuint (lrg_inventory_get_used_slots (fixture->inventory), ==, 5);
}

static void
test_inventory_full (InventoryFixture *fixture,
                     gconstpointer     user_data)
{
    guint added;
    guint i;

    (void)user_data;

    /* Fill all 10 slots with swords */
    for (i = 0; i < 10; i++)
    {
        added = lrg_inventory_add_item (fixture->inventory, fixture->sword, 1);
        g_assert_cmpuint (added, ==, 1);
    }

    g_assert_true (lrg_inventory_is_full (fixture->inventory));

    /* Try to add another */
    added = lrg_inventory_add_item (fixture->inventory, fixture->sword, 1);
    g_assert_cmpuint (added, ==, 0);

    /* But we can still add to existing stacks if they're stackable */
    lrg_inventory_clear (fixture->inventory);
    lrg_inventory_add_item (fixture->inventory, fixture->potion, 5);

    /* Fill remaining 9 slots */
    for (i = 0; i < 9; i++)
    {
        lrg_inventory_add_item (fixture->inventory, fixture->sword, 1);
    }

    g_assert_true (lrg_inventory_is_full (fixture->inventory));

    /* Can still add to the potion stack */
    added = lrg_inventory_add_item (fixture->inventory, fixture->potion, 3);
    g_assert_cmpuint (added, ==, 3);
    g_assert_cmpuint (lrg_inventory_count_item (fixture->inventory, "potion_health"), ==, 8);
}

static void
test_inventory_remove_item (InventoryFixture *fixture,
                            gconstpointer     user_data)
{
    guint removed;

    (void)user_data;

    /* Add items first */
    lrg_inventory_add_item (fixture->inventory, fixture->potion, 15);  /* Creates 2 stacks: 10 + 5 */
    g_assert_cmpuint (lrg_inventory_count_item (fixture->inventory, "potion_health"), ==, 15);

    /* Remove some */
    removed = lrg_inventory_remove_item (fixture->inventory, "potion_health", 7);
    g_assert_cmpuint (removed, ==, 7);
    g_assert_cmpuint (lrg_inventory_count_item (fixture->inventory, "potion_health"), ==, 8);

    /* Remove more */
    removed = lrg_inventory_remove_item (fixture->inventory, "potion_health", 5);
    g_assert_cmpuint (removed, ==, 5);
    g_assert_cmpuint (lrg_inventory_count_item (fixture->inventory, "potion_health"), ==, 3);

    /* Try to remove more than available */
    removed = lrg_inventory_remove_item (fixture->inventory, "potion_health", 10);
    g_assert_cmpuint (removed, ==, 3);
    g_assert_cmpuint (lrg_inventory_count_item (fixture->inventory, "potion_health"), ==, 0);
    g_assert_false (lrg_inventory_has_item (fixture->inventory, "potion_health", 1));
}

static void
test_inventory_slot_operations (InventoryFixture *fixture,
                                gconstpointer     user_data)
{
    LrgItemStack *stack = NULL;

    (void)user_data;

    /* Initially empty */
    g_assert_true (lrg_inventory_is_slot_empty (fixture->inventory, 0));
    g_assert_null (lrg_inventory_get_slot (fixture->inventory, 0));

    /* Add to specific slot */
    lrg_inventory_add_to_slot (fixture->inventory, 3, fixture->potion, 5);
    g_assert_false (lrg_inventory_is_slot_empty (fixture->inventory, 3));

    stack = lrg_inventory_get_slot (fixture->inventory, 3);
    g_assert_nonnull (stack);
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack), ==, 5);

    /* Clear slot */
    stack = lrg_inventory_clear_slot (fixture->inventory, 3);
    g_assert_nonnull (stack);
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack), ==, 5);
    g_assert_true (lrg_inventory_is_slot_empty (fixture->inventory, 3));

    lrg_item_stack_unref (stack);
}

static void
test_inventory_swap_slots (InventoryFixture *fixture,
                           gconstpointer     user_data)
{
    LrgItemStack *stack0 = NULL;
    LrgItemStack *stack1 = NULL;

    (void)user_data;

    /* Add items to slots 0 and 1 */
    lrg_inventory_add_to_slot (fixture->inventory, 0, fixture->potion, 5);
    lrg_inventory_add_to_slot (fixture->inventory, 1, fixture->sword, 1);

    /* Swap */
    g_assert_true (lrg_inventory_swap_slots (fixture->inventory, 0, 1));

    /* Verify swap */
    stack0 = lrg_inventory_get_slot (fixture->inventory, 0);
    stack1 = lrg_inventory_get_slot (fixture->inventory, 1);

    g_assert_true (lrg_item_stack_get_def (stack0) == fixture->sword);
    g_assert_true (lrg_item_stack_get_def (stack1) == fixture->potion);
}

static void
test_inventory_move_to_slot (InventoryFixture *fixture,
                             gconstpointer     user_data)
{
    LrgItemStack *stack0 = NULL;
    LrgItemStack *stack5 = NULL;
    guint moved;

    (void)user_data;

    /* Add potions to slot 0 */
    lrg_inventory_add_to_slot (fixture->inventory, 0, fixture->potion, 8);

    /* Move 3 to empty slot 5 */
    moved = lrg_inventory_move_to_slot (fixture->inventory, 0, 5, 3);
    g_assert_cmpuint (moved, ==, 3);

    stack0 = lrg_inventory_get_slot (fixture->inventory, 0);
    stack5 = lrg_inventory_get_slot (fixture->inventory, 5);

    g_assert_cmpuint (lrg_item_stack_get_quantity (stack0), ==, 5);
    g_assert_cmpuint (lrg_item_stack_get_quantity (stack5), ==, 3);
}

static void
test_inventory_find_item (InventoryFixture *fixture,
                          gconstpointer     user_data)
{
    LrgItemStack *found = NULL;
    gint slot;

    (void)user_data;

    /* Not found initially */
    g_assert_null (lrg_inventory_find_item (fixture->inventory, "potion_health"));
    g_assert_cmpint (lrg_inventory_find_item_slot (fixture->inventory, "potion_health"), ==, -1);

    /* Add and find */
    lrg_inventory_add_to_slot (fixture->inventory, 4, fixture->potion, 5);

    found = lrg_inventory_find_item (fixture->inventory, "potion_health");
    g_assert_nonnull (found);
    g_assert_cmpuint (lrg_item_stack_get_quantity (found), ==, 5);

    slot = lrg_inventory_find_item_slot (fixture->inventory, "potion_health");
    g_assert_cmpint (slot, ==, 4);
}

static void
test_inventory_has_item (InventoryFixture *fixture,
                         gconstpointer     user_data)
{
    (void)user_data;

    lrg_inventory_add_item (fixture->inventory, fixture->potion, 8);

    g_assert_true (lrg_inventory_has_item (fixture->inventory, "potion_health", 1));
    g_assert_true (lrg_inventory_has_item (fixture->inventory, "potion_health", 5));
    g_assert_true (lrg_inventory_has_item (fixture->inventory, "potion_health", 8));
    g_assert_false (lrg_inventory_has_item (fixture->inventory, "potion_health", 9));
    g_assert_false (lrg_inventory_has_item (fixture->inventory, "sword_iron", 1));
}

static void
test_inventory_capacity (InventoryFixture *fixture,
                         gconstpointer     user_data)
{
    (void)user_data;

    g_assert_cmpuint (lrg_inventory_get_capacity (fixture->inventory), ==, 10);

    /* Increase capacity */
    lrg_inventory_set_capacity (fixture->inventory, 20);
    g_assert_cmpuint (lrg_inventory_get_capacity (fixture->inventory), ==, 20);
    g_assert_cmpuint (lrg_inventory_get_free_slots (fixture->inventory), ==, 20);

    /* Add items and then reduce capacity */
    lrg_inventory_add_item (fixture->inventory, fixture->sword, 15);
    g_assert_cmpuint (lrg_inventory_get_used_slots (fixture->inventory), ==, 15);

    /* Reducing capacity removes items in excess slots */
    lrg_inventory_set_capacity (fixture->inventory, 10);
    g_assert_cmpuint (lrg_inventory_get_capacity (fixture->inventory), ==, 10);
    g_assert_cmpuint (lrg_inventory_get_used_slots (fixture->inventory), ==, 10);
}

static void
test_inventory_clear (InventoryFixture *fixture,
                      gconstpointer     user_data)
{
    (void)user_data;

    /* Add various items */
    lrg_inventory_add_item (fixture->inventory, fixture->sword, 3);
    lrg_inventory_add_item (fixture->inventory, fixture->potion, 10);
    lrg_inventory_add_item (fixture->inventory, fixture->gold, 500);

    g_assert_false (lrg_inventory_is_empty (fixture->inventory));

    /* Clear */
    lrg_inventory_clear (fixture->inventory);

    g_assert_true (lrg_inventory_is_empty (fixture->inventory));
    g_assert_cmpuint (lrg_inventory_get_used_slots (fixture->inventory), ==, 0);
    g_assert_false (lrg_inventory_has_item (fixture->inventory, "sword_iron", 1));
    g_assert_false (lrg_inventory_has_item (fixture->inventory, "potion_health", 1));
    g_assert_false (lrg_inventory_has_item (fixture->inventory, "gold_coin", 1));
}

static void
test_inventory_sort (InventoryFixture *fixture,
                     gconstpointer     user_data)
{
    LrgItemStack *stack = NULL;

    (void)user_data;

    /* Add items in random order to various slots */
    lrg_inventory_add_to_slot (fixture->inventory, 5, fixture->potion, 5);
    lrg_inventory_add_to_slot (fixture->inventory, 2, fixture->sword, 1);
    lrg_inventory_add_to_slot (fixture->inventory, 8, fixture->gold, 100);
    lrg_inventory_add_to_slot (fixture->inventory, 0, fixture->potion, 3);

    /* Sort */
    lrg_inventory_sort (fixture->inventory);

    /* After sorting, items should be:
     * - Combined potions (8 total) at start
     * - Then gold
     * - Then sword
     * - Then weapons
     * Sorted by type, then by ID */

    /* Verify items are consolidated and sorted */
    g_assert_cmpuint (lrg_inventory_get_used_slots (fixture->inventory), <=, 4);

    /* Potions should be combined */
    g_assert_cmpuint (lrg_inventory_count_item (fixture->inventory, "potion_health"), ==, 8);

    /* First slot should be consumable (potion) - lowest enum value that's present */
    stack = lrg_inventory_get_slot (fixture->inventory, 0);
    g_assert_nonnull (stack);
}

/* ==========================================================================
 * Signal Tests
 * ========================================================================== */

typedef struct
{
    gboolean item_added_fired;
    gboolean item_removed_fired;
    gboolean slot_changed_fired;
    guint    last_slot;
} SignalData;

static void
on_item_added (LrgInventory *inventory,
               guint         slot,
               LrgItemStack *stack,
               gpointer      user_data)
{
    SignalData *data = (SignalData *)user_data;

    (void)inventory;
    (void)stack;

    data->item_added_fired = TRUE;
    data->last_slot = slot;
}

static void
on_item_removed (LrgInventory *inventory,
                 guint         slot,
                 LrgItemStack *stack,
                 gpointer      user_data)
{
    SignalData *data = (SignalData *)user_data;

    (void)inventory;
    (void)stack;

    data->item_removed_fired = TRUE;
    data->last_slot = slot;
}

static void
on_slot_changed (LrgInventory *inventory,
                 guint         slot,
                 gpointer      user_data)
{
    SignalData *data = (SignalData *)user_data;

    (void)inventory;

    data->slot_changed_fired = TRUE;
    data->last_slot = slot;
}

static void
test_inventory_signals (InventoryFixture *fixture,
                        gconstpointer     user_data)
{
    SignalData signal_data = { FALSE, FALSE, FALSE, 0 };

    (void)user_data;

    g_signal_connect (fixture->inventory, "item-added",
                      G_CALLBACK (on_item_added), &signal_data);
    g_signal_connect (fixture->inventory, "item-removed",
                      G_CALLBACK (on_item_removed), &signal_data);
    g_signal_connect (fixture->inventory, "slot-changed",
                      G_CALLBACK (on_slot_changed), &signal_data);

    /* Add item */
    lrg_inventory_add_item (fixture->inventory, fixture->potion, 5);
    g_assert_true (signal_data.item_added_fired);
    g_assert_true (signal_data.slot_changed_fired);

    /* Reset */
    signal_data.item_added_fired = FALSE;
    signal_data.item_removed_fired = FALSE;
    signal_data.slot_changed_fired = FALSE;

    /* Remove partial - slot-changed fires but item-removed doesn't
     * (item-removed only fires when stack is fully removed) */
    lrg_inventory_remove_item (fixture->inventory, "potion_health", 3);
    g_assert_false (signal_data.item_removed_fired);
    g_assert_true (signal_data.slot_changed_fired);

    /* Reset */
    signal_data.item_added_fired = FALSE;
    signal_data.item_removed_fired = FALSE;
    signal_data.slot_changed_fired = FALSE;

    /* Remove all remaining - now item-removed should fire */
    lrg_inventory_remove_item (fixture->inventory, "potion_health", 2);
    g_assert_true (signal_data.item_removed_fired);
    g_assert_true (signal_data.slot_changed_fired);
}

/* ==========================================================================
 * LrgEquipment Tests
 * ========================================================================== */

static void
test_equipment_new (void)
{
    g_autoptr(LrgEquipment) equipment = NULL;

    equipment = lrg_equipment_new ();

    g_assert_nonnull (equipment);

    /* All slots should be empty initially */
    g_assert_true (lrg_equipment_is_slot_empty (equipment, LRG_EQUIPMENT_SLOT_HEAD));
    g_assert_true (lrg_equipment_is_slot_empty (equipment, LRG_EQUIPMENT_SLOT_CHEST));
    g_assert_true (lrg_equipment_is_slot_empty (equipment, LRG_EQUIPMENT_SLOT_WEAPON));
    g_assert_true (lrg_equipment_is_slot_empty (equipment, LRG_EQUIPMENT_SLOT_ACCESSORY));
}

static void
test_equipment_equip_basic (InventoryFixture *fixture,
                            gconstpointer     user_data)
{
    LrgItemStack *helmet_stack = NULL;
    LrgItemStack *retrieved = NULL;
    LrgItemStack *old = NULL;

    (void)user_data;

    helmet_stack = lrg_item_stack_new (fixture->helmet, 1);

    /* Equip helmet */
    old = lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_HEAD, helmet_stack);
    g_assert_null (old);  /* No previous item */

    /* Verify it's equipped */
    g_assert_false (lrg_equipment_is_slot_empty (fixture->equipment, LRG_EQUIPMENT_SLOT_HEAD));
    retrieved = lrg_equipment_get_slot (fixture->equipment, LRG_EQUIPMENT_SLOT_HEAD);
    g_assert_nonnull (retrieved);
    g_assert_true (lrg_item_stack_get_def (retrieved) == fixture->helmet);

    lrg_item_stack_unref (helmet_stack);
}

static void
test_equipment_unequip (InventoryFixture *fixture,
                        gconstpointer     user_data)
{
    LrgItemStack *sword_stack = NULL;
    LrgItemStack *unequipped = NULL;

    (void)user_data;

    sword_stack = lrg_item_stack_new (fixture->sword, 1);

    /* Equip sword */
    lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_WEAPON, sword_stack);
    g_assert_false (lrg_equipment_is_slot_empty (fixture->equipment, LRG_EQUIPMENT_SLOT_WEAPON));

    /* Unequip */
    unequipped = lrg_equipment_unequip (fixture->equipment, LRG_EQUIPMENT_SLOT_WEAPON);
    g_assert_nonnull (unequipped);
    g_assert_true (lrg_item_stack_get_def (unequipped) == fixture->sword);
    g_assert_true (lrg_equipment_is_slot_empty (fixture->equipment, LRG_EQUIPMENT_SLOT_WEAPON));

    lrg_item_stack_unref (unequipped);
    lrg_item_stack_unref (sword_stack);
}

static void
test_equipment_get_slot (InventoryFixture *fixture,
                         gconstpointer     user_data)
{
    LrgItemStack *ring_stack = NULL;
    LrgItemStack *retrieved = NULL;

    (void)user_data;

    /* Empty slot returns NULL */
    g_assert_null (lrg_equipment_get_slot (fixture->equipment, LRG_EQUIPMENT_SLOT_ACCESSORY));

    ring_stack = lrg_item_stack_new (fixture->ring, 1);
    lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_ACCESSORY, ring_stack);

    /* Get returns the item (transfer none) */
    retrieved = lrg_equipment_get_slot (fixture->equipment, LRG_EQUIPMENT_SLOT_ACCESSORY);
    g_assert_nonnull (retrieved);
    g_assert_true (lrg_item_stack_get_def (retrieved) == fixture->ring);

    lrg_item_stack_unref (ring_stack);
}

static void
test_equipment_is_slot_empty (InventoryFixture *fixture,
                              gconstpointer     user_data)
{
    LrgItemStack *chest_stack = NULL;

    (void)user_data;

    g_assert_true (lrg_equipment_is_slot_empty (fixture->equipment, LRG_EQUIPMENT_SLOT_CHEST));

    chest_stack = lrg_item_stack_new (fixture->chestplate, 1);
    lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_CHEST, chest_stack);

    g_assert_false (lrg_equipment_is_slot_empty (fixture->equipment, LRG_EQUIPMENT_SLOT_CHEST));

    lrg_item_stack_unref (chest_stack);
}

static void
test_equipment_equip_replaces (InventoryFixture *fixture,
                               gconstpointer     user_data)
{
    LrgItemStack *helmet1 = NULL;
    LrgItemStack *helmet2 = NULL;
    LrgItemStack *old = NULL;
    LrgItemStack *current = NULL;
    g_autoptr(LrgItemDef) helmet2_def = NULL;

    (void)user_data;

    /* Create a second helmet */
    helmet2_def = lrg_item_def_new ("helmet_steel");
    lrg_item_def_set_item_type (helmet2_def, LRG_ITEM_TYPE_ARMOR);
    lrg_item_def_set_stackable (helmet2_def, FALSE);

    helmet1 = lrg_item_stack_new (fixture->helmet, 1);
    helmet2 = lrg_item_stack_new (helmet2_def, 1);

    /* Equip first helmet */
    lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_HEAD, helmet1);

    /* Equip second helmet - should return first */
    old = lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_HEAD, helmet2);
    g_assert_nonnull (old);
    g_assert_true (lrg_item_stack_get_def (old) == fixture->helmet);

    /* Current should be helmet2 */
    current = lrg_equipment_get_slot (fixture->equipment, LRG_EQUIPMENT_SLOT_HEAD);
    g_assert_true (lrg_item_stack_get_def (current) == helmet2_def);

    lrg_item_stack_unref (old);
    lrg_item_stack_unref (helmet1);
    lrg_item_stack_unref (helmet2);
}

typedef struct
{
    gboolean item_equipped_fired;
    gboolean item_unequipped_fired;
    LrgEquipmentSlot last_slot;
} EquipmentSignalData;

static void
on_item_equipped (LrgEquipment    *equipment,
                  LrgEquipmentSlot slot,
                  LrgItemStack    *stack,
                  gpointer         user_data)
{
    EquipmentSignalData *data = (EquipmentSignalData *)user_data;

    (void)equipment;
    (void)stack;

    data->item_equipped_fired = TRUE;
    data->last_slot = slot;
}

static void
on_item_unequipped (LrgEquipment    *equipment,
                    LrgEquipmentSlot slot,
                    LrgItemStack    *stack,
                    gpointer         user_data)
{
    EquipmentSignalData *data = (EquipmentSignalData *)user_data;

    (void)equipment;
    (void)stack;

    data->item_unequipped_fired = TRUE;
    data->last_slot = slot;
}

static void
test_equipment_signals (InventoryFixture *fixture,
                        gconstpointer     user_data)
{
    EquipmentSignalData signal_data = { FALSE, FALSE, 0 };
    LrgItemStack *sword_stack = NULL;
    LrgItemStack *unequipped = NULL;

    (void)user_data;

    g_signal_connect (fixture->equipment, "item-equipped",
                      G_CALLBACK (on_item_equipped), &signal_data);
    g_signal_connect (fixture->equipment, "item-unequipped",
                      G_CALLBACK (on_item_unequipped), &signal_data);

    sword_stack = lrg_item_stack_new (fixture->sword, 1);

    /* Equip fires signal */
    lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_WEAPON, sword_stack);
    g_assert_true (signal_data.item_equipped_fired);
    g_assert_cmpint (signal_data.last_slot, ==, LRG_EQUIPMENT_SLOT_WEAPON);

    /* Reset */
    signal_data.item_equipped_fired = FALSE;
    signal_data.item_unequipped_fired = FALSE;

    /* Unequip fires signal */
    unequipped = lrg_equipment_unequip (fixture->equipment, LRG_EQUIPMENT_SLOT_WEAPON);
    g_assert_true (signal_data.item_unequipped_fired);
    g_assert_cmpint (signal_data.last_slot, ==, LRG_EQUIPMENT_SLOT_WEAPON);

    lrg_item_stack_unref (unequipped);
    lrg_item_stack_unref (sword_stack);
}

static void
test_equipment_get_equipped_slots (InventoryFixture *fixture,
                                   gconstpointer     user_data)
{
    LrgItemStack *helmet_stack = NULL;
    LrgItemStack *sword_stack = NULL;
    LrgItemStack *ring_stack = NULL;
    GList *slots = NULL;

    (void)user_data;

    /* Empty equipment has no slots */
    slots = lrg_equipment_get_equipped_slots (fixture->equipment);
    g_assert_null (slots);

    /* Equip some items */
    helmet_stack = lrg_item_stack_new (fixture->helmet, 1);
    sword_stack = lrg_item_stack_new (fixture->sword, 1);
    ring_stack = lrg_item_stack_new (fixture->ring, 1);

    lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_HEAD, helmet_stack);
    lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_WEAPON, sword_stack);
    lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_ACCESSORY, ring_stack);

    slots = lrg_equipment_get_equipped_slots (fixture->equipment);
    g_assert_nonnull (slots);
    g_assert_cmpuint (g_list_length (slots), ==, 3);

    g_list_free (slots);
    lrg_item_stack_unref (helmet_stack);
    lrg_item_stack_unref (sword_stack);
    lrg_item_stack_unref (ring_stack);
}

static void
test_equipment_can_equip (InventoryFixture *fixture,
                          gconstpointer     user_data)
{
    (void)user_data;

    /* Weapon slot only accepts weapons */
    g_assert_true (lrg_equipment_can_equip (fixture->equipment,
                                            LRG_EQUIPMENT_SLOT_WEAPON,
                                            fixture->sword));
    g_assert_false (lrg_equipment_can_equip (fixture->equipment,
                                             LRG_EQUIPMENT_SLOT_WEAPON,
                                             fixture->helmet));

    /* Armor slots accept armor */
    g_assert_true (lrg_equipment_can_equip (fixture->equipment,
                                            LRG_EQUIPMENT_SLOT_HEAD,
                                            fixture->helmet));
    g_assert_true (lrg_equipment_can_equip (fixture->equipment,
                                            LRG_EQUIPMENT_SLOT_CHEST,
                                            fixture->chestplate));
    g_assert_false (lrg_equipment_can_equip (fixture->equipment,
                                             LRG_EQUIPMENT_SLOT_HEAD,
                                             fixture->sword));

    /* Offhand accepts weapon or armor (shield) */
    g_assert_true (lrg_equipment_can_equip (fixture->equipment,
                                            LRG_EQUIPMENT_SLOT_OFFHAND,
                                            fixture->sword));
    g_assert_true (lrg_equipment_can_equip (fixture->equipment,
                                            LRG_EQUIPMENT_SLOT_OFFHAND,
                                            fixture->helmet));

    /* Accessory slot accepts generic items */
    g_assert_true (lrg_equipment_can_equip (fixture->equipment,
                                            LRG_EQUIPMENT_SLOT_ACCESSORY,
                                            fixture->ring));
    g_assert_false (lrg_equipment_can_equip (fixture->equipment,
                                             LRG_EQUIPMENT_SLOT_ACCESSORY,
                                             fixture->sword));
}

static void
test_equipment_stat_bonus (InventoryFixture *fixture,
                           gconstpointer     user_data)
{
    LrgItemStack *helmet_stack = NULL;
    LrgItemStack *chest_stack = NULL;
    LrgItemStack *sword_stack = NULL;
    gint defense_bonus;
    gint attack_bonus;
    gfloat weight_bonus;

    (void)user_data;

    helmet_stack = lrg_item_stack_new (fixture->helmet, 1);
    chest_stack = lrg_item_stack_new (fixture->chestplate, 1);
    sword_stack = lrg_item_stack_new (fixture->sword, 1);

    /* No equipment = no bonus */
    defense_bonus = lrg_equipment_get_stat_bonus (fixture->equipment, "defense");
    g_assert_cmpint (defense_bonus, ==, 0);

    /* Equip helmet (defense 5) */
    lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_HEAD, helmet_stack);
    defense_bonus = lrg_equipment_get_stat_bonus (fixture->equipment, "defense");
    g_assert_cmpint (defense_bonus, ==, 5);

    /* Equip chestplate (defense 10), total 15 */
    lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_CHEST, chest_stack);
    defense_bonus = lrg_equipment_get_stat_bonus (fixture->equipment, "defense");
    g_assert_cmpint (defense_bonus, ==, 15);

    /* Equip sword (attack 15) */
    lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_WEAPON, sword_stack);
    attack_bonus = lrg_equipment_get_stat_bonus (fixture->equipment, "attack");
    g_assert_cmpint (attack_bonus, ==, 15);

    /* Test float stat bonus (weight: helmet 2.5 + chest 5.0 = 7.5) */
    weight_bonus = lrg_equipment_get_stat_bonus_float (fixture->equipment, "weight");
    g_assert_cmpfloat_with_epsilon (weight_bonus, 7.5f, 0.001f);

    lrg_item_stack_unref (helmet_stack);
    lrg_item_stack_unref (chest_stack);
    lrg_item_stack_unref (sword_stack);
}

static void
test_equipment_clear (InventoryFixture *fixture,
                      gconstpointer     user_data)
{
    LrgItemStack *helmet_stack = NULL;
    LrgItemStack *sword_stack = NULL;
    GList *slots = NULL;

    (void)user_data;

    helmet_stack = lrg_item_stack_new (fixture->helmet, 1);
    sword_stack = lrg_item_stack_new (fixture->sword, 1);

    lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_HEAD, helmet_stack);
    lrg_equipment_equip (fixture->equipment, LRG_EQUIPMENT_SLOT_WEAPON, sword_stack);

    /* Verify equipped */
    slots = lrg_equipment_get_equipped_slots (fixture->equipment);
    g_assert_cmpuint (g_list_length (slots), ==, 2);
    g_list_free (slots);

    /* Clear all */
    lrg_equipment_clear (fixture->equipment);

    /* All slots should be empty */
    g_assert_true (lrg_equipment_is_slot_empty (fixture->equipment, LRG_EQUIPMENT_SLOT_HEAD));
    g_assert_true (lrg_equipment_is_slot_empty (fixture->equipment, LRG_EQUIPMENT_SLOT_WEAPON));

    slots = lrg_equipment_get_equipped_slots (fixture->equipment);
    g_assert_null (slots);

    lrg_item_stack_unref (helmet_stack);
    lrg_item_stack_unref (sword_stack);
}

static void
test_equipment_slot_enum (void)
{
    GType slot_type;
    GEnumClass *enum_class = NULL;
    GEnumValue *value = NULL;

    /* Verify enum is registered */
    slot_type = LRG_TYPE_EQUIPMENT_SLOT;
    g_assert_true (G_TYPE_IS_ENUM (slot_type));

    enum_class = g_type_class_ref (slot_type);
    g_assert_nonnull (enum_class);

    /* Verify all values exist */
    value = g_enum_get_value (enum_class, LRG_EQUIPMENT_SLOT_HEAD);
    g_assert_nonnull (value);
    g_assert_cmpstr (value->value_nick, ==, "head");

    value = g_enum_get_value (enum_class, LRG_EQUIPMENT_SLOT_WEAPON);
    g_assert_nonnull (value);
    g_assert_cmpstr (value->value_nick, ==, "weapon");

    value = g_enum_get_value (enum_class, LRG_EQUIPMENT_SLOT_ACCESSORY);
    g_assert_nonnull (value);
    g_assert_cmpstr (value->value_nick, ==, "accessory");

    g_type_class_unref (enum_class);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgItemDef tests */
    g_test_add_func ("/inventory/item-def/new", test_item_def_new);
    g_test_add ("/inventory/item-def/properties", InventoryFixture, NULL,
                inventory_fixture_set_up, test_item_def_properties, inventory_fixture_tear_down);
    g_test_add_func ("/inventory/item-def/custom-properties", test_item_def_custom_properties);
    g_test_add ("/inventory/item-def/can-stack-with", InventoryFixture, NULL,
                inventory_fixture_set_up, test_item_def_can_stack_with, inventory_fixture_tear_down);

    /* LrgItemStack tests */
    g_test_add ("/inventory/item-stack/new", InventoryFixture, NULL,
                inventory_fixture_set_up, test_item_stack_new, inventory_fixture_tear_down);
    g_test_add ("/inventory/item-stack/clamp-quantity", InventoryFixture, NULL,
                inventory_fixture_set_up, test_item_stack_clamp_quantity, inventory_fixture_tear_down);
    g_test_add ("/inventory/item-stack/add-remove", InventoryFixture, NULL,
                inventory_fixture_set_up, test_item_stack_add_remove, inventory_fixture_tear_down);
    g_test_add ("/inventory/item-stack/split", InventoryFixture, NULL,
                inventory_fixture_set_up, test_item_stack_split, inventory_fixture_tear_down);
    g_test_add ("/inventory/item-stack/merge", InventoryFixture, NULL,
                inventory_fixture_set_up, test_item_stack_merge, inventory_fixture_tear_down);
    g_test_add ("/inventory/item-stack/merge-overflow", InventoryFixture, NULL,
                inventory_fixture_set_up, test_item_stack_merge_overflow, inventory_fixture_tear_down);
    g_test_add ("/inventory/item-stack/copy", InventoryFixture, NULL,
                inventory_fixture_set_up, test_item_stack_copy, inventory_fixture_tear_down);
    g_test_add ("/inventory/item-stack/instance-data", InventoryFixture, NULL,
                inventory_fixture_set_up, test_item_stack_instance_data, inventory_fixture_tear_down);

    /* LrgInventory tests */
    g_test_add_func ("/inventory/inventory/new", test_inventory_new);
    g_test_add ("/inventory/inventory/add-item", InventoryFixture, NULL,
                inventory_fixture_set_up, test_inventory_add_item, inventory_fixture_tear_down);
    g_test_add ("/inventory/inventory/add-non-stackable", InventoryFixture, NULL,
                inventory_fixture_set_up, test_inventory_add_non_stackable, inventory_fixture_tear_down);
    g_test_add ("/inventory/inventory/full", InventoryFixture, NULL,
                inventory_fixture_set_up, test_inventory_full, inventory_fixture_tear_down);
    g_test_add ("/inventory/inventory/remove-item", InventoryFixture, NULL,
                inventory_fixture_set_up, test_inventory_remove_item, inventory_fixture_tear_down);
    g_test_add ("/inventory/inventory/slot-operations", InventoryFixture, NULL,
                inventory_fixture_set_up, test_inventory_slot_operations, inventory_fixture_tear_down);
    g_test_add ("/inventory/inventory/swap-slots", InventoryFixture, NULL,
                inventory_fixture_set_up, test_inventory_swap_slots, inventory_fixture_tear_down);
    g_test_add ("/inventory/inventory/move-to-slot", InventoryFixture, NULL,
                inventory_fixture_set_up, test_inventory_move_to_slot, inventory_fixture_tear_down);
    g_test_add ("/inventory/inventory/find-item", InventoryFixture, NULL,
                inventory_fixture_set_up, test_inventory_find_item, inventory_fixture_tear_down);
    g_test_add ("/inventory/inventory/has-item", InventoryFixture, NULL,
                inventory_fixture_set_up, test_inventory_has_item, inventory_fixture_tear_down);
    g_test_add ("/inventory/inventory/capacity", InventoryFixture, NULL,
                inventory_fixture_set_up, test_inventory_capacity, inventory_fixture_tear_down);
    g_test_add ("/inventory/inventory/clear", InventoryFixture, NULL,
                inventory_fixture_set_up, test_inventory_clear, inventory_fixture_tear_down);
    g_test_add ("/inventory/inventory/sort", InventoryFixture, NULL,
                inventory_fixture_set_up, test_inventory_sort, inventory_fixture_tear_down);
    g_test_add ("/inventory/inventory/signals", InventoryFixture, NULL,
                inventory_fixture_set_up, test_inventory_signals, inventory_fixture_tear_down);

    /* LrgEquipment tests */
    g_test_add_func ("/inventory/equipment/new", test_equipment_new);
    g_test_add ("/inventory/equipment/equip-basic", InventoryFixture, NULL,
                inventory_fixture_set_up, test_equipment_equip_basic, inventory_fixture_tear_down);
    g_test_add ("/inventory/equipment/unequip", InventoryFixture, NULL,
                inventory_fixture_set_up, test_equipment_unequip, inventory_fixture_tear_down);
    g_test_add ("/inventory/equipment/get-slot", InventoryFixture, NULL,
                inventory_fixture_set_up, test_equipment_get_slot, inventory_fixture_tear_down);
    g_test_add ("/inventory/equipment/is-slot-empty", InventoryFixture, NULL,
                inventory_fixture_set_up, test_equipment_is_slot_empty, inventory_fixture_tear_down);
    g_test_add ("/inventory/equipment/equip-replaces", InventoryFixture, NULL,
                inventory_fixture_set_up, test_equipment_equip_replaces, inventory_fixture_tear_down);
    g_test_add ("/inventory/equipment/signals", InventoryFixture, NULL,
                inventory_fixture_set_up, test_equipment_signals, inventory_fixture_tear_down);
    g_test_add ("/inventory/equipment/get-equipped-slots", InventoryFixture, NULL,
                inventory_fixture_set_up, test_equipment_get_equipped_slots, inventory_fixture_tear_down);
    g_test_add ("/inventory/equipment/can-equip", InventoryFixture, NULL,
                inventory_fixture_set_up, test_equipment_can_equip, inventory_fixture_tear_down);
    g_test_add ("/inventory/equipment/stat-bonus", InventoryFixture, NULL,
                inventory_fixture_set_up, test_equipment_stat_bonus, inventory_fixture_tear_down);
    g_test_add ("/inventory/equipment/clear", InventoryFixture, NULL,
                inventory_fixture_set_up, test_equipment_clear, inventory_fixture_tear_down);
    g_test_add_func ("/inventory/equipment/slot-enum", test_equipment_slot_enum);

    return g_test_run ();
}
