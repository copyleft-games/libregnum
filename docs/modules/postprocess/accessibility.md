# Colorblind Accessibility Filter

`LrgColorblindFilter` provides color correction and simulation for players with color vision deficiency.

## Type

```c
#define LRG_TYPE_COLORBLIND_FILTER (lrg_colorblind_filter_get_type ())
G_DECLARE_FINAL_TYPE (LrgColorblindFilter, lrg_colorblind_filter, LRG, COLORBLIND_FILTER, LrgPostEffect)
```

## Colorblindness Types

```c
typedef enum {
    LRG_COLORBLIND_NONE,          /* No filter */
    LRG_COLORBLIND_DEUTERANOPIA,  /* Red-green (green-weak, most common) */
    LRG_COLORBLIND_PROTANOPIA,    /* Red-green (red-weak) */
    LRG_COLORBLIND_TRITANOPIA,    /* Blue-yellow (rare) */
    LRG_COLORBLIND_ACHROMATOPSIA  /* Total color blindness (very rare) */
} LrgColorblindType;
```

## Filter Modes

```c
typedef enum {
    LRG_COLORBLIND_MODE_SIMULATE,  /* Show what colorblind users see */
    LRG_COLORBLIND_MODE_CORRECT    /* Adjust colors to improve visibility */
} LrgColorblindMode;
```

## Functions

```c
LrgColorblindFilter *lrg_colorblind_filter_new (void);
LrgColorblindFilter *lrg_colorblind_filter_new_with_type (LrgColorblindType filter_type);

LrgColorblindType lrg_colorblind_filter_get_filter_type (LrgColorblindFilter *self);
void lrg_colorblind_filter_set_filter_type (LrgColorblindFilter *self, LrgColorblindType filter_type);

LrgColorblindMode lrg_colorblind_filter_get_mode (LrgColorblindFilter *self);
void lrg_colorblind_filter_set_mode (LrgColorblindFilter *self, LrgColorblindMode mode);

gfloat lrg_colorblind_filter_get_strength (LrgColorblindFilter *self);
void lrg_colorblind_filter_set_strength (LrgColorblindFilter *self, gfloat strength);
```

## Usage

### Accessibility Option

Add a colorblind option to your game settings:

```c
static LrgColorblindFilter *cb_filter = NULL;

void
init_accessibility (void)
{
    cb_filter = lrg_colorblind_filter_new ();
    lrg_colorblind_filter_set_mode (cb_filter, LRG_COLORBLIND_MODE_CORRECT);
    lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (cb_filter));

    /* Start disabled */
    lrg_post_effect_set_enabled (LRG_POST_EFFECT (cb_filter), FALSE);
}

void
on_colorblind_setting_changed (LrgColorblindType type)
{
    if (type == LRG_COLORBLIND_NONE)
    {
        lrg_post_effect_set_enabled (LRG_POST_EFFECT (cb_filter), FALSE);
    }
    else
    {
        lrg_colorblind_filter_set_filter_type (cb_filter, type);
        lrg_post_effect_set_enabled (LRG_POST_EFFECT (cb_filter), TRUE);
    }
}
```

### Simulation Mode (Developer Tool)

Use simulation mode during development to test your game's color accessibility:

```c
/* Developer tool: show what players with deuteranopia see */
g_autoptr(LrgColorblindFilter) sim = lrg_colorblind_filter_new_with_type (LRG_COLORBLIND_DEUTERANOPIA);
lrg_colorblind_filter_set_mode (sim, LRG_COLORBLIND_MODE_SIMULATE);
```

### Strength Adjustment

Allow players to adjust the correction strength:

```c
/* Slider: 0% to 100% correction */
void
on_strength_slider_changed (gfloat value)
{
    lrg_colorblind_filter_set_strength (cb_filter, value);
}
```

## Design Tips for Colorblind Accessibility

1. **Don't rely on color alone** - Use shapes, patterns, or icons alongside color
2. **Test with simulation mode** - See your game through colorblind eyes
3. **Use high contrast** - Ensure important elements are distinguishable
4. **Consider deuteranopia first** - It's the most common type (~6% of males)

## Common Color Confusions

| Type | Confused Colors |
|------|-----------------|
| Deuteranopia | Red/green, orange/green, brown/green |
| Protanopia | Red/green, red/black, purple/blue |
| Tritanopia | Blue/green, yellow/pink |
| Achromatopsia | All colors appear as grayscale |

## Settings Menu Example

```c
/* Typical accessibility menu options */
static const gchar *colorblind_options[] = {
    "None",
    "Deuteranopia (Red-Green)",
    "Protanopia (Red-Green)",
    "Tritanopia (Blue-Yellow)",
    "Achromatopsia (Grayscale)"
};

static LrgColorblindType option_to_type[] = {
    LRG_COLORBLIND_NONE,
    LRG_COLORBLIND_DEUTERANOPIA,
    LRG_COLORBLIND_PROTANOPIA,
    LRG_COLORBLIND_TRITANOPIA,
    LRG_COLORBLIND_ACHROMATOPSIA
};
```
