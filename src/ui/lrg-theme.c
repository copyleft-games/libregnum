/* lrg-theme.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * UI theme: colour roles, metrics, motion, style rules and the UI
 * font, with built-in presets and YAML loading/saving.
 *
 * Storage is two flat arrays indexed by #LrgThemeColor and
 * #LrgThemeMetric. The original per-colour API (primary, surface ...)
 * reads and writes the matching roles, so old code keeps working and
 * sees the same values new widgets draw with.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <yaml-glib.h>

#include "lrg-theme.h"
#include "../paint/lrg-paint-font.h"
#include "../text/lrg-font-manager.h"
#include "../lrg-log.h"

/* Base size the UI font is rasterised at; widgets draw smaller sizes
 * from its mipmaps */
#define UI_FONT_BASE_SIZE (64)

/* Name of the preset lrg_theme_new() starts from */
#define DEFAULT_PRESET "midnight"

struct _LrgTheme
{
	GObject    parent_instance;

	GrlColor   colors[LRG_THEME_COLOR_COUNT];
	gfloat     metrics[LRG_THEME_METRIC_COUNT];

	gchar     *name;
	gboolean   dark;
	LrgMotion  motion;

	/* Legacy spacing values without a role of their own */
	gfloat     padding_small;
	gfloat     padding_normal;
	gfloat     padding_large;

	/* Fonts */
	GrlFont   *default_font;
	GrlFont   *ui_font;
	gchar     *font_path;
	gboolean   ui_font_attempted;

	/* SDF text (LrgPaintFont) */
	gchar        *font_family;
	gchar       **font_fallbacks;
	gboolean      sdf_text;
	LrgPaintFont *paint_font;
	gboolean      paint_font_attempted;

	/* Style rules and the resolution cache */
	GPtrArray  *rules;        /* LrgStyleRule */
	GHashTable *look_cache;   /* key string -> LrgPartLook overlay */

	guint      generation;
	guint      freeze;        /* >0 while batching changes */
	gboolean   pending;       /* a change happened while frozen */
};

G_DEFINE_FINAL_TYPE (LrgTheme, lrg_theme, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-theme-error-quark, lrg_theme_error)

enum
{
	PROP_0,

	/* Colours (legacy names) */
	PROP_PRIMARY_COLOR,
	PROP_SECONDARY_COLOR,
	PROP_ACCENT_COLOR,
	PROP_BACKGROUND_COLOR,
	PROP_SURFACE_COLOR,
	PROP_TEXT_COLOR,
	PROP_TEXT_SECONDARY_COLOR,
	PROP_BORDER_COLOR,
	PROP_ERROR_COLOR,
	PROP_SUCCESS_COLOR,

	/* Typography */
	PROP_DEFAULT_FONT,
	PROP_FONT_SIZE_SMALL,
	PROP_FONT_SIZE_NORMAL,
	PROP_FONT_SIZE_LARGE,

	/* Spacing */
	PROP_PADDING_SMALL,
	PROP_PADDING_NORMAL,
	PROP_PADDING_LARGE,
	PROP_BORDER_WIDTH,
	PROP_CORNER_RADIUS,

	/* New */
	PROP_NAME,
	PROP_DARK,
	PROP_MOTION,

	N_PROPS
};

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

static LrgTheme   *default_theme = NULL;
static gboolean    font_init_attempted = FALSE;

static void        reset_paint_font (LrgTheme *self);

/* Legacy colour property -> role */
static const LrgThemeColor legacy_roles[] = {
	LRG_THEME_COLOR_ACCENT,      /* PROP_PRIMARY_COLOR */
	LRG_THEME_COLOR_CONTROL,     /* PROP_SECONDARY_COLOR */
	LRG_THEME_COLOR_FOCUS,       /* PROP_ACCENT_COLOR */
	LRG_THEME_COLOR_WINDOW,      /* PROP_BACKGROUND_COLOR */
	LRG_THEME_COLOR_PANEL,       /* PROP_SURFACE_COLOR */
	LRG_THEME_COLOR_TEXT,        /* PROP_TEXT_COLOR */
	LRG_THEME_COLOR_TEXT_MUTED,  /* PROP_TEXT_SECONDARY_COLOR */
	LRG_THEME_COLOR_BORDER,      /* PROP_BORDER_COLOR */
	LRG_THEME_COLOR_DANGER,      /* PROP_ERROR_COLOR */
	LRG_THEME_COLOR_SUCCESS      /* PROP_SUCCESS_COLOR */
};

/* ==========================================================================
 * Presets
 * ========================================================================== */

typedef struct
{
	const gchar *name;
	gboolean     dark;
	LrgMotion    motion;
	const gchar *accent;
	/* role, hex; terminated by a NULL hex */
	struct { LrgThemeColor role; const gchar *hex; } colors[LRG_THEME_COLOR_COUNT + 1];
	/* metric overrides on top of base_metrics; terminated by value < 0 */
	struct { LrgThemeMetric metric; gfloat value; } metrics[LRG_THEME_METRIC_COUNT + 1];
} Preset;

/* Metrics every preset starts from */
static const gfloat base_metrics[LRG_THEME_METRIC_COUNT] = {
	16.0f,  /* FONT_SIZE */
	12.0f,  /* FONT_SIZE_SMALL */
	24.0f,  /* FONT_SIZE_LARGE */
	32.0f,  /* FONT_SIZE_TITLE */
	8.0f,   /* CORNER_RADIUS */
	1.0f,   /* BORDER_WIDTH */
	36.0f,  /* CONTROL_HEIGHT */
	14.0f,  /* PADDING_X */
	8.0f,   /* PADDING_Y */
	8.0f,   /* SPACING */
	32.0f,  /* ROW_HEIGHT */
	12.0f,  /* POPUP_RADIUS */
	18.0f,  /* SHADOW_BLUR */
	6.0f,   /* SHADOW_OFFSET_Y */
	2.0f,   /* FOCUS_RING_WIDTH */
	8.0f,   /* SCROLLBAR_SIZE */
	20.0f,  /* CHECK_SIZE */
	18.0f   /* HANDLE_SIZE */
};

#define C(role, hex) { LRG_THEME_COLOR_##role, hex }
#define M(metric, v) { LRG_THEME_METRIC_##metric, v }
#define END_COLORS   { LRG_THEME_COLOR_WINDOW, NULL }
#define END_METRICS  { LRG_THEME_METRIC_FONT_SIZE, -1.0f }

static const Preset presets[] = {
	{
		"midnight", TRUE, LRG_MOTION_SMOOTH, "#7c6cff",
		{
			C (WINDOW, "#141417"), C (PANEL, "#1f1f24"), C (SURFACE, "#28282e"),
			C (BORDER, "#34343c"), C (TEXT, "#ececf1"), C (TEXT_MUTED, "#9a9aa6"),
			C (CONTROL, "#2b2b32"), C (CONTROL_HOVER, "#36363e"), C (CONTROL_PRESSED, "#41414a"),
			C (INPUT, "#111114"), C (TRACK, "#34343c"), C (HANDLE, "#f4f4f6"),
			C (POPUP, "#24242a"), C (TOOLTIP, "#0c0c0f"), C (SHADOW, "#000000a8"),
			C (VEIL, "#05050899"), C (SUCCESS, "#34d399"), C (WARNING, "#fbbf24"),
			C (DANGER, "#f87171"), C (DISABLED, "#5f5f6a"),
			END_COLORS
		},
		{ END_METRICS }
	},
	{
		"daylight", FALSE, LRG_MOTION_SMOOTH, "#5b4cf0",
		{
			C (WINDOW, "#f1f1f4"), C (PANEL, "#ffffff"), C (SURFACE, "#f6f6f9"),
			C (BORDER, "#dcdce3"), C (TEXT, "#1c1c22"), C (TEXT_MUTED, "#666675"),
			C (CONTROL, "#ebebf0"), C (CONTROL_HOVER, "#e1e1e8"), C (CONTROL_PRESSED, "#d4d4dd"),
			C (INPUT, "#ffffff"), C (TRACK, "#d9d9e1"), C (HANDLE, "#ffffff"),
			C (POPUP, "#ffffff"), C (TOOLTIP, "#26262e"), C (SHADOW, "#1a1a3a38"),
			C (VEIL, "#12121a55"), C (SUCCESS, "#16a34a"), C (WARNING, "#d97706"),
			C (DANGER, "#dc2626"), C (DISABLED, "#a6a6b2"),
			END_COLORS
		},
		{ M (SHADOW_BLUR, 22.0f), END_METRICS }
	},
	{
		"catppuccin-mocha", TRUE, LRG_MOTION_BOUNCY, "#cba6f7",
		{
			C (WINDOW, "#11111b"), C (PANEL, "#1e1e2e"), C (SURFACE, "#313244"),
			C (BORDER, "#45475a"), C (TEXT, "#cdd6f4"), C (TEXT_MUTED, "#a6adc8"),
			C (CONTROL, "#313244"), C (CONTROL_HOVER, "#45475a"), C (CONTROL_PRESSED, "#585b70"),
			C (INPUT, "#181825"), C (TRACK, "#45475a"), C (HANDLE, "#cdd6f4"),
			C (POPUP, "#1e1e2e"), C (TOOLTIP, "#181825"), C (SHADOW, "#000000b0"),
			C (VEIL, "#11111bb0"), C (SUCCESS, "#a6e3a1"), C (WARNING, "#f9e2af"),
			C (DANGER, "#f38ba8"), C (LINK, "#89b4fa"), C (DISABLED, "#6c7086"),
			END_COLORS
		},
		{ M (CORNER_RADIUS, 10.0f), M (POPUP_RADIUS, 14.0f), END_METRICS }
	},
	{
		"catppuccin-latte", FALSE, LRG_MOTION_BOUNCY, "#8839ef",
		{
			C (WINDOW, "#dce0e8"), C (PANEL, "#eff1f5"), C (SURFACE, "#e6e9ef"),
			C (BORDER, "#bcc0cc"), C (TEXT, "#4c4f69"), C (TEXT_MUTED, "#6c6f85"),
			C (CONTROL, "#ccd0da"), C (CONTROL_HOVER, "#bcc0cc"), C (CONTROL_PRESSED, "#acb0be"),
			C (INPUT, "#eff1f5"), C (TRACK, "#bcc0cc"), C (HANDLE, "#eff1f5"),
			C (POPUP, "#eff1f5"), C (TOOLTIP, "#4c4f69"), C (SHADOW, "#4c4f6944"),
			C (VEIL, "#4c4f6955"), C (SUCCESS, "#40a02b"), C (WARNING, "#df8e1d"),
			C (DANGER, "#d20f39"), C (LINK, "#1e66f5"), C (DISABLED, "#9ca0b0"),
			END_COLORS
		},
		{ M (CORNER_RADIUS, 10.0f), M (POPUP_RADIUS, 14.0f), END_METRICS }
	},
	{
		"high-contrast", TRUE, LRG_MOTION_NONE, "#ffd400",
		{
			C (WINDOW, "#000000"), C (PANEL, "#000000"), C (SURFACE, "#0a0a0a"),
			C (BORDER, "#ffffff"), C (TEXT, "#ffffff"), C (TEXT_MUTED, "#e6e6e6"),
			C (CONTROL, "#000000"), C (CONTROL_HOVER, "#1f1f1f"), C (CONTROL_PRESSED, "#383838"),
			C (INPUT, "#000000"), C (TRACK, "#9e9e9e"), C (HANDLE, "#ffffff"),
			C (POPUP, "#000000"), C (TOOLTIP, "#000000"), C (SHADOW, "#00000000"),
			C (VEIL, "#000000d0"), C (SUCCESS, "#00ff6a"), C (WARNING, "#ffd400"),
			C (DANGER, "#ff5c5c"), C (FOCUS, "#00e5ff"), C (LINK, "#00e5ff"),
			C (DISABLED, "#9e9e9e"),
			END_COLORS
		},
		{ M (BORDER_WIDTH, 2.0f), M (FOCUS_RING_WIDTH, 3.0f), M (CORNER_RADIUS, 4.0f),
		  M (SHADOW_BLUR, 0.0f), END_METRICS }
	}
};

#undef C
#undef M
#undef END_COLORS
#undef END_METRICS

static const gchar * const preset_names[] = {
	"midnight", "daylight", "catppuccin-mocha", "catppuccin-latte", "high-contrast", NULL
};

static const Preset *
find_preset (const gchar *name)
{
	gsize i;

	if (name == NULL)
		return NULL;

	for (i = 0; i < G_N_ELEMENTS (presets); i++)
		if (g_strcmp0 (presets[i].name, name) == 0)
			return &presets[i];

	return NULL;
}

/* ==========================================================================
 * Change tracking
 * ========================================================================== */

/*
 * notify_changed:
 *
 * Bumps the generation, drops cached rule resolutions and emits
 * ::changed (once, when a freeze ends, if changes were batched).
 */
