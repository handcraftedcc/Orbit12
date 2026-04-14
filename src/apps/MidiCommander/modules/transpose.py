from ..core.module import Module

class Transpose(Module):
    name = "transpose"
    label = "Transpose"
    version = 1
    def __init__(self,state,slotid):
            parms = []
            super().__init__(state,slotid,parms)