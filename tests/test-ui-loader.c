/* test-ui-loader.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Declarative YAML UI: building, components, repeat/foreach/include, hot reload.
 *
 * Headless: widgets are built and inspected without a window.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <sys/types.h>
#include <utime.h>
#include <string.h>
#include <libregnum.h>

/* ==========================================================================
 * A test widget with property types the stock widgets lack
 * ========================================================================== */

#define TEST_TYPE_GAUGE (test_gauge_get_type ())
G_DECLARE_FINAL_TYPE (TestGauge, test_gauge, TEST, GAUGE, LrgWidget)

struct _TestGauge
{
	LrgWidget      parent_instance;
	guint          mode;
	GStrv          tags;
	gint           seed;
	gint64         big;
	guchar         small;
	GrlVector2     point;
	LrgCornerRadii radii;
	gdouble        level;
};

enum
{
	GAUGE_PROP_0,
	GAUGE_PROP_MODE,
	GAUGE_PROP_TAGS,
	GAUGE_PROP_SEED,
	GAUGE_PROP_BIG,
	GAUGE_PROP_SMALL,
	GAUGE_PROP_POINT,
	GAUGE_PROP_RADII,
	GAUGE_PROP_LEVEL,
	GAUGE_N_PROPS
};

G_DEFINE_FINAL_TYPE (TestGauge, test_gauge, LRG_TYPE_WIDGET)

static void
test_gauge_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	TestGauge *self = TEST_GAUGE (object);

	switch (prop_id)
	{
	case GAUGE_PROP_MODE:
		self->mode = g_value_get_flags (value);
		break;
	case GAUGE_PROP_TAGS:
		g_strfreev (self->tags);
		self->tags = g_value_dup_boxed (value);
		break;
	case GAUGE_PROP_SEED:
		self->seed = g_value_get_int (value);
		break;
	case GAUGE_PROP_BIG:
		self->big = g_value_get_int64 (value);
		break;
	case GAUGE_PROP_SMALL:
		self->small = g_value_get_uchar (value);
		break;
	case GAUGE_PROP_POINT:
		if (g_value_get_boxed (value) != NULL)
			self->point = *(GrlVector2 *)g_value_get_boxed (value);
		break;
	case GAUGE_PROP_RADII:
		if (g_value_get_boxed (value) != NULL)
			self->radii = *(LrgCornerRadii *)g_value_get_boxed (value);
		break;
	case GAUGE_PROP_LEVEL:
		self->level = g_value_get_double (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
test_gauge_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	TestGauge *self = TEST_GAUGE (object);

	switch (prop_id)
	{
	case GAUGE_PROP_MODE:
		g_value_set_flags (value, self->mode);
		break;
	case GAUGE_PROP_TAGS:
		g_value_set_boxed (value, self->tags);
		break;
	case GAUGE_PROP_SEED:
		g_value_set_int (value, self->seed);
		break;
	case GAUGE_PROP_BIG:
		g_value_set_int64 (value, self->big);
		break;
	case GAUGE_PROP_SMALL:
		g_value_set_uchar (value, self->small);
		break;
	case GAUGE_PROP_POINT:
		g_value_set_boxed (value, &self->point);
		break;
	case GAUGE_PROP_RADII:
		g_value_set_boxed (value, &self->radii);
		break;
	case GAUGE_PROP_LEVEL:
		g_value_set_double (value, self->level);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
test_gauge_finalize (GObject *object)
{
	g_strfreev (TEST_GAUGE (object)->tags);
	G_OBJECT_CLASS (test_gauge_parent_class)->finalize (object);
}

static void
test_gauge_class_init (TestGaugeClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS (klass);
	GParamFlags rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS;

	oc->set_property = test_gauge_set_property;
	oc->get_property = test_gauge_get_property;
	oc->finalize = test_gauge_finalize;

	g_object_class_install_property (oc, GAUGE_PROP_MODE,
		g_param_spec_flags ("mode", NULL, NULL, LRG_TYPE_WIDGET_STATE, 0, rw));
	g_object_class_install_property (oc, GAUGE_PROP_TAGS,
		g_param_spec_boxed ("tags", NULL, NULL, G_TYPE_STRV, rw));
	g_object_class_install_property (oc, GAUGE_PROP_SEED,
		g_param_spec_int ("seed", NULL, NULL, 0, 1000, 0,
		                  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (oc, GAUGE_PROP_BIG,
		g_param_spec_int64 ("big", NULL, NULL, G_MININT64, G_MAXINT64, 0, rw));
	g_object_class_install_property (oc, GAUGE_PROP_SMALL,
		g_param_spec_uchar ("small", NULL, NULL, 0, 255, 0, rw));
	g_object_class_install_property (oc, GAUGE_PROP_POINT,
		g_param_spec_boxed ("point", NULL, NULL, GRL_TYPE_VECTOR2, rw));
	g_object_class_install_property (oc, GAUGE_PROP_RADII,
		g_param_spec_boxed ("radii", NULL, NULL, LRG_TYPE_CORNER_RADII, rw));
	g_object_class_install_property (oc, GAUGE_PROP_LEVEL,
		g_param_spec_double ("level", NULL, NULL, 0.0, 10.0, 0.0, rw));
}

static void
test_gauge_init (TestGauge *self)
{
}

/* An abstract widget type, which the loader must refuse to instantiate */
#define TEST_TYPE_ABSTRACT (test_abstract_get_type ())
G_DECLARE_DERIVABLE_TYPE (TestAbstract, test_abstract, TEST, ABSTRACT, LrgWidget)

struct _TestAbstractClass
{
	LrgWidgetClass parent_class;
};

G_DEFINE_ABSTRACT_TYPE (TestAbstract, test_abstract, LRG_TYPE_WIDGET)

static void
test_abstract_class_init (TestAbstractClass *klass)
{
}

static void
test_abstract_init (TestAbstract *self)
{
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgWidget *
load_ok (LrgUILoader *loader,
         const gchar *yaml)
{
	g_autoptr(GError) error = NULL;
	LrgWidget *w;

	w = lrg_ui_loader_load_from_data (loader, yaml, -1, &error);
	g_assert_no_error (error);
	g_assert_nonnull (w);
	return w;
}

/* Loads @yaml expecting an error with @code whose message starts with @prefix */
static gchar *
load_err (LrgUILoader *loader,
          const gchar *yaml,
          gint         code,
          const gchar *prefix)
{
	g_autoptr(GError) error = NULL;
	LrgWidget *w;

	w = lrg_ui_loader_load_from_data (loader, yaml, -1, &error);
	if (w != NULL)
		g_error ("expected an error loading \"%s\"", yaml);
	g_assert_error (error, LRG_UI_LOADER_ERROR, code);
	if (prefix != NULL && !g_str_has_prefix (error->message, prefix))
		g_error ("expected message starting with \"%s\", got \"%s\"", prefix, error->message);
	return g_strdup (error->message);
}

/* Text of a label found by id */
static const gchar *
label_text (LrgUILoader *loader,
            const gchar *id)
{
	LrgWidget *w = lrg_ui_loader_get_widget (loader, id);

	g_assert_nonnull (w);
	g_assert_true (LRG_IS_LABEL (w));
	return lrg_label_get_text (LRG_LABEL (w));
}

/* ==========================================================================
 * Value conversions
 * ========================================================================== */

static void
test_convert_scalars (void)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autoptr(LrgWidget) root = NULL;
	LrgWidget *w;

	root = load_ok (loader,
		"type: vbox\n"
		"children:\n"
		"  - {type: label, id: l, text: Hello, font-size: 21.5, alignment: center, visible: no}\n"
		"  - {type: slider, id: s, min: -5, max: 50.5, value: 12.25, step: 0.25}\n"
		"  - {type: text-input, id: t, max-length: 7, placeholder: 'Type here', password_mode: yes}\n"
		"  - {type: checkbox, id: c, checked: true, label: 'Check me'}\n"
		"  - {type: LrgButton, id: b, text: 42, halign: CENTER, valign: LRG_ALIGN_END, fill_weight: 2}\n");

	w = lrg_ui_loader_get_widget (loader, "l");
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (w)), ==, "Hello");
	g_assert_cmpfloat_with_epsilon (lrg_label_get_font_size (LRG_LABEL (w)), 21.5f, 1e-5);
	g_assert_cmpint (lrg_label_get_alignment (LRG_LABEL (w)), ==, LRG_TEXT_ALIGN_CENTER);
	g_assert_false (lrg_widget_get_visible (w));

	w = lrg_ui_loader_get_widget (loader, "s");
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_min (LRG_SLIDER (w)), -5.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_max (LRG_SLIDER (w)), 50.5, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (LRG_SLIDER (w)), 12.25, 1e-9);

	w = lrg_ui_loader_get_widget (loader, "t");
	g_assert_cmpuint (lrg_text_input_get_max_length (LRG_TEXT_INPUT (w)), ==, 7);
	g_assert_cmpstr (lrg_text_input_get_placeholder (LRG_TEXT_INPUT (w)), ==, "Type here");
	g_assert_true (lrg_text_input_get_password_mode (LRG_TEXT_INPUT (w)));

	w = lrg_ui_loader_get_widget (loader, "c");
	g_assert_true (lrg_checkbox_get_checked (LRG_CHECKBOX (w)));

	w = lrg_ui_loader_get_widget (loader, "b");
	g_assert_cmpstr (lrg_button_get_text (LRG_BUTTON (w)), ==, "42");
	g_assert_cmpint (lrg_widget_get_halign (w), ==, LRG_ALIGN_CENTER);
	g_assert_cmpint (lrg_widget_get_valign (w), ==, LRG_ALIGN_END);
	g_assert_cmpfloat (lrg_widget_get_fill_weight (w), ==, 2.0f);
}

static void
test_convert_color (void)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autoptr(LrgWidget) root = NULL;
	const GrlColor *c;

	root = load_ok (loader,
		"type: vbox\n"
		"children:\n"
		"  - {type: label, id: a, color: '#f80'}\n"
		"  - {type: label, id: b, color: '#11223344'}\n"
		"  - {type: label, id: c, color: [1, 2, 3]}\n"
		"  - {type: label, id: d, color: {r: 9, a: 10}}\n"
		"  - {type: label, id: e, color: transparent}\n"
		"  - {type: label, id: f, color: '#abcdef'}\n"
		"  - {type: label, id: g, color: '#1234'}\n");

	c = lrg_label_get_color (LRG_LABEL (lrg_ui_loader_get_widget (loader, "a")));
	g_assert_cmpuint (c->r, ==, 0xff);
	g_assert_cmpuint (c->g, ==, 0x88);
	g_assert_cmpuint (c->b, ==, 0x00);
	g_assert_cmpuint (c->a, ==, 0xff);
	c = lrg_label_get_color (LRG_LABEL (lrg_ui_loader_get_widget (loader, "b")));
	g_assert_cmpuint (c->r, ==, 0x11);
	g_assert_cmpuint (c->a, ==, 0x44);
	c = lrg_label_get_color (LRG_LABEL (lrg_ui_loader_get_widget (loader, "c")));
	g_assert_cmpuint (c->g, ==, 2);
	g_assert_cmpuint (c->a, ==, 255);
	c = lrg_label_get_color (LRG_LABEL (lrg_ui_loader_get_widget (loader, "d")));
	g_assert_cmpuint (c->r, ==, 9);
	g_assert_cmpuint (c->g, ==, 0);
	g_assert_cmpuint (c->a, ==, 10);
	c = lrg_label_get_color (LRG_LABEL (lrg_ui_loader_get_widget (loader, "e")));
	g_assert_cmpuint (c->a, ==, 0);
	c = lrg_label_get_color (LRG_LABEL (lrg_ui_loader_get_widget (loader, "f")));
	g_assert_cmpuint (c->b, ==, 0xef);
	c = lrg_label_get_color (LRG_LABEL (lrg_ui_loader_get_widget (loader, "g")));
	g_assert_cmpuint (c->a, ==, 0x44);

	g_free (load_err (loader, "{type: label, color: '#12'}", LRG_UI_LOADER_ERROR_INVALID_VALUE,
	                  "(root): invalid value for property 'color' of LrgLabel: invalid colour"));
}

static void
test_convert_insets (void)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autoptr(LrgWidget) root = NULL;
	LrgInsets m;

	root = load_ok (loader,
		"type: vbox\n"
		"children:\n"
		"  - {type: label, id: a, margin: 8}\n"
		"  - {type: label, id: b, margin: [1, 2, 3, 4]}\n"
		"  - {type: label, id: c, margin: [5, 6]}\n"
		"  - {type: label, id: d, margin: {left: 7, bottom: 9}}\n");

	lrg_widget_get_margin (lrg_ui_loader_get_widget (loader, "a"), &m);
	g_assert_cmpfloat (m.left, ==, 8.0f);
	g_assert_cmpfloat (m.bottom, ==, 8.0f);
	/* CSS order: top, right, bottom, left */
	lrg_widget_get_margin (lrg_ui_loader_get_widget (loader, "b"), &m);
	g_assert_cmpfloat (m.top, ==, 1.0f);
	g_assert_cmpfloat (m.right, ==, 2.0f);
	g_assert_cmpfloat (m.bottom, ==, 3.0f);
	g_assert_cmpfloat (m.left, ==, 4.0f);
	lrg_widget_get_margin (lrg_ui_loader_get_widget (loader, "c"), &m);
	g_assert_cmpfloat (m.top, ==, 5.0f);
	g_assert_cmpfloat (m.bottom, ==, 5.0f);
	g_assert_cmpfloat (m.left, ==, 6.0f);
	g_assert_cmpfloat (m.right, ==, 6.0f);
	lrg_widget_get_margin (lrg_ui_loader_get_widget (loader, "d"), &m);
	g_assert_cmpfloat (m.left, ==, 7.0f);
	g_assert_cmpfloat (m.top, ==, 0.0f);
	g_assert_cmpfloat (m.bottom, ==, 9.0f);

	g_free (load_err (loader, "{type: label, margin: [1, 2, 3]}", LRG_UI_LOADER_ERROR_INVALID_VALUE,
	                  "(root): invalid value for property 'margin'"));
	g_free (load_err (loader, "{type: label, margin: {middle: 3}}", LRG_UI_LOADER_ERROR_INVALID_VALUE,
	                  "(root): invalid value for property 'margin' of LrgLabel: unknown side 'middle'"));
}

