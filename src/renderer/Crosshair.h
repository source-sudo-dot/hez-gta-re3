#pragma once

// A drawn reticle in place of the hud sprite, with a marker that flashes when a shot
// lands.  White for a hit, amber for a head shot, red for a kill.
class CCrosshair
{
public:
	// ModernCrosshair under [Display] in re3.ini
	static bool bModern;

	// called from the weapon code when one of the player's shots lands on a ped
	static void RegisterHit(bool headShot);
	// called from CDarkel where every kill by the player already passes through
	static void RegisterKill(bool headShot);

	static void Draw(float x, float y, float size);

private:
	// seconds left on the marker, and what it should look like
	static float m_fMarkerTime;
	static int32 m_nMarkerKind;
};
