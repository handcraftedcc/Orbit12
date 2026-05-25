#Handles all UI
from rainbowio import colorwheel
from adafruit_macropad import MacroPad
import displayio
import terminalio
import vectorio
from adafruit_bitmap_font import bitmap_font
from adafruit_display_text.bitmap_label import Label
from .state import State, ChainModes
from .constants import PARMSPERPAGE

DISPLAYRES = (128,64)

COLORS = {
    "black": 0x000000,
    "white": 0xFFFFFF,
    "red":  colorwheel(0),
    "orange":  colorwheel(21),
    "yellow":  colorwheel(42),
    "lime":  colorwheel(64),
    "green":  colorwheel(85),
    "cyan":  colorwheel(128),
    "blue":  colorwheel(150),
    "purple":  colorwheel(191),
    "magenta":  colorwheel(213),
    "pink":  colorwheel(235),
}

KEYCOLORBASE = COLORS["cyan"]
KEYCOLORROOT = COLORS["lime"]
KEYCOLORDRUMS = COLORS["red"]

KEYCOLORNAVNOTE = COLORS["blue"]
KEYCOLORNAVPARMS = COLORS["orange"]
KEYCOLORENTER = COLORS["yellow"]

KEYCOLORSTOP = COLORS["red"]
KEYCOLORSTART = COLORS["green"]
KEYOFF = COLORS["black"]

KEYBRIGHTNESSDEFAULT = 0.3
KEYBRIGHTNESSHELD = 1
KEYBRIGHTNESSMULTIPLIER = 1

class UIManager:
    def __init__(self,macropad: MacroPad,state):
        self.macropad = macropad
        self.main_group = displayio.Group()
        self.state = state
        self.neo_pixels = NeoPixels(self.macropad, self.state)

        
        self.palette = displayio.Palette(4) 
        self.palette.make_transparent(0) # 0 = transparent
        self.palette[1] = 0xFFFFFF # 1 = normal text (white)
        self.palette[2] = 0x000000 # 2 = selected text (black)
        self.palette[3] = 0xFFFFFF # 3 = selected background (white)

        self.margin = 3
        self.parm_spacing = 1
        self.chain_parm_sep_spacing = 3

        self.font = bitmap_font.load_font("fonts/RolandLCD5x5.bdf")
        #self.font = bitmap_font.load_font("fonts/4x6.bdf")

        self.chain = Chain(self.state, self.palette, self.margin, self.main_group, self.font)
        self.parameter_section = ParameterSection(self.state, self.palette, self.margin, self.main_group, self.font)

        bitmap = displayio.OnDiskBitmap("apps/Orion/imgs/5x5 v4.bmp")
        tile_grid = displayio.TileGrid(bitmap, pixel_shader=bitmap.pixel_shader)

        group = displayio.Group()
        group.append(tile_grid)

        self.main_group.append(group)

        self.screen = Screen(self.macropad,self.main_group)


class Screen:
    def __init__(self,macropad: MacroPad,main_group):
        self.display = macropad.display
        self.display.root_group = main_group
        macropad.display.refresh()

    def update(self):
        self.display.refresh()

