# Text Effects

`LrgTextEffect` is the base class for animated text effects. Each effect modifies the position, scale, rotation, or color of individual characters.

## Base Type

```c
#define LRG_TYPE_TEXT_EFFECT (lrg_text_effect_get_type ())
G_DECLARE_DERIVABLE_TYPE (LrgTextEffect, lrg_text_effect, LRG, TEXT_EFFECT, GObject)
```

## Virtual Methods

```c
struct _LrgTextEffectClass
{
    GObjectClass parent_class;

    void (*update)    (LrgTextEffect *self, gfloat delta_time);
    void (*apply)     (LrgTextEffect *self, guint char_index, gfloat *x, gfloat *y,
                       gfloat *scale, gfloat *rotation, GrlColor *color);
    void (*reset)     (LrgTextEffect *self);
    gboolean (*is_complete) (LrgTextEffect *self);
};
```

## Base Properties

```c
gfloat lrg_text_effect_get_speed (LrgTextEffect *self);
void lrg_text_effect_set_speed (LrgTextEffect *self, gfloat speed);

gfloat lrg_text_effect_get_time (LrgTextEffect *self);
```

## Base Methods

```c
void lrg_text_effect_update (LrgTextEffect *self, gfloat delta_time);
void lrg_text_effect_apply (LrgTextEffect *self, guint char_index,
                             gfloat *x, gfloat *y, gfloat *scale,
                             gfloat *rotation, GrlColor *color);
void lrg_text_effect_reset (LrgTextEffect *self);
gboolean lrg_text_effect_is_complete (LrgTextEffect *self);
```

---

## Built-in Effects

### Shake Effect

Random position offset per character.

```c
#define LRG_TYPE_TEXT_EFFECT_SHAKE (lrg_text_effect_shake_get_type ())
G_DECLARE_FINAL_TYPE (LrgTextEffectShake, lrg_text_effect_shake, LRG, TEXT_EFFECT_SHAKE, LrgTextEffect)

LrgTextEffect *lrg_text_effect_shake_new (void);
LrgTextEffect *lrg_text_effect_shake_new_with_params (gfloat intensity, gfloat speed);

void lrg_text_effect_shake_set_intensity (LrgTextEffectShake *self, gfloat intensity);
gfloat lrg_text_effect_shake_get_intensity (LrgTextEffectShake *self);
```

**Behavior**: Each character gets a random X/Y offset that changes rapidly.

---

### Wave Effect

Sine wave vertical motion.

```c
#define LRG_TYPE_TEXT_EFFECT_WAVE (lrg_text_effect_wave_get_type ())
G_DECLARE_FINAL_TYPE (LrgTextEffectWave, lrg_text_effect_wave, LRG, TEXT_EFFECT_WAVE, LrgTextEffect)

LrgTextEffect *lrg_text_effect_wave_new (void);
LrgTextEffect *lrg_text_effect_wave_new_with_params (gfloat amplitude, gfloat frequency, gfloat speed);

void lrg_text_effect_wave_set_amplitude (LrgTextEffectWave *self, gfloat amplitude);
gfloat lrg_text_effect_wave_get_amplitude (LrgTextEffectWave *self);

void lrg_text_effect_wave_set_frequency (LrgTextEffectWave *self, gfloat frequency);
gfloat lrg_text_effect_wave_get_frequency (LrgTextEffectWave *self);
```

**Behavior**: Characters move up and down in a wave pattern. Phase is offset per character index.

```
offset_y = amplitude * sin(time * speed + char_index * frequency)
```

---

### Rainbow Effect

Color cycling through hue spectrum.

```c
#define LRG_TYPE_TEXT_EFFECT_RAINBOW (lrg_text_effect_rainbow_get_type ())
G_DECLARE_FINAL_TYPE (LrgTextEffectRainbow, lrg_text_effect_rainbow, LRG, TEXT_EFFECT_RAINBOW, LrgTextEffect)

LrgTextEffect *lrg_text_effect_rainbow_new (void);
LrgTextEffect *lrg_text_effect_rainbow_new_with_params (gfloat speed, gfloat saturation);

void lrg_text_effect_rainbow_set_saturation (LrgTextEffectRainbow *self, gfloat saturation);
gfloat lrg_text_effect_rainbow_get_saturation (LrgTextEffectRainbow *self);
```

**Behavior**: Each character's hue is offset by its index, creating a rainbow gradient that scrolls over time.

```
hue = (time * speed + char_index * 0.1) mod 1.0
color = hsv_to_rgb(hue, saturation, 1.0)
```

---

### Typewriter Effect

Progressive character reveal.

