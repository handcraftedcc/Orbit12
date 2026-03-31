"""ParamLab app: validates all Orbit12 UI parameter types."""

import os
import time

from adafruit_macropad import MacroPad
from orbit12ui import MenuController, create_parameter, load_ui_json
from orbit12ui.layout import compute_window_start
from orbit12ui.parameters import FolderParameter
from orbit12ui.textfit import fit_single_line, fit_split_line, split_line_overflows


VISIBLE_ROWS = 4
ROW_WIDTH = 21
ROW_CONTENT_WIDTH = ROW_WIDTH - 1
SCROLL_STEP_SECONDS = 0.3
SCROLL_IDLE_SECONDS = 0.5
LOOP_SLEEP_SECONDS = 0.002


def _resolve_ui_path():
    candidates = (
        "/apps/ParamLab/ui.json",
        "apps/ParamLab/ui.json",
        "DeviceFiles/apps/ParamLab/ui.json",
    )
    for path in candidates:
        try:
            os.stat(path)
            return path
        except OSError:
            continue
    raise RuntimeError("ParamLab ui.json not found")


def _build_lines(menu, scroll_tick=0, marquee_active=False):
    lines = [""] * (VISIBLE_ROWS + 1)
    visible = menu.visible_items()
    if not visible:
        lines[VISIBLE_ROWS] = fit_single_line("No visible items", ROW_WIDTH, False, 0)
        return lines

    selected_item = menu.current_item()
    selected_index = visible.index(selected_item)

    start = compute_window_start(
        selected_index,
        item_count=len(visible),
        visible_rows=VISIBLE_ROWS,
        preview_rows=1,
    )

    for line_idx in range(VISIBLE_ROWS):
        item_index = start + line_idx
        line = ""
        if item_index < len(visible):
            item = visible[item_index]
            prefix = ">" if item is selected_item else " "
            left = item.label
            right = item.display_value()
            if isinstance(item, FolderParameter):
                left = "[" + item.label + "]"
                right = ""
            elif item.name == "exit_app":
                left = "[Exit]"
                right = ""
            line = prefix + fit_split_line(
                left,
                right,
                width=ROW_CONTENT_WIDTH,
                selected=((item is selected_item) and marquee_active),
                tick=scroll_tick,
            )
        lines[line_idx] = line
    return lines


def _apply_lines(text_lines, line_cache, next_lines):
    changed = False
    for idx, text in enumerate(next_lines):
        if line_cache[idx] != text:
            text_lines[idx].text = text
            line_cache[idx] = text
            changed = True
    return changed


def _selected_row_needs_scroll(menu):
    item = menu.current_item()
    if item is None:
        return False
    if item.is_editing:
        return False
    if isinstance(item, FolderParameter):
        return split_line_overflows("[" + item.label + "]", "", ROW_CONTENT_WIDTH)
    return split_line_overflows(item.label, item.display_value(), ROW_CONTENT_WIDTH)


def _resolve_rotation():
    rotation = globals().get("DEVICE_ROTATION")
    if rotation in (0, 180):
        return rotation
    return 0


def _resolve_macropad():
    shared = globals().get("SHARED_MACROPAD")
    if shared is not None:
        return shared
    return MacroPad(rotation=_resolve_rotation())


def _drain_key_events(macropad):
    while macropad.keys.events:
        macropad.keys.events.get()


def run():
    tabs = load_ui_json(_resolve_ui_path())
    if not tabs:
        raise RuntimeError("ParamLab ui.json has no tabs")

    root_items = tabs[0]["items"]
    has_exit = False
    for item in root_items:
        if item.name == "exit_app":
            has_exit = True
            break
    if not has_exit:
        root_items.append(create_parameter({"type": "button", "name": "exit_app", "label": "Exit"}))

    menu = MenuController(tabs[0]["items"])

    macropad = _resolve_macropad()
    macropad.display.auto_refresh = False
    text_lines = macropad.display_text(title="ParamLab")
    text_lines.show()
    _drain_key_events(macropad)

    dirty = True
    scroll_tick = 0
    last_scroll_time = time.monotonic()
    last_input_time = last_scroll_time
    last_encoder = macropad.encoder
    line_cache = [None] * (VISIBLE_ROWS + 1)

    while True:
        while macropad.keys.events:
            event = macropad.keys.events.get()
            if event and event.pressed:
                if event.key_number == 0:
                    menu.back()
                    dirty = True
                    last_input_time = time.monotonic()
                    scroll_tick = 0
                elif event.key_number == 11:
                    return

        if last_encoder != macropad.encoder:
            delta = macropad.encoder - last_encoder
            last_encoder = macropad.encoder
            menu.rotate(delta)
            dirty = True
            last_input_time = time.monotonic()
            scroll_tick = 0

        macropad.encoder_switch_debounced.update()
        if macropad.encoder_switch_debounced.pressed:
            result = menu.press()
            if result and result.get("type") == "button" and result.get("name") == "exit_app":
                return
            dirty = True
            last_input_time = time.monotonic()
            scroll_tick = 0

        now = time.monotonic()
        if (
            _selected_row_needs_scroll(menu)
            and (now - last_input_time) >= SCROLL_IDLE_SECONDS
            and (now - last_scroll_time) >= SCROLL_STEP_SECONDS
        ):
            scroll_tick += 1
            last_scroll_time = now
            dirty = True

        if dirty:
            marquee_active = (
                _selected_row_needs_scroll(menu) and (now - last_input_time) >= SCROLL_IDLE_SECONDS
            )
            next_lines = _build_lines(menu, scroll_tick=scroll_tick, marquee_active=marquee_active)
            if _apply_lines(text_lines, line_cache, next_lines):
                macropad.display.refresh()
            dirty = False

        time.sleep(LOOP_SLEEP_SECONDS)


if __name__ == "__main__":
    run()
