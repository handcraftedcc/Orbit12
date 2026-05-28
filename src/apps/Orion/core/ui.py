#Handles all UI
from .music import SCALENAMES, NOTES
from adafruit_macropad import MacroPad
import displayio
import terminalio
import vectorio
from adafruit_display_text.bitmap_label import Label
from .state import State, ChainModes, UISection, ChainElements
from .constants import PARMSPERPAGE

from ..modules import _registry as ModuleRegistry
from ..inputmodules import _registry as InputModuleRegistry

DISPLAYRES = (128,64)

### Tilegrid Helpers ###
FONTCHARS = ''' ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?;:\''()[]{}<>+-=*/\\_%&@#$^|~`'''
FONTBITMAP = displayio.OnDiskBitmap("apps/Orion/imgs/Font_6x6.bmp")
FONTBITMAP_DIMENSIONS = (6,6)

STATEINDICATORSBITMAP = displayio.OnDiskBitmap("apps/Orion/imgs/StateIndicators_7x5.bmp")
STATEINDICATORSBITMAP_DIMENSIONS = (7,5)

CHAINCURSORBITMAP = displayio.OnDiskBitmap("apps/Orion/imgs/ChainNav_12x5.1.bmp")
CHAINCURSORBITMAP_DIMENSIONS = (12,6)

CHAINITEMS = "IT123456O*"

MODULECHARS = 6
KEYCHARS = 6
PARMLABELCHARS = 6
PARMVALUECHARS = 5
HELPERCHARS = 24
CHAINCHARS = 10
MODULESELECTORCHARS = 10



def tile_for_char(char):
    index = FONTCHARS.find(char)
    if index < 0:
        return 0
    return index

def set_text(tile_grid, text, align_right=False, write_range=(None,None)):
    text = str(text)

    start, end = write_range
    if start is None:
        start = 0
    if end is None:
        end = tile_grid.width

    start = max(0, min(start, tile_grid.width))
    end = max(start, min(end, tile_grid.width))
    write_width = end - start
    if write_width <= 0:
        return

    if align_right:
        offset = write_width - len(text)
        if offset < 0:
            offset = 0

        for i in range(write_width):
            text_index = i - offset
            tile_index = start + i
            if 0 <= text_index < len(text):
                tile_grid[tile_index] = tile_for_char(text[text_index])
            else:
                tile_grid[tile_index] = 0
    else:
        for i in range(write_width):
            tile_index = start + i
            if i < len(text):
                tile_grid[tile_index] = tile_for_char(text[i])
            else:
                tile_grid[tile_index] = 0



