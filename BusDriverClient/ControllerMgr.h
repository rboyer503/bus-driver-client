#pragma once

#include <boost/thread/mutex.hpp>
extern "C"
{
	#include <hidsdi.h>
}

#define MAX_BUTTONS		128


class ControllerMgr
{
	bool m_initialized;
	BOOL m_bButtonStates[MAX_BUTTONS];
	BOOL m_bPrevButtonStates[MAX_BUTTONS];
	long m_lAxisX;
	long m_lAxisY;
	long m_lAxisZ;
	long m_lAxisRz;
	long m_lHat;
	int m_NumberOfButtons;
	char m_controllerStatus[3];
	char m_internalStatus[3];
	boost::mutex m_mutex;
	void (*m_callback)(int, BOOL);

public:
	ControllerMgr();

	bool IsInitialized() const { return m_initialized; }
	char * GetControllerStatus();

	bool Initialize(HWND hWnd, void (*callback)(int, BOOL));
	void ProcessRawInput(PRAWINPUT pRawInput);

	/*void GetAxisData(long & lAxisX, long & lAxisY, long & lAxisZ, long & lAxisRz)
	{
		lAxisX = m_lAxisX;
		lAxisY = m_lAxisY;
		lAxisZ = m_lAxisZ;
		lAxisRz = m_lAxisRz;
	}*/
};
