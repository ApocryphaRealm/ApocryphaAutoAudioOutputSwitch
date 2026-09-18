# Changelog

## 1.0.4 - 2026-09-18 - untested

### Fixed
- **Updating over 1.0.2 could leave the game silent.** 1.0.3 renamed the DLL, and an update installed on top of the old
  files rather than replacing them left both `ApocryphaAutoAudioOutputSwitch.dll` and `AutoAudioOutputSwitch.dll` in
  `SKSE\Plugins`. SKSE loads both, and two copies of this mod hooking the same XAudio2 engine and rebuilding it under
  each other is silence on the next device switch (Arshia13, Nexus Posts, 2026-09-18: *"Update to 1.0.3 and now game is
  mute using wireless headphone ... it was fine in 1.0.2"*). This build now checks for the old DLL at load and, when it
  is there, stands down with a message naming the file to delete instead of running alongside it.
- **Settings survive the rename.** Values in an old `ApocryphaAutoAudioOutputSwitch.ini` - the player's - are taken over
  the freshly shipped `AutoAudioOutputSwitch.ini` once, written into it, and the old file is renamed `.migrated`, so a
  preferred device or a switched-off option set before 1.0.3 is not lost and a later edit of the new file is never
  undone. The log says how many values came across.

### Changed
- The Address Library pre-check runs before anything else at load: a missing Address Library file for the game version
  gets a message naming the file and the plugin loads inert, instead of CommonLibSSE-NG's bare failure line.

## 1.0.3 - 2026-09-16 - untested

### Changed
- **The Apocrypha prefix is gone from the files** (the owner, 2026-09-16: *"Unprefix all of them"*; framework mods keep theirs, this is not one). The DLL is `AutoAudioOutputSwitch.dll`, so the INI is `AutoAudioOutputSwitch.ini`, the log `AutoAudioOutputSwitch.log`, and the translation files `AutoAudioOutputSwitch_<language>.txt`. An old `ApocryphaAutoAudioOutputSwitch.ini` is not read; copy your values across once. No behaviour change; the code is 1.0.2's.

## 1.0.2 - 2026-09-15 - working

### Fixed
- The game could stop responding when the output device was unplugged or switched off. This is a deadlock inside XAudio2 2.7 itself (the June 2010 build Skyrim installs), found from a memory dump with Microsoft's public symbols: when a device disappears, the audio session's disconnect callback and XAudio2's own processing thread can both report the error at once; the first holds XAudio2's lock while waiting for the processing thread to stop, and the processing thread waits for that lock. The hangs this changelog earlier blamed on releasing the old engine's sounds during a switch were this same race - the 3-second wait added in 1.0.1 only made it less likely. The processing thread no longer waits for a lock held by a thread that is waiting for it; XAudio2 then finishes handling the lost device normally and the switch goes ahead. The patch is applied only to that exact XAudio2 build and is checked before it is made (falsification episode 51).
- A crash with Better AltTab (Nexus 121342) installed: after the game's sound switched devices, alt-tabbing crashed the game inside Better AltTab (`BetterAltTab.dll`, `Xaudio::stopEngine`). Better AltTab keeps the audio engine pointer from the moment the game creates the engine and stops or starts it at every focus change; when a rebuild did not produce a new engine, that pointer was left on an engine the game had already released. Reproduced on SE 1.5.97 with the reporter's crash address (falsification episode 49). A rebuild now keeps its own reference on every engine the game releases during it - the engine before the game's shutdown, and any engine whose initialisation fails - so a pointer another mod kept stays a live (stopped) engine, and lets them go once a rebuild succeeds.

- Switching devices failed with Better AltTab installed: the rebuilt engine's init faulted every time (`SkyrimSE.exe+BFCBB4`), because Better AltTab's hook on the game's engine-creation call (SE 66746+0x44, AE/1.7 67952+0xB7) branches through a trampoline block that has been freed once the game's data has loaded (found with the new fault log and VirtualQuery: MEM_FREE; falsification episode 50). Before a rebuild the call is now checked; if it goes through memory that is not there it is pointed back at the game's own CoCreateInstance import, and the engine Better AltTab cached is kept referenced for good. Switching works again with Better AltTab installed (reproduced: rebuild in 15 ms, alt-tab afterwards, no crash); Better AltTab's mute then acts on the engine it saw first - so its StartEngine/StopEngine calls on that kept engine are now passed to the engine that is playing, and when it had stopped the sound (the game in the background) a switch keeps the new engine stopped too: Better AltTab's background mute keeps working across switches.

### Changed
- When the game's audio shutdown or init faults during a switch, the log names the faulting module and offset, what was being read or written, and the call stack at the fault. With Better AltTab installed the rebuilt engine's init faulted before the engine could be created; this is how that fault is being traced.

## 1.0.1 - 2026-09-14 - working

### Added
- Media keys work in game, built in from Media Keys Fix SKSE (the owner: 'i want to incorporate the media keys fix if possible'). Skyrim creates its keyboard with exclusive access, so Windows never sees the volume, mute and other media keys while the game has focus, and a Bluetooth keyboard's Fn media keys bypass even low-level keyboard hooks then (falsification 43-45). New [Keyboard] settings, on the settings page too: bMediaKeys (on) makes the game's keyboard non-exclusive by changing the flags it passes to SetCooperativeLevel (0x15 -> 0x16, checked against the expected instruction first), bDisableWindowsKey (on) keeps the Windows key from opening the Start menu, bDisableDeadKeys (off) wraps the game's ToUnicode call so accent keys type at once in the console. Applied at plugin load, so a change takes effect at the next start. When MediaKeysFix.dll itself is installed this mod leaves the keyboard alone. The approach, addresses and instruction pattern come from Media Keys Fix SKSE by Emerson Pinter (Nexus 92948, LGPL-3.0-or-later), re-implemented and credited in THIRD_PARTY_NOTICES.md. A low-level key hook tried in this version's test builds was removed. One trampoline is now allocated at load for both call hooks.
- PC volume in game (the owner: 'change pc volume in game'). A PC volume section on the settings page with a Volume slider (0-100 %, arrow keys nudge it) and a Mute switch for the Windows volume of the device the game plays on - the same volume the taskbar's speaker icon sets. A background thread follows the game's device (the Windows default when it has none), applies the change and reads the device back about every 150 ms, so the page never waits on Windows. DevBench aaos.control op=volume [level] [mute]. New strings in all eleven languages.
- Switch to a device connected while playing (the owner: 'allow users to connect their headset after the game started'). A new setting, bSwitchToNewDevice (on by default, a switch on the settings page): when an output device becomes active after the game started - a headset plugged in or switched on - the game's sound moves to it, even when Windows does not make it the default. The Windows notifications remember the newest arrivals; the switch check ranks a connected preferred device first, then the newest arriving output, then the Windows default, and forgets the arrivals once it has acted, so a later default change still counts. The switch itself is the existing engine rebuild.

### Fixed
- The game's sound could go silent for good when the headset it played on was unplugged (reproduced on 1.0.0 and 1.0.1 with a wired USB headset, SE 1.5.97). When the switch began before XAudio2 had finished handling the removed device, releasing the old engine's sounds waited on XAudio2 while XAudio2 waited on it, and the game's audio thread never came back. Now, when the current device disappears, the switch waits (up to 3 seconds) for the audio engine to report the lost device before touching it; the unplug that worked on 1.0.0 was the one where that report came first (falsification episode 46).
- A crash when the game's sound moved to another device on some systems (Nexus report on AE 1.6.1170: headset to soundbar, access violation at 67955+0x231 on the audio thread). The rebuild runs the game's own audio shutdown and init; when init could not create a new XAudio2 engine (its Initialize failing while a device is still changing), the game kept sound switched on with no engine and crashed on the next sound it started, because it builds a sound's voice through the engine with no check. Now a sound started with no engine is skipped instead (the game drops it the way it drops any sound it cannot play), a switch is retried every 5 seconds until a device takes the audio, a switch with no engine skips the game's shutdown (which would also fail on it), and a rebuild that leaves no engine says so in the log and on the settings page. DevBench aaos.control op=failinit reproduces it on purpose (falsification episode 47).
- Save no longer fails on an INI from an earlier version that lacks a newer key: the missing key is added at the end of its section.

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
