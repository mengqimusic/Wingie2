import math
from pathlib import Path
import re
import unittest

from ratio_mode_reference import (
    FREQUENCY_MAX,
    FREQUENCY_MIN,
    mtof,
    note_frequency,
    ratio_mode_frequencies,
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

    def test_faust_and_generated_inventory_match_contract(self):
        source = (REPO_ROOT / "Wingie2.dsp").read_text(encoding="utf-8")
        generated = (REPO_ROOT / "Wingie2/Wingie2.cpp").read_text(encoding="utf-8")
        self.assertIn('hslider("../../ratio_mode_ratio_%i", ratio_req(i), 0.125, 32, 0.001)', source)
        self.assertIn("ratio_mode(note, n) = note_freq(note)", source)
        self.assertIn("ba.selectn(5, s)", source)
        self.assertIn("a = max(16, min(f(note, index, source), 16000));", source)
        matches = re.findall(
            r'addHorizontalSlider\("ratio_mode_ratio_(\d)",[^\n]+FAUSTFLOAT\(0\.125f\), FAUSTFLOAT\(32\.0f\), FAUSTFLOAT\(0\.001f\)\)',
            generated,
        )
        self.assertEqual(matches, [str(index) for index in range(9)])


if __name__ == "__main__":
    unittest.main()
