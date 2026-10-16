from ..core import music
from ..core.module import Module
from ..core import parms as Parms
from ..core.note_array import NoteArray, NoteOnArray, NoteOffArray


### QUANTIZE MODULE ###

class Quantize(Module):
    name = "quantize"
    label = "QNT"
    version = 1

    def __init__(self, module_helper, slot_id):
        self.module_helper = module_helper
        self.transport = self.module_helper.transport
        self.rate_value = 8
        self.rate_ticks = music.RATE_MIDI_TICKS[self.rate_value]
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        self.pending_ons = NoteArray(velocities=True, times=True)
        self.scheduled_offs = NoteArray(times=True)
        self.note_delays = NoteArray(times=True)
        self.held_notes = NoteArray()
        self.popped_ids = NoteArray()
        super().__init__(module_helper, slot_id)

    ### PARMS ###

    def create_main_parms(self):
        return [Parms.Parm(name="rate", label="RATE", default=self.rate_value,
                           parm_type=Parms.RateParmType, edit_callback_function=self.set_rate)]

    def set_rate(self, value):
        self.rate_value = value
        self.rate_ticks = music.RATE_MIDI_TICKS[value]
        return self.rate_ticks

    ### PROCESSING ###

    def process(self, note_ons, note_offs):
        """
        Hold note-ons until the next grid tick and align note-offs to that delay.
        """
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        current_tick = self.transport.midi_tick
        target_tick = ((current_tick // self.rate_ticks) + 1) * self.rate_ticks
        note_delay = target_tick - current_tick

        # Incoming note-ons are queued or refreshed for the next grid tick.
        for i in range(note_ons.length):
            note = note_ons.notes[i]
            velocity = note_ons.velocity_at(i)

            if self.held_notes.contains(note):
                self.held_notes.remove_value_first(note)
                self.note_offs_out.append_value(note)

            for p in range(self.scheduled_offs.length - 1, -1, -1):
                if self.scheduled_offs.notes[p] == note:
                    self.scheduled_offs.remove_index(p)

            found = False
            for p in range(self.pending_ons.length):
                if self.pending_ons.notes[p] == note:
                    self.pending_ons.velocities[p] = velocity
                    self.pending_ons.times[p] = target_tick
                    found = True
                    break
            if found:
                found = False
                for d in range(self.note_delays.length):
                    if self.note_delays.notes[d] == note:
                        self.note_delays.times[d] = note_delay
                        found = True
                        break
                if not found:
                    self.note_delays.append_value(note, time=note_delay)
                continue

            if self.pending_ons.length >= self.pending_ons.max_length:
                old_note = self.pending_ons.notes[0]
                self.pending_ons.remove_index(0)
                for d in range(self.note_delays.length):
                    if self.note_delays.notes[d] == old_note:
                        self.note_delays.remove_index(d)
                        break
            self.pending_ons.append_value(note, velocity=velocity, time=target_tick)
            found = False
            for d in range(self.note_delays.length):
                if self.note_delays.notes[d] == note:
                    self.note_delays.times[d] = note_delay
                    found = True
                    break
            if not found:
                self.note_delays.append_value(note, time=note_delay)

        # Note-offs keep the same delay as their matching pending note-on.
        for i in range(note_offs.length):
            note = note_offs.notes[i]
            note_delay = target_tick - current_tick
            for d in range(self.note_delays.length):
                if self.note_delays.notes[d] == note:
                    note_delay = self.note_delays.times[d]
                    self.note_delays.remove_index(d)
                    break

            found = False
            off_tick = current_tick + note_delay + 1
            for p in range(self.scheduled_offs.length):
                if self.scheduled_offs.notes[p] == note:
                    self.scheduled_offs.times[p] = off_tick
                    found = True
                    break
            if not found:
                if self.scheduled_offs.length >= self.scheduled_offs.max_length:
                    self.note_offs_out.append_value(self.scheduled_offs.notes[0])
                    self.scheduled_offs.remove_index(0)
                self.scheduled_offs.append_value(note, time=off_tick)

        # Promote pending note-ons whose target tick has arrived.
        self.popped_ids.clear()
        for i in range(self.pending_ons.length):
            if self.pending_ons.times[i] <= current_tick:
                note = self.pending_ons.notes[i]
                if self.held_notes.contains(note):
                    self.held_notes.remove_value_first(note)
                    self.note_offs_out.append_value(note)
                elif self.held_notes.length >= self.held_notes.max_length:
                    self.note_offs_out.append_value(self.held_notes.notes[0])
                    self.held_notes.remove_index(0)
                self.held_notes.append_value(note)
                self.note_ons_out.append_value(note, velocity=self.pending_ons.velocity_at(i))
                self.popped_ids.append_value(i)
        self.pending_ons.remove_indexes(self.popped_ids)

        # Release held notes once their quantized off tick arrives.
        self.popped_ids.clear()
        for i in range(self.scheduled_offs.length):
            if self.scheduled_offs.times[i] <= current_tick:
                note = self.scheduled_offs.notes[i]
                if self.note_ons_out.contains(note):
                    continue
                if self.held_notes.contains(note):
                    self.held_notes.remove_value_first(note)
                    self.note_offs_out.append_value(note)
                self.popped_ids.append_value(i)
        self.scheduled_offs.remove_indexes(self.popped_ids)

        return self.note_ons_out, self.note_offs_out

    ### CLEANUP ###

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.pending_ons.clear()
        self.scheduled_offs.clear()
        self.note_delays.clear()
        self.held_notes.clear()
        self.popped_ids.clear()

    def remove(self):
        super().remove()
        self.transport = None
        self.pending_ons = None
        self.scheduled_offs = None
        self.note_delays = None
        self.held_notes = None
        self.popped_ids = None
