from . import music as Music

'''
Parameter objects and edit/display behavior for module controls.
'''

### PARAMETER MODEL ###

class Parm:

    def __init__(
            self,
            name,
            label,
            parm_type,
            default,
            minmax = None,
            increment = 1,
            options = None,
            include_bars = False,
            include_rates = True,
            multiple_octaves = False,
            octave_range = (1,8),
            bind_object = None,
            bind_attribute = None,
            edit_callback_function = None,
            enter_callback_function = None,
            exit_callback_function = None
        ):

        self.name = name
        self.label = label
        self.type = parm_type
        self.value = default
        self.minmax = minmax
        self.jog_increment = increment
        self.options = options
        self.include_bars = include_bars
        self.include_rates = include_rates
        self.multiple_octaves = multiple_octaves
        self.octave_range = octave_range
        self.bind_object = bind_object
        self.bind_attribute = bind_attribute
        self.edit_callback_function = edit_callback_function
        self.enter_callback_function = enter_callback_function
        self.exit_callback_function = exit_callback_function

        self.display_value = self.get_display_value()

    def get_display_value(self):
        self.display_value = self.type.get_display_value(self)
        return self.display_value

    def get_actual_value(self):
        return self.type.get_actual_value(self)

    def edit(self,delta):
        self.value = self.type.edit(self,delta*self.jog_increment)
        self.display_value = self.get_display_value()
        if self.bind_object is not None and self.bind_attribute is not None:
            setattr(self.bind_object, self.bind_attribute, self.value)
        if self.edit_callback_function:
            self.edit_callback_function(self.value)
        return self.value, self.display_value

    def set_value(self,value):
        if self.minmax:
            value = min(max(value, self.minmax[0]),self.minmax[1])
        self.value = value
        self.display_value = self.get_display_value()
        if self.bind_object is not None and self.bind_attribute is not None:
            setattr(self.bind_object, self.bind_attribute, self.value)
        return self.value, self.display_value
    
    def enter(self):
        if self.enter_callback_function:
            self.enter_callback_function(self.value)
        return self.type.enter(self)

    def exit(self):
        if self.exit_callback_function:
            self.exit_callback_function(self.value)
        return self.type.exit(self)        


### PARAMETER TYPES ###

class ParmType:
    label = None
    name = None

    @classmethod
    def get_display_value(cls, parm):
        return parm.value

    @classmethod
    def get_actual_value(cls, parm):
        return parm.value

    @classmethod
    def edit(cls, parm, delta):
        new_value = None
        if parm.minmax:
            new_value = max(parm.minmax[0],min(parm.minmax[1],parm.value+delta))
        else:
            new_value = parm.value+delta
        if new_value: new_value = round(new_value,2)
        return new_value

    @classmethod
    def enter(cls,parm):
        return ParmEnterResult.STAY_IN_EDIT

    @classmethod
    def exit(cls,parm):
        pass

class ParmEnterResult:
    STAY_IN_EDIT = 0
    RETURN_TO_SELECTION = 1


## Simple Types ##

class IntParmType(ParmType):
    pass

class BooleanParmType(ParmType):
    @classmethod
    def get_display_value(cls, parm):
        if parm.value == 0:
            parm.display_value = "OFF"
        else:
            parm.display_value = "ON"
        return parm.display_value

    @classmethod
    def edit(cls, parm, delta):
        return (parm.value + delta) % 2

class FloatParmType(ParmType):
    pass

class PercentParmType(ParmType):
    @classmethod
    def get_display_value(cls, parm):
        parm.display_value = str(round(parm.value * 100)) + "%"
        return parm.display_value

class StringParmType(ParmType):
    pass

class ButtonParmType(ParmType):
    @classmethod
    def enter(cls, parm):
        return ParmEnterResult.RETURN_TO_SELECTION

    @classmethod
    def get_display_value(cls, parm):
        return ">"


## Selection Types ##

class EnumParmType(ParmType):
    @classmethod
    def get_display_value(cls, parm):
        return parm.options[parm.value]

    @classmethod
    def edit(cls, parm, delta):
        count = len(parm.options)
        if count == 0:
            return 0
        else:
            new_value = (parm.value + delta) % count
            parm.display_value = parm.options[new_value]
        return new_value

class PatternParmType(ParmType):
    @classmethod
    def get_display_value(cls, parm):
        return "PTN" + str(parm.value + 1)

    @classmethod
    def edit(cls, parm, delta):
        count = len(Music.patterns_bit)
        if count == 0:
            return 0
        return (parm.value + delta) % count


## Musical Types ##

class RateParmType(ParmType):
    rates_labels = Music.RATE_LABELS
    rates_midi_ticks = Music.RATE_MIDI_TICKS
    bar_count = 16
    len_rates = len(Music.RATE_MIDI_TICKS)

    @classmethod
    def edit(cls, parm, delta):
        count = 0
        if parm.include_bars:
            count+=cls.bar_count
        if parm.include_rates:
            count+=cls.len_rates
        if count == 0:
            return 0
        else:
            return (parm.value + delta) % count

    @classmethod
    def get_display_value(cls, parm):
        label = None

        # Rates and bars
        if parm.include_bars and parm.include_rates:
            if parm.value < cls.bar_count:
                label = str(16-parm.value) + "BAR"
            else:
                label = cls.rates_labels[parm.value-cls.bar_count]

        # Rates only
        elif parm.include_rates:
            label = cls.rates_labels[parm.value]

        # Bars only
        else:
            label = str(16 - parm.value)

        return label

    @classmethod
    def get_actual_value(cls, parm):
        rate = 0

        # Rates and bars
        if parm.include_bars and parm.include_rates:
            if parm.value < cls.bar_count:
                rate = (16-parm.value)*96
            else:
                rate = cls.rates_midi_ticks[parm.value-cls.bar_count]

        # Rates only
        elif parm.include_rates:
            rate = cls.rates_midi_ticks[parm.value]

        # Bars only
        else:
            rate = (16-parm.value)*96

        return rate

class NoteParmType(ParmType):
    notes = Music.NOTES

    @classmethod
    def edit(cls, parm, delta):
        if parm.multiple_octaves:
            if parm.minmax:
                return max(parm.minmax[0], min(parm.minmax[1], parm.value + delta))
            count = (parm.octave_range[1] - parm.octave_range[0]) * 12 + 1
            return max(0, min(count - 1, parm.value + delta))
        return max(0, min(11, parm.value + delta))

    @classmethod
    def get_display_value(cls, parm):
        label = None
        if parm.multiple_octaves:
            note_num = parm.value % 12
            octave_num = parm.value // 12 + parm.octave_range[0]
            label = cls.notes[note_num] + str(octave_num)
        else:
            note_num = parm.value
            label = cls.notes[note_num]

        return label
