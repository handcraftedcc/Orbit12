from . import state
from . import parms as Parms
from .note_array import NoteOnArray,NoteOffArray

class ModuleHelper: #Used to centralize and unify module object access
    def __init__(self, macropad, state, input_manager, output_manager, transport):
        self.macropad = macropad
        self.state = state
        self.input_manager = input_manager
        self.output_manager = output_manager
        self.ui_manager = None
        self.neo_pixels = None
        self.transport = transport

# Module base
class Module:
    name = None
    label = None
    version = 1
    def __init__(self, module_helper : ModuleHelper, slot_id, include_default_parms = True, include_out_parms = True, include_source_parms = True):
        self.parms = []
        self.module_helper = module_helper
        self.state = module_helper.state
        self.ui_manager = module_helper.ui_manager
        self.slot_id = slot_id
        self.operation_mode = 0
        self.out_channel = 0
        self.source_mode = 0
        if not hasattr(self,"note_ons_out"):
            self.note_ons_out = None
        if not hasattr(self,"note_offs_out"):
            self.note_offs_out = None

        # Add default parms each module will have
        if include_default_parms:
            self.parms.extend(self.create_top_parms())
        self.parms.extend(self.create_main_parms())
        if include_source_parms:
            self.parms.extend(self.create_source_parms())
        if include_out_parms:
            self.parms.extend(self.create_out_parms())



    ### Parm Creation ###

    def create_top_parms(self):
        from ..modules._registry import (
            AVAILABLE_MODULES,
            AVAILABLE_MODULE_NAMES,
            AVAILABLE_MODULE_LABELS
        )
        parms = []

        def switch_module(value):
            module_key = AVAILABLE_MODULE_NAMES[value]
            if module_key == self.name:
                return None
            else:
                state_ref = self.state
                slot_id = self.slot_id
                ui_manager = self.module_helper.ui_manager
                state_ref.set_chain_module(slot_id, AVAILABLE_MODULES[module_key])
                ui_manager.parameter_section.rebuild_parm_section()
                return module_key

        current_module_index = AVAILABLE_MODULE_NAMES.index(self.name)
        module_picker_parm = Parms.Parm("module_picker", "||MDLE||", Parms.EnumParmType, current_module_index,
                                        options=AVAILABLE_MODULE_LABELS,
                                        exit_callback_function=switch_module)
        parms.append(module_picker_parm)
        return parms

    def create_main_parms(self):
        parms = []
        return parms

    def create_out_parms(self):
        parms = []
        operation_options = [
            "NEXT",
            "OUT+N",
            "OUT+S",
            "SKIP"
        ]
        operation_mode_parm = Parms.Parm("operation_mode", "OP", Parms.EnumParmType, 0,
                                        options=operation_options,
                                        edit_callback_function=self.set_operation_mode)
        parms.append(operation_mode_parm)
        out_channel_parm = Parms.Parm("out_channel", "OUT CH", Parms.IntParmType, self.out_channel+1, minmax = (1,16),
                                         edit_callback_function=self.set_out_channel)
        parms.append(out_channel_parm)
        return parms

    def create_source_parms(self):
        parms = []
        source_options = [
            "PREV",
            "IN",
            "S1",
            "S2",
            "S3",
            "S4",
            "S5",
        ]
        source_mode_parm = Parms.Parm("source_mode", "SRC", Parms.EnumParmType, 0,
                                        options=source_options,
                                        edit_callback_function=self.set_source_mode)
        parms.append(source_mode_parm)
        return parms

    def process_outs(self, note_ons, note_offs):
        if self.operation_mode == 0:
            return

    def set_operation_mode(self, value):
        self.operation_mode = value

    def set_out_channel(self, value):
        self.out_channel = value-1

    def set_source_mode(self, value):
        self.source_mode = value

    ### UI Utilities ###

    def get_parms(self):
        return self.parms

    def get_parm(self, parm_id):
        if 0 <= parm_id < len(self.parms):
            return self.parms[parm_id]
        return None

    def get_parm_by_name(self, name):
        for parm in self.parms:
            if parm.name == name:
                return parm
        return None
    
    def get_parm_value(self, parm_id):
        return self.parms[parm_id].value
    
    def set_parm_value(self, parm_id, parm_value):
        self.parms[parm_id].value = parm_value

    def remove(self):
        if self.note_ons_out is not None or self.note_offs_out is not None:
            self.stop()

        for parm in self.parms or []:
            parm.enter_callback_function = None
            parm.edit_callback_function = None
            parm.exit_callback_function = None
            parm.options = None

        self.parms = []
        self.note_ons_out = None
        self.note_offs_out = None
        self.ui_manager = None
        self.module_helper = None
        self.state = None
        self.slot_id = None

    ### Process inputs ###
    def process_super(self, note_ons, note_offs):
        if self.source_mode != 0 and self.source_mode < self.slot_id:
            source = None
            if self.source_mode == 1:
                source = self.state.chain_modules[state.ChainElements.IN]
            else:
                source = self.state.chain_modules[self.source_mode]
            print("source", source)
            if hasattr(source,"note_ons_out") and hasattr(source,"note_offs_out"):
                note_ons,note_offs = source.note_ons_out,source.note_offs_out


        if self.operation_mode == 1:
            note_ons, note_offs = self.process(note_ons, note_offs) # Out & Next
            self.module_helper.output_manager.schedule_midi_notes(note_ons, note_offs, channel = self.out_channel)
            return note_ons, note_offs
        if self.operation_mode == 2: # Out & Skip
            note_ons_out, note_offs_out = self.process(note_ons, note_offs)
            self.module_helper.output_manager.schedule_midi_notes(note_ons_out, note_offs_out, channel = self.out_channel)
            return note_ons, note_offs
        if self.operation_mode == 3: # Skip
            return note_ons, note_offs
        else: #Next/Default
            return self.process(note_ons, note_offs)

    def process(self, note_ons, note_offs):
        self.note_ons_out = note_ons
        self.note_offs_out = note_offs
        return self.note_ons_out, self.note_offs_out

    def stop(self):
        pass