class NeoPixels:
    def __init__(self,macropad: MacroPad, state: State):
        self.pixels = macropad.pixels
        self.state = state
        self.brightness_default = KEYBRIGHTNESSDEFAULT*KEYBRIGHTNESSMULTIPLIER
        self.brightness_held = KEYBRIGHTNESSHELD*KEYBRIGHTNESSMULTIPLIER
        self.pixels.fill(int(KEYCOLORBASE*self.brightness_default))
        self.pixel_colors = [KEYCOLORBASE]*12
        self.pixel_colors_temp = [KEYCOLORBASE]*12
        self.held_pixels = bytearray(12)

    def set_held_pixel(self, pressed_pad):
        if not self.held_pixels[pressed_pad]:
            self.held_pixels[pressed_pad] = 1
            self.paint_pixel(pressed_pad,brightness = self.brightness_held)

    def release_held_pixel(self, released_pad):
        if self.held_pixels[released_pad]:
            self.held_pixels[released_pad] = 0
            self.paint_pixel(released_pad,brightness = self.brightness_default)

    def combine_color_and_brightness(self, color, brightness):
        r = int(((color >> 16) & 0xFF) * brightness)
        g = int(((color >> 8) & 0xFF) * brightness)
        b = int((color & 0xFF) * brightness)
        return (r << 16) | (g << 8) | b

    def paint_pixels(self):
        for idx in range(len(self.pixel_colors)):
            color = self.pixel_colors[idx]
            color = self.combine_color_and_brightness(color,self.brightness_default)
            self.pixels[idx] = color

    def paint_pixel(self, pad, brightness=1.0):
        color = self.pixel_colors[pad]
        color = self.combine_color_and_brightness(color,brightness)
        self.pixels[pad] = color

    def set_key_colors_simple(self, key_bytearray: bytearray):
        """key_bytearray needs to be 12 long. root keys are true, Others are false"""
        if len(key_bytearray) == 12:
            for idx, key in enumerate(key_bytearray):
                if key:
                    self.pixel_colors[idx] = KEYCOLORROOT
                else:
                    self.pixel_colors[idx] = KEYCOLORBASE
            self.paint_pixels()
        else:
            raise RuntimeError("Array wrong length (should be 12 bytes)")


    def set_key_colors(self, key_color_list : list):
        """List of new key colors. Key color list needs to be 12 items long"""
        if len(key_color_list) == 12:
            for idx, key in enumerate(key_color_list):
                self.pixel_colors[idx] = key
            self.paint_pixels()
        else:
            raise RuntimeError("Array wrong length (should be 12 bytes)")

    def set_nav_state_colors(self, update = False):
        if self.state.nav_keys_state == 0: # NOTES
            color_list = [KEYCOLORNAVPARMS, KEYCOLORNAVNOTE, KEYOFF,
                    KEYCOLORNAVNOTE, KEYCOLORNAVNOTE, KEYCOLORNAVNOTE,
                    KEYOFF, KEYOFF, KEYOFF,
                    KEYCOLORSTOP, KEYCOLORSTART, KEYOFF,
                    ]
        else: # NAV
            color_list = [KEYCOLORNAVNOTE, KEYCOLORNAVPARMS, KEYCOLORENTER,
                    KEYCOLORNAVPARMS, KEYCOLORNAVPARMS, KEYCOLORNAVPARMS,
                    KEYOFF, KEYOFF, KEYOFF,
                    KEYCOLORSTOP, KEYCOLORSTART, KEYOFF,
                    ]
        if not update:
            self.pixel_colors_temp = self.pixel_colors.copy()
        self.set_key_colors(color_list)

    def exit_nav_state_colors(self):
        self.set_key_colors(self.pixel_colors_temp)


class Section:
    def __init__(self,state: State):
        self.visible = True
        self.group = displayio.Group()
        self.state = state
        pass

class Chain(Section):
    def __init__(self,state,palette,margin,main_group, font):
        super().__init__(state)
        
        self.items = ["I", "T", "1", "2", "3", "4", "5", "6", "O"]
        self.font = font
        self.temp_items = self.items.copy()
        self.text = "-".join(self.items)
        self.chain_label = Label(
            self.font,
            text=self.text,
            color=0xFFFFFF,
            color_palette=palette,
        )
        self.chain_label.anchor_point = (0,0)
        self.chain_label.anchored_position = (margin,margin-3)
        self.group.append(self.chain_label)

        self.rebuild_chain_section()

        # divider line
        line = displayio.Bitmap(DISPLAYRES[0]-margin*2, 1, 2)  # width, height, colors
        line.fill(1)
        tile = displayio.TileGrid(line, pixel_shader=palette)
        tile.x = margin
        tile.y = margin + 5

        self.group.append(tile)

        main_group.append(self.group)

    def rebuild_chain_section(self):
        for idx, _ in enumerate(self.temp_items):
            if idx == self.state.active_chain:
                if self.state.active_chain_mode == ChainModes.SWAP:
                    self.temp_items[idx] = "<" + self.items[idx] + ">"
                else:
                    self.temp_items[idx] = "[" + self.items[idx] + "]"

            else:
                self.temp_items[idx] = self.items[idx]
        self.chain_label.text="-".join(self.temp_items)

    def highlight_chain(self):
        self.chain_label.clear_accent_ranges()
        self.chain_label.add_accent_range(0,len(self.chain_label.text), 2, 3)

    def clear_chain_highlights(self):
        self.chain_label.clear_accent_ranges()

