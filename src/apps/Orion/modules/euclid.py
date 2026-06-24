from ..core import music, utils
from ..core.module import Module
from ..core import parms as Parms
from ..core.note_array import NoteOnArray, NoteOffArray
from array import array
import adafruit_ticks as ticks


### EUCLID OPTIONS ###

LANES = 6
NO_NOTE = 255

SORT_OPTIONS = (
    "UP",
    "DOWN",
    "ORD",
)

RETRIGGER_OPTIONS = (
    "STBL",
    "CONT",
    "RTRG",
)

RETRIGGER_STABLE = 0
RETRIGGER_RETRIGGER = 2


def euclid_step(step, pulses, steps):
    if steps <= 0 or pulses <= 0:
        return False
    if pulses >= steps:
        return True
    return ((step + 1) * pulses) // steps != (step * pulses) // steps


### EUCLID MODULE ###

class Euclid(Module):
    name = "euclid"
    label = "EUCLID"
    version = 1
    save_attrs = ("pulses", "lengths", "rotations")

    def __init__(self, module_helper, slot_id):
        self.module_helper = module_helper
        self.transport = self.module_helper.transport

        self.lane = 0
        self.rate_value = 8
        self.rate_ticks = music.RATE_MIDI_TICKS[self.rate_value]
        self.sort_mode = 0
        self.retrigger_mode = RETRIGGER_STABLE
        self.gate = 100

        self.pulses = bytearray((4,) * LANES)
        self.lengths = bytearray((16,) * LANES)
        self.rotations = [0] * LANES

        self.held_notes = NoteOnArray()
        self.note_order = NoteOnArray()
        self.active_notes = bytearray((NO_NOTE,) * LANES)
        self.active_times = array('I', [0] * LANES)
        self.note_ons_out = NoteOnArray(length=LANES)
        self.note_offs_out = NoteOffArray(length=LANES)

        self.step_index = 0
        self.last_grid_bin = -1
        self.was_transport_running = self.transport.running

        super().__init__(module_helper, slot_id)

    def ensure_lane_state(self):
        if len(self.pulses) != LANES:
            self.pulses = bytearray(list(self.pulses[:LANES]) + [4] * max(0, LANES - len(self.pulses)))
        if len(self.lengths) != LANES:
            self.lengths = bytearray(list(self.lengths[:LANES]) + [16] * max(0, LANES - len(self.lengths)))
        if len(self.rotations) != LANES:
            self.rotations = list(self.rotations[:LANES]) + [0] * max(0, LANES - len(self.rotations))

    ### PARMS ###

    def create_main_parms(self):
        self.ensure_lane_state()
        return [
            Parms.Parm(name="lane", label="LANE", default=self.lane,
                       parm_type=Parms.IntParmType, minmax=(0, LANES - 1),
                       edit_callback_function=self.set_lane),
            Parms.Parm(name="rate", label="RATE", default=self.rate_value,
                       parm_type=Parms.RateParmType,
                       edit_callback_function=self.set_rate),
            Parms.Parm(name="pulses", label="PULSES", default=self.pulses[self.lane],
                       parm_type=Parms.IntParmType, minmax=(0, 128),
                       edit_callback_function=self.set_pulses),
            Parms.Parm(name="length", label="LENGTH", default=self.lengths[self.lane],
                       parm_type=Parms.IntParmType, minmax=(1, 128),
                       edit_callback_function=self.set_length),
            Parms.Parm(name="rotation", label="ROT", default=self.rotations[self.lane],
                       parm_type=Parms.IntParmType, minmax=(-127, 127),
                       edit_callback_function=self.set_rotation),
            Parms.Parm(name="sort", label="SORT", default=self.sort_mode,
                       parm_type=Parms.EnumParmType, options=SORT_OPTIONS,
                       bind_object=self, bind_attribute="sort_mode"),
            Parms.Parm(name="gate", label="GATE", default=self.gate,
                       parm_type=Parms.FloatParmType, increment=5, minmax=(0, 100000),
                       bind_object=self, bind_attribute="gate"),
            Parms.Parm(name="retrigger_mode", label="RTRG", default=self.retrigger_mode,
                       parm_type=Parms.EnumParmType, options=RETRIGGER_OPTIONS,
                       bind_object=self, bind_attribute="retrigger_mode"),
        ]

    def set_lane(self, value):
        self.ensure_lane_state()
        self.lane = value
        self.get_parm_by_name("pulses").set_value(self.pulses[value])
        self.get_parm_by_name("length").set_value(self.lengths[value])
        self.get_parm_by_name("rotation").set_value(self.rotations[value])

        self.queue_parm_rebuild()
        return value

    def set_rate(self, value):
        self.rate_value = value
        self.rate_ticks = music.RATE_MIDI_TICKS[value]
        return self.rate_ticks

    def set_pulses(self, value):
        self.ensure_lane_state()
        self.pulses[self.lane] = value
        return value

    def set_length(self, value):
        self.ensure_lane_state()
        self.lengths[self.lane] = value
        if self.pulses[self.lane] > value:
            self.pulses[self.lane] = value
            self.get_parm_by_name("pulses").set_value(value)
        return value

    def set_rotation(self, value):
        self.ensure_lane_state()
        self.rotations[self.lane] = value
        return value

    ### NOTE STATE ###

    def update_held_notes(self, note_ons, note_offs):
        had_notes = self.held_notes.length > 0

        for i in range(note_offs.length):
            note = note_offs.notes[i]
            self.held_notes.remove_value_first(note)
            for lane in range(LANES):
                if self.active_notes[lane] == note:
                    self.note_offs_out.append_value(note)
                    self.active_notes[lane] = NO_NOTE

        for i in range(note_ons.length):
            note = note_ons.notes[i]
            if self.held_notes.contains(note):
                self.held_notes.remove_value_first(note)
            self.held_notes.append_from(note_ons, i)

        if note_ons.length > 0 and not had_notes and self.retrigger_mode == RETRIGGER_RETRIGGER:
            self.step_index = 0
            self.last_grid_bin = -1

    def build_note_order(self):
        self.note_order.clear()
        self.note_order.append_values(self.held_notes)
        if self.sort_mode == 0:
            self.note_order.sort_notes()
        elif self.sort_mode == 1:
            self.note_order.sort_notes()
            self.note_order.reverse_notes()

    ### SEQUENCING ###

    def process_lane(self, lane, step):
        length = self.lengths[lane]
        pulses = min(self.pulses[lane], length)
        lane_step = (step + self.rotations[lane]) % length
        active_note = self.active_notes[lane]
        hit = euclid_step(lane_step, pulses, length)

        if lane >= self.note_order.length:
            if active_note != NO_NOTE:
                self.note_offs_out.append_value(active_note)
                self.active_notes[lane] = NO_NOTE
            return
        if not hit:
            return

        note = self.note_order.notes[lane]
        if active_note != NO_NOTE:
            self.note_offs_out.append_value(active_note)

        self.active_notes[lane] = note
        self.active_times[lane] = ticks.ticks_add(self.transport.now, int(self.gate))
        self.note_ons_out.append_value(note, velocity=self.note_order.velocity_at(lane))

    ### PROCESSING ###

    def process(self, note_ons, note_offs):
        self.ensure_lane_state()
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        running = self.transport.running

        for lane in range(LANES):
            note = self.active_notes[lane]
            if note != NO_NOTE and ticks.ticks_less(self.active_times[lane], self.transport.now):
                self.note_offs_out.append_value(note)
                self.active_notes[lane] = NO_NOTE

        self.update_held_notes(note_ons, note_offs)

        if running and not self.was_transport_running:
            self.last_grid_bin = -1
            if self.retrigger_mode == RETRIGGER_RETRIGGER:
                self.step_index = 0

        if running and self.held_notes.length > 0:
            self.build_note_order()
            self.last_grid_bin, changed, _ = utils.grid_change(
                self.transport.midi_tick, self.rate_ticks, self.last_grid_bin
            )
            if changed:
                if self.retrigger_mode == RETRIGGER_STABLE:
                    step = self.last_grid_bin
                else:
                    step = self.step_index
                    self.step_index += 1

                for lane in range(LANES):
                    self.process_lane(lane, step)
        else:
            self.stop_active_notes()

        self.was_transport_running = running
        return self.note_ons_out, self.note_offs_out

    ### CLEANUP ###

    def stop_active_notes(self):
        for lane in range(LANES):
            note = self.active_notes[lane]
            if note != NO_NOTE:
                self.note_offs_out.append_value(note)
                self.active_notes[lane] = NO_NOTE

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.held_notes.clear()
        self.note_order.clear()
        self.active_notes = bytearray((NO_NOTE,) * LANES)
        self.active_times = array('I', [0] * LANES)
        self.step_index = 0
        self.last_grid_bin = -1
        self.was_transport_running = False

    def remove(self):
        super().remove()
        self.transport = None
        self.held_notes = None
        self.note_order = None
        self.active_notes = None
        self.active_times = None
