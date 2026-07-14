#!/usr/bin/env python3
import math
import random
import unittest

from anti_feedback_reference import ModeState, render_mode, step_mode, user_rho


class AntiFeedbackReferenceTest(unittest.TestCase):
    def setUp(self):
        self.parameters = {
            "frequency": 440.0,
            "t60": 10.0,
            "energy_limit": 0.25,
            "rho_guard": 0.5,
        }

    def test_inactive_controller_matches_bypass(self):
        randomizer = random.Random(20260714)
        excitation = [randomizer.uniform(-0.001, 0.001) for _ in range(4096)]
        enabled = render_mode(excitation, energy_limit=1.0e6,
                              frequency=1379.0, t60=7.5, rho_guard=0.5)
        bypassed = render_mode(excitation, energy_limit=1.0e6,
                               frequency=1379.0, t60=7.5, rho_guard=0.5,
                               enabled=False)
        self.assertEqual(enabled, bypassed)

    def test_overload_uses_previous_block_peak(self):
        excitation = [1.0] + [0.0] * 63
        frames = render_mode(excitation, **self.parameters)
        self.assertTrue(all(frame.input_gain == 1.0 for frame in frames[:32]))
        self.assertTrue(all(frame.effective_rho == frame.rho_user
                            for frame in frames[:32]))
        self.assertLess(frames[32].input_gain, 1.0)
        self.assertEqual(frames[32].effective_rho, self.parameters["rho_guard"])
        self.assertEqual(frames[32].control_peak, frames[31].running_peak)

    def test_gain_and_rho_bounds_remain_finite(self):
        excitation = [4.0 if index % 37 == 0 else -0.25
                      for index in range(8192)]
        frames = render_mode(excitation, **self.parameters)
        expected_user_rho = user_rho(self.parameters["t60"], 44100)
        for frame in frames:
            self.assertTrue(math.isfinite(frame.energy))
            self.assertGreaterEqual(frame.input_gain, 0.0)
            self.assertLessEqual(frame.input_gain, 1.0)
            self.assertGreater(frame.effective_rho, 0.0)
            self.assertLessEqual(frame.effective_rho, expected_user_rho)
            self.assertLess(expected_user_rho, 1.0)

    def test_modes_are_independent(self):
        overloaded = ModeState()
        quiet = ModeState()
        quiet_reference = ModeState()
        for index in range(256):
            loud_input = 2.0 if index < 64 else 0.0
            quiet_input = 0.001 if index == 0 else 0.0
            step_mode(overloaded, loud_input, **self.parameters)
            quiet_frame = step_mode(quiet, quiet_input, **self.parameters)
            reference_frame = step_mode(quiet_reference, quiet_input,
                                        **self.parameters, enabled=False)
            self.assertEqual(quiet_frame, reference_frame)

    def test_recovery_restores_user_decay_without_reset(self):
        excitation = [1.0] + [0.0] * 127
        frames = render_mode(excitation, **self.parameters)
        guarded_blocks = {
            index // 32 for index, frame in enumerate(frames)
            if frame.effective_rho == self.parameters["rho_guard"]
        }
        self.assertIn(1, guarded_blocks)
        recovery = frames[64]
        self.assertEqual(recovery.input_gain, 1.0)
        self.assertEqual(recovery.effective_rho, recovery.rho_user)
        self.assertGreater(recovery.energy, 0.0)

    def test_output_mute_does_not_change_detector(self):
        excitation = [0.5] * 96
        audible = render_mode(excitation, **self.parameters)
        muted = render_mode(excitation, **self.parameters, output_gain=0.0)
        for audible_frame, muted_frame in zip(audible, muted):
            self.assertEqual(audible_frame.energy, muted_frame.energy)
            self.assertEqual(audible_frame.input_gain, muted_frame.input_gain)
            self.assertEqual(audible_frame.effective_rho,
                             muted_frame.effective_rho)
            self.assertEqual(muted_frame.output, 0.0)
        self.assertTrue(any(frame.output != 0.0 for frame in audible))


if __name__ == "__main__":
    unittest.main()
