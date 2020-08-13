#pragma once

enum eXkcpMode {
	xkcp_mode_default = 0,
	xkcp_mode_normal,
	xkcp_mode_fast,
};

enum eXkcpEventType {
	xkcp_connect = 0,		// ������
	xkcp_new_conv,			// �����������·���conv
	xkcp_disconnect,		// �Ͽ�����
	xkcp_heart_beat,		// ����
	xkcp_msg,				// ����
};

void xkcp_init();
