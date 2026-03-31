"""Orbit12 UI parameter models."""

from orbit12ui.conditions import evaluate_condition

NOTE_NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
STRING_SYMBOLS = ("✓", "←", "✗") + tuple(chr(v) for v in range(ord("A"), ord("Z") + 1)) + tuple(
    str(n) for n in range(10)
)


def _clamp(value, min_value, max_value):
    return min(max(value, min_value), max_value)


class BaseParameter:
    """Base parameter behavior shared by all parameter types."""

    type_name = "base"

    def __init__(self, spec):
        self.spec = spec
        self.type_name = spec.get("type", self.type_name)
        self.name = spec["name"]
        self.label = spec.get("label") or self.name
        self.viscondition = spec.get("viscondition")
        self.is_editing = False
        # Avoid triggering subclass property setters during base init.
        self.__dict__["value"] = spec.get("default")

    def visible(self, context):
        return evaluate_condition(self.viscondition, context)

    def start_edit(self):
        self.is_editing = True

    def stop_edit(self):
        self.is_editing = False

    def rotate(self, delta):
        return False

    def press(self):
        if self.is_editing:
            self.stop_edit()
            return True
        self.start_edit()
        return False

    def display_value(self):
        return "" if self.value is None else str(self.value)


class IntParameter(BaseParameter):
    type_name = "int"

    def __init__(self, spec):
        super().__init__(spec)
        self.min_value = int(spec.get("min", 0))
        self.max_value = int(spec.get("max", 127))
        self.step = int(spec.get("step", 1))
        default = spec.get("default", self.min_value)
        self.value = int(_clamp(int(default), self.min_value, self.max_value))

    def rotate(self, delta):
        if not self.is_editing or delta == 0:
            return False
        self.value = int(_clamp(self.value + (self.step * delta), self.min_value, self.max_value))
        return True


class FloatParameter(BaseParameter):
    type_name = "float"

    def __init__(self, spec):
        super().__init__(spec)
        self.min_value = float(spec.get("min", 0.0))
        self.max_value = float(spec.get("max", 1.0))
        self.step = float(spec.get("step", 0.01))
        self.decimals = int(spec.get("decimals", 2))
        self.mode = spec.get("mode", "default")
        default = float(spec.get("default", self.min_value))
        self.value = float(_clamp(default, self.min_value, self.max_value))

    def rotate(self, delta):
        if not self.is_editing or delta == 0:
            return False
        self.value = float(_clamp(self.value + (self.step * delta), self.min_value, self.max_value))
        return True

    def display_value(self):
        if self.mode == "percent":
            return ("{0:." + str(self.decimals) + "f}%").format(self.value * 100.0)
        return ("{0:." + str(self.decimals) + "f}").format(self.value)


class BooleanParameter(BaseParameter):
    type_name = "boolean"

    def __init__(self, spec):
        super().__init__(spec)
        self.mode = spec.get("mode", "truefalse")
        self.value = bool(spec.get("default", False))

    def rotate(self, delta):
        if not self.is_editing or delta == 0:
            return False
        if abs(delta) % 2 == 1:
            self.value = not self.value
        return True

    def display_value(self):
        if self.mode == "onoff":
            return "On" if self.value else "Off"
        return "True" if self.value else "False"


class EnumParameter(BaseParameter):
    type_name = "enum"

    def __init__(self, spec):
        super().__init__(spec)
        self.items = list(spec.get("items", []))
        if not self.items:
            self.items = [""]
        self.wrap = bool(spec.get("wrap", True))

        default = spec.get("default", 0)
        if isinstance(default, int):
            self.index = int(_clamp(default, 0, len(self.items) - 1))
        elif default in self.items:
            self.index = self.items.index(default)
        else:
            self.index = 0

    @property
    def value(self):
        return self.items[self.index]

    @value.setter
    def value(self, new_value):
        if new_value in self.items:
            self.index = self.items.index(new_value)

    def rotate(self, delta):
        if not self.is_editing or delta == 0:
            return False

        new_index = self.index + delta
        if self.wrap:
            self.index = new_index % len(self.items)
        else:
            self.index = int(_clamp(new_index, 0, len(self.items) - 1))
        return True


