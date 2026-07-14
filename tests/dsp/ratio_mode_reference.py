import math


RATIO_COUNT = 9
FREQUENCY_MIN = 16.0
FREQUENCY_MAX = 16000.0


def mtof(note, a3_frequency=440.0):
    return a3_frequency * math.pow(2.0, (note - 69.0) / 12.0)


def note_frequency(note, a3_frequency=440.0, alternate_ratios=None):
    if alternate_ratios is None:
        return mtof(note, a3_frequency)
    if len(alternate_ratios) != 12 or not all(math.isfinite(value) and value > 0 for value in alternate_ratios):
        raise ValueError("alternate tuning must contain 12 finite positive ratios")
    degree = note % 12
    c_note = note - degree
    return mtof(c_note, a3_frequency) * alternate_ratios[degree]


def ratio_mode_frequencies(fundamental, ratios):
    if not math.isfinite(fundamental) or fundamental <= 0:
        raise ValueError("fundamental must be finite and positive")
    if len(ratios) != RATIO_COUNT or not all(math.isfinite(value) and value > 0 for value in ratios):
        raise ValueError("ratio profile must contain nine finite positive values")
    return [min(FREQUENCY_MAX, max(FREQUENCY_MIN, fundamental * ratio)) for ratio in ratios]
