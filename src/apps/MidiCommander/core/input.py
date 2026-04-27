import adafruit_macropad as MacroPad

class InputManager:
    def __init__(self,macropad: MacroPad):
        self.macropad = macropad
        self.last_knob_pos = macropad.encoder
        self.downstate = 0
        self.encoderpressconsumed = None


    def get_inputs(self):
        pressed = 0 #bitmask - encoder + 12 keys
        released = 0 #bitmask - encoder + 12 keys
        knob_delta = 0 #encoder int value

        macropad = self.macropad
        while macropad.keys.events:  # check for key press or release
            key_event = macropad.keys.events.get()
            if key_event:
                key = key_event.key_number
                flag = 1 << (key + 1)
                if key_event.pressed:
                    pressed |= flag
                    self.downstate |= flag

                if key_event.released:
                    released |= flag
                    self.downstate &= ~flag
            
        macropad.encoder_switch_debounced.update()  # check the knob switch for press or release
        
        if macropad.encoder_switch_debounced.pressed:
            pressed |= 1
            self.downstate |= 1

        if macropad.encoder_switch_debounced.released:
            released |= 1
            self.downstate &= ~1
            
        if self.last_knob_pos is not macropad.encoder:  # knob has been turned
            knob_pos = macropad.encoder  # read encoder
            knob_delta = knob_pos - self.last_knob_pos  # compute knob_delta since last read
            self.last_knob_pos = knob_pos  # save new reading

        return pressed,released,knob_delta,self.downstate
    


    #Chord mode: Lower are 6 scale chords and upper 6 are modifiers: Inv   7   Sus   Add9   Bass   Spread -> ALTS: Octave, Power (only lower and upper), Borrow (Minor<>Major)
        