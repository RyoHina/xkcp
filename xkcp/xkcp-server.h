#pragma once
#include "ikcp.h"
#include "xkcp.h"
#include <map>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <functional>
#include <winsock2.h>


//---------------------------------------------------------------------
// CXKcpServer
//---------------------------------------------------------------------
class CXKcpSession;
class CXKcpServer {
public:
	CXKcpServer(int mode = xkcp_mode_fast);
	~CXKcpServer();

	int listen(unsigned short port);

	// sync
	CXKcpSession* accept();

private:
	SOCKET sock_ = INVALID_SOCKET;
	std::thread th_;
	int mode_;

	CXKcpSession * zeroSession_;
	IUINT32 sessionID_ = 1;
	std::map<IUINT32, CXKcpSession*> mapSessions_;

	std::mutex acceptSessionMutex_;
	std::map<CXKcpSession*, int> acceptSession_;
};


//---------------------------------------------------------------------
// XKcpSession
//---------------------------------------------------------------------
class CXKcpSession {
	friend CXKcpServer;
public:
	CXKcpSession(IUINT32 conv, int mode);
	~CXKcpSession();

	int send(const char* data, int len);

	// sync
	int recv(char* data, int len);

private:
	void update();
	void set_socket(SOCKET s);
	void set_client_addr(sockaddr_in* addr);

	int send_direct(const char* data, int len);
	int input_data(char* buffer, int len);

	// 返回0 无需其他处理
	// 返回1 表示建立连接，需要CXKcpServer 返回 accept成功
	// 返回-1 表示出错，断掉连接，释放掉当前对象
	int dispatch(char* buffer, int len);

	bool is_connected_ = false;
	ikcpcb *kcp_ = nullptr;

	SOCKET sock_ = INVALID_SOCKET;
	sockaddr_in client_addr_;

	std::mutex recvDataMutex_;
	std::list<std::string> recvData_;
	void add_recv_data(const std::string&data);

	static int udp_output(const char *buf, int len, ikcpcb *kcp, void *user);
};
