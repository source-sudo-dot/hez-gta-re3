#include "common.h"
#include "AutoSave.h"

#include "crossplatform.h"
#include "Completion.h"
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
bool   CAutoSave::m_bPending[NUM_AUTOSAVE_KINDS];
uint32 CAutoSave::m_nEarliest[NUM_AUTOSAVE_KINDS];
bool   CAutoSave::m_bProgressKnown = false;
int32  CAutoSave::m_nProgress = 0;

// A mission reports itself passed a good few lines before it has finished with the
// world, so the state is given a moment to settle before it is written.  A hidden
// package, a unique jump or a rampage asks for the other slot the same way, and so
// do the side jobs - taxi, ambulance, fire truck, vigilante.  Those never report
// themselves passed, but every fare, patient, fire or criminal does report in; the
// request stands until the job is over, so it comes to one save at its end.
#define SETTLE_MS (2500)

void
CAutoSave::Request(eAutoSaveKind kind)
{
	if (!bEnabled)
		return;

	m_bPending[kind] = true;
	m_nEarliest[kind] = CTimer::GetTimeInMilliseconds() + SETTLE_MS;
}

void
CAutoSave::Reset(void)
{
	for (int i = 0; i < NUM_AUTOSAVE_KINDS; i++)
		m_bPending[i] = false;
	m_bProgressKnown = false;
}

// Everything on the progress list but the story missions, added up.  The missions have
// the other slot; all the rest - packages, rampages, jumps, off-road, RC, the cars for
// the garages and the crane, the side jobs - is the world, and whenever this sum goes
// up, the world slot is written.  Rather than every one of them being hooked where it
// happens, which missed the rampages: those are passed by the engine, not the script.
int32
CAutoSave::WorldProgress(void)
{
	CCompletion::tGoal goals[CCompletion::MAX_GOALS];
	int32 n = CCompletion::Collect(goals);
	int32 sum = 0;
	for (int32 i = 0; i < n; i++)
		if (strcmp(goals[i].key, "FEZ_CMS") != 0 && strcmp(goals[i].key, "FEZ_CMA") != 0)
			sum += goals[i].done;
	return sum;
}

void
CAutoSave::Process(void)
{
	if (!bEnabled) {
		for (int i = 0; i < NUM_AUTOSAVE_KINDS; i++)
			m_bPending[i] = false;
		m_bProgressKnown = false;
		return;
	}

	int32 progress = WorldProgress();
	if (m_bProgressKnown && progress > m_nProgress)
		Request(AUTOSAVE_WORLD);
	m_nProgress = progress;
	m_bProgressKnown = true;

	// the mission one goes first should both be waiting; the other follows a frame later
	int kind;
	for (kind = 0; kind < NUM_AUTOSAVE_KINDS; kind++)
		if (m_bPending[kind] && CTimer::GetTimeInMilliseconds() >= m_nEarliest[kind])
			break;
	if (kind == NUM_AUTOSAVE_KINDS)
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

	// Only with the player on his feet and under control.  A save does keep a player who
	// is sitting in a car, and the car with him, but loading never puts him back behind the
	// wheel: CPed::Load reads his position and nothing about a vehicle, CVehicle::Load reads
	// no driver, and nothing after either seats him again - so he would stand where the seat
	// was, inside the car.  IsPedInControl is false for driving, riding, getting in and out,
	// jumping and falling alike.  The request waits until he is out.
	if (info->m_pPed->bInVehicle || !info->m_pPed->IsPedInControl())
		return;

#ifdef MISSION_REPLAY
	// the game's own save between missions keeps out of a mission retry, and so does this
	if (AllowMissionReplay != MISSION_RETRY_STAGE_NORMAL)
		return;
#endif

	m_bPending[kind] = false;

#ifdef MISSION_REPLAY
	// A save made from the menu is the player sleeping it off: six hours pass and his
	// stamina comes back.  This one is not, so it goes in as a quick save, which is the
	// flag that leaves all of that alone.
	IsQuickSave = SAVE_TYPE_QUICKSAVE;
#endif
	bool saved = PcSaveHelper.SaveSlot(kind == AUTOSAVE_MISSION ? AUTOSAVE_SLOT : AUTOSAVE_WORLD_SLOT);
#ifdef MISSION_REPLAY
	IsQuickSave = 0;
#endif

	PcSaveHelper.PopulateSlotInfo();

	if (saved)
		CHud::SetHelpMessage(TheText.Get("FEZ_ASD"), false);
}