static void
notify_changed (LrgTheme *self)
{
	self->generation++;
	g_hash_table_remove_all (self->look_cache);

	if (self->freeze > 0)
	{
		self->pending = TRUE;
		return;
	}

	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static void
freeze_changed (LrgTheme *self)
{
	self->freeze++;
}

static void
thaw_changed (LrgTheme *self)
{
	g_return_if_fail (self->freeze > 0);

	self->freeze--;
	if (self->freeze == 0 && self->pending)
	{
		self->pending = FALSE;
		g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
	}
}

/* ==========================================================================
 * Accent derivation
 * ========================================================================== */

static void
derive_accent (LrgTheme       *self,
               const GrlColor *accent)
{
	static const GrlColor black = { 17, 17, 22, 255 };
	static const GrlColor white = { 255, 255, 255, 255 };
	GrlColor              tmp;

	self->colors[LRG_THEME_COLOR_ACCENT] = *accent;

	if (self->dark)
	{
		lrg_paint_color_shade (accent, 0.16f, &self->colors[LRG_THEME_COLOR_ACCENT_HOVER]);
		lrg_paint_color_shade (accent, -0.14f, &self->colors[LRG_THEME_COLOR_ACCENT_PRESSED]);
		lrg_paint_color_shade (accent, 0.30f, &self->colors[LRG_THEME_COLOR_LINK]);
	}
	else
	{
		lrg_paint_color_shade (accent, -0.08f, &self->colors[LRG_THEME_COLOR_ACCENT_HOVER]);
		lrg_paint_color_shade (accent, -0.20f, &self->colors[LRG_THEME_COLOR_ACCENT_PRESSED]);
		lrg_paint_color_shade (accent, -0.10f, &self->colors[LRG_THEME_COLOR_LINK]);
	}

	self->colors[LRG_THEME_COLOR_FOCUS] = *accent;
	lrg_paint_color_with_alpha (accent, 0.32f, &tmp);
	self->colors[LRG_THEME_COLOR_SELECTION] = tmp;

	self->colors[LRG_THEME_COLOR_TEXT_ON_ACCENT] =
		lrg_paint_color_contrast (&black, accent) > lrg_paint_color_contrast (&white, accent)
		? black : white;
}

/* apply_preset_values: colours, metrics, motion, name (no signal) */
static void
apply_preset_values (LrgTheme     *self,
                     const Preset *preset)
{
	GrlColor accent;
	guint    i;

	memcpy (self->metrics, base_metrics, sizeof (base_metrics));
	for (i = 0; preset->metrics[i].value >= 0.0f; i++)
		self->metrics[preset->metrics[i].metric] = preset->metrics[i].value;

	self->dark = preset->dark;
	self->motion = preset->motion;
	g_free (self->name);
	self->name = g_strdup (preset->name);

	/* Accent family first, then explicit colours (which may override
	 * a derived one, e.g. a preset's own link colour) */
	if (lrg_color_parse (preset->accent, &accent))
		derive_accent (self, &accent);

	for (i = 0; preset->colors[i].hex != NULL; i++)
	{
		GrlColor c;

		if (lrg_color_parse (preset->colors[i].hex, &c))
			self->colors[preset->colors[i].role] = c;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_theme_finalize (GObject *object)
{
	LrgTheme *self = LRG_THEME (object);

	g_clear_object (&self->default_font);
	g_clear_object (&self->ui_font);
	g_clear_pointer (&self->font_path, g_free);
	g_clear_pointer (&self->font_family, g_free);
	g_clear_pointer (&self->font_fallbacks, g_strfreev);
	g_clear_object (&self->paint_font);
	g_clear_pointer (&self->name, g_free);
	g_clear_pointer (&self->rules, g_ptr_array_unref);
	g_clear_pointer (&self->look_cache, g_hash_table_unref);

	if (default_theme == self)
		default_theme = NULL;

	G_OBJECT_CLASS (lrg_theme_parent_class)->finalize (object);
}

static void
lrg_theme_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	LrgTheme *self = LRG_THEME (object);

	if (prop_id >= PROP_PRIMARY_COLOR && prop_id <= PROP_SUCCESS_COLOR)
	{
		g_value_set_boxed (value, &self->colors[legacy_roles[prop_id - PROP_PRIMARY_COLOR]]);
		return;
	}

	switch (prop_id)
	{
	case PROP_DEFAULT_FONT:
		g_value_set_object (value, self->default_font);
		break;
	case PROP_FONT_SIZE_SMALL:
		g_value_set_float (value, self->metrics[LRG_THEME_METRIC_FONT_SIZE_SMALL]);
		break;
	case PROP_FONT_SIZE_NORMAL:
		g_value_set_float (value, self->metrics[LRG_THEME_METRIC_FONT_SIZE]);
		break;
	case PROP_FONT_SIZE_LARGE:
		g_value_set_float (value, self->metrics[LRG_THEME_METRIC_FONT_SIZE_LARGE]);
		break;
	case PROP_PADDING_SMALL:
		g_value_set_float (value, self->padding_small);
		break;
	case PROP_PADDING_NORMAL:
		g_value_set_float (value, self->padding_normal);
		break;
	case PROP_PADDING_LARGE:
		g_value_set_float (value, self->padding_large);
		break;
	case PROP_BORDER_WIDTH:
		g_value_set_float (value, self->metrics[LRG_THEME_METRIC_BORDER_WIDTH]);
		break;
	case PROP_CORNER_RADIUS:
		g_value_set_float (value, self->metrics[LRG_THEME_METRIC_CORNER_RADIUS]);
		break;
	case PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case PROP_DARK:
		g_value_set_boolean (value, self->dark);
		break;
	case PROP_MOTION:
		g_value_set_enum (value, self->motion);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_theme_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	LrgTheme *self = LRG_THEME (object);

	if (prop_id >= PROP_PRIMARY_COLOR && prop_id <= PROP_SUCCESS_COLOR)
	{
		const GrlColor *c = g_value_get_boxed (value);

		if (c != NULL)
			lrg_theme_set_color (self, legacy_roles[prop_id - PROP_PRIMARY_COLOR], c);
		return;
	}

	switch (prop_id)
	{
	case PROP_DEFAULT_FONT:
		lrg_theme_set_default_font (self, g_value_get_object (value));
		break;
	case PROP_FONT_SIZE_SMALL:
		lrg_theme_set_font_size_small (self, g_value_get_float (value));
		break;
	case PROP_FONT_SIZE_NORMAL:
		lrg_theme_set_font_size_normal (self, g_value_get_float (value));
		break;
	case PROP_FONT_SIZE_LARGE:
		lrg_theme_set_font_size_large (self, g_value_get_float (value));
		break;
	case PROP_PADDING_SMALL:
		lrg_theme_set_padding_small (self, g_value_get_float (value));
		break;
	case PROP_PADDING_NORMAL:
		lrg_theme_set_padding_normal (self, g_value_get_float (value));
		break;
	case PROP_PADDING_LARGE:
		lrg_theme_set_padding_large (self, g_value_get_float (value));
		break;
	case PROP_BORDER_WIDTH:
		lrg_theme_set_border_width (self, g_value_get_float (value));
		break;
	case PROP_CORNER_RADIUS:
		lrg_theme_set_corner_radius (self, g_value_get_float (value));
		break;
	case PROP_NAME:
		lrg_theme_set_name (self, g_value_get_string (value));
		break;
	case PROP_DARK:
		lrg_theme_set_dark (self, g_value_get_boolean (value));
		break;
	case PROP_MOTION:
		lrg_theme_set_motion (self, g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static GParamSpec *
color_pspec (const gchar *name,
             const gchar *blurb)
{
	return g_param_spec_boxed (name, NULL, blurb, GRL_TYPE_COLOR,
	                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
	                           G_PARAM_EXPLICIT_NOTIFY);
}

static GParamSpec *
float_pspec (const gchar *name,
             const gchar *blurb,
             gfloat       def)
{
	return g_param_spec_float (name, NULL, blurb, 0.0f, G_MAXFLOAT, def,
	                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
	                           G_PARAM_EXPLICIT_NOTIFY);
}

static void
lrg_theme_class_init (LrgThemeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_theme_finalize;
	object_class->get_property = lrg_theme_get_property;
	object_class->set_property = lrg_theme_set_property;

	/**
	 * LrgTheme:primary-color:
	 *
	 * Alias of %LRG_THEME_COLOR_ACCENT.
	 */
	properties[PROP_PRIMARY_COLOR] = color_pspec ("primary-color", "Accent colour");
	/**
	 * LrgTheme:secondary-color:
	 *
	 * Alias of %LRG_THEME_COLOR_CONTROL.
	 */
	properties[PROP_SECONDARY_COLOR] = color_pspec ("secondary-color", "Control colour");
	/**
	 * LrgTheme:accent-color:
	 *
	 * Alias of %LRG_THEME_COLOR_FOCUS.
	 */
	properties[PROP_ACCENT_COLOR] = color_pspec ("accent-color", "Focus colour");
	/**
	 * LrgTheme:background-color:
	 *
	 * Alias of %LRG_THEME_COLOR_WINDOW.
	 */
	properties[PROP_BACKGROUND_COLOR] = color_pspec ("background-color", "Window colour");
	/**
	 * LrgTheme:surface-color:
	 *
	 * Alias of %LRG_THEME_COLOR_PANEL.
	 */
	properties[PROP_SURFACE_COLOR] = color_pspec ("surface-color", "Panel colour");
	/**
	 * LrgTheme:text-color:
	 *
	 * Alias of %LRG_THEME_COLOR_TEXT.
	 */
	properties[PROP_TEXT_COLOR] = color_pspec ("text-color", "Text colour");
	/**
	 * LrgTheme:text-secondary-color:
	 *
	 * Alias of %LRG_THEME_COLOR_TEXT_MUTED.
	 */
	properties[PROP_TEXT_SECONDARY_COLOR] = color_pspec ("text-secondary-color", "Muted text colour");
	/**
	 * LrgTheme:border-color:
	 *
	 * Alias of %LRG_THEME_COLOR_BORDER.
	 */
	properties[PROP_BORDER_COLOR] = color_pspec ("border-color", "Border colour");
	/**
	 * LrgTheme:error-color:
	 *
	 * Alias of %LRG_THEME_COLOR_DANGER.
	 */
	properties[PROP_ERROR_COLOR] = color_pspec ("error-color", "Danger colour");
	/**
	 * LrgTheme:success-color:
	 *
	 * Alias of %LRG_THEME_COLOR_SUCCESS.
	 */
	properties[PROP_SUCCESS_COLOR] = color_pspec ("success-color", "Success colour");

	/**
	 * LrgTheme:default-font:
	 *
	 * Font used by legacy widgets that draw with graylib directly.
	 */
	properties[PROP_DEFAULT_FONT] =
		g_param_spec_object ("default-font", NULL, "Default font", GRL_TYPE_FONT,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                     G_PARAM_EXPLICIT_NOTIFY);
	/**
	 * LrgTheme:font-size-small:
	 *
	 * Alias of %LRG_THEME_METRIC_FONT_SIZE_SMALL.
	 */
	properties[PROP_FONT_SIZE_SMALL] = float_pspec ("font-size-small", "Small font size", 12.0f);
	/**
	 * LrgTheme:font-size-normal:
	 *
	 * Alias of %LRG_THEME_METRIC_FONT_SIZE.
	 */
	properties[PROP_FONT_SIZE_NORMAL] = float_pspec ("font-size-normal", "Body font size", 16.0f);
	/**
	 * LrgTheme:font-size-large:
	 *
	 * Alias of %LRG_THEME_METRIC_FONT_SIZE_LARGE.
	 */
	properties[PROP_FONT_SIZE_LARGE] = float_pspec ("font-size-large", "Large font size", 24.0f);
	/**
	 * LrgTheme:padding-small:
	 *
	 * Small padding.
	 */
	properties[PROP_PADDING_SMALL] = float_pspec ("padding-small", "Small padding", 4.0f);
	/**
	 * LrgTheme:padding-normal:
	 *
	 * Normal padding.
	 */
	properties[PROP_PADDING_NORMAL] = float_pspec ("padding-normal", "Normal padding", 8.0f);
	/**
	 * LrgTheme:padding-large:
	 *
	 * Large padding.
	 */
	properties[PROP_PADDING_LARGE] = float_pspec ("padding-large", "Large padding", 16.0f);
	/**
	 * LrgTheme:border-width:
	 *
	 * Alias of %LRG_THEME_METRIC_BORDER_WIDTH.
	 */
	properties[PROP_BORDER_WIDTH] = float_pspec ("border-width", "Border width", 1.0f);
	/**
	 * LrgTheme:corner-radius:
	 *
	 * Alias of %LRG_THEME_METRIC_CORNER_RADIUS.
	 */
	properties[PROP_CORNER_RADIUS] = float_pspec ("corner-radius", "Corner radius", 8.0f);

	/**
	 * LrgTheme:name:
	 *
	 * Human-readable theme name.
	 */
	properties[PROP_NAME] =
		g_param_spec_string ("name", NULL, "Theme name", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                     G_PARAM_EXPLICIT_NOTIFY);
	/**
	 * LrgTheme:dark:
	 *
	 * Whether this is a dark theme.
	 */
	properties[PROP_DARK] =
		g_param_spec_boolean ("dark", NULL, "Dark theme", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);
	/**
	 * LrgTheme:motion:
	 *
	 * Spring preset for widget state transitions.
	 */
	properties[PROP_MOTION] =
		g_param_spec_enum ("motion", NULL, "Transition motion", LRG_TYPE_MOTION,
		                   LRG_MOTION_SMOOTH,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                   G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgTheme::changed:
	 * @theme: the theme
	 *
	 * Emitted after any colour, metric, motion, font or style rule
	 * changes. Batched loads emit it once.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_theme_init (LrgTheme *self)
{
	self->rules = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_style_rule_unref);
	self->look_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	self->padding_small = 4.0f;
	self->padding_normal = 8.0f;
	self->padding_large = 16.0f;
	self->sdf_text = TRUE;

	apply_preset_values (self, find_preset (DEFAULT_PRESET));
}

/* ==========================================================================
 * Construction and the default theme
 * ========================================================================== */

LrgTheme *
lrg_theme_get_default (void)
{
	if (default_theme == NULL)
		default_theme = lrg_theme_new ();

	return default_theme;
}

void
lrg_theme_set_default (LrgTheme *theme)
{
	g_return_if_fail (theme == NULL || LRG_IS_THEME (theme));

	if (theme == default_theme)
		return;

	/* Keep the legacy font the engine installed on the old default */
	if (theme != NULL && default_theme != NULL &&
	    theme->default_font == NULL && default_theme->default_font != NULL)
		theme->default_font = g_object_ref (default_theme->default_font);

	if (default_theme != NULL)
	{
		LrgTheme *old = default_theme;

		default_theme = NULL;
		g_object_unref (old);
	}

	default_theme = theme != NULL ? g_object_ref (theme) : NULL;
}

LrgTheme *
lrg_theme_new (void)
{
	return g_object_new (LRG_TYPE_THEME, NULL);
}

const gchar * const *
lrg_theme_list_presets (void)
{
	return preset_names;
}

LrgTheme *
lrg_theme_new_preset (const gchar *name)
{
	const Preset *preset;
	LrgTheme     *self;

	preset = find_preset (name);
	if (preset == NULL)
		return NULL;

	self = lrg_theme_new ();
	apply_preset_values (self, preset);
	return self;
}

gboolean
lrg_theme_apply_preset (LrgTheme    *self,
                        const gchar *name)
{
	const Preset *preset;

	g_return_val_if_fail (LRG_IS_THEME (self), FALSE);

	preset = find_preset (name);
	if (preset == NULL)
		return FALSE;

	apply_preset_values (self, preset);
	notify_changed (self);
	return TRUE;
}

LrgTheme *
lrg_theme_copy (LrgTheme *self)
{
	LrgTheme *copy;
	guint     i;

	g_return_val_if_fail (LRG_IS_THEME (self), NULL);

	copy = lrg_theme_new ();
	memcpy (copy->colors, self->colors, sizeof (self->colors));
	memcpy (copy->metrics, self->metrics, sizeof (self->metrics));
	copy->dark = self->dark;
	copy->motion = self->motion;
	copy->padding_small = self->padding_small;
	copy->padding_normal = self->padding_normal;
	copy->padding_large = self->padding_large;
	g_free (copy->name);
	copy->name = g_strdup (self->name);
	copy->font_path = g_strdup (self->font_path);
	if (self->default_font != NULL)
		copy->default_font = g_object_ref (self->default_font);
	if (self->ui_font != NULL)
		copy->ui_font = g_object_ref (self->ui_font);
	copy->ui_font_attempted = self->ui_font_attempted;
	copy->font_family = g_strdup (self->font_family);
	copy->font_fallbacks = g_strdupv (self->font_fallbacks);
	copy->sdf_text = self->sdf_text;
	if (self->paint_font != NULL)
		copy->paint_font = g_object_ref (self->paint_font);
	copy->paint_font_attempted = self->paint_font_attempted;

	for (i = 0; i < self->rules->len; i++)
		g_ptr_array_add (copy->rules, lrg_style_rule_ref (g_ptr_array_index (self->rules, i)));

	return copy;
}

/* ==========================================================================
 * Roles and metrics
 * ========================================================================== */

const GrlColor *
lrg_theme_get_color (LrgTheme      *self,
                     LrgThemeColor  role)
{
	g_return_val_if_fail (LRG_IS_THEME (self), NULL);
	g_return_val_if_fail ((guint) role < LRG_THEME_COLOR_COUNT, NULL);

	return &self->colors[role];
}

void
lrg_theme_set_color (LrgTheme       *self,
                     LrgThemeColor   role,
                     const GrlColor *color)
{
	guint i;

	g_return_if_fail (LRG_IS_THEME (self));
	g_return_if_fail ((guint) role < LRG_THEME_COLOR_COUNT);
	g_return_if_fail (color != NULL);

	if (memcmp (&self->colors[role], color, sizeof (GrlColor)) == 0)
		return;

	self->colors[role] = *color;

	/* Keep legacy property listeners informed */
	for (i = 0; i < G_N_ELEMENTS (legacy_roles); i++)
		if (legacy_roles[i] == role)
			g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIMARY_COLOR + i]);

	notify_changed (self);
}

gfloat
lrg_theme_get_metric (LrgTheme       *self,
                      LrgThemeMetric  metric)
{
	g_return_val_if_fail (LRG_IS_THEME (self), 0.0f);
	g_return_val_if_fail ((guint) metric < LRG_THEME_METRIC_COUNT, 0.0f);

	return self->metrics[metric];
}

void
lrg_theme_set_metric (LrgTheme       *self,
                      LrgThemeMetric  metric,
                      gfloat          value)
{
	g_return_if_fail (LRG_IS_THEME (self));
	g_return_if_fail ((guint) metric < LRG_THEME_METRIC_COUNT);

	if (self->metrics[metric] == value)
		return;

	self->metrics[metric] = value;

	switch (metric)
	{
	case LRG_THEME_METRIC_FONT_SIZE_SMALL:
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE_SMALL]);
		break;
	case LRG_THEME_METRIC_FONT_SIZE:
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE_NORMAL]);
		break;
	case LRG_THEME_METRIC_FONT_SIZE_LARGE:
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE_LARGE]);
		break;
	case LRG_THEME_METRIC_BORDER_WIDTH:
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_WIDTH]);
		break;
	case LRG_THEME_METRIC_CORNER_RADIUS:
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
		break;
	default:
		break;
	}

	notify_changed (self);
}

