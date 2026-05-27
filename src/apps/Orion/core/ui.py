#Handles all UI
from adafruit_macropad import MacroPad
import displayio
import terminalio
import vectorio
from adafruit_display_text.bitmap_label import Label
from .state import State, ChainModes
from .constants import PARMSPERPAGE

DISPLAYRES = (128,64)

### Tilegrid Helpers ###
FONTCHARS = ''' ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?;:\''()[]{}<>+-=*/\\_%&@#$^|~`'''
FONTBITMAP = displayio.OnDiskBitmap("apps/Orion/imgs/Font_6x6.bmp")
FONTBITMAP_DIMENSIONS = (6,6)

STATEINDICATORSBITMAP = displayio.OnDiskBitmap("apps/Orion/imgs/StateIndicators_7x5.bmp")
STATEINDICATORSBITMAP_DIMENSIONS = (7,5)

CHAINCURSORBITMAP = displayio.OnDiskBitmap("apps/Orion/imgs/ChainNav_12x5.1.bmp")
CHAINCURSORBITMAP_DIMENSIONS = (12,6)

def tile_for_char(char):
    index = FONTCHARS.find(char)
    if index < 0:
        return 0
    return index

def set_text(tile_grid, text, width):
    for i in range(width):
        if i < len(text):
            tile_grid[i] = tile_for_char(text[i])
        else:
            tile_grid[i] = 0

class UIManager:
    def __init__(self,macropad: MacroPad,state):
        self.macropad = macropad
        self.main_group = displayio.Group()
        self.state = state
        
        self.palette = displayio.Palette(4) 
        self.palette.make_transparent(0) # 0 = transparent
        self.palette[1] = 0xFFFFFF # 1 = normal text (white)
        self.palette[2] = 0x000000 # 2 = selected text (black)
        self.palette[3] = 0xFFFFFF # 3 = selected background (white)

        self.margin = 3
        self.parm_spacing = 2
        self.chain_parm_sep_spacing = 4

        self.chain = Chain(self.state, self.main_group)
        self.chain.group.hidden = True
        self.parameter_section = ParameterSection(self.state, self.main_group)
        #self.parameter_section.group.hidden = True
        self.module_selector = ModuleSelector(self.state, self.main_group)
        self.module_selector.group.hidden = True

        self.header_footer = HeaderFooter(self.state, self.main_group)

        self.screen = Screen(self.macropad,self.main_group)


class Screen:
    def __init__(self,macropad: MacroPad,main_group):
        self.display = macropad.display
        self.display.root_group = main_group
        macropad.display.refresh()

    def update(self):
        self.display.refresh()


class Section:
    def __init__(self,state: State):
        self.visible = True
        self.group = displayio.Group()
        self.state = state
        pass

class HeaderFooter(Section):
    def __init__(self, state: State, main_group):
        super().__init__(state)
        white_palette = displayio.Palette(1)
        white_palette[0] = 0xFFFFFF

        text_palette = displayio.Palette(2)
        text_palette[0] = 0xFFFFFF
        text_palette[1] = 0x000000


        self.header_bg = vectorio.Rectangle(
            pixel_shader=white_palette,
            width=128,
            height=11,
            x=0,
            y=0,
        )

        self.group.append(self.header_bg)

        self.header_chain_id = displayio.TileGrid(
            FONTBITMAP,
            pixel_shader=text_palette,
            width=1,          # number of visible tile cells
            height=1,
            tile_width=FONTBITMAP_DIMENSIONS[0],
            tile_height=FONTBITMAP_DIMENSIONS[1],
            x=4, y=3
        )

        self.group.append(self.header_chain_id)
        self.header_chain_id[0] = tile_for_char("I")

        self.header_chain_arrow = displayio.TileGrid(
            STATEINDICATORSBITMAP,
            pixel_shader=text_palette,
            width=1,  # number of visible tile cells
            height=1,
            tile_width=STATEINDICATORSBITMAP_DIMENSIONS[0],
            tile_height=STATEINDICATORSBITMAP_DIMENSIONS[1],
            x=9, y=3
        )

        self.group.append(self.header_chain_arrow)
        self.header_chain_arrow[0] = 0

        self.header_chain_module = displayio.TileGrid(
            FONTBITMAP,
            pixel_shader=text_palette,
            width=6,  # number of visible tile cells
            height=1,
            tile_width=FONTBITMAP_DIMENSIONS[0],
            tile_height=FONTBITMAP_DIMENSIONS[1],
            x=16, y=3
        )

        self.group.append(self.header_chain_module)
        set_text(self.header_chain_module,"INPUT",6)

        self.header_key_info = displayio.TileGrid(
            FONTBITMAP,
            pixel_shader=text_palette,
            width=5,  # number of visible tile cells
            height=1,
            tile_width=FONTBITMAP_DIMENSIONS[0],
            tile_height=FONTBITMAP_DIMENSIONS[1],
            x=80, y=3
        )

        self.group.append(self.header_key_info)
        set_text(self.header_key_info, "A#MIN", 5)

        self.header_state_icons = displayio.TileGrid(
            STATEINDICATORSBITMAP,
            pixel_shader=text_palette,
            width=2,  # number of visible tile cells
            height=1,
            tile_width=STATEINDICATORSBITMAP_DIMENSIONS[0],
            tile_height=STATEINDICATORSBITMAP_DIMENSIONS[1],
            x=112, y=3
        )

        self.group.append(self.header_state_icons)
        self.header_state_icons[0] = 1
        self.header_state_icons[1] = 2

        self.footer_bg = vectorio.Rectangle(
            pixel_shader=white_palette,
            width=128,
            height=11,
            x=0,
            y=53,
        )

        self.group.append(self.footer_bg)

        self.footer_help_text = displayio.TileGrid(
            FONTBITMAP,
            pixel_shader=text_palette,
            width=24,  # number of visible tile cells
            height=1,
            tile_width=FONTBITMAP_DIMENSIONS[0],
            tile_height=FONTBITMAP_DIMENSIONS[1],
            x=4, y=56
        )

        self.group.append(self.footer_help_text)
        set_text(self.footer_help_text, ":THIS IS HELP A TEXT", 24)

        main_group.append(self.group)

    def update_header_chain_id(self, new_chain_id):
        self.header_chain_id[0] = tile_for_char(new_chain_id)

    def update_header_module_name(self, new_module_name):
        set_text(self.header_chain_module, new_module_name, 6)

    def update_header_key_info(self, new_key_info):
        set_text(self.header_key_info, new_key_info, 5)

    def update_header_state_icons(self, tile, value):
        self.header_state_icons[tile] = value

    def update_footer_help_text(self, new_help_text):
        set_text(self.footer_help_text, new_help_text, 24)


