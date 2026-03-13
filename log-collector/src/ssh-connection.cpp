/*
 * --------------------------------------------------------------------------------
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * --------------------------------------------------------------------------------
 */

#include <fcntl.h>
#include <cerrno>
#include <vector>

#include "log.h"
#include "ssh-connection.h"

namespace SSH {

SSH::SSH() : session(nullptr) {}
SSH::~SSH() {
	if (session) {
#ifdef _DEBUG
		LOGD("Clear ssh session");
#endif
		libssh2_session_disconnect(session, "Normal Shutdown");
		libssh2_session_free(session);
		session = nullptr;
	}
}

LIBSSH2_SESSION* SSH::initialize_session(int socket_fd)
{
	int ret = libssh2_init(0);
	if (ret) {
		LOGE("libssh2_init");
		return nullptr;
	}

	session = libssh2_session_init();
	if (!session) {
		LOGE("libssh2_session_init");
		return nullptr;
	}

	// Blocking I/O
	libssh2_session_set_blocking(session, 1);
	ret = libssh2_session_handshake(session, socket_fd);
	if (ret) {
		LOGE("libssh2_session_handshake");
		libssh2_session_free(session);
		session = nullptr;
		return nullptr;
	}
	return session;
}

bool SSH::authenticate_session(const char* username,
		bool byPassword,
		const char* password,
		const char* publicKey,
		const char* privateKey) {

	if (byPassword) {
		if (libssh2_userauth_password(session, username, password)) {
			LOGE("Password Authentication Failed");
			return false;
		}
	} else {
		if (libssh2_userauth_publickey_fromfile(session, username, publicKey, privateKey, password)) {
			LOGE("Key Authentication Failed");
			return false;
		}
	}
	return true;
}

std::string SSH::parent_dir(const std::string& path) {
	const std::string::size_type pos = path.find_last_of('/');
	if (pos == std::string::npos) return std::string();
	return path.substr(0, pos + 1);
}

static int sftp_mkdir_p(LIBSSH2_SESSION* session, const std::string& full_dir) {
	if (!session) return -1;

	LIBSSH2_SFTP* sftp = libssh2_sftp_init(session);
	if (!sftp) {
		LOGE("SFTP init failed in mkdir_p");
		return -1;
	}

	// Split components
	std::vector<std::string> parts;
	{
		std::string token;
		for (char c : full_dir) {
			if (c == '/') {
				if (!token.empty()) { parts.push_back(token); token.clear(); }
			} else {
				token.push_back(c);
			}
		}
		if (!token.empty()) parts.push_back(token);
	}

	std::string prefix;
	if (!full_dir.empty() && full_dir[0] == '/') prefix = "/";

	for (size_t i = 0; i < parts.size(); ++i) {
		if (!prefix.empty() && prefix.back() != '/') prefix.push_back('/');
		prefix += parts[i];
		LIBSSH2_SFTP_ATTRIBUTES attrs{};

		int rc = libssh2_sftp_stat_ex(sftp, prefix.c_str(),
				static_cast<unsigned int>(prefix.size()),
				LIBSSH2_SFTP_STAT, &attrs);

		if (rc == 0) {
			continue;
		}

		rc = libssh2_sftp_mkdir(sftp, prefix.c_str(), 0755);

		if (rc != 0) {
			LIBSSH2_SFTP_ATTRIBUTES attrs2{};
			int rc2 = libssh2_sftp_stat_ex(sftp, prefix.c_str(),
					static_cast<unsigned int>(prefix.size()),
					LIBSSH2_SFTP_STAT, &attrs2);
			if (rc2 != 0) {
				LOGE(std::string("Failed to create remote directory: ") + prefix.c_str());
				libssh2_sftp_shutdown(sftp);
				return -1;
			}
		}
	}

	libssh2_sftp_shutdown(sftp);
	return 0;
}

int SSH::ensure_remote_dir(const std::string& dir_path)
{
	if (!session) return -1;

	if (dir_path.empty()) return 0;
	return sftp_mkdir_p(session, dir_path);
}

int SSH::sftp_transfer_file(const std::string& local_path,
		const std::string& remote_path,
		int local_open_flags) {

	if (!session) return -1;

	// Ensure parent directory exists on remote
	const std::string remoteDir = parent_dir(remote_path);
	if (!remoteDir.empty()) {
		if (ensure_remote_dir(remoteDir) != 0) {
			LOGE(std::string("Could not ensure remote dir: ") + remoteDir.c_str());
			return -1;
		}
	}

	// Open local file
	const int fd = ::open(local_path.c_str(), O_RDONLY | local_open_flags);
	if (fd < 0) {
		LOGE(std::string("Failed to open local file: ") + local_path.c_str() + " : " + std::strerror(errno));
		return -1;
	}

	// Start SFTP and open remote file
	LIBSSH2_SFTP* sftp = libssh2_sftp_init(session);
	if (!sftp) {
		LOGE("SFTP init failed");
		::close(fd);
		return -1;
	}

	LIBSSH2_SFTP_HANDLE* fh = libssh2_sftp_open_ex(
			sftp,
			remote_path.c_str(),
			static_cast<unsigned int>(remote_path.size()),
			LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
			0644,
			LIBSSH2_SFTP_OPENFILE
			);
	if (!fh) {
		LOGE(std::string("SFTP open failed for: ") + remote_path.c_str());
		libssh2_sftp_shutdown(sftp);
		::close(fd);
		return -1;
	}

	// Stream copy
	char buf[64 * 1024];
	long long total = 0;

	for (;;) {
		ssize_t r = ::read(fd, buf, sizeof(buf));
		if (r > 0) {
			const char* p = buf;
			ssize_t left = r;
			while (left > 0) {
				ssize_t w = libssh2_sftp_write(fh, p, static_cast<size_t>(left));
				if (w < 0) {
					LOGE(std::string("SFTP write failed for: ") + remote_path.c_str());
					libssh2_sftp_close(fh);
					libssh2_sftp_shutdown(sftp);
					::close(fd);
					return -1;
				}
				p += w;
				left -= w;
				total += w;
			}
			continue;
		} else if (r == 0) {
			break;
		} else {
			if (errno == EINTR) continue;
			if ((local_open_flags & O_NONBLOCK) && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				break;
			}
			LOGE(std::string("Read error while streaming local file: ") + local_path.c_str()
					+ " : " + std::strerror(errno));
			libssh2_sftp_close(fh);
			libssh2_sftp_shutdown(sftp);
			::close(fd);
			return -1;
		}
	}

	libssh2_sftp_close(fh);
	libssh2_sftp_shutdown(sftp);
	::close(fd);

	LOGI(std::string("Transferred: ") + local_path.c_str() + " to " + remote_path.c_str()
			+ " (" + std::to_string(total) + " bytes)");
	return 0;
}

} // namespace SSH