void
lrg_theme_set_accent (LrgTheme       *self,
                      const GrlColor *accent)
{
	g_return_if_fail (LRG_IS_THEME (self));
	g_return_if_fail (accent != NULL);

	derive_accent (self, accent);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIMARY_COLOR]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACCENT_COLOR]);
	notify_changed (self);
}

gfloat
lrg_theme_get_contrast (LrgTheme      *self,
                        LrgThemeColor  foreground,
                        LrgThemeColor  background)
{
	g_return_val_if_fail (LRG_IS_THEME (self), 1.0f);
	g_return_val_if_fail ((guint) foreground < LRG_THEME_COLOR_COUNT, 1.0f);
	g_return_val_if_fail ((guint) background < LRG_THEME_COLOR_COUNT, 1.0f);

	return lrg_paint_color_contrast (&self->colors[foreground], &self->colors[background]);
}

/* ==========================================================================
 * Identity and motion
 * ========================================================================== */

const gchar *
lrg_theme_get_name (LrgTheme *self)
{
	g_return_val_if_fail (LRG_IS_THEME (self), NULL);

	return self->name;
}

void
lrg_theme_set_name (LrgTheme    *self,
                    const gchar *name)
{
	g_return_if_fail (LRG_IS_THEME (self));

	if (g_strcmp0 (self->name, name) == 0)
		return;

	g_free (self->name);
	self->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

gboolean
lrg_theme_get_dark (LrgTheme *self)
{
	g_return_val_if_fail (LRG_IS_THEME (self), TRUE);

	return self->dark;
}

void
lrg_theme_set_dark (LrgTheme *self,
                    gboolean  dark)
{
	g_return_if_fail (LRG_IS_THEME (self));

	dark = !!dark;
	if (self->dark == dark)
		return;

	self->dark = dark;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DARK]);
	notify_changed (self);
}

LrgMotion
lrg_theme_get_motion (LrgTheme *self)
{
	g_return_val_if_fail (LRG_IS_THEME (self), LRG_MOTION_SMOOTH);

	return self->motion;
}

void
lrg_theme_set_motion (LrgTheme  *self,
                      LrgMotion  motion)
{
	g_return_if_fail (LRG_IS_THEME (self));

	if (self->motion == motion)
		return;

	self->motion = motion;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MOTION]);
	notify_changed (self);
}

guint
lrg_theme_get_generation (LrgTheme *self)
{
	g_return_val_if_fail (LRG_IS_THEME (self), 0);

	return self->generation;
}

/* ==========================================================================
 * Legacy colour accessors
 * ========================================================================== */

#define LEGACY_COLOR(fn, role) \
	const GrlColor * \
	lrg_theme_get_##fn (LrgTheme *self) \
	{ \
		return lrg_theme_get_color (self, LRG_THEME_COLOR_##role); \
	} \
	void \
	lrg_theme_set_##fn (LrgTheme       *self, \
	                    const GrlColor *color) \
	{ \
		lrg_theme_set_color (self, LRG_THEME_COLOR_##role, color); \
	}

LEGACY_COLOR (primary_color, ACCENT)
LEGACY_COLOR (secondary_color, CONTROL)
LEGACY_COLOR (accent_color, FOCUS)
LEGACY_COLOR (background_color, WINDOW)
LEGACY_COLOR (surface_color, PANEL)
LEGACY_COLOR (text_color, TEXT)
LEGACY_COLOR (text_secondary_color, TEXT_MUTED)
LEGACY_COLOR (border_color, BORDER)
LEGACY_COLOR (error_color, DANGER)
LEGACY_COLOR (success_color, SUCCESS)

#undef LEGACY_COLOR

/* ==========================================================================
 * Legacy size accessors
 * ========================================================================== */

#define LEGACY_METRIC(fn, metric) \
	gfloat \
	lrg_theme_get_##fn (LrgTheme *self) \
	{ \
		return lrg_theme_get_metric (self, LRG_THEME_METRIC_##metric); \
	} \
	void \
	lrg_theme_set_##fn (LrgTheme *self, \
	                    gfloat    value) \
	{ \
		lrg_theme_set_metric (self, LRG_THEME_METRIC_##metric, value); \
	}

LEGACY_METRIC (font_size_small, FONT_SIZE_SMALL)
LEGACY_METRIC (font_size_normal, FONT_SIZE)
LEGACY_METRIC (font_size_large, FONT_SIZE_LARGE)
LEGACY_METRIC (border_width, BORDER_WIDTH)
LEGACY_METRIC (corner_radius, CORNER_RADIUS)

#undef LEGACY_METRIC

#define LEGACY_PADDING(fn, field, prop) \
	gfloat \
	lrg_theme_get_##fn (LrgTheme *self) \
	{ \
		g_return_val_if_fail (LRG_IS_THEME (self), 0.0f); \
		return self->field; \
	} \
	void \
	lrg_theme_set_##fn (LrgTheme *self, \
	                    gfloat    value) \
	{ \
		g_return_if_fail (LRG_IS_THEME (self)); \
		if (self->field == value) \
			return; \
		self->field = value; \
		g_object_notify_by_pspec (G_OBJECT (self), properties[prop]); \
		notify_changed (self); \
	}

LEGACY_PADDING (padding_small, padding_small, PROP_PADDING_SMALL)
LEGACY_PADDING (padding_normal, padding_normal, PROP_PADDING_NORMAL)
LEGACY_PADDING (padding_large, padding_large, PROP_PADDING_LARGE)

#undef LEGACY_PADDING

/* ==========================================================================
 * Fonts
 * ========================================================================== */

GrlFont *
lrg_theme_get_default_font (LrgTheme *self)
{
	g_return_val_if_fail (LRG_IS_THEME (self), NULL);

	/* Lazy initialisation from the font manager, once per process */
	if (self->default_font == NULL && !font_init_attempted && IsWindowReady ())
	{
		LrgFontManager *font_mgr;

		font_init_attempted = TRUE;
		font_mgr = lrg_font_manager_get_default ();

		if (lrg_font_manager_initialize (font_mgr, NULL))
		{
			GrlFont *font = lrg_font_manager_get_default_font (font_mgr);

			if (font != NULL)
			{
				self->default_font = g_object_ref (font);
				lrg_debug (LRG_LOG_DOMAIN_UI, "Lazy-initialized default font from font manager");
			}
		}
	}

	return self->default_font;
}

void
lrg_theme_set_default_font (LrgTheme *self,
                            GrlFont  *font)
{
	g_return_if_fail (LRG_IS_THEME (self));
	g_return_if_fail (font == NULL || GRL_IS_FONT (font));

	if (g_set_object (&self->default_font, font))
	{
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEFAULT_FONT]);
		notify_changed (self);
	}
}

/* Fonts tried for the UI font, best first */
static const gchar * const ui_font_candidates[] = {
	"/usr/share/fonts/adwaita-sans-fonts/AdwaitaSans-Regular.ttf",
	"/usr/share/fonts/inter/Inter-Regular.ttf",
	"/usr/share/fonts/inter/InterVariable.ttf",
	"/usr/share/fonts/rsms-inter-fonts/Inter-Regular.ttf",
	"/usr/share/fonts/TTF/Inter-Regular.ttf",
	"/usr/share/fonts/abattis-cantarell-fonts/Cantarell-Regular.otf",
	"/usr/share/fonts/cantarell/Cantarell-Regular.otf",
	"/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
	"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
	"/usr/share/fonts/noto/NotoSans-Regular.ttf",
	"/usr/share/fonts/open-sans/OpenSans-Regular.ttf",
	"/usr/share/fonts/truetype/open-sans/OpenSans-Regular.ttf",
	"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	"/usr/share/fonts/TTF/DejaVuSans.ttf",
	NULL
};

/*
 * build_codepoints:
 *
 * ASCII, Latin-1, Latin Extended-A and the punctuation and symbols UI
 * text commonly uses (quotes, dashes, ellipsis, bullet, euro, arrows,
 * check and cross marks).
 */
static gint *
build_codepoints (gint *count)
{
	static const gint extra[] = {
		0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026,
		0x20AC, 0x2122, 0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x2713,
		0x2715, 0x2716, 0x25B2, 0x25B6, 0x25BC, 0x25C0
	};
	GArray *cps;
	gint    cp;
	guint   i;

	cps = g_array_new (FALSE, FALSE, sizeof (gint));
	for (cp = 0x20; cp <= 0x7E; cp++)
		g_array_append_val (cps, cp);
	for (cp = 0xA0; cp <= 0x17F; cp++)
		g_array_append_val (cps, cp);
	for (i = 0; i < G_N_ELEMENTS (extra); i++)
		g_array_append_val (cps, extra[i]);

	*count = (gint) cps->len;
	return (gint *) (gpointer) g_array_free (cps, FALSE);
}

