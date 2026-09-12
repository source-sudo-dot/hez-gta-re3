#include "common.h"
#include "PadRumble.h"

#ifdef _WIN32

#include <windows.h>
extern "C" {
#include <hidsdi.h>
}
#include <setupapi.h>

// Sony, and the two pads that take this report
#define SONY_VENDOR_ID   (0x054C)
#define DUALSENSE_ID     (0x0CE6)
#define DUALSENSE_EDGE_ID (0x0DF2)

// The pad answers on one of two output reports depending on how it is plugged in, and
// the length is what tells the two apart.
#define REPORT_LENGTH_USB (48)
#define REPORT_LENGTH_BT  (78)

// validFlag0: the plain two motor rumble, and taking the haptics away from them
#define FLAG0_COMPATIBLE_VIBRATION (0x01)
#define FLAG0_HAPTICS_SELECT       (0x02)

static HANDLE     gDevice = INVALID_HANDLE_VALUE;
static USHORT     gReportLength = 0;
static OVERLAPPED gWrite;
static bool       gWritePending = false;
static uint8      gLastLeft = 0;
static uint8      gLastRight = 0;
static uint32     gNextLookup = 0;
static uint8      gBuffer[REPORT_LENGTH_BT];
static uint32     gLastError = 0;
static int32      gWrites = 0;

// The bluetooth report is signed with a crc32 of everything before it, with an 0xA2
// seed byte in front standing for "this is an output report".
static uint32
Crc32(const uint8 *data, int32 length, uint32 crc)
{
	crc = ~crc;
	for (int32 i = 0; i < length; i++) {
		crc ^= data[i];
		for (int32 bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xEDB88320 & (uint32)(-(int32)(crc & 1)));
	}
	return ~crc;
}

static void
CloseDevice(void)
{
	if (gDevice != INVALID_HANDLE_VALUE) {
		CancelIo(gDevice);
		CloseHandle(gDevice);
		gDevice = INVALID_HANDLE_VALUE;
	}
	if (gWrite.hEvent != nil) {
		CloseHandle(gWrite.hEvent);
		gWrite.hEvent = nil;
	}
	gWritePending = false;
	gReportLength = 0;
}

// Walk the hid interfaces looking for a pad we know.  Only tried now and then, so
// plugging one in part way through picks it up without the miss costing anything.
static void
OpenDevice(void)
{
	GUID hidGuid;
	HidD_GetHidGuid(&hidGuid);

	HDEVINFO devs = SetupDiGetClassDevsA(&hidGuid, nil, nil, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (devs == INVALID_HANDLE_VALUE)
		return;

	SP_DEVICE_INTERFACE_DATA iface;
	iface.cbSize = sizeof(iface);

	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devs, nil, &hidGuid, i, &iface); i++) {
		DWORD needed = 0;
		SetupDiGetDeviceInterfaceDetailA(devs, &iface, nil, 0, &needed, nil);
		if (needed == 0 || needed > 1024)
			continue;

		uint8 detailBuf[1024];
		SP_DEVICE_INTERFACE_DETAIL_DATA_A *detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_A *)detailBuf;
		detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
		if (!SetupDiGetDeviceInterfaceDetailA(devs, &iface, detail, needed, nil, nil))
			continue;

		HANDLE h = CreateFileA(detail->DevicePath, GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nil, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nil);
		if (h == INVALID_HANDLE_VALUE)
			continue;

		HIDD_ATTRIBUTES attr;
		attr.Size = sizeof(attr);
		if (!HidD_GetAttributes(h, &attr) || attr.VendorID != SONY_VENDOR_ID ||
			(attr.ProductID != DUALSENSE_ID && attr.ProductID != DUALSENSE_EDGE_ID)) {
			CloseHandle(h);
			continue;
		}

		// A pad puts up more than one interface; the one that takes the report is the
		// one whose output report is the right size.
		PHIDP_PREPARSED_DATA preparsed = nil;
		USHORT length = 0;
		if (HidD_GetPreparsedData(h, &preparsed)) {
			HIDP_CAPS caps;
			if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS)
				length = caps.OutputReportByteLength;
			HidD_FreePreparsedData(preparsed);
		}

		if (length != REPORT_LENGTH_USB && length != REPORT_LENGTH_BT) {
			CloseHandle(h);
			continue;
		}

		memset(&gWrite, 0, sizeof(gWrite));
		gWrite.hEvent = CreateEventA(nil, TRUE, FALSE, nil);
		gDevice = h;
		gReportLength = length;
		break;
	}

	SetupDiDestroyDeviceInfoList(devs);
}

