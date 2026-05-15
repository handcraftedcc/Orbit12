# Utilities for Note Array Management
from .state import POLYPHONY
import random
from array import array

### Main Class ###

class NoteArray:
    def __init__(self, length = POLYPHONY, velocities = False, times = False):
        self.notes = bytearray(length)
        if velocities: self.velocities = bytearray(length)
        else: self.velocities = None
        if times: self.times = array('H',[0]*length)
        else: self.times = None
        self.length = 0
        self.max_length = length

    ### Helper Functions ###
    def contains(self, value):
        for i in range(self.length):
            if self.notes[i] == value:
                return True
        return False

    def index_of(self, value, order=0):
        if order == 0:
            for i in range(0, self.length):
                if self.notes[i] == value:
                    return i
        else:
            for i in range(self.length - 1,-1,-1):
                if self.notes[i] == value:
                    return i
        return None

    def get(self, index):
        if index < 0 or index >= self.length:
            return None
        return self.notes[index]
    
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
                    self.times[i] = 127

    def sort_notes(self):
        for i in range(1, self.length):
            key = self.notes[i]
            has_vel = self.velocities is not None
            has_times = self.times is not None
            if has_vel: vel = self.velocities[i]
            if has_times: time = self.times[i]
            j = i - 1
            while j >= 0 and self.notes[j] > key:
                self.notes[j + 1] = self.notes[j]
                if has_vel: self.velocities[j + 1] = self.velocities[j]
                if has_times: self.times[j + 1] = self.times[j]
                j -= 1
            self.notes[j + 1] = key
            if has_vel: self.velocities[j+1] = vel
            if has_times: self.times[j + 1] = time
            
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
            
    def notes_remove_index(self, index: int):
        if index < 0 or index >= self.length:
            return False
        for i in range(index, self.length-1):
            self.notes[i] = self.notes[i + 1]
            if self.velocities is not None: self.velocities[i] = self.velocities[i + 1]
            if self.times is not None: self.times[i] = self.times[i + 1]
        self.length = self.length - 1
        return True

    def notes_remove_value_first(self, value, order=0):
        if order == 1:
            i = self.length - 1
            while i >= 0:
                if self.notes[i] == value:
                    self.notes_remove_index(i)
                    return i
                i -= 1
        else:
            i = 0
            while i < self.length:
                if self.notes[i] == value:
                    self.notes_remove_index(i)
                    return i
                else:
                    i += 1
        return None

    def notes_remove_value_all(self, value, order=0):
        if order == 1:
            i = self.length - 1
            while i >= 0:
                if self.notes[i] == value:
                    self.notes_remove_index(i)
                i -= 1
        else:
            i = 0
            while i < self.length:
                if self.notes[i] == value:
                    self.notes_remove_index(i)
                else:
                    i += 1

    def notes_append_value(self, value: int, velocity=127, time = 0):
        if self.length >= self.max_length:
            return False
        self.notes[self.length] = value
        if self.velocities is not None: self.velocities[self.length] = velocity
        if self.times is not None: self.times[self.length] = time
        self.length += 1
        return True

    def notes_append_value_sorted(self, value: int, velocity=127, time=0, dedup=False):
        if self.length >= self.max_length:
            return False
        added = False
        has_vel = self.velocities is not None
        has_times = self.times is not None
        for i in range(self.length):
            if value == self.notes[i] and dedup:
                return False
            if value < self.notes[i]:
                for j in range(self.length, i, -1):
                    self.notes[j] = self.notes[j - 1]
                    if has_vel: self.velocities[j] = self.velocities[j - 1]
                    if has_times: self.times[j] = self.times[j - 1]
                self.notes[i] = value
                if has_vel: self.velocities[i] = velocity
                if has_times: self.times[i] = time
                added = True
                break
        if not added:
            self.notes[self.length] = value
            if has_vel: self.velocities[self.length] = velocity
            if has_vel: self.times[self.length] = time
        self.length += 1
        return True

class NoteOnArray(NoteArray):
    def __init__(self, length = POLYPHONY):
        super().__init__(length, velocities = True)

class NoteOffArray(NoteArray):
    def __init__(self, length = POLYPHONY):
        super().__init__(length)

class NoteRelationshipArray:
    def __init__(self, length = POLYPHONY):
        self.in_array = NoteArray(length)
        self.out_array = NoteArray(length)
        self.return_array = NoteArray(length)

    def add_note(self, in_note, out_note):
        if self.in_array.length >= self.in_array.max_length:
            return False

        self.in_array.notes_append_value(in_note)
        self.out_array.notes_append_value(out_note)
        return True

    def remove_note_single(self, in_note, first_in_first_out=True):
        index = self.in_array.notes_remove_value_first(in_note,order= not first_in_first_out)
        if index is None:
            return None
        out_note = self.out_array.notes[index]
        self.out_array.notes_remove_index(index)
        return out_note


    def remove_note_all(self, in_note):
        self.return_array.length = 0
        i = 0
        while i < self.in_array.length:
            if self.in_array.notes[i] == in_note:
                self.return_array.notes_append_value(self.out_array.notes[i])
                self.in_array.notes_remove_index(i)
                self.out_array.notes_remove_index(i)
            else:
                i += 1
        return self.return_array


