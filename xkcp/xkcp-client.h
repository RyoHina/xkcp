#pragma once
#include "ikcp.h"
#include "xkcp.h"
#include <string>
#include <thread>
#include <functional>
#include <winsock2.h>

class CXKcpClient {
public:
	CXKcpClient(int mode = xkcp_mode_fast);
	~CXKcpClient();

	void set_connect_timeout(int ms);
	void set_heart_beat_timeout(int ms);

	// sync functions
	int connect(const char*ip, unsigned short port);
	int send(const char* data, int len);
	int recv(char* data, int len);
	void close();

	IUINT32 get_connected_conv();
private:
	SOCKET sock_ = INVALID_SOCKET;
	sockaddr_in server_addr_;
	ikcpcb *kcp_ = nullptr;

	IUINT32 conv_ = 0;
	int mode_;
	void renew_kcp();
	int connect_timeout_ms_ = 5000;
	int heart_beat_timeout_ms_ = 30 * 1000;

	bool is_connected_ = false;
	std::thread th_;

	static int udp_output(const char *buf, int len, ikcpcb *kcp, void *user);
};
