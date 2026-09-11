#pragma once

// What is left before the game is finished, gathered in one place.
//
// Every figure here is one the game already keeps; nothing is counted twice over.  The
// four side jobs are the only ones without a total of their own - the script simply
// awards the progress when the threshold is passed - so those four thresholds are named
// here, and they are the fixed ones the game was built around.
class CCompletion
{
public:
	enum { MAX_GOALS = 8 };

	struct tGoal
	{
		const char *key;
		int32 done;
		int32 total;
	};

	// fills the goals and says how many there are
	static int32 Collect(tGoal *out);
	static int32 Percent(void);
};
