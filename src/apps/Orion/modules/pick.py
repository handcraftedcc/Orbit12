from ..core.module import Module

class Pick(Module):
    name = "pick"
    label = "Pick"
    version = 1
    def __init__(self, module_helper, slot_id):
            parms = []
            super().__init__(module_helper, slot_id, parms)