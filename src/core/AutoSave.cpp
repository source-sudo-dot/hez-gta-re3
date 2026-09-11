#include "common.h"
#include "AutoSave.h"

#include "Camera.h"
#include "CutsceneMgr.h"
#include "Frontend.h"
#include "Game.h"
#include "GenericGameStorage.h"
#include "Hud.h"
#include "PCSave.h"
#include "PlayerInfo.h"
#include "PlayerPed.h"
#include "Script.h"
#include "Stats.h"
#include "Text.h"
#include "Timer.h"
#include "World.h"

bool   CAutoSave::bEnabled = true;
bool   CAutoSave::m_bPending = false;
uint32 CAutoSave::m_nEarliest = 0;

// The last of the eight the player sees.  The mobile version wrote to a ninth one kept
// for the mission replay, which no load list shows, so its saves could not be loaded.
#define AUTOSAVE_SLOT (SLOT_COUNT - 1)

// A mission reports itself passed a good few lines before it has finished with the
// world, so the state is given a moment to settle before it is written.
#define SETTLE_MS (2500)

void
CAutoSave::Request(void)
{
	if (!bEnabled)
		return;

	m_bPending = true;
	m_nEarliest = CTimer::GetTimeInMilliseconds() + SETTLE_MS;
}

void
CAutoSave::Process(void)
{
	if (!m_bPending)
		return;

	if (!bEnabled) {
		m_bPending = false;
		return;
	}

	if (CTimer::GetTimeInMilliseconds() < m_nEarliest)
		return;

	// Nothing is written while the game is not the player's to play: another mission
	// running, a cutscene, the menu up, or the player dead or in the back of a car.
	if (gGameState != GS_PLAYING_GAME || CTheScripts::IsPlayerOnAMission())
		return;
	if (CCutsceneMgr::IsRunning() || TheCamera.m_WideScreenOn)
		return;
	if (FrontEndMenuManager.GetIsMenuActive() || CTimer::GetIsPaused())
		return;

	CPlayerInfo *info = &CWorld::Players[CWorld::PlayerInFocus];
	if (info->m_pPed == nil || info->m_pPed->GetPedState() == PED_DEAD)
		return;
	if (info->m_WBState != WBSTATE_PLAYING)
		return;

	m_bPending = false;

#ifdef MISSION_REPLAY
	// A save made from the menu is the player sleeping it off: six hours pass and his
	// stamina comes back.  This one is not, so it goes in as a quick save, which is the
	// flag that leaves all of that alone.
	IsQuickSave = SAVE_TYPE_QUICKSAVE;
#endif
	bool saved = PcSaveHelper.SaveSlot(AUTOSAVE_SLOT);
#ifdef MISSION_REPLAY
	IsQuickSave = 0;
#endif

	PcSaveHelper.PopulateSlotInfo();

	if (saved)
		CHud::SetHelpMessage(TheText.Get("FEZ_ASD"), false);
}
