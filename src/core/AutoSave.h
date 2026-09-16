#pragma once

// Writes the game out once a mission has been passed and the world has settled again, into a
// slot of its own that the load list shows and no save can write over.
class CAutoSave
{
public:
	// AutoSave under [Display] in reVC.ini
	static bool bEnabled;
	// ProgressAutoSave under [Display]: the second slot, written after progress off missions
	static bool bProgressEnabled;

	// the script says the mission was passed; the writing waits for a quiet moment
	static void Request(void);
	static void Process(void);

private:
	static void WatchProgress(void);
	static bool Save(int slot, const char *doneKey);

	static bool   m_bPending;
	static uint32 m_nEarliest;
	static bool   m_bProgressPending;
	static uint32 m_nProgressEarliest;
};
