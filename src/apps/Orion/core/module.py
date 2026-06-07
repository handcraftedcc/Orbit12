from .note_array import NoteOnArray, NoteOffArray
from . import parms as Parms
import gc

'''
Shared base classes for Orion chain modules.
'''

### ROUTING OPTIONS ###

OPERATION_OPTIONS = (
    "NEXT",
    "ADD",
    "OUT+N",
    "OUT+S",
    "SKIP",
)

SOURCE_OPTIONS = (
    "PREV",
    "IN",
    "S1",
    "S2",
    "S3",
    "S4",
    "S5",
)

EMPTY_NOTE_ON = NoteOnArray()
EMPTY_NOTE_OFF = NoteOffArray()


### SHARED CONTEXT ###

class ModuleHelper:
    # Centralized object access for modules.
    def __init__(self, Orion, macropad, state, input_manager, output_manager, transport):
        self.orion = Orion
        self.macropad = macropad
        self.state = state
        self.input_manager = input_manager
        self.output_manager = output_manager
        self.ui_manager = None
        self.neo_pixels = None
        self.transport = transport


### MODULE BASE ###

class Module:
    name = None
    label = None
    version = 1
    save_attrs = ()
    def __init__(self, module_helper, slot_id, include_default_parms = True, include_out_parms = True, include_source_parms = True):
        gc.collect()
        self.parms = None
        self.label_length = len(self.label)
        self.module_helper = module_helper
        self.state = module_helper.state
        self.ui_manager = module_helper.ui_manager
        self.slot_id = slot_id
        self.include_default_parms = include_default_parms
        self.include_out_parms = include_out_parms
        self.include_source_parms = include_source_parms
        self.operation_mode = 0
        self.out_channel = 0
        self.source_mode = 0
        if not hasattr(self,"note_ons_out"):
            self.note_ons_out = None
        if not hasattr(self,"note_offs_out"):
            self.note_offs_out = None

        gc.collect()



    ### Parm Creation ###

    def create_top_parms(self):
        return []

    def create_main_parms(self):
        parms = []
        return parms

    def create_out_parms(self):
        parms = []

        operation_mode_parm = Parms.Parm("operation_mode", "OP", Parms.EnumParmType, self.operation_mode,
                                        options=OPERATION_OPTIONS,
                                        bind_object=self,
                                        bind_attribute="operation_mode")
        parms.append(operation_mode_parm)
        out_channel_parm = Parms.Parm("out_channel", "OUTCH", Parms.IntParmType, self.out_channel+1, minmax = (1,16),
                                         edit_callback_function=self.set_out_channel)
        parms.append(out_channel_parm)
        return parms

    def create_source_parms(self):
        parms = []
        source_mode_parm = Parms.Parm("source_mode", "SRC", Parms.EnumParmType, self.source_mode,
                                        options=SOURCE_OPTIONS,
                                        bind_object=self,
                                        bind_attribute="source_mode")
        parms.append(source_mode_parm)
        return parms

    def set_out_channel(self, value):
        self.out_channel = value-1

    ### UI Utilities ###

    def _build_parms(self):
        parms = []
        if self.include_default_parms:
            parms.extend(self.create_top_parms())
        parms.extend(self.create_main_parms())
        if self.include_source_parms:
            parms.extend(self.create_source_parms())
        if self.include_out_parms:
            parms.extend(self.create_out_parms())
        return parms

    def get_parms(self):
        if self.parms is None:
            self.parms = self._build_parms()
        return self.parms

    def get_parm(self, parm_id):
        parms = self.get_parms()
        if 0 <= parm_id < len(parms):
            return parms[parm_id]
        return None

    def get_parm_by_name(self, name):
        for parm in self.get_parms():
            if parm.name == name:
                return parm
        return None
    
    def release_parms(self):
        if self.parms is None:
            return

        # Break callback/object references before unloading modules.
        for parm in self.parms:
            parm.bind_object = None
            parm.bind_attribute = None
            parm.enter_callback_function = None
            parm.edit_callback_function = None
            parm.exit_callback_function = None
            parm.options = None

        self.parms = None

    def remove(self):
        if self.note_ons_out is not None or self.note_offs_out is not None:
            self.stop()

        self.release_parms()
        self.note_ons_out = None
        self.note_offs_out = None
        self.ui_manager = None
        self.module_helper = None
        self.state = None
        self.slot_id = None

    ### PROCESSING ###

    def process_super(self, note_ons, note_offs):
        """
        Apply source and operation routing around the module-specific process step.
        """
        if self.source_mode != 0 and self.source_mode < self.slot_id:
            source = None
            if self.source_mode == 1:
                source = self.state.chain_modules[0]
            else:
                source = self.state.chain_modules[self.source_mode]
            if (hasattr(source,"note_ons_out") and hasattr(source,"note_offs_out") and
                    source.note_ons_out is not None and source.note_offs_out is not None):
                note_ons,note_offs = source.note_ons_out,source.note_offs_out

        if self.operation_mode == 1 and self.slot_id != 0: # Additive
            note_ons_in, note_offs_in = note_ons, note_offs
            note_ons, note_offs = self.process(note_ons, note_offs)
            note_ons.append_values(note_ons_in)
            note_offs.append_values(note_offs_in)
            return note_ons, note_offs
        elif (self.operation_mode == 2 and self.slot_id != 0) or (self.operation_mode == 1 and self.slot_id == 0): # Out & Next
            note_ons, note_offs = self.process(note_ons, note_offs)
            self.module_helper.output_manager.schedule_midi_notes(note_ons, note_offs, channel = self.out_channel)
            return note_ons, note_offs
        elif self.slot_id == 0 and self.operation_mode == 2: # Input only
            note_ons, note_offs = self.process(note_ons, note_offs)
            self.module_helper.output_manager.schedule_midi_notes(note_ons, note_offs, channel = self.out_channel)
            return EMPTY_NOTE_ON, EMPTY_NOTE_OFF
        elif self.operation_mode == 3: # Out & Skip
            note_ons_out, note_offs_out = self.process(note_ons, note_offs)
            self.module_helper.output_manager.schedule_midi_notes(note_ons_out, note_offs_out, channel = self.out_channel)
            return note_ons, note_offs
        elif self.operation_mode == 4: # Skip
            return note_ons, note_offs
        else: #Next/Default
            return self.process(note_ons, note_offs)

    def process(self, note_ons, note_offs):
        self.note_ons_out = note_ons
        self.note_offs_out = note_offs
        return self.note_ons_out, self.note_offs_out

    def stop(self):
        pass
