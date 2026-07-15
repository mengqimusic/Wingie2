import math


RESONATOR_COUNT = 9
FREQUENCY_MIN = 16.0
FREQUENCY_MAX = 16000.0
BAR_FACTOR = 0.44444


def mtof(note, a3_frequency=440.0):
    return a3_frequency * math.pow(2.0, (note - 69.0) / 12.0)


def note_frequency(note, a3_frequency=440.0, alternate_ratios=None):
    if alternate_ratios is None:
        return mtof(note, a3_frequency)
    if len(alternate_ratios) != 12:
        raise ValueError("alternate tuning must contain 12 ratios")
    degree = note % 12
    return mtof(note - degree, a3_frequency) * alternate_ratios[degree]


def clipped_frequencies(fundamental, ratios):
    return [
        min(FREQUENCY_MAX, max(FREQUENCY_MIN, fundamental * ratio))
        for ratio in ratios
    ]


def string_mode_frequencies(fundamental):
    return clipped_frequencies(
        fundamental, [index + 1.0 for index in range(RESONATOR_COUNT)]
    )


def bar_mode_frequencies(fundamental):
    return clipped_frequencies(
        fundamental,
        [BAR_FACTOR * (index + 1.5) ** 2 for index in range(RESONATOR_COUNT)],
    )
