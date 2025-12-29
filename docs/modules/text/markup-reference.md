# Markup Reference

Complete reference for BBCode-style markup tags supported by `LrgRichText`.

## Style Tags

### Bold: `[b]`

Makes text bold.

```
[b]Bold text[/b]
```

### Italic: `[i]`

Makes text italic.

```
[i]Italic text[/i]
```

### Underline: `[u]`

Underlines text.

```
[u]Underlined text[/u]
```

### Strikethrough: `[s]`

Strikes through text.

```
[s]Crossed out[/s]
```

---

## Color Tag: `[color]`

Changes text color.

### Hex Format

```
[color=#FF0000]Red[/color]
[color=#0F0]Green (short)[/color]
[color=#0000FF]Blue[/color]
```

### Named Colors

```
[color=red]Red[/color]
[color=green]Green[/color]
[color=blue]Blue[/color]
[color=yellow]Yellow[/color]
[color=white]White[/color]
[color=black]Black[/color]
[color=gray]Gray[/color]
[color=orange]Orange[/color]
[color=purple]Purple[/color]
[color=pink]Pink[/color]
[color=cyan]Cyan[/color]
```

### RGBA Format

```
[color=#FF000080]Semi-transparent red[/color]
```

---

## Size Tag: `[size]`

Scales the font size. Value is a multiplier of the base font size.

```
[size=0.5]Half size[/size]
[size=1.0]Normal size[/size]
[size=1.5]1.5x size[/size]
[size=2.0]Double size[/size]
```

---

## Effect Tags

### Shake: `[shake]`

Characters shake randomly.

```
[shake]Shaky text![/shake]
```

Parameters:
- `intensity` - Shake magnitude (default: 2.0)
- `speed` - Shake frequency (default: 20.0)

```
[shake intensity=5.0 speed=30.0]More intense![/shake]
```

### Wave: `[wave]`

Characters move in a wave pattern.

```
[wave]Wavy text~[/wave]
```

Parameters:
- `amplitude` - Wave height (default: 3.0)
- `frequency` - Wave frequency (default: 4.0)
- `speed` - Animation speed (default: 5.0)

```
[wave amplitude=5.0 frequency=2.0]Big slow wave[/wave]
```

### Rainbow: `[rainbow]`

Characters cycle through rainbow colors.

```
[rainbow]Colorful text![/rainbow]
```

Parameters:
- `speed` - Color cycling speed (default: 2.0)
- `saturation` - Color saturation 0-1 (default: 1.0)

```
[rainbow speed=5.0]Fast rainbow[/rainbow]
```

### Typewriter: `[typewriter]`

Characters appear one at a time.

```
[typewriter]Revealing text...[/typewriter]
```

Parameters:
- `speed` - Characters per second (default: 20.0)
- `delay` - Initial delay in seconds (default: 0.0)

```
[typewriter speed=10.0 delay=0.5]Slow reveal after delay[/typewriter]
```

### Pulse: `[pulse]`

Text scales up and down rhythmically.

```
[pulse]Pulsing![/pulse]
```

Parameters:
- `min_scale` - Minimum scale (default: 0.9)
- `max_scale` - Maximum scale (default: 1.1)
- `speed` - Pulse frequency (default: 3.0)

```
[pulse min_scale=0.8 max_scale=1.2]Big pulse[/pulse]
```

### Fade In: `[fadein]`

Text fades in from transparent.

```
[fadein]Fading in...[/fadein]
```

Parameters:
- `duration` - Fade duration in seconds (default: 1.0)
- `delay` - Delay before starting (default: 0.0)

```
[fadein duration=2.0]Slow fade[/fadein]
```

---

## Nesting Tags

Tags can be nested for combined effects:

```
[b][i]Bold and italic[/i][/b]
[color=#FF0000][b]Bold red[/b][/color]
[rainbow][wave]Rainbow wave[/wave][/rainbow]
[size=2.0][shake]Big shaky[/shake][/size]
```

### Nesting Order

Tags apply from inside out:

```
[b][color=#FF0000]text[/color][/b]
```

Result: Bold, red text

---

## Escaping

Use `[[` to output a literal `[`:

```
Use [[b]] for bold text
```

Output: `Use [b] for bold text`

---

## Examples

### Dialog Box

```
[color=#FFD700][b]NPC Name:[/b][/color] Hello, traveler!
Would you like to [color=#00FF00]accept[/color] this quest?
```

### Warning Message

```
[shake][color=#FF0000][b]WARNING![/b][/color][/shake]
Low health!
```

### Title Screen

```
[rainbow][wave][size=2.0]ADVENTURE QUEST[/size][/wave][/rainbow]

[fadein delay=1.0]Press START to begin[/fadein]
```

### Typewriter Dialog

```
[typewriter speed=25.0]The ancient door slowly creaks open,
revealing a [color=#8844FF]mysterious chamber[/color]
filled with [shake]untold treasures[/shake]...[/typewriter]
```

### Item Description

```
[b]Legendary Sword[/b]
[color=#FFD700]★★★★★[/color]

[i]A blade forged in dragon fire.[/i]

[color=#00FF00]+50 Attack[/color]
[color=#FF4444]+25% Critical[/color]
```

---

## Tag Summary

| Tag | Parameters | Description |
|-----|------------|-------------|
| `[b]` | - | Bold |
| `[i]` | - | Italic |
| `[u]` | - | Underline |
| `[s]` | - | Strikethrough |
| `[color=X]` | hex, name | Text color |
| `[size=X]` | float | Size multiplier |
| `[shake]` | intensity, speed | Random shake |
| `[wave]` | amplitude, frequency, speed | Sine wave motion |
| `[rainbow]` | speed, saturation | Color cycling |
| `[typewriter]` | speed, delay | Character reveal |
| `[pulse]` | min_scale, max_scale, speed | Scale animation |
| `[fadein]` | duration, delay | Alpha fade in |
