#include "xkcp-client.h"

// 发送一个 udp 包
/* static */ int CXKcpClient::udp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
	CXKcpClient *client = (CXKcpClient *)user;
	sendto(client->sock_,
		(char*)buf,
		len,
		0,
		(sockaddr*)&(client->server_addr_),
		sizeof(sockaddr_in));
	return 0;
}

void CXKcpClient::renew_kcp() {
	int mode = mode_;
	if (kcp_) {
		ikcp_release(kcp_);
	}
	kcp_ = ikcp_create(conv_, (void*)this);
	kcp_->output = &CXKcpClient::udp_output;
	ikcp_wndsize(kcp_, 128, 128);

	// 判断测试用例的模式
	if (mode == xkcp_mode_default) {
		// 默认模式
		ikcp_nodelay(kcp_, 0, 10, 0, 0);
	}
	else if (mode == xkcp_mode_normal) {
		// 普通模式，关闭流控等
		ikcp_nodelay(kcp_, 0, 10, 0, 1);
	}
	else {
		// 启动快速模式
		// 第二个参数 nodelay-启用以后若干常规加速将启动
		// 第三个参数 interval为内部处理时钟，默认设置为 10ms
		// 第四个参数 resend为快速重传指标，设置为2
		// 第五个参数 为是否禁用常规流控，这里禁止
		ikcp_nodelay(kcp_, 2, 10, 2, 1);
		kcp_->rx_minrto = 10;
		kcp_->fastresend = 1;
	}
}

// mode=0 默认模式，mode=1 普通模式 mode=2快速模式
CXKcpClient::CXKcpClient(int mode) {
	mode_ = mode;
	connect_timeout_ms_ = 5000;
	renew_kcp();
}

CXKcpClient::~CXKcpClient() {
	if (kcp_ && is_connected_) {
		char disconnect = xkcp_disconnect;
		ikcp_send(kcp_, &disconnect, 1);
		Sleep(15);
	}
	close();
}

void CXKcpClient::set_connect_timeout(int ms) {
	connect_timeout_ms_ = ms;
}

void CXKcpClient::set_heart_beat_timeout(int ms) {
	heart_beat_timeout_ms_ = ms;
}


//---------------------------------------------------------------------
// sync functions
//---------------------------------------------------------------------
int CXKcpClient::connect(const char* ip, unsigned short port) {
	sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (INVALID_SOCKET == sock_) {
		return -1;
	}

	// 设置为非阻塞模式
	unsigned long ul = 1;
	int ret = ioctlsocket(sock_, FIONBIO, (unsigned long *)&ul);
	if (ret == SOCKET_ERROR) {
		closesocket(sock_);
		return -1;
	}

	// 设置服务器地址和端口
	server_addr_.sin_family = AF_INET;
	server_addr_.sin_port = ::htons(port);
	server_addr_.sin_addr.S_un.S_addr = ::inet_addr(ip);

	// 发送连接数据
	char type = xkcp_connect;
	ikcp_send(kcp_, &type, 1);

	int hr;
	char buffer[1500] = { 0 };
	DWORD dwConnectTimeout = timeGetTime() + connect_timeout_ms_;
	while (true) {
		Sleep(3);
		DWORD now = timeGetTime();
		// connect timeout~~
		if (now >= dwConnectTimeout) {
			renew_kcp();
			closesocket(sock_);
			return -1;
		}

		if (ikcp_check(kcp_, now) > now) {
			continue;
		}
		ikcp_update(kcp_, now);

		// 接收到新数据
		while (true) {
			int iFromLen = sizeof(sockaddr_in);
			sockaddr_in servAddr = {};
			hr = recvfrom(sock_,
				buffer,
				sizeof(buffer),
				0,
				(sockaddr*)&servAddr,
				&iFromLen);
			if (hr <= 0) break;

			if (hr == 5 && buffer[0] == xkcp_new_conv) {
				conv_ = *(IUINT32*)(buffer + 1);
				renew_kcp();
				// 重新发送连接数据
				char type = xkcp_connect;
				ikcp_send(kcp_, &type, 1);
				break;
			}
			ikcp_input(kcp_, buffer, hr);
		}

		// 从kcp获取数据
		while (true) {
			hr = ikcp_recv(kcp_, buffer, sizeof(buffer));
			if (hr <= 0) break;

			// 连接建立成功
			if (hr == 1 && buffer[0] == xkcp_connect) {
				is_connected_ = true;
				break;
			}
		}

		if (is_connected_) {
			break;
		}
	}

	// connect ok
	if (is_connected_) {
		// start thread
		th_ = std::thread([this] {
			int hr;
			char buffer[1500] = { 0 };
			// 心跳包检测~~~
			DWORD dwRecvTimeout = timeGetTime() + heart_beat_timeout_ms_;
			while (is_connected_) {
				Sleep(3);
				DWORD now = timeGetTime();
				// timeout~~
				if (now >= dwRecvTimeout) {
					printf("client timeout!\r\n");
					renew_kcp();
					closesocket(sock_);
					is_connected_ = false;
					return;
				}

				if (ikcp_check(kcp_, now) > now) {
					continue;
				}
				ikcp_update(kcp_, now);

				// 接收到新数据
				while (true) {
					int iFromLen = sizeof(sockaddr_in);
					sockaddr_in servAddr = {};
					hr = recvfrom(sock_,
						buffer,
						sizeof(buffer),
						0,
						(sockaddr*)&servAddr,
						&iFromLen);
					if (hr <= 0) break;
					ikcp_input(kcp_, buffer, hr);
					dwRecvTimeout = timeGetTime() + heart_beat_timeout_ms_;
				}
			}
		});
		return 0;
	}

	// timeout
	return -1;
}

int CXKcpClient::send(const char* data, int len) {
	if (!is_connected_) {
		return -1;
	}

	char buffer[1500] = { 0 };
	buffer[0] = xkcp_msg;
	memcpy(buffer + 1, data, len);
	return ikcp_send(kcp_, buffer, len + 1);
}

int CXKcpClient::recv(char* data, int len) {
	while (is_connected_) {
		Sleep(5);
		int hr = ikcp_recv(kcp_, data, len);
		if (hr == -3) {
			return -3; // buffer too small
		}
		if (hr <= 0) continue;

		if (data[0] == xkcp_disconnect) {
			assert(len == 1);
			is_connected_ = false;
			return -1;
		}

		if (data[0] == xkcp_heart_beat) {
			assert(len == 1);
			continue;
		}

		if (data[0] == xkcp_msg) {
			assert(len > 1);
			memcpy(data, data + 1, hr - 1);
			data[hr - 1] = '\0';
			return hr - 1;
		}
		assert(false);
		return -1;
	}

	// disconnected
	return -1;
}

void CXKcpClient::close() {
	is_connected_ = false;
	if (th_.joinable()) {
		th_.join();
	}
	if (kcp_) {
		ikcp_release(kcp_);
		kcp_ = nullptr;
	}
	if (sock_ != INVALID_SOCKET) {
		closesocket(sock_);
		sock_ = INVALID_SOCKET;
	}
}

IUINT32 CXKcpClient::get_connected_conv() {
	return conv_;
}