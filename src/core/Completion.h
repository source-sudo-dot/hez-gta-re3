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
	enum { MAX_GOALS = 24 };

	struct tGoal
	{
		const char *key;
		int32 done;
		int32 total;	// 0 when the game keeps none
	};

	// fills the goals and says how many there are
	static int32 Collect(tGoal *out);
	static int32 Percent(void);

	// Places on the map still to be done, for the map's square toggle: where it is, and the
	// script variable that is set once it is done.
	struct tMapMark
	{
		float x, y;
		int32 doneVar;
	};
	enum { NUM_STORES = 15, NUM_UNIQUE_JUMPS = 36 };
	static const tMapMark ms_aStores[NUM_STORES];
	static const tMapMark ms_aUniqueJumps[NUM_UNIQUE_JUMPS];
	static bool IsMarkDone(const tMapMark &mark);
};
