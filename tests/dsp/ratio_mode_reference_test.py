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
    ratio_poly_voice_frequencies,
    string_mode_frequencies,
)


REPO_ROOT = Path(__file__).resolve().parents[2]


def extract_braced_block(source, marker):
    marker_index = source.index(marker)
    opening_index = source.index("{", marker_index)
    depth = 0
    for index in range(opening_index, len(source)):
        character = source[index]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[opening_index : index + 1]
    raise AssertionError(f"unterminated block after {marker!r}")


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
        self.assertIn('pr0 = vslider("poly_pitch_ratio_0"', source)
        self.assertIn("a = (pn0 : mtof) * pr0;", source)
        self.assertNotIn("ratio_mode_ratio_", generated)
        self.assertNotIn('addHorizontalSlider("note0"', generated)
        self.assertIn('addVerticalSlider("poly_pitch_ratio_0"', generated)
        self.assertIn("Mode[ch] == POLY_MODE ? 0 : 1", firmware)
        self.assertIn("configured_note_frequency(midiNote) * wingie_mpe::pitchRatio(currentPitchBend[ch])", firmware)
        self.assertIn("return index + 1.0f;", firmware)
        self.assertIn("return barFactor * barIndex * barIndex;", firmware)
        self.assertIn("fundamental * pitched_mode_ratio(Mode[ch], index)", firmware)
        self.assertIn("set_conventional_channel_note(ch, channel, pitch);", midi)
        self.assertIn("value == 127 ? RATIO_MODE : (value >> 5)", midi)

    def test_faust_compute_uses_iram_with_local_literals(self):
        build_options = (REPO_ROOT / "Wingie2/build_opt.h").read_text(encoding="utf-8")
        generated = (REPO_ROOT / "Wingie2/Wingie2.cpp").read_text(encoding="utf-8")

        self.assertEqual(build_options.strip(), "-mtext-section-literals")
        self.assertIn('#include "esp_attr.h"', generated)
        self.assertIn("virtual void IRAM_ATTR compute", generated)

    def test_only_cave_mode_applies_cave_mutes(self):
        firmware = (REPO_ROOT / "Wingie2/Wingie2.ino").read_text(encoding="utf-8")
        cave_block = extract_braced_block(firmware, "void apply_cave_bank_to_dsp")
        pitched_block = extract_braced_block(firmware, "void apply_pitched_mode_channel")
        current_mode_block = extract_braced_block(firmware, "void apply_current_mode_parameters")

        self.assertIn("cm_ms[ch][bank][index]", cave_block)
        self.assertIn("unmute_channel_resonators(ch);", pitched_block)
        self.assertNotIn("cm_ms", pitched_block)
        self.assertIn("if (Mode[ch] == POLY_MODE || Mode[ch] == RATIO_MODE)", current_mode_block)
        self.assertIn("unmute_channel_resonators(ch);", current_mode_block)
        self.assertIn("else if (Mode[ch] == CAVE_MODE)", current_mode_block)

    def test_ratio_poly_uses_cave_path_voice_groups(self):
        mpe = (REPO_ROOT / "Wingie2/MPE.ino").read_text(encoding="utf-8")
        control = (REPO_ROOT / "Wingie2/control.ino").read_text(encoding="utf-8")
        serial_config = (REPO_ROOT / "Wingie2/serial_config.ino").read_text(encoding="utf-8")

        ratio_voice_block = extract_braced_block(mpe, "void apply_ratio_voice_pitch")
        self.assertIn("const byte index = 3 * voice + k;", ratio_voice_block)
        self.assertIn("fundamental * ratio_profile.ratios[index]", ratio_voice_block)
        self.assertIn("configured_note_frequency(state.note)", ratio_voice_block)
        self.assertIn("poly_total_bend(ch, voice)", ratio_voice_block)
        self.assertIn("cm_freq_set(ch, index, frequency);", ratio_voice_block)

        unmute_block = extract_braced_block(mpe, "void unmute_ratio_voice")
        self.assertIn("cm_mute_set(ch, 3 * voice + k, false);", unmute_block)

        # RATIO 与 POLY 共用声部分配/弯音语义：统一分发入口按 Mode[ch] 分流。
        self.assertIn("void cycle_voice_note(byte ch, byte noteValue)", mpe)
        self.assertIn("void apply_all_voice_pitch(byte ch)", mpe)
        self.assertIn("if (Mode[ch] == RATIO_MODE) cycle_ratio_voice_note(ch, noteValue);", mpe)

        # 设备按键保持 Ratio 原有 +0/+12 偏移，而非 Poly 的 +12/+24。
        self.assertIn(
            "cycle_ratio_voice_note(ch, i + BASE_NOTE + oct[ch] * 12 + (ch ? 12 : 0));",
            control,
        )
        # Tap Sequencer 回放仅 STRING/BAR。
        self.assertIn("if (Mode[ch] != STRING_MODE && Mode[ch] != BAR_MODE) continue;", control)

        # ratio profile 更新时全声部重应用。
        self.assertIn("apply_all_ratio_voice_pitch(ch);", serial_config)

    def test_cave_bank_updates_only_when_octave_changes(self):
        control = (REPO_ROOT / "Wingie2/control.ino").read_text(encoding="utf-8")
        octave_change_block = extract_braced_block(
            control, "if (octPrev[ch] != oct[ch])"
        )

        self.assertIn("if (Mode[ch] == CAVE_MODE)", octave_change_block)
        self.assertIn("apply_cave_bank_to_dsp(ch, cave);", octave_change_block)

    def test_active_cave_bank_refreshes_after_tuning_changes(self):
        firmware = (REPO_ROOT / "Wingie2/Wingie2.ino").read_text(encoding="utf-8")
        tune_block = extract_braced_block(firmware, "bool tune_caves()")
        restore_block = extract_braced_block(firmware, "void restore_caves_to_unq()")

        active_apply = "if (Mode[ch] == CAVE_MODE) apply_cave_bank_to_dsp(ch, oct[ch] + 1);"
        self.assertIn(active_apply, tune_block)
        self.assertIn(active_apply, restore_block)

    def test_startup_initializes_all_mode_parameters(self):
        control = (REPO_ROOT / "Wingie2/control.ino").read_text(encoding="utf-8")

        self.assertIn(
            "set_channel_note(0, BASE_NOTE + oct[0] * 12);\n"
            "  set_channel_note(1, BASE_NOTE + oct[1] * 12 + 12);\n"
            "  apply_current_mode_parameters(0);\n"
            "  apply_current_mode_parameters(1);",
            control,
        )

    def test_ratio_mode_cycles_all_led_colors(self):
        firmware = (REPO_ROOT / "Wingie2/Wingie2.ino").read_text(encoding="utf-8")
        control = (REPO_ROOT / "Wingie2/control.ino").read_text(encoding="utf-8")
        led_block = extract_braced_block(firmware, "void set_mode_led")
        mode_change_block = extract_braced_block(
            control,
            "if (modeChangingFromKeys[ch] || modeChangingFromMIDI[ch])",
        )
        channel_mode_block = extract_braced_block(
            firmware,
            "void apply_channel_mode_change",
        )
        save_blink_end_block = extract_braced_block(control, "if (!led_blink)")
        ratio_cycle_block = extract_braced_block(
            control,
            "if (!save_routine_flag && !led_blink)",
        )

        self.assertIn("#define RATIO_LED_INTERVAL 20", firmware)
        self.assertIn("ledColor[4]", firmware)
        self.assertIn("uint8_t ratio_led_phase[2] = {0, 0};", firmware)
        self.assertIn("ledColor[ratio_led_phase[ch]]", led_block)
        self.assertIn("apply_channel_mode_change(ch);", mode_change_block)
        for reset_block in (channel_mode_block, save_blink_end_block):
            phase_reset = reset_block.index("ratio_led_phase[ch] = 0;")
            timer_reset = reset_block.index("ratio_led_timer[ch] = ")
            render = reset_block.index("set_mode_led(ch);")
            self.assertLess(phase_reset, render)
            self.assertLess(timer_reset, render)
        self.assertIn("ratio_led_timer[ch] = changedAt;", channel_mode_block)
        self.assertIn("ratio_led_timer[ch] = currentMillis;", save_blink_end_block)
        self.assertIn(
            "currentMillis - ratio_led_timer[ch] >= RATIO_LED_INTERVAL",
            ratio_cycle_block,
        )
        self.assertIn("ratio_led_timer[ch] = currentMillis;", ratio_cycle_block)
        self.assertIn(
            "ratio_led_phase[ch] = (ratio_led_phase[ch] + 1) & 3;",
            ratio_cycle_block,
        )
        self.assertIn("set_mode_led(ch);", ratio_cycle_block)
        self.assertNotIn("ratio_led_on", firmware + control)


