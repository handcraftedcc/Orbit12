from ..module import Module
from .. import parms as Parms
import gc

class Input(Module):
    name = "input"
    label = "Input"
    def __init__(self, state, slot_id):
            parms = []
            super().__init__(state, slot_id, parms, include_default_parms=False)

            print_ram_usage_parm = Parms.Parm("printRam","Print Ram",Parms.ButtonParmType,None,enter_callback_function=self.print_ram_usage)
            self.parms.append(print_ram_usage_parm)     
        
    def print_ram_usage(self):
        gc.collect()
        print(gc.mem_alloc())
        print(gc.mem_free())