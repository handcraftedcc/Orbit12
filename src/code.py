import board
import displayio
import gc
import time

gc.collect()
print("1. allocated import:",gc.mem_alloc())
print("free:",gc.mem_free())

display = board.DISPLAY
display.auto_refresh = False
display.bus.send(0xAF, b"")
_display_sleep = False

# Optional: clear whatever was already on screen
main_group = displayio.Group()
display.root_group = main_group

# Load bitmap from CIRCUITPY drive
bitmap = displayio.OnDiskBitmap("/apps/Orion/imgs/LaunchScreen.bmp")

# Put it on screen
tile_grid = displayio.TileGrid(
    bitmap,
    pixel_shader=bitmap.pixel_shader,
    x=0,
    y=0
)

main_group.append(tile_grid)
display.refresh()

time.sleep(1.5)

main_group = None
bitmap = None
tile_grid = None
display.root_group = None
display = None


del board
del displayio
gc.collect()

gc.collect()
print("2. allocated import:",gc.mem_alloc())
print("free:",gc.mem_free())


import apps.Orion.Orion as Orion
#import apps.MidiTester.code as MidiTester
#import apps.Orion.tinytest as tinytest

midi_commander = Orion.Orion()
midi_commander.run()

#MidiTester.run()
#tinytest.run()
