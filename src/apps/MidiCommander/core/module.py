from . import parms as Parms

# Module base
class Module:
    name = None
    label = None
    version = 1
    def __init__(self, state, slot_id, parms: list[Parms.Parm], include_default_parms = True):
        self.parms = parms
        self.state = state
        self.input = input
        self.slot_id = slot_id

        # Add default parms each module will have
        if include_default_parms:
            #swap_parm = Parms.Parm("swap","Swap",Parms.ButtonParmType,None)
            #self.parms.append(swap_parm)
            def switch_module(value):
                module_key = state.available_modules_names[value]
                if module_key == self.name:
                    return None
                else:
                    self.state.set_chain_module(self.slot_id, self.state.available_modules[module_key])
                    self.state.ui_manager.parameter_section.rebuild_parm_section()
                    return module_key
            current_module_index = state.available_modules_names.index(self.name)
            module_picker_parm = Parms.Parm("module_picker", "MODULE:", Parms.EnumParmType, current_module_index, options=self.state.available_modules_labels,
                                     exit_callback_function=switch_module)
            self.parms.append(module_picker_parm)

    ### UI Utilities ###

    def get_parms(self):
        return self.parms

    def get_parm(self, parm_id):
        if 0 <= parm_id < len(self.parms):
            return self.parms[parm_id]
        return None
    
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


    ### Process inputs ###
    def process(self, note_ons, note_offs):
        return note_ons, note_offs
