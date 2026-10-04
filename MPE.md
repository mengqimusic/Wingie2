# Wingie2 MPE

Wingie2 implements the MIDI Polyphonic Expression 1.1 member-channel note, note-ownership, and
Pitch Bend path over its existing MIDI 1.0 input, as an optional single Lower Zone with per-note
left/right alternating engine assignment. Member Channel Pressure (0xD0) lengthens decay on
the owning side by summing per-note boosts (Poly/Ratio 2s each, up to 6s per side;
String/Bar 6s for the single owner); member CC 74 is consumed but not mapped.

## The MPE Switch

MPE is governed by a single switch, exposed as `mpe_enabled` in the USB configuration page
(config schema 6) and stored in flash. Factory default: **off**. The switch is the only zone
authority: MCM (RPN 6 on Channel 1) may resize the zone while the switch is on, and is consumed
but ignored while the switch is off. A restart restores the switched layout.

### Switch off — conventional routing

No zone exists. Channels 1–16 are all conventional and follow the configurable Left/Right/Both
routing (factory defaults: Left 1, Right 2, Both 3). Notes on channels not assigned to a route
do not sound. Channel 13 tuning CC, Channel 14/15 Cave-frequency CC, and Channel 16
global-settings CC are all reachable.

### Switch on — standard Lower Zone

One Lower Zone claims every channel: Manager Channel 1, Member Channels 2–16. All notes and
Pitch Bend follow the MPE path below; the Left/Right/Both routes do not apply. Per-channel CC
beyond the manager is not mapped, which means the conventional control channels are not reachable
while MPE is on: tuning (13), Cave frequency (14/15), and global settings (16) are all consumed
by the zone. Use per-note Pitch Bend for tuning, and the USB configuration page for Cave and
global settings.

Because the zone covers all 16 channels, notes from dual-zone controllers also sound — both
zones' notes are merged into Wingie2's single zone (per-zone separation is lost, nothing is
dropped).

## Note Assignment and Pitch Bend

- Each Note On in the Zone is assigned to one engine side, alternating left/right in arrival
  order (the same free-running alternation as the conventional Both route). A side in Cave Mode
  is skipped: all notes land on the sounding side; if both sides are in Cave Mode, notes are
  ignored.
- Poly and Ratio Modes each bind a Note On to one of three voices on the assigned side. A free
  voice is used first; when all three are active, the oldest voice is replaced.
- Member Pitch Bend changes every active voice owned by that Member Channel, on either side.
- Manager Pitch Bend (Channel 1) is global: it changes every active MPE voice on both sides.
- Manager CC (Channel 1) is applied to both sides; Member CC 74 drives the second half of
  the pressure travel (below), other Member CCs are not mapped.
- String and Bar use the latest Note On as a monophonic owner on the assigned side. Member
  control stops after its matching Note Off, while the last pitch remains latched and Manager
  Pitch Bend remains active.
- Note Off is routed by (channel, note) ownership across both sides. A Note On with velocity 0
  is treated as Note Off.

Member Pitch Bend defaults to ±48 semitones and Manager Pitch Bend defaults to ±2 semitones. RPN 0
is accepted on Manager and Member Channels; a Member range received on one channel applies to
every Member. Pitch Bend state is tracked before Note On so an MPE source can establish a note's
initial microtonal offset.

## Per-note Expression (0xD0 + CC74)

Osmose-style MPE controllers send an onset burst (Channel Pressure, CC 74, then Pitch Bend) just
before Note On; Wingie2 latches both values per channel, so a note sounds with the
expression already established.

- **Pressure adds decay over the full key travel, summed per side**: the Osmose exports its
  pressure travel as two sequential 7-bit segments — Channel Pressure (0xD0) for 0–127, then
  member-channel CC 74 rising from 0 once 0xD0 saturates (measured: CC74>0 only while 0xD0
  is exactly 127; the Osmose has no per-key Y sensor, so CC74 carries no slide gesture).
  The two segments form one linear axis E = 0xD0 + CC74 ∈ 0–254, and boost per voice =
  depth × E/254, with depth 2s per voice in Poly/Ratio (three voices add, capped at 6s
  per side) and 6s for the single String/Bar owner and conventional channels. Full
  physical floor is full depth: one key floored is +2s (three floored keys +6s, capped);
  0xD0 saturation alone is half travel, +1s per key. Each voice tracks its owner
  channel's pressure and timbre, including after Note Off, so the tail follows the key
  as it lifts. Because the Faust graph has one `decay` slider per side, the three voice
  boosts are added and written into that slider, clamped to a 20s overall ceiling (the
  Decay fader itself spans 0.1–10s): with the fader at 10s, pressure can still add its
  full 6s. CC74 on conventional channels (l/r/both) feeds the same side path; CC74 on
  the Manager channel has no decay effect, mirroring 0xD0.
- **Why not per-voice t60 in the Faust graph**: `decay_boost_*` per-voice sliders watchdog-reset
  at boot (~10.5s, CPU0 Faust DSP Task starves IDLE). Retried on the current baseline after
  the revert: `-Os` still watchdog-resets, `-O2` overflows IRAM0 by 80 bytes. The budget is
  at the edge, so pressure widens the existing per-side `decay` slider instead; the DSP graph
  stays unchanged.
- Other Member CCs (besides CC 74) are not mapped to synthesis parameters.
- Conventional (non-MPE) Channel Pressure on a routed channel applies to that side as a whole
  (single slot, 6s depth like String/Bar).

## Tuning

Pitch Bend is applied after the base note has been resolved through Wingie2's current A3 and
tuning. An MPE source can therefore provide alternate tuning by sending per-note Pitch Bend before
Note On. Select Standard internal tuning when the source should be the only tuning authority. If
an internal alternate tuning remains enabled, the internal interval and MPE offset are both
applied.

## Migrating from the always-on firmware

- The MPE switch is back as `mpe_enabled` in the USB configuration page (config schema 6),
  persisted in flash, factory default off.
- With the switch on, the zone covers Channels 2–16 instead of the previous startup layout
  (Channels 2–7): the earlier silent drop on Channels 11–15 no longer exists.
- With the switch off, Channels 1–7 are conventional again (previously they were always claimed
  by the zone), so a non-MPE controller on Channel 1 behaves like any other conventional channel.
- The Upper-Zone dual-zone warning is gone: dual-zone controllers now sound on every channel,
  merged into the single zone.
- Factory default routing for fresh devices is Left 1, Right 2, Both 3. Existing saved settings
  are not rewritten by the firmware; change the channels in the configuration page if needed.
