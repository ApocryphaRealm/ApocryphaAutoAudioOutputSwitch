# Changelog

## 1.0.0

First release. Built on Live Audio Output Switching SE by Mern (Maarten Harms, MIT).

- The game's audio moves to another output device while it runs: when the device it plays on is unplugged, when the
  Windows default output changes, when the preferred device is connected, or with Switch now. Live sounds carry on
  after a switch; long music and dialogue tracks restart.
- A preferred device (`sPreferredDevice`, part of its name), chosen on the settings page or in the INI.
- Settings page on the Apocrypha Menu Framework in eleven languages.
- Works on Skyrim SE 1.5.97, AE 1.6.1170 and Skyrim 1.7.x (one installer, one build per line).
- DevBench tool `aaos.control`: live state, the device lists, a forced reset, and the settings.
