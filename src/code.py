from adafruit_macropad import MacroPad

import apps.Orion.Orion as Orion
#import apps.MidiTester.code as MidiTester
#import apps.Orion.tinytest as tinytest



midi_commander = Orion.Orion()
midi_commander.run()

#MidiTester.run()
#tinytest.run()