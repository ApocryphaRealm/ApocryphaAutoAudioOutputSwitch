#include "PCH.h"

#include "Settings.h"

#include "utils/INISettingCollection.h"
#include "utils/Logger.h"
#include "utils/Setting.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace settings
{
	namespace
	{
		std::string iniPath;
		std::mutex preferredLock;
		std::mutex fileLock;
		std::string preferredDevice;  // guarded by preferredLock

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		std::string Trim(const std::string& a_s)
		{
			const auto b = a_s.find_first_not_of(" \t\r\n");
			if (b == std::string::npos) { return {}; }
			const auto e = a_s.find_last_not_of(" \t\r\n");
			return a_s.substr(b, e - b + 1);
		}

		bool ParseBool(const std::string& a_text, bool& a_out)
		{
			const std::string v = Lower(Trim(a_text));
			if (v == "1" || v == "true" || v == "yes") { a_out = true; return true; }
			if (v == "0" || v == "false" || v == "no") { a_out = false; return true; }
			return false;
		}

		bool ParseUInt(const std::string& a_text, std::uint32_t& a_out)
		{
			try { a_out = static_cast<std::uint32_t>(std::stoull(Trim(a_text), nullptr, 0)); return true; } catch (...) { return false; }
		}

		bool ParseString(const std::string& a_text, std::string& a_out)
		{
			std::string v = Trim(a_text);
			if (v.size() >= 2 && v.front() == '"' && v.back() == '"') { v = v.substr(1, v.size() - 2); }
			a_out = v;
			return true;
		}

		bool IsSectionHeader(const std::string& a_t) { return a_t.size() >= 2 && a_t.front() == '[' && a_t.back() == ']'; }

		// The INI this build reads, and the one 1.0.2 and earlier wrote. A player who updated over the rename
		// (1.0.3, 2026-09-16) still has their values in the old file; they are read first and the new file's
		// values win, so nothing a player set is lost across the rename and nothing they set since is undone.
		std::string oldIniPath;
		int migratedKeys = 0;

		void ReadFile(const std::string& a_path, std::map<std::string, std::string>& a_keys)
		{
			std::ifstream in(a_path);
			if (!in) { return; }
			std::string line, section;
			while (std::getline(in, line))
			{
				const std::string t = Trim(line);
				if (t.empty() || t[0] == ';' || t[0] == '#') { continue; }
				if (IsSectionHeader(t)) { section = Lower(t.substr(1, t.size() - 2)); continue; }
				const auto eq = t.find('=');
				if (eq == std::string::npos) { continue; }
				a_keys[Lower(Trim(t.substr(0, eq))) + ":" + section] = t.substr(eq + 1);
			}
		}

		bool LoadFileValues()
		{
			if (!std::filesystem::exists(iniPath))
			{
				logger::warn("INI not found at {}; keeping compiled defaults", iniPath);
				return false;
			}
			std::map<std::string, std::string> k;
			migratedKeys = 0;
			if (!oldIniPath.empty() && std::filesystem::exists(oldIniPath))
			{
				ReadFile(oldIniPath, k);
				migratedKeys = static_cast<int>(k.size());
				logger::info("settings: {} value(s) read from the pre-rename INI {}; the current INI's values take precedence", migratedKeys, oldIniPath);
			}
			ReadFile(iniPath, k);
			auto get = [&](const char* a_key, auto a_apply) {
				const auto it = k.find(a_key);
				if (it == k.end()) { logger::debug("INI key {} missing; keeping current value", a_key); return; }
				if (!a_apply(it->second)) { logger::warn("INI value \"{}\" for {} is not valid; keeping current value", Trim(it->second), a_key); }
			};
			get("uloglevel:debug", [](const std::string& v) { return ParseUInt(v, debug::logLevel); });
			get("benabled:general", [](const std::string& v) { bool b; if (!ParseBool(v, b)) { return false; } general::enabled = b; return true; });
			get("bswitchondefaultchange:general", [](const std::string& v) { bool b; if (!ParseBool(v, b)) { return false; } general::switchOnDefaultChange = b; return true; });
			get("bswitchtonewdevice:general", [](const std::string& v) { bool b; if (!ParseBool(v, b)) { return false; } general::switchToNewDevice = b; return true; });
			get("uresetdelayms:general", [](const std::string& v) {
				std::uint32_t u;
				if (!ParseUInt(v, u)) { return false; }
				general::resetDelayMs = std::clamp<std::uint32_t>(u, 0u, 10000u);
				return true;
			});
			get("bmediakeys:keyboard", [](const std::string& v) { bool b; if (!ParseBool(v, b)) { return false; } keyboard::mediaKeys = b; return true; });
			get("bdisablewindowskey:keyboard", [](const std::string& v) { bool b; if (!ParseBool(v, b)) { return false; } keyboard::disableWindowsKey = b; return true; });
			get("bdisabledeadkeys:keyboard", [](const std::string& v) { bool b; if (!ParseBool(v, b)) { return false; } keyboard::disableDeadKeys = b; return true; });
			get("spreferreddevice:general", [](const std::string& v) { std::string s; ParseString(v, s); SetPreferredDevice(s); return true; });
			logger::info("settings loaded from {}: enabled={} preferredDevice=\"{}\" switchOnDefaultChange={} switchToNewDevice={} resetDelayMs={} logLevel={}", iniPath,
						 general::enabled.load(), GetPreferredDevice(), general::switchOnDefaultChange.load(), general::switchToNewDevice.load(), general::resetDelayMs.load(), debug::logLevel);
			logger::info("keyboard settings: mediaKeys={} disableWindowsKey={} disableDeadKeys={}", keyboard::mediaKeys.load(), keyboard::disableWindowsKey.load(),
						 keyboard::disableDeadKeys.load());
			return true;
		}

		bool WriteKey(std::vector<std::string>& a_lines, const char* a_section, const char* a_key, const std::string& a_value)
		{
			const std::string wantSection = Lower(a_section);
			const std::string wantKey = Lower(a_key);
			std::string section;
			for (auto& line : a_lines)
			{
				const std::string t = Trim(line);
				if (IsSectionHeader(t)) { section = Lower(t.substr(1, t.size() - 2)); continue; }
				const auto eq = t.find('=');
				if (eq == std::string::npos || section != wantSection) { continue; }
				if (Lower(Trim(t.substr(0, eq))) == wantKey)
				{
					line = std::string(a_key) + "=" + a_value;
					return true;
				}
			}
			// A key an older INI does not have yet (a setting added in a later version) goes at the end of its section.
			std::string current;
			std::size_t insertAt = std::string::npos;
			for (std::size_t i = 0; i < a_lines.size(); ++i)
			{
				const std::string t = Trim(a_lines[i]);
				if (IsSectionHeader(t))
				{
					current = Lower(t.substr(1, t.size() - 2));
					if (current == wantSection) { insertAt = i + 1; }
					continue;
				}
				if (current == wantSection && !t.empty()) { insertAt = i + 1; }
			}
			if (insertAt == std::string::npos)
			{
				// A whole section an older INI does not have yet goes at the end of the file.
				if (!a_lines.empty() && !Trim(a_lines.back()).empty()) { a_lines.emplace_back(); }
				a_lines.push_back(std::string("[") + a_section + "]");
				a_lines.push_back(std::string(a_key) + "=" + a_value);
				logger::info("Save: [{}] {} added", a_section, a_key);
				return true;
			}
			a_lines.insert(a_lines.begin() + static_cast<std::ptrdiff_t>(insertAt), std::string(a_key) + "=" + a_value);
			logger::info("Save: {} added to [{}]", a_key, a_section);
			return true;
		}
	}

	std::string GetPreferredDevice()
	{
		std::scoped_lock l(preferredLock);
		return preferredDevice;
	}

	void SetPreferredDevice(std::string a_value)
	{
		std::scoped_lock l(preferredLock);
		preferredDevice = std::move(a_value);
	}

	void Init(const std::string& a_iniFileName, const std::string& a_previousIniFileName)
	{
		iniPath = (std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / a_iniFileName).string();
		oldIniPath = a_previousIniFileName.empty() ? std::string() :
			(std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / a_previousIniFileName).string();

		auto* collection = utils::INISettingCollection::GetSingleton();
		collection->AddSettings(
			utils::MakeSetting("uLogLevel:Debug", static_cast<unsigned int>(debug::logLevel)),
			utils::MakeSetting("bEnabled:General", general::enabled.load()),
			utils::MakeSetting("sPreferredDevice:General", ""),
			utils::MakeSetting("bSwitchOnDefaultChange:General", general::switchOnDefaultChange.load()),
			utils::MakeSetting("bSwitchToNewDevice:General", general::switchToNewDevice.load()),
			utils::MakeSetting("uResetDelayMs:General", static_cast<unsigned int>(general::resetDelayMs.load())),
			utils::MakeSetting("bMediaKeys:Keyboard", keyboard::mediaKeys.load()),
			utils::MakeSetting("bDisableWindowsKey:Keyboard", keyboard::disableWindowsKey.load()),
			utils::MakeSetting("bDisableDeadKeys:Keyboard", keyboard::disableDeadKeys.load()));

		LoadFileValues();
	}

	int MigratedKeys()
	{
		return migratedKeys;
	}

	bool Reload()
	{
		const bool ok = LoadFileValues();
		ApplyLogLevel();
		return ok;
	}

	bool Save()
	{
		std::scoped_lock l(fileLock);
		std::vector<std::string> lines;
		{
			std::ifstream in(iniPath);
			if (!in) { logger::error("Save: could not open {} for reading", iniPath); return false; }
			std::string line;
			while (std::getline(in, line)) { lines.push_back(line); }
		}

		bool ok = true;
		ok &= WriteKey(lines, "Debug", "uLogLevel", std::to_string(debug::logLevel));
		ok &= WriteKey(lines, "General", "bEnabled", general::enabled ? "1" : "0");
		ok &= WriteKey(lines, "General", "sPreferredDevice", GetPreferredDevice());
		ok &= WriteKey(lines, "General", "bSwitchOnDefaultChange", general::switchOnDefaultChange ? "1" : "0");
		ok &= WriteKey(lines, "General", "bSwitchToNewDevice", general::switchToNewDevice ? "1" : "0");
		ok &= WriteKey(lines, "General", "uResetDelayMs", std::to_string(general::resetDelayMs.load()));
		ok &= WriteKey(lines, "Keyboard", "bMediaKeys", keyboard::mediaKeys ? "1" : "0");
		ok &= WriteKey(lines, "Keyboard", "bDisableWindowsKey", keyboard::disableWindowsKey ? "1" : "0");
		ok &= WriteKey(lines, "Keyboard", "bDisableDeadKeys", keyboard::disableDeadKeys ? "1" : "0");

		std::ofstream out(iniPath, std::ios::trunc);
		if (!out) { logger::error("Save: could not open {} for writing", iniPath); return false; }
		for (const auto& line : lines) { out << line << '\n'; }
		logger::info("settings saved to {}", iniPath);
		return ok;
	}

	void RestoreDefaults()
	{
		// The compiled defaults, which the shipped INI repeats (rule 16).
		debug::logLevel = 0;
		general::enabled = true;
		general::switchOnDefaultChange = true;
		general::switchToNewDevice = true;
		general::resetDelayMs = 500;
		keyboard::mediaKeys = true;
		keyboard::disableWindowsKey = true;
		keyboard::disableDeadKeys = false;
		SetPreferredDevice("");
		ApplyLogLevel();
	}

	void ApplyLogLevel()
	{
		const auto lvl = static_cast<spdlog::level::level_enum>(std::clamp<std::uint32_t>(debug::logLevel, 0u, 6u));
		SKSE::log::set_level(lvl, lvl);
	}

	const std::string& GetIniPath() { return iniPath; }
}
