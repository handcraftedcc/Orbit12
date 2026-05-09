from ..core.module import Module

class Chord(Module):
    name = "chord"
    label = "Chord"
    version = 1
    def __init__(self, state, slot_id):
            parms = []
            super().__init__(state, slot_id, parms)