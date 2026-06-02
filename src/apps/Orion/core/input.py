class InputManager:
    def __init__(self, macropad):
        self.macropad = macropad
        self.last_knob_pos = macropad.encoder
        self.downstate = bytearray(13)
        self.encoder_press_consumed = None

        self.pressed = bytearray(13)  # bytearray - encoder + 12 keys
        self.released = bytearray(13)  # bytearray - encoder + 12 keys
        self.knob_delta = 0
        self.knob_delta_nth = 8
        self.count = 0

    def get_inputs(self):
        for i in range(13):
            self.pressed[i] = 0
            self.released[i] = 0
            self.knob_delta = 0

        while self.macropad.keys.events:  # check for key press or release
            key_event = self.macropad.keys.events.get()
            if key_event:
                key = key_event.key_number
                if key_event.pressed:
                    self.pressed[key + 1] = 1
                    self.downstate[key + 1] = 1

                if key_event.released:
                    self.released[key + 1] = 1
                    self.downstate[key + 1] = 0

        self.macropad.encoder_switch_debounced.update()  # check the knob switch for press or release

        if self.macropad.encoder_switch_debounced.pressed:
            self.pressed[0] = 1
            self.downstate[0] = 1

        if self.macropad.encoder_switch_debounced.released:
            self.released[0] = 1
            self.downstate[0] = 0

        if self.last_knob_pos != self.macropad.encoder and not self.count % self.knob_delta_nth:  # knob has been turned
            knob_pos = self.macropad.encoder  # read encoder
            self.knob_delta = knob_pos - self.last_knob_pos  # compute knob_delta since last read
            self.last_knob_pos = knob_pos  # save new reading

        self.count += 1

        return self.pressed, self.released, self.knob_delta, self.downstate

    # Chord mode: Lower are 6 scale chords and upper 6 are modifiers: Inv   7   Sus   Add9   Bass   Spread -> ALTS: Octave, Power (only lower and upper), Borrow (Minor<>Major)
