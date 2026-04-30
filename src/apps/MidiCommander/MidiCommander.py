#pylint:disable= 'unindent does not match any outer indentation level (apps.MidiCommander.MidiCommander, line 92)'
# Main app manager
from adafruit_macropad import MacroPad
from .core import ui
from .core import transport
from .core import input
from .core import state
from .core import parms as Parms

SCREENREFRESHRATE = 1

class MidiCommander:
    def __init__(self):
        self.macropad = MacroPad(rotation=0)  # create the macropad object, rotate orientation
        self.macropad.display.auto_refresh = False  # avoid lag
        self.macropad.encoder_switch_debounced.interval = 0.001

        self.state = state.State()
        self.ui_manager = ui.UIManager(self.macropad,self.state)
        self.state.ui_manager = self.ui_manager

        self.clock = transport.clock()
        self.input = input.InputManager(self.macropad)


        self.run_tick = 0

        self.encoder_consumed = None

    def run(self):
        while True:
            
            screen_update_needed = False
            
            ### Update transport
            midi_tick = self.clock.update()

            ### Get input
            pressed,released,knob_delta,downstate = self.input.get_inputs()

            # Bitmasks:
            # FLAG = 1 << spot
            # Set bit: mask |= FLAG
            # Clear bit: mask &= ~FLAG
            # Test bit: mask & FLAG
            # Toggle bit: mask ^= FLAG

            # KNOB bit == 1
            # Key bits == 1 << KeyNum

            ### Process inputs
            # Process encoder knob turn
            # -> depends on state - either navbar jogging, parm jogging or parm modification
            if knob_delta!=0:
                screen_update_needed = True
                if self.state.active_ui_section == state.UISection.CHAIN:
                        self.state.move_active_chain_elem(knob_delta)
                        self.ui_manager.chain.set_selected(self.state.active_chain)
                        self.ui_manager.parmeter_section.rebuild_parm_section()
                elif self.state.active_ui_section == state.UISection.PARMSELECTION:
                    current_page = self.state.active_parm_page
                    self.state.move_active_parm_elem(knob_delta)
                    if self.state.active_parm < 0:
                        self.ui_manager.chain.highlight_chain()
                        self.ui_manager.parmeter_section.clear_parm_highlights()
                    else:
                        self.ui_manager.chain.clear_chain_highlights()
                        self.ui_manager.parmeter_section.highlight_parm()
                    if current_page != self.state.active_parm_page:
                        self.ui_manager.parmeter_section.rebuild_parm_section()
                elif self.state.active_ui_section == state.UISection.PARMEDIT:
                    pass

            # Process encoder button press
            # -> depends on state - either navbar selection, parm selection, or parm confirmation
            if pressed & 1:
                self.input.encoder_press_consumed = 0 # Since encoder press can either be a modifier or a selection we don't do anything on press and see if it was "consumed" by key presses
                pressed &= ~1

            # Process midi key press
            # -> if knob down then use it as function - if not emit notes
            if pressed != 0:
                if downstate & 1: #knob is held -> combination
                    self.input.encoder_press_consumed = 1
                else: #knob is not held -> simple button press
                    pass

            # Process encoder button press
            # -> depends on state - either navbar selection, parm selection, or parm confirmation
            if released & 1:
                if self.input.encoder_press_consumed == 1: #Was consumed by a key press
                    self.input.encoder_press_consumed = None
                else: # Was not consumed -> knob action
                    screen_update_needed = True
                    if self.state.active_ui_section == state.UISection.CHAIN:
                            #Switch state to active module
                            self.state.active_ui_section = state.UISection.PARMSELECTION
                    elif self.state.active_ui_section == state.UISection.PARMSELECTION:
                        #Check if on chain selection -> Switch back to chain selection
                        if self.state.active_parm == -1:
                            self.ui_manager.chain.clear_chain_highlights()
                            self.state.active_ui_section = state.UISection.CHAIN
                        else:
                            self.state.active_ui_section = state.UISection.PARMEDIT
                            active_parm = self.state.get_active_module_parm()
                            enter_result = active_parm.enter()
                            if enter_result == Parms.ParmEnterResult.RETURN_TO_SELECTION:
                                self.state.active_ui_section = state.UISection.PARMSELECTION
                        #Otherwise go into parm edit mode
                    elif self.state.active_ui_section == state.UISection.PARMEDIT:
                        #Apply current parm setting and go back to parm selection mode
                        active_parm = self.state.get_active_module_parm()
                        active_parm.exit()
                        self.state.active_ui_section = state.UISection.PARMSELECTION
                        pass
                released &= ~1

            # Process midi key release
            # -> if knob down then use it as function - if not emit notes
            if pressed != 0:
                if downstate & 1: #knob is held -> combination
                    self.input.encoder_press_consumed = 1
                else: #knob is not held -> simple button press
                    pass
            

            # Update UI & screen (every nth tick)
            if screen_update_needed and self.run_tick % SCREENREFRESHRATE == 0:
                self.ui_manager.screen.update()
            # Process slots
             
            # Output

            #Manages the flow through the loop

            self.run_tick+=1