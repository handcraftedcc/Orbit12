"""Orbit12 UI menu controller for navigation/editing."""

from orbit12ui.parameters import ButtonParameter, FolderParameter


class _BackItem:
    name = ".."
    label = ".."
    is_editing = False

    def display_value(self):
        return ""


BACK_ITEM = _BackItem()


class MenuController:
    def __init__(self, items):
        self._stack = [list(items)]
        self._selected = [0]

    def _context_values(self):
        context = {}

        def collect(items):
            for param in items:
                context[param.name] = param.value
                if isinstance(param, FolderParameter):
                    collect(param.children)

        collect(self._stack[0])
        return context

    def _current_list(self):
        return self._stack[-1]

    def visible_items(self):
        context = self._context_values()
        visible = [param for param in self._current_list() if param.visible(context)]
        return [BACK_ITEM] + visible

    def _ensure_selected(self):
        visible = self.visible_items()
        if not visible:
            self._selected[-1] = 0
            return
        self._selected[-1] = self._selected[-1] % len(visible)

    def current_item(self):
        self._ensure_selected()
        visible = self.visible_items()
        if not visible:
            return None
        return visible[self._selected[-1]]

    def rotate(self, delta):
        if delta == 0:
            return

        item = self.current_item()
        if item is None:
            return

        if item.is_editing:
            item.rotate(delta)
            return

        visible = self.visible_items()
        if not visible:
            return

        self._selected[-1] = (self._selected[-1] + delta) % len(visible)

    def press(self):
        item = self.current_item()
        if item is None:
            return None

        if item is BACK_ITEM:
            if self.back():
                return {"type": "back"}
            return {"type": "root_back"}

        if item.is_editing:
            done = item.press()
            if done:
                return {"type": "edit_done", "name": item.name}
            return {"type": "edit_update", "name": item.name}

        if isinstance(item, FolderParameter):
            self._stack.append(item.children)
            self._selected.append(0)
            return {"type": "enter_folder", "name": item.name}

        if isinstance(item, ButtonParameter):
            return {"type": "button", "name": item.name}

        item.start_edit()
        return {"type": "edit_start", "name": item.name}

    def back(self):
        item = self.current_item()
        if item is not None and item.is_editing:
            item.stop_edit()
            return True

        if len(self._stack) > 1:
            self._stack.pop()
            self._selected.pop()
            self._selected[-1] = 0
            return True
        return False
