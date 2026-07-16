# Alternate Tuning Support

By default, Wingie2 uses standard western tuning (equal temperament). As of version 3.1, eight additional tunings are available. These are the currently available tunings (other tunings are possible, but require a source code change and a new firmware build):

 * Centaur (Kraig Grady)
 * Harp of New Albion (Terry Riley)
 * Carlos Harmonic (Wendy Carlos)
 * Well Tuned Piano (La Monte Young)
 * Meta Slendro (Grady/Wilson)
 * bihexany (Gene Ward Smith)
 * hexachordal dodecaphonic (Paul Erlich) 
 * Augmented[12] (Mike Smith, Paul Erlich)

Alternate tuning is enabled in three ways: by holding down the **left Mode button** when starting the device, using MIDI, or using the USB configuration page. MIDI and USB configuration allow the tuning to be changed without restarting the device.

Alternate tuning also affects Caves mode; see below for details.

 Alternate tuning honors the Global Tuning (A3) setting.

## Enabling/Disabling Alternate Tuning at Startup

Hold down the **left Mode button** before plugging in the USB cable.The positions of the sliders will determine which alternate tuning will be used, as follows:

 | Left slider | Middle slider| right slider| tuning |
 |---|---|---|---|
 | down | down | down | Centaur |
 | down | down | up| Harp of New Albion |
 | down | up | down | Carlos Harmonic |
 | down | up | up | Well Tuned Piano |
 | up | down | down| Meta Slendro |
 | up | down | up | bihexany |
 | up | up | down | hexachordal dodecaphonic |
 | up | up | up | Augmented[12] |

[For nerds: this method treats the sliders as base-2 digits, where the right slider represents the 1's column, the middle slider represents the 2's column, and the left slider represents the 4's column. Thus, all sliders down indicates 0, all sliders up indicates 7, etc.]

To disable alternate tuning at startup, hold down the **right Mode button**. The device will then start using standard tuning.

## Enabling/Disabling Alternate Tuning using MIDI

 | channel| CC| value| tuning|
 | --- | ---| --- | --- |
 | 13 | 23 | 0 | Disable alternate tuning (return to standard tuning) |
 |-|-|1| Centaur |
 |-|-|2| Harp of New Albion |
 |-|-|3| Carlos Harmonic |
 |-|-|4| Well Tuned Piano |
 |-|-|5| Meta Slendro |
 |-|-|6| bihexany |
 |-|-|7| hexachordal dodecaphonic |
 |-|-|8| Augmented[12] |

## Enabling/Disabling Alternate Tuning using USB configuration

The current USB configuration page requires Wingie2 `config schema 3` firmware. When it connects, it
reads the current Tuning and Global Tuning (A3) values as part of one complete device snapshot. Select
Standard to disable alternate tuning, or select one of the eight alternate tunings to enable it. A
valid edit changes the running instrument immediately; there is no Apply step.

The page does not continuously synchronize changes made from the hardware or MIDI. Use Refresh to
read a new complete snapshot after changing tuning outside the page.


 ## Caves in Alternate Tuning

 When you enable alternate tuning, the caves are also tuned to match the tuning you selected. In
`config schema 3` firmware, Cave frequencies cover `16.00–16000.00 Hz` and are edited and stored at
`0.01 Hz` resolution.
 
 In order to accommodate all 12 pitches, the caves are arranged so that the left channel uses the even-numbered scale tones, covering one and one-third octaves:

    C, D, E, F#, G#, A#, C', D', E'

The right channel uses the odd-numbered scale tones, also covering one and one-third octaves:

    C#, D#, F, G, A, B, A#, C#', D#', F'

The three-position toggles switch between three octaves, similar to Poly, String and Bar modes. However, the left and right caves are always in the same octave, so that all scale pitches are covered across the two sides.


## Saving the tuning setting

Changing Tuning or Global Tuning (A3) from the USB configuration page affects the running instrument
immediately but does not write flash automatically. Save the current tuning and related Cave settings
either by holding down both Mode buttons simultaneously, as described in the manual, or by using the
confirmed Save to Flash action on the USB configuration page. MIDI tuning changes also take effect
immediately; enabling alternate tuning preserves the unquantized Cave backup used for later recovery.
On restart, the saved tuning configuration is restored.
