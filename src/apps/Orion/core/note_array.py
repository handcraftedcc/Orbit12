from .constants import POLYPHONY
from array import array
import adafruit_ticks as ticks

'''
Fixed-size note containers for CircuitPython memory control.
'''

### NOTE ARRAYS ###

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

    ## Reading ##

    def contains(self, value, channel=None):
        for i in range(self.length):
            if self.notes[i] != value:
                continue
            if channel is None:
                return True
            if self.channels is not None and self.channels[i] == channel:
                return True
        return False

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

    def velocity_at(self, index, default=127):
        if self.velocities is None:
            return default
        return self.velocities[index]

    def time_at(self, index, default=None):
        if self.times is None:
            return default
        return self.times[index]

    def channel_at(self, index, default=0):
        if self.channels is None:
            return default
        return self.channels[index]

    def get_value(self, index, default_velocity=127, default_time=None, default_channel=0):
        return (
            self.notes[index],
            self.velocity_at(index, default_velocity),
            self.time_at(index, default_time),
            self.channel_at(index, default_channel),
        )

    ## Writing ##

    def clear(self):
        self.length = 0

    def set_value_at(self, index, value, velocity=127, time=0, channel=0):
        self.notes[index] = value
        if self.velocities is not None: self.velocities[index] = velocity
        if self.times is not None: self.times[index] = time
        if self.channels is not None: self.channels[index] = channel

    def copy_index(self, to_index, from_index):
        self.notes[to_index] = self.notes[from_index]
        if self.velocities is not None: self.velocities[to_index] = self.velocities[from_index]
        if self.times is not None: self.times[to_index] = self.times[from_index]
        if self.channels is not None: self.channels[to_index] = self.channels[from_index]

    def sort_notes(self):
        # Insertion sort keeps optional velocity/time/channel arrays aligned.
        for i in range(1, self.length):
            key, velocity, time, channel = self.get_value(i, default_time=0)
            j = i - 1
            while j >= 0 and self.notes[j] > key:
                self.copy_index(j + 1, j)
                j -= 1
            self.set_value_at(j + 1, key, velocity, time, channel)
            
    def swap_notes(self, index_1, index_2):
        note_1, velocity_1, time_1, channel_1 = self.get_value(index_1, default_time=0)
        note_2, velocity_2, time_2, channel_2 = self.get_value(index_2, default_time=0)
        self.set_value_at(index_1, note_2, velocity_2, time_2, channel_2)
        self.set_value_at(index_2, note_1, velocity_1, time_1, channel_1)
            
    def reverse_notes(self):
        left = 0
        right = self.length - 1
        while left < right:
            self.swap_notes(left, right)
            left += 1
            right -= 1
            
    def remove_index(self, index):
        if index < 0 or index >= self.length:
            return False
        # Shift everything after index left by one slot.
        for i in range(index, self.length-1):
            self.copy_index(i, i + 1)
        self.length = self.length - 1
        return True

    def remove_indexes(self, indexes):
        for i in range(indexes.length - 1, -1, -1):
            self.remove_index(indexes.notes[i])

    def remove_value_first(self, value, order=0, channel=None):
        notes = self.notes
        channels = self.channels
        match_any_channel = channel is None
        if order == 1:
            i = self.length - 1
            # Reverse scan removes the most recent matching note.
            while i >= 0:
                if notes[i] == value and (match_any_channel or (channels is not None and channels[i] == channel)):
                    self.remove_index(i)
                    return i
                i -= 1
        else:
            i = 0
            # Forward scan removes the oldest matching note.
            while i < self.length:
                if notes[i] == value and (match_any_channel or (channels is not None and channels[i] == channel)):
                    self.remove_index(i)
                    return i
                i += 1

        return None

    def append_value(self, value, velocity=127, time = 0, channel = 0):
        if self.length >= self.max_length:
            return False
        self.set_value_at(self.length, value, velocity, time, channel)
        self.length += 1
        return True

    def append_values(self, note_array):
        # Copy only active entries, preserving optional metadata when present.
        for idx in range(note_array.length):
            if not self.append_from(note_array, idx):
                return False

        return True

    def append_from(self, note_array, index, default_velocity=127, default_time=0, default_channel=0):
        return self.append_value(
            note_array.notes[index],
            note_array.velocity_at(index, default_velocity),
            note_array.time_at(index, default_time),
            note_array.channel_at(index, default_channel),
        )

    def pop_due(self, current, out_array, popped_ids, include_velocity=False):
        popped_ids.clear()
        if self.times is None:
            return

        for i in range(self.length):
            if ticks.ticks_less(self.times[i], current):
                if include_velocity:
                    out_array.append_value(self.notes[i], velocity=self.velocity_at(i))
                else:
                    out_array.append_value(self.notes[i])
                popped_ids.append_value(i)

        self.remove_indexes(popped_ids)

    def insert_value_at_index(self, value, index, velocity = 127, time = 0, channel = 0):
        if self.length >= self.max_length:
            return False

        if index < 0 or index > self.length:
            return False

        # Shift active entries right to create an insertion slot.
        for i in range(self.length, index, -1):
            self.copy_index(i, i - 1)

        self.set_value_at(index, value, velocity, time, channel)
        self.length += 1
        return True

class NoteOnArray(NoteArray):
    def __init__(self, length = POLYPHONY, channels = False):
        super().__init__(length, velocities = True, channels= channels)

class NoteOffArray(NoteArray):
    def __init__(self, length = POLYPHONY, channels = False):
        super().__init__(length, channels = channels)


### NOTE RELATIONSHIPS ###

class NoteRelationshipArray:

    def __init__(self, length = POLYPHONY):
        self.in_notes = bytearray(length)
        self.out_notes = bytearray(length)
        self.return_notes = bytearray(length)
        self.length = 0
        self.return_length = 0
        self.max_length = length

    def has_out_note(self, out_note):
        for i in range(self.length):
            note = self.out_notes[i]
            if out_note == note:
                return True,i
        return False,None

    def add_note(self, in_note, out_note):
        if self.length >= self.max_length:
            return False
        self.in_notes[self.length] = in_note
        self.out_notes[self.length] = out_note
        self.length += 1
        return True

    def remove_index(self, index):
        if index < 0 or index >= self.length:
            return False
        for i in range(index, self.length - 1):
            self.in_notes[i] = self.in_notes[i + 1]
            self.out_notes[i] = self.out_notes[i + 1]
        self.length -= 1
        return True

    def remove_note_single(self, in_note, first_in_first_out=True):
        if first_in_first_out:
            i = 0
            # Return the oldest generated note for this input note.
            while i < self.length:
                if self.in_notes[i] == in_note:
                    out_note = self.out_notes[i]
                    self.remove_index(i)
                    return out_note
                i += 1
        else:
            i = self.length - 1
            # Return the newest generated note for this input note.
            while i >= 0:
                if self.in_notes[i] == in_note:
                    out_note = self.out_notes[i]
                    self.remove_index(i)
                    return out_note
                i -= 1
        return None

    def remove_note_all(self, in_note):
        self.return_length = 0
        i = 0
        # Collect all mapped output notes while compacting the relationship list.
        while i < self.length:
            if self.in_notes[i] == in_note:
                self.return_notes[self.return_length] = self.out_notes[i]
                self.return_length += 1
                self.remove_index(i)
            else:
                i += 1
        return self

    def clear(self):
        self.length = 0
        self.return_length = 0
