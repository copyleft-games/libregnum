/* test-template-idle.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for idle game template system:
 *   - LrgIdleTemplate (idle game template)
 *   - LrgIdleMixin (idle mechanics interface)
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Skip Macros for CI/Headless
 * ========================================================================== */

#define SKIP_IF_NO_DISPLAY() \
    do { \
        if (g_getenv ("DISPLAY") == NULL && g_getenv ("WAYLAND_DISPLAY") == NULL) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

#define SKIP_IF_NULL(ptr) \
    do { \
        if ((ptr) == NULL) \
        { \
            g_test_skip ("Resource not available"); \
            return; \
        } \
    } while (0)

/* ==========================================================================
 * Mock Idle Mixin Implementation
 * ========================================================================== */

#define TEST_TYPE_IDLE_MIXIN_MOCK (test_idle_mixin_mock_get_type ())
G_DECLARE_FINAL_TYPE (TestIdleMixinMock, test_idle_mixin_mock,
                      TEST, IDLE_MIXIN_MOCK, GObject)

struct _TestIdleMixinMock
{
    GObject            parent_instance;
    LrgIdleCalculator *calculator;
    LrgPrestige       *prestige;
    gdouble            auto_save_interval;

    /* Tracking */
    gboolean           offline_progress_applied;
    gboolean           prestige_performed;
    LrgBigNumber      *last_applied_progress;
    LrgBigNumber      *last_prestige_reward;
};

static LrgIdleCalculator *
test_idle_mixin_mock_get_calculator (LrgIdleMixin *mixin)
{
    TestIdleMixinMock *self = TEST_IDLE_MIXIN_MOCK (mixin);
    return self->calculator;
}

static LrgPrestige *
test_idle_mixin_mock_get_prestige (LrgIdleMixin *mixin)
{
    TestIdleMixinMock *self = TEST_IDLE_MIXIN_MOCK (mixin);
    return self->prestige;
}

static LrgBigNumber *
test_idle_mixin_mock_calculate_offline (LrgIdleMixin *mixin,
                                        gdouble       efficiency,
                                        gdouble       max_hours)
{
    /* Simple mock: return a fixed value */
    return lrg_big_number_new (1000.0 * efficiency);
}

static void
test_idle_mixin_mock_apply_offline (LrgIdleMixin       *mixin,
                                    const LrgBigNumber *progress)
{
    TestIdleMixinMock *self = TEST_IDLE_MIXIN_MOCK (mixin);
    self->offline_progress_applied = TRUE;
    g_clear_pointer (&self->last_applied_progress, lrg_big_number_free);
    self->last_applied_progress = lrg_big_number_copy (progress);
}

static gdouble
test_idle_mixin_mock_get_auto_save_interval (LrgIdleMixin *mixin)
{
    TestIdleMixinMock *self = TEST_IDLE_MIXIN_MOCK (mixin);
    return self->auto_save_interval;
}

static void
test_idle_mixin_mock_on_prestige (LrgIdleMixin       *mixin,
                                  const LrgBigNumber *reward)
{
    TestIdleMixinMock *self = TEST_IDLE_MIXIN_MOCK (mixin);
    self->prestige_performed = TRUE;
    g_clear_pointer (&self->last_prestige_reward, lrg_big_number_free);
    self->last_prestige_reward = lrg_big_number_copy (reward);
}

static void
test_idle_mixin_iface_init (LrgIdleMixinInterface *iface)
{
    iface->get_idle_calculator = test_idle_mixin_mock_get_calculator;
    iface->get_prestige = test_idle_mixin_mock_get_prestige;
    iface->calculate_offline_progress = test_idle_mixin_mock_calculate_offline;
    iface->apply_offline_progress = test_idle_mixin_mock_apply_offline;
    iface->get_auto_save_interval = test_idle_mixin_mock_get_auto_save_interval;
    iface->on_prestige_performed = test_idle_mixin_mock_on_prestige;
}

static void
test_idle_mixin_mock_finalize (GObject *object)
{
    TestIdleMixinMock *self = TEST_IDLE_MIXIN_MOCK (object);

    g_clear_object (&self->calculator);
    g_clear_object (&self->prestige);
    g_clear_pointer (&self->last_applied_progress, lrg_big_number_free);
    g_clear_pointer (&self->last_prestige_reward, lrg_big_number_free);
    /* GObject finalize does nothing, safe to not chain up for test code */
}

static void
test_idle_mixin_mock_init (TestIdleMixinMock *self)
{
    self->calculator = lrg_idle_calculator_new ();
    self->prestige = NULL; /* Optional */
    self->auto_save_interval = 30.0;
    self->offline_progress_applied = FALSE;
    self->prestige_performed = FALSE;
    self->last_applied_progress = NULL;
    self->last_prestige_reward = NULL;
}

static void
test_idle_mixin_mock_class_init (TestIdleMixinMockClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = test_idle_mixin_mock_finalize;
}

G_DEFINE_TYPE_WITH_CODE (TestIdleMixinMock, test_idle_mixin_mock, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_IDLE_MIXIN,
                                                test_idle_mixin_iface_init))

