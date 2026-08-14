#pragma once

namespace SS
{
	// Real desaturation, by writing the cinematic parameters the tonemapper
	// reads.
	//
	// There are three doors into the same room and they are not equally
	// reliable, so this drives two of them at once.
	//
	//  * overrideBaseData - a spare pointer on ImageSpaceManager. When it is
	//    non-null the engine sources the frame's base image space from it. This
	//    is what Photo Mode's filter tab uses and it is the polite door.
	//
	//  * currentBaseData - the live struct the current image space actually
	//    resolves from. Writing through it is blunter, but Community Shaders'
	//    own weather editor changes saturation this way and users see it happen,
	//    so it is known to reach CS's replacement tonemapper. Values are saved
	//    and put back, and the pointer is re-checked every tick because it moves
	//    when the weather or the cell changes - restoring into the wrong image
	//    space would leave someone else's grade permanently wrong.
	//
	//  * The IMAD record shipped in the esp. Kept only as an option; it has to
	//    survive the whole modifier stack and is the easiest of the three to
	//    lose. Sense owns that one.
	//
	// Which of the first two is doing the work is not obvious from the outside,
	// which is why Report() logs what the engine actually resolved.
	class Vision
	{
	public:
		[[nodiscard]] static Vision* GetSingleton();

		// Claim what we can and start the ramp. a_endsAt is on the shared clock.
		void Begin(float a_startsAt, float a_endsAt);

		// Ease toward the target and back out. Cheap; call it every tick.
		void Update();

		// Put everything back exactly as it was found.
		void End();

	private:
		Vision() = default;

		void Report();

		RE::ImageSpaceBaseData  _override{};             // handed to overrideBaseData
		RE::ImageSpaceBaseData  _base{};                 // the world's own grade
		RE::ImageSpaceBaseData* _wroteInto{ nullptr };   // currentBaseData we mutated
		RE::ImageSpaceBaseData  _restore{};              // its contents before we touched it
		float                   _startsAt{ 0.0f };
		float                   _endsAt{ 0.0f };
		bool                    _heldOverride{ false };
		bool                    _warnedTaken{ false };
		bool                    _reported{ false };
	};
}
