# Main app manager
import keypad
import adafruit_ticks as ticks
import gc
from .core.orion_macropad import OrionMacroPad as MacroPad

gc.collect()
print("START")
print("allocated:",gc.mem_alloc())
print("free:",gc.mem_free())

import microcontroller

from .core.state import UISection, ChainElements

microcontroller.cpu.frequency = 250_000_000

from .core import ui
from .core import neo_pixels
from .core import transport
from .core import input
from .core import state
gc.collect()
from .core import output
from .core import parms as Parms
from .core import module
from .core.note_array import NoteOnArray, NoteOffArray
gc.collect()

from .inputmodules.note import Note as InputModule
from .core._modules._empty import Empty as EmptyModule
from .core._modules._transport import Transport as TransportModule
from .core._modules._output import Output as OutputModule
from .core._modules._settings import Settings as SettingsModule
gc.collect()
print("allocated import:",gc.mem_alloc())
print("free:",gc.mem_free())

SCREEN_REFRESH_RATE = 4
SCREEN_SLEEP_TIME = 60000
SWAP_CHAIN_MIN = ChainElements.SLOT1
SWAP_CHAIN_MAX = ChainElements.SLOT6

def _unload_module(module_path):
    import sys
    package_name, _, attr_name = module_path.rpartition(".")
    package = sys.modules.get(package_name)
    if package is not None and hasattr(package, attr_name):
        delattr(package, attr_name)
    if module_path in sys.modules:
        del sys.modules[module_path]
        gc.collect()

