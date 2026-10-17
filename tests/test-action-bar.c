/* test-action-bar.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgActionSlot, LrgActionSlotState, LrgActionSlotProvider
 * and LrgActionBar: contents, pages, swaps, a provider's states and
 * uses, resizing and the variant round trip with its refusals.
 */

#include <glib.h>
#include <gio/gio.h>
#include <libregnum.h>

/* --- a provider: "potion" slots count down, "spell" ones cool down --- */

#define TEST_TYPE_PROVIDER (test_provider_get_type ())
G_DECLARE_FINAL_TYPE (TestProvider, test_provider, TEST, PROVIDER, GObject)

struct _TestProvider
{
    GObject parent_instance;
    gint    potions;
    gdouble cooldown;
    guint   uses;
};

static gboolean
provider_query (LrgActionSlotProvider *provider,
                const LrgActionSlot   *slot,
                LrgActionSlotState    *state)
{
    TestProvider *self = TEST_PROVIDER (provider);

    if (lrg_action_slot_is_kind (slot, "potion"))
    {
        state->count = self->potions;
        state->usable = self->potions > 0;
        return TRUE;
    }
    if (lrg_action_slot_is_kind (slot, "spell"))
    {
        state->cooldown_remaining = self->cooldown;
        state->cooldown_duration = 2.0;
        state->usable = self->cooldown <= 0.0;
        return TRUE;
    }
    return FALSE;
}

static gboolean
provider_activate (LrgActionSlotProvider  *provider,
                   const LrgActionSlot    *slot,
                   GError                **error)
{
    TestProvider *self = TEST_PROVIDER (provider);

    if (lrg_action_slot_is_kind (slot, "potion") && self->potions > 0)
    {
        self->potions--;
        self->uses++;
        return TRUE;
    }
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "cannot use %s", lrg_action_slot_get_id (slot));
    return FALSE;
}

static void
test_provider_iface_init (LrgActionSlotProviderInterface *iface)
{
    iface->query = provider_query;
    iface->activate = provider_activate;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (TestProvider, test_provider, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (LRG_TYPE_ACTION_SLOT_PROVIDER, test_provider_iface_init))

static void
test_provider_class_init (TestProviderClass *klass)
{
    (void)klass;
}

static void
test_provider_init (TestProvider *self)
{
    self->potions = 2;
}

/* --- tests --- */

static void
test_slot (void)
{
    g_autoptr(LrgActionSlot) slot = NULL;
    g_autoptr(LrgActionSlot) copy = NULL;
    g_autoptr(LrgActionSlot) other = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) wrong = NULL;
    LrgActionSlotState state;

    slot = lrg_action_slot_new ("spell", "Fireball", "Fireball", "");
    g_assert_cmpstr (lrg_action_slot_get_kind (slot), ==, "spell");
    g_assert_null (lrg_action_slot_get_icon (slot));
    value = g_variant_ref_sink (lrg_action_slot_to_variant (slot));
    copy = lrg_action_slot_new_from_variant (value);
    g_assert_true (lrg_action_slot_equal (slot, copy));
    g_assert_cmpstr (lrg_action_slot_get_label (copy), ==, "Fireball");
    other = lrg_action_slot_new ("spell", "Heal", NULL, NULL);
    g_assert_false (lrg_action_slot_equal (slot, other));
    g_assert_true (lrg_action_slot_equal (NULL, NULL));
    g_assert_false (lrg_action_slot_is_kind (NULL, "spell"));
    wrong = g_variant_ref_sink (g_variant_new ("(ssss)", "", "x", "", ""));
    g_assert_null (lrg_action_slot_new_from_variant (wrong));

    lrg_action_slot_state_init (&state);
    g_assert_true (state.usable);
    g_assert_cmpint (state.count, ==, -1);
    g_assert_cmpfloat (lrg_action_slot_state_get_cooldown_fraction (&state), ==, 0.0);
    state.cooldown_duration = 4.0;
    state.cooldown_remaining = 1.0;
    g_assert_cmpfloat (lrg_action_slot_state_get_cooldown_fraction (&state), ==, 0.25);
}

