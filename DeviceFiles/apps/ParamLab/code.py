"""ParamLab app: validates all Orbit12 UI parameter types."""

import os
import time

from adafruit_macropad import MacroPad
from orbit12ui import MenuController, load_ui_json


VISIBLE_ROWS = 5


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


def _truncate(text, limit=20):
    if len(text) <= limit:
        return text
    return text[: limit - 1] + "~"


def _render(text_lines, menu):
    visible = menu.visible_items()
    if not visible:
        for line_idx in range(VISIBLE_ROWS):
            text_lines[line_idx].text = ""
        text_lines[VISIBLE_ROWS].text = "No visible items"
        text_lines.show()
        return

    selected_item = menu.current_item()
    selected_index = visible.index(selected_item)

    start = 0
    if selected_index >= VISIBLE_ROWS:
        start = selected_index - (VISIBLE_ROWS - 1)

    for line_idx in range(VISIBLE_ROWS):
        item_index = start + line_idx
        line = ""
        if item_index < len(visible):
            item = visible[item_index]
            prefix = ">" if item is selected_item else " "
            left = _truncate(item.label, 9)
            right = _truncate(item.display_value(), 11)
            line = "%s %-9s %11s" % (prefix, left, right)
        text_lines[line_idx].text = line

    text_lines[VISIBLE_ROWS].text = "K0=Back K11=Exit"
    text_lines.show()


def run():
    tabs = load_ui_json(_resolve_ui_path())
    if not tabs:
        raise RuntimeError("ParamLab ui.json has no tabs")

    menu = MenuController(tabs[0]["items"])

    macropad = MacroPad(rotation=180)
    macropad.display.auto_refresh = False
    text_lines = macropad.display_text(title="ParamLab")

    dirty = True
    last_encoder = macropad.encoder

    while True:
        while macropad.keys.events:
            event = macropad.keys.events.get()
            if event and event.pressed:
                if event.key_number == 0:
                    menu.back()
                    dirty = True
                elif event.key_number == 11:
                    return

        if last_encoder != macropad.encoder:
            delta = macropad.encoder - last_encoder
            last_encoder = macropad.encoder
            menu.rotate(delta)
            dirty = True

        macropad.encoder_switch_debounced.update()
        if macropad.encoder_switch_debounced.pressed:
            menu.press()
            dirty = True

        if dirty:
            _render(text_lines, menu)
            macropad.display.refresh()
            dirty = False

        time.sleep(0.01)


if __name__ == "__main__":
    run()
