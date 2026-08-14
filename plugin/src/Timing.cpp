#include "Timing.h"

namespace SS
{
	namespace
	{
		[[nodiscard]] float RawNow()
		{
			static const auto epoch = std::chrono::steady_clock::now();
			return std::chrono::duration<float>(std::chrono::steady_clock::now() - epoch).count();
		}

		std::mutex g_lock;
		float      g_frozenTotal{ 0.0f };   // seconds spent stopped, all time
		float      g_frozenSince{ -1.0f };  // raw time the current stop began, -1 = running
	}

	float Now()
	{
		// Contended by the render thread and the wave thread once a frame each,
		// which is nothing. Correctness is worth more than the lock here.
		std::scoped_lock guard{ g_lock };
		const auto raw = g_frozenSince >= 0.0f ? g_frozenSince : RawNow();
		return raw - g_frozenTotal;
	}

	void SetClockFrozen(bool a_frozen)
	{
		std::scoped_lock guard{ g_lock };

		if (a_frozen) {
			if (g_frozenSince < 0.0f) {
				g_frozenSince = RawNow();
			}
			return;
		}

		if (g_frozenSince >= 0.0f) {
			g_frozenTotal += RawNow() - g_frozenSince;
			g_frozenSince = -1.0f;
		}
	}

	float RealNow()
	{
		return RawNow();
	}

	bool ClockFrozen()
	{
		std::scoped_lock guard{ g_lock };
		return g_frozenSince >= 0.0f;
	}
}
