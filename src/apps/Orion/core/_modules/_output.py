from ..module import Module
from .. import parms as Parms

class Output(Module):
    name = "output"
    label = "Output"
    def __init__(self, module_helper, slot_id, macropad = None):
        self.out_ch = 0
        super().__init__(module_helper, slot_id, include_default_parms=False, include_out_parms=False)
        self.macropad = self.state.macropad
        self.print_state = 0


    def create_main_parms(self):
        parms = []
        print_output_parm = Parms.Parm(name="print", label="Print", parm_type=Parms.BooleanParmType, default=0,
                                       edit_callback_function=self.set_print)
        parms.append(print_output_parm)
        out_ch_parm = Parms.Parm(name="out_ch", label="Out Ch", parm_type=Parms.IntParmType, default=self.out_ch+1, minmax = (1,16),
                                       edit_callback_function=self.set_out_ch)
        parms.append(out_ch_parm)
        return parms

    def set_print(self, value):
        self.print_state = value

    def set_out_ch(self, value):
        self.out_ch = value-1

    def process(self, note_ons, note_offs):
        if note_ons.length>0 or note_offs.length>0:
            self.module_helper.output_manager.schedule_midi_notes(note_ons, note_offs, self.out_ch)

        if self.print_state == 1:
            if note_ons.length>0 or note_offs.length>0:
                print("Ons:", list(note_ons.notes), "Offs:", list(note_offs.notes))
        return note_ons, note_offs