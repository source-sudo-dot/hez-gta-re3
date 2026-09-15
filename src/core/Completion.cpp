#include "common.h"
#include "Completion.h"

#include "Game.h"
#include "Lists.h"
#include "PlayerInfo.h"
#include "Script.h"
#include "Stats.h"
#include "World.h"

// Everything main.scm hands a progress point out for, 154 of them.  Where the stats keep a figure
// it is used; the rest the script keeps in variables of its own, each set in the same breath as
// the point, and those are read here by their place in the script's variable space.  The places
// are those of the main.scm Vice City shipped with.
enum
{
	JOB_LEVEL_FOR_COMPLETION = 12,

	VAR_STORES_ROBBED = 6124,	// counts to 15, then set to -1 once the point is given
	VAR_ASSETS_DONE = 4700,		// the eight businesses and the Vercetti Estate
	VAR_TAXI_FARES = 1476,
	VAR_PIZZA_BOY_DONE = 1556,
	VAR_SHOOTING_RANGE_DONE = 432,
};

static const int32 importLists[] = { 4500, 4504, 4508, 4512 };
static const int32 streetRaces[] = { 6352, 6356, 6360, 6364, 6368, 6372 };
static const int32 stadiumEvents[] = { 6388, 6392, 220 };	// hotring, bloodring, dirtring
static const int32 chopperCheckpoints[] = { 6336, 6340, 6344, 6348 };
static const int32 offRoad[] = { 1356, 1404, 1452, 1456 };
static const int32 rcMissions[] = { 32624, 32964, 33940 };

// The missions the stats count as passed, all 88 of them.  The count itself is not in the save, so
// it starts again from nothing on every load; each mission also sets a variable of its own as it
// is passed, and those are.  First the story, asset, gang and contract missions, set next to
// REGISTER_MISSION_PASSED (the ice cream factory keeps its flag apart from the others)...
static const int32 passedMissions[] = {
	896, 900, 904, 908,			// lawyer
	916, 920, 924, 928, 932,		// colonel
	940, 944, 948, 952, 956,		// diaz / baron
	968,					// kent paul
	976, 980, 984,				// sergio / texan
	992, 996, 1000, 1004,			// malibu
	1016, 1020,				// phil
	1028, 1032, 1036, 1040,			// film studio
	1064, 1068, 1072,			// protection
	1076, 1080,				// print works, the finale
	1088, 1092,				// counterfeit
	1100, 1104, 1108,			// bikers
	1116, 1120, 1124, 1128,			// cubans
	1136, 1140, 1144,			// haitians
	1152, 1156, 1160,			// love fist
	1192, 1196, 1200, 1204, 1208,		// assassinations
	1232, 1236, 1240,			// kaufman cabs
	2448,					// ice cream factory
};
// ...then the side jobs, set next to REGISTER_ODDJOB_MISSION_PASSED
static const int32 passedOddJobs[] = {
	4500, 4504, 4508, 4512,			// import/export lists
	432,					// shooting range
	1492, 6324, 6332, 6328,			// taxi, paramedic, firefighter, vigilante
	6388, 6392, 220,			// hotring, bloodring, dirtring
	6352, 6356, 6360, 6364, 6368, 6372,	// street races
	6336, 6340, 6344, 6348,			// chopper checkpoints
	1452, 1456, 1356, 1404,			// off-road
	1556,					// pizza boy
	32624, 32964, 33940,			// rc
	2428,					// boatyard
};

// the seven safehouses in the stats' property list, after the eight businesses
enum { FIRST_SAFEHOUSE = 8, NUM_SAFEHOUSES = 7 };

static int32
ScriptVar(int32 offset)
{
	if (offset < 8 || offset + 4 > CTheScripts::GetSizeOfVariableSpace())
		return 0;
	return *CTheScripts::GetPointerToScriptVariable(offset);
}

static int32
CountFlags(const int32 *offsets, int32 count)
{
	int32 done = 0;
	for (int32 i = 0; i < count; i++)
		if (ScriptVar(offsets[i]) != 0)
			done++;
	return done;
}

