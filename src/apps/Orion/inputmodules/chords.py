from ..core.module import Module
from ..core import parms as Parms
from ..core._modules._input import Input
from ..core import ui
import rainbowio

from ..core import music as Music
from ..core._modules._input import PADMAP

'''
Idea:
Top 6 buttons are modifiers:
- Inversion
- 7th
- Sus
- Add9
- Bass
- Spread

Bottom 6 buttons are root notes.
'''

# TODO: Update key color for chords
class ModifierMap: #What keys do what -> Might turn this into parameters at some point
    SEV = 0
    ADD9 = 3
    SUS = 1
    INV = 4
    POWER = 2
    BORROW = 5

class BassModes:
    NoBass = 0
    Root = 1
    Second = 2
    Lowest = 3
    Highest = 4

class Chords(Input):
    name = "chords"
    label = "Chords"
    def __init__(self, module_helper, slot_id):
        parms = []
        super().__init__(module_helper, slot_id, include_musical_parms=True)

        # Bass Mode
        bass_modes = ("None", "Root", "Second", "Lowest", "Highest")
        self.bass_mode = 0
        bass_mode_parm = Parms.Parm(name="bass", label="Bass", parm_type=Parms.EnumParmType, default=0,
                                      options=bass_modes,
                                      edit_callback_function=self.set_bass_mode)
        self.parms.append(bass_mode_parm)

        # Spread Mode
        spread_modes = ("Tight", "Medium", "Wide")
        self.spread_mode = 0
        spread_mode_parm = Parms.Parm(name="spread", label="Spread", parm_type=Parms.EnumParmType, default=0,
                                      options=spread_modes,
                                      edit_callback_function=self.set_spread_mode)
        self.parms.append(spread_mode_parm)

        # Borrow Scale
        borrow_scale_options = ["AUTO"]
        borrow_scale_options.extend(Music.SCALENAMES[1:])
        self.borrow_scale = 0
        borrow_scale_parm = Parms.Parm(name="borrow_scale", label="Borrow Scale", parm_type=Parms.EnumParmType, default=0,
                                      options=borrow_scale_options,
                                      edit_callback_function=self.set_borrow_scale)
        self.parms.append(borrow_scale_parm)

        self.color_pixels()

        self.held_modifiers = []
        self.held_note_relationship = {}
        self.note_ons = []
        self.note_offs = []

    def set_bass_mode(self, bass_mode):
        self.bass_mode = bass_mode

    def set_spread_mode(self, spread_mode):
        self.spread_mode = spread_mode

    def set_borrow_scale(self, borrow_scale):
        self.borrow_scale = borrow_scale

    def color_pixels(self, color_overrides: dict = None):
        # set special color
        overrides = {}
        for i in range(6):
            overrides[i+6] = rainbowio.colorwheel(i*60)
        super().color_pixels(overrides)


    def build_chord(self, pad_note):
        if ModifierMap.BORROW in self.held_modifiers:
            if self.borrow_scale == 0:
                borrow_scale = Music.AUTOBORROWRELATIONSHIP[self.state.scale]
            else:
                borrow_scale = self.borrow_scale
            scale = Music.SCALES[borrow_scale]
        else:
            scale = Music.SCALES[self.state.scale]
        scale_notes = len(scale)

        # Build chord tones
        root_pad_note = pad_note + self.state.key_offset
        root_pad_octave = root_pad_note // scale_notes
        #TODO: Extract octave and add to final octave as currently the keys just wrap in same octave
        root_degree = root_pad_note % scale_notes
        #TODO: Add offsets for pentatonic scales - 2,3 for MPEN, 1,3 for mPEN and 2,3 for SPEN
        note2_degree = root_degree+2
        note3_degree = root_degree+4

        seventh_degree = None
        add9_degree = None

        # Apply sus/7th/add9
        if ModifierMap.SUS in self.held_modifiers:
            #if root_degree == 5:
            #   note2_degree += 1
            #else:
            note2_degree -= 1
            print("sus")

        if ModifierMap.SEV in self.held_modifiers:
            #TODO: Add override for pentatonic (+4 instead of +6)
            seventh_degree = root_degree + 6
            print("seventh")

        if ModifierMap.ADD9 in self.held_modifiers:
            # TODO: Add override for pentatonic (+4 instead of +8)
            add9_degree = root_degree + 8
            print("add9")

        # Create upper voicing
        # Resolve degrees into actual notes
        chord = [root_degree,note2_degree,note3_degree]
        if seventh_degree is not None:
            chord.append(seventh_degree)

        if add9_degree is not None:
            chord.append(add9_degree)

        print("base_chord_degrees: ", chord)

        for idx, note in enumerate(chord):
            octave = (note // scale_notes + (self.state.octave + Music.OCTAVEOFFSET + root_pad_octave))*12
            degree = note % scale_notes
            note = scale[degree]+self.state.key
            chord[idx] = note + octave

        root = chord[0]

        print("base_chord_notes: ", chord)

        # Apply inversion to upper voicing
        if ModifierMap.INV in self.held_modifiers:
            chord[0] += 12
            print("inversion")

        # Apply spread to upper voicing
        if self.spread_mode != 0:
            chord[-1] += 12
            if self.spread_mode == 2:
                chord[-2] += 12

        # Apply power chord (remove second)
        if ModifierMap.POWER in self.held_modifiers:
            chord.pop(1)

        # Add base note underneath
        if self.bass_mode != BassModes.NoBass:
            if self.bass_mode == BassModes.Root:
                chord = [root - 12] + chord
            if self.bass_mode == BassModes.Second:
                chord = [chord[1]-12]+chord
            if self.bass_mode == BassModes.Lowest:
                chord = [min(chord) - 12] + chord
            if self.bass_mode == BassModes.Highest:
                chord = [max(chord)-12]+chord

        return chord

    def process(self, note_ons, note_offs, velocities):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.note_ons.clear()
        self.note_offs.clear()
        self.velocities_out.clear()

        ## Process input notes and split them into notes and modifiers
        for pad_note in note_ons:
            pad_note = PADMAP.index(pad_note)  # Map from 0-11 starting from bottom left to top right
            if pad_note < 6:  # -> Chord root
                self.note_ons.append(pad_note)
            else:  # -> Modifier
                pad_note -= 6
                if pad_note not in self.held_modifiers:
                    self.held_modifiers.append(pad_note)

        for pad_note in note_offs:
            pad_note = PADMAP.index(pad_note)  # Map from 0-11 starting from bottom left to top right
            if pad_note < 6:  # -> Root Note
                self.note_offs.append(pad_note)
            else:  # -> Modifier
                pad_note -= 6
                if pad_note in self.held_modifiers:
                    self.held_modifiers.remove(pad_note)

        ## Now create the chords ##
        for pad_note in self.note_ons:
            chord = self.build_chord(pad_note)
            self.held_note_relationship[pad_note] = chord
            for note in chord:
                self.note_ons_out.append(note)
                self.velocities_out.append(self.velocity)


        for pad_note in self.note_offs:
            chord = self.held_note_relationship.get(pad_note)  # returns None if missing
            if chord:
                for note in chord:
                    self.note_offs_out.append(note)
                self.held_note_relationship.pop(pad_note, None)

        if note_ons or note_offs:
            print("Held Notes Relationship: ", self.held_note_relationship)

        return self.note_ons_out, self.note_offs_out, self.velocities_out
        