static void
test_convert_custom (void)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autoptr(LrgRegistry) registry = lrg_registry_new ();
	g_autoptr(LrgWidget) root = NULL;
	TestGauge *g;

	g_type_ensure (TEST_TYPE_GAUGE);
	lrg_registry_register (registry, "gauge", TEST_TYPE_GAUGE);
	lrg_ui_loader_set_registry (loader, registry);
	g_assert_true (lrg_ui_loader_get_registry (loader) == registry);

	root = load_ok (loader,
		"type: vbox\n"
		"children:\n"
		"  - type: gauge\n"
		"    id: g1\n"
		"    mode: [checked, selected]\n"
		"    tags: [a, b, c]\n"
		"    seed: 77\n"
		"    big: 123456789012\n"
		"    small: 200\n"
		"    point: [1.5, -2]\n"
		"    radii: [1, 2, 3, 4]\n"
		"    level: 2.5\n"
		"  - {type: TestGauge, id: g2, mode: 'hovered | focused', tags: solo, point: {x: 3}, radii: 6}\n");

	g = TEST_GAUGE (lrg_ui_loader_get_widget (loader, "g1"));
	g_assert_cmpuint (g->mode, ==, LRG_WIDGET_STATE_CHECKED | LRG_WIDGET_STATE_SELECTED);
	g_assert_cmpuint (g_strv_length (g->tags), ==, 3);
	g_assert_cmpstr (g->tags[2], ==, "c");
	g_assert_cmpint (g->seed, ==, 77);
	g_assert_cmpint (g->big, ==, G_GINT64_CONSTANT (123456789012));
	g_assert_cmpuint (g->small, ==, 200);
	g_assert_cmpfloat (g->point.x, ==, 1.5f);
	g_assert_cmpfloat (g->point.y, ==, -2.0f);
	g_assert_cmpfloat (g->radii.top_left, ==, 1.0f);
	g_assert_cmpfloat (g->radii.bottom_left, ==, 4.0f);
	g_assert_cmpfloat_with_epsilon (g->level, 2.5, 1e-9);

	g = TEST_GAUGE (lrg_ui_loader_get_widget (loader, "g2"));
	g_assert_cmpuint (g->mode, ==, LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_FOCUSED);
	g_assert_cmpstr (g->tags[0], ==, "solo");
	g_assert_cmpfloat (g->point.x, ==, 3.0f);
	g_assert_cmpfloat (g->point.y, ==, 0.0f);
	g_assert_cmpfloat (g->radii.top_right, ==, 6.0f);

	/* Conversion errors */
	g_free (load_err (loader, "{type: gauge, level: 11}", LRG_UI_LOADER_ERROR_INVALID_VALUE,
	                  "(root): invalid value for property 'level' of TestGauge: 11 is out of range"));
	g_free (load_err (loader, "{type: gauge, small: 1.5}", LRG_UI_LOADER_ERROR_INVALID_VALUE,
	                  "(root): invalid value for property 'small' of TestGauge: expected an integer"));
	g_free (load_err (loader, "{type: gauge, mode: [sleepy]}", LRG_UI_LOADER_ERROR_INVALID_VALUE,
	                  "(root): invalid value for property 'mode' of TestGauge: unknown LrgWidgetState flag 'sleepy'"));
	g_free (load_err (loader, "{type: label, alignment: diagonal}", LRG_UI_LOADER_ERROR_INVALID_VALUE,
	                  "(root): invalid value for property 'alignment' of LrgLabel: unknown LrgTextAlignment value 'diagonal'"));
	g_free (load_err (loader, "{type: label, font-size: big}", LRG_UI_LOADER_ERROR_INVALID_VALUE,
	                  "(root): invalid value for property 'font-size' of LrgLabel: expected a number, got a scalar 'big'"));
	g_free (load_err (loader, "{type: label, visible: maybe}", LRG_UI_LOADER_ERROR_INVALID_VALUE,
	                  "(root): invalid value for property 'visible' of LrgLabel: expected true or false"));
	g_free (load_err (loader, "{type: label, text: [a]}", LRG_UI_LOADER_ERROR_INVALID_VALUE,
	                  "(root): invalid value for property 'text' of LrgLabel: expected text, got a list"));
	g_free (load_err (loader, "{type: label, state-flags: [hovered]}", LRG_UI_LOADER_ERROR_UNKNOWN_PROPERTY,
	                  "(root): property 'state-flags' of LrgLabel is read-only"));
}

