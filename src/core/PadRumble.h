#pragma once

// The rumble motors.  re3 only ever drove them through XInput, which this build does not
// use and a DualSense is not on anyway - Windows hands one out as a plain HID device.  So
// the report goes to it directly.  Everything here is a no-op where that does not apply.
class CPadRumble
{
public:
	// once a frame, with what the game is asking of the two motors, 0 to 255
	static void Update(uint8 left, uint8 right);
	static void Shutdown(void);
	// one line for the StickDebug overlay: whether a pad was found, what report it
	// takes, and what the last write came back with
	static void DebugLine(char *out);
};
