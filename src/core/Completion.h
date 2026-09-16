#pragma once

// What is left before the game is finished, gathered in one place and shown small in the corner
// of the map.
//
// Every figure here is one the game already keeps.  Where the game keeps no total of its own the
// figure is counted rather than measured, since a number invented here would be a claim the game
// never makes.
class CCompletion
{
public:
	enum { MAX_GOALS = 24, MAX_MAP_MARKS = 256 };

	// what a place on the map stands for, and so its colour; the progress rows name theirs too
	enum eMarkKind
	{
		MARK_NONE = -1,
		MARK_PACKAGE,
		MARK_RAMPAGE,
		MARK_UNIQUE_JUMP,
		MARK_SAFEHOUSE,
		MARK_STORE,
		MARK_IMPORT_EXPORT,
		MARK_STREET_RACE,
		MARK_STADIUM,
		MARK_CHOPPER,
		MARK_OFF_ROAD,
		MARK_RC,
		MARK_SHOOTING_RANGE,
		MARK_PIZZA,
		NUM_MARK_KINDS
	};

	struct tGoal
	{
		const char *key;
		int32 done;
		int32 total;	// 0 when the game keeps none
		int8 markKind;	// MARK_NONE when it has no places on the map
	};

	struct tMapMark
	{
		float x, y;
		int8 kind;
	};

	// fills the goals and says how many there are
	static int32 Collect(tGoal *out);
	static int32 Percent(void);

	// every place still to be done, for the map's square toggle
	static int32 CollectMapMarks(tMapMark *out, int32 max);
	static void MarkColour(int32 kind, uint8 &r, uint8 &g, uint8 &b);
};
