from ..core.module import Module
from ..core import parms as Parms
import adafruit_ticks as ticks
from ..core import utils
from ..core.note_array import NoteOnArray,NoteOffArray

ORDER_LIST = (
    "ORD",
    "UP",
    "DOWN",
    "UPDWN",
    "RAND",
)

MODE_UP = ORDER_LIST.index("UP")
MODE_DOWN = ORDER_LIST.index("DOWN")
MODE_INORDER = ORDER_LIST.index("ORD")
MODE_ALTERNATE = ORDER_LIST.index("UPDWN")
MODE_RANDOM = ORDER_LIST.index("RAND")


STRUM_ARTICULATE_PATTERNS = (
    bytes((24, 16, 8, 4)),    # 1 ACCL - very wide start, tight finish
    bytes((4, 8, 16, 24)),    # 2 DECL - tight start, wide finish
    bytes((28, 4, 18, 5)),    # 4 RAKE - hard drag, snap, drag, snap
    bytes((4, 22, 5, 20)),    # 5 BNC - strong short/long bounce
    bytes((18, 5, 20, 6)),    # 6 SWNG - exaggerated lopsided roll
    bytes((32, 8, 5, 4)),     # 7 FLAM - huge first gap, rapid cascade
)

STRUM_ARTICULATE_LIST = (
    "EVEN",
    "RNDLTE",
    "RNDSTR",
    "ACCL",
    "DECL",
    "HUMN",
    "RAKE",
    "SWNG",
    "FLAM",
)

STRUM_CHORD_LENGTH = 5


def get_tilt_multiplier(strum_position, tilt, note_count=5):
    if note_count <= 1:
        return 1.0

    pos_norm = strum_position / (note_count - 1)

    if tilt < 0:
        return (1.0 + tilt) + (-tilt * pos_norm)
    else:
        return 1.0 - (tilt * pos_norm)

def get_tilt_velocity(velocity, strum_position, tilt, note_count=5):
    tilt_velocity = int(velocity * get_tilt_multiplier(strum_position, tilt, note_count))
    return max(0, min(127, tilt_velocity))


class Strum(Module):
    name = "strum"
    label = "STRUM"
    help_text = "CHORD STRUM GEN"
    version = 1
    def __init__(self, module_helper, slot_id):
        self.module_helper = module_helper
        self.transport = self.module_helper.transport

        # Setup Attribs
        self.note_register = NoteOnArray()
        self.note_ons_out = NoteOnArray()
        self.note_offs_out = NoteOffArray()

        self.amount = 30
        self.order = 0
        self.tilt = 0
        self.tilt_note_count = 3
        self.articulate = 0

        self.strum_active = False
        self.next_strum_scheduled = None
        self.strum_position = 0

        self.strum_count = 0

        super().__init__(module_helper, slot_id)

    def create_main_parms(self):
        parms = []
        # Strum amount
        amount_parm = Parms.Parm(name="amount", label="AMOUNT", default=self.amount, parm_type=Parms.IntParmType,
                               bind_object=self, bind_attribute="amount", help_text="STRUM AMOUNT MS", minmax = (0,1000000))
        parms.append(amount_parm)

        order_parm = Parms.Parm(name="order", label="ORDER", default=self.order, parm_type=Parms.EnumParmType,
                                 bind_object=self, bind_attribute="order", help_text="STRUM ORDER", options = ORDER_LIST,
                                minmax = (0,len(ORDER_LIST)))
        parms.append(order_parm)

        tilt_parm = Parms.Parm(name="tilt", label="TILT", default=self.tilt, parm_type=Parms.PercentParmType,
                                 bind_object=self, bind_attribute="tilt", help_text="TILT AMOUNT", minmax = (-1,1),
                               increment= 0.05)
        parms.append(tilt_parm)

        tilt_note_count_parm = Parms.Parm(name="tilt_note_count", label="COUNT", default=self.tilt_note_count, parm_type=Parms.IntParmType,
                               bind_object=self, bind_attribute="tilt_note_count", help_text="TILT NOTE COUNT", minmax=(1, 12),
                               increment=1)
        parms.append(tilt_note_count_parm)

        articulate_parm = Parms.Parm(name="articulate", label="ARTC", default=self.articulate, parm_type=Parms.EnumParmType,
                                bind_object=self, bind_attribute="articulate", help_text="ARTICULATE TIMING",
                                options= STRUM_ARTICULATE_LIST)
        parms.append(articulate_parm)




        return parms

    def process(self, note_ons, note_offs):
        self.note_ons_out.clear()
        self.note_offs_out.clear()

        for i in range(note_ons.length):
            note = note_ons.notes[i]
            if not self.note_register.contains(note):
                velocity = 127
                if note_ons.velocities:
                    velocity = note_ons.velocities[i]
                self.note_register.append_value(note, velocity = velocity)

        for i in range(note_offs.length):
            note = note_offs.notes[i]
            self.note_register.remove_value_first(note)
        self.note_offs_out.append_values(note_offs)

        if self.order in (MODE_UP, MODE_DOWN, MODE_ALTERNATE):
            self.note_register.sort_notes()
            if self.order == MODE_DOWN or (self.order == MODE_ALTERNATE and self.strum_count%2==0):
                self.note_register.reverse_notes()

        if not self.strum_active and self.note_register.length>0:
            self.strum_active = True
            self.next_strum_scheduled = self.transport.now

        if self.strum_active and self.note_register.length<1:
            self.strum_active = False
            self.strum_position = 0
            self.next_strum_scheduled = None
            self.strum_count += 1

        if (self.strum_active and self.note_register.length > 0 and
                ticks.ticks_diff(self.next_strum_scheduled, self.transport.now) < 0):
            # Emit next note
            n = 0
            if self.order == MODE_RANDOM:
                n = utils.random_int(self.strum_count,self.note_register.length-1,0)
            note = self.note_register.notes[n]
            velocity = get_tilt_velocity(self.note_register.velocities[n], self.strum_position, self.tilt, STRUM_CHORD_LENGTH)
            self.note_ons_out.append_value(note, velocity = velocity)
            # Remove from register
            self.note_register.remove_index(n)
            # Schedule next one
            articulate_modifier = 1
            if self.articulate == 1: #Random light
                articulate_modifier = utils.random_int(self.strum_count+25,4,1) + 10
            elif self.articulate == 2:
                articulate_modifier = utils.random_int(self.strum_count + 25, 9, 1) + 10
            elif self.articulate-3 < len(STRUM_ARTICULATE_PATTERNS):
                articulation_pattern = STRUM_ARTICULATE_PATTERNS[self.articulate-3]
                articulation_position = min(self.strum_position, len(articulation_pattern) - 1)
                articulate_modifier = articulation_pattern[articulation_position]


            strum_delta = int(self.amount * (articulate_modifier/10))
            self.next_strum_scheduled = ticks.ticks_add(self.next_strum_scheduled,strum_delta)
            self.strum_position = (self.strum_position + 1) % STRUM_CHORD_LENGTH



        return self.note_ons_out, self.note_offs_out

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.note_register.clear()

    def remove(self):
        super().remove()
        self.transport = None
        self.note_register = None
