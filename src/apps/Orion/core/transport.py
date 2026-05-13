import adafruit_ticks as ticks
from .state import TimingSteps

from adafruit_midi.timing_clock import TimingClock
from adafruit_midi.start import Start
from adafruit_midi.stop import Stop
from adafruit_midi.midi_continue import Continue

import microcontroller
microcontroller.cpu.frequency = 250_000_000

class Transport:
	def __init__(self,state,output_manager, macropad):
		self.output_manager = output_manager
		self.macropad = macropad
		self.state = state

		self.bpm = state.bpm
		self.swing = state.swing
		self.timing_step = state.timing_step
		self.midi_tick = 1
		self.midi_tick_scheduled = 1
		self.tick_interval_ms_f = 0
		self.tick_interval_ms_i = 0
		self.tick_interval_err = 0
		self.update_bpm(self.bpm)

		self.timing_step = 0
		self.timing_step_interval = 6
		self.timing_stepped = 0
		self.running = 0

	def set_timing_step_interval(self,value):
		if value == TimingSteps.One16th:  # 1/16th
			self.timing_step_interval = 6.0
		if value == TimingSteps.One32nd:  # 1/32nd
			self.timing_step_interval = 3.0
		if value == TimingSteps.One64th:  # 1/64
			self.timing_step_interval = 1.5

	def update_bpm(self,value):
		self.bpm = value
		self.tick_interval_ms_f = 60000.0 / (self.bpm * 24.0)
		self.tick_interval_ms_i = int(self.tick_interval_ms_f)
		self.tick_interval_err = 0.0

	def update(self):
		current = ticks.ticks_ms()
		stepped = False

		if self.state.transport_mode == 1:
			max_msgs = 24
			for _ in range(max_msgs):
				msg = self.macropad.midi.receive()
				if msg is None:
					break
				elif isinstance(msg, Start):
					self.reset()
					self.running = 1
				elif isinstance(msg, Stop):
					self.running = 0
				elif isinstance(msg, Continue):
					self.running = 1
				elif isinstance(msg, TimingClock):
					if self.running == 0:
						continue
					self.midi_tick += 1
					if self.midi_tick % self.timing_step_interval == 0:
						self.timing_step += 1
						self.timing_stepped = 1
					else:
						self.timing_stepped = 0
					stepped = True
				else:
					pass

		elif self.running == 1:
			while ticks.ticks_less(self.midi_tick_scheduled, current):
				self.schedule_next_tick()
				self.midi_tick += 1
				self.output_manager.schedule_midi_clock()
				if self.midi_tick % self.timing_step_interval == 0:
					self.timing_step += 1
					self.timing_stepped = 1
				else:
					self.timing_stepped = 0
				stepped = True

		return stepped

	def schedule_next_tick(self):
		interval = self.tick_interval_ms_i
		self.tick_interval_err += self.tick_interval_ms_f - self.tick_interval_ms_i
		if self.tick_interval_err >= 1.0:
			interval += 1
			self.tick_interval_err -= 1.0
		self.midi_tick_scheduled = ticks.ticks_add(self.midi_tick_scheduled, interval)

	def reset(self):
		now = ticks.ticks_ms()
		self.midi_tick = 0
		self.timing_step = 0
		self.timing_stepped = 0
		self.tick_interval_err = 0.0
		self.midi_tick_scheduled = now

	def clock_start(self):
		self.reset()
		self.schedule_next_tick()
		self.running = 1
		if self.state.transport_mode != 1:
			self.output_manager.schedule_midi_start()
		
	def clock_stop(self):
		self.reset()
		self.running = 0
		if self.state.transport_mode != 1:
			self.output_manager.schedule_midi_stop()
