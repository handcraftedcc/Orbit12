"""Orbit12 Launcher app."""

import time
import traceback

import displayio
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
ENABLE_MARQUEE = True


def _rotation_label(settings):
    if settings.get("device_rotation") == "flip":
        return "flip"
    return "default"


def _build_rows(apps):
    return build_menu_rows(apps)


def _settings_rows(settings):
    return ["Orient: " + _rotation_label(settings), "[Back]"]


def _selected_row_needs_scroll(rows, selected_index):
    if selected_index < 0 or selected_index >= len(rows):
        return False
    return single_line_overflows(rows[selected_index], ROW_CONTENT_WIDTH)


def _build_lines(rows, selected_index, offset, status_text="", scroll_tick=0, marquee_active=False):
    lines = [""] * (VISIBLE_ROWS + 1)
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
        lines[idx] = line
    lines[VISIBLE_ROWS] = fit_single_line(status_text, width=ROW_WIDTH, selected=False, tick=0)
    return lines


def _apply_lines(text_lines, line_cache, next_lines):
    changed = False
    for idx, text in enumerate(next_lines):
        if line_cache[idx] != text:
            text_lines[idx].text = text
            line_cache[idx] = text
            changed = True
    return changed


def _build_text_ui(macropad, text_lines):
    bg_bitmap = displayio.Bitmap(macropad.display.width, macropad.display.height, 1)
    bg_palette = displayio.Palette(1)
    bg_palette[0] = 0x000000
    background = displayio.TileGrid(bg_bitmap, pixel_shader=bg_palette)
    ui_group = displayio.Group()
    ui_group.append(background)
    ui_group.append(text_lines.text_group)
    return ui_group


def run(apps_root="/apps", settings_path="/userdata/global_settings.json"):
    settings = load_global_settings(settings_path)
    current_rotation = rotation_from_settings(settings)

    macropad = MacroPad(rotation=current_rotation)
    macropad.display.auto_refresh = False

    text_lines = macropad.display_text(title="Orbit12 Launcher")
    ui_group = _build_text_ui(macropad, text_lines)
    macropad.display.root_group = ui_group

    apps = discover_apps(apps_root)
    rows = _build_rows(apps)
    selected = 0
    settings_selected = 0
    in_settings = False
    last_encoder = macropad.encoder
    scroll_tick = 0
    status_text = ""
    dirty = True
    line_cache = [None] * (VISIBLE_ROWS + 1)
    needs_show = False
    marquee_active = False
    next_marquee_at = None
    marquee_key = None

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
            scroll_tick = 0
            marquee_active = False
            next_marquee_at = None

        if macropad.encoder_switch_debounced.pressed:
            if in_settings:
                if settings_selected == 0:
                    if settings.get("device_rotation") == "flip":
                        settings["device_rotation"] = "default"
                    else:
                        settings["device_rotation"] = "flip"
                    current_rotation = rotation_from_settings(settings)
                    try:
                        macropad.rotation = current_rotation
                    except Exception as err:
                        print("Launcher orientation apply error")
                        traceback.print_exception(type(err), err, err.__traceback__)
                    try:
                        save_global_settings(settings, settings_path)
                        status_text = ""
                    except Exception as err:
                        status_text = ""
                        print("Launcher settings error")
                        traceback.print_exception(type(err), err, err.__traceback__)
                    in_settings = False
                    rows = _build_rows(apps)
                    dirty = True
                    scroll_tick = 0
                    marquee_active = False
                    next_marquee_at = None
                    line_cache = [None] * (VISIBLE_ROWS + 1)
                    needs_show = True
                else:
                    in_settings = False
                    rows = _build_rows(apps)
                    dirty = True
                    scroll_tick = 0
                    marquee_active = False
                    next_marquee_at = None
            else:
                if selected == len(rows) - 1:
                    in_settings = True
                    rows = _settings_rows(settings)
                    settings_selected = 0
                    dirty = True
                    scroll_tick = 0
                    marquee_active = False
                    next_marquee_at = None
                else:
                    try:
                        execute_entry(
                            apps[selected]["entry"],
                            shared_globals={
                                "SHARED_MACROPAD": macropad,
                                "DEVICE_ROTATION": current_rotation,
                            },
                        )
                        status_text = ""
                    except Exception as err:  # broad: launcher must stay alive
                        status_text = "Launch error"
                        print("Launcher error launching", apps[selected]["name"])
                        traceback.print_exception(type(err), err, err.__traceback__)
                    # Another app likely replaced display.root_group; reclaim launcher UI group.
                    needs_show = True
                    apps = discover_apps(apps_root)
                    rows = _build_rows(apps)
                    if selected >= len(rows):
                        selected = len(rows) - 1
                    last_encoder = macropad.encoder
                    dirty = True
                    scroll_tick = 0
                    marquee_active = False
                    next_marquee_at = None

        now = time.monotonic()
        active_selected = settings_selected if in_settings else selected
        if ENABLE_MARQUEE and next_marquee_at is not None and now >= next_marquee_at:
            if marquee_active:
                scroll_tick += 1
                next_marquee_at = now + SCROLL_STEP_SECONDS
            else:
                marquee_active = True
                next_marquee_at = now + SCROLL_STEP_SECONDS
            dirty = True

        if dirty:
            current_key = (in_settings, active_selected, rows[active_selected] if 0 <= active_selected < len(rows) else "")
            if not ENABLE_MARQUEE:
                marquee_active = False
                next_marquee_at = None
            elif not _selected_row_needs_scroll(rows, active_selected):
                marquee_active = False
                next_marquee_at = None
                scroll_tick = 0
                marquee_key = current_key
            else:
                if marquee_key != current_key:
                    marquee_key = current_key
                    marquee_active = False
                    scroll_tick = 0
                    next_marquee_at = now + SCROLL_IDLE_SECONDS
                elif next_marquee_at is None:
                    delay = SCROLL_STEP_SECONDS if marquee_active else SCROLL_IDLE_SECONDS
                    next_marquee_at = now + delay

            start = compute_window_start(
                active_selected,
                item_count=len(rows),
                visible_rows=VISIBLE_ROWS,
                preview_rows=1,
            )
            force_refresh = False
            if needs_show:
                macropad.display.root_group = ui_group
                needs_show = False
                force_refresh = True
            next_lines = _build_lines(
                rows,
                active_selected,
                start,
                status_text=status_text,
                scroll_tick=scroll_tick,
                marquee_active=marquee_active,
            )
            if _apply_lines(text_lines, line_cache, next_lines) or force_refresh:
                macropad.display.refresh()
            dirty = False

        time.sleep(LOOP_SLEEP_SECONDS)


if __name__ == "__main__":
    run()
