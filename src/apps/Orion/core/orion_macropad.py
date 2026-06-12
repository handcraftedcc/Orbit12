import board
import digitalio
import keypad
import neopixel
import rotaryio
import usb_midi
import adafruit_midi
from adafruit_debouncer import Debouncer
from adafruit_midi.note_on import NoteOn
from adafruit_midi.note_off import NoteOff


_DISPLAY_SLEEP_COMMAND = 0xAE
_DISPLAY_WAKE_COMMAND = 0xAF


### HARDWARE WRAPPER ###

class OrionMacroPad:
    def __init__(self, rotation=0, midi_in_channel=1, midi_out_channel=1):
        self._pixels = neopixel.NeoPixel(board.NEOPIXEL, 12)

        self._encoder = rotaryio.IncrementalEncoder(board.ROTA, board.ROTB)
        self._encoder_switch = digitalio.DigitalInOut(board.BUTTON)
        self._encoder_switch.switch_to_input(pull=digitalio.Pull.UP)
        self._debounced_switch = Debouncer(self._encoder_switch)

        self.display = board.DISPLAY
        #self.display.bus.send(_DISPLAY_WAKE_COMMAND, b"")
        self._display_sleep = False

        self._rotation = 0
        self._key_pins = None
        self._keys = None
        self.rotate(rotation)

        try:
            self._midi = adafruit_midi.MIDI(
                midi_in=usb_midi.ports[0],
                in_channel=midi_in_channel - 1,
                midi_out=usb_midi.ports[1],
                out_channel=midi_out_channel - 1,
            )
        except IndexError:
            self._midi = None

    def rotate(self, rotation):
        """
        Configure physical key order and display rotation.
        """
        if rotation != 0:
            raise ValueError("OrionMacroPad only supports rotation 0.")

        self._rotation = rotation
        self._key_pins = (
            board.KEY1, board.KEY2, board.KEY3,
            board.KEY4, board.KEY5, board.KEY6,
            board.KEY7, board.KEY8, board.KEY9,
            board.KEY10, board.KEY11, board.KEY12,
        )

        if self._keys is not None:
            self._keys.deinit()
        self._keys = keypad.Keys(self._key_pins, value_when_pressed=False, pull=True)
        self.display.rotation = rotation

    @property
    def rotation(self):
        return self._rotation

    @rotation.setter
    def rotation(self, rotation):
        self.rotate(rotation)

    @property
    def pixels(self):
        return self._pixels

    @property
    def keys(self):
        return self._keys

    @property
    def encoder(self):
        return self._encoder.position * -1

    @property
    def encoder_switch(self):
        return not self._encoder_switch.value

    @property
    def encoder_switch_debounced(self):
        self._debounced_switch.pressed = self._debounced_switch.fell
        self._debounced_switch.released = self._debounced_switch.rose
        return self._debounced_switch

    @property
    def display_sleep(self):
        return self._display_sleep

    @display_sleep.setter
    def display_sleep(self, sleep):
        if self._display_sleep == sleep:
            return
        if sleep:
            command = _DISPLAY_SLEEP_COMMAND
        else:
            command = _DISPLAY_WAKE_COMMAND
        self.display.bus.send(command, b"")
        self._display_sleep = sleep

    @property
    def midi(self):
        return self._midi

    @staticmethod
    def NoteOn(note, velocity=127, channel=None):
        return NoteOn(note=note, velocity=velocity, channel=channel)

    @staticmethod
    def NoteOff(note, velocity=0, channel=None):
        return NoteOff(note=note, velocity=velocity, channel=channel)