/* load_font_file: rasterise @path for UI use, NULL on failure */
static GrlFont *
load_font_file (const gchar *path)
{
	g_autofree gint *cps = NULL;
	GrlFont         *font;
	Font            *handle;
	gint             count;

	if (path == NULL || !g_file_test (path, G_FILE_TEST_IS_REGULAR) || !IsWindowReady ())
		return NULL;

	cps = build_codepoints (&count);
	font = grl_font_new_from_file_ex (path, UI_FONT_BASE_SIZE, cps, count);
	if (font == NULL)
		return NULL;

	handle = grl_font_get_handle (font);
	if (handle == NULL || handle->texture.id == 0 || handle->glyphCount == 0)
	{
		g_object_unref (font);
		return NULL;
	}

	/* Mipmaps + trilinear keep glyphs clean when drawn far below the
	 * base size */
	GenTextureMipmaps (&handle->texture);
	SetTextureFilter (handle->texture, TEXTURE_FILTER_TRILINEAR);

	return font;
}

GrlFont *
lrg_theme_get_ui_font (LrgTheme *self)
{
	g_return_val_if_fail (LRG_IS_THEME (self), NULL);

	if (self->ui_font == NULL && !self->ui_font_attempted && IsWindowReady ())
	{
		guint i;

		self->ui_font_attempted = TRUE;

		/* A font requested by the theme wins over the system list */
		if (self->font_path != NULL)
		{
			self->ui_font = load_font_file (self->font_path);
			if (self->ui_font == NULL)
				lrg_warning (LRG_LOG_DOMAIN_UI, "theme font %s could not be loaded",
				             self->font_path);
		}

		for (i = 0; self->ui_font == NULL && ui_font_candidates[i] != NULL; i++)
			self->ui_font = load_font_file (ui_font_candidates[i]);

		if (self->ui_font != NULL)
		{
			LrgPaintFont *sdf = lrg_theme_get_paint_font (self);

			/* Widgets pass this GrlFont to the paint text calls; the
			 * binding routes them to the SDF font */
			if (sdf != NULL)
				lrg_paint_font_bind (sdf, self->ui_font);
			lrg_debug (LRG_LOG_DOMAIN_UI, "UI font loaded%s", sdf != NULL ? " (SDF)" : "");
		}
	}

	return self->ui_font != NULL ? self->ui_font : lrg_theme_get_default_font (self);
}

void
lrg_theme_set_ui_font (LrgTheme *self,
                       GrlFont  *font)
{
	g_return_if_fail (LRG_IS_THEME (self));
	g_return_if_fail (font == NULL || GRL_IS_FONT (font));

	g_set_object (&self->ui_font, font);
	self->ui_font_attempted = font != NULL;
	notify_changed (self);
}

gboolean
lrg_theme_load_ui_font (LrgTheme     *self,
                        const gchar  *path,
                        GError      **error)
{
	GrlFont *font;

	g_return_val_if_fail (LRG_IS_THEME (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	font = load_font_file (path);
	if (font == NULL)
	{
		g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_FONT,
		             "cannot load font %s%s", path,
		             IsWindowReady () ? "" : " (no window open)");
		return FALSE;
	}

	g_free (self->font_path);
	self->font_path = g_strdup (path);
	reset_paint_font (self);
	if (lrg_theme_get_paint_font (self) != NULL)
		lrg_paint_font_bind (self->paint_font, font);
	lrg_theme_set_ui_font (self, font);
	g_object_unref (font);
	return TRUE;
}

const gchar *
lrg_theme_get_font_path (LrgTheme *self)
{
	g_return_val_if_fail (LRG_IS_THEME (self), NULL);

	return self->font_path;
}

/* ==========================================================================
 * SDF text
 * ========================================================================== */

/* reset_paint_font: forget the SDF font so it is rebuilt from the
 * current settings on next use */
static void
reset_paint_font (LrgTheme *self)
{
	g_clear_object (&self->paint_font);
	self->paint_font_attempted = FALSE;
}

/* reset_fonts: settings changed; reload both UI fonts lazily */
static void
reset_fonts (LrgTheme *self)
{
	reset_paint_font (self);
	g_clear_object (&self->ui_font);
	self->ui_font_attempted = FALSE;
}

/* looks_like_path: fallback entries naming a file rather than a family */
static gboolean
looks_like_path (const gchar *entry)
{
	return strchr (entry, '/') != NULL ||
	       g_str_has_suffix (entry, ".ttf") || g_str_has_suffix (entry, ".otf") ||
	       g_str_has_suffix (entry, ".ttc") || g_str_has_suffix (entry, ".TTF") ||
	       g_str_has_suffix (entry, ".OTF") || g_str_has_suffix (entry, ".TTC");
}

/*
 * build_paint_font:
 *
 * The SDF font for the theme's settings: the font file, else the
 * family, else the system UI face; then the theme's fallbacks in order
 * and the system fallbacks. With no font settings at all, one shared
 * font of the face the bitmap UI font uses (first of
 * ui_font_candidates) is used, so every theme shares one set of glyphs.
 */
static LrgPaintFont *
build_paint_font (LrgTheme *self)
{
	g_autoptr(GError) error = NULL;
	LrgPaintFont     *font = NULL;
	guint             i;

	if (self->font_path == NULL && self->font_family == NULL &&
	    (self->font_fallbacks == NULL || self->font_fallbacks[0] == NULL))
	{
		/* Same face the bitmap UI font picks (Adwaita Sans, Inter, ...),
		 * built once and shared by every theme without font settings */
		static gboolean      tried = FALSE;
		static LrgPaintFont *shared = NULL;

		if (!tried)
		{
			tried = TRUE;
			for (i = 0; shared == NULL && ui_font_candidates[i] != NULL; i++)
				if (g_file_test (ui_font_candidates[i], G_FILE_TEST_IS_REGULAR))
					shared = lrg_paint_font_new_from_file (ui_font_candidates[i], NULL);
			if (shared != NULL)
				lrg_paint_font_add_system_fallbacks (shared);
			else if (lrg_paint_font_get_system_default () != NULL)
				shared = g_object_ref (lrg_paint_font_get_system_default ());
		}
		return shared != NULL ? g_object_ref (shared) : NULL;
	}

	if (self->font_path != NULL)
	{
		font = lrg_paint_font_new_from_file (self->font_path, &error);
		if (font == NULL)
		{
			lrg_warning (LRG_LOG_DOMAIN_UI, "theme font: %s", error->message);
			g_clear_error (&error);
		}
	}
	if (font == NULL && self->font_family != NULL)
	{
		font = lrg_paint_font_new_for_family (self->font_family, &error);
		if (font == NULL)
		{
			lrg_warning (LRG_LOG_DOMAIN_UI, "theme font-family: %s", error->message);
			g_clear_error (&error);
		}
	}
	if (font == NULL)
		font = lrg_paint_font_new_for_family ("sans-serif", NULL);
	if (font == NULL)
		return NULL;

	for (i = 0; self->font_fallbacks != NULL && self->font_fallbacks[i] != NULL; i++)
	{
		const gchar             *entry = self->font_fallbacks[i];
		g_autoptr(LrgPaintFont)  fallback = NULL;

		fallback = looks_like_path (entry)
		           ? lrg_paint_font_new_from_file (entry, &error)
		           : lrg_paint_font_new_for_family (entry, &error);
		if (fallback == NULL)
		{
			lrg_debug (LRG_LOG_DOMAIN_UI, "theme font fallback skipped: %s", error->message);
			g_clear_error (&error);
			continue;
		}
		lrg_paint_font_add_fallback (font, fallback);
	}
	lrg_paint_font_add_system_fallbacks (font);
	return font;
}

LrgPaintFont *
lrg_theme_get_paint_font (LrgTheme *self)
{
	g_return_val_if_fail (LRG_IS_THEME (self), NULL);

	if (!self->sdf_text)
		return NULL;
	if (self->paint_font == NULL && !self->paint_font_attempted)
	{
		self->paint_font_attempted = TRUE;
		self->paint_font = build_paint_font (self);
	}
	return self->paint_font;
}

void
lrg_theme_set_paint_font (LrgTheme     *self,
                          LrgPaintFont *font)
{
	g_return_if_fail (LRG_IS_THEME (self));
	g_return_if_fail (font == NULL || LRG_IS_PAINT_FONT (font));

	g_set_object (&self->paint_font, font);
	self->paint_font_attempted = font != NULL;
	if (self->ui_font != NULL)
		lrg_paint_font_bind (self->sdf_text ? lrg_theme_get_paint_font (self) : NULL,
		                     self->ui_font);
	notify_changed (self);
}

const gchar *
lrg_theme_get_font_family (LrgTheme *self)
{
	g_return_val_if_fail (LRG_IS_THEME (self), NULL);

	return self->font_family;
}

void
lrg_theme_set_font_family (LrgTheme    *self,
                           const gchar *family)
{
	g_return_if_fail (LRG_IS_THEME (self));

	if (g_strcmp0 (self->font_family, family) == 0)
		return;
	g_free (self->font_family);
	self->font_family = g_strdup (family);
	reset_fonts (self);
	notify_changed (self);
}

const gchar * const *
lrg_theme_get_font_fallbacks (LrgTheme *self)
{
	g_return_val_if_fail (LRG_IS_THEME (self), NULL);

	return (const gchar * const *) self->font_fallbacks;
}

void
lrg_theme_set_font_fallbacks (LrgTheme           *self,
                              const gchar * const *fallbacks)
{
	g_return_if_fail (LRG_IS_THEME (self));

	g_strfreev (self->font_fallbacks);
	self->font_fallbacks = (fallbacks != NULL && fallbacks[0] != NULL)
	                       ? g_strdupv ((gchar **) fallbacks) : NULL;
	reset_fonts (self);
	notify_changed (self);
}

gboolean
lrg_theme_get_sdf_text (LrgTheme *self)
{
	g_return_val_if_fail (LRG_IS_THEME (self), FALSE);

	return self->sdf_text;
}

void
lrg_theme_set_sdf_text (LrgTheme *self,
                        gboolean  enabled)
{
	g_return_if_fail (LRG_IS_THEME (self));

	enabled = !!enabled;
	if (self->sdf_text == enabled)
		return;
	self->sdf_text = enabled;
	reset_fonts (self);
	notify_changed (self);
}

/* ==========================================================================
 * Style rules
 * ========================================================================== */

void
lrg_theme_add_style_rule (LrgTheme     *self,
                          LrgStyleRule *rule)
{
	g_return_if_fail (LRG_IS_THEME (self));
	g_return_if_fail (rule != NULL);

	g_ptr_array_add (self->rules, lrg_style_rule_ref (rule));
	notify_changed (self);
}

void
lrg_theme_clear_style_rules (LrgTheme *self)
{
	g_return_if_fail (LRG_IS_THEME (self));

	if (self->rules->len == 0)
		return;

	g_ptr_array_set_size (self->rules, 0);
	notify_changed (self);
}

void
lrg_theme_style_rules_changed (LrgTheme *self)
{
	g_return_if_fail (LRG_IS_THEME (self));

	notify_changed (self);
}

GPtrArray *
lrg_theme_get_style_rules (LrgTheme *self)
{
	g_return_val_if_fail (LRG_IS_THEME (self), NULL);

	return self->rules;
}

typedef struct
{
	guint         specificity;
	guint         order;
	LrgStyleRule *rule;
} RuleMatch;

static gint
compare_matches (gconstpointer a,
                 gconstpointer b)
{
	const RuleMatch *ma = a;
	const RuleMatch *mb = b;

	if (ma->specificity != mb->specificity)
		return ma->specificity < mb->specificity ? -1 : 1;
	return ma->order < mb->order ? -1 : (ma->order > mb->order ? 1 : 0);
}

void
lrg_theme_resolve_look (LrgTheme       *self,
                        GType           widget_type,
                        const GQuark   *classes,
                        guint           n_classes,
                        LrgUIPart       part,
                        LrgWidgetState  state,
                        LrgPartLook    *look)
{
	gchar        key[256];
	gsize        used;
	guint        i;
	LrgPartLook *overlay;

	g_return_if_fail (LRG_IS_THEME (self));
	g_return_if_fail (look != NULL);

	if (self->rules->len == 0)
		return;

	/* Cache key: type, part, state, then the class quarks */
	used = (gsize) g_snprintf (key, sizeof (key), "%" G_GSIZE_FORMAT ":%d:%u",
	                           (gsize) widget_type, (gint) part, (guint) state);
	for (i = 0; i < n_classes && used < sizeof (key) - 12; i++)
		used += (gsize) g_snprintf (key + used, sizeof (key) - used, ",%u", classes[i]);

	overlay = g_hash_table_lookup (self->look_cache, key);
	if (overlay == NULL)
	{
		g_autoptr(GArray) matches = g_array_new (FALSE, FALSE, sizeof (RuleMatch));

		for (i = 0; i < self->rules->len; i++)
		{
			LrgStyleRule *rule = g_ptr_array_index (self->rules, i);
			RuleMatch     m;

			if (!lrg_style_rule_matches (rule, widget_type, classes, n_classes, part, state))
				continue;

			m.specificity = lrg_style_rule_get_specificity (rule);
			m.order = i;
			m.rule = rule;
			g_array_append_val (matches, m);
		}

		g_array_sort (matches, compare_matches);

		overlay = lrg_part_look_new ();
		for (i = 0; i < matches->len; i++)
			lrg_part_look_merge (overlay,
			                     lrg_style_rule_get_look (g_array_index (matches, RuleMatch, i).rule));

		g_hash_table_insert (self->look_cache, g_strdup (key), overlay);
	}

	lrg_part_look_merge (look, overlay);
}

/* ==========================================================================
 * YAML loading
 * ========================================================================== */

static const gchar * const top_level_keys[] = {
	"name", "dark", "motion", "preset", "accent", "font", "font-family", "font-fallbacks",
	"font-size", "sdf-text", "colors", "metrics", "styles", NULL
};

static const gchar * const style_keys[] = {
	"widget", "class", "part", "state", "background", "foreground", "border",
	"border-width", "radius", "shadow", "shadow-blur", "shadow-offset", "gradient",
	"opacity", "size", "glow", "glow-size", "material", "glass", "effect",
	"effect-colors", "effect-intensity", NULL
};

static const gchar * const glass_keys[] = {
	"frost", "refraction", "saturation", "tint", "rim", "dispersion", "blur", NULL
};

static const gchar * const gradient_keys[] = {
	"kind", "start", "end", "angle", "center-x", "center-y", "radius", NULL
};

/* scalar_of: the scalar text of @node, NULL for non-scalars */
static const gchar *
scalar_of (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

static gboolean
check_keys (YamlMapping         *mapping,
            const gchar * const *allowed,
            const gchar         *where,
            GError             **error)
{
	GList *keys;
	GList *l;

	keys = yaml_mapping_get_members (mapping);
	for (l = keys; l != NULL; l = l->next)
	{
		if (!g_strv_contains (allowed, l->data))
		{
			g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			             "%s: unknown key '%s'", where, (const gchar *) l->data);
			g_list_free (keys);
			return FALSE;
		}
	}
	g_list_free (keys);
	return TRUE;
}

static gboolean
parse_color_node (YamlNode     *node,
                  GrlColor     *out,
                  const gchar  *where,
                  GError      **error)
{
	if (!lrg_color_parse (scalar_of (node), out))
	{
		g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
		             "%s: expected a hex colour like \"#7c6cff\" (quote it: '#' starts "
		             "a YAML comment)", where);
		return FALSE;
	}
	return TRUE;
}

