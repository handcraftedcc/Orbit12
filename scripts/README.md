# Orbit12 Install Scripts

## Script

- `scripts/install_to_circuitpy.py`

This script:
1. Copies `DeviceFiles/` to `build/`
2. Compiles non-entry `.py` files to `.mpy`
3. Deploys `build/` to CIRCUITPY using `rsync`
4. Preserves `userdata/` on device (excluded from overwrite)
5. Skips reinstalling existing Adafruit libraries by default (faster installs)

## Requirements

- Python 3
- `rsync`
- `mpy-cross` (or pass `--mpy-cross /path/to/mpy-cross`)

## Common Commands

### Build Only

```bash
python3 scripts/install_to_circuitpy.py --build-only --mpy-cross /path/to/mpy-cross
```

### Preview Install (Dry Run)

```bash
python3 scripts/install_to_circuitpy.py --dry-run --device /Volumes/CIRCUITPY --mpy-cross /path/to/mpy-cross
```

### Build + Install

```bash
python3 scripts/install_to_circuitpy.py --device /Volumes/CIRCUITPY --mpy-cross /path/to/mpy-cross
```

### Force Reinstall Adafruit Libraries

```bash
python3 scripts/install_to_circuitpy.py --device /Volumes/CIRCUITPY --mpy-cross /path/to/mpy-cross --reinstall-adafruit-libraries
```

### Recovery Install (No Compile)

If your installed `mpy-cross` is not CircuitPython-compatible, use:

```bash
python3 scripts/install_to_circuitpy.py --device /Volumes/CIRCUITPY --no-compile
```

## Notes

- Runtime entry files stay as `.py` intentionally:
  - `/code.py`
  - `/apps/<AppName>/code.py`
  - `/apps/<AppName>/<AppName>.py`
- Other Python sources are compiled to `.mpy` in `build/`.
- The script validates `mpy-cross` output before install and exits if incompatible.
