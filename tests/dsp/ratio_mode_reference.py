import math


RATIO_COUNT = 9
FREQUENCY_MIN = 16.0
FREQUENCY_MAX = 16000.0
BAR_FACTOR = 0.44444


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


def ratio_poly_voice_frequencies(fundamental, ratios, voice):
    if not math.isfinite(fundamental) or fundamental <= 0:
        raise ValueError("fundamental must be finite and positive")
    if len(ratios) != RATIO_COUNT or not all(math.isfinite(value) and value > 0 for value in ratios):
        raise ValueError("ratio profile must contain nine finite positive values")
    if not isinstance(voice, int) or isinstance(voice, bool) or voice not in (0, 1, 2):
        raise ValueError("voice must be an integer in {0, 1, 2}")
    base = 3 * voice
    return [min(FREQUENCY_MAX, max(FREQUENCY_MIN, fundamental * ratios[base + k])) for k in range(3)]


def string_mode_frequencies(fundamental):
    return ratio_mode_frequencies(fundamental, [index + 1 for index in range(RATIO_COUNT)])


def bar_mode_frequencies(fundamental):
    ratios = [BAR_FACTOR * (index + 1.5) ** 2 for index in range(RATIO_COUNT)]
    return ratio_mode_frequencies(fundamental, ratios)
