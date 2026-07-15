from pathlib import Path
import unittest

from mode_source_reference import (
    BAR_FACTOR,
    bar_mode_frequencies,
    mtof,
    note_frequency,
    string_mode_frequencies,
)


REPO_ROOT = Path(__file__).resolve().parents[2]


class ModeSourceReferenceTest(unittest.TestCase):
    def test_string_and_bar_frequency_formulas(self):
        fundamental = note_frequency(57)
        strings = string_mode_frequencies(fundamental)
        bars = bar_mode_frequencies(fundamental)
        self.assertAlmostEqual(strings[0], fundamental)
        self.assertAlmostEqual(strings[8], fundamental * 9)
        self.assertAlmostEqual(bars[0], fundamental * BAR_FACTOR * 1.5**2)
        self.assertAlmostEqual(bars[8], fundamental * BAR_FACTOR * 9.5**2)

    def test_alternate_tuning_precedes_mode_ratios(self):
        tuning = [
            1,
            16 / 15,
            9 / 8,
            6 / 5,
            5 / 4,
            4 / 3,
            64 / 45,
            3 / 2,
            8 / 5,
            5 / 3,
            16 / 9,
            15 / 8,
        ]
        fundamental = note_frequency(61, alternate_ratios=tuning)
        self.assertAlmostEqual(fundamental, mtof(60) * 16 / 15)
        self.assertAlmostEqual(string_mode_frequencies(fundamental)[1], fundamental * 2)

    def test_firmware_uses_two_sources_and_unmutes_non_cave_modes(self):
        dsp = (REPO_ROOT / "Wingie2.dsp").read_text(encoding="utf-8")
        generated = (REPO_ROOT / "Wingie2/Wingie2.cpp").read_text(encoding="utf-8")
        firmware = (REPO_ROOT / "Wingie2/Wingie2.ino").read_text(encoding="utf-8")
        midi = (REPO_ROOT / "Wingie2/MIDI.ino").read_text(encoding="utf-8")
        control = (REPO_ROOT / "Wingie2/control.ino").read_text(encoding="utf-8")

        self.assertIn("ba.selectn(2, s)", dsp)
        self.assertNotIn("strings(note, n)", dsp)
        self.assertNotIn("bars(note, n)", dsp)
        self.assertNotIn('addHorizontalSlider("note0"', generated)
        self.assertIn("Mode[ch] == POLY_MODE ? 0 : 1", firmware)
        self.assertIn("return index + 1.0f;", firmware)
        self.assertIn("return barFactor * barIndex * barIndex;", firmware)
        self.assertIn("void unmute_channel_resonators(byte ch)", firmware)
        self.assertIn("if (Mode[ch] == POLY_MODE)", firmware)
        self.assertIn("unmute_channel_resonators(ch);", firmware)
        self.assertIn("set_channel_note(ch, pitch);", midi)
        self.assertIn("apply_cave_bank_to_dsp(ch, cave);", control)


if __name__ == "__main__":
    unittest.main()
