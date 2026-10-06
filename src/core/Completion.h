#pragma once

// What is left before the game is finished, gathered in one place.
//
// Most figures here are ones the game already keeps.  The rest - the side jobs, the
// off-road and RC missions - live only in the original main.scm's own variables, and the
// thresholds are the ones that script checks before it hands out the progress.  Those
// are only read when the script loaded is that very one; with any other they are left out.
class CCompletion
{
public:
	enum { MAX_GOALS = 16, MAX_MARKERS = 64 };

	// what a goal's swatch and its markers on the map look like
	enum eLegend {
		LEGEND_NONE,
		LEGEND_OFFROAD,
		LEGEND_RC,
		LEGEND_IMPORTEXPORT,
		LEGEND_CRANE,
		LEGEND_PACKAGE,	// drawn by the radar itself, as the squares it always was
		LEGEND_RAMPAGE,
		LEGEND_JUMP,
		LEGEND_COUNT
	};

	struct tGoal
	{
		const char *key;
		int32 done;
		int32 total;
		int32 legend;
	};

	struct tMarker
	{
		float x, y;
		int32 legend;
	};

	// fills the goals and says how many there are
	static int32 Collect(tGoal *out);
	// the places still to be visited, for the map
	static int32 CollectMarkers(tMarker *out);
	static CRGBA LegendColour(int32 legend);
	static int32 Percent(void);
};
