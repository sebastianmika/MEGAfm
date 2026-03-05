#!/usr/bin/env python3
"""
MEGAfm Factory Preset Decoder

Decodes the kFactoryPresets byte array from constants.h into a pandas DataFrame.
Each row is one preset, columns are CC numbers + parameter names.

The decoding mirrors loadPreset() in preset.cpp and the CC output mirrors
dumpPreset() in midi.cpp.
"""

import pandas as pd

# ---------------------------------------------------------------------------
# Enum dictionaries
# ---------------------------------------------------------------------------

VOICE_MODES = {
    0: "Poly12",
    1: "Wide6",
    2: "DualCh3",
    3: "Unison",
    4: "Wide4",
    5: "Wide3",
}

ARP_MODES = {
    0: "Off",
    1: "Up",
    2: "Down",
    3: "Up/Down",
    4: "Random1",
    5: "Random2",
    6: "Sequence1",
    7: "Sequence2",
}

LFO_SHAPES = {
    0: "Square",
    1: "Inv Square",
    2: "Triangle",
    3: "Saw",
    4: "Inv Saw",
    5: "Random Inf",
    6: "Random 8",
    7: "Random 16",
    8: "Random 32",
}

ENVELOPE_MODES = {
    0: "Off",
    1: "Forward",
    2: "Ping Pong",
}

# ---------------------------------------------------------------------------
# Bit helpers (matching Arduino bitRead/bitWrite)
# ---------------------------------------------------------------------------

def bit_read(val, bit):
    return (val >> bit) & 1

def bit_write(val, bit, b):
    if b:
        return val | (1 << bit)
    else:
        return val & ~(1 << bit)

# ---------------------------------------------------------------------------
# fmShifts from FM.h — used to boost sub-8-bit values to full 8-bit range
# ---------------------------------------------------------------------------

FM_SHIFTS = [
    5, 4, 1, 6, 3, 3, 3, 4, 4,   # op1: detune, mult, level, RS, AR, D1R, D2R, sustain, release
    5, 4, 1, 6, 3, 3, 3, 4, 4,   # op3 (note: ops are stored 1,3,2,4 in fmBase due to hardware layout)
    5, 4, 1, 6, 3, 3, 3, 4, 4,   # op2
    5, 4, 1, 6, 3, 3, 3, 4, 4,   # op4
    0, 0, 0, 0, 0, 0, 5, 5,      # fmBase[36..43]: LFO/arp/vib/algo/feedback
]
# fmBase[44..50] have no shift (not in the array, loop only runs for i < 44)

PRESET_SIZE = 79
NUM_FACTORY_PRESETS = 50

# Linked array skips these fmBase indices (rate scaling params, not linkable)
LINKED_SKIP = {3, 12, 21, 30}

# Human-readable names for each fmBase index
# Op order in fmBase: Op1 (0-8), Op3 (9-17), Op2 (18-26), Op4 (27-35), globals (36-50)
FMBASE_NAMES = {
    0: "Op1 Detune", 1: "Op1 Multiple", 2: "Op1 Level",
    3: "Op1 Rate Scaling", 4: "Op1 Attack", 5: "Op1 Decay",
    6: "Op1 Sustain Rate", 7: "Op1 Sustain", 8: "Op1 Release",
    9: "Op3 Detune", 10: "Op3 Multiple", 11: "Op3 Level",
    12: "Op3 Rate Scaling", 13: "Op3 Attack", 14: "Op3 Decay",
    15: "Op3 Sustain Rate", 16: "Op3 Sustain", 17: "Op3 Release",
    18: "Op2 Detune", 19: "Op2 Multiple", 20: "Op2 Level",
    21: "Op2 Rate Scaling", 22: "Op2 Attack", 23: "Op2 Decay",
    24: "Op2 Sustain Rate", 25: "Op2 Sustain", 26: "Op2 Release",
    27: "Op4 Detune", 28: "Op4 Multiple", 29: "Op4 Level",
    30: "Op4 Rate Scaling", 31: "Op4 Attack", 32: "Op4 Decay",
    33: "Op4 Sustain Rate", 34: "Op4 Sustain", 35: "Op4 Release",
    36: "LFO1 Rate", 37: "LFO1 Depth", 38: "LFO2 Rate", 39: "LFO2 Depth",
    40: "LFO3 Rate", 41: "LFO3 Depth", 42: "Algorithm", 43: "Feedback",
    44: "Unused 44", 45: "Unused 45", 46: "Arp Rate", 47: "Arp Range",
    48: "Vibrato Rate", 49: "Vibrato Depth", 50: "Fat",
}

# ---------------------------------------------------------------------------
# kFactoryPresets from constants.h
# ---------------------------------------------------------------------------

