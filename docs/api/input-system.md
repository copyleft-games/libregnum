# Input System

The input system provides a unified, extensible interface for handling all types of input in libregnum games. It abstracts away direct calls to graylib, allowing for multiple input sources to be aggregated and enabling advanced features like input mocking, AI control, and remote input.

## Architecture

```
LrgInputManager (singleton, aggregates all sources)
       |
       +-- LrgInput (abstract base class, derivable)
             |
             +-- LrgInputKeyboard (keyboard input via graylib)
             +-- LrgInputMouse (mouse input via graylib)
             +-- LrgInputGamepad (gamepad input via graylib)
             +-- LrgInputMock (testing/simulation)
             +-- LrgInputSoftware (AI/MCP control)
```

### Key Design Principles

1. **Abstraction**: Games never call `grl_input_*` functions directly; they use `LrgInputManager` or `LrgInput` subclasses
2. **Extensibility**: New input sources can be added by subclassing `LrgInput`
3. **Aggregation**: Multiple input sources are combined with type-specific rules
4. **Priority**: Sources are queried in priority order (higher priority first)

## LrgInput Base Class

`LrgInput` is an abstract derivable type that defines the interface for all input sources. Subclasses override only the methods relevant to their input type.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `name` | `gchar*` | Human-readable name for this input source |
| `enabled` | `gboolean` | Whether this source is active (default: TRUE) |
| `priority` | `gint` | Query order (higher = first, default: 0) |

### Virtual Methods

All virtual methods have default implementations that return `FALSE` or `0`. Subclasses override only what they support:

```c
/* Keyboard */
gboolean (*is_key_pressed)  (LrgInput *self, GrlKey key);
gboolean (*is_key_down)     (LrgInput *self, GrlKey key);
gboolean (*is_key_released) (LrgInput *self, GrlKey key);

/* Mouse */
gboolean (*is_mouse_button_pressed)  (LrgInput *self, GrlMouseButton button);
gboolean (*is_mouse_button_down)     (LrgInput *self, GrlMouseButton button);
gboolean (*is_mouse_button_released) (LrgInput *self, GrlMouseButton button);
void     (*get_mouse_position) (LrgInput *self, gfloat *x, gfloat *y);
void     (*get_mouse_delta)    (LrgInput *self, gfloat *dx, gfloat *dy);

/* Gamepad */
gboolean (*is_gamepad_available)       (LrgInput *self, gint gamepad);
gboolean (*is_gamepad_button_pressed)  (LrgInput *self, gint gamepad, GrlGamepadButton btn);
gboolean (*is_gamepad_button_down)     (LrgInput *self, gint gamepad, GrlGamepadButton btn);
gboolean (*is_gamepad_button_released) (LrgInput *self, gint gamepad, GrlGamepadButton btn);
gfloat   (*get_gamepad_axis)           (LrgInput *self, gint gamepad, GrlGamepadAxis axis);
```

## LrgInputManager

`LrgInputManager` is a singleton that aggregates multiple `LrgInput` sources. It provides the same query methods as `LrgInput` but aggregates results across all enabled sources.

### Getting the Manager

```c
LrgInputManager *input = lrg_input_manager_get_default ();
```

The default manager automatically registers:
- `LrgInputKeyboard` (name: "keyboard")
- `LrgInputMouse` (name: "mouse")
- `LrgInputGamepad` (name: "gamepad")

### Adding Custom Sources

```c
LrgInputMock *mock = lrg_input_mock_new ();
lrg_input_set_priority (LRG_INPUT (mock), 100);  /* Higher than default sources */
lrg_input_manager_add_source (input, LRG_INPUT (mock));
```

### Aggregation Rules

Different query types use different aggregation strategies:

| Query Type | Aggregation | Description |
|------------|-------------|-------------|
| Button/Key pressed/down/released | **OR** | Returns `TRUE` if any source reports `TRUE` |
| Mouse position | **First-wins** | Returns position from highest-priority source |
| Mouse delta | **Sum** | Adds deltas from all sources |
| Gamepad axis | **Max absolute** | Returns value with largest absolute magnitude |

## Concrete Input Sources

### LrgInputKeyboard

Wraps graylib's keyboard functions. Implements:
- `is_key_pressed`
- `is_key_down`
- `is_key_released`

```c
LrgInput *keyboard = lrg_input_keyboard_new ();
```

### LrgInputMouse

Wraps graylib's mouse functions. Implements:
- `is_mouse_button_pressed`
- `is_mouse_button_down`
- `is_mouse_button_released`
- `get_mouse_position`
- `get_mouse_delta`

```c
LrgInput *mouse = lrg_input_mouse_new ();
```

### LrgInputGamepad

Wraps graylib's gamepad functions. Implements:
- `is_gamepad_available`
- `is_gamepad_button_pressed`
- `is_gamepad_button_down`
- `is_gamepad_button_released`
- `get_gamepad_axis`

```c
LrgInput *gamepad = lrg_input_gamepad_new ();
```

### LrgInputMock (Testing)

Simulates any input type for unit testing and integration testing. Provides methods to programmatically set input state.

