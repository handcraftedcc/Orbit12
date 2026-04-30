from ..module import Module

class Output(Module):
    def __init__(self, state, slot_id):
            parms = []
            super().__init__(state, slot_id, parms, include_default_parms=False)