import board
import digitalio
import storage


def any_key_held():
    for key_id in range(1, 13):
        pin = digitalio.DigitalInOut(getattr(board, "KEY" + str(key_id)))
        pin.switch_to_input(pull=digitalio.Pull.UP)
        pressed = not pin.value
        pin.deinit()
        if pressed:
            return True
    return False


if not any_key_held():
    storage.remount("/", readonly=False)
else:
    print("USB Mode")

import sys
if "storage" in sys.modules:
    del sys.modules["storage"]
del storage
del sys

import gc
gc.collect()
del gc
