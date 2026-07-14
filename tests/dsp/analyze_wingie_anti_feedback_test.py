#!/usr/bin/env python3
import unittest

from analyze_wingie_anti_feedback import validate


def channel(difference_peak=0.0, candidate_rms=0.5, reference_rms=1.0,
            tail_candidate_rms=0.25, tail_reference_rms=0.5):
    return {
        "difference_peak": difference_peak,
        "candidate_rms": candidate_rms,
        "reference_rms": reference_rms,
        "tail_candidate_rms": tail_candidate_rms,
        "tail_reference_rms": tail_reference_rms,
    }


class ProductValidationTest(unittest.TestCase):
    def test_transparency_rejects_any_difference(self):
        report = {"channels": [channel(), channel(difference_peak=0.01)]}
        with self.assertRaises(ValueError):
            validate("normal", report)

    def test_sustained_requires_both_channels_to_reduce_rms(self):
        passing = {"channels": [channel(0.1), channel(0.1)]}
        validate("sustained", passing)
        failing = {"channels": [channel(0.1), channel(0.1,
                                                candidate_rms=1.0)]}
        with self.assertRaises(ValueError):
            validate("sustained", failing)

    def test_feedback_requires_both_recovery_tails_to_improve(self):
        passing = {"channels": [channel(0.1), channel(0.1)]}
        validate("feedback", passing)
        failing = {"channels": [channel(0.1), channel(
            0.1, tail_candidate_rms=0.5, tail_reference_rms=0.5)]}
        with self.assertRaises(ValueError):
            validate("feedback", failing)

    def test_isolation_rejects_target_miss_and_crosstalk(self):
        validate("isolation", {"channels": [channel(0.1), channel()]})
        with self.assertRaises(ValueError):
            validate("isolation", {"channels": [channel(), channel()]})
        with self.assertRaises(ValueError):
            validate("isolation", {"channels": [channel(0.1),
                                                   channel(0.01)]})

    def test_rapid_notes_records_without_transparency_gate(self):
        validate("rapid-notes", {"channels": [channel(), channel(0.01)]})


if __name__ == "__main__":
    unittest.main()
