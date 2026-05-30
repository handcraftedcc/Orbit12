from ..core import music
from ..core.constants import POLYPHONY
from ..core.module import Module
from ..core import parms as Parms
import adafruit_ticks as ticks
from ..core import utils
from ..core.note_array import NoteArray,NoteOnArray,NoteOffArray

ORDER_LIST = (
    "ORD",
    "UP",
    "DOWN",
)

MODE_UP = ORDER_LIST.index("UP")
MODE_DOWN = ORDER_LIST.index("DOWN")
MODE_INORDER = ORDER_LIST.index("ORD")

STRUM_ARTICULATE_PATTERNS = (
    bytearray((10, 10, 10, 10)),  # 0 EVEN - steady spacing
    bytearray((14, 12, 9, 7)),    # 1 ACCL - starts wide, gets tighter
    bytearray((7, 9, 12, 14)),    # 2 DECL - starts tight, spreads out
    bytearray((8, 12, 9, 11)),    # 3 HUMN - lightly uneven natural timing
    bytearray((16, 7, 13, 8)),    # 4 RAKE - strong drag, uneven catch-up
    bytearray((6, 14, 7, 13)),    # 5 BNC - short/long alternating bounce
    bytearray((12, 8, 13, 9)),    # 6 SWNG - rolling lopsided feel
    bytearray((18, 9, 7, 6)),     # 7 FLAM - big first gap, then quick cascade
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
        self.articulate = 0

        self.strum_active = False
        self.next_strum_scheduled = None
        self.strum_position = 0

        super().__init__(module_helper, slot_id)

    def create_main_parms(self):
        parms = []
        # Strum amount
        amount_parm = Parms.Parm(name="amount", label="AMOUNT", default=self.amount, parm_type=Parms.IntParmType,
                               bind_object=self, bind_attribute="amount", help_text="ARP STEP RATE", minmax = (0,1000000))
        parms.append(amount_parm)

        order_parm = Parms.Parm(name="order", label="ORDER", default=self.order, parm_type=Parms.EnumParmType,
                                 bind_object=self, bind_attribute="order", help_text="STRUM ORDER", options = ORDER_LIST,
                                minmax = (0,len(ORDER_LIST)))
        parms.append(order_parm)

        tilt_parm = Parms.Parm(name="tilt", label="TILT", default=self.tilt, parm_type=Parms.PercentParmType,
                                 bind_object=self, bind_attribute="tilt", help_text="TILT AMOUNT", minmax = (-1,1),
                               increment= 0.1)
        parms.append(tilt_parm)

        articulate_parm = Parms.Parm(name="articulate", label="ARTCLT", default=self.articulate, parm_type=Parms.IntParmType,
                                bind_object=self, bind_attribute="articulate", help_text="ARTICULATE PATTERN",
                                minmax=(0, len(STRUM_ARTICULATE_PATTERNS)-1))
        parms.append(articulate_parm)




        return parms

    def process(self, note_ons: NoteOnArray, note_offs: NoteOffArray):
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

        if self.order == MODE_UP or self.order == MODE_DOWN:
            self.note_register.sort_notes()
            if self.order == MODE_DOWN:
                self.note_register.reverse_notes()

        if not self.strum_active and self.note_register.length>0:
            self.strum_active = True
            self.next_strum_scheduled = self.transport.now

        if self.strum_active and self.note_register.length<1:
            self.strum_active = False
            self.strum_position = 0
            self.next_strum_scheduled = None

        if (self.strum_active and self.note_register.length > 0 and
                ticks.ticks_diff(self.next_strum_scheduled, self.transport.now) < 0):
            # Emit next note
            note = self.note_register.notes[0]
            velocity = int(self.note_register.velocities[0] * get_tilt_multiplier(self.strum_position,self.tilt,STRUM_CHORD_LENGTH))
            self.note_ons_out.append_value(note, velocity = velocity)
            # Remove from register
            self.note_register.remove_index(0)
            # Schedule next one
            strum_delta = int(self.amount * (STRUM_ARTICULATE_PATTERNS[self.articulate][min(self.strum_position,STRUM_CHORD_LENGTH)]/10))
            self.next_strum_scheduled = ticks.ticks_add(self.next_strum_scheduled,strum_delta)
            self.strum_position += 1
            self.strum_position = self.strum_position % STRUM_CHORD_LENGTH

        return self.note_ons_out, self.note_offs_out

    def stop(self):
        self.note_ons_out.clear()
        self.note_offs_out.clear()
        self.note_register.clear()

    def remove(self):
        super().remove()
        self.transport = None
        self.note_register = None

