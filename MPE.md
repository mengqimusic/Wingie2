# Wingie2 MPE

Wingie2 implements the MIDI Polyphonic Expression 1.1 member-channel note, note-ownership, and
Pitch Bend path over its existing MIDI 1.0 input, as an optional single Lower Zone with per-note
left/right alternating engine assignment. This scope does not map Channel Pressure or CC 74 to
synthesis parameters.

## The MPE Switch

MPE is governed by a single switch, exposed as `mpe_enabled` in the USB configuration page
(config schema 5) and stored in flash. Factory default: **off**. The switch is the only zone
authority: MCM (RPN 6 on Channel 1) may resize the zone while the switch is on, and is consumed
but ignored while the switch is off. A restart restores the switched layout.

### Switch off — conventional routing

No zone exists. Channels 1–16 are all conventional and follow the configurable Left/Right/Both
routing (factory defaults: Left 8, Right 9, Both 10). Notes on channels not assigned to a route
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
- Manager CC (Channel 1) is applied to both sides; Member CC is not mapped.
- String and Bar use the latest Note On as a monophonic owner on the assigned side. Member
  control stops after its matching Note Off, while the last pitch remains latched and Manager
  Pitch Bend remains active.
- Note Off is routed by (channel, note) ownership across both sides. A Note On with velocity 0
  is treated as Note Off.

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

## Migrating from the always-on firmware

- The MPE switch is back as `mpe_enabled` in the USB configuration page (config schema 5),
  persisted in flash, factory default off.
- With the switch on, the zone covers Channels 2–16 instead of the previous startup layout
  (Channels 2–7): the earlier silent drop on Channels 11–15 no longer exists.
- With the switch off, Channels 1–7 are conventional again (previously they were always claimed
  by the zone), so a non-MPE controller on Channel 1 behaves like any other conventional channel.
- The Upper-Zone dual-zone warning is gone: dual-zone controllers now sound on every channel,
  merged into the single zone.