class MidiCommander:
    def __init__(self):
        # Init macropad
        self.macropad = MacroPad(rotation=0)  # create the macropad object, rotate orientation
        self.macropad.display.auto_refresh = False  # avoid lag
        self.macropad.encoder_switch_debounced.interval = 0.001
        self.macropad._keys.deinit()
        self.macropad._keys = keypad.Keys(
            self.macropad._key_pins,
            value_when_pressed=False,
            pull=True,
            interval=0.002,
            max_events=64,
            debounce_threshold=1,
        )

        # Init objects
        self.state = state.State(self.macropad)
        self.input_manager = input.InputManager(self.macropad)
        self.output_manager = output.OutputManager(self.macropad, self.state)
        self.transport = transport.Transport(self.state, self.output_manager, self.macropad)
        self.neo_pixels = neo_pixels.NeoPixels(self.macropad, self.state)
        self.module_helper = module.ModuleHelper(self, self.macropad, self.state, self.input_manager, self.output_manager, self.transport)


        # Pass objects to state (Have to do after because of circular dependency)
        self.state.input = self.input_manager
        self.state.module_helper = self.module_helper
        self.state.output_manager = self.output_manager

        # Init items that get used each loop
        self.run_tick = 0
        self.encoder_consumed = None
        self.screen_update_needed = False
        self.ui_queue = []
        self.last_gc_ms = ticks.ticks_ms()
        self.knob_hold_down_start = None
        self.chain_swap_mode = 0 #0 = False, 1 = is pending, 2 = active
        self.last_screen_activity_ms = ticks.ticks_ms()
        self.screen_sleeping = False
        #self.ui_rebuild_pending = False

        self.note_ons = NoteOnArray()
        self.note_offs = NoteOffArray()

        self.module_helper.neo_pixels = self.neo_pixels

        #Init modules
        self.create_stock_chain_modules()
        self.state.update_parm_count()

        # Init UI
        self.ui_manager = ui.UIManager(self.macropad,self.state, self.transport)
        self.module_helper.ui_manager = self.ui_manager
        self.state.ui_manager = self.ui_manager
        self.load_active_scene()
        self.state.update_parm_count()
        self.queue_full_ui_refresh()

        # Update Pixels
        self.state.chain_modules[0].color_pixels()
        self.neo_pixels.paint_pixels()

        #gc.disable()

    def create_stock_chain_modules(self):
        self.state.chain_modules = [None]*state.TOTALSLOTCOUNT
        self.state.chain_modules[ChainElements.IN] = InputModule(self.module_helper, ChainElements.IN)
        self.state.chain_modules[ChainElements.TRANSPORT] = TransportModule(self.module_helper, ChainElements.TRANSPORT)
        for slot in range(SWAP_CHAIN_MIN, SWAP_CHAIN_MAX + 1):
            self.state.chain_modules[slot] = EmptyModule(self.module_helper, slot)
        self.state.chain_modules[ChainElements.OUT] = OutputModule(self.module_helper, ChainElements.OUT)
        self.state.chain_modules[ChainElements.SETTINGS] = SettingsModule(self.module_helper, ChainElements.SETTINGS)

    def unload_scene_modules(self):
        for slot in range(state.TOTALSLOTCOUNT):
            self.state.unload_chain_module(slot)
        gc.collect()

    def restore_stock_state_values(self):
        active_scene = self.state.active_scene
        self.state.reset_to_defaults()
        self.state.active_scene = active_scene
        self.transport.update_bpm(self.state.bpm)
        self.transport.update_swing(self.state.swing)
        self.transport.running = 0
        self.transport.reset()
        self.output_manager.pending_midi_clock_ticks = 0

    def reset_scene(self, refresh=True):
        self.state.stop_all_modules()
        gc.collect()

        self.unload_scene_modules()

        self.restore_stock_state_values()
        self.create_stock_chain_modules()
        self.state.active_chain = ChainElements.IN
        self.state.active_ui_section = UISection.CHAIN
        self.state.active_parm = 0
        self.state.active_parm_page = 0
        self.state.module_selector_active_module = 0
        self.state.update_parm_count()

        self.chain_swap_mode = 0
        self.note_ons.clear()
        self.note_offs.clear()
        if refresh:
            self.queue_full_ui_refresh()
            self.state.chain_modules[ChainElements.IN].color_pixels()
            self.neo_pixels.paint_pixels()
        gc.collect()

    def add_to_ui_queue(self, callback):
        if callback not in self.ui_queue:
            self.ui_queue.append(callback)
        #self.ui_rebuild_pending = True

    def queue_full_ui_refresh(self):
        self.add_to_ui_queue(self.ui_manager.switch_section)
        self.add_to_ui_queue(self.ui_manager.header_footer.update_header_key_info)
        self.add_to_ui_queue(self.ui_manager.header_footer.update_header_module_label)
        self.add_to_ui_queue(self.ui_manager.header_footer.update_header_chain_preview)
        self.add_to_ui_queue(self.ui_manager.header_footer.update_footer_scene_info)
        self.add_to_ui_queue(self.ui_manager.header_footer.update_footer_state_icons)
        self.add_to_ui_queue(self.ui_manager.chain.rebuild_chain_section)
        self.add_to_ui_queue(self.ui_manager.parameter_section.rebuild_parm_section)

    def load_scene_module(self, slot_id, module_key):
        current_module = self.state.chain_modules[slot_id]
        if current_module is not None and current_module.name == module_key:
            current_module.stop()
            return current_module

        if slot_id == ChainElements.IN:
            from .inputmodules import _registry as registry
        elif SWAP_CHAIN_MIN <= slot_id <= SWAP_CHAIN_MAX:
            from .modules import _registry as registry
        else:
            return current_module

        self.state.unload_chain_module(slot_id)
        gc.collect()

        module_class = None
        for _ in range(3):
            gc.collect()
            try:
                module_class = registry.get_module_class(module_key)
                break
            except MemoryError:
                gc.collect()

        if module_class is None and module_key in registry.MODULE_IMPORT_NAMES:
            self.state.chain_modules[slot_id] = self.state.create_fallback_module(slot_id)
            self.state.module_load_failed = True
            return self.state.chain_modules[slot_id]

        if module_class is None:
            self.state.chain_modules[slot_id] = self.state.create_fallback_module(slot_id)
            return self.state.chain_modules[slot_id]

        if not self.state.load_chain_module(slot_id, module_class):
            return self.state.chain_modules[slot_id]

        return self.state.chain_modules[slot_id]

    def load_active_scene(self):
        from .core import scenes
        try:
            self.state.active_scene = scenes.read_active_scene()
            loaded = scenes.load_scene(self, self.state.active_scene)
            if loaded is None:
                self.reset_scene()
                scenes.save_scene(self.state, self.state.active_scene)
                loaded = True
            elif loaded is not True:
                self.reset_scene()
            scenes.write_active_scene(self.state.active_scene)
        finally:
            _unload_module(scenes.__name__)
        return loaded

    def save_scene(self):
        from .core import scenes
        try:
            filename = scenes.save_scene(self.state, self.state.active_scene)
        finally:
            _unload_module(scenes.__name__)
        return filename

    def switch_scene(self, scene):
        if scene == self.state.active_scene:
            return True

        from .core import scenes
        try:
            scenes.save_scene(self.state, self.state.active_scene)
            self.state.stop_all_modules()
            gc.collect()
            self.unload_scene_modules()
            self.restore_stock_state_values()
            self.create_stock_chain_modules()
            self.state.active_scene = scenes.clamp_scene(scene)
            loaded = scenes.load_scene(self, self.state.active_scene)
            if loaded is None:
                self.reset_scene()
                scenes.save_scene(self.state, self.state.active_scene)
                loaded = True
            elif loaded is not True:
                self.reset_scene()
            scenes.write_active_scene(self.state.active_scene)
        finally:
            _unload_module(scenes.__name__)
        self.state.update_parm_count()
        self.state.active_ui_section = UISection.CHAIN
        self.state.active_parm = 0
        self.state.active_parm_page = 0
        self.queue_full_ui_refresh()
        self.neo_pixels.paint_pixels()
        return loaded

    def can_do_ui_work(self, min_slack_ms=10):
        if self.state.transport_mode == 1:
            return self.output_manager.pending_midi_clock_ticks == 0
        if self.transport.running:
            now = ticks.ticks_ms()
            ms_to_next = ticks.ticks_diff(self.transport.midi_tick_scheduled, now)
            #return self.output_manager.pending_midi_clock_ticks == 0 and ms_to_next >= min_slack_ms
            return ms_to_next >= min_slack_ms
        else:
            return True

    def maybe_gc(self):
        now = ticks.ticks_ms()

        if not self.transport.running:
            if ticks.ticks_diff(now, self.last_gc_ms) > 300:
                gc.collect()
                self.last_gc_ms = now
            return

        if self.state.transport_mode == 0:
            ms_to_next = ticks.ticks_diff(self.transport.midi_tick_scheduled, now)
            safe = (
                    self.output_manager.pending_midi_clock_ticks == 0
                    and ms_to_next >= 12
                    and not self.ui_queue
                    and ticks.ticks_diff(now, self.last_gc_ms) > 300
            )
            if safe:
                gc.collect()
                self.last_gc_ms = now

    def wake_screen(self):
        self.last_screen_activity_ms = ticks.ticks_ms()
        if self.screen_sleeping:
            self.macropad.display_sleep = False
            self.screen_sleeping = False
            self.screen_update_needed = True

    def maybe_sleep_screen(self):
        if self.screen_sleeping:
            return
        if self.ui_queue or self.screen_update_needed:
            return
        if ticks.ticks_diff(ticks.ticks_ms(), self.last_screen_activity_ms) > SCREEN_SLEEP_TIME:
            self.macropad.display_sleep = True
            self.screen_sleeping = True

    def move_active_chain_element(self, delta):
        if self.chain_swap_mode == 2: #Swap instead of move active
            if self.state.active_chain < SWAP_CHAIN_MIN:
                self.disable_chain_swap_mode()
                return
            if self.state.active_chain > SWAP_CHAIN_MAX:
                self.disable_chain_swap_mode()
                return
            active_element = self.state.active_chain
            direction = 1
            if delta < 0:
                direction = -1
            for i in range(abs(delta)):
                if direction == 1 and active_element >= SWAP_CHAIN_MAX:
                    break
                elif direction == -1 and active_element <= SWAP_CHAIN_MIN:
                    break
                self.state.swap_modules(active_element, active_element + direction)
                active_element = active_element+direction
                self.state.move_active_chain_elem(direction)
            self.state.stop_all_modules()
        else:
            self.state.move_active_chain_elem(delta)
            self.state.active_parm = 0
            self.add_to_ui_queue(self.ui_manager.parameter_section.rebuild_parm_section)
        self.add_to_ui_queue(self.ui_manager.header_footer.update_header_chain_id)
        self.add_to_ui_queue(self.ui_manager.header_footer.update_header_module_label)
        self.add_to_ui_queue(self.ui_manager.header_footer.update_header_chain_preview)
        self.add_to_ui_queue(self.ui_manager.chain.rebuild_chain_section)


    def move_active_parm_element(self, delta):
        self.state.move_active_parm_elem(delta)
        if self.state.active_parm < 0:
            self.add_to_ui_queue(self.ui_manager.parameter_section.cursor_change_state)
        else:
            self.add_to_ui_queue(self.ui_manager.parameter_section.update_parm_selection)


    def enter_parm_edit(self):
        if self.state.active_parm == -2: #Check if on chain selection -> Switch back to chain selection
            self.state.active_ui_section = UISection.CHAIN
    
            self.add_to_ui_queue(self.ui_manager.switch_section)

        elif self.state.active_parm == -1: #Check if on chain selection -> Switch to module selection
            self.enter_module_selection()
            return
        else:  # Go into parm edit
            self.state.active_ui_section = UISection.PARMEDIT

            active_parm = self.state.get_active_module_parm()
            if active_parm is None:
                self.enter_chain_view()
                return
            enter_result = active_parm.enter()
            if self.state.active_ui_section != UISection.PARMEDIT:
                return
            if enter_result == Parms.ParmEnterResult.RETURN_TO_SELECTION:
                self.state.active_ui_section = UISection.PARMSELECTION

            self.add_to_ui_queue(self.ui_manager.parameter_section.cursor_change_state)

    def exit_parm_edit(self):
        """Apply current parm setting and go back to parm selection mode"""
        active_parm = self.state.get_active_module_parm()
        if active_parm is None:
            self.enter_chain_view()
            return
        active_parm.exit()
        if self.state.active_ui_section != UISection.PARMEDIT:
            self.add_to_ui_queue(self.ui_manager.switch_section)
            return
        self.state.active_ui_section = UISection.PARMSELECTION


        self.add_to_ui_queue(self.ui_manager.parameter_section.cursor_change_state)

    def edit_parm(self, delta):
        active_parm = self.state.get_active_module_parm()
        if active_parm is None:
            self.enter_chain_view()
            return
        new_value, new_display_value = active_parm.edit(delta)

        self.ui_manager.parameter_section.queue_parm_value_update(new_display_value)
        self.add_to_ui_queue(self.ui_manager.parameter_section.flush_parm_value_update)

    def enter_parm_selection(self):
        """Switch state to active module"""
        active_module = self.state.chain_modules[self.state.active_chain]
        if active_module is None or active_module.name == "empty":
            self.enter_module_selection()
            return
        else:
            self.state.active_ui_section = UISection.PARMSELECTION
            self.state.active_parm = 0
    
            self.add_to_ui_queue(self.ui_manager.parameter_section.rebuild_parm_section)
        self.add_to_ui_queue(self.ui_manager.switch_section)

    def enter_chain_view(self):
        self.state.active_ui_section = UISection.CHAIN
        self.state.active_parm = 0

        self.add_to_ui_queue(self.ui_manager.switch_section)

    def enter_module_selection(self):
        if self.state.active_chain == ChainElements.TRANSPORT:
            return
        self.state.active_ui_section = UISection.MODULESELECTION
        self.state.module_selector_enter()

        self.add_to_ui_queue(self.ui_manager.module_selector.update_module_name)
        self.add_to_ui_queue(self.ui_manager.switch_section)

    def move_module_selection(self, delta):
        self.state.module_selector_change_module_selection(delta)

        self.add_to_ui_queue(self.ui_manager.module_selector.update_module_name)

    def exit_module_selection(self):
        module_loaded = self.state.module_selector_apply_module_selection()
        self.add_to_ui_queue(self.ui_manager.header_footer.update_header_module_label)
        self.add_to_ui_queue(self.ui_manager.header_footer.update_header_chain_preview)
        active_module = self.state.chain_modules[self.state.active_chain]
        active_module_name = active_module.name if active_module is not None else "empty"
        if module_loaded is False:
            self.state.active_ui_section = UISection.CHAIN
            self.add_to_ui_queue(self.ui_manager.switch_section)
            return
        if active_module_name == "empty":
            self.state.active_ui_section = UISection.CHAIN
    
            self.add_to_ui_queue(self.ui_manager.switch_section)
        else:
            self.enter_parm_selection()

    def enable_chain_swap_mode(self):
        if self.state.active_chain < SWAP_CHAIN_MIN:
            self.disable_chain_swap_mode()
    
            return
        if self.state.active_chain > SWAP_CHAIN_MAX:
            self.disable_chain_swap_mode()
    
            return
        self.state.active_chain_mode = state.ChainModes.SWAP

        self.add_to_ui_queue(self.ui_manager.chain.rebuild_chain_section)

    def disable_chain_swap_mode(self):
        self.state.active_chain_mode = state.ChainModes.SELECT

        self.add_to_ui_queue(self.ui_manager.chain.rebuild_chain_section)
        self.knob_hold_down_start = None
        self.chain_swap_mode = 0

    def run(self):
        while True:
            #current = ticks.ticks_ms()
            self.note_ons.clear()
            self.note_offs.clear()

            ### Get input
            pressed,released,knob_delta,downstate = self.input_manager.get_inputs()

            if any(pressed) or any(released) or knob_delta != 0:
                self.wake_screen()

            ### Process inputs ###
            ## Process encoder knob turn ##
            # -> depends on state - either navbar jogging, parm jogging or parm modification
            if knob_delta!=0:
                # Active section: Chain #
                if self.state.active_ui_section == UISection.CHAIN:
                    self.move_active_chain_element(knob_delta)

                elif self.state.active_ui_section == UISection.MODULESELECTION:
                    self.move_module_selection(knob_delta)

                # Active section: Parm Selection #
                elif self.state.active_ui_section == UISection.PARMSELECTION:
                    self.move_active_parm_element(knob_delta)

                # Active Section: Parm Edit #
                elif self.state.active_ui_section == UISection.PARMEDIT:
                    self.edit_parm(knob_delta)


            ## Process encoder button press ##
            # -> depends on state - either navbar selection, parm selection, or parm confirmation
            if pressed[0]:
                self.knob_hold_down_start = ticks.ticks_ms()
                self.input_manager.encoder_press_consumed = 0 # Since encoder press can either be a modifier or a selection we don't do anything on press and see if it was "consumed" by key presses
                pressed [0] = 0
                self.neo_pixels.set_nav_state_colors()

            ## Chain swap mode - timer ##
            if (self.state.active_ui_section == UISection.CHAIN and 
                    self.knob_hold_down_start and self.chain_swap_mode == 0 and downstate[0]):
                if ticks.ticks_diff(ticks.ticks_ms(), self.knob_hold_down_start) >= 1000:
                    self.chain_swap_mode = 1
                    self.enable_chain_swap_mode()

            ## Process midi key press ##
            # -> if knob down then use it as function - if not emit notes
            if any(pressed):
                self.input_manager.encoder_press_consumed = 1
                if self.chain_swap_mode == 1: # Cancel Pending
                    self.disable_chain_swap_mode()
                if downstate[0]==1: #knob is held -> combination
                    self.input_manager.encoder_press_consumed = 1

                    if pressed[2]: # Start Clock
                        self.transport.clock_start()
                        self.add_to_ui_queue(self.ui_manager.header_footer.update_footer_state_icons)

                    if pressed[1]: # Stop Clock
                        self.transport.clock_stop()
                        self.add_to_ui_queue(self.ui_manager.header_footer.update_footer_state_icons)

                    if pressed[4]: # Swap Module
                        self.enter_module_selection()

                    if pressed[5]: # Go to Chain
                        self.enter_chain_view()

                    if pressed[7]: # Switch Nav State
                        self.state.nav_keys_state = (self.state.nav_keys_state+1) % 2
                        self.neo_pixels.set_nav_state_colors(update=True)
                        self.add_to_ui_queue(self.ui_manager.header_footer.update_footer_state_icons)

                    if pressed[9]: # Switch between Nav and Modify State
                        if self.state.active_ui_section == UISection.CHAIN:
                            if self.state.active_chain_mode == state.ChainModes.SWAP:
                                self.disable_chain_swap_mode()
                            self.enter_parm_selection()
                        elif self.state.active_ui_section == UISection.MODULESELECTION:
                            self.exit_module_selection()
                        elif self.state.active_ui_section == UISection.PARMSELECTION:
                            self.enter_parm_edit()
                        elif self.state.active_ui_section == UISection.PARMEDIT:
                            self.exit_parm_edit()

                    if self.state.nav_keys_state == 0: #Nav Notes
                        if pressed[8]:
                            self.state.octave+=1
                            self.add_to_ui_queue(self.ui_manager.header_footer.update_header_key_info)
                        if pressed[11]:
                            self.state.octave-=1
                            self.add_to_ui_queue(self.ui_manager.header_footer.update_header_key_info)
                        if pressed[10]:
                            self.state.key_offset -= 1
                        if pressed[12]:
                            self.state.key_offset += 1
                        if self.state.active_chain == ChainElements.IN:
                            in_module = self.state.chain_modules[self.state.active_chain]
                            parm_octave = in_module.get_parm_by_name("octave")
                            if parm_octave:
                                parm_octave.set_value(self.state.octave)
                            key_offset = in_module.get_parm_by_name("key_offset")
                            if key_offset:
                                key_offset.set_value(self.state.key_offset)
                            self.add_to_ui_queue(self.ui_manager.parameter_section.rebuild_parm_section)

                    if self.state.nav_keys_state == 1: #Nav Parms
                        # Active section: Chain #
                        if self.state.active_ui_section == UISection.CHAIN:
                            if pressed[8]: #UP
                                if self.state.active_chain_mode == state.ChainModes.SWAP:
                                    self.disable_chain_swap_mode()
                                self.state.active_ui_section = UISection.PARMSELECTION
                                self.move_active_parm_element(-1)
                            if pressed[11]: #DOWN
                                if self.state.active_chain_mode == state.ChainModes.SWAP:
                                    self.disable_chain_swap_mode()
                                self.state.active_ui_section = UISection.PARMSELECTION
                                self.move_active_parm_element(1)
                            if pressed[10]: #LEFT
                                self.move_active_chain_element(-1)
                            if pressed[12]: #RIGHT
                                self.move_active_chain_element(1)

                        # Active section: Module Selection #
                        elif self.state.active_ui_section == UISection.MODULESELECTION:
                            if pressed[8]:  # UP
                                self.move_module_selection(-1)
                            if pressed[11]:  # DOWN
                                self.move_module_selection(1)
                            if pressed[10]:  # LEFT
                                self.move_module_selection(-1)
                            if pressed[12]:  # RIGHT
                                self.move_module_selection(1)

                        # Active section: Parm Selection #
                        elif self.state.active_ui_section == UISection.PARMSELECTION:
                            if pressed[8]:  # UP
                                self.move_active_parm_element(-1)
                            if pressed[11]:  # DOWN
                                self.move_active_parm_element(1)
                            if pressed[10]:  # LEFT
                                self.state.active_parm = 0
                                self.move_active_chain_element(-1)
                            if pressed[12]:  # RIGHT
                                self.state.active_parm = 0
                                self.move_active_chain_element(1)

                        # Active Section: Parm Edit #
                        elif self.state.active_ui_section == UISection.PARMEDIT:
                            if pressed[8]:  # UP
                                self.edit_parm(1)
                            if pressed[11]:  # DOWN
                                self.edit_parm(-1)
                            if pressed[10]:  # LEFT
                                self.edit_parm(-1)
                            if pressed[12]:  # RIGHT
                                self.edit_parm(1)

                else: #knob is not held -> simple button press
                    # Generate note ons from keys
                    for index in range(1, 13):
                        if pressed[index]:
                            self.note_ons.append_value(index - 1)
                    if not self.transport.running and self.state.transport_mode == 0:
                        self.transport.clock_start()
                        self.add_to_ui_queue(self.ui_manager.header_footer.update_footer_state_icons)

                for index in range(1, 13):
                    if pressed[index]:
                        self.neo_pixels.set_held_pixel(index-1)

            ## Process encoder button release ##
            # -> depends on state - either navbar selection, parm selection, or parm confirmation
            if released[0]:
                if self.input_manager.encoder_press_consumed == 1: #Was consumed by a key press
                    self.input_manager.encoder_press_consumed = None
                    self.output_manager.all_notes_off()
                    if self.chain_swap_mode == 1:
                        self.disable_chain_swap_mode()
                else: # Was not consumed -> knob action

                    # Active section: Chain #
                    if self.state.active_ui_section == UISection.CHAIN:
                        #Swap mode#
                        if self.chain_swap_mode == 1:
                            self.enable_chain_swap_mode()
                            self.chain_swap_mode = 2
                        elif self.chain_swap_mode == 2:
                            self.disable_chain_swap_mode()
                        else: #Disable timer etc
                            self.chain_swap_mode = 0
                            self.knob_hold_down_start = None
                            #Switch state to active module
                            self.enter_parm_selection()

                    elif self.state.active_ui_section == UISection.MODULESELECTION:
                        self.exit_module_selection()

                    # Active section: Parm Selection #
                    elif self.state.active_ui_section == UISection.PARMSELECTION:
                        #Check if on chain selection -> Switch back to chain selection
                        self.enter_parm_edit()

                    # Active section: Parm Edit #
                    elif self.state.active_ui_section == UISection.PARMEDIT:
                        # Apply current parm setting and go back to parm selection mode
                        self.exit_parm_edit()

                released[0] = 0 # Clear knob release bit
                self.neo_pixels.exit_nav_state_colors()
                self.state.chain_modules[0].color_pixels()

            ## Process midi key release ##
            # -> if knob down then use it as function - if not emit notes
            if any(released):
                if downstate[0]: #knob is held -> combination
                    self.input_manager.encoder_press_consumed = 1
                # Generate note offs from keys
                for index in range(1, 13):
                    if released[index]:
                        self.note_offs.append_value(index - 1)
                for index in range(1, 13):
                    if released[index]:
                        self.neo_pixels.release_held_pixel(index-1)

            ### Update transport
            midi_tick = self.transport.update()

            ### Process slots ###
            ## Process input ##
            note_ons, note_offs = self.state.chain_modules[ChainElements.IN].process_super(self.note_ons, self.note_offs)

            ## Process modules ##
            for slot in range(ChainElements.SLOT1, ChainElements.SLOT6 + 1):
                note_ons, note_offs = self.state.chain_modules[slot].process_super(note_ons, note_offs)
            ## Process output ##
            note_ons, note_offs = self.state.chain_modules[ChainElements.OUT].process_super(note_ons, note_offs)

            ### Output ###
            self.output_manager.process_midi_out()
            self.output_manager.process_midi_clock_out()

            self.maybe_gc()

            ### Update UI & screen ###
            if self.ui_queue and self.can_do_ui_work(min_slack_ms=12):
                callback = self.ui_queue.pop(0)
                current = ticks.ticks_ms()
                callback()
                difference = ticks.ticks_diff(ticks.ticks_ms(), current)

                # mark refresh, but don't flush display immediately
                self.screen_update_needed = True

            self.maybe_sleep_screen()

            if not self.screen_sleeping and self.screen_update_needed and self.run_tick % SCREEN_REFRESH_RATE == 0 and self.can_do_ui_work(min_slack_ms=12):
                self.ui_manager.screen.update()
                self.screen_update_needed = False

            self.maybe_gc()

            ### Loop Progression ###
            self.run_tick+=1
