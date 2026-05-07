# Main app manager
from adafruit_macropad import MacroPad
import keypad

from .core import ui
from .core import transport
from .core import input
from .core import state
from .core import parms as Parms
from .core import music as Music

SCREENREFRESHRATE = 1

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
        self.ui_manager = ui.UIManager(self.macropad,self.state)
        self.clock = transport.Clock()
        self.input_manager = input.InputManager(self.macropad)

        #TODO: Instead of passing individual items into modules create one object that has all the references and pass that.

        # Know objects
        self.state.ui_manager = self.ui_manager
        self.state.input = self.input_manager

        # Init items that get used each loop
        self.run_tick = 0
        self.encoder_consumed = None

        self.note_ons = []
        self.note_offs = []


    def run(self):
        while True:
            
            screen_update_needed = False
            self.note_ons.clear()
            self.note_offs.clear()
            
            ### Update transport
            midi_tick = self.clock.update()

            ### Get input
            pressed,released,knob_delta,downstate = self.input_manager.get_inputs()

            # Bitmasks:
            # FLAG = 1 << spot
            # Set bit: mask |= FLAG
            # Clear bit: mask &= ~FLAG
            # Test bit: mask & FLAG
            # Toggle bit: mask ^= FLAG

            # KNOB bit == 1
            # Key bits == 1 << KeyNum

            ### Process inputs ###
            ## Process encoder knob turn ##
            # -> depends on state - either navbar jogging, parm jogging or parm modification
            if knob_delta!=0:
                screen_update_needed = True

                # Active section: Chain #
                if self.state.active_ui_section == state.UISection.CHAIN:
                        self.state.move_active_chain_elem(knob_delta)
                        self.ui_manager.chain.set_selected(self.state.active_chain)
                        self.ui_manager.parameter_section.rebuild_parm_section()

                # Active section: Parm Selection #
                elif self.state.active_ui_section == state.UISection.PARMSELECTION:
                    current_page = self.state.active_parm_page
                    self.state.move_active_parm_elem(knob_delta)
                    if self.state.active_parm < 0:
                        self.ui_manager.chain.highlight_chain()
                        self.ui_manager.parameter_section.clear_parm_highlights()
                    else:
                        self.ui_manager.chain.clear_chain_highlights()
                        self.ui_manager.parameter_section.highlight_parm()
                    if current_page != self.state.active_parm_page:
                        self.ui_manager.parameter_section.rebuild_parm_section()

                # Active Section: Parm Edit #
                elif self.state.active_ui_section == state.UISection.PARMEDIT:
                    new_value, new_display_value = self.state.get_active_module_parm().edit(knob_delta)
                    self.ui_manager.parameter_section.update_parm_value(new_display_value)


            ## Process encoder button press ##
            # -> depends on state - either navbar selection, parm selection, or parm confirmation
            if pressed & 1:
                self.input_manager.encoder_press_consumed = 0 # Since encoder press can either be a modifier or a selection we don't do anything on press and see if it was "consumed" by key presses
                pressed &= ~1



            ## Process midi key press ##
            # -> if knob down then use it as function - if not emit notes
            if pressed != 0:
                if downstate & 1: #knob is held -> combination
                    self.input_manager.encoder_press_consumed = 1
                else: #knob is not held -> simple button press
                    # Generate note ons from keys
                    for bit_index in range(1, 13):
                        if pressed & (1 << bit_index):
                            self.note_ons.append(bit_index - 1)


            ## Process encoder button release ##
            # -> depends on state - either navbar selection, parm selection, or parm confirmation
            if released & 1:
                if self.input_manager.encoder_press_consumed == 1: #Was consumed by a key press
                    self.input_manager.encoder_press_consumed = None
                else: # Was not consumed -> knob action
                    screen_update_needed = True

                    # Active section: Chain #
                    if self.state.active_ui_section == state.UISection.CHAIN:
                            #Switch state to active module
                            self.state.active_ui_section = state.UISection.PARMSELECTION
                            self.state.active_parm = 0
                            self.ui_manager.chain.clear_chain_highlights()
                            self.ui_manager.parameter_section.highlight_parm()

                    # Active section: Parm Selection #
                    elif self.state.active_ui_section == state.UISection.PARMSELECTION:
                        #Check if on chain selection -> Switch back to chain selection
                        if self.state.active_parm == -1:
                            self.ui_manager.chain.clear_chain_highlights()
                            self.state.active_ui_section = state.UISection.CHAIN
                        else: #Go into parm edit
                            self.state.active_ui_section = state.UISection.PARMEDIT
                            active_parm = self.state.get_active_module_parm()
                            enter_result = active_parm.enter()
                            if enter_result == Parms.ParmEnterResult.RETURN_TO_SELECTION:
                                self.state.active_ui_section = state.UISection.PARMSELECTION

                            self.ui_manager.parameter_section.clear_parm_highlights()
                            self.ui_manager.parameter_section.highlight_parm_value()

                    # Active section: Parm Edit #
                    elif self.state.active_ui_section == state.UISection.PARMEDIT:
                        #Apply current parm setting and go back to parm selection mode
                        active_parm = self.state.get_active_module_parm()
                        active_parm.exit()
                        self.state.active_ui_section = state.UISection.PARMSELECTION

                        self.ui_manager.parameter_section.clear_parm_value_highlight()
                        self.ui_manager.parameter_section.highlight_parm()

                released &= ~1 # Clear knob release bit

            ## Process midi key release ##
            # -> if knob down then use it as function - if not emit notes
            if released != 0:
                if downstate & 1: #knob is held -> combination
                    self.input_manager.encoder_press_consumed = 1
                else: #knob is not held -> simple button press
                    # Generate note offs from keys
                    for bit_index in range(1, 13):
                        if released & (1 << bit_index):
                            self.note_offs.append(bit_index - 1)


            ### Process slots ###
            ## Process input ##
            note_ons = self.note_ons
            note_offs = self.note_offs
            note_ons, note_offs = self.state.chain_modules[state.ChainElements.IN].process(note_ons, note_offs)

            ### Process transport ##
            #note_ons, note_offs = self.state.chain_modules[state.ChainElements.TRANSPORT].process(note_ons, note_offs)

            ## Process modules ##
            for slot in range(state.ChainElements.SLOT1,state.ChainElements.SLOT6+1):
                note_ons, note_offs = self.state.chain_modules[slot].process(note_ons, note_offs)

            ## Process output ##
            note_ons, note_offs = self.state.chain_modules[state.ChainElements.OUT].process(note_ons, note_offs)

            ### Update UI & screen (every nth tick) ###
            if screen_update_needed and self.run_tick % SCREENREFRESHRATE == 0:
                self.ui_manager.screen.update()

            ### Output ###

            ### Loop Progression ###
            self.run_tick+=1