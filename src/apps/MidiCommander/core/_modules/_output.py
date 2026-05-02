from ..module import Module

class Output(Module):
    name = "output"
    label = "Output"
    def __init__(self, state, slot_id, macropad = None):
            parms = []
            super().__init__(state, slot_id, parms, include_default_parms=False)
            self.macropad = self.state.macropad

    def process(self, note_ons, note_offs):
        #TODO: Move the specifics to the output.py and just have this module call those.
        #TODO: Also, need to handle note_offs if input module changed (keys shifted etc)
        
        for note in note_ons:
            self.macropad.midi.send(self.macropad.NoteOn(note, 120))  # send midi noteon
        for note in note_offs:
            self.macropad.midi.send(self.macropad.NoteOff(note, 0))  # send midi noteon
        return note_ons, note_offs