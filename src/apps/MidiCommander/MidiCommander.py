# Main app manager
from adafruit_macropad import MacroPad
from .core import ui
from .core import transport
from .core import input

SCREENREFRESHRATE = 4

class MidiCommander:
    def __init__(self):
        self.macropad = MacroPad(rotation=0)  # create the macropad object, rotate orientation
        self.macropad.display.auto_refresh = False  # avoid lag

        self.ui_manager = ui.UIManager(self.macropad)

        self.clock = transport.clock()
        self.input = input.InputManager(self.macropad)

        self.runtick = 0

    def run(self):
        while True:
            
            ### Update transport
            midi_tick = self.clock.update()

            ### Get input
            pressed,released,knob_delta,downstate = self.input.get_inputs()

            ### Process inputs
            # Process encoder button
            # -> depends on state - either navbar selection, parm selection, or parm confirmation

            # Process encoder knob
            # -> depends on state - either navbar jogging, parm jogging or parm modification

            # Process midi keys
            # -> if knob down then use it as function - if not emit notes
            



            # Update UI & screen (every nth tick)
            if self.runtick:
                pass
            # Process slots
             
            # Output
            
            #Manages the flow through the loop

            self.runtick+=1