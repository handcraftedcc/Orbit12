from . import parms as Parms

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
            #swap_parm = Parms.Parm("swap","Swap",Parms.ButtonParmType,None)
            #self.parms.append(swap_parm)
            remove_parm = Parms.Parm("remove","Remove",Parms.ButtonParmType,None,enter_callback_function=lambda: self.state.reset_chain_module(self.slotid))
            self.parms.append(remove_parm)        

    def get_parms(self):
        return self.parms
    
    def get_parm(self,parmid):
        return self.parms[parmid]
    
    def get_parm_value(self,parmid):
        return self.parms[parmid].value
    
    def set_parm_value(self,parmid,parmvalue):
        self.parms[parmid].value = parmvalue

    def remove(self):
        def remove(self):
            for parm in self.parms:
                parm.enter_callback_function = None
                parm.edit_callback_function = None
                parm.exit_callback_function = None

            self.parms = []
            self.state = None
            self.slotid = None

class ModuleSelector():
    def __init__(self):
        pass