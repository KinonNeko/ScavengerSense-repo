#include "Capture.h"
#include <windows.h>
#include <wincodec.h>
#include <atomic>
#include <cstring>
#include <format>
#include <stdexcept>

namespace SSC
{
	using Microsoft::WRL::ComPtr;

	void Require(HRESULT result, const char* operation)
	{
		if (FAILED(result)) { throw std::runtime_error(std::format("{}: HRESULT 0x{:08X}", operation, static_cast<unsigned>(result))); }
	}

	void Readback::Queue(ID3D11DeviceContext* context, ID3D11Texture2D* source)
	{
		if (Pending()) { throw std::runtime_error("A capture is already pending"); }
		if (!context || !source) { throw std::runtime_error("No D3D11 capture surface"); }
		D3D11_TEXTURE2D_DESC desc{};
		source->GetDesc(&desc);
		const auto format = desc.Format;
		_bgra = format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
			format == DXGI_FORMAT_B8G8R8X8_UNORM || format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
		if (!_bgra && format != DXGI_FORMAT_R8G8B8A8_UNORM && format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
			throw std::runtime_error(std::format("Capture format {} is unsupported; this preview supports 8-bit SDR PNG, not HDR/10-bit output", static_cast<unsigned>(format)));
		}
		if (!desc.Width || !desc.Height || desc.ArraySize != 1 || desc.Width > 16384 || desc.Height > 16384 ||
			static_cast<std::uint64_t>(desc.Width) * desc.Height * 4 > 256ull * 1024 * 1024) {
			throw std::runtime_error("Unsupported capture dimensions (maximum 256 MiB)");
		}
		ComPtr<ID3D11Device> device, sourceDevice;
		context->GetDevice(&device);
		source->GetDevice(&sourceDevice);
		if (device.Get() != sourceDevice.Get()) { throw std::runtime_error("Capture surface and context use different devices"); }
		ComPtr<ID3D11Texture2D> resolved;
		auto* copySource = source;
		const auto samples = desc.SampleDesc.Count;
		desc.SampleDesc = {1,0}; desc.MipLevels = 1;
		desc.BindFlags = 0; desc.MiscFlags = 0;
		if (samples > 1) {
			desc.Usage = D3D11_USAGE_DEFAULT; desc.CPUAccessFlags = 0;
			Require(device->CreateTexture2D(&desc, nullptr, &resolved), "Create resolve texture");
			context->ResolveSubresource(resolved.Get(), 0, source, 0, format);
			copySource = resolved.Get();
		}
		desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture2D> staging;
		ComPtr<ID3D11Query> query;
		Require(device->CreateTexture2D(&desc, nullptr, &staging), "Create staging texture");
		const D3D11_QUERY_DESC queryDesc{D3D11_QUERY_EVENT,0};
		Require(device->CreateQuery(&queryDesc, &query), "Create capture query");
		context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, copySource, 0, nullptr);
		context->End(query.Get());
		_staging = std::move(staging); _query = std::move(query);
	}

	std::optional<Pixels> Readback::Poll(ID3D11DeviceContext* context)
	{
		if (!Pending()) { return {}; }
		BOOL complete = FALSE;
		const auto ready = context->GetData(_query.Get(), &complete, sizeof(complete), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		Require(ready, "Poll capture query");
		if (ready == S_FALSE || !complete) { return {}; }
		D3D11_MAPPED_SUBRESOURCE mapped{};
		const auto status = context->Map(_staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
		if (status == DXGI_ERROR_WAS_STILL_DRAWING) { return {}; }
		Require(status, "Map capture texture");
		// Always unmap, including allocation failure.
		struct Unmap { ID3D11DeviceContext* context; ID3D11Texture2D* texture; ~Unmap() { context->Unmap(texture,0); } } unmap{context,_staging.Get()};
		D3D11_TEXTURE2D_DESC desc{}; _staging->GetDesc(&desc);
		Pixels result{desc.Width,desc.Height,{}};
		result.rgba.resize(static_cast<std::size_t>(desc.Width)*desc.Height*4);
		for (unsigned y=0; y<desc.Height; ++y) {
			const auto* input = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y)*mapped.RowPitch;
			auto* output = result.rgba.data() + static_cast<std::size_t>(y)*desc.Width*4;
			for (unsigned x=0; x<desc.Width; ++x) {
				output[4*x] = input[4*x+(_bgra?2:0)]; output[4*x+1] = input[4*x+1];
				output[4*x+2] = input[4*x+(_bgra?0:2)]; output[4*x+3] = 255;
			}
		}
		return result;
	}

	void SavePNG(const std::filesystem::path& path, const Pixels& pixels)
	{
		if (!pixels.width || !pixels.height || pixels.rgba.size() != static_cast<std::size_t>(pixels.width)*pixels.height*4 ||
			pixels.rgba.size() > 256ull*1024*1024) { throw std::runtime_error("Invalid PNG pixel buffer"); }
		if (std::filesystem::exists(path)) { throw std::runtime_error("Refusing to overwrite an existing screenshot"); }
		if (!path.parent_path().empty()) { std::filesystem::create_directories(path.parent_path()); }
		auto temporary=path; temporary += ".partial";
		if (std::filesystem::exists(temporary)) { throw std::runtime_error("Temporary screenshot path already exists"); }
		struct Cleanup { std::filesystem::path path; bool created{}; ~Cleanup() { if (created) { DeleteFileW(path.c_str()); } } } cleanup{temporary};
		const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) { Require(initialized, "Initialize PNG COM"); }
		struct Apartment { bool owned; ~Apartment() { if (owned) { CoUninitialize(); } } } apartment{SUCCEEDED(initialized)};
		{
		ComPtr<IWICImagingFactory> factory;
		Require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "Create PNG factory");
		ComPtr<IWICStream> stream;
		Require(factory->CreateStream(&stream), "Create PNG stream");
		Require(stream->InitializeFromFilename(temporary.c_str(), GENERIC_WRITE), "Open PNG output");
		cleanup.created=true;
		ComPtr<IWICBitmapEncoder> encoder;
		Require(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "Create PNG encoder");
		Require(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initialize PNG encoder");
		ComPtr<IWICBitmapFrameEncode> frame;
		Require(encoder->CreateNewFrame(&frame, nullptr), "Create PNG frame");
		Require(frame->Initialize(nullptr), "Initialize PNG frame");
		Require(frame->SetSize(pixels.width,pixels.height), "Set PNG dimensions");
		auto format = GUID_WICPixelFormat24bppBGR;
		Require(frame->SetPixelFormat(&format), "Set PNG pixel format");
		if (format != GUID_WICPixelFormat24bppBGR) { throw std::runtime_error("PNG encoder changed the pixel format"); }
		std::vector<BYTE> bgr(static_cast<std::size_t>(pixels.width)*pixels.height*3);
		for (std::size_t pixel=0;pixel<bgr.size()/3;++pixel) {
			bgr[3*pixel]=pixels.rgba[4*pixel+2]; bgr[3*pixel+1]=pixels.rgba[4*pixel+1]; bgr[3*pixel+2]=pixels.rgba[4*pixel];
		}
		Require(frame->WritePixels(pixels.height,pixels.width*3,static_cast<UINT>(bgr.size()),bgr.data()), "Write PNG pixels");
		Require(frame->Commit(), "Commit PNG frame");
		Require(encoder->Commit(), "Commit PNG file");
		}
		// A complete file appears atomically; never replace an existing photo.
		if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)) { throw std::runtime_error("Could not finish the PNG file"); }
		cleanup.created=false;
	}

	std::filesystem::path UniquePNG(const std::filesystem::path& directory)
	{
		static std::atomic<unsigned> sequence{};
		SYSTEMTIME time{}; GetLocalTime(&time);
		return directory / std::format("ScavengerSense_{:04}-{:02}-{:02}_{:02}-{:02}-{:02}_{:03}_{}_{}.png",
			time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,GetCurrentProcessId(),++sequence);
	}
}
