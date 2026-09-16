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

// The stats' own count of missions passed is not in the save, so it starts again from nothing on
// every load.  Each mission sets a variable of its own beside REGISTER_MISSION_PASSED, and those
// are saved, so the missions are counted from them, split the way the game groups them.  The side
// jobs, the other 31 of the 88, have rows of their own below.
static const int32 storyMissions[] = {
	896, 900, 904, 908,			// ken rosenberg
	916, 920, 924, 928, 932,		// colonel cortez
	940, 944, 948, 952, 956,		// ricardo diaz
	968,					// kent paul
	976, 980, 984,				// avery carrington
	1076, 1080,				// cap the collector, keep your friends close
};
static const int32 assetMissions[] = {
	992, 996, 1000, 1004,			// malibu club
	1016, 1020,				// phil cassidy
	1028, 1032, 1036, 1040,			// interglobal films
	1064, 1068, 1072,			// vercetti estate
	1088, 1092,				// print works
	1232, 1236, 1240,			// kaufman cabs
	2448,					// ice cream factory
};
static const int32 gangMissions[] = {
	1100, 1104, 1108,			// mitch baker
	1116, 1120, 1124, 1128,			// umberto robina
	1136, 1140, 1144,			// auntie poulet
	1152, 1156, 1160,			// love fist
};
static const int32 assassinations[] = { 1192, 1196, 1200, 1204, 1208 };

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

#define ADD_FLAGS(key, list) Add(out, n, key, CountFlags(list, ARRAY_SIZE(list)), ARRAY_SIZE(list))

int32
CCompletion::Collect(tGoal *out)
{
	CPlayerInfo &player = CWorld::Players[CWorld::PlayerInFocus];
	int32 n = 0;

	ADD_FLAGS("FEZ_CSY", storyMissions);
	ADD_FLAGS("FEZ_CAM", assetMissions);
	ADD_FLAGS("FEZ_CGM", gangMissions);
	ADD_FLAGS("FEZ_CCT", assassinations);
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

	ADD_FLAGS("FEZ_CIE", importLists);
	ADD_FLAGS("FEZ_CSR", streetRaces);
	ADD_FLAGS("FEZ_CSD", stadiumEvents);
	ADD_FLAGS("FEZ_CHC", chopperCheckpoints);
	ADD_FLAGS("FEZ_COR", offRoad);
	ADD_FLAGS("FEZ_CRC", rcMissions);
	Add(out, n, "FEZ_CSG", ScriptVar(VAR_SHOOTING_RANGE_DONE) != 0 ? 1 : 0, 1);

	Add(out, n, "FEZ_CPM", CStats::HighestLevelAmbulanceMission, JOB_LEVEL_FOR_COMPLETION);
	Add(out, n, "FEZ_CFF", CStats::HighestLevelFireMission, JOB_LEVEL_FOR_COMPLETION);
	Add(out, n, "FEZ_CVG", CStats::HighestLevelVigilanteMission, JOB_LEVEL_FOR_COMPLETION);
	Add(out, n, "FEZ_CPZ", ScriptVar(VAR_PIZZA_BOY_DONE) != 0 ? 1 : 0, 1);
	Add(out, n, "FEZ_CTX", ScriptVar(VAR_TAXI_FARES), 100);

	return n;
}

#undef ADD_FLAGS

// The stores that count towards the fifteen.  Twelve are watched by one script, which sets a
// variable of its own for each when it is robbed; the other three by the hardware store script,
// where one is an area rather than a point, so the middle of it is used.
const CCompletion::tMapMark CCompletion::ms_aStores[NUM_STORES] = {
	{ -859.2f, -632.7f, 6176 },
	{ -854.3f, 850.0f, 6180 },
	{ -830.4f, 741.9f, 6184 },
	{ -846.6f, -72.6f, 6188 },
	{ 379.9f, 210.2f, 6192 },
	{ 383.2f, 759.7f, 6196 },
	{ 449.7f, 781.5f, 6200 },
	{ 352.7f, 1111.3f, 6204 },
	{ 423.5f, 1039.4f, 6208 },
	{ 468.7f, 1206.6f, 6212 },
	{ -1167.5f, -613.5f, 6216 },
	{ -1192.2f, -323.7f, 6220 },
	{ 202.7f, -474.1f, 3544 },
	{ 384.05f, 1063.5f, 3548 },
	{ -967.5f, -693.2f, 3552 },
};

// Where the script starts watching each unique jump for a take off, and the variable it sets
// once the jump has been done.
const CCompletion::tMapMark CCompletion::ms_aUniqueJumps[NUM_UNIQUE_JUMPS] = {
	{ -1487.781f, -1044.546f, 3180 },
	{ -1352.695f, -755.212f, 3184 },
	{ -1216.490f, -911.833f, 3188 },
	{ -1252.139f, -1054.685f, 3192 },
	{ -1551.685f, -1075.674f, 3196 },
	{ -1595.712f, -1272.881f, 3200 },
	{ -1553.337f, -1230.952f, 3204 },
	{ -1340.022f, -998.257f, 3208 },
	{ 24.721f, 897.801f, 3212 },
	{ 317.205f, -223.201f, 3216 },
	{ -674.345f, 1162.422f, 3220 },
	{ -529.840f, 830.062f, 3224 },
	{ -839.022f, 1153.526f, 3228 },
	{ -312.447f, 1109.196f, 3232 },
	{ -1011.583f, -30.098f, 3236 },
	{ -942.702f, -114.506f, 3240 },
	{ -900.789f, 260.804f, 3244 },
	{ -1041.895f, -569.323f, 3248 },
	{ 208.993f, -963.672f, 3252 },
	{ 46.115f, -964.415f, 3256 },
	{ 435.854f, -334.321f, 3260 },
	{ 110.481f, -1230.600f, 3264 },
	{ 7.435f, -1245.895f, 3268 },
	{ 9.103f, -1326.505f, 3272 },
	{ -321.028f, -1379.498f, 3276 },
	{ -321.028f, -1276.589f, 3280 },
	{ 218.050f, -1152.000f, 3284 },
	{ 259.056f, -945.833f, 3288 },
	{ 444.500f, -118.400f, 3292 },
	{ 284.473f, -494.114f, 3296 },
	{ 370.790f, -709.863f, 3300 },
	{ 461.589f, -522.230f, 3304 },
	{ 454.105f, -504.736f, 3308 },
	{ 460.910f, -383.362f, 3312 },
	{ 259.041f, -480.608f, 3316 },
	{ -346.818f, -290.741f, 3320 },
};

bool
CCompletion::IsMarkDone(const tMapMark &mark)
{
	return ScriptVar(mark.doneVar) != 0;
}

// the same figure the stats page shows
int32
CCompletion::Percent(void)
{
	return (int32)CStats::GetPercentageProgress();
}
