#!/usr/bin/env python3
import argparse
import array
import json
import math
import pathlib


SCENARIOS = (
    "normal",
    "transient",
    "sustained",
    "rapid-notes",
    "mode-changes",
    "feedback",
    "isolation",
)


def load_stereo(path):
    values = array.array("f")
    file_path = pathlib.Path(path)
    with file_path.open("rb") as stream:
        values.fromfile(stream, file_path.stat().st_size // values.itemsize)
    if len(values) % 2:
        raise ValueError("render contains a partial stereo frame")
    if not all(math.isfinite(value) for value in values):
        raise ValueError("render contains non-finite output")
    return values


def rms(values):
    return math.sqrt(sum(value * value for value in values) / max(1, len(values)))


def analyze(reference_path, candidate_path):
    reference = load_stereo(reference_path)
    candidate = load_stereo(candidate_path)
    if len(reference) != len(candidate):
        raise ValueError("render lengths differ")
    difference = [candidate[index] - reference[index]
                  for index in range(len(reference))]
    channels = []
    for channel in range(2):
        channel_reference = reference[channel::2]
        channel_candidate = candidate[channel::2]
        channel_difference = difference[channel::2]
        tail_start = max(0, len(channel_reference) - 10 * 44100)
        channels.append({
            "candidate_peak": max(abs(value) for value in channel_candidate),
            "candidate_rms": rms(channel_candidate),
            "difference_peak": max(abs(value) for value in channel_difference),
            "difference_rms": rms(channel_difference),
            "reference_peak": max(abs(value) for value in channel_reference),
            "reference_rms": rms(channel_reference),
            "tail_candidate_rms": rms(channel_candidate[tail_start:]),
            "tail_reference_rms": rms(channel_reference[tail_start:]),
        })
    return {"frames": len(reference) // 2, "channels": channels}


def validate(scenario, report):
    channels = report["channels"]
    if scenario in ("normal", "transient", "mode-changes"):
        if any(channel["difference_peak"] != 0.0 for channel in channels):
            raise ValueError(f"{scenario} is not bit-exact with bypass")
    elif scenario == "sustained":
        if not all(channel["difference_peak"] > 0.0 and
                   channel["candidate_rms"] < channel["reference_rms"]
                   for channel in channels):
            raise ValueError("sustained input did not activate both controllers")
    elif scenario == "feedback":
        if not all(channel["difference_peak"] > 0.0 and
                   channel["tail_candidate_rms"] <
                   channel["tail_reference_rms"]
                   for channel in channels):
            raise ValueError("feedback recovery did not improve both channels")
    elif scenario == "isolation":
        if channels[0]["difference_peak"] == 0.0:
            raise ValueError("isolation stimulus did not activate its target")
        if channels[1]["difference_peak"] != 0.0:
            raise ValueError("isolation stimulus changed the unaffected channel")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("scenario", choices=SCENARIOS)
    parser.add_argument("reference")
    parser.add_argument("candidate")
    arguments = parser.parse_args()
    report = analyze(arguments.reference, arguments.candidate)
    validate(arguments.scenario, report)
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
