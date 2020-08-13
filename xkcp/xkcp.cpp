#include "xkcp.h"
#include <winsock2.h>
#include <mmsystem.h>
#pragma comment(lib,"winmm.lib")
#pragma comment(lib,"ws2_32.lib")

void xkcp_init() {
	// initialize 
	WSADATA wsaData;
	WSAStartup(MAKEWORD(1, 1), &wsaData);
	timeBeginPeriod(1);
}
