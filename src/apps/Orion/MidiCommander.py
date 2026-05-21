# Main app manager
from adafruit_macropad import MacroPad
import keypad
import adafruit_ticks as ticks
import gc

import microcontroller
microcontroller.cpu.frequency = 250_000_000

from .core import ui
from .core import transport
from .core import input
from .core import state
from .core import output
from .core import parms as Parms
from .core import music as Music
from .core import module
from .core.note_array import NoteArray, NoteOnArray, NoteOffArray

from .inputmodules.note import Note as InputModule
from .core._modules._empty import Empty as EmptyModule
from .core._modules._transport import Transport as TransportModule
from .core._modules._output import Output as OutputModule

SCREENREFRESHRATE = 4

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
        self.module_helper = module.ModuleHelper(self.macropad, self.state, self.input_manager, self.output_manager, self.transport)

        # Pass objects to state (Have to do after because of circular dependency)
        self.state.input = self.input_manager
        self.state.module_helper = self.module_helper

        # Init items that get used each loop
        self.run_tick = 0
        self.encoder_consumed = None
        self.screen_update_needed = False
        self.ui_queue = []
        self.last_gc_ms = ticks.ticks_ms()
        #self.ui_rebuild_pending = False

        self.note_ons = NoteOnArray()
        self.note_offs = NoteOffArray()

        #Init modules
        self.state.chain_modules = [None]*state.TOTALSLOTCOUNT
        self.state.chain_modules[0] = InputModule(self.module_helper,0)
        self.state.chain_modules[1] = TransportModule(self.module_helper,1)
        for slot in range (2,state.TOTALSLOTCOUNT-1):
            self.state.chain_modules[slot] = EmptyModule(self.module_helper,slot)
        self.state.chain_modules[state.TOTALSLOTCOUNT-1] = OutputModule(self.module_helper,state.TOTALSLOTCOUNT-1)
        self.state.update_parm_count()

        # Init UI
        self.ui_manager = ui.UIManager(self.macropad,self.state)
        self.module_helper.ui_manager = self.ui_manager
        self.state.ui_manager = self.ui_manager

        # Update Pixels
        self.state.chain_modules[0].color_pixels()
        self.ui_manager.neo_pixels.paint_pixels()

        #gc.disable()

    def add_to_ui_queue(self, callback):
        if callback not in self.ui_queue:
            self.ui_queue.append(callback)
        #self.ui_rebuild_pending = True

    def can_do_ui_work(self, min_slack_ms=6):
        #if self.state.transport_mode == 1:
        #    return self.output_manager.pending_midi_clock_ticks == 0
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
                    and ms_to_next >= 15
                    and not self.ui_queue
                    and ticks.ticks_diff(now, self.last_gc_ms) > 300
            )
            if safe:
                gc.collect()
                self.last_gc_ms = now

    def move_active_chain_element(self, delta):
        self.state.move_active_chain_elem(delta)
        self.state.active_parm = -1
        self.add_to_ui_queue(self.ui_manager.chain.clear_chain_highlights)
        self.add_to_ui_queue(self.ui_manager.chain.rebuild_chain_section)
        self.add_to_ui_queue(self.ui_manager.parameter_section.rebuild_parm_section)

    def move_active_parm_element(self, delta):
        current_page = self.state.active_parm_page
        self.state.move_active_parm_elem(delta)
        if self.state.active_parm < 0:
            self.add_to_ui_queue(self.ui_manager.chain.highlight_chain)
            self.add_to_ui_queue(self.ui_manager.parameter_section.clear_parm_highlights)
        else:
            self.add_to_ui_queue(self.ui_manager.chain.clear_chain_highlights)
            self.add_to_ui_queue(self.ui_manager.parameter_section.highlight_parm)
        if current_page != self.state.active_parm_page:
            self.add_to_ui_queue(self.ui_manager.parameter_section.rebuild_parm_section)

    def enter_parm_edit(self):
        if self.state.active_parm == -1: #Check if on chain selection -> Switch back to chain selection
            self.add_to_ui_queue(self.ui_manager.chain.clear_chain_highlights)
            self.state.active_ui_section = state.UISection.CHAIN
        else:  # Go into parm edit
            self.state.active_ui_section = state.UISection.PARMEDIT
            active_parm = self.state.get_active_module_parm()
            enter_result = active_parm.enter()
            if enter_result == Parms.ParmEnterResult.RETURN_TO_SELECTION:
                self.state.active_ui_section = state.UISection.PARMSELECTION

            self.add_to_ui_queue(self.ui_manager.parameter_section.clear_parm_highlights)
            self.add_to_ui_queue(self.ui_manager.parameter_section.highlight_parm_value)

    def exit_parm_edit(self):
        """Apply current parm setting and go back to parm selection mode"""
        active_parm = self.state.get_active_module_parm()
        active_parm.exit()
        self.state.active_ui_section = state.UISection.PARMSELECTION

        self.add_to_ui_queue(self.ui_manager.parameter_section.clear_parm_value_highlight)
        self.add_to_ui_queue(self.ui_manager.parameter_section.highlight_parm)

    def edit_parm(self, delta):
        new_value, new_display_value = self.state.get_active_module_parm().edit(delta)
        self.ui_manager.parameter_section.update_parm_value(new_display_value)

    def enter_parm_selection(self):
        """Switch state to active module"""
        self.state.active_ui_section = state.UISection.PARMSELECTION
        self.state.active_parm = 0
        self.add_to_ui_queue(self.ui_manager.chain.clear_chain_highlights)
        self.add_to_ui_queue(self.ui_manager.parameter_section.highlight_parm)

    def run(self):
        while True:
            #current = ticks.ticks_ms()
            self.note_ons.clear()
            self.note_offs.clear()

            ### Get input
            pressed,released,knob_delta,downstate = self.input_manager.get_inputs()

            ### Process inputs ###
            ## Process encoder knob turn ##
            # -> depends on state - either navbar jogging, parm jogging or parm modification
            if knob_delta!=0:
                # Active section: Chain #
                if self.state.active_ui_section == state.UISection.CHAIN:
                    self.move_active_chain_element(knob_delta)

                # Active section: Parm Selection #
                elif self.state.active_ui_section == state.UISection.PARMSELECTION:
                    self.move_active_parm_element(knob_delta)

                # Active Section: Parm Edit #
                elif self.state.active_ui_section == state.UISection.PARMEDIT:
                    self.edit_parm(knob_delta)


            ## Process encoder button press ##
            # -> depends on state - either navbar selection, parm selection, or parm confirmation
            if pressed[0]:
                self.input_manager.encoder_press_consumed = 0 # Since encoder press can either be a modifier or a selection we don't do anything on press and see if it was "consumed" by key presses
                pressed [0] = 0
                self.ui_manager.neo_pixels.set_nav_state_colors()

            ## Process midi key press ##
            # -> if knob down then use it as function - if not emit notes
            if any(pressed):
                self.input_manager.encoder_press_consumed = 1
                if downstate[0]==1: #knob is held -> combination
                    self.input_manager.encoder_press_consumed = 1
                    if pressed[11]:
                        self.transport.clock_start()
                    if pressed[10]:
                        self.transport.clock_stop()

                    if pressed[1]: # Switch Nav State
                        self.state.nav_keys_state = (self.state.nav_keys_state+1) % 2
                        self.ui_manager.neo_pixels.set_nav_state_colors(update=True)

                    if pressed[3]: # Switch between Nav and Modify State
                        if self.state.active_ui_section == state.UISection.PARMSELECTION:
                            self.enter_parm_edit()
                        elif self.state.active_ui_section == state.UISection.PARMEDIT:
                            self.exit_parm_edit()

                    if self.state.nav_keys_state == 0: #Nav Notes
                        if pressed[2]:
                            self.state.octave+=1
                        if pressed[5]:
                            self.state.octave-=1
                        if pressed[4]:
                            self.state.key_offset -= 1
                        if pressed[6]:
                            self.state.key_offset += 1

                    if self.state.nav_keys_state == 1: #Nav Parms
                        # Active section: Chain #
                        if self.state.active_ui_section == state.UISection.CHAIN:
                            if pressed[2]: #UP
                                self.state.active_ui_section = state.UISection.PARMSELECTION
                                self.move_active_parm_element(-1)
                            if pressed[5]: #DOWN
                                self.state.active_ui_section = state.UISection.PARMSELECTION
                                self.move_active_parm_element(1)
                            if pressed[4]: #LEFT
                                self.move_active_chain_element(-1)
                            if pressed[6]: #RIGHT
                                self.move_active_chain_element(1)

                        # Active section: Parm Selection #
                        elif self.state.active_ui_section == state.UISection.PARMSELECTION:
                            if pressed[2]:  # UP
                                self.move_active_parm_element(-1)
                            if pressed[5]:  # DOWN
                                self.move_active_parm_element(1)
                            if pressed[4]:  # LEFT
                                self.state.active_ui_section = state.UISection.CHAIN
                                self.state.active_parm = -1
                                self.ui_manager.parameter_section.clear_parm_highlights()
                                self.move_active_chain_element(-1)
                            if pressed[6]:  # RIGHT
                                self.state.active_ui_section = state.UISection.CHAIN
                                self.state.active_parm = -1
                                self.ui_manager.parameter_section.clear_parm_highlights()
                                self.move_active_chain_element(1)

                        # Active Section: Parm Edit #
                        elif self.state.active_ui_section == state.UISection.PARMEDIT:
                            if pressed[2]:  # UP
                                self.edit_parm(1)
                            if pressed[5]:  # DOWN
                                self.edit_parm(-1)
                            if pressed[4]:  # LEFT
                                self.edit_parm(-1)
                            if pressed[6]:  # RIGHT
                                self.edit_parm(1)


                else: #knob is not held -> simple button press
                    # Generate note ons from keys
                    for index in range(1, 13):
                        if pressed[index]:
                            self.note_ons.append_value(index - 1)
                    if not self.transport.running and self.state.transport_mode == 0:
                        self.transport.clock_start()

                for index in range(1, 13):
                    if pressed[index]:
                        self.ui_manager.neo_pixels.set_held_pixel(index-1)

            ## Process encoder button release ##
            # -> depends on state - either navbar selection, parm selection, or parm confirmation
            if released[0]:
                if self.input_manager.encoder_press_consumed == 1: #Was consumed by a key press
                    self.input_manager.encoder_press_consumed = None
                    self.output_manager.all_notes_off()
                else: # Was not consumed -> knob action
                    # Active section: Chain #
                    if self.state.active_ui_section == state.UISection.CHAIN:
                            #Switch state to active module
                            self.enter_parm_selection()

                    # Active section: Parm Selection #
                    elif self.state.active_ui_section == state.UISection.PARMSELECTION:
                        #Check if on chain selection -> Switch back to chain selection
                        self.enter_parm_edit()

                    # Active section: Parm Edit #
                    elif self.state.active_ui_section == state.UISection.PARMEDIT:
                        # Apply current parm setting and go back to parm selection mode
                        self.exit_parm_edit()

                released[0] = 0 # Clear knob release bit
                self.ui_manager.neo_pixels.exit_nav_state_colors()
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
                        self.ui_manager.neo_pixels.release_held_pixel(index-1)

            ### Update transport
            midi_tick = self.transport.update()

            ### Process slots ###
            ## Process input ##
            note_ons, note_offs = self.state.chain_modules[state.ChainElements.IN].process_super(self.note_ons, self.note_offs)

            ## Process modules ##
            for slot in range(state.ChainElements.SLOT1, state.ChainElements.SLOT6 + 1):
                note_ons, note_offs = self.state.chain_modules[slot].process_super(note_ons, note_offs)
            ## Process output ##
            note_ons, note_offs = self.state.chain_modules[state.ChainElements.OUT].process_super(note_ons, note_offs)

            ### Output ###
            self.output_manager.process_midi_out()
            self.output_manager.process_midi_clock_out()

            self.maybe_gc()

            ### Update UI & screen ###
            if self.ui_queue and self.can_do_ui_work(min_slack_ms=13):
                callback = self.ui_queue.pop(0)
                current = ticks.ticks_ms()
                callback()
                difference = ticks.ticks_diff(ticks.ticks_ms(), current)

            # mark refresh, but don't flush display immediately
            self.screen_update_needed = True

            #diff = ticks.ticks_diff(ticks.ticks_ms(), current)
            #if diff > 3: print(diff)

            if self.screen_update_needed and self.run_tick % SCREENREFRESHRATE == 0 and self.can_do_ui_work():
                self.ui_manager.screen.update()
                self.screen_update_needed = False

            self.maybe_gc()

            ### Loop Progression ###
            self.run_tick+=1
