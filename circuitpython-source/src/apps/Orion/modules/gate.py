from ..core.module import Module
from ..core import parms as Parms
from ..core import utils
from ..core.note_array import NoteArray, NoteOnArray, NoteOffArray
import adafruit_ticks as ticks


### GATE MODULE ###

class Gate(Module):
    name = "gate"
    label = "GATE"
    version = 1

    def __init__(self, module_helper, slot_id):
        self.module_helper = module_helper
        self.transport = self.module_helper.transport
        self.gate = 100
        self.gate_random = 0
        self.pattern_length = 0
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        self.scheduled_offs = NoteArray(times=True)
        self.popped_ids = NoteArray()
        super().__init__(module_helper, slot_id)

    ### PARMS ###

    def create_main_parms(self):
        return [
            Parms.Parm(name="gate", label="GATE", default=self.gate, parm_type=Parms.IntParmType,
                       increment=5, bind_object=self, bind_attribute="gate"),
            Parms.Parm(name="gate_random", label="GTRND", default=self.gate_random,
                       parm_type=Parms.IntParmType, increment=5,
                       bind_object=self, bind_attribute="gate_random"),
            Parms.Parm(name="pattern_length", label="PTLEN", default=self.pattern_length,
                       parm_type=Parms.IntParmType, minmax=(0, 128),
                       bind_object=self, bind_attribute="pattern_length"),
        ]

    ### PROCESSING ###

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        current = self.transport.now
        # Expire scheduled note-offs from previous gate events.
        self.scheduled_offs.pop_due(current, self.note_offs_out, self.popped_ids)

        gate = self.gate
        seed_step = self.transport.midi_tick // 6
        if self.pattern_length:
            seed_step = seed_step % self.pattern_length
        # Incoming notes pass through immediately and schedule their own offs.
        for i in range(note_ons.length):
            note = note_ons.notes[i]
            self.note_ons_out.append_from(note_ons, i)
            random_gate = gate + utils.random_int(seed_step + i, self.gate_random, 0)
            self.scheduled_offs.append_value(note, time=ticks.ticks_add(current, random_gate))

        return self.note_ons_out, self.note_offs_out

    ### CLEANUP ###

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.scheduled_offs.clear()
        self.popped_ids.clear()

    def remove(self):
        super().remove()
        self.transport = None
        self.scheduled_offs = None
        self.popped_ids = None
