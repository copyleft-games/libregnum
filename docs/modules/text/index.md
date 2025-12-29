# Rich Text Module

The text module provides BBCode-style markup parsing, font management, and animated text effects.

## Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      LrgRichText                            │
│  ┌───────────────────────────────────────────────────────┐  │
│  │ [b]Bold[/b] and [color=#FF0000]red[/color] text      │  │
│  └───────────────────────────────────────────────────────┘  │
│                           ↓ parse                           │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐           │
│  │ Span 1  │ │ Span 2  │ │ Span 3  │ │ Span 4  │           │
│  │ "Bold"  │ │ " and " │ │ "red"   │ │ " text" │           │
│  │ bold=T  │ │ normal  │ │ #FF0000 │ │ normal  │           │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘           │
└─────────────────────────────────────────────────────────────┘
```

## Key Components

| Type | Description |
|------|-------------|
| `LrgFontManager` | Singleton for font loading and caching |
| `LrgRichText` | Parses BBCode markup into styled spans |
| `LrgTextSpan` | Single styled text run (GBoxed) |
| `LrgTextEffect` | Animated text effect base class |

## Quick Start

```c
#include <libregnum/text/lrg-rich-text.h>

/* Get font manager and load a font */
LrgFontManager *fonts = lrg_font_manager_get_default ();
lrg_font_manager_load (fonts, "main", "fonts/roboto.ttf", 24);

/* Create rich text with markup */
g_autoptr(LrgRichText) text = lrg_rich_text_new ();
lrg_rich_text_set_markup (text, "[b]Hello[/b] [color=#00FF00]World[/color]!");

/* In game loop */
void
update (gfloat delta_time)
{
    lrg_rich_text_update (text, delta_time);  /* Animate effects */
}

void
draw (void)
{
    lrg_rich_text_draw (text, 100.0f, 100.0f);
}
```

## Supported Markup Tags

| Tag | Description | Example |
|-----|-------------|---------|
| `[b]...[/b]` | Bold | `[b]Bold text[/b]` |
| `[i]...[/i]` | Italic | `[i]Italic text[/i]` |
| `[u]...[/u]` | Underline | `[u]Underlined[/u]` |
| `[color=X]...[/color]` | Text color | `[color=#FF0000]Red[/color]` |
| `[size=X]...[/size]` | Font size scale | `[size=2.0]Big[/size]` |
| `[shake]...[/shake]` | Shake effect | `[shake]Shaky![/shake]` |
| `[wave]...[/wave]` | Wave effect | `[wave]Wavy~[/wave]` |
| `[rainbow]...[/rainbow]` | Rainbow colors | `[rainbow]Colorful[/rainbow]` |
| `[typewriter]...[/typewriter]` | Character reveal | `[typewriter]Typing...[/typewriter]` |

## Documentation

- [Font Manager](font-manager.md) - Font loading and caching
- [Rich Text](rich-text.md) - Markup parsing and rendering
- [Markup Reference](markup-reference.md) - Complete tag documentation
- [Text Effects](effects.md) - Animated effect types
