"""ParamLab app: validates all Orbit12 UI parameter types."""

import os
import time

import displayio
from adafruit_macropad import MacroPad
from orbit12ui import MenuController, create_parameter, load_ui
from orbit12ui.layout import compute_window_start
from orbit12ui.parameters import FolderParameter
from orbit12ui.textfit import fit_single_line, fit_split_line, split_line_overflows


VISIBLE_ROWS = 4
ROW_WIDTH = 21
ROW_CONTENT_WIDTH = ROW_WIDTH - 1
SCROLL_STEP_SECONDS = 0.3
SCROLL_IDLE_SECONDS = 0.5
LOOP_SLEEP_SECONDS = 0.002
ENABLE_MARQUEE = True


def _resolve_ui_path():
    candidates = (
        "/apps/ParamLab/ui.py",
        "apps/ParamLab/ui.py",
        "DeviceFiles/apps/ParamLab/ui.py",
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
    raise RuntimeError("ParamLab UI source not found")


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


def _selected_marquee_key(menu):
    item = menu.current_item()
    if item is None:
        return None
    if isinstance(item, FolderParameter):
        return (item.name, "[" + item.label + "]", "", item.is_editing)
    return (item.name, item.label, item.display_value(), item.is_editing)


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


def _build_text_ui(macropad, text_lines):
    bg_bitmap = displayio.Bitmap(macropad.display.width, macropad.display.height, 1)
    bg_palette = displayio.Palette(1)
    bg_palette[0] = 0x000000
    background = displayio.TileGrid(bg_bitmap, pixel_shader=bg_palette)
    ui_group = displayio.Group()
    ui_group.append(background)
    ui_group.append(text_lines.text_group)
    return ui_group


def _drain_key_events(macropad):
    while macropad.keys.events:
        macropad.keys.events.get()


def run():
    tabs = load_ui(_resolve_ui_path())
    if not tabs:
        raise RuntimeError("ParamLab UI source has no tabs")

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
    ui_group = _build_text_ui(macropad, text_lines)
    macropad.display.root_group = ui_group
    _drain_key_events(macropad)

    dirty = True
    scroll_tick = 0
    now = time.monotonic()
    last_encoder = macropad.encoder
    line_cache = [None] * (VISIBLE_ROWS + 1)
    marquee_active = False
    next_marquee_at = None
    marquee_key = None

    while True:
        while macropad.keys.events:
            event = macropad.keys.events.get()
            if event and event.pressed:
                if event.key_number == 0:
                    menu.back()
                    dirty = True
                    scroll_tick = 0
                    marquee_active = False
                    next_marquee_at = None
                elif event.key_number == 11:
                    return

        if last_encoder != macropad.encoder:
            delta = macropad.encoder - last_encoder
            last_encoder = macropad.encoder
            menu.rotate(delta)
            dirty = True
            scroll_tick = 0
            marquee_active = False
            next_marquee_at = None

        macropad.encoder_switch_debounced.update()
        if macropad.encoder_switch_debounced.pressed:
            result = menu.press()
            if result and result.get("type") == "button" and result.get("name") == "exit_app":
                return
            dirty = True
            scroll_tick = 0
            marquee_active = False
            next_marquee_at = None

        now = time.monotonic()
        if ENABLE_MARQUEE and next_marquee_at is not None and now >= next_marquee_at:
            if marquee_active:
                scroll_tick += 1
                next_marquee_at = now + SCROLL_STEP_SECONDS
            else:
                marquee_active = True
                next_marquee_at = now + SCROLL_STEP_SECONDS
            dirty = True

        if dirty:
            current_key = _selected_marquee_key(menu)
            if not ENABLE_MARQUEE:
                marquee_active = False
                next_marquee_at = None
            elif not _selected_row_needs_scroll(menu):
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

            next_lines = _build_lines(menu, scroll_tick=scroll_tick, marquee_active=marquee_active)
            if _apply_lines(text_lines, line_cache, next_lines):
                macropad.display.refresh()
            dirty = False

        time.sleep(LOOP_SLEEP_SECONDS)


if __name__ == "__main__":
    run()