static gboolean
parse_number_node (YamlNode     *node,
                   gfloat       *out,
                   const gchar  *where,
                   GError      **error)
{
	const gchar *text = scalar_of (node);
	gchar       *end = NULL;
	gdouble      v = 0.0;

	if (text != NULL)
		v = g_ascii_strtod (text, &end);
	if (text == NULL || end == text || (end != NULL && *end != '\0'))
	{
		g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
		             "%s: expected a number", where);
		return FALSE;
	}
	*out = (gfloat) v;
	return TRUE;
}

static gboolean
parse_enum_node (YamlNode     *node,
                 GType         type,
                 gint         *out,
                 const gchar  *where,
                 GError      **error)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	const gchar          *text = scalar_of (node);
	GEnumValue           *value;

	value = text != NULL ? g_enum_get_value_by_nick (klass, text) : NULL;
	if (value == NULL)
	{
		g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
		             "%s: unknown value '%s'", where, text != NULL ? text : "(not a string)");
		return FALSE;
	}
	*out = value->value;
	return TRUE;
}

/* parse_state_node: a single nick or a sequence of nicks */
static gboolean
parse_state_node (YamlNode        *node,
                  LrgWidgetState  *out,
                  GError         **error)
{
	g_autoptr(GFlagsClass) klass = g_type_class_ref (LRG_TYPE_WIDGET_STATE);
	LrgWidgetState         state = LRG_WIDGET_STATE_NONE;
	YamlSequence          *seq = NULL;
	guint                  n = 1;
	guint                  i;

	if (yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		seq = yaml_node_get_sequence (node);
		n = yaml_sequence_get_length (seq);
	}

	for (i = 0; i < n; i++)
	{
		YamlNode    *item = seq != NULL ? yaml_sequence_get_element (seq, i) : node;
		const gchar *text = scalar_of (item);
		GFlagsValue *value = text != NULL ? g_flags_get_value_by_nick (klass, text) : NULL;

		if (value == NULL)
		{
			g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			             "styles.state: unknown state '%s'", text != NULL ? text : "?");
			return FALSE;
		}
		state |= (LrgWidgetState) value->value;
	}

	*out = state;
	return TRUE;
}

static gboolean
parse_gradient (YamlNode          *node,
                LrgPaintGradient  *out,
                GError           **error)
{
	YamlMapping *m;
	gint         kind = LRG_PAINT_GRADIENT_LINEAR;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
		                     "styles.gradient: expected a mapping");
		return FALSE;
	}
	m = yaml_node_get_mapping (node);
	if (!check_keys (m, gradient_keys, "styles.gradient", error))
		return FALSE;

	memset (out, 0, sizeof (LrgPaintGradient));
	out->center_x = 0.5f;
	out->center_y = 0.5f;

	if (yaml_mapping_has_member (m, "kind") &&
	    !parse_enum_node (yaml_mapping_get_member (m, "kind"), LRG_TYPE_PAINT_GRADIENT_KIND,
	                      &kind, "styles.gradient.kind", error))
		return FALSE;
	out->kind = (LrgPaintGradientKind) kind;

	if (!yaml_mapping_has_member (m, "start") || !yaml_mapping_has_member (m, "end"))
	{
		g_set_error_literal (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
		                     "styles.gradient: needs 'start' and 'end'");
		return FALSE;
	}
	if (!parse_color_node (yaml_mapping_get_member (m, "start"), &out->start,
	                       "styles.gradient.start", error) ||
	    !parse_color_node (yaml_mapping_get_member (m, "end"), &out->end,
	                       "styles.gradient.end", error))
		return FALSE;

	if (yaml_mapping_has_member (m, "angle") &&
	    !parse_number_node (yaml_mapping_get_member (m, "angle"), &out->angle,
	                        "styles.gradient.angle", error))
		return FALSE;
	if (yaml_mapping_has_member (m, "center-x") &&
	    !parse_number_node (yaml_mapping_get_member (m, "center-x"), &out->center_x,
	                        "styles.gradient.center-x", error))
		return FALSE;
	if (yaml_mapping_has_member (m, "center-y") &&
	    !parse_number_node (yaml_mapping_get_member (m, "center-y"), &out->center_y,
	                        "styles.gradient.center-y", error))
		return FALSE;
	if (yaml_mapping_has_member (m, "radius") &&
	    !parse_number_node (yaml_mapping_get_member (m, "radius"), &out->radius,
	                        "styles.gradient.radius", error))
		return FALSE;

	return TRUE;
}

/*
 * parse_glass:
 *
 * The `glass:` mapping of a style rule. Unspecified keys keep the
 * defaults of lrg_paint_glass_init_default(); ranges are checked so a
 * typo like `frost: 30` fails loudly instead of rendering oddly.
 */
static gboolean
parse_glass (YamlNode       *node,
             LrgPaintGlass  *out,
             GError        **error)
{
	YamlMapping *m;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
		                     "styles.glass: expected a mapping");
		return FALSE;
	}
	m = yaml_node_get_mapping (node);
	if (!check_keys (m, glass_keys, "styles.glass", error))
		return FALSE;

	lrg_paint_glass_init_default (out);

#define GLASS_NUMBER(key, field, lo, hi) \
	if (yaml_mapping_has_member (m, key)) \
	{ \
		if (!parse_number_node (yaml_mapping_get_member (m, key), &out->field, \
		                        "styles.glass." key, error)) \
			return FALSE; \
		if (out->field < (lo) || out->field > (hi)) \
		{ \
			g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID, \
			             "styles.glass." key ": %g is outside %g..%g", \
			             (gdouble) out->field, (gdouble) (lo), (gdouble) (hi)); \
			return FALSE; \
		} \
	}

	GLASS_NUMBER ("frost", frost, 0.0f, 1.0f)
	GLASS_NUMBER ("refraction", refraction, 0.0f, 256.0f)
	GLASS_NUMBER ("saturation", saturation, 0.0f, 4.0f)
	GLASS_NUMBER ("rim", rim_light, 0.0f, 1.0f)
	GLASS_NUMBER ("dispersion", dispersion, 0.0f, 1.0f)
	GLASS_NUMBER ("blur", blur_radius, 0.0f, 256.0f)

#undef GLASS_NUMBER

	if (yaml_mapping_has_member (m, "tint") &&
	    !parse_color_node (yaml_mapping_get_member (m, "tint"), &out->tint,
	                       "styles.glass.tint", error))
		return FALSE;

	return TRUE;
}

/* parse_effect_colors: one colour or a list of one to three */
static gboolean
parse_effect_colors (YamlNode     *node,
                     LrgPartLook  *look,
                     GError      **error)
{
	GrlColor c[3];
	guint    n = 1;
	guint    i;

	if (yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (node);

		n = yaml_sequence_get_length (seq);
		if (n < 1 || n > 3)
		{
			g_set_error_literal (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			                     "styles.effect-colors: give one to three colours");
			return FALSE;
		}
		for (i = 0; i < n; i++)
			if (!parse_color_node (yaml_sequence_get_element (seq, i), &c[i],
			                       "styles.effect-colors", error))
				return FALSE;
	}
	else if (!parse_color_node (node, &c[0], "styles.effect-colors", error))
		return FALSE;

	lrg_part_look_set_effect_colors (look, &c[0], n > 1 ? &c[1] : NULL, n > 2 ? &c[2] : NULL);
	return TRUE;
}

/* parse_radius: one number for all corners or a list of four */
static gboolean
parse_radius (YamlNode        *node,
              LrgCornerRadii  *out,
              GError         **error)
{
	gfloat v[4];
	guint  i;

	if (yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (node);

		if (yaml_sequence_get_length (seq) != 4)
		{
			g_set_error_literal (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			                     "styles.radius: a list needs four values "
			                     "(top-left, top-right, bottom-right, bottom-left)");
			return FALSE;
		}
		for (i = 0; i < 4; i++)
			if (!parse_number_node (yaml_sequence_get_element (seq, i), &v[i],
			                        "styles.radius", error))
				return FALSE;
		out->top_left = v[0];
		out->top_right = v[1];
		out->bottom_right = v[2];
		out->bottom_left = v[3];
		return TRUE;
	}

	if (!parse_number_node (node, &v[0], "styles.radius", error))
		return FALSE;
	lrg_corner_radii_init_uniform (out, v[0]);
	return TRUE;
}

static LrgStyleRule *
parse_style (YamlNode  *node,
             guint      index,
             GError   **error)
{
	g_autoptr(LrgStyleRule) rule = NULL;
	YamlMapping            *m;
	LrgPartLook            *look;
	gint                    part = LRG_UI_PART_BOX;
	LrgWidgetState          state = LRG_WIDGET_STATE_NONE;
	GrlColor                c;
	gfloat                  f;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
		             "styles[%u]: expected a mapping", index);
		return NULL;
	}
	m = yaml_node_get_mapping (node);
	if (!check_keys (m, style_keys, "styles", error))
		return NULL;

	if (yaml_mapping_has_member (m, "part") &&
	    !parse_enum_node (yaml_mapping_get_member (m, "part"), LRG_TYPE_UI_PART, &part,
	                      "styles.part", error))
		return NULL;
	if (yaml_mapping_has_member (m, "state") &&
	    !parse_state_node (yaml_mapping_get_member (m, "state"), &state, error))
		return NULL;

	rule = lrg_style_rule_new (scalar_of (yaml_mapping_get_member (m, "widget")),
	                           scalar_of (yaml_mapping_get_member (m, "class")),
	                           (LrgUIPart) part, state);
	look = lrg_style_rule_get_look (rule);

#define COLOR_FIELD(key, setter) \
	if (yaml_mapping_has_member (m, key)) \
	{ \
		if (!parse_color_node (yaml_mapping_get_member (m, key), &c, "styles." key, error)) \
			return NULL; \
		setter; \
	}
#define NUMBER_FIELD(key, setter) \
	if (yaml_mapping_has_member (m, key)) \
	{ \
		if (!parse_number_node (yaml_mapping_get_member (m, key), &f, "styles." key, error)) \
			return NULL; \
		setter; \
	}

	COLOR_FIELD ("background", lrg_part_look_set_background (look, &c))
	COLOR_FIELD ("foreground", lrg_part_look_set_foreground (look, &c))
	COLOR_FIELD ("border", { look->border = c; look->set |= LRG_PART_LOOK_BORDER; })
	COLOR_FIELD ("shadow", { look->shadow = c; look->set |= LRG_PART_LOOK_SHADOW; })
	COLOR_FIELD ("glow", { look->glow = c; look->set |= LRG_PART_LOOK_GLOW; })
	NUMBER_FIELD ("border-width", { look->border_width = f; look->set |= LRG_PART_LOOK_BORDER_WIDTH; })
	NUMBER_FIELD ("shadow-blur", { look->shadow_blur = f; look->set |= LRG_PART_LOOK_SHADOW_BLUR; })
	NUMBER_FIELD ("shadow-offset", { look->shadow_offset_y = f; look->set |= LRG_PART_LOOK_SHADOW_OFFSET; })
	NUMBER_FIELD ("opacity", lrg_part_look_set_opacity (look, f))
	NUMBER_FIELD ("size", lrg_part_look_set_size (look, f))
	NUMBER_FIELD ("glow-size", { look->glow_size = f; look->set |= LRG_PART_LOOK_GLOW_SIZE; })

#undef COLOR_FIELD
#undef NUMBER_FIELD

	if (yaml_mapping_has_member (m, "radius"))
	{
		if (!parse_radius (yaml_mapping_get_member (m, "radius"), &look->radii, error))
			return NULL;
		look->set |= LRG_PART_LOOK_RADII;
	}
	if (yaml_mapping_has_member (m, "gradient"))
	{
		LrgPaintGradient g;

		if (!parse_gradient (yaml_mapping_get_member (m, "gradient"), &g, error))
			return NULL;
		lrg_part_look_set_gradient (look, &g);
	}
	if (yaml_mapping_has_member (m, "material"))
	{
		gint material = LRG_PAINT_MATERIAL_NONE;

		if (!parse_enum_node (yaml_mapping_get_member (m, "material"), LRG_TYPE_PAINT_MATERIAL,
		                      &material, "styles.material", error))
			return NULL;
		lrg_part_look_set_material (look, (LrgPaintMaterial) material);
	}
	if (yaml_mapping_has_member (m, "glass"))
	{
		LrgPaintGlass g;

		if (!parse_glass (yaml_mapping_get_member (m, "glass"), &g, error))
			return NULL;
		lrg_part_look_set_glass (look, &g);
	}
	if (yaml_mapping_has_member (m, "effect"))
	{
		const gchar *name = scalar_of (yaml_mapping_get_member (m, "effect"));

		/* "none" clears an inherited effect; anything else must be a
		 * built-in or already registered effect */
		if (g_strcmp0 (name, "none") == 0)
			lrg_part_look_set_effect (look, NULL);
		else if (name == NULL || lrg_paint_effect_lookup (name) < 0)
		{
			g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			             "styles.effect: unknown effect '%s' (register user effects "
			             "before loading the theme)", name != NULL ? name : "?");
			return NULL;
		}
		else
			lrg_part_look_set_effect (look, name);
	}
	if (yaml_mapping_has_member (m, "effect-colors") &&
	    !parse_effect_colors (yaml_mapping_get_member (m, "effect-colors"), look, error))
		return NULL;
	if (yaml_mapping_has_member (m, "effect-intensity"))
	{
		if (!parse_number_node (yaml_mapping_get_member (m, "effect-intensity"), &f,
		                        "styles.effect-intensity", error))
			return NULL;
		if (f < 0.0f || f > 4.0f)
		{
			g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			             "styles.effect-intensity: %g is outside 0..4", (gdouble) f);
			return NULL;
		}
		lrg_part_look_set_effect_intensity (look, f);
	}

	return g_steal_pointer (&rule);
}

