"""Orbit12 Launcher app."""

import time
import traceback

from adafruit_macropad import MacroPad
from orbit12ui.layout import compute_window_start
from orbit12ui.textfit import fit_single_line, single_line_overflows

from launcher_core import (
    build_menu_rows,
    discover_apps,
    execute_entry,
    load_global_settings,
    rotation_from_settings,
    save_global_settings,
)


VISIBLE_ROWS = 4
ROW_WIDTH = 21
ROW_CONTENT_WIDTH = ROW_WIDTH - 1
SCROLL_STEP_SECONDS = 0.2
SCROLL_IDLE_SECONDS = 0.5
LOOP_SLEEP_SECONDS = 0.005


def _rotation_label(settings):
    if settings.get("device_rotation") == "flip":
        return "Flip"
    return "Default"


def _build_rows(apps):
    rows = [".."] + build_menu_rows(apps)
    if not rows:
        return ["..", "[Settings]"]
    return rows


def _settings_rows(settings):
    return ["..", "Device Rotation: " + _rotation_label(settings)]


def _render(text_lines, rows, selected_index, offset, status_text="", scroll_tick=0, marquee_active=False):
    for idx in range(VISIBLE_ROWS):
        row_index = offset + idx
        line = ""
        if row_index < len(rows):
            prefix = ">" if row_index == selected_index else " "
            line = prefix + fit_single_line(
                rows[row_index],
                width=ROW_CONTENT_WIDTH,
                selected=((row_index == selected_index) and marquee_active),
                tick=scroll_tick,
            )
        text_lines[idx].text = line
    text_lines[VISIBLE_ROWS].text = fit_single_line(status_text, width=ROW_WIDTH, selected=False, tick=0)
    text_lines.show()


def run(apps_root="/apps", settings_path="/userdata/global_settings.json"):
    settings = load_global_settings(settings_path)
    current_rotation = rotation_from_settings(settings)

    macropad = MacroPad(rotation=current_rotation)
    macropad.display.auto_refresh = False

    text_lines = macropad.display_text(title="Orbit12 Launcher")

    apps = discover_apps(apps_root)
    rows = _build_rows(apps)
    selected = 0
    settings_selected = 0
    in_settings = False
    last_encoder = macropad.encoder
    last_scroll_time = time.monotonic()
    last_input_time = last_scroll_time
    scroll_tick = 0
    status_text = ""
    dirty = True

    while True:
        macropad.encoder_switch_debounced.update()

        if last_encoder != macropad.encoder:
            delta = macropad.encoder - last_encoder
            last_encoder = macropad.encoder
            if in_settings:
                settings_selected = (settings_selected + delta) % len(rows)
            else:
                selected = (selected + delta) % len(rows)
            dirty = True
            last_input_time = time.monotonic()
            scroll_tick = 0

        if macropad.encoder_switch_debounced.pressed:
            if in_settings:
                if settings_selected == 1:
                    if settings.get("device_rotation") == "flip":
                        settings["device_rotation"] = "default"
                    else:
                        settings["device_rotation"] = "flip"
                    try:
                        save_global_settings(settings, settings_path)
                        status_text = "Rotation saved"
                        # Reload code to apply display/key rotation cleanly.
                        try:
                            import supervisor

                            supervisor.reload()
                        except Exception:
                            return
                    except Exception as err:
                        status_text = "Settings error"
                        print("Launcher settings error")
                        traceback.print_exception(type(err), err, err.__traceback__)
                    in_settings = False
                    rows = _build_rows(apps)
                    dirty = True
                    last_input_time = time.monotonic()
                    scroll_tick = 0
                else:
                    in_settings = False
                    rows = _build_rows(apps)
                    dirty = True
                    last_input_time = time.monotonic()
                    scroll_tick = 0
            else:
                if selected == 0:
                    status_text = ""
                    last_input_time = time.monotonic()
                    scroll_tick = 0
                elif selected == len(rows) - 1:
                    in_settings = True
                    rows = _settings_rows(settings)
                    settings_selected = 0
                    dirty = True
                    last_input_time = time.monotonic()
                    scroll_tick = 0
                else:
                    try:
                        execute_entry(
                            apps[selected - 1]["entry"],
                            shared_globals={
                                "SHARED_MACROPAD": macropad,
                                "DEVICE_ROTATION": current_rotation,
                            },
                        )
                        status_text = ""
                    except Exception as err:  # broad: launcher must stay alive
                        status_text = "Launch error"
                        print("Launcher error launching", apps[selected - 1]["name"])
                        traceback.print_exception(type(err), err, err.__traceback__)
                    apps = discover_apps(apps_root)
                    rows = _build_rows(apps)
                    if selected >= len(rows):
                        selected = len(rows) - 1
                    last_encoder = macropad.encoder
                    dirty = True
                    last_input_time = time.monotonic()
                    scroll_tick = 0

        now = time.monotonic()
        active_selected = settings_selected if in_settings else selected
        if (
            0 <= active_selected < len(rows)
            and single_line_overflows(rows[active_selected], ROW_CONTENT_WIDTH)
            and (now - last_input_time) >= SCROLL_IDLE_SECONDS
            and (now - last_scroll_time) >= SCROLL_STEP_SECONDS
        ):
            scroll_tick += 1
            last_scroll_time = now
            dirty = True

        if dirty:
            start = compute_window_start(
                active_selected,
                item_count=len(rows),
                visible_rows=VISIBLE_ROWS,
                preview_rows=1,
            )
            marquee_active = (
                0 <= active_selected < len(rows)
                and single_line_overflows(rows[active_selected], ROW_CONTENT_WIDTH)
                and (now - last_input_time) >= SCROLL_IDLE_SECONDS
            )
            _render(
                text_lines,
                rows,
                active_selected,
                start,
                status_text=status_text,
                scroll_tick=scroll_tick,
                marquee_active=marquee_active,
            )
            macropad.display.refresh()
            dirty = False

        time.sleep(LOOP_SLEEP_SECONDS)


if __name__ == "__main__":
    run()
