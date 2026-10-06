#include "common.h"
#include "Completion.h"

#include "Cranes.h"
#include "Font.h"
#include "Frontend.h"
#include "Game.h"
#include "Garages.h"
#include "Messages.h"
#include "ModelIndices.h"
#include "Pickups.h"
#include "Lists.h"
#include "PlayerInfo.h"
#include "Script.h"
#include "Stats.h"
#include "Text.h"
#include "World.h"

// The side jobs, the off-road and the RC missions are kept nowhere but in the script's
// own variables, so they are read from there - but only when the script is the one they
// were found in.  The size of its main part and the number of missions are enough to
// tell it from any other.
#define ORIGINAL_MAIN_SIZE (0x1A8FD)
#define ORIGINAL_MISSIONS  (80)

static bool
IsOriginalScript(void)
{
	return CTheScripts::MainScriptSize == ORIGINAL_MAIN_SIZE &&
		CTheScripts::NumberOfMissionScripts == ORIGINAL_MISSIONS;
}

// a global of main.scm, by its number there
static int32
ScriptVar(int32 var)
{
	return *(int32*)&CTheScripts::ScriptSpace[var * 4];
}

static int32
CountBits(uint32 bits)
{
	int32 n = 0;
	for (; bits; bits &= bits - 1)
		n++;
	return n;
}

// Where each one starts, and the variable the script sets once it is passed.  The
// off-road ones begin by getting into the car parked there, the RC ones by getting
// into the van.
static const struct {
	int32 var;
	float x, y;
} OffroadMissions[] = {
	{ 378, 1299.0f, -641.75f },	// Patriot Playground
	{ 379, 63.375f, -591.25f },	// A Ride In The Park
	{ 380, -218.5f, 263.6875f },	// Gripped!
	{ 381, 283.1875f, -566.4375f },	// Multistorey Mayhem
}, RcMissions[] = {
	{ 409, 1014.0f, -120.0f },	// Diablo Destruction
	{ 410, 1158.0f, -309.0f },	// Mafia Massacre
	{ 412, 366.0f, -1312.0f },	// Rumpo Rampage
	{ 411, -633.75f, 64.5625f },	// Casino Calamity
};

// Where each unique jump takes off, in the order of the script's own numbers for them;
// the script marks jump n found in the variable 782 + n.
#define JUMP_FOUND_VAR (783)
static const CVector2D UniqueJumps[] = {
	CVector2D(940.375f, -933.6875f),
	CVector2D(1168.9375f, -1156.875f),
	CVector2D(789.6875f, -572.25f),
	CVector2D(470.6875f, -918.375f),
	CVector2D(1231.8125f, -1129.8125f),
	CVector2D(1136.5625f, -976.8125f),
	CVector2D(1375.75f, -952.125f),
	CVector2D(-1182.4375f, 22.1875f),
	CVector2D(-1160.5625f, 105.9375f),
	CVector2D(991.6875f, -470.375f),
	CVector2D(793.125f, -929.9375f),
	CVector2D(157.5625f, -998.1875f),
	CVector2D(271.125f, -607.0f),
	CVector2D(320.1875f, -896.3125f),
	CVector2D(-994.75f, 253.5625f),
	CVector2D(-699.0f, -172.25f),
	CVector2D(-1100.5f, -847.4375f),
	CVector2D(-1375.75f, -848.625f),
	CVector2D(-1379.8125f, -625.0625f),
	CVector2D(-1177.375f, -569.875f),
};

// The script hands out a point for every car on the two lists and seven for the crane
// once it has had all its emergency vehicles.
#define IMPORT_EXPORT_CARS (16)
#define CRANE_CARS         (7)

static int32
PortlandCars(void)
{
	return CountBits(CGarages::CarTypesCollected[CGarages::GetCarsCollectedIndexForGarageType(GARAGE_COLLECTCARS_1)] & 0xFFFF);
}

static int32
ShoresideCars(void)
{
	return CountBits(CGarages::CarTypesCollected[CGarages::GetCarsCollectedIndexForGarageType(GARAGE_COLLECTCARS_2)] & 0xFFFF);
}

static int32
CraneCars(void)
{
	return CountBits(CCranes::CarsCollectedMilitaryCrane & 0x7F);
}

// The side jobs the way the script counts them: the paramedic is level twelve and then
// 35 and 70 patients over all, the firefighter twenty fires on each of the three islands,
// the vigilante twenty criminals on each island and the taxi a hundred fares.  Each
// island counts only up to its twenty.
static int32
PerIsland(int32 var1, int32 var2, int32 var3)
{
	return Min(ScriptVar(var1), 20) + Min(ScriptVar(var2), 20) + Min(ScriptVar(var3), 20);
}

