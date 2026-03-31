# Orbit12 Architecture

## Project Layout

The runtime layout should mirror the MacroPad CIRCUITPY drive.

```text
/
  code.py
  /apps
    /Launcher
      code.py
    /Orbit12MidiCommander
      orbit12midicommander.py (or code.py)
  /lib
  /userdata
    global_settings.json
    /Orbit12MidiCommander
      settings.json
      /presets
        preset1.json
```

In this repository, `DeviceFiles/` represents that device root.

## Startup Flow

1. `code.py` at root is the primary entrypoint.
2. It starts `Launcher`.
3. Launcher discovers runnable apps under `/apps`.
4. Selecting an app runs that app entry file.

## App Model

Each app lives in its own folder under `/apps/<AppName>/`.

- Preferred entry: `/apps/<AppName>/code.py`
- Fallback entry: `/apps/<AppName>/<AppName>.py`

Apps should expose a predictable runtime loop pattern (input -> update -> render) even if the exact API is lightweight at first.

UI definition model:

- simple apps: static `ui.json`
- complex apps: generated `ui.py` (returns schema at runtime)
- both use the shared `lib/orbit12ui` menu/parameter runtime

## Shared Areas

- `/lib` contains shared libraries (Adafruit + Orbit12 shared libs such as `orbit12ui`)
- `/userdata` contains all mutable user data
- App code should not write into `/apps` or `/lib` during runtime

## Data Ownership

- Global settings: `/userdata/global_settings.json`
- App settings: `/userdata/<AppName>/settings.json`
- App presets: `/userdata/<AppName>/presets/*.json`

## Repo-to-Device Mapping

- Repo source mirror: `DeviceFiles/`
- Device root on MacroPad: `/`
- Export/deploy step may compile `.py` to `.mpy`, but source authoring stays in Python.
