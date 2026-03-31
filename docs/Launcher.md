# Orbit12 Launcher Specification

## Responsibility

Launcher is the first app started by root `code.py`.

It must:

1. List runnable apps from `/apps`
2. Exclude the `Launcher` folder itself
3. Render each app as `AppName >`
4. Add `[Settings]` as the last item for global settings
5. Launch selected app entrypoint

## App Discovery

For each folder in `/apps/<AppName>/`:

1. Ignore if folder name is `Launcher`
2. Resolve entrypoint by priority:
   1. `/apps/<AppName>/code.py`
   2. `/apps/<AppName>/<AppName>.py`

Folder name is the temporary display name until optional metadata file support is added.

## UI Behavior

1. Encoder rotate: move selection
2. Encoder press:
   - app row -> launch app
   - `[Settings]` -> open global settings page (stub allowed initially)

## Launch Contract

Launcher should import and execute the selected app entry with basic failure handling.

Recommended failure behavior:

1. Show error text on display
2. Keep launcher alive
3. Allow user to return to app list

## Future Extension (Not Required Yet)

- optional `app.json` per app for name/version/description/icon
- sorting and hidden apps
- app metadata-based category sections
