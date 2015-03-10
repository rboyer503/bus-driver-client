#include "stdafx.h"
#include "ControllerMgr.h"

#define CHECK(exp)		{ if (!(exp)) goto Error; }
#define SAFE_FREE(p)	{ if(p) { HeapFree(hHeap, 0, p); (p) = NULL; } }


ControllerMgr::ControllerMgr() :
	m_initialized(false),
	m_lAxisX(0), m_lAxisY(0), m_lAxisZ(0), m_lAxisRz(0), m_lHat(0), m_NumberOfButtons(0)
{
	ZeroMemory(m_bButtonStates, sizeof(m_bButtonStates));
	ZeroMemory(m_bPrevButtonStates, sizeof(m_bPrevButtonStates));
	ZeroMemory(m_controllerStatus, sizeof(m_controllerStatus));
	ZeroMemory(m_internalStatus, sizeof(m_internalStatus));
}

char * ControllerMgr::GetControllerStatus()
{
	// Transfer internally managed status into external copy, clear internal button status, and return it.
	boost::mutex::scoped_lock lock(m_mutex);
	for (int i = 0; i < 3; ++i)
		m_controllerStatus[i] = m_internalStatus[i];
	m_internalStatus[0] = 0;

	return m_controllerStatus;
}

bool ControllerMgr::Initialize(HWND hWnd, void (*callback)(int, BOOL))
{
	// Register for joystick devices.
	RAWINPUTDEVICE rid;
	rid.usUsagePage = 1;
	rid.usUsage = 4;
	rid.dwFlags = 0;
	rid.hwndTarget = hWnd;

	if (!RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE)))
		return false;

	m_callback = callback;

	m_initialized = true;
	return true;
}

void ControllerMgr::ProcessRawInput(PRAWINPUT pRawInput)
{
	PHIDP_PREPARSED_DATA pPreparsedData;
	HIDP_CAPS            Caps;
	PHIDP_BUTTON_CAPS    pButtonCaps;
	PHIDP_VALUE_CAPS     pValueCaps;
	USHORT               capsLength;
	UINT                 bufferSize;
	HANDLE               hHeap;
	USAGE                usage[MAX_BUTTONS];
	ULONG                i, usageLength, value;

	pPreparsedData = NULL;
	pButtonCaps    = NULL;
	pValueCaps     = NULL;
	hHeap          = GetProcessHeap();

	//
	// Get the preparsed data block
	//

	CHECK( GetRawInputDeviceInfo(pRawInput->header.hDevice, RIDI_PREPARSEDDATA, NULL, &bufferSize) == 0 );
	CHECK( pPreparsedData = (PHIDP_PREPARSED_DATA)HeapAlloc(hHeap, 0, bufferSize) );
	CHECK( (int)GetRawInputDeviceInfo(pRawInput->header.hDevice, RIDI_PREPARSEDDATA, pPreparsedData, &bufferSize) >= 0 );

	//
	// Get the joystick's capabilities
	//

	// Button caps
	CHECK( HidP_GetCaps(pPreparsedData, &Caps) == HIDP_STATUS_SUCCESS )
	CHECK( pButtonCaps = (PHIDP_BUTTON_CAPS)HeapAlloc(hHeap, 0, sizeof(HIDP_BUTTON_CAPS) * Caps.NumberInputButtonCaps) );

	capsLength = Caps.NumberInputButtonCaps;
	CHECK( HidP_GetButtonCaps(HidP_Input, pButtonCaps, &capsLength, pPreparsedData) == HIDP_STATUS_SUCCESS )
	m_NumberOfButtons = pButtonCaps->Range.UsageMax - pButtonCaps->Range.UsageMin + 1;

	// Value caps
	CHECK( pValueCaps = (PHIDP_VALUE_CAPS)HeapAlloc(hHeap, 0, sizeof(HIDP_VALUE_CAPS) * Caps.NumberInputValueCaps) );
	capsLength = Caps.NumberInputValueCaps;
	CHECK( HidP_GetValueCaps(HidP_Input, pValueCaps, &capsLength, pPreparsedData) == HIDP_STATUS_SUCCESS )

	//
	// Get the pressed buttons
	//

	usageLength = m_NumberOfButtons;
	CHECK(
		HidP_GetUsages(
			HidP_Input, pButtonCaps->UsagePage, 0, usage, &usageLength, pPreparsedData,
			(PCHAR)pRawInput->data.hid.bRawData, pRawInput->data.hid.dwSizeHid
		) == HIDP_STATUS_SUCCESS );

	ZeroMemory(m_bButtonStates, sizeof(m_bButtonStates));

	for(i = 0; i < usageLength; i++)
		m_bButtonStates[usage[i] - pButtonCaps->Range.UsageMin] = TRUE;

	//
	// Get the state of discrete-valued-controls
	//

	for(i = 0; i < Caps.NumberInputValueCaps; i++)
	{
		CHECK(
			HidP_GetUsageValue(
				HidP_Input, pValueCaps[i].UsagePage, 0, pValueCaps[i].Range.UsageMin, &value, pPreparsedData,
				(PCHAR)pRawInput->data.hid.bRawData, pRawInput->data.hid.dwSizeHid
			) == HIDP_STATUS_SUCCESS );

		switch(pValueCaps[i].Range.UsageMin)
		{
		case 0x30:	// X-axis
			m_lAxisX = (long)value - 128;
			break;

		case 0x31:	// Y-axis
			m_lAxisY = (long)value - 128;
			break;

		case 0x32: // Z-axis
			m_lAxisZ = (long)value - 128;
			break;

		case 0x35: // Rotate-Z
			m_lAxisRz = (long)value - 128;
			break;

		case 0x39:	// Hat Switch
			m_lHat = (long)value;
			break;
		}
	}

	// Update internal controller status.
	int buttonIndexUpdated = -1;
	{
		boost::mutex::scoped_lock lock(m_mutex);
		for (int index = 0; index < m_NumberOfButtons; index++)
		{
			if (m_bButtonStates[index] != m_bPrevButtonStates[index])
			{
				// NOTE: Controller status and debug callback only handling one button press at a time.
				// Ready for next button press after command update is send - ~20ms max.
				// This is adequate for our application.
				if (m_bButtonStates[index])
					m_internalStatus[0] = static_cast<char>(index + 1);
				m_bPrevButtonStates[index] = m_bButtonStates[index];
				buttonIndexUpdated = index;
			}
		}

		m_internalStatus[1] = static_cast<char>(m_lAxisY);
		m_internalStatus[2] = static_cast<char>(m_lAxisZ);
	}

	//  Notify registered callback of button press changes.
	if (buttonIndexUpdated >= 0)
		m_callback(buttonIndexUpdated, m_bButtonStates[buttonIndexUpdated]);

	//
	// Clean up
	//

Error:
	SAFE_FREE(pPreparsedData);
	SAFE_FREE(pButtonCaps);
	SAFE_FREE(pValueCaps);
}