class UIManager:
    def __init__(self,macropad: MacroPad, state, transport):
        self.macropad = macropad
        self.main_group = displayio.Group()
        self.state = state
        self.transport = transport
        
        self.palette = displayio.Palette(4) 
        self.palette.make_transparent(0) # 0 = transparent
        self.palette[1] = 0xFFFFFF # 1 = normal text (white)
        self.palette[2] = 0x000000 # 2 = selected text (black)
        self.palette[3] = 0xFFFFFF # 3 = selected background (white)

        self.margin = 3
        self.parm_spacing = 2
        self.chain_parm_sep_spacing = 4

        self.chain = Chain(self.state, self.main_group)
        #self.chain.group.hidden = True
        self.header_footer = HeaderFooter(self.state, self.transport, self.main_group)
        self.parameter_section = ParameterSection(self.state, self.main_group)
        self.parameter_section.group.hidden = True
        self.module_selector = ModuleSelector(self.state, self.main_group)
        self.module_selector.group.hidden = True

        self.screen = Screen(self.macropad,self.main_group)

    def switch_section(self):
        active_section = self.state.active_ui_section
        if active_section == UISection.CHAIN:
            self.chain.group.hidden = False
            self.parameter_section.group.hidden = True
            self.module_selector.group.hidden = True
        elif active_section == UISection.PARMSELECTION or active_section == UISection.PARMEDIT:
            self.chain.group.hidden = True
            self.parameter_section.group.hidden = False
            self.module_selector.group.hidden = True
        elif active_section == UISection.MODULESELECTION:
            self.chain.group.hidden = True
            self.parameter_section.group.hidden = True
            self.module_selector.group.hidden = False
        self.screen.update()


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
    def __init__(self, state: State, transport, main_group):
        super().__init__(state)

        self.transport = transport
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
            width=MODULECHARS,  # number of visible tile cells
            height=1,
            tile_width=FONTBITMAP_DIMENSIONS[0],
            tile_height=FONTBITMAP_DIMENSIONS[1],
            x=16, y=3
        )

        self.group.append(self.header_chain_module)
        set_text(self.header_chain_module,"INPUT")

        self.header_key_info = displayio.TileGrid(
            FONTBITMAP,
            pixel_shader=text_palette,
            width=KEYCHARS,  # number of visible tile cells
            height=1,
            tile_width=FONTBITMAP_DIMENSIONS[0],
            tile_height=FONTBITMAP_DIMENSIONS[1],
            x=75, y=3
        )

        self.group.append(self.header_key_info)
        set_text(self.header_key_info, "A#MIN3", align_right=True)

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
            width=HELPERCHARS,  # number of visible tile cells
            height=1,
            tile_width=FONTBITMAP_DIMENSIONS[0],
            tile_height=FONTBITMAP_DIMENSIONS[1],
            x=4, y=56
        )

        self.group.append(self.footer_help_text)
        set_text(self.footer_help_text, ":THIS IS HELP A TEXT")

        self.update_header_key_info()
        self.update_header_chain_id()
        self.update_header_module_label()

        main_group.append(self.group)

    def update_header_chain_id(self):
        letter = CHAINITEMS[self.state.active_chain]
        self.header_chain_id[0] = tile_for_char(letter)

    def update_header_module_label(self):
        module_label = self.state.chain_modules[self.state.active_chain].label
        set_text(self.header_chain_module, module_label)

    def update_header_key_info(self, alt_text = None):
        new_text = None
        if not alt_text:
            key = NOTES[self.state.key]
            scale = SCALENAMES[self.state.scale]
            octave = str(self.state.octave)
            new_text = key+octave+scale
        else:
            new_text = alt_text
        set_text(self.header_key_info, new_text, align_right=True)

    def update_header_state_icons(self):
        if self.transport.running:
            self.header_state_icons[0] = 0
        else:
            self.header_state_icons[0] = 1
        if self.state.nav_keys_state == 0:
            self.header_state_icons[1] = 2
        else:
            self.header_state_icons[1] = 3

    def update_footer_help_text(self, new_help_text):
        set_text(self.footer_help_text, new_help_text)


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
            width=CHAINCHARS,  # number of visible tile cells
            height=1,
            tile_width=FONTBITMAP_DIMENSIONS[0],
            tile_height=FONTBITMAP_DIMENSIONS[1],
            x=0, y=0
        )

        self.group_chain.append(self.chain)
        set_text(self.chain, CHAINITEMS)
        self.group_chain.scale = 2
        self.group_chain.x = 4
        self.group_chain.y = 27
        self.group.append(self.group_chain)

        self.chain_cursor_top = displayio.TileGrid(
            CHAINCURSORBITMAP,
            pixel_shader=text_palette,
            width=CHAINCHARS,  # number of visible tile cells
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
            width=CHAINCHARS,  # number of visible tile cells
            height=1,
            tile_width=CHAINCURSORBITMAP_DIMENSIONS[0],
            tile_height=CHAINCURSORBITMAP_DIMENSIONS[1],
            x=3, y=41
        )

        self.group.append(self.chain_cursor_bottom)
        self.chain_cursor_bottom[0] = 3
        #self.rebuild_chain_section()

        main_group.append(self.group)

    def set_cursor(self, position):
        """Type 0: Normal Selection, Type 1: Swap"""
        for i in range(self.chain_cursor_top.width):
            if i != position:
                if self.chain_cursor_top[i] != 0:
                    self.chain_cursor_top[i] = 0
                if self.chain_cursor_bottom[i] != 0:
                    self.chain_cursor_bottom[i] = 0
        if self.state.active_chain_mode == ChainModes.SELECT:
            if self.chain_cursor_top[position] != 2:
                self.chain_cursor_top[position] = 2
            if self.chain_cursor_bottom[position] != 3:
                self.chain_cursor_bottom[position] = 3
        if self.state.active_chain_mode == ChainModes.SWAP:
            if self.chain_cursor_top[position] != 1:
                self.chain_cursor_top[position] = 1
            if self.chain_cursor_bottom[position] != 1:
                self.chain_cursor_bottom[position] = 1

    def rebuild_chain_section(self):
        #TODO: Remove references
        self.set_cursor(self.state.active_chain)

    def highlight_chain(self):
        #TODO: Remove references
        pass

    def clear_chain_highlights(self):
        #TODO: Remove references
        pass

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
        self.last_active_parm = 0
        self.scroll_offset = 0

        self.black_palette = displayio.Palette(1)
        self.black_palette[0] = 0x000000

        text_palette = displayio.Palette(2)
        text_palette[0] = 0x000000
        text_palette[1] = 0xFFFFFF
        text_palette.make_transparent(0)

        self.white_palette = displayio.Palette(1)
        self.white_palette[0] = 0xFFFFFF

        self.cursor = vectorio.Rectangle(
            pixel_shader=self.white_palette,
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
                width=PARMLABELCHARS,  # number of visible tile cells
                height=1,
                tile_width=FONTBITMAP_DIMENSIONS[0],
                tile_height=FONTBITMAP_DIMENSIONS[1],
                x=0, y=0,
            )
            set_text(parm_label , labeltext)
            parm_group.append(parm_label)
            self.parm_labels.append(parm_label)

            parm_value = displayio.TileGrid(
                FONTBITMAP,
                pixel_shader=text_palette,
                width=PARMVALUECHARS,  # number of visible tile cells
                height=1,
                tile_width=FONTBITMAP_DIMENSIONS[0],
                tile_height=FONTBITMAP_DIMENSIONS[1],
                x=91, y=0
            )
            set_text(parm_value, " 100%", align_right=True)
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

    def queue_parm_value_update(self, new_value):
        self.pending_parm_value = new_value
        self.pending_parm_id = self.active_visible_row()

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
            parm_id = self.active_visible_row()
        if parm_id is None:
            return
        set_text(self.parm_values[parm_id], new_value, align_right=True)

    def active_visible_row(self):
        row = self.state.active_parm - self.scroll_offset
        if row < 0 or row >= PARMSPERPAGE:
            return None
        return row

    def cursor_change_state(self):
        """Use to change cursor state between parm selection and edit"""
        highlight_row = self.state.active_parm - self.scroll_offset

        if self.state.active_parm < -2: return
        elif self.state.active_parm == -2: #Chain Selection
            self.cursor.x = 0
            self.cursor.height = 5
            self.cursor.y = 3
            self.cursor.pixel_shader = self.black_palette
        elif self.state.active_parm == -1: #Module Selection
            label_length = self.state.chain_modules[self.state.active_chain].label_length
            self.cursor.x = 54 - (6-label_length)*6
            self.cursor.height = 5
            self.cursor.y = 3
            self.cursor.pixel_shader = self.black_palette
        else:
            self.cursor.height = 10
            self.cursor.y = 15 + highlight_row * 8
            self.cursor.pixel_shader = self.white_palette
            if self.state.active_ui_section == UISection.PARMEDIT:
                self.cursor.x = DISPLAYRES[0]-2
            else:
                self.cursor.x = 0

    def rebuild_parm_section(self):
        module = self.state.chain_modules[self.state.active_chain]
        parms = module.get_parms()
        self.parm_count = len(parms)

        if self.parm_count == 0:
            for i in range(PARMSPERPAGE):
                set_text(self.parm_labels[i], " ")
                set_text(self.parm_values[i], " ")
            return

        active_parm = self.state.active_parm
        if active_parm < 0:
            active_parm = 0
        elif active_parm >= self.parm_count:
            active_parm = self.parm_count - 1

        if active_parm > self.last_active_parm:
            direction = 1
        elif active_parm < self.last_active_parm:
            direction = -1
        else:
            direction = 1

        visible_rows = PARMSPERPAGE
        if direction > 0:
            preferred_row = visible_rows - 2
        else:
            preferred_row = 1

        max_scroll = max(0, self.parm_count - visible_rows)

        self.scroll_offset = active_parm - preferred_row
        if self.scroll_offset < 0:
            self.scroll_offset = 0
        elif self.scroll_offset > max_scroll:
            self.scroll_offset = max_scroll

        highlight_row = active_parm - self.scroll_offset

        for i in range(visible_rows):
            parm_index = self.scroll_offset + i

            if parm_index < self.parm_count:
                parm = parms[parm_index]
                set_text(self.parm_labels[i], parm.label)
                set_text(self.parm_values[i], parm.display_value, align_right=True)
            else:
                set_text(self.parm_labels[i], " ")
                set_text(self.parm_values[i], " ")

            if i == highlight_row: #Highlight parm
                self.parm_groups[i].scale = 2
                self.parm_values[i].x = 31
            else: #Reset others
                self.parm_groups[i].scale = 1
                self.parm_values[i].x = 91

            ypos = 15 + 8 * i
            if i > highlight_row: ypos += 5 #Offset rows under highlighted row
            self.parm_groups[i].y = ypos

        self.cursor_change_state()

        self.last_active_parm = active_parm



class ModuleSelector(Section):
    def __init__(self,state,main_group):
        super().__init__(state)

        text_palette = displayio.Palette(2)
        text_palette[0] = 0x000000
        text_palette[1] = 0xFFFFFF
        self.group = displayio.Group()

        self.module_selector = displayio.TileGrid(
            FONTBITMAP,
            pixel_shader=text_palette,
            width=10,  # number of visible tile cells
            height=1,
            tile_width=FONTBITMAP_DIMENSIONS[0],
            tile_height=FONTBITMAP_DIMENSIONS[1],
            x=0, y=0
        )

        self.group.append(self.module_selector)
        set_text(self.module_selector, "< EMPTY  >")
        self.group.scale = 2
        self.group.x = 4
        self.group.y = 27

        main_group.append(self.group)

    def update_module_name(self):
        if self.state.active_chain == ChainElements.IN:
            registry = InputModuleRegistry
        else:
            registry = ModuleRegistry
        new_label = registry.AVAILABLE_MODULE_LABELS[self.state.module_selector_active_module]
        set_text(self.module_selector, new_label, write_range=(2,9))



        