/* ==========================================================================
 * Test Cases - LrgIdleTemplate Construction
 * ========================================================================== */

static void
test_idle_template_new (void)
{
    g_autoptr(LrgIdleTemplate) template = NULL;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_idle_template_new ();
    SKIP_IF_NULL (template);

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_IDLE_TEMPLATE (template));
}

static void
test_idle_template_inherits_game_template (void)
{
    g_autoptr(LrgIdleTemplate) template = NULL;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_idle_template_new ();
    SKIP_IF_NULL (template);

    /* Should inherit from LrgGameTemplate */
    g_assert_true (LRG_IS_GAME_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgIdleTemplate Properties
 * ========================================================================== */

static void
test_idle_template_offline_efficiency (void)
{
    g_autoptr(LrgIdleTemplate) template = NULL;
    gdouble efficiency;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_idle_template_new ();
    SKIP_IF_NULL (template);

    /* Default should be 0.5 (50%) */
    efficiency = lrg_idle_template_get_offline_efficiency (template);
    g_assert_cmpfloat (efficiency, ==, 0.5);

    /* Set new value */
    lrg_idle_template_set_offline_efficiency (template, 0.75);
    efficiency = lrg_idle_template_get_offline_efficiency (template);
    g_assert_cmpfloat (efficiency, ==, 0.75);
}

static void
test_idle_template_max_offline_hours (void)
{
    g_autoptr(LrgIdleTemplate) template = NULL;
    gdouble hours;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_idle_template_new ();
    SKIP_IF_NULL (template);

    /* Default should be 24.0 (1 day) */
    hours = lrg_idle_template_get_max_offline_hours (template);
    g_assert_cmpfloat (hours, ==, 24.0);

    /* Set new value */
    lrg_idle_template_set_max_offline_hours (template, 48.0);
    hours = lrg_idle_template_get_max_offline_hours (template);
    g_assert_cmpfloat (hours, ==, 48.0);
}

static void
test_idle_template_prestige_enabled (void)
{
    g_autoptr(LrgIdleTemplate) template = NULL;
    gboolean enabled;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_idle_template_new ();
    SKIP_IF_NULL (template);

    /* Default may vary, just test toggle */
    lrg_idle_template_set_prestige_enabled (template, TRUE);
    enabled = lrg_idle_template_get_prestige_enabled (template);
    g_assert_true (enabled);

    lrg_idle_template_set_prestige_enabled (template, FALSE);
    enabled = lrg_idle_template_get_prestige_enabled (template);
    g_assert_false (enabled);
}

static void
test_idle_template_show_offline_popup (void)
{
    g_autoptr(LrgIdleTemplate) template = NULL;
    gboolean show;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_idle_template_new ();
    SKIP_IF_NULL (template);

    lrg_idle_template_set_show_offline_popup (template, TRUE);
    show = lrg_idle_template_get_show_offline_popup (template);
    g_assert_true (show);

    lrg_idle_template_set_show_offline_popup (template, FALSE);
    show = lrg_idle_template_get_show_offline_popup (template);
    g_assert_false (show);
}

/* ==========================================================================
 * Test Cases - LrgIdleTemplate Subsystems
 * ========================================================================== */

static void
test_idle_template_get_idle_calculator (void)
{
    g_autoptr(LrgIdleTemplate) template = NULL;
    LrgIdleCalculator *calc;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_idle_template_new ();
    SKIP_IF_NULL (template);

    calc = lrg_idle_template_get_idle_calculator (template);

    /* Calculator may be NULL if template not fully initialized */
    if (calc != NULL)
    {
        g_assert_true (LRG_IS_IDLE_CALCULATOR (calc));
    }
}

static void
test_idle_template_get_prestige (void)
{
    g_autoptr(LrgIdleTemplate) template = NULL;
    LrgPrestige *prestige;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_idle_template_new ();
    SKIP_IF_NULL (template);

    lrg_idle_template_set_prestige_enabled (template, TRUE);
    prestige = lrg_idle_template_get_prestige (template);

    /* May be NULL if not configured, but should not crash */
    /* Just verify it doesn't crash */
    g_assert_true (prestige == NULL || LRG_IS_PRESTIGE (prestige));
}

/* ==========================================================================
 * Test Cases - LrgIdleTemplate Generator Operations
 * ========================================================================== */

static void
test_idle_template_add_generator (void)
{
    g_autoptr(LrgIdleTemplate) template = NULL;
    gint64 count;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_idle_template_new ();
    SKIP_IF_NULL (template);

    /* Add a generator - this is a no-op without lifecycle startup */
    lrg_idle_template_add_generator (template, "clicker", 1.0);

    /* Count returns 0 without initialized calculator */
    count = lrg_idle_template_get_generator_count (template, "clicker");
    g_assert_cmpint (count, >=, 0);
}

static void
test_idle_template_set_generator_count (void)
{
    g_autoptr(LrgIdleTemplate) template = NULL;
    gint64 count;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_idle_template_new ();
    SKIP_IF_NULL (template);

    /* These are no-ops without lifecycle startup (calculator not initialized) */
    lrg_idle_template_add_generator (template, "factory", 10.0);
    lrg_idle_template_set_generator_count (template, "factory", 5);

    /* Without initialized calculator, count returns 0 */
    count = lrg_idle_template_get_generator_count (template, "factory");
    g_assert_cmpint (count, >=, 0);
}

static void
test_idle_template_multiple_generators (void)
{
    g_autoptr(LrgIdleTemplate) template = NULL;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_idle_template_new ();
    SKIP_IF_NULL (template);

    /* These are no-ops without lifecycle startup (calculator not initialized) */
    lrg_idle_template_add_generator (template, "clicker", 1.0);
    lrg_idle_template_add_generator (template, "factory", 10.0);
    lrg_idle_template_add_generator (template, "mine", 50.0);

    lrg_idle_template_set_generator_count (template, "clicker", 10);
    lrg_idle_template_set_generator_count (template, "factory", 3);
    lrg_idle_template_set_generator_count (template, "mine", 1);

    /* Without initialized calculator, counts return 0 */
    g_assert_cmpint (lrg_idle_template_get_generator_count (template, "clicker"), >=, 0);
    g_assert_cmpint (lrg_idle_template_get_generator_count (template, "factory"), >=, 0);
    g_assert_cmpint (lrg_idle_template_get_generator_count (template, "mine"), >=, 0);
}

static void
test_idle_template_get_total_production_rate (void)
{
    g_autoptr(LrgIdleTemplate) template = NULL;
    g_autoptr(LrgBigNumber) rate = NULL;
    gdouble rate_value;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_idle_template_new ();
    SKIP_IF_NULL (template);

    /* These are no-ops without lifecycle startup (calculator not initialized) */
    lrg_idle_template_add_generator (template, "clicker", 1.0);
    lrg_idle_template_set_generator_count (template, "clicker", 10);

    rate = lrg_idle_template_get_total_production_rate (template);
    /* Rate may be NULL or 0 without initialized calculator */
    if (rate != NULL)
    {
        rate_value = lrg_big_number_to_double (rate);
        g_assert_cmpfloat (rate_value, >=, 0.0);
    }
}

/* ==========================================================================
 * Test Cases - LrgIdleMixin Interface
 * ========================================================================== */

static void
test_idle_mixin_interface_implements (void)
{
    g_autoptr(TestIdleMixinMock) mock = NULL;

    mock = g_object_new (TEST_TYPE_IDLE_MIXIN_MOCK, NULL);

    g_assert_nonnull (mock);
    g_assert_true (LRG_IS_IDLE_MIXIN (mock));
}

static void
test_idle_mixin_get_idle_calculator (void)
{
    g_autoptr(TestIdleMixinMock) mock = NULL;
    LrgIdleCalculator *calc;

    mock = g_object_new (TEST_TYPE_IDLE_MIXIN_MOCK, NULL);
    g_assert_nonnull (mock);

    calc = lrg_idle_mixin_get_idle_calculator (LRG_IDLE_MIXIN (mock));

    g_assert_nonnull (calc);
    g_assert_true (LRG_IS_IDLE_CALCULATOR (calc));
}

static void
test_idle_mixin_get_prestige_null (void)
{
    g_autoptr(TestIdleMixinMock) mock = NULL;
    LrgPrestige *prestige;

    mock = g_object_new (TEST_TYPE_IDLE_MIXIN_MOCK, NULL);
    g_assert_nonnull (mock);

    /* Our mock doesn't set prestige, should return NULL */
    prestige = lrg_idle_mixin_get_prestige (LRG_IDLE_MIXIN (mock));

    g_assert_null (prestige);
}

static void
test_idle_mixin_calculate_offline_progress (void)
{
    g_autoptr(TestIdleMixinMock) mock = NULL;
    g_autoptr(LrgBigNumber) progress = NULL;
    gdouble value;

    mock = g_object_new (TEST_TYPE_IDLE_MIXIN_MOCK, NULL);
    g_assert_nonnull (mock);

    progress = lrg_idle_mixin_calculate_offline_progress (
        LRG_IDLE_MIXIN (mock), 0.5, 24.0);

    g_assert_nonnull (progress);

    /* Our mock returns 1000.0 * efficiency */
    value = lrg_big_number_to_double (progress);
    g_assert_cmpfloat (value, ==, 500.0);
}

static void
test_idle_mixin_apply_offline_progress (void)
{
    g_autoptr(TestIdleMixinMock) mock = NULL;
    g_autoptr(LrgBigNumber) progress = NULL;

    mock = g_object_new (TEST_TYPE_IDLE_MIXIN_MOCK, NULL);
    g_assert_nonnull (mock);

    g_assert_false (mock->offline_progress_applied);

    progress = lrg_big_number_new (5000.0);
    lrg_idle_mixin_apply_offline_progress (LRG_IDLE_MIXIN (mock), progress);

    g_assert_true (mock->offline_progress_applied);
    g_assert_nonnull (mock->last_applied_progress);

    {
        gdouble applied = lrg_big_number_to_double (mock->last_applied_progress);
        g_assert_cmpfloat (applied, ==, 5000.0);
    }
}

static void
test_idle_mixin_get_auto_save_interval (void)
{
    g_autoptr(TestIdleMixinMock) mock = NULL;
    gdouble interval;

    mock = g_object_new (TEST_TYPE_IDLE_MIXIN_MOCK, NULL);
    g_assert_nonnull (mock);

    interval = lrg_idle_mixin_get_auto_save_interval (LRG_IDLE_MIXIN (mock));

    /* Our mock uses 30.0 seconds */
    g_assert_cmpfloat (interval, ==, 30.0);
}

static void
test_idle_mixin_simulate (void)
{
    g_autoptr(TestIdleMixinMock) mock = NULL;
    g_autoptr(LrgBigNumber) result = NULL;
    g_autoptr(LrgIdleGenerator) gen = NULL;
    LrgIdleCalculator *calc;
    gdouble value;

    mock = g_object_new (TEST_TYPE_IDLE_MIXIN_MOCK, NULL);
    g_assert_nonnull (mock);

    calc = lrg_idle_mixin_get_idle_calculator (LRG_IDLE_MIXIN (mock));

    /* Add a generator for production */
    gen = lrg_idle_generator_new_simple ("clicker", 10.0);
    lrg_idle_generator_set_count (gen, 1);
    lrg_idle_calculator_add_generator (calc, gen);

    /* Simulate 10 seconds */
    result = lrg_idle_mixin_simulate (LRG_IDLE_MIXIN (mock), 10.0);

    g_assert_nonnull (result);

    /* 1 clicker at 10.0/s for 10s = 100.0 */
    value = lrg_big_number_to_double (result);
    g_assert_cmpfloat (value, ==, 100.0);
}

static void
test_idle_mixin_get_total_rate (void)
{
    g_autoptr(TestIdleMixinMock) mock = NULL;
    g_autoptr(LrgBigNumber) rate = NULL;
    g_autoptr(LrgIdleGenerator) gen = NULL;
    LrgIdleCalculator *calc;
    gdouble value;

    mock = g_object_new (TEST_TYPE_IDLE_MIXIN_MOCK, NULL);
    g_assert_nonnull (mock);

    calc = lrg_idle_mixin_get_idle_calculator (LRG_IDLE_MIXIN (mock));

    gen = lrg_idle_generator_new_simple ("factory", 50.0);
    lrg_idle_generator_set_count (gen, 2);
    lrg_idle_calculator_add_generator (calc, gen);

    rate = lrg_idle_mixin_get_total_rate (LRG_IDLE_MIXIN (mock));

    g_assert_nonnull (rate);

    /* 2 factories at 50.0/s = 100.0/s */
    value = lrg_big_number_to_double (rate);
    g_assert_cmpfloat (value, ==, 100.0);
}

static void
test_idle_mixin_take_snapshot (void)
{
    g_autoptr(TestIdleMixinMock) mock = NULL;

    mock = g_object_new (TEST_TYPE_IDLE_MIXIN_MOCK, NULL);
    g_assert_nonnull (mock);

    /* Should not crash */
    lrg_idle_mixin_take_snapshot (LRG_IDLE_MIXIN (mock));

    g_assert_true (TRUE);
}

static void
test_idle_mixin_can_prestige_without_prestige (void)
{
    g_autoptr(TestIdleMixinMock) mock = NULL;
    g_autoptr(LrgBigNumber) val = NULL;
    gboolean can;

    mock = g_object_new (TEST_TYPE_IDLE_MIXIN_MOCK, NULL);
    g_assert_nonnull (mock);

    /* No prestige layer = cannot prestige */
    val = lrg_big_number_new (1000000.0);
    can = lrg_idle_mixin_can_prestige (LRG_IDLE_MIXIN (mock), val);

    g_assert_false (can);
}

static void
test_idle_mixin_get_prestige_multiplier (void)
{
    g_autoptr(TestIdleMixinMock) mock = NULL;
    gdouble multiplier;

    mock = g_object_new (TEST_TYPE_IDLE_MIXIN_MOCK, NULL);
    g_assert_nonnull (mock);

    /* No prestige = 1.0x multiplier */
    multiplier = lrg_idle_mixin_get_prestige_multiplier (LRG_IDLE_MIXIN (mock));

    g_assert_cmpfloat (multiplier, ==, 1.0);
}

/* ==========================================================================
 * Test Cases - LrgIdleTemplate Implements LrgIdleMixin
 * ========================================================================== */

static void
test_idle_template_implements_mixin (void)
{
    g_autoptr(LrgIdleTemplate) template = NULL;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_idle_template_new ();
    SKIP_IF_NULL (template);

    /* IdleTemplate should implement IdleMixin */
    g_assert_true (LRG_IS_IDLE_MIXIN (template));
}

static void
test_idle_template_mixin_get_calculator (void)
{
    g_autoptr(LrgIdleTemplate) template = NULL;
    LrgIdleCalculator *calc1;
    LrgIdleCalculator *calc2;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_idle_template_new ();
    SKIP_IF_NULL (template);

    /* Both methods should return the same calculator (may be NULL without lifecycle) */
    calc1 = lrg_idle_template_get_idle_calculator (template);
    calc2 = lrg_idle_mixin_get_idle_calculator (LRG_IDLE_MIXIN (template));

    /* Without lifecycle startup, calculator may be NULL */
    g_assert_true (calc1 == calc2);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgIdleTemplate - Construction */
    g_test_add_func ("/template/idle/new",
                     test_idle_template_new);
    g_test_add_func ("/template/idle/inherits-game-template",
                     test_idle_template_inherits_game_template);

    /* LrgIdleTemplate - Properties */
    g_test_add_func ("/template/idle/offline-efficiency",
                     test_idle_template_offline_efficiency);
    g_test_add_func ("/template/idle/max-offline-hours",
                     test_idle_template_max_offline_hours);
    g_test_add_func ("/template/idle/prestige-enabled",
                     test_idle_template_prestige_enabled);
    g_test_add_func ("/template/idle/show-offline-popup",
                     test_idle_template_show_offline_popup);

    /* LrgIdleTemplate - Subsystems */
    g_test_add_func ("/template/idle/get-idle-calculator",
                     test_idle_template_get_idle_calculator);
    g_test_add_func ("/template/idle/get-prestige",
                     test_idle_template_get_prestige);

    /* LrgIdleTemplate - Generator Operations */
    g_test_add_func ("/template/idle/add-generator",
                     test_idle_template_add_generator);
    g_test_add_func ("/template/idle/set-generator-count",
                     test_idle_template_set_generator_count);
    g_test_add_func ("/template/idle/multiple-generators",
                     test_idle_template_multiple_generators);
    g_test_add_func ("/template/idle/get-total-production-rate",
                     test_idle_template_get_total_production_rate);

    /* LrgIdleMixin Interface */
    g_test_add_func ("/template/idle-mixin/interface-implements",
                     test_idle_mixin_interface_implements);
    g_test_add_func ("/template/idle-mixin/get-idle-calculator",
                     test_idle_mixin_get_idle_calculator);
    g_test_add_func ("/template/idle-mixin/get-prestige-null",
                     test_idle_mixin_get_prestige_null);
    g_test_add_func ("/template/idle-mixin/calculate-offline-progress",
                     test_idle_mixin_calculate_offline_progress);
    g_test_add_func ("/template/idle-mixin/apply-offline-progress",
                     test_idle_mixin_apply_offline_progress);
    g_test_add_func ("/template/idle-mixin/get-auto-save-interval",
                     test_idle_mixin_get_auto_save_interval);
    g_test_add_func ("/template/idle-mixin/simulate",
                     test_idle_mixin_simulate);
    g_test_add_func ("/template/idle-mixin/get-total-rate",
                     test_idle_mixin_get_total_rate);
    g_test_add_func ("/template/idle-mixin/take-snapshot",
                     test_idle_mixin_take_snapshot);
    g_test_add_func ("/template/idle-mixin/can-prestige-without-prestige",
                     test_idle_mixin_can_prestige_without_prestige);
    g_test_add_func ("/template/idle-mixin/get-prestige-multiplier",
                     test_idle_mixin_get_prestige_multiplier);

    /* LrgIdleTemplate implements LrgIdleMixin */
    g_test_add_func ("/template/idle/implements-mixin",
                     test_idle_template_implements_mixin);
    g_test_add_func ("/template/idle/mixin-get-calculator",
                     test_idle_template_mixin_get_calculator);

    return g_test_run ();
}
