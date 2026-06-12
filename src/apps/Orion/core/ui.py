'''
Display layout and screen update helpers for Orion.
'''

from .music import SCALENAMES, NOTES
import displayio
import vectorio
from .state import ChainModes, UISection, ChainElements
from .constants import PARMSPERPAGE

from ..modules import _registry as ModuleRegistry
from ..inputmodules import _registry as InputModuleRegistry

### DISPLAY ASSETS ###

DISPLAYRES = (128,64)

FONTCHARS = ''' ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?;:\''()[]{}<>+-=*/\\_%&@#$^|~`'''
FONTBITMAP = displayio.OnDiskBitmap("apps/Orion/imgs/Font_6x6.bmp")
FONTBITMAP_DIMENSIONS = (6,6)

STATEINDICATORSBITMAP = displayio.OnDiskBitmap("apps/Orion/imgs/StateIndicators_7x5.bmp")
STATEINDICATORSBITMAP_DIMENSIONS = (7,5)

CHAINCURSORBITMAP = displayio.OnDiskBitmap("apps/Orion/imgs/ChainNav_12x5.1.bmp")
CHAINCURSORBITMAP_DIMENSIONS = (12,6)

CHAINPREVIEWBITMAP = displayio.OnDiskBitmap("apps/Orion/imgs/ChainPreview_3x3.bmp")
CHAINPREVIEWBITMAP_DIMENSIONS = (3,3)

CHAINITEMS = "IT123456O*"

MODULECHARS = 6
KEYCHARS = 6
PARMLABELCHARS = 6
PARMVALUECHARS = 5
HELPERCHARS = 17
CHAINCHARS = 10
MODULESELECTORCHARS = 10


### TEXT HELPERS ###

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
                tile = tile_for_char(text[text_index])
            else:
                tile = 0
            if tile_grid[tile_index] != tile:
                tile_grid[tile_index] = tile
    else:
        for i in range(write_width):
            tile_index = start + i
            if i < len(text):
                tile = tile_for_char(text[i])
            else:
                tile = 0
            if tile_grid[tile_index] != tile:
                tile_grid[tile_index] = tile


### UI MANAGER ###

class UIManager:
    def __init__(self,macropad, state, transport, defer_refresh=False):
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

        self.screen = Screen(self.macropad,self.main_group, defer_refresh)

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


### SCREEN ###

class Screen:
    def __init__(self,macropad,main_group, defer_refresh=False):
        self.display = macropad.display
        self.display.root_group = main_group
        if not defer_refresh:
            self.display.refresh()

    def update(self):
        self.display.refresh()


### SECTIONS ###

class Section:
    def __init__(self,state):
        self.visible = True
        self.group = displayio.Group()
        self.state = state
        pass


## Header Footer ##

