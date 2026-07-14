#!/usr/bin/env python3
from dataclasses import dataclass
import math


@dataclass
class ModeState:
    q: float = 0.0
    p: float = 0.0
    running_peak: float = 0.0
    input_gain: float = 1.0
    effective_rho: float = 0.0
    rho_user: float = 0.0
    sine: float = 0.0
    cosine: float = 1.0
    sample_index: int = 0


@dataclass(frozen=True)
class ModeFrame:
    q: float
    p: float
    energy: float
    running_peak: float
    control_peak: float
    input_gain: float
    rho_user: float
    effective_rho: float
    output: float


def user_rho(t60, sample_rate):
    if not t60 > 0.0 or sample_rate <= 0:
        raise ValueError("t60 and sample_rate must be positive")
    return 0.001 ** (1.0 / (t60 * sample_rate))


def step_mode(
    state,
    excitation,
    frequency,
    t60,
    energy_limit,
    rho_guard,
    enabled=True,
    output_gain=1.0,
    sample_rate=44100,
    block_size=32,
):
    if not energy_limit > 0.0:
        raise ValueError("energy_limit must be positive")
    if not 0.0 < rho_guard < 1.0:
        raise ValueError("rho_guard must be between zero and one")
    if block_size <= 0:
        raise ValueError("block_size must be positive")

    control_peak = state.running_peak
    if state.sample_index % block_size == 0:
        state.rho_user = user_rho(t60, sample_rate)
        theta = 2.0 * math.pi * frequency / sample_rate
        state.sine = math.sin(theta)
        state.cosine = math.cos(theta)
        overload = max(1.0, control_peak / energy_limit)
        state.input_gain = 1.0 / overload if enabled else 1.0
        state.effective_rho = (
            min(state.rho_user, rho_guard)
            if enabled and overload > 1.0
            else state.rho_user
        )
        state.running_peak = 0.0

    previous_q = state.q
    previous_p = state.p
    state.q = state.effective_rho * (
        state.sine * previous_p + state.cosine * previous_q
    )
    state.p = state.input_gain * excitation + state.effective_rho * (
        state.cosine * previous_p - state.sine * previous_q
    )
    energy = state.q * state.q + state.p * state.p
    state.running_peak = max(state.running_peak, energy)
    output = output_gain * (2.0 / state.rho_user) * state.q
    state.sample_index += 1

    values = (
        state.q,
        state.p,
        energy,
        state.running_peak,
        control_peak,
        state.input_gain,
        state.rho_user,
        state.effective_rho,
        output,
    )
    if not all(math.isfinite(value) for value in values):
        raise FloatingPointError("non-finite modal controller state")
    return ModeFrame(*values)


def render_mode(excitation, **parameters):
    state = ModeState()
    return [step_mode(state, sample, **parameters) for sample in excitation]