class ParameterSection(Section):
    def __init__(self,state,palette,margin,main_group, font):
        super().__init__(state)
        self.palette = palette
        self.margin = margin
        self.font = font
        self.highlighted = None
        self.active = None
        self.pages = 1
        self.active_page = 0
        self.parm_count = 4
        self.parm_labels = []
        self.parm_values = []
        self.pending_parm_value = None
        self.pending_parm_id = None
        
        for i in range(PARMSPERPAGE):
            ypos = 12+8*i
            labeltext = "LABEL" + str(i)
            parm_label = Label(self.font, color_palette=palette, text=labeltext)
            parm_label.anchor_point = (0, 0)
            parm_label.anchored_position = (margin, ypos)
            self.parm_labels.append(parm_label)
            valuetext = str(5)
            parm_value = Label(self.font, color_palette=self.palette, text=valuetext)
            parm_value.anchor_point = (1, 0)
            parm_value.anchored_position = (DISPLAYRES[0]-margin, ypos)
            self.parm_values.append(parm_value)
            self.group.append(parm_label)
            self.group.append(parm_value)

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

        self.rebuild_parm_section()

        main_group.append(self.group)
            
    def update_parm(self, labeltext, new_value):
        parm_id = self.state.active_parm % PARMSPERPAGE
        label = self.parm_labels[parm_id]
        value = self.parm_values[parm_id]
        label.text = labeltext
        value.text = str(new_value)

    def queue_parm_value_update(self, new_value):
        self.pending_parm_value = new_value
        self.pending_parm_id = self.state.active_parm % PARMSPERPAGE

    def flush_parm_value_update(self):
        if self.pending_parm_value is None:
            return

        new_value = self.pending_parm_value
        parm_id = self.pending_parm_id
        self.pending_parm_value = None
        self.pending_parm_id = None
        self.update_parm_value(new_value, parm_id)

    def update_parm_value(self, new_value, parm_id=None):
        if parm_id is None:
            parm_id = self.state.active_parm % PARMSPERPAGE
        value = self.parm_values[parm_id]
        new_text = str(new_value)
        if value.text == new_text:
            return
        value.text = new_text
        if parm_id == self.highlighted:
            value.clear_accent_ranges()
            value.add_accent_range(0, len(value.text), 2, 3)

    def highlight_parm(self):
        highlight_id = self.state.active_parm % PARMSPERPAGE
        if self.highlighted is not None: 
            self.parm_labels[self.highlighted].clear_accent_ranges()
        this_label = self.parm_labels[highlight_id]
        this_label.add_accent_to_substring(this_label.text, 2, 3)
        self.highlighted = highlight_id

    def clear_parm_highlights(self):
        for i in range(PARMSPERPAGE):
            self.parm_labels[i].clear_accent_ranges()

    def highlight_parm_value(self):
        highlight_id = self.state.active_parm % PARMSPERPAGE

        if self.highlighted is not None:
            self.parm_values[self.highlighted].clear_accent_ranges()

        this_label = self.parm_values[highlight_id]
        this_label.clear_accent_ranges()
        this_label.add_accent_range(0, len(this_label.text), 2, 3)

        self.highlighted = highlight_id

    def clear_parm_value_highlight(self):
        highlight_id = self.state.active_parm % PARMSPERPAGE
        self.parm_values[highlight_id].clear_accent_ranges()

    def set_page_indicator(self):
        track_width = DISPLAYRES[0]-self.margin*2
        indicator_width = int(track_width/self.pages)
        x_offset = indicator_width*self.active_page
        self.page_indicator.x = x_offset+self.margin
        self.page_indicator.width = indicator_width

    def rebuild_parm_section(self):
        module = self.state.chain_modules[self.state.active_chain]
        parms = module.get_parms()
        self.parm_count = len(parms)
        self.pages = (self.parm_count + PARMSPERPAGE -1 )//PARMSPERPAGE
        self.active_page = max(0,self.state.active_parm) // PARMSPERPAGE
        start = self.active_page*PARMSPERPAGE
        for i in range(PARMSPERPAGE):
            parm_index = start + i

            if parm_index < self.parm_count:
                parm = parms[parm_index]
                self.parm_labels[i].text = parm.label
                self.parm_values[i].text = str(parm.display_value)
            else:
                self.parm_labels[i].text = ""
                self.parm_values[i].text = ""
        if self.state.active_parm != -1: self.highlight_parm()

        self.set_page_indicator()



        


