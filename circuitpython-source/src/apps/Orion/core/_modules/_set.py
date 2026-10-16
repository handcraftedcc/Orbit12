from ..module import Module
from .. import parms as Parms
from .. import music as Music
from ..state import ChainElements


TRANSPORT_MODE_OPTIONS = ("INT", "EXT")


class Set(Module):
    name = "set"
    label = "SET"

    def __init__(self, module_helper, slot_id):
        self.transport = module_helper.transport
        super().__init__(module_helper, slot_id, include_default_parms=False, include_out_parms=False, include_source_parms=False)

    def create_main_parms(self):
        return [
            Parms.Parm("key", "KEY", Parms.NoteParmType, self.state.key,
                       edit_callback_function=self.set_key),
            Parms.Parm("scale", "SCL", Parms.EnumParmType, self.state.scale,
                       options=Music.SCALENAMES,
                       edit_callback_function=self.set_scale),
            Parms.Parm("bpm", "BPM", Parms.IntParmType, self.transport.bpm, minmax=(1, 300),
                       edit_callback_function=self.set_bpm),
            Parms.Parm("swing", "SWNG", Parms.PercentParmType, self.transport.swing, minmax=(0, 1),
                       increment=0.05,
                       edit_callback_function=self.set_swing),
            Parms.Parm("transport_mode", "TRMDE", Parms.EnumParmType, self.state.transport_mode,
                       options=TRANSPORT_MODE_OPTIONS,
                       exit_callback_function=self.set_transport_mode),
        ]

    def refresh_input_colors(self):
        input_module = self.state.chain_modules[ChainElements.IN]
        if input_module is not None and hasattr(input_module, "color_pixels"):
            input_module.color_pixels()

    def set_key(self, value):
        self.state.key = value
        self.module_helper.output_manager.all_notes_off()
        if self.module_helper.ui_manager:
            self.module_helper.ui_manager.header_footer.update_header_key_info()
        return value

    def set_scale(self, value):
        self.state.scale = value
        self.module_helper.output_manager.all_notes_off()
        self.refresh_input_colors()
        if self.module_helper.ui_manager:
            self.module_helper.ui_manager.header_footer.update_header_key_info()
        return value

    def set_bpm(self, value):
        self.transport.update_bpm(value)
        return self.transport.bpm

    def set_swing(self, value):
        self.transport.update_swing(value)
        return self.transport.swing

    def set_transport_mode(self, value):
        self.state.transport_mode = value
        self.module_helper.output_manager.pending_midi_clock_ticks = 0
        self.transport.running = 0
        self.transport.reset()
        if self.module_helper.ui_manager:
            self.module_helper.ui_manager.header_footer.update_footer_state_icons()
        return value

    def remove(self):
        super().remove()
        self.transport = None
