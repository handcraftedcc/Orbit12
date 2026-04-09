# Main app manager
from adafruit_macropad import MacroPad
from .core import ui
from .core import transport
from .core import input
from .core import state

SCREENREFRESHRATE = 4

class MidiCommander:
    def __init__(self):
        self.macropad = MacroPad(rotation=0)  # create the macropad object, rotate orientation
        self.macropad.display.auto_refresh = False  # avoid lag

        self.state = state.State()
        self.ui_manager = ui.UIManager(self.macropad)
        self.clock = transport.clock()
        self.input = input.InputManager(self.macropad)


        self.runtick = 0

        self.encoderconsumed = None

    def run(self):
        while True:
            
            ### Update transport
            midi_tick = self.clock.update()

            ### Get input
            pressed,released,knob_delta,downstate = self.input.get_inputs()

            # Bitmasks:
            # Set bit: mask |= FLAG
            # Clear bit: mask &= ~FLAG
            # Test bit: mask & FLAG
            # Toggle bit: mask ^= FLAG

            ### Process inputs
            # Process encoder knob turn
            # -> depends on state - either navbar jogging, parm jogging or parm modification
            if self.state.active_ui_section == state.UISection.CHAIN:
                self.state.move_active_chain_elem(knob_delta)
            elif self.state.active_ui_section == state.UISection.PARMSELECTION:
                self.state.move_active_param_elem(knob_delta)
            elif self.state.active_ui_section == state.UISection.PARMEDIT:
                pass

            # Process encoder button press
            # -> depends on state - either navbar selection, parm selection, or parm confirmation
            if pressed & 0:
                self.input.encoderpressconsumed = 0 # Since encoder press can either be a modifier or a selection we don't do anything on press and see if it was "consumed" by key presses
                pressed &= ~0

            # Process midi keys
            # -> if knob down then use it as function - if not emit notes
            if pressed != 0:
                if downstate & 0: #knob is held -> combination
                    self.input.encoderpressconsumed = 1
                    pass
                else: #knob is not held -> simple button press
                    pass

            # Process encoder button press
            # -> depends on state - either navbar selection, parm selection, or parm confirmation
            if released & 0:
                if self.input.encoderpressconsumed == 1: #Was consumed by a key press
                    self.input.encoderpressconsumed = None
                else: # Was not consumed -> knob action
                    if self.state.active_ui_section == state.UISection.CHAIN:
                        pass
                    elif self.state.active_ui_section == state.UISection.PARMSELECTION:
                        pass
                    elif self.state.active_ui_section == state.UISection.PARMEDIT:
                        pass

            
            

            # Update UI & screen (every nth tick)
            if self.runtick % SCREENREFRESHRATE == 0:
            	self.screen.update()
            # Process slots
             
            # Output
            
            #Manages the flow through the loop

            self.runtick+=1