/* look: material, glass and effect fields, as theme YAML has them */
static void
test_look_fx (void)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autoptr(LrgWidget) w = NULL;
	const LrgPartLook *look;

	w = load_ok (loader,
		"type: panel\n"
		"look:\n"
		"  box:\n"
		"    material: frosted\n"
		"    glass: {frost: 0.8, blur: 24, tint: '#20406080', rim: 0.5}\n"
		"    effect: aurora\n"
		"    effect-colors: ['#ff0000', '#00ff00']\n"
		"    effect-intensity: 0.6\n"
		"    hovered:\n"
		"      effect: none\n");
	look = lrg_widget_get_part_look (w, LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE);
	g_assert_nonnull (look);
	g_assert_true (look->set & LRG_PART_LOOK_MATERIAL);
	g_assert_cmpint (look->material, ==, LRG_PAINT_MATERIAL_FROSTED);
	g_assert_true (look->set & LRG_PART_LOOK_GLASS);
	g_assert_cmpfloat_with_epsilon (look->glass.frost, 0.8f, 0.0001f);
	g_assert_cmpfloat_with_epsilon (look->glass.blur_radius, 24.0f, 0.0001f);
	g_assert_cmpfloat_with_epsilon (look->glass.rim_light, 0.5f, 0.0001f);
	g_assert_cmpuint (look->glass.tint.g, ==, 0x40);
	g_assert_true (look->set & LRG_PART_LOOK_EFFECT);
	g_assert_cmpstr (g_quark_to_string (look->effect), ==, "aurora");
	g_assert_true (look->set & LRG_PART_LOOK_EFFECT_COLORS);
	g_assert_cmpuint (look->effect_params.color1.r, ==, 255);
	g_assert_cmpuint (look->effect_params.color2.g, ==, 255);
	g_assert_cmpuint (look->effect_params.color3.g, ==, 255);   /* repeats the last */
	g_assert_cmpfloat_with_epsilon (look->effect_params.intensity, 0.6f, 0.0001f);

	look = lrg_widget_get_part_look (w, LRG_UI_PART_BOX, LRG_WIDGET_STATE_HOVERED);
	g_assert_nonnull (look);
	g_assert_true (look->set & LRG_PART_LOOK_EFFECT);
	g_assert_cmpuint (look->effect, ==, 0);

	/* Validation matches theme YAML */
	g_free (load_err (loader, "{type: panel, look: {box: {material: marble}}}",
	                  LRG_UI_LOADER_ERROR_INVALID_VALUE, "(root): 'look'"));
	g_free (load_err (loader, "{type: panel, look: {box: {glass: {frost: 2}}}}",
	                  LRG_UI_LOADER_ERROR_INVALID_VALUE, "(root): 'look': glass.frost: 2 is outside 0..1"));
	g_free (load_err (loader, "{type: panel, look: {box: {glass: {shine: 1}}}}",
	                  LRG_UI_LOADER_ERROR_INVALID_VALUE, "(root): 'look': unknown glass field 'shine'"));
	g_free (load_err (loader, "{type: panel, look: {box: {effect: lava}}}",
	                  LRG_UI_LOADER_ERROR_INVALID_VALUE, "(root): 'look': unknown effect 'lava'"));
	g_free (load_err (loader, "{type: panel, look: {box: {effect-colors: [red, red, red, red]}}}",
	                  LRG_UI_LOADER_ERROR_INVALID_VALUE, "(root): 'look': 'effect-colors' takes one to three"));
}

/* ==========================================================================
 * Special keys
 * ========================================================================== */

static void
test_special_keys (void)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autoptr(LrgWidget) root = NULL;
	const LrgPartLook *look;
	LrgWidget *w;
	gfloat a;
	gfloat b;

	root = load_ok (loader,
		"type: LrgVBox\n"
		"id: root\n"
		"class: screen dark\n"
		"children:\n"
		"  - type: button\n"
		"    id: ok\n"
		"    class: [primary, wide]\n"
		"    min-size: [120, 40]\n"
		"    max-size: {width: 300}\n"
		"    appear: {kind: fly-up, delay: 0.1}\n"
		"    look:\n"
		"      box:\n"
		"        background: '#102030'\n"
		"        radius: 12\n"
		"        border: {color: '#ffffff', width: 2}\n"
		"        hovered:\n"
		"          background: '#405060'\n"
		"          glow: {color: '#ff00ff', size: 8}\n"
		"        checked+hovered: {opacity: 0.5}\n"
		"      label: {foreground: white}\n"
		"  - {type: label, id: fade, appear: pop}\n"
		"  - type: scroll-box\n"
		"    id: scroller\n"
		"    child: {type: VBox, id: inner, children: [{type: label, text: deep}]}\n"
		"  - type: tab-view\n"
		"    id: tabs\n"
		"    active-tab: 1\n"
		"    tabs:\n"
		"      - {label: One, content: {type: label, id: page1}}\n"
		"      - {label: Two, content: {type: label, id: page2}}\n"
		"      - {label: Never, if: false, content: {type: label}}\n"
		"  - {type: combo-box, id: combo, items: [Red, Green, Blue], selected: 2}\n"
		"  - {type: panel, id: card, fill-children: true, children: [{type: label}]}\n");

	g_assert_true (LRG_IS_VBOX (root));
	g_assert_true (lrg_ui_loader_get_widget (loader, "root") == root);
	g_assert_cmpstr (lrg_widget_get_name (root), ==, "root");
	g_assert_true (lrg_widget_has_class (root, "screen"));
	g_assert_true (lrg_widget_has_class (root, "dark"));
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (root)), ==, 6);

	w = lrg_ui_loader_get_widget (loader, "ok");
	g_assert_true (LRG_IS_BUTTON (w));
	g_assert_true (lrg_widget_find (root, "ok") == w);
	g_assert_true (lrg_widget_has_class (w, "primary"));
	g_assert_true (lrg_widget_has_class (w, "wide"));
	lrg_widget_get_min_size (w, &a, &b);
	g_assert_cmpfloat (a, ==, 120.0f);
	g_assert_cmpfloat (b, ==, 40.0f);
	lrg_widget_get_max_size (w, &a, &b);
	g_assert_cmpfloat (a, ==, 300.0f);
	g_assert_cmpfloat (b, ==, 0.0f);
	g_assert_cmpint (lrg_widget_get_appear (w), ==, LRG_APPEAR_FLY_UP);
	g_assert_cmpint (lrg_widget_get_appear (lrg_ui_loader_get_widget (loader, "fade")), ==, LRG_APPEAR_POP);

	look = lrg_widget_get_part_look (w, LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE);
	g_assert_nonnull (look);
	g_assert_true (look->set & LRG_PART_LOOK_BACKGROUND);
	g_assert_cmpuint (look->background.g, ==, 0x20);
	g_assert_true (look->set & LRG_PART_LOOK_RADII);
	g_assert_cmpfloat (look->radii.top_left, ==, 12.0f);
	g_assert_cmpfloat (look->border_width, ==, 2.0f);
	look = lrg_widget_get_part_look (w, LRG_UI_PART_BOX, LRG_WIDGET_STATE_HOVERED);
	g_assert_nonnull (look);
	g_assert_cmpuint (look->background.r, ==, 0x40);
	g_assert_cmpfloat (look->glow_size, ==, 8.0f);
	look = lrg_widget_get_part_look (w, LRG_UI_PART_BOX, LRG_WIDGET_STATE_CHECKED | LRG_WIDGET_STATE_HOVERED);
	g_assert_nonnull (look);
	g_assert_cmpfloat (look->opacity, ==, 0.5f);
	look = lrg_widget_get_part_look (w, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE);
	g_assert_nonnull (look);
	g_assert_cmpuint (look->foreground.r, ==, 255);

	w = lrg_ui_loader_get_widget (loader, "scroller");
	g_assert_true (LRG_IS_SCROLL_BOX (w));
	g_assert_true (lrg_scroll_box_get_child (LRG_SCROLL_BOX (w)) == lrg_ui_loader_get_widget (loader, "inner"));

	w = lrg_ui_loader_get_widget (loader, "tabs");
	g_assert_cmpuint (lrg_tab_view_get_tab_count (LRG_TAB_VIEW (w)), ==, 2);
	g_assert_cmpstr (lrg_tab_view_get_tab_label (LRG_TAB_VIEW (w), 1), ==, "Two");
	g_assert_true (lrg_tab_view_get_tab_content (LRG_TAB_VIEW (w), 0) == lrg_ui_loader_get_widget (loader, "page1"));
	g_assert_cmpuint (lrg_tab_view_get_active_tab (LRG_TAB_VIEW (w)), ==, 1);

	w = lrg_ui_loader_get_widget (loader, "combo");
	g_assert_cmpuint (lrg_combo_box_get_n_items (LRG_COMBO_BOX (w)), ==, 3);
	g_assert_cmpint (lrg_combo_box_get_selected (LRG_COMBO_BOX (w)), ==, 2);
	g_assert_cmpstr (lrg_combo_box_get_selected_text (LRG_COMBO_BOX (w)), ==, "Blue");

	g_assert_true (lrg_panel_get_fill_children (LRG_PANEL (lrg_ui_loader_get_widget (loader, "card"))));
	g_assert_null (lrg_ui_loader_get_widget (loader, "nope"));

	/* Misused special keys */
	g_free (load_err (loader, "{type: label, tabs: []}", LRG_UI_LOADER_ERROR_STRUCTURE,
	                  "(root): 'tabs' is only valid on LrgTabView"));
	g_free (load_err (loader, "{type: label, items: [a]}", LRG_UI_LOADER_ERROR_STRUCTURE,
	                  "(root): 'items' is only valid on LrgComboBox"));
	g_free (load_err (loader, "{type: label, children: [{type: label}]}", LRG_UI_LOADER_ERROR_STRUCTURE,
	                  "(root): LrgLabel cannot have children"));
	g_free (load_err (loader, "{type: label, look: {elbow: {}}}", LRG_UI_LOADER_ERROR_INVALID_VALUE,
	                  "(root): 'look': unknown part 'elbow'"));
	g_free (load_err (loader, "{type: label, look: {box: {sparkle: 1}}}", LRG_UI_LOADER_ERROR_INVALID_VALUE,
	                  "(root): 'look': unknown look field 'sparkle'"));
	g_free (load_err (loader, "{type: label, appear: wobble}", LRG_UI_LOADER_ERROR_INVALID_VALUE,
	                  "(root): 'appear': unknown LrgAppear value 'wobble'"));
	g_free (load_err (loader, "{type: label, min-size: 4}", LRG_UI_LOADER_ERROR_INVALID_VALUE,
	                  "(root): 'min-size': expected [width, height]"));
	g_free (load_err (loader, "{type: scroll-box, children: [{type: label}, {type: label}]}",
	                  LRG_UI_LOADER_ERROR_STRUCTURE, "(root): LrgScrollBox takes exactly one child, got 2"));
	g_free (load_err (loader, "{type: label, slots: {}}", LRG_UI_LOADER_ERROR_STRUCTURE,
	                  "(root): 'slots' is only valid where a component is used"));
}

/* ==========================================================================
 * Expressions
 * ========================================================================== */

