#include "xkcp-client.h"

// ����һ�� udp��
int CXKcpClient::udp_output(const char *buf, int len, ikcpcb *kcp, void *user)
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

// mode=0 Ĭ��ģʽ��mode=1 ��ͨģʽ mode=2����ģʽ
CXKcpClient::CXKcpClient(int mode) {
	mode_ = mode;
	connect_timeout_ms_ = 5000;
	renew_kcp();
}

CXKcpClient::~CXKcpClient() {
	close();
	if (th_.joinable()) {
		th_.join();
	}
}

//---------------------------------------------------------------------
// sync functions
//---------------------------------------------------------------------
void CXKcpClient::set_connect_timeout(int ms) {
	connect_timeout_ms_ = ms;
}

int CXKcpClient::connect(const char* ip, unsigned short port) {
	sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (INVALID_SOCKET == sock_) {
		return -1;
	}

	// ����Ϊ������ģʽ
	unsigned long ul = 1;
	int ret = ioctlsocket(sock_, FIONBIO, (unsigned long *)&ul);
	if (ret == SOCKET_ERROR) {
		closesocket(sock_);
		return -1;
	}

	// ���÷�������ַ�Ͷ˿�
	server_addr_.sin_family = AF_INET;
	server_addr_.sin_port = ::htons(port);
	server_addr_.sin_addr.S_un.S_addr = ::inet_addr(ip);

	// ������������
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

		// ���յ�������
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
		}

		// ��kcp��ȡ����
		while (true) {
			hr = ikcp_recv(kcp_, buffer, sizeof(buffer));
			if (hr <= 0) break;

			// xkcp_new_conv
			if (hr == 5 && buffer[0] == xkcp_new_conv) {
				conv_ = *(IUINT32*)(buffer + 1);
				renew_kcp();
				// ���·�����������
				char type = xkcp_connect;
				ikcp_send(kcp_, &type, 1);
				break;
			}

			// ���ӽ����ɹ�
			if (hr == 1 && buffer[0] == xkcp_connect) {
				is_connected_ = true;
				break;
			}
		}

		if (is_connected_) {
			break;
		}
	}

	// ok
	if (is_connected_) {
		// start thread
		th_ = std::thread([this] {
			int hr;
			char buffer[1500] = { 0 };
			// ���������~~~
			DWORD dwRecvTimeout = timeGetTime() + recv_timeout_ms_;
			while (is_connected_) {
				Sleep(3);
				DWORD now = timeGetTime();
				// timeout~~
				if (now >= dwRecvTimeout) {
					renew_kcp();
					closesocket(sock_);
					is_connected_ = false;
					return;
				}

				if (ikcp_check(kcp_, now) > now) {
					continue;
				}
				ikcp_update(kcp_, now);

				// ���յ�������
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
					dwRecvTimeout = timeGetTime() + recv_timeout_ms_;
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
		return -555;
	}

	char buffer[1500] = { 0 };
	buffer[0] = xkcp_msg;
	memcpy(buffer + 1, data, len);
	return ikcp_send(kcp_, buffer, len + 1);
}

int CXKcpClient::recv(char* data, int len) {
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

	// disconnected
	return -1;
}

void CXKcpClient::close() {
	if (kcp_) {
		ikcp_release(kcp_);
		kcp_ = nullptr;
	}
	if (sock_ != INVALID_SOCKET) {
		closesocket(sock_);
		sock_ = INVALID_SOCKET;
	}
}
