Auto Audio Output Switch 1.0.7

SKSE plugin. Skyrim picks its audio output once, at startup, and goes silent if that device is unplugged. With this
plugin the game moves its sound to another connected device when the one it plays on is unplugged, when you connect a
headset while playing, when the Windows default output changes, when your preferred device is connected, or with
Switch now - live sounds carry on. It also puts your PC volume on the settings page and makes the keyboard's media keys
(volume, mute) work while the game has focus.

Built on Live Audio Output Switching SE by Mern (Maarten Harms), MIT -
https://www.nexusmods.com/skyrimspecialedition/mods/184853 - whose engine-rebuild procedure makes the switch work.

Dual-line build. The installer asks which Skyrim you have and installs the matching DLL:
  - Skyrim SE 1.5.97 / AE 1.6.1170  -> the SE/AE build (CommonLibSSE-NG 3.7.0)
  - Skyrim 1.7.99 / 1.7.104         -> the 1.7 build (CommonLibSSE-NG 7.2.0), licence texts included

Settings: the Auto Audio Output Switch page in the Apocrypha Menu Framework (or SKSE Menu Framework), or
SKSE\Plugins\AutoAudioOutputSwitch.ini. Translations for eleven languages ship in Interface\Translations.
The .pdb ships beside each DLL for Crash Logger.

Media keys: [Keyboard] bMediaKeys (on), bDisableWindowsKey (on), bDisableDeadKeys (off); a change takes effect at the
next game start. If Media Keys Fix SKSE (MediaKeysFix.dll) is installed, this mod leaves the keyboard to it.

Better AltTab is compatible: switching works with it installed, and its background mute keeps working after a switch.

Log: Documents\My Games\Skyrim Special Edition\SKSE\AutoAudioOutputSwitch.log
Source: https://github.com/ApocryphaRealm/ApocryphaAutoAudioOutputSwitch

CREDITS

Live Audio Output Switching SE by Mern (Maarten Harms), MIT: the engine-rebuild procedure this mod is built on,
ported here to three game versions (its licence notice is in THIRD_PARTY_NOTICES.md).
Media Keys Fix SKSE by Emerson Pinter (epinter), LGPL-3.0 - https://www.nexusmods.com/skyrimspecialedition/mods/92948 -
the media keys approach (the keyboard access flags and the dead-key wrap), re-implemented here with credit as its
permissions allow. None of its files are included.
Auto Audio Switch by Parapets: the original idea.

LICENCE

GPL-3.0-or-later: LICENSE and NOTICE.md. Components under other licences, with their notices:
THIRD_PARTY_NOTICES.md.

WHAT CHANGED IN 1.0.7
  - Controller vibration keeps working after a switch. Skyrim makes much of its rumble from sound, through two
    "rumble mixers" it creates once at startup; the game's own audio rebuild destroyed them and never made them
    again, so sound-driven rumble stopped at the first switch. They are now handed back and made again with the
    game's own functions at every switch.
------------

Version 1.0.7
Fixed controller vibration stopping after an audio switch: the game's two sound-rumble mixers, which its audio rebuild destroyed, are now made again at every switch.

Version 1.0.6
Fixed the Log level help on the settings page naming the old log file; it now names AutoAudioOutputSwitch.log in all eleven languages.

Version 1.0.5
Fixed a crash after switching devices with Dragonborn ReVoiced installed (DBReV keeps the game's audio engine; the engine a switch let go is now kept for the session and other mods' calls on it are refused).
Changed the Volume and Switch delay sliders to move one unit per arrow-key or D-pad press.
Changed the log to ship at info. For a bug report, set uLogLevel=0 in the INI and reproduce.

Version 1.0.4
Fixed the game freezing when the output device was unplugged or switched off. The freeze is a deadlock inside XAudio2 2.7 itself (the audio library Skyrim uses): two of its threads could report the lost device at the same moment and wait for each other. This mod now lets the second one step aside, and only on the exact XAudio2 build Skyrim installs.
Fixed a crash and failed switches with Better AltTab installed: its audio hook stops working once the game has loaded, so switching repairs it first, keeps the engine Better AltTab holds alive, and passes its alt-tab mute on to the engine that is playing - Better AltTab keeps muting the game in the background after a switch.
Faults inside the game's audio code during a switch are now logged with the module, offset and call stack.

Version 1.0.1
Added switching to a headset or other output device you connect while playing, even when Windows does not make it the
default (bSwitchToNewDevice, on by default).
Added a PC volume section on the settings page: a Volume slider and a Mute switch for the device the game plays on.
Added media keys in game (volume, mute) and an optional Windows-key lock, from Media Keys Fix SKSE by Emerson Pinter.
Fixed the game's sound going silent for good when the device it played on was unplugged at an unlucky moment: the
switch now waits for the audio engine to finish handling the removed device first.
Fixed a crash reported on AE 1.6.1170 when the sound moved to another device and the rebuilt audio engine could not be
created: the game no longer starts a sound on a missing engine, and the switch is retried until a device takes it.
Fixed saving settings from an INI written by an earlier version.

Version 1.0.0
First release. The game's sound moves to another output device without a restart - on unplug, on a Windows default
change, to a preferred device, or with Switch now - by rebuilding the game's own audio engine on the new device.
Settings page on the Apocrypha Menu Framework in eleven languages.