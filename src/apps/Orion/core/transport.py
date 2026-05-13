import adafruit_ticks as ticks
from .state import TimingSteps
import gc

from adafruit_midi.timing_clock import TimingClock
from adafruit_midi.start import Start
from adafruit_midi.stop import Stop
from adafruit_midi.midi_continue import Continue

class Transport:
	def __init__(self, state,output_manager, macropad):
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
			max_msgs = 200
			for _ in range(max_msgs):
				msg = self.macropad.midi.receive()
				if msg is None:
					break
				elif isinstance(msg, TimingClock):
					self.midi_tick += 1
					if self.midi_tick % self.timing_step_interval == 0:
						self.timing_step += 1
						self.timing_stepped = 1
					else:
						self.timing_stepped = 0
					#if self.running == 0:
					#	self.clock_start()
					stepped = True
				elif isinstance(msg, Start):
					self.clock_start(send_out = False)
				elif isinstance(msg, Stop):
					self.clock_stop(send_out = False)
				elif isinstance(msg, Continue):
					pass
				else:
					pass
		else:
			while ticks.ticks_less(self.midi_tick_scheduled, current):
				self.schedule_next_tick()
				self.midi_tick += 1
				self.output_manager.schedule_midi_clock()
				stepped = True

		if self.running == 1:
			if self.midi_tick % self.timing_step_interval == 0:
				self.timing_step += 1
				self.timing_stepped = 1
			else:
				self.timing_stepped = 0

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
