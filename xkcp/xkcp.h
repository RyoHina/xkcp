#pragma once

enum eXkcpMode {
	xkcp_mode_default = 0,
	xkcp_mode_normal,
	xkcp_mode_fast,
};

enum eXkcpEventType {
	xkcp_connect = 0,		// 新连接
	xkcp_new_conv,			// 服务器端重新分配conv
	xkcp_disconnect,		// 断开连接
	xkcp_heart_beat,		// 心跳
	xkcp_msg,				// 数据
};

void xkcp_init();
