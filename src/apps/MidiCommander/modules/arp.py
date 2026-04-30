from ..core.module import Module

class Arp(Module):
    name = "arp"
    label = "Arp"
    version = 1
    def __init__(self, state, slot_id):
            parms = []
            super().__init__(state, slot_id, parms)