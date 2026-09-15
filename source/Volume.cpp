// Auto Audio Output Switch - the PC volume of the device the game plays on. Own code, GPL-3.0-or-later (2026-09-14).
#include "PCH.h"

#include "Volume.h"

#include "AudioSwitch.h"
#include "utils/Logger.h"

#include <Windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <format>
#include <mutex>
#include <string>
#include <thread>

namespace volume
{
	namespace
	{
		std::mutex g_lock;
		Info g_info;  // guarded by g_lock
		std::atomic<float> g_pendingLevel{ -1.0F };  // -1 = nothing requested
		std::atomic<int> g_pendingMute{ -1 };        // -1 = nothing requested, 0 / 1
		std::atomic<bool> g_started{ false };

		std::string Narrow(LPCWSTR a_text)
		{
			if (!a_text) { return {}; }
			const int size = WideCharToMultiByte(CP_UTF8, 0, a_text, -1, nullptr, 0, nullptr, nullptr);
			if (size <= 1) { return {}; }
			std::string out(static_cast<std::size_t>(size - 1), '\0');
			WideCharToMultiByte(CP_UTF8, 0, a_text, -1, out.data(), size, nullptr, nullptr);
			return out;
		}

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
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

		void ReadIdAndName(IMMDevice* a_device, std::string& a_id, std::string& a_name)
		{
			LPWSTR rawId = nullptr;
			if (SUCCEEDED(a_device->GetId(&rawId)) && rawId)
			{
				a_id = Lower(Narrow(rawId));
				CoTaskMemFree(rawId);
			}
			IPropertyStore* store = nullptr;
			if (SUCCEEDED(a_device->OpenPropertyStore(STGM_READ, &store)) && store)
			{
				PROPVARIANT value;
				PropVariantInit(&value);
				if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR) { a_name = Narrow(value.pwszVal); }
				PropVariantClear(&value);
				store->Release();
			}
		}

		// The active render endpoint with this lower-case id, or the Windows default output when the id is empty.
		IMMDevice* OpenDevice(IMMDeviceEnumerator* a_enumerator, const std::string& a_id, std::string& a_outId, std::string& a_outName)
		{
			IMMDevice* found = nullptr;
			if (a_id.empty())
			{
				a_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &found);
			}
			else
			{
				IMMDeviceCollection* collection = nullptr;
				if (SUCCEEDED(a_enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)) && collection)
				{
					UINT count = 0;
					collection->GetCount(&count);
					for (UINT i = 0; i < count && !found; ++i)
					{
						IMMDevice* device = nullptr;
						if (FAILED(collection->Item(i, &device)) || !device) { continue; }
						std::string id, name;
						ReadIdAndName(device, id, name);
						if (id == a_id) { found = device; }
						else { device->Release(); }
					}
					collection->Release();
				}
			}
			if (found) { ReadIdAndName(found, a_outId, a_outName); }
			return found;
		}

		void Loop()
		{
			const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			IMMDeviceEnumerator* enumerator = nullptr;
			if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator))) || !enumerator)
			{
				logger::warn("volume: the Windows audio device list could not be opened; the PC volume control is off this session");
				if (SUCCEEDED(co)) { CoUninitialize(); }
				return;
			}

			IMMDevice* device = nullptr;
			IAudioEndpointVolume* endpointVolume = nullptr;
			std::string openKey;  // the device id followed, or "(default)"
			std::string openName;
			bool loggedFailure = false;

			auto release = [&]() {
				if (endpointVolume) { endpointVolume->Release(); endpointVolume = nullptr; }
				if (device) { device->Release(); device = nullptr; }
				openKey.clear();
			};

			while (true)
			{
				const std::string want = audioswitch::CurrentDeviceId();
				const std::string wantKey = want.empty() ? std::string("(default)") : want;
				if (!endpointVolume || wantKey != openKey)
				{
					release();
					std::string id, name;
					device = OpenDevice(enumerator, want, id, name);
					if (device && SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&endpointVolume))) && endpointVolume)
					{
						openKey = wantKey;
						openName = name;
						loggedFailure = false;
						{
							std::scoped_lock l(g_lock);
							g_info.device = name;
							g_info.deviceId = id;
						}
						logger::info("volume: following \"{}\"{}", name, want.empty() ? " (the Windows default output; the game has no device of its own)" : "");
					}
					else
					{
						release();
						{
							std::scoped_lock l(g_lock);
							g_info.ok = false;
						}
						if (!loggedFailure)
						{
							loggedFailure = true;
							logger::info("volume: no output device's volume can be opened right now ({})", want.empty() ? "no Windows default output" : want);
						}
					}
				}

				if (endpointVolume)
				{
					const float level = g_pendingLevel.exchange(-1.0F);
					if (level >= 0.0F)
					{
						const HRESULT hr = endpointVolume->SetMasterVolumeLevelScalar(std::clamp(level, 0.0F, 1.0F), nullptr);
						logger::info("volume: set to {:.0f}% on \"{}\" ({})", level * 100.0F, openName, SUCCEEDED(hr) ? "ok" : std::format("hr {:08X}", static_cast<unsigned>(hr)));
					}
					const int mute = g_pendingMute.exchange(-1);
					if (mute >= 0)
					{
						const HRESULT hr = endpointVolume->SetMute(mute == 1 ? TRUE : FALSE, nullptr);
						logger::info("volume: mute {} on \"{}\" ({})", mute == 1 ? "on" : "off", openName, SUCCEEDED(hr) ? "ok" : std::format("hr {:08X}", static_cast<unsigned>(hr)));
					}
					float current = 0.0F;
					BOOL muted = FALSE;
					if (SUCCEEDED(endpointVolume->GetMasterVolumeLevelScalar(&current)) && SUCCEEDED(endpointVolume->GetMute(&muted)))
					{
						std::scoped_lock l(g_lock);
						g_info.ok = true;
						g_info.level = current;
						g_info.muted = muted != FALSE;
					}
					else
					{
						// The device went away (AUDCLNT_E_DEVICE_INVALIDATED): open whichever device the game uses next pass.
						release();
						std::scoped_lock l(g_lock);
						g_info.ok = false;
					}
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(150));
			}
		}
	}

	void Start()
	{
		if (g_started.exchange(true)) { return; }
		std::thread(Loop).detach();
		logger::info("volume: PC volume thread started");
	}

	Info Get()
	{
		std::scoped_lock l(g_lock);
		return g_info;
	}

	void SetLevel(float a_level) { g_pendingLevel = std::clamp(a_level, 0.0F, 1.0F); }

	void SetMuted(bool a_muted) { g_pendingMute = a_muted ? 1 : 0; }

	std::string StateJson()
	{
		const Info i = Get();
		return std::format(R"("volume":{{"ok":{},"level":{:.0f},"muted":{},"device":"{}","deviceId":"{}"}})", i.ok ? "true" : "false", i.level * 100.0F,
						   i.muted ? "true" : "false", EscapeJson(i.device), EscapeJson(i.deviceId));
	}
}
