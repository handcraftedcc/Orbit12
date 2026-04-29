#Handles all UI
from rainbowio import colorwheel
from adafruit_macropad import MacroPad
import displayio
import terminalio
import vectorio
from adafruit_display_text.bitmap_label import Label
from .state import State
from .state import PARMSPERPAGE

DISPLAYRES = (128,64)

class UIManager:
    def __init__(self,macropad: MacroPad,state):
        self.macropad = macropad
        self.main_group = displayio.Group()
        self.neopixels = NeoPixels(self.macropad)
        self.state = state
        
        self.palette = displayio.Palette(4) 
        self.palette.make_transparent(0) # 0 = transparent
        self.palette[1] = 0xFFFFFF # 1 = normal text (white)
        self.palette[2] = 0x000000 # 2 = selected text (black)
        self.palette[3] = 0xFFFFFF # 3 = selected background (white)

        self.margin = 3
        self.parmspacing = 2
        self.chainparmsepspacing = 4

        self.chain = Chain(self.state, self.palette, self.margin, self.main_group)
        self.parmeter_section = parmeterSection(self.state, self.palette, self.margin, self.main_group, self.parmspacing, self.chainparmsepspacing)

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
    def __init__(self,state: State):
        self.visible = True
        self.group = displayio.Group()
        self.state = state
        pass

class Chain(Section):
    def __init__(self,state,palette,margin,main_group):
        super().__init__(state)
        
        self.items = ["I", "T", "1", "2", "3", "4", "5", "6", "O"]
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
        labels = self.items.copy()
        labels[selected]=f"[{labels[selected]}]"
        self.chain_label.text="-".join(labels)
        self.selected = selected

    def highlight_chain(self):
        self.chain_label.clear_accent_ranges()
        self.chain_label.add_accent_range(0,len(self.chain_label.text), 2, 3)

    def clear_chain_highlights(self):
        self.chain_label.clear_accent_ranges()

class parmeterSection(Section):
    def __init__(self,state,palette,margin,main_group,sparmspacing,chainparmsepspacing):
        super().__init__(state)
        self.palette = palette
        self.margin = margin
        self.highlighted = None
        self.active = None
        self.pages = 1
        self.active_page = 0
        self.parmcount = 4
        self.parmlabels = []
        self.parmvalues = []
        
        for i in range(PARMSPERPAGE):
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

        # page indicator line
        track_width = DISPLAYRES[0] - margin * 2
        self.page_indicator = vectorio.Rectangle(
            pixel_shader=palette,
            width=track_width,
            height=1,
            x=margin,
            y=DISPLAYRES[1] - margin,
        )
        self.page_indicator.color_index = 1
        self.group.append(self.page_indicator)


        main_group.append(self.group)
            
    def update_parm(self, i, labeltext, newvalue):
        label = self.parmlabels[i]
        value = self.parmvalues[i]
        label.text = labeltext
        value.text = str(newvalue)

    def update_parm_value(self, i, newvalue):
        value = self.parmvalues[i]
        value.text = str(newvalue)

    def highlight_parm(self):
        highlightid = self.state.active_parm
        if self.highlighted is not None: 
            self.parmlabels[self.highlighted].clear_accent_ranges()
        thislabel = self.parmlabels[highlightid]
        thislabel.add_accent_to_substring(thislabel.text, 2, 3)
        self.highlighted = highlightid

    def clear_parm_highlights(self):
        for i in range(self.parmcount):
            self.parmlabels[i].clear_accent_ranges()

    def rebuild_parm_section(self):
        module = self.state.chain_modules[self.state.active_chain]
        parms = module.get_parms()
        self.parm_count = len(parms)
        self.pages = self.parm_count//PARMSPERPAGE
        self.activepage = self.state.active_parm//PARMSPERPAGE
        start = self.active_page*PARMSPERPAGE
        for i in range(PARMSPERPAGE):
            if i<self.parm_count:
                if parms[i+start]:
                    newtext = None
                    newtext = parms[i+start].label
                    if parms[i+start].display_value:
                        newtext = str(parms[i+start].display_value)
                    else: 
                        self.parmvalues[i].text = ""
                    if newtext != self.parmvalues[i].text:
                        self.parmlabels[i].text = newtext
            else:
                self.parmlabels[i].text = ""
                self.parmvalues[i].text = ""
        if self.state.active_parm != -1: self.highlight_parm()

    def set_page_indicator(self, page_index, page_count):
        track_width = DISPLAYRES[0]-self.margin*2
        indicator_width = track_width/page_count
        x_offset = indicator_width*page_index
        self.page_indicator.x = x_offset+self.margin
        self.page_indicator.width = indicator_width

        




