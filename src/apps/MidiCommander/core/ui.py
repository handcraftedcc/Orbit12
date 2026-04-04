#Handles all UI
from rainbowio import colorwheel

class Screen:
    def __init__(self,macropad):
        text_lines = macropad.display_text()
        text_lines[0].text = "[I]-T-1-2-3-4-5-6-O"
        text_lines[1].text = "Parm1"
        text_lines[2].text = "Parm2"
        text_lines[3].text = "Parm3"
        text_lines[4].text = "Parm4"
        text_lines.show()
        macropad.display.refresh()

class NeoPixels:
    def __init__(self,macropad):
        key_color = colorwheel(120)  # fill with cyan to start
        macropad.pixels.brightness = 0.1
        macropad.pixels.fill(key_color)

class Element:
    def __init__(self):
        self.visible = True
        pass

class Navbar(Element):
    def __init__(self):
        super().__init__()
        pass
