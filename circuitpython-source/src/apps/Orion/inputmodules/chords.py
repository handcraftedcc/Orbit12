from ..core import parms as Parms
from ..core import neo_pixels
from ..core._modules._input import Input
from ..core.note_array import  NoteArray,NoteOnArray,NoteOffArray,NoteRelationshipArray
import rainbowio

from ..core import music as Music
from ..core._modules._input import PADMAP

### CHORD OPTIONS ###

LAYOUT_SPLIT = 0
LAYOUT_4X8 = 1
LAYOUT_OPTIONS = ("SPLT", "4&8")
INPUT_SLOT = 1
UI_PARMSELECTION = 1
UI_PARMEDIT = 2
ROOT_PADS_BY_LAYOUT = (
    (0, 1, 2, 3, 4, 5),
    (1, 2, 4, 5, 7, 8, 10, 11),
)
MODIFIER_PADS_BY_LAYOUT = (
    (6, 7, 8, 9, 10, 11),
    (0, 3, 6, 9),
)


class ModifierAction:
    OFF, INV1, INV2, INV3, SEV, ADD9, SUS2, SUS4, POWER, DIM, BORROW = range(11)


MODIFIER_ACTION_OPTIONS = (
    "OFF", "INV1", "INV2", "INV3", "SEV",
    "ADD9", "SUS2", "SUS4", "PWR", "DIM", "BRRW",
)
DEFAULT_MODIFIER_ACTIONS = (
    ModifierAction.SEV,
    ModifierAction.SUS4,
    ModifierAction.POWER,
    ModifierAction.ADD9,
    ModifierAction.INV1,
    ModifierAction.BORROW,
)
PENTATONIC_ADD4_SCALES = (Music.IDX_MAJP, Music.IDX_MINP, Music.IDX_SPEN)
def get_pad_slot(pads, pad_note):
    return pads.index(pad_note) if pad_note in pads else None


def get_root_slot(layout, pad_note):
    return get_pad_slot(ROOT_PADS_BY_LAYOUT[layout], pad_note)


def get_modifier_slot(layout, pad_note):
    return get_pad_slot(MODIFIER_PADS_BY_LAYOUT[layout], pad_note)


def get_layout_root_degree(root_slot, layout, split_drop, scale_notes):
    if layout == LAYOUT_SPLIT and scale_notes == 7:
        drop_index = min(max(split_drop, 1), 7) - 1
        if root_slot >= drop_index:
            return root_slot + 1
    return root_slot