static void
test_bar_contents_and_pages (void)
{
    g_autoptr(LrgActionBar) bar = lrg_action_bar_new ("main", 12, 2);
    g_autoptr(LrgActionSlot) a = lrg_action_slot_new ("spell", "Fireball", NULL, NULL);
    g_autoptr(LrgActionSlot) b = lrg_action_slot_new ("potion", "17", "Potion", NULL);
    guint page;
    guint index;

    g_assert_cmpuint (lrg_action_bar_get_slot_count (bar), ==, 12);
    g_assert_true (lrg_action_bar_set_slot (bar, 0, a));
    g_assert_false (lrg_action_bar_set_slot (bar, 12, a));
    g_assert_true (lrg_action_bar_set_slot_at (bar, 1, 5, b));
    g_assert_null (lrg_action_bar_get_slot (bar, 5));
    g_assert_cmpuint (lrg_action_bar_next_page (bar), ==, 1);
    g_assert_true (lrg_action_bar_get_slot (bar, 5) == b);
    g_assert_cmpuint (lrg_action_bar_next_page (bar), ==, 0);
    g_assert_cmpuint (lrg_action_bar_previous_page (bar), ==, 1);
    g_assert_true (lrg_action_bar_find (bar, "spell", "Fireball", &page, &index));
    g_assert_cmpuint (page, ==, 0);
    g_assert_cmpuint (index, ==, 0);
    g_assert_false (lrg_action_bar_find (bar, "spell", "Heal", NULL, NULL));
    g_assert_true (lrg_action_bar_swap_slots (bar, 5, 6));
    g_assert_true (lrg_action_bar_get_slot (bar, 6) == b);
    g_assert_null (lrg_action_bar_get_slot (bar, 5));

    /* shrinking drops what no longer fits */
    lrg_action_bar_resize (bar, 4, 1);
    g_assert_cmpuint (lrg_action_bar_get_page (bar), ==, 0);
    g_assert_true (lrg_action_bar_get_slot (bar, 0) == a);
    g_assert_false (lrg_action_bar_find (bar, "potion", "17", NULL, NULL));
    lrg_action_bar_clear (bar);
    g_assert_null (lrg_action_bar_get_slot (bar, 0));
}

