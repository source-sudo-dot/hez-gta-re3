#pragma once

// Writes the game out once a mission has been passed and the world has settled again.
//
// re3 carries the mobile version of this already, switched off with "makeing autosave is
// pointless and is a bit buggy" next to it.  Buggy because it wrote to a ninth slot that
// the load list never shows, and because it went off the moment a mission script ended,
// which is well before the world has finished tidying up after it.  This one waits, and
// writes to a slot the player can actually load.
//
// There are two of those: one for passed missions and one for everything else worth
// keeping out in the world, so collecting a package never pushes the last mission save
// out of reach.
enum eAutoSaveKind
{
	AUTOSAVE_MISSION,
	AUTOSAVE_WORLD,
	NUM_AUTOSAVE_KINDS
};

class CAutoSave
{
public:
	// AutoSave under [Display] in re3.ini
	static bool bEnabled;

	// the script says the mission was passed, or a package, jump or rampage is done;
	// the writing waits for a quiet moment
	static void Request(eAutoSaveKind kind);
	static void Process(void);
	// a new game or a loaded one: what is pending is dropped and the progress is taken
	// as it stands, so loading a game further along does not count as getting there
	static void Reset(void);

private:
	static bool   m_bPending[NUM_AUTOSAVE_KINDS];
	static uint32 m_nEarliest[NUM_AUTOSAVE_KINDS];
	static bool   m_bProgressKnown;
	static int32  m_nProgress;

	static int32 WorldProgress(void);
};