class Chain(Section):
    def __init__(self,state,main_group):
        super().__init__(state)

        text_palette = displayio.Palette(2)
        text_palette[0] = 0x000000
        text_palette[1] = 0xFFFFFF
        self.group_chain = displayio.Group()

        self.chain = displayio.TileGrid(
            FONTBITMAP,
            pixel_shader=text_palette,
            width=10,  # number of visible tile cells
            height=1,
            tile_width=FONTBITMAP_DIMENSIONS[0],
            tile_height=FONTBITMAP_DIMENSIONS[1],
            x=0, y=0
        )

        self.group_chain.append(self.chain)
        set_text(self.chain, "IT123456O*", 10)
        self.group_chain.scale = 2
        self.group_chain.x = 4
        self.group_chain.y = 27
        self.group.append(self.group_chain)

        self.chain_cursor_top = displayio.TileGrid(
            CHAINCURSORBITMAP,
            pixel_shader=text_palette,
            width=10,  # number of visible tile cells
            height=1,
            tile_width=CHAINCURSORBITMAP_DIMENSIONS[0],
            tile_height=CHAINCURSORBITMAP_DIMENSIONS[1],
            x=3, y=17
        )

        self.group.append(self.chain_cursor_top)
        self.chain_cursor_top[0] = 2

        self.chain_cursor_bottom = displayio.TileGrid(
            CHAINCURSORBITMAP,
            pixel_shader=text_palette,
            width=10,  # number of visible tile cells
            height=1,
            tile_width=CHAINCURSORBITMAP_DIMENSIONS[0],
            tile_height=CHAINCURSORBITMAP_DIMENSIONS[1],
            x=3, y=41
        )

        self.group.append(self.chain_cursor_bottom)
        self.chain_cursor_bottom[0] = 3
        #self.rebuild_chain_section()

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
    def __init__(self,state,main_group):
        super().__init__(state)
        self.highlighted = None
        self.active = None
        self.pages = 1
        self.active_page = 0
        self.parm_count = 4
        self.parm_labels = []
        self.parm_values = []
        self.parm_groups = []
        self.pending_parm_value = None
        self.pending_parm_id = None

        black_palette = displayio.Palette(1)
        black_palette[0] = 0xFFFFFF

        text_palette = displayio.Palette(2)
        text_palette[0] = 0x000000
        text_palette[1] = 0xFFFFFF

        white_palette = displayio.Palette(1)
        white_palette[0] = 0xFFFFFF

        self.cursor = vectorio.Rectangle(
            pixel_shader=white_palette,
            width=2,
            height=10,
            x=0,
            y=15,
        )

        self.group.append(self.cursor)

        for i in range(PARMSPERPAGE):
            parm_group = displayio.Group()
            ypos = 15+8*i
            if i>0: ypos += 5
            labeltext = "LABL" + str(i)
            parm_label = displayio.TileGrid(
                FONTBITMAP,
                pixel_shader=text_palette,
                width=5,  # number of visible tile cells
                height=1,
                tile_width=FONTBITMAP_DIMENSIONS[0],
                tile_height=FONTBITMAP_DIMENSIONS[1],
                x=0, y=0,
            )
            set_text(parm_label , labeltext, 5)
            parm_group.append(parm_label)
            self.parm_labels.append(parm_label)

            parm_value = displayio.TileGrid(
                FONTBITMAP,
                pixel_shader=text_palette,
                width=5,  # number of visible tile cells
                height=1,
                tile_width=FONTBITMAP_DIMENSIONS[0],
                tile_height=FONTBITMAP_DIMENSIONS[1],
                x=91, y=0
            )
            set_text(parm_value, " 100%", 5)
            parm_group.append(parm_value)
            if i == 0:
                parm_group.scale = 2
                parm_value.x = 31
            parm_group.x = 4
            parm_group.y = ypos

            self.parm_groups.append(parm_group)
            self.group.append(parm_group)
            self.parm_values.append(parm_value)

        #self.rebuild_parm_section()
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

class ModuleSelector(Section):
    def __init__(self,state,main_group):
        super().__init__(state)

        text_palette = displayio.Palette(2)
        text_palette[0] = 0x000000
        text_palette[1] = 0xFFFFFF
        self.group = displayio.Group()

        self.chain = displayio.TileGrid(
            FONTBITMAP,
            pixel_shader=text_palette,
            width=10,  # number of visible tile cells
            height=1,
            tile_width=FONTBITMAP_DIMENSIONS[0],
            tile_height=FONTBITMAP_DIMENSIONS[1],
            x=0, y=0
        )

        self.group.append(self.chain)
        set_text(self.chain, "< MODULE >", 10)
        self.group.scale = 2
        self.group.x = 4
        self.group.y = 27

        #self.rebuild_chain_section()

        main_group.append(self.group)



        


