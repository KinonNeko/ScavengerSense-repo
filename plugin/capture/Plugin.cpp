#include "Capture.h"
#include "Settings.h"
#include "PresentHook.h"
#include "../src/FrameworkABI.h"
#include "../src/ImGuiCompat.h"
#include <shellapi.h>
#include <fstream>
#include <map>

extern "C" __declspec(dllimport) void AddSectionItem(const char*,void(__stdcall*)());

namespace SSC
{
	using Microsoft::WRL::ComPtr;
	namespace {
		constexpr auto kIni="Data/SKSE/Plugins/ScavengerSenseCapture.ini";
		Settings settings;
		std::filesystem::path gameRoot;
		std::map<std::string,std::string> translations;
		std::vector<std::string> languages;
		std::atomic<bool> enabled{false}, menuOpen{}, hookReady{};
		PresentHook hook;
		IDXGISwapChain* installedChain{};
		std::atomic<ULONGLONG> menuDrawnAt{};
		bool lastChord{}, hookFailed{};
		const char* T(const char* key) {
			const auto it=translations.find(key); return it!=translations.end() && !it->second.empty() ? it->second.c_str() : key;
		}
		std::filesystem::path OutputDirectory() {
			if (settings.directory.empty() || settings.directory.find_first_of("\r\n")!=std::string::npos) { throw std::runtime_error("Invalid screenshot directory"); }
			auto directory=std::filesystem::u8path(settings.directory);
			return directory.is_absolute() ? directory.lexically_normal() : (gameRoot/directory).lexically_normal();
		}
		std::string UTF8(const std::filesystem::path& path) {
			const auto text=path.u8string(); return {reinterpret_cast<const char*>(text.data()),text.size()};
		}
		void LoadLanguage() {
			auto language=LanguageID(settings.language);
			if (language=="auto") {
				language="english";
				std::vector<std::filesystem::path> fragments;
				std::error_code ec;
				for (const auto& entry:std::filesystem::directory_iterator("Data/SKSE/Plugins/ScavengerSense_setup",ec)) {
					if (entry.is_regular_file(ec) && entry.path().extension()==".ini") { fragments.push_back(entry.path()); }
				}
				std::sort(fragments.begin(),fragments.end());
				fragments.push_back("Data/SKSE/Plugins/ScavengerSense.ini");
				for (const auto& file:fragments) {
					auto ini=ReadIni(file); if (const auto it=ini["general"].find("language");it!=ini["general"].end()) { language=LanguageID(it->second); }
				}
			}
			translations=ReadIni(std::filesystem::path("Data/SKSE/Plugins")/("ScavengerSenseCapture_"+language+".ini"))["translation"];
			languages={"auto","english","chinese"};
			std::error_code ec;
			for (const auto& entry:std::filesystem::directory_iterator("Data/SKSE/Plugins",ec)) {
				const auto name=entry.path().filename().string(); const std::string prefix="ScavengerSenseCapture_";
				if (entry.is_regular_file(ec) && name.starts_with(prefix) && name.ends_with(".ini")) {
					const auto code=name.substr(prefix.size(),name.size()-prefix.size()-4);
					if (LanguageID(code)==code && std::find(languages.begin(),languages.end(),code)==languages.end()) { languages.push_back(code); }
				}
			}
		}

