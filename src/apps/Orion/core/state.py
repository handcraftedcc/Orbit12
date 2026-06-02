from ._modules._empty import Empty as EmptyModule
from .constants import TOTALSLOTCOUNT
from .music import NOTES
from ..modules import _registry as ModuleRegistry
from ..inputmodules import _registry as InputModuleRegistry

import gc

class State:
    """ Handles general state of the App. See parameter list below """
    def __init__(self, macropad):
        # Music State
        self.key = NOTES.index("C")
        self.scale = 1
        self.octave = 3
        self.key_offset = 0
        self.key_custom_text = None

        # Timing State
        self.bpm = 120
        self.swing = 0.0
        self.timing_step = TimingSteps.One16th
        self.transport_mode = 0

        # Chain State
        self.chain_modules = []
        self.active_ui_section = UISection.CHAIN
        self.active_chain = ChainElements.IN
        self.active_chain_mode = ChainModes.SELECT

        # Parm State
        self.active_parm = -1 #-1 would be on the chain, 0 would be parameter 0 etc
        self.active_parm_page = 0
        self.parm_count = 2

        # Module Selector State
        self.module_selector_active_module = 0

        # Others
        self.ui_manager = None
        self.input_manager = None
        self.output_manager = None
        self.macropad = macropad
        self.module_helper = None
        self.nav_keys_state = 0 # 0 is notes, 1 is parms
        self.help_text = "SEL CHAIN ELMT"
        self.module_load_failed = False

    def update_parm_count(self):
        module = self.chain_modules[self.active_chain]
        self.parm_count = len(module.get_parms()) if module is not None else 0

    def move_active_chain_elem(self,delta):
        current_module = self.chain_modules[self.active_chain]
        if current_module is not None:
            current_module.release_parms()
        self.active_chain = (self.active_chain+delta)%TOTALSLOTCOUNT
        self.update_parm_count()
        self.active_parm = 0

    def move_active_parm_elem(self,delta):
        prev_parm = self.active_parm
        self.active_parm = ((self.active_parm + 2 + delta) % (self.parm_count + 2)) - 2
        if self.active_parm == -1 and self.active_chain in ChainElements.STATICELEMENTS:
            if prev_parm < self.active_parm:
                self.active_parm = 0
            else:
                self.active_parm = -2
        self.update_parm_count()

    def set_chain_module(self, slot_id, module_class):
        self.unload_chain_module(slot_id)
        self.load_chain_module(slot_id, module_class)

    def load_chain_module(self, slot_id, module_class):
        self.module_load_failed = False
        gc.collect()
        try:
            self.chain_modules[slot_id] = module_class(self.module_helper, slot_id)
        except MemoryError:
            gc.collect()
            self.chain_modules[slot_id] = self.create_fallback_module(slot_id)
            self.help_text = "NOT ENOUGH MEM"
            self.module_load_failed = True
        self.update_parm_count()
        self.active_parm = 0
        gc.collect()
        return not self.module_load_failed

    def get_fallback_module_class(self, slot_id):
        if slot_id == ChainElements.IN:
            try:
                return InputModuleRegistry.get_module_class("note")
            except MemoryError:
                gc.collect()
        return EmptyModule

    def create_fallback_module(self, slot_id):
        module_class = self.get_fallback_module_class(slot_id)
        try:
            return module_class(self.module_helper, slot_id)
        except MemoryError:
            gc.collect()
            return EmptyModule(self.module_helper, slot_id)

    def module_key_in_use(self, module_key):
        for module in self.chain_modules:
            if module is not None and module.name == module_key:
                return True
        return False

    def unload_chain_module(self, slot_id):
        old_module = self.chain_modules[slot_id]

        if old_module is not None:
            old_key = old_module.name
            old_module.remove()
            self.chain_modules[slot_id] = None
            gc.collect()
            if not self.module_key_in_use(old_key):
                if slot_id == ChainElements.IN:
                    InputModuleRegistry.unload_module_class(old_key)
                else:
                    ModuleRegistry.unload_module_class(old_key)
                gc.collect()

    def get_active_chain_module(self):
        return self.chain_modules[self.active_chain]

    def get_active_module_parm(self):
        return self.get_active_chain_module().get_parm(self.active_parm)

    def module_selector_enter(self):
        if self.active_chain == ChainElements.IN:
            registry = InputModuleRegistry
        else:
            registry = ModuleRegistry
        active_key = self.chain_modules[self.active_chain].name
        if active_key in registry.AVAILABLE_MODULE_NAMES:
            self.module_selector_active_module = registry.AVAILABLE_MODULE_NAMES.index(active_key)
        else:
            self.module_selector_active_module = 0

    def module_selector_change_module_selection(self,delta):
        registry = None
        if self.active_chain == ChainElements.IN:
            registry = InputModuleRegistry
        else:
            registry = ModuleRegistry
        self.module_selector_active_module = (self.module_selector_active_module + delta) % len(registry.AVAILABLE_MODULE_NAMES)

    def module_selector_apply_module_selection(self):
        if self.active_chain == ChainElements.IN:
            registry = InputModuleRegistry
        else:
            registry = ModuleRegistry
        active_key = self.chain_modules[self.active_chain].name
        selected_key = registry.AVAILABLE_MODULE_NAMES[self.module_selector_active_module]
        if active_key == selected_key :
            return
        else:
            slot_id = self.active_chain
            self.unload_chain_module(slot_id)
            try:
                module_class = registry.get_module_class(selected_key)
            except MemoryError:
                gc.collect()
                self.help_text = "NOT ENOUGH MEM"
                self.module_load_failed = True
                self.chain_modules[slot_id] = self.create_fallback_module(slot_id)
                self.update_parm_count()
                self.active_parm = 0
                gc.collect()
                return False
            return self.load_chain_module(slot_id, module_class)

    def stop_all_modules(self):
        for module in self.chain_modules:
            module.stop()
        self.output_manager.all_notes_off()

    def swap_modules(self, slot_id1, slot_id2):
        self.chain_modules[slot_id1], self.chain_modules[slot_id2] = self.chain_modules[slot_id2], self.chain_modules[
            slot_id1]
        self.chain_modules[slot_id1].slot_id = slot_id1
        self.chain_modules[slot_id2].slot_id = slot_id2

class UISection:
    CHAIN = 0
    PARMSELECTION = 1
    PARMEDIT = 2
    MODULESELECTION = 3

class ChainElements:
    IN = 0
    TRANSPORT = 1
    SLOT1 = 2
    SLOT2 = 3
    SLOT3 = 4
    SLOT4 = 5
    SLOT5 = 6
    SLOT6 = 7
    OUT = 8
    SETTINGS = 9

    STATICELEMENTS = (TRANSPORT,OUT,SETTINGS)

class ChainModes:
    SELECT = 0
    SWAP = 1

class TimingSteps:
    One16th = 0
    One32nd = 1
    One64th = 2