static void
test_expressions (void)
{
	static const struct { const gchar *expr; const gchar *expect; } cases[] = {
		{ "${1 + 2 * 3}", "7" },
		{ "${(1 + 2) * 3}", "9" },
		{ "${10 / 4}", "2.5" },
		{ "${10 % 4}", "2" },
		{ "${-3 + +5}", "2" },
		{ "${2 < 3}", "true" },
		{ "${2 >= 3}", "false" },
		{ "${3 <= 3 && 4 > 3}", "true" },
		{ "${1 == 1.0}", "true" },
		{ "${'a' != 'b'}", "true" },
		{ "${!true || false}", "false" },
		{ "${n > 2 ? 'big' : 'small'}", "big" },
		{ "${n < 2 ? 'big' : n == 3 ? 'three' : 'other'}", "three" },
		{ "${'x' + 1 + 2}", "x12" },
		{ "${1 + 2 + 'x'}", "3x" },
		{ "Value: ${n * 10}%", "Value: 30%" },
		{ "${min(4, 2, 8)}/${max(4, 2, 8)}", "2/8" },
		{ "${len('héllo')} ${len(list)}", "5 3" },
		{ "${default(missing, 'fallback')}", "fallback" },
		{ "${default(name, 'fallback')}", "Ada" },
		{ "${upper(name)}${lower('XY')}", "ADAxy" },
		{ "${str(1.5) + str(true)}", "1.5true" },
		{ "${int(7.9)} ${float('2.5') * 2} ${round(2.5)} ${floor(-1.5)} ${ceil(1.2)} ${abs(-4)}", "7 5 3 -2 2 4" },
		{ "${list[1]} ${list[-1]} ${point.x}", "b c 4" },
		{ "${empty('')} ${empty(list)}", "true false" },
		{ "${[1, 2, 3][2]}", "3" },
		{ "${false && missing.x}", "false" },
		{ "${true || missing}", "true" },
		{ "$${not} ${'}'}", "${not} }" },
		{ "${flag ? 1 : 0}", "1" },
		{ "${0.1 + 0.2}", "0.3" },
	};
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	GValue v = G_VALUE_INIT;
	guint i;

	g_value_init (&v, G_TYPE_INT);
	g_value_set_int (&v, 3);
	lrg_ui_loader_set_variable (loader, "n", &v);
	g_value_unset (&v);
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "Ada");
	lrg_ui_loader_set_variable (loader, "name", &v);
	g_value_unset (&v);
	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, TRUE);
	lrg_ui_loader_set_variable (loader, "flag", &v);
	g_value_unset (&v);

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autofree gchar *yaml = NULL;
		g_autoptr(LrgWidget) root = NULL;
		g_autofree gchar *quoted = NULL;

		quoted = g_strescape (cases[i].expr, "\303\251");
		yaml = g_strdup_printf ("variables:\n"
		                        "  list: [a, b, c]\n"
		                        "  point: {x: 4, y: 5}\n"
		                        "root: {type: label, id: out, text: \"%s\"}\n", quoted);
		root = load_ok (loader, yaml);
		if (g_strcmp0 (label_text (loader, "out"), cases[i].expect) != 0)
			g_error ("%s: expected \"%s\", got \"%s\"", cases[i].expr, cases[i].expect,
			         label_text (loader, "out"));
	}

	/* Removing a variable */
	lrg_ui_loader_set_variable (loader, "flag", NULL);

	/* Errors carry the path, the expression and the position */
	{
		g_autofree gchar *msg = NULL;

		msg = load_err (loader, "type: vbox\nchildren:\n  - {type: label, text: '${flag}'}\n",
		                LRG_UI_LOADER_ERROR_EXPRESSION,
		                "children[0]: in '${flag}': unknown variable 'flag' (at line 3, column");
	}
	g_free (load_err (loader, "{type: label, text: '${1 +}'}", LRG_UI_LOADER_ERROR_EXPRESSION,
	                  "(root): in '${1 +}': unexpected end of expression"));
	g_free (load_err (loader, "{type: label, text: '${1 / 0}'}", LRG_UI_LOADER_ERROR_EXPRESSION,
	                  "(root): in '${1 / 0}': division by zero"));
	g_free (load_err (loader, "{type: label, text: '${2 - \"a\"}'}", LRG_UI_LOADER_ERROR_EXPRESSION,
	                  "(root): in '${2 - \"a\"}': operator '-' needs numbers"));
	g_free (load_err (loader, "{type: label, text: '${nope(1)}'}", LRG_UI_LOADER_ERROR_EXPRESSION,
	                  "(root): in '${nope(1)}': unknown function 'nope'"));
	g_free (load_err (loader, "{type: label, text: '${1 = 1}'}", LRG_UI_LOADER_ERROR_EXPRESSION,
	                  "(root): in '${1 = 1}': '=' is not an operator"));
	g_free (load_err (loader, "{type: label, text: 'a ${[1]} b'}", LRG_UI_LOADER_ERROR_EXPRESSION,
	                  "(root): in 'a ${[1]} b': cannot insert a list into text"));

	/* A whole-value expression keeps its type: a number for a float */
	{
		g_autoptr(LrgWidget) root = NULL;

		root = load_ok (loader, "{type: label, id: x, font-size: '${n * 4}'}");
		g_assert_cmpfloat (lrg_label_get_font_size (LRG_LABEL (root)), ==, 12.0f);
	}
}

/* ==========================================================================
 * Components
 * ========================================================================== */

static const gchar *COMPONENTS_YAML =
	"variables:\n"
	"  unit: px\n"
	"components:\n"
	"  Field:\n"
	"    params: {label: Name, hint: '', label-width: 90, wide: false}\n"
	"    body:\n"
	"      type: hbox\n"
	"      class: field\n"
	"      spacing: 8\n"
	"      children:\n"
	"        - {type: label, id: 'lbl-${instance}', text: '${label}:', min-size: ['${wide ? 140 : label_width}', 0]}\n"
	"        - {type: text-input, placeholder: '${default(hint, label)}', fill-weight: 1}\n"
	"        - slot: true\n"
	"  Card:\n"
	"    params: {title: Card}\n"
	"    body:\n"
	"      type: panel\n"
	"      children:\n"
	"        - type: vbox\n"
	"          children:\n"
	"            - {type: label, text: '${title} (${unit})'}\n"
	"            - slot: true\n"
	"              children: [{type: label, id: empty-note, text: Nothing here}]\n"
	"            - type: hbox\n"
	"              id: footer\n"
	"              children:\n"
	"                - slot: footer\n"
	"  Plain:\n"
	"    body: {type: vbox}\n"
	"root:\n"
	"  type: vbox\n"
	"  children:\n"
	"    - type: Field\n"
	"      id: f1\n"
	"      label: Email\n"
	"      hint: you@example.com\n"
	"      class: highlighted\n"
	"      children: [{type: button, id: show, text: Show}]\n"
	"    - {type: Field, id: f2, wide: true}\n"
	"    - {type: Field, id: f3, label-width: 55}\n"
	"    - type: Card\n"
	"      id: card1\n"
	"      title: Inventory\n"
	"      children: [{type: label, id: item1}, {type: label, id: item2}]\n"
	"      slots:\n"
	"        footer: [{type: button, id: close, text: Close}]\n"
	"    - {type: Card, id: card2}\n"
	"    - {type: Plain, id: plain, children: [{type: label, id: appended}]}\n";

