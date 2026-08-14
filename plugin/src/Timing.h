#pragma once

namespace SS
{
	// ONE clock for the whole plugin.
	//
	// This exists because having a private `Now()` in each translation unit is a
	// trap: each one holds its own `static const auto epoch = steady_clock::now()`
	// initialised on first call, so the two clocks start at different moments and
	// their timestamps are not comparable. Sense stamps a label with "born at
	// 0.0, dies at 6.0" off its clock; Labels reads its own clock, gets 36.0
	// because it started ticking when the game loaded, decides every label is
	// long expired, and draws nothing. The bug scales with how long you wait
	// before the first sweep, which makes it look intermittent.
	//
	// The clock can also be stopped. Everything timed against it - the wave, the
	// name tags, the ring, the vignette, the desaturation ramp - then freezes
	// together, which is what has to happen when the game pauses behind a menu.
	// Freezing the clock is far safer than shifting every deadline by hand: with
	// one clock there is exactly one thing to get right.
	[[nodiscard]] float Now();

	// Idempotent. Safe from any thread.
	void SetClockFrozen(bool a_frozen);

	[[nodiscard]] bool ClockFrozen();

	// The same clock with the stops left in. Use this only for things measured
	// against nothing but themselves - the gap between two key presses, say,
	// where a stopped clock would read both taps at the same instant and turn
	// every single press into a double tap. Never mix the two.
	[[nodiscard]] float RealNow();
}
