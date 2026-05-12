import adafruit_ticks as ticks
from .state import TimingSteps


class Transport:
	def __init__(self,state):
		self.bpm = state.bpm
		self.swing = state.swing
		self.timing_step = state.timing_step
		self.midi_tick = 1
		self.midi_tick_scheduled = 1
		self.midi_tick_interval = int(60000 / (self.bpm * 24))
		self.timing_step = 0
		self.timing_step_interval = 6
		self.timing_stepped = 0
		self.running = 0
		self.mode = 0 #mode0 = internal - mode1 = external
				
	def set_timing_step_interval(self,value):
		if value == TimingSteps.One16th:  # 1/16th
			self.timing_step_interval = 6.0
		if value == TimingSteps.One32nd:  # 1/32nd
			self.timing_step_interval = 3.0
		if value == TimingSteps.One64th:  # 1/64
			self.timing_step_interval = 1.5

	def update(self):
		# todo: instead of using current time comparison, schedule a next_time so lateness doesn't stack and drift'
		if not self.running:
			return False
		
		current = ticks.ticks_ms()
		if ticks.ticks_less(self.midi_tick_scheduled,current):
			self.schedule_next_tick()
			self.midi_tick += 1
			if self.midi_tick % self.timing_step_interval == 0:
				self.timing_step += 1
				self.timing_stepped = 1
			else:
				self.timing_stepped = 0
			return True
		else:
			return False


	def schedule_next_tick(self):
		self.midi_tick_scheduled = ticks.ticks_add(self.midi_tick_scheduled,self.midi_tick_interval)

	def reset(self):
		self.midi_tick = 0
		self.midi_tick_scheduled = ticks.ticks_ms()
	
	def clock_start(self):
		self.reset()
		self.schedule_next_tick()
		self.running = 1
		
	def clock_stop(self):
		self.reset()
		self.running = 0