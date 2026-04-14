from . import parms as Parms
from .. import modules as Modules

AVAILABLE_MODULES = {
    "Arp": Modules.ArpModule,
    "Transpose": Modules.TransposeModule,
    "Pick": Modules.PickModule
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

    def getparams(self):
        return self.parms
    
    def setparamvalue(self,parmid,parmvalue):
        return self.parms[parmid].value

class ModuleSelector():
    def __init__(self):
        pass