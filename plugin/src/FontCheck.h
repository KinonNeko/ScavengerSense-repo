#pragma once

namespace SS
{
	// Does the menu framework have the glyphs we are about to hand it?
	//
	// A font can hold every character a language needs and still draw none of
	// them: SKSE Menu Framework builds its atlas from a set of switches in its
	// own INI, and the ones for non-Latin scripts are off out of the box. Turn
	// the mod's menu to Chinese without touching that file and every name comes
	// out as mojibake - which looks like a text-encoding fault, not like a
	// missing line in somebody else's config, and is why it took a long hunt
	// through code pages, fonts and input methods to find.
	//
	// This reads that file and says so. It never writes to it: the same file
	// holds the player's own menu key and theme, and none of that is ours.
	void CheckMenuFont();

	// The same finding, said on screen once the player is somewhere to read
	// it. Nothing is drawn at data load, so the warning would be lost there.
	void SayMenuFontWarning();
}
