#!/usr/bin/env python3
import argparse
import array
import json
import math
import pathlib

from anti_feedback_reference import ModeState, step_mode


RECORD_WIDTH = 7
FIELD_NAMES = ("input", "frequency", "q", "p", "peak", "gain", "rho")
SCENARIO_PARAMETERS = {
    "inactive": {"energy_limit": 64.0, "enabled": True},
    "overload-recovery": {"energy_limit": 0.25, "enabled": True},
    "sustained": {"energy_limit": 0.25, "enabled": True},
    "bypass": {"energy_limit": 0.25, "enabled": False},
}


def load_records(path):
    values = array.array("f")
    with pathlib.Path(path).open("rb") as stream:
        values.fromfile(stream, pathlib.Path(path).stat().st_size // values.itemsize)
    if len(values) % RECORD_WIDTH:
        raise ValueError("contract render has a partial record")
    return [dict(zip(FIELD_NAMES, values[index:index + RECORD_WIDTH]))
            for index in range(0, len(values), RECORD_WIDTH)]


def analyze(scenario, path):
    records = load_records(path)
    if len(records) != 512:
        raise ValueError(f"expected 512 records, found {len(records)}")
    parameters = SCENARIO_PARAMETERS[scenario]
    state = ModeState()
    maxima = {field: 0.0 for field in ("q", "p", "peak", "gain", "rho")}
    active_blocks = set()
    for index, record in enumerate(records):
        frame = step_mode(
            state,
            record["input"],
            frequency=record["frequency"],
            t60=10.0,
            energy_limit=parameters["energy_limit"],
            rho_guard=0.5,
            enabled=parameters["enabled"],
        )
        expected = {
            "q": frame.q,
            "p": frame.p,
            "peak": frame.running_peak,
            "gain": frame.input_gain,
            "rho": frame.effective_rho,
        }
        for field, expected_value in expected.items():
            error = abs(record[field] - expected_value)
            maxima[field] = max(maxima[field], error)
            tolerance = 1.0e-5 * max(1.0, abs(expected_value))
            if error > tolerance:
                raise ValueError(
                    f"{field} mismatch at sample {index}: "
                    f"actual={record[field]} expected={expected_value}"
                )
        if record["gain"] < 1.0:
            active_blocks.add(index // 32)
        if index % 32:
            previous = records[index - 1]
            if record["gain"] != previous["gain"] or record["rho"] != previous["rho"]:
                raise ValueError(f"control changed inside block at sample {index}")
        if not all(math.isfinite(value) for value in record.values()):
            raise ValueError(f"non-finite record at sample {index}")
    if scenario in ("overload-recovery", "sustained") and not active_blocks:
        raise ValueError("controller never activated")
    if scenario in ("inactive", "bypass") and active_blocks:
        raise ValueError("controller activated while inactive or bypassed")
    return {
        "scenario": scenario,
        "samples": len(records),
        "active_blocks": sorted(active_blocks),
        "max_reference_error": maxima,
        "min_gain": min(record["gain"] for record in records),
        "min_rho": min(record["rho"] for record in records),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("scenario", choices=SCENARIO_PARAMETERS)
    parser.add_argument("render")
    arguments = parser.parse_args()
    print(json.dumps(analyze(arguments.scenario, arguments.render),
                     indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
