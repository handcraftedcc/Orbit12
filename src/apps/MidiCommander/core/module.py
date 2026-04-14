from . import parms as Parms
from ..modules.arp import Arp
from ..modules.transpose import Transpose
from ..modules.pick import Pick

AVAILABLE_MODULES = {
    "Arp": Arp,
    "Transpose": Transpose,
    "Pick": Pick
}

# Module base

class Module:
    name = None
    label = None
    version = 1
    def __init__(self,state,slotid,parms: list[Parms.Parm],include_default_parms = True):
        self.parms = parms
        self.state = state
        self.slotid = slotid

        # Add default parms each module will have
        if include_default_parms:
            swap_parm = Parms.Parm("swap","Swap",Parms.ButtonParmType,None)
            self.parms.append(swap_parm)
            remove_parm = Parms.Parm("remove","Remove",Parms.ButtonParmType,None)
            self.parms.append(remove_parm)        

    def get_params(self):
        return self.parms
    
    def get_param_value(self,parmid):
        return self.parms[parmid].value
    
    def set_param_value(self,parmid,parmvalue):
        self.parms[parmid].value = parmvalue

class ModuleSelector():
    def __init__(self):
        pass