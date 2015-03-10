#pragma once

#include <WinSock2.h>
#include <boost/thread.hpp>

#define SM_MONITOR_PORT 5000
#define SM_COMMAND_PORT 5001


class SocketMgr
{
	SOCKET m_socketMon;
	SOCKET m_socketCmd;
	bool m_initialized;
	bool m_connected;
	boost::thread m_threadMon;
	boost::thread m_threadCmd;

public:
	SocketMgr();
	~SocketMgr();

	bool IsInitialized() const { return m_initialized; }
	bool IsConnected() const { return m_connected; }

	bool Initialize();
	bool Connect(const char * ipAddress);
	void Disconnect();

private:
	void ManageMonitorStream();
	void ManageCommandStream();
	bool RecvFrame(char * pRawData, int & size);
};
