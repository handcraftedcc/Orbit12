from ..core import parms as Parms
from ..core._modules._input import Input
from ..core.note_array import  NoteArray,NoteOnArray,NoteOffArray,NoteRelationshipArray
import rainbowio

from ..core import music as Music
from ..core._modules._input import PADMAP

### CHORD INPUT DESIGN ###

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

### CHORD OPTIONS ###

# TODO: Update key color for chords
class ModifierMap:
    # What keys do what. Might become parameters later.
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

BASS_MODE_OPTIONS = ("NONE", "ROOT", "2ND", "LOW", "HIGH")
SPREAD_MODE_OPTIONS = ("TIGHT", "MED", "WIDE")
BORROW_SCALE_OPTIONS = ("AUTO",) + Music.SCALENAMES[1:]


### CHORD INPUT ###

class Chords(Input):
    name = "chords"
    label = "CHRD"
    def __init__(self, module_helper, slot_id):
        self.bass_mode = 0
        self.spread_mode = 0
        self.borrow_scale = 0
        self.max_chords = 1
        super().__init__(module_helper, slot_id, include_musical_parms=True)

        self.color_pixels()

        self.held_modifiers = NoteArray(length = 6)
        self.held_note_relationship = NoteRelationshipArray(12)
        self.note_ons = NoteOnArray(length = 6)
        self.note_offs = NoteOffArray(length = 6)
        self.temp_chord = NoteArray(length = 6)
        self.held_chords = 0

    ### PARMS ###

    def create_main_parms(self):
        parms = super().create_main_parms()
        # Bass Mode
        bass_mode_parm = Parms.Parm(name="bass", label="BASS", parm_type=Parms.EnumParmType, default=self.bass_mode,
                                      options=BASS_MODE_OPTIONS,
                                      bind_object=self, bind_attribute="bass_mode")
        parms.append(bass_mode_parm)

        # Spread Mode
        spread_mode_parm = Parms.Parm(name="spread", label="SPRD", parm_type=Parms.EnumParmType, default=self.spread_mode,
                                      options=SPREAD_MODE_OPTIONS,
                                      bind_object=self, bind_attribute="spread_mode")
        parms.append(spread_mode_parm)

        # Borrow Scale
        borrow_scale_parm = Parms.Parm(name="borrow_scale", label="BRWSCL", parm_type=Parms.EnumParmType, default=self.borrow_scale,
                                      options=BORROW_SCALE_OPTIONS,
                                      bind_object=self, bind_attribute="borrow_scale")
        parms.append(borrow_scale_parm)

        # Max Chords
        max_chords_parm = Parms.Parm(name="max_chords", label="MAXCT", parm_type=Parms.IntParmType,
                                       default=self.max_chords, minmax = (1,6),
                                       bind_object=self, bind_attribute="max_chords")
        parms.append(max_chords_parm)
        return parms

    def color_pixels(self, color_overrides = None):
        # set special color
        overrides = {}
        for i in range(6):
            overrides[i+6] = rainbowio.colorwheel(i*60)
        super().color_pixels(overrides)


    ### CHORD BUILDING ###

    def build_chord(self, pad_note):
        # Borrow mode temporarily swaps the scale used for chord construction.
        if self.held_modifiers.contains(ModifierMap.BORROW):
            if self.borrow_scale == 0:
                borrow_scale = Music.AUTOBORROWRELATIONSHIP[self.state.scale]
            else:
                borrow_scale = self.borrow_scale
            scale = Music.SCALES[borrow_scale]
        else:
            scale = Music.SCALES[self.state.scale]
        scale_notes = len(scale)

        # Resolve the pad to a scale degree and octave before adding chord tones.
        root_pad_note = pad_note + self.state.key_offset
        root_pad_octave = root_pad_note // scale_notes

        # Pentatonic scales need custom degree jumps to sound chord-like.
        pentatonic_state = [False, False]

        if (self.state.scale == Music.IDX_MAJP or
                self.state.scale == Music.IDX_SPEN):
            pentatonic_state[0] = True

        if self.state.scale == Music.IDX_MINP:
            pentatonic_state[1] = True

        #TODO: Extract octave and add to final octave as currently the keys just wrap in same octave
        root_degree = root_pad_note % scale_notes
        if pentatonic_state[0]:
            #print("MAJP")
            note2_degree = root_degree + 1
            note3_degree = root_degree + 3
        elif pentatonic_state[1]:
            #print("MINP")
            note2_degree = root_degree + 2
            note3_degree = root_degree + 3
        else:
            note2_degree = root_degree+2
            note3_degree = root_degree+4

        seventh_degree = None
        add9_degree = None

        # Optional modifiers add or reshape upper chord tones.
        if self.held_modifiers.contains(ModifierMap.SUS):
            #if root_degree == 5:
            #   note2_degree += 1
            #else:
            note2_degree -= 1
            #print("sus")



        if self.held_modifiers.contains(ModifierMap.SEV):
            if any(pentatonic_state):
                #print("pent7")
                seventh_degree = root_degree + 4
            else:
                seventh_degree = root_degree + 6
            #print("seventh")

        if self.held_modifiers.contains(ModifierMap.ADD9):
            if any(pentatonic_state):
                add9_degree = root_degree + 6
            else:
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
            octave = (note // scale_notes + (self.state.octave + 2 + root_pad_octave))*12
            degree = note % scale_notes
            note = scale[degree]+self.state.key
            self.temp_chord.notes[i] = note + octave

        root = self.temp_chord.notes[0]

        #print("base_chord_notes: ", self.temp_chord.notes)

        # Voicing modifiers operate after degrees become MIDI notes.
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
                bass = self.temp_chord.get_min_note()[0] - 12
            if self.bass_mode == BassModes.Highest:
                bass = self.temp_chord.get_max_note()[0] - 12
            if bass is not None:
                self.temp_chord.insert_value_at_index(bass,0)

    ### PROCESSING ###

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.note_ons.clear()
        self.note_offs.clear()

        ## Handle Note Offs##
        # Split pad releases into root releases and modifier releases.
        for i in range(note_offs.length):
            pad_note = note_offs.notes[i]
            pad_note = PADMAP.index(pad_note)  # Map from 0-11 starting from bottom left to top right
            if pad_note < 6:  # -> Root Note
                self.note_offs.append_value(pad_note)
            else:  # -> Modifier
                pad_note -= 6
                if self.held_modifiers.contains(pad_note):
                    self.held_modifiers.remove_value_first(pad_note)

        #Check if there is room for new chords based on max_chords

        new_chords = 0
        for i in range(note_ons.length):
            pad_note = note_ons.notes[i]
            pad_note = PADMAP.index(pad_note)
            if pad_note < 6:  # -> Root Note
                new_chords += 1
        force_offs = (new_chords + self.held_chords) - self.max_chords
        if force_offs > 0:
            counter = 0
            while counter < force_offs:
                in_note = self.held_note_relationship.in_notes[0]
                if not self.note_offs.contains(in_note):
                    counter += 1
                    self.note_offs.append_value(in_note)

        # Convert released roots back into all chord notes they created.
        for i in range(self.note_offs.length):
            pad_note = self.note_offs.notes[i]
            has, ct = self.held_note_relationship.has_in_note(pad_note)
            if not has: continue
            chord = self.held_note_relationship.remove_note_all(pad_note)  # returns None if missing
            self.held_chords -= 1
            if chord.return_length > 0:
                for j in range(chord.return_length):
                    note = chord.return_notes[j]
                    has_out_note, _ = self.held_note_relationship.has_out_note(note)
                    if not has_out_note:
                        self.note_offs_out.append_value(note)

        ## Process input notes and split them into notes and modifiers
        # Root pads create chords; upper pads latch modifier state.
        for i in range(note_ons.length):
            pad_note = note_ons.notes[i]
            pad_note = PADMAP.index(pad_note)  # Map from 0-11 starting from bottom left to top right
            if pad_note < 6:  # -> Chord root
                if self.note_ons.length>=self.max_chords:
                    continue
                self.note_ons.append_value(pad_note)
            else:  # -> Modifier
                pad_note -= 6
                if not self.held_modifiers.contains(pad_note):
                    self.held_modifiers.append_value(pad_note)

        ## Create and export chords ##
        # Store input-to-output relationships so releases can find chord tones.
        for i in range(self.note_ons.length):
            pad_note = self.note_ons.notes[i]
            self.held_chords += 1
            self.build_chord(pad_note)
            for j in range(self.temp_chord.length):
                chord_note = self.temp_chord.notes[j]
                if self.held_note_relationship.length < self.held_note_relationship.max_length:
                    has_out_note, _ = self.held_note_relationship.has_out_note(chord_note)
                    if has_out_note:
                        self.note_offs_out.append_value(chord_note)
                    self.held_note_relationship.add_note(pad_note, chord_note)
                    self.note_ons_out.append_value(chord_note, velocity=self.velocity)

        #if note_ons.length > 0 or note_offs.length > 0:
            #print("Held Notes Relationship: ", self.held_note_relationship.in_notes, self.held_note_relationship.out_notes)
        return self.note_ons_out, self.note_offs_out

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.held_modifiers.clear()
        self.held_note_relationship.clear()
        self.note_ons.clear()
        self.note_offs.clear()
        self.temp_chord.clear()

    def remove(self):
        super().remove()
        self.held_modifiers = None
        self.held_note_relationship = None
        self.note_ons = None
        self.note_offs = None
        self.temp_chord = None
