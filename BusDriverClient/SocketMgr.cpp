#include "stdafx.h"
#include "SocketMgr.h"
#include "ControllerMgr.h"
#include <errno.h>
#include <opencv2/opencv.hpp>
#pragma warning( push ) 
#pragma warning( disable : 4244 ) 
#include <boost/archive/binary_iarchive.hpp>
#pragma warning( pop )
#include <boost/iostreams/stream.hpp>
#include "cvmat_serialization.h"

using namespace std;
using namespace cv;
namespace io = boost::iostreams;

#define MAX_BUFFER_SIZE (320 * 240 + 100)


extern ControllerMgr * g_pControllerMgr;


SocketMgr::SocketMgr() :
	m_socketMon(INVALID_SOCKET), m_socketCmd(INVALID_SOCKET), m_initialized(false), m_connected(false)
{
}

SocketMgr::~SocketMgr()
{
	Disconnect();
	WSACleanup();
}

bool SocketMgr::Initialize()
{
	// Start up WinSock.
    WSADATA wsadata;
    if (WSAStartup(0x0202, &wsadata))
		return false;

    // Did we get the right Winsock version?
    if (wsadata.wVersion != 0x0202)
    {
        WSACleanup();
        return false;
    }

	m_initialized = true;
	return true;
}

bool SocketMgr::Connect(const char * ipAddress)
{
	bool ret = true;

	// Create socket for video monitoring stream.
    SOCKADDR_IN target;

    target.sin_family = AF_INET;
    target.sin_port = htons(SM_MONITOR_PORT);
    target.sin_addr.s_addr = inet_addr(ipAddress);

    m_socketMon = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socketMon == INVALID_SOCKET)
        return false;

	// Create socket for command transmission.
	m_socketCmd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socketCmd == INVALID_SOCKET)
        return false;

    // Connect the monitor socket.
    if (connect(m_socketMon, (SOCKADDR *)&target, sizeof(target)) == SOCKET_ERROR)
        return false;

	// Connect the command socket.
	target.sin_port = htons(SM_COMMAND_PORT);
    if (connect(m_socketCmd, (SOCKADDR *)&target, sizeof(target)) == SOCKET_ERROR)
	{
		closesocket(m_socketMon);
        return false;
	}

	// Begin monitoring video stream.
	m_threadMon = boost::thread(&SocketMgr::ManageMonitorStream, this);

	// Begin command stream.
	m_threadCmd = boost::thread(&SocketMgr::ManageCommandStream, this);

	m_connected = true;
	return true;
}

void SocketMgr::Disconnect()
{
	if (m_connected)
	{
		if (m_threadMon.joinable())
		{
			m_threadMon.interrupt();
			m_threadMon.join();
		}

		if (m_threadCmd.joinable())
		{
			m_threadCmd.interrupt();
			m_threadCmd.join();
		}

		closesocket(m_socketMon);
		closesocket(m_socketCmd);
		m_connected = false;
	}
}

void SocketMgr::ManageMonitorStream()
{
	string windowName = "Video Stream from Edison";

	// Enter monitor loop.
	char * pBuffer = new char[MAX_BUFFER_SIZE];
	int size = 0;
	while (1)
	{
		// Get a frame from the server.
		if (!RecvFrame(pBuffer, size))
			break;

		// Deserialize frame into CV matrix.
		Mat frame;
		io::basic_array_source<char> source(pBuffer, size);
		io::stream<io::basic_array_source<char> > sourceStream(source);
		boost::archive::binary_iarchive ia(sourceStream);
		ia >> frame;

		// Display to OpenCV window.
		imshow(windowName, frame);

		// Give main thread an opportunity to shut this thread down.
		try
		{
			boost::this_thread::interruption_point();
		}
		catch (boost::thread_interrupted&)
        {
			break;
        }
	}

	// Clean up.
	delete [] pBuffer;
}

void SocketMgr::ManageCommandStream()
{
	// Enter timed command loop.
	while (1)
	{
		// Send current controller status.
		if (g_pControllerMgr)
			send(m_socketCmd, g_pControllerMgr->GetControllerStatus(), 3, 0);

		// Delay for 20 ms and give main thread an opportunity to shut this thread down.
		try
		{
			boost::this_thread::sleep(boost::posix_time::milliseconds(20));
		}
		catch (boost::thread_interrupted&)
        {
			break;
        }
	}
}

bool SocketMgr::RecvFrame(char * pRawData, int & size)
{
	if (recv(m_socketMon, (char *)(&size), sizeof(size), 0) < 0)
		return false;

	int sizeRemaining = size;
	while (sizeRemaining > 0)
	{
		int bytesRecv = recv(m_socketMon, pRawData, sizeRemaining, 0);
		if (bytesRecv > 0)
		{
			pRawData += bytesRecv;
			sizeRemaining -= bytesRecv;
			continue;
		}
		if (bytesRecv == -1)
		{
			if ( (errno == EAGAIN) || (errno == EWOULDBLOCK) )
				continue;
		}

		// Something bad happened.
		break;
	}

	return (sizeRemaining == 0);
}
