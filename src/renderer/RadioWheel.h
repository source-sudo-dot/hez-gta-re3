#pragma once

// A ring of the radio stations, on the same button the weapon wheel uses.  The two never
// meet: the weapons are for a player on his feet and the radio for one behind a wheel.
// A tap of the button turns the radio off and taps it back to the station it was on.
class CRadioWheel
{
public:
	// RadioWheel under [Display] in re3.ini
	static bool bEnabled;
	static bool bOpen;

	static void Init(void);
	static void Process(void);
	static void Draw(void);

private:
	// the stations on the ring and where the pick sits in it
	static int32 m_aSlots[16];
	static int32 m_nSlots;
	static int32 m_nSelected;
	static float m_fOpenAmount;
	static float m_fPointX;
	static float m_fPointY;
	// what was on before the radio was turned off
	static int32 m_nLastStation;
	static uint32 m_nPressedAt;
	static bool m_bWaitingToOpen;

	static bool CanOpen(void);
	static void CollectSlots(void);
	static void Open(void);
	static void Close(bool takeSelection);
	static void ToggleOff(void);
	static void Tune(int32 station);
	static wchar *NameOf(int32 station);
};
