# Orbit12 UI + Launcher Bootstrap Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement the reusable `orbit12ui` parameter system, launcher boot flow, and a ParamLab validation app in `DeviceFiles/`.

**Architecture:** Build a pure-Python `lib/orbit12ui` core that is device-friendly and unit-testable. Keep hardware interaction thin inside app entrypoints (`apps/Launcher`, `apps/ParamLab`) by using `adafruit_macropad` for input/display and orbit12ui for parameter behavior. Root `code.py` becomes a simple launcher bootstrap.

**Tech Stack:** CircuitPython, Adafruit MacroPad library (`adafruit_macropad`), Python `unittest` (host-side validation), JSON schema-driven UI.

---

### Task 1: Add failing tests for UI parameter core

**Files:**
- Create: `tests/test_orbit12ui_parameters.py`
- Create: `tests/test_orbit12ui_menu.py`

**Step 1: Write the failing tests**

Write tests for:
- int/float clamp and step behavior
- float percent display formatting
- boolean modes (`truefalse`, `onoff`)
- enum wrap behavior on/off
- string editor (`✓`, `←`, `✗`, `A-Z`, `0-9`) flow
- rate list generation with includeBars/includeTriplets options
- note list generation with/without octaves and negative ranges
- viscondition operators (`==`, `!=`, `<`, `>`, `<=`, `>=`)
- folder navigation and button action via menu controller

**Step 2: Run tests to verify they fail**

Run: `python3 -m unittest discover -s tests -p 'test_orbit12ui*.py' -v`
Expected: FAIL/ERROR because `orbit12ui` modules do not exist.

**Step 3: Write minimal implementation**

Implement orbit12ui core modules to satisfy test behavior.

**Step 4: Run tests to verify they pass**

Run: `python3 -m unittest discover -s tests -p 'test_orbit12ui*.py' -v`
Expected: PASS.

### Task 2: Implement `lib/orbit12ui` schema + menu core

**Files:**
- Create: `DeviceFiles/lib/orbit12ui/__init__.py`
- Create: `DeviceFiles/lib/orbit12ui/conditions.py`
- Create: `DeviceFiles/lib/orbit12ui/parameters.py`
- Create: `DeviceFiles/lib/orbit12ui/menu.py`
- Create: `DeviceFiles/lib/orbit12ui/schema.py`

**Step 1: Write the failing tests**

Add tests for:
- loading `ui.json` tabs/items into parameter objects
- label fallback to `name`
- nested folder children parsing

**Step 2: Run test to verify it fails**

Run: `python3 -m unittest discover -s tests -p 'test_orbit12ui*.py' -v`
Expected: FAIL for schema loading behavior.

**Step 3: Write minimal implementation**

Implement parser and object creation without extra features beyond required spec.

**Step 4: Run test to verify it passes**

Run: `python3 -m unittest discover -s tests -p 'test_orbit12ui*.py' -v`
Expected: PASS.

### Task 3: Implement launcher bootstrap and app discovery

**Files:**
- Modify: `DeviceFiles/code.py`
- Modify: `DeviceFiles/apps/Launcher/code.py`
- Create: `DeviceFiles/apps/Launcher/launcher_core.py`
- Create: `tests/test_launcher_core.py`

**Step 1: Write the failing tests**

Test discovery and entrypoint resolution:
- excludes `Launcher`
- includes folders with `code.py` or `<AppName>.py`
- returns sorted app rows
- app row text format `AppName >`
- settings row is last

**Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests/test_launcher_core.py -v`
Expected: FAIL because launcher core module missing behavior.

**Step 3: Write minimal implementation**

Add discovery helpers and launcher `run()` loop with basic navigation and app execution (`exec` entry file).

**Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests/test_launcher_core.py -v`
Expected: PASS.

### Task 4: Create ParamLab app and schema example

**Files:**
- Create: `DeviceFiles/apps/ParamLab/code.py`
- Create: `DeviceFiles/apps/ParamLab/ui.json`
- Create: `tests/test_parmlab_schema.py`

**Step 1: Write the failing test**

Test that `ui.json` covers all required parameter types/options and includes at least one `viscondition`.

**Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests/test_parmlab_schema.py -v`
Expected: FAIL before app/schema exists.

**Step 3: Write minimal implementation**

Create ParamLab `ui.json` with required coverage and a basic app loop powered by orbit12ui menu controller.

**Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests/test_parmlab_schema.py -v`
Expected: PASS.

### Task 5: Verification and docs alignment

**Files:**
- Modify: `docs/CurrentStatus.md`

**Step 1: Run full verification**

Run: `python3 -m unittest discover -s tests -p 'test_*.py' -v`
Expected: all created tests PASS.

**Step 2: Update status doc**

Mark launcher bootstrap, orbit12ui core, and ParamLab as implemented.

**Step 3: Final validation command**

Run: `python3 -m py_compile $(find DeviceFiles -name '*.py')`
Expected: no syntax errors.
