"""Explicit Windows WinMM duplex test for the isolated T00 smoke firmware.

Requires its CDC '?' counter and 'T' one-note trigger. No third-party packages.
Does not flash or claim timing accuracy. Names must identify exactly one port.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import re
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM6')
    parser.add_argument('--name', default='Orbit12 MIDI')
    args = parser.parse_args()
    if sys.platform != 'win32':
        raise SystemExit('This smoke capture tool uses Windows WinMM.')
    mm = c.WinDLL('winmm')
    kernel = c.WinDLL('kernel32', use_last_error=True)

    class InCaps(c.Structure):
        _fields_ = [('mid', w.WORD), ('pid', w.WORD), ('version', w.DWORD),
                    ('name', w.WCHAR * 32), ('support', w.DWORD)]

    class OutCaps(c.Structure):
        _fields_ = [('mid', w.WORD), ('pid', w.WORD), ('version', w.DWORD),
                    ('name', w.WCHAR * 32), ('technology', w.WORD),
                    ('voices', w.WORD), ('notes', w.WORD), ('channels', w.WORD),
                    ('support', w.DWORD)]

    def choose(prefix, caps_type):
        found = []
        for device in range(getattr(mm, prefix + 'GetNumDevs')()):
            caps = caps_type()
            result = getattr(mm, prefix + 'GetDevCapsW')(device, c.byref(caps), c.sizeof(caps))
            if result == 0 and args.name.casefold() in caps.name.casefold():
                found.append((device, caps.name))
        if len(found) != 1:
            raise RuntimeError(f'Expected one {prefix} matching {args.name!r}, got {found}')
        return found[0]

    def check(result, operation):
        if result:
            raise RuntimeError(f'{operation} returned WinMM error {result}')

    in_id, in_name = choose('midiIn', InCaps)
    out_id, out_name = choose('midiOut', OutCaps)
    print(f'MIDI input: {in_id} {in_name}; output: {out_id} {out_name}')
    # Explicit signatures prevent truncating 64-bit WinMM handles.
    pointer = c.c_size_t
    callback_type = c.WINFUNCTYPE(None, w.HANDLE, w.UINT, pointer, pointer, pointer)
    messages = []

    @callback_type
    def callback(_handle, message, _instance, data, _timestamp):
        if message == 0x3c3:  # MIM_DATA
            messages.append(int(data) & 0xffffff)

    mm.midiInOpen.argtypes = [c.POINTER(w.HANDLE), w.UINT, pointer, pointer, w.DWORD]
    mm.midiOutOpen.argtypes = [c.POINTER(w.HANDLE), w.UINT, pointer, pointer, w.DWORD]
    for name in ('midiInStart', 'midiInStop', 'midiInReset', 'midiInClose', 'midiOutClose'):
        getattr(mm, name).argtypes = [w.HANDLE]
    mm.midiOutShortMsg.argtypes = [w.HANDLE, w.DWORD]
    kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, w.LPVOID, w.DWORD, w.DWORD, w.HANDLE]
    kernel.CreateFileW.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.ReadFile.argtypes = [w.HANDLE, w.LPVOID, w.DWORD, c.POINTER(w.DWORD), w.LPVOID]
    kernel.WriteFile.argtypes = kernel.ReadFile.argtypes
    kernel.EscapeCommFunction.argtypes = [w.HANDLE, w.DWORD]

    class Timeouts(c.Structure):
        _fields_ = [(name, w.DWORD) for name in ('interval', 'read_multiplier', 'read_constant', 'write_multiplier', 'write_constant')]
    kernel.SetCommTimeouts.argtypes = [w.HANDLE, c.POINTER(Timeouts)]
    device_path = chr(92) * 2 + '.' + chr(92) + args.port
    serial = kernel.CreateFileW(device_path, 0xc0000000, 0, None, 3, 0, None)
    if serial == w.HANDLE(-1).value:
        raise c.WinError(c.get_last_error())
    incoming, outgoing = w.HANDLE(), w.HANDLE()
    try:
        if not kernel.SetCommTimeouts(serial, c.byref(Timeouts(20, 0, 100, 0, 100))):
            raise c.WinError(c.get_last_error())
        kernel.EscapeCommFunction(serial, 5)  # DTR asserted; no 1200-baud reset.

        def write(data):
            count = w.DWORD()
            if not kernel.WriteFile(serial, c.c_char_p(data), len(data), c.byref(count), None) or count.value != len(data):
                raise RuntimeError('CDC write failed')

        def query():
            write(b'?')
            data = bytearray()
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                buffer, count = c.create_string_buffer(128), w.DWORD()
                if not kernel.ReadFile(serial, buffer, 128, c.byref(count), None):
                    raise c.WinError(c.get_last_error())
                data.extend(buffer.raw[:count.value])
                match = re.search(rb'T00 RX=(\d+)', data)
                if match:
                    return int(match[1])
            raise RuntimeError(f'CDC counter timeout: {bytes(data)!r}')

        check(mm.midiInOpen(c.byref(incoming), in_id, c.cast(callback, c.c_void_p).value, 0, 0x30000), 'midiInOpen')
        check(mm.midiOutOpen(c.byref(outgoing), out_id, 0, 0, 0), 'midiOutOpen')
        check(mm.midiInStart(incoming), 'midiInStart')
        time.sleep(0.1)
        before = query()
        check(mm.midiOutShortMsg(outgoing, 0x053d90), 'send note-on')
        check(mm.midiOutShortMsg(outgoing, 0x003d80), 'send note-off')
        time.sleep(0.1)
        after = query()
        if after - before != 2:
            raise RuntimeError(f'Expected two incoming firmware messages, counter {before}->{after}')
        write(b'T')
        time.sleep(0.3)
        expected = [0x013c90, 0x003c80]
        if messages != expected:
            raise RuntimeError(f'Expected TX packets {expected}, got {messages}')
        print(f'PASS: firmware RX counter {before}->{after}; captured TX note60 on/off channel1: {[hex(v) for v in messages]}')
    finally:
        if incoming:
            mm.midiInStop(incoming)
            mm.midiInReset(incoming)
            mm.midiInClose(incoming)
        if outgoing:
            mm.midiOutClose(outgoing)
        kernel.CloseHandle(serial)


if __name__ == '__main__':
    main()