def resolve_modifier_state(held_modifiers, modifier_actions):
    inversion = 0
    sus_mode = 0
    is_sev = False
    is_add9 = False
    is_power = False
    is_borrow = False
    is_dim = False

    if hasattr(held_modifiers, "notes"):
        active_modifiers = held_modifiers.notes[:held_modifiers.length]
    else:
        active_modifiers = held_modifiers

    for modifier_slot in active_modifiers:
        if modifier_slot >= len(modifier_actions):
            continue
        action = modifier_actions[modifier_slot]
        if not action:
            continue
        if action < ModifierAction.SEV:
            inversion = action if inversion == 0 else min(inversion, action)
        elif action == ModifierAction.SEV:
            is_sev = True
        elif action == ModifierAction.ADD9:
            is_add9 = True
        elif action <= ModifierAction.SUS4:
            sus_mode = action
        elif action == ModifierAction.POWER:
            is_power = True
        elif action == ModifierAction.DIM:
            is_dim = True
        else:
            is_borrow = True

    return inversion, sus_mode, is_sev, is_add9, is_power, is_borrow, is_dim

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
        self.modifier_pad = 0
        self.layout = LAYOUT_SPLIT
        self.split_drop = 7
        self.bass_mode = 0
        self.spread_mode = 0
        self.borrow_scale = 0
        self.max_chords = 1
        self.modifier_actions = list(DEFAULT_MODIFIER_ACTIONS)
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
        velocity_parm = parms.pop()

        parms.extend([
            Parms.Parm(name="modifier_select", label="SEL", parm_type=Parms.IntParmType,
                       default=self.modifier_pad + 1, minmax=(1, 6),
                       edit_callback_function=self.set_modifier_pad_from_parm),
            Parms.Parm(name="modifier_action", label="MOD", parm_type=Parms.EnumParmType,
                       default=self.modifier_actions[self.modifier_pad],
                       options=MODIFIER_ACTION_OPTIONS,
                       edit_callback_function=self.set_selected_modifier_action),
            Parms.Parm(name="bass", label="BASS", parm_type=Parms.EnumParmType, default=self.bass_mode,
                       options=BASS_MODE_OPTIONS, bind_object=self, bind_attribute="bass_mode"),
            Parms.Parm(name="spread", label="SPRD", parm_type=Parms.EnumParmType, default=self.spread_mode,
                       options=SPREAD_MODE_OPTIONS, bind_object=self, bind_attribute="spread_mode"),
            Parms.Parm(name="borrow_scale", label="BRWSCL", parm_type=Parms.EnumParmType, default=self.borrow_scale,
                       options=BORROW_SCALE_OPTIONS, bind_object=self, bind_attribute="borrow_scale"),
            Parms.Parm(name="max_chords", label="MAXCT", parm_type=Parms.IntParmType,
                       default=self.max_chords, minmax=(1, 6),
                       bind_object=self, bind_attribute="max_chords"),
        ])

        parms.append(velocity_parm)
        parms.extend([
            Parms.Parm(name="layout", label="LAY", parm_type=Parms.EnumParmType, default=self.layout,
                       options=LAYOUT_OPTIONS, edit_callback_function=self.set_layout),
            Parms.Parm(name="split_drop", label="SPLDRP", parm_type=Parms.IntParmType, default=self.split_drop,
                       minmax=(1, 7), bind_object=self, bind_attribute="split_drop"),
        ])
        return parms

    def color_pixels(self, color_overrides=None):
        color_array = [neo_pixels.KEYCOLORBASE] * 12
        scale_notes = len(Music.SCALES[self.state.scale])
        for root_slot, pad in enumerate(ROOT_PADS_BY_LAYOUT[self.layout]):
            degree = get_layout_root_degree(root_slot, self.layout, self.split_drop, scale_notes) + self.state.key_offset
            if degree % scale_notes == 0:
                color_array[PADMAP.index(pad)] = neo_pixels.KEYCOLORROOT

        for pad in MODIFIER_PADS_BY_LAYOUT[self.layout]:
            color_array[PADMAP.index(pad)] = neo_pixels.KEYCOLORNAVPARMS

        try:
            self.module_helper.neo_pixels.set_key_colors(color_array)
        except:
            pass

    def set_layout(self, value):
        self.layout = value
        self.held_modifiers.clear()
        self.module_helper.output_manager.all_notes_off()
        self.color_pixels()
        return value

    def set_modifier_pad(self, value):
        self.modifier_pad = min(max(value, 0), 5)
        self.update_modifier_parms()
        return self.modifier_pad

    def set_modifier_pad_from_parm(self, value):
        return self.set_modifier_pad(value - 1)

    def set_selected_modifier_action(self, value):
        self.modifier_actions[self.modifier_pad] = value
        return value

    def update_modifier_parms(self):
        modifier_action_parm = self.get_parm_by_name("modifier_action")
        if modifier_action_parm is not None:
            modifier_action_parm.set_value(self.modifier_actions[self.modifier_pad])
        modifier_select_parm = self.get_parm_by_name("modifier_select")
        if modifier_select_parm is not None:
            modifier_select_parm.set_value(self.modifier_pad + 1)

    def follow_modifier_input(self, slot):
        if (slot == self.modifier_pad or self.state.active_chain != INPUT_SLOT or
                self.state.active_ui_section not in (UI_PARMSELECTION, UI_PARMEDIT)):
            return
        self.modifier_pad = slot
        self.update_modifier_parms()
        self.queue_parm_rebuild()

    def build_chord(self, root_slot, modifier_state):
        inversion, sus_mode, is_sev, is_add9, is_power, is_borrow, is_dim = modifier_state
        scale_id = self.state.scale
        scale = Music.SCALES[scale_id]
        if is_borrow:
            borrow_scale = Music.AUTOBORROWRELATIONSHIP[scale_id] if self.borrow_scale == 0 else self.borrow_scale
            scale = Music.SCALES[borrow_scale]
        scale_notes = len(scale)
        root_pad_note = get_layout_root_degree(root_slot, self.layout, self.split_drop, scale_notes) + self.state.key_offset
        root_pad_octave, root_degree = divmod(root_pad_note, scale_notes)
        is_major_pent = scale_id in (Music.IDX_MAJP, Music.IDX_SPEN)
        is_minor_pent = scale_id == Music.IDX_MINP

        if is_major_pent:
            note2_degree, note3_degree = root_degree + 1, root_degree + 3
        elif is_minor_pent:
            note2_degree, note3_degree = root_degree + 2, root_degree + 3
        else:
            note2_degree, note3_degree = root_degree + 2, root_degree + 4

        if sus_mode == ModifierAction.SUS2:
            note2_degree = root_degree + 1
        elif sus_mode == ModifierAction.SUS4:
            note2_degree = root_degree + 3

        self.temp_chord.clear()
        for degree in (root_degree, note2_degree, note3_degree):
            self.temp_chord.append_value(degree)
        if is_sev:
            self.temp_chord.append_value(root_degree + (4 if scale_id in PENTATONIC_ADD4_SCALES else 6))
        if is_add9:
            self.temp_chord.append_value(root_degree + (6 if scale_id in PENTATONIC_ADD4_SCALES else 8))

        for i in range(self.temp_chord.length):
            octave, degree = divmod(self.temp_chord.notes[i], scale_notes)
            self.temp_chord.notes[i] = (
                scale[degree] + self.state.key + (octave + self.state.octave + 2 + root_pad_octave) * 12
            )

        root = self.temp_chord.notes[0]
        if is_dim:
            if sus_mode == 0 and self.temp_chord.length > 1:
                self.temp_chord.notes[1] -= 1
            if self.temp_chord.length > 2:
                self.temp_chord.notes[2] -= 1

        for i in range(min(inversion, max(self.temp_chord.length - 1, 0))):
            self.temp_chord.notes[i] += 12

        if self.spread_mode != 0:
            self.temp_chord.notes[self.temp_chord.length - 1] += 12
            if self.spread_mode == 2:
                self.temp_chord.notes[self.temp_chord.length - 2] += 12

        if is_power:
            self.temp_chord.remove_index(1)

        if self.bass_mode != BassModes.NoBass:
            bass = None
            if self.bass_mode == BassModes.Root:
                bass = root - 12
            elif self.bass_mode == BassModes.Second:
                bass = self.temp_chord.notes[1] - 12
            elif self.bass_mode == BassModes.Lowest:
                bass = self.temp_chord.get_min_note()[0] - 12
            elif self.bass_mode == BassModes.Highest:
                bass = self.temp_chord.get_max_note()[0] - 12
            if bass is not None:
                self.temp_chord.insert_value_at_index(bass, 0)

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.note_ons.clear()
        self.note_offs.clear()
        root_pads = ROOT_PADS_BY_LAYOUT[self.layout]
        modifier_pads = MODIFIER_PADS_BY_LAYOUT[self.layout]

        for i in range(note_offs.length):
            pad_note = PADMAP.index(note_offs.notes[i])
            root_slot = get_pad_slot(root_pads, pad_note)
            if root_slot is not None:
                self.note_offs.append_value(root_slot)
                continue
            modifier_slot = get_pad_slot(modifier_pads, pad_note)
            if modifier_slot is not None:
                self.held_modifiers.remove_value_first(modifier_slot)

        for i in range(self.note_offs.length):
            has, _ = self.held_note_relationship.has_in_note(self.note_offs.notes[i])
            if not has:
                continue
            chord = self.held_note_relationship.remove_note_all(self.note_offs.notes[i])
            self.held_chords -= 1
            for j in range(chord.return_length):
                note = chord.return_notes[j]
                has_out_note, _ = self.held_note_relationship.has_out_note(note)
                if not has_out_note:
                    self.note_offs_out.append_value(note)

        new_chords = sum(
            1 for i in range(note_ons.length)
            if get_pad_slot(root_pads, PADMAP.index(note_ons.notes[i])) is not None
        )
        force_offs = (new_chords + self.held_chords) - self.max_chords
        if force_offs > 0:
            release_start = self.note_offs.length
            queued_roots = []
            for i in range(self.held_note_relationship.length):
                in_note = self.held_note_relationship.in_notes[i]
                if in_note in queued_roots or self.note_offs.contains(in_note):
                    continue
                queued_roots.append(in_note)
                self.note_offs.append_value(in_note)
                force_offs -= 1
                if force_offs == 0:
                    break

            for i in range(release_start, self.note_offs.length):
                has, _ = self.held_note_relationship.has_in_note(self.note_offs.notes[i])
                if not has:
                    continue
                chord = self.held_note_relationship.remove_note_all(self.note_offs.notes[i])
                self.held_chords -= 1
                for j in range(chord.return_length):
                    note = chord.return_notes[j]
                    has_out_note, _ = self.held_note_relationship.has_out_note(note)
                    if not has_out_note:
                        self.note_offs_out.append_value(note)

        for i in range(note_ons.length):
            pad_note = PADMAP.index(note_ons.notes[i])
            root_slot = get_pad_slot(root_pads, pad_note)
            if root_slot is not None:
                if self.note_ons.length >= self.max_chords:
                    continue
                self.note_ons.append_value(root_slot)
                continue
            modifier_slot = get_pad_slot(modifier_pads, pad_note)
            if modifier_slot is not None:
                self.follow_modifier_input(modifier_slot)
                if not self.held_modifiers.contains(modifier_slot):
                    self.held_modifiers.append_value(modifier_slot)

        modifier_state = resolve_modifier_state(self.held_modifiers, self.modifier_actions)
        for i in range(self.note_ons.length):
            root_slot = self.note_ons.notes[i]
            self.build_chord(root_slot, modifier_state)
            added_notes = 0
            for j in range(self.temp_chord.length):
                chord_note = self.temp_chord.notes[j]
                if self.held_note_relationship.length >= self.held_note_relationship.max_length:
                    break
                has_out_note, _ = self.held_note_relationship.has_out_note(chord_note)
                if has_out_note:
                    self.note_offs_out.append_value(chord_note)
                self.held_note_relationship.add_note(root_slot, chord_note)
                self.note_ons_out.append_value(chord_note, velocity=self.velocity)
                added_notes += 1
            if added_notes:
                self.held_chords += 1

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
