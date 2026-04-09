from enum import IntEnum,Enum


class State:
    def __init__(self):
        self.chain_modules = []
        self.key = "C"
        self.scale = "chrom"
        self.bpm = 100

        self.active_ui_section = UISection.CHAIN
        self.active_chain = ChainElements.IN
        self.active_param = -1 #-1 would be on the chain, 0 would be parameter 0 etc
        self.paramcount = 2

    def move_active_chain_elem(self,delta):
        self.active_chain = (self.active_chain+delta)%8

    def move_active_param_elem(self,delta):
        self.active_param = (self.active_param+delta)%self.paramcount
        
class UISection(Enum):
    CHAIN = 0
    PARMSELECTION = 1
    PARMEDIT = 2

class ChainElements(IntEnum):
    IN = 0
    TRANSPORT = 1
    SLOT1 = 2
    SLOT2 = 3
    SLOT3 = 4
    SLOT4 = 5
    SLOT5 = 6
    SLOT6 = 7
    OUT = 8
