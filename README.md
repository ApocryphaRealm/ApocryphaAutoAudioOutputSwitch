# Auto Audio Output Switch

An SKSE plugin for Skyrim Special Edition 1.5.97, Anniversary Edition 1.6.1170 and Skyrim 1.7.x that lets the game
move to another audio output device without a restart.

**Built on Live Audio Output Switching SE** by Mern (Maarten Harms) - its engine-rebuild procedure is what makes the
switch work; this mod ports it to three game versions and adds a preferred device and a settings page. See Credits.

Skyrim chooses its output device once, at startup, and goes silent for the rest of the session if that device is
unplugged. With this plugin the game moves its sound:

- to another connected device when the one it plays on is unplugged, disabled or stops working,
- to the Windows default output when the default changes (`bSwitchOnDefaultChange=1`),
- to a preferred device whenever it is connected (`sPreferredDevice`, part of the device's name), and
- on demand, with Switch now on the settings page.

Settings: the Auto Audio Output Switch page in the Apocrypha Menu Framework (or SKSE Menu Framework), or
`SKSE/Plugins/ApocryphaAutoAudioOutputSwitch.ini`. Log:
`Documents/My Games/Skyrim Special Edition/SKSE/ApocryphaAutoAudioOutputSwitch.log`.

## How it works

Every Skyrim runtime plays audio through XAudio2 2.7, and XAudio2 2.7 cannot move an engine to another device: once its
device is gone the engine is dead. So a switch rebuilds the game's own audio engine. On the game's audio thread,
between sound-processing passes, every live sound lets go of its voice, the game's own audio shutdown and init run
(init creates the new engine on the target device), and the game's own per-sound setup gives every sound a voice on the
new engine. Switches are triggered by Windows audio endpoint notifications and by the engine's device-loss callback;
a newly connected device is probed until it actually takes audio. Game functions are located through Address Library
IDs, confirmed in the 1.5.97 and 1.7.104 code. See `include/AudioSwitch.h`.

Long streamed tracks (music, dialogue) restart from their beginning after a switch; short and looping sounds carry on.

## Credits

- **Live Audio Output Switching SE** by Mern (Maarten Harms), MIT -
  https://www.nexusmods.com/skyrimspecialedition/mods/184853 and https://github.com/maartenharms/live-audio-output-switching.
  The engine-rebuild procedure this plugin is built on was found there for 1.5.97 and is ported here to three game
  versions. Its licence notice is in `THIRD_PARTY_NOTICES.md`.
- **Parapets** - Auto Audio Switch for Anniversary Edition, the original idea. None of its files were read here.

## Building

Visual Studio 2022 with the C++ toolset, and `VCPKG_ROOT` pointing at a vcpkg checkout.

- SE 1.5.97 / AE 1.6.1170: `configure.bat`, then `build.bat` (CommonLibSSE-NG 3.7.0).
- Skyrim 1.7.x: `configure17.bat`, then `build17.bat` (CommonLibSSE-NG 7.2.0 through `cmake/ports-17`).

## DevBench

With DevBench running, the tool `aaos.control` reports live state (`op=state`), lists devices (`op=devices`), runs a
check or a forced switch (`op=check`, `op=reset`) and changes settings (`op=prefer`, `op=set`, `op=reload`).

## Licence

GPL-3.0-or-later - see `LICENSE` and `NOTICE.md`; components under other licences, with their notices, are in
`THIRD_PARTY_NOTICES.md`.
