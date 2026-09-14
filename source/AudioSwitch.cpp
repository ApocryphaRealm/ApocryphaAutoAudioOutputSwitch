// Auto Audio Input Switch - own code, GPL-3.0-or-later (2026-09-13). The design is described in AudioSwitch.h.
#include "PCH.h"

#include "AudioSwitch.h"

#include "Settings.h"
#include "XAudio27.h"
#include "utils/Logger.h"

#include <Windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace audioswitch
{
	namespace
	{
		using namespace xa27;

		// The stand-in submix sits after every voice the game can create (a voice may only send to a submix with a
		// higher processing stage).
		constexpr UINT32 kProxyStage = 0x7FFFFFFF;

		std::string Narrow(const wchar_t* a_text)
		{
			if (!a_text || !*a_text) { return {}; }
			const int len = WideCharToMultiByte(CP_UTF8, 0, a_text, -1, nullptr, 0, nullptr, nullptr);
			if (len <= 1) { return {}; }
			std::string out(static_cast<std::size_t>(len - 1), '\0');
			WideCharToMultiByte(CP_UTF8, 0, a_text, -1, out.data(), len, nullptr, nullptr);
			return out;
		}

		std::string Lower(std::string a_text)
		{
			for (char& c : a_text) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_text;
		}

		std::string Trim(const std::string& a_text)
		{
			const auto b = a_text.find_first_not_of(" \t\r\n\"");
			if (b == std::string::npos) { return {}; }
			const auto e = a_text.find_last_not_of(" \t\r\n\"");
			return a_text.substr(b, e - b + 1);
		}

		std::string Hr(HRESULT a_hr) { return std::format("0x{:08X}", static_cast<std::uint32_t>(a_hr)); }

		std::string Ptr(const void* a_ptr) { return std::format("{:X}", reinterpret_cast<std::uintptr_t>(a_ptr)); }

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
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (static_cast<unsigned char>(c) < 0x20) { out += std::format("\\u{:04x}", static_cast<unsigned char>(c)); }
					else { out += c; }
					break;
				}
			}
			return out;
		}

		const char* EndpointStateName(DWORD a_state)
		{
			switch (a_state)
			{
			case DEVICE_STATE_ACTIVE: return "active";
			case DEVICE_STATE_DISABLED: return "disabled";
			case DEVICE_STATE_NOTPRESENT: return "not present";
			case DEVICE_STATE_UNPLUGGED: return "unplugged";
			default: return "unknown";
			}
		}

		thread_local int t_inside = 0;                    // > 0 while this plugin itself calls the original methods

		struct Inside
		{
			Inside() { ++t_inside; }
			~Inside() { --t_inside; }
		};

		struct Device
		{
			UINT32 index{ 0 };
			std::string id;
			std::string name;
			UINT32 role{ 0 };
			UINT32 channels{ 0 };
			UINT32 rate{ 0 };
		};

		std::vector<Device> ListDevices(IXAudio2* a_engine, HRESULT& a_hr)
		{
			std::vector<Device> out;
			UINT32 count = 0;
			{
				Inside inside;
				a_hr = a_engine->GetDeviceCount(&count);
			}
			if (FAILED(a_hr)) { return out; }
			for (UINT32 i = 0; i < count; ++i)
			{
				DeviceDetails details{};
				const HRESULT hr = a_engine->GetDeviceDetails(i, &details);
				if (FAILED(hr))
				{
					logger::debug("GetDeviceDetails({}) failed: {}", i, Hr(hr));
					continue;
				}
				details.DeviceID[255] = L'\0';
				details.DisplayName[255] = L'\0';
				out.push_back({ i, Narrow(details.DeviceID), Narrow(details.DisplayName), details.Role,
								details.OutputFormat.Format.nChannels, details.OutputFormat.Format.nSamplesPerSec });
			}
			return out;
		}

		void LogDevices(const std::vector<Device>& a_devices, HRESULT a_hr, std::string_view a_when)
		{
			if (FAILED(a_hr)) { logger::warn("{}: XAudio2 could not list output devices ({})", a_when, Hr(a_hr)); }
			logger::debug("{}: XAudio2 lists {} output device(s)", a_when, a_devices.size());
			for (const auto& d : a_devices)
			{
				logger::debug("  [{}] \"{}\" role=0x{:X}{} channels={} rate={} id={}", d.index, d.name, d.role,
							  (d.role & kDefaultGameDevice) ? " (Windows default)" : "", d.channels, d.rate, d.id);
			}
		}

		// sPreferredDevice: a full device id, or part of a device name (case-insensitive). Empty = none.
		const Device* PreferredMatch(const std::vector<Device>& a_devices)
		{
			const std::string pref = Lower(Trim(settings::GetPreferredDevice()));
			if (pref.empty()) { return nullptr; }
			for (const auto& d : a_devices) { if (Lower(d.id) == pref) { return &d; } }
			for (const auto& d : a_devices) { if (Lower(d.name).find(pref) != std::string::npos) { return &d; } }
			return nullptr;
		}

		const Device* DefaultDevice(const std::vector<Device>& a_devices)
		{
			for (const auto& d : a_devices) { if (d.role & kDefaultGameDevice) { return &d; } }
			return a_devices.empty() ? nullptr : &a_devices.front();
		}

		class EngineCallback;

		struct Engine
		{
			std::uint64_t serial{ 0 };
			IXAudio2* engine{ nullptr };
			IXAudio2Voice* master{ nullptr };  // the real mastering voice; null while no device is attached
			IXAudio2Voice* proxy{ nullptr };   // the submix the game holds as its mastering voice
			UINT32 reqChannels{ 0 };
			UINT32 reqRate{ 0 };
			UINT32 reqFlags{ 0 };
			UINT32 gameIndex{ 0 };
			UINT32 proxyChannels{ 0 };
			UINT32 proxyRate{ 0 };
			std::string deviceId;
			std::string deviceName;
			EngineCallback* callback{ nullptr };
			std::atomic<bool> critical{ false };
			std::atomic<std::uint32_t> criticalCount{ 0 };
			std::atomic<std::int32_t> lastCriticalHr{ 0 };
			std::atomic<std::uint64_t> passes{ 0 };
			std::uint32_t resets{ 0 };
			std::string lastResult{ "created" };
		};

		class EngineCallback final : public IXAudio2EngineCallback
		{
		public:
			explicit EngineCallback(Engine* a_engine) :
				_engine(a_engine)
			{}

			void STDMETHODCALLTYPE OnProcessingPassStart() override {}

			void STDMETHODCALLTYPE OnProcessingPassEnd() override { _engine->passes.fetch_add(1, std::memory_order_relaxed); }

			// XAudio2's own thread: record and wake the worker, nothing else (no XAudio2 calls, no logging).
			void STDMETHODCALLTYPE OnCriticalError(HRESULT a_error) override
			{
				_engine->lastCriticalHr = static_cast<std::int32_t>(a_error);
				_engine->criticalCount.fetch_add(1);
				_engine->critical = true;
				RequestReset("the audio engine reported a critical error (its device stopped working)", false);
			}

		private:
			Engine* _engine;
		};

		std::recursive_mutex g_lock;                      // guards g_engines and every call that changes a voice graph
		std::vector<std::unique_ptr<Engine>> g_engines;
		std::atomic<std::uint64_t> g_nextSerial{ 1 };

		ReleaseFn g_origRelease{ nullptr };
		CreateSourceFn g_origSource{ nullptr };
		CreateSubmixFn g_origSubmix{ nullptr };
		CreateMasteringFn g_origMaster{ nullptr };
		DestroyVoiceFn g_origSubmixDestroy{ nullptr };
		InitializeFn g_origInitialize{ nullptr };
		GetDeviceCountFn g_origDeviceCount{ nullptr };
		std::atomic<std::uint32_t> g_initializeCalls{ 0 };
		std::atomic<std::uint32_t> g_deviceCountCalls{ 0 };
		std::atomic<bool> g_hooked{ false };
		std::atomic<bool> g_destroyHooked{ false };
		std::atomic<std::uint32_t> g_redirectedSources{ 0 };
		std::atomic<std::uint32_t> g_redirectedSubmixes{ 0 };
		std::string g_installResult{ "not run" };          // written once at load, before any other thread exists

		std::mutex g_wakeLock;                            // guards the fields below
		std::condition_variable g_wakeCv;
		bool g_pending{ false };
		bool g_force{ false };
		bool g_busy{ false };
		std::string g_pendingReason;
		std::string g_lastReason{ "none" };
		std::string g_watcherResult{ "not started" };
		std::chrono::steady_clock::time_point g_lastEvent{};
		std::uint32_t g_requests{ 0 };
		std::uint32_t g_runs{ 0 };

		Engine* FindEngine(IXAudio2* a_engine)
		{
			for (auto& e : g_engines) { if (e->engine == a_engine) { return e.get(); } }
			return nullptr;
		}

		bool PatchSlot(void** a_vtable, std::size_t a_slot, void* a_hook, void** a_original, const char* a_name)
		{
			void** entry = a_vtable + a_slot;
			if (*entry == a_hook)
			{
				logger::debug("{} is already hooked", a_name);
				return true;
			}
			DWORD old = 0;
			if (!VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &old))
			{
				logger::error("could not unprotect the vtable slot for {} (error {})", a_name, GetLastError());
				return false;
			}
			*a_original = *entry;
			*entry = a_hook;
			VirtualProtect(entry, sizeof(void*), old, &old);
			logger::info("hooked {} (vtable slot {} at {}, original {})", a_name, a_slot, Ptr(entry), Ptr(*a_original));
			return true;
		}

		void STDMETHODCALLTYPE HookSubmixDestroy(IXAudio2Voice* a_voice);

		void HookDestroyOn(IXAudio2Voice* a_voice)
		{
			if (g_destroyHooked.exchange(true)) { return; }
			void** vtable = *reinterpret_cast<void***>(a_voice);
			if (!PatchSlot(vtable, slot::kDestroyVoice, reinterpret_cast<void*>(&HookSubmixDestroy),
					reinterpret_cast<void**>(&g_origSubmixDestroy), "IXAudio2SubmixVoice::DestroyVoice"))
			{
				g_destroyHooked = false;
			}
		}

		// ---- the swap ----

		bool Swap(Engine& a_engine, const Device& a_target, const std::vector<Device>& a_devices)
		{
			const std::string from = a_engine.deviceName.empty() ? std::string("(no device)") : a_engine.deviceName;
			VoiceSends none{ 0, nullptr };
			const HRESULT detach = a_engine.proxy->SetOutputVoices(&none);
			if (FAILED(detach)) { logger::warn("detaching the stand-in voice failed ({}); continuing", Hr(detach)); }
			if (a_engine.master)
			{
				a_engine.master->DestroyVoice();
				a_engine.master = nullptr;
			}

			std::vector<const Device*> order{ &a_target };
			if (const Device* def = DefaultDevice(a_devices); def && def != &a_target) { order.push_back(def); }
			for (const auto& d : a_devices)
			{
				if (std::find(order.begin(), order.end(), &d) == order.end()) { order.push_back(&d); }
			}

			IXAudio2Voice* master = nullptr;
			const Device* used = nullptr;
			for (const Device* d : order)
			{
				HRESULT hr;
				{
					Inside inside;
					hr = g_origMaster(a_engine.engine, &master, a_engine.reqChannels, a_engine.reqRate, a_engine.reqFlags, d->index, nullptr);
				}
				logger::debug("CreateMasteringVoice on [{}] \"{}\" (channels={} rate={}): {}", d->index, d->name, a_engine.reqChannels, a_engine.reqRate, Hr(hr));
				if (SUCCEEDED(hr) && master)
				{
					used = d;
					break;
				}
				master = nullptr;
			}

			if (!used)
			{
				a_engine.deviceId.clear();
				a_engine.deviceName.clear();
				a_engine.lastResult = std::format("no device accepted a mastering voice (was \"{}\"); silent until a device appears", from);
				logger::warn("{}", a_engine.lastResult);
				return false;
			}

			SendDescriptor toMaster{ 0, master };
			VoiceSends sends{ 1, &toMaster };
			const HRESULT attach = a_engine.proxy->SetOutputVoices(&sends);
			a_engine.master = master;
			a_engine.deviceId = used->id;
			a_engine.deviceName = used->name;
			std::string restart;
			if (a_engine.critical.exchange(false))
			{
				const HRESULT start = a_engine.engine->StartEngine();
				restart = std::format("; engine restarted after the critical error ({})", Hr(start));
			}
			++a_engine.resets;
			a_engine.lastResult = std::format("switched \"{}\" -> \"{}\" (attach {}{})", from, used->name, Hr(attach), restart);
			if (used != &a_target) { a_engine.lastResult += std::format("; \"{}\" refused, used the next device", a_target.name); }
			logger::info("engine {}: {}", Ptr(a_engine.engine), a_engine.lastResult);
			return SUCCEEDED(attach);
		}

		void DoResets(const std::string& a_reason, bool a_force)
		{
			std::scoped_lock lock(g_lock);
			if (g_engines.empty())
			{
				logger::debug("reset requested ({}) but the game has not created its audio engine yet", a_reason);
				return;
			}
			for (auto& owned : g_engines)
			{
				Engine& e = *owned;
				if (!e.proxy) { continue; }
				HRESULT listHr = S_OK;
				const auto devices = ListDevices(e.engine, listHr);
				LogDevices(devices, listHr, "reset check");

				const Device* current = nullptr;
				if (!e.deviceId.empty())
				{
					for (const auto& d : devices) { if (d.id == e.deviceId) { current = &d; } }
				}
				const Device* preferred = PreferredMatch(devices);
				const Device* def = DefaultDevice(devices);
				const Device* target = preferred ? preferred : def;
				const bool critical = e.critical.load();

				bool swap = false;
				std::string decision;
				if (!target) { decision = "no output device exists; waiting for one"; }
				else if (a_force) { swap = true; decision = "forced"; }
				else if (critical) { swap = true; decision = "the engine reported a critical error"; }
				else if (!e.master) { swap = true; decision = "no device was attached"; }
				else if (!current) { swap = true; decision = "the current device is gone"; }
				else if (preferred) { swap = current != preferred; decision = swap ? "the preferred device is available" : "already on the preferred device"; }
				else if (settings::general::switchOnDefaultChange) { swap = current != def; decision = swap ? "the Windows default device changed" : "already on the Windows default device"; }
				else { decision = "the current device is still present and bSwitchOnDefaultChange is off"; }

				logger::info("reset check ({}): engine {} on \"{}\", target \"{}\", critical={}: {}", a_reason, Ptr(e.engine),
							 e.deviceName.empty() ? "(none)" : e.deviceName, target ? target->name : "(none)", critical, decision);
				if (swap) { Swap(e, *target, devices); }
				else { e.lastResult = decision; }
			}
		}

		// ---- the hooks ----

		// Diagnostics: prove whether an engine is created and initialised AFTER the hooks went in, even on a machine
		// with no output device (where the game never reaches CreateMasteringVoice).
		HRESULT STDMETHODCALLTYPE HookInitialize(IXAudio2* a_this, UINT32 a_flags, UINT32 a_processor)
		{
			const HRESULT hr = g_origInitialize(a_this, a_flags, a_processor);
			if (t_inside == 0)
			{
				const auto n = g_initializeCalls.fetch_add(1) + 1;
				logger::info("engine {} initialised (flags=0x{:X} processor=0x{:X}): {} - call {}", Ptr(a_this), a_flags, a_processor, Hr(hr), n);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE HookGetDeviceCount(IXAudio2* a_this, UINT32* a_count)
		{
			const HRESULT hr = g_origDeviceCount(a_this, a_count);
			if (t_inside == 0 && g_deviceCountCalls.fetch_add(1) < 4)
			{
				logger::info("engine {} asked for its device count: {} device(s) ({})", Ptr(a_this), a_count ? *a_count : 0u, Hr(hr));
			}
			return hr;
		}


		HRESULT STDMETHODCALLTYPE HookCreateMastering(IXAudio2* a_this, IXAudio2Voice** a_out, UINT32 a_channels, UINT32 a_rate, UINT32 a_flags, UINT32 a_index, void* a_chain)
		{
			if (t_inside > 0 || !a_out || !settings::general::enabled)
			{
				return g_origMaster(a_this, a_out, a_channels, a_rate, a_flags, a_index, a_chain);
			}

			std::scoped_lock lock(g_lock);
			logger::info("engine {} creates its mastering voice: channels={} rate={} flags=0x{:X} device index={} effect chain={}",
						 Ptr(a_this), a_channels, a_rate, a_flags, a_index, Ptr(a_chain));
			if (FindEngine(a_this))
			{
				logger::warn("engine {} already has a managed mastering voice; this one passes through unchanged", Ptr(a_this));
				return g_origMaster(a_this, a_out, a_channels, a_rate, a_flags, a_index, a_chain);
			}

			HRESULT listHr = S_OK;
			const auto devices = ListDevices(a_this, listHr);
			LogDevices(devices, listHr, "engine start");

			UINT32 pick = a_index;
			std::string why = "the game's choice (the Windows default device)";
			if (const Device* preferred = PreferredMatch(devices))
			{
				pick = preferred->index;
				why = std::format("the preferred device \"{}\"", preferred->name);
			}
			else if (!settings::GetPreferredDevice().empty())
			{
				logger::info("preferred device \"{}\" is not present; using the game's choice", settings::GetPreferredDevice());
			}

			IXAudio2Voice* master = nullptr;
			HRESULT hr;
			{
				Inside inside;
				hr = g_origMaster(a_this, &master, a_channels, a_rate, a_flags, pick, nullptr);
			}
			if ((FAILED(hr) || !master) && pick != a_index)
			{
				logger::warn("the preferred device refused a mastering voice ({}); using the game's choice", Hr(hr));
				pick = a_index;
				why = "the game's choice (the preferred device refused)";
				Inside inside;
				hr = g_origMaster(a_this, &master, a_channels, a_rate, a_flags, pick, nullptr);
			}
			if (FAILED(hr) || !master)
			{
				logger::error("no mastering voice could be created ({}): no output device was usable when the game started, so the game has no audio this session", Hr(hr));
				return FAILED(hr) ? hr : E_FAIL;
			}

			VoiceDetails details{};
			master->GetVoiceDetails(&details);
			SendDescriptor toMaster{ 0, master };
			VoiceSends sends{ 1, &toMaster };
			IXAudio2Voice* proxy = nullptr;
			{
				Inside inside;
				hr = g_origSubmix(a_this, &proxy, details.InputChannels, details.InputSampleRate, 0, kProxyStage, &sends, a_chain);
			}
			if (FAILED(hr) || !proxy)
			{
				logger::error("could not create the stand-in submix voice ({}); this engine keeps a plain mastering voice and cannot switch devices", Hr(hr));
				*a_out = master;
				return S_OK;
			}
			HookDestroyOn(proxy);

			auto engine = std::make_unique<Engine>();
			engine->serial = g_nextSerial++;
			engine->engine = a_this;
			engine->master = master;
			engine->proxy = proxy;
			engine->reqChannels = a_channels;
			engine->reqRate = a_rate;
			engine->reqFlags = a_flags;
			engine->gameIndex = a_index;
			engine->proxyChannels = details.InputChannels;
			engine->proxyRate = details.InputSampleRate;
			for (const auto& d : devices)
			{
				if (d.index == pick)
				{
					engine->deviceId = d.id;
					engine->deviceName = d.name;
				}
			}
			engine->callback = new EngineCallback(engine.get());
			const HRESULT cb = a_this->RegisterForCallbacks(engine->callback);
			engine->lastResult = std::format("started on \"{}\" ({})", engine->deviceName, why);
			logger::info("engine {}: real mastering voice {} on [{}] \"{}\" ({}); stand-in submix {} ({} ch, {} Hz) handed to the game; critical-error callback {}",
						 Ptr(a_this), Ptr(master), pick, engine->deviceName, why, Ptr(proxy), details.InputChannels, details.InputSampleRate, Hr(cb));
			*a_out = proxy;
			g_engines.push_back(std::move(engine));
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE HookCreateSubmix(IXAudio2* a_this, IXAudio2Voice** a_out, UINT32 a_channels, UINT32 a_rate, UINT32 a_flags, UINT32 a_stage, const VoiceSends* a_sends, void* a_chain)
		{
			if (t_inside > 0 || a_sends)
			{
				return g_origSubmix(a_this, a_out, a_channels, a_rate, a_flags, a_stage, a_sends, a_chain);
			}
			std::scoped_lock lock(g_lock);
			const Engine* engine = FindEngine(a_this);
			if (!engine || !engine->proxy || a_stage >= kProxyStage)
			{
				return g_origSubmix(a_this, a_out, a_channels, a_rate, a_flags, a_stage, a_sends, a_chain);
			}
			SendDescriptor toProxy{ 0, engine->proxy };
			VoiceSends sends{ 1, &toProxy };
			const HRESULT hr = g_origSubmix(a_this, a_out, a_channels, a_rate, a_flags, a_stage, &sends, a_chain);
			if (g_redirectedSubmixes.fetch_add(1) == 0)
			{
				logger::debug("first submix voice with the default send list pointed at the stand-in ({})", Hr(hr));
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE HookCreateSource(IXAudio2* a_this, IXAudio2Voice** a_out, const WAVEFORMATEX* a_format, UINT32 a_flags, float a_maxRatio, void* a_callback, const VoiceSends* a_sends, void* a_chain)
		{
			if (t_inside > 0 || a_sends)
			{
				return g_origSource(a_this, a_out, a_format, a_flags, a_maxRatio, a_callback, a_sends, a_chain);
			}
			std::scoped_lock lock(g_lock);
			const Engine* engine = FindEngine(a_this);
			if (!engine || !engine->proxy)
			{
				return g_origSource(a_this, a_out, a_format, a_flags, a_maxRatio, a_callback, a_sends, a_chain);
			}
			SendDescriptor toProxy{ 0, engine->proxy };
			VoiceSends sends{ 1, &toProxy };
			const HRESULT hr = g_origSource(a_this, a_out, a_format, a_flags, a_maxRatio, a_callback, &sends, a_chain);
			if (g_redirectedSources.fetch_add(1) == 0)
			{
				logger::debug("first source voice with the default send list pointed at the stand-in ({})", Hr(hr));
			}
			return hr;
		}

		void STDMETHODCALLTYPE HookSubmixDestroy(IXAudio2Voice* a_voice)
		{
			std::scoped_lock lock(g_lock);
			const auto it = std::find_if(g_engines.begin(), g_engines.end(), [&](const auto& e) { return e->proxy == a_voice; });
			g_origSubmixDestroy(a_voice);
			if (it == g_engines.end()) { return; }

			Engine& e = **it;
			logger::info("engine {}: the game destroyed its mastering voice (audio shutdown); destroying the real one on \"{}\"", Ptr(e.engine), e.deviceName);
			if (e.master)
			{
				e.master->DestroyVoice();
				e.master = nullptr;
			}
			e.engine->UnregisterForCallbacks(e.callback);
			delete e.callback;
			g_engines.erase(it);
		}

		ULONG STDMETHODCALLTYPE HookRelease(IXAudio2* a_this)
		{
			std::scoped_lock lock(g_lock);
			const ULONG refs = g_origRelease(a_this);
			if (refs == 0)
			{
				const auto it = std::find_if(g_engines.begin(), g_engines.end(), [&](const auto& e) { return e->engine == a_this; });
				if (it != g_engines.end())
				{
					logger::info("engine {} released with its stand-in still recorded; record dropped", Ptr(a_this));
					delete (*it)->callback;  // the engine is gone, so it can no longer call it
					g_engines.erase(it);
				}
			}
			return refs;
		}

		// ---- Windows endpoint notifications ----

		class NotificationClient final : public IMMNotificationClient
		{
		public:
			ULONG STDMETHODCALLTYPE AddRef() override { return 1; }   // static lifetime
			ULONG STDMETHODCALLTYPE Release() override { return 1; }

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID a_iid, void** a_out) override
			{
				if (!a_out) { return E_POINTER; }
				if (a_iid == IID_IUnknown || a_iid == __uuidof(IMMNotificationClient))
				{
					*a_out = static_cast<IMMNotificationClient*>(this);
					return S_OK;
				}
				*a_out = nullptr;
				return E_NOINTERFACE;
			}

			HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR a_id, DWORD a_state) override
			{
				RequestReset(std::format("endpoint {} is now {}", Narrow(a_id), EndpointStateName(a_state)), false);
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR a_id) override
			{
				RequestReset(std::format("endpoint {} added", Narrow(a_id)), false);
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR a_id) override
			{
				RequestReset(std::format("endpoint {} removed", Narrow(a_id)), false);
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow a_flow, ERole a_role, LPCWSTR a_id) override
			{
				if (a_flow == eRender && a_role == eConsole)
				{
					RequestReset(std::format("the Windows default output changed to {}", a_id ? Narrow(a_id) : std::string("(none)")), false);
				}
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
		};

		NotificationClient g_client;

		void WorkerMain()
		{
			const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			IMMDeviceEnumerator* enumerator = nullptr;
			HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
			if (SUCCEEDED(hr) && enumerator) { hr = enumerator->RegisterEndpointNotificationCallback(&g_client); }
			{
				std::scoped_lock l(g_wakeLock);
				g_watcherResult = SUCCEEDED(hr) ? std::string("watching Windows audio endpoints") :
				                                  std::format("endpoint notifications unavailable ({}); only critical errors and the tool trigger resets", Hr(hr));
			}
			if (SUCCEEDED(hr)) { logger::info("worker started (COM {}); watching Windows audio endpoints", Hr(co)); }
			else { logger::error("worker started (COM {}) but endpoint notifications could not be registered ({})", Hr(co), Hr(hr)); }
			// The enumerator stays alive for the life of the process: releasing it would end the notifications.

			for (;;)
			{
				std::string reason;
				bool force = false;
				{
					std::unique_lock l(g_wakeLock);
					g_wakeCv.wait(l, [] { return g_pending; });
					for (;;)
					{
						const auto due = g_lastEvent + std::chrono::milliseconds(settings::general::resetDelayMs.load());
						if (std::chrono::steady_clock::now() >= due) { break; }
						g_wakeCv.wait_until(l, due);
					}
					reason = std::move(g_pendingReason);
					force = g_force;
					g_pendingReason.clear();
					g_pending = false;
					g_force = false;
					g_busy = true;
					g_lastReason = reason;
					++g_runs;
				}
				if (settings::general::enabled) { DoResets(reason, force); }
				else { logger::debug("reset requested ({}) but bEnabled is off", reason); }
				{
					std::scoped_lock l(g_wakeLock);
					g_busy = false;
				}
				g_wakeCv.notify_all();
			}
		}
	}

	void Install()
	{
		HMODULE module = LoadLibraryW(L"XAudio2_7.dll");
		if (!module)
		{
			g_installResult = std::format("XAudio2_7.dll could not be loaded (error {}); nothing hooked", GetLastError());
			logger::error("{}", g_installResult);
			return;
		}
		HMODULE pinned = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"XAudio2_7.dll", &pinned);  // its vtable must never move

		// Read the shared vtable from a throwaway, uninitialised engine on a helper thread, so this plugin never
		// chooses the COM apartment of the game's main thread.
		void** vtable = nullptr;
		HRESULT created = E_FAIL;
		std::thread([&]() {
			const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			IXAudio2* probe = nullptr;
			created = CoCreateInstance(CLSID_XAudio2, nullptr, CLSCTX_INPROC_SERVER, IID_IXAudio2, reinterpret_cast<void**>(&probe));
			if (SUCCEEDED(created) && probe)
			{
				vtable = *reinterpret_cast<void***>(probe);
				probe->Release();
			}
			if (SUCCEEDED(co)) { CoUninitialize(); }
		}).join();

		if (FAILED(created) || !vtable)
		{
			g_installResult = std::format("the XAudio2 2.7 engine could not be created ({}); nothing hooked", Hr(created));
			logger::error("{}", g_installResult);
			return;
		}

		MEMORY_BASIC_INFORMATION info{};
		if (!VirtualQuery(vtable, &info, sizeof(info)) || info.AllocationBase != static_cast<void*>(module))
		{
			g_installResult = std::format("the IXAudio2 vtable {} is not inside XAudio2_7.dll {}; nothing hooked (another layer wraps it)", Ptr(vtable), Ptr(module));
			logger::error("{}", g_installResult);
			return;
		}

		bool ok = true;
		ok &= PatchSlot(vtable, slot::kCreateMasteringVoice, reinterpret_cast<void*>(&HookCreateMastering), reinterpret_cast<void**>(&g_origMaster), "IXAudio2::CreateMasteringVoice");
		ok &= PatchSlot(vtable, slot::kCreateSubmixVoice, reinterpret_cast<void*>(&HookCreateSubmix), reinterpret_cast<void**>(&g_origSubmix), "IXAudio2::CreateSubmixVoice");
		ok &= PatchSlot(vtable, slot::kCreateSourceVoice, reinterpret_cast<void*>(&HookCreateSource), reinterpret_cast<void**>(&g_origSource), "IXAudio2::CreateSourceVoice");
		ok &= PatchSlot(vtable, slot::kRelease, reinterpret_cast<void*>(&HookRelease), reinterpret_cast<void**>(&g_origRelease), "IXAudio2::Release");
		ok &= PatchSlot(vtable, slot::kInitialize, reinterpret_cast<void*>(&HookInitialize), reinterpret_cast<void**>(&g_origInitialize), "IXAudio2::Initialize");
		ok &= PatchSlot(vtable, slot::kGetDeviceCount, reinterpret_cast<void*>(&HookGetDeviceCount), reinterpret_cast<void**>(&g_origDeviceCount), "IXAudio2::GetDeviceCount");
		g_hooked = ok && g_origMaster && g_origSubmix && g_origSource && g_origRelease && g_origInitialize && g_origDeviceCount;
		g_installResult = g_hooked ? std::format("hooked (XAudio2_7.dll at {}, vtable {})", Ptr(module), Ptr(vtable)) :
		                             std::string("one or more vtable slots could not be hooked");
		logger::info("install: {}", g_installResult);
	}

	void StartWatcher()
	{
		if (!g_hooked)
		{
			logger::warn("not starting the worker: the XAudio2 hooks are not installed");
			return;
		}
		std::thread(WorkerMain).detach();
	}

	void RequestReset(std::string_view a_reason, bool a_force)
	{
		{
			std::scoped_lock l(g_wakeLock);
			if (g_pendingReason.size() < 1024)
			{
				if (!g_pendingReason.empty()) { g_pendingReason += "; "; }
				g_pendingReason += a_reason;
			}
			g_pending = true;
			g_force = g_force || a_force;
			g_lastEvent = std::chrono::steady_clock::now();
			++g_requests;
		}
		g_wakeCv.notify_all();
	}

	bool WaitIdle(int a_timeoutMs)
	{
		std::unique_lock l(g_wakeLock);
		return g_wakeCv.wait_for(l, std::chrono::milliseconds(a_timeoutMs), [] { return !g_pending && !g_busy; });
	}

	std::string StateJson()
	{
		std::string engines;
		{
			std::scoped_lock lock(g_lock);
			for (const auto& e : g_engines)
			{
				if (!engines.empty()) { engines += ","; }
				engines += std::format(
					"{{\"engine\":\"{}\",\"device\":\"{}\",\"deviceId\":\"{}\",\"attached\":{},\"master\":\"{}\",\"standIn\":\"{}\","
					"\"channels\":{},\"rate\":{},\"gameIndex\":{},\"passes\":{},\"critical\":{},\"criticalErrors\":{},\"lastCriticalHr\":\"{}\","
					"\"resets\":{},\"lastResult\":\"{}\"}}",
					Ptr(e->engine), EscapeJson(e->deviceName), EscapeJson(e->deviceId), e->master ? "true" : "false", Ptr(e->master), Ptr(e->proxy),
					e->proxyChannels, e->proxyRate, e->gameIndex, e->passes.load(), e->critical.load() ? "true" : "false", e->criticalCount.load(),
					Hr(e->lastCriticalHr.load()), e->resets, EscapeJson(e->lastResult));
			}
		}
		std::scoped_lock l(g_wakeLock);
		return std::format(
			"\"hooks\":{{\"installed\":{},\"result\":\"{}\",\"destroyHooked\":{},\"redirectedSources\":{},\"redirectedSubmixes\":{},\"initializeCalls\":{},\"deviceCountCalls\":{}}},"
			"\"worker\":{{\"result\":\"{}\",\"pending\":{},\"busy\":{},\"requests\":{},\"runs\":{},\"lastReason\":\"{}\"}},\"engines\":[{}]",
			g_hooked.load() ? "true" : "false", EscapeJson(g_installResult), g_destroyHooked.load() ? "true" : "false", g_redirectedSources.load(),
			g_redirectedSubmixes.load(), g_initializeCalls.load(), g_deviceCountCalls.load(), EscapeJson(g_watcherResult), g_pending ? "true" : "false", g_busy ? "true" : "false", g_requests, g_runs,
			EscapeJson(g_lastReason), engines);
	}

	std::string DevicesJson()
	{
		std::string xa;
		{
			std::scoped_lock lock(g_lock);
			if (!g_engines.empty())
			{
				HRESULT hr = S_OK;
				for (const auto& d : ListDevices(g_engines.front()->engine, hr))
				{
					if (!xa.empty()) { xa += ","; }
					xa += std::format("{{\"index\":{},\"name\":\"{}\",\"id\":\"{}\",\"role\":{},\"channels\":{},\"rate\":{}}}",
						d.index, EscapeJson(d.name), EscapeJson(d.id), d.role, d.channels, d.rate);
				}
			}
		}

		std::string endpoints;
		const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		IMMDeviceEnumerator* enumerator = nullptr;
		if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator))) && enumerator)
		{
			std::string defaultId;
			IMMDevice* def = nullptr;
			if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &def)) && def)
			{
				LPWSTR id = nullptr;
				if (SUCCEEDED(def->GetId(&id)) && id)
				{
					defaultId = Narrow(id);
					CoTaskMemFree(id);
				}
				def->Release();
			}
			IMMDeviceCollection* collection = nullptr;
			if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED | DEVICE_STATE_DISABLED, &collection)) && collection)
			{
				UINT count = 0;
				collection->GetCount(&count);
				for (UINT i = 0; i < count; ++i)
				{
					IMMDevice* device = nullptr;
					if (FAILED(collection->Item(i, &device)) || !device) { continue; }
					std::string id, name;
					LPWSTR rawId = nullptr;
					if (SUCCEEDED(device->GetId(&rawId)) && rawId)
					{
						id = Narrow(rawId);
						CoTaskMemFree(rawId);
					}
					DWORD state = 0;
					device->GetState(&state);
					IPropertyStore* store = nullptr;
					if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store)
					{
						PROPVARIANT value;
						PropVariantInit(&value);
						if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR) { name = Narrow(value.pwszVal); }
						PropVariantClear(&value);
						store->Release();
					}
					device->Release();
					if (!endpoints.empty()) { endpoints += ","; }
					endpoints += std::format("{{\"name\":\"{}\",\"id\":\"{}\",\"state\":\"{}\",\"default\":{}}}", EscapeJson(name), EscapeJson(id),
						EndpointStateName(state), id == defaultId ? "true" : "false");
				}
				collection->Release();
			}
			enumerator->Release();
		}
		if (SUCCEEDED(co)) { CoUninitialize(); }
		return std::format("\"xaudio2Devices\":[{}],\"windowsEndpoints\":[{}]", xa, endpoints);
	}

	Status GetStatus()
	{
		static std::mutex cacheLock;
		static Status cached;
		std::unique_lock lock(g_lock, std::try_to_lock);
		std::scoped_lock c(cacheLock);
		if (!lock.owns_lock()) { return cached; }
		Status s;
		s.hooked = g_hooked;
		if (!g_engines.empty())
		{
			const Engine& e = *g_engines.front();
			s.managed = true;
			s.attached = e.master != nullptr;
			s.critical = e.critical.load();
			s.device = e.deviceName;
			s.resets = e.resets;
			s.lastResult = e.lastResult;
		}
		cached = s;
		return s;
	}

	std::vector<std::string> DeviceNames()
	{
		static std::mutex cacheLock;
		static std::vector<std::string> cached;
		static std::chrono::steady_clock::time_point refreshed{};
		std::scoped_lock c(cacheLock);
		const auto now = std::chrono::steady_clock::now();
		if (now - refreshed < std::chrono::seconds(1)) { return cached; }
		std::unique_lock lock(g_lock, std::try_to_lock);
		if (!lock.owns_lock()) { return cached; }
		refreshed = now;
		cached.clear();
		if (!g_engines.empty())
		{
			HRESULT hr = S_OK;
			for (const auto& d : ListDevices(g_engines.front()->engine, hr)) { cached.push_back(d.name); }
		}
		return cached;
	}

	void LogSummary(std::string_view a_when)
	{
		std::scoped_lock lock(g_lock);
		logger::info("{}: hooks {}; {} engine(s) managed", a_when, g_hooked ? "installed" : "NOT installed", g_engines.size());
		if (g_hooked && g_engines.empty())
		{
			if (g_initializeCalls > 0)
			{
				logger::warn("{}: the game started its audio engine but created no output - no output device was usable, so the game has no audio this session", a_when);
			}
			else
			{
				logger::warn("{}: the game has not started an audio engine through the hooks yet", a_when);
			}
		}
		for (const auto& e : g_engines)
		{
			logger::info("  engine {} on \"{}\" ({} ch, {} Hz), {} reset(s), last: {}", Ptr(e->engine), e->deviceName, e->proxyChannels, e->proxyRate, e->resets, e->lastResult);
		}
	}
}
