from ..module import Module
from .. import parms as Parms

class Output(Module):
    name = "output"
    label = "Output"
    def __init__(self, module_helper, slot_id, macropad = None):
        parms = []
        super().__init__(module_helper, slot_id, parms, include_default_parms=False)
        self.macropad = self.state.macropad
        self.print_state = 0

        print_output_parm = Parms.Parm(name="print", label="Print", parm_type=Parms.BooleanParmType, default=0,
                                  edit_callback_function=self.set_print)
        self.parms.append(print_output_parm)


    def set_print(self, value):
        self.print_state = value

    def process(self, note_ons, note_offs, velocities):
        #TODO: Move the specifics to the output.py and just have this module call those.
        #TODO: Also, need to handle note_offs if input module changed (keys shifted etc)

        for idx, note in enumerate(note_ons):
            if velocities[idx]:
                velocity = velocities[idx]
            else:
                velocity = 127
            self.macropad.midi.send(self.macropad.NoteOn(note, velocity))  # send midi note_on
        for note in note_offs:
            self.macropad.midi.send(self.macropad.NoteOff(note, 0))  # send midi note_off

        if self.print_state == 1:
            print("Ons:", note_ons, "Offs:", note_offs)
        return note_ons, note_offs, velocities