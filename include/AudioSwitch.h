#pragma once

// Auto Audio Input Switch - own code, GPL-3.0-or-later (2026-09-13).
//
// How the game's audio is moved between devices without a restart, on every runtime (all use XAudio2 2.7):
//
//  * XAudio2_7.dll is pinned and the IXAudio2 COM vtable, shared by every engine instance, gets four slots
//    patched: CreateMasteringVoice, CreateSubmixVoice, CreateSourceVoice and Release. No game address is used.
//  * When an engine creates its mastering voice, the real one is created on the chosen device and a SUBMIX voice
//    (processing stage 0x7FFFFFFF, sending only to the real one) is handed back in its place. In XAudio2 2.7 a
//    mastering voice has no methods a submix voice lacks, so every voice the game creates sends to that stand-in.
//    Voices created with the default send list are pointed at the stand-in explicitly.
//  * A reset detaches the stand-in, destroys the real mastering voice, creates a new one on the target device and
//    re-attaches the stand-in; after a critical error (the device vanished) the engine is started again. The game's
//    own voices are never touched, so nothing it holds becomes invalid.
//  * Resets are requested by Windows endpoint notifications (device added, removed, state changed, default changed),
//    by the engine's critical-error callback and by the DevBench tool, debounced, and run on a worker thread.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace audioswitch
{
	struct Status
	{
		bool hooked{ false };
		bool managed{ false };    // the game's engine was created through the hooks
		bool attached{ false };   // a real mastering voice exists
		bool critical{ false };   // the device stopped working and no switch has succeeded since
		std::string device;
		std::uint32_t resets{ 0 };
		std::string lastResult;
	};

	// For the settings page (render thread): never blocks on the worker - returns the last snapshot while a
	// switch is running. The device list is refreshed at most once a second.
	Status GetStatus();
	std::vector<std::string> DeviceNames();

	void Install();                                   // SKSEPluginLoad: pin XAudio2_7.dll and patch the vtable
	void StartWatcher();                              // SKSEPluginLoad: endpoint notifications + the reset worker
	void RequestReset(std::string_view a_reason, bool a_force);
	bool WaitIdle(int a_timeoutMs);                   // for the DevBench tool: true once no reset is pending or running
	std::string StateJson();                          // engines, hooks, counters
	std::string DevicesJson();                        // XAudio2 device list (from a live engine) and Windows endpoints
	void LogSummary(std::string_view a_when);
}