static void
test_components (void)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autoptr(LrgWidget) root = NULL;
	LrgWidget *f1;
	LrgWidget *w;
	gfloat mw;
	gfloat mh;

	root = load_ok (loader, COMPONENTS_YAML);

	/* Use-site id and class land on the body's root (classes merged) */
	f1 = lrg_ui_loader_get_widget (loader, "f1");
	g_assert_true (LRG_IS_HBOX (f1));
	g_assert_true (lrg_widget_has_class (f1, "field"));
	g_assert_true (lrg_widget_has_class (f1, "highlighted"));
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (f1)), ==, 3);

	/* Params and expressions */
	w = lrg_container_get_child (LRG_CONTAINER (f1), 0);
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (w)), ==, "Email:");
	lrg_widget_get_min_size (w, &mw, &mh);
	g_assert_cmpfloat (mw, ==, 90.0f);
	w = lrg_container_get_child (LRG_CONTAINER (f1), 1);
	g_assert_cmpstr (lrg_text_input_get_placeholder (LRG_TEXT_INPUT (w)), ==, "you@example.com");
	/* Default slot */
	g_assert_true (lrg_container_get_child (LRG_CONTAINER (f1), 2) == lrg_ui_loader_get_widget (loader, "show"));

	/* Defaults, the dash alias and the empty slot */
	w = lrg_container_get_child (LRG_CONTAINER (lrg_ui_loader_get_widget (loader, "f2")), 0);
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (w)), ==, "Name:");
	lrg_widget_get_min_size (w, &mw, &mh);
	g_assert_cmpfloat (mw, ==, 140.0f);
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (lrg_ui_loader_get_widget (loader, "f2"))), ==, 2);
	w = lrg_container_get_child (LRG_CONTAINER (lrg_ui_loader_get_widget (loader, "f2")), 1);
	g_assert_cmpstr (lrg_text_input_get_placeholder (LRG_TEXT_INPUT (w)), ==, "Name");
	w = lrg_container_get_child (LRG_CONTAINER (lrg_ui_loader_get_widget (loader, "f3")), 0);
	lrg_widget_get_min_size (w, &mw, &mh);
	g_assert_cmpfloat (mw, ==, 55.0f);

	/* Each instance gets its own number */
	g_assert_nonnull (lrg_ui_loader_get_widget (loader, "lbl-0"));
	g_assert_nonnull (lrg_ui_loader_get_widget (loader, "lbl-1"));
	g_assert_nonnull (lrg_ui_loader_get_widget (loader, "lbl-2"));

	/* Named slot, default slot filled, document variable in the body */
	w = lrg_ui_loader_get_widget (loader, "card1");
	g_assert_true (LRG_IS_PANEL (w));
	w = lrg_container_get_child (LRG_CONTAINER (w), 0);
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (w)), ==, 4);
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (lrg_container_get_child (LRG_CONTAINER (w), 0))), ==,
	                 "Inventory (px)");
	g_assert_true (lrg_container_get_child (LRG_CONTAINER (w), 1) == lrg_ui_loader_get_widget (loader, "item1"));
	g_assert_true (lrg_widget_get_parent (lrg_ui_loader_get_widget (loader, "close")) ==
	               LRG_CONTAINER (lrg_widget_find (lrg_ui_loader_get_widget (loader, "card1"), "footer")));

	/* Slot fallback content */
	g_assert_nonnull (lrg_widget_find (lrg_ui_loader_get_widget (loader, "card2"), "empty-note"));

	/* No slot: children are appended to a container root */
	g_assert_true (lrg_container_get_child (LRG_CONTAINER (lrg_ui_loader_get_widget (loader, "plain")), 0) ==
	               lrg_ui_loader_get_widget (loader, "appended"));

	/* Errors */
	g_free (load_err (loader,
		"components:\n"
		"  Loop: {body: {type: vbox, children: [{type: Loop}]}}\n"
		"root: {type: Loop}\n",
		LRG_UI_LOADER_ERROR_COMPONENT, "<Loop>.children[0]: component 'Loop' uses itself (Loop -> Loop)"));
	g_free (load_err (loader,
		"components:\n"
		"  Box: {body: {type: vbox, children: [{slot: true}]}}\n"
		"root: {type: Box, slots: {side: [{type: label}]}}\n",
		LRG_UI_LOADER_ERROR_COMPONENT, "(root): component 'Box' has no slot 'side'"));
	g_free (load_err (loader,
		"components:\n"
		"  Txt: {params: {t: x}, body: {type: label, text: '${t}'}}\n"
		"root: {type: Txt, colour: red}\n",
		LRG_UI_LOADER_ERROR_UNKNOWN_PROPERTY,
		"(root): 'colour' is neither a parameter of component 'Txt' nor a property of its root LrgLabel"));
	g_free (load_err (loader,
		"components:\n"
		"  Txt: {body: {type: label}}\n"
		"root: {type: Txt, children: [{type: label}]}\n",
		LRG_UI_LOADER_ERROR_COMPONENT, "(root): component 'Txt' has no slot for children"));
	g_free (load_err (loader, "{type: vbox, children: [{slot: true}]}", LRG_UI_LOADER_ERROR_COMPONENT,
	                  "children[0]: 'slot' is only valid inside a component body"));
	g_free (load_err (loader, "components: {Bad: {params: {}}}\nroot: {type: vbox}\n",
	                  LRG_UI_LOADER_ERROR_COMPONENT, "components.Bad: a component needs a 'body' node"));
	g_free (load_err (loader,
		"components:\n"
		"  Two: {body: {repeat: 2, body: {type: label}}}\n"
		"root: {type: Two}\n",
		LRG_UI_LOADER_ERROR_COMPONENT, "(root): the body of component 'Two' must give exactly one widget, got 2"));
}

/* ==========================================================================
 * repeat / for-each / if / include
 * ========================================================================== */

static void
test_structural (void)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autoptr(LrgWidget) root = NULL;
	LrgWidget *w;

	root = load_ok (loader,
		"variables:\n"
		"  people: [{name: Ann, age: 31}, {name: Bob, age: 17}, {name: Cy, age: 45}]\n"
		"  stats: {hp: 10, mp: 4}\n"
		"root:\n"
		"  type: vbox\n"
		"  children:\n"
		"    - type: hbox\n"
		"      id: rep\n"
		"      children:\n"
		"        - repeat: {count: 3, var: k, body: {type: label, id: 'r${k}', text: '${k * k}'}}\n"
		"        - repeat: '${1 + 1}'\n"
		"          body: [{type: label, text: 'a${i}'}, {type: label, text: 'b${i}'}]\n"
		"    - type: vbox\n"
		"      id: each\n"
		"      children:\n"
		"        - for-each:\n"
		"            items: '${people}'\n"
		"            var: p\n"
		"            index: n\n"
		"            body: {type: label, if: '${p.age >= 18}', id: 'p${n}', text: '${n}:${p.name}'}\n"
		"    - type: vbox\n"
		"      id: words\n"
		"      children:\n"
		"        - for-each: 'x|y|z'\n"
		"          body: {type: label, text: '${index}${item}'}\n"
		"        - for-each: '${stats}'\n"
		"          var: v\n"
		"          index: key\n"
		"          body: {type: label, text: '${key}=${v}'}\n"
		"        - for-each: [10, 20]\n"
		"          body: {type: label, text: '${item + 1}'}\n"
		"    - {type: label, id: gone, if: false}\n"
		"    - {type: label, id: kept, if: '${len(people) == 3}'}\n");

	w = lrg_ui_loader_get_widget (loader, "rep");
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (w)), ==, 7);
	g_assert_cmpstr (label_text (loader, "r2"), ==, "4");
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (lrg_container_get_child (LRG_CONTAINER (w), 3))), ==, "a0");
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (lrg_container_get_child (LRG_CONTAINER (w), 6))), ==, "b1");

	w = lrg_ui_loader_get_widget (loader, "each");
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (w)), ==, 2);
	g_assert_cmpstr (label_text (loader, "p0"), ==, "0:Ann");
	g_assert_cmpstr (label_text (loader, "p2"), ==, "2:Cy");
	g_assert_null (lrg_ui_loader_get_widget (loader, "p1"));

	w = lrg_ui_loader_get_widget (loader, "words");
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (w)), ==, 7);
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (lrg_container_get_child (LRG_CONTAINER (w), 2))), ==, "2z");
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (lrg_container_get_child (LRG_CONTAINER (w), 3))), ==, "hp=10");
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (lrg_container_get_child (LRG_CONTAINER (w), 4))), ==, "mp=4");
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (lrg_container_get_child (LRG_CONTAINER (w), 6))), ==, "21");

	g_assert_null (lrg_ui_loader_get_widget (loader, "gone"));
	g_assert_nonnull (lrg_ui_loader_get_widget (loader, "kept"));

	g_free (load_err (loader, "{type: vbox, children: [{repeat: -1, body: {type: label}}]}",
	                  LRG_UI_LOADER_ERROR_INVALID_VALUE, "children[0]: repeat count must be an integer"));
	g_free (load_err (loader, "{type: vbox, children: [{repeat: {count: 2}}]}",
	                  LRG_UI_LOADER_ERROR_STRUCTURE, "children[0]: repeat needs a 'body'"));
	g_free (load_err (loader, "{type: vbox, children: [{repeat: 2, body: {type: label}, colour: 1}]}",
	                  LRG_UI_LOADER_ERROR_STRUCTURE, "children[0]: unknown key 'colour' in a repeat node"));
	g_free (load_err (loader, "{type: vbox, children: [{repeat: 2, body: {type: label, colour: 1}}]}",
	                  LRG_UI_LOADER_ERROR_UNKNOWN_PROPERTY,
	                  "children[0].repeat[0].body: unknown property 'colour' on LrgLabel"));
	g_free (load_err (loader, "{type: vbox, children: [{text: hi}]}", LRG_UI_LOADER_ERROR_STRUCTURE,
	                  "children[0]: a node needs 'type'"));
}

static gchar *
write_temp (const gchar *dir,
            const gchar *name,
            const gchar *text)
{
	g_autoptr(GError) error = NULL;
	gchar *path = g_build_filename (dir, name, NULL);

	g_file_set_contents (path, text, -1, &error);
	g_assert_no_error (error);
	return path;
}

static void
test_include (void)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autoptr(LrgWidget) root = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *main_path = NULL;
	g_autofree gchar *p1 = NULL;
	g_autofree gchar *p2 = NULL;
	g_autofree gchar *p3 = NULL;
	g_autofree gchar *p4 = NULL;
	g_autofree gchar *p5 = NULL;
	LrgWidget *w;

	dir = g_dir_make_tmp ("lrg-ui-loader-XXXXXX", &error);
	g_assert_no_error (error);

	p1 = write_temp (dir, "parts.yaml",
		"components:\n"
		"  Badge: {params: {text: '?'}, body: {type: label, class: badge, text: '[${text}]'}}\n");
	p2 = write_temp (dir, "row.yaml",
		"type: hbox\n"
		"id: 'row-${who}'\n"
		"children: [{type: Badge, text: '${who}'}]\n");
	{
		g_autofree gchar *sub = g_build_filename (dir, "sub", NULL);

		g_mkdir (sub, 0700);
	}
	p3 = write_temp (dir, "sub/deep.yaml", "include: ../row.yaml\nwith: {who: deep}\n");
	main_path = write_temp (dir, "main.yaml",
		"type: vbox\n"
		"children:\n"
		"  - include: parts.yaml\n"
		"  - {include: row.yaml, with: {who: ann}}\n"
		"  - {include: row.yaml, with: {who: bob}}\n"
		"  - include: sub/deep.yaml\n"
		"  - {type: Badge, id: direct, text: hi}\n");

	root = lrg_ui_loader_load_from_file (loader, main_path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (root);
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (root)), ==, 4);
	w = lrg_ui_loader_get_widget (loader, "row-bob");
	g_assert_nonnull (w);
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (lrg_container_get_child (LRG_CONTAINER (w), 0))), ==, "[bob]");
	g_assert_nonnull (lrg_ui_loader_get_widget (loader, "row-deep"));
	g_assert_cmpstr (label_text (loader, "direct"), ==, "[hi]");
	g_assert_true (lrg_widget_has_class (lrg_ui_loader_get_widget (loader, "direct"), "badge"));

	/* Cycles are detected and named */
	p4 = write_temp (dir, "a.yaml", "type: vbox\nchildren: [{include: b.yaml}]\n");
	p5 = write_temp (dir, "b.yaml", "type: vbox\nchildren: [{include: a.yaml}]\n");
	{
		g_autoptr(LrgWidget) none = lrg_ui_loader_load_from_file (loader, p4, &error);

		g_assert_null (none);
		g_assert_error (error, LRG_UI_LOADER_ERROR, LRG_UI_LOADER_ERROR_INCLUDE_CYCLE);
		g_assert_true (g_str_has_prefix (error->message,
		               "children[0]{b.yaml}.children[0]: include cycle: a.yaml -> b.yaml -> a.yaml"));
		g_clear_error (&error);
	}

	/* Missing include */
	{
		g_autofree gchar *bad = write_temp (dir, "bad.yaml", "type: vbox\nchildren: [{include: nope.yaml}]\n");
		g_autoptr(LrgWidget) none = lrg_ui_loader_load_from_file (loader, bad, &error);

		g_assert_null (none);
		g_assert_error (error, LRG_UI_LOADER_ERROR, LRG_UI_LOADER_ERROR_IO);
		g_assert_true (g_str_has_prefix (error->message, "children[0]: cannot read"));
		g_assert_nonnull (strstr (error->message, "bad.yaml:2:"));
		g_clear_error (&error);
	}
	{
		g_autoptr(LrgWidget) none = lrg_ui_loader_load_from_file (loader, "/nonexistent/x.yaml", &error);

		g_assert_null (none);
		g_assert_error (error, LRG_UI_LOADER_ERROR, LRG_UI_LOADER_ERROR_IO);
		g_clear_error (&error);
	}
}

