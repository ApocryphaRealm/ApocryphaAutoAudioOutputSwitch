#pragma once

// Auto Audio Output Switch - media keys in game. GPL-3.0-or-later (2026-09-14).
//
// The owner, 2026-09-14, after Media Keys Fix SKSE fixed the keyboard's volume keys in game: "i want to incorporate the media
// keys fix if possible". Skyrim creates its DirectInput keyboard with DISCL_EXCLUSIVE | DISCL_FOREGROUND | DISCL_NOWINKEY
// (0x15), so while the game has focus Windows never sees the volume, mute and other media keys - and a Bluetooth keyboard's
// Fn media keys bypass even low-level keyboard hooks then (falsification episodes 43-45). The approach comes from Media Keys
// Fix SKSE by Emerson Pinter (epinter; Nexus 92948; LGPL-3.0-or-later, notice in THIRD_PARTY_NOTICES.md), re-implemented
// here:
//
//  * the flags the game passes to SetCooperativeLevel (the `mov r8d, 15h` inside its DirectInput device setup, Address
//    Library 67471 SE / 68781 AE, +0x55) become DISCL_FOREGROUND | DISCL_NONEXCLUSIVE, plus DISCL_NOWINKEY when the Windows
//    key stays disabled - checked against the expected instruction bytes before anything is written;
//  * optionally the game's ToUnicode call in its keyboard processing (67472 SE +0x20D / 68782 AE +0x2CB) is wrapped so a
//    dead key yields its character at once: non-exclusive access otherwise leaves accent keys (US-International and
//    similar layouts) waiting for a second key in the console.
//
// Applied once at plugin load, before the game creates the keyboard, so a changed setting takes effect at the next start.
// When MediaKeysFix.dll itself is installed this does nothing, so the two never patch the same bytes.

#include <string>

namespace mediakeys
{
	enum class Status
	{
		kNotRun,
		kOff,       // bMediaKeys=0
		kActive,    // the patch is in
		kOtherMod,  // Media Keys Fix SKSE is installed and does it
		kFailed     // the expected code was not found on this game version
	};

	void Install();               // SKSEPluginLoad, after settings::Init and SKSE::AllocTrampoline
	Status GetStatus();
	std::string StateJson();      // "mediaKeys":{...} member, no braces around it
}
