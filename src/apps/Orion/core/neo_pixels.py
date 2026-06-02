from rainbowio import colorwheel

KEYCOLORBASE = colorwheel(128)
KEYCOLORROOT = colorwheel(64)
KEYCOLORDRUMS = colorwheel(0)

KEYCOLORNAVNOTE = colorwheel(150)
KEYCOLORNAVPARMS = colorwheel(21)
KEYCOLORENTER = colorwheel(42)

KEYCOLORSTOP = colorwheel(0)
KEYCOLORSTART = colorwheel(85)
KEYCOLORCHAIN = 0xFFFFFF
KEYCOLORMODULESWAP = colorwheel(191)

KEYOFF = 0x000000
KEYWHITE = 0xFFFFFF

KEYBRIGHTNESSDEFAULT = 0.3
KEYBRIGHTNESSHELD = 1

NAV_NOTE_COLORS = (
    KEYCOLORSTOP, KEYCOLORSTART, KEYOFF,
    KEYCOLORMODULESWAP, KEYCOLORCHAIN, KEYOFF,
    KEYCOLORNAVPARMS, KEYCOLORNAVNOTE, KEYOFF,
    KEYCOLORNAVNOTE, KEYCOLORNAVNOTE, KEYCOLORNAVNOTE,
)

NAV_PARM_COLORS = (
    KEYCOLORSTOP, KEYCOLORSTART, KEYOFF,
    KEYCOLORMODULESWAP, KEYCOLORCHAIN, KEYOFF,
    KEYCOLORNAVNOTE, KEYCOLORNAVPARMS, KEYCOLORENTER,
    KEYCOLORNAVPARMS, KEYCOLORNAVPARMS, KEYCOLORNAVPARMS,
)


def combine_color_and_brightness(color, brightness):
    r = int(((color >> 16) & 0xFF) * brightness)
    g = int(((color >> 8) & 0xFF) * brightness)
    b = int((color & 0xFF) * brightness)
    return (r << 16) | (g << 8) | b


class NeoPixels:
    def __init__(self, macropad, state):
        self.pixels = macropad.pixels
        self.state = state
        self.brightness_multiplier = 1.0
        self.brightness_default = KEYBRIGHTNESSDEFAULT
        self.brightness_held = KEYBRIGHTNESSHELD
        self.pixels.fill(int(KEYCOLORBASE*self.brightness_default))
        self.pixel_colors = [KEYCOLORBASE]*12
        self.pixel_colors_temp = [KEYCOLORBASE]*12
        self.held_pixels = bytearray(12)

    def set_held_pixel(self, pressed_pad):
        if not self.held_pixels[pressed_pad]:
            self.held_pixels[pressed_pad] = 1
            self.paint_pixel(pressed_pad,brightness = self.brightness_held*self.brightness_multiplier)

    def release_held_pixel(self, released_pad):
        if self.held_pixels[released_pad]:
            self.held_pixels[released_pad] = 0
            self.paint_pixel(released_pad,brightness = self.brightness_default*self.brightness_multiplier)

    def paint_pixels(self):
        for idx in range(len(self.pixel_colors)):
            color = self.pixel_colors[idx]
            color = combine_color_and_brightness(color,self.brightness_default*self.brightness_multiplier)
            self.pixels[idx] = color

    def paint_pixel(self, pad, brightness=1.0):
        color = self.pixel_colors[pad]
        color = combine_color_and_brightness(color,brightness*self.brightness_multiplier)
        self.pixels[pad] = color

    def set_key_colors(self, key_color_list):
        """List of new key colors. Key color list needs to be 12 items long"""
        if len(key_color_list) == 12:
            for idx, key in enumerate(key_color_list):
                self.pixel_colors[idx] = key
            self.paint_pixels()
        else:
            raise RuntimeError("Array wrong length (should be 12 bytes)")

    def set_nav_state_colors(self, update = False):
        if self.state.nav_keys_state == 0: # NOTES
            color_list = NAV_NOTE_COLORS
        else: # NAV
            color_list = NAV_PARM_COLORS
        if not update:
            for idx in range(12):
                self.pixel_colors_temp[idx] = self.pixel_colors[idx]
        self.set_key_colors(color_list)

    def exit_nav_state_colors(self):
        self.set_key_colors(self.pixel_colors_temp)
