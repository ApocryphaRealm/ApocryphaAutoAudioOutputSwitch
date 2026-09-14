// Auto Audio Output Switch - settings page on the Apocrypha Menu Framework. Own code, GPL-3.0-or-later (2026-09-13).
#include "PCH.h"

#include "UI.h"

#include "SKSEMenuFramework.h"

#include "AudioSwitch.h"
#include "Settings.h"

#include "utils/Logger.h"
#include "utils/Strings.h"
#include "utils/Toggle.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace UI
{
	namespace
	{
		std::string statusMessage;
		std::string selectedSlider;  // the slider the arrow keys drive (project convention)

		constexpr const char* kLogLevelNames[] = { "Trace", "Debug", "Info", "Warning", "Error", "Critical", "Off" };
		constexpr const char* kLogLevelKeys[] = { "AAOS_LogLevel_Trace", "AAOS_LogLevel_Debug", "AAOS_LogLevel_Info",
													"AAOS_LogLevel_Warning", "AAOS_LogLevel_Error", "AAOS_LogLevel_Critical", "AAOS_LogLevel_Off" };
		constexpr int kLogLevelCount = 7;

		// File I/O and anything slow is handed to the main thread; the page draws from the renderer's present hook.
		void OnMainThread(std::function<void()> a_task)
		{
			if (auto* taskInterface = SKSE::GetTaskInterface()) { taskInterface->AddTask(std::move(a_task)); }
		}

		bool HasRequiredExports()
		{
			constexpr const char* required[] = {
				"AddSectionItem", "igTextV", "igTextDisabledV", "igTextWrappedV", "igSetTooltipV", "igSeparatorText",
				"igCombo_Str_arr", "igSliderFloat", "igIsKeyPressed_Bool", "igIsItemClicked", "igIsItemActive",
				"igIsItemHovered", "igButton", "igSameLine", "igSpacing", "igPushItemWidth", "igPopItemWidth",
				// utils/Toggle.h
				"igGetCursorScreenPos", "igGetWindowDrawList", "igGetFrameHeight", "igInvisibleButton", "igPushID_Str",
				"igPopID", "ImDrawList_AddRectFilled", "ImDrawList_AddCircleFilled"
			};
			for (const char* name : required)
			{
				if (!GetMenuFrameworkFunction<void*>(name))
				{
					logger::warn("The menu framework does not export \"{}\"", name);
					return false;
				}
			}
			return true;
		}

		void HelpMarker(const char* a_description)
		{
			ImGuiMCP::SameLine();
			ImGuiMCP::TextDisabled("%s", strings::TR("AAOS_HelpMark", "(?)"));
			if (ImGuiMCP::IsItemHovered()) { ImGuiMCP::SetTooltip("%s", a_description); }
		}

		bool NudgeableSlider(const char* a_label, float* a_value, float a_min, float a_max, const char* a_format, float a_step)
		{
			bool changed = ImGuiMCP::SliderFloat(a_label, a_value, a_min, a_max, a_format);
			if (ImGuiMCP::IsItemClicked() || ImGuiMCP::IsItemActive()) { selectedSlider = a_label; }
			if (selectedSlider == a_label)
			{
				float nudge = 0.0F;
				if (ImGuiMCP::IsKeyPressed(ImGuiMCP::ImGuiKey_LeftArrow) || ImGuiMCP::IsKeyPressed(ImGuiMCP::ImGuiKey_DownArrow)) { nudge -= a_step; }
				if (ImGuiMCP::IsKeyPressed(ImGuiMCP::ImGuiKey_RightArrow) || ImGuiMCP::IsKeyPressed(ImGuiMCP::ImGuiKey_UpArrow)) { nudge += a_step; }
				if (nudge != 0.0F)
				{
					*a_value = std::clamp(*a_value + nudge, a_min, a_max);
					changed = true;
				}
				ImGuiMCP::SameLine();
				ImGuiMCP::TextDisabled("%s", strings::TR("AAOS_NudgeArrows", "<-->"));
			}
			return changed;
		}

		void RenderStatusSection()
		{
			ImGuiMCP::SeparatorText(strings::TR("AAOS_Status", "Status"));
			const audioswitch::Status s = audioswitch::GetStatus();
			if (!s.hooked)
			{
				ImGuiMCP::TextWrapped("%s", strings::TR("AAOS_StatusNotHooked", "The game's audio engine could not be hooked. See the log for why."));
				return;
			}
			if (!s.managed)
			{
				ImGuiMCP::TextWrapped("%s", strings::TR("AAOS_StatusNoEngine", "The game has no audio engine to manage - no output device was usable when it started."));
				return;
			}
			if (s.critical)
			{
				ImGuiMCP::TextWrapped("%s", strings::TR("AAOS_StatusInvalidated", "The output device stopped working. Switching to another connected device..."));
			}
			else if (s.attached)
			{
				ImGuiMCP::Text("%s %s", strings::TR("AAOS_CurrentDevice", "Playing on:"), s.device.c_str());
			}
			else
			{
				ImGuiMCP::TextWrapped("%s", strings::TR("AAOS_NoDevice", "No output device is connected. The game switches as soon as one appears."));
			}
			HelpMarker(strings::TR("AAOS_HelpCurrent", "The device the game's sound goes to right now."));

			if (ImGuiMCP::Button(strings::TR("AAOS_SwitchNowBtn", "Switch now")))
			{
				audioswitch::RequestReset("settings page: Switch now", true);
				statusMessage = strings::TR("AAOS_StatusSwitching", "Switching to the preferred or default device...");
			}
			HelpMarker(strings::TR("AAOS_HelpSwitchNow", "Moves the game's sound to the preferred device, or to the Windows default device when none is set, even if nothing changed."));
		}

		void RenderDeviceSection()
		{
			using namespace settings;
			ImGuiMCP::SeparatorText(strings::TR("AAOS_Output", "Output device"));

			bool enabled = general::enabled;
			if (ImGuiMCP::Toggle(strings::TR("AAOS_Enabled", "Manage the output device"), &enabled))
			{
				general::enabled = enabled;
				if (enabled) { audioswitch::RequestReset("settings page: enabled", false); }
			}
			HelpMarker(strings::TR("AAOS_HelpEnabled", "Off leaves the game on whatever device it is using, exactly as it would be without this mod."));

			// Preferred device: "Windows default" first, then every device XAudio2 lists, then the saved one if absent.
			const std::string preferred = GetPreferredDevice();
			std::vector<std::string> names = audioswitch::DeviceNames();
			std::vector<std::string> labels{ strings::TR("AAOS_WindowsDefault", "Windows default device") };
			int current = 0;
			bool found = preferred.empty();
			for (std::size_t i = 0; i < names.size(); ++i)
			{
				labels.push_back(names[i]);
				if (!found && names[i] == preferred)
				{
					current = static_cast<int>(i + 1);
					found = true;
				}
			}
			if (!found)
			{
				labels.push_back(preferred + " " + strings::TR("AAOS_NotConnected", "(not connected)"));
				names.push_back(preferred);
				current = static_cast<int>(labels.size() - 1);
			}
			std::vector<const char*> items;
			for (const auto& l : labels) { items.push_back(l.c_str()); }
			if (ImGuiMCP::Combo(strings::TR("AAOS_Preferred", "Preferred device"), &current, items.data(), static_cast<int>(items.size())))
			{
				const std::string chosen = current <= 0 ? std::string() : names[static_cast<std::size_t>(current - 1)];
				SetPreferredDevice(chosen);
				audioswitch::RequestReset("settings page: preferred device changed", false);
			}
			HelpMarker(strings::TR("AAOS_HelpPreferred", "The game uses this device whenever it is connected, and returns to it when it is plugged back in. Windows default device follows whatever Windows is set to."));

			bool follow = general::switchOnDefaultChange;
			if (ImGuiMCP::Toggle(strings::TR("AAOS_FollowDefault", "Follow the Windows default device"), &follow))
			{
				general::switchOnDefaultChange = follow;
				if (follow) { audioswitch::RequestReset("settings page: follow default on", false); }
			}
			HelpMarker(strings::TR("AAOS_HelpFollowDefault", "When the preferred device is not connected, switch whenever the Windows default output changes. Off stays on the current device until it is removed."));

			float delay = static_cast<float>(general::resetDelayMs.load());
			if (NudgeableSlider(strings::TR("AAOS_Delay", "Switch delay"), &delay, 0.0F, 5000.0F, "%.0f ms", 100.0F))
			{
				general::resetDelayMs = static_cast<std::uint32_t>(delay);
			}
			HelpMarker(strings::TR("AAOS_HelpDelay", "How long to wait after a device change before switching. Windows reports one change as several events; the wait lets them settle."));
		}

		void RenderDebugSection()
		{
			using namespace settings;
			ImGuiMCP::SeparatorText(strings::TR("AAOS_Debug", "Debug"));

			int level = std::clamp(static_cast<int>(debug::logLevel), 0, kLogLevelCount - 1);
			std::vector<std::string> store;
			for (int i = 0; i < kLogLevelCount; ++i) { store.push_back(strings::TR(kLogLevelKeys[i], kLogLevelNames[i])); }
			std::vector<const char*> labels;
			for (const auto& s : store) { labels.push_back(s.c_str()); }
			if (ImGuiMCP::Combo(strings::TR("AAOS_LogLevel", "Log level"), &level, labels.data(), kLogLevelCount))
			{
				debug::logLevel = static_cast<std::uint32_t>(level);
				ApplyLogLevel();
			}
			HelpMarker(strings::TR("AAOS_HelpLogLevel", "Applies immediately. The log is at Documents\\My Games\\Skyrim Special Edition\\SKSE\\ApocryphaAutoAudioOutputSwitch.log. Set this to Trace or Debug before reproducing a bug you plan to report."));
		}

		void RenderButtons()
		{
			ImGuiMCP::SeparatorText("");

			if (ImGuiMCP::Button(strings::TR("AAOS_SaveBtn", "Save")))
			{
				statusMessage = strings::TR("AAOS_StatusSaving", "Saving...");
				OnMainThread([]() {
					statusMessage = settings::Save() ? strings::TR("AAOS_StatusSaved", "Settings saved.")
													   : strings::TR("AAOS_StatusSaveFail", "Could not write the INI. See the log for why.");
				});
			}
			HelpMarker(strings::TR("AAOS_HelpSave", "Writes every setting on this page to the plugin's INI so it survives a restart."));

			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button(strings::TR("AAOS_ReloadBtn", "Reload from INI")))
			{
				statusMessage = strings::TR("AAOS_StatusReloading", "Reloading...");
				OnMainThread([]() {
					const bool ok = settings::Reload();
					audioswitch::RequestReset("settings page: reloaded", false);
					statusMessage = ok ? strings::TR("AAOS_StatusReloaded", "Settings reloaded from the INI.")
									   : strings::TR("AAOS_StatusReloadFail", "Could not read the INI. See the log for why.");
				});
			}
			HelpMarker(strings::TR("AAOS_HelpReload", "Throws away any change made here since the last save and re-reads the INI from disk. Also picks up edits made to the file by hand."));

			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button(strings::TR("AAOS_RestoreBtn", "Restore defaults")))
			{
				settings::RestoreDefaults();
				audioswitch::RequestReset("settings page: defaults restored", false);
				statusMessage = strings::TR("AAOS_StatusRestored", "Defaults restored. Press Save to keep them.");
			}
			HelpMarker(strings::TR("AAOS_HelpRestore", "Puts every setting back to the value it has on a fresh install. Nothing is written until you press Save."));

			if (!statusMessage.empty()) { ImGuiMCP::TextWrapped("%s", statusMessage.c_str()); }
			ImGuiMCP::Spacing();
			ImGuiMCP::Text("%s", settings::GetIniPath().c_str());
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled())
		{
			logger::info("No menu framework is installed; settings are read from the INI only");
			return;
		}
		if (!HasRequiredExports())
		{
			logger::warn("The installed menu framework is older than this plugin's settings page needs. Update it "
						 "(Apocrypha Menu Framework, or SKSE Menu Framework 3 or newer) to configure Auto Audio Output Switch in game.");
			return;
		}
		SKSEMenuFramework::SetSection("Auto Audio Output Switch");
		SKSEMenuFramework::AddSectionItem("Settings", SettingsPanel::Render);
		logger::info("Registered the settings page with the menu framework");
	}

	void __stdcall SettingsPanel::Render()
	{
		strings::Tick();

		ImGuiMCP::TextWrapped("%s", strings::TR("AAOS_Intro", "Changes apply as soon as you make them. Press Save to keep them for the next time you play."));
		ImGuiMCP::Spacing();

		ImGuiMCP::PushItemWidth(320.0F);
		RenderStatusSection();
		ImGuiMCP::Spacing();
		RenderDeviceSection();
		ImGuiMCP::Spacing();
		RenderDebugSection();
		ImGuiMCP::PopItemWidth();

		RenderButtons();
	}
}
