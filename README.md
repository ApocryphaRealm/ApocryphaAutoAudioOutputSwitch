# Auto Audio Input Switch

An SKSE plugin for Skyrim Special Edition 1.5.97, Anniversary Edition 1.6.1170 and Skyrim 1.7.x that lets the game
move to another audio output device without a restart.

Skyrim chooses its output device once, at startup, and goes silent for the rest of the session if that device is
unplugged. With this plugin the game switches:

- to the Windows default output when the active device is removed or the default changes
  (`bSwitchOnDefaultChange=1`), and
- to a preferred device whenever it is connected (`sPreferredDevice`, part of the device's name).

Settings: `SKSE/Plugins/ApocryphaAutoAudioInputSwitch.ini`. Log:
`Documents/My Games/Skyrim Special Edition/SKSE/ApocryphaAutoAudioInputSwitch.log`.

## How it works

Every Skyrim runtime plays audio through XAudio2 2.7. The plugin pins `XAudio2_7.dll` and hooks four slots of the
IXAudio2 interface table, which all engine instances share. When the game creates its mastering voice, the real one
is created on the chosen device and a submix voice is given to the game in its place; every sound the game plays sends
to that stand-in. Switching destroys the real mastering voice and creates a new one on the target device, then
reconnects the stand-in - the game's own voices are never touched. Switches are triggered by Windows audio endpoint
notifications and by the engine's critical-error callback, debounced, on a worker thread. See
`include/AudioSwitch.h`.

This is a clean-room implementation written from the public description of Parapets' Auto Audio Switch; none of that
mod's code was read or used.

## Building

Visual Studio 2022 with the C++ toolset, and `VCPKG_ROOT` pointing at a vcpkg checkout.

- SE 1.5.97 / AE 1.6.1170: `configure.bat`, then `build.bat` (CommonLibSSE-NG 3.7.0).
- Skyrim 1.7.x: `configure17.bat`, then `build17.bat` (CommonLibSSE-NG 7.2.0 through `cmake/ports-17`).

## DevBench

With DevBench running, the tool `aais.control` reports live state (`op=state`), lists devices (`op=devices`), runs a
check or a forced switch (`op=check`, `op=reset`) and changes settings (`op=prefer`, `op=set`, `op=reload`).

## Licence

GPL-3.0-or-later - see `LICENSE` and `NOTICE.md`; components under other licences, with their notices, are in
`THIRD_PARTY_NOTICES.md`.
