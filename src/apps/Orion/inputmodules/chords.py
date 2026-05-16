from ..core import parms as Parms
from ..core._modules._input import Input
from ..core.note_array import  NoteArray,NoteOnArray,NoteOffArray,NoteRelationshipArray
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

        self.held_modifiers = NoteArray(length = 6)
        self.held_note_relationship = NoteRelationshipArray()
        self.note_ons = NoteOnArray(length = 6)
        self.note_offs = NoteOffArray(length = 6)
        self.temp_chord = NoteArray(length = 6)

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
        if self.held_modifiers.contains(ModifierMap.BORROW):
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
        if self.held_modifiers.contains(ModifierMap.SUS):
            #if root_degree == 5:
            #   note2_degree += 1
            #else:
            note2_degree -= 1
            #print("sus")

        if self.held_modifiers.contains(ModifierMap.SEV):
            #TODO: Add override for pentatonic (+4 instead of +6)
            seventh_degree = root_degree + 6
            #print("seventh")

        if self.held_modifiers.contains(ModifierMap.ADD9):
            # TODO: Add override for pentatonic (+4 instead of +8)
            add9_degree = root_degree + 8
            #print("add9")

        # Add notes to one array
        self.temp_chord.clear()
        self.temp_chord.append_value(root_degree)
        self.temp_chord.append_value(note2_degree)
        self.temp_chord.append_value(note3_degree)
        if seventh_degree is not None:
            self.temp_chord.append_value(seventh_degree)

        if add9_degree is not None:
            self.temp_chord.append_value(add9_degree)

        #print("base_chord_degrees: ", self.temp_chord.notes)

        # Resolve into actual midi notes
        for i in range(self.temp_chord.length):
            note = self.temp_chord.notes[i]
            octave = (note // scale_notes + (self.state.octave + Music.OCTAVEOFFSET + root_pad_octave))*12
            degree = note % scale_notes
            note = scale[degree]+self.state.key
            self.temp_chord.notes[i] = note + octave

        root = self.temp_chord.notes[0]

        #print("base_chord_notes: ", self.temp_chord.notes)

        # Apply inversion to upper voicing
        if self.held_modifiers.contains(ModifierMap.INV):
            self.temp_chord.notes[0] += 12
            #print("inversion")

        # Apply spread to upper voicing
        if self.spread_mode != 0:
            self.temp_chord.notes[self.temp_chord.length-1] += 12
            if self.spread_mode == 2:
                self.temp_chord.notes[self.temp_chord.length-2] += 12

        # Apply power chord (remove second)
        if self.held_modifiers.contains(ModifierMap.POWER):
            self.temp_chord.remove_index(1)

        # Add base note underneath
        if self.bass_mode != BassModes.NoBass:
            bass = None
            if self.bass_mode == BassModes.Root:
                bass = root - 12
            if self.bass_mode == BassModes.Second:
                bass = self.temp_chord.notes[1]-12
            if self.bass_mode == BassModes.Lowest:
                bass = self.temp_chord.get_min_note() - 12
            if self.bass_mode == BassModes.Highest:
                bass = self.temp_chord.get_max_note() - 12
            if bass is not None:
                self.temp_chord.insert_value_at_index(bass,0)

    def process(self, note_ons:NoteOnArray, note_offs:NoteOffArray):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.note_ons.clear()
        self.note_offs.clear()

        ## Process input notes and split them into notes and modifiers
        for i in range(note_ons.length):
            pad_note = note_ons.notes[i]
            pad_note = PADMAP.index(pad_note)  # Map from 0-11 starting from bottom left to top right
            if pad_note < 6:  # -> Chord root
                self.note_ons.append_value(pad_note)
            else:  # -> Modifier
                pad_note -= 6
                if not self.held_modifiers.contains(pad_note):
                    self.held_modifiers.append_value(pad_note)

        for i in range(note_offs.length):
            pad_note = note_offs.notes[i]
            pad_note = PADMAP.index(pad_note)  # Map from 0-11 starting from bottom left to top right
            if pad_note < 6:  # -> Root Note
                self.note_offs.append_value(pad_note)
            else:  # -> Modifier
                pad_note -= 6
                if self.held_modifiers.contains(pad_note):
                    self.held_modifiers.remove_value_first(pad_note)

        ## Now create the chords ##
        for i in range(self.note_ons.length):
            pad_note = self.note_ons.notes[i]
            self.build_chord(pad_note)
            for j in range(self.temp_chord.length):
                chord_note = self.temp_chord.notes[j]
                self.held_note_relationship.add_note(pad_note,chord_note)
                self.note_ons_out.append_value(chord_note,velocity=self.velocity)

        for i in range(self.note_offs.length):
            pad_note = self.note_offs.notes[i]
            chord = self.held_note_relationship.remove_note_all(pad_note)  # returns None if missing
            if chord.length > 0:
                for j in range(chord.length):
                    note = chord.notes[j]
                    self.note_offs_out.append_value(note)

        if note_ons.length > 0 or note_offs.length > 0:
            print("Held Notes Relationship: ", self.held_note_relationship.in_array, self.held_note_relationship.out_array)

        return self.note_ons_out, self.note_offs_out
        
