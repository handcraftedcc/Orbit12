# Orbit12 UI Parameter System

## Goal

Create a reusable parameter-driven UI library in:

- `lib/orbit12ui/`

The UI can be defined by JSON (`ui.json`) for simple apps or generated with Python (`ui.py`) for
complex apps. Both feed the same parameter/menu engine and render as a vertical list:

- label on the left
- value on the right

## Navigation Model

1. Encoder rotate (normal mode): move selection between parameters.
2. Encoder press:
   - if selected row is `..`, go to previous menu level
   - enters edit mode for editable parameters
   - executes action for button parameters
   - enters folder for folder parameters
3. Encoder rotate (edit mode): changes value of the selected parameter.
4. Encoder press (edit mode): confirm/exit edit mode.
5. `..` appears only in folder/subfolder contexts (not at app root).

## Display Constraints (MacroPad 128x64)

- Use 4 content rows + 1 status row when a title is shown.
- Row width target is 21 monospace characters.
- Parameter rows render as:
  - 1 char selector prefix (`>` or space)
  - 20 chars content area
- List windowing keeps the selected row on the second-to-last visible row when possible, so one
  row below remains visible as a "what's next" preview.
- Label/value rendering:
  - both sides truncate with `..` when not selected and overflowing
  - selected overflowing rows scroll horizontally (marquee) to reveal hidden text

## Common Parameter Fields

All parameter entries should support:

- `type` (required)
- `name` (required, code-facing identifier)
- `label` (optional, display name; falls back to `name` if omitted)
- `default` (optional, type-specific)
- `viscondition` (optional, conditional visibility expression)

## Supported Types

## `int`

Options:

- `min`
- `max`
- `step`
- `default`

## `float`

Options:

- `min`
- `max`
- `step`
- `default`
- `decimals` (display precision)
- `mode` (`default` or `percent`)

`percent` mode:

- value domain is still numeric and not clamped to 0..1 unless app chooses so
- display should render `value * 100` with `%`
- supports negative and >100% values

## `boolean`

Options:

- `mode`: `truefalse` or `onoff`
- `default`

## `enum`

Options:

- `items` (list of strings)
- `wrap` (bool, default `true`)
- `default` (index or item value)

## `string`

Options:

- `default`
- editing uses uppercase letters and digits only

String edit character cycle must include, in wrap order:

- `OK` (accept/save text)
- `<-` (delete previous character)
- `ESC` (cancel edit)
- `A` ... `Z`
- `0` ... `9`

Notes:

- Default symbol mode is ASCII for maximum compatibility with `terminalio.FONT`.
- Optional `symbolMode: "unicode"` can use `✓`, `←`, `✗` when a font with those glyphs is available.

Example editing flow:

- start: `[A]`
- choose `B` then confirm letter: `B[A]`
- finish with accept: `BANGER[✓]`

## `rate`

Used for musical rates.

Options:

- `includeBars` (bool)
- `includeTriplets` (bool)
- `default`

Base order should include:

- bars descending: `16 bars ... 2 bars` (when `includeBars=true`)
- then note rates: `1/1`, `1/1T`, `1/2`, `1/2T`, `1/4`, `1/4T`, `1/8`, `1/8T`, `1/16`, `1/16T`, `1/32`, `1/32T`

If `includeBars=false`, start at `1/1`.
If `includeTriplets=false`, omit all `T` values.

## `note`

Used for musical note selection.

Options:

- `includeOctaves` (bool)
- `octaveRange` (two-int array, supports negatives, e.g. `[-2, 8]`)
- `default`

Behavior:

- `includeOctaves=false`: cycle pitch classes only (`C`, `C#`, ..., `B`)
- `includeOctaves=true`: cycle full note names with octave (`C0`, `C#0`, ..., `B8`) based on `octaveRange`

## `button`

Behavior:

- selecting triggers a callback/action
- display value side should be `Button >`

Options:

- `action` or callback identifier

## `folder`

Behavior:

- contains child parameters
- selecting enters the folder
- display row should show `[FolderName]` in the label area
- folder rows should not show a value text

Options:

- `children` (list of parameters)

## `ui.json` Structure

Use top-level tab/section style for folder grouping.

Example:

```json
{
  "tabs": [
    {
      "name": "main",
      "label": "Main",
      "items": [
        { "type": "int", "name": "swing", "label": "Swing", "min": 0, "max": 100, "step": 1, "default": 0 },
        { "type": "folder", "name": "advanced", "label": "Advanced", "children": [] }
      ]
    }
  ]
}
```

## Generated UI (`ui.py`) for complex apps

For apps that need runtime-generated menus (hardware scan, dynamic module list, profile-dependent
options), use a Python UI source and load it with `orbit12ui.load_ui(path)`.

Supported Python entrypoints:

- `build_ui()` function that returns a schema
- `UI_SCHEMA` constant
- `TABS` list

All forms map into the same tab schema as JSON and use the same parameter classes.

Example:

```python
def build_ui():
    modules = ["ARP", "CHORDS", "CC"]
    return {
        "tabs": [
            {
                "name": "main",
                "items": [
                    {"type": "enum", "name": "module", "items": modules},
                    {"type": "int", "name": "swing", "min": 0, "max": 100, "default": 0},
                ],
            }
        ]
    }
```

## Conditional Visibility (`viscondition`)

Each parameter may include an expression referencing another parameter.

Supported operators:

- `==`
- `!=`
- `<`
- `>`
- `<=`
- `>=`

Rules:

- Numeric comparisons use numeric values.
- `==` and `!=` must support strings.
- Expression example: `viscondition: "mode == \"ARP\""` or `viscondition: "steps < 5"`