void
CPadRumble::Update(uint8 left, uint8 right)
{
	if (gDevice == INVALID_HANDLE_VALUE) {
		// nothing to say and no pad found, so do not go looking on every frame
		if (left == 0 && right == 0)
			return;
		uint32 now = GetTickCount();
		if (now < gNextLookup)
			return;
		gNextLookup = now + 2000;
		OpenDevice();
		if (gDevice == INVALID_HANDLE_VALUE)
			return;
	}

	if (gWritePending) {
		DWORD written;
		if (!GetOverlappedResult(gDevice, &gWrite, &written, FALSE)) {
			if (GetLastError() == ERROR_IO_INCOMPLETE)
				return;		// still going out, try again next frame
			CloseDevice();	// unplugged part way through
			return;
		}
		gWritePending = false;
	}

	if (left == gLastLeft && right == gLastRight)
		return;
	gLastLeft = left;
	gLastRight = right;

	memset(gBuffer, 0, sizeof(gBuffer));
	if (gReportLength == REPORT_LENGTH_USB) {
		gBuffer[0] = 0x02;
		gBuffer[1] = FLAG0_COMPATIBLE_VIBRATION | FLAG0_HAPTICS_SELECT;
		gBuffer[3] = right;
		gBuffer[4] = left;
	} else {
		gBuffer[0] = 0x31;
		gBuffer[1] = 0x02;
		gBuffer[2] = FLAG0_COMPATIBLE_VIBRATION | FLAG0_HAPTICS_SELECT;
		gBuffer[4] = right;
		gBuffer[5] = left;

		static const uint8 seed = 0xA2;
		uint32 crc = Crc32(&seed, 1, 0);
		crc = Crc32(gBuffer, REPORT_LENGTH_BT - 4, crc);
		gBuffer[REPORT_LENGTH_BT - 4] = crc & 0xFF;
		gBuffer[REPORT_LENGTH_BT - 3] = (crc >> 8) & 0xFF;
		gBuffer[REPORT_LENGTH_BT - 2] = (crc >> 16) & 0xFF;
		gBuffer[REPORT_LENGTH_BT - 1] = (crc >> 24) & 0xFF;
	}

	ResetEvent(gWrite.hEvent);
	DWORD written;
	gWrites++;
	if (WriteFile(gDevice, gBuffer, gReportLength, &written, &gWrite)) {
		gLastError = 0;
		return;
	}
	gLastError = GetLastError();
	if (gLastError == ERROR_IO_PENDING)
		gWritePending = true;
	else
		CloseDevice();
}

void
CPadRumble::DebugLine(char *out)
{
	sprintf(out, "rumble dev %s len %d writes %d err %u motor %d/%d",
		gDevice == INVALID_HANDLE_VALUE ? "no" : "yes", (int)gReportLength,
		gWrites, gLastError, gLastLeft, gLastRight);
}

void
CPadRumble::Shutdown(void)
{
	if (gDevice != INVALID_HANDLE_VALUE) {
		// leave the motors quiet behind us
		gLastLeft = gLastRight = 1;
		gWritePending = false;
		Update(0, 0);
		if (gWritePending)
			WaitForSingleObject(gWrite.hEvent, 100);
	}
	CloseDevice();
}

#else

void CPadRumble::Update(uint8, uint8) {}
void CPadRumble::Shutdown(void) {}
void CPadRumble::DebugLine(char *out) { out[0] = '\0'; }

#endif