		class Service
		{
		public:
			enum class Phase { Idle, Waiting, Readback, Writing };
			bool Request(bool afterMenu=false) {
				std::scoped_lock guard(lock);
				if (!enabled || !hookReady || phase!=Phase::Idle) { return false; }
				phase=Phase::Waiting; deferred=afterMenu; started=GetTickCount64();
				status=afterMenu ? "Close the menu to capture (30 seconds)" : "Screenshot queued"; return true;
			}
			void Cancel() {
				std::scoped_lock guard(lock);
				if (phase==Phase::Waiting) { phase=Phase::Idle; status="Screenshot cancelled"; }
			}
			std::string Status() { std::scoped_lock guard(lock); return status; }
			void Error(const std::string& why,bool reset=true) {
				std::scoped_lock guard(lock); status="Screenshot failed: "+why; if (reset) { phase=Phase::Idle; }
				logger::error("capture: {}",why);
			}
			void Present(IDXGISwapChain* chain) noexcept {
				try {
					Phase current; bool wait;
					{ std::scoped_lock guard(lock); current=phase; wait=deferred; }
					if (current==Phase::Waiting) {
						if (GetTickCount64()-started>30000) { Error("Timed out waiting for a frame or the menu to close"); return; }
						if (!enabled) { Cancel(); return; }
						if (wait && menuOpen) { return; }
						ComPtr<ID3D11Device> device;
						Require(chain->GetDevice(IID_PPV_ARGS(&device)),"Screenshot needs a D3D11 swap chain (D3D12/frame-generation wrappers may be unsupported)");
						ComPtr<ID3D11Texture2D> buffer;
						Require(chain->GetBuffer(0,IID_PPV_ARGS(&buffer)),"Get screenshot back buffer");
						device->GetImmediateContext(&context);
						path=UniquePNG(OutputDirectory());
						readback.Queue(context.Get(),buffer.Get());
						D3D11_TEXTURE2D_DESC desc{}; buffer->GetDesc(&desc);
						logger::info("capture: before DXGI Present, {}x{}, format {}, samples {}; {}",desc.Width,desc.Height,
							static_cast<unsigned>(desc.Format),desc.SampleDesc.Count,UTF8(path));
						started=GetTickCount64();
						std::scoped_lock guard(lock); phase=Phase::Readback; status="Reading screenshot";
					} else if (current==Phase::Readback) {
						if (GetTickCount64()-started>5000) { throw std::runtime_error("GPU readback timed out"); }
						if (auto pixels=readback.Poll(context.Get())) {
							readback.Reset(); context.Reset();
							auto job=std::make_unique<Job>(Job{this,path,std::move(*pixels)});
							{ std::scoped_lock guard(lock); phase=Phase::Writing; status="Saving PNG"; }
							if (!TrySubmitThreadpoolCallback(WritePNG,job.get(),nullptr)) { throw std::runtime_error("Could not start the PNG worker"); }
							job.release();
						}
					}
				} catch (const std::exception& e) { readback.Reset(); context.Reset(); Error(e.what()); }
				catch (...) { readback.Reset(); context.Reset(); Error("Unexpected capture error"); }
			}
		private:
			struct Job { Service* service; std::filesystem::path path; Pixels pixels; };
			static void CALLBACK WritePNG(PTP_CALLBACK_INSTANCE,void* data) noexcept {
				std::unique_ptr<Job> job(static_cast<Job*>(data));
				try {
					SavePNG(job->path,job->pixels);
					const auto name=UTF8(job->path);
					{ std::scoped_lock guard(job->service->lock); job->service->status="Saved PNG: "+name; job->service->phase=Phase::Idle; }
					logger::info("capture: saved {}",name);
				} catch (const std::exception& e) { job->service->Error(e.what()); }
				catch (...) { job->service->Error("Unexpected PNG encoding error"); }
			}
			std::mutex lock;
			Phase phase{Phase::Idle};
			std::string status{"Ready"};
			bool deferred{};
			ULONGLONG started{};
			Readback readback;
			ComPtr<ID3D11DeviceContext> context;
			std::filesystem::path path;
		};
		// Process lifetime: SKSE does not unload plugins while playing. Retain
		// worker status until process exit; no GPU/COM work in DllMain.
		Service& CaptureService() { static auto* service=new Service; return *service; }
		void BeforePresent(void* service,IDXGISwapChain* chain) { static_cast<Service*>(service)->Present(chain); }