/* ==========================================================================
 * Handlers
 * ========================================================================== */

static void
on_counted (LrgButton *button,
            gpointer   user_data)
{
	gint *counter = user_data;

	g_assert_true (LRG_IS_BUTTON (button));
	(*counter)++;
}

static void
on_swapped (gpointer   user_data,
            LrgButton *button)
{
	gint *counter = user_data;

	g_assert_true (LRG_IS_BUTTON (button));
	*counter += 100;
}

static void
test_handlers (void)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autoptr(LrgWidget) root = NULL;
	gint count = 0;

	lrg_ui_loader_add_handler (loader, "count", G_CALLBACK (on_counted), &count);
	lrg_ui_loader_add_handler (loader, "swapped", G_CALLBACK (on_swapped), &count);

	root = load_ok (loader,
		"type: vbox\n"
		"children:\n"
		"  - {type: button, id: b1, on: {clicked: count}}\n"
		"  - {type: button, id: b2, on: {clicked: {handler: swapped, swapped: true, after: true}}}\n"
		"  - {type: slider, id: s, on: {'notify::value': count}}\n");

	g_signal_emit_by_name (lrg_ui_loader_get_widget (loader, "b1"), "clicked");
	g_assert_cmpint (count, ==, 1);
	g_signal_emit_by_name (lrg_ui_loader_get_widget (loader, "b2"), "clicked");
	g_assert_cmpint (count, ==, 101);

	/* Setting up the widget never fires its handlers */
	g_assert_cmpint (count, ==, 101);

	g_free (load_err (loader, "{type: button, on: {clicked: nobody}}", LRG_UI_LOADER_ERROR_HANDLER,
	                  "(root): unknown handler 'nobody' for signal 'clicked'"));
	g_free (load_err (loader, "{type: button, on: {exploded: count}}", LRG_UI_LOADER_ERROR_HANDLER,
	                  "(root): LrgButton has no signal 'exploded'"));
	g_free (load_err (loader, "{type: button, on: {clicked: [count]}}", LRG_UI_LOADER_ERROR_HANDLER,
	                  "(root): signal 'clicked': expected a handler name"));

	/* Symbol lookup: a public libregnum function is a global symbol */
	g_free (load_err (loader, "{type: button, on: {clicked: lrg_widget_queue_layout}}",
	                  LRG_UI_LOADER_ERROR_HANDLER, "(root): unknown handler 'lrg_widget_queue_layout'"));
	lrg_ui_loader_set_use_symbols (loader, TRUE);
	g_assert_true (lrg_ui_loader_get_use_symbols (loader));
	{
		g_autoptr(LrgWidget) b = load_ok (loader, "{type: button, on: {clicked: lrg_widget_queue_layout}}");

		lrg_widget_layout (b);
		g_assert_false (lrg_widget_get_needs_layout (b));
		g_signal_emit_by_name (b, "clicked");
		g_assert_true (lrg_widget_get_needs_layout (b));
	}
	g_free (load_err (loader, "{type: button, on: {clicked: no_such_symbol_xyz}}", LRG_UI_LOADER_ERROR_HANDLER,
	                  "(root): unknown handler 'no_such_symbol_xyz' for signal 'clicked' (not registered and no such symbol)"));
}

/* ==========================================================================
 * Errors
 * ========================================================================== */

static void
test_error_paths (void)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autofree gchar *msg = NULL;

	msg = load_err (loader,
		"type: vbox\n"
		"children:\n"
		"  - {type: label}\n"
		"  - {type: label}\n"
		"  - type: hbox\n"
		"    children:\n"
		"      - type: label\n"
		"        colour: red\n",
		LRG_UI_LOADER_ERROR_UNKNOWN_PROPERTY,
		"children[2].children[0]: unknown property 'colour' on LrgLabel");
	g_assert_nonnull (strstr (msg, "(at line 8, column 17)"));

	g_free (load_err (loader, "type: vbox\nchildren:\n  - type: Frobnicator\n",
	                  LRG_UI_LOADER_ERROR_UNKNOWN_TYPE,
	                  "children[0]: unknown widget type or component 'Frobnicator' (at line 3, column 11)"));
	g_type_ensure (TEST_TYPE_ABSTRACT);
	g_free (load_err (loader, "type: TestAbstract\n", LRG_UI_LOADER_ERROR_UNKNOWN_TYPE,
	                  "(root): 'TestAbstract' is abstract"));
	g_free (load_err (loader, "type: GObject\n", LRG_UI_LOADER_ERROR_UNKNOWN_TYPE,
	                  "(root): 'GObject' (GObject) is not a widget type"));
	g_free (load_err (loader, "type: vbox\nchildren: [\n", LRG_UI_LOADER_ERROR_PARSE,
	                  "(document): YAML syntax error:"));
	g_free (load_err (loader, "type: label\ntext: a\ntext: b\n", LRG_UI_LOADER_ERROR_PARSE,
	                  "(document): duplicate key 'text' (at line 3, column 1)"));
	g_free (load_err (loader, "", LRG_UI_LOADER_ERROR_STRUCTURE, "(document): the document is empty"));
	g_free (load_err (loader, "components: {}\n", LRG_UI_LOADER_ERROR_STRUCTURE,
	                  "(document): the document has no root widget"));
	g_free (load_err (loader, "- {type: label}\n- {type: label}\n", LRG_UI_LOADER_ERROR_STRUCTURE,
	                  "(document): the document gives 2 root widgets"));
	g_free (load_err (loader, "flavour: lime\n", LRG_UI_LOADER_ERROR_STRUCTURE,
	                  "(root): unknown top-level key 'flavour'"));
	g_free (load_err (loader, "42\n", LRG_UI_LOADER_ERROR_STRUCTURE,
	                  "(root): a document must be a mapping or a list"));
}

/* ==========================================================================
 * Hot reload
 * ========================================================================== */

/* Writes @text to @path and gives it modification time @stamp (seconds),
 * so successive writes are told apart regardless of the filesystem's
 * timestamp granularity */
static void
write_stamped (const gchar *path,
               const gchar *text,
               gint64       stamp)
{
	g_autoptr(GError) error = NULL;
	struct utimbuf times;

	g_file_set_contents (path, text, -1, &error);
	g_assert_no_error (error);
	times.actime = (time_t)stamp;
	times.modtime = (time_t)stamp;
	g_assert_cmpint (g_utime (path, &times), ==, 0);
}

typedef struct
{
	guint   reloaded;
	guint   failed;
	gchar  *last_error;
} ReloadLog;

static void
on_reloaded (LrgUILoader *loader,
             const gchar *path,
             LrgWidget   *root,
             gpointer     user_data)
{
	ReloadLog *log = user_data;

	g_assert_true (LRG_IS_WIDGET (root));
	log->reloaded++;
}

static void
on_reload_failed (LrgUILoader  *loader,
                  const gchar  *path,
                  const GError *error,
                  gpointer      user_data)
{
	ReloadLog *log = user_data;

	log->failed++;
	g_free (log->last_error);
	log->last_error = g_strdup (error->message);
}

static const gchar *RELOAD_V1 =
	"type: vbox\n"
	"id: root\n"
	"children:\n"
	"  - {type: label, id: title, text: Hello, font-size: 20}\n"
	"  - {type: text-input, id: name, placeholder: Name}\n"
	"  - {type: slider, id: vol, min: 0, max: 100, value: 50}\n"
	"  - {type: label, text: anonymous}\n"
	"  - {type: button, id: go, text: Go, on: {clicked: count}}\n"
	"  - {type: label, id: doomed, text: bye}\n"
	"  - {type: combo-box, id: pick, items: [a, b, c], selected: 0}\n";

/* title text changed, slider max changed (value not), doomed removed,
 * new label added, go moved before name, anonymous label kept */
static const gchar *RELOAD_V2 =
	"type: vbox\n"
	"id: root\n"
	"children:\n"
	"  - {type: label, id: title, text: Welcome, font-size: 20}\n"
	"  - {type: button, id: go, text: Go, on: {clicked: count}}\n"
	"  - {type: text-input, id: name, placeholder: Name}\n"
	"  - {type: slider, id: vol, min: 0, max: 200, value: 50}\n"
	"  - {type: label, text: anonymous}\n"
	"  - {type: label, id: fresh, text: new}\n"
	"  - {type: combo-box, id: pick, items: [x, b, c, d], selected: 0}\n";