K_FACTORY_PRESETS = [
    105, 22,  43,  52,  63,  5,   68,  91,  16,  31,  31,  243, 182, 78,  32,  62,  63,  3,   8,   127, 16,  21,  16,
    213, 89,  15,  73,  0,   4,   74,  7,   7,   6,   31,  0,   0,   255, 0,   0,   73,  8,   2,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   150, 96,  54,  9,   69,  11,  20,  21,  7,   4,   91,  16,  31,  31,  240, 186,
    83,  32,  62,  63,  208, 136, 127, 16,  21,  31,  208, 89,  15,  73,  0,   4,   74,  7,   7,   6,   31,  0,   0,
    255, 0,   0,   72,  8,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   64,  146, 55,  54,  10,  48,  0,
    8,   20,  7,   63,  0,   63,  63,  63,  144, 176, 0,   32,  32,  48,  240, 186, 127, 32,  51,  40,  184, 83,  46,
    73,  0,   214, 120, 5,   0,   6,   31,  24,  0,   255, 0,   0,   179, 136, 10,  0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   32,  0,   0,   127, 130, 134, 137, 139, 137, 134, 130, 129, 130, 132, 133, 134,
    135, 134, 132, 16,  66,  53,  54,  106, 107, 46,  62,  53,  7,   4,   91,  16,  31,  11,  240, 185, 83,  32,  62,
    63,  10,  8,   127, 16,  21,  13,  208, 89,  15,  73,  0,   4,   74,  7,   7,   6,   31,  0,   0,   46,  16,  0,
    73,  8,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   142, 53,  54,  9,   54,  64,  95,  95,
    245, 183, 0,   99,  112, 127, 197, 226, 122, 61,  32,  63,  165, 178, 123, 39,  51,  63,  213, 0,   55,  67,  142,
    207, 6,   0,   0,   0,   0,   34,  0,   0,   75,  0,   11,  75,  3,   0,   0,   128, 0,   0,   0,   0,   0,   0,
    0,   0,   4,   0,   0,   37,  128, 128, 127, 134, 139, 142, 141, 137, 139, 134, 132, 134, 129, 130, 125, 127, 122,
    120, 0,   151, 50,  0,   0,   56,  20,  15,  31,  8,   49,  75,  52,  47,  63,  249, 189, 82,  40,  48,  63,  56,
    60,  106, 52,  42,  63,  249, 31,  15,  167, 39,  18,  74,  0,   0,   6,   31,  0,   0,   40,  12,  0,   72,  10,
    3,   0,   0,   0,   0,   0,   0,   4,   0,   0,   0,   0,   0,   12,  0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   141, 2,   179, 0,   91,  7,   17,  0,   7,   7,   114,
    4,   30,  0,   249, 135, 83,  8,   16,  0,   8,   103, 127, 32,  47,  40,  251, 31,  15,  31,  0,   18,  74,  4,
    5,   6,   31,  0,   0,   40,  12,  0,   72,  8,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    12,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   205,
    24,  0,   0,   50,  64,  83,  85,  247, 190, 120, 111, 96,  125, 154, 177, 119, 49,  46,  45,  8,   56,  127, 52,
    32,  63,  248, 0,   55,  67,  142, 255, 199, 0,   5,   0,   0,   0,   0,   0,   0,   0,   9,   75,  3,   0,   0,
    0,   0,   0,   0,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   127, 130, 134, 137, 139, 137, 134, 132,
    0,   134, 137, 130, 129, 125, 122, 120, 8,   146, 108, 117, 0,   68,  64,  94,  73,  255, 199, 124, 64,  82,  67,
    155, 217, 117, 0,   0,   31,  14,  119, 127, 32,  63,  35,  255, 99,  255, 209, 142, 255, 2,   5,   2,   0,   0,
    0,   0,   0,   75,  0,   131, 202, 3,   0,   6,   0,   0,   0,   0,   2,   0,   0,   0,   0,   5,   0,   0,   1,
    128, 196, 127, 130, 134, 137, 139, 137, 134, 132, 0,   134, 137, 130, 129, 125, 122, 120, 8,   203, 37,  0,   0,
    20,  18,  25,  31,  7,   97,  88,  42,  56,  63,  215, 141, 85,  8,   14,  31,  231, 178, 127, 45,  59,  63,  214,
    158, 168, 132, 252, 105, 41,  2,   4,   6,   31,  0,   0,   0,   0,   0,   75,  73,  1,   0,   0,   0,   0,   0,
    8,   0,   0,   0,   0,   0,   0,   12,  0,   0,   0,   64,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   147, 82,  0,   121, 111, 32,  49,  63,  5,   68,  127, 13,  0,   0,   3,   0,   125,
    0,   30,  27,  3,   0,   127, 0,   13,  16,  213, 89,  15,  73,  0,   4,   74,  5,   7,   6,   31,  0,   0,   255,
    0,   0,   83,  8,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   16,  143, 1,   0,   0,   0,   13,  24,  31,
    1,   10,  0,   0,   0,   20,  208, 226, 0,   40,  48,  59,  165, 129, 124, 0,   31,  27,  232, 117, 134, 190, 132,
    135, 255, 3,   0,   6,   31,  0,   0,   0,   0,   0,   131, 230, 0,   0,   16,  16,  0,   0,   8,   0,   0,   0,
    0,   0,   0,   12,  0,   0,   0,   64,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   146, 64,  0,   0,   110, 13,  18,  21,  13,  125, 122, 41,  45,  53,  126, 118, 98,  63,  63,  63,  245,
    129, 127, 0,   31,  28,  248, 141, 233, 255, 149, 75,  25,  4,   3,   6,   31,  0,   0,   0,   42,  0,   203, 119,
    2,   0,   24,  0,   0,   0,   8,   0,   0,   0,   132, 0,   0,   12,  0,   2,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   144, 58,  0,   1,   123, 13,  8,   29,  231, 255, 30,
    32,  32,  63,  14,  117, 112, 40,  47,  52,  245, 142, 82,  0,   0,   31,  8,   195, 91,  255, 149, 59,  83,  7,
    2,   6,   31,  0,   0,   0,   42,  0,   195, 167, 2,   0,   8,   0,   0,   1,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   128, 128, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   96,  152,
    9,   50,  11,  89,  0,   8,   29,  224, 252, 49,  32,  32,  63,  14,  114, 127, 50,  47,  52,  245, 142, 82,  0,
    0,   31,  8,   254, 74,  255, 149, 202, 83,  7,   2,   6,   31,  0,   0,   0,   42,  0,   195, 59,  2,   0,   8,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   128, 128, 0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   148, 64,  0,   1,   69,  65,  79,  95,  252, 131, 86,  76,  95,  95,
    124, 106, 111, 63,  47,  63,  8,   0,   127, 15,  20,  31,  93,  11,  0,   67,  3,   90,  66,  4,   6,   0,   0,
    0,   0,   0,   75,  0,   139, 199, 19,  0,   0,   0,   0,   0,   0,   2,   0,   0,   0,   0,   0,   4,   4,   0,
    0,   0,   127, 130, 134, 137, 139, 137, 134, 132, 0,   134, 137, 130, 129, 125, 122, 120, 104, 152, 26,  0,   108,
    108, 32,  50,  63,  13,  48,  7,   32,  63,  59,  247, 230, 117, 40,  46,  59,  165, 129, 110, 0,   31,  27,  232,
    188, 81,  241, 218, 167, 254, 5,   0,   6,   31,  0,   0,   0,   0,   0,   139, 239, 1,   0,   16,  16,  0,   0,
    8,   0,   0,   0,   0,   0,   0,   12,  0,   0,   0,   64,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   153, 66,  0,   0,   112, 0,   8,   29,  224, 240, 1,   32,  32,  63,  14,  112, 127,
    40,  47,  52,  245, 135, 65,  0,   0,   31,  8,   255, 8,   242, 246, 202, 83,  5,   2,   6,   31,  0,   0,   0,
    42,  0,   203, 26,  3,   0,   8,   0,   0,   0,   0,   0,   0,   0,   4,   0,   0,   0,   0,   0,   128, 128, 0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   150, 54,  0,   0,   127, 0,   18,
    31,  13,  48,  127, 32,  63,  59,  247, 228, 127, 40,  50,  59,  165, 128, 127, 0,   2,   27,  232, 54,  69,  150,
    218, 55,  0,   6,   6,   6,   31,  0,   0,   0,   0,   0,   75,  118, 1,   0,   16,  24,  0,   0,   8,   0,   0,
    0,   0,   0,   0,   12,  0,   0,   0,   64,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   150, 86,  83,  104, 69,  43,  52,  63,  5,   64,  112, 16,  31,  31,  243, 179, 96,  32,  62,  63,
    3,   3,   127, 16,  21,  16,  213, 89,  15,  73,  0,   4,   74,  2,   7,   6,   31,  0,   0,   255, 0,   0,   73,
    8,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   150, 4,   0,   10,  14,  11,  15,  24,  0,   48,
    84,  32,  35,  61,  0,   89,  112, 8,   16,  25,  0,   62,  113, 33,  55,  57,  112, 31,  15,  31,  0,   18,  74,
    5,   4,   6,   31,  0,   0,   40,  12,  0,   72,  8,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   12,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    208, 17,  0,   0,   102, 0,   20,  30,  128, 231, 9,   34,  34,  63,  192, 137, 127, 8,   16,  31,  192, 142, 76,
    4,   3,   31,  192, 31,  15,  31,  0,   215, 10,  5,   7,   6,   31,  0,   0,   81,  0,   8,   75,  72,  2,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   14,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   104, 75,  49,  10,  8,   13,  26,  28,  78,  55,  92,  32,
    58,  59,  220, 191, 0,   40,  48,  59,  238, 231, 123, 32,  53,  59,  186, 31,  15,  31,  0,   192, 10,  6,   6,
    6,   31,  0,   0,   40,  12,  0,   72,  72,  2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   207,
    39,  0,   10,  82,  7,   24,  21,  135, 203, 0,   0,   31,  0,   11,  57,  83,  43,  48,  62,  0,   103, 117, 32,
    47,  32,  0,   31,  15,  31,  0,   18,  74,  3,   0,   6,   31,  0,   0,   40,  12,  0,   67,  8,   2,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   12,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   202, 19,  15,  10,  108, 11,  17,  30,  8,   75,  7,   0,   31,  0,
    11,  55,  83,  32,  47,  62,  0,   96,  123, 32,  34,  32,  0,   31,  15,  31,  0,   18,  74,  7,   0,   6,   31,
    0,   0,   255, 0,   0,   75,  8,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   71,  46,  15,  114,
    111, 96,  116, 117, 245, 230, 127, 98,  113, 115, 183, 255, 117, 49,  46,  63,  15,  120, 127, 63,  63,  63,  255,
    0,   55,  67,  142, 118, 93,  7,   5,   0,   0,   0,   0,   0,   75,  0,   0,   75,  3,   0,   0,   0,   0,   0,
    0,   2,   0,   0,   0,   0,   0,   4,   0,   0,   0,   0,   127, 130, 134, 137, 139, 137, 134, 132, 0,   134, 137,
    130, 129, 125, 122, 120, 8,   141, 59,  53,  0,   0,   64,  84,  86,  247, 187, 117, 96,  110, 122, 242, 176, 73,
    49,  46,  63,  0,   51,  127, 32,  52,  62,  225, 0,   55,  67,  142, 102, 95,  4,   0,   0,   0,   0,   0,   0,
    75,  0,   8,   75,  3,   0,   0,   0,   0,   0,   0,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   127,
    130, 134, 137, 139, 137, 134, 132, 0,   134, 137, 130, 129, 125, 122, 120, 8,   14,  83,  40,  0,   120, 30,  0,
    21,  231, 255, 127, 32,  45,  63,  240, 182, 83,  60,  41,  63,  0,   63,  15,  32,  61,  63,  240, 0,   0,   0,
    0,   0,   0,   5,   4,   0,   0,   0,   0,   148, 17,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   16,  172, 39,  35,  126, 48,  104, 116, 117, 255, 130, 58,  64,  89,  92,  95,  49,  83,  49,  46,  59,
    14,  66,  116, 0,   26,  23,  127, 0,   55,  67,  142, 255, 199, 3,   7,   0,   0,   0,   0,   0,   75,  0,   8,
    75,  3,   0,   0,   0,   0,   0,   0,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   127, 130, 134, 137,
    139, 137, 134, 132, 0,   134, 137, 130, 129, 125, 122, 120, 8,   150, 10,  43,  0,   0,   1,   26,  22,  49,  55,
    117, 58,  61,  59,  215, 178, 31,  34,  48,  59,  238, 231, 123, 47,  53,  59,  178, 31,  15,  31,  0,   192, 10,
    0,   0,   6,   31,  0,   0,   96,  0,   0,   72,  72,  2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    203, 21,  0,   9,   4,   31,  15,  21,  72,  7,   123, 4,   25,  25,  225, 177, 83,  63,  48,  57,  8,   48,  52,
    60,  48,  57,  168, 255, 255, 255, 123, 255, 255, 7,   0,   6,   31,  0,   0,   91,  0,   0,   137, 122, 0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   64,  0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   206, 26,  13,  0,   0,   0,   24,  22,  7,   0,   58,  0,   0,
    31,  224, 191, 115, 49,  63,  63,  240, 142, 127, 0,   0,   31,  176, 0,   0,   0,   0,   0,   0,   1,   6,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   16,  207, 10,
    8,   9,   69,  30,  0,   21,  231, 255, 127, 63,  32,  63,  240, 185, 83,  60,  32,  63,  240, 191, 127, 61,  61,
    63,  240, 0,   0,   0,   0,   0,   0,   5,   4,   0,   0,   36,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   174, 1,   1,   0,   14,  1,   26,  28,  49,  62,  125, 58,  61,  59,  215,
    187, 0,   34,  48,  59,  238, 231, 124, 47,  53,  59,  179, 31,  15,  31,  0,   192, 10,  0,   0,   6,   31,  0,
    0,   192, 20,  0,   72,  72,  2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   144, 202, 29,  0,   0,   10,
    79,  75,  95,  240, 142, 101, 88,  82,  95,  224, 217, 60,  0,   18,  31,  16,  103, 124, 54,  43,  63,  240, 113,
    112, 25,  33,  66,  249, 2,   7,   0,   0,   0,   0,   0,   0,   1,   136, 250, 2,   0,   16,  0,   0,   0,   0,
    8,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   127, 130, 134, 137, 139, 137, 134, 132, 0,   134, 137, 130,
    129, 125, 122, 120, 8,   207, 64,  7,   0,   10,  79,  75,  95,  240, 142, 74,  88,  82,  95,  224, 191, 60,  32,
    50,  63,  16,  103, 114, 54,  43,  63,  240, 75,  180, 117, 33,  66,  249, 2,   7,   0,   0,   0,   0,   0,   0,
    0,   136, 122, 2,   0,   16,  16,  0,   0,   0,   8,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   127, 130,
    134, 137, 139, 137, 134, 132, 0,   134, 137, 130, 129, 125, 122, 120, 104, 204, 47,  7,   10,  6,   64,  75,  73,
    240, 142, 90,  91,  82,  95,  224, 211, 83,  17,  14,  3,   224, 238, 115, 47,  63,  63,  240, 0,   55,  67,  142,
    118, 21,  4,   2,   0,   0,   0,   0,   0,   130, 0,   9,   75,  3,   0,   0,   0,   0,   0,   0,   2,   0,   0,
    0,   0,   0,   4,   0,   0,   0,   0,   127, 130, 134, 137, 139, 137, 134, 132, 0,   134, 137, 130, 129, 125, 122,
    120, 8,   205, 33,  10,  10,  6,   67,  95,  73,  247, 135, 119, 93,  82,  95,  224, 185, 122, 61,  46,  63,  240,
    231, 115, 60,  63,  63,  240, 0,   55,  67,  142, 255, 199, 4,   2,   0,   0,   0,   0,   134, 0,   0,   8,   75,
    3,   0,   0,   0,   0,   0,   0,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   127, 130, 134, 137, 139,
    137, 134, 132, 0,   134, 137, 130, 129, 125, 122, 120, 8,   206, 30,  7,   0,   100, 131, 148, 149, 213, 190, 127,
    32,  41,  62,  148, 164, 83,  249, 244, 240, 35,  127, 117, 50,  35,  58,  244, 190, 253, 232, 255, 240, 255, 7,
    0,   138, 9,   25,  24,  92,  13,  0,   192, 127, 0,   1,   1,   1,   64,  0,   0,   12,  0,   0,   0,   0,   1,
    1,   0,   33,  128, 128, 22,  15,  2,   13,  11,  3,   17,  22,  23,  1,   21,  11,  4,   9,   17,  15,  0,   174,
    1,   0,   0,   6,   93,  75,  95,  176, 135, 127, 64,  82,  95,  240, 218, 110, 0,   14,  3,   224, 239, 26,  32,
    45,  47,  240, 0,   55,  67,  142, 102, 93,  4,   2,   0,   0,   84,  0,   0,   75,  5,   51,  75,  3,   0,   0,
    0,   0,   0,   0,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   127, 134, 139, 146, 139, 134, 139, 134,
    0,   134, 137, 130, 129, 125, 122, 120, 8,   4,   68,  128, 9,   14,  64,  88,  95,  240, 135, 119, 64,  82,  93,
    224, 215, 125, 17,  14,  3,   224, 239, 91,  32,  63,  37,  240, 0,   55,  29,  205, 255, 95,  0,   2,   0,   0,
    71,  235, 0,   75,  5,   51,  107, 2,   0,   0,   0,   0,   0,   0,   2,   0,   0,   128, 8,   0,   0,   0,   0,
    0,   0,   127, 127, 128, 127, 127, 134, 127, 127, 127, 127, 128, 127, 127, 137, 127, 127, 0,   98,  68,  7,   9,
    69,  64,  84,  85,  247, 135, 107, 80,  84,  94,  235, 186, 83,  49,  46,  35,  224, 225, 111, 32,  63,  52,  240,
    0,   55,  67,  142, 255, 199, 7,   2,   0,   0,   67,  0,   0,   75,  0,   27,  75,  3,   0,   0,   0,   0,   0,
    0,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   127, 130, 134, 137, 139, 137, 134, 132, 0,   134, 137,
    130, 129, 125, 122, 120, 8,   204, 54,  7,   10,  6,   64,  75,  73,  240, 135, 116, 64,  82,  95,  224, 215, 100,
    17,  14,  3,   224, 238, 65,  32,  63,  47,  240, 5,   215, 114, 142, 255, 95,  4,   2,   0,   0,   22,  161, 0,
    75,  255, 163, 203, 3,   0,   0,   16,  0,   0,   0,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   127,
    130, 134, 137, 139, 137, 134, 132, 0,   134, 137, 130, 129, 125, 122, 120, 8,   200, 86,  7,   0,   0,   64,  84,
    79,  240, 128, 71,  95,  95,  94,  224, 142, 125, 1,   14,  15,  160, 135, 121, 0,   19,  5,   234, 0,   55,  101,
    8,   255, 95,  4,   2,   0,   0,   65,  255, 0,   75,  1,   27,  74,  3,   0,   0,   0,   0,   0,   0,   2,   0,
    0,   1,   128, 0,   0,   0,   0,   0,   0,   127, 130, 134, 137, 139, 137, 134, 132, 0,   134, 137, 130, 129, 125,
    122, 120, 8,   206, 88,  61,  10,  64,  75,  73,  80,  135, 243, 64,  114, 127, 96,  208, 234, 17,  46,  35,  32,
    238, 238, 91,  47,  61,  32,  18,  255, 67,  142, 255, 95,  7,   0,   0,   0,   61,  34,  0,   75,  2,   179, 203,
    3,   0,   0,   0,   0,   2,   0,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   127, 139, 127, 130, 142,
    130, 132, 144, 132, 128, 140, 140, 130, 142, 130, 113, 12,  8,   79,  88,  7,   106, 8,   45,  58,  60,  71,  55,
    92,  32,  58,  59,  220, 176, 119, 40,  48,  32,  14,  98,  127, 32,  54,  59,  176, 31,  15,  31,  0,   192, 10,
    0,   6,   6,   31,  0,   0,   40,  12,  0,   75,  72,  2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   140, 101, 0,   10,  82,  64,  64,  77,  248, 191, 127, 96,  107, 127, 7,   54,  127, 49,  46,  50,  249, 176,
    115, 32,  63,  34,  7,   0,   55,  67,  142, 255, 199, 2,   7,   0,   0,   10,  0,   227, 75,  10,  11,  75,  3,
    0,   0,   0,   0,   0,   0,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   127, 130, 134, 137, 139, 137,
    134, 132, 0,   134, 137, 130, 129, 125, 122, 120, 8,   140, 66,  91,  0,   62,  64,  72,  85,  247, 176, 127, 96,
    101, 115, 87,  48,  0,   49,  46,  63,  255, 176, 127, 32,  48,  34,  7,   0,   55,  67,  142, 255, 199, 5,   5,
    0,   0,   0,   0,   227, 75,  0,   11,  75,  3,   0,   0,   0,   0,   0,   0,   2,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   127, 130, 134, 137, 139, 137, 134, 132, 0,   134, 137, 130, 129, 125, 122, 120, 8,   149, 43,
    7,   0,   56,  64,  64,  93,  255, 180, 105, 96,  96,  126, 255, 178, 117, 49,  46,  62,  255, 176, 127, 32,  54,
    34,  1,   0,   55,  67,  142, 107, 95,  1,   0,   0,   0,   0,   0,   227, 75,  166, 11,  79,  1,   0,   0,   0,
    0,   0,   0,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,   64,  127, 130, 134, 137, 139, 137, 183, 132, 0,
    134, 137, 130, 129, 125, 122, 120, 8,   5,   99,  42,  0,   48,  65,  69,  69,  247, 176, 127, 96,  108, 108, 199,
    176, 127, 49,  46,  35,  119, 63,  127, 32,  45,  47,  241, 136, 55,  67,  142, 255, 93,  5,   0,   0,   0,   0,
    0,   227, 75,  0,   11,  11,  3,   0,   0,   0,   0,   0,   0,   2,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    64,  127, 130, 134, 137, 139, 137, 134, 132, 0,   134, 137, 130, 129, 125, 122, 120, 8,   0,
    83,  1,   1,   1,   0,   0,   0,   0,
    0,   0,   12,  12,  0,   0,   0,   0,   0,   255, 255, 255, 255, 255, 127, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 7,   7,   255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
]


