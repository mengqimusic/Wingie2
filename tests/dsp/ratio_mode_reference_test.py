import math
from pathlib import Path
import unittest

from ratio_mode_reference import (
    BAR_FACTOR,
    FREQUENCY_MAX,
    FREQUENCY_MIN,
    bar_mode_frequencies,
    mtof,
    note_frequency,
    ratio_mode_frequencies,
    string_mode_frequencies,
)


REPO_ROOT = Path(__file__).resolve().parents[2]


class RatioModeReferenceTest(unittest.TestCase):
    def test_standard_tuning_and_subharmonic_ratios(self):
        fundamental = note_frequency(69)
        self.assertAlmostEqual(fundamental, 440.0)
        frequencies = ratio_mode_frequencies(
            fundamental,
            [0.125, 0.5, 1, 1.5, 2, 3, 4, 5, 9],
        )
        self.assertAlmostEqual(frequencies[0], 55.0)
        self.assertAlmostEqual(frequencies[1], 220.0)
        self.assertAlmostEqual(frequencies[2], 440.0)
        self.assertAlmostEqual(frequencies[8], 3960.0)

    def test_alternate_tuning_sets_fundamental_before_ratio(self):
        tuning = [1, 16 / 15, 9 / 8, 6 / 5, 5 / 4, 4 / 3, 64 / 45, 3 / 2, 8 / 5, 5 / 3, 16 / 9, 15 / 8]
        fundamental = note_frequency(61, alternate_ratios=tuning)
        self.assertAlmostEqual(fundamental, mtof(60) * 16 / 15)
        frequencies = ratio_mode_frequencies(fundamental, [0.5, 1, 2, 3, 4, 5, 6, 7, 8])
        self.assertAlmostEqual(frequencies[0], fundamental * 0.5)
        self.assertAlmostEqual(frequencies[2], fundamental * 2)

    def test_frequency_limits_and_non_finite_inputs(self):
        frequencies = ratio_mode_frequencies(1000, [0.001, 0.125, 1, 2, 4, 8, 16, 32, 100])
        self.assertEqual(frequencies[0], FREQUENCY_MIN)
        self.assertEqual(frequencies[-1], FREQUENCY_MAX)
        with self.assertRaises(ValueError):
            ratio_mode_frequencies(440, [1, 2, 3, 4, math.nan, 6, 7, 8, 9])

    def test_string_and_bar_frequencies_are_preserved_control_side(self):
        fundamental = note_frequency(57)
        strings = string_mode_frequencies(fundamental)
        bars = bar_mode_frequencies(fundamental)
        self.assertAlmostEqual(strings[0], fundamental)
        self.assertAlmostEqual(strings[8], fundamental * 9)
        self.assertAlmostEqual(bars[0], fundamental * BAR_FACTOR * 1.5 ** 2)
        self.assertAlmostEqual(bars[8], fundamental * BAR_FACTOR * 9.5 ** 2)

    def test_faust_and_firmware_inventory_match_contract(self):
        source = (REPO_ROOT / "Wingie2.dsp").read_text(encoding="utf-8")
        generated = (REPO_ROOT / "Wingie2/Wingie2.cpp").read_text(encoding="utf-8")
        firmware = (REPO_ROOT / "Wingie2/Wingie2.ino").read_text(encoding="utf-8")
        midi = (REPO_ROOT / "Wingie2/MIDI.ino").read_text(encoding="utf-8")
        self.assertNotIn("ratio_mode_ratio_", source)
        self.assertNotIn("ratio_mode(note, n)", source)
        self.assertIn("ba.selectn(2, s)", source)
        self.assertIn("a = max(16, min(f(index, source), 16000));", source)
        self.assertNotIn("ratio_mode_ratio_", generated)
        self.assertNotIn('addHorizontalSlider("note0"', generated)
        self.assertIn("Mode[ch] == POLY_MODE ? 0 : 1", firmware)
        self.assertIn("const float fundamental = configured_note_frequency(midiNote);", firmware)
        self.assertIn("return index + 1.0f;", firmware)
        self.assertIn("return barFactor * barIndex * barIndex;", firmware)
        self.assertIn("fundamental * pitched_mode_ratio(Mode[ch], index)", firmware)
        self.assertIn("Mode[ch] == RATIO_MODE ? false : cm_ms", firmware)
        self.assertIn("set_channel_note(ch, pitch);", midi)
        self.assertIn("value == 127 ? RATIO_MODE : (value >> 5)", midi)


if __name__ == "__main__":
    unittest.main()
