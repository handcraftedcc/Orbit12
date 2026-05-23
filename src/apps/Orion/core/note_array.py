# Utilities for Note Array Management
from .constants import POLYPHONY
import random
from array import array

### Main Class ###

class NoteArray:
    def __init__(self, length = POLYPHONY, velocities = False, times = False, channels = None):
        self.notes = bytearray(length)
        if velocities: self.velocities = bytearray(length)
        else: self.velocities = None
        if times: self.times = array('I',[0]*length)
        else: self.times = None
        if channels: self.channels = bytearray(length)
        else: self.channels = None
        self.length = 0
        self.max_length = length

    ### Helper Functions ###
    ## Reading ##
    def get(self, index):
        if index < 0 or index >= self.length:
            return None
        return self.notes[index]

    def contains(self, value, channel=None):
        for i in range(self.length):
            if self.notes[i] != value:
                continue
            if channel is None:
                return True
            if self.channels is not None and self.channels[i] == channel:
                return True
        return False

    def index_of(self, value, order=0, channel=None):
        def matches(i):
            if self.notes[i] != value:
                return False

            if channel is None:
                return True

            return self.channels is not None and self.channels[i] == channel

        if order == 0:
            for i in range(0, self.length):
                if matches(i):
                    return i
        else:
            for i in range(self.length - 1, -1, -1):
                if matches(i):
                    return i

        return None

    def get_min_note(self):
        if self.length == 0:
            return None
        min_note = self.notes[0]
        min_note_index = 0
        for i in range(1, self.length):
            if self.notes[i] < min_note:
                min_note = self.notes[i]
                min_note_index = i
        return min_note, min_note_index

    def get_max_note(self):
        if self.length == 0:
            return None
        max_note = self.notes[0]
        max_note_index = 0
        for i in range(1, self.length):
            if self.notes[i] > max_note:
                max_note = self.notes[i]
                max_note_index = i
        return max_note, max_note_index

    ## Writing ##

    def clear(self):
        self.length = 0

    def copy(self, note_array: "NoteArray"):
        copy_length = note_array.length
        if self.max_length < note_array.length:
            copy_length = self.max_length
        self.length = copy_length
        for i in range(copy_length):
            self.notes[i] = note_array.notes[i]
            if self.velocities is not None:
                if note_array.velocities is not None:
                    self.velocities[i] = note_array.velocities[i]
                else:
                    self.velocities[i] = 127
            if self.times is not None:
                if note_array.times is not None:
                    self.times[i] = note_array.times[i]
                else:
                    self.times[i] = 0
            if self.channels is not None:
                if note_array.channels is not None:
                    self.channels[i] = note_array.channels[i]
                else:
                    self.channels[i] = 0

    def sort_notes(self):
        for i in range(1, self.length):
            key = self.notes[i]
            has_vel = self.velocities is not None
            has_times = self.times is not None
            has_channels = self.channels is not None
            if has_vel: vel = self.velocities[i]
            if has_times: time = self.times[i]
            if has_channels: channel = self.channels[i]
            j = i - 1
            while j >= 0 and self.notes[j] > key:
                self.notes[j + 1] = self.notes[j]
                if has_vel: self.velocities[j + 1] = self.velocities[j]
                if has_times: self.times[j + 1] = self.times[j]
                if has_channels: self.channels[j + 1] = self.channels[j]
                j -= 1
            self.notes[j + 1] = key
            if has_vel: self.velocities[j+1] = vel
            if has_times: self.times[j + 1] = time
            if has_channels: self.channels[j + 1] = channel
            
    def swap_notes(self, index_1: int, index_2: int):
        note = self.notes[index_1]
        self.notes[index_1] = self.notes[index_2]
        self.notes[index_2] = note
        if self.velocities is not None:
            vel = self.velocities[index_1]
            self.velocities[index_1] = self.velocities[index_2]
            self.velocities[index_2] = vel
        if self.times is not None:
            vel = self.times[index_1]
            self.times[index_1] = self.times[index_2]
            self.times[index_2] = vel
        if self.channels is not None:
            channel = self.channels[index_1]
            self.channels[index_1] = self.channels[index_2]
            self.channels[index_2] = channel
            
    def reverse_notes(self):
        left = 0
        right = self.length - 1
        while left < right:
            self.swap_notes(left, right)
            left += 1
            right -= 1

    def randomize_notes(self, seed: int):
        random.seed(seed)
        for i in range(self.length - 1, 0, -1):
            rand = random.randint(0, i)
            self.swap_notes(rand, i)
            
    def remove_index(self, index: int):
        if index < 0 or index >= self.length:
            return False
        for i in range(index, self.length-1):
            self.notes[i] = self.notes[i + 1]
            if self.velocities is not None: self.velocities[i] = self.velocities[i + 1]
            if self.times is not None: self.times[i] = self.times[i + 1]
            if self.channels is not None: self.channels[i] = self.channels[i + 1]
        self.length = self.length - 1
        return True

    def remove_value_first(self, value, order=0, channel=None):
        def matches(i):
            if self.notes[i] != value:
                return False

            if channel is None:
                return True

            return self.channels is not None and self.channels[i] == channel

        if order == 1:
            i = self.length - 1
            while i >= 0:
                if matches(i):
                    self.remove_index(i)
                    return i
                i -= 1
        else:
            i = 0
            while i < self.length:
                if matches(i):
                    self.remove_index(i)
                    return i
                i += 1

        return None

    def remove_value_all(self, value, order=0, channel=None):
        def matches(i):
            if self.notes[i] != value:
                return False

            if channel is None:
                return True

            return self.channels is not None and self.channels[i] == channel

        if order == 1:
            i = self.length - 1
            while i >= 0:
                if matches(i):
                    self.remove_index(i)
                i -= 1
        else:
            i = 0
            while i < self.length:
                if matches(i):
                    self.remove_index(i)
                else:
                    i += 1

    def append_value(self, value: int, velocity=127, time = 0, channel = 0):
        if self.length >= self.max_length:
            return False
        self.notes[self.length] = value
        if self.velocities is not None: self.velocities[self.length] = velocity
        if self.times is not None: self.times[self.length] = time
        if self.channels is not None: self.channels[self.length] = channel
        self.length += 1
        return True

    def append_values(self, note_array: "NoteArray"):
        for idx in range(note_array.length):
            note = note_array.notes[idx]

            velocity = 127
            if note_array.velocities is not None:
                velocity = note_array.velocities[idx]

            time = 0
            if note_array.times is not None:
                time = note_array.times[idx]

            channel = 0
            if note_array.channels is not None:
                channel = note_array.channels[idx]

            if not self.append_value(note, velocity, time, channel):
                return False

        return True

    def insert_value_at_index(self, value, index: int, velocity = 127, time = 0, channel = 0):
        if self.length >= self.max_length:
            return False

        if index < 0 or index > self.length:
            return False

        has_vel = self.velocities is not None
        has_times = self.times is not None
        has_channels = self.channels is not None

        for i in range(self.length, index, -1):
            self.notes[i] = self.notes[i - 1]
            if has_vel:
                self.velocities[i] = self.velocities[i - 1]
            if has_times:
                self.times[i] = self.times[i - 1]
            if has_channels:
                self.channels[i] = self.channels[i - 1]

        self.notes[index] = value
        if has_vel:
            self.velocities[index] = velocity
        if has_times:
            self.times[index] = time
        if has_channels:
            self.channels[index] = channel

        self.length += 1
        return True

    def append_value_sorted(self, value: int, velocity=127, time=0, channel = 0, dedup=False):
        if self.length >= self.max_length:
            return False

        for i in range(self.length):
            if value == self.notes[i] and dedup:
                return False

            if value < self.notes[i]:
                return self.insert_value_at_index(value, i, velocity, time, channel)

        return self.append_value(value, velocity, time, channel)

