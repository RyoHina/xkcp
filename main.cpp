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
		auto client = s.accept();
		char buffer[1500] = { 0 };
		hr = client->recv(buffer, sizeof(buffer));
		if (hr <= 0) {
			printf("test_server() bad client recv.");
			return;
		}
		printf("test_server() client recv:%s, len=%d.", buffer, hr);

		const char* msg = "this is server relply message.";
		client->send(msg, (int)strlen(msg));
	}
}

void test_client() {
	// 客户端连接
	CXKcpClient c;
	if (0 != c.connect("127.0.0.1", 8888)) {
		printf("connect failed.");
		return;
	}

	c.send("hello", 5);

	char buffer[20] = { 0 };
	int len = c.recv(buffer, 20);
	printf("recv:%s len=%d", buffer, len);
}

int main(int argc, char *argv[])
{
	// initialize
	xkcp_init();

	// 创建Server线程
	std::thread server(test_server);
	
	// 创建Client线程
	std::thread client(test_client);
	client.join();

	server.join();
	return 0;
}
