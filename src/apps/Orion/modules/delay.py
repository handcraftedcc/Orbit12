from ..core.module import Module
from ..core import parms as Parms
from ..core import utils
from ..core.note_array import NoteArray, NoteOnArray, NoteOffArray
import adafruit_ticks as ticks


### DELAY MODULE ###

class Delay(Module):
    name = "delay"
    label = "DELAY"
    version = 1

    def __init__(self, module_helper, slot_id):
        self.module_helper = module_helper
        self.transport = self.module_helper.transport
        self.delay = 100
        self.delay_random = 0
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        self.scheduled_ons = NoteArray(velocities=True, times=True)
        self.scheduled_offs = NoteArray(times=True)
        self.note_delays = NoteArray(times=True)
        self.popped_ids = NoteArray()
        super().__init__(module_helper, slot_id)

    ### PARMS ###

    def create_main_parms(self):
        return [
            Parms.Parm(name="delay", label="DLY", default=self.delay, parm_type=Parms.IntParmType,
                       increment=5, bind_object=self, bind_attribute="delay"),
            Parms.Parm(name="delay_random", label="DLRND", default=self.delay_random,
                       parm_type=Parms.IntParmType, increment=5,
                       bind_object=self, bind_attribute="delay_random"),
        ]

    ### PROCESSING ###

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        current = self.transport.now
        delay = int(self.delay)
        # Store matching on/off delays so note length survives random delay.
        for i in range(note_ons.length):
            note = note_ons.notes[i]
            velocity = note_ons.velocity_at(i)
            note_delay = delay + utils.random_int(self.transport.midi_tick + i, self.delay_random, 0)
            self.scheduled_ons.append_value(note, velocity=velocity, time=ticks.ticks_add(current, note_delay))
            self.note_delays.append_value(note, time=note_delay)

        # Use the original note-on delay for its matching note-off when possible.
        for i in range(note_offs.length):
            note = note_offs.notes[i]
            note_delay = delay
            for n in range(self.note_delays.length):
                if self.note_delays.notes[n] == note:
                    note_delay = self.note_delays.times[n]
                    self.note_delays.remove_index(n)
                    break
            self.scheduled_offs.append_value(note, time=ticks.ticks_add(current, note_delay))

        # Emit due note-ons and remove them from the pending queue.
        self.scheduled_ons.pop_due(current, self.note_ons_out, self.popped_ids, include_velocity=True)

        # Emit due note-offs after ons so delayed retriggers stay ordered.
        self.scheduled_offs.pop_due(current, self.note_offs_out, self.popped_ids)

        return self.note_ons_out, self.note_offs_out

    ### CLEANUP ###

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.scheduled_ons.clear()
        self.scheduled_offs.clear()
        self.note_delays.clear()
        self.popped_ids.clear()

    def remove(self):
        super().remove()
        self.transport = None
        self.scheduled_ons = None
        self.scheduled_offs = None
        self.note_delays = None
        self.popped_ids = None