/*
 * Staged values: everything is parsed and validated first, then
 * applied, so a document with an error leaves the theme untouched.
 */
typedef struct
{
	const Preset *preset;
	gchar        *name;
	gint          dark;          /* -1 unset */
	gint          motion;        /* -1 unset */
	gboolean      has_accent;
	GrlColor      accent;
	gchar        *font;
	gchar        *font_family;
	gchar       **font_fallbacks;
	gboolean      has_fallbacks;
	gint          sdf_text;      /* -1 unset */
	gboolean      color_set[LRG_THEME_COLOR_COUNT];
	GrlColor      colors[LRG_THEME_COLOR_COUNT];
	gboolean      metric_set[LRG_THEME_METRIC_COUNT];
	gfloat        metrics[LRG_THEME_METRIC_COUNT];
	GPtrArray    *rules;
} Staged;

static void
staged_clear (Staged *s)
{
	g_clear_pointer (&s->name, g_free);
	g_clear_pointer (&s->font, g_free);
	g_clear_pointer (&s->font_family, g_free);
	g_clear_pointer (&s->font_fallbacks, g_strfreev);
	g_clear_pointer (&s->rules, g_ptr_array_unref);
}

static gboolean
parse_document (const gchar  *data,
                gssize        length,
                const gchar  *base_dir,
                Staged       *s,
                GError      **error)
{
	g_autoptr(YamlParser) parser = yaml_parser_new ();
	g_autoptr(GError)     local = NULL;
	YamlNode             *root_node;
	YamlMapping          *root;
	YamlNode             *node;

	memset (s, 0, sizeof (Staged));
	s->dark = -1;
	s->motion = -1;
	s->sdf_text = -1;
	s->rules = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_style_rule_unref);

	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_PARSE,
		             "theme is not valid YAML: %s", local->message);
		return FALSE;
	}

	root_node = yaml_parser_get_root (parser);
	if (root_node == NULL || yaml_node_get_node_type (root_node) == YAML_NODE_NULL)
		return TRUE;   /* empty document: nothing to change */
	if (yaml_node_get_node_type (root_node) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, LRG_THEME_ERROR, LRG_THEME_ERROR_PARSE,
		                     "theme document must be a mapping");
		return FALSE;
	}
	root = yaml_node_get_mapping (root_node);
	if (!check_keys (root, top_level_keys, "theme", error))
		return FALSE;

	/* preset */
	if ((node = yaml_mapping_get_member (root, "preset")) != NULL)
	{
		s->preset = find_preset (scalar_of (node));
		if (s->preset == NULL)
		{
			g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			             "preset: unknown preset '%s'", scalar_of (node) ? scalar_of (node) : "?");
			return FALSE;
		}
	}

	/* name, dark, motion */
	if ((node = yaml_mapping_get_member (root, "name")) != NULL)
		s->name = g_strdup (scalar_of (node));
	if ((node = yaml_mapping_get_member (root, "dark")) != NULL)
	{
		const gchar *t = scalar_of (node);

		if (g_strcmp0 (t, "true") == 0 || g_strcmp0 (t, "yes") == 0)
			s->dark = 1;
		else if (g_strcmp0 (t, "false") == 0 || g_strcmp0 (t, "no") == 0)
			s->dark = 0;
		else
		{
			g_set_error_literal (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			                     "dark: expected true or false");
			return FALSE;
		}
	}
	if ((node = yaml_mapping_get_member (root, "motion")) != NULL &&
	    !parse_enum_node (node, LRG_TYPE_MOTION, &s->motion, "motion", error))
		return FALSE;

	/* accent */
	if ((node = yaml_mapping_get_member (root, "accent")) != NULL)
	{
		if (!parse_color_node (node, &s->accent, "accent", error))
			return FALSE;
		s->has_accent = TRUE;
	}

	/* font, resolved against the theme file's directory */
	if ((node = yaml_mapping_get_member (root, "font")) != NULL)
	{
		const gchar *t = scalar_of (node);

		if (t == NULL)
		{
			g_set_error_literal (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			                     "font: expected a file path");
			return FALSE;
		}
		s->font = (base_dir != NULL && !g_path_is_absolute (t))
		          ? g_build_filename (base_dir, t, NULL)
		          : g_strdup (t);
	}

	/* font-family: a family name for SDF text */
	if ((node = yaml_mapping_get_member (root, "font-family")) != NULL)
	{
		const gchar *t = scalar_of (node);

		if (t == NULL)
		{
			g_set_error_literal (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			                     "font-family: expected a family name");
			return FALSE;
		}
		s->font_family = g_strdup (t);
	}

	/* font-fallbacks: family names or font files, tried in order */
	if ((node = yaml_mapping_get_member (root, "font-fallbacks")) != NULL)
	{
		YamlSequence *seq;
		GPtrArray    *list;
		guint         n;
		guint         i;

		if (yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		{
			g_set_error_literal (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			                     "font-fallbacks: expected a list of family names or files");
			return FALSE;
		}
		seq = yaml_node_get_sequence (node);
		n = yaml_sequence_get_length (seq);
		list = g_ptr_array_new ();
		for (i = 0; i < n; i++)
		{
			const gchar *t = scalar_of (yaml_sequence_get_element (seq, i));

			if (t == NULL || *t == '\0')
			{
				g_ptr_array_free (list, TRUE);
				g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
				             "font-fallbacks[%u]: expected a family name or file", i);
				return FALSE;
			}
			/* files resolve against the theme file's directory */
			g_ptr_array_add (list, (base_dir != NULL && looks_like_path (t) && !g_path_is_absolute (t))
			                       ? g_build_filename (base_dir, t, NULL) : g_strdup (t));
		}
		g_ptr_array_add (list, NULL);
		s->font_fallbacks = (gchar **) g_ptr_array_free (list, FALSE);
		s->has_fallbacks = TRUE;
	}

	/* sdf-text: draw UI text from distance-field atlases */
	if ((node = yaml_mapping_get_member (root, "sdf-text")) != NULL)
	{
		const gchar *t = scalar_of (node);

		if (g_strcmp0 (t, "true") == 0 || g_strcmp0 (t, "yes") == 0)
			s->sdf_text = 1;
		else if (g_strcmp0 (t, "false") == 0 || g_strcmp0 (t, "no") == 0)
			s->sdf_text = 0;
		else
		{
			g_set_error_literal (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			                     "sdf-text: expected true or false");
			return FALSE;
		}
	}

	/* font-size: shorthand for metrics.font-size (metrics win) */
	if ((node = yaml_mapping_get_member (root, "font-size")) != NULL)
	{
		if (!parse_number_node (node, &s->metrics[LRG_THEME_METRIC_FONT_SIZE], "font-size", error))
			return FALSE;
		s->metric_set[LRG_THEME_METRIC_FONT_SIZE] = TRUE;
	}

	/* colors: role nick -> hex */
	if ((node = yaml_mapping_get_member (root, "colors")) != NULL)
	{
		g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_THEME_COLOR);
		YamlMapping          *m;
		GList                *keys;
		GList                *l;

		if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		{
			g_set_error_literal (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			                     "colors: expected a mapping of role: colour");
			return FALSE;
		}
		m = yaml_node_get_mapping (node);
		keys = yaml_mapping_get_members (m);
		for (l = keys; l != NULL; l = l->next)
		{
			GEnumValue        *v = g_enum_get_value_by_nick (klass, l->data);
			g_autofree gchar  *where = g_strdup_printf ("colors.%s", (const gchar *) l->data);

			if (v == NULL)
			{
				g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
				             "colors: unknown role '%s'", (const gchar *) l->data);
				g_list_free (keys);
				return FALSE;
			}
			if (!parse_color_node (yaml_mapping_get_member (m, l->data),
			                       &s->colors[v->value], where, error))
			{
				g_list_free (keys);
				return FALSE;
			}
			s->color_set[v->value] = TRUE;
		}
		g_list_free (keys);
	}

	/* metrics: metric nick -> number */
	if ((node = yaml_mapping_get_member (root, "metrics")) != NULL)
	{
		g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_THEME_METRIC);
		YamlMapping          *m;
		GList                *keys;
		GList                *l;

		if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		{
			g_set_error_literal (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			                     "metrics: expected a mapping of metric: number");
			return FALSE;
		}
		m = yaml_node_get_mapping (node);
		keys = yaml_mapping_get_members (m);
		for (l = keys; l != NULL; l = l->next)
		{
			GEnumValue       *v = g_enum_get_value_by_nick (klass, l->data);
			g_autofree gchar *where = g_strdup_printf ("metrics.%s", (const gchar *) l->data);

			if (v == NULL)
			{
				g_set_error (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
				             "metrics: unknown metric '%s'", (const gchar *) l->data);
				g_list_free (keys);
				return FALSE;
			}
			if (!parse_number_node (yaml_mapping_get_member (m, l->data),
			                        &s->metrics[v->value], where, error))
			{
				g_list_free (keys);
				return FALSE;
			}
			s->metric_set[v->value] = TRUE;
		}
		g_list_free (keys);
	}

	/* styles: list of rules */
	if ((node = yaml_mapping_get_member (root, "styles")) != NULL &&
	    yaml_node_get_node_type (node) != YAML_NODE_NULL)
	{
		YamlSequence *seq;
		guint         i;

		if (yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		{
			g_set_error_literal (error, LRG_THEME_ERROR, LRG_THEME_ERROR_INVALID,
			                     "styles: expected a list of rules");
			return FALSE;
		}
		seq = yaml_node_get_sequence (node);
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			LrgStyleRule *rule = parse_style (yaml_sequence_get_element (seq, i), i, error);

			if (rule == NULL)
				return FALSE;
			g_ptr_array_add (s->rules, rule);
		}
	}

	return TRUE;
}

static void
apply_staged (LrgTheme *self,
              Staged   *s)
{
	guint i;

	if (s->preset != NULL)
		apply_preset_values (self, s->preset);
	if (s->name != NULL)
	{
		g_free (self->name);
		self->name = g_steal_pointer (&s->name);
	}
	if (s->dark >= 0)
		self->dark = s->dark != 0;
	if (s->motion >= 0)
		self->motion = (LrgMotion) s->motion;
	if (s->has_accent)
		derive_accent (self, &s->accent);
	for (i = 0; i < LRG_THEME_COLOR_COUNT; i++)
		if (s->color_set[i])
			self->colors[i] = s->colors[i];
	for (i = 0; i < LRG_THEME_METRIC_COUNT; i++)
		if (s->metric_set[i])
			self->metrics[i] = s->metrics[i];
	for (i = 0; i < s->rules->len; i++)
		g_ptr_array_add (self->rules, lrg_style_rule_ref (g_ptr_array_index (s->rules, i)));

	if (s->font != NULL)
	{
		g_free (self->font_path);
		self->font_path = g_steal_pointer (&s->font);
		reset_fonts (self);
	}
	if (s->font_family != NULL)
	{
		g_free (self->font_family);
		self->font_family = g_steal_pointer (&s->font_family);
		reset_fonts (self);
	}
	if (s->has_fallbacks)
	{
		g_strfreev (self->font_fallbacks);
		self->font_fallbacks = g_steal_pointer (&s->font_fallbacks);
		reset_fonts (self);
	}
	if (s->sdf_text >= 0 && self->sdf_text != (s->sdf_text != 0))
	{
		self->sdf_text = s->sdf_text != 0;
		reset_fonts (self);
	}
}

static gboolean
load_with_base (LrgTheme     *self,
                const gchar  *data,
                gssize        length,
                const gchar  *base_dir,
                GError      **error)
{
	Staged s;

	if (!parse_document (data, length, base_dir, &s, error))
	{
		staged_clear (&s);
		return FALSE;
	}

	freeze_changed (self);
	apply_staged (self, &s);
	staged_clear (&s);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
	notify_changed (self);
	thaw_changed (self);

	return TRUE;
}

