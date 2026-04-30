from . import parms as Parms

# Module base

class Module:
    name = None
    label = None
    version = 1
    def __init__(self, state, slot_id, parms: list[Parms.Parm], include_default_parms = True):
        self.parms = parms
        self.state = state
        self.slot_id = slot_id

        # Add default parms each module will have
        if include_default_parms:
            #swap_parm = Parms.Parm("swap","Swap",Parms.ButtonParmType,None)
            #self.parms.append(swap_parm)
            remove_parm = Parms.Parm("remove","Remove", Parms.ButtonParmType, None, enter_callback_function=lambda: self.state.reset_chain_module(self.slot_id))
            self.parms.append(remove_parm)        

    def get_parms(self):
        return self.parms
    
    def get_parm(self, parm_id):
        return self.parms[parm_id]
    
    def get_parm_value(self, parm_id):
        return self.parms[parm_id].value
    
    def set_parm_value(self, parm_id, parm_value):
        self.parms[parm_id].value = parm_value

    def remove(self):
        def remove(self):
            for parm in self.parms:
                parm.enter_callback_function = None
                parm.edit_callback_function = None
                parm.exit_callback_function = None

            self.parms = []
            self.state = None
            self.slot_id = None

class ModuleSelector():
    def __init__(self):
        pass