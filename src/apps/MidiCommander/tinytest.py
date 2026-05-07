def run():
    from adafruit_macropad import MacroPad
    import keypad

    macropad = MacroPad(rotation=0)
    macropad.display.auto_refresh = False
    macropad.encoder_switch_debounced.interval = 0.001
    macropad._keys.deinit()
    macropad._keys = keypad.Keys(
        macropad._key_pins,
        value_when_pressed=False,
        pull=True,
        interval=0.002,
        max_events=64,
    )

    NOTE = 36

    while True:
        event = macropad.keys.events.get()
        if event:
            if event.pressed:
                macropad.midi.send(macropad.NoteOn(NOTE, 120))
            elif event.released:
                macropad.midi.send(macropad.NoteOff(NOTE, 0))