# Main app manager
from adafruit_macropad import MacroPad
from .core import ui



class MidiCommander:
    def __init__(self):
        self.macropad = MacroPad(rotation=0)  # create the macropad object, rotate orientation
        self.macropad.display.auto_refresh = False  # avoid lag

        self.screen = ui.Screen(self.macropad)
        self.neopixels = ui.NeoPixels(self.macropad)

    def run(self):
        while True:
            pass
            #Manages the flow through the loop