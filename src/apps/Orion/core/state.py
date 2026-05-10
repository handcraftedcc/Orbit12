from ..inputmodules.note import Note as InputModule
from ._modules._empty import Empty as EmptyModule
from ._modules._transport import Transport as TransportModule
from ._modules._output import Output as OutputModule
from ..modules._registry import AVAILABLE_MODULES, AVAILABLE_MODULE_NAMES, AVAILABLE_MODULE_LABELS
from .music import NOTES
from .music import SCALENAMES as SCALES

import gc

TOTALSLOTCOUNT = 9
MODULESLOTCOUNT = 6
PARMSPERPAGE = 4

POLYPHONY = 8


class State:
    def __init__(self, macropad):
        # Music State
        self.key = NOTES.index("C")
        self.scale = 1
        self.octave = 0
        self.key_offset = 0

        # Timing State
        self.bpm = 100

        # Chain State
        self.chain_modules = []
        self.active_ui_section = UISection.CHAIN
        self.active_chain = ChainElements.IN

        # Parm State
        self.active_parm = -1 #-1 would be on the chain, 0 would be parameter 0 etc
        self.active_parm_page = 0
        self.parm_count = 2

        # Others
        self.ui_manager = None
        self.input_manager = None
        self.macropad = macropad
        self.module_helper = None

    def update_parm_count(self):
        self.parm_count = len(self.chain_modules[self.active_chain].get_parms())

    def move_active_chain_elem(self,delta):
        self.active_chain = (self.active_chain+delta)%9
        self.update_parm_count()
        self.active_parm = -1

    def move_active_parm_elem(self,delta):
        self.active_parm = ((self.active_parm + 1 + delta) % (self.parm_count + 1)) - 1
        self.active_parm_page = page_index = max(0, self.active_parm) // PARMSPERPAGE

    def set_chain_module(self, slot_id, module_class):
        self.chain_modules[slot_id]=module_class(self.module_helper, slot_id)
        self.update_parm_count()
        self.active_parm=0

    def reset_chain_module(self, slot_id):
        old_module = self.chain_modules[slot_id]

        if old_module is not None:
            old_module.remove()

        self.chain_modules[slot_id] = EmptyModule(self.module_helper, slot_id)
        self.update_parm_count()
        self.active_parm=0

    def get_active_chain_module(self):
        return self.chain_modules[self.active_chain]

    def get_active_module_parm(self):
        return self.get_active_chain_module().get_parm(self.active_parm)


class UISection:
    CHAIN = 0
    PARMSELECTION = 1
    PARMEDIT = 2

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
