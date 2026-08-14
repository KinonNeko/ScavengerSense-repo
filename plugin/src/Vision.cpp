#include "Vision.h"

#include "Config.h"
#include "GameMenus.h"
#include "Timing.h"

namespace SS
{
	namespace
	{
		[[nodiscard]] float Lerp(float a_from, float a_to, float a_t)
		{
			return a_from + (a_to - a_from) * a_t;
		}

		// Ramp in quickly so the world drains of colour as the pulse leaves you,
		// then back out over the last second so it does not snap.
		[[nodiscard]] float Envelope(float a_now, float a_start, float a_end)
		{
			if (a_now <= a_start || a_now >= a_end) {
				return 0.0f;
			}

			const auto in = (a_now - a_start) / 0.35f;
			const auto out = (a_end - a_now) / 1.20f;
			return std::clamp(std::min({ in, out, 1.0f }), 0.0f, 1.0f);
		}

		// Fills the cinematic and tint fields of a_out from a_from, a_from being
		// whatever the world was doing before we interfered.
		void Grade(RE::ImageSpaceBaseData& a_out, const RE::ImageSpaceBaseData& a_from, float a_k)
		{
			const auto* settings = Settings::GetSingleton();

			// Multipliers, not absolutes, so a dark cave stays a dark cave and
			// the effect reads the same everywhere.
			a_out.cinematic.saturation = a_from.cinematic.saturation * Lerp(1.0f, settings->tintSaturation, a_k);
			a_out.cinematic.brightness = a_from.cinematic.brightness * Lerp(1.0f, settings->tintBrightness, a_k);
			a_out.cinematic.contrast = a_from.cinematic.contrast * Lerp(1.0f, settings->tintContrast, a_k);

			const auto rgb = settings->tintCastColour;
			a_out.tint.color.red = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
			a_out.tint.color.green = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
			a_out.tint.color.blue = static_cast<float>(rgb & 0xFF) / 255.0f;
			a_out.tint.amount = Lerp(a_from.tint.amount, settings->tintCastAmount, a_k);
		}
	}

	Vision* Vision::GetSingleton()
	{
		static Vision singleton;
		return std::addressof(singleton);
	}

	void Vision::Begin(float a_startsAt, float a_endsAt)
	{
		const auto* settings = Settings::GetSingleton();
		if (!settings->tintEnabled) {
			return;
		}

		auto* manager = RE::ImageSpaceManager::GetSingleton();
		if (!manager) {
			return;
		}

		_startsAt = a_startsAt;
		_endsAt = a_endsAt;

		auto* current = manager->GetCurrentBaseData();
		if (!current) {
			return;
		}

		const bool fresh = !_heldOverride && !_wroteInto;
		if (fresh) {
			_base = *current;
			_reported = false;
		}

		if (settings->tintUseOverride && !_heldOverride) {
			if (manager->GetOverrideBaseData() == nullptr) {
				_override = _base;
				manager->GetOverrideBaseData() = std::addressof(_override);
				_heldOverride = true;
				_warnedTaken = false;
			} else if (!_warnedTaken) {
				// Begin runs every tick, so this must not become sixty lines a
				// second.
				_warnedTaken = true;
				logger::warn("vision: something else already owns the image space override");
			}
		}

		if (settings->tintWriteCurrent && !_wroteInto) {
			_wroteInto = current;
			_restore = *current;
		}

		if (fresh) {
			logger::info(
				"vision: engaged (override={} writeCurrent={}) target saturation {:.2f} -> {:.2f}",
				_heldOverride, _wroteInto != nullptr,
				_base.cinematic.saturation, _base.cinematic.saturation * settings->tintSaturation);
		}
	}

	void Vision::Update()
	{
		if (!_heldOverride && !_wroteInto) {
			return;
		}

		const auto* settings = Settings::GetSingleton();

		// A menu renders through all of this, so drop back to the world's own
		// grade while one is open. Note the clock is stopped at the same time,
		// which is why this cannot simply wait for the envelope to run down.
		const auto k = (settings->menuAware && GameMenus::GetSingleton()->Blocking())
						   ? 0.0f
						   : Envelope(Now(), _startsAt, _endsAt) *
								 std::clamp(settings->tintStrength, 0.0f, 1.0f);

		if (_heldOverride) {
			_override = _base;
			Grade(_override, _base, k);
		}

		if (_wroteInto) {
			auto* manager = RE::ImageSpaceManager::GetSingleton();
			auto* current = manager ? manager->GetCurrentBaseData() : nullptr;

			// The image space changes with the weather and with every interior
			// you walk into. If it moved, put the old one back before adopting
			// the new one - otherwise the values we are holding get written into
			// whatever image space happens to be current when the wave ends, and
			// that one stays wrong until the game restarts.
			if (current != _wroteInto) {
				*_wroteInto = _restore;
				logger::info("vision: image space changed mid-sweep, restored the old one");
				if (!current) {
					_wroteInto = nullptr;
					return;
				}
				_wroteInto = current;
				_restore = *current;
				_base = *current;
			}

			Grade(*_wroteInto, _restore, k);
		}

		if (!_reported && k > 0.5f) {
			_reported = true;
			Report();
		}
	}

	void Vision::Report()
	{
		auto* manager = RE::ImageSpaceManager::GetSingleton();
		if (!manager) {
			return;
		}

		// The one measurement that says whether any of this reached the frame.
		// resolved is what the engine actually handed the tonemapper; if that
		// still reads ~1.0 while ours reads 0.25, neither door is open and the
		// answer is a different technique, not a different value.
		const auto& resolved = manager->GetImageSpaceData().baseData;
		const auto* current = manager->GetCurrentBaseData();

		logger::info(
			"vision readback: asked for saturation {:.3f} | current={:.3f} resolved={:.3f} "
			"brightness resolved={:.3f} contrast resolved={:.3f} | override held={} points at us={} | wroteInto={}",
			_heldOverride ? _override.cinematic.saturation
						  : (_wroteInto ? _wroteInto->cinematic.saturation : -1.0f),
			current ? current->cinematic.saturation : -1.0f,
			resolved.cinematic.saturation,
			resolved.cinematic.brightness,
			resolved.cinematic.contrast,
			_heldOverride,
			manager->GetOverrideBaseData() == std::addressof(_override),
			_wroteInto != nullptr);
	}

	void Vision::End()
	{
		if (_wroteInto) {
			auto* manager = RE::ImageSpaceManager::GetSingleton();
			auto* current = manager ? manager->GetCurrentBaseData() : nullptr;
			// Only put values back where we took them from.
			if (current == _wroteInto) {
				*_wroteInto = _restore;
			}
			_wroteInto = nullptr;
		}

		if (_heldOverride) {
			if (auto* manager = RE::ImageSpaceManager::GetSingleton()) {
				// If something else grabbed the pointer meanwhile, clearing it
				// would break that mod.
				if (manager->GetOverrideBaseData() == std::addressof(_override)) {
					manager->GetOverrideBaseData() = nullptr;
				}
			}
			_heldOverride = false;
		}
	}
}