# ---------------------------------------------------------------------------
# Preset decoder — mirrors loadPreset() in preset.cpp
# ---------------------------------------------------------------------------

def decode_preset(data):
    """Decode a 79-byte (or 78-used-byte) preset into internal state.

    Returns a dict with all extracted parameters.
    """
    assert len(data) >= 78, f"Expected at least 78 bytes, got {len(data)}"
    pos = 0

    def get_byte():
        nonlocal pos
        b = data[pos]
        pos += 1
        return b

    # --- Operator data (4 ops × 6 bytes = 24 bytes) ---
    fmBase = [0] * 51
    SSEG = [0] * 4
    inverted_saw = [0] * 3
    inverted_square = [0] * 3
    oct_offset = 0
    v4_preset = 0

    offset = 0
    for i in range(4):
        # Byte 1: detune + mult + SSEG bit 0
        temp = get_byte()
        fmBase[0 + offset] = 0
        fmBase[0 + offset] = bit_write(fmBase[0 + offset], 0, bit_read(temp, 4))  # detune
        fmBase[0 + offset] = bit_write(fmBase[0 + offset], 1, bit_read(temp, 5))
        fmBase[0 + offset] = bit_write(fmBase[0 + offset], 2, bit_read(temp, 6))
        fmBase[1 + offset] = 0
        fmBase[1 + offset] = bit_write(fmBase[1 + offset], 0, bit_read(temp, 0))  # mult
        fmBase[1 + offset] = bit_write(fmBase[1 + offset], 1, bit_read(temp, 1))
        fmBase[1 + offset] = bit_write(fmBase[1 + offset], 2, bit_read(temp, 2))
        fmBase[1 + offset] = bit_write(fmBase[1 + offset], 3, bit_read(temp, 3))
        SSEG[i] = bit_write(SSEG[i], 0, bit_read(temp, 7))

        # Byte 2: total level
        fmBase[2 + offset] = get_byte()

        # Byte 3: rate scale + AR + SSEG bit 1
        temp = get_byte()
        fmBase[3 + offset] = 0
        fmBase[3 + offset] = bit_write(fmBase[3 + offset], 0, bit_read(temp, 6))  # rate scale
        fmBase[3 + offset] = bit_write(fmBase[3 + offset], 1, bit_read(temp, 7))
        fmBase[4 + offset] = 0
        for b in range(5):
            fmBase[4 + offset] = bit_write(fmBase[4 + offset], b, bit_read(temp, b))  # AR
        SSEG[i] = bit_write(SSEG[i], 1, bit_read(temp, 5))

        # Byte 4: D1R + invertedSaw/Square + octOffset
        temp = get_byte()
        fmBase[5 + offset] = 0
        for b in range(5):
            fmBase[5 + offset] = bit_write(fmBase[5 + offset], b, bit_read(temp, b))  # D1R
        if i < 3:
            inverted_saw[i] = bit_read(temp, 5)
            inverted_square[i] = bit_read(temp, 6)
            if i < 2:
                oct_offset = bit_write(oct_offset, i, bit_read(temp, 7))

        # Byte 5: D2R + v4Preset bit
        temp = get_byte()
        fmBase[6 + offset] = 0
        for b in range(5):
            fmBase[6 + offset] = bit_write(fmBase[6 + offset], b, bit_read(temp, b))  # D2R
        v4_preset = bit_write(v4_preset, i, bit_read(temp, 5))

        # Byte 6: sustain + release
        temp = get_byte()
        fmBase[7 + offset] = 0
        fmBase[7 + offset] = bit_write(fmBase[7 + offset], 0, bit_read(temp, 4))  # sustain
        fmBase[7 + offset] = bit_write(fmBase[7 + offset], 1, bit_read(temp, 5))
        fmBase[7 + offset] = bit_write(fmBase[7 + offset], 2, bit_read(temp, 6))
        fmBase[7 + offset] = bit_write(fmBase[7 + offset], 3, bit_read(temp, 7))
        fmBase[8 + offset] = 0
        fmBase[8 + offset] = bit_write(fmBase[8 + offset], 0, bit_read(temp, 0))  # release
        fmBase[8 + offset] = bit_write(fmBase[8 + offset], 1, bit_read(temp, 1))
        fmBase[8 + offset] = bit_write(fmBase[8 + offset], 2, bit_read(temp, 2))
        fmBase[8 + offset] = bit_write(fmBase[8 + offset], 3, bit_read(temp, 3))

        offset += 9

    # --- fmBase[36..50] raw bytes (15 bytes) ---
    for i in range(36, 51):
        fmBase[i] = get_byte()

    # --- Boost to 8-bit (mirrors the << fmShifts[i] in loadPreset) ---
    for i in range(44):
        fmBase[i] = (fmBase[i] << FM_SHIFTS[i]) & 0xFF

    # --- Voice mode, arp mode, LFO shapes (2 bytes) ---
    voice_mode = 0
    arp_mode = 0
    lfo_shape = [0, 0, 0]

    temp = get_byte()
    voice_mode = 0
    voice_mode = bit_write(voice_mode, 0, bit_read(temp, 0))
    voice_mode = bit_write(voice_mode, 1, bit_read(temp, 1))
    voice_mode = bit_write(voice_mode, 2, bit_read(temp, 2))
    if v4_preset == 15 or (v4_preset != 15 and voice_mode == 3):  # kVoicingUnison
        arp_mode = bit_write(arp_mode, 0, bit_read(temp, 3))
        arp_mode = bit_write(arp_mode, 1, bit_read(temp, 4))
        arp_mode = bit_write(arp_mode, 2, bit_read(temp, 5))
    lfo_shape[0] = bit_write(lfo_shape[0], 0, bit_read(temp, 6))
    lfo_shape[0] = bit_write(lfo_shape[0], 1, bit_read(temp, 7))

    temp = get_byte()
    lfo_shape[1] = bit_write(lfo_shape[1], 0, bit_read(temp, 0))
    lfo_shape[1] = bit_write(lfo_shape[1], 1, bit_read(temp, 1))
    lfo_shape[2] = bit_write(lfo_shape[2], 0, bit_read(temp, 2))
    lfo_shape[2] = bit_write(lfo_shape[2], 1, bit_read(temp, 3))
    retrig = [bit_read(temp, 4), bit_read(temp, 5), bit_read(temp, 6)]
    looping = [bit_read(temp, 7), 0, 0]

    # --- Linked data (18 bytes) ---
    linked = [[0] * 51 for _ in range(3)]

    temp = get_byte()
    looping[1] = bit_read(temp, 0)
    looping[2] = bit_read(temp, 1)

    # The linked data is packed densely, skipping indices 3, 12, 21, 30.
    # We reproduce the exact bit layout from loadPreset().
    # LFO 0, first part of this byte
    linked[0][0] = bit_read(temp, 2)
    linked[0][1] = bit_read(temp, 3)
    linked[0][2] = bit_read(temp, 4)
    linked[0][4] = bit_read(temp, 5)
    linked[0][5] = bit_read(temp, 6)
    linked[0][6] = bit_read(temp, 7)

    temp = get_byte()
    linked[0][7] = bit_read(temp, 0)
    linked[0][8] = bit_read(temp, 1)
    linked[0][9] = bit_read(temp, 2)
    linked[0][10] = bit_read(temp, 3)
    linked[0][11] = bit_read(temp, 4)
    linked[0][13] = bit_read(temp, 5)
    linked[0][14] = bit_read(temp, 6)
    linked[0][15] = bit_read(temp, 7)

    temp = get_byte()
    linked[0][16] = bit_read(temp, 0)
    linked[0][17] = bit_read(temp, 1)
    linked[0][18] = bit_read(temp, 2)
    linked[0][19] = bit_read(temp, 3)
    linked[0][20] = bit_read(temp, 4)
    linked[0][22] = bit_read(temp, 5)
    linked[0][23] = bit_read(temp, 6)
    linked[0][24] = bit_read(temp, 7)

    temp = get_byte()
    linked[0][25] = bit_read(temp, 0)
    linked[0][26] = bit_read(temp, 1)
    linked[0][27] = bit_read(temp, 2)
    linked[0][28] = bit_read(temp, 3)
    linked[0][29] = bit_read(temp, 4)
    linked[0][31] = bit_read(temp, 5)
    linked[0][32] = bit_read(temp, 6)
    linked[0][33] = bit_read(temp, 7)

    temp = get_byte()
    linked[0][34] = bit_read(temp, 0)
    linked[0][35] = bit_read(temp, 1)
    linked[0][36] = bit_read(temp, 2)
    linked[0][37] = bit_read(temp, 3)
    linked[0][38] = bit_read(temp, 4)
    linked[0][39] = bit_read(temp, 5)
    linked[0][40] = bit_read(temp, 6)
    linked[0][41] = bit_read(temp, 7)

    temp = get_byte()
    linked[0][42] = bit_read(temp, 0)
    linked[0][43] = bit_read(temp, 1)
    linked[0][44] = bit_read(temp, 2)
    linked[0][45] = bit_read(temp, 3)
    linked[0][46] = bit_read(temp, 4)
    linked[0][47] = bit_read(temp, 5)
    linked[0][48] = bit_read(temp, 6)
    linked[0][49] = bit_read(temp, 7)

    temp = get_byte()
    linked[0][50] = bit_read(temp, 0)
    linked[1][0] = bit_read(temp, 1)
    linked[1][1] = bit_read(temp, 2)
    linked[1][2] = bit_read(temp, 3)
    linked[1][4] = bit_read(temp, 4)
    linked[1][5] = bit_read(temp, 5)
    linked[1][6] = bit_read(temp, 6)
    linked[1][7] = bit_read(temp, 7)

    temp = get_byte()
    linked[1][8] = bit_read(temp, 0)
    linked[1][9] = bit_read(temp, 1)
    linked[1][10] = bit_read(temp, 2)
    linked[1][11] = bit_read(temp, 3)
    linked[1][13] = bit_read(temp, 4)
    linked[1][14] = bit_read(temp, 5)
    linked[1][15] = bit_read(temp, 6)
    linked[1][16] = bit_read(temp, 7)

    temp = get_byte()
    linked[1][17] = bit_read(temp, 0)
    linked[1][18] = bit_read(temp, 1)
    linked[1][19] = bit_read(temp, 2)
    linked[1][20] = bit_read(temp, 3)
    linked[1][22] = bit_read(temp, 4)
    linked[1][23] = bit_read(temp, 5)
    linked[1][24] = bit_read(temp, 6)
    linked[1][25] = bit_read(temp, 7)

    temp = get_byte()
    linked[1][26] = bit_read(temp, 0)
    linked[1][27] = bit_read(temp, 1)
    linked[1][28] = bit_read(temp, 2)
    linked[1][29] = bit_read(temp, 3)
    linked[1][31] = bit_read(temp, 4)
    linked[1][32] = bit_read(temp, 5)
    linked[1][33] = bit_read(temp, 6)
    linked[1][34] = bit_read(temp, 7)

    temp = get_byte()
    linked[1][35] = bit_read(temp, 0)
    linked[1][36] = bit_read(temp, 1)
    linked[1][37] = bit_read(temp, 2)
    linked[1][38] = bit_read(temp, 3)
    linked[1][39] = bit_read(temp, 4)
    linked[1][40] = bit_read(temp, 5)
    linked[1][41] = bit_read(temp, 6)
    linked[1][42] = bit_read(temp, 7)

    temp = get_byte()
    linked[1][43] = bit_read(temp, 0)
    linked[1][44] = bit_read(temp, 1)
    linked[1][45] = bit_read(temp, 2)
    linked[1][46] = bit_read(temp, 3)
    linked[1][47] = bit_read(temp, 4)
    linked[1][48] = bit_read(temp, 5)
    linked[1][49] = bit_read(temp, 6)
    linked[1][50] = bit_read(temp, 7)

    temp = get_byte()
    linked[2][0] = bit_read(temp, 0)
    linked[2][1] = bit_read(temp, 1)
    linked[2][2] = bit_read(temp, 2)
    linked[2][4] = bit_read(temp, 3)
    linked[2][5] = bit_read(temp, 4)
    linked[2][6] = bit_read(temp, 5)
    linked[2][7] = bit_read(temp, 6)
    linked[2][8] = bit_read(temp, 7)

    temp = get_byte()
    linked[2][9] = bit_read(temp, 0)
    linked[2][10] = bit_read(temp, 1)
    linked[2][11] = bit_read(temp, 2)
    linked[2][13] = bit_read(temp, 3)
    linked[2][14] = bit_read(temp, 4)
    linked[2][15] = bit_read(temp, 5)
    linked[2][16] = bit_read(temp, 6)
    linked[2][17] = bit_read(temp, 7)

    temp = get_byte()
    linked[2][18] = bit_read(temp, 0)
    linked[2][19] = bit_read(temp, 1)
    linked[2][20] = bit_read(temp, 2)
    linked[2][22] = bit_read(temp, 3)
    linked[2][23] = bit_read(temp, 4)
    linked[2][24] = bit_read(temp, 5)
    linked[2][25] = bit_read(temp, 6)
    linked[2][26] = bit_read(temp, 7)

    temp = get_byte()
    linked[2][27] = bit_read(temp, 0)
    linked[2][28] = bit_read(temp, 1)
    linked[2][29] = bit_read(temp, 2)
    linked[2][31] = bit_read(temp, 3)
    linked[2][32] = bit_read(temp, 4)
    linked[2][33] = bit_read(temp, 5)
    linked[2][34] = bit_read(temp, 6)
    linked[2][35] = bit_read(temp, 7)

    temp = get_byte()
    linked[2][36] = bit_read(temp, 0)
    linked[2][37] = bit_read(temp, 1)
    linked[2][38] = bit_read(temp, 2)
    linked[2][39] = bit_read(temp, 3)
    linked[2][40] = bit_read(temp, 4)
    linked[2][41] = bit_read(temp, 5)
    linked[2][42] = bit_read(temp, 6)
    linked[2][43] = bit_read(temp, 7)

    temp = get_byte()
    linked[2][44] = bit_read(temp, 0)
    linked[2][45] = bit_read(temp, 1)
    linked[2][46] = bit_read(temp, 2)
    linked[2][47] = bit_read(temp, 3)
    linked[2][48] = bit_read(temp, 4)
    linked[2][49] = bit_read(temp, 5)
    linked[2][50] = bit_read(temp, 6)
    # bit 7 unused

    # --- Sequence (16 bytes) ---
    seq = [get_byte() for _ in range(16)]

    # --- Seq length + glide (1 byte) ---
    temp = get_byte()
    seq_length = 0
    seq_length = bit_write(seq_length, 0, bit_read(temp, 0))
    seq_length = bit_write(seq_length, 1, bit_read(temp, 1))
    seq_length = bit_write(seq_length, 2, bit_read(temp, 2))
    seq_length = bit_write(seq_length, 3, bit_read(temp, 3))
    seq_length += 1  # stored as 0-15, displayed as 1-16

    glide = 0
    glide = bit_write(glide, 0, bit_read(temp, 4))
    glide = bit_write(glide, 1, bit_read(temp, 5))
    glide = bit_write(glide, 2, bit_read(temp, 6))
    glide = bit_write(glide, 3, bit_read(temp, 7))

    # --- Fine (1 byte) ---
    fine = get_byte()

    # --- Volume (1 byte) ---
    vol = get_byte()

    # Byte 78 is unused padding
    assert pos == 78, f"Expected to consume 78 bytes, consumed {pos}"

    # --- Derive envelope modes from SSEG ---
    envelope_mode = [0] * 4
    for i in range(4):
        if bit_read(SSEG[i], 1):
            if bit_read(SSEG[i], 0):
                envelope_mode[i] = 2  # ping pong
            else:
                envelope_mode[i] = 1  # forward/once
        else:
            envelope_mode[i] = 0  # off

    return {
        "fmBase": fmBase,
        "voice_mode": voice_mode,
        "arp_mode": arp_mode,
        "lfo_shape": lfo_shape,
        "inverted_saw": inverted_saw,
        "inverted_square": inverted_square,
        "retrig": retrig,
        "looping": looping,
        "linked": linked,
        "seq": seq,
        "seq_length": seq_length,
        "glide": glide,
        "fine": fine,
        "vol": vol,
        "oct_offset": oct_offset,
        "v4_preset": v4_preset,
        "envelope_mode": envelope_mode,
    }


