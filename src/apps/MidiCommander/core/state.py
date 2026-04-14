from ._modules._input import Input as InputModule
from ._modules._empty import Empty as EmptyModule
from ._modules._transport import Transport as TransportModule
from ._modules._output import Output as OutputModule

TOTALSLOTCOUNT = 9
MODULESLOTCOUNT = 6


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


        #Init modules
        self.chainmodules = [None]*TOTALSLOTCOUNT
        self.chainmodules[0] = InputModule(self,0)
        self.chainmodules[1] = TransportModule(self,1)
        for slot in range (2,TOTALSLOTCOUNT-1):
            self.chainmodules[slot] = EmptyModule(self,slot)
        self.chainmodules[TOTALSLOTCOUNT-1] = OutputModule(self,TOTALSLOTCOUNT-1)



    def move_active_chain_elem(self,delta):
        self.active_chain = (self.active_chain+delta)%9

    def move_active_param_elem(self,delta):
        self.active_param = (self.active_param+delta)%self.paramcount

    def set_chain_module(self,slotid,moduleclass):
        self.chainmodules[slotid]=moduleclass(self,slotid)
        
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
