from ..core.module import Module
from ..core import parms as Parms
from ..core import music
from ..core.note_array import NoteRelationshipArray,NoteOnArray,NoteOffArray
from ..core import utils

class Randomize(Module):
    name = "randomize"
    label = "Randomize"
    version = 1
    def __init__(self, module_helper, slot_id):
        self.note_relationship = NoteRelationshipArray()

        self.note_range = 0
        self.note_increment = 1
        self.note_mode = 0
        self.octave_range = 0
        self.octave_mode = 0
        self.velocity_range = 0
        self.velocity_mode = 0

        self.pattern_length = 0
        self.scale_aware = True

        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()

        self.random_seed_base = 0
        self.random_seed_user = 0


        super().__init__(module_helper, slot_id)

    def create_main_parms(self):
        parms = []

        mode_options = ["Unipolar", "Bipolar"]

        # Randomize Velocity
        self.velocity_range_parm = Parms.Parm(name="velocity", label="Vel Range", default=self.velocity_range, parm_type=Parms.IntParmType,
                                          increment=1, edit_callback_function=self.set_velocity_range)
        parms.append(self.velocity_range_parm)

        self.velocity_mode_parm = Parms.Parm(name="velocity_mode", label="Vel Mode", default=self.velocity_mode,
                                         parm_type=Parms.EnumParmType,
                                         options=mode_options, edit_callback_function=self.set_velocity_mode)
        parms.append(self.velocity_mode_parm)

        # Randomize Note
        self.note_range_parm = Parms.Parm(name="note", label="Note Range", default=self.note_range, parm_type=Parms.IntParmType,
                                         increment=1, edit_callback_function=self.set_note_range)
        parms.append(self.note_range_parm)

        self.note_increment_parm = Parms.Parm(name="note_increment", label="Note Increm", default=self.note_increment, parm_type=Parms.IntParmType,
                                         minmax = (1,128), increment=1, edit_callback_function=self.set_note_increment)
        parms.append(self.note_increment_parm)

        self.note_mode_parm = Parms.Parm(name="note_mode", label="Note Mode", default=self.note_mode,
                                           parm_type=Parms.EnumParmType,
                                           options=mode_options, edit_callback_function=self.set_note_mode)
        parms.append(self.note_mode_parm)

        self.scale_aware_parm = Parms.Parm(name="scale_aware", label="Scale Aware", default=True,
                                           parm_type=Parms.BooleanParmType,
                                           edit_callback_function=self.set_scale_aware)
        parms.append(self.scale_aware_parm)

        # Randomize Octave
        self.octave_range_parm = Parms.Parm(name="octave", label="Oct Range", default=self.octave_range, parm_type=Parms.IntParmType,
                                          increment=1, edit_callback_function=self.set_octave_range)
        parms.append(self.octave_range_parm)

        self.octave_mode_parm = Parms.Parm(name="octave_mode", label="Oct Mode", default=self.octave_mode,
                                         parm_type=Parms.EnumParmType,
                                         options=mode_options, edit_callback_function=self.set_octave_mode)
        parms.append(self.octave_mode_parm)

        # Pattern Length
        self.pattern_length_parm = Parms.Parm(name="pattern_length", label="Pattern Len", default=0,
                                           parm_type=Parms.IntParmType, minmax = (0,128),
                                           edit_callback_function=self.set_pattern_length) #In 16th steps
        parms.append(self.pattern_length_parm)

        # Random Seed User
        self.random_seed_user_parm = Parms.Parm(name="random_seed_user", label="Random Seed", default=0,
                                              parm_type=Parms.IntParmType, minmax=(0, 10000),
                                              edit_callback_function=self.set_random_seed_user)
        parms.append(self.random_seed_user_parm)

        return parms

    def set_note_range(self, value):
        self.note_range = value

    def set_note_increment(self, value):
        self.note_increment = value

    def set_note_mode(self, value):
        self.note_mode = value

    def set_octave_range(self, value):
        self.octave_range = value

    def set_octave_mode(self, value):
        self.octave_mode = value

    def set_velocity_range(self, value):
        self.velocity_range = value

    def set_velocity_mode(self, value):
        self.velocity_mode = value

    def set_scale_aware(self, value):
        self.scale_aware = value

    def set_pattern_length(self, value):
        self.pattern_length = value

    def set_random_seed_user(self, value):
        self.random_seed_user = value

    def process(self, note_ons: NoteOnArray, note_offs: NoteOffArray):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        if self.scale_aware:
            scale = music.SCALES[self.module_helper.state.scale]
        else:
            scale = None

        # Set random seed:
        if self.pattern_length != 0:
            self.random_seed_base = self.module_helper.transport.midi_tick % (self.pattern_length*6)
        else:
            self.random_seed_base = self.module_helper.transport.midi_tick

        for i in range(note_ons.length):
            # Calc Note
            note_offset = 0
            if self.note_range != 0:
                note_offset = utils.random_int(self.random_seed_base + self.random_seed_user + i + 200,
                                         self.note_range, self.note_mode)*self.note_increment

            # Calc Octave
            octave_offset = 0
            if self.octave_range != 0:
                octave_offset = utils.random_int(self.random_seed_base + self.random_seed_user + i + 400,
                                               self.octave_range, self.octave_mode)

            # Calc Velocity
            velocity_offset = 0
            if self.velocity_range != 0:
                velocity_offset = utils.random_int(self.random_seed_base + self.random_seed_user + i + 600,
                                                 self.velocity_range, self.velocity_mode)

            note = note_ons.notes[i]
            new_note = music.transpose(note, note_offset, octave_offset, self.scale_aware, self.state.key, scale)

            velocity = 127
            if note_ons.velocities is not None:
                velocity = note_ons.velocities[i]

            velocity += velocity_offset
            velocity = min(127, max(0, velocity))

            self.note_ons_out.append_value(new_note, velocity=velocity)
            self.note_relationship.add_note(note, new_note)

        for i in range(note_offs.length):
            note = note_offs.notes[i]
            off_note = self.note_relationship.remove_note_single(note)
            if off_note is not None:
                self.note_offs_out.append_value(off_note)

        return self.note_ons_out, self.note_offs_out
