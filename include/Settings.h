#pragma once

// Auto Audio Input Switch - settings. Plain-file INI (redirector-proof, the project standard).

#include <atomic>
#include <cstdint>
#include <string>

namespace settings
{
	namespace debug
	{
		inline std::uint32_t logLevel = 0;  // uLogLevel:Debug
	}

	namespace general
	{
		inline std::atomic<bool> enabled = true;                // bEnabled:General - manage the game's output device
		inline std::atomic<bool> switchOnDefaultChange = true;  // bSwitchOnDefaultChange:General - follow the Windows default
		inline std::atomic<std::uint32_t> resetDelayMs = 500;   // uResetDelayMs:General - quiet time before a reset runs
	}

	// sPreferredDevice:General - part of a device's name (or its full id); empty = the Windows default device.
	std::string GetPreferredDevice();
	void SetPreferredDevice(std::string a_value);

	void Init(const std::string& a_iniFileName);
	bool Reload();
	bool Save();
	void ApplyLogLevel();
	const std::string& GetIniPath();
}
