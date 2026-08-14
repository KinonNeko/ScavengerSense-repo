#pragma once

namespace SS::Menu
{
	// Registers the Scavenger Sense page with SKSE Menu Framework.
	// Returns false if the framework did not answer, in which case the plugin
	// keeps working off the INI alone.
	bool Register();
}
