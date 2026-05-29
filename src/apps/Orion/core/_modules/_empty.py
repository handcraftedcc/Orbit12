from .. import module as Module
from .. import parms as Parms

class Empty(Module.Module):
    name = "empty"
    label = "EMPTY"
    version = 1
    def __init__(self, module_helper, slot_id):
        super().__init__(module_helper, slot_id, include_default_parms=True, include_out_parms=False, include_source_parms=False)

    def create_main_parms(self):
        parms = []
        # Key
        pick_module_parm = Parms.Parm(name="pick", label="EMPTY", parm_type=Parms.ButtonParmType, default=0,
                              enter_callback_function=self.enter_module_picker)
        parms.append(pick_module_parm)
        return parms

    def enter_module_picker(self, value):
        self.module_helper.orion.enter_module_selection()
