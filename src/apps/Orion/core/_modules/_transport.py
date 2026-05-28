from ..module import Module
from .. import parms as Parms
from ..parms import Parm

class Transport(Module):
    """ This module only handles the UI of the transport.
    The actual midi clock stuff happens in the transport object in core."""
    name = "transport"
    label = "TRNSP"
    def __init__(self, module_helper, slot_id):
            self.module_helper = module_helper
            self.state = self.module_helper.state
            self.transport = self.module_helper.transport
            self.transport_mode = self.state.transport_mode
            self.bpm = self.state.bpm
            self.parms = []

            super().__init__(module_helper, slot_id, include_default_parms=False, include_out_parms=False, include_source_parms=False)

    def create_main_parms(self):
        parms = []

        transport_mode_options = ["INT", "EXT"]
        transport_mode_parm = Parms.Parm("mode", "MDE", Parms.EnumParmType, 0,
                                         options=transport_mode_options, exit_callback_function=self.set_transport_mode)
        parms.append(transport_mode_parm)

        bpm_parm = Parms.Parm("bpm", "BPM", Parms.IntParmType, 120, minmax=[1, 300],
                              edit_callback_function=self.set_bpm)
        parms.append(bpm_parm)

        swing_parm = Parms.Parm("swing", "SWNG", Parms.PercentParmType, self.state.swing, minmax=(0, 1),
                                increment=0.05,
                                edit_callback_function=self.set_swing)
        parms.append(swing_parm)

        return parms


    def set_transport_mode(self, value):
        self.state.transport_mode = value
        self.module_helper.output_manager.pending_midi_clock_ticks = 0
        self.transport.running = 0
        self.transport.reset()

    def set_bpm(self, value):
        self.transport.update_bpm(value)

    def set_swing(self, value):
        self.transport.update_swing(value)
        return self.transport.swing

    def remove(self):
        super().remove()
        self.transport = None
