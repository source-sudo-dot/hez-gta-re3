#pragma once

// Slow motion on a button, on a meter that runs down while it is held on and fills back
// up while it is off, so it cannot simply be left running.
//
// The slowing itself is the adrenaline pill the game already has: it drops the time
// scale to a third, speeds the player's own animations back up so he moves at his normal
// pace through it, plays a sound and puts everything back when it ends.  Rather than
// build a second one of those next to it, this reaches for the same flags.
class CBulletTime
{
public:
	// BulletTime, BulletTimeSeconds and BulletTimeRecharge under [Display] in re3.ini
	static bool bEnabled;
	static float m_fDuration;
	static float m_fRecharge;

	// 0 empty, 1 full
	static float m_fCharge;
	static bool m_bActive;

	static void Init(void);
	static void Process(void);
	// drawn under the crosshair, so it is only up when the crosshair is
	static void DrawMeter(float x, float y);
};