static void
Add(CCompletion::tGoal *out, int32 &n, const char *key, int32 done, int32 total)
{
	out[n].key = key;
	out[n].done = Clamp(done, 0, total);
	out[n].total = total;
	n++;
}

int32
CCompletion::Collect(tGoal *out)
{
	CPlayerInfo &player = CWorld::Players[CWorld::PlayerInFocus];
	int32 n = 0;

	// one count for all of them; the game keeps no figure per mission giver
	int32 missionsTotal = ARRAY_SIZE(passedMissions) + ARRAY_SIZE(passedOddJobs);
	if (CStats::TotalNumberMissions > 0)
		missionsTotal = CStats::TotalNumberMissions;
	Add(out, n, "FEZ_CMS", CountFlags(passedMissions, ARRAY_SIZE(passedMissions)) +
		CountFlags(passedOddJobs, ARRAY_SIZE(passedOddJobs)), missionsTotal);
	Add(out, n, "FEZ_CAS", ScriptVar(VAR_ASSETS_DONE), 9);
	Add(out, n, "FEZ_CHP", player.m_nCollectedPackages, player.m_nTotalPackages);

	// The rampages are not in the game at all without the blood, and the percentage leaves them
	// out of its total to match, so they are left out here as well.
	if (CGame::nastyGame)
		Add(out, n, "FEZ_CRP", CStats::NumberKillFrenziesPassed, CStats::TotalNumberKillFrenzies);

	Add(out, n, "FEZ_CUJ", CStats::NumberOfUniqueJumpsFound, CStats::TotalNumberOfUniqueJumps);

	int32 safehouses = 0;
	for (int32 i = FIRST_SAFEHOUSE; i < FIRST_SAFEHOUSE + NUM_SAFEHOUSES; i++)
		if (CStats::PropertyOwned[i])
			safehouses++;
	Add(out, n, "FEZ_CSH", safehouses, NUM_SAFEHOUSES);

	int32 stores = ScriptVar(VAR_STORES_ROBBED);
	Add(out, n, "FEZ_CST", stores < 0 ? 15 : stores, 15);

	Add(out, n, "FEZ_CIE", CountFlags(importLists, ARRAY_SIZE(importLists)), ARRAY_SIZE(importLists));
	Add(out, n, "FEZ_CSR", CountFlags(streetRaces, ARRAY_SIZE(streetRaces)), ARRAY_SIZE(streetRaces));
	Add(out, n, "FEZ_CSD", CountFlags(stadiumEvents, ARRAY_SIZE(stadiumEvents)), ARRAY_SIZE(stadiumEvents));
	Add(out, n, "FEZ_CHC", CountFlags(chopperCheckpoints, ARRAY_SIZE(chopperCheckpoints)), ARRAY_SIZE(chopperCheckpoints));
	Add(out, n, "FEZ_COR", CountFlags(offRoad, ARRAY_SIZE(offRoad)), ARRAY_SIZE(offRoad));
	Add(out, n, "FEZ_CRC", CountFlags(rcMissions, ARRAY_SIZE(rcMissions)), ARRAY_SIZE(rcMissions));
	Add(out, n, "FEZ_CSG", ScriptVar(VAR_SHOOTING_RANGE_DONE) != 0 ? 1 : 0, 1);

	Add(out, n, "FEZ_CPM", CStats::HighestLevelAmbulanceMission, JOB_LEVEL_FOR_COMPLETION);
	Add(out, n, "FEZ_CFF", CStats::HighestLevelFireMission, JOB_LEVEL_FOR_COMPLETION);
	Add(out, n, "FEZ_CVG", CStats::HighestLevelVigilanteMission, JOB_LEVEL_FOR_COMPLETION);
	Add(out, n, "FEZ_CPZ", ScriptVar(VAR_PIZZA_BOY_DONE) != 0 ? 1 : 0, 1);
	Add(out, n, "FEZ_CTX", ScriptVar(VAR_TAXI_FARES), 100);

	return n;
}

// the same figure the stats page shows
int32
CCompletion::Percent(void)
{
	return (int32)CStats::GetPercentageProgress();
}
