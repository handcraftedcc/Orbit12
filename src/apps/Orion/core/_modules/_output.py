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

        if note_ons or note_offs:
            self.module_helper.output_manager.schedule_midi_notes(note_ons, note_offs, velocities)

        if self.print_state == 1:
            print("Ons:", note_ons, "Offs:", note_offs)
        return note_ons, note_offs, velocities