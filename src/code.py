from adafruit_macropad import MacroPad

import apps.MidiCommander.MidiCommander as MidiCommander
#import apps.MidiTester.code as MidiTester
#import apps.MidiCommander.tinytest as tinytest



midi_commander = MidiCommander.MidiCommander()
midi_commander.run()

#MidiTester.run()
#tinytest.run()