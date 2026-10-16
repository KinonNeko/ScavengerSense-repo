#include "../capture/Capture.h"
#include "../capture/PresentHook.h"
#include "../capture/Settings.h"
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <windows.h>
#include <wincodec.h>
#include <future>
#include <fstream>
#include <iostream>
#include <thread>

using namespace SSC;
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
static int checks{};
static void Check(bool value,const char* why) { ++checks; if (!value) { throw std::runtime_error(why); } }
template<class F> static void Reject(F&& fn,const char* why) { bool rejected=false; try { fn(); } catch (const std::exception&) { rejected=true; } Check(rejected,why); }
static Pixels Collect(Readback& readback,ID3D11DeviceContext* context) {
	context->Flush();
	for (int i=0;i<2000;++i) {
		if (auto pixels=readback.Poll(context)) { readback.Reset(); return std::move(*pixels); }
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	throw std::runtime_error("Readback timeout");
}
static Pixels Decode(const fs::path& path) {
	ComPtr<IWICImagingFactory> factory;
	Require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Test WIC factory");
	ComPtr<IWICBitmapDecoder> decoder; Require(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"Decode test PNG");
	ComPtr<IWICBitmapFrameDecode> frame; Require(decoder->GetFrame(0,&frame),"Decode PNG frame");
	ComPtr<IWICFormatConverter> converter; Require(factory->CreateFormatConverter(&converter),"Test converter");
	Require(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Convert test PNG");
	Pixels pixels; Require(frame->GetSize(&pixels.width,&pixels.height),"PNG size");
	pixels.rgba.resize(static_cast<std::size_t>(pixels.width)*pixels.height*4);
	Require(converter->CopyPixels(nullptr,pixels.width*4,static_cast<UINT>(pixels.rgba.size()),pixels.rgba.data()),"Read decoded pixels"); return pixels;
}
static bool Colour(const Pixels& p,unsigned x,unsigned y,unsigned r,unsigned g,unsigned b) {
	const auto* at=p.rgba.data()+4*(static_cast<std::size_t>(y)*p.width+x);
	return at[0]==r && at[1]==g && at[2]==b && at[3]==255;
}
struct Surface {
	HWND window{};
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	ComPtr<IDXGISwapChain> chain;
	ComPtr<ID3D11RenderTargetView> view;
	Surface() {
		window=CreateWindowExW(0,L"STATIC",L"Capture offline test",WS_POPUP,0,0,128,96,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
		if (!window) { throw std::runtime_error("No hidden test window"); }
		DXGI_SWAP_CHAIN_DESC desc{};
		desc.BufferDesc.Width=128; desc.BufferDesc.Height=96; desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=1; desc.SampleDesc.Count=1;
		desc.OutputWindow=window; desc.Windowed=TRUE; desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
		Require(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
			&desc,&chain,&device,nullptr,&context),"Create WARP test device"); Bind();
	}
	void Bind() {
		ComPtr<ID3D11Texture2D> buffer; Require(chain->GetBuffer(0,IID_PPV_ARGS(&buffer)),"Test buffer");
		Require(device->CreateRenderTargetView(buffer.Get(),nullptr,&view),"Test RTV");
		context->OMSetRenderTargets(1,view.GetAddressOf(),nullptr);
	}
	~Surface() { view.Reset(); chain.Reset(); context.Reset(); device.Reset(); if (window) { DestroyWindow(window); } }
};
struct Pending {
	Readback readback;
	ID3D11DeviceContext* context{};
	int calls{};
	bool request{};
	static void Before(void* data,IDXGISwapChain* chain) {
		auto& state=*static_cast<Pending*>(data); ++state.calls;
		if (state.request) {
			ComPtr<ID3D11Texture2D> buffer; Require(chain->GetBuffer(0,IID_PPV_ARGS(&buffer)),"Hook buffer");
			state.readback.Queue(state.context,buffer.Get()); state.request=false;
		}
	}
};

int main() try {
	Require(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Test COM");
	Settings settings; settings.directory="Screenshots/测试"; settings.language="chinese"; settings.ctrl=false; settings.alt=true; settings.key=VK_F8;
	settings.Save("roundtrip.ini"); Settings loaded; loaded.Load("roundtrip.ini");
	Check(loaded.directory==settings.directory && loaded.language=="chinese" && !loaded.ctrl && loaded.shift && loaded.alt && loaded.key==VK_F8,"UTF-8 config and key round trip");
	{ std::ofstream file("bad.ini"); file << "\xEF\xBB\xBF[General]\r\nlanguage=../../outside\nenabled=garbage\n[Hotkey]\nkey=9999\nctrl=false\n"; }
	loaded.Load("bad.ini"); Check(loaded.language=="english" && loaded.enabled && loaded.key==VK_F10 && !loaded.ctrl,"invalid config has bounded defaults");
	settings.directory="bad\n[Injected]"; Reject([&]{settings.Save("roundtrip.ini");},"reject INI injection without overwriting old file");
	loaded.Load("roundtrip.ini"); Check(loaded.directory=="Screenshots/测试","failed save preserves original settings");
	const auto first=UniquePNG(fs::path(L"captures/截图")), second=UniquePNG(fs::path(L"captures/截图")); Check(first!=second,"rapid screenshots have different paths");
	Surface surface;
	// This separate test executable owns an ImGui implementation. Production
	// DLLs import the host API and never link these test-only ImGui objects.
	ImGui::CreateContext(); auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={128,96}; io.DeltaTime=1.0f/60;
	Check(ImGui_ImplDX11_Init(surface.device.Get(),surface.context.Get()),"test backend starts");
	const float blue[]{0,0,1,0}; surface.context->ClearRenderTargetView(surface.view.Get(),blue);
	Readback early;
	{ ComPtr<ID3D11Texture2D> buffer; surface.chain->GetBuffer(0,IID_PPV_ARGS(&buffer)); early.Queue(surface.context.Get(),buffer.Get()); }
	const auto beforeUI=Collect(early,surface.context.Get()); Check(Colour(beforeUI,30,30,0,0,255),"early capture has only the scene");
	ImGui_ImplDX11_NewFrame(); ImGui::NewFrame();
	ImGui::GetForegroundDrawList()->AddRectFilled({20,20},{80,70},IM_COL32(0,255,0,255));
	ImGui::GetForegroundDrawList()->AddRectFilled({40,35},{55,50},IM_COL32(255,0,0,255));
	ImGui::Render(); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	Pending pending; pending.context=surface.context.Get(); PresentHook hook; std::string error;
	Check(hook.Install(surface.chain.Get(),Pending::Before,&pending,error),"Present hook installs");
	Check(hook.Install(surface.chain.Get(),Pending::Before,&pending,error),"repeat install does not recurse");
	pending.request=true;
	surface.chain->Present(0,DXGI_PRESENT_TEST); Check(pending.request && pending.calls==0,"TEST presentation does not consume a screenshot");
	surface.chain->Present(0,0); Check(!pending.request && pending.calls==1,"real presentation consumes one request");
	const auto final=Collect(pending.readback,surface.context.Get());
	Check(Colour(final,5,5,0,0,255) && Colour(final,30,30,0,255,0) && Colour(final,45,40,255,0,0),"late screenshot retains the scene and both overlay layers");
	auto worker=std::async(std::launch::async,[&]{SavePNG(first,final);}); worker.get();
	const auto decoded=Decode(first);
	Check(decoded.width==128 && decoded.height==96 && decoded.rgba==final.rgba,"worker PNG decodes to exact opaque pixels");
	Reject([&]{SavePNG(first,final);},"screenshots do not overwrite existing files");
	Check(Decode(first).rgba==final.rgba,"existing screenshot remains unchanged");
	{ Surface other; const int calls=pending.calls; other.chain->Present(0,0); Check(pending.calls==calls,"other swap chains are passed through"); }
	// Resize to a non-aligned row width; staging row pitch must not leak.
	surface.context->OMSetRenderTargets(0,nullptr,nullptr); surface.view.Reset();
	Require(surface.chain->ResizeBuffers(1,97,73,DXGI_FORMAT_R8G8B8A8_UNORM,0),"Resize test buffers"); surface.Bind();
	const float orange[]{1,0.5f,0,0}; surface.context->ClearRenderTargetView(surface.view.Get(),orange);
	pending.request=true; surface.chain->Present(0,0); auto resized=Collect(pending.readback,surface.context.Get());
	Check(resized.width==97 && resized.height==73 && resized.rgba.size()==97*73*4,"resize and padded rows retain exact dimensions");
	Check(Colour(resized,96,72,255,128,0),"last pixel survives row-pitch copy and alpha is opaque");
	ComPtr<IDXGISwapChain1> newer;
	if (SUCCEEDED(surface.chain.As(&newer))) {
		const int calls=pending.calls; DXGI_PRESENT_PARAMETERS parameters{};
		newer->Present1(0,DXGI_PRESENT_TEST,&parameters); Check(pending.calls==calls,"Present1 TEST also passes through");
		newer->Present1(0,0,&parameters); Check(pending.calls>calls,"Present1 reaches the capture hook");
	}
	for (const auto format:{DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}) {
		D3D11_TEXTURE2D_DESC desc{}; desc.Width=3;desc.Height=2;desc.MipLevels=1;desc.ArraySize=1;desc.SampleDesc.Count=1;desc.Format=format;desc.Usage=D3D11_USAGE_DEFAULT;
		const std::uint8_t bgra[]{3,2,1,0,3,2,1,0,3,2,1,0,3,2,1,0,3,2,1,0,3,2,1,0};
		D3D11_SUBRESOURCE_DATA data{bgra,12,0}; ComPtr<ID3D11Texture2D> texture;
		Require(surface.device->CreateTexture2D(&desc,&data,&texture),"Create format fixture");
		Readback capture; capture.Queue(surface.context.Get(),texture.Get());
		Reject([&]{capture.Queue(surface.context.Get(),texture.Get());},"GPU requests are bounded");
		auto image=Collect(capture,surface.context.Get());
		Check(format==DXGI_FORMAT_B8G8R8A8_UNORM ? Colour(image,2,1,1,2,3) : Colour(image,2,1,3,2,1),"BGRA conversion and sRGB byte preservation");
	}
	for (auto format:{DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R10G10B10A2_UNORM}) {
		D3D11_TEXTURE2D_DESC hdr{}; hdr.Width=4;hdr.Height=4;hdr.MipLevels=1;hdr.ArraySize=1;hdr.SampleDesc.Count=1;hdr.Format=format;
		ComPtr<ID3D11Texture2D> hdrTexture; Require(surface.device->CreateTexture2D(&hdr,nullptr,&hdrTexture),"HDR fixture");
		Readback unsupported; Reject([&]{unsupported.Queue(surface.context.Get(),hdrTexture.Get());},"HDR/10-bit explicitly rejected rather than saved with incorrect colours");
	}
	{
		D3D11_TEXTURE2D_DESC desc{}; desc.Width=4;desc.Height=4;desc.MipLevels=1;desc.ArraySize=1;desc.SampleDesc.Count=4;
		desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.BindFlags=D3D11_BIND_RENDER_TARGET;
		ComPtr<ID3D11Texture2D> multisampled; Require(surface.device->CreateTexture2D(&desc,nullptr,&multisampled),"MSAA fixture");
		ComPtr<ID3D11RenderTargetView> view; Require(surface.device->CreateRenderTargetView(multisampled.Get(),nullptr,&view),"MSAA RTV");
		const float green[]{0,1,0,1}; surface.context->ClearRenderTargetView(view.Get(),green);
		Readback resolve; resolve.Queue(surface.context.Get(),multisampled.Get());
		Check(Colour(Collect(resolve,surface.context.Get()),3,3,0,255,0),"MSAA resolve preserves colour");
	}
	const int calls=pending.calls; hook.Uninstall(); surface.chain->Present(0,0); Check(pending.calls==calls,"uninstall restores the original Present chain");
	ImGui_ImplDX11_Shutdown(); ImGui::DestroyContext(); CoUninitialize();
	std::cout << checks << " screenshot checks passed; WARP scene + ImGui overlays + chained Present/Present1 + PNG round trip\n";
	return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
