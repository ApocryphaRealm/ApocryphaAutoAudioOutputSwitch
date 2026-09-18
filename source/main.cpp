// Auto Audio Output Switch - own code, GPL-3.0-or-later (2026-09-13). Moves Skyrim's audio to another output device
// without a restart: when a preferred device is chosen, when the active device is removed or a new one appears, and
// when the Windows default output changes. Works at the XAudio2 2.7 COM layer every runtime shares - no game
// address, no ESP, no scripts.
#include "PCH.h"

#include "AudioSwitch.h"
#include "DevBenchTool.h"
#include "KeyboardAccess.h"
#include "Settings.h"
#include "UI.h"
#include "Volume.h"

#include "utils/AddressLibraryGuard.h"
#include "utils/Logger.h"
#include "utils/Strings.h"

namespace
{
	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type)
		{
		case SKSE::MessagingInterface::kPostLoad:
			DevBenchTool::Init(false);
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			strings::Configure("AutoAudioOutputSwitch");
			UI::Register();
			DevBenchTool::Init(true);
			audioswitch::LogSummary("data loaded");
			// A preferred device that appeared after the engine started, or a default that changed during loading.
			audioswitch::RequestReset("data loaded", false);
			break;
		default:
			break;
		}
	}
}

namespace
{
	// 1.0.3 renamed the DLL (ApocryphaAutoAudioOutputSwitch.dll -> AutoAudioOutputSwitch.dll). An update
	// installed OVER 1.0.2 rather than replacing it leaves both files in SKSE\\Plugins, and SKSE loads both:
	// two switchers hooking the same XAudio2 engine and rebuilding it under each other is silence or a
	// crash (Arshia13 on the Nexus page, 2026-09-18: "Update to 1.0.3 and now game is mute using wireless
	// headphone ... it was fine in 1.0.2"). SKSE loads plugins in name order, so the old one is already in
	// the process when this one runs; when it is, this one stands down and says which file to delete.
	constexpr const wchar_t* kPreviousDll = L"ApocryphaAutoAudioOutputSwitch.dll";

	bool PreviousBuildIsLoaded()
	{
		HMODULE old = GetModuleHandleW(kPreviousDll);
		if (!old) { return false; }
		const char* text =
			"Auto Audio Output Switch: the old build (ApocryphaAutoAudioOutputSwitch.dll) is still installed beside "
			"this one, and running both would leave the game silent.\n\n"
			"This build has stood down. Delete the old file from Data\\SKSE\\Plugins - "
			"ApocryphaAutoAudioOutputSwitch.dll, .pdb and .ini (your settings are read from the old .ini "
			"automatically, so copy nothing) - or reinstall the mod choosing Replace rather than Merge.";
		logger::critical("[Update] {}", text);
		MessageBoxA(nullptr, text, "Auto Audio Output Switch", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
		return true;
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::log::init("AutoAudioOutputSwitch");
	// Address Library pre-check (the guard every mod of ours carries), BEFORE SKSE::Init, which opens the
	// Address Library itself (logic library 6026): a missing file gets a message naming it, not CommonLib's
	// bare failure, and the plugin loads inert.
	if (!AddressLibraryGuard::Guard("Auto Audio Output Switch"))
	{
		return true;
	}
	if (PreviousBuildIsLoaded())
	{
		return true;
	}
	SKSE::Init(a_skse);

	settings::Init("AutoAudioOutputSwitch.ini", "ApocryphaAutoAudioOutputSwitch.ini");
	settings::ApplyLogLevel();
	SKSE::log::describe_level("AutoAudioOutputSwitch.ini");

	logger::info("Auto Audio Output Switch {} loading", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."));

	// One trampoline for both call hooks: the audio thread's sound processing (5-byte call) and, when dead keys are
	// disabled, the keyboard's ToUnicode call (6-byte call) - 14 bytes each.
	SKSE::AllocTrampoline(42);  // media keys ToUnicode wrap + the audio voice guard (14 bytes each), with room
	mediakeys::Install();   // before the game creates its DirectInput keyboard
	audioswitch::Install();
	audioswitch::StartWatcher();
	volume::Start();

	SKSE::GetMessagingInterface()->RegisterListener(MessageHandler);

	return true;
}
