#pragma once
#include <map>
#include <optional>
#include <set>
#include <string>

namespace SS
{
	// Movie-local snapshots. The callbacks are the only Scaleform-specific part,
	// so restoration and reload behaviour can be checked without running Skyrim.
	class HudMask
	{
	public:
		template <class Read, class Write>
		void Apply(const void* movie, const std::set<std::string>& wanted, Read read, Write write)
		{
			if (movie != _movie) { _alphas.clear(); _movie = movie; }
			if (!movie) { return; }
			for (auto it = _alphas.begin(); it != _alphas.end();) {
				if (!wanted.contains(it->first)) {
					write(it->first, it->second);
					it = _alphas.erase(it);
				} else { ++it; }
			}
			for (const auto& path : wanted) {
				if (const auto current = read(path)) {
					_alphas.try_emplace(path, *current);
					write(path, 0.0);
				}
			}
		}
		[[nodiscard]] bool Active() const { return !_alphas.empty(); }
	private:
		const void* _movie{ nullptr };
		std::map<std::string, double> _alphas;
	};
}