class NoteOnArray(NoteArray):
    def __init__(self, length = POLYPHONY, channels = False):
        super().__init__(length, velocities = True, channels= channels)

class NoteOffArray(NoteArray):
    def __init__(self, length = POLYPHONY, channels = False):
        super().__init__(length, channels = channels)

class NoteRelationshipArray:
    def __init__(self, length = POLYPHONY):
        self.in_array = NoteArray(length)
        self.out_array = NoteArray(length)
        self.return_array = NoteArray(length)

    def has_in_note(self, in_note):
        for i in range(self.in_array.length):
            note = self.in_array.notes[i]
            if in_note == note:
                return True,i
        return False,None

    def has_out_note(self, out_note):
        for i in range(self.out_array.length):
            note = self.out_array.notes[i]
            if out_note == note:
                return True,i
        return False,None

    def add_note(self, in_note, out_note):
        if self.in_array.length >= self.in_array.max_length:
            return False
        self.in_array.append_value(in_note)
        self.out_array.append_value(out_note)
        return True

    def replace_note_in_index(self, in_note, index):
        self.in_array.notes[index] = in_note

    def remove_note_single(self, in_note, first_in_first_out=True):
        index = self.in_array.remove_value_first(in_note, order= not first_in_first_out)
        if index is None:
            return None
        out_note = self.out_array.notes[index]
        self.out_array.remove_index(index)
        return out_note

    def remove_note_all(self, in_note):
        self.return_array.clear()
        i = 0
        while i < self.in_array.length:
            if self.in_array.notes[i] == in_note:
                self.return_array.append_value(self.out_array.notes[i])
                self.in_array.remove_index(i)
                self.out_array.remove_index(i)
            else:
                i += 1
        return self.return_array

    def clear(self):
        self.in_array.clear()
        self.out_array.clear()
        self.return_array.clear()

