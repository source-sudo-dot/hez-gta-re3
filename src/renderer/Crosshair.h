#pragma once

// A drawn reticle in place of the hud sprite, with a marker that flashes when a shot
// lands.  White for a hit, amber for a head shot, red for a kill.
class CCrosshair
{
public:
	// ModernCrosshair under [Display] in re3.ini
	static bool bModern;
	// HitMarkers under [Display] in re3.ini
	static bool bHitMarkers;
	// radius in the units the rest of the hud is laid out in, CrosshairSize
	static float m_fSize;

	// called from the weapon code when one of the player's shots lands on a ped
	static void RegisterHit(bool headShot);
	// called from CDarkel where every kill by the player already passes through
	static void RegisterKill(bool headShot);

	// scale is 1 for a pistol, a little more for the rifle sight
	static void Draw(float x, float y, float scale);

private:
	// seconds left on the marker.  The shape says whether it was a head shot, the
	// colour whether it was a kill, so the two read apart from each other.
	static float m_fMarkerTime;
	static bool m_bHeadShot;
	static bool m_bKill;
};
