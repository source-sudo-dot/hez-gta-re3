#include "common.h"
#include "Completion.h"

#include "Game.h"
#include "Lists.h"
#include "PlayerInfo.h"
#include "Stats.h"
#include "World.h"

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

	// The rampages are not in the game at all without the blood, and the percentage leaves them
	// out of its total to match, so they are left out here as well.
	if (CGame::nastyGame) {
		out[n].key = "FEZ_CRP";
		out[n].done = CStats::NumberKillFrenziesPassed;
		out[n++].total = CStats::TotalNumberKillFrenzies;
	}

	out[n].key = "FEZ_CUJ";
	out[n].done = CStats::NumberOfUniqueJumpsFound;
	out[n++].total = CStats::TotalNumberOfUniqueJumps;

	// Vice City's own additions.  The property count has an array behind it but no figure for how
	// many count towards the end, and the side jobs are awarded by the script once their level is
	// reached, so these are counted.
	out[n].key = "FEZ_CPR";
	out[n].done = CStats::NumPropertyOwned;
	out[n++].total = 0;

	out[n].key = "FEZ_CPM";
	out[n].done = CStats::HighestLevelAmbulanceMission;
	out[n++].total = 0;

	out[n].key = "FEZ_CFF";
	out[n].done = CStats::HighestLevelFireMission;
	out[n++].total = 0;

	out[n].key = "FEZ_CVG";
	out[n].done = CStats::HighestLevelVigilanteMission;
	out[n++].total = 0;

	out[n].key = "FEZ_CTX";
	out[n].done = CStats::PassengersDroppedOffWithTaxi;
	out[n++].total = 0;

	return n;
}

// the same figure the stats page shows
int32
CCompletion::Percent(void)
{
	return (int32)CStats::GetPercentageProgress();
}
