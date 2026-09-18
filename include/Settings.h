#pragma once

// Auto Audio Output Switch - settings. Plain-file INI (redirector-proof, the project standard).

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
		inline std::atomic<bool> switchToNewDevice = true;      // bSwitchToNewDevice:General - move to an output connected while playing
		inline std::atomic<std::uint32_t> resetDelayMs = 500;   // uResetDelayMs:General - quiet time before a reset runs
	}

	// [Keyboard] - media keys in game (KeyboardAccess.h); read at plugin load, so a change applies at the next game start.
	namespace keyboard
	{
		inline std::atomic<bool> mediaKeys = true;          // bMediaKeys:Keyboard - the game's keyboard is non-exclusive
		inline std::atomic<bool> disableWindowsKey = true;  // bDisableWindowsKey:Keyboard
		inline std::atomic<bool> disableDeadKeys = false;   // bDisableDeadKeys:Keyboard
	}

	// sPreferredDevice:General - part of a device's name (or its full id); empty = the Windows default device.
	std::string GetPreferredDevice();
	void SetPreferredDevice(std::string a_value);

	// a_previousIniFileName: the INI name before the 1.0.3 rename; its values are read first, the current file's win.
	void Init(const std::string& a_iniFileName, const std::string& a_previousIniFileName = {});
	int MigratedKeys();  // how many keys the previous INI supplied on the last load (0 when it is not there)
	bool Reload();
	bool Save();
	void RestoreDefaults();  // every setting back to its fresh-install value; nothing is written until Save
	void ApplyLogLevel();
	const std::string& GetIniPath();
}
