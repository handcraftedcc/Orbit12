"""Terminal console: plain prompts on stdin/stdout. No colors, no terminal control codes."""

from __future__ import annotations

import sys
from typing import Callable, List, Optional

from .device import Cancelled
from .interfaces import Console


class TerminalConsole(Console):
    def __init__(self, assume_yes: bool = False, input_fn: Callable[[str], str] = input,
                 out=None, err=None) -> None:
        self._assume_yes = assume_yes
        self._input = input_fn
        self._out = out or sys.stdout
        self._err = err or sys.stderr

    def _read(self, prompt: str) -> str:
        try:
            return self._input(prompt)
        except EOFError:
            raise Cancelled("no input available; nothing was changed") from None

    def say(self, message: str) -> None:
        print(message, file=self._out, flush=True)

    def warn(self, message: str) -> None:
        print(f"WARNING: {message}", file=self._err, flush=True)

    def pause_for_action(self, message: str) -> None:
        print(message, file=self._out, flush=True)
        self._read("Press Enter when done (Ctrl+C to stop) ")

    def confirm(self, message: str, default: bool = False) -> bool:
        if self._assume_yes:
            print(f"{message} [assumed yes: --yes]", file=self._out, flush=True)
            return True
        suffix = "[Y/n]" if default else "[y/N]"
        answer = self._read(f"{message} {suffix} ").strip().lower()
        if not answer:
            return default
        return answer in ("y", "yes")

    def confirm_typed(self, word: str, message: str) -> bool:
        # --yes never answers a typed confirmation.
        print(message, file=self._out, flush=True)
        return self._read(f"Type {word} to continue: ").strip() == word

    def ask(self, prompt: str) -> str:
        return self._read(f"{prompt}: ").strip()

    def choose(self, prompt: str, options: List[str]) -> Optional[int]:
        for number, text in enumerate(options, start=1):
            print(f"  {number}. {text}", file=self._out, flush=True)
        answer = self._read(f"{prompt} [1-{len(options)}]: ").strip()
        if answer.isdigit() and 1 <= int(answer) <= len(options):
            return int(answer) - 1
        return None
