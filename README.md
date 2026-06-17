# Orbit12

Orbit 12 is an open-source USB MIDI Keyboard Instrument Software built for the rather affordable Adafruit Macropad RP2050 (~$50). It currently ships with one app:
ORION an incredibly flexible modular chain based MIDI instrument.

I started this project with the desire for a portable midi keyboard that was both, tiny and powerful, filled with tools to make composing both fun and easy, even without deep music theory knowledge.

The whole app is scale aware, and includes a variety of input modules, and midi processors.


Start here:

- [UX guide](docs/UX.md)
- [Input modules](docs/Input-Modules.md)
- [Processing modules](docs/Modules.md)
- [Architecture](docs/Architecture.md)
- [Module development guide](docs/Module-Dev-Guide.md)

## What You Need

- An Adafruit MacroPad RP2040 Starter Kit: [product page](https://www.adafruit.com/product/5128)
- CircuitPython installed on the board: [Adafruit install guide](https://learn.adafruit.com/welcome-to-circuitpython/installing-circuitpython)
- Recommended firmware: [CircuitPython 10.1.4](https://github.com/adafruit/circuitpython/releases/tag/10.1.4)

This project was built and tested around the MacroPad RP2040 and CircuitPython's memory and timing limits. It is meant for that class of device, not for generic desktop MIDI workflows.

Orion is the only app in the project right now. Additional apps such as a step sequencer and MIDI tracker are planned for later.

## Install

There are two supported ways to get Orbit12 onto a board.

### Option 1: Use a Release Build

First install CircuitPython on the MacroPad, using the [Adafruit install guide](https://learn.adafruit.com/welcome-to-circuitpython/installing-circuitpython).

Download the prebuilt Orbit12 package from the [Releases page](https://github.com/handcraftedcc/Orbit12/releases) and copy its contents to `CIRCUITPY`.

If you use a release build, you still need the board to be running CircuitPython 10.1.4 or a compatible nearby release.

### Option 2: Build From Source

1. Install CircuitPython on the MacroPad, ideally version 10.1.4.
2. Clone or unpack this repository.
3. Build the CircuitPython tree with the helpers in `tools/`.
4. Copy the generated tree to the `CIRCUITPY` drive.

The project keeps source under `src/` and generated `.mpy` output under `src_mpy/`. Loading plain `.py` files directly to the MacroPad works, but it increases memory pressure, so built `.mpy` files are strongly recommended.

The main helpers are:

- `tools/build_mpy.py`
- `tools/build_sync_mpy.sh`
- `tools/build_sync_current_mpy.py`

The `tools/` folder also includes a PyCharm settings zip. It installs external tools and macros for building and sending files to the MacroPad, and it is the recommended way to work on both `.py` and `.mpy` files while developing this project.

Useful supporting resources:

- Adafruit CircuitPython bundle and library downloads: [circuitpython.org/libraries](https://circuitpython.org/libraries)
- CircuitPython build helper notes: use the CircuitPython `mpy-cross` compiler, not the MicroPython one

## Project Layout

- `src/`: source tree that lands on the board
- `src_mpy/`: compiled output tree for release/build sync
- `tools/`: build and sync helpers
- `docs/`: user and contributor documentation

## About AI Involvement

Some background:
One of the goals of this projects for me was to up my python game - a language I already used in Houdini for scripting, but I wanted to get better at it.
I did initially try a purely "vibecoded approach" but this quickly failed, as the RP2040 ram is very limited for an app of this size and depth, and AI was unable to write consice and focused enough code in this case, and I kept getting out of memory errors.

Thus, most of Orbit12 is hand coded initially but AI was used heavily during the project for planning, debugging, optimization, learning CircuitPython APIs, generating supporting musical data such as scale-related material, some late stage refactoring and helping with documentation.

A few late-stage additions and modules (like the scene saving or the arpwalk module) were fully AI coded, then reviewed and integrated into the rest of the app, primarily to save time and repetitive work - as my free time is limited.

## Inspiration
A lot of this projects inspiration was taken from [Schwung](https://github.com/charlesvestal/schwung) - an amazing "hack" for the Ableton Move, to which I contributed a few modules.

## Open Source

Orbit12 is open source. See the repository license for the exact terms.