```c
#define LRG_TYPE_TEXT_EFFECT_TYPEWRITER (lrg_text_effect_typewriter_get_type ())
G_DECLARE_FINAL_TYPE (LrgTextEffectTypewriter, lrg_text_effect_typewriter, LRG, TEXT_EFFECT_TYPEWRITER, LrgTextEffect)

LrgTextEffect *lrg_text_effect_typewriter_new (void);
LrgTextEffect *lrg_text_effect_typewriter_new_with_params (gfloat chars_per_second, gfloat delay);

void lrg_text_effect_typewriter_set_chars_per_second (LrgTextEffectTypewriter *self, gfloat cps);
gfloat lrg_text_effect_typewriter_get_chars_per_second (LrgTextEffectTypewriter *self);

void lrg_text_effect_typewriter_set_delay (LrgTextEffectTypewriter *self, gfloat delay);
gfloat lrg_text_effect_typewriter_get_delay (LrgTextEffectTypewriter *self);

guint lrg_text_effect_typewriter_get_visible_count (LrgTextEffectTypewriter *self);
```

**Behavior**: Characters are hidden (alpha = 0) until their reveal time. `is_complete()` returns TRUE when all characters are visible.

---

### Pulse Effect

Rhythmic scale animation.

```c
#define LRG_TYPE_TEXT_EFFECT_PULSE (lrg_text_effect_pulse_get_type ())
G_DECLARE_FINAL_TYPE (LrgTextEffectPulse, lrg_text_effect_pulse, LRG, TEXT_EFFECT_PULSE, LrgTextEffect)

LrgTextEffect *lrg_text_effect_pulse_new (void);
LrgTextEffect *lrg_text_effect_pulse_new_with_params (gfloat min_scale, gfloat max_scale, gfloat speed);

void lrg_text_effect_pulse_set_min_scale (LrgTextEffectPulse *self, gfloat scale);
gfloat lrg_text_effect_pulse_get_min_scale (LrgTextEffectPulse *self);

void lrg_text_effect_pulse_set_max_scale (LrgTextEffectPulse *self, gfloat scale);
gfloat lrg_text_effect_pulse_get_max_scale (LrgTextEffectPulse *self);
```

**Behavior**: All characters scale uniformly between min and max scale using a sine wave.

```
scale = min_scale + (max_scale - min_scale) * (sin(time * speed) * 0.5 + 0.5)
```

---

### Fade In Effect

One-time alpha fade.

```c
#define LRG_TYPE_TEXT_EFFECT_FADE_IN (lrg_text_effect_fade_in_get_type ())
G_DECLARE_FINAL_TYPE (LrgTextEffectFadeIn, lrg_text_effect_fade_in, LRG, TEXT_EFFECT_FADE_IN, LrgTextEffect)

LrgTextEffect *lrg_text_effect_fade_in_new (void);
LrgTextEffect *lrg_text_effect_fade_in_new_with_params (gfloat duration, gfloat delay);

void lrg_text_effect_fade_in_set_duration (LrgTextEffectFadeIn *self, gfloat duration);
gfloat lrg_text_effect_fade_in_get_duration (LrgTextEffectFadeIn *self);

void lrg_text_effect_fade_in_set_delay (LrgTextEffectFadeIn *self, gfloat delay);
gfloat lrg_text_effect_fade_in_get_delay (LrgTextEffectFadeIn *self);
```

**Behavior**: After `delay` seconds, alpha fades from 0 to 1 over `duration` seconds. `is_complete()` returns TRUE when fully visible.

---

## Creating Custom Effects

Subclass `LrgTextEffect` to create custom effects:

```c
#define MY_TYPE_TEXT_EFFECT_WOBBLE (my_text_effect_wobble_get_type ())
G_DECLARE_FINAL_TYPE (MyTextEffectWobble, my_text_effect_wobble, MY, TEXT_EFFECT_WOBBLE, LrgTextEffect)

struct _MyTextEffectWobble
{
    LrgTextEffect parent;
    gfloat angle;
};

static void
my_text_effect_wobble_apply (LrgTextEffect *effect,
                              guint          char_index,
                              gfloat        *x,
                              gfloat        *y,
                              gfloat        *scale,
                              gfloat        *rotation,
                              GrlColor      *color)
{
    MyTextEffectWobble *self = MY_TEXT_EFFECT_WOBBLE (effect);
    gfloat time = lrg_text_effect_get_time (effect);

    /* Rotate each character based on time and index */
    *rotation = sinf (time * 3.0f + char_index * 0.5f) * self->angle;
}

static void
my_text_effect_wobble_class_init (MyTextEffectWobbleClass *klass)
{
    LrgTextEffectClass *effect_class = LRG_TEXT_EFFECT_CLASS (klass);
    effect_class->apply = my_text_effect_wobble_apply;
}
```

---

## Effect Application Order

When multiple effects are combined (nested tags), they are applied in sequence:

1. Position offset (x, y)
2. Scale
3. Rotation
4. Color modification

Each effect receives the accumulated values from previous effects and can modify them further.

---

## Performance Considerations

1. **Effect updates**: Only effects currently in use are updated
2. **Stateless effects**: Shake, wave, rainbow are computed on-the-fly from time
3. **Stateful effects**: Typewriter, fade-in track progress and complete when done
4. **Reset**: Call `reset()` to replay one-shot effects like typewriter and fade-in