class HeaderFooter(Section):
    def __init__(self, state, transport, main_group):
        super().__init__(state)

        self.transport = transport
        self.key_info_custom_text = None
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

        self.header_chain_preview = displayio.TileGrid(
            CHAINPREVIEWBITMAP,
            pixel_shader=text_palette,
            width=5,  # number of visible tile cells
            height=2,
            tile_width=CHAINPREVIEWBITMAP_DIMENSIONS[0],
            tile_height=CHAINPREVIEWBITMAP_DIMENSIONS[1],
            x=10, y=3
        )

        self.group.append(self.header_chain_preview)
        for i in range(1,10):
            self.header_chain_preview[i] = 1

        self.header_chain_module = displayio.TileGrid(
            FONTBITMAP,
            pixel_shader=text_palette,
            width=MODULECHARS,  # number of visible tile cells
            height=1,
            tile_width=FONTBITMAP_DIMENSIONS[0],
            tile_height=FONTBITMAP_DIMENSIONS[1],
            x=26, y=3
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
            x=89, y=3
        )

        self.group.append(self.header_key_info)
        set_text(self.header_key_info, "A#MIN3", align_right=True)

        self.footer_bg = vectorio.Rectangle(
            pixel_shader=white_palette,
            width=128,
            height=11,
            x=0,
            y=53,
        )

        self.group.append(self.footer_bg)

        self.footer_scene_info = displayio.TileGrid(
            FONTBITMAP,
            pixel_shader=text_palette,
            width=HELPERCHARS,  # number of visible tile cells
            height=1,
            tile_width=FONTBITMAP_DIMENSIONS[0],
            tile_height=FONTBITMAP_DIMENSIONS[1],
            x=4, y=56
        )

        self.group.append(self.footer_scene_info)
        set_text(self.footer_scene_info, "SCN 01")

        self.footer_state_icons = displayio.TileGrid(
            STATEINDICATORSBITMAP,
            pixel_shader=text_palette,
            width=2,  # number of visible tile cells
            height=1,
            tile_width=STATEINDICATORSBITMAP_DIMENSIONS[0],
            tile_height=STATEINDICATORSBITMAP_DIMENSIONS[1],
            x=112, y=56
        )

        self.group.append(self.footer_state_icons)
        self.footer_state_icons[0] = 1
        self.footer_state_icons[1] = 3

        self.update_header_key_info()
        self.update_header_chain_id()
        self.update_header_module_label()
        self.update_footer_state_icons()
        self.update_header_chain_preview()
        self.update_footer_scene_info()

        main_group.append(self.group)

    def update_header_chain_id(self):
        letter = CHAINITEMS[self.state.active_chain]
        self.header_chain_id[0] = tile_for_char(letter)

    def update_header_module_label(self):
        module = self.state.chain_modules[self.state.active_chain]
        module_label = module.label if module is not None else "EMPTY"
        set_text(self.header_chain_module, module_label)

    def update_header_key_info(self):
        if not self.state.key_custom_text:
            key = NOTES[self.state.key]
            scale = SCALENAMES[self.state.scale]
            octave = str(self.state.octave)
            new_text = key+octave+scale
        else:
            new_text = self.state.key_custom_text
        set_text(self.header_key_info, new_text, align_right=True)

    def update_footer_state_icons(self):
        if self.state.transport_mode == 1:
            self.footer_state_icons[0] = 2
        elif self.transport.running:
            self.footer_state_icons[0] = 0
        else:
            self.footer_state_icons[0] = 1
        if self.state.nav_keys_state == 0:
            self.footer_state_icons[1] = 3
        else:
            self.footer_state_icons[1] = 4

    def update_footer_scene_info(self):
        set_text(self.footer_scene_info, "SCN " + "{:02d}".format(self.state.active_scene + 1))

    def update_header_chain_preview(self):
        for i in range(10):
            if i == self.state.active_chain:
                self.header_chain_preview[i]=3
                continue
            module = self.state.chain_modules[i]
            if module is None or module.name == "empty":
                self.header_chain_preview[i] = 1
            else:
                self.header_chain_preview[i] = 2


## Chain View ##

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
        self.set_cursor(self.state.active_chain)

        main_group.append(self.group)

    def set_cursor(self, position):
        """Type 0: Normal Selection, Type 1: Swap"""
        for i in range(self.chain_cursor_top.width):
            if i != position:
                if self.chain_cursor_top[i] != 0:
                    self.chain_cursor_top[i] = 0
                module = self.state.chain_modules[i]
                if module is None or module.name == "empty":
                    if self.chain_cursor_bottom[i] != 1:
                        self.chain_cursor_bottom[i] = 1
                else:
                    if self.chain_cursor_bottom[i] != 0:
                        self.chain_cursor_bottom[i] = 0
        if self.state.active_chain_mode == ChainModes.SELECT:
            if self.chain_cursor_top[position] != 3:
                self.chain_cursor_top[position] = 3
            if self.chain_cursor_bottom[position] != 4:
                self.chain_cursor_bottom[position] = 4
        if self.state.active_chain_mode == ChainModes.SWAP:
            if self.chain_cursor_top[position] != 2:
                self.chain_cursor_top[position] = 2
            if self.chain_cursor_bottom[position] != 2:
                self.chain_cursor_bottom[position] = 2

    def rebuild_chain_section(self):
        #TODO: Remove references
        self.set_cursor(self.state.active_chain)


