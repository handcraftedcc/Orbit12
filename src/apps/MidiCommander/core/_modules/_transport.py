from ..module import Module
from .. import parms as Parms
from ..parms import Parm

class Transport(Module):
    def __init__(self,state,slotid):
            parms = [
                  Parm("test","test",Parms.FloatParmType,0.25,[-5,5])
            ]

            super().__init__(state,slotid,parms,include_default_parms=False)