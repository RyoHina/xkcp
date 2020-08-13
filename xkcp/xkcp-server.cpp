#include "xkcp-server.h"

// ����һ�� udp��
int XKcpSession::udp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
	XKcpSession *session = (XKcpSession *)user;
	sendto(session->sock_,
		(char*)buf,
		len,
		0,
		(sockaddr*)&(session->client_addr_),
		sizeof(sockaddr_in));
	return 0;
}

XKcpSession::XKcpSession(IUINT32 conv, int mode) {
	if (kcp_) {
		ikcp_release(kcp_);
	}
	kcp_ = ikcp_create(conv, (void*)this);
	kcp_->output = &XKcpSession::udp_output;
	ikcp_wndsize(kcp_, 128, 128);

	// �жϲ���������ģʽ
	if (mode == xkcp_mode_default) {
		// Ĭ��ģʽ
		ikcp_nodelay(kcp_, 0, 10, 0, 0);
	}
	else if (mode == xkcp_mode_normal) {
		// ��ͨģʽ���ر����ص�
		ikcp_nodelay(kcp_, 0, 10, 0, 1);
	}
	else {
		// ��������ģʽ
		// �ڶ������� nodelay-�����Ժ����ɳ�����ٽ�����
		// ���������� intervalΪ�ڲ�����ʱ�ӣ�Ĭ������Ϊ 10ms
		// ���ĸ����� resendΪ�����ش�ָ�꣬����Ϊ2
		// ��������� Ϊ�Ƿ���ó������أ������ֹ
		ikcp_nodelay(kcp_, 2, 10, 2, 1);
		kcp_->rx_minrto = 10;
		kcp_->fastresend = 1;
	}
}

XKcpSession::~XKcpSession() {
	if (kcp_) {
		ikcp_release(kcp_);
	}
}

void XKcpSession::update() {
	if (kcp_ && is_connected_) {
		auto now = timeGetTime();
		if (ikcp_check(kcp_, now) > now) {
			return;
		}
		ikcp_update(kcp_, now);
	}
}


int XKcpSession::send(const char* data, int len) {
	if (!is_connected_) {
		return -1;
	}

	char buffer[1500] = { 0 };
	buffer[0] = xkcp_msg;
	memcpy(buffer + 1, data, len);
	return send_direct(buffer, len + 1);
}

int XKcpSession::send_direct(const char* data, int len) {
	if (!is_connected_) {
		return -1;
	}
	return ikcp_send(kcp_, data, len);
}


int XKcpSession::recv(char* data, int len) {
	while (is_connected_) {
		Sleep(1);
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
			return hr - 1;
		}
		assert(false);
		return -1;
	}

	// timeout
	return -1;
}

int XKcpSession::input_data(char* buffer, int len) {
	if (kcp_) {
		ikcp_input(kcp_, buffer, len);

		char buffer[1500] = { 0 };
		int hr = ikcp_recv(kcp_, buffer, sizeof(buffer));
		if (hr > 0) {
			return dispatch(buffer, hr);
		}
	}
	return 0;
}


// ����0 ������������
// ����1 ��ʾ�������ӣ���ҪCXKcpServer ���� accept�ɹ�
// ����-1 ��ʾ�����ͷŵ���ǰ����
int XKcpSession::dispatch(char* buffer, int len) {
	unsigned char type = buffer[0];

	// ������
	if (type == xkcp_connect) {
		assert(len == 1);
		// ���ݴ����ˣ��ѽ������Ӳ�Ӧ���յ�xkcp_connect
		if (is_connected_) {
			is_connected_ = false;
			return -1;
		}

		char conn = xkcp_connect;
		send_direct(&conn, 1);
		is_connected_ = true;
		return 1;
	}

	// �Ͽ�����
	if (type == xkcp_disconnect) {
		assert(len == 1);
		is_connected_ = false;
		return -1;
	}

	// ����
	if (type == xkcp_heart_beat) {
		assert(len == 1);
		if (!is_connected_) {
			return -1;
		}
		unsigned char conn = xkcp_heart_beat;
		send_direct((const char*)&conn, 1);
		return 0;
	}

	// ����
	if (type == xkcp_msg) {
		assert(len > 1);
		if (!is_connected_) {
			return -1;
		}
		ikcp_input(kcp_, buffer + 1, len - 1);
		return 0;
	}

	assert(false);
	is_connected_ = false;
	return -1;
}

