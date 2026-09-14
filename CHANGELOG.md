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
- Proven with a real Bluetooth headset unplugged and plugged back in (SE 1.5.97): the switch to the monitor and back
  took 30 ms and 49 ms with every live sound kept. Two faults found on the way are fixed: after the engine rebuild the
  game's output-effect mixer table and its spare pool still pointed at destroyed mixers (emptied now), and a sound sent
  to a mixer that no longer exists crashed XAudio2 (XAudio2_7+0x28B53) a few seconds after the switch (that send is
  dropped now; the sound plays without that effect).
- If a switch ever waits more than 2 seconds for the game's audio thread, the log records where the thread is and its
  call stack.
