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
			strings::Configure("ApocryphaAutoAudioOutputSwitch");
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

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SKSE::log::init("ApocryphaAutoAudioOutputSwitch");

	settings::Init("ApocryphaAutoAudioOutputSwitch.ini");
	settings::ApplyLogLevel();
	SKSE::log::describe_level("ApocryphaAutoAudioOutputSwitch.ini");

	logger::info("Auto Audio Output Switch {} loading", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."));

	// One trampoline for both call hooks: the audio thread's sound processing (5-byte call) and, when dead keys are
	// disabled, the keyboard's ToUnicode call (6-byte call) - 14 bytes each.
	SKSE::AllocTrampoline(28);
	mediakeys::Install();   // before the game creates its DirectInput keyboard
	audioswitch::Install();
	audioswitch::StartWatcher();
	volume::Start();

	SKSE::GetMessagingInterface()->RegisterListener(MessageHandler);

	return true;
}
