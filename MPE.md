# Wingie2 MPE

Wingie2 implements the MIDI Polyphonic Expression 1.1 member-channel note, note-ownership, and
Pitch Bend path over its existing MIDI 1.0 input, as a single always-on Lower Zone with per-note
left/right alternating engine assignment. This scope does not map Channel Pressure or CC 74 to
synthesis parameters.

## Zone and Routing

MPE is always on: there is no enable switch. At startup Wingie2 claims one Lower Zone — Manager
Channel 1 with 6 Member Channels 2–7. RPN 6 MPE Configuration Messages on Channel 1 may resize or
disable the Zone at runtime; a restart restores the startup layout. MCM received on Channel 16
(Upper Zone) is consumed but ignored, so a dual-zone controller must be reconfigured to a single
zone or its upper-zone notes on Channels 13–15 will not sound.

Channels 8–16 keep the conventional Left/Right/Both routing (factory defaults: Left 8, Right 9,
Both 10). Channel 16 global-settings CC and Channel 14/15 Cave-frequency CC remain reachable.

## Note Assignment and Pitch Bend

- Each Note On in the Zone is assigned to one engine side, alternating left/right in arrival
  order (the same free-running alternation as the conventional Both route). A side in Cave Mode
  is skipped: all notes land on the sounding side; if both sides are in Cave Mode, notes are
  ignored.
- Poly Mode binds each Note On to one of three voices on its assigned side. A free voice is used
  first; when all three are active, the oldest voice is replaced.
- Member Pitch Bend changes every active voice owned by that Member Channel, on either side.
- Manager Pitch Bend (Channel 1) is global: it changes every active MPE voice on both sides.
- Manager CC (Channel 1) is applied to both sides; Member CC is not mapped.
- String, Bar, and Ratio use the latest Note On as a monophonic owner on the assigned side. Member
  control stops after its matching Note Off, while the last pitch remains latched and Manager
  Pitch Bend remains active.
- Note Off is routed by (channel, note) ownership across both sides.

Member Pitch Bend defaults to ±48 semitones and Manager Pitch Bend defaults to ±2 semitones. RPN 0
is accepted on Manager and Member Channels; a Member range received on one channel applies to
every Member. Pitch Bend state is tracked before Note On so an MPE source can establish a note's
initial microtonal offset.

## Tuning

Pitch Bend is applied after the base note has been resolved through Wingie2's current A3 and
tuning. An MPE source can therefore provide alternate tuning by sending per-note Pitch Bend before
Note On. Select Standard internal tuning when the source should be the only tuning authority. If
an internal alternate tuning remains enabled, the internal interval and MPE offset are both
applied.

## Migrating from the dual-zone firmware

- MPE no longer has an on/off preference; the USB configuration page's MPE field is removed and
  `get_settings` no longer reports `mpe_enabled` (config schema 4).
- The Zone occupies Channels 1–7, so conventional routes saved as Channels 1/2/3 no longer apply.
  Set Left/Right/Both to Channels 8+ in the USB configuration page (factory defaults for new
  devices are 8/9/10; existing saved settings are not rewritten by the firmware).
- A conventional (non-MPE) controller should use Channel 1 or Channels 8–16. On Channels 2–7 its
  Pitch Bend is interpreted as MPE Member Pitch Bend (default ±48 semitones) and its CC is not
  mapped.
