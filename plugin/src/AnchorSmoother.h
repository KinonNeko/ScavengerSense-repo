#pragma once

#include <cmath>
#include <cstdint>
#include <iterator>
#include <unordered_map>

namespace SS
{
	struct AnchorPoint { float x{}, y{}; };

	// Render-thread state only. Bypassing smoothing also forgets the old anchor,
	// so changing scope never resumes a stale, lagging position.
	class AnchorSmoother
	{
	public:
		AnchorPoint Update(std::uint32_t owner, AnchorPoint at, float now, float tau)
		{
			if (tau <= .001f || owner == 0) {
				_states.erase(owner);
				return at;
			}
			auto& state = _states[owner];
			const float dt = now - state.seen;
			state.seen = now;
			const float dx = at.x - state.at.x, dy = at.y - state.at.y;
			if (dt <= 0 || dt > .5f || dx * dx + dy * dy > 240.f * 240.f) {
				state.at = at;
			} else {
				const float k = 1.f - std::exp(-dt / tau);
				state.at.x += dx * k;
				state.at.y += dy * k;
			}
			return state.at;
		}

		void Prune(float now)
		{
			if (now - _lastPrune < 5.f) { return; }
			_lastPrune = now;
			for (auto it = _states.begin(); it != _states.end();) {
				it = now - it->second.seen > 3.f ? _states.erase(it) : std::next(it);
			}
		}
	private:
		struct State { AnchorPoint at{}; float seen{ -1000.f }; };
		std::unordered_map<std::uint32_t, State> _states;
		float _lastPrune{ -1000.f };
	};
}
