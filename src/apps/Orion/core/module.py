from . import parms as Parms

class ModuleHelper: #Used to centralize and unify module object access
    def __init__(self, macropad, state, input_manager, output_manager, transport):
        self.macropad = macropad
        self.state = state
        self.input_manager = input_manager
        self.output_manager = output_manager
        self.ui_manager = None
        self.transport = transport

# Module base
class Module:
    name = None
    label = None
    version = 1
    def __init__(self, module_helper : ModuleHelper, slot_id, parms: list[Parms.Parm], include_default_parms = True):
        self.parms = parms
        self.module_helper = module_helper
        self.state = module_helper.state
        self.ui_manager = module_helper.ui_manager
        self.slot_id = slot_id

        # Add default parms each module will have
        if include_default_parms:
            from ..modules._registry import (
                AVAILABLE_MODULES,
                AVAILABLE_MODULE_NAMES,
                AVAILABLE_MODULE_LABELS,
            )

            #swap_parm = Parms.Parm("swap","Swap",Parms.ButtonParmType,None)
            #self.parms.append(swap_parm)
            def switch_module(value):
                module_key = AVAILABLE_MODULE_NAMES[value]
                if module_key == self.name:
                    return None
                else:
                    self.state.set_chain_module(self.slot_id, AVAILABLE_MODULES[module_key])
                    self.module_helper.ui_manager.parameter_section.rebuild_parm_section()
                    return module_key
            current_module_index = AVAILABLE_MODULE_NAMES.index(self.name)
            module_picker_parm = Parms.Parm("module_picker", "MODULE:", Parms.EnumParmType, current_module_index, options=AVAILABLE_MODULE_LABELS,
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
    def process(self, note_ons, note_offs, velocities):
        return note_ons, note_offs, velocities

