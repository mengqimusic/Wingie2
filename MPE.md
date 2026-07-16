# Wingie2 MPE

Wingie2 implements the MIDI Polyphonic Expression 1.1 Zone, note-ownership, and Pitch Bend path over
its existing MIDI 1.0 input. This scope does not map Channel Pressure or CC 74 to synthesis parameters.

## Zones and Routing

The saved startup setting is off by default. When enabled, the default dual-zone layout is:

| Side | Manager | Members |
| --- | --- | --- |
| Left | Channel 1 | Channels 2–4 |
| Right | Channel 16 | Channels 15–13 |

RPN 6 MPE Configuration Messages on Channel 1 or 16 may resize or disable the corresponding Zone at
runtime. The most recent overlapping Zone assignment wins. A claimed MPE channel is not also interpreted
through the conventional Left/Right/Both routing; unclaimed channels retain the conventional behavior.

## Notes and Pitch Bend

- Poly Mode binds each Note On to one of three voices on its side. A free voice is used first; when all
  three are active, the oldest voice is replaced.
- Member Pitch Bend changes every active voice currently owned by that Member Channel.
- Manager Pitch Bend changes all three voices on the corresponding side.
- String, Bar, and Ratio use the latest Note On as a monophonic owner. Member control stops after its
  matching Note Off, while the last pitch remains latched and Manager Pitch Bend remains active.
- Cave ignores Note and Pitch Bend messages on claimed MPE channels.

Member Pitch Bend defaults to ±48 semitones and Manager Pitch Bend defaults to ±2 semitones. RPN 0 is
accepted on Manager and Member Channels; a Member range received on one channel applies to every Member
in that Zone. Pitch Bend state is tracked before Note On so an MPE source can establish a note's initial
microtonal offset.

## Tuning

Pitch Bend is applied after the base note has been resolved through Wingie2's current A3 and tuning. An
MPE source can therefore provide alternate tuning by sending per-note Pitch Bend before Note On. Select
Standard internal tuning when the source should be the only tuning authority. If an internal alternate
tuning remains enabled, the internal interval and MPE offset are both applied.
