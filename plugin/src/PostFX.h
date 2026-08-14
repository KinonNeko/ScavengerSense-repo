#pragma once

namespace SS
{
	// A real full-screen post-process pass, for when the image space route is
	// not enough.
	//
	// Why this exists: writing the cinematic parameters works on a stock game
	// but not under Community Shaders, and there is no way to make CS honour
	// them. It has no plugin API at all - no exported interface, no messaging,
	// features are a hardcoded compile-time list, and dropping an HLSL into its
	// Features folder is rejected by a name whitelist. There is nothing to ask
	// nicely.
	//
	// So we run downstream of it instead. The menu framework's HUD hook renders
	// ImGui after everything else in the frame, and ImGui draw lists can carry
	// callbacks that hand us the device context mid-render. At that moment the
	// finished frame is bound as the render target, so we copy it, sample the
	// copy through our own pixel shader, and write the graded result back over
	// it. Nothing downstream can undo that, because there is nothing downstream.
	//
	// Three things make this safe to ship:
	//
	//  * ImGui's D3D11 backend brackets its whole render in a device-state save
	//    and restore, and we queue ImDrawCallback_ResetRenderState straight
	//    after our own callback so it rebinds its pipeline before drawing
	//    anything else. That covers all but one of the bindings we touch - the
	//    pixel shader's constant buffer slot, which ImGui never uses and so
	//    never rebinds, and which we therefore clear by hand.
	//
	//  * Failures are sorted into two kinds. A frame that simply is not ours to
	//    grade - nothing bound, a multisampled target, a target that is not a 2D
	//    texture - is skipped and tried again next frame. Only something that
	//    cannot come right, like a shader that will not compile, disables the
	//    feature for the session.
	//
	//  * Nothing is created on the render thread if it can be created earlier.
	//    Warm() compiles the shaders at load time, so the first sweep does not
	//    pay for a LoadLibrary and two shader compiles inside Present.
	//
	// Off by default. The image space route is lighter and is enough for anyone
	// not running Community Shaders.
	class PostFX
	{
	public:
		struct Params
		{
			float saturation{ 1.0f };
			float brightness{ 1.0f };
			float contrast{ 1.0f };
			float strength{ 0.0f };

			float tintR{ 0.0f };
			float tintG{ 0.0f };
			float tintB{ 0.0f };
			float tintAmount{ 0.0f };

			float vignetteInner{ 0.45f };
			float vignetteOuter{ 1.05f };
			float vignetteStrength{ 0.0f };
			float uvScaleX{ 1.0f };

			float washR{ 0.0f };
			float washG{ 0.0f };
			float washB{ 0.0f };
			float uvScaleY{ 1.0f };
		};
		static_assert(sizeof(Params) == 64, "constant buffers want 16 byte rows");

		[[nodiscard]] static PostFX* GetSingleton();

		// Compile and build everything that does not depend on the frame buffer.
		// Call once from the main thread at load; costs nothing if the feature
		// is switched off.
		void Warm();

		// Queue the pass onto an ImGui draw list. Render thread only. Does
		// nothing if the feature is off, has failed, or has nothing to do.
		void Submit(void* a_drawList, const Params& a_params);

		// Runs the pass. Called only from the ImGui draw callback, on the render
		// thread, with the finished frame bound.
		void Execute();

		// True once a pass has actually landed and nothing has gone wrong since,
		// so the ImGui vignette can stand aside rather than doubling up.
		[[nodiscard]] bool Working() const
		{
			return _ran.load(std::memory_order_relaxed) && !Failed();
		}

		// Set once something unrecoverable has happened; the feature stays off
		// for the rest of the session and the other routes take over again.
		[[nodiscard]] bool Failed() const { return _failed.load(std::memory_order_relaxed); }

		// Human readable, for the settings page.
		[[nodiscard]] std::string Status() const;

	private:
		PostFX() = default;

		bool PrepareShared(void* a_device);
		bool PrepareTarget(void* a_device, std::uint32_t a_viewFormat, const void* a_targetDesc);
		void Fail(std::string a_why);
		void Skip(const char* a_why);

		Params      _params{};
		std::string _failure;

		std::atomic_bool _failed{ false };
		std::atomic_bool _ran{ false };
		std::string      _lastSkip;
		bool             _named{ false };

		// Held as void* so this header does not drag d3d11.h into everything
		// that happens to include it.
		void*         _vs{ nullptr };
		void*         _ps{ nullptr };
		void*         _copy{ nullptr };
		void*         _srv{ nullptr };
		void*         _cb{ nullptr };
		void*         _sampler{ nullptr };
		void*         _blend{ nullptr };
		void*         _depth{ nullptr };
		void*         _raster{ nullptr };
		std::uint32_t _width{ 0 };
		std::uint32_t _height{ 0 };
		std::uint32_t _viewFormat{ 0 };
		std::uint32_t _copyFormat{ 0 };
	};
}
