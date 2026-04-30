from .. import module as Module
from .. import parms as Parms
from ...modules import _registry as module_registry

try:
    from typing import TYPE_CHECKING
    if TYPE_CHECKING:
        from ..state import State
except ImportError:
    pass

class Empty(Module.Module):
    def __init__(self, state: "State", slot_id):
        parms = []
        for module_class in module_registry.AVAILABLE_MODULES.values():
            callback_function=lambda module_class=module_class: self.state.set_chain_module(self.slot_id, module_class)
            module_parm = Parms.Parm(module_class.name,module_class.label,Parms.ButtonParmType,0,enter_callback_function=callback_function)
            parms.append(module_parm)
        super().__init__(state, slot_id, parms, include_default_parms=False)