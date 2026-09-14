// Auto Audio Input Switch - own code, GPL-3.0-or-later (2026-09-13). Moves Skyrim's audio to another output device
// without a restart: when a preferred device is chosen, when the active device is removed or a new one appears, and
// when the Windows default output changes. Works at the XAudio2 2.7 COM layer every runtime shares - no game
// address, no ESP, no scripts.
#include "PCH.h"

#include "AudioSwitch.h"
#include "DevBenchTool.h"
#include "Settings.h"
#include "UI.h"

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
			strings::Configure("ApocryphaAutoAudioInputSwitch");
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
	SKSE::log::init("ApocryphaAutoAudioInputSwitch");

	settings::Init("ApocryphaAutoAudioInputSwitch.ini");
	settings::ApplyLogLevel();
	SKSE::log::describe_level("ApocryphaAutoAudioInputSwitch.ini");

	logger::info("Auto Audio Input Switch {} loading", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."));

	audioswitch::Install();
	audioswitch::StartWatcher();

	SKSE::GetMessagingInterface()->RegisterListener(MessageHandler);

	return true;
}
