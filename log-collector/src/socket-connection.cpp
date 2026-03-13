/*
 * --------------------------------------------------------------------------------
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * --------------------------------------------------------------------------------
 */
#include <unistd.h>

#include "socket-connection.h"
#include "log.h"

namespace Socket {

	Socket::Socket(int Socket) : SocketId(Socket) {}
	ClientSocket::ClientSocket(const std::string serverIP, int serverPort)
		: serverIP(serverIP), serverPort(serverPort), clientSocket(0), Socket(-1) {}

	int ClientSocket::connectToServer()
	{
		if(!createSocket()) {
			LOGE("Error Creating Socket");
			return -1;
		}

		if(!setServerAdds()) {
			LOGE("Error setServerAdds Socket");
			return -1;
		}
		if(!connectToServerSocket()) {
			LOGE("Error connect To ServerSocket");
			return -1;
		}
#ifdef _DEBUG
		LOGI("Connected to Server");
#endif
		return clientSocket;
	}

	ClientSocket::~ClientSocket()

	{
		if (clientSocket > 0){
#ifdef _DEBUG
			LOGI("Client Socket Closed");
#endif
			close(clientSocket);
		}
	}

	bool ClientSocket::createSocket()
	{
		clientSocket = ::socket(AF_INET, SOCK_STREAM, 0);
		Socket::SocketId = clientSocket;
#ifdef _DEBUG
		LOGI("client sock " << clientSocket);
#endif
		return (clientSocket >= 0);
	}

	bool ClientSocket::setServerAdds()
	{
		serverAdds.sin_family = AF_INET;
		serverAdds.sin_port   = htons(serverPort);
		serverAdds.sin_addr.s_addr = inet_addr(serverIP.c_str());
		return true;
	}

	bool ClientSocket::connectToServerSocket()
	{
		return (::connect(clientSocket, (struct sockaddr*)&serverAdds, sizeof(serverAdds)) >= 0);
	}

} // namespace Socket
