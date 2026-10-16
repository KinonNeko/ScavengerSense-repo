#include "PresentHook.h"
#include <windows.h>
#include <wrl/client.h>
#include <algorithm>
#include <mutex>
#include <vector>

namespace SSC
{
	namespace {
		struct Entry { void** slot; void* next; PresentHook* owner; PresentHook::Callback callback; void* user; std::vector<void*> targets; };
		struct Registry { std::mutex lock; std::vector<Entry> entries; };
		struct Resolved { void* next{}; PresentHook::Callback callback{}; void* user{}; bool selected{}; };
		Registry& Hooks() { static auto* registry=new Registry; return *registry; }
		using Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
		using Present1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
		Resolved Lookup(void* chain,unsigned index) {
			auto& registry=Hooks(); std::scoped_lock lock(registry.lock);
			auto** slot=*static_cast<void***>(chain)+index;
			const auto it=std::find_if(registry.entries.begin(),registry.entries.end(),[&](const auto& e){return e.slot==slot;});
			return it==registry.entries.end() ? Resolved{} : Resolved{it->next,it->callback,it->user,
				std::find(it->targets.begin(),it->targets.end(),chain)!=it->targets.end()};
		}
		void Capture(const Resolved& entry,IDXGISwapChain* chain,UINT flags) noexcept {
			if ((flags&DXGI_PRESENT_TEST)==0 && entry.callback && entry.selected) {
				try { entry.callback(entry.user,chain); } catch (...) {} // never cross the graphics ABI
			}
		}
		HRESULT STDMETHODCALLTYPE Thunk(IDXGISwapChain* chain,UINT sync,UINT flags) {
			const auto entry=Lookup(chain,8); if (!entry.next) { return E_UNEXPECTED; }
			Capture(entry,chain,flags);
			return reinterpret_cast<Present>(entry.next)(chain,sync,flags);
		}
		HRESULT STDMETHODCALLTYPE Thunk1(IDXGISwapChain1* chain,UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* parameters) {
			const auto entry=Lookup(chain,22); if (!entry.next) { return E_UNEXPECTED; }
			Capture(entry,chain,flags);
			return reinterpret_cast<Present1>(entry.next)(chain,sync,flags,parameters);
		}
		bool Patch(void** slot,void* expected,void* replacement) {
			DWORD old{}; if (!VirtualProtect(slot,sizeof(void*),PAGE_EXECUTE_READWRITE,&old)) { return false; }
			const auto previous=InterlockedCompareExchangePointer(slot,replacement,expected);
			DWORD ignored{}; VirtualProtect(slot,sizeof(void*),old,&ignored);
			return previous==expected;
		}
	}

	bool PresentHook::Install(IDXGISwapChain* chain,Callback callback,void* user,std::string& error)
	{
		if (!chain || !callback) { error="No swap chain or callback"; return false; }
		auto& registry=Hooks(); std::scoped_lock lock(registry.lock);
		const auto install=[&](void* object,unsigned index,void* thunk) {
			auto** slot=*static_cast<void***>(object)+index;
			const auto it=std::find_if(registry.entries.begin(),registry.entries.end(),[&](const auto& e){return e.slot==slot;});
			if (it!=registry.entries.end()) {
				if (it->owner!=this) { error="Swap chain is owned by another capture instance"; return false; }
				if (std::find(it->targets.begin(),it->targets.end(),object)==it->targets.end()) { it->targets.push_back(object); }
				// Another plugin may wrap our entry later. Do not wrap it again:
				// its saved next entry may already be us, causing recursion.
				return true;
			}
			const auto previous=*slot;
			registry.entries.push_back({slot,previous,this,callback,user,{object}});
			if (!Patch(slot,previous,thunk)) { registry.entries.pop_back(); error="Could not chain DXGI Present"; return false; }
			return true;
		};
		if (!install(chain,8,reinterpret_cast<void*>(&Thunk))) { return false; }
		Microsoft::WRL::ComPtr<IDXGISwapChain1> newer;
		if (SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&newer)))) {
			if (!install(newer.Get(),22,reinterpret_cast<void*>(&Thunk1))) { return false; }
		}
		return true;
	}
	void PresentHook::Uninstall()
	{
		auto& registry=Hooks(); std::scoped_lock lock(registry.lock);
		for (auto it=registry.entries.begin();it!=registry.entries.end();) {
			if (it->owner!=this) { ++it; continue; }
			const auto thunk=(*it->slot==reinterpret_cast<void*>(&Thunk1)) ? reinterpret_cast<void*>(&Thunk1) : reinterpret_cast<void*>(&Thunk);
			// If another wrapper still calls us, retain a pass-through entry.
			if (Patch(it->slot,thunk,it->next)) { it=registry.entries.erase(it); }
			else { it->callback=nullptr; it->targets.clear(); ++it; }
		}
	}
}
