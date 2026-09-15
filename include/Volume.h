#pragma once

// Auto Audio Output Switch - the PC volume of the device the game plays on. Own code, GPL-3.0-or-later (2026-09-14).
//
// The owner, 2026-09-14: "change pc volume in game". A background thread (COM, multithreaded apartment) follows the
// endpoint the game's sound goes to (audioswitch::CurrentDeviceId; the Windows default output when the game has none),
// opens its IAudioEndpointVolume - the same volume and mute the taskbar's speaker icon sets - applies any requested
// level or mute, and caches what the device reports about every 150 ms. The settings page and the DevBench tool only
// read that cache and post requests, so nothing on the render thread waits on Windows audio.
//
// The keyboard's volume keys are not handled here. Skyrim holds the keyboard exclusively, so Windows never sees them
// while the game has focus (a Bluetooth keyboard's Fn media keys even bypass low-level hooks then). Media Keys Fix SKSE
// (Nexus 92948) removes that exclusive access and Windows handles the keys itself - the owner: "that does fix it". A key
// hook tried in 1.0.1's test builds was removed in its favour.

#include <string>

namespace volume
{
	struct Info
	{
		bool ok{ false };      // a device's volume was read in the latest pass
		float level{ 0.0F };   // 0..1, Windows master volume scalar
		bool muted{ false };
		std::string device;    // its name as Windows shows it
		std::string deviceId;  // lower-case endpoint id
	};

	void Start();              // SKSEPluginLoad, after audioswitch::StartWatcher; safe to call twice
	Info Get();                // the latest cached reading; any thread
	void SetLevel(float a_level);  // 0..1, applied by the volume thread within a pass
	void SetMuted(bool a_muted);
	std::string StateJson();   // "volume":{...} member, no braces around it
}
