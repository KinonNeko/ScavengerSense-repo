#include "PostFX.h"

#include "Config.h"

// Same arrangement as the other translation units that talk to ImGui: the menu
// framework re-exports the cimgui C API from its own DLL, so we use the host's
// context rather than linking a second copy of ImGui.
#define CIMGUI_NO_EXPORT
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>

#include <d3d11.h>
#include <d3dcompiler.h>

namespace SS
{
	namespace
	{
		// One source, two entry points.
		//
		// The vertex shader takes no vertex buffer: three vertices generated
		// from SV_VertexID cover the screen with one oversized triangle. That
		// avoids binding an input layout at all, which is one less piece of
		// pipeline state to get wrong.
		constexpr const char* kShaderSource = R"(
Texture2D    Frame : register(t0);
SamplerState Point : register(s0);

cbuffer Params : register(b0)
{
    float4 Grade;     // saturation, brightness, contrast, strength
    float4 Tint;      // rgb, amount
    float4 Vignette;  // inner, outer, strength, uvScaleX
    float4 Wash;      // rgb, uvScaleY
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    // We draw and sample over the same region, so this is an identity mapping
    // unless the viewport covers less of the target than we assume.
    float2 uv = input.uv * float2(Vignette.w, Wash.w);

    float3 source = Frame.Sample(Point, uv).rgb;

    // Rec.709 luma. This is the whole reason the pass exists: a shader can read
    // the pixel it is replacing, and alpha blending cannot, so this is the only
    // place a real desaturation can happen.
    float luma = dot(source, float3(0.2126, 0.7152, 0.0722));

    float3 graded = lerp(luma.xxx, source, Grade.x);
    graded *= Grade.y;

    // Contrast pivots around mid grey. On a floating point HDR target the scene
    // runs well past 1.0, so the pivot is scaled by how bright the pixel
    // already is - otherwise highlights blow out and darks go negative.
    float pivot = 0.5 * max(1.0, luma);
    graded = (graded - pivot) * Grade.z + pivot;

    // Tint the luminance rather than the colour, so the cast reads as the scene
    // being lit differently instead of a sheet of colour over the top of it.
    graded = lerp(graded, luma.xxx * Tint.rgb * 2.0, Tint.a);

    float3 result = lerp(source, graded, Grade.w);

    // Vignette, in the same pass, against the unscaled screen position. The
    // ellipse is slightly wider than tall so it follows the shape of a monitor.
    float2 offset = input.uv - 0.5;
    float  radius = length(offset * float2(1.0, 0.82)) * 1.4142;
    float  amount = saturate((radius - Vignette.x) / max(Vignette.y - Vignette.x, 0.0001));
    result = lerp(result, Wash.rgb, amount * amount * Vignette.z);

    return float4(max(result, 0.0), 1.0);
}
)";

