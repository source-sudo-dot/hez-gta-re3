#include "common.h"
#include "AutoSave.h"

#include "crossplatform.h"
#include "Camera.h"
#include "Completion.h"
#include "CutsceneMgr.h"
#include "Frontend.h"
#include "GenericGameStorage.h"
#include "Hud.h"
#include "PCSave.h"
#include "Pad.h"
#include "Lists.h"
#include "PlayerInfo.h"
#include "PlayerPed.h"
#include "Script.h"
#include "Text.h"
#include "Timer.h"
#include "Wanted.h"
#include "World.h"

bool   CAutoSave::bEnabled = true;
bool   CAutoSave::bProgressEnabled = true;
bool   CAutoSave::m_bPending = false;
uint32 CAutoSave::m_nEarliest = 0;
bool   CAutoSave::m_bProgressPending = false;
uint32 CAutoSave::m_nProgressEarliest = 0;

// A mission reports itself passed a good few lines before it has finished with the world, so the
// state is given a moment to settle before it is written.
#define SETTLE_MS (2500)

void
CAutoSave::Request(void)
{
	if (!bEnabled)
		return;

	m_bPending = true;
	m_nEarliest = CTimer::GetTimeInMilliseconds() + SETTLE_MS;
}

// The rows of the progress list that are missions; their progress is the mission autosave's.
static bool
IsMissionRow(const char *key)
{
	static const char *missionRows[] = { "FEZ_CSY", "FEZ_CAM", "FEZ_CGM", "FEZ_CCT", "FEZ_CAS" };
	for (int i = 0; i < ARRAY_SIZE(missionRows); i++)
		if (strcmp(key, missionRows[i]) == 0)
			return true;
	return false;
}

// Every logical frame of play the progress list is held against the one before.  Any row off the
// missions that went up asks for the second autosave.  Whenever the game is not being played (the
// menu, a load, a new game) what was held is let go and taken again on the first frame back, so a
// loaded game is never taken for progress.
void
CAutoSave::WatchProgress(void)
{
	static CCompletion::tGoal last[CCompletion::MAX_GOALS];
	static int32 lastCount = -1;

	if (gGameState != GS_PLAYING_GAME || FrontEndMenuManager.GetIsMenuActive()) {
		lastCount = -1;
		return;
	}
	if (CTimer::GetIsPaused())
		return;

	CCompletion::tGoal now[CCompletion::MAX_GOALS];
	int32 count = CCompletion::Collect(now);

	if (count == lastCount) {
		for (int32 i = 0; i < count; i++) {
			if (IsMissionRow(now[i].key) || strcmp(now[i].key, last[i].key) != 0)
				continue;
			if (now[i].done > last[i].done) {
				// which row set it off, to reVC_autosave.log while this is looked into
				{
					FILE *f = fopen("reVC_autosave.log", "a");
					if (f) {
						fprintf(f, "autosave progress t=%u %s %d->%d/%d pending=%d\n", CTimer::GetTimeInMilliseconds(),
							now[i].key, last[i].done, now[i].done, now[i].total, m_bProgressPending);
						fclose(f);
					}
				}
				m_bProgressPending = true;
				m_nProgressEarliest = CTimer::GetTimeInMilliseconds() + SETTLE_MS;
			}
		}
	}

	for (int32 i = 0; i < count; i++)
		last[i] = now[i];
	lastCount = count;
}

// Whether the game is the player's to play and quiet enough to be written out.
static bool
IsQuietMoment(void)
{
	// Nothing is written while the game is not the player's to play: another mission running, a
	// cutscene, the menu up, or the player dead or busted.
	if (gGameState != GS_PLAYING_GAME || CTheScripts::IsPlayerOnAMission())
		return false;
	if (CCutsceneMgr::IsRunning() || TheCamera.m_WideScreenOn)
		return false;
	if (FrontEndMenuManager.GetIsMenuActive() || CTimer::GetIsPaused())
		return false;

	CPlayerInfo *info = &CWorld::Players[CWorld::PlayerInFocus];
	if (info->m_pPed == nil || info->m_pPed->m_nPedState == PED_DEAD || info->m_pPed->m_nPedState == PED_DIE)
		return false;
	if (info->m_WBState != WBSTATE_PLAYING)
		return false;
	return true;
}

bool
CAutoSave::Save(int slot, const char *doneKey)
{
#ifdef MISSION_REPLAY
	// A save made from the menu is the player sleeping it off: hours pass and he is healed.  This
	// one is not, so it goes in as a quick save, which is the flag that leaves all of that alone.
	IsQuickSave = SAVE_TYPE_QUICKSAVE;
#endif
	int8 result = PcSaveHelper.SaveSlot(slot);
	// SaveSlot hands back 0 both on success and when the file could not be made, so the error
	// code is what tells them apart
	bool saved = result == 0 && PcSaveHelper.nErrorCode == SAVESTATUS_SUCCESSFUL;
#ifdef MISSION_REPLAY
	IsQuickSave = 0;
#endif

	PcSaveHelper.PopulateSlotInfo();

	if (saved)
		CHud::SetHelpMessage(TheText.Get(doneKey), false);
	return saved;
}

void
CAutoSave::Process(void)
{
	if (bProgressEnabled)
		WatchProgress();
	else
		m_bProgressPending = false;

	if (!bEnabled)
		m_bPending = false;

	uint32 now = CTimer::GetTimeInMilliseconds();

	if (m_bPending && now >= m_nEarliest && IsQuietMoment()) {
		m_bPending = false;
		Save(AUTOSAVE_SLOT, "FEZ_ASD");
		return;
	}

	// Progress off a mission comes in the middle of things: in the air off a jump, running from a
	// store.  The game puts the player back where the save was made, so it waits until he is on his
	// feet or in a vehicle and hardly moving.  The save does not keep the wanted level, so a load
	// would shake off the police for free: it also waits until they have given up.
	if (m_bProgressPending && now >= m_nProgressEarliest && IsQuietMoment()) {
		CPlayerPed *ped = FindPlayerPed();
		if (ped->m_pWanted->GetWantedLevel() > 0)
			return;
		if (!ped->bInVehicle && (!ped->bIsStanding || ped->bIsInTheAir))
			return;
		if (FindPlayerSpeed().Magnitude() > 0.05f)
			return;
		m_bProgressPending = false;
		bool saved = Save(PROGRESS_AUTOSAVE_SLOT, "FEZ_ASD");
		{
			FILE *f = fopen("reVC_autosave.log", "a");
			if (f) {
				fprintf(f, "autosave progress written t=%u ok=%d\n", CTimer::GetTimeInMilliseconds(), saved);
				fclose(f);
			}
		}
	}
}
