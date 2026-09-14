#pragma once

namespace UI
{
	// Adds this mod's page to the menu framework (Apocrypha Menu Framework preferred, stock SKSE Menu Framework as
	// the fallback - see include/SKSEMenuFramework.h). Safe when neither is present or the build is too old: it logs
	// why and does nothing else. Call once at kDataLoaded.
	void Register();

	namespace SettingsPanel
	{
		void __stdcall Render();
	}
}
