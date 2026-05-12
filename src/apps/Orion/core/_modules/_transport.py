from ..module import Module
from .. import parms as Parms
from ..parms import Parm

import gc

def print_ram_usage(value):
    gc.collect()
    print(gc.mem_alloc())
    print(gc.mem_free())

class Transport(Module):
    """ This module only handles the UI of the transport.
    The actual midi clock stuff happens in the transport object in core."""
    name = "transport"
    label = "Transport"
    def __init__(self, module_helper, slot_id):
            self.module_helper = module_helper
            self.state = self.module_helper.state
            self.transport = self.module_helper.transport
            self.transport_mode = self.transport.mode
            self.bpm = self.state.bpm
            self.swing = self.state.swing
            self.timing_step = self.state.timing_step

            self.parms = []

            print_ram_usage_parm = Parms.Parm("printRam", "Print Ram", Parms.ButtonParmType, None,
                                              enter_callback_function=print_ram_usage)
            self.parms.append(print_ram_usage_parm)

            transport_mode_options = ["Internal", "External"]
            transport_mode_parm = Parms.Parm("mode", "Mode", Parms.EnumParmType, 0,
                                          options=transport_mode_options, enter_callback_function=self.set_transport_mode)
            self.parms.append(transport_mode_parm)

            bpm_parm = Parms.Parm("bpm", "BPM", Parms.IntParmType, 120,
                                              enter_callback_function=self.set_bpm)
            self.parms.append(bpm_parm)

            swing_parm = Parms.Parm("swing", "Swing", Parms.FloatParmType, 0, minmax = [-1,1],
                                              enter_callback_function=self.set_bpm)
            self.parms.append(swing_parm)

            timing_step_options = ["1/16","1/32","1/64"]
            timing_step_parm = Parms.Parm("timing_step", "Timing", Parms.EnumParmType, 0,
                                          options = timing_step_options, enter_callback_function=self.set_timing_step)
            self.parms.append(timing_step_parm)

            super().__init__(module_helper, slot_id, self.parms, include_default_parms=False)

    def set_transport_mode(self):
        self.transport_mode = self.state.transport_mode

    def set_bpm(self, value):
        self.bpm = value

    def set_swing(self, value):
        self.swing = value

    def set_timing_step(self, value):
        self.timing_step = value
        self.transport.set_timing_step_interval(value)