gboolean
lrg_theme_load_from_data (LrgTheme     *self,
                          const gchar  *data,
                          gssize        length,
                          GError      **error)
{
	g_return_val_if_fail (LRG_IS_THEME (self), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return load_with_base (self, data, length, NULL, error);
}

gboolean
lrg_theme_load_from_file (LrgTheme     *self,
                          const gchar  *path,
                          GError      **error)
{
	g_autofree gchar *contents = NULL;
	g_autofree gchar *dir = NULL;
	gsize             len;

	g_return_val_if_fail (LRG_IS_THEME (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_file_get_contents (path, &contents, &len, error))
		return FALSE;

	dir = g_path_get_dirname (path);
	if (!load_with_base (self, contents, (gssize) len, dir, error))
	{
		g_prefix_error (error, "%s: ", path);
		return FALSE;
	}
	return TRUE;
}

LrgTheme *
lrg_theme_new_from_data (const gchar  *data,
                         gssize        length,
                         GError      **error)
{
	g_autoptr(LrgTheme) self = lrg_theme_new ();

	if (!lrg_theme_load_from_data (self, data, length, error))
		return NULL;
	return g_steal_pointer (&self);
}

LrgTheme *
lrg_theme_new_from_file (const gchar  *path,
                         GError      **error)
{
	g_autoptr(LrgTheme) self = lrg_theme_new ();

	if (!lrg_theme_load_from_file (self, path, error))
		return NULL;
	return g_steal_pointer (&self);
}

/* ==========================================================================
 * YAML saving
 * ========================================================================== */

static const gchar *
enum_nick (GType type,
           gint  value)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	GEnumValue           *v = g_enum_get_value (klass, value);

	return v != NULL ? v->value_nick : NULL;
}

static void
set_color_member (YamlMapping    *m,
                  const gchar    *key,
                  const GrlColor *c)
{
	g_autofree gchar *hex = lrg_color_to_string (c);

	yaml_mapping_set_string_member (m, key, hex);
}

static YamlMapping *
rule_to_mapping (LrgStyleRule *rule)
{
	YamlMapping       *m = yaml_mapping_new ();
	const LrgPartLook *look = lrg_style_rule_get_look (rule);
	LrgWidgetState     state = lrg_style_rule_get_state (rule);

	if (lrg_style_rule_get_widget_type (rule) != NULL)
		yaml_mapping_set_string_member (m, "widget", lrg_style_rule_get_widget_type (rule));
	if (lrg_style_rule_get_style_class (rule) != NULL)
		yaml_mapping_set_string_member (m, "class", lrg_style_rule_get_style_class (rule));
	yaml_mapping_set_string_member (m, "part",
	                                enum_nick (LRG_TYPE_UI_PART, lrg_style_rule_get_part (rule)));

	if (state != LRG_WIDGET_STATE_NONE)
	{
		g_autoptr(GFlagsClass)  klass = g_type_class_ref (LRG_TYPE_WIDGET_STATE);
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();
		guint                   i;

		for (i = 0; i < klass->n_values; i++)
		{
			GFlagsValue *v = &klass->values[i];

			if (v->value != 0 && (state & (LrgWidgetState) v->value) == (LrgWidgetState) v->value)
				yaml_sequence_add_string_element (seq, v->value_nick);
		}
		yaml_mapping_set_sequence_member (m, "state", seq);
	}

	if (look->set & LRG_PART_LOOK_BACKGROUND)
		set_color_member (m, "background", &look->background);
	if (look->set & LRG_PART_LOOK_FOREGROUND)
		set_color_member (m, "foreground", &look->foreground);
	if (look->set & LRG_PART_LOOK_BORDER)
		set_color_member (m, "border", &look->border);
	if (look->set & LRG_PART_LOOK_BORDER_WIDTH)
		yaml_mapping_set_double_member (m, "border-width", look->border_width);
	if (look->set & LRG_PART_LOOK_RADII)
	{
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();

		yaml_sequence_add_double_element (seq, look->radii.top_left);
		yaml_sequence_add_double_element (seq, look->radii.top_right);
		yaml_sequence_add_double_element (seq, look->radii.bottom_right);
		yaml_sequence_add_double_element (seq, look->radii.bottom_left);
		yaml_mapping_set_sequence_member (m, "radius", seq);
	}
	if (look->set & LRG_PART_LOOK_SHADOW)
		set_color_member (m, "shadow", &look->shadow);
	if (look->set & LRG_PART_LOOK_SHADOW_BLUR)
		yaml_mapping_set_double_member (m, "shadow-blur", look->shadow_blur);
	if (look->set & LRG_PART_LOOK_SHADOW_OFFSET)
		yaml_mapping_set_double_member (m, "shadow-offset", look->shadow_offset_y);
	if (look->set & LRG_PART_LOOK_GRADIENT)
	{
		g_autoptr(YamlMapping) g = yaml_mapping_new ();

		yaml_mapping_set_string_member (g, "kind",
		                                enum_nick (LRG_TYPE_PAINT_GRADIENT_KIND, look->gradient.kind));
		set_color_member (g, "start", &look->gradient.start);
		set_color_member (g, "end", &look->gradient.end);
		yaml_mapping_set_double_member (g, "angle", look->gradient.angle);
		yaml_mapping_set_double_member (g, "center-x", look->gradient.center_x);
		yaml_mapping_set_double_member (g, "center-y", look->gradient.center_y);
		yaml_mapping_set_double_member (g, "radius", look->gradient.radius);
		yaml_mapping_set_mapping_member (m, "gradient", g);
	}
	if (look->set & LRG_PART_LOOK_OPACITY)
		yaml_mapping_set_double_member (m, "opacity", look->opacity);
	if (look->set & LRG_PART_LOOK_SIZE)
		yaml_mapping_set_double_member (m, "size", look->size);
	if (look->set & LRG_PART_LOOK_GLOW)
		set_color_member (m, "glow", &look->glow);
	if (look->set & LRG_PART_LOOK_GLOW_SIZE)
		yaml_mapping_set_double_member (m, "glow-size", look->glow_size);
	if (look->set & LRG_PART_LOOK_MATERIAL)
		yaml_mapping_set_string_member (m, "material",
		                                enum_nick (LRG_TYPE_PAINT_MATERIAL, look->material));
	if (look->set & LRG_PART_LOOK_GLASS)
	{
		g_autoptr(YamlMapping) g = yaml_mapping_new ();

		yaml_mapping_set_double_member (g, "frost", look->glass.frost);
		yaml_mapping_set_double_member (g, "refraction", look->glass.refraction);
		yaml_mapping_set_double_member (g, "saturation", look->glass.saturation);
		set_color_member (g, "tint", &look->glass.tint);
		yaml_mapping_set_double_member (g, "rim", look->glass.rim_light);
		yaml_mapping_set_double_member (g, "dispersion", look->glass.dispersion);
		yaml_mapping_set_double_member (g, "blur", look->glass.blur_radius);
		yaml_mapping_set_mapping_member (m, "glass", g);
	}
	if (look->set & LRG_PART_LOOK_EFFECT)
		yaml_mapping_set_string_member (m, "effect",
		                                look->effect != 0 ? g_quark_to_string (look->effect) : "none");
	if (look->set & LRG_PART_LOOK_EFFECT_COLORS)
	{
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();
		g_autofree gchar       *c1 = lrg_color_to_string (&look->effect_params.color1);
		g_autofree gchar       *c2 = lrg_color_to_string (&look->effect_params.color2);
		g_autofree gchar       *c3 = lrg_color_to_string (&look->effect_params.color3);

		yaml_sequence_add_string_element (seq, c1);
		yaml_sequence_add_string_element (seq, c2);
		yaml_sequence_add_string_element (seq, c3);
		yaml_mapping_set_sequence_member (m, "effect-colors", seq);
	}
	if (look->set & LRG_PART_LOOK_EFFECT_INTENSITY)
		yaml_mapping_set_double_member (m, "effect-intensity", look->effect_params.intensity);

	return m;
}

gchar *
lrg_theme_to_data (LrgTheme  *self,
                   gsize     *length,
                   GError   **error)
{
	g_autoptr(YamlMapping)   root = yaml_mapping_new ();
	g_autoptr(YamlMapping)   colors = yaml_mapping_new ();
	g_autoptr(YamlMapping)   metrics = yaml_mapping_new ();
	g_autoptr(YamlSequence)  styles = yaml_sequence_new ();
	g_autoptr(YamlNode)      root_node = NULL;
	g_autoptr(YamlGenerator) generator = NULL;
	guint                    i;

	g_return_val_if_fail (LRG_IS_THEME (self), NULL);

	if (self->name != NULL)
		yaml_mapping_set_string_member (root, "name", self->name);
	yaml_mapping_set_boolean_member (root, "dark", self->dark);
	yaml_mapping_set_string_member (root, "motion", enum_nick (LRG_TYPE_MOTION, self->motion));
	if (self->font_path != NULL)
		yaml_mapping_set_string_member (root, "font", self->font_path);
	if (self->font_family != NULL)
		yaml_mapping_set_string_member (root, "font-family", self->font_family);
	if (self->font_fallbacks != NULL)
	{
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();

		for (i = 0; self->font_fallbacks[i] != NULL; i++)
			yaml_sequence_add_string_element (seq, self->font_fallbacks[i]);
		yaml_mapping_set_sequence_member (root, "font-fallbacks", seq);
	}
	if (!self->sdf_text)
		yaml_mapping_set_boolean_member (root, "sdf-text", FALSE);

	for (i = 0; i < LRG_THEME_COLOR_COUNT; i++)
		set_color_member (colors, enum_nick (LRG_TYPE_THEME_COLOR, (gint) i), &self->colors[i]);
	yaml_mapping_set_mapping_member (root, "colors", colors);

	for (i = 0; i < LRG_THEME_METRIC_COUNT; i++)
		yaml_mapping_set_double_member (metrics, enum_nick (LRG_TYPE_THEME_METRIC, (gint) i),
		                                self->metrics[i]);
	yaml_mapping_set_mapping_member (root, "metrics", metrics);

	if (self->rules->len > 0)
	{
		for (i = 0; i < self->rules->len; i++)
		{
			g_autoptr(YamlMapping) m = rule_to_mapping (g_ptr_array_index (self->rules, i));

			yaml_sequence_add_mapping_element (styles, m);
		}
		yaml_mapping_set_sequence_member (root, "styles", styles);
	}

	root_node = yaml_node_new_mapping (root);
	generator = yaml_generator_new ();
	yaml_generator_set_root (generator, root_node);
	return yaml_generator_to_data (generator, length, error);
}

gboolean
lrg_theme_save_to_file (LrgTheme     *self,
                        const gchar  *path,
                        GError      **error)
{
	g_autofree gchar *data = NULL;
	gsize             len = 0;

	g_return_val_if_fail (LRG_IS_THEME (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	data = lrg_theme_to_data (self, &len, error);
	if (data == NULL)
		return FALSE;
	return g_file_set_contents (path, data, (gssize) len, error);
}

/* ==========================================================================
 * Palette generation and contrast
 * ========================================================================== */

gfloat
lrg_color_contrast_ratio (const GrlColor *a,
                          const GrlColor *b)
{
	g_return_val_if_fail (a != NULL, 1.0f);
	g_return_val_if_fail (b != NULL, 1.0f);

	/* WCAG relative luminance lives with the other colour maths in the
	 * paint module; this is the theme-level name for it */
	return lrg_paint_color_contrast (a, b);
}

/*
 * color_to_hsl:
 *
 * sRGB 8-bit colour to hue (0..1), saturation and lightness (0..1).
 * Grey colours report hue 0.
 */
static void
color_to_hsl (const GrlColor *c,
              gdouble        *h,
              gdouble        *s,
              gdouble        *l)
{
	gdouble r = (gdouble) c->r / 255.0;
	gdouble g = (gdouble) c->g / 255.0;
	gdouble b = (gdouble) c->b / 255.0;
	gdouble max = MAX (r, MAX (g, b));
	gdouble min = MIN (r, MIN (g, b));
	gdouble d = max - min;

	*l = (max + min) / 2.0;
	if (d <= 0.0)
	{
		*h = 0.0;
		*s = 0.0;
		return;
	}

	*s = *l > 0.5 ? d / (2.0 - max - min) : d / (max + min);
	if (max == r)
		*h = (g - b) / d + (g < b ? 6.0 : 0.0);
	else if (max == g)
		*h = (b - r) / d + 2.0;
	else
		*h = (r - g) / d + 4.0;
	*h /= 6.0;
}

/* hue_channel: one channel of the standard HSL -> RGB conversion */
static gdouble
hue_channel (gdouble p,
             gdouble q,
             gdouble t)
{
	if (t < 0.0)
		t += 1.0;
	if (t > 1.0)
		t -= 1.0;
	if (t < 1.0 / 6.0)
		return p + (q - p) * 6.0 * t;
	if (t < 0.5)
		return q;
	if (t < 2.0 / 3.0)
		return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
	return p;
}

/*
 * hsl_color:
 *
 * Hue/saturation/lightness (0..1, clamped) plus an 8-bit alpha to an
 * sRGB colour, rounded to the nearest channel value.
 */
static GrlColor
hsl_color (gdouble h,
           gdouble s,
           gdouble l,
           guint8  alpha)
{
	GrlColor out;
	gdouble  r;
	gdouble  g;
	gdouble  b;

	s = CLAMP (s, 0.0, 1.0);
	l = CLAMP (l, 0.0, 1.0);

	if (s <= 0.0)
	{
		r = g = b = l;
	}
	else
	{
		gdouble q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
		gdouble p = 2.0 * l - q;

		r = hue_channel (p, q, h + 1.0 / 3.0);
		g = hue_channel (p, q, h);
		b = hue_channel (p, q, h - 1.0 / 3.0);
	}

	out.r = (guint8) CLAMP ((gint) (r * 255.0 + 0.5), 0, 255);
	out.g = (guint8) CLAMP ((gint) (g * 255.0 + 0.5), 0, 255);
	out.b = (guint8) CLAMP ((gint) (b * 255.0 + 0.5), 0, 255);
	out.a = alpha;
	return out;
}

void
lrg_theme_generate_palette (LrgTheme       *self,
                            const GrlColor *accent,
                            gfloat          tint,
                            gfloat          depth,
                            gboolean        dark)
{
	GrlColor *c;
	GrlColor  status;
	gdouble   hue;
	gdouble   accent_s;
	gdouble   accent_l;
	gdouble   ns;       /* saturation of the surfaces */
	gdouble   ts;       /* saturation of text */
	gdouble   base;
	gdouble   step;
	guint     i;

	g_return_if_fail (LRG_IS_THEME (self));
	g_return_if_fail (accent != NULL);

	c = self->colors;
	tint = CLAMP (tint, 0.0f, 1.0f);
	depth = CLAMP (depth, 0.0f, 1.0f);
	dark = !!dark;

	/* The neutrals share the accent's hue; tint decides how strongly
	 * (a fully tinted dark theme reads as "navy", not "blue") */
	color_to_hsl (accent, &hue, &accent_s, &accent_l);
	ns = (gdouble) tint * (dark ? 0.32 : 0.36);
	ts = (gdouble) tint * 0.22;

	if (dark)
	{
		/* Depth pushes the window towards black and spreads the layers
		 * further apart, so cards lift off the window more */
		base = 0.115 - 0.075 * (gdouble) depth;
		step = 0.022 + 0.038 * (gdouble) depth;

		c[LRG_THEME_COLOR_WINDOW] = hsl_color (hue, ns, base, 255);
		c[LRG_THEME_COLOR_PANEL] = hsl_color (hue, ns, base + step, 255);
		c[LRG_THEME_COLOR_SURFACE] = hsl_color (hue, ns, base + 2.0 * step, 255);
		c[LRG_THEME_COLOR_POPUP] = hsl_color (hue, ns, base + 1.5 * step, 255);
		c[LRG_THEME_COLOR_CONTROL] = hsl_color (hue, ns, base + 2.0 * step + 0.015, 255);
		c[LRG_THEME_COLOR_CONTROL_HOVER] = hsl_color (hue, ns, base + 2.0 * step + 0.060, 255);
		c[LRG_THEME_COLOR_CONTROL_PRESSED] = hsl_color (hue, ns, base + 2.0 * step + 0.105, 255);
		c[LRG_THEME_COLOR_INPUT] = hsl_color (hue, ns, base - 0.35 * step, 255);
		c[LRG_THEME_COLOR_BORDER] = hsl_color (hue, ns, base + 2.0 * step + 0.075, 255);
		c[LRG_THEME_COLOR_TRACK] = hsl_color (hue, ns, base + 2.0 * step + 0.090, 255);
		c[LRG_THEME_COLOR_TOOLTIP] = hsl_color (hue, ns, base - 0.5 * step, 255);
		c[LRG_THEME_COLOR_TEXT] = hsl_color (hue, ts, 0.93, 255);
		c[LRG_THEME_COLOR_TEXT_MUTED] = hsl_color (hue, ts, 0.66, 255);
		c[LRG_THEME_COLOR_HANDLE] = hsl_color (hue, ts * 0.5, 0.96, 255);
		c[LRG_THEME_COLOR_DISABLED] = hsl_color (hue, ts, 0.42, 255);
		c[LRG_THEME_COLOR_SHADOW] = hsl_color (hue, 0.0, 0.0, 0xa8);
		c[LRG_THEME_COLOR_VEIL] = hsl_color (hue, ns, 0.02, 0x99);
	}
	else
	{
		/* Light: the panel is (nearly) white and everything else steps
		 * down from it; depth darkens the window and the controls */
		step = 0.018 + 0.030 * (gdouble) depth;
		base = 1.0 - 0.02 * (gdouble) depth;

		c[LRG_THEME_COLOR_PANEL] = hsl_color (hue, ns, base, 255);
		c[LRG_THEME_COLOR_WINDOW] = hsl_color (hue, ns, base - 2.2 * step - 0.01, 255);
		c[LRG_THEME_COLOR_SURFACE] = hsl_color (hue, ns, base - 0.9 * step, 255);
		c[LRG_THEME_COLOR_POPUP] = hsl_color (hue, ns, base, 255);
		c[LRG_THEME_COLOR_CONTROL] = hsl_color (hue, ns, base - 2.5 * step - 0.02, 255);
		c[LRG_THEME_COLOR_CONTROL_HOVER] = hsl_color (hue, ns, base - 2.5 * step - 0.055, 255);
		c[LRG_THEME_COLOR_CONTROL_PRESSED] = hsl_color (hue, ns, base - 2.5 * step - 0.09, 255);
		c[LRG_THEME_COLOR_INPUT] = hsl_color (hue, ns, base, 255);
		c[LRG_THEME_COLOR_BORDER] = hsl_color (hue, ns, base - 4.5 * step - 0.06, 255);
		c[LRG_THEME_COLOR_TRACK] = hsl_color (hue, ns, base - 4.5 * step - 0.07, 255);
		c[LRG_THEME_COLOR_TOOLTIP] = hsl_color (hue, ts, 0.16, 255);
		c[LRG_THEME_COLOR_TEXT] = hsl_color (hue, ts, 0.12, 255);
		c[LRG_THEME_COLOR_TEXT_MUTED] = hsl_color (hue, ts, 0.40, 255);
		c[LRG_THEME_COLOR_HANDLE] = hsl_color (hue, 0.0, 1.0, 255);
		c[LRG_THEME_COLOR_DISABLED] = hsl_color (hue, ts, 0.66, 255);
		c[LRG_THEME_COLOR_SHADOW] = hsl_color (hue, ns, 0.12, 0x38);
		c[LRG_THEME_COLOR_VEIL] = hsl_color (hue, ns, 0.08, 0x55);
	}

	/* Status colours: bright on dark, deeper on light, so both read */
	lrg_color_parse (dark ? "#34d399" : "#16a34a", &status);
	c[LRG_THEME_COLOR_SUCCESS] = status;
	lrg_color_parse (dark ? "#fbbf24" : "#d97706", &status);
	c[LRG_THEME_COLOR_WARNING] = status;
	lrg_color_parse (dark ? "#f87171" : "#dc2626", &status);
	c[LRG_THEME_COLOR_DANGER] = status;

	/* The accent family last: hover/pressed/link shades depend on the
	 * dark flag, exactly as lrg_theme_set_accent() derives them */
	self->dark = dark;
	derive_accent (self, accent);

	/* Links are body text: a bright accent (cyan, yellow) shaded for a
	 * light theme still washes out on white, so keep shading it
	 * towards the text side until it reads on panels and surfaces */
	for (i = 0; i < 25; i++)
	{
		GrlColor *link = &c[LRG_THEME_COLOR_LINK];

		if (lrg_paint_color_contrast (link, &c[LRG_THEME_COLOR_PANEL]) >= 4.5f &&
		    lrg_paint_color_contrast (link, &c[LRG_THEME_COLOR_SURFACE]) >= 4.5f)
			break;
		lrg_paint_color_shade (link, dark ? 0.06f : -0.06f, link);
	}

	/* Every legacy colour property changed; tell listeners once each */
	g_object_freeze_notify (G_OBJECT (self));
	for (i = 0; i < G_N_ELEMENTS (legacy_roles); i++)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIMARY_COLOR + i]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DARK]);
	g_object_thaw_notify (G_OBJECT (self));

	notify_changed (self);
}

