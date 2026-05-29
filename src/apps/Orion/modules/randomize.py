from ..core.module import Module
from ..core import parms as Parms
from ..core import music
from ..core.note_array import NoteRelationshipArray,NoteOnArray,NoteOffArray
from ..core import utils

RANDOMIZE_MODE_OPTIONS = ("UNI", "BI")

class Randomize(Module):
    name = "randomize"
    label = "RND"
    help_text = "RAND VEL/OCT/NOTE.."
    version = 1
    def __init__(self, module_helper, slot_id):
        self.note_relationship = NoteRelationshipArray()

        self.note_range = 0
        self.note_increment = 1
        self.note_chance = 0.5
        self.note_mode = 0
        self.octave_range = 0
        self.octave_chance = 0.5
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

        # Randomize Velocity
        velocity_range_parm = Parms.Parm(name="velocity", label="VEL RNG", default=self.velocity_range,
                                         parm_type=Parms.IntParmType, increment=1,
                                         edit_callback_function=self.set_velocity_range)
        parms.append(velocity_range_parm)

        velocity_mode_parm = Parms.Parm(name="velocity_mode", label="VEL MDE", default=self.velocity_mode,
                                        parm_type=Parms.EnumParmType, options=RANDOMIZE_MODE_OPTIONS,
                                        edit_callback_function=self.set_velocity_mode)
        parms.append(velocity_mode_parm)

        # Randomize Note
        note_range_parm = Parms.Parm(name="note", label="NOTE RNG", default=self.note_range,
                                     parm_type=Parms.IntParmType, increment=1,
                                     edit_callback_function=self.set_note_range)
        parms.append(note_range_parm)

        note_increment_parm = Parms.Parm(name="note_increment", label="NOTE INC", default=self.note_increment,
                                         parm_type=Parms.IntParmType, minmax=(1,128), increment=1,
                                         edit_callback_function=self.set_note_increment)
        parms.append(note_increment_parm)

        note_chance_parm = Parms.Parm(name="note_chance", label="NOTE %", default=self.note_chance,
                                      parm_type=Parms.PercentParmType, increment=0.05, minmax=(0,1),
                                      edit_callback_function=self.set_note_chance)
        parms.append(note_chance_parm)

        note_mode_parm = Parms.Parm(name="note_mode", label="NOTE MDE", default=self.note_mode,
                                    parm_type=Parms.EnumParmType, options=RANDOMIZE_MODE_OPTIONS,
                                    edit_callback_function=self.set_note_mode)
        parms.append(note_mode_parm)

        scale_aware_parm = Parms.Parm(name="scale_aware", label="SCL AWR", default=self.scale_aware,
                                      parm_type=Parms.BooleanParmType,
                                      edit_callback_function=self.set_scale_aware)
        parms.append(scale_aware_parm)

        # Randomize Octave
        octave_range_parm = Parms.Parm(name="octave", label="OCT RNG", default=self.octave_range,
                                       parm_type=Parms.IntParmType, increment=1,
                                       edit_callback_function=self.set_octave_range)
        parms.append(octave_range_parm)

        octave_chance_parm = Parms.Parm(name="octave_chance", label="OCT %", default=self.octave_chance,
                                        parm_type=Parms.PercentParmType, minmax=(0, 1),
                                        increment=0.05, edit_callback_function=self.set_octave_chance)
        parms.append(octave_chance_parm)

        octave_mode_parm = Parms.Parm(name="octave_mode", label="OCT MDE", default=self.octave_mode,
                                      parm_type=Parms.EnumParmType, options=RANDOMIZE_MODE_OPTIONS,
                                      edit_callback_function=self.set_octave_mode)
        parms.append(octave_mode_parm)

        # Pattern Length
        pattern_length_parm = Parms.Parm(name="pattern_length", label="PTN LEN", default=self.pattern_length,
                                         parm_type=Parms.IntParmType, minmax=(0,128),
                                         edit_callback_function=self.set_pattern_length) #In 16th steps
        parms.append(pattern_length_parm)

        # Random Seed User
        random_seed_user_parm = Parms.Parm(name="random_seed_user", label="SEED", default=self.random_seed_user,
                                           parm_type=Parms.IntParmType, minmax=(0, 10000),
                                           edit_callback_function=self.set_random_seed_user)
        parms.append(random_seed_user_parm)

        return parms

    def set_note_range(self, value):
        self.note_range = value

    def set_note_increment(self, value):
        self.note_increment = value

    def set_note_chance(self, value):
        self.note_chance = value

    def set_note_mode(self, value):
        self.note_mode = value

    def set_octave_range(self, value):
        self.octave_range = value

    def set_octave_chance(self, value):
        self.octave_chance = value

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
            if self.note_range != 0 and self.note_chance != 0:
                note_chance = utils.random_float(self.random_seed_base + self.random_seed_user + i + 100)
                if note_chance < self.note_chance:
                    note_offset = utils.random_int(self.random_seed_base + self.random_seed_user + i + 200,
                                                   self.note_range, self.note_mode) * self.note_increment

            # Calc Octave
            octave_offset = 0
            if self.octave_range != 0 and self.octave_chance != 0:
                octave_chance = utils.random_float(self.random_seed_base + self.random_seed_user + i + 300)
                if octave_chance < self.octave_chance:
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

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.note_relationship.clear()

    def remove(self):
        super().remove()
        self.note_relationship = None
