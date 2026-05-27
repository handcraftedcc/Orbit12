import adafruit_macropad as MacroPad
from .state import State
from rainbowio import colorwheel

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


def combine_color_and_brightness(color, brightness):
    r = int(((color >> 16) & 0xFF) * brightness)
    g = int(((color >> 8) & 0xFF) * brightness)
    b = int((color & 0xFF) * brightness)
    return (r << 16) | (g << 8) | b


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

    def paint_pixels(self):
        for idx in range(len(self.pixel_colors)):
            color = self.pixel_colors[idx]
            color = combine_color_and_brightness(color,self.brightness_default)
            self.pixels[idx] = color

    def paint_pixel(self, pad, brightness=1.0):
        color = self.pixel_colors[pad]
        color = combine_color_and_brightness(color,brightness)
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