		using D3DCompileFn = HRESULT(WINAPI*)(
			LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
			LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

		// Loaded rather than linked. d3dcompiler_47.dll ships with Windows, but
		// linking it would make the whole plugin refuse to load if it were ever
		// missing - and this is an optional feature.
		[[nodiscard]] D3DCompileFn GetCompiler()
		{
			static D3DCompileFn compile = [] {
				auto* module = ::LoadLibraryA("d3dcompiler_47.dll");
				return module ? reinterpret_cast<D3DCompileFn>(
									reinterpret_cast<void*>(::GetProcAddress(module, "D3DCompile")))
							  : nullptr;
			}();
			return compile;
		}

		// The typeless member of a format's family.
		//
		// This matters more than it looks. A flip model swap chain is commonly
		// created fully typed - R8G8B8A8_UNORM, say - and then given an _SRGB
		// render target view, which D3D allows only for swap chain buffers. Copy
		// that texture's format onto an ordinary texture of our own and ask for
		// an _SRGB view of it and the runtime refuses, because the relaxation
		// does not extend to committed resources. Creating the copy as typeless
		// sidesteps it: typeless to fully typed copies within one family are
		// legal, and a typeless texture will accept any view in its family.
		//
		// Borderless windowed, ENB and the HDR mods are exactly the people who
		// would have hit that, which is to say exactly the people this feature
		// is for.
		[[nodiscard]] DXGI_FORMAT TypelessFamily(DXGI_FORMAT a_format)
		{
			switch (a_format) {
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			case DXGI_FORMAT_R8G8B8A8_UINT:
			case DXGI_FORMAT_R8G8B8A8_SNORM:
			case DXGI_FORMAT_R8G8B8A8_SINT:
				return DXGI_FORMAT_R8G8B8A8_TYPELESS;

			case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
				return DXGI_FORMAT_B8G8R8A8_TYPELESS;

			case DXGI_FORMAT_B8G8R8X8_TYPELESS:
			case DXGI_FORMAT_B8G8R8X8_UNORM:
			case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
				return DXGI_FORMAT_B8G8R8X8_TYPELESS;

			case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			case DXGI_FORMAT_R10G10B10A2_UNORM:
			case DXGI_FORMAT_R10G10B10A2_UINT:
				return DXGI_FORMAT_R10G10B10A2_TYPELESS;

			case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
			case DXGI_FORMAT_R16G16B16A16_UNORM:
			case DXGI_FORMAT_R16G16B16A16_UINT:
			case DXGI_FORMAT_R16G16B16A16_SNORM:
			case DXGI_FORMAT_R16G16B16A16_SINT:
				return DXGI_FORMAT_R16G16B16A16_TYPELESS;

			case DXGI_FORMAT_R32G32B32A32_TYPELESS:
			case DXGI_FORMAT_R32G32B32A32_FLOAT:
			case DXGI_FORMAT_R32G32B32A32_UINT:
			case DXGI_FORMAT_R32G32B32A32_SINT:
				return DXGI_FORMAT_R32G32B32A32_TYPELESS;

			default:
				// R11G11B10_FLOAT and friends have no typeless member; using the
				// format itself is correct there.
				return a_format;
			}
		}

		template <class T>
		void Release(void*& a_com)
		{
			if (a_com) {
				static_cast<T*>(a_com)->Release();
				a_com = nullptr;
			}
		}

		[[nodiscard]] ID3D11Device* Device()
		{
			auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
			return renderer ? reinterpret_cast<ID3D11Device*>(renderer->GetRuntimeData().forwarder) : nullptr;
		}

		// Which of the engine's named render targets a texture is, if any. Only
		// used for the log, but it is the line that tells you at a glance
		// whether you are grading the frame or some compositing buffer.
		[[nodiscard]] std::string NameTarget(ID3D11Texture2D* a_texture)
		{
			auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
			if (!renderer || !a_texture) {
				return "unknown";
			}

			auto& data = renderer->GetRuntimeData();
			for (std::size_t i = 0; i < static_cast<std::size_t>(RE::RENDER_TARGET::kTOTAL); ++i) {
				if (reinterpret_cast<ID3D11Texture2D*>(data.renderTargets[i].texture) == a_texture) {
					return std::format("engine render target #{}", i);
				}
			}

			return "not one of the engine's render targets (probably the swap chain)";
		}

		// The surface actually being presented. Everything else is a guess about
		// what the engine happens to be compositing into at the moment the menu
		// framework's end-of-frame hook fires.
		[[nodiscard]] ID3D11Texture2D* BackBuffer()
		{
			auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
			if (!renderer) {
				return nullptr;
			}

			auto* swapChain = reinterpret_cast<IDXGISwapChain*>(
				renderer->GetRuntimeData().renderWindows[0].swapChain);
			if (!swapChain) {
				return nullptr;
			}

			ID3D11Texture2D* buffer = nullptr;
			if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer)))) {
				return nullptr;
			}
			return buffer;  // caller releases
		}

		void __cdecl Trampoline(const ImDrawList*, const ImDrawCmd*);
	}

	PostFX* PostFX::GetSingleton()
	{
		static PostFX singleton;
		return std::addressof(singleton);
	}

	std::string PostFX::Status() const
	{
		if (Failed()) {
			return "unavailable: " + _failure;
		}
		if (_ran.load(std::memory_order_relaxed)) {
			return std::format("running at {}x{}", _width, _height);
		}
		if (!_lastSkip.empty()) {
			return "waiting: " + _lastSkip;
		}
		return _vs ? "ready" : "idle";
	}

	void PostFX::Fail(std::string a_why)
	{
		if (!Failed()) {
			_failure = std::move(a_why);
			_failed.store(true, std::memory_order_relaxed);
			logger::error("postfx: disabled for this session - {}", _failure);
		}

		Release<ID3D11VertexShader>(_vs);
		Release<ID3D11PixelShader>(_ps);
		Release<ID3D11Texture2D>(_copy);
		Release<ID3D11ShaderResourceView>(_srv);
		Release<ID3D11Buffer>(_cb);
		Release<ID3D11SamplerState>(_sampler);
		Release<ID3D11BlendState>(_blend);
		Release<ID3D11DepthStencilState>(_depth);
		Release<ID3D11RasterizerState>(_raster);
	}

	void PostFX::Skip(const char* a_why)
	{
		// Not every frame is ours to grade, and a frame we cannot use is not a
		// reason to give up for good: a loading screen or another mod reordering
		// its passes could produce one at any moment. Say so once and try again
		// next frame.
		if (_lastSkip != a_why) {
			_lastSkip = a_why;
			logger::warn("postfx: skipping frames - {}", _lastSkip);
		}
	}

	void PostFX::Warm()
	{
		if (!Settings::GetSingleton()->postProcess || Failed() || _vs) {
			return;
		}

		auto* device = Device();
		if (!device) {
			// Not fatal - the renderer may simply not be up yet, and Execute
			// will try again on the first frame that needs it.
			return;
		}

		if (PrepareShared(device)) {
			logger::info("postfx: shaders compiled ahead of time");
		}
	}

	void PostFX::Submit(void* a_drawList, const Params& a_params)
	{
		if (Failed() || !a_drawList) {
			return;
		}

		if (a_params.strength <= 0.004f && a_params.vignetteStrength <= 0.004f) {
			return;
		}

		_params = a_params;

		auto* draw = static_cast<ImDrawList*>(a_drawList);
		ImDrawList_AddCallback(draw, Trampoline, this);
		// Hand the pipeline back to ImGui explicitly. Without this it would keep
		// drawing with our shaders bound and the HUD would come out as a
		// full-screen smear of whatever our pixel shader last produced.
		ImDrawList_AddCallback(draw, ImDrawCallback_ResetRenderState, nullptr);
	}

	bool PostFX::PrepareShared(void* a_device)
	{
		if (_vs && _ps && _cb && _sampler && _blend && _depth && _raster) {
			return true;
		}

		auto* device = static_cast<ID3D11Device*>(a_device);

		auto* compile = GetCompiler();
		if (!compile) {
			Fail("d3dcompiler_47.dll is not available");
			return false;
		}

		const auto build = [&](const char* a_entry, const char* a_target, ID3DBlob*& a_out) {
			ID3DBlob*  errors = nullptr;
			const auto hr = compile(kShaderSource, std::strlen(kShaderSource), "ScavengerSense",
				nullptr, nullptr, a_entry, a_target, 0, 0, &a_out, &errors);
			if (FAILED(hr) || !a_out) {
				const auto* text = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no detail";
				Fail(std::format("{} failed to compile: {}", a_entry, text));
			}
			if (errors) {
				errors->Release();
			}
			return !Failed();
		};

		ID3DBlob* vsBlob = nullptr;
		ID3DBlob* psBlob = nullptr;
		bool      ok = build("VSMain", "vs_5_0", vsBlob) && build("PSMain", "ps_5_0", psBlob);

		if (ok) {
			ID3D11VertexShader* vs = nullptr;
			ID3D11PixelShader*  ps = nullptr;
			ok = SUCCEEDED(device->CreateVertexShader(
					 vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs)) &&
			     SUCCEEDED(device->CreatePixelShader(
					 psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps));
			if (ok) {
				_vs = vs;
				_ps = ps;
			} else {
				if (vs) {
					vs->Release();
				}
				if (ps) {
					ps->Release();
				}
				Fail("the device refused the compiled shaders");
			}
		}

		if (vsBlob) {
			vsBlob->Release();
		}
		if (psBlob) {
			psBlob->Release();
		}
		if (!ok) {
			return false;
		}

		D3D11_BUFFER_DESC cbDesc{};
		cbDesc.ByteWidth = sizeof(Params);
		cbDesc.Usage = D3D11_USAGE_DYNAMIC;
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		ID3D11Buffer* cb = nullptr;
		if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &cb))) {
			Fail("could not create the constant buffer");
			return false;
		}
		_cb = cb;

		D3D11_SAMPLER_DESC sampler{};
		sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sampler.MaxLOD = D3D11_FLOAT32_MAX;

		ID3D11SamplerState* samplerState = nullptr;
		if (FAILED(device->CreateSamplerState(&sampler, &samplerState))) {
			Fail("could not create the sampler");
			return false;
		}
		_sampler = samplerState;

		// Straight replace: everything worth blending was blended in the shader,
		// and blending here would mix our result with the frame we just read.
		// Alpha is deliberately left alone - some overlays composite on it.
		D3D11_BLEND_DESC blend{};
		blend.RenderTarget[0].BlendEnable = FALSE;
		blend.RenderTarget[0].RenderTargetWriteMask =
			D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;

		ID3D11BlendState* blendState = nullptr;
		if (FAILED(device->CreateBlendState(&blend, &blendState))) {
			Fail("could not create the blend state");
			return false;
		}
		_blend = blendState;

		D3D11_DEPTH_STENCIL_DESC depth{};
		depth.DepthEnable = FALSE;
		depth.StencilEnable = FALSE;

		ID3D11DepthStencilState* depthState = nullptr;
		if (FAILED(device->CreateDepthStencilState(&depth, &depthState))) {
			Fail("could not create the depth state");
			return false;
		}
		_depth = depthState;

		// ScissorEnable stays FALSE by zero initialisation, and that is
		// load bearing: ImGui does not set a scissor rect before the first
		// command, so whatever the game left behind is still in force and would
		// clip our triangle to some corner of the screen.
		D3D11_RASTERIZER_DESC raster{};
		raster.FillMode = D3D11_FILL_SOLID;
		raster.CullMode = D3D11_CULL_NONE;
		raster.DepthClipEnable = FALSE;
		raster.ScissorEnable = FALSE;

		ID3D11RasterizerState* rasterState = nullptr;
		if (FAILED(device->CreateRasterizerState(&raster, &rasterState))) {
			Fail("could not create the rasteriser state");
			return false;
		}
		_raster = rasterState;

		return true;
	}

	bool PostFX::PrepareTarget(void* a_device, std::uint32_t a_viewFormat, const void* a_targetDesc)
	{
		auto*       device = static_cast<ID3D11Device*>(a_device);
		const auto& target = *static_cast<const D3D11_TEXTURE2D_DESC*>(a_targetDesc);
		const auto  viewFormat = static_cast<DXGI_FORMAT>(a_viewFormat);
		const auto  copyFormat = TypelessFamily(viewFormat);

		// Recreate whenever the frame buffer changes shape or format - a
		// resolution change, a borderless toggle, an upscaler switching modes.
		// Both formats are in the key: keying on one of them lets an alternating
		// pair of targets rebuild a full-screen texture every single frame.
		if (_copy && _width == target.Width && _height == target.Height &&
			_viewFormat == static_cast<std::uint32_t>(viewFormat) &&
			_copyFormat == static_cast<std::uint32_t>(copyFormat)) {
			return true;
		}

		Release<ID3D11ShaderResourceView>(_srv);
		Release<ID3D11Texture2D>(_copy);

		D3D11_TEXTURE2D_DESC desc = target;
		desc.Format = copyFormat;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		ID3D11Texture2D* copy = nullptr;
		if (FAILED(device->CreateTexture2D(&desc, nullptr, &copy)) || !copy) {
			Fail("could not create the frame copy texture");
			return false;
		}
		_copy = copy;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = viewFormat;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		ID3D11ShaderResourceView* srv = nullptr;
		if (FAILED(device->CreateShaderResourceView(copy, &srvDesc, &srv)) || !srv) {
			Fail("could not create a shader resource view of the frame copy");
			return false;
		}
		_srv = srv;

		_width = target.Width;
		_height = target.Height;
		_viewFormat = static_cast<std::uint32_t>(viewFormat);
		_copyFormat = static_cast<std::uint32_t>(copyFormat);

		logger::info("postfx: target is {}x{}, view format {}, copy format {}",
			_width, _height, _viewFormat, _copyFormat);
		return true;
	}

	void PostFX::Execute()
	{
		if (Failed()) {
			return;
		}

		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		if (!renderer) {
			Fail("no renderer singleton");
			return;
		}

		auto& data = renderer->GetRuntimeData();
		auto* device = reinterpret_cast<ID3D11Device*>(data.forwarder);
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(data.context);
		if (!device || !context) {
			Fail("no D3D11 device or context");
			return;
		}

		if (!PrepareShared(device)) {
			return;
		}

		// What the game left bound. We always save it, because whatever we do
		// next, ImGui carries on drawing here the moment we return.
		ID3D11RenderTargetView* boundRTV = nullptr;
		ID3D11DepthStencilView* boundDSV = nullptr;
		context->OMGetRenderTargets(1, &boundRTV, &boundDSV);

		// Grade the surface being presented rather than whatever happened to be
		// bound. The menu framework's hook runs at the end of the frame and
		// inherits the last binding the engine made; if that is a UI
		// compositing target, grading it recolours the interface and leaves the
		// world untouched - which is exactly the symptom that sent me here.
		const bool useBackBuffer = Settings::GetSingleton()->postProcessBackBuffer;

		ID3D11RenderTargetView* rtv = nullptr;
		ID3D11Texture2D*        texture = nullptr;
		ID3D11Resource*         resource = nullptr;
		bool                    ownRTV = false;

		if (useBackBuffer) {
			texture = BackBuffer();
			if (!texture) {
				Skip("the swap chain would not hand over its back buffer");
				if (boundRTV) {
					boundRTV->Release();
				}
				if (boundDSV) {
					boundDSV->Release();
				}
				return;
			}
			if (FAILED(device->CreateRenderTargetView(texture, nullptr, &rtv)) || !rtv) {
				texture->Release();
				Skip("could not make a render target view of the back buffer");
				if (boundRTV) {
					boundRTV->Release();
				}
				if (boundDSV) {
					boundDSV->Release();
				}
				return;
			}
			ownRTV = true;
		} else {
			if (!boundRTV) {
				Skip("nothing is bound as a render target at HUD time");
				if (boundDSV) {
					boundDSV->Release();
				}
				return;
			}
			rtv = boundRTV;
			rtv->AddRef();
			rtv->GetResource(&resource);
			if (resource) {
				resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
			}
			if (!texture) {
				if (resource) {
					resource->Release();
				}
				rtv->Release();
				boundRTV->Release();
				if (boundDSV) {
					boundDSV->Release();
				}
				Skip("the bound render target is not a 2D texture");
				return;
			}
		}

		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtv->GetDesc(&rtvDesc);

		if (!_named) {
			_named = true;
			logger::info("postfx: grading the {} - {} - {}x{}",
				useBackBuffer ? "back buffer" : "bound target",
				NameTarget(texture), desc.Width, desc.Height);
		}

		if (desc.SampleDesc.Count > 1) {
			Skip("the bound render target is multisampled, which cannot be copied directly");
		} else if (PrepareTarget(device, static_cast<std::uint32_t>(rtvDesc.Format), &desc)) {
			// We bind our own viewport covering the whole target below, and we
			// sample the whole of the copy, so the mapping is one to one. This
			// used to be derived from whatever viewport was current, which was
			// always ImGui's - a more complicated way of arriving at 1.0.
			_params.uvScaleX = 1.0f;
			_params.uvScaleY = 1.0f;

			D3D11_MAPPED_SUBRESOURCE mapped{};
			auto*                    cb = static_cast<ID3D11Buffer*>(_cb);
			if (FAILED(context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				Skip("the constant buffer could not be mapped");
			} else {
				std::memcpy(mapped.pData, &_params, sizeof(_params));
				context->Unmap(cb, 0);

				context->CopyResource(static_cast<ID3D11Texture2D*>(_copy), texture);

				// Draw into the surface we chose, with no depth buffer. This is
				// put back before we return so ImGui keeps drawing where it was.
				context->OMSetRenderTargets(1, &rtv, nullptr);

				// Our own viewport covering the whole surface. ImGui's is sized
				// to its display, which is not necessarily the same thing once
				// we have switched targets. ResetRenderState puts ImGui's back.
				D3D11_VIEWPORT full{};
				full.Width = static_cast<float>(desc.Width);
				full.Height = static_cast<float>(desc.Height);
				full.MaxDepth = 1.0f;
				context->RSSetViewports(1, &full);

				auto* srv = static_cast<ID3D11ShaderResourceView*>(_srv);
				auto* sampler = static_cast<ID3D11SamplerState*>(_sampler);

				ID3D11Buffer* noBuffer = nullptr;
				UINT          zero = 0;
				context->IASetInputLayout(nullptr);
				context->IASetVertexBuffers(0, 1, &noBuffer, &zero, &zero);
				context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
				context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				context->VSSetShader(static_cast<ID3D11VertexShader*>(_vs), nullptr, 0);
				context->PSSetShader(static_cast<ID3D11PixelShader*>(_ps), nullptr, 0);
				context->GSSetShader(nullptr, nullptr, 0);
				context->HSSetShader(nullptr, nullptr, 0);
				context->DSSetShader(nullptr, nullptr, 0);

				context->PSSetShaderResources(0, 1, &srv);
				context->PSSetSamplers(0, 1, &sampler);
				context->PSSetConstantBuffers(0, 1, &cb);

				const float blendFactor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
				context->OMSetBlendState(static_cast<ID3D11BlendState*>(_blend), blendFactor, 0xFFFFFFFF);
				context->OMSetDepthStencilState(static_cast<ID3D11DepthStencilState*>(_depth), 0);
				context->RSSetState(static_cast<ID3D11RasterizerState*>(_raster));

				context->Draw(3, 0);

				// Two bindings have to be undone by hand. The shader resource
				// must not still be readable when something renders into the
				// same texture; and the pixel shader constant buffer is the one
				// slot ImGui neither backs up nor rebinds, because its own
				// shader does not use one - so ours would stay bound for the
				// rest of the frame and into whatever came next.
				ID3D11ShaderResourceView* noSRV = nullptr;
				context->PSSetShaderResources(0, 1, &noSRV);
				context->PSSetConstantBuffers(0, 1, &noBuffer);

				if (!_ran.load(std::memory_order_relaxed)) {
					_ran.store(true, std::memory_order_relaxed);
					_lastSkip.clear();
					logger::info("postfx: first pass ran at {}x{}", _width, _height);
				}
			}
		}

		// Put the binding back before returning. ImGui resumes drawing into
		// whatever is bound the instant this callback ends, and if we left our
		// own target in place the whole HUD would land somewhere else. The
		// depth view goes back too, untouched.
		context->OMSetRenderTargets(1, &boundRTV, boundDSV);

		texture->Release();
		if (resource) {
			resource->Release();
		}
		rtv->Release();
		static_cast<void>(ownRTV);
		if (boundRTV) {
			boundRTV->Release();
		}
		if (boundDSV) {
			boundDSV->Release();
		}
	}

	namespace
	{
		void __cdecl Trampoline(const ImDrawList*, const ImDrawCmd* a_cmd)
		{
			if (a_cmd && a_cmd->UserCallbackData) {
				static_cast<PostFX*>(a_cmd->UserCallbackData)->Execute();
			}
		}
	}
}
