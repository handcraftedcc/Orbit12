# Orbit12 UI Parameter System

## Goal

Create a reusable parameter-driven UI library in:

- `lib/orbit12ui/`

The UI is defined by JSON (`ui.json`) and rendered as a vertical list:

- label on the left
- value on the right

## Navigation Model

1. Encoder rotate (normal mode): move selection between parameters.
2. Encoder press:
   - enters edit mode for editable parameters
   - executes action for button parameters
   - enters folder for folder parameters
3. Encoder rotate (edit mode): changes value of the selected parameter.
4. Encoder press (edit mode): confirm/exit edit mode.

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

- `✓` (accept/save text)
- `←` (delete previous character)
- `✗` (cancel edit)
- `A` ... `Z`
- `0` ... `9`

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
- display should show `[Folder]`

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