		void __stdcall Frame() noexcept {
			try {
				auto* io=igGetIO(); if (!io) { return; }
				menuOpen=io->WantCaptureMouse || io->WantCaptureKeyboard || GetTickCount64()-menuDrawnAt.load()<100;
				auto* renderer=RE::BSGraphics::Renderer::GetSingleton();
				auto* chain=renderer ? reinterpret_cast<IDXGISwapChain*>(renderer->GetRuntimeData().renderWindows[0].swapChain) : nullptr;
				if (enabled && chain && installedChain!=chain && !hookFailed) {
					std::string error;
					if (hook.Install(chain,BeforePresent,&CaptureService(),error)) { installedChain=chain; hookReady=true; logger::info("capture: Present/Present1 chained"); }
					else { hookFailed=true; hookReady=false; CaptureService().Error(error); }
				}
				DWORD process{}; GetWindowThreadProcessId(GetForegroundWindow(),&process);
				const auto down=[](int key){return (GetAsyncKeyState(key)&0x8000)!=0;};
				const bool chord=enabled && process==GetCurrentProcessId() && settings.key>0 && !io->WantTextInput &&
					down(settings.key) && (!settings.ctrl || down(VK_CONTROL)) && (!settings.shift || down(VK_SHIFT)) && (!settings.alt || down(VK_MENU));
				if (chord && !lastChord) { CaptureService().Request(); }
				lastChord=chord;
			} catch (const std::exception& e) { CaptureService().Error(e.what()); }
			catch (...) { CaptureService().Error("Unexpected frame callback error"); }
		}
		void __stdcall Menu() noexcept {
			try {
				menuDrawnAt=GetTickCount64(); menuOpen=true;
				igTextWrapped("%s",T("Optional screenshot add-on. Captures the presented SDR frame, including overlays already drawn."));
				if (igCheckbox(T("Enable screenshot add-on"),&settings.enabled)) { enabled=settings.enabled; }
				igTextDisabled("%s",T("Default shortcut: Ctrl + Shift + F10"));
				const char* keys[]{T("Off"),"F6","F7","F8","F9","F10","F11","F12",T("Print Screen")};
				static const int codes[]{0,VK_F6,VK_F7,VK_F8,VK_F9,VK_F10,VK_F11,VK_F12,VK_SNAPSHOT};
				int selected=-1; for (int i=0;i<9;++i) { if (settings.key==codes[i]) { selected=i; } }
				if (igBeginCombo(T("Screenshot key"),selected>=0 ? keys[selected] : T("Custom key from INI"),0)) {
					for (int i=0;i<9;++i) { if (igSelectable_Bool(keys[i],selected==i,0,ImVec2{})) { settings.key=codes[i]; } }
					igEndCombo();
				}
				igCheckbox("Ctrl",&settings.ctrl); igSameLine(0,-1); igCheckbox("Shift",&settings.shift); igSameLine(0,-1); igCheckbox("Alt",&settings.alt);
				igTextWrapped("%s",T("Keys are not consumed. Using Print Screen or another mod's shortcut may also create their screenshot."));
				char folder[1024]{}; std::snprintf(folder,sizeof(folder),"%s",settings.directory.c_str());
				if (igInputText(T("Screenshot folder"),folder,sizeof(folder),0,nullptr,nullptr)) { settings.directory=folder; }
				igTextWrapped("%s",T("Relative folders are inside the Skyrim game directory, not Data or MO2 overwrite. PNG uses the current back-buffer resolution."));
				if (igBeginCombo(T("Capture menu language"),settings.language.c_str(),0)) {
					for (const auto& language:languages) {
						if (igSelectable_Bool(language.c_str(),settings.language==language,0,ImVec2{})) { settings.language=language; LoadLanguage(); break; }
					}
					igEndCombo();
				}
				if (igButton(T("Capture after closing menu"),ImVec2{})) { CaptureService().Request(true); }
				igSameLine(0,-1);
				if (igButton(T("Cancel queued screenshot"),ImVec2{})) { CaptureService().Cancel(); }
				igTextWrapped("%s",T("Close the framework menu within 30 seconds. The screenshot waits until keyboard/mouse capture is released."));
				if (igButton(T("Save capture settings"),ImVec2{})) { settings.Save(kIni); }
				igSameLine(0,-1);
				if (igButton(T("Reload capture settings"),ImVec2{})) { settings.Load(kIni); enabled=settings.enabled; LoadLanguage(); }
				if (igButton(T("Open screenshot folder"),ImVec2{})) {
					const auto directory=OutputDirectory(); std::filesystem::create_directories(directory);
					if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",directory.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32) { throw std::runtime_error("Could not open screenshot folder"); }
				}
				igSeparator();
				igTextWrapped("%s",T("Status and errors are also written to ScavengerSenseCapture.log."));
				const auto status=CaptureService().Status(); igTextWrapped("%s",status.c_str());
				igTextWrapped("%s",T("Preview limitation: HDR/10-bit and D3D12 frame-generation wrappers are not supported. Later render hooks can still change the final image."));
			} catch (const std::exception& e) { CaptureService().Error(e.what(),false); }
			catch (...) { CaptureService().Error("Unexpected menu callback error",false); }
		}
		void OnMessage(SKSE::MessagingInterface::Message* message) {
			if (message->type!=SKSE::MessagingInterface::kDataLoaded) { return; }
			try {
				settings.Load(kIni); enabled=settings.enabled; LoadLanguage();
				auto* host=GetModuleHandleW(L"SKSEMenuFramework.dll");
				using RegisterHUD=void*(*)(void(__stdcall*)());
				const auto registerHUD=reinterpret_cast<RegisterHUD>(GetProcAddress(host,"RegisterHudElement"));
				if (!registerHUD) { throw std::runtime_error("Menu Framework needs RegisterHudElement"); }
				registerHUD(Frame);
				AddSectionItem("Scavenger Sense/Capture",Menu);
				logger::info("capture: ready; enabled={}, default Ctrl+Shift+F10; folder={}",settings.enabled,UTF8(OutputDirectory()));
			} catch (const std::exception& e) { CaptureService().Error(e.what()); }
		}
	}

	bool RequestScreenshot() noexcept { try { return CaptureService().Request(); } catch (...) { return false; } }
	bool Load(const SKSE::LoadInterface* skse) {
		SKSE::Init(skse,false);
		const auto logs=SKSE::log::log_directory();
		if (!logs) { return false; }
		auto sink=std::make_shared<spdlog::sinks::basic_file_sink_mt>((*logs/"ScavengerSenseCapture.log").string(),true);
		spdlog::set_default_logger(std::make_shared<spdlog::logger>("capture",sink));
		spdlog::set_level(spdlog::level::info); spdlog::flush_on(spdlog::level::info);
		if (!SS::CheckFrameworkABI()) { return false; }
		std::wstring executable(32768,L'\0'); const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
		if (!length || length>=executable.size()) { return false; }
		executable.resize(length); gameRoot=std::filesystem::path(executable).parent_path();
		const auto* messaging=SKSE::GetMessagingInterface();
		return messaging && messaging->RegisterListener(OnMessage);
	}
}

SKSEPluginInfo(.Version=REL::Version{0,1,0,0},.Name="ScavengerSenseCapture"sv,.Author="KShakes"sv)
SKSEPluginLoad(const SKSE::LoadInterface* skse) { return SSC::Load(skse); }
extern "C" __declspec(dllexport) bool ScavengerSenseCapture_Request() noexcept { return SSC::RequestScreenshot(); }
