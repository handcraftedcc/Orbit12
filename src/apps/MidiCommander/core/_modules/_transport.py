from ..module import Module
from .. import parms as Parms
from ..parms import Parm

import gc

def print_ram_usage(value):
    gc.collect()
    print(gc.mem_alloc())
    print(gc.mem_free())

class Transport(Module):
    name = "transport"
    label = "Transport"
    def __init__(self, state, slot_id):
            self.parms = [
                  Parm("test","test",Parms.FloatParmType,0.25,[-5,5])
            ]

            print_ram_usage_parm = Parms.Parm("printRam", "Print Ram", Parms.ButtonParmType, None,
                                              enter_callback_function=print_ram_usage)
            self.parms.append(print_ram_usage_parm)

            super().__init__(state, slot_id, self.parms, include_default_parms=False)