/* Role pairs that carry text in the built-in widgets */
static const struct { LrgThemeColor fg; LrgThemeColor bg; } contrast_pairs[] = {
	{ LRG_THEME_COLOR_TEXT,           LRG_THEME_COLOR_WINDOW },
	{ LRG_THEME_COLOR_TEXT,           LRG_THEME_COLOR_PANEL },
	{ LRG_THEME_COLOR_TEXT,           LRG_THEME_COLOR_SURFACE },
	{ LRG_THEME_COLOR_TEXT,           LRG_THEME_COLOR_CONTROL },
	{ LRG_THEME_COLOR_TEXT,           LRG_THEME_COLOR_INPUT },
	{ LRG_THEME_COLOR_TEXT,           LRG_THEME_COLOR_POPUP },
	{ LRG_THEME_COLOR_TEXT_MUTED,     LRG_THEME_COLOR_PANEL },
	{ LRG_THEME_COLOR_TEXT_MUTED,     LRG_THEME_COLOR_SURFACE },
	{ LRG_THEME_COLOR_TEXT_ON_ACCENT, LRG_THEME_COLOR_ACCENT },
	{ LRG_THEME_COLOR_TEXT_ON_ACCENT, LRG_THEME_COLOR_ACCENT_HOVER },
	{ LRG_THEME_COLOR_TEXT_ON_ACCENT, LRG_THEME_COLOR_ACCENT_PRESSED },
	{ LRG_THEME_COLOR_LINK,           LRG_THEME_COLOR_PANEL }
};

guint
lrg_theme_get_n_contrast_pairs (void)
{
	return G_N_ELEMENTS (contrast_pairs);
}

gboolean
lrg_theme_get_contrast_pair (guint          index,
                             LrgThemeColor *foreground,
                             LrgThemeColor *background)
{
	if (index >= G_N_ELEMENTS (contrast_pairs))
		return FALSE;

	if (foreground != NULL)
		*foreground = contrast_pairs[index].fg;
	if (background != NULL)
		*background = contrast_pairs[index].bg;
	return TRUE;
}

guint
lrg_theme_count_contrast_failures (LrgTheme *self,
                                   gfloat    min_ratio)
{
	guint failures = 0;
	guint i;

	g_return_val_if_fail (LRG_IS_THEME (self), 0);

	for (i = 0; i < G_N_ELEMENTS (contrast_pairs); i++)
		if (lrg_paint_color_contrast (&self->colors[contrast_pairs[i].fg],
		                              &self->colors[contrast_pairs[i].bg]) < min_ratio)
			failures++;
	return failures;
}

/* Fraction steps tried when nudging a colour; the first that passes wins */
#define FIX_STEPS (64)

/*
 * nudge_towards:
 * @color: (inout): the colour to move
 * @other: the colour it must contrast with
 * @to_white: move towards white (%TRUE) or black
 * @min_ratio: the ratio to reach
 *
 * Mixes @color towards white or black by the smallest fraction (in
 * 1/FIX_STEPS steps) that reaches @min_ratio against @other, keeping
 * its alpha. Linear scan rather than bisection: contrast is not
 * monotonic in the mix when @color starts on the far side of @other.
 *
 * Returns: %TRUE if @min_ratio was reached; otherwise @color ends at
 *   the extreme
 */
static gboolean
nudge_towards (GrlColor       *color,
               const GrlColor *other,
               gboolean        to_white,
               gfloat          min_ratio)
{
	GrlColor goal;
	GrlColor mixed;
	guint    i;

	goal.r = goal.g = goal.b = to_white ? 255 : 0;
	goal.a = color->a;

	for (i = 1; i <= FIX_STEPS; i++)
	{
		lrg_paint_color_mix (color, &goal, (gfloat) i / (gfloat) FIX_STEPS, &mixed);
		if (lrg_paint_color_contrast (&mixed, other) >= min_ratio)
		{
			*color = mixed;
			return TRUE;
		}
	}

	*color = goal;
	return FALSE;
}

/* Upper bound on passes over the pair list; pairs sharing a role may
 * need a recheck after another pair moved it */
#define FIX_MAX_PASSES (8)

guint
lrg_theme_fix_contrast (LrgTheme *self,
                        gfloat    min_ratio)
{
	static const GrlColor white = { 255, 255, 255, 255 };
	static const GrlColor black = { 0, 0, 0, 255 };
	GrlColor              original[LRG_THEME_COLOR_COUNT];
	gboolean              to_white[LRG_THEME_COLOR_COUNT] = { FALSE };
	guint                 changed = 0;
	guint                 pass;
	guint                 i;

	g_return_val_if_fail (LRG_IS_THEME (self), 0);

	min_ratio = CLAMP (min_ratio, 1.0f, 21.0f);
	memcpy (original, self->colors, sizeof (original));

	/* Pick one direction per foreground role up front, from all the
	 * backgrounds it sits on: towards white if white's worst contrast
	 * beats black's. A fixed direction means foregrounds only ever
	 * move towards an extreme and backgrounds only away from it, so
	 * pairs sharing a role cannot undo each other */
	for (i = 0; i < G_N_ELEMENTS (contrast_pairs); i++)
	{
		LrgThemeColor fg = contrast_pairs[i].fg;
		gfloat        worst_white = 21.0f;
		gfloat        worst_black = 21.0f;
		guint         j;

		for (j = 0; j < G_N_ELEMENTS (contrast_pairs); j++)
		{
			if (contrast_pairs[j].fg != fg)
				continue;
			worst_white = MIN (worst_white, lrg_paint_color_contrast (&white, &self->colors[contrast_pairs[j].bg]));
			worst_black = MIN (worst_black, lrg_paint_color_contrast (&black, &self->colors[contrast_pairs[j].bg]));
		}
		to_white[fg] = worst_white >= worst_black;
	}

	for (pass = 0; pass < FIX_MAX_PASSES; pass++)
	{
		gboolean all_pass = TRUE;

		for (i = 0; i < G_N_ELEMENTS (contrast_pairs); i++)
		{
			GrlColor *fg = &self->colors[contrast_pairs[i].fg];
			GrlColor *bg = &self->colors[contrast_pairs[i].bg];
			gboolean  up = to_white[contrast_pairs[i].fg];

			if (lrg_paint_color_contrast (fg, bg) >= min_ratio)
				continue;
			all_pass = FALSE;

			/* 1. Move the text towards its extreme by the smallest
			 *    step that reads */
			if (nudge_towards (fg, bg, up, min_ratio))
				continue;

			/* 2. The text is pure white/black and still fails (a
			 *    mid-tone background): push the background away */
			nudge_towards (bg, fg, !up, min_ratio);
		}

		if (all_pass)
			break;
	}

	/* Count what moved and keep legacy property listeners informed */
	g_object_freeze_notify (G_OBJECT (self));
	for (i = 0; i < LRG_THEME_COLOR_COUNT; i++)
	{
		guint l;

		if (memcmp (&original[i], &self->colors[i], sizeof (GrlColor)) == 0)
			continue;
		changed++;
		for (l = 0; l < G_N_ELEMENTS (legacy_roles); l++)
			if (legacy_roles[l] == (LrgThemeColor) i)
				g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIMARY_COLOR + l]);
	}
	g_object_thaw_notify (G_OBJECT (self));

	if (changed > 0)
		notify_changed (self);
	return changed;
}
