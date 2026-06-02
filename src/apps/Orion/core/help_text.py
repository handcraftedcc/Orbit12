from .state import ChainElements, ChainModes, UISection
from ..modules import _registry as ModuleRegistry
from ..inputmodules import _registry as InputModuleRegistry


HELP_TEXT_LIMIT = 17

MSG_STATE_CHAIN_SELECTION = "SEL CHAIN ELEMNT"
MSG_STATE_CHAIN_SWAP = "SWAP CHAIN ELEMNT"
MSG_STATE_PARM_SELECTION = "SEL PARM"
MSG_STATE_PARM_EDIT = "EDIT PARM"
MSG_STATE_MODULE_SELECTION = "SEL MODULE"

MSG_KEY_NAV_PARMS = "KEY NAV: PARMS"
MSG_KEY_NAV_OCT_OFFS = "KEY NAV: OCT/OFS"
MSG_TRANSPORT_START = "TRNSPRT START"
MSG_TRANSPORT_STOP = "TRNSPRT STOP"
MSG_OCTAVE_UP = "OCT UP"
MSG_OCTAVE_DOWN = "OCT DOWN"
MSG_KEY_OFFSET_UP = "KEY OFS UP"
MSG_KEY_OFFSET_DOWN = "KEY OFS DWN"
MSG_ENTER_PARMS = "OPEN PARMS"
MSG_ENTER_CHAIN = "OPEN CHAIN"
MSG_MODULE_PICK = "PICK MODULE"


def normalize_help_text(text):
    if not text:
        return " "
    return str(text).upper()[:HELP_TEXT_LIMIT]


def _module_registry_for_state(state):
    if state.active_chain == ChainElements.IN:
        return InputModuleRegistry
    return ModuleRegistry


def module_selector_help_text(state):
    registry = _module_registry_for_state(state)
    help_texts = getattr(registry, "AVAILABLE_MODULE_HELP_TEXTS", ())
    if not help_texts:
        return MSG_STATE_MODULE_SELECTION

    index = state.module_selector_active_module
    if index < 0 or index >= len(help_texts):
        return MSG_STATE_MODULE_SELECTION
    return normalize_help_text(help_texts[index])


def active_parameter_help_text(state, fallback):
    module = state.get_active_chain_module()
    if module is None:
        return fallback

    parm = module.get_parm(state.active_parm)
    if parm is None:
        return fallback

    return normalize_help_text(parm.get_help_text() or fallback)


def contextual_help_text(state):
    active_section = state.active_ui_section

    if active_section == UISection.MODULESELECTION:
        return module_selector_help_text(state)

    if active_section == UISection.PARMSELECTION:
        if state.active_parm == -2:
            return MSG_STATE_CHAIN_SELECTION
        if state.active_parm == -1:
            return MSG_STATE_MODULE_SELECTION
        return active_parameter_help_text(state, MSG_STATE_PARM_SELECTION)

    if active_section == UISection.PARMEDIT:
        return active_parameter_help_text(state, MSG_STATE_PARM_EDIT)

    if active_section == UISection.CHAIN and state.active_chain_mode == ChainModes.SWAP:
        return MSG_STATE_CHAIN_SWAP

    return MSG_STATE_CHAIN_SELECTION