static void
test_reload_diff (void)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autoptr(LrgVBox) host = lrg_vbox_new ();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	ReloadLog log = { 0 };
	LrgWidget *root;
	LrgWidget *title;
	LrgWidget *name;
	LrgWidget *vol;
	LrgWidget *anon;
	LrgWidget *go;
	LrgWidget *pick;
	GWeakRef doomed;
	gint count = 0;

	lrg_ui_loader_add_handler (loader, "count", G_CALLBACK (on_counted), &count);
	g_signal_connect (loader, "reloaded", G_CALLBACK (on_reloaded), &log);
	g_signal_connect (loader, "reload-failed", G_CALLBACK (on_reload_failed), &log);

	dir = g_dir_make_tmp ("lrg-ui-reload-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "screen.yaml", NULL);
	write_stamped (path, RELOAD_V1, 1000000000);

	root = lrg_ui_loader_watch_file (loader, path, LRG_CONTAINER (host), &error);
	g_assert_no_error (error);
	g_assert_nonnull (root);
	g_assert_true (lrg_widget_get_parent (root) == LRG_CONTAINER (host));

	title = lrg_ui_loader_get_widget (loader, "title");
	name = lrg_ui_loader_get_widget (loader, "name");
	vol = lrg_ui_loader_get_widget (loader, "vol");
	go = lrg_ui_loader_get_widget (loader, "go");
	pick = lrg_ui_loader_get_widget (loader, "pick");
	anon = lrg_container_get_child (LRG_CONTAINER (root), 3);
	g_weak_ref_init (&doomed, lrg_ui_loader_get_widget (loader, "doomed"));

	/* Nothing changed: poll does nothing */
	g_assert_false (lrg_ui_loader_poll (loader));
	g_assert_cmpuint (log.reloaded, ==, 0);

	/* The user interacts: type a name, move the slider, pick an item */
	lrg_text_input_set_text (LRG_TEXT_INPUT (name), "Zach");
	lrg_slider_set_value (LRG_SLIDER (vol), 73.0);
	lrg_combo_box_set_selected (LRG_COMBO_BOX (pick), 1);

	write_stamped (path, RELOAD_V2, 1000000010);
	g_assert_true (lrg_ui_loader_poll (loader));
	g_assert_cmpuint (log.reloaded, ==, 1);
	g_assert_cmpuint (log.failed, ==, 0);

	/* Identity kept */
	g_assert_true (lrg_ui_loader_get_widget (loader, "root") == root);
	g_assert_true (lrg_ui_loader_get_widget (loader, "title") == title);
	g_assert_true (lrg_ui_loader_get_widget (loader, "name") == name);
	g_assert_true (lrg_ui_loader_get_widget (loader, "vol") == vol);
	g_assert_true (lrg_ui_loader_get_widget (loader, "go") == go);
	g_assert_true (lrg_ui_loader_get_widget (loader, "pick") == pick);

	/* Changed property applied, untouched state kept */
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (title)), ==, "Welcome");
	g_assert_cmpstr (lrg_text_input_get_text (LRG_TEXT_INPUT (name)), ==, "Zach");
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_max (LRG_SLIDER (vol)), 200.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (LRG_SLIDER (vol)), 73.0, 1e-9);
	/* New items; the selection follows its text ("b") */
	g_assert_cmpuint (lrg_combo_box_get_n_items (LRG_COMBO_BOX (pick)), ==, 4);
	g_assert_cmpstr (lrg_combo_box_get_selected_text (LRG_COMBO_BOX (pick)), ==, "b");

	/* Order follows the document, id-less widget matched by type/rank */
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (root)), ==, 7);
	g_assert_true (lrg_container_get_child (LRG_CONTAINER (root), 0) == title);
	g_assert_true (lrg_container_get_child (LRG_CONTAINER (root), 1) == go);
	g_assert_true (lrg_container_get_child (LRG_CONTAINER (root), 2) == name);
	g_assert_true (lrg_container_get_child (LRG_CONTAINER (root), 3) == vol);
	g_assert_true (lrg_container_get_child (LRG_CONTAINER (root), 4) == anon);
	g_assert_cmpstr (label_text (loader, "fresh"), ==, "new");
	g_assert_true (lrg_container_get_child (LRG_CONTAINER (root), 5) == lrg_ui_loader_get_widget (loader, "fresh"));

	/* Removed widget destroyed */
	g_assert_null (lrg_ui_loader_get_widget (loader, "doomed"));
	{
		GObject *obj = g_weak_ref_get (&doomed);

		g_assert_null (obj);
	}
	g_weak_ref_clear (&doomed);

	/* Handlers of reused widgets stay connected exactly once */
	g_signal_emit_by_name (go, "clicked");
	g_assert_cmpint (count, ==, 1);

	/* The YAML value of a property changed: it wins over the live state */
	write_stamped (path,
		"type: vbox\n"
		"id: root\n"
		"children:\n"
		"  - {type: label, id: title, text: Welcome, font-size: 20}\n"
		"  - {type: button, id: go, text: Go!, on: {clicked: count}}\n"
		"  - {type: text-input, id: name, placeholder: Name, text: Default}\n"
		"  - {type: slider, id: vol, min: 0, max: 200, value: 10}\n"
		"  - {type: label, text: anonymous}\n"
		"  - {type: label, id: fresh, text: new}\n"
		"  - {type: combo-box, id: pick, items: [x, b, c, d], selected: 0}\n",
		1000000020);
	g_assert_true (lrg_ui_loader_poll (loader));
	g_assert_cmpuint (log.reloaded, ==, 2);
	g_assert_true (lrg_ui_loader_get_widget (loader, "vol") == vol);
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (LRG_SLIDER (vol)), 10.0, 1e-9);
	g_assert_cmpstr (lrg_text_input_get_text (LRG_TEXT_INPUT (name)), ==, "Default");
	g_assert_cmpstr (lrg_button_get_text (LRG_BUTTON (go)), ==, "Go!");
	g_signal_emit_by_name (go, "clicked");
	g_assert_cmpint (count, ==, 2);

	/* A broken file keeps the old tree and reports why */
	write_stamped (path,
		"type: vbox\n"
		"id: root\n"
		"children:\n"
		"  - {type: label, id: title, text: Broken, colour: red}\n",
		1000000030);
	g_assert_true (lrg_ui_loader_poll (loader));
	g_assert_cmpuint (log.failed, ==, 1);
	g_assert_cmpuint (log.reloaded, ==, 2);
	g_assert_true (g_str_has_prefix (log.last_error, "children[0]: unknown property 'colour' on LrgLabel"));
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (title)), ==, "Welcome");
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (root)), ==, 7);
	/* Reported once, not every frame */
	g_assert_false (lrg_ui_loader_poll (loader));
	g_assert_cmpuint (log.failed, ==, 1);

	write_stamped (path, "type: vbox\nchildren: [\n", 1000000040);
	g_assert_true (lrg_ui_loader_poll (loader));
	g_assert_cmpuint (log.failed, ==, 2);
	g_assert_true (g_str_has_prefix (log.last_error, "(document): YAML syntax error"));

	/* Fixing it applies the diff to the old tree */
	write_stamped (path, RELOAD_V2, 1000000050);
	g_assert_true (lrg_ui_loader_poll (loader));
	g_assert_cmpuint (log.reloaded, ==, 3);
	g_assert_true (lrg_ui_loader_get_widget (loader, "title") == title);

	/* lrg_ui_loader_reload() works without a change */
	g_assert_true (lrg_ui_loader_reload (loader, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (log.reloaded, ==, 4);

	/* A root whose type changes is replaced in the host */
	write_stamped (path, "type: hbox\nid: root\nchildren: [{type: label, id: title, text: Row}]\n", 1000000060);
	g_assert_true (lrg_ui_loader_poll (loader));
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (host)), ==, 1);
	g_assert_true (LRG_IS_HBOX (lrg_container_get_child (LRG_CONTAINER (host), 0)));
	g_assert_true (lrg_ui_loader_get_widget (loader, "root") == lrg_container_get_child (LRG_CONTAINER (host), 0));

	/* Unwatching stops reloads */
	g_assert_true (lrg_ui_loader_unwatch_file (loader, path));
	g_assert_false (lrg_ui_loader_unwatch_file (loader, path));
	write_stamped (path, RELOAD_V1, 1000000070);
	g_assert_false (lrg_ui_loader_poll (loader));

	g_free (log.last_error);
	g_unlink (path);
	g_rmdir (dir);
}

static gboolean
set_flag (gpointer user_data)
{
	*(gboolean *)user_data = TRUE;
	return G_SOURCE_REMOVE;
}

