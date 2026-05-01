from ..module import Module
from .. import parms as Parms
from ..parms import Parm

class Transport(Module):
    name = "transport"
    label = "Transport"
    def __init__(self, state, slot_id):
            parms = [
                  Parm("test","test",Parms.FloatParmType,0.25,[-5,5])
            ]

            super().__init__(state, slot_id, parms, include_default_parms=False)