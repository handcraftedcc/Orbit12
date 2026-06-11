from ..core.module import Module
from ..core import parms as Parms
from ..core.note_array import NoteOnArray, NoteOffArray
import adafruit_ticks as ticks


### RETRIGGER OPTIONS ###

MODE_OPTIONS = ("PRESS", "REL", "BOTH")
MODE_PRESS, MODE_RELEASE, MODE_BOTH = 0, 1, 2


### RETRIGGER MODULE ###

class Retrigger(Module):
    name = "retrigger"
    label = "RTRGR"
    version = 1

    def __init__(self, module_helper, slot_id):
        self.transport = module_helper.transport
        self.mode = MODE_PRESS
        self.threshold = 50
        self.last_event_time = None
        self.held_notes = NoteOnArray()
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()
        super().__init__(module_helper, slot_id)

    ### PARMS ###

    def create_main_parms(self):
        return [
            Parms.Parm(name="mode", label="MODE", default=self.mode,
                       parm_type=Parms.EnumParmType, options=MODE_OPTIONS,
                       bind_object=self, bind_attribute="mode"),
            Parms.Parm(name="threshold", label="THRSH", default=self.threshold,
                       parm_type=Parms.IntParmType, minmax=(0, 1000),
                       bind_object=self, bind_attribute="threshold"),
        ]

    ### HELD NOTES ###

    def add_held_note(self, note_ons, index):
        note = note_ons.notes[index]
        if self.held_notes.contains(note):
            self.held_notes.remove_value_first(note)
        self.held_notes.append_from(note_ons, index)

    def update_held_notes(self, note_ons, note_offs):
        for i in range(note_offs.length):
            self.held_notes.remove_value_first(note_offs.notes[i])
        for i in range(note_ons.length):
            self.add_held_note(note_ons, i)

    ### RETRIGGERING ###

    def threshold_ready(self):
        if self.threshold <= 0 or self.last_event_time is None:
            return True
        next_time = ticks.ticks_add(self.last_event_time, self.threshold)
        return not ticks.ticks_less(self.transport.now, next_time)

    def note_event(self, event):
        if not event:
            return False
        ready = self.threshold_ready()
        self.last_event_time = self.transport.now
        return ready

    def emit_retrigger(self):
        for i in range(self.held_notes.length):
            self.note_offs_out.append_value(self.held_notes.notes[i])
        self.note_ons_out.append_values(self.held_notes)

    ### PROCESSING ###

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        had_notes = self.held_notes.length > 0

        # Check if a full retrigger already happened
        if note_offs.length>0 and self.held_notes.length>0 and self.held_notes.length == note_offs.length:
            all_off = True
            for i in range(self.held_notes.length):
                note = self.held_notes.notes[i]
                all_off = note_offs.contains(note)
                if not all_off:
                    break
            if all_off:
                self.note_offs_out.append_values(note_offs)
                self.note_ons_out.append_values(note_ons)
                self.held_notes.length = 0
                return self.note_ons_out, self.note_offs_out

        # Check for triggers
        press_event = self.mode in (MODE_PRESS,MODE_BOTH) and note_ons.length > 0
        release_event = self.mode in (MODE_RELEASE, MODE_BOTH) and note_offs.length > 0

        self.update_held_notes(note_ons, note_offs)
        retrigger = False
        if press_event:
            ready = self.note_event(True)
            retrigger = had_notes and self.held_notes.length > 0 and ready
        elif release_event:
            ready = self.note_event(True)
            retrigger = self.held_notes.length > 0 and ready

        self.note_offs_out.append_values(note_offs)
        if retrigger:
            self.emit_retrigger()
        else:
            self.note_ons_out.append_values(note_ons)

        return self.note_ons_out, self.note_offs_out

    ### CLEANUP ###

    def stop(self):
        self.held_notes.clear()
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.last_event_time = None

    def remove(self):
        super().remove()
        self.transport = None
        self.held_notes = None
