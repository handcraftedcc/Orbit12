from ..core.module import Module
from ..core import parms as Parms
from ..core._modules._input import Input

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

# TODO: Add parms for some of the settings & instead of a base modifier maybe just make that a parameter?
# TODO: Update key color for chords
# TODO: Stop one chord off from swallowing another one -> if the current node is still being held by another key then don't send note off?
#  Maybe before sending note off check if the note is held by any other chord. Or create a simple note list, where you pop and add the played notes.
#  And then only remove the note if it's the "last one in it"
class ModifierMap: #What keys do what -> Might turn this into parameters at some point
    SEV = 0
    ADD9 = 3
    SUS = 1
    INV = 4
    BASS = 2
    SPREAD = 5

class Chords(Input):
    name = "chords"
    label = "Chords"
    def __init__(self, state, slot_id):
        parms = []
        super().__init__(state, slot_id, include_musical_parms=True)
        self.held_modifiers = []
        self.held_note_relationship = {}
        self.held_notes = []
        self.note_ons = []
        self.note_offs = []

    def build_chord(self, pad_note):
        scale = Music.SCALES[self.state.scale]
        scale_notes = len(scale)

        # Build chord tones
        root_pad_note = pad_note + self.state.key_offset
        root_degree = root_pad_note % scale_notes
        note2_degree = root_degree+2
        note3_degree = root_degree+4

        seventh_degree = None
        add9_degree = None
        bass_degree = None

        # Apply sus/7th/add9
        if ModifierMap.SUS in self.held_modifiers:
            if root_degree == 5:
                note2_degree += 1
            else:
                note2_degree -= 1
            print("sus")

        if ModifierMap.SEV in self.held_modifiers:
            seventh_degree = root_degree + 6
            print("seventh")

        if ModifierMap.ADD9 in self.held_modifiers:
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
            octave = (note // scale_notes + (self.state.octave + Music.OCTAVEOFFSET))*12
            degree = note % scale_notes
            note = scale[degree]+self.state.key
            chord[idx] = note + octave

        print("base_chord_notes: ", chord)

        # Apply inversion to upper voicing
        if ModifierMap.INV in self.held_modifiers:
            chord[0] += 12
            print("inversion")

        # Apply spread to upper voicing
        if ModifierMap.SPREAD in self.held_modifiers:
            chord[-1] += 12
            print("spread")

        # Add base note underneath
        if ModifierMap.BASS in self.held_modifiers:
            chord = [chord[0]-12]+chord
            print("bass")

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
                self.held_notes.append(note)
                if note not in self.note_ons_out:
                    self.note_ons_out.append(note)
                    self.velocities_out.append(self.velocity)


        for pad_note in self.note_offs:
            chord = self.held_note_relationship.get(pad_note)  # returns None if missing
            if chord:
                for note in chord:
                    if note in self.held_notes:
                        self.held_notes.remove(note)
                    if note not in self.note_offs_out and note not in self.held_notes:
                        self.note_offs_out.append(note)
                self.held_note_relationship.pop(pad_note, None)

        return self.note_ons_out, self.note_offs_out, self.velocities_out
        