class RatioPolyVoiceFrequenciesTest(unittest.TestCase):
    def test_voice_groups_use_their_own_ratio_slice(self):
        fundamental = 100.0
        ratios = [1, 2, 3, 4, 5, 6, 7, 8, 9]
        self.assertEqual(ratio_poly_voice_frequencies(fundamental, ratios, 0), [100.0, 200.0, 300.0])
        self.assertEqual(ratio_poly_voice_frequencies(fundamental, ratios, 1), [400.0, 500.0, 600.0])
        self.assertEqual(ratio_poly_voice_frequencies(fundamental, ratios, 2), [700.0, 800.0, 900.0])

    def test_voice_uses_its_slice_not_the_whole_profile(self):
        fundamental = note_frequency(69)
        ratios = [0.125, 0.5, 1, 1.5, 2, 3, 4, 5, 9]
        self.assertAlmostEqual(ratio_poly_voice_frequencies(fundamental, ratios, 1)[0], fundamental * 1.5)
        self.assertAlmostEqual(ratio_poly_voice_frequencies(fundamental, ratios, 2)[2], fundamental * 9)
        self.assertEqual(len(ratio_poly_voice_frequencies(fundamental, ratios, 0)), 3)

    def test_clamps_to_frequency_limits_per_voice(self):
        frequencies = ratio_poly_voice_frequencies(1000, [0.001, 0.125, 1, 2, 4, 8, 16, 32, 100], 2)
        self.assertEqual(frequencies[0], FREQUENCY_MAX)
        self.assertEqual(frequencies[2], FREQUENCY_MAX)
        low = ratio_poly_voice_frequencies(1000, [0.001, 0.125, 1, 2, 4, 8, 16, 32, 100], 0)
        self.assertEqual(low[0], FREQUENCY_MIN)
        self.assertEqual(low[2], 1000.0)

    def test_boundary_values_pass_unclamped(self):
        ratios = [0.016, 16.0, 1, 1, 1, 1, 1, 1, 1]
        frequencies = ratio_poly_voice_frequencies(1000.0, ratios, 0)
        self.assertEqual(frequencies[0], FREQUENCY_MIN)
        self.assertEqual(frequencies[1], FREQUENCY_MAX)

    def test_rejects_invalid_fundamental(self):
        ratios = [1] * 9
        for bad in (0.0, -440.0, math.nan, math.inf, -math.inf):
            with self.assertRaises(ValueError):
                ratio_poly_voice_frequencies(bad, ratios, 0)

    def test_rejects_invalid_ratio_profile(self):
        with self.assertRaises(ValueError):
            ratio_poly_voice_frequencies(440, [1, 2, 3], 0)
        with self.assertRaises(ValueError):
            ratio_poly_voice_frequencies(440, [1, 2, 3, 4, math.nan, 6, 7, 8, 9], 0)
        with self.assertRaises(ValueError):
            ratio_poly_voice_frequencies(440, [1, 2, 3, 4, math.inf, 6, 7, 8, 9], 0)
        with self.assertRaises(ValueError):
            ratio_poly_voice_frequencies(440, [1, 2, 3, 4, 0, 6, 7, 8, 9], 0)
        with self.assertRaises(ValueError):
            ratio_poly_voice_frequencies(440, [1, 2, 3, 4, -5, 6, 7, 8, 9], 0)

    def test_rejects_invalid_voice(self):
        ratios = [1] * 9
        for bad in (-1, 3, 4, 1.5, "0", None, True):
            with self.assertRaises(ValueError):
                ratio_poly_voice_frequencies(440, ratios, bad)


if __name__ == "__main__":
    unittest.main()
