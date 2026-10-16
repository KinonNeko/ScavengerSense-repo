#pragma once
#include <dxgi1_2.h>
#include <string>

namespace SSC
{
	// Chains the existing COM entry rather than replacing engine instructions.
	// Calls made for other swap chains or DXGI_PRESENT_TEST are passed through.
	class PresentHook
	{
	public:
		using Callback = void(*)(void*, IDXGISwapChain*);
		bool Install(IDXGISwapChain* chain, Callback callback, void* user, std::string& error);
		void Uninstall();  // tests; SKSE DLLs remain loaded until process exit
	};
}