static void
test_reload_details (void)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autoptr(LrgVBox) host = lrg_vbox_new ();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *inc = NULL;
	ReloadLog log = { 0 };
	LrgWidget *root;
	LrgWidget *tabs;
	LrgWidget *page2;
	LrgWidget *scroller;
	LrgWidget *inner;
	LrgWidget *a;
	LrgWidget *gauge;
	const LrgPartLook *look;

	g_type_ensure (TEST_TYPE_GAUGE);
	g_signal_connect (loader, "reloaded", G_CALLBACK (on_reloaded), &log);
	g_signal_connect (loader, "reload-failed", G_CALLBACK (on_reload_failed), &log);

	dir = g_dir_make_tmp ("lrg-ui-reload2-XXXXXX", &error);
	g_assert_no_error (error);
	inc = g_build_filename (dir, "badge.yaml", NULL);
	write_stamped (inc, "{type: label, id: badge, text: v1}\n", 2000000000);
	path = g_build_filename (dir, "screen.yaml", NULL);
	write_stamped (path,
		"type: vbox\n"
		"children:\n"
		"  - include: badge.yaml\n"
		"  - type: tab-view\n"
		"    id: tabs\n"
		"    tabs:\n"
		"      - {label: One, content: {type: label, id: page1}}\n"
		"      - {label: Two, content: {type: label, id: page2}}\n"
		"  - type: scroll-box\n"
		"    id: scroller\n"
		"    child: {type: vbox, id: inner, children: [{type: label, id: a}]}\n"
		"  - {type: label, id: styled, class: [x, y], margin: 4, look: {box: {background: '#ff0000'}}}\n"
		"  - {type: TestGauge, id: gauge, seed: 1, level: 3}\n",
		2000000000);

	root = lrg_ui_loader_watch_file (loader, path, LRG_CONTAINER (host), &error);
	g_assert_no_error (error);
	tabs = lrg_ui_loader_get_widget (loader, "tabs");
	page2 = lrg_ui_loader_get_widget (loader, "page2");
	scroller = lrg_ui_loader_get_widget (loader, "scroller");
	inner = lrg_ui_loader_get_widget (loader, "inner");
	a = lrg_ui_loader_get_widget (loader, "a");
	gauge = lrg_ui_loader_get_widget (loader, "gauge");
	lrg_tab_view_set_active_tab (LRG_TAB_VIEW (tabs), 1);

	/* Editing an included file reloads the document */
	write_stamped (inc, "{type: label, id: badge, text: v2}\n", 2000000010);
	g_assert_true (lrg_ui_loader_poll (loader));
	g_assert_cmpuint (log.reloaded, ==, 1);
	g_assert_cmpstr (label_text (loader, "badge"), ==, "v2");

	/* Tab relabelled in place; page added; scroll child kept; classes,
	 * margin and look replaced or removed; construct-only change rebuilds */
	write_stamped (path,
		"type: vbox\n"
		"children:\n"
		"  - include: badge.yaml\n"
		"  - type: tab-view\n"
		"    id: tabs\n"
		"    tabs:\n"
		"      - {label: Uno, content: {type: label, id: page1}}\n"
		"      - {label: Dos, content: {type: label, id: page2}}\n"
		"  - type: scroll-box\n"
		"    id: scroller\n"
		"    child: {type: vbox, id: inner, children: [{type: label, id: a, text: changed}]}\n"
		"  - {type: label, id: styled, class: [y, z]}\n"
		"  - {type: TestGauge, id: gauge, seed: 2, level: 3}\n",
		2000000020);
	g_assert_true (lrg_ui_loader_poll (loader));
	g_assert_cmpuint (log.reloaded, ==, 2);
	g_assert_true (lrg_ui_loader_get_widget (loader, "tabs") == tabs);
	g_assert_true (lrg_ui_loader_get_widget (loader, "page2") == page2);
	g_assert_cmpstr (lrg_tab_view_get_tab_label (LRG_TAB_VIEW (tabs), 0), ==, "Uno");
	g_assert_cmpuint (lrg_tab_view_get_active_tab (LRG_TAB_VIEW (tabs)), ==, 1);
	g_assert_true (lrg_ui_loader_get_widget (loader, "scroller") == scroller);
	g_assert_true (lrg_ui_loader_get_widget (loader, "inner") == inner);
	g_assert_true (lrg_ui_loader_get_widget (loader, "a") == a);
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (a)), ==, "changed");
	{
		LrgWidget *styled = lrg_ui_loader_get_widget (loader, "styled");
		LrgInsets m;

		g_assert_false (lrg_widget_has_class (styled, "x"));
		g_assert_true (lrg_widget_has_class (styled, "y"));
		g_assert_true (lrg_widget_has_class (styled, "z"));
		lrg_widget_get_margin (styled, &m);
		g_assert_cmpfloat (m.left, ==, 0.0f);
		look = lrg_widget_get_part_look (styled, LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE);
		g_assert_null (look);
	}
	g_assert_true (lrg_ui_loader_get_widget (loader, "gauge") != gauge);
	g_assert_cmpint (TEST_GAUGE (lrg_ui_loader_get_widget (loader, "gauge"))->seed, ==, 2);

	/* Pages added and reordered: contents reused, active page kept */
	write_stamped (path,
		"type: vbox\n"
		"children:\n"
		"  - include: badge.yaml\n"
		"  - type: tab-view\n"
		"    id: tabs\n"
		"    tabs:\n"
		"      - {label: Dos, content: {type: label, id: page2}}\n"
		"      - {label: Uno, content: {type: label, id: page1}}\n"
		"      - {label: Tres, content: {type: label, id: page3}}\n",
		2000000030);
	g_assert_true (lrg_ui_loader_poll (loader));
	g_assert_cmpuint (lrg_tab_view_get_tab_count (LRG_TAB_VIEW (tabs)), ==, 3);
	g_assert_true (lrg_tab_view_get_tab_content (LRG_TAB_VIEW (tabs), 0) == page2);
	g_assert_cmpstr (lrg_tab_view_get_tab_label (LRG_TAB_VIEW (tabs), 2), ==, "Tres");
	g_assert_null (lrg_ui_loader_get_widget (loader, "scroller"));
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (root)), ==, 2);

	/* Variables re-evaluate on reload */
	{
		GValue v = G_VALUE_INIT;

		write_stamped (inc, "{type: label, id: badge, text: '${default(greeting, \"none\")}'}\n", 2000000040);
		g_assert_true (lrg_ui_loader_poll (loader));
		g_assert_cmpstr (label_text (loader, "badge"), ==, "none");
		g_value_init (&v, G_TYPE_STRING);
		g_value_set_string (&v, "hey");
		lrg_ui_loader_set_variable (loader, "greeting", &v);
		g_value_unset (&v);
		g_assert_true (lrg_ui_loader_reload (loader, NULL));
		g_assert_cmpstr (label_text (loader, "badge"), ==, "hey");
	}

	/* Monitors: switching them on keeps poll working (it then waits for
	 * the monitor's flag; drive the main context to deliver it) */
	lrg_ui_loader_set_use_monitors (loader, TRUE);
	g_assert_true (lrg_ui_loader_get_use_monitors (loader));
	write_stamped (inc, "{type: label, id: badge, text: monitored}\n", 2000000050);
	{
		gboolean timed_out = FALSE;
		guint timeout;

		/* Block on the main context until the monitor fires (or 5 s pass) */
		timeout = g_timeout_add_seconds (5, set_flag, &timed_out);
		while (g_strcmp0 (label_text (loader, "badge"), "monitored") != 0 && !timed_out)
		{
			g_main_context_iteration (NULL, TRUE);
			lrg_ui_loader_poll (loader);
		}
		if (!timed_out)
			g_source_remove (timeout);
	}
	g_assert_cmpstr (label_text (loader, "badge"), ==, "monitored");
	lrg_ui_loader_set_use_monitors (loader, FALSE);

	lrg_ui_loader_unwatch_file (loader, path);
	g_free (log.last_error);
	g_unlink (path);
	g_unlink (inc);
	g_rmdir (dir);
}

/* ==========================================================================
 * The showcase document
 * ========================================================================== */

static void
on_any (gpointer instance,
        gpointer user_data)
{
}

static void
test_showcase (void)
{
	static const gchar *candidates[] = {
		"examples/data/ui/showcase-settings.yaml",
		"../examples/data/ui/showcase-settings.yaml",
		"../../examples/data/ui/showcase-settings.yaml",
		"../../../examples/data/ui/showcase-settings.yaml",
	};
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgWidget) root = NULL;
	g_autoptr(LrgCanvas) canvas = NULL;
	const gchar *path = NULL;
	LrgWidget *w;
	guint i;

	for (i = 0; i < G_N_ELEMENTS (candidates) && path == NULL; i++)
	{
		if (g_file_test (candidates[i], G_FILE_TEST_IS_REGULAR))
			path = candidates[i];
	}
	g_assert_nonnull (path);

	lrg_ui_loader_add_handler (loader, "settings_apply", G_CALLBACK (on_any), NULL);
	lrg_ui_loader_add_handler (loader, "settings_reset", G_CALLBACK (on_any), NULL);
	lrg_ui_loader_add_handler (loader, "settings_slider_changed", G_CALLBACK (on_any), NULL);

	root = lrg_ui_loader_load_from_file (loader, path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (root);

	g_assert_true (lrg_ui_loader_get_widget (loader, "settings") == root);
	w = lrg_ui_loader_get_widget (loader, "settings-tabs");
	g_assert_cmpuint (lrg_tab_view_get_tab_count (LRG_TAB_VIEW (w)), ==, 3);
	g_assert_true (LRG_IS_SLIDER (lrg_ui_loader_get_widget (loader, "slider-music")));
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (LRG_SLIDER (lrg_ui_loader_get_widget (loader, "slider-voice"))),
	                                90.0, 1e-9);
	g_assert_cmpstr (label_text (loader, "value-effects"), ==, "75%");
	g_assert_cmpstr (label_text (loader, "value-fov"), ==, "90");
	g_assert_cmpstr (lrg_button_get_text (LRG_BUTTON (lrg_ui_loader_get_widget (loader, "preset-3"))), ==, "Ultra");
	g_assert_true (lrg_widget_has_class (lrg_ui_loader_get_widget (loader, "preset-2"), "primary"));
	g_assert_cmpint (lrg_combo_box_get_selected (LRG_COMBO_BOX (lrg_ui_loader_get_widget (loader, "video-resolution"))), ==, 1);

	/* It lays out headless inside a canvas */
	canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (canvas), 1280.0f, 720.0f);
	lrg_container_add_child (LRG_CONTAINER (canvas), root);
	lrg_widget_layout (LRG_WIDGET (canvas));
	g_assert_true (lrg_widget_get_root (root) == LRG_WIDGET (canvas));
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/ui-loader/convert/scalars", test_convert_scalars);
	g_test_add_func ("/ui-loader/convert/color", test_convert_color);
	g_test_add_func ("/ui-loader/convert/insets", test_convert_insets);
	g_test_add_func ("/ui-loader/convert/custom", test_convert_custom);
	g_test_add_func ("/ui-loader/special-keys", test_special_keys);
	g_test_add_func ("/ui-loader/look-fx", test_look_fx);
	g_test_add_func ("/ui-loader/expressions", test_expressions);
	g_test_add_func ("/ui-loader/components", test_components);
	g_test_add_func ("/ui-loader/structural", test_structural);
	g_test_add_func ("/ui-loader/include", test_include);
	g_test_add_func ("/ui-loader/handlers", test_handlers);
	g_test_add_func ("/ui-loader/errors", test_error_paths);
	g_test_add_func ("/ui-loader/reload/diff", test_reload_diff);
	g_test_add_func ("/ui-loader/reload/details", test_reload_details);
	g_test_add_func ("/ui-loader/showcase", test_showcase);

	return g_test_run ();
}
