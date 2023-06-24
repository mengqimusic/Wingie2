# Alternate Tuning Support

By default, Wingie2 uses standard western tuning (equal temperament). As of version 3.1, eight additional tunings are available. These are the available tunings:

 * Centaur
 * Harp of New Albion
 * Modified Harp of New Albion (septimal 7th)
 * Zeta Centauri
 * Well Tuned Piano
 * interleaved 1-3-5-7 hexanies
 * Meta Slendero (Meru C)
 * Marwa extended

 Alternate tuning is enabled in two ways: by holding down the left mode button when starting the device; or using MIDI. The advantage of using MIDI is that it is unnecessary to restart the device, allowing the tuning to be changed on the fly.

 ## Enabling/Disabling Alternate Tuning at Startup

 Hold down the **left mode button** before plugging in the USB cable.The positions of the sliders will determine which alternate tuning will be used, as follows:

 | Left slider | Middle slider| right slider| tuning |
 |---|---|---|---|
 | down | down | down | Centaur |
 | down | down | up| New Albion |
 | down | up | down | New Albion (mod) |
 | down | up | up | Zeta Centauri |
 | up | down | down| Well Tuned Piano |
 | up | down | up | interleaved hexanies |
 | up | up | down | Meta Slendro |
 | up | up | up | Marwa extended |

 [For nerds: this method treats the sliders as base two digits, where the right slider represents the 1's column, the middle slider represents the 2's column, and the left slider represents the 4's column. Thus, all sliders down indicates 0, all sliders up indicates 7, etc.]

 To disable alternate tuning at startup, hold down the **right mode button**. The device will then start using standard tuning

 ## Enabling/Disabling Alternate Tuning using MIDI

 | channel| CC| value| tuning|
 | --- | ---| --- | --- |
 | 13 | 23 | 0 | Disable alternate tuning (return to standard tuning) |
 |-|-|1| Centaur|
 |-|-|2| New Albion|
 |-|-|3| New Albion (mod) |
 |-|-|4| Zeta Centauri|
 |-|-|5| Well Tuned Piano|
 |-|-|6| interleaved hexanies|
 |-|-|7| Meta Slendro|
 |-|-|8| Marwa extended|

## Saving the tuning setting

The current tuning configuration is saved along with other settings when you hold down both mode buttons simultaneously, has described in the manual. On restart, your tuning configuration will be restored.