# ---------------------------------------------------------------------------
# State to CC messages — mirrors dumpPreset() in midi.cpp
# ---------------------------------------------------------------------------

# fmBase index names (for documentation)
# Op layout: [detune, mult, level, RS, AR, D1R, D2R, sustain, release] × 4 ops
# Then: LFO1_rate, LFO1_depth, LFO2_rate, LFO2_depth, LFO3_rate, LFO3_depth,
#        algorithm, feedback, unused44, unused45, arp_rate, arp_range,
#        vib_rate, vib_depth, fat

def _lfo_shape_to_cc70_val(lfo_idx, shape, inv_saw, inv_square):
    """Convert internal LFO shape representation to CC 70 value.

    Mirrors the dumpPreset() LFO shape encoding:
      Square=0, InvSquare=1, Triangle=2, Saw=3, InvSaw=4, Random=5+
    Plus offset of 16*lfo_idx.
    """
    if shape == 0:  # square
        val = inv_square
    elif shape == 1:  # triangle
        val = 2
    elif shape == 2:  # saw
        val = 3 + inv_saw
    elif shape == 3:  # random
        val = 5  # noiseTableLength not stored in preset; defaults to inf (=2)
    else:
        val = 0
    return val + 16 * lfo_idx


def state_to_row(state):
    """Convert decoded preset state to a flat dict of named parameters.

    Each key is "CC{cc_num} {name}" for CC-representable params, or
    "CC-1 {name}" for params with no CC representation.
    Values are the CC values that would be sent (0-127 range) or raw values.
    """
    fm = state["fmBase"]
    row = {}

    # --- Continuous CC parameters ---
    # Format: "CC{num}+{name}+({value_range})"
    # Use col.split("+") to get [cc, name, range].
    # The dumpPreset function sends fmBase[x] >> 1 for most params (0-127).

    # OP1 (fmBase indices 0-8, with 3=RS)
    row["CC18+Op1 Detune+(0-127)"] = fm[0] >> 1
    row["CC27+Op1 Multiple+(0-127)"] = fm[1] >> 1
    row["CC19+Op1 Level+(0-127)"] = fm[2] >> 1
    row["CC29+Op1 Attack+(0-127)"] = fm[4] >> 1
    row["CC21+Op1 Decay+(0-127)"] = fm[5] >> 1
    row["CC25+Op1 Sustain+(0-127)"] = fm[7] >> 1
    row["CC17+Op1 Sustain Rate+(0-127)"] = fm[6] >> 1
    row["CC30+Op1 Release+(0-127)"] = fm[8] >> 1

    # OP2 (fmBase indices 18-26, with 21=RS) — note: internal order is 1,3,2,4
    row["CC31+Op2 Detune+(0-127)"] = fm[18] >> 1
    row["CC32+Op2 Multiple+(0-127)"] = fm[19] >> 1
    row["CC40+Op2 Level+(0-127)"] = fm[20] >> 1
    row["CC36+Op2 Attack+(0-127)"] = fm[22] >> 1
    row["CC44+Op2 Decay+(0-127)"] = fm[23] >> 1
    row["CC42+Op2 Sustain+(0-127)"] = fm[25] >> 1
    row["CC34+Op2 Sustain Rate+(0-127)"] = fm[24] >> 1
    row["CC11+Op2 Release+(0-127)"] = fm[26] >> 1

    # OP3 (fmBase indices 9-17, with 12=RS)
    row["CC20+Op3 Detune+(0-127)"] = fm[9] >> 1
    row["CC24+Op3 Multiple+(0-127)"] = fm[10] >> 1
    row["CC16+Op3 Level+(0-127)"] = fm[11] >> 1
    row["CC49+Op3 Attack+(0-127)"] = fm[13] >> 1
    row["CC50+Op3 Decay+(0-127)"] = fm[14] >> 1
    row["CC51+Op3 Sustain+(0-127)"] = fm[16] >> 1
    row["CC45+Op3 Sustain Rate+(0-127)"] = fm[15] >> 1
    row["CC37+Op3 Release+(0-127)"] = fm[17] >> 1

    # OP4 (fmBase indices 27-35, with 30=RS)
    row["CC47+Op4 Detune+(0-127)"] = fm[27] >> 1
    row["CC39+Op4 Multiple+(0-127)"] = fm[28] >> 1
    row["CC38+Op4 Level+(0-127)"] = fm[29] >> 1
    row["CC46+Op4 Attack+(0-127)"] = fm[31] >> 1
    row["CC33+Op4 Decay+(0-127)"] = fm[32] >> 1
    row["CC41+Op4 Sustain+(0-127)"] = fm[34] >> 1
    row["CC43+Op4 Sustain Rate+(0-127)"] = fm[33] >> 1
    row["CC35+Op4 Release+(0-127)"] = fm[35] >> 1

    # Global continuous
    row["CC7+Volume+(0-127)"] = 128 - state["vol"]  # special: sent as 128 - lastVol
    row["CC4+Algorithm+(1-8)"] = 1 + (fm[42] >> 5)  # special: 1 + (fmBase[42] >> 5)
    row["CC3+Feedback+(0-127)"] = fm[43] >> 1
    row["CC28+Fat+(0-127)"] = fm[50] >> 1
    row["CC15+LFO1 Rate+(0-127)"] = fm[36] >> 1
    row["CC12+LFO1 Depth+(0-127)"] = fm[37] >> 1
    row["CC10+LFO2 Rate+(0-127)"] = fm[38] >> 1
    row["CC9+LFO2 Depth+(0-127)"] = fm[39] >> 1
    row["CC14+LFO3 Rate+(0-127)"] = fm[40] >> 1
    row["CC2+LFO3 Depth+(0-127)"] = fm[41] >> 1
    row["CC6+Arp Rate+(0-127)"] = fm[46] >> 1
    row["CC5+Arp Range+(0-127)"] = fm[47] >> 1
    row["CC48+Vibrato Rate+(0-127)"] = fm[48] >> 1
    row["CC13+Vibrato Depth+(0-127)"] = fm[49] >> 1

    # --- CC 78 multi-valued params ---
    row["CC78+Voice Mode+(0-5)"] = state["voice_mode"]
    row["CC78+Voice Mode Name+(0-5)"] = VOICE_MODES.get(state["voice_mode"], "Unknown")
    row["CC78+Octave Offset+(10-13)"] = state["oct_offset"]
    row["CC78+Op1 Rate Scaling+(20-23)"] = fm[3] >> 6
    row["CC78+Op2 Rate Scaling+(30-33)"] = fm[12] >> 6
    row["CC78+Op3 Rate Scaling+(40-43)"] = fm[21] >> 6
    row["CC78+Op4 Rate Scaling+(50-53)"] = fm[30] >> 6
    row["CC78+Op1 Envelope Mode+(25-27)"] = state["envelope_mode"][0]
    row["CC78+Op1 Envelope Mode Name+(25-27)"] = ENVELOPE_MODES.get(state["envelope_mode"][0], "Unknown")
    row["CC78+Op2 Envelope Mode+(35-37)"] = state["envelope_mode"][1]
    row["CC78+Op2 Envelope Mode Name+(35-37)"] = ENVELOPE_MODES.get(state["envelope_mode"][1], "Unknown")
    row["CC78+Op3 Envelope Mode+(45-47)"] = state["envelope_mode"][2]
    row["CC78+Op3 Envelope Mode Name+(45-47)"] = ENVELOPE_MODES.get(state["envelope_mode"][2], "Unknown")
    row["CC78+Op4 Envelope Mode+(55-57)"] = state["envelope_mode"][3]
    row["CC78+Op4 Envelope Mode Name+(55-57)"] = ENVELOPE_MODES.get(state["envelope_mode"][3], "Unknown")

    # --- CC 70 multi-valued params ---
    row["CC70+Arp Mode+(85-92)"] = state["arp_mode"]
    row["CC70+Arp Mode Name+(85-92)"] = ARP_MODES.get(state["arp_mode"], "Unknown")

    for i in range(3):
        lfo_num = i + 1
        base = 16 * i  # CC70 value offset per LFO
        shape = state["lfo_shape"][i]
        inv_saw = state["inverted_saw"][i] if i < 3 else 0
        inv_square = state["inverted_square"][i] if i < 3 else 0
        cc70_val = _lfo_shape_to_cc70_val(i, shape, inv_saw, inv_square)
        shape_offset = cc70_val - base
        row[f"CC70+LFO{lfo_num} Shape+({base}-{base+5})"] = shape_offset
        row[f"CC70+LFO{lfo_num} Shape Name+({base}-{base+5})"] = LFO_SHAPES.get(shape_offset, "Unknown")
        row[f"CC70+LFO{lfo_num} Retrig+({base+6}-{base+7})"] = state["retrig"][i]
        row[f"CC70+LFO{lfo_num} Loop+({base+8}-{base+9})"] = state["looping"][i]

    # --- CC 75 Glide ---
    row["CC75+Glide+(0-127)"] = state["glide"] << 3  # sent as glide << 3

    # --- CC 76 Fine ---
    row["CC76+Fine+(0-127)"] = state["fine"] >> 1  # sent as fine >> 1

    # --- CC 71-73 LFO Link targets ---
    # Each target pot encodes as val = target*2 + linked (0=unlinked, 1=linked)
    for lfo in range(3):
        cc_num = 71 + lfo
        lfo_num = lfo + 1
        for target in range(51):
            if target in LINKED_SKIP:
                continue
            target_name = FMBASE_NAMES.get(target, f"fmBase[{target}]")
            val_lo = target * 2
            val_hi = target * 2 + 1
            row[f"CC{cc_num}+LFO{lfo_num} Link {target_name}+({val_lo}-{val_hi})"] = state["linked"][lfo][target]

    # --- Parameters with no CC (CC=-1) ---
    row["CC-1+fmBase[44] (unused)+(N/A)"] = fm[44]
    row["CC-1+fmBase[45] (unused)+(N/A)"] = fm[45]
    row["CC-1+v4 Preset Flag+(N/A)"] = state["v4_preset"]
    row["CC-1+Seq Length+(N/A)"] = state["seq_length"]
    for i in range(16):
        row[f"CC-1+Seq[{i}]+(N/A)"] = state["seq"][i]
    row["CC-1+Padding Byte+(N/A)"] = "N/A"

    return row