class StringParameter(BaseParameter):
    type_name = "string"

    def __init__(self, spec):
        super().__init__(spec)
        self.value = str(spec.get("default", ""))
        self._original = self.value
        self._buffer = self.value
        self._symbols = STRING_SYMBOLS
        self._symbol_index = self._symbols.index("A")

    def start_edit(self):
        self.is_editing = True
        self._original = self.value
        self._buffer = self.value
        self._symbol_index = self._symbols.index("A")

    def current_symbol(self):
        return self._symbols[self._symbol_index]

    def rotate(self, delta):
        if not self.is_editing or delta == 0:
            return False
        self._symbol_index = (self._symbol_index + delta) % len(self._symbols)
        return True

    def press(self):
        if not self.is_editing:
            self.start_edit()
            return False

        symbol = self.current_symbol()
        if symbol == "✓":
            self.value = self._buffer
            self.stop_edit()
            return True

        if symbol == "←":
            self._buffer = self._buffer[:-1]
            self.value = self._buffer
            return False

        if symbol == "✗":
            self._buffer = self._original
            self.value = self._original
            self.stop_edit()
            return True

        self._buffer += symbol
        self.value = self._buffer
        self._symbol_index = self._symbols.index("A")
        return False

    def display_value(self):
        if self.is_editing:
            return self._buffer + "[" + self.current_symbol() + "]"
        return self.value


class RateParameter(EnumParameter):
    type_name = "rate"

    def __init__(self, spec):
        include_bars = bool(spec.get("includeBars", True))
        include_triplets = bool(spec.get("includeTriplets", True))

        items = []
        if include_bars:
            for bars in range(16, 1, -1):
                items.append(str(bars) + " bars")

        for denom in (1, 2, 4, 8, 16, 32):
            base = "1/" + str(denom)
            items.append(base)
            if include_triplets:
                items.append(base + "T")

        new_spec = dict(spec)
        new_spec["items"] = items
        if "default" not in new_spec:
            new_spec["default"] = 0
        super().__init__(new_spec)


class NoteParameter(EnumParameter):
    type_name = "note"

    def __init__(self, spec):
        include_octaves = bool(spec.get("includeOctaves", True))
        items = []
        if include_octaves:
            octave_range = spec.get("octaveRange", [0, 8])
            low = int(octave_range[0])
            high = int(octave_range[1])
            if high < low:
                low, high = high, low
            for octave in range(low, high + 1):
                for note_name in NOTE_NAMES:
                    items.append(note_name + str(octave))
        else:
            items = list(NOTE_NAMES)

        new_spec = dict(spec)
        new_spec["items"] = items
        if "default" not in new_spec:
            new_spec["default"] = 0
        super().__init__(new_spec)


class ButtonParameter(BaseParameter):
    type_name = "button"

    def display_value(self):
        return "Button >"


class FolderParameter(BaseParameter):
    type_name = "folder"

    def __init__(self, spec):
        super().__init__(spec)
        raw_children = list(spec.get("children", []))
        self.children = [create_parameter(child) for child in raw_children]

    def display_value(self):
        return "[Folder]"


def create_parameter(spec):
    if isinstance(spec, BaseParameter):
        return spec

    param_type = str(spec.get("type", "")).lower()
    if param_type == "int":
        return IntParameter(spec)
    if param_type == "float":
        return FloatParameter(spec)
    if param_type == "boolean":
        return BooleanParameter(spec)
    if param_type in ("enum", "enumerate"):
        return EnumParameter(spec)
    if param_type == "string":
        return StringParameter(spec)
    if param_type == "rate":
        return RateParameter(spec)
    if param_type == "note":
        return NoteParameter(spec)
    if param_type == "button":
        return ButtonParameter(spec)
    if param_type == "folder":
        return FolderParameter(spec)

    raise ValueError("Unsupported parameter type: " + param_type)
