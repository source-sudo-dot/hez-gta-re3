#include "common.h"
#include "Completion.h"

#include "Font.h"
#include "Frontend.h"
#include "Game.h"
#include "Messages.h"
#include "PlayerInfo.h"
#include "Stats.h"
#include "Text.h"
#include "World.h"

// What the four side jobs ask for.  They are the only goals the game holds no total for.
#define PARAMEDIC_LEVELS (12)
#define FIRES_TO_PUT_OUT (20)
#define CRIMINALS_TO_CATCH (20)
#define TAXI_FARES (100)

int32
CCompletion::Collect(tGoal *out)
{
	CPlayerInfo &player = CWorld::Players[CWorld::PlayerInFocus];
	int32 n = 0;

	out[n].key = "FEZ_CMS";
	out[n].done = CStats::MissionsPassed;
	out[n++].total = CStats::TotalNumberMissions;

	out[n].key = "FEZ_CHP";
	out[n].done = player.m_nCollectedPackages;
	out[n++].total = player.m_nTotalPackages;

	// The rampages are not in the game at all without the blood, and the percentage
	// leaves them out of its total to match, so they are left out here as well.
	if (CGame::nastyGame) {
		out[n].key = "FEZ_CRP";
		out[n].done = CStats::NumberKillFrenziesPassed;
		out[n++].total = CStats::TotalNumberKillFrenzies;
	}

	out[n].key = "FEZ_CUJ";
	out[n].done = CStats::NumberOfUniqueJumpsFound;
	out[n++].total = CStats::TotalNumberOfUniqueJumps;

	out[n].key = "FEZ_CPM";
	out[n].done = CStats::HighestLevelAmbulanceMission;
	out[n++].total = PARAMEDIC_LEVELS;

	out[n].key = "FEZ_CFF";
	out[n].done = CStats::FiresExtinguished;
	out[n++].total = FIRES_TO_PUT_OUT;

	out[n].key = "FEZ_CVG";
	out[n].done = CStats::CriminalsCaught;
	out[n++].total = CRIMINALS_TO_CATCH;

	out[n].key = "FEZ_CTX";
	out[n].done = CStats::PassengersDroppedOffWithTaxi;
	out[n++].total = TAXI_FARES;

	return n;
}

int32
CCompletion::Percent(void)
{
	if (CStats::TotalProgressInGame == 0)
		return 0;

	// The same sum the stats page makes: without the blood the rampages are gone and the
	// total is one short of what the script set.
	int32 total = CGame::nastyGame ? CStats::TotalProgressInGame : CStats::TotalProgressInGame - 1;
	return Min((int32)(CStats::ProgressMade * 100.0f / total), 100);
}