static void
test_bar_provider (void)
{
    g_autoptr(LrgActionBar) bar = lrg_action_bar_new (NULL, 6, 1);
    g_autoptr(TestProvider) provider = g_object_new (TEST_TYPE_PROVIDER, NULL);
    g_autoptr(LrgActionSlot) potion = lrg_action_slot_new ("potion", "17", NULL, NULL);
    g_autoptr(LrgActionSlot) spell = lrg_action_slot_new ("spell", "Heal", NULL, NULL);
    g_autoptr(LrgActionSlot) odd = lrg_action_slot_new ("odd", "1", NULL, NULL);
    g_autoptr(GError) error = NULL;
    LrgActionSlotState state;

    lrg_action_bar_set_slot (bar, 0, potion);
    lrg_action_bar_set_slot (bar, 1, spell);
    lrg_action_bar_set_slot (bar, 2, odd);

    g_assert_true (lrg_action_bar_query (bar, 0, LRG_ACTION_SLOT_PROVIDER (provider), &state));
    g_assert_cmpint (state.count, ==, 2);
    g_assert_true (lrg_action_bar_activate (bar, 0, LRG_ACTION_SLOT_PROVIDER (provider), &error));
    g_assert_true (lrg_action_bar_activate (bar, 0, LRG_ACTION_SLOT_PROVIDER (provider), &error));
    g_assert_false (lrg_action_bar_activate (bar, 0, LRG_ACTION_SLOT_PROVIDER (provider), &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_clear_error (&error);
    lrg_action_bar_query (bar, 0, LRG_ACTION_SLOT_PROVIDER (provider), &state);
    g_assert_false (state.usable);
    g_assert_cmpint (state.count, ==, 0);

    provider->cooldown = 1.5;
    lrg_action_bar_query (bar, 1, LRG_ACTION_SLOT_PROVIDER (provider), &state);
    g_assert_false (state.usable);
    g_assert_cmpfloat (lrg_action_slot_state_get_cooldown_fraction (&state), ==, 0.75);

    /* an unknown kind: the default state, the provider said no */
    g_assert_false (lrg_action_slot_provider_query (LRG_ACTION_SLOT_PROVIDER (provider), odd, &state));
    g_assert_true (state.usable);

    /* empty slots do nothing */
    g_assert_false (lrg_action_bar_query (bar, 4, LRG_ACTION_SLOT_PROVIDER (provider), &state));
    g_assert_false (lrg_action_bar_activate (bar, 4, LRG_ACTION_SLOT_PROVIDER (provider), &error));
    g_assert_no_error (error);
    g_assert_cmpuint (provider->uses, ==, 2);
}

static void
test_bar_variant (void)
{
    g_autoptr(LrgActionBar) bar = lrg_action_bar_new ("main", 12, 3);
    g_autoptr(LrgActionBar) copy = lrg_action_bar_new ("copy", 2, 1);
    g_autoptr(LrgActionSlot) a = lrg_action_slot_new ("spell", "Fireball", "Fireball", "fire");
    g_autoptr(LrgActionSlot) b = lrg_action_slot_new ("macro", "fish", NULL, NULL);
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) bad_layout = NULL;
    g_autoptr(GVariant) bad_slot = NULL;
    g_autoptr(GVariant) wrong = NULL;
    g_autoptr(GError) error = NULL;

    lrg_action_bar_set_slot_at (bar, 0, 3, a);
    lrg_action_bar_set_slot_at (bar, 2, 11, b);
    lrg_action_bar_set_page (bar, 2);
    lrg_action_bar_set_visible (bar, FALSE);
    value = g_variant_ref_sink (lrg_action_bar_to_variant (bar));
    g_assert_true (lrg_action_bar_restore (copy, value, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_action_bar_get_slot_count (copy), ==, 12);
    g_assert_cmpuint (lrg_action_bar_get_page_count (copy), ==, 3);
    g_assert_cmpuint (lrg_action_bar_get_page (copy), ==, 2);
    g_assert_false (lrg_action_bar_get_visible (copy));
    g_assert_true (lrg_action_slot_equal (lrg_action_bar_get_slot (copy, 11), b));
    g_assert_cmpstr (lrg_action_slot_get_icon (lrg_action_bar_get_slot_at (copy, 0, 3)), ==, "fire");

    /* refusals leave the bar as it was */
    bad_layout = g_variant_ref_sink (g_variant_new_parsed ("(uint32 0, uint32 1, uint32 0, true, @a(uu(ssss)) [])"));
    g_assert_false (lrg_action_bar_restore (copy, bad_layout, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    bad_slot = g_variant_ref_sink (g_variant_new_parsed (
        "(uint32 2, uint32 1, uint32 0, true, [(uint32 0, uint32 5, ('spell', 'x', '', ''))])"));
    g_assert_false (lrg_action_bar_restore (copy, bad_slot, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    wrong = g_variant_ref_sink (g_variant_new_string ("bar"));
    g_assert_false (lrg_action_bar_restore (copy, wrong, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_assert_cmpuint (lrg_action_bar_get_slot_count (copy), ==, 12);
    g_assert_true (lrg_action_slot_equal (lrg_action_bar_get_slot (copy, 11), b));
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/action-bar/slot", test_slot);
    g_test_add_func ("/action-bar/contents-and-pages", test_bar_contents_and_pages);
    g_test_add_func ("/action-bar/provider", test_bar_provider);
    g_test_add_func ("/action-bar/variant", test_bar_variant);

    return g_test_run ();
}
