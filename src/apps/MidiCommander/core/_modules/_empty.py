from .. import module as Module
from .. import parms as Parms

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from ..state import State

class Empty(Module.Module):
    
    def __init__(self,state: "State",slotid):
        super().__init__(state,slotid,[],include_default_parms=False)

    def get_params(self):
        parms = []
        for module_class in Module.AVAILABLE_MODULES.values():
            callback_function=lambda module_class=module_class: self.state.set_chain_module(self.slotid, module_class)
            moduleparm = Parms.Parm(module_class.name,module_class.label,Parms.ButtonParmType,0,callback_function=callback_function)
            parms.append(moduleparm)
        return parms
    
