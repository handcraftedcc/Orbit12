from ..core import neo_pixels
from ..core import parms as Parms
from ..core import music as Music
from ..core._modules._input import Input, PADMAP
from ..core.note_array import NoteArray, NoteRelationshipArray


### MATRIX OPTIONS ###

PAD_COUNT = 12
INPUT_SLOT = 1
UI_PARMSELECTION = 1
UI_PARMEDIT = 2
SUS_OPTIONS = ("OFF", "SUS2", "SUS4")
BASS_MODE_OPTIONS = ("NONE", "ROOT", "2ND", "LOW", "HIGH")
SPREAD_MODE_OPTIONS = ("TIGHT", "MED", "WIDE")
RESET_OPTIONS = (">", "DEG", "CHRD", "NOTE", "HALF")
DEFAULT_NOTES = (1, 1, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8)
DEFAULT_CHORDS = (1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0)
DEGREE_NOTES = (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)
HALF_NOTES = (1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5, 6)
ALL_CHORDS = (1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1)
NO_CHORDS = (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
HALF_CHORDS = (1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0)
LAYOUTS = (
    None,
    (DEFAULT_NOTES, DEFAULT_CHORDS),
    (DEGREE_NOTES, ALL_CHORDS),
    (DEGREE_NOTES, NO_CHORDS),
    (HALF_NOTES, HALF_CHORDS),
)
PAD_VALUE_PARMS = (
    ("note", "NOTE", "notes", Parms.IntParmType, (-24, 24), None, True),
    ("chord", "CHORD", "chords", Parms.BooleanParmType, None, None, True),
    ("seventh", "7TH", "sevenths", Parms.BooleanParmType, None, None, False),
    ("ninth", "9TH", "ninths", Parms.BooleanParmType, None, None, False),
    ("inversion", "INV", "inversions", Parms.IntParmType, (0, 3), None, False),
    ("sus", "SUS", "sus", Parms.EnumParmType, None, SUS_OPTIONS, False),
    ("dim", "DIM", "dim", Parms.BooleanParmType, None, None, False),
    ("power", "PWR", "power", Parms.BooleanParmType, None, None, False),
)

MATRIX_NOTE_COLOR = neo_pixels.KEYCOLORBASE
MATRIX_NOTE_ROOT_COLOR = neo_pixels.KEYCOLORROOT
MATRIX_CHORD_COLOR = neo_pixels.KEYCOLORNAVPARMS
MATRIX_CHORD_ROOT_COLOR = neo_pixels.KEYCOLORENTER


### MATRIX INPUT ###

class Matrix(Input):
    name = "matrix"
    label = "MTRX"
    version = 1
    save_attrs = ("notes", "chords", "sevenths", "ninths", "inversions", "sus", "power", "dim")

    def __init__(self, module_helper, slot_id):
        self.pad = 0
        self.bass_mode = 0
        self.spread_mode = 0
        self.reset_parm = None
        self.notes = list(DEFAULT_NOTES)
        self.chords = bytearray(DEFAULT_CHORDS)
        self.sevenths = bytearray(PAD_COUNT)
        self.ninths = bytearray(PAD_COUNT)
        self.inversions = bytearray(PAD_COUNT)
        self.sus = bytearray(PAD_COUNT)
        self.power = bytearray(PAD_COUNT)
        self.dim = bytearray(PAD_COUNT)
        self.held_note_relationship = NoteRelationshipArray(36)
        self.temp_notes = NoteArray(length=6)
        super().__init__(module_helper, slot_id, include_musical_parms=True)

    ### PARMS ###

    def create_main_parms(self):
        parms = [
            Parms.Parm(name="pad", label="PAD", default=self.pad,
                       parm_type=Parms.IntParmType, minmax=(0, PAD_COUNT - 1),
                       edit_callback_function=self.set_pad),
        ]
        for name, label, attr, parm_type, minmax, options, repaint in PAD_VALUE_PARMS:
            parms.append(Parms.Parm(name=name, label=label, default=getattr(self, attr)[self.pad],
                                    parm_type=parm_type, minmax=minmax, options=options,
                                    edit_callback_function=lambda value, attr=attr, repaint=repaint:
                                    self.set_pad_value(attr, value, repaint)))
        parms.extend([
            Parms.Parm(name="bass", label="BASS", default=self.bass_mode,
                       parm_type=Parms.EnumParmType, options=BASS_MODE_OPTIONS,
                       bind_object=self, bind_attribute="bass_mode"),
            Parms.Parm(name="spread", label="SPRD", default=self.spread_mode,
                       parm_type=Parms.EnumParmType, options=SPREAD_MODE_OPTIONS,
                       bind_object=self, bind_attribute="spread_mode"),
        ])
        parms.extend(super().create_main_parms())
        reset_parm = Parms.Parm(name="reset", label="RESET", default=0,
                                parm_type=Parms.EnumParmType, options=RESET_OPTIONS,
                                enter_callback_function=self.arm_reset,
                                exit_callback_function=self.reset_layout)
        self.reset_parm = reset_parm
        parms.append(reset_parm)
        return parms

    def set_pad(self, value):
        self.pad = value
        for name, _, attr, _, _, _, _ in PAD_VALUE_PARMS:
            self.get_parm_by_name(name).set_value(getattr(self, attr)[value])
        return value

    def set_pad_value(self, attr, value, repaint=False):
        getattr(self, attr)[self.pad] = value
        if repaint:
            self.color_pixels()
        return value

    ### RESET ###

    def arm_reset(self, value):
        if self.reset_parm is not None:
            self.reset_parm.display_value = "SUR?"
            self.queue_parm_rebuild()
        return value

    def clear_pad_modifiers(self):
        for values in (self.sevenths, self.ninths, self.inversions, self.sus, self.power, self.dim):
            values[:] = bytearray(PAD_COUNT)

    def apply_layout(self, notes, chords):
        self.clear_pad_modifiers()
        for pad in range(PAD_COUNT):
            self.notes[pad] = notes[pad]
            self.chords[pad] = chords[pad]
        self.set_pad(self.pad)
        self.color_pixels()

    def reset_layout(self, value):
        if value:
            notes, chord_count = LAYOUTS[value]
            self.apply_layout(notes, chord_count)

        if self.reset_parm is not None:
            self.reset_parm.set_value(0)
            self.queue_parm_rebuild()
        return value

    ### PAD DISPLAY ###

    def select_pad_from_input(self, pad):
        if pad == self.pad:
            return
        pad_parm = self.get_parm_by_name("pad")
        if pad_parm is not None:
            pad_parm.set_value(pad)
        self.set_pad(pad)
        self.queue_parm_rebuild()

    def should_follow_played_pad(self):
        return (self.state.active_chain == INPUT_SLOT and
                self.state.active_ui_section in (UI_PARMSELECTION, UI_PARMEDIT))

    def color_pixels(self, color_overrides=None):
        color_array = [MATRIX_NOTE_COLOR] * PAD_COUNT
        scale_notes = len(Music.SCALES[self.state.scale])
        for pad in range(PAD_COUNT):
            color = MATRIX_CHORD_COLOR if self.chords[pad] else MATRIX_NOTE_COLOR
            if (self.notes[pad] - 1 + self.state.key_offset) % scale_notes == 0:
                color = MATRIX_CHORD_ROOT_COLOR if self.chords[pad] else MATRIX_NOTE_ROOT_COLOR
            color_array[PADMAP.index(pad)] = color
        try:
            self.module_helper.neo_pixels.set_key_colors(color_array)
        except:
            pass

    ### NOTE BUILDING ###

    def note_for_degree(self, degree):
        scale = Music.SCALES[self.state.scale]
        scale_notes = len(scale)
        degree += self.state.key_offset
        octave = degree // scale_notes + self.state.octave
        degree = degree % scale_notes
        return scale[degree] + self.state.key + (octave + 2) * 12

    def build_pad_notes(self, pad):
        root = self.notes[pad] - 1
        self.temp_notes.clear()

        if not self.chords[pad]:
            self.temp_notes.append_value(self.note_for_degree(root))
            return

        degrees = [root]
        if self.sus[pad] == 1:
            degrees.append(root + 1)
        elif self.sus[pad] == 2:
            degrees.append(root + 3)
        elif not self.power[pad]:
            degrees.append(root + 2)

        degrees.append(root + 4)
        if self.sevenths[pad]:
            degrees.append(root + 6)
        if self.ninths[pad]:
            degrees.append(root + 8)

        for degree in degrees:
            self.temp_notes.append_value(self.note_for_degree(degree))

        root_note = self.temp_notes.notes[0]
        if self.dim[pad]:
            if self.sus[pad] == 0 and self.temp_notes.length > 1:
                self.temp_notes.notes[1] -= 1
            if self.temp_notes.length > 2:
                self.temp_notes.notes[2] -= 1

        inversions = min(self.inversions[pad], self.temp_notes.length - 1)
        for i in range(inversions):
            self.temp_notes.notes[i] += 12

        if self.spread_mode != 0:
            self.temp_notes.notes[self.temp_notes.length - 1] += 12
            if self.spread_mode == 2 and self.temp_notes.length > 2:
                self.temp_notes.notes[self.temp_notes.length - 2] += 12

        if self.bass_mode != 0:
            bass = None
            if self.bass_mode == 1:
                bass = root_note - 12
            elif self.bass_mode == 2 and self.temp_notes.length > 1:
                bass = self.temp_notes.notes[1] - 12
            elif self.bass_mode == 3:
                bass = self.temp_notes.get_min_note()[0] - 12
            elif self.bass_mode == 4:
                bass = self.temp_notes.get_max_note()[0] - 12
            if bass is not None:
                self.temp_notes.insert_value_at_index(bass, 0)

    ### PROCESSING ###

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        for i in range(note_offs.length):
            pad = PADMAP.index(note_offs.notes[i])
            off_notes = self.held_note_relationship.remove_note_all(pad)
            for j in range(off_notes.return_length):
                note = off_notes.return_notes[j]
                has_out_note, _ = self.held_note_relationship.has_out_note(note)
                if not has_out_note:
                    self.note_offs_out.append_value(note)

        for i in range(note_ons.length):
            pad = PADMAP.index(note_ons.notes[i])
            if self.should_follow_played_pad():
                self.select_pad_from_input(pad)
            self.build_pad_notes(pad)
            for j in range(self.temp_notes.length):
                note = self.temp_notes.notes[j]
                has_out_note, _ = self.held_note_relationship.has_out_note(note)
                if has_out_note:
                    self.note_offs_out.append_value(note)
                self.note_ons_out.append_value(note, velocity=self.velocity)
                self.held_note_relationship.add_note(pad, note)

        return self.note_ons_out, self.note_offs_out

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.held_note_relationship.clear()
        self.temp_notes.clear()

    def remove(self):
        super().remove()
        self.held_note_relationship = None
        self.temp_notes = None
