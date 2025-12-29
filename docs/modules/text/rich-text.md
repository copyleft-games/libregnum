# Rich Text

`LrgRichText` parses BBCode-style markup into styled text spans and renders them with optional animated effects.

## Type

```c
#define LRG_TYPE_RICH_TEXT (lrg_rich_text_get_type ())
G_DECLARE_FINAL_TYPE (LrgRichText, lrg_rich_text, LRG, RICH_TEXT, GObject)
```

## Creation

```c
LrgRichText *lrg_rich_text_new (void);
LrgRichText *lrg_rich_text_new_with_markup (const gchar *markup);
```

## Markup

```c
void lrg_rich_text_set_markup (LrgRichText *self, const gchar *markup);
const gchar *lrg_rich_text_get_markup (LrgRichText *self);

void lrg_rich_text_set_plain_text (LrgRichText *self, const gchar *text);
const gchar *lrg_rich_text_get_plain_text (LrgRichText *self);
```

## Font

```c
void lrg_rich_text_set_font_name (LrgRichText *self, const gchar *name);
const gchar *lrg_rich_text_get_font_name (LrgRichText *self);

void lrg_rich_text_set_font_size (LrgRichText *self, gfloat size);
gfloat lrg_rich_text_get_font_size (LrgRichText *self);
```

## Default Style

```c
void lrg_rich_text_set_color (LrgRichText *self, GrlColor color);
GrlColor lrg_rich_text_get_color (LrgRichText *self);

void lrg_rich_text_set_line_spacing (LrgRichText *self, gfloat spacing);
gfloat lrg_rich_text_get_line_spacing (LrgRichText *self);
```

## Alignment

```c
typedef enum {
    LRG_TEXT_ALIGN_LEFT,
    LRG_TEXT_ALIGN_CENTER,
    LRG_TEXT_ALIGN_RIGHT
} LrgTextAlign;

void lrg_rich_text_set_alignment (LrgRichText *self, LrgTextAlign align);
LrgTextAlign lrg_rich_text_get_alignment (LrgRichText *self);
```

## Bounds

```c
void lrg_rich_text_set_max_width (LrgRichText *self, gfloat width);
gfloat lrg_rich_text_get_max_width (LrgRichText *self);

void lrg_rich_text_get_bounds (LrgRichText *self, gfloat *width, gfloat *height);
```

## Spans

```c
guint lrg_rich_text_get_span_count (LrgRichText *self);
LrgTextSpan *lrg_rich_text_get_span (LrgRichText *self, guint index);
```

## Update and Draw

```c
void lrg_rich_text_update (LrgRichText *self, gfloat delta_time);
void lrg_rich_text_draw (LrgRichText *self, gfloat x, gfloat y);
void lrg_rich_text_draw_ex (LrgRichText *self, gfloat x, gfloat y, gfloat alpha);
```

## Effect Control

```c
void lrg_rich_text_reset_effects (LrgRichText *self);
void lrg_rich_text_set_effect_speed (LrgRichText *self, gfloat speed);
gfloat lrg_rich_text_get_effect_speed (LrgRichText *self);
```

---

## Example: Basic Usage

```c
g_autoptr(LrgRichText) text = lrg_rich_text_new ();
lrg_rich_text_set_markup (text, "[b]Hello[/b] [i]World[/i]!");

/* Draw at position */
lrg_rich_text_draw (text, 100.0f, 100.0f);
```

---

## Example: Styled Dialog

```c
g_autoptr(LrgRichText) dialog = lrg_rich_text_new ();

/* Set defaults */
lrg_rich_text_set_font_name (dialog, "dialog");
lrg_rich_text_set_font_size (dialog, 18.0f);
lrg_rich_text_set_color (dialog, GRL_WHITE);
lrg_rich_text_set_max_width (dialog, 400.0f);
lrg_rich_text_set_line_spacing (dialog, 4.0f);

/* Set text with character name highlighted */
lrg_rich_text_set_markup (dialog,
    "[color=#FFD700][b]Elder:[/b][/color] "
    "You must travel to the [i]Ancient Temple[/i] "
    "and retrieve the [color=#FF4444]Crystal of Power[/color].");

/* Update and draw */
lrg_rich_text_update (dialog, delta_time);
lrg_rich_text_draw (dialog, 50.0f, 400.0f);
```

---

## Example: Animated Text

```c
g_autoptr(LrgRichText) title = lrg_rich_text_new ();
lrg_rich_text_set_alignment (title, LRG_TEXT_ALIGN_CENTER);
lrg_rich_text_set_font_size (title, 48.0f);

/* Rainbow animated title */
lrg_rich_text_set_markup (title, "[rainbow][wave]GAME OVER[/wave][/rainbow]");

/* In game loop */
void
update (gfloat delta_time)
{
    lrg_rich_text_update (title, delta_time);
}

void
draw (void)
{
    gfloat width, height;
    lrg_rich_text_get_bounds (title, &width, &height);
    lrg_rich_text_draw (title, (800.0f - width) / 2.0f, 300.0f);
}
```

---

## Example: Typewriter Effect

```c
g_autoptr(LrgRichText) message = lrg_rich_text_new ();
lrg_rich_text_set_markup (message, "[typewriter]Loading game data...[/typewriter]");
lrg_rich_text_set_effect_speed (message, 0.5f);  /* Slow reveal */

/* Reset to replay */
void
replay_message (void)
{
    lrg_rich_text_reset_effects (message);
}
```

---

## Example: Nested Tags

Tags can be nested for combined effects:

```c
/* Bold red text */
lrg_rich_text_set_markup (text, "[b][color=#FF0000]DANGER![/color][/b]");

/* Large, bold, rainbow wave */
lrg_rich_text_set_markup (text,
    "[size=2.0][b][rainbow][wave]VICTORY![/wave][/rainbow][/b][/size]");
```

---

## Parsing Rules

1. **Case insensitive**: `[B]`, `[b]`, and `[Bold]` are equivalent
2. **Auto-close**: Unclosed tags are closed at end of string
3. **Escape**: Use `[[` to output a literal `[`
4. **Unknown tags**: Preserved as literal text
5. **Color formats**: `#RGB`, `#RRGGBB`, or named colors (`red`, `green`, `blue`, etc.)

---

## Performance

- Markup is parsed once when set via `set_markup()`
- Spans are cached until markup changes
- Effects update per-frame only if the text has animated tags
- Use `set_plain_text()` for frequently changing text without markup
