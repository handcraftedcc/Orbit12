from ..core.module import Module

class Pick(Module):
    name = "pick"
    label = "Pick"
    version = 1
    def __init__(self, state, slot_id):
            parms = []
            super().__init__(state, slot_id, parms)