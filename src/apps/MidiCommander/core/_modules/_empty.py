from .. import module as Module
from .. import parms as Parms
from ...modules import _registry as moduleregistry

try:
    from typing import TYPE_CHECKING
    if TYPE_CHECKING:
        from ..state import State
except ImportError:
    pass

class Empty(Module.Module):
    
    def __init__(self,state: "State",slotid):
        parms = []
        for module_class in moduleregistry.AVAILABLE_MODULES.values():
            callback_function=lambda module_class=module_class: self.state.set_chain_module(self.slotid, module_class)
            moduleparm = Parms.Parm(module_class.name,module_class.label,Parms.ButtonParmType,0,callback_function=callback_function)
            parms.append(moduleparm)
        super().__init__(state,slotid,parms,include_default_parms=False)
