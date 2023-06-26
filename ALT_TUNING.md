# Alternate Tuning Support

By default, Wingie2 uses standard western tuning (equal temperament). As of version 3.1, eight additional tunings are available. These are the currently available tunings (other tunings are possible, but require a source code change and a new firmware buid):

 * Centaur (Kraig Grady)
 * Harp of New Albion (Terry Riley)
 * Carlos Harmonic (Wendy Carlos)
 * Well Tuned Piano (La Monte Young)
 * Meta Slendro (Grady/Wilson)
 * bihexany (Gene Ward Smith)
 * hexachordal dodecaphonic (Paul Erlich) 
 * Augmented[12] (Mike Smith, Paul Erlich)

 Alternate tuning is enabled in two ways: by holding down the **left Mode button** when starting the device; or using MIDI. The advantage of using MIDI is that it is unnecessary to restart the device, allowing the tuning to be changed on the fly.

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

## Saving the tuning setting

The current tuning configuration is saved along with other settings when you hold down both Mode buttons simultaneously, has described in the manual. On restart, your tuning configuration will be restored.