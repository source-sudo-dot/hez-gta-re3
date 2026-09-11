#include "common.h"
#include "Completion.h"

#include "Font.h"
#include "Frontend.h"
#include "Game.h"
#include "Messages.h"
#include "Lists.h"
#include "PlayerInfo.h"
#include "Stats.h"
#include "Text.h"
#include "World.h"

// The four side jobs carry no total: the script simply awards the progress once the
// threshold is passed and never says what it was, and a number invented here would be
// a claim the game never makes.  They are counted, not measured.

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
	out[n++].total = 0;

	out[n].key = "FEZ_CFF";
	out[n].done = CStats::FiresExtinguished;
	out[n++].total = 0;

	out[n].key = "FEZ_CVG";
	out[n].done = CStats::CriminalsCaught;
	out[n++].total = 0;

	out[n].key = "FEZ_CTX";
	out[n].done = CStats::PassengersDroppedOffWithTaxi;
	out[n++].total = 0;

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
