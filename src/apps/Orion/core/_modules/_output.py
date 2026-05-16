from ..module import Module
from .. import parms as Parms

class Output(Module):
    name = "output"
    label = "Output"
    def __init__(self, module_helper, slot_id, macropad = None):

        super().__init__(module_helper, slot_id, include_default_parms=False, include_out_parms=False)
        self.macropad = self.state.macropad
        self.print_state = 0

    def create_main_parms(self):
        parms = []
        print_output_parm = Parms.Parm(name="print", label="Print", parm_type=Parms.BooleanParmType, default=0,
                                       edit_callback_function=self.set_print)
        parms.append(print_output_parm)
        return parms

    def set_print(self, value):
        self.print_state = value

    def process(self, note_ons, note_offs):
        #TODO: Move the specifics to the output.py and just have this module call those.
        #TODO: Also, need to handle note_offs if input module changed (keys shifted etc)

        if note_ons.length>0 or note_offs.length>0:
            self.module_helper.output_manager.schedule_midi_notes(note_ons, note_offs)

        if self.print_state == 1:
            print("Ons:", note_ons, "Offs:", note_offs)
        return note_ons, note_offs