# ---------------------------------------------------------------------------
# Build the DataFrame
# ---------------------------------------------------------------------------

def decode_all_presets():
    """Decode all factory presets into a pandas DataFrame."""
    rows = []
    for p in range(NUM_FACTORY_PRESETS):
        start = p * PRESET_SIZE
        preset_bytes = K_FACTORY_PRESETS[start : start + PRESET_SIZE]
        state = decode_preset(preset_bytes)
        row = state_to_row(state)
        row["Preset Number"] = p
        rows.append(row)

    df = pd.DataFrame(rows)
    # Move preset number to first column
    cols = ["Preset Number"] + [c for c in df.columns if c != "Preset Number"]
    df = df[cols]
    return df


# ---------------------------------------------------------------------------
# Byte accounting documentation
# ---------------------------------------------------------------------------

BYTE_ACCOUNTING = """
PRESET BYTE ACCOUNTING (79 bytes total)
========================================

Bytes 0-23 (24 bytes): Operator parameters, 4 ops x 6 bytes each
  Per operator:
    Byte 0: bits[4:6]=detune(3b), bits[0:3]=mult(4b), bit7=SSEG[i].bit0 (envelope mode)
    Byte 1: total level (8b)
    Byte 2: bits[6:7]=rate_scale(2b), bits[0:4]=attack(5b), bit5=SSEG[i].bit1 (envelope mode)
    Byte 3: bits[0:4]=D1R(5b), bit5=invertedSaw[i] (i<3 only), bit6=invertedSquare[i] (i<3 only),
            bit7=octOffset.bit[i] (i<2 only)
    Byte 4: bits[0:4]=D2R(5b), bit5=v4Preset.bit[i] (firmware version flag, NOT a CC param)
    Byte 5: bits[4:7]=sustain(4b), bits[0:3]=release(4b)

Bytes 24-38 (15 bytes): fmBase[36..50] stored as raw bytes
    [36]=LFO1 Rate, [37]=LFO1 Depth, [38]=LFO2 Rate, [39]=LFO2 Depth,
    [40]=LFO3 Rate, [41]=LFO3 Depth, [42]=Algorithm, [43]=Feedback,
    [44]=UNUSED, [45]=UNUSED, [46]=Arp Rate, [47]=Arp Range,
    [48]=Vibrato Rate, [49]=Vibrato Depth, [50]=Fat

Byte 39 (1 byte): voiceMode(3b) + arpMode(3b) + lfoShape[0](2b)
Byte 40 (1 byte): lfoShape[1](2b) + lfoShape[2](2b) + retrig[0..2](3b) + looping[0](1b)

Bytes 41-58 (18 bytes): looping[1..2](2b) + linked[0..2] bitfields (141b) + 1 unused bit
    linked[] skips indices 3, 12, 21, 30 (rate scaling params, not linkable)
    Each LFO covers 47 targets = 141 bits total for 3 LFOs

Bytes 59-74 (16 bytes): seq[0..15] — arpeggiator sequence steps (NO CC representation)

Byte 75 (1 byte): seqLength(4b, low nibble, stored as 0-15, +1 for 1-16) + glide(4b, high nibble)

Byte 76 (1 byte): fine tuning

Byte 77 (1 byte): volume

Byte 78 (1 byte): UNUSED PADDING — not read or written by loadPreset/savePreset

SKIPPED/NO-CC BYTES:
  - fmBase[44], fmBase[45]: Loaded from preset but never sent as CC, no knob mapping. Reserved/unused.
  - v4Preset (4 bits across op bytes): Firmware version flag. Not a user parameter.
  - seq[0..15]: Stored per-preset but not sent as CC in dumpPreset().
  - Byte 78: Padding to reach 79-byte preset size.
  - SSEG bits, invertedSaw/Square bits: Not directly CC-sent, but contribute to CC 78 (envelope mode)
    and CC 70 (LFO shape) respectively.
"""


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    df = decode_all_presets()
    print(f"Decoded {len(df)} factory presets into {len(df.columns)} columns.")
    print()
    print("Column names:")
    for col in df.columns:
        print(f"  {col}")
    print()
    print(BYTE_ACCOUNTING)
    print("First 5 presets (transposed for readability):")
    print(df.head().T.to_string())
    df.to_csv("factory_presets.csv", index=False)
    print("\nSaved to factory_presets.csv")
