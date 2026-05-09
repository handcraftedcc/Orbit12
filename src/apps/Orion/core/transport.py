import adafruit_ticks as ticks

class Clock:
	def __init__(self,):
		self.bpm = 100
		self.midi_tick = 0
		self.midi_tick_scheduled = 0
		self.running = 0
		self.mode = 0 #mode0 = internal - mode1 = external
				
	def update(self):
		# todo: instead of using current time comparison, schedule a next_time so lateness doesn't stack and drift'
		if not self.running:
			return False
		
		current = ticks.ticks_ms()
		if self.midi_tick_scheduled<=current:
			self.schedule_next_tick()
			self.midi_tick += 1
			return True
		else:
			return False
			
	def schedule_next_tick(self):
		interval = int(60000 / (self.bpm * 24))
		self.midi_tick_scheduled += ticks.ticks_add(self.midi_tick_scheduled,interval)

	def reset(self):
		self.midi_tick = 0
	
	def clock_start(self):
		self.reset()
		self.schedule_next_tick()
		self.running = 1
		
	def clock_stop(self):
		self.reset()
		self.running = 0
	
	def set_bpm(self,bpm):
		self.bpm=bpm
		
	def set_mode(self,mode):
		self.mode=mode