int32
CCompletion::Collect(tGoal *out)
{
	CPlayerInfo &player = CWorld::Players[CWorld::PlayerInFocus];
	bool original = IsOriginalScript();
	int32 n = 0;

#define GOAL(k, d, t, l) do { out[n].key = k; out[n].done = d; out[n].total = t; out[n].legend = l; n++; } while(0)

	if (original) {
		int32 offroad = 0, rc = 0;
		for (int32 i = 0; i < (int32)ARRAY_SIZE(OffroadMissions); i++)
			if (ScriptVar(OffroadMissions[i].var)) offroad++;
		for (int32 i = 0; i < (int32)ARRAY_SIZE(RcMissions); i++)
			if (ScriptVar(RcMissions[i].var)) rc++;

		int32 extra = (int32)(ARRAY_SIZE(OffroadMissions) + ARRAY_SIZE(RcMissions));
		GOAL("FEZ_CMS", CStats::MissionsPassed - offroad - rc, CStats::TotalNumberMissions - extra, LEGEND_NONE);
		GOAL("FEZ_COR", offroad, (int32)ARRAY_SIZE(OffroadMissions), LEGEND_OFFROAD);
		GOAL("FEZ_CRC", rc, (int32)ARRAY_SIZE(RcMissions), LEGEND_RC);
	} else
		GOAL("FEZ_CMA", CStats::MissionsPassed, CStats::TotalNumberMissions, LEGEND_NONE);

	GOAL("FEZ_CHP", player.m_nCollectedPackages, player.m_nTotalPackages, LEGEND_PACKAGE);

	// The rampages are not in the game at all without the blood, and the percentage
	// leaves them out of its total to match, so they are left out here as well.
	if (CGame::nastyGame)
		GOAL("FEZ_CRP", CStats::NumberKillFrenziesPassed, CStats::TotalNumberKillFrenzies, LEGEND_RAMPAGE);

	GOAL("FEZ_CUJ", CStats::NumberOfUniqueJumpsFound, CStats::TotalNumberOfUniqueJumps, original ? LEGEND_JUMP : LEGEND_NONE);

	GOAL("FEZ_CIP", PortlandCars(), IMPORT_EXPORT_CARS, LEGEND_IMPORTEXPORT);
	GOAL("FEZ_CIS", ShoresideCars(), IMPORT_EXPORT_CARS, LEGEND_IMPORTEXPORT);
	GOAL("FEZ_CEC", CraneCars(), CRANE_CARS, LEGEND_IMPORTEXPORT);

	if (original) {
		GOAL("FEZ_CPM", Min(CStats::HighestLevelAmbulanceMission, 12), 12, LEGEND_NONE);
		GOAL("FEZ_CPS", Min(ScriptVar(1078), 70), 70, LEGEND_NONE);
		GOAL("FEZ_CFF", PerIsland(1083, 1084, 1085), 60, LEGEND_NONE);
		GOAL("FEZ_CVG", PerIsland(1075, 1076, 1077), 60, LEGEND_NONE);
		GOAL("FEZ_CTX", Min(ScriptVar(395), 100), 100, LEGEND_NONE);
	} else {
		// without the script's own counters these are all there is, and they have no end
		GOAL("FEZ_CPM", Min(CStats::HighestLevelAmbulanceMission, 12), 12, LEGEND_NONE);
		GOAL("FEZ_CFF", CStats::FiresExtinguished, 0, LEGEND_NONE);
		GOAL("FEZ_CVG", CStats::CriminalsCaught, 0, LEGEND_NONE);
		GOAL("FEZ_CTX", CStats::PassengersDroppedOffWithTaxi, 0, LEGEND_NONE);
	}

#undef GOAL
	return n;
}

int32
CCompletion::CollectMarkers(tMarker *out)
{
	int32 n = 0;

#define MARKER(mx, my, l) do { out[n].x = mx; out[n].y = my; out[n].legend = l; n++; } while(0)

	if (IsOriginalScript()) {
		for (int32 i = 0; i < (int32)ARRAY_SIZE(OffroadMissions); i++)
			if (!ScriptVar(OffroadMissions[i].var))
				MARKER(OffroadMissions[i].x, OffroadMissions[i].y, LEGEND_OFFROAD);
		for (int32 i = 0; i < (int32)ARRAY_SIZE(RcMissions); i++)
			if (!ScriptVar(RcMissions[i].var))
				MARKER(RcMissions[i].x, RcMissions[i].y, LEGEND_RC);
		for (int32 i = 0; i < (int32)ARRAY_SIZE(UniqueJumps); i++)
			if (!ScriptVar(JUMP_FOUND_VAR + i))
				MARKER(UniqueJumps[i].x, UniqueJumps[i].y, LEGEND_JUMP);
	}

	// The rampages are pickups like the packages: the script puts the skull back down
	// somewhere else when one is failed and never again once it is passed, so the skulls
	// in the list are just the ones still to do.
	for (int32 i = 0; i < NUMPICKUPS && n < MAX_MARKERS; i++)
		if (CPickups::aPickUps[i].m_eType != PICKUP_NONE && CPickups::aPickUps[i].m_eModelIndex == MI_PICKUP_KILLFRENZY)
			MARKER(CPickups::aPickUps[i].m_vecPos.x, CPickups::aPickUps[i].m_vecPos.y, LEGEND_RAMPAGE);

	// the two garages by their middles, the crane where it stands on the docks
	if (PortlandCars() < IMPORT_EXPORT_CARS)
		MARKER(1510.0f, -676.5f, LEGEND_IMPORTEXPORT);
	if (ShoresideCars() < IMPORT_EXPORT_CARS)
		MARKER(-1107.5f, 136.0f, LEGEND_IMPORTEXPORT);
	if (CraneCars() < CRANE_CARS)
		MARKER(1570.25f, -675.375f, LEGEND_IMPORTEXPORT);

#undef MARKER
	return n;
}

CRGBA
CCompletion::LegendColour(int32 legend)
{
	switch (legend) {
	case LEGEND_OFFROAD: return CRGBA(255, 150, 30, 255);
	case LEGEND_RC: return CRGBA(240, 70, 220, 255);
	case LEGEND_IMPORTEXPORT: return CRGBA(60, 210, 255, 255);
	case LEGEND_PACKAGE: return CRGBA(120, 230, 90, 255);
	case LEGEND_RAMPAGE: return CRGBA(235, 40, 40, 255);
	case LEGEND_JUMP: return CRGBA(255, 235, 60, 255);
	default: return CRGBA(255, 255, 255, 255);
	}
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
