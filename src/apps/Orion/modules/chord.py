from ..core.module import Module

class Chord(Module):
    name = "chord"
    label = "Chord"
    version = 1
    def __init__(self, module_helper, slot_id):
            parms = []
            super().__init__(module_helper, slot_id, parms)