// Auto Audio Output Switch - media keys in game. GPL-3.0-or-later (2026-09-14). The approach follows Media Keys Fix SKSE by
// Emerson Pinter (LGPL-3.0-or-later); see KeyboardAccess.h and THIRD_PARTY_NOTICES.md.
#include "PCH.h"

#include "KeyboardAccess.h"

#include "Settings.h"
#include "utils/Logger.h"

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <mutex>
#include <string>
#include <system_error>

namespace mediakeys
{
	namespace
	{
		constexpr std::uint32_t kDisclNonExclusive = 0x02;
		constexpr std::uint32_t kDisclForeground = 0x04;
		constexpr std::uint32_t kDisclNoWinKey = 0x10;

		std::mutex g_lock;
		Status g_status = Status::kNotRun;  // guarded by g_lock
		std::string g_detail = "not applied yet";
		std::uint32_t g_flags = 0;
		bool g_deadKeys = false;

		using ToUnicodeFn = int (*)(UINT, UINT, const BYTE*, LPWSTR, int, UINT);
		ToUnicodeFn g_originalToUnicode = nullptr;

		// A dead key makes ToUnicode return 2 (the accent, then the character); report one character so the game types it.
		int ToUnicodeHook(UINT a_vk, UINT a_scan, const BYTE* a_state, LPWSTR a_buffer, int a_size, UINT a_flags)
		{
			const int result = g_originalToUnicode ? g_originalToUnicode(a_vk, a_scan, a_state, a_buffer, a_size, a_flags) : 0;
			return result > 0 ? 1 : result;
		}

		// -1 = any byte.
		bool Matches(std::uintptr_t a_address, std::initializer_list<int> a_bytes)
		{
			const auto* p = reinterpret_cast<const std::uint8_t*>(a_address);
			std::size_t i = 0;
			for (const int b : a_bytes)
			{
				if (b >= 0 && p[i] != static_cast<std::uint8_t>(b)) { return false; }
				++i;
			}
			return true;
		}

		std::string EscapeJson(const std::string& a_in)
		{
			std::string out;
			for (const char c : a_in)
			{
				if (c == '"' || c == '\\') { out += '\\'; }
				if (static_cast<unsigned char>(c) < 0x20) { out += ' '; continue; }
				out += c;
			}
			return out;
		}

		void Set(Status a_status, std::string a_detail)
		{
			std::scoped_lock l(g_lock);
			g_status = a_status;
			g_detail = std::move(a_detail);
		}

		const char* StatusName(Status a_status)
		{
			switch (a_status)
			{
			case Status::kOff: return "off";
			case Status::kActive: return "active";
			case Status::kOtherMod: return "Media Keys Fix SKSE handles it";
			case Status::kFailed: return "failed";
			default: return "not run";
			}
		}
	}

	void Install()
	{
		if (!settings::keyboard::mediaKeys)
		{
			Set(Status::kOff, "bMediaKeys is off: the game keeps exclusive access to the keyboard");
			logger::info("media keys: off (bMediaKeys=0); the game keeps exclusive access to the keyboard");
			return;
		}

		std::error_code ec;
		if (std::filesystem::exists(std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "MediaKeysFix.dll", ec))
		{
			Set(Status::kOtherMod, "Media Keys Fix SKSE (MediaKeysFix.dll) is installed and removes the exclusive keyboard access itself");
			logger::info("media keys: Media Keys Fix SKSE is installed; it removes the game's exclusive keyboard access, so this mod leaves the keyboard alone");
			return;
		}

		const std::uintptr_t setup = REL::RelocationID(67471, 68781).address();
		const std::uintptr_t instruction = setup + 0x55;
		if (!Matches(instruction, { 0x41, 0xB8, 0x15, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD0, 0xFF }))
		{
			Set(Status::kFailed, std::format("the keyboard's SetCooperativeLevel flags were not found at +0x{:X} of the DirectInput setup", instruction - REL::Module::get().base()));
			logger::warn("media keys: the game's keyboard access flags (mov r8d, 15h) were not found at +0x{:X}; nothing changed, media keys stay with the game",
						 instruction - REL::Module::get().base());
			return;
		}
		std::uint32_t flags = kDisclForeground | kDisclNonExclusive;
		if (settings::keyboard::disableWindowsKey) { flags |= kDisclNoWinKey; }
		REL::safe_write(instruction + 2, flags);
		{
			std::scoped_lock l(g_lock);
			g_flags = flags;
		}
		Set(Status::kActive, std::format("keyboard access flags 0x15 -> 0x{:02X} (non-exclusive{})", flags, settings::keyboard::disableWindowsKey ? ", Windows key disabled" : ""));
		logger::info("media keys: the game's keyboard is now non-exclusive (SetCooperativeLevel flags 0x15 -> 0x{:02X}{}); Windows handles the media keys in game",
					 flags, settings::keyboard::disableWindowsKey ? ", Windows key disabled" : ", Windows key enabled");

		if (!settings::keyboard::disableDeadKeys) { return; }
		const std::uintptr_t call = REL::RelocationID(67472, 68782).address() + REL::Relocate(0x20D, 0x2CB);
		if (!Matches(call, { 0xFF, 0x15, -1, -1, -1, -1, 0x83, 0xF8, 0x01, 0x75 }))
		{
			logger::warn("media keys: the game's ToUnicode call was not found at +0x{:X}; dead keys are not changed", call - REL::Module::get().base());
			return;
		}
		auto& trampoline = SKSE::GetTrampoline();
		g_originalToUnicode = *reinterpret_cast<ToUnicodeFn*>(trampoline.write_call<6>(call, reinterpret_cast<std::uintptr_t>(&ToUnicodeHook)));
		if (!g_originalToUnicode)
		{
			logger::warn("media keys: wrapping the game's ToUnicode call returned no original function; dead keys may misbehave");
			return;
		}
		{
			std::scoped_lock l(g_lock);
			g_deadKeys = true;
		}
		logger::info("media keys: dead keys disabled (the game's ToUnicode call is wrapped)");
	}

	Status GetStatus()
	{
		std::scoped_lock l(g_lock);
		return g_status;
	}

	std::string StateJson()
	{
		std::scoped_lock l(g_lock);
		return std::format(R"("mediaKeys":{{"status":"{}","detail":"{}","flags":"0x{:02X}","deadKeysDisabled":{}}})", StatusName(g_status), EscapeJson(g_detail), g_flags,
						   g_deadKeys ? "true" : "false");
	}
}
