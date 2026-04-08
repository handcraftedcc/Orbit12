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

        self.screen = ui.Screen(self.macropad)
        self.neopixels = ui.NeoPixels(self.macropad)

        self.clock = transport.clock()
        self.input = input.InputManager(self.macropad)

        self.runtick = 0

    def run(self):
        while True:
            
            # Update transport
            midi_tick = self.clock.update()
            # Get input
            inputs = self.input.get_inputs()
            # Update UI & screen (every nth tick)
            if self.runtick
            # Process slots
             
            # Output
            
            #Manages the flow through the loop

            self.runtick+=1