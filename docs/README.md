# Orbit12 Documentation

This folder is the canonical project documentation for architecture, UI, launcher behavior, and development constraints.

## Documents

- `Architecture.md` - overall project and on-device file architecture
- `DevGuidelines.md` - CircuitPython and RP2040 coding guidelines
- `UI.md` - Orbit12 UI parameter library and `ui.json` schema
- `Launcher.md` - app launcher behavior and discovery rules
- `ParamLab.md` - test app specification for all parameter types/options
- `AdafruitLibraryAudit.md` - existing reusable bundle/device capabilities and gaps
- `CurrentStatus.md` - implemented vs placeholder state in `DeviceFiles/`

## Source of Truth

- `DeviceFiles/` is treated as the source mirror of what will exist on-device.
- Build/export can convert Python sources to `.mpy`, but behavior and structure must remain aligned with these docs.
