from ..core.module import Module

class Chord(Module):
    name = "chord"
    label = "Chord"
    version = 1
    def __init__(self,state,slotid):
            parms = []
            super().__init__(state,slotid,parms)