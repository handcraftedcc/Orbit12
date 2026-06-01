import adafruit_ticks as ticks
import gc

from adafruit_midi.timing_clock import TimingClock
from adafruit_midi.start import Start
from adafruit_midi.stop import Stop
from adafruit_midi.midi_continue import Continue

MAX_SWING = 0.6
SWING_TICKS_PER_HALF = 6
SWING_TICKS_PER_PAIR = SWING_TICKS_PER_HALF * 2

class Transport:
	def __init__(self, state,output_manager, macropad):
		self.output_manager = output_manager
		self.macropad = macropad
		self.state = state

		self.bpm = state.bpm
		self.swing = state.swing
		self.midi_tick = 1
		self.midi_tick_scheduled = 1
		self.tick_interval_ms_f = 0
		self.tick_interval_ms_i = 0
		self.tick_interval_err = 0
		self.update_bpm(self.bpm)
		self.now = ticks.ticks_ms()

		self.running = 0

	def update_bpm(self,value):
		self.bpm = value
		self.tick_interval_ms_f = 60000.0 / (self.bpm * 24.0)
		self.tick_interval_ms_i = int(self.tick_interval_ms_f)
		self.tick_interval_err = 0.0

	def update_swing(self, value):
		self.swing = max(0.0, min(1, value))
		self.state.swing = self.swing

	def next_tick_swing_factor(self):
		if self.swing == 0:
			return 1.0
		next_tick = self.midi_tick + 1
		tick_in_pair = next_tick % SWING_TICKS_PER_PAIR
		if tick_in_pair > 0 and tick_in_pair <= SWING_TICKS_PER_HALF:
			return 1.0 + self.swing*MAX_SWING
		return 1.0 - self.swing*MAX_SWING

	def update(self):
		self.now = ticks.ticks_ms()
		steps = 0

		if self.state.transport_mode == 1:
			max_msgs = 200
			for _ in range(max_msgs):
				msg = self.macropad.midi.receive()
				if msg is None:
					break
				elif isinstance(msg, TimingClock):
					if self.running == 0:
						self.clock_start(send_out = False)
					self.midi_tick += 1
					steps += 1
					break
				elif isinstance(msg, Start):
					self.reset()
					self.running = 0
				elif isinstance(msg, Stop):
					self.clock_stop(send_out = False)
				elif isinstance(msg, Continue):
					pass
				else:
					pass

		else:
			self.drain_midi_input()

		if self.state.transport_mode == 0 and self.running == 1:
			if ticks.ticks_less(self.midi_tick_scheduled, self.now):
				self.schedule_next_tick()
				self.midi_tick += 1
				self.output_manager.schedule_midi_clock()
				steps += 1

		return steps

	def drain_midi_input(self):
		max_msgs = 200
		for _ in range(max_msgs):
			msg = self.macropad.midi.receive()
			if msg is None:
				break

	def schedule_next_tick(self):
		tick_interval = self.tick_interval_ms_f * self.next_tick_swing_factor()
		interval = int(tick_interval)
		self.tick_interval_err += tick_interval - interval
		if self.tick_interval_err >= 1.0:
			interval += 1
			self.tick_interval_err -= 1.0
		self.midi_tick_scheduled = ticks.ticks_add(self.midi_tick_scheduled, interval)

	def reset(self):
		self.now = ticks.ticks_ms()
		self.midi_tick = 0
		self.tick_interval_err = 0.0
		self.midi_tick_scheduled = self.now
		self.output_manager.pending_midi_clock_ticks = 0

	def clock_start(self, send_out = True):
		gc.collect()
		self.reset()
		self.schedule_next_tick()
		self.running = 1
		self.output_manager.pending_midi_clock_ticks = 0
		if send_out: self.output_manager.schedule_midi_start()
		
	def clock_stop(self, send_out = True):
		self.reset()
		self.running = 0
		self.output_manager.pending_midi_clock_ticks = 0
		if send_out: self.output_manager.schedule_midi_stop()
		self.state.stop_all_modules()
		gc.collect()
