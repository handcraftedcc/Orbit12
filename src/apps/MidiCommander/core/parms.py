# Creates parm templates

class parmManager:
    def __init__(self):
        pass

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
            edit_callback_function = None,
            enter_callback_function = None,
            exit_callback_function = None
        ):

        self.name = name
        self.label = label
        self.type = parm_type
        self.value = default
        self.display_value = self.value
        self.minmax = minmax
        self.jog_increment = increment
        self.options = options
        self.include_bars = include_bars
        self.include_rates = include_rates
        self.multiple_octaves = multiple_octaves
        self.octave_range = octave_range
        self.edit_callback_function = edit_callback_function
        self.enter_callback_function = enter_callback_function
        self.exit_callback_function = exit_callback_function

    def edit(self,delta):
        self.value = self.type.edit(self,delta)
        return self.value
    
    def enter(self):
        return self.type.enter(self)

    def exit(self):
        return self.type.exit(self)        

class ParmType:
    label = None
    name = None
    value_type = None

    @classmethod
    def edit(cls, parm, delta):
        if parm.minmax:
            return max(parm.minmax[0],min(parm.minmax[1],parm.value+delta))
        else:
            return parm.value+delta
        
    def enter(cls,parm):
        return ParmEnterResult.STAY_IN_EDIT

    def exit(cls,parm):
        pass

class ParmEnterResult:
    STAY_IN_EDIT = 0
    RETURN_TO_SELECTION = 1

class ValueType:
    INT = 0
    FLOAT = 1
    STRING = 2
    NONE = 3

class IntParmType(ParmType):
    value_type = ValueType.INT

class FloatParmType(ParmType):
    value_type = ValueType.FLOAT

class PercentParmType(ParmType):
    value_type = ValueType.FLOAT

class StringParmType(ParmType):
    value_type = ValueType.STRING

class ButtonParmType(ParmType):
    value_type = ValueType.NONE

    @classmethod
    def enter(cls, parm):
        if parm.enter_callback_function is not None:
            parm.enter_callback_function()

        return ParmEnterResult.RETURN_TO_SELECTION



class EnumParmType(ParmType):
    value_type = ValueType.INT

    @classmethod
    def edit(cls, parm, delta):
        count = len(parm.options)
        if count == 0:
            return 0
        else:
            return (parm.value + delta) % count

class RateParmType(ParmType):
    value_type = ValueType.INT
    bars_labels = [None] * 16
    bars_values = [None] * 16 #16ths
    for bar in range(16):
        bars_labels[bar] = str(16-bar)+"bars"
        bars_values[bar] = 16*(16-bar)
    rates_labels = ["1/1","1/1T","1/2","1/2T","1/4","1/4T","1/8","1/8T","1/16","1/16T","1/32","1/32T"]
    rates_value = [16,16/3,8,8/3,4,4/3,2,2/3,1,1/3,0.5,0.5/3]
    len_bars = len(bars_labels)
    len_rates = len(rates_labels)

    @classmethod
    def edit(cls, parm, delta):
        count = 0
        if parm.include_bars:
            count+=cls.len_bars
        if parm.include_rates:
            count+=cls.len_rates
        if count == 0:
            return 0
        else:
            return (parm.value + delta) % count

class NoteParmType(ParmType):
    value_type = ValueType.INT
    notes = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

    @classmethod
    def get_range_labels(cls,parm): #TODO: Instead of getting all the labels in one big list just write one that just gets the current label instead.
        labels = []
        for octave in range(parm.octave_range[0],parm.octave_range[1]):
            for note in cls.notes:
                labels.append(note+str(octave))
        labels.append(cls.notes[0]+str(parm.octave_range[1]))

        return labels
        
    @classmethod
    def get_label(cls,parm):
        label = None
        if parm.multiple_octaves:
            notenum = parm.value%12
            octavenum = parm.value//12+parm.octave_range[0]
            label = cls.notes[notenum]+str(octavenum)
        else:
            notenum = parm.value
            label = cls.notes[notenum]
    	
        return label
    		

    @classmethod
    def edit(cls, parm, delta):
        count = None
        if parm.multiple_octaves:
            count = (parm.octave_range[1]-parm.octave_range[0])*12+1
        else:
            count = 12
        
        return max(0,min(count-1,(parm.value + delta)))


