"""Helpers to fit Orbit12 UI rows to the MacroPad 128x64 display width."""


ELLIPSIS = ".."
SCROLL_GAP = "   "


def _to_text(value):
    if value is None:
        return ""
    return str(value)


def _pad_right(text, width):
    if width <= 0:
        return ""
    if len(text) >= width:
        return text[:width]
    return text + (" " * (width - len(text)))


def _pad_left(text, width):
    if width <= 0:
        return ""
    if len(text) >= width:
        return text[-width:]
    return (" " * (width - len(text))) + text


def truncate_text(text, width):
    text = _to_text(text)
    if width <= 0:
        return ""
    if len(text) <= width:
        return text
    if width <= len(ELLIPSIS):
        return "." * width
    return text[: width - len(ELLIPSIS)] + ELLIPSIS


def scroll_text(text, width, tick):
    text = _to_text(text)
    if width <= 0:
        return ""
    if len(text) <= width:
        return _pad_right(text, width)
    loop = text + SCROLL_GAP
    start = tick % len(loop)
    doubled = loop + loop
    return doubled[start : start + width]


def fit_single_line(text, width, selected=False, tick=0):
    text = _to_text(text)
    if width <= 0:
        return ""
    if len(text) <= width:
        return _pad_right(text, width)
    if selected:
        return scroll_text(text, width, tick)
    return truncate_text(text, width)


def single_line_overflows(text, width):
    return len(_to_text(text)) > max(0, width)


def split_line_overflows(left_text, right_text, width):
    if width <= 0:
        return False
    left = _to_text(left_text)
    right = _to_text(right_text)
    return (len(left) + 1 + len(right)) > width


def _fit_field(text, width, selected, tick, align_left):
    text = _to_text(text)
    if width <= 0:
        return ""
    if len(text) > width:
        return scroll_text(text, width, tick) if selected else truncate_text(text, width)
    if align_left:
        return _pad_right(text, width)
    return _pad_left(text, width)


def fit_split_line(left_text, right_text, width, selected=False, tick=0):
    left = _to_text(left_text)
    right = _to_text(right_text)

    if width <= 0:
        return ""
    if width == 1:
        return fit_single_line(left, width, selected, tick)

    available = width - 1
    combined_len = len(left) + len(right)
    if combined_len <= available:
        spacing = (available - combined_len) + 1
        return left + (" " * spacing) + right

    min_left = 4 if available >= 9 else max(1, available // 2)
    min_right = 4 if available >= 9 else max(1, available - min_left)

    right_width = min(max(len(right), min_right), available - min_left)
    left_width = available - right_width
    if left_width < 1:
        left_width = 1
        right_width = available - left_width

    left_part = _fit_field(left, left_width, selected, tick, True)
    right_part = _fit_field(right, right_width, selected, tick, False)
    return left_part + " " + right_part
