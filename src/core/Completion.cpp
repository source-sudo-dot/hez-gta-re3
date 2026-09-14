#include "common.h"
#include "Completion.h"

#include "Game.h"
#include "Lists.h"
#include "PlayerInfo.h"
#include "Script.h"
#include "Stats.h"
#include "World.h"

// The thresholds below are the ones main.scm checks before it hands out the progress point:
// a hundred fares, level twelve on the three emergency jobs, and one point for each of the fifteen
// properties bought.
enum
{
	TAXI_FARES_FOR_COMPLETION = 100,
	JOB_LEVEL_FOR_COMPLETION = 12,
};

// Pizza Boy keeps no figure the stats know of, and its level is set back to one each time it is
// started, so only whether it has been finished lasts.  main.scm keeps that in a variable of its
// own, set in the same breath as the progress point.
enum
{
	SCRIPT_VAR_PIZZA_BOY_DONE = 1556,
};

static bool
ScriptFlag(int32 offset)
{
	if (offset < 8 || offset + 4 > CTheScripts::GetSizeOfVariableSpace())
		return false;
	return *CTheScripts::GetPointerToScriptVariable(offset) != 0;
}

static void
Add(CCompletion::tGoal *out, int32 &n, const char *key, int32 done, int32 total)
{
	out[n].key = key;
	out[n].done = total > 0 ? Min(done, total) : done;
	out[n].total = total;
	n++;
}

int32
CCompletion::Collect(tGoal *out)
{
	CPlayerInfo &player = CWorld::Players[CWorld::PlayerInFocus];
	int32 n = 0;

	// one count for all of them; the game keeps no figure per mission giver
	Add(out, n, "FEZ_CMS", CStats::MissionsPassed, CStats::TotalNumberMissions);
	Add(out, n, "FEZ_CHP", player.m_nCollectedPackages, player.m_nTotalPackages);

	// The rampages are not in the game at all without the blood, and the percentage leaves them
	// out of its total to match, so they are left out here as well.
	if (CGame::nastyGame)
		Add(out, n, "FEZ_CRP", CStats::NumberKillFrenziesPassed, CStats::TotalNumberKillFrenzies);

	Add(out, n, "FEZ_CUJ", CStats::NumberOfUniqueJumpsFound, CStats::TotalNumberOfUniqueJumps);
	Add(out, n, "FEZ_CPR", CStats::NumPropertyOwned, CStats::TOTAL_PROPERTIES);
	Add(out, n, "FEZ_CPM", CStats::HighestLevelAmbulanceMission, JOB_LEVEL_FOR_COMPLETION);
	Add(out, n, "FEZ_CFF", CStats::HighestLevelFireMission, JOB_LEVEL_FOR_COMPLETION);
	Add(out, n, "FEZ_CVG", CStats::HighestLevelVigilanteMission, JOB_LEVEL_FOR_COMPLETION);
	Add(out, n, "FEZ_CTX", CStats::PassengersDroppedOffWithTaxi, TAXI_FARES_FOR_COMPLETION);
	Add(out, n, "FEZ_CPZ", ScriptFlag(SCRIPT_VAR_PIZZA_BOY_DONE) ? 1 : 0, 1);

	return n;
}

// the same figure the stats page shows
int32
CCompletion::Percent(void)
{
	return (int32)CStats::GetPercentageProgress();
}
