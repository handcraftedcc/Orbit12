"""Orbit12 Launcher app."""

import time

from adafruit_macropad import MacroPad

from launcher_core import build_menu_rows, discover_apps, execute_entry


VISIBLE_ROWS = 5


def _build_rows(apps):
    rows = build_menu_rows(apps)
    if not rows:
        return ["[Settings]"]
    return rows


def _render(text_lines, rows, selected_index, offset):
    for idx in range(VISIBLE_ROWS):
        row_index = offset + idx
        line = ""
        if row_index < len(rows):
            prefix = ">" if row_index == selected_index else " "
            line = prefix + " " + rows[row_index]
        text_lines[idx].text = line
    text_lines.show()


def run(apps_root="/apps"):
    macropad = MacroPad(rotation=180)
    macropad.display.auto_refresh = False

    text_lines = macropad.display_text(title="Orbit12 Launcher")

    apps = discover_apps(apps_root)
    rows = _build_rows(apps)
    selected = 0
    last_encoder = macropad.encoder
    dirty = True

    while True:
        macropad.encoder_switch_debounced.update()

        if last_encoder != macropad.encoder:
            delta = macropad.encoder - last_encoder
            last_encoder = macropad.encoder
            selected = (selected + delta) % len(rows)
            dirty = True

        if macropad.encoder_switch_debounced.pressed:
            if selected == len(rows) - 1:
                text_lines[VISIBLE_ROWS].text = "Global settings TODO"
                dirty = True
            else:
                try:
                    execute_entry(apps[selected]["entry"])
                except Exception as err:  # broad: launcher must stay alive
                    text_lines[VISIBLE_ROWS].text = "Launch error: " + str(err)
                    dirty = True
                apps = discover_apps(apps_root)
                rows = _build_rows(apps)
                selected = 0
                last_encoder = macropad.encoder
                dirty = True

        if dirty:
            start = 0
            if selected >= VISIBLE_ROWS:
                start = selected - (VISIBLE_ROWS - 1)
            _render(text_lines, rows, selected, start)
            macropad.display.refresh()
            dirty = False

        time.sleep(0.01)


if __name__ == "__main__":
    run()
