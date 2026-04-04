import adafruit_ticks as ticks

class clock:
	def __init__(self,):
		self.bpm = 100
		self.miditick = 0
		self.miditicklasttime = 0
		self.running = 0
		self.mode = 0 #mode0 = internal - mode1 = external
				
	def update(self):
		# todo: instead of using current time comparison, schedule a nexttime so lateness doesn's stack and drift'
		if not self.running:
			return False
		interval = 60000 / (self.bpm * 24)
		current = ticks.ticks_ms()
		sincelast = ticks.ticks_diff(current,self.miditicklasttime)
		if sincelast>interval:
			self.miditick += 1
			self.miditicklasttime = current
			return True
		else:
			return False
			
	def reset(self):
		self.miditick = 0
		self.miditicklasttime = ticks.ticks_ms()
	
	def clockstart(self):
		self.reset()
		self.running = 1
		
	def clockstop(self):
		self.reset()
		self.running = 0
	
	def setbpm(self,bpm):
		self.bpm=bpm
		
	def setmode(self,mode):
		self.mode=mode
		
		
		
		 