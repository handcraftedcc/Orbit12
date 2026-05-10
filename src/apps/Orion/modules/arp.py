from ..core.module import Module
from ..core import parms as Parms
import random

class Arp(Module):
    name = "arp"
    label = "Arp"
    version = 1
    def __init__(self, module_helper, slot_id):
            parms = []
            super().__init__(module_helper, slot_id, parms)

            # Setup Attribs
            self.rate = 1
            self.note_register = []
            self.mode = None
            self.mode_list = [
                "up",
                "down",
                "random"
            ]
            self.random_seed = 0
            self.gate = 0


            # Setup Parms

            # Rate
            self.rate_parm = Parms.Parm(name="rate", label="Rate", default=0, parm_type=Parms.RateParmType,
                                        edit_callback_function=self.update_rate)
            self.parms.append(self.rate_parm)

            # Mode
            self.mode_parm = Parms.Parm(name="mode", label="Mode", default=0, parm_type=Parms.EnumParmType,
                                        options=self.mode_list,edit_callback_function=self.update_mode)
            self.parms.append(self.mode_parm)

            # Gate
            self.gate_parm = Parms.Parm(name="gate", label="Gate", default=100, parm_type=Parms.FloatParmType,
                                        increment=5, edit_callback_function=self.update_gate)
            self.parms.append(self.gate_parm)

            # Init values
            self.rate = self.rate_parm.get_actual_value()
            self.mode = self.mode_parm.get_actual_value()
            self.gate = self.gate_parm.get_actual_value()


    def update_rate(self, value):
        self.rate = self.rate_parm.get_actual_value()
        return self.rate

    def update_mode(self, value):
        self.mode = self.mode_parm.get_actual_value()
        return self.mode

    def update_gate(self, value):
        self.gate = self.gate_parm.get_actual_value()
        return self.gate_parm.get_actual_value()

    def update_random_seed(self):
        self.random_seed += 1
        return self.random_seed

    def update_note_register(self, held_notes):
        register = []
        if self.mode == "up":
            register.append(held_notes)
        elif self.mode == "down":
            register.append(held_notes)
            register.reverse()
        elif self.mode == "random":
            register.append(held_notes)
            random.seed(self.random_seed)
            random.shuffle(register)






