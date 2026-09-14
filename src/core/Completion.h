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
	enum { MAX_GOALS = 20 };

	struct tGoal
	{
		const char *key;
		int32 done;
		int32 total;	// 0 when the game keeps none
	};

	// fills the goals and says how many there are
	static int32 Collect(tGoal *out);
	static int32 Percent(void);
};
