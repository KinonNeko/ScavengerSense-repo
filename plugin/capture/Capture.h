#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace SSC
{
	struct Pixels
	{
		unsigned width{}, height{};
		std::vector<std::uint8_t> rgba;
	};

	// Every immediate-context operation stays on the rendering thread. Only
	// the CPU-owned Pixels value is passed to the PNG worker.
	class Readback
	{
	public:
		void Queue(ID3D11DeviceContext* context, ID3D11Texture2D* source);
		std::optional<Pixels> Poll(ID3D11DeviceContext* context);
		bool Pending() const { return _staging != nullptr; }
		void Reset() { _staging.Reset(); _query.Reset(); }
	private:
		Microsoft::WRL::ComPtr<ID3D11Texture2D> _staging;
		Microsoft::WRL::ComPtr<ID3D11Query> _query;
		bool _bgra{};
	};

	void SavePNG(const std::filesystem::path& path, const Pixels& pixels);
	std::filesystem::path UniquePNG(const std::filesystem::path& directory);
	void Require(HRESULT result, const char* operation);
}