```c
LrgInputMock *mock = lrg_input_mock_new ();

/* Simulate key press */
lrg_input_mock_set_key_state (mock, GRL_KEY_SPACE, LRG_KEY_STATE_PRESSED);

/* Simulate mouse movement */
lrg_input_mock_set_mouse_position (mock, 400.0f, 300.0f);
lrg_input_mock_set_mouse_delta (mock, 10.0f, -5.0f);

/* Simulate gamepad */
lrg_input_mock_set_gamepad_button (mock, 0, GRL_GAMEPAD_BUTTON_A, TRUE);
lrg_input_mock_set_gamepad_axis (mock, 0, GRL_GAMEPAD_AXIS_LEFT_X, 0.75f);
```

### LrgInputSoftware (AI/MCP)

For programmatic control by AI agents or MCP servers. Similar to mock but designed for runtime use.

```c
LrgInputSoftware *ai_input = lrg_input_software_new ();

/* AI agent can control the game */
lrg_input_software_press_key (ai_input, GRL_KEY_W);
lrg_input_software_move_mouse (ai_input, 100.0f, 50.0f);
```

## Usage Examples

### Basic Input Polling

```c
LrgInputManager *input = lrg_input_manager_get_default ();

/* In game loop, poll first */
lrg_input_manager_poll (input);

/* Check keyboard */
if (lrg_input_manager_is_key_pressed (input, GRL_KEY_SPACE))
    player_jump ();

if (lrg_input_manager_is_key_down (input, GRL_KEY_W))
    player_move_forward ();

/* Check mouse */
gfloat mouse_x, mouse_y;
lrg_input_manager_get_mouse_position (input, &mouse_x, &mouse_y);

if (lrg_input_manager_is_mouse_button_pressed (input, GRL_MOUSE_BUTTON_LEFT))
    player_shoot (mouse_x, mouse_y);

/* Check gamepad */
if (lrg_input_manager_is_gamepad_available (input, 0))
{
    gfloat axis_x = lrg_input_manager_get_gamepad_axis (input, 0, GRL_GAMEPAD_AXIS_LEFT_X);
    gfloat axis_y = lrg_input_manager_get_gamepad_axis (input, 0, GRL_GAMEPAD_AXIS_LEFT_Y);
    player_move (axis_x, axis_y);
}
```

### Using with LrgInputBinding

The existing `LrgInputBinding` system automatically uses `LrgInputManager`:

```c
LrgInputBinding *jump = lrg_input_binding_new_keyboard (GRL_KEY_SPACE, LRG_INPUT_MODIFIER_NONE);

if (lrg_input_binding_is_pressed (jump))
    player_jump ();
```

### Disabling Input Temporarily

```c
/* Disable all input (e.g., during cutscene) */
lrg_input_manager_set_enabled (input, FALSE);

/* Disable specific source */
LrgInput *keyboard = lrg_input_manager_get_source (input, "keyboard");
lrg_input_set_enabled (keyboard, FALSE);
```

### Testing with Mock Input

```c
static void
test_player_movement (void)
{
    LrgInputManager *input = lrg_input_manager_get_default ();
    LrgInputMock    *mock  = lrg_input_mock_new ();

    /* Add mock with high priority so it overrides real input */
    lrg_input_set_priority (LRG_INPUT (mock), 1000);
    lrg_input_manager_add_source (input, LRG_INPUT (mock));

    /* Simulate W key being held */
    lrg_input_mock_set_key_state (mock, GRL_KEY_W, LRG_KEY_STATE_DOWN);

    /* Update player */
    player_update (player, 1.0f / 60.0f);

    /* Verify player moved forward */
    g_assert_cmpfloat (player->position.z, <, 0.0f);

    /* Cleanup */
    lrg_input_manager_remove_source (input, LRG_INPUT (mock));
    g_object_unref (mock);
}
```

## Creating Custom Input Sources

To create a custom input source, subclass `LrgInput` and override the relevant virtual methods:

```c
#define MY_TYPE_NETWORK_INPUT (my_network_input_get_type ())
G_DECLARE_FINAL_TYPE (MyNetworkInput, my_network_input, MY, NETWORK_INPUT, LrgInput)

struct _MyNetworkInput
{
    LrgInput parent_instance;

    /* Track remote player's input state */
    gboolean keys_down[512];
    gfloat   mouse_x;
    gfloat   mouse_y;
};

G_DEFINE_FINAL_TYPE (MyNetworkInput, my_network_input, LRG_TYPE_INPUT)

static gboolean
my_network_input_is_key_down (LrgInput *self,
                              GrlKey    key)
{
    MyNetworkInput *network = MY_NETWORK_INPUT (self);

    if (key >= 0 && key < 512)
        return network->keys_down[key];

    return FALSE;
}

static void
my_network_input_class_init (MyNetworkInputClass *klass)
{
    LrgInputClass *input_class = LRG_INPUT_CLASS (klass);

    input_class->is_key_down = my_network_input_is_key_down;
    /* Override other methods as needed */
}
```

## Header Files

| Header | Description |
|--------|-------------|
| `lrg-input.h` | Abstract base class |
| `lrg-input-manager.h` | Singleton manager |
| `lrg-input-keyboard.h` | Keyboard source |
| `lrg-input-mouse.h` | Mouse source |
| `lrg-input-gamepad.h` | Gamepad source |
| `lrg-input-mock.h` | Mock source for testing |
| `lrg-input-software.h` | Programmatic control |

## See Also

- [Input Bindings](input-binding.md) - High-level action-to-input mapping
- [Input Maps](input-map.md) - Collections of input bindings
- [Best Practices](../best-practices.md) - General coding guidelines
