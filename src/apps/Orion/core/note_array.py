# Utilities for Note Array Management
from .state import POLYPHONY
import random

### Main Class ###

class NoteArray:
    def __init__(self, length = POLYPHONY, velocities = False):
        self.notes = bytearray(length)
        if velocities: self.velocities = bytearray(length)
        else: self.velocities = None
        self.length = 0
        self.max_length = length

    ### Helper Functions ###
    
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

    def sort_notes(self):
        for i in range(1, self.length):
            key = self.notes[i]
            has_vel = self.velocities is not None
            if has_vel: vel = self.velocities[i]
            j = i - 1
            while j >= 0 and self.notes[j] > key:
                self.notes[j + 1] = self.notes[j]
                if has_vel:
                    self.velocities[j + 1] = self.velocities[j]
                j -= 1
            self.notes[j + 1] = key
            if has_vel: self.velocities[j+1] = vel
            
    def swap_notes(self, index_1: int, index_2: int):
        note = self.notes[index_1]
        self.notes[index_1] = self.notes[index_2]
        self.notes[index_2] = note
        if self.velocities is not None:
            vel = self.velocities[index_1]
            self.velocities[index_1] = self.velocities[index_2]
            self.velocities[index_2] = vel
            
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
        self.length = self.length - 1
        return True

    def notes_remove_value(self, value, first_only=False):
        i = 0
        while i < self.length:
            if self.notes[i] != value:
                i += 1
                continue
            self.notes_remove_index(i)
            if first_only:
                break

    def notes_add_value(self, value: int, velocity=127):
        if self.length >= self.max_length:
            return False
        self.notes[self.length] = value
        if self.velocities is not None: self.velocities[self.length] = velocity
        self.length += 1
        return True

    def notes_add_value_sorted(self, value: int, velocity=127, dedup=False):
        if self.length == self.max_length:
            return False
        added = False
        has_vel = self.velocities is not None
        for i in range(self.length):
            if value == self.notes[i] and dedup:
                return False
            if value < self.notes[i]:
                for j in range(self.length, i, -1):
                    self.notes[j] = self.notes[j - 1]
                    if has_vel: self.velocities[j] = self.velocities[j - 1]
                self.notes[i] = value
                if has_vel: self.velocities[i] = velocity
                added = True
                break
        if not added:
            self.notes[self.length] = value
            if has_vel: self.velocities[self.length] = velocity
        self.length += 1
        return True

class NoteOnArray(NoteArray):
    def __init__(self, length = POLYPHONY):
        super().__init__(length, velocities = True)

class NoteOffArray(NoteArray):
    def __init__(self, length = POLYPHONY):
        super().__init__(length)




