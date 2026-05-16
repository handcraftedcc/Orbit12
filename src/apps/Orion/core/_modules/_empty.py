from .. import module as Module
from .. import parms as Parms

try:
    from typing import TYPE_CHECKING
    if TYPE_CHECKING:
        from ..state import State
except ImportError:
    pass

class Empty(Module.Module):
    name = "empty"
    label = "Empty"
    version = 1
    def __init__(self, module_helper, slot_id):
        super().__init__(module_helper, slot_id, include_default_parms=True, include_out_parms=False)