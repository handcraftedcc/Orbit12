from ._modules._input import Input as InputModule
from ._modules._empty import Empty as EmptyModule
from ._modules._transport import Transport as TransportModule
from ._modules._output import Output as OutputModule

TOTALSLOTCOUNT = 9
MODULESLOTCOUNT = 6
PARMSPERPAGE = 4


class State:
    def __init__(self):
        self.chain_modules = []
        self.key = "C"
        self.scale = "chrom"
        self.bpm = 100

        self.active_ui_section = UISection.CHAIN
        self.active_chain = ChainElements.IN
        self.active_parm = -1 #-1 would be on the chain, 0 would be parmeter 0 etc
        self.active_parm_page = 0
        self.parmcount = 2


        #Init modules
        self.chain_modules = [None]*TOTALSLOTCOUNT
        self.chain_modules[0] = InputModule(self,0)
        self.chain_modules[1] = TransportModule(self,1)
        for slot in range (2,TOTALSLOTCOUNT-1):
            self.chain_modules[slot] = EmptyModule(self,slot)
        self.chain_modules[TOTALSLOTCOUNT-1] = OutputModule(self,TOTALSLOTCOUNT-1)



    def move_active_chain_elem(self,delta):
        self.active_chain = (self.active_chain+delta)%9
        self.parmcount = len(self.chain_modules[self.active_chain].get_parms())
        self.active_parm = -1

    def move_active_parm_elem(self,delta):
        self.active_parm = ((self.active_parm + 1 + delta) % (self.parmcount + 1)) - 1
        self.active_parm_page = page_index = max(0, self.active_parm) // PARMSPERPAGE

    def set_chain_module(self,slotid,moduleclass):
        self.chain_modules[slotid]=moduleclass(self,slotid)
        
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