## Parameter View ##

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

    def get_active_parm_and_scroll(self, parm_count):
        """
        Keep the active parm near the scroll direction's preferred row.
        """
        active_parm = self.state.active_parm
        if active_parm < 0:
            active_parm = 0
        elif active_parm >= parm_count:
            active_parm = parm_count - 1

        if active_parm > self.last_active_parm:
            direction = 1
        elif active_parm < self.last_active_parm:
            direction = -1
        else:
            direction = 1

        if direction > 0:
            preferred_row = PARMSPERPAGE - 2
        else:
            preferred_row = 1

        max_scroll = max(0, parm_count - PARMSPERPAGE)
        scroll_offset = active_parm - preferred_row
        if scroll_offset < 0:
            scroll_offset = 0
        elif scroll_offset > max_scroll:
            scroll_offset = max_scroll

        return active_parm, scroll_offset

    def update_parm_row_layout(self, highlight_row):
        # Enlarge the highlighted row and make following rows flow around it.
        for i in range(PARMSPERPAGE):
            if i == highlight_row:
                if self.parm_groups[i].scale != 2:
                    self.parm_groups[i].scale = 2
                if self.parm_values[i].x != 31:
                    self.parm_values[i].x = 31
            else:
                if self.parm_groups[i].scale != 1:
                    self.parm_groups[i].scale = 1
                if self.parm_values[i].x != 91:
                    self.parm_values[i].x = 91

            ypos = 15 + 8 * i
            if i > highlight_row:
                ypos += 5
            if self.parm_groups[i].y != ypos:
                self.parm_groups[i].y = ypos

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
            module = self.state.chain_modules[self.state.active_chain]
            label_length = module.label_length if module is not None else 6
            self.cursor.x = 64 - (6-label_length)*6
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

    def update_parm_selection(self):
        module = self.state.chain_modules[self.state.active_chain]
        if module is None:
            self.rebuild_parm_section()
            return
        parm_count = len(module.get_parms())
        if parm_count == 0:
            self.rebuild_parm_section()
            return

        active_parm, scroll_offset = self.get_active_parm_and_scroll(parm_count)
        if parm_count != self.parm_count or scroll_offset != self.scroll_offset:
            self.rebuild_parm_section()
            return

        highlight_row = active_parm - self.scroll_offset
        self.update_parm_row_layout(highlight_row)
        self.cursor_change_state()
        self.last_active_parm = active_parm

    def rebuild_parm_section(self):
        module = self.state.chain_modules[self.state.active_chain]
        if module is None:
            self.parm_count = 0
            for i in range(PARMSPERPAGE):
                set_text(self.parm_labels[i], " ")
                set_text(self.parm_values[i], " ")
            return

        parms = module.get_parms()
        self.parm_count = len(parms)

        if self.parm_count == 0:
            for i in range(PARMSPERPAGE):
                set_text(self.parm_labels[i], " ")
                set_text(self.parm_values[i], " ")
            return

        active_parm, scroll_offset = self.get_active_parm_and_scroll(self.parm_count)
        max_scroll = max(0, self.parm_count - PARMSPERPAGE)
        if (active_parm == self.last_active_parm and self.scroll_offset <= max_scroll and
                self.scroll_offset <= active_parm < self.scroll_offset + PARMSPERPAGE):
            scroll_offset = self.scroll_offset
        self.scroll_offset = scroll_offset
        visible_rows = PARMSPERPAGE

        highlight_row = active_parm - self.scroll_offset

        # Map visible rows to the scrolled parm window.
        for i in range(visible_rows):
            parm_index = self.scroll_offset + i

            if parm_index < self.parm_count:
                parm = parms[parm_index]
                set_text(self.parm_labels[i], parm.label)
                set_text(self.parm_values[i], parm.display_value, align_right=True)
            else:
                set_text(self.parm_labels[i], " ")
                set_text(self.parm_values[i], " ")

        self.update_parm_row_layout(highlight_row)

        self.cursor_change_state()

        self.last_active_parm = active_parm


## Module Selector ##

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



        
