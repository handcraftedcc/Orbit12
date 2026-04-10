#Handles all UI
from rainbowio import colorwheel
from adafruit_macropad import MacroPad
import displayio
import terminalio
from adafruit_display_text.bitmap_label import Label

DISPLAYRES = (128,64)


class UIManager:
    def __init__(self,macropad: MacroPad):
        self.macropad = macropad
        self.main_group = displayio.Group()
        self.neopixels = NeoPixels(self.macropad)
        
        self.palette = displayio.Palette(4) 
        self.palette.make_transparent(0) # 0 = transparent
        self.palette[1] = 0xFFFFFF # 1 = normal text (white)
        self.palette[2] = 0x000000 # 2 = selected text (black)
        self.palette[3] = 0xFFFFFF # 3 = selected background (white)

        self.margin = 3
        self.parmspacing = 2
        self.chainparmsepspacing = 4

        self.chain = Chain(self.palette, self.margin, self.main_group)
        self.parameter_section = ParameterSection(self.palette, self.margin, self.main_group, self.parmspacing, self.chainparmsepspacing)

        self.screen = Screen(self.macropad,self.main_group)


class Screen:
    def __init__(self,macropad: MacroPad,main_group):
        self.display = macropad.display
        self.display.root_group = main_group
        macropad.display.refresh()

    def update(self):
        self.display.refresh()

class NeoPixels:
    def __init__(self,macropad: MacroPad):
        key_color = colorwheel(120)  # fill with cyan to start
        macropad.pixels.brightness = 0.1
        macropad.pixels.fill(key_color)

class Section:
    def __init__(self):
        self.visible = True
        self.group = displayio.Group()
        pass

class Chain(Section):
    def __init__(self,palette,margin,main_group):
        super().__init__()
        
        self.items = ["IN", "T", "1", "2", "3", "4", "5", "6", "OUT"]
        self.selected = 0
        text = "-".join(self.items)
        self.text = text
        self.chain_label = Label(
            terminalio.FONT,
            text=self.text,
            color=0xFFFFFF,
            color_palette=palette,
        )
        self.chain_label.anchor_point = (0,0)
        self.chain_label.anchored_position = (margin,margin-3)
        self.group.append(self.chain_label)

        self.set_selected(0)

        # divider line
        line = displayio.Bitmap(DISPLAYRES[0]-margin*2, 1, 2)  # width, height, colors
        line.fill(1)
        tile = displayio.TileGrid(line, pixel_shader=palette)
        tile.x = margin
        tile.y = margin + 10

        self.group.append(tile)

        main_group.append(self.group)

    def set_selected(self,selected):
        self.chain_label.clear_accent_ranges()
        self.chain_label.add_accent_to_substring(self.items[selected], 2, 3)
        self.selected = selected

class ParameterSection(Section):
    def __init__(self,palette,margin,main_group,sparmspacing,chainparmsepspacing):
        super().__init__()
        self.palette = palette
        self.margin = margin
        self.highlighted = None
        self.active = None
        self.parmcount = 4
        self.parmlabels = []
        self.parmvalues = []
        
        for i in range(self.parmcount):
            ypos = 15+11*i
            labeltext = "label" + str(i)
            parmlabel = Label(terminalio.FONT, color_palette=palette, text=labeltext)
            parmlabel.anchor_point = (0, 0)
            parmlabel.anchored_position = (margin, ypos)
            self.parmlabels.append(parmlabel)
            valuetext = str(5)
            parmvalue = Label(terminalio.FONT, color_palette=self.palette, text=valuetext)
            parmvalue.anchor_point = (1, 0)
            parmvalue.anchored_position = (DISPLAYRES[0]-margin, ypos)
            self.parmvalues.append(parmvalue)
            self.group.append(parmlabel)
            self.group.append(parmvalue)

        main_group.append(self.group)
            
    def set_parm(self, i, labeltext, newvalue):
        label = self.parmlabels[i]
        value = self.parmvalues[i]
        label.text = labeltext
        value.text = str(newvalue)

    def set_parm_value(self, i, newvalue):
        value = self.parmvalues[i]
        value.text = str(newvalue)

    def highlight_parm(self, highlightid):
        if self.highlighted: 
            self.parmlabels[self.highlighted].clear_accent_ranges()
        thislabel = self.parmlabels[highlightid]
        thislabel.add_accent_to_substring(thislabel.text, 2, 3)
        self.highlighted = highlightid

    def clear_parm_highlights(self):
        for i in range(self.parmcount):
            self.parmlabels[i].clear_accent_ranges()


