# Changelog

## 1.0.0

First release.

- The game's audio moves to another output device while it runs: when the active device is removed, when a new
  device appears, when the Windows default output changes, or when the preferred device is connected.
- A preferred device in the INI (`sPreferredDevice`, part of its name), used whenever it is connected.
- Works on Skyrim SE 1.5.97, AE 1.6.1170 and Skyrim 1.7.x, at the XAudio2 2.7 layer every runtime shares; no game
  address is patched.
- DevBench tool `aais.control`: live state, the device lists, a forced reset, and the settings.
