from ..core.module import Module
from ..core import parms as Parms
from ..core import utils
from ..core.note_array import NoteOnArray, NoteOffArray


### DROP OPTIONS ###

DROP_MODE_OPTIONS = ("NOTE", "STEP")
DROP_MODE_NOTE = 0
DROP_MODE_STEP = 1


### DROP MODULE ###

class Drop(Module):
    name = "drop"
    label = "DROP"
    version = 1

    def __init__(self, module_helper, slot_id):
        self.transport = module_helper.transport
        self.drop = 0
        self.mode = DROP_MODE_NOTE
        self.pattern_length = 0
        self.seed = 0
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        super().__init__(module_helper, slot_id)

    ### PARMS ###

    def create_main_parms(self):
        return [
            Parms.Parm(name="drop", label="DROP", default=self.drop,
                       parm_type=Parms.IntParmType, minmax=(0, 100), increment=5,
                       bind_object=self, bind_attribute="drop"),
            Parms.Parm(name="mode", label="MDE", default=self.mode,
                       parm_type=Parms.EnumParmType, options=DROP_MODE_OPTIONS,
                       bind_object=self, bind_attribute="mode"),
            Parms.Parm(name="pattern_length", label="PTLEN", default=self.pattern_length,
                       parm_type=Parms.IntParmType, minmax=(0, 128),
                       bind_object=self, bind_attribute="pattern_length"),
            Parms.Parm(name="seed", label="SEED", default=self.seed,
                       parm_type=Parms.IntParmType, minmax=(0, 10000),
                       bind_object=self, bind_attribute="seed"),
        ]

    ### PROCESSING ###

    def step_seed(self):
        step = self.transport.midi_tick // 6
        if self.pattern_length:
            step = step % self.pattern_length
        return step + self.seed

    def should_drop(self, seed):
        return self.drop >= 100 or (self.drop > 0 and utils.random_float(seed) < self.drop / 100)

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        seed_base = self.step_seed()
        for i in range(note_ons.length):
            seed = seed_base if self.mode == DROP_MODE_STEP else seed_base + i
            if not self.should_drop(seed):
                self.note_ons_out.append_from(note_ons, i)

        self.note_offs_out.append_values(note_offs)
        return self.note_ons_out, self.note_offs_out

    ### CLEANUP ###

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

    def remove(self):
        super().remove()
        self.transport = None