void XKcpSession::set_socket(SOCKET s) {
	sock_ = s;
}

void XKcpSession::set_client_addr(sockaddr_in* addr) {
	memcpy(&client_addr_, addr, sizeof(sockaddr_in));
}

CXKcpServer::CXKcpServer(int mode) {
	mode_ = mode;
	zeroSession_ = new XKcpSession(0, mode_);
	zeroSession_->is_connected_ = true;
}

CXKcpServer::~CXKcpServer() {
	delete zeroSession_;
	if (sock_ != INVALID_SOCKET) {
		closesocket(sock_);
	}

	if (th_.joinable()) {
		th_.join();
	}
}

int CXKcpServer::listen(unsigned short port) {
	sock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (INVALID_SOCKET == sock_) {
		return -1;
	}

	//�󶨵�ַ��Ϣ
	struct sockaddr_in cli_addr = { 0 };
	cli_addr.sin_family = AF_INET;
	cli_addr.sin_port = htons(port);
	cli_addr.sin_addr.s_addr = 0;
	if (bind(sock_, (struct sockaddr*)&cli_addr, sizeof(cli_addr)) < 0)
	{
		closesocket(sock_);
		sock_ = INVALID_SOCKET;
		return -1;
	}

	// ����Ϊ������ģʽ
	unsigned long ul = 1;
	int ret = ioctlsocket(sock_, FIONBIO, (unsigned long *)&ul);
	if (ret == SOCKET_ERROR) {
		closesocket(sock_);
		return -1;
	}

	// �����߳�, ���������շ�
	th_ = std::thread([this] {
		while (true) {
			Sleep(1);

			zeroSession_->update();
			for (auto& item : mapSessions_) {
				item.second->update();
			}

			char buffer[1500] = {};
			sockaddr_in servAddr = {};
			int iFromLen = sizeof(sockaddr_in);

			int iRet = ::recvfrom(sock_,
				buffer,
				sizeof(buffer),
				0,
				(sockaddr*)&servAddr,
				&iFromLen);
			// IKCP_OVERHEAD == 24 bytes
			if (iRet < 24) {
				continue;
			}

			IUINT32 conv = ikcp_getconv(buffer);
			// ��Ҫ��������conv
			if (conv == 0) {
				zeroSession_->set_socket(sock_);
				zeroSession_->set_client_addr(&servAddr);
				char newConv[5] = { 0 };
				newConv[0] = xkcp_new_conv;
				memcpy(newConv + 1, &sessionID_, 4);
				sessionID_++;
				zeroSession_->send_direct(newConv, 5);
				continue;
			}

			if (mapSessions_.find(conv) == mapSessions_.end()) {
				mapSessions_[conv] = new XKcpSession(conv, mode_);
			}

			// ����socket��addr
			mapSessions_[conv]->set_socket(sock_);
			mapSessions_[conv]->set_client_addr(&servAddr);
			int hr = mapSessions_[conv]->input_data(buffer, iRet);

			// �½�������
			if (hr == 1) {
				std::lock_guard<std::mutex> lc(acceptSessionMutex_);
				acceptSession_[mapSessions_[conv]] = 0;
			}

			// �����ͷ�����
			if (hr == -1) {
				auto ptr = mapSessions_[conv];
				delete mapSessions_[conv];
				mapSessions_.erase(conv);

				// ����δaccept
				std::lock_guard<std::mutex> lc(acceptSessionMutex_);
				acceptSession_.erase(ptr);
			}
		}
	});

	return 0;
}

XKcpSession* CXKcpServer::accept() {
	while (true) {
		Sleep(100);
		std::lock_guard<std::mutex> lc(acceptSessionMutex_);
		if (acceptSession_.empty())
			continue;
		for (auto item : acceptSession_) {
			acceptSession_.erase(item.first);
			return item.first;
		}
	}
}
