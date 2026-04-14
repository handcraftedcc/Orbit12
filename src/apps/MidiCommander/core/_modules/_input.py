from ..module import Module

class Input(Module):
    def __init__(self,state,slotid):
            parms = []
            super().__init__(state,slotid,parms,include_default_parms=False)