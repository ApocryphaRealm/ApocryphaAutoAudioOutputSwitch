#include "PCH.h"

#include "DevBenchTool.h"

#include "AudioSwitch.h"
#include "DevBench/DevBenchAPI.h"
#include "Settings.h"
#include "utils/Logger.h"

#include <format>
#include <string>
#include <string_view>

namespace DevBenchTool
{
	namespace
	{
		std::string EscapeJson(std::string_view a_in)
		{
			std::string out;
			out.reserve(a_in.size() + 8);
			for (const char c : a_in)
			{
				switch (c)
				{
				case '\\': out += "\\\\"; break;
				case '"': out += "\\\""; break;
				case '\n': out += "\\n"; break;
				default: out += c; break;
				}
			}
			return out;
		}

		// The value of a top-level JSON member, as text: a quoted string or a bare word. Empty when absent.
		std::string Get(std::string_view a_json, const char* a_name, bool* a_present = nullptr)
		{
			if (a_present) { *a_present = false; }
			const std::string key = std::format("\"{}\"", a_name);
			auto pos = a_json.find(key);
			if (pos == std::string_view::npos) { return {}; }
			pos = a_json.find(':', pos + key.size());
			if (pos == std::string_view::npos) { return {}; }
			++pos;
			while (pos < a_json.size() && (a_json[pos] == ' ' || a_json[pos] == '\t')) { ++pos; }
			if (pos >= a_json.size()) { return {}; }
			if (a_present) { *a_present = true; }
			if (a_json[pos] == '"')
			{
				std::string out;
				for (++pos; pos < a_json.size() && a_json[pos] != '"'; ++pos)
				{
					if (a_json[pos] == '\\' && pos + 1 < a_json.size()) { ++pos; }
					out += a_json[pos];
				}
				return out;
			}
			std::string out;
			while (pos < a_json.size() && a_json[pos] != ',' && a_json[pos] != '}' && a_json[pos] != ' ') { out += a_json[pos++]; }
			return out;
		}

		std::string SettingsJson()
		{
			return std::format(
				"\"settings\":{{\"enabled\":{},\"preferredDevice\":\"{}\",\"switchOnDefaultChange\":{},\"resetDelayMs\":{},\"logLevel\":{},\"iniPath\":\"{}\"}}",
				settings::general::enabled ? "true" : "false", EscapeJson(settings::GetPreferredDevice()),
				settings::general::switchOnDefaultChange ? "true" : "false", settings::general::resetDelayMs.load(), settings::debug::logLevel,
				EscapeJson(settings::GetIniPath()));
		}

		std::string StateReply(const char* a_op, bool a_ok, std::string_view a_extra = {})
		{
			return std::format("{{\"ok\":{},\"op\":\"{}\",{},{}{}}}", a_ok ? "true" : "false", a_op, SettingsJson(), audioswitch::StateJson(), a_extra);
		}

		void ControlTool(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const std::string_view args = a_argsJson ? a_argsJson : "";
			const std::string op = Get(args, "op");

			if (op == "devices")
			{
				a_write(a_sink, std::format("{{\"ok\":true,\"op\":\"devices\",{}}}", audioswitch::DevicesJson()).c_str());
				return;
			}
			if (op == "reset" || op == "check")
			{
				audioswitch::RequestReset(op == "reset" ? "DevBench tool: forced reset" : "DevBench tool: check", op == "reset");
				const bool idle = audioswitch::WaitIdle(10000);
				a_write(a_sink, StateReply(op == "reset" ? "reset" : "check", idle, idle ? "" : ",\"error\":\"the worker did not finish within 10 s\"").c_str());
				return;
			}
			if (op == "prefer")
			{
				bool present = false;
				const std::string name = Get(args, "name", &present);
				if (!present)
				{
					a_write(a_sink, R"j({"ok":false,"op":"prefer","error":"need name (part of a device name, a full device id, or empty for the Windows default)"})j");
					return;
				}
				settings::SetPreferredDevice(name);
				const bool saved = settings::Save();
				audioswitch::RequestReset(std::format("DevBench tool: preferred device set to \"{}\"", name), false);
				const bool idle = audioswitch::WaitIdle(10000);
				a_write(a_sink, StateReply("prefer", saved && idle).c_str());
				return;
			}
			if (op == "set")
			{
				bool changed = false;
				bool present = false;
				std::string v = Get(args, "enabled", &present);
				if (present) { settings::general::enabled = (v == "true" || v == "1"); changed = true; }
				v = Get(args, "switchOnDefaultChange", &present);
				if (present) { settings::general::switchOnDefaultChange = (v == "true" || v == "1"); changed = true; }
				v = Get(args, "resetDelayMs", &present);
				if (present)
				{
					try { settings::general::resetDelayMs = std::min<std::uint32_t>(static_cast<std::uint32_t>(std::stoul(v)), 10000u); changed = true; } catch (...) {}
				}
				const bool saved = changed && settings::Save();
				a_write(a_sink, StateReply("set", saved, changed ? "" : ",\"error\":\"nothing to set (enabled, switchOnDefaultChange, resetDelayMs)\"").c_str());
				return;
			}
			if (op == "reload")
			{
				const bool ok = settings::Reload();
				audioswitch::RequestReset("DevBench tool: settings reloaded", false);
				const bool idle = audioswitch::WaitIdle(10000);
				a_write(a_sink, StateReply("reload", ok && idle).c_str());
				return;
			}

			a_write(a_sink, StateReply("state", true).c_str());
		}
	}

	void Init(bool a_lastAttempt)
	{
		static bool registered = false;
		if (registered) { return; }

		DevBenchAPI::IDevBenchInterface001* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench)
		{
			if (a_lastAttempt) { logger::info("DevBench not detected; skipping the \"aais.control\" tool"); }
			else { logger::debug("DevBench not detected yet; will retry at the next message"); }
			return;
		}

		constexpr const char* descriptor =
			"{"
			"\"description\":\"Auto Audio Input Switch live state and driver. op=state (default): settings, hooks, worker, every managed "
			"XAudio2 engine (device, attached, processing passes, critical errors, resets, last result). op=devices: the XAudio2 device list "
			"and the Windows render endpoints with state and default. op=check runs a reset check now (switches only if needed); op=reset "
			"forces a switch to the target device. op=prefer name=\\\"...\\\" sets the preferred device (part of a name, a full id, or empty) "
			"and checks. op=set with enabled / switchOnDefaultChange / resetDelayMs. op=reload re-reads the INI and checks.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},"
			"\"enabled\":{\"type\":\"boolean\"},\"switchOnDefaultChange\":{\"type\":\"boolean\"},\"resetDelayMs\":{\"type\":\"integer\"}}},"
			"\"readOnly\":false"
			"}";

		if (devBench->RegisterTool("aais.control", descriptor, &ControlTool, nullptr))
		{
			logger::info("Registered \"aais.control\" with DevBench (build {})", devBench->GetBuildNumber());
			registered = true;
		}
	}
}
