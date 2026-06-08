from ..core import neo_pixels
from ..core import parms as Parms
from ..core import music as Music
from ..core._modules._input import Input, PADMAP
from ..core.note_array import NoteArray


### CHORD WALK OPTIONS ###

CHORD_PADS = (0, 3, 6, 9)
TRIGGER_PADS = (1, 2, 4, 5, 7, 8, 10, 11)
INPUT_SLOT = 0
UI_PARMSELECTION = 1
UI_PARMEDIT = 2
PAD_COUNT = 4
SUS_OPTIONS = ("OFF", "SUS2", "SUS4")
DEFAULT_NOTES = (1, 3, 5, 7)
PAD_VALUE_PARMS = (
    ("note", "NOTE", "notes", Parms.IntParmType, (-24, 24), None),
    ("seventh", "7TH", "sevenths", Parms.BooleanParmType, None, None),
    ("sus", "SUS", "sus", Parms.EnumParmType, None, SUS_OPTIONS),
    ("power", "PWR", "power", Parms.BooleanParmType, None, None),
)


### CHORD WALK INPUT ###

class ChordWalk(Input):
    name = "chord_walk"
    label = "CHRDWLK"
    save_attrs = ("notes", "sevenths", "sus", "power")

    def __init__(self, module_helper, slot_id):
        self.pad = 0
        self.preview_chord = 0
        self.notes = list(DEFAULT_NOTES)
        self.sevenths = bytearray(PAD_COUNT)
        self.sus = bytearray(PAD_COUNT)
        self.power = bytearray(PAD_COUNT)
        self.held_chords = NoteArray(length=PAD_COUNT)
        self.held_triggers = NoteArray(length=8, velocities=True)
        self.register = NoteArray(length=6)
        self.trigger_root = None
        self.current_step = 0
        self.active_note = None
        module_helper.state.key_offset = -2
        super().__init__(module_helper, slot_id, include_musical_parms=True)
        self.held_chords.append_value(0)
        self.build_register(0)

    ### PARMS ###

    def create_main_parms(self):
        parms = [
            Parms.Parm(name="pad", label="PAD", default=self.pad,
                       parm_type=Parms.IntParmType, minmax=(0, PAD_COUNT - 1),
                       edit_callback_function=self.set_pad),
        ]
        for name, label, attr, parm_type, minmax, options in PAD_VALUE_PARMS:
            parms.append(Parms.Parm(name=name, label=label, default=getattr(self, attr)[self.pad],
                                    parm_type=parm_type, minmax=minmax, options=options,
                                    edit_callback_function=lambda value, attr=attr:
                                    self.set_pad_value(attr, value)))
        parms.extend([
            Parms.Parm(name="preview_chord", label="PRVCRD", default=self.preview_chord,
                       parm_type=Parms.BooleanParmType,
                       bind_object=self, bind_attribute="preview_chord"),
        ])
        parms.extend(super().create_main_parms())
        return parms

    def set_pad(self, value):
        self.pad = value
        for name, _, attr, _, _, _ in PAD_VALUE_PARMS:
            self.get_parm_by_name(name).set_value(getattr(self, attr)[value])
        self.queue_parm_rebuild()
        return value

    def set_pad_value(self, attr, value):
        getattr(self, attr)[self.pad] = value
        if self.held_chords.length and self.pad == self.held_chords.notes[self.held_chords.length - 1]:
            self.build_register(self.pad)
            self.color_pixels()
        return value

    ### PAD HELPERS ###

    def should_follow_played_pad(self):
        return (self.state.active_chain == INPUT_SLOT and
                self.state.active_ui_section in (UI_PARMSELECTION, UI_PARMEDIT))

    def select_pad_from_input(self, slot):
        if slot == self.pad:
            return
        pad_parm = self.get_parm_by_name("pad")
        if pad_parm is not None:
            pad_parm.set_value(slot)
        self.set_pad(slot)

    def color_pixels(self, color_overrides=None):
        color_array = [neo_pixels.KEYCOLORBASE] * 12
        for pad in CHORD_PADS:
            color_array[PADMAP.index(pad)] = neo_pixels.KEYCOLORNAVPARMS
        if self.register.length > 0:
            for i in range(len(TRIGGER_PADS)):
                if (i + self.state.key_offset) % self.register.length == 0:
                    color_array[PADMAP.index(TRIGGER_PADS[i])] = neo_pixels.KEYCOLORROOT
        try:
            self.module_helper.neo_pixels.set_key_colors(color_array)
        except:
            pass

    def pad_index(self, pad_note):
        return PADMAP.index(pad_note)

    def chord_slot(self, pad):
        for i in range(PAD_COUNT):
            if CHORD_PADS[i] == pad:
                return i
        return None

    def trigger_index(self, pad):
        for i in range(len(TRIGGER_PADS)):
            if TRIGGER_PADS[i] == pad:
                return i
        return None

    ### CHORD REGISTER ###

    def note_for_degree(self, degree):
        scale = Music.SCALES[self.state.scale]
        scale_notes = len(scale)
        octave = degree // scale_notes + self.state.octave
        degree = degree % scale_notes
        return self.clamp_note(scale[degree] + self.state.key + (octave + 2) * 12)

    def clamp_note(self, note):
        return max(0, min(127, note))

    def build_register(self, slot):
        root = self.notes[slot] - 1
        degrees = [root]
        self.register.clear()

        if self.sus[slot] == 1:
            degrees.append(root + 1)
        elif self.sus[slot] == 2:
            degrees.append(root + 3)
        elif not self.power[slot]:
            degrees.append(root + 2)

        degrees.append(root + 4)
        if self.sevenths[slot]:
            degrees.append(root + 6)

        for degree in degrees:
            self.register.append_value(self.note_for_degree(degree))

        self.register.sort_notes()

    def active_velocity(self):
        if self.held_triggers.length == 0:
            return self.velocity
        return self.held_triggers.velocity_at(self.held_triggers.length - 1, self.velocity)

    def note_for_step(self):
        if self.register.length == 0:
            return None
        while True:
            index = self.current_step % self.register.length
            octave = self.current_step // self.register.length
            note = self.register.notes[index] + octave * 12
            if note > 127:
                self.current_step -= 1
            elif note < 0:
                self.current_step += 1
            else:
                return note

    def refresh_register(self):
        if self.held_chords.length == 0:
            self.register.clear()
            if self.active_note is not None:
                self.note_offs_out.append_value(self.active_note)
                self.active_note = None
            self.color_pixels()
            return

        self.build_register(self.held_chords.notes[self.held_chords.length - 1])
        self.color_pixels()
        if self.held_triggers.length > 0:
            self.set_active_note(self.note_for_step(), self.active_velocity())

    ### WALK STATE ###

    def add_held_chord(self, slot):
        if self.held_chords.contains(slot):
            self.held_chords.remove_value_first(slot)
        self.held_chords.append_value(slot)

    def add_held_trigger(self, index, velocity):
        if self.held_triggers.contains(index):
            self.held_triggers.remove_value_first(index)
        self.held_triggers.append_value(index, velocity=velocity)

    def remove_held_trigger(self, index):
        self.held_triggers.remove_value_first(index)
        if self.trigger_root == index and self.held_triggers.length > 0:
            self.trigger_root = self.held_triggers.notes[self.held_triggers.length - 1]

    def reset_triggers(self):
        self.held_triggers.clear()
        self.trigger_root = None
        self.current_step = 0
        self.active_note = None

    def set_active_note(self, note, velocity):
        if note is None:
            return
        if self.active_note is not None and self.active_note != note:
            self.note_offs_out.append_value(self.active_note)
        if self.active_note != note:
            self.note_ons_out.append_value(note, velocity=velocity)
        self.active_note = note

    ### PROCESSING ###

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        for i in range(note_offs.length):
            pad = self.pad_index(note_offs.notes[i])
            slot = self.chord_slot(pad)
            if slot is not None:
                if self.preview_chord:
                    self.build_register(slot)
                    for j in range(self.register.length):
                        self.note_offs_out.append_value(self.register.notes[j])
                continue
            trigger = self.trigger_index(pad)
            if trigger is not None:
                self.remove_held_trigger(trigger)

        if self.held_triggers.length == 0 and self.active_note is not None:
            self.note_offs_out.append_value(self.active_note)
            self.reset_triggers()

        for i in range(note_ons.length):
            pad = self.pad_index(note_ons.notes[i])
            slot = self.chord_slot(pad)
            if slot is not None:
                velocity = note_ons.velocity_at(i, self.velocity)
                if self.should_follow_played_pad():
                    self.select_pad_from_input(slot)
                self.add_held_chord(slot)
                self.build_register(slot)
                self.color_pixels()
                if self.preview_chord:
                    for j in range(self.register.length):
                        self.note_ons_out.append_value(self.register.notes[j], velocity=velocity)
                continue

            trigger = self.trigger_index(pad)
            if trigger is None:
                continue
            velocity = note_ons.velocity_at(i, self.velocity)
            if self.held_triggers.length == 0:
                self.current_step = trigger + self.state.key_offset
                self.trigger_root = trigger
            else:
                self.current_step += trigger - self.trigger_root
            self.add_held_trigger(trigger, velocity)
            self.set_active_note(self.note_for_step(), velocity)

        return self.note_ons_out, self.note_offs_out

    ### CLEANUP ###

    def stop(self):
        super().stop()
        self.held_chords.clear()
        self.register.clear()
        self.reset_triggers()

    def remove(self):
        super().remove()
        self.held_chords = None
        self.held_triggers = None
        self.register = None
