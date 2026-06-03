### CONSTANTS ###

DRUMBASENOTE = 36

NOTES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")

SCALENAMES = (
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
    "MAJP",
    "MINP",
    "SPEN",
    "BLU",
    "MIN6",
    "M7B",
    "WT",
    "DIMH",
    "DIMW"
    )

SCALES = (
    # CHROMATIC
    bytes((0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11)),

    # MAJOR / MINOR SYSTEM
    bytes((0, 2, 4, 5, 7, 9, 11)),           # Major (Ionian)
    bytes((0, 2, 3, 5, 7, 8, 10)),           # Natural Minor (Aeolian)
    bytes((0, 2, 3, 5, 7, 8, 11)),           # Harmonic Minor
    bytes((0, 2, 3, 5, 7, 9, 11)),           # Melodic Minor

    # MODES
    bytes((0, 2, 4, 5, 7, 9, 11)),           # Ionian (same as major)
    bytes((0, 2, 3, 5, 7, 9, 10)),           # Dorian
    bytes((0, 1, 3, 5, 7, 8, 10)),           # Phrygian
    bytes((0, 2, 4, 6, 7, 9, 11)),           # Lydian
    bytes((0, 2, 4, 5, 7, 9, 10)),           # Mixolydian
    bytes((0, 2, 3, 5, 7, 8, 10)),           # Aeolian (same as natural minor)
    bytes((0, 1, 3, 5, 6, 8, 10)),           # Locrian

    # PENTATONIC
    bytes((0, 2, 4, 7, 9)),                  # Major Pentatonic
    bytes((0, 3, 5, 7, 10)),                 # Minor Pentatonic
    bytes((0, 2, 5, 7, 10)),                 # Suspended Pentatonic

    # BLUES
    bytes((0, 3, 5, 6, 7, 10)),              # Blues Scale

    # COMMON VARIANTS
    bytes((0, 2, 3, 5, 7, 9, 10)),           # Minor w/ Major 6 (Dorian flavor)
    bytes((0, 2, 4, 5, 7, 9, 10)),           # Major w/ b7 (Mixolydian flavor)

    # SYMMETRICAL / JAZZ
    bytes((0, 2, 4, 6, 8, 10)),              # Whole Tone
    bytes((0, 1, 3, 4, 6, 7, 9, 10)),        # Diminished (Half-Whole)
    bytes((0, 2, 3, 5, 6, 8, 9, 11)),        # Diminished (Whole-Half)
)

IDX_MAJP = SCALENAMES.index("MAJP")
IDX_MINP = SCALENAMES.index("MINP")
IDX_SPEN = SCALENAMES.index("SPEN")

PENTATONIC_IDS = (IDX_MAJP, IDX_MINP, IDX_SPEN)

AUTOBORROWRELATIONSHIP = bytes((
    0,   # CHR   -> CHR

    2,   # MAJ   -> MIN
    1,   # MIN  -> MAJ
    2,   # HMIN  -> MIN
    6,   # MMIN  -> DOR

    2,   # ION   -> MIN
    2,   # DOR   -> MIN
    2,   # PHR   -> MIN
    5,   # LYD   -> ION
    5,   # MIX   -> ION
    5,   # AEO  -> ION
    7,   # LOC   -> PHR

    1,   # MPEN  -> MAJ
    2,   # mPEN  -> MIN
    6,   # SPEN  -> DOR

    13,  # BLU   -> mPEN

    2,   # m6    -> MIN
    1,   # M7b   -> MAJ

    9,   # WT    -> MIX
    3,   # DIMh  -> HMIN
    2,   # DIMw  -> MIN
))

RATE_LABELS= ("1/1", "1/1T", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T")
RATE_MIDI_TICKS = bytes((96, 64, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2))

#Arp patterns

def get_pattern_step(pattern_bit, pattern_len, step):
    step = step % pattern_len
    bit_index = pattern_len - step - 1
    return (pattern_bit >> bit_index) & 1

def get_pattern_empty_steps(pattern_bit, pattern_len):
    empty_steps = 0

    for step in range(pattern_len):
        if ((pattern_bit >> step) & 1) == 0:
            empty_steps += 1

    return empty_steps

patterns_bit = bytes((
    # 2
    0b11,
    0b10,

    # 3
    0b110,
    0b100,

    # 4
    0b1110,
    0b1100,
    0b1000,

    # 5
    0b11110,
    0b11100,
    0b11000,
    0b10101,
    0b10100,

    # 6
    0b111110,
    0b111100,
    0b111000,
    0b110101,
    0b101001,

    # 8
    0b11111110,
    0b11111100,
    0b11111000,
    0b11110000,
    0b11100000,
    0b10111101,
    0b10010100,
    0b10110010,
))

patterns_len = bytes((
    2,
    2,
    3,
    3,
    4,
    4,
    4,
    5,
    5,
    5,
    5,
    5,
    6,
    6,
    6,
    6,
    6,
    8,
    8,
    8,
    8,
    8,
    8,
    8,
    8,
))


def transpose(note, semitones, octaves = 0, scale_aware = True, root = 0, scale = SCALES[1]):
    if scale_aware:
        normalized_note = (note - root + 12) % 12
        octave = (note - root) // 12
        closest_index = 0
        closest_distance = abs(scale[0] - normalized_note)
        for index in range(1, len(scale)):
            distance = abs(scale[index] - normalized_note)
            if distance < closest_distance:
                closest_distance = distance
                closest_index = index
        new_index = closest_index + semitones
        scale_notes = len(scale)
        octave_shift = new_index // scale_notes
        new_index = new_index - octave_shift * scale_notes
        note = scale[new_index]
        note = note + (octave + octave_shift + octaves) * 12 + root
    else:
        note = note + semitones + octaves * 12
    return note
