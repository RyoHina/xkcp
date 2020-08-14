// main.cpp
//

#include <stdio.h>
#include "xkcp-client.h"
#include "xkcp-server.h"

void test_server() {
	int hr;
	CXKcpServer s;
	s.listen(8888);
	while (true) {
		CXKcpSession* client = s.accept();
		printf("CXKcpServer accept new socket.\r\n");
		char buffer[1500] = { 0 };
		hr = client->recv(buffer, sizeof(buffer));
		if (hr <= 0) {
			printf("test_server() bad client recv.");
			return;
		}
		printf("CXKcpServer recv:'%s', len=%d.\r\n", buffer, hr);

		const char* msg = "This is a server relply message!";
		printf("CXKcpServer send:'%s', len=%d.\r\n", msg, (int)strlen(msg));
		client->send(msg, (int)strlen(msg));
	}
}

void test_client() {
	// 客户端连接
	CXKcpClient c;
	if (0 != c.connect("127.0.0.1", 8888)) {
		printf("connect failed.\r\n");
		return;
	}
	else {
		printf("CXKcpClient connect OK.\r\n");
	}

	printf("CXKcpClient send 'hello', len=5.\r\n");
	c.send("hello", 5);

	char buffer[1500] = { 0 };
	int len = c.recv(buffer, sizeof(buffer));
	printf("CXKcpClient recv:'%s', len=%d.\r\n", buffer, len);
}

int main(int argc, char *argv[])
{
	// initialize
	xkcp_init();

	// 创建Server线程
	std::thread server(test_server);
	
	// 创建Client线程
	while (true) {
		printf("****** client start *****\r\n");
		std::thread client(test_client);
		client.join();
		printf("****** client end Sleep 3s. *****\r\n\r\n");
		Sleep(3000);
	}

	server.join();
	return 0;
}
