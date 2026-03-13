#ifndef _SOCKET_CONNECTION_H_
#define _SOCKET_CONNECTION_H_
/*
 * --------------------------------------------------------------------------------
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * --------------------------------------------------------------------------------
 */
#include <arpa/inet.h>
#include <string>

namespace Socket {

class Socket {
	protected:
		int SocketId;
		explicit Socket(int socketFd);
	public:
		inline int fd() const { return SocketId; }
};

class ClientSocket : public Socket
{
	public:
		ClientSocket(const std::string serverIP, int serverPort);
		int connectToServer();
		~ClientSocket();

	private:
		const std::string serverIP;
		int serverPort;
		int clientSocket;
		sockaddr_in serverAdds;

		bool createSocket();
		bool setServerAdds();
		bool connectToServerSocket();
};

} // namespace Socket

#endif // _SOCKET_CONNECTION_H_
