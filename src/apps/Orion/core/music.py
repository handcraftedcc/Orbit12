OCTAVEOFFSET = 4 #C3
DRUMBASENOTE = 36

NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

SCALENAMES = [
    "CHR",
    "MAJ",
    "MIN",
    "HMIN",
    "MMIN",
    "ION",
    "DOR",
    "PHR",
    "LYD",
    "MIX",
    "AEO",
    "LOC",
    "MPEN",
    "mPEN",
    "SPEN",
    "BLU",
    "m6",
    "M7b",
    "WT",
    "DIMh",
    "DIMw"
    ]

SCALES = [
    # CHROMATIC
    [0,1,2,3,4,5,6,7,8,9,10,11],

    # MAJOR / MINOR SYSTEM
    [0, 2, 4, 5, 7, 9, 11],           # Major (Ionian)
    [0, 2, 3, 5, 7, 8, 10],           # Natural Minor (Aeolian)
    [0, 2, 3, 5, 7, 8, 11],           # Harmonic Minor
    [0, 2, 3, 5, 7, 9, 11],           # Melodic Minor

    # MODES
    [0, 2, 4, 5, 7, 9, 11],           # Ionian (same as major)
    [0, 2, 3, 5, 7, 9, 10],           # Dorian
    [0, 1, 3, 5, 7, 8, 10],           # Phrygian
    [0, 2, 4, 6, 7, 9, 11],           # Lydian
    [0, 2, 4, 5, 7, 9, 10],           # Mixolydian
    [0, 2, 3, 5, 7, 8, 10],           # Aeolian (same as natural minor)
    [0, 1, 3, 5, 6, 8, 10],           # Locrian

    # PENTATONIC
    [0, 2, 4, 7, 9],                  # Major Pentatonic
    [0, 3, 5, 7, 10],                 # Minor Pentatonic
    [0, 2, 5, 7, 10],                 # Suspended Pentatonic

    # BLUES
    [0, 3, 5, 6, 7, 10],              # Blues Scale

    # COMMON VARIANTS
    [0, 2, 3, 5, 7, 9, 10],           # Minor w/ Major 6 (Dorian flavor)
    [0, 2, 4, 5, 7, 9, 10],           # Major w/ b7 (Mixolydian flavor)

    # SYMMETRICAL / JAZZ
    [0, 2, 4, 6, 8, 10],              # Whole Tone
    [0, 1, 3, 4, 6, 7, 9, 10],        # Diminished (Half-Whole)
    [0, 2, 3, 5, 6, 8, 9, 11],        # Diminished (Whole-Half)
]

def note_num_to_name(note_num):
    note_octave = note_num // 12 - 1
    note_name = NOTES[note_num%12]
    return str(note_octave)+note_name


RATE_LABELS= ["1/1", "1/1T", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"]
RATE_VALUES = [16, 16 / 3, 8, 8 / 3, 4, 4 / 3, 2, 2 / 3, 1, 1 / 3, 0.5, 0.5 / 3]