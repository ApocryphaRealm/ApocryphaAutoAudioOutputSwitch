// Auto Audio Output Switch - GPL-3.0-or-later (2026-09-13).
//
// The device switch rebuilds the game's own audio engine on the game's audio thread. That procedure follows
// Live Audio Output Switching SE by Maarten Harms (MIT; its notice is in THIRD_PARTY_NOTICES.md), which found it
// for Skyrim SE 1.5.97. This file ports it to SE 1.5.97, AE 1.6.1170 and Skyrim 1.7.x through Address Library IDs
// (each function and global confirmed in the 1.5.97 and 1.7.104 code), and adds the preferred device, the settings
// page and the DevBench tool. See AudioSwitch.h.
#include "PCH.h"

#include "AudioSwitch.h"

#include "Settings.h"
#include "XAudio27.h"
#include "utils/Logger.h"

#include <Windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace audioswitch
{
	namespace
	{
		using namespace xa27;

		// ---- game layout (identical on 1.5.97 and 1.7.104 in the disassembly) ----
		constexpr std::uintptr_t kAudioMasterOff = 0x58;   // BSXAudio2Audio: IXAudio2MasteringVoice*
		constexpr std::uintptr_t kSoundVoiceOff = 0x128;   // BSXAudio2GameSound: its source voice
		constexpr std::uintptr_t kMgrMapCapacity = 0x34;   // BSAudioManager sound table: slot count
		constexpr std::uintptr_t kMgrMapEntries = 0x50;    // BSAudioManager sound table: entry array
		constexpr std::uintptr_t kEntryStride = 0x18;      // {key, sound* @+8, next @+0x10}; empty when next == 0
		constexpr std::uintptr_t kVoiceListCount = 0x88;   // global voice list: count; data inline at +8 when flags < 0
		constexpr int kSlotInit = 1;                       // BSXAudio2Audio vtable
		constexpr int kSlotShutdown = 2;

		struct GameAddresses
		{
			std::uintptr_t threadLoop{ 0 };     // BSAudioManagerThread run loop
			std::uintptr_t processSounds{ 0 };  // the per-pass sound processing it calls
			std::uintptr_t setupSound{ 0 };     // create + set up one sound's source voice (rcx = sound) -> bool
			std::uintptr_t voiceListA{ 0 };
			std::uintptr_t voiceListB{ 0 };
			std::uintptr_t audioObject{ 0 };    // global BSXAudio2Audio*
			std::uintptr_t audioVtable{ 0 };
		};
		GameAddresses g_addr;

		std::string Narrow(const wchar_t* a_text)
		{
			if (!a_text || !*a_text) { return {}; }
			const int len = WideCharToMultiByte(CP_UTF8, 0, a_text, -1, nullptr, 0, nullptr, nullptr);
			if (len <= 1) { return {}; }
			std::string out(static_cast<std::size_t>(len - 1), '\0');
			WideCharToMultiByte(CP_UTF8, 0, a_text, -1, out.data(), len, nullptr, nullptr);
			return out;
		}

		std::wstring Widen(const std::string& a_text)
		{
			if (a_text.empty()) { return {}; }
			const int len = MultiByteToWideChar(CP_UTF8, 0, a_text.c_str(), -1, nullptr, 0);
			std::wstring out(static_cast<std::size_t>(len > 0 ? len - 1 : 0), L'\0');
			if (len > 1) { MultiByteToWideChar(CP_UTF8, 0, a_text.c_str(), -1, out.data(), len); }
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

		// ---- SEH-guarded raw access (no C++ objects with destructors in these functions) ----
		struct SehInfo
		{
			DWORD code{ 0 };
			void* at{ nullptr };
		};

		int FillSeh(EXCEPTION_POINTERS* a_ep, SehInfo* a_out)
		{
			if (a_ep && a_ep->ExceptionRecord)
			{
				a_out->code = a_ep->ExceptionRecord->ExceptionCode;
				a_out->at = a_ep->ExceptionRecord->ExceptionAddress;
			}
			return EXCEPTION_EXECUTE_HANDLER;
		}

		void* ReadPtr(void* a_base, std::uintptr_t a_off)
		{
			__try { return *reinterpret_cast<void**>(static_cast<std::uint8_t*>(a_base) + a_off); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
		}

		void WritePtr(void* a_base, std::uintptr_t a_off, void* a_value)
		{
			__try { *reinterpret_cast<void**>(static_cast<std::uint8_t*>(a_base) + a_off) = a_value; }
			__except (EXCEPTION_EXECUTE_HANDLER) {}
		}

		int CallGameSlot(void* a_object, int a_slot, SehInfo* a_seh)
		{
			__try
			{
				auto fn = reinterpret_cast<int (*)(void*)>((*reinterpret_cast<void***>(a_object))[a_slot]);
				return fn(a_object);
			}
			__except (FillSeh(GetExceptionInformation(), a_seh)) { return -1; }
		}

		bool CallSetupSound(std::uintptr_t a_fn, void* a_sound)
		{
			__try { return reinterpret_cast<bool (*)(void*)>(a_fn)(a_sound); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		void DestroyVoiceSafe(void* a_voice)
		{
			__try { static_cast<IXAudio2Voice*>(a_voice)->DestroyVoice(); }
			__except (EXCEPTION_EXECUTE_HANDLER) {}
		}

		// The live game sounds, read from BSAudioManager's sound table. a_out receives up to a_max sound pointers.
		int SnapshotSounds(void* a_mgr, void** a_out, int a_max)
		{
			int n = 0;
			__try
			{
				const std::uint32_t capacity = *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(a_mgr) + kMgrMapCapacity);
				auto* entries = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(a_mgr) + kMgrMapEntries);
				if (!entries || capacity == 0 || capacity > 0x10000) { return 0; }
				for (std::uint32_t i = 0; i < capacity && n < a_max; ++i)
				{
					const std::uint8_t* e = entries + static_cast<std::size_t>(i) * kEntryStride;
					if (*reinterpret_cast<void* const*>(e + 0x10) != nullptr)
					{
						if (void* s = *reinterpret_cast<void* const*>(e + 0x08)) { a_out[n++] = s; }
					}
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { n = 0; }
			return n;
		}

		// Removes sounds from the table: every sound when a_onlyVoiceless is false, otherwise only sounds that have no
		// source voice (so the game's per-sound update never dereferences a missing voice). Returns how many.
		int RemoveSounds(void* a_mgr, bool a_onlyVoiceless)
		{
			int removed = 0;
			__try
			{
				const std::uint32_t capacity = *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(a_mgr) + kMgrMapCapacity);
				auto* entries = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(a_mgr) + kMgrMapEntries);
				if (!entries || capacity == 0 || capacity > 0x10000) { return 0; }
				for (std::uint32_t i = 0; i < capacity; ++i)
				{
					std::uint8_t* e = entries + static_cast<std::size_t>(i) * kEntryStride;
					if (*reinterpret_cast<void**>(e + 0x10) == nullptr) { continue; }
					void* s = *reinterpret_cast<void**>(e + 0x08);
					const bool voiceless = !s || *reinterpret_cast<void**>(static_cast<std::uint8_t*>(s) + kSoundVoiceOff) == nullptr;
					if (!a_onlyVoiceless || voiceless)
					{
						*reinterpret_cast<void**>(e + 0x08) = nullptr;
						*reinterpret_cast<void**>(e + 0x10) = nullptr;
						++removed;
					}
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {}
			return removed;
		}

		// A voice whose private implementation has no vtable was already freed; the game's shutdown would crash on it.
		bool VoiceIsFreed(void* a_voice)
		{
			__try
			{
				void* impl = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(a_voice) + 0x10);
				return !impl || *reinterpret_cast<void**>(impl) == nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return true; }
		}

		// Nulls entries of a global voice list whose voice was already freed, so the game's shutdown skips them.
		int ScrubVoiceList(std::uintptr_t a_head)
		{
			int scrubbed = 0;
			__try
			{
				auto* head = reinterpret_cast<std::uint8_t*>(a_head);
				const std::int32_t flags = *reinterpret_cast<std::int32_t*>(head);
				const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(head + kVoiceListCount);
				if (count == 0 || count > 0x4000) { return 0; }
				auto* data = flags < 0 ? head + 8 : *reinterpret_cast<std::uint8_t**>(head + 8);
				if (!data) { return 0; }
				for (std::uint32_t i = 0; i < count; ++i)
				{
					std::uint8_t* entry = data + static_cast<std::size_t>(i) * 0x10;
					void* voice = *reinterpret_cast<void**>(entry + 8);
					if (voice && VoiceIsFreed(voice))
					{
						*reinterpret_cast<void**>(entry) = nullptr;
						*reinterpret_cast<void**>(entry + 8) = nullptr;
						++scrubbed;
					}
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {}
			return scrubbed;
		}

		// ---- state ----
		struct State
		{
			void* audio{ nullptr };             // BSXAudio2Audio
			IXAudio2* engine{ nullptr };
			IXAudio2Voice* master{ nullptr };
			std::string deviceId;               // lower-case endpoint id
			std::string deviceName;
			UINT32 channels{ 0 };
			UINT32 rate{ 0 };
			std::uint32_t switches{ 0 };
			std::uint32_t failures{ 0 };
			std::string lastResult{ "waiting for the game's audio engine" };
		};

		std::mutex g_stateLock;
		State g_state;
		std::atomic<bool> g_critical{ false };
		std::atomic<std::uint32_t> g_criticalCount{ 0 };
		std::atomic<std::uint64_t> g_passes{ 0 };
		thread_local int t_inside = 0;

		struct Inside
		{
			Inside() { ++t_inside; }
			~Inside() { --t_inside; }
		};

		CreateMasteringFn g_origMaster{ nullptr };
		InitializeFn g_origInitialize{ nullptr };
		GetDeviceCountFn g_origDeviceCount{ nullptr };
		using ProcessSoundsFn = void (*)(void*);
		ProcessSoundsFn g_origProcessSounds{ nullptr };
		std::atomic<bool> g_hooked{ false };
		std::atomic<bool> g_threadHooked{ false };
		std::atomic<std::uint32_t> g_initializeCalls{ 0 };
		std::atomic<std::uint32_t> g_deviceCountCalls{ 0 };
		std::string g_installResult{ "not run" };
		std::string g_threadHookResult{ "not run" };

		// swap handoff: the worker decides and waits; the audio thread performs
		std::mutex g_swapLock;
		std::condition_variable g_swapCv;
		std::atomic<bool> g_swapPending{ false };
		bool g_swapDone{ false };
		std::string g_swapTargetId;  // lower-case endpoint id the new mastering voice must use (guarded by g_swapLock)

		void SetResult(std::string a_text, bool a_failure = false)
		{
			std::scoped_lock l(g_stateLock);
			g_state.lastResult = std::move(a_text);
			if (a_failure) { ++g_state.failures; }
		}

		std::string LastResult()
		{
			std::scoped_lock l(g_stateLock);
			return g_state.lastResult;
		}

		// ---- XAudio2 device list (only called with an engine the caller knows is alive) ----
		struct Device
		{
			UINT32 index{ 0 };
			std::string id;  // lower-case
			std::string name;
			UINT32 role{ 0 };
		};

		std::vector<Device> ListXaDevices(IXAudio2* a_engine)
		{
			std::vector<Device> out;
			UINT32 count = 0;
			HRESULT hr;
			{
				Inside inside;
				hr = a_engine->GetDeviceCount(&count);
			}
			if (FAILED(hr)) { return out; }
			for (UINT32 i = 0; i < count; ++i)
			{
				DeviceDetails details{};
				if (FAILED(a_engine->GetDeviceDetails(i, &details))) { continue; }
				details.DeviceID[255] = L'\0';
				details.DisplayName[255] = L'\0';
				out.push_back({ i, Lower(Narrow(details.DeviceID)), Narrow(details.DisplayName), details.Role });
			}
			return out;
		}

		// ---- Windows endpoints (safe whether or not the engine is alive) ----
		struct Endpoint
		{
			std::string id;  // lower-case
			std::string name;
			DWORD state{ 0 };
			bool isDefault{ false };
		};

		std::vector<Endpoint> ListEndpoints(bool a_activeOnly)
		{
			std::vector<Endpoint> out;
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
						defaultId = Lower(Narrow(id));
						CoTaskMemFree(id);
					}
					def->Release();
				}
				IMMDeviceCollection* collection = nullptr;
				const DWORD mask = a_activeOnly ? DEVICE_STATE_ACTIVE : (DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED | DEVICE_STATE_DISABLED);
				if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, mask, &collection)) && collection)
				{
					UINT count = 0;
					collection->GetCount(&count);
					for (UINT i = 0; i < count; ++i)
					{
						IMMDevice* device = nullptr;
						if (FAILED(collection->Item(i, &device)) || !device) { continue; }
						Endpoint ep;
						LPWSTR rawId = nullptr;
						if (SUCCEEDED(device->GetId(&rawId)) && rawId)
						{
							ep.id = Lower(Narrow(rawId));
							CoTaskMemFree(rawId);
						}
						device->GetState(&ep.state);
						IPropertyStore* store = nullptr;
						if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store)
						{
							PROPVARIANT value;
							PropVariantInit(&value);
							if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR) { ep.name = Narrow(value.pwszVal); }
							PropVariantClear(&value);
							store->Release();
						}
						device->Release();
						ep.isDefault = !defaultId.empty() && ep.id == defaultId;
						out.push_back(std::move(ep));
					}
					collection->Release();
				}
				enumerator->Release();
			}
			if (SUCCEEDED(co)) { CoUninitialize(); }
			return out;
		}

		// sPreferredDevice: a full endpoint id, or part of a device name (case-insensitive). Empty = none.
		const Endpoint* PreferredEndpoint(const std::vector<Endpoint>& a_list)
		{
			const std::string pref = Lower(Trim(settings::GetPreferredDevice()));
			if (pref.empty()) { return nullptr; }
			for (const auto& e : a_list) { if (e.id == pref) { return &e; } }
			for (const auto& e : a_list) { if (Lower(e.name).find(pref) != std::string::npos) { return &e; } }
			return nullptr;
		}

		// A newly connected endpoint (Bluetooth especially) can report ACTIVE before its stream takes buffers; an
		// XAudio2 2.7 mastering voice created then crashes the mixer. A shared-mode WASAPI render probe proves it is
		// ready and warms it up. (Technique from Live Audio Output Switching SE, MIT.)
		bool EndpointReady(const std::string& a_id)
		{
			if (a_id.empty()) { return true; }
			const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			bool ok = false;
			IMMDeviceEnumerator* enumerator = nullptr;
			if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator))) && enumerator)
			{
				IMMDevice* device = nullptr;
				const std::wstring wid = Widen(a_id);
				if (SUCCEEDED(enumerator->GetDevice(wid.c_str(), &device)) && device)
				{
					DWORD state = 0;
					if (SUCCEEDED(device->GetState(&state)) && state == DEVICE_STATE_ACTIVE)
					{
						IAudioClient* client = nullptr;
						if (SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client))) && client)
						{
							WAVEFORMATEX* format = nullptr;
							if (SUCCEEDED(client->GetMixFormat(&format)) && format)
							{
								if (SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, format, nullptr)))
								{
									IAudioRenderClient* render = nullptr;
									if (SUCCEEDED(client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render))) && render)
									{
										BYTE* buffer = nullptr;
										if (SUCCEEDED(render->GetBuffer(64, &buffer)) && buffer)
										{
											render->ReleaseBuffer(0, 0);
											ok = true;
										}
										render->Release();
									}
								}
								CoTaskMemFree(format);
							}
							client->Release();
						}
					}
					device->Release();
				}
				enumerator->Release();
			}
			else
			{
				ok = true;  // cannot probe: do not block the switch
			}
			if (SUCCEEDED(co)) { CoUninitialize(); }
			return ok;
		}

		// ---- engine callback: an unplugged device kills the engine; turn that into a rebuild ----
		class EngineCallback final : public IXAudio2EngineCallback
		{
		public:
			void STDMETHODCALLTYPE OnProcessingPassStart() override {}
			void STDMETHODCALLTYPE OnProcessingPassEnd() override { g_passes.fetch_add(1, std::memory_order_relaxed); }
			void STDMETHODCALLTYPE OnCriticalError(HRESULT) override
			{
				g_criticalCount.fetch_add(1);
				g_critical = true;
				RequestReset("the audio engine lost its device", false, true);
			}
		};
		EngineCallback g_callback;

		bool IsGameAudioObject(void* a_candidate)
		{
			return a_candidate && g_addr.audioVtable && ReadPtr(a_candidate, 0) == reinterpret_cast<void*>(g_addr.audioVtable);
		}

		void* GameAudioObject()
		{
			if (!g_addr.audioObject) { return nullptr; }
			void* object = ReadPtr(reinterpret_cast<void*>(g_addr.audioObject), 0);
			return IsGameAudioObject(object) ? object : nullptr;
		}

		// ---- hooks ----
		HRESULT STDMETHODCALLTYPE HookInitialize(IXAudio2* a_this, UINT32 a_flags, UINT32 a_processor)
		{
			const HRESULT hr = g_origInitialize(a_this, a_flags, a_processor);
			if (t_inside == 0)
			{
				const auto n = g_initializeCalls.fetch_add(1) + 1;
				logger::info("engine {} initialised (flags=0x{:X}): {} - call {}", Ptr(a_this), a_flags, Hr(hr), n);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE HookGetDeviceCount(IXAudio2* a_this, UINT32* a_count)
		{
			const HRESULT hr = g_origDeviceCount(a_this, a_count);
			if (t_inside == 0 && g_deviceCountCalls.fetch_add(1) < 8)
			{
				logger::info("engine {} asked for its device count: {} device(s) ({})", Ptr(a_this), a_count ? *a_count : 0u, Hr(hr));
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE HookCreateMastering(IXAudio2* a_this, IXAudio2Voice** a_out, UINT32 a_channels, UINT32 a_rate, UINT32 a_flags, UINT32 a_index, void* a_chain)
		{
			// Only the game's own call is managed: it passes &BSXAudio2Audio::masteringVoice (+0x58) as the out pointer.
			void* object = a_out ? static_cast<void*>(reinterpret_cast<std::uint8_t*>(a_out) - kAudioMasterOff) : nullptr;
			if (t_inside > 0 || !IsGameAudioObject(object))
			{
				return g_origMaster(a_this, a_out, a_channels, a_rate, a_flags, a_index, a_chain);
			}

			const auto devices = ListXaDevices(a_this);
			logger::debug("the game creates its mastering voice: channels={} rate={} device index={}; XAudio2 lists {} device(s)", a_channels, a_rate, a_index, devices.size());
			for (const auto& d : devices) { logger::debug("  [{}] \"{}\" role=0x{:X} id={}", d.index, d.name, d.role, d.id); }

			std::string target;
			{
				std::scoped_lock l(g_swapLock);
				target = g_swapTargetId;
			}
			UINT32 pick = a_index;
			std::string why = "the game's choice (the Windows default device)";
			if (settings::general::enabled)
			{
				const Device* chosen = nullptr;
				if (!target.empty())
				{
					for (const auto& d : devices) { if (d.id == target) { chosen = &d; } }
					if (chosen) { why = "the switch target"; }
				}
				if (!chosen)
				{
					const std::string pref = Lower(Trim(settings::GetPreferredDevice()));
					if (!pref.empty())
					{
						for (const auto& d : devices) { if (!chosen && (d.id == pref || Lower(d.name).find(pref) != std::string::npos)) { chosen = &d; } }
						if (chosen) { why = "the preferred device"; }
					}
				}
				if (chosen) { pick = chosen->index; }
			}

			HRESULT hr = g_origMaster(a_this, a_out, a_channels, a_rate, a_flags, pick, a_chain);
			if (FAILED(hr) && pick != a_index)
			{
				logger::warn("device index {} refused a mastering voice ({}); using the game's choice", pick, Hr(hr));
				pick = a_index;
				why = "the game's choice (the chosen device refused)";
				hr = g_origMaster(a_this, a_out, a_channels, a_rate, a_flags, pick, a_chain);
			}
			if (FAILED(hr) || !*a_out)
			{
				logger::error("the game's mastering voice could not be created ({}): no usable output device", Hr(hr));
				std::scoped_lock l(g_stateLock);
				g_state.audio = object;
				g_state.engine = a_this;
				g_state.master = nullptr;
				g_state.deviceId.clear();
				g_state.deviceName.clear();
				return hr;
			}

			const HRESULT cb = a_this->RegisterForCallbacks(&g_callback);
			g_critical = false;
			std::string name, id;
			for (const auto& d : devices)
			{
				if (d.index == pick)
				{
					name = d.name;
					id = d.id;
				}
			}
			{
				std::scoped_lock l(g_stateLock);
				g_state.audio = object;
				g_state.engine = a_this;
				g_state.master = *a_out;
				g_state.deviceId = id;
				g_state.deviceName = name;
				g_state.channels = a_channels;
				g_state.rate = a_rate;
			}
			logger::info("the game's output is on [{}] \"{}\" ({}); engine {}, mastering voice {}, device-loss callback {}", pick, name, why, Ptr(a_this), Ptr(*a_out), Hr(cb));
			return hr;
		}

		// ---- the switch, on the game's audio thread ----
		void FinishSwap()
		{
			{
				std::scoped_lock l(g_swapLock);
				g_swapDone = true;
				g_swapTargetId.clear();
			}
			g_swapCv.notify_all();
		}

		void FailSwap(std::string a_text)
		{
			logger::error("{}", a_text);
			SetResult(std::move(a_text), true);
			FinishSwap();
		}

		void PerformSwap()
		{
			const auto start = std::chrono::steady_clock::now();
			void* audio = GameAudioObject();
			auto* mgr = RE::BSAudioManager::GetSingleton();
			if (!audio || !mgr)
			{
				FailSwap(std::format("switch skipped: the game's audio object {} or manager {} is not available", Ptr(audio), Ptr(mgr)));
				return;
			}
			std::string from;
			{
				std::scoped_lock l(g_stateLock);
				from = g_state.deviceName.empty() ? std::string("(no device)") : g_state.deviceName;
			}

			// 1. every live sound drops its source voice (destroyed while the engine still exists); the sound stays
			static void* sounds[2048];
			const int soundCount = SnapshotSounds(mgr, sounds, 2048);
			int detached = 0;
			for (int i = 0; i < soundCount; ++i)
			{
				if (void* voice = ReadPtr(sounds[i], kSoundVoiceOff))
				{
					DestroyVoiceSafe(voice);
					WritePtr(sounds[i], kSoundVoiceOff, nullptr);
					++detached;
				}
			}

			// 2. already-freed voices left in the game's own voice lists would crash its shutdown
			const int scrubbed = ScrubVoiceList(g_addr.voiceListA) + ScrubVoiceList(g_addr.voiceListB);

			// 3. the game's own shutdown and init; init creates the mastering voice through our hook on the target
			SehInfo seh{};
			CallGameSlot(audio, kSlotShutdown, &seh);
			{
				std::scoped_lock l(g_stateLock);
				g_state.engine = nullptr;
				g_state.master = nullptr;
			}
			if (seh.code)
			{
				RemoveSounds(mgr, false);
				FailSwap(std::format("switch failed: the game's audio shutdown faulted (0x{:08X}); sounds were dropped", seh.code));
				return;
			}
			const int initResult = CallGameSlot(audio, kSlotInit, &seh);
			if (seh.code)
			{
				RemoveSounds(mgr, false);
				FailSwap(std::format("switch failed: the game's audio init faulted (0x{:08X}); sounds were dropped", seh.code));
				return;
			}
			if (!ReadPtr(audio, kAudioMasterOff))
			{
				const int dropped = RemoveSounds(mgr, false);
				FailSwap(std::format("no device accepted the rebuilt audio (was \"{}\"); {} sound(s) dropped; waiting for a device", from, dropped));
				return;
			}

			// 4. the game's own per-sound voice setup rebuilds every surviving sound on the new engine
			int revived = 0;
			int failed = 0;
			for (int i = 0; i < soundCount; ++i)
			{
				if (ReadPtr(sounds[i], kSoundVoiceOff)) { ++revived; continue; }
				if (CallSetupSound(g_addr.setupSound, sounds[i]) && ReadPtr(sounds[i], kSoundVoiceOff)) { ++revived; }
				else { ++failed; }
			}
			const int removed = RemoveSounds(mgr, true);
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
			{
				std::scoped_lock l(g_stateLock);
				++g_state.switches;
				g_state.lastResult = std::format("switched \"{}\" -> \"{}\" in {} ms: {} sound(s) revived, {} failed, {} removed; {} voice(s) detached, {} stale list entr{} cleared, init {}",
					from, g_state.deviceName, ms, revived, failed, removed, detached, scrubbed, scrubbed == 1 ? "y" : "ies", initResult);
				logger::info("{}", g_state.lastResult);
			}
			FinishSwap();
		}

		void HookProcessSounds(void* a_manager)
		{
			if (g_swapPending.exchange(false)) { PerformSwap(); }
			g_origProcessSounds(a_manager);
		}

		bool PatchSlot(void** a_vtable, std::size_t a_slot, void* a_hook, void** a_original, const char* a_name)
		{
			void** entry = a_vtable + a_slot;
			if (*entry == a_hook) { return true; }
			DWORD old = 0;
			if (!VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &old))
			{
				logger::error("could not unprotect the vtable slot for {} (error {})", a_name, GetLastError());
				return false;
			}
			*a_original = *entry;
			*entry = a_hook;
			VirtualProtect(entry, sizeof(void*), old, &old);
			logger::info("hooked {} (vtable slot {}, original {})", a_name, a_slot, Ptr(*a_original));
			return true;
		}

		bool InstallThreadHook()
		{
			// Find the call to the per-pass sound processing inside the audio thread's run loop by its target, so the
			// instruction offset may differ between runtimes (+0x57 on 1.5.97, +0x58 on 1.7.104).
			const auto* loop = reinterpret_cast<const std::uint8_t*>(g_addr.threadLoop);
			std::uintptr_t site = 0;
			for (std::uintptr_t off = 0; off < 0x100 && !site; ++off)
			{
				if (loop[off] != 0xE8) { continue; }
				std::int32_t rel = 0;
				std::memcpy(&rel, loop + off + 1, sizeof(rel));
				if (g_addr.threadLoop + off + 5 + static_cast<std::intptr_t>(rel) == g_addr.processSounds) { site = g_addr.threadLoop + off; }
			}
			if (!site)
			{
				g_threadHookResult = "the call to the sound processing was not found in the audio thread loop; switching disabled";
				logger::error("{}", g_threadHookResult);
				return false;
			}
			SKSE::AllocTrampoline(14);
			auto& trampoline = SKSE::GetTrampoline();
			g_origProcessSounds = reinterpret_cast<ProcessSoundsFn>(trampoline.write_call<5>(site, reinterpret_cast<std::uintptr_t>(&HookProcessSounds)));
			g_threadHookResult = std::format("hooked the audio thread's sound-processing call at +0x{:X}", site - g_addr.threadLoop);
			logger::info("{}", g_threadHookResult);
			return g_origProcessSounds != nullptr;
		}

		// ---- the decision, on the worker thread ----
		std::mutex g_wakeLock;  // guards the fields below
		std::condition_variable g_wakeCv;
		bool g_pending{ false };
		bool g_force{ false };
		bool g_urgent{ false };
		bool g_busy{ false };
		std::string g_avoidId;
		std::string g_pendingReason;
		std::string g_lastReason{ "none" };
		std::string g_watcherResult{ "not started" };
		std::chrono::steady_clock::time_point g_lastEvent{};
		std::uint32_t g_requests{ 0 };
		std::uint32_t g_runs{ 0 };

		void Evaluate(const std::string& a_reason, bool a_force, const std::string& a_avoidId)
		{
			State snap;
			{
				std::scoped_lock l(g_stateLock);
				snap = g_state;
			}
			if (!g_threadHooked)
			{
				SetResult("switching is disabled: the audio thread could not be hooked", true);
				return;
			}
			if (!GameAudioObject())
			{
				logger::debug("switch check ({}) before the game created its audio", a_reason);
				return;
			}

			auto active = ListEndpoints(true);
			if (!a_avoidId.empty())
			{
				const std::string avoid = Lower(a_avoidId);
				std::erase_if(active, [&](const Endpoint& e) { return e.id == avoid; });
			}
			const Endpoint* preferred = PreferredEndpoint(active);
			const Endpoint* def = nullptr;
			for (const auto& e : active) { if (e.isDefault) { def = &e; } }
			const Endpoint* target = preferred ? preferred : (def ? def : (active.empty() ? nullptr : &active.front()));
			const bool currentActive = std::any_of(active.begin(), active.end(), [&](const Endpoint& e) { return e.id == snap.deviceId; });
			const bool critical = g_critical.load();

			bool swap = false;
			std::string decision;
			if (!target) { decision = "no output device is connected; waiting for one"; }
			else if (a_force) { swap = true; decision = "forced"; }
			else if (critical) { swap = true; decision = "the engine lost its device"; }
			else if (!snap.master) { swap = true; decision = "the game has no output yet"; }
			else if (!currentActive) { swap = true; decision = "the current device is gone"; }
			else if (preferred) { swap = snap.deviceId != preferred->id; decision = swap ? "the preferred device is available" : "already on the preferred device"; }
			else if (settings::general::switchOnDefaultChange) { swap = snap.deviceId != target->id; decision = swap ? "the Windows default device changed" : "already on the Windows default device"; }
			else { decision = "the current device is still connected and following the Windows default is off"; }

			logger::info("switch check ({}): on \"{}\", target \"{}\", critical={}: {}", a_reason, snap.deviceName.empty() ? "(none)" : snap.deviceName,
						 target ? target->name : "(none)", critical, decision);
			if (!swap)
			{
				SetResult(decision);
				return;
			}

			bool ready = false;
			for (int attempt = 0; attempt < 10 && !ready; ++attempt)
			{
				ready = EndpointReady(target->id);
				if (!ready)
				{
					logger::info("\"{}\" is not taking audio yet (attempt {}); waiting", target->name, attempt + 1);
					std::this_thread::sleep_for(std::chrono::seconds(1));
				}
			}
			if (!ready)
			{
				SetResult(std::format("\"{}\" never started taking audio; no switch", target->name), true);
				return;
			}

			bool timedOut = false;
			{
				std::unique_lock l(g_swapLock);
				g_swapTargetId = target->id;
				g_swapDone = false;
				g_swapPending = true;
				if (!g_swapCv.wait_for(l, std::chrono::seconds(10), [] { return g_swapDone; }))
				{
					g_swapPending = false;
					g_swapTargetId.clear();
					timedOut = true;
				}
			}
			if (timedOut)
			{
				SetResult("the game's audio thread did not run the switch within 10 s", true);
				logger::warn("the game's audio thread did not run the switch within 10 s");
			}
		}

		class NotificationClient final : public IMMNotificationClient
		{
		public:
			ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
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
				const std::string id = Lower(Narrow(a_id));
				bool leaving = false;
				{
					std::scoped_lock l(g_stateLock);
					leaving = a_state != DEVICE_STATE_ACTIVE && !g_state.deviceId.empty() && g_state.deviceId == id;
				}
				RequestReset(std::format("endpoint {} is now {}", id, EndpointStateName(a_state)), false, leaving, leaving ? id : std::string());
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR a_id) override
			{
				RequestReset(std::format("endpoint {} added", Lower(Narrow(a_id))), false);
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR a_id) override
			{
				const std::string id = Lower(Narrow(a_id));
				bool leaving = false;
				{
					std::scoped_lock l(g_stateLock);
					leaving = !g_state.deviceId.empty() && g_state.deviceId == id;
				}
				RequestReset(std::format("endpoint {} removed", id), false, leaving, leaving ? id : std::string());
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow a_flow, ERole a_role, LPCWSTR a_id) override
			{
				if (a_flow == eRender && a_role == eConsole)
				{
					RequestReset(std::format("the Windows default output changed to {}", a_id ? Lower(Narrow(a_id)) : std::string("(none)")), false);
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
			std::string watcher = SUCCEEDED(hr) ? std::string("watching Windows audio endpoints") :
			                                      std::format("endpoint notifications unavailable ({}); only device loss and the tool trigger switches", Hr(hr));
			{
				std::scoped_lock l(g_wakeLock);
				g_watcherResult = watcher;
			}
			logger::info("worker started (COM {}): {}", Hr(co), watcher);

			for (;;)
			{
				std::string reason;
				std::string avoid;
				bool force = false;
				{
					std::unique_lock l(g_wakeLock);
					g_wakeCv.wait(l, [] { return g_pending; });
					while (!g_urgent)
					{
						const auto due = g_lastEvent + std::chrono::milliseconds(settings::general::resetDelayMs.load());
						if (std::chrono::steady_clock::now() >= due) { break; }
						g_wakeCv.wait_until(l, due);
					}
					reason = std::move(g_pendingReason);
					avoid = std::move(g_avoidId);
					force = g_force;
					g_pendingReason.clear();
					g_avoidId.clear();
					g_pending = false;
					g_force = false;
					g_urgent = false;
					g_busy = true;
					g_lastReason = reason;
					++g_runs;
				}
				if (settings::general::enabled) { Evaluate(reason, force, avoid); }
				else { logger::debug("switch check ({}) skipped: bEnabled is off", reason); }
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
		g_addr.threadLoop = REL::RelocationID(66482, 67746).address();
		g_addr.processSounds = REL::RelocationID(66461, 67725).address();
		g_addr.setupSound = REL::RelocationID(66762, 68003).address();
		g_addr.voiceListA = REL::RelocationID(511864, 388390).address();
		g_addr.voiceListB = REL::RelocationID(511867, 388393).address();
		g_addr.audioObject = REL::RelocationID(523613, 410149).address();
		g_addr.audioVtable = REL::RelocationID(285056, 236527).address();
		logger::info("game addresses: thread loop {:X}, sound processing {:X}, sound setup {:X}, voice lists {:X}/{:X}, audio object {:X}, audio vtable {:X}",
					 g_addr.threadLoop, g_addr.processSounds, g_addr.setupSound, g_addr.voiceListA, g_addr.voiceListB, g_addr.audioObject, g_addr.audioVtable);

		HMODULE module = LoadLibraryW(L"XAudio2_7.dll");
		if (!module)
		{
			g_installResult = std::format("XAudio2_7.dll could not be loaded (error {}); nothing hooked", GetLastError());
			logger::error("{}", g_installResult);
			return;
		}
		HMODULE pinned = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"XAudio2_7.dll", &pinned);  // the game frees it on every rebuild

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
			g_installResult = std::format("the IXAudio2 vtable {} is not inside XAudio2_7.dll; nothing hooked", Ptr(vtable));
			logger::error("{}", g_installResult);
			return;
		}

		bool ok = true;
		ok &= PatchSlot(vtable, slot::kCreateMasteringVoice, reinterpret_cast<void*>(&HookCreateMastering), reinterpret_cast<void**>(&g_origMaster), "IXAudio2::CreateMasteringVoice");
		ok &= PatchSlot(vtable, slot::kInitialize, reinterpret_cast<void*>(&HookInitialize), reinterpret_cast<void**>(&g_origInitialize), "IXAudio2::Initialize");
		ok &= PatchSlot(vtable, slot::kGetDeviceCount, reinterpret_cast<void*>(&HookGetDeviceCount), reinterpret_cast<void**>(&g_origDeviceCount), "IXAudio2::GetDeviceCount");
		g_hooked = ok && g_origMaster && g_origInitialize && g_origDeviceCount;
		g_threadHooked = g_hooked && InstallThreadHook();
		g_installResult = g_hooked ? std::format("hooked (XAudio2_7.dll at {}, vtable {})", Ptr(module), Ptr(vtable)) : std::string("one or more vtable slots could not be hooked");
		logger::info("install: {}; {}", g_installResult, g_threadHookResult);
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

	void RequestReset(std::string_view a_reason, bool a_force, bool a_urgent, std::string_view a_avoidId)
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
			g_urgent = g_urgent || a_urgent;
			if (!a_avoidId.empty()) { g_avoidId = std::string(a_avoidId); }
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

	Status GetStatus()
	{
		Status s;
		s.hooked = g_hooked && g_threadHooked;
		std::scoped_lock l(g_stateLock);
		s.managed = g_state.audio != nullptr;
		s.attached = g_state.master != nullptr;
		s.critical = g_critical.load();
		s.device = g_state.deviceName;
		s.resets = g_state.switches;
		s.lastResult = g_state.lastResult;
		return s;
	}

	std::vector<std::string> DeviceNames()
	{
		static std::mutex cacheLock;
		static std::vector<std::string> cached;
		static std::chrono::steady_clock::time_point refreshed{};
		std::scoped_lock c(cacheLock);
		const auto now = std::chrono::steady_clock::now();
		if (now - refreshed >= std::chrono::seconds(2))
		{
			refreshed = now;
			cached.clear();
			for (const auto& e : ListEndpoints(true)) { cached.push_back(e.name); }
		}
		return cached;
	}

	std::string StateJson()
	{
		State snap;
		{
			std::scoped_lock l(g_stateLock);
			snap = g_state;
		}
		std::scoped_lock l(g_wakeLock);
		return std::format(
			"\"hooks\":{{\"installed\":{},\"result\":\"{}\",\"audioThread\":{},\"audioThreadResult\":\"{}\",\"initializeCalls\":{},\"deviceCountCalls\":{}}},"
			"\"worker\":{{\"result\":\"{}\",\"pending\":{},\"busy\":{},\"swapPending\":{},\"requests\":{},\"runs\":{},\"lastReason\":\"{}\"}},"
			"\"engines\":[{{\"audioObject\":\"{}\",\"engine\":\"{}\",\"master\":\"{}\",\"attached\":{},\"device\":\"{}\",\"deviceId\":\"{}\",\"channels\":{},\"rate\":{},"
			"\"passes\":{},\"critical\":{},\"criticalErrors\":{},\"resets\":{},\"failures\":{},\"lastResult\":\"{}\"}}]",
			g_hooked.load() ? "true" : "false", EscapeJson(g_installResult), g_threadHooked.load() ? "true" : "false", EscapeJson(g_threadHookResult),
			g_initializeCalls.load(), g_deviceCountCalls.load(), EscapeJson(g_watcherResult), g_pending ? "true" : "false", g_busy ? "true" : "false",
			g_swapPending.load() ? "true" : "false", g_requests, g_runs, EscapeJson(g_lastReason), Ptr(snap.audio), Ptr(snap.engine), Ptr(snap.master),
			snap.master ? "true" : "false", EscapeJson(snap.deviceName), EscapeJson(snap.deviceId), snap.channels, snap.rate, g_passes.load(),
			g_critical.load() ? "true" : "false", g_criticalCount.load(), snap.switches, snap.failures, EscapeJson(snap.lastResult));
	}

	std::string DevicesJson()
	{
		std::string endpoints;
		for (const auto& e : ListEndpoints(false))
		{
			if (!endpoints.empty()) { endpoints += ","; }
			endpoints += std::format("{{\"name\":\"{}\",\"id\":\"{}\",\"state\":\"{}\",\"default\":{}}}", EscapeJson(e.name), EscapeJson(e.id),
				EndpointStateName(e.state), e.isDefault ? "true" : "false");
		}
		return std::format("\"windowsEndpoints\":[{}]", endpoints);
	}

	void LogSummary(std::string_view a_when)
	{
		State snap;
		{
			std::scoped_lock l(g_stateLock);
			snap = g_state;
		}
		logger::info("{}: hooks {}, audio thread {}; game audio object {}; on \"{}\"; {} switch(es), last: {}", a_when, g_hooked ? "installed" : "NOT installed",
					 g_threadHooked ? "hooked" : "NOT hooked", Ptr(snap.audio), snap.deviceName, snap.switches, snap.lastResult);
		if (g_hooked && !snap.audio)
		{
			logger::warn("{}: the game has not created its audio output through the hooks - no output device was usable when it started", a_when);
		}
	}
}
