#ifndef _SSH_CONNECTION_H_
#define _SSH_CONNECTION_H_
/*
 * --------------------------------------------------------------------------------
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * --------------------------------------------------------------------------------
 */
#include <libssh2.h>
#include <libssh2_sftp.h>

namespace SSH {

class SSH
{
	public:
		SSH();
		~SSH();

		LIBSSH2_SESSION* initialize_session(int socket);
		bool authenticate_session(const char* username, bool byPassword = false, const char* password = nullptr, const char* publicKey = nullptr, const char* privateKey = nullptr);

		int sftp_transfer_file(const std::string& local_path, const std::string& remote_path, int local_open_flags = 0);
		int ensure_remote_dir(const std::string& remote_dir);

	private:
		LIBSSH2_SESSION* session = nullptr;
		static std::string parent_dir(const std::string& path);
};
} // namespace SSH

#endif // _SSH_CONNECTION_H_
