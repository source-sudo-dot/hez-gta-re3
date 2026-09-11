#pragma once

// Writes the game out once a mission has been passed and the world has settled again.
//
// re3 carries the mobile version of this already, switched off with "makeing autosave is
// pointless and is a bit buggy" next to it.  Buggy because it wrote to a ninth slot that
// the load list never shows, and because it went off the moment a mission script ended,
// which is well before the world has finished tidying up after it.  This one waits, and
// writes to a slot the player can actually load.
class CAutoSave
{
public:
	// AutoSave under [Display] in re3.ini
	static bool bEnabled;

	// the script says the mission was passed; the writing waits for a quiet moment
	static void Request(void);
	static void Process(void);

private:
	static bool   m_bPending;
	static uint32 m_nEarliest;
};
