#pragma once

#include "WeaponType.h"

// A ring of the weapons the player is carrying, held open with one button.
// The game is stopped while it is up, so Process() is called before the paused
// check in CGame::Process() and Draw() runs with the rest of the 2D stuff.
class CWeaponWheel
{
public:
	// WeaponWheel under [Display] in re3.ini
	static bool bEnabled;
	static bool bOpen;

	static void Init(void);
	static void Process(void);
	static void Draw(void);

private:
	// the weapons on the ring, in inventory order, and where the pick sits in it
	static int32 m_aSlots[WEAPONTYPE_TOTAL_INVENTORY_WEAPONS];
	static int32 m_nSlots;
	static int32 m_nSelected;
	// how far the ring is open, 0 to 1, so it grows and fades instead of popping up
	static float m_fOpenAmount;
	// where the stick or the mouse is pointing, in screen directions
	static float m_fPointX;
	static float m_fPointY;
	// a tap puts the weapon away and taps it back out, so what was put away is kept
	static int32 m_nStowedWeapon;
	static uint32 m_nPressedAt;
	static bool m_bWaitingToOpen;

	static bool CanOpen(void);
	static void CollectSlots(void);
	static void Open(void);
	static void Close(bool takeSelection);
	static void ToggleStow